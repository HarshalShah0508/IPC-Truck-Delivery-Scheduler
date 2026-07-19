#include "helper.h"
#include <stdbool.h>

struct timeval start, stop;

char currentAuthStrings[MAX_TRUCKS][TRUCK_MAX_CAP + 1];
int turnNumber = 0;

int main(int argc, char* argv[]) {
    srand(time(NULL));

    if (argc < 2) {
        printf("Error: Test case number must be passed as a command line argument.\n");
        exit(1);
    }

    // --- 1. Read Test Case & Parameters ---
    int N, D, S, T, B, totalRequests;
    char testcaseFileName[25];
    sprintf(testcaseFileName, "testcase%s.txt", argv[1]);

    FILE* testcaseFile = fopen(testcaseFileName, "r");
    if (testcaseFile == NULL) {
        perror("Error opening testcase file in helper");
        exit(1);
    }

    fscanf(testcaseFile, "%d %d %d %d %d %d", &N, &D, &S, &T, &B, &totalRequests);

    if (N > MAX_GRID_SIZE || D > MAX_TRUCKS || S > MAX_SOLVERS || totalRequests > MAX_TOTAL_PACKAGES) {
        printf("Error: Test case parameters exceed compiled limits.\n");
        exit(1);
    }

    // --- 2. Initialize IPC ---
    key_t shmKey = rand() % CONSTANT;
    int shmId;
    MainSharedMemory* mainShmPtr;

    // Create Shared Memory
    if ((shmId = shmget(shmKey, sizeof(MainSharedMemory), PERMS | IPC_CREAT)) == -1) {
        perror("Error in shmget"); exit(1);
    }
    if ((mainShmPtr = shmat(shmId, NULL, 0)) == (void*)-1) {
        perror("Error in shmat"); exit(1);
    }

    // Create Solver Threads & Message Queues
    SolverInfo solverInfo[S];
    SolverArguments solverArguments[S];
    for (int i = 0; i < S; i++) {
        solverInfo[i].msgKey = rand() % CONSTANT;
        solverInfo[i].msgId = msgget(solverInfo[i].msgKey, PERMS | IPC_CREAT);
        if (solverInfo[i].msgId == -1) {
            perror("Error in msgget for solver"); exit(1);
        }
        solverArguments[i].solverNumber = i;
        solverArguments[i].messageQueueKey = solverInfo[i].msgKey;
        if (pthread_create(&solverInfo[i].threadId, NULL, solverRoutine, (void*)&solverArguments[i])) {
            perror("Error in pthread_create for solver"); exit(1);
        }
    }

    // Create Main Message Queue (Student <-> Helper)
    key_t msgKey = rand() % CONSTANT;
    int msgId;
    if ((msgId = msgget(msgKey, PERMS | IPC_CREAT)) == -1) {
        perror("Error in msgget for main queue"); exit(1);
    }

    // Helper's private grid to store toll info
    int localGrid[MAX_GRID_SIZE][MAX_GRID_SIZE];

    // --- 3. Initialize World State ---
    PackageInfo packageInfo[totalRequests];
    for (int i = 0; i < totalRequests; i++) {
        int x1, y1, x2, y2, expiry, arrival;
        fscanf(testcaseFile, "%d %d %d %d %d %d", &x1, &y1, &x2, &y2, &expiry, &arrival);
        packageInfo[i].request.packageId = i;
        packageInfo[i].request.pickup_x = x1;
        packageInfo[i].request.pickup_y = y1;
        packageInfo[i].request.dropoff_x = x2;
        packageInfo[i].request.dropoff_y = y2;
        packageInfo[i].request.arrival_turn = arrival;
        packageInfo[i].request.expiry_turn = expiry + arrival;
        packageInfo[i].status = PACKAGE_WAITING;
        packageInfo[i].current_x = x1;
        packageInfo[i].current_y = y1;
        packageInfo[i].on_truck_id = -1;
        packageInfo[i].movedThisTurn = 0;
        packageInfo[i].has_expired = false;
        mainShmPtr->packageLocations[i][0] = -1; // Not yet visible
        mainShmPtr->packageLocations[i][1] = -1;
    }

    // Initialize Grid (Toll Booths)
    memset(localGrid, 0, sizeof(int) * MAX_GRID_SIZE * MAX_GRID_SIZE);
    for (int i = 0; i < B; i++) {
        int x, y, cost;
        fscanf(testcaseFile, "%d %d %d", &x, &y, &cost);
        localGrid[x][y] = cost;
    }
    fclose(testcaseFile);

  // Delete the testcase file before running the student's solution.
    int deletionProcessId = fork();
    if (deletionProcessId == -1) {
        perror("Error while forking to delete the testcase file");
        exit(1);
    }

    if (deletionProcessId == 0) {
        if (execlp("rm", "rm", testcaseFileName, NULL) == -1) {
            perror("Error in execlp while deleting the testcase file");
            exit(1);
        }
    }

    // Initialize Trucks
    TruckInfo truckInfo[D];
    for (int i = 0; i < D; i++) {
        truckInfo[i].current_x = 0;
        truckInfo[i].current_y = 0;
        truckInfo[i].package_count = 0;
        truckInfo[i].turns_in_toll = 0;
        mainShmPtr->truckPositions[i][0] = 0;
        mainShmPtr->truckPositions[i][1] = 0;
        mainShmPtr->truckPackageCount[i] = 0;
        mainShmPtr->truckTurnsInToll[i] = 0;
        for (int j = 0; j < TRUCK_MAX_CAP; j++) {
            truckInfo[i].packages_on_board[j] = -1; // -1 signifies empty slot
        }
    }


    FILE* inputFile = fopen("input.txt", "w");
    if (inputFile == NULL) {
        perror("Error creating student input file"); exit(1);
    }
    fprintf(inputFile, "%d\n%d\n%d\n%d\n%d\n%d\n%d", N, D, S, T, B, shmKey, msgKey);
    for (int i = 0; i < S; i++) {
        fprintf(inputFile, "\n%d", solverInfo[i].msgKey);
    }
    fclose(inputFile);

    gettimeofday(&start, NULL);
    printf("Testcase %s\n", argv[1]);
    fflush(stdout);

    int childId = fork();
    if (childId == -1) {
        perror("Error while forking"); exit(1);
    }
    if (childId == 0) {
        // Child process
        if (execlp("./solution", "solution", NULL) == -1) {
            perror("Error in execlp"); exit(1);
        }
    }


    int requestsRemaining = totalRequests, upcomingRequest = 0, errorOccured = 0, expiredPackages = 0;
    TurnReadyRequest turnReadyRequest;
    TurnChangeResponse turnChangeResponse;
    turnChangeResponse.mtype = 2;
    turnChangeResponse.errorOccured = 0;
    turnChangeResponse.finished = 0;

    while (requestsRemaining > 0) {
        turnNumber++;
        turnChangeResponse.turnNumber = turnNumber;
        turnChangeResponse.newPackageRequestCount = 0;
        for (int i = 0; i < totalRequests; i++) packageInfo[i].movedThisTurn = 0;

        while (upcomingRequest < totalRequests && packageInfo[upcomingRequest].request.arrival_turn == turnNumber) {
            mainShmPtr->newPackageRequests[turnChangeResponse.newPackageRequestCount] = packageInfo[upcomingRequest].request;
            mainShmPtr->packageLocations[upcomingRequest][0] = packageInfo[upcomingRequest].current_x;
            mainShmPtr->packageLocations[upcomingRequest][1] = packageInfo[upcomingRequest].current_y;
            upcomingRequest++;
            turnChangeResponse.newPackageRequestCount++;
        }

        for (int i = 0; i < upcomingRequest; i++) {
            if (packageInfo[i].has_expired == false &&
                packageInfo[i].status != PACKAGE_DELIVERED &&
                turnNumber > packageInfo[i].request.expiry_turn)
            {
                packageInfo[i].has_expired = true; // Mark as counted
                packageInfo[i].status = PACKAGE_EXPIRED;
                expiredPackages++;
            }
            else if (packageInfo[i].has_expired == true &&
                     packageInfo[i].status != PACKAGE_DELIVERED)
            {
                packageInfo[i].status = PACKAGE_EXPIRED; // Keep status as expired
            }
        }

        for (int i = 0; i < D; i++) {
            if (truckInfo[i].package_count > 0) {
                createNewAuthString(currentAuthStrings[i], truckInfo[i].package_count);
            }
        }

        if (msgsnd(msgId, &turnChangeResponse, sizeof(TurnChangeResponse) - sizeof(long), 0) == -1) {
            perror("Error in msgsnd (new turn)"); exit(1);
        }
        if (msgrcv(msgId, &turnReadyRequest, sizeof(TurnReadyRequest) - sizeof(long), 1, 0) == -1) {
            perror("Error in msgrcv (turn ready)"); exit(1);
        }

        // Validate Auth Strings
        for (int i = 0; i < D; i++) {
            if (truckInfo[i].package_count > 0 && mainShmPtr->truckMovementInstructions[i] != 's') {
                if (strcmp(mainShmPtr->authStrings[i], currentAuthStrings[i]) != 0) {
                    printf("Turn %d: ERROR - Truck %d auth string is incorrect.\n", turnNumber, i);
                    errorOccured = 1; break;
                }
            }
        }
        if (errorOccured) break;

        bool was_in_toll[D];
        for (int i = 0; i < D; i++) {
            was_in_toll[i] = false;
            if (truckInfo[i].turns_in_toll > 0) {
                was_in_toll[i] = true; // Mark that we are serving a toll this turn
                truckInfo[i].turns_in_toll--;
                mainShmPtr->truckMovementInstructions[i] = 's'; // Force 'stay'
            }
        }

        // Process Drop-offs
        for (int i = 0; i < D; i++) {
            int packageId = mainShmPtr->dropOffCommands[i];
            if (packageId == -1) continue;

            if (packageId < 0 || packageId >= totalRequests || packageInfo[packageId].status == PACKAGE_WAITING || packageInfo[packageId].on_truck_id != i) {
                 printf("Turn %d: ERROR - Truck %d tried to drop off invalid/unowned package %d.\n", turnNumber, i, packageId);
                 errorOccured = 1; break;
            }
            else if (truckInfo[i].current_x != packageInfo[packageId].request.dropoff_x ||
                truckInfo[i].current_y != packageInfo[packageId].request.dropoff_y) {

                packageInfo[packageId].status = PACKAGE_WAITING;
            }
            else {
                // Valid dropoff
                packageInfo[packageId].status = PACKAGE_DELIVERED;
                requestsRemaining--;
            }
            packageInfo[packageId].on_truck_id = -1;
            packageInfo[packageId].current_x = truckInfo[i].current_x;
            packageInfo[packageId].current_y = truckInfo[i].current_y;
            packageInfo[packageId].movedThisTurn = 1;
            truckInfo[i].package_count--;
            for (int j = 0; j < TRUCK_MAX_CAP; j++) {
                if (truckInfo[i].packages_on_board[j] == packageId) {
                    truckInfo[i].packages_on_board[j] = -1;
                    break;
                }
            }
            
        }
        if (errorOccured) break;

        // Process Pickups
        for (int i = 0; i < D; i++) {
            int packageId = mainShmPtr->pickUpCommands[i];
            if (packageId == -1) continue;

            if (packageId < 0 || packageId >= totalRequests || packageInfo[packageId].status == PACKAGE_ON_TRUCK || packageInfo[packageId].status == PACKAGE_DELIVERED) {
                printf("Turn %d: ERROR - Truck %d tried to pick up invalid/completed package %d.\n", turnNumber, i, packageId);
                errorOccured = 1; break;
            }
            if (packageInfo[packageId].movedThisTurn) {
                printf("Turn %d: ERROR - Truck %d tried to pick up package %d, which was dropped off this turn.\n", turnNumber, i, packageId);
                errorOccured = 1; break;
            }
            if (truckInfo[i].current_x != packageInfo[packageId].current_x ||
                truckInfo[i].current_y != packageInfo[packageId].current_y) {
                 printf("Turn %d: ERROR - Truck %d at (%d, %d) tried to pick up package %d at wrong location.\n",
                    turnNumber, i, truckInfo[i].current_x, truckInfo[i].current_y, packageId);
                 errorOccured = 1; break;
            }
            if (truckInfo[i].package_count >= TRUCK_MAX_CAP) {
                printf("Turn %d: ERROR - Truck %d tried to pick up package %d but is full.\n", turnNumber, i, packageId);
                errorOccured = 1; break;
            }

            // Valid pickup (Note: we allow pickup of EXPIRED packages)
            if (packageInfo[packageId].status != PACKAGE_EXPIRED) {
                 packageInfo[packageId].status = PACKAGE_ON_TRUCK;
            }
            packageInfo[packageId].on_truck_id = i;
            packageInfo[packageId].current_x = -1; // On truck
            packageInfo[packageId].current_y = -1;
            truckInfo[i].package_count++;
            for (int j = 0; j < TRUCK_MAX_CAP; j++) {
                if (truckInfo[i].packages_on_board[j] == -1) {
                    truckInfo[i].packages_on_board[j] = packageId;
                    break;
                }
            }
        }
        if (errorOccured) break;

        // Process Movements
        for (int i = 0; i < D; i++) {
            char move = mainShmPtr->truckMovementInstructions[i];
            int new_x = truckInfo[i].current_x;
            int new_y = truckInfo[i].current_y;

            if (move == 'u') new_y--;
            else if (move == 'd') new_y++;
            else if (move == 'l') new_x--;
            else if (move == 'r') new_x++;
            else if (move != 's') {
                printf("Turn %d: ERROR - Truck %d gave invalid move command '%c'.\n", turnNumber, i, move);
                errorOccured = 1; break;
            }

            if (new_x < 0 || new_x >= N || new_y < 0 || new_y >= N) {
                printf("Turn %d: ERROR - Truck %d tried to move out of bounds to (%d, %d).\n", turnNumber, i, new_x, new_y);
                errorOccured = 1; break;
            }

            // Valid move
            truckInfo[i].current_x = new_x;
            truckInfo[i].current_y = new_y;

            // Apply toll only if truck *arrived* at a toll, not if it was *waiting* at one
            int toll_cost = localGrid[new_x][new_y];
            if (toll_cost > 0 && !was_in_toll[i]) {
                truckInfo[i].turns_in_toll = toll_cost;
            }
        }
        if (errorOccured) break;

        for (int i = 0; i < D; i++) {
            mainShmPtr->truckPositions[i][0] = truckInfo[i].current_x;
            mainShmPtr->truckPositions[i][1] = truckInfo[i].current_y;
            mainShmPtr->truckPackageCount[i] = truckInfo[i].package_count;
            mainShmPtr->truckTurnsInToll[i] = truckInfo[i].turns_in_toll;
        }
        for (int i = 0; i < totalRequests; i++) {
            mainShmPtr->packageLocations[i][0] = packageInfo[i].current_x;
            mainShmPtr->packageLocations[i][1] = packageInfo[i].current_y;
        }

    } // End of main game loop

    // --- 6. Shutdown ---
    turnChangeResponse.errorOccured = errorOccured;
    turnChangeResponse.finished = 1;
    msgsnd(msgId, &turnChangeResponse, sizeof(TurnChangeResponse) - sizeof(long), 0);

    wait(NULL); // Wait for student process to terminate
    gettimeofday(&stop, NULL);
    double result = ((stop.tv_sec - start.tv_sec)) + ((stop.tv_usec - start.tv_usec) / 1e6);
    
    printf(
      "Your solution took %lf seconds to execute. This time may vary with "
      "server load, and won't be used for final evaluation.\n",
      result);
    if(errorOccured){
        printf("Your solution took %d turns, and had a total of %d expired packages, "
               "but failed to complete the test case. These numbers do "
               "not vary with server load.\n",
               turnNumber, expiredPackages);
    }
    else{
        printf(
        "Your solution took %d turns, and had a total of %d expired packages, "
        "to successfully complete the test case. These numbers do "
        "not vary with server load.\n",
        turnNumber, expiredPackages);
    }

    msgctl(msgId, IPC_RMID, NULL); // Main queue

    SolverRequest solverRequest;
    solverRequest.mtype = 1; // Exit mtype
    for (int i = 0; i < S; i++) {
        msgsnd(solverInfo[i].msgId, &solverRequest, sizeof(solverRequest) - sizeof(long), 0);
        pthread_join(solverInfo[i].threadId, NULL);
        msgctl(solverInfo[i].msgId, IPC_RMID, NULL); // Solver queues
    }

    shmdt(mainShmPtr);
    shmctl(shmId, IPC_RMID, 0); // Shared memory

    return 0;
}

void* solverRoutine(void* args) {
    SolverArguments arguments = *(SolverArguments*)args;
    int targetTruck = 0;
    SolverRequest request;
    SolverResponse response;
    response.mtype = 4;
    response.guessIsCorrect = 0;

    int messageQueueId;
    if ((messageQueueId = msgget(arguments.messageQueueKey, PERMS)) == -1) {
        perror("solverRoutine: msgget"); exit(1);
    }

    while (1) {
        // Wait for a message of mtype 1, 2, or 3
        if (msgrcv(messageQueueId, &request, sizeof(request) - sizeof(long), -3, 0) == -1) {
            perror("solverRoutine: msgrcv"); exit(1);
        }

        switch (request.mtype) {
            case 1: // Exit signal
                pthread_exit(NULL);
            case 2: // Set target truck
                targetTruck = request.truckNumber;
                break;
            case 3: // Check guess
                response.guessIsCorrect = 0;
                if (strcmp(currentAuthStrings[targetTruck], request.authStringGuess) == 0) {
                    response.guessIsCorrect = 1;
                }
                if (msgsnd(messageQueueId, &response, sizeof(response) - sizeof(long), 0) == -1) {
                    perror("solverRoutine: msgsnd");
                }
        }
    }
}

void createNewAuthString(char* authStringLocation, int length) {
    char letters[4] = {'u', 'd', 'l', 'r'};
    for (int i = 0; i < length; i++) {
        int offset = rand() % AUTH_STRING_UNIQUE_LETTERS;
        authStringLocation[i] = letters[offset];
    }
    authStringLocation[length] = '\0';
}
