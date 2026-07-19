import random
from collections import defaultdict


def generate_test_case(
        N, D, S, T, B, max_new_requests_per_turn, number_of_requests, max_booth_cost, test_case_number):
    filename = f"testcase{test_case_number}.txt"

    with open(filename, "w") as file:
        file.write(f"{N} {D} {S} {T} {B} {number_of_requests}\n")

        # Stores how many packages arrive on each turn (from 1 to T)
        turn_arrival_counts = defaultdict(int)
        rows = []

        for _ in range(number_of_requests):
            while True:

                x1 = random.randrange(N)
                y1 = random.randrange(N)
                x2 = random.randrange(N)
                y2 = random.randrange(N)

                if x1 == x2 and y1 == y2:
                    continue

                arrival_turn = random.randint(1, T)

                # Check if adding a package at this turn respects the limit
                if turn_arrival_counts[arrival_turn] < max_new_requests_per_turn:
                    turn_arrival_counts[arrival_turn] += 1

                    min_expiry = 1 + abs(x1 - x2) + abs(y1 - y2)
                    max_expiry = N * N
                    expiry = random.randint(min_expiry, max_expiry)

                    rows.append((x1, y1, x2, y2, expiry, arrival_turn))
                    break

        # Sort rows by the sixth number (arrival_turn) in ascending order
        rows.sort(key=lambda x: x[5])

        # Write each package request to the file
        for row in rows:
            file.write(f"{row[0]} {row[1]} {row[2]} {row[3]} {row[4]} {row[5]}\n")

        # Generate toll booths
        booths = set()
        for _ in range(B):
            while True:
                x = random.randrange(N)
                y = random.randrange(N)

                if (x, y) not in booths:
                    booths.add((x, y))
                    cost = random.randint(1, max_booth_cost)
                    file.write(f"{x} {y} {cost}\n")
                    break

    print(f"Test case written to {filename}")


N = 10 # size of the grid (5x5)
D = 5 # number of drivers
S = 3# number of solvers
T = 20  # Max turn at which a request can arrive
B = 10 # number of toll booths
max_new_requests_per_turn = 30  # Max packages that can arrive on the same turn
number_of_requests = 10 # Total packages in the simulation
max_booth_cost = 30  # Max toll cost
test_case_number = 1

random.seed(test_case_number) 

generate_test_case(
    N, D, S, T, B, max_new_requests_per_turn, number_of_requests, max_booth_cost, test_case_number
)