#include <utility>
#include <deque>
#include <future>
#include <semaphore.h>

#include <cstring>
#include <string>

#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include "teams.hpp"
#include "contest.hpp"
#include "collatz.hpp"

#define BUFFER_NUMBER 8

#define SEMAPHORE_TP_SHARE "/semaphore_that_processes_share"
#define MEMORY_TP_SHARE "/memory_that_processes_share"

// Simple function for errors.
void exit1ErrorEnding(std::string errorMessage) {
   std::cerr << errorMessage;
   exit(1);
}

// Function to call function from SharedResuluts.
uint64_t calcCollatzSharedX(const InfInt &number, std::shared_ptr<SharedResults> sharedResults) {
    return sharedResults->calcCollatzShared(number);
}

// TeamNewThreads will be using this function to calculate Collatz problem.
static void calcCollatzTeamNewThreads(uint64_t idx, InfInt const & numberCalcCollatz, 
    uint64_t& numberThreads, ContestResult& r, 
    std::mutex& m, std::condition_variable& conVar, 
    std::shared_ptr<SharedResults> &sharedResultsMemory) {


    // Collatz problem - calling function.
    // Depending on if Team uses shared memory or not we call different function.
    if (sharedResultsMemory) {
        r[idx] = calcCollatzSharedX(numberCalcCollatz, sharedResultsMemory);
    }
    else {
        r[idx] = calcCollatz(numberCalcCollatz);
    }

    // Updating - 1 less thread.
    {
        std::lock_guard<std::mutex> lock(m);
        numberThreads--;
    }
    conVar.notify_all();
}

ContestResult TeamNewThreads::runContestImpl(ContestInput const & contestInput) {
    ContestResult r;
    size_t contestInputSize = contestInput.size();
    r.resize(contestInputSize);

    std::vector<std::thread> threads;
    threads.resize(contestInputSize);

    auto threadPoolSize = getSize();

    // 0s because we start from 0 threads (and intetration for them as well).
    uint64_t idx = 0;
    uint64_t numberThreads = 0;

    // Managing threads.
    std::mutex mainMutex;
    std::mutex m;
    std::condition_variable conVar;
    std::unique_lock<std::mutex> lock(mainMutex);

    // For X team.
    auto sharedResultsMemory = getSharedResults();

    for (InfInt const & singleInput : contestInput) {
        if (threadPoolSize <= numberThreads) {
            conVar.wait(lock, [&numberThreads, &threadPoolSize] { return threadPoolSize > numberThreads; } );
        }

        threads[idx] = createThread(calcCollatzTeamNewThreads, 
            idx, std::ref(singleInput),
            std::ref(numberThreads), std::ref(r), 
            std::ref(m), std::ref(conVar), 
            std::ref(sharedResultsMemory));

        {
            std::lock_guard<std::mutex> l(m);
            ++numberThreads;
        }

        ++idx;
    }

    // Mandatory waiting for end of all threads.
    for (uint64_t i = 0; i < idx; ++i) {
        threads[i].join();
    }

    return r;
}

// TeamConstThreads will be using this function to calculate Collatz problem.
static void calcCollatzTeamConstThreads(ContestInput const & contestInput, uint64_t idx,
    uint64_t numberConstThreads, ContestResult& r, 
    std::shared_ptr<SharedResults> &sharedResultsMemory) {


    while (idx < contestInput.size()) {
        // Depending on if Team uses shared memory or not we call different function.
        if (sharedResultsMemory) {
            r[idx] = calcCollatzSharedX(contestInput[idx], sharedResultsMemory);
        }
        else {
            r[idx] = calcCollatz(contestInput[idx]);
        }

        // We iterate by adding number of threads, and while keeps us in bounds.
        idx += numberConstThreads;
    }
}

ContestResult TeamConstThreads::runContestImpl(ContestInput const & contestInput) {
    ContestResult r;
    r.resize(contestInput.size());

    int32_t numberConstThreads = getSize();

    std::vector<std::thread> threads;
    threads.resize(numberConstThreads);

    // For X team.
    auto sharedResultsMemory = getSharedResults();

    for (uint64_t i = 0; i < numberConstThreads; ++i) {
        threads[i] = createThread(calcCollatzTeamConstThreads, std::ref(contestInput), i, 
            numberConstThreads, std::ref(r), 
            std::ref(sharedResultsMemory));
    }

    // Mandatory waiting for end of all threads.
    for (uint64_t i = 0; i < numberConstThreads; ++i) {
        threads[i].join();
    }

    return r;
}

ContestResult TeamPool::runContest(ContestInput const & contestInput) {
    ContestResult r;
    size_t contestInputSize = contestInput.size();
    r.resize(contestInputSize);

    cxxpool::thread_pool pool{getSize()};

    std::future<uint64_t> futures[contestInputSize];

    // For X team.
    auto sharedResultsMemory = getSharedResults();

    // Pushing tasks.
    for (size_t i = 0; i < contestInputSize; ++i) {
        InfInt inputCalc = contestInput[i];

        // Depending on if Team uses shared memory or not we call different function.
        if (sharedResultsMemory) {
            futures[i] = pool.push(calcCollatzSharedX, inputCalc, sharedResultsMemory);
        }
        else {
            futures[i] = pool.push(calcCollatz, inputCalc);
        }
    }

    // Getting tasks.
    for (size_t i = 0; i < contestInputSize; ++i) {
        r[i] = futures[i].get();
    }

    return r;
}

// In args I will be saving "YES" or "NO" which tells new_process.cpp
// if we want semaphore or not.
ContestResult TeamNewProcesses::runContest(ContestInput const & contestInput) {
    ContestResult r;

    int32_t memorySharing = -1;

    memorySharing = shm_open(MEMORY_TP_SHARE, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    if (memorySharing == -1) {
        exit1ErrorEnding("Memory sharing error.");
    }

    auto MEMORY_SHARE_SIZE = (int64_t) (sizeof(uint64_t) * contestInput.size());

    if (ftruncate(memorySharing, MEMORY_SHARE_SIZE) == -1) {
        std::cerr << MEMORY_SHARE_SIZE;
        exit1ErrorEnding(" - Ftruncate error.");
    }

    int prot, flags;
    prot = PROT_READ | PROT_WRITE;
    flags = MAP_SHARED;

    auto mappedMemory = (uint64_t *) mmap(nullptr, MEMORY_SHARE_SIZE, prot, flags, memorySharing, 0);

    if (mappedMemory == MAP_FAILED) {
        exit1ErrorEnding("Map failed error.");
    }

    // Semaphore.
    sem_t *s;
    s = sem_open(SEMAPHORE_TP_SHARE, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, getSize());
    if (s == SEM_FAILED) {
        exit1ErrorEnding("Semaphore failed error.");
    }

    uint64_t i = -1;
    pid_t pid;
    std::vector<pid_t> wait_pid;

    for (InfInt const & singleInput : contestInput) {
        ++i;

        char idx[BUFFER_NUMBER];
        char numbers[singleInput.numberOfDigits() + 7];
        char memorySize[BUFFER_NUMBER];

        strcpy(idx, std::to_string(i).c_str());
        strcpy(numbers, singleInput.toString().c_str());
        strcpy(memorySize, std::to_string(MEMORY_SHARE_SIZE).c_str());

        char* args[6] = {(char *) "new_process", (char *) "YES", memorySize, idx, numbers, nullptr};

        if (sem_wait(s)) {
            exit1ErrorEnding("Semaphore wait error.");
        }

        switch (pid = fork()) {
            case -1:
                exit1ErrorEnding("Fork error.");
            case 0:
                execvp("./new_process", args);
                exit(0);
            default:
                wait_pid.push_back(pid);
        }
    }

    // Mandatory waiting for end of all children.
    for (size_t i = 0; i < wait_pid.size(); ++i) {
        wait(0);
    }

    close(memorySharing);
    sem_unlink(SEMAPHORE_TP_SHARE);
    shm_unlink(MEMORY_TP_SHARE);

    for (int i = 0; i < contestInput.size(); ++i) {
        r.push_back(mappedMemory[i]);
    }

    return r;
}

ContestResult TeamConstProcesses::runContest(ContestInput const & contestInput) {
    ContestResult r;

    uint32_t numberConstProcesses = getSize();

    int32_t memorySharing = -1;
    memorySharing = shm_open(MEMORY_TP_SHARE, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    if (memorySharing == -1) {
        exit1ErrorEnding("Shared memory error.");
    }

    auto MEMORY_SHARE_SIZE = (int64_t) (sizeof(uint64_t) * contestInput.size());

    if (ftruncate(memorySharing, MEMORY_SHARE_SIZE) == -1) {
        std::cerr << MEMORY_SHARE_SIZE;
        exit1ErrorEnding(" - Ftruncate error.");
    }

    int prot, flags;
    prot = PROT_READ | PROT_WRITE;
    flags = MAP_SHARED;

    auto mappedMemory = (uint64_t *) mmap(nullptr, MEMORY_SHARE_SIZE, prot, flags, memorySharing, 0);

    if (mappedMemory == MAP_FAILED) {
        exit1ErrorEnding("Map error.");
    }

    pid_t pid;
    std::vector<pid_t> wait_pid;

    for (uint64_t i = 0; i < numberConstProcesses; ++i) {
        uint64_t argsSize = 0;
        uint64_t indexIteration = i; // Basically modulo numberConstProcesses.
        char memorySize[BUFFER_NUMBER];

        strcpy(memorySize, std::to_string(MEMORY_SHARE_SIZE).c_str());
        char *numbers[(contestInput.size() / numberConstProcesses) + 1];

        while (indexIteration < contestInput.size()) {
            numbers[argsSize] = new char[contestInput[indexIteration].numberOfDigits() + 7];
            strcpy(numbers[argsSize], contestInput[indexIteration].toString().c_str());
            ++argsSize;
            indexIteration += numberConstProcesses;
        }

        char *args[2 * argsSize + 4];
        args[0] = (char *) "new_process";
        args[1] = (char *) "NO";
        args[2] = memorySize;
        char id[argsSize][BUFFER_NUMBER];

        for (uint64_t ii = i, j = 3, k = 0; k < argsSize; ii += numberConstProcesses, j = j + 2, ++k) {
            strcpy(id[k], std::to_string(ii).c_str());
            args[j] = id[k];
            args[j + 1] = numbers[k];
        }

        args[2 * argsSize + 3] = nullptr;

        switch (pid = fork()) {
            case -1:
                exit1ErrorEnding("Fork error.");
            case 0:
                execv("./new_process", args);
                exit(0);
            default:
                wait_pid.push_back(pid);
        }
    }

    // Mandatory waiting for end of all children.
    for (size_t i = 0; i < wait_pid.size(); ++i) {
        wait(nullptr);
    }

    close(memorySharing);
    shm_unlink(MEMORY_TP_SHARE);

    for (int i = 0; i < contestInput.size(); ++i) {
        r.push_back(mappedMemory[i]);
    }

    return r;
}

ContestResult TeamAsync::runContest(ContestInput const & contestInput) {
    ContestResult r;
    size_t contestInputSize = contestInput.size();
    r.resize(contestInputSize);

    std::future<uint64_t> futures[contestInputSize];

    // For X team.
    auto sharedResultsMemory = getSharedResults();

    // Pushing tasks.
    for (size_t i = 0; i < contestInputSize; ++i) {
        InfInt inputCalc = contestInput[i];

        // Using this: https://en.cppreference.com/w/cpp/thread/async .
        // Depending on if Team uses shared memory or not we call different function.
        if (sharedResultsMemory) {
            futures[i] = std::async(calcCollatzSharedX, inputCalc, sharedResultsMemory);
        }
        else {
            futures[i] = std::async(calcCollatz, inputCalc);
        }
    }

    // Getting tasks.
    for (size_t i = 0; i < contestInputSize; ++i) {
        r[i] = futures[i].get();
    }

    return r;
}
