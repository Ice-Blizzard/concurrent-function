#include <iostream>
#include <string>
#include <semaphore.h>

#include "lib/infint/InfInt.h"
#include "collatz.hpp"

#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

#define SEMAPHORE_TP_SHARE "/semaphore_that_processes_share"
#define MEMORY_TP_SHARE "/memory_that_processes_share"

// Simple function for errors.
void exit1ErrorEnding(std::string errorMessage) {
   std::cerr << errorMessage;
   exit(1);
}

int main(int argc, char *argv[])
{
    // We need semaphore to protect our shared memory (obviously we need it).
    sem_t *s;
    // Used in TeamNewProcess.
    if (argv[1][0] == 'Y') {
        s = sem_open(SEMAPHORE_TP_SHARE, O_RDWR);
        if (s == SEM_FAILED) {
            exit1ErrorEnding("Semaphore failed error.");
        }
    }

    // Shared memory.
    int sharedMemory = shm_open(MEMORY_TP_SHARE, O_RDWR, 0);
    if (sharedMemory == -1) {
        exit1ErrorEnding("Shared memory error.");
    }

    int64_t sharedMemorySize = std::stoi(argv[2]);

    int prot, flags;
    prot = PROT_READ | PROT_WRITE;
    flags = MAP_SHARED;

    auto results = (uint64_t *) mmap(nullptr, sharedMemorySize, prot, flags, sharedMemory, 0);
    if (results == nullptr) {
        exit1ErrorEnding("Map error.");
    }

    // Collatz problem calculations.
    for (int i = 3; i < (argc - 1); i = i + 2) {
        uint64_t idx = std::stoi(argv[i]);

        // We use the fact that InfInt has constructors.
        InfInt calcCollatzVariable(argv[i + 1]);
        results[idx] = calcCollatz(calcCollatzVariable);
    }

    // After everything is done we can close our semaphore.
    if (argv[1][0] == 'Y') {
        if (sem_post(s)) {
            exit1ErrorEnding("Semaphore post error.");
        }
    }

    return 0;
}
