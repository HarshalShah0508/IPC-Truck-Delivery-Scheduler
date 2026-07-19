#ifndef HELPER_H
#define HELPER_H

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define PERMS 0666
#define CONSTANT 100000000

// --- Problem-Specific Constants ---
#define MAX_GRID_SIZE 500
#define MAX_TRUCKS 250
#define MAX_SOLVERS 250
#define TRUCK_MAX_CAP 20
#define AUTH_STRING_UNIQUE_LETTERS 4
#define MAX_NEW_REQUESTS 50
#define MAX_TOTAL_PACKAGES 5000

// --- IPC Message Structs ---

// Student -> Helper: Signals turn commands are set in SHM
typedef struct TurnReadyRequest {
    long mtype; // mtype = 1
} TurnReadyRequest;

// Helper -> Student: Signals new turn state is ready in SHM
typedef struct TurnChangeResponse {
    long mtype; // mtype = 2
    int turnNumber;
    int newPackageRequestCount;
    int errorOccured; // 1 if an error occurred, 0 otherwise
    int finished;     // 1 if simulation is over, 0 otherwise
} TurnChangeResponse;

// Represents a package request
typedef struct PackageRequest {
    int packageId;
    int pickup_x;
    int pickup_y;
    int dropoff_x;
    int dropoff_y;
    int arrival_turn;
    int expiry_turn;
} PackageRequest;

// --- Shared Memory Structure ---
typedef struct MainSharedMemory {
    // --- Data FROM Student TO Helper ---
    char authStrings[MAX_TRUCKS][TRUCK_MAX_CAP + 1];
    char truckMovementInstructions[MAX_TRUCKS]; // 'u', 'd', 'l', 'r', 's'
    int pickUpCommands[MAX_TRUCKS];             // Package ID or -1
    int dropOffCommands[MAX_TRUCKS];            // Package ID or -1

    // --- Data FROM Helper TO Student ---
    int truckPositions[MAX_TRUCKS][2];      // Current (x, y)
    int truckPackageCount[MAX_TRUCKS];
    int truckTurnsInToll[MAX_TRUCKS];
    PackageRequest newPackageRequests[MAX_NEW_REQUESTS];
    int packageLocations[MAX_TOTAL_PACKAGES][2]; // (x, y) or (-1, -1) if on truck
} MainSharedMemory;

// --- Solver Process Structs ---

// Student -> Solver
typedef struct SolverRequest {
    long mtype;
    int truckNumber;
    char authStringGuess[TRUCK_MAX_CAP + 1];
} SolverRequest;

// Solver -> Student
typedef struct SolverResponse {
    long mtype;
    int guessIsCorrect; // 1 if correct, 0 if incorrect
} SolverResponse;

// Arguments for launching a solver thread
typedef struct SolverArguments {
    int solverNumber;
    key_t messageQueueKey;
} SolverArguments;

// Info for managing solver threads
typedef struct SolverInfo {
    pthread_t threadId;
    key_t msgKey;
    int msgId;
} SolverInfo;

typedef enum {
    PACKAGE_WAITING,
    PACKAGE_ON_TRUCK,
    PACKAGE_DELIVERED,
    PACKAGE_EXPIRED
} PackageStatus;

// Helper's internal representation of a package
typedef struct PackageInfo {
    PackageRequest request;
    PackageStatus status;
    int current_x;
    int current_y;
    int on_truck_id; // -1 if not on a truck
    int movedThisTurn; // Flag to prevent pickup/dropoff in same turn
    bool has_expired;  // Flag to prevent double-counting expired packages
} PackageInfo;

// Helper's internal representation of a truck
typedef struct TruckInfo {
    int current_x;
    int current_y;
    int package_count;
    int packages_on_board[TRUCK_MAX_CAP]; // Stores IDs of packages
    int turns_in_toll; // Turns remaining to wait
} TruckInfo;


void* solverRoutine(void* args);
void createNewAuthString(char* authStringLocation, int length);

#endif // HELPER_H
