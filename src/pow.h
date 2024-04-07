// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitbi Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_POW_H
#define BITCOIN_POW_H

#include <consensus/params.h>

#include <stdint.h>
#include "sync.h"
#include "crypto/sha256.h"
#include <randomx.h>
#include "arith_uint256.h"
#include <primitives/block.h>
#include <logging.h>
#include <cpuid.h>


class CBlockHeader;
class CBlockIndex;
class uint256;

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params&);
unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params&);

/** Check whether a block hash satisfies the proof-of-work requirement specified by nBits */
bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params&);

/**
 * Return false if the proof-of-work requirement specified by new_nbits at a
 * given height is not possible, given the proof-of-work on the prior block as
 * specified by old_nbits.
 *
 * This function only checks that the new value is within a factor of 4 of the
 * old value for blocks at the difficulty adjustment interval, and otherwise
 * requires the values to be the same.
 *
 * Always returns true on networks where min difficulty blocks are allowed,
 * such as regtest/testnet.
 */
bool PermittedDifficultyTransition(const Consensus::Params& params, int64_t height, uint32_t old_nbits, uint32_t new_nbits);


void JustCheck();

bool CheckProofOfWorkX(const CBlockHeader& block, const Consensus::Params& params);

static inline uint256 HashBytesToUnit256(unsigned char *hashBytes) {
    uint256 hVal;
    memcpy(hVal.begin(), hashBytes, 32);
    return hVal;
}

static inline bool isAVX2Supported() {
    unsigned int eax, ebx, ecx, edx;
    __get_cpuid(1, &eax, &ebx, &ecx, &edx);
    bool osUsesXSAVE_XRSTORE = ecx & bit_XSAVE;
    bool cpuAVX2Support = ecx & bit_AVX2;

    if (osUsesXSAVE_XRSTORE && cpuAVX2Support) {
        // Check if the OS will save the YMM registers
        __get_cpuid(0, &eax, &ebx, &ecx, &edx);
        return ecx & bit_OSXSAVE;
    }

    return false;
}

static inline bool isSSSE3Supported() {
    unsigned int eax, ebx, ecx, edx;
    __get_cpuid(1, &eax, &ebx, &ecx, &edx);
    return ecx & bit_SSSE3;
}

class RxWorkMiner
{
private:
    randomx_vm *mVm;
    randomx_dataset *mDataset;
    const CBlockHeader  mBlockHeader;
    Mutex mMutex;

    inline bool checkPow(arith_uint256 hash, arith_uint256 bnTarget) const {
    // Check proof of work matches claimed amount
       return hash <= bnTarget;
    }
    RxWorkMiner(uint256 key, const CBlockHeader block) : mBlockHeader(block)
    {
        randomx_flags flags = RANDOMX_FLAG_FULL_MEM | RANDOMX_FLAG_JIT ;
        if (isAVX2Supported()) {
            flags |= RANDOMX_FLAG_ARGON2_AVX2;
        }
        if (isSSSE3Supported()) {
            flags |= RANDOMX_FLAG_ARGON2_SSSE3;
        }
        randomx_cache * cache = randomx_alloc_cache(flags);
        if (cache == nullptr)
        {
            LogPrintf("RxWorkMiner Cache allocation failed\n");
            return;
        }
        const int WIDTH = 32;
        
        randomx_init_cache(cache, key.data(), WIDTH);


        mDataset = randomx_alloc_dataset(flags);
        if (mDataset == nullptr)
        {
            LogPrintf("RxWorkMiner Dataset allocation failed\n");
            return ;
        }

        auto datasetItemCount = randomx_dataset_item_count();
        std::vector<std::thread> threads;
        const int nThreads = 8;
        if (nThreads > 1) {
            auto perThread = datasetItemCount / nThreads;
            auto remainder = datasetItemCount % nThreads;
            uint32_t startItem = 0;
            for (int i = 0; i < nThreads; ++i) {
                auto count = perThread + (i == nThreads - 1 ? remainder : 0);
                threads.push_back(std::thread(&randomx_init_dataset, mDataset, cache, startItem, count));
                startItem += count;
            }
            for (unsigned i = 0; i < threads.size(); ++i) {
                threads[i].join();
            }
        }
        else {
            randomx_init_dataset(mDataset, cache, 0, datasetItemCount);
        }

        randomx_release_cache(cache);

        mVm = randomx_create_vm(flags, nullptr, mDataset);
        if (mVm == nullptr)
        {
            LogPrintf("RxWorkMiner Failed to create a virtual machine\n");
            return;
        }
    }

    static uint256 sha256dKeyBlock(const CBlockHeader& block);
public:
    
    RxWorkMiner(const CBlockHeader& block):  RxWorkMiner(sha256dKeyBlock(block), block){
    }

    ~RxWorkMiner()
    {
        randomx_destroy_vm(mVm);
	    randomx_release_dataset(mDataset);
    }

    bool Mine(uint256* pHash, uint32_t* pNonce, bool (*ShutdownRequested)());
};

#endif // BITCOIN_POW_H
