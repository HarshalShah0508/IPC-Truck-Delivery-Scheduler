# IPC-Based Truck Delivery Scheduler

[![C](https://img.shields.io/badge/C-POSIX-00599C?style=flat-square&logo=c&logoColor=white)](https://en.wikipedia.org/wiki/C_(programming_language))
[![pthreads](https://img.shields.io/badge/Concurrency-pthreads-informational?style=flat-square)](https://en.wikipedia.org/wiki/POSIX_Threads)
[![System V IPC](https://img.shields.io/badge/IPC-System%20V-informational?style=flat-square)](https://en.wikipedia.org/wiki/Unix_System_V)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg?style=flat-square)](LICENSE)

A POSIX C client that controls a fleet of delivery trucks on a grid, communicating with a course-provided simulator over System V shared memory and message queues, for the Operating Systems (CS F372) Assignment 2 at BITS Pilani.

## Problem Statement

Given an `N x N` grid and `D` trucks, packages arrive over time at random cells and must be delivered to specified destination cells before an expiry turn. Each turn, a truck can move one step (up/down/left/right/stay) and pick up or drop off one package. The scheduling goal is to minimize:

1. The total number of turns needed to deliver every package.
2. The number of packages delivered after their expiry turn.

**The catch:** to move a truck that is currently carrying packages, the controller must first obtain a correct *authorization string* (a sequence of `u`/`d`/`l`/`r` characters, one per package on board) by querying one of `S` independent solver processes over a dedicated message queue and brute-forcing guesses until the solver confirms a match. Cells may also be toll booths, which freeze a truck that lands on them for a fixed number of turns.

The simulator (helper process) and solver processes are provided by the course staff and are not part of this submission — only `solution.c` was authored for this assignment.

## Features

- Turn-based control loop synchronized with the helper process via System V message queues.
- Greedy truck-to-package assignment based on a scored cost function (travel distance plus an expiry-urgency penalty).
- Per-truck state machine (idle → en route to pickup → en route to dropoff) driving movement and pickup/dropoff commands each turn.
- Multithreaded, parallel brute-force search for authorization strings, using a mutex/condition-variable-guarded pool to share a limited number of solver processes (`S <= D`) across trucks needing to move.
- Automatic handling of toll-booth waits (skips movement instructions for trucks reported as stuck in a toll).
- Python-based random test case generator for local testing.

## Technologies Used

- **C** (POSIX-compliant, compiled with `gcc ... -lpthread`)
- **pthreads** — for parallel solver queries and thread pool synchronization (mutex + condition variable)
- **System V IPC** — shared memory (`shmget`/`shmat`) and message queues (`msgget`/`msgsnd`/`msgrcv`) for communication with the helper and solver processes
- **Python 3** — test case generation (`testcase_gen.py`, using the standard `random` and `collections` modules)

## Project Structure

```
.
├── helper.c                       # Provided simulator/grader process (not authored for this submission)
├── helper.h                       # Shared struct/constant definitions (provided)
├── solution.c                     # Student solution: truck scheduling & IPC client
├── testcase_gen.py                # Generates random testcase input files
├── LICENSE
└── docs/
    ├── assignment_spec.pdf        # Original assignment specification (provided)
    ├── INSTRUCTIONS.md            # Original build/run instructions (provided)
    └── sample_testcases.txt       # Sample generated test case configurations (grid size, trucks, solvers, IPC keys)
```

## How It Works

### Communication Protocol

- On every turn, the helper process sends a `TurnChangeResponse` (`mtype = 2`) over the main message queue, reporting the current turn number, any new package requests, and whether the simulation has finished or errored.
- The client reads/writes truck commands (movement, pickup, dropoff, authorization strings) into a shared `MainSharedMemory` segment.
- Once commands are set, the client sends a `TurnReadyRequest` (`mtype = 1`) to signal the helper to process the turn.
- To move a loaded truck, the client selects a free solver from a shared pool, sets its target truck (`mtype = 2` on the solver's queue), then sends authorization string guesses (`mtype = 3`) until the solver replies `guessIsCorrect = 1` (`mtype = 4`).

### Scheduling Logic (`f()` in `solution.c`)

1. New package requests reported for the turn are added to an internal package database.
2. For every idle truck and every unclaimed, un-expired-status package, a candidate job is scored as:
   - `score = (pickup distance + delivery distance) + expiry penalty`
   - A large fixed penalty is added if the package would already be expired on arrival; a smaller, sliding penalty is added if the delivery is within an "urgency window" of its expiry turn.
3. Candidate jobs are sorted by score (insertion sort) and assigned greedily: the lowest-score, still-available (truck, package) pairs are matched first.
4. Each truck's state (idle / heading to pickup / heading to dropoff) determines its Manhattan-direction move for the turn, and triggers a pickup or dropoff command once the truck reaches the target cell.
5. Trucks that are not empty and intend to move are handed off to a worker thread that queries a solver and performs a depth-first brute-force search (`ispossible`) over all `4^L` combinations of `u/d/l/r` (where `L` is the number of packages onboard) until the correct authorization string is found.

### Test Case Generation

`testcase_gen.py` produces a `testcaseN.txt` file containing:
- Grid size (`N`), number of trucks (`D`), solvers (`S`), max arrival turn (`T`), toll booths (`B`), and total package count.
- Randomly generated package requests (pickup/dropoff coordinates, arrival turn, expiry turn), capped at a configurable maximum number of arrivals per turn.
- Randomly placed toll booth cells with random wait costs, up to a configurable maximum.

Sample generated configurations (grid size, truck/solver counts, IPC keys) for four test cases are included in `docs/sample_testcases.txt`.

## Installation

Requires `gcc` on a POSIX-compliant system (Ubuntu ≥ 22.04 recommended).

```bash
gcc solution.c -lpthread -o solution
gcc helper.c -lpthread -o helper
```

## Usage

1. Generate a test case (parameters are edited directly in `testcase_gen.py`: `N`, `D`, `S`, `T`, `B`, `max_new_requests_per_turn`, `number_of_requests`, `max_booth_cost`, `test_case_number`):

   ```bash
   python testcase_gen.py
   ```

2. Ensure `helper.c`, `helper.h`, `solution.c`, and the generated test case file are in the same directory, then run:

   ```bash
   ./helper <TESTCASE_NUMBER>
   ```

   where `<TESTCASE_NUMBER>` matches the `test_case_number` used when generating the test case. The helper process launches and drives `solution` and the solver processes automatically.

## Results / Output

The helper process reports, per test case, the number of turns taken and the total number of expired packages upon successful completion — the two metrics the solution is scored against.

## Constraints (per assignment spec)

- Grid size `N <= 500`, trucks `D <= 250`, solvers `S <= D`, max arrival turn `T <= 500`, toll booths `B <= 500`.
- At most 50 new package requests may arrive in a single turn.
- A truck can carry at most 20 packages at once.
- Authorization strings consist only of lowercase `u`, `d`, `l`, `r`.

## Limitations

- Truck-to-package assignment is purely greedy (nearest/most-urgent first via a single-pass scored match), not a globally optimal assignment.
- Job scoring and sorting use an `O(n^2)` insertion sort over all (truck, package) candidate pairs each turn.
- Authorization string discovery is an exhaustive depth-first search with worst-case `4^L` guesses per truck (`L` up to 20), parallelized across the available solver pool but not otherwise pruned or optimized.

## License

MIT for the authored submission (`solution.c`, `testcase_gen.py`) — see [LICENSE](LICENSE). Course-provided files (`helper.c`, `helper.h`, `docs/assignment_spec.pdf`, `docs/INSTRUCTIONS.md`) are included for context and reproducibility only and remain the property of the course staff.

## Author

Harshal Shah — BITS Pilani, Hyderabad Campus, CS F372 (Operating Systems), Assignment 2.
