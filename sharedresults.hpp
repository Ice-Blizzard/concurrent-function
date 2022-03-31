#ifndef SHAREDRESULTS_HPP
#define SHAREDRESULTS_HPP

#include <mutex>

#include "lib/infint/InfInt.h"

class SharedResults
{
    /* Whole idea was to just have map with InfInt and uint64_t that had shared info.
       Another idea was to just have a table, since it's much faster to access
       and save results. Only problem is that it has limit. I decided to merge
       those two ideas, to speed up "tests" with small numbers. */
    static constexpr int limit = 131072; // 2^17

    // Table.
    uint64_t *sharedTable;
    std::mutex sharedTableMutex;

    // Map.
    std::map<InfInt, uint64_t> sharedMap;
    std::mutex sharedMapMutex;

public:
    SharedResults() : sharedTableMutex(), sharedMap(), sharedMapMutex() {
        sharedTable = new uint64_t[limit]();
    }

    uint64_t calcCollatzShared(const InfInt &number) {
        // Map case.
        if (InfInt(limit) <= number) {
            std::lock_guard<std::mutex> lock(sharedMapMutex);
            auto iterator = sharedMap.find(number);

            // Shared doesn't have what we are looking for so we create it.
            if (iterator == sharedMap.end()) {
                uint64_t result = calcCollatz(number);
                sharedMap[number] = result;
                return result;
            }

            return iterator->second;
        }

        // Table case.
        int numberInt = number.toInt();

        // We already have what we are looking for in shared.
        if (sharedTable[numberInt] != 0) {
            return sharedTable[numberInt];
        }

        std::lock_guard<std::mutex> lock(sharedTableMutex);

        sharedTable[numberInt] = calcCollatz(number);

        return sharedTable[numberInt];
    }

    ~SharedResults() {
        delete [] sharedTable;
    }
};

#endif
