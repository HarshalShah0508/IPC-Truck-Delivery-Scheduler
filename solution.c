#include "helper.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
typedef struct {
    int trkid;
    int pkgid;
    int score;
} job;
typedef struct simstate {
    int grid;
    int trks;
    int solvs;
    int maxT;
    int booths;
    key_t shmkey;
    key_t mainq;
    key_t solvq[MAX_SOLVERS];
    MainSharedMemory *shm;
    int mainid;
    int solvqs[MAX_SOLVERS];
    int turn;
    PackageRequest pkgdb[MAX_TOTAL_PACKAGES];
    int pkstat[MAX_TOTAL_PACKAGES];
    int trkstat[MAX_TRUCKS];
    int trkjob[MAX_TRUCKS];
    int spool[MAX_SOLVERS];
    int psize;
    pthread_mutex_t plock;
    pthread_cond_t pcond;
} state;
typedef struct {
    int trkid;
    int authln;
    state* sim;
} task;
void f(state* sim, int turn, int newpkg);
void* tf(void* arg);
bool ispossible(state* sim, int trkid, int qid, char* buf, int len, int pos);
int main() {
    state sim;
    memset(&sim, 0, sizeof(state));
    FILE *file = fopen("input.txt", "r");
    if (file == NULL) {
        perror("Error opening input.txt");
        exit(1);
    }
    fscanf(file, "%d %d %d %d %d %d %d", &sim.grid, &sim.trks, &sim.solvs, &sim.maxT, &sim.booths, &sim.shmkey, &sim.mainq);
    for (int i = 0; i < sim.solvs; i++) {
        fscanf(file, "%d", &sim.solvq[i]);
    }
    fclose(file);
    int shmid = shmget(sim.shmkey, sizeof(MainSharedMemory), PERMS);
    if (shmid == -1) {
        perror("shmget failed");
        exit(1);
    }
    sim.shm = (MainSharedMemory *)shmat(shmid, NULL, 0);
    if (sim.shm == (void *)-1) {
        perror("shmat failed");
        exit(1);
    }
    sim.mainid = msgget(sim.mainq, PERMS);
    if (sim.mainid == -1) {
        perror("msgget (main) failed");
        exit(1);
    }
    for (int i = 0; i < sim.solvs; i++) {
        sim.solvqs[i] = msgget(sim.solvq[i], PERMS);
        if (sim.solvqs[i] == -1) {
            perror("msgget (solver) failed");
            exit(1);
        }
    }
    pthread_mutex_init(&sim.plock, NULL);
    pthread_cond_init(&sim.pcond, NULL);
    sim.psize = sim.solvs;
    for(int i = 0; i < sim.solvs; i++) sim.spool[i] = i;
    for (int i = 0; i < sim.trks; i++) sim.trkjob[i] = -1;
    TurnChangeResponse msg;
    TurnReadyRequest ready;
    ready.mtype = 1;
    for(;;) {
        if (msgrcv(sim.mainid, &msg, sizeof(TurnChangeResponse) - sizeof(long), 2, 0) == -1) {
            perror("msgrcv (main) failed");
            break;
        }
        if (msg.errorOccured || msg.finished) break;
        f(&sim, msg.turnNumber, msg.newPackageRequestCount);
        if (msgsnd(sim.mainid, &ready, sizeof(TurnReadyRequest) - sizeof(long), 0) == -1) {
            perror("msgsnd (main) failed");
            break;
        }
    }
    if (shmdt(sim.shm) == -1) perror("shmdt failed");
    pthread_mutex_destroy(&sim.plock);
    pthread_cond_destroy(&sim.pcond);
    return 0;
}
void f(state* sim, int turn, int newpkg) {
    sim->turn = turn;
    for (int i = 0; i < newpkg; i++) {
        PackageRequest req = sim->shm->newPackageRequests[i];
        sim->pkgdb[req.packageId] = req;
        sim->pkstat[req.packageId] = 1;
    }
    long long maxjob = (long long)sim->trks*MAX_TOTAL_PACKAGES;
    job* jobs = malloc(maxjob*sizeof(job));
    if (jobs == NULL) {
        perror("Failed to allocate memory for jobs");
        return; 
    }
    int jcount = 0;
    bool trbusy[sim->trks];
    bool pktaken[MAX_TOTAL_PACKAGES];
    memset(trbusy, 0, sizeof(trbusy));
    memset(pktaken, 0, sizeof(pktaken));
    long long penaltyexpired = 1000000;
    int urgencywindow = 30;
    long long urgencymult = 500;
    for (int i = 0; i < sim->trks; i++) {
        if (sim->trkstat[i] == 0) {
            for (int j = 0; j < MAX_TOTAL_PACKAGES; j++) {
                if (sim->pkstat[j] == 1) {
                    PackageRequest* p = &sim->pkgdb[j];
                    int* pos = sim->shm->truckPositions[i];
                    int tpick = abs(pos[0] - p->pickup_x) + abs(pos[1] - p->pickup_y);
                    int tdel = abs(p->pickup_x - p->dropoff_x) + abs(p->pickup_y - p->dropoff_y);
                    int ttotal = tpick + tdel;
                    int buf = p->expiry_turn - (sim->turn + ttotal);
                    long long penlty = (buf < 0) ? penaltyexpired : 0;
                    penlty += (buf < urgencywindow) ? (urgencywindow - (long long)buf) * urgencymult : 0;
                    long long score_ll = (long long)ttotal + penlty;
                    int score;
                    if (score_ll > INT_MAX) score = INT_MAX;
                    else score = (int)score_ll;
                    jobs[jcount].trkid = i;
                    jobs[jcount].pkgid = j;
                    jobs[jcount].score = score;
                    jcount++;
                }
            }
        }
    }
    for (int i = 1; i < jcount; i++) {
        job keyjob = jobs[i];
        int j = i - 1;
        while (j >= 0 && jobs[j].score > keyjob.score) {
            jobs[j + 1] = jobs[j];
            j--;
        }
        jobs[j + 1] = keyjob;
    }
    for (int i = 0; i < jcount; i++) {
        int trkid = jobs[i].trkid;
        int pkgid = jobs[i].pkgid;
        if (sim->trkstat[trkid] == 0 && sim->pkstat[pkgid] == 1) {
            sim->trkstat[trkid] = 1;
            sim->trkjob[trkid] = pkgid;
            sim->pkstat[pkgid] = 2;
            trbusy[trkid] = true; 
            pktaken[pkgid] = true;
        }
    }
    free(jobs);
    pthread_t th[sim->trks];
    task tasks[sim->trks];
    bool dojoin[sim->trks];
    char moves[sim->trks];
    memset(dojoin, 0, sizeof(dojoin));
    for (int i = 0; i < sim->trks; i++) {
        sim->shm->pickUpCommands[i] = -1;
        sim->shm->dropOffCommands[i] = -1;
        strcpy(sim->shm->authStrings[i], "");
        if (sim->shm->truckTurnsInToll[i] > 0) {
            moves[i] = 's';
            continue;
        }
        int* pos = sim->shm->truckPositions[i];
        if (sim->trkstat[i] == 0) moves[i] = 's';
        else if (sim->trkstat[i] == 1) {
            int tid = sim->trkjob[i];
            PackageRequest* tpkg = &sim->pkgdb[tid];
            if (pos[0] < tpkg->pickup_x) moves[i] = 'r';
            else if (pos[0] > tpkg->pickup_x) moves[i] = 'l';
            else if (pos[1] < tpkg->pickup_y) moves[i] = 'd';
            else if (pos[1] > tpkg->pickup_y) moves[i] = 'u';
            else moves[i] = 's';
            if (moves[i] == 's') {
                sim->shm->pickUpCommands[i] = tid;
                sim->trkstat[i] = 2;
                sim->pkstat[tid] = 3;
            }
        } 
        else if (sim->trkstat[i] == 2) {
            int tid = sim->trkjob[i];
            PackageRequest* tpkg = &sim->pkgdb[tid];
            if (pos[0] < tpkg->dropoff_x) moves[i] = 'r';
            else if (pos[0] > tpkg->dropoff_x) moves[i] = 'l';
            else if (pos[1] < tpkg->dropoff_y) moves[i] = 'd';
            else if (pos[1] > tpkg->dropoff_y) moves[i] = 'u';
            else moves[i] = 's';
            if (moves[i] == 's') {
                sim->shm->dropOffCommands[i] = tid;
                sim->trkstat[i] = 0;
                sim->pkstat[tid] = 4;
            }
        }
    }
    for (int i = 0; i < sim->trks; i++) {
        int pkgs = sim->shm->truckPackageCount[i];
        if (sim->shm->truckTurnsInToll[i] == 0 && moves[i] != 's' && pkgs > 0) {
            tasks[i].trkid = i;
            tasks[i].authln = pkgs;
            tasks[i].sim = sim;
            if (pthread_create(&th[i], NULL, tf, &tasks[i]) != 0) perror("pthread_create failed");
            dojoin[i] = true;
        } 
        else sim->shm->truckMovementInstructions[i] = moves[i];
    }
    for (int i = 0; i < sim->trks; i++) {
        if (dojoin[i]) {
            pthread_join(th[i], NULL);
            sim->shm->truckMovementInstructions[i] = moves[i];
        }
    }
}
bool ispossible(state* sim, int trkid, int qid, char* buf, int len, int pos) {
    if (pos == len) {
        buf[len] = '\0';
        SolverRequest req;
        req.mtype = 3;
        req.truckNumber = trkid;
        strcpy(req.authStringGuess, buf);
        if (msgsnd(qid, &req, sizeof(SolverRequest) - sizeof(long), 0) == -1) {
            perror("msgsnd to solver failed");
            return false;
        }
        SolverResponse res;
        if (msgrcv(qid, &res, sizeof(SolverResponse) - sizeof(long), 4, 0) == -1) {
            perror("msgrcv from solver failed");
            return false;
        }
        return res.guessIsCorrect == 1;
    }
    char dirs[] = {'u', 'd', 'l', 'r'};
    for (int i = 0; i < 4; i++) {
        buf[pos] = dirs[i];
        if (ispossible(sim, trkid, qid, buf, len, pos + 1)) return true;
    }
    return false;
}
void* tf(void* arg) {
    task* job = (task*)arg;
    int trkid = job->trkid;
    int authln = job->authln;
    state* sim = job->sim;
    pthread_mutex_lock(&sim->plock);
    while (sim->psize == 0) pthread_cond_wait(&sim->pcond, &sim->plock);
    int sid = sim->spool[--sim->psize];
    pthread_mutex_unlock(&sim->plock);
    int qid = sim->solvqs[sid];
    SolverRequest req;
    req.mtype = 2;
    req.truckNumber = trkid;
    strcpy(req.authStringGuess, "");
    msgsnd(qid, &req, sizeof(SolverRequest) - sizeof(long), 0);
    char buf[TRUCK_MAX_CAP + 1];
    if (ispossible(sim, trkid, qid, buf, authln, 0)) strcpy(sim->shm->authStrings[trkid], buf);
    pthread_mutex_lock(&sim->plock);
    sim->spool[sim->psize++] = sid;
    pthread_cond_signal(&sim->pcond);
    pthread_mutex_unlock(&sim->plock);
    return NULL;
}