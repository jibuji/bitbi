// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitbi Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow.h>

#include <arith_uint256.h>
#include <chain.h>
#include <primitives/block.h>
#include <uint256.h>
#include "hash.h"
#include "util/syncstack.h"
#include "common/stopwatch.h"
#ifdef __linux__ 
    #include <sys/sysinfo.h>
#elif _WIN32
    #include <windows.h>
#else
    #error "Unknown compiler"
#endif


unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);
    unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();

    // Only change once per difficulty adjustment interval
    if ((pindexLast->nHeight+1) % params.DifficultyAdjustmentInterval() != 0)
    {
        if (params.fPowAllowMinDifficultyBlocks)
        {
            // Special difficulty rule for testnet:
            // If the new block's timestamp is more than 2* 10 minutes
            // then allow mining of a min-difficulty block.
            if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing*2)
                return nProofOfWorkLimit;
            else
            {
                // Return the last non-special-min-difficulty-rules-block
                const CBlockIndex* pindex = pindexLast;
                while (pindex->pprev && pindex->nHeight % params.DifficultyAdjustmentInterval() != 0 && pindex->nBits == nProofOfWorkLimit)
                    pindex = pindex->pprev;
                return pindex->nBits;
            }
        }
        return pindexLast->nBits;
    }

    // Go back by what we want to be 14 days worth of blocks
    int nHeightFirst = pindexLast->nHeight - (params.DifficultyAdjustmentInterval()-1);
    assert(nHeightFirst >= 0);
    const CBlockIndex* pindexFirst = pindexLast->GetAncestor(nHeightFirst);
    assert(pindexFirst);
    return CalculateNextWorkRequired(pindexLast, pindexFirst->GetBlockTime(), params);
}

unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    // Limit adjustment step
    int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
    if (nActualTimespan < params.nPowTargetTimespan/4)
        nActualTimespan = params.nPowTargetTimespan/4;
    if (nActualTimespan > params.nPowTargetTimespan*4)
        nActualTimespan = params.nPowTargetTimespan*4;

    // Retarget
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    arith_uint256 bnNew;
    bnNew.SetCompact(pindexLast->nBits);
    // bnNew *= nActualTimespan;
    // bnNew /= params.nPowTargetTimespan;
    bnNew *= (nActualTimespan*2048/params.nPowTargetTimespan);
    bnNew /= 2048;
    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    return bnNew.GetCompact();
}

// Check that on difficulty adjustments, the new difficulty does not increase
// or decrease beyond the permitted limits.
bool PermittedDifficultyTransition(const Consensus::Params& params, int64_t height, uint32_t old_nbits, uint32_t new_nbits)
{
    if (params.fPowAllowMinDifficultyBlocks) return true;

    if (height % params.DifficultyAdjustmentInterval() == 0) {
        int64_t smallest_timespan = params.nPowTargetTimespan/4;
        int64_t largest_timespan = params.nPowTargetTimespan*4;

        const arith_uint256 pow_limit = UintToArith256(params.powLimit);
        arith_uint256 observed_new_target;
        observed_new_target.SetCompact(new_nbits);

        // Calculate the largest difficulty value possible:
        arith_uint256 largest_difficulty_target;
        largest_difficulty_target.SetCompact(old_nbits);
        largest_difficulty_target *= largest_timespan;
        largest_difficulty_target /= params.nPowTargetTimespan;

        if (largest_difficulty_target > pow_limit) {
            largest_difficulty_target = pow_limit;
        }

        // Round and then compare this new calculated value to what is
        // observed.
        arith_uint256 maximum_new_target;
        maximum_new_target.SetCompact(largest_difficulty_target.GetCompact());
        if (maximum_new_target < observed_new_target) return false;

        // Calculate the smallest difficulty value possible:
        arith_uint256 smallest_difficulty_target;
        smallest_difficulty_target.SetCompact(old_nbits);
        smallest_difficulty_target *= smallest_timespan;
        smallest_difficulty_target /= params.nPowTargetTimespan;

        if (smallest_difficulty_target > pow_limit) {
            smallest_difficulty_target = pow_limit;
        }

        // Round and then compare this new calculated value to what is
        // observed.
        arith_uint256 minimum_new_target;
        minimum_new_target.SetCompact(smallest_difficulty_target.GetCompact());
        if (minimum_new_target > observed_new_target) return false;
    } else if (old_nbits != new_nbits) {
        return false;
    }
    return true;
}

bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(params.powLimit))
        return false;

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
        return false;

    return true;
}



class RxWorkVerifier
{
private:
    randomx_cache *mCache;
    Mutex mMutex;
public:
    RxWorkVerifier()
    {
        randomx_flags flags =  RANDOMX_FLAG_JIT ;
        if (isAVX2Supported()) {
            flags |= RANDOMX_FLAG_ARGON2_AVX2;
        }
        if (isSSSE3Supported()) {
            flags |= RANDOMX_FLAG_ARGON2_SSSE3;
        }
        mCache = randomx_alloc_cache(flags);
        if (mCache == nullptr)
        {
            LogPrintf("RxWorkVerifier Cache allocation failed\n");
            return;
        }
    }
    ~RxWorkVerifier()
    {
        randomx_release_cache(mCache);
    }

    uint256 PowHash(uint256 key,  unsigned char* input, size_t inputSize)
    {
        LOCK(mMutex);
        const int WIDTH = 32;
        
        randomx_init_cache(mCache, key.data(), WIDTH);

        randomx_vm *vm = randomx_create_vm(randomx_get_flags(), mCache, nullptr);

        uint8_t result[WIDTH];
        randomx_calculate_hash(vm, input, inputSize, result);

        randomx_destroy_vm(vm);

        return HashBytesToUnit256(result);
    }
};

class RxWorkVerifier2
{
private:
    SyncStack<randomx_cache*> mCacheStack;
public:
    RxWorkVerifier2()
    {
        randomx_flags flags =  RANDOMX_FLAG_JIT ;
        if (isAVX2Supported()) {
            flags |= RANDOMX_FLAG_ARGON2_AVX2;
        }
        if (isSSSE3Supported()) {
            flags |= RANDOMX_FLAG_ARGON2_SSSE3;
        }
        //pre-allocate 2*ncores caches
        const int nCaches = 2*std::thread::hardware_concurrency();
        LogPrintf("RxWorkVerifier2 pre-allocate %d caches\n", nCaches);
        for (int i = 0; i < nCaches; ++i) {
            randomx_cache* cache = randomx_alloc_cache(flags);
            if (cache == nullptr)
            {
                LogPrintf("RxWorkVerifier Cache allocation failed\n");
                return;
            }
            mCacheStack.push(cache);
        }
    }
    ~RxWorkVerifier2()
    {
        for (int i = 0; i < mCacheStack.size(); ++i) {
            randomx_cache* cache = mCacheStack.pop();
            randomx_release_cache(cache);
        }
    }

    uint256 PowHash(uint256 key,  unsigned char* input, size_t inputSize)
    {
        const int WIDTH = 32;
        randomx_cache *cache = mCacheStack.pop();
        randomx_init_cache(cache, key.data(), WIDTH);

        randomx_vm *vm = randomx_create_vm(randomx_get_flags(), cache, nullptr);

        uint8_t result[WIDTH];
        randomx_calculate_hash(vm, input, inputSize, result);

        randomx_destroy_vm(vm);
        mCacheStack.push(cache);

        return HashBytesToUnit256(result);
    }
};

static inline const int WIDTH = 32;

class VerifierCtx {
public:
    uint256 m_key;
    randomx_vm *m_vm;
    randomx_cache *m_cache;
    VerifierCtx(uint256 key):m_vm(nullptr), m_cache(nullptr), m_key(uint256()){
        reinitialize(key);
    }
    void reinitialize(uint256 key) {
        if (key != m_key) {
            if (m_vm) {
                randomx_destroy_vm(m_vm);
            }
            if (m_cache) {
                randomx_release_cache(m_cache);
            }
            randomx_flags flags =  RANDOMX_FLAG_JIT ;
            if (isAVX2Supported()) {
                flags |= RANDOMX_FLAG_ARGON2_AVX2;
            }
            if (isSSSE3Supported()) {
                flags |= RANDOMX_FLAG_ARGON2_SSSE3;
            }
            m_cache = randomx_alloc_cache(flags);
            const int WIDTH = 32;
            randomx_init_cache(m_cache, key.data(), WIDTH);
            m_vm = randomx_create_vm(flags, m_cache, nullptr);
            m_key = key;
        }
    }

    ~VerifierCtx() {
        randomx_destroy_vm(m_vm);
        randomx_release_cache(m_cache);
    }
};

#ifdef __linux__
static inline uint64_t FreePhysicalMemory() {
    struct sysinfo memInfo;

    sysinfo (&memInfo);
    long long freePhysMem = memInfo.freeram;
    // to bytes
    freePhysMem *= memInfo.mem_unit;
    return freePhysMem;
}
#elif _WIN32
static inline uint64_t FreePhysicalMemory() {
    MEMORYSTATUSEX statex;
    statex.dwLength = sizeof(statex);
    GlobalMemoryStatusEx(&statex);
    DWORDLONG freeRAM = statex.ullAvailPhys;
    return freeRAM;
}
#else
    #error "Unknown compiler"
#endif

class RxWorkVerifier3
{
private:
    SyncStack<VerifierCtx*> mCacheStack;
    int m_nCaches;
public:
    RxWorkVerifier3()
    {   
        // 256M per cache
        // pre-allocate 2*ncores caches, but should less then FreePhysicalMemory()/256M
        const uint64_t ONE_CACHE_SIZE = 256*1024*1024;
        const uint64_t freeMemory = FreePhysicalMemory();
        LogPrintf("RxWorkVerifier3 FreePhysicalMemory=%ld\n", freeMemory);
        m_nCaches = std::min((int)(1*std::thread::hardware_concurrency()), (int)(freeMemory/ONE_CACHE_SIZE));
        for (int i = 0; i < m_nCaches; ++i) {
            VerifierCtx* cache = new VerifierCtx(uint256());
            mCacheStack.push(cache);
        }
    }
    ~RxWorkVerifier3()
    {
        for (int i = 0; i < mCacheStack.size(); ++i) {
            VerifierCtx* cache = mCacheStack.pop();
            delete cache;
        }
    }

    uint256 PowHash(uint256 key,  unsigned char* input, size_t inputSize)
    {
        VerifierCtx *cache = mCacheStack.pop();
        cache->reinitialize(key);

        uint8_t result[WIDTH];
        randomx_calculate_hash(cache->m_vm, input, inputSize, result);

        mCacheStack.push(cache);

        return HashBytesToUnit256(result);
    }
};



void bin2hex(char *s, const unsigned char *p, size_t len)
{
	int i;
	for (i = 0; i < len; i++)
		sprintf(s + (i * 2), "%02x", (unsigned int)p[i]);
}

static RxWorkVerifier3 g_RxWorkVerifier{};
bool CheckProofOfWorkX(const CBlockHeader& block, const Consensus::Params& params) {
    // serialize header without the nonce field
    CHashWriter keyss(PROTOCOL_VERSION);
    //change the key approximately every 345678 seconds(~4days)
    keyss << block.nVersion << block.nTime/345678 << block.nBits << uint32_t(0);
    uint256 key256 = keyss.GetHash();

    //double check for sure
    unsigned char input[80] = {0};
    WriteLE32(&input[0], block.nVersion);
    memcpy(&input[4], block.hashPrevBlock.begin(), 32);
    memcpy(&input[36], block.hashMerkleRoot.begin(), 32);
    WriteLE32(&input[68], block.nTime);
    WriteLE32(&input[72], block.nBits);
    WriteLE32(&input[76], block.nNonce);

    // char input_hex[161] = {0};
	// bin2hex(input_hex, (unsigned char *)input, 80);
    // LogPrintf("CheckProofOfWorkX key=%s, input=%s\n", key256.ToString().c_str(), input_hex);
    uint256 result = g_RxWorkVerifier.PowHash(key256, input, 80);
    return CheckProofOfWork(result, block.nBits, params);
}

std::string doubleSHA256(const std::string& data) {
    CSHA256 sha;
    uint256 hash;
    sha.Write((unsigned char*)data.data(), data.size());
    sha.Finalize(hash.begin());
    sha.Reset().Write(hash.begin(), CSHA256::OUTPUT_SIZE).Finalize(hash.begin());
    return hash.GetHex();
}

static std::string hexStr(unsigned char* hash, uint32_t size) {
    std::string str;
    for (int i = 0; i < size; i++) {
        char buf[3];
        sprintf(buf, "%02x", hash[i]);
        str += buf;
    }
    return str;

}


static const uint32_t sha256_h[8] = {
	0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
	0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
};


static const uint32_t sha256_k[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
	0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
	0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
	0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
	0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
	0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
	0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
	0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
	0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

void sha256_init(uint32_t *state)
{
	memcpy(state, sha256_h, 32);
}
static inline uint32_t be32dec(const void *pp)
{
	const uint8_t *p = (uint8_t const *)pp;
	return ((uint32_t)(p[3]) + ((uint32_t)(p[2]) << 8) +
	    ((uint32_t)(p[1]) << 16) + ((uint32_t)(p[0]) << 24));
}

/* SHA256 round function */
#define RND(a, b, c, d, e, f, g, h, k) \
	do { \
		t0 = h + S1(e) + Ch(e, f, g) + k; \
		t1 = S0(a) + Maj(a, b, c); \
		d += t0; \
		h  = t0 + t1; \
	} while (0)

/* Adjusted round function for rotating state */
#define RNDr(S, W, i) \
	RND(S[(64 - i) % 8], S[(65 - i) % 8], \
	    S[(66 - i) % 8], S[(67 - i) % 8], \
	    S[(68 - i) % 8], S[(69 - i) % 8], \
	    S[(70 - i) % 8], S[(71 - i) % 8], \
	    W[i] + sha256_k[i])



static inline uint32_t swab32(uint32_t v)
{
	return __builtin_bswap32(v);
}


/* Elementary functions used by SHA256 */
#define Ch(x, y, z)     ((x & (y ^ z)) ^ z)
#define Maj(x, y, z)    ((x & (y | z)) | (y & z))
#define ROTR(x, n)      ((x >> n) | (x << (32 - n)))
#define S0(x)           (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define S1(x)           (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define s0(x)           (ROTR(x, 7) ^ ROTR(x, 18) ^ (x >> 3))
#define s1(x)           (ROTR(x, 17) ^ ROTR(x, 19) ^ (x >> 10))


/*
 * SHA256 block compression function.  The 256-bit state is transformed via
 * the 512-bit input block to produce a new state.
 */
void sha256_transform(uint32_t *state, const uint32_t *block, int swap)
{
	uint32_t W[64];
	uint32_t S[8];
	uint32_t t0, t1;
	int i;

	/* 1. Prepare message schedule W. */
	if (swap) {
		for (i = 0; i < 16; i++)
			W[i] = swab32(block[i]);
	} else
		memcpy(W, block, 64);
	for (i = 16; i < 64; i += 2) {
		W[i]   = s1(W[i - 2]) + W[i - 7] + s0(W[i - 15]) + W[i - 16];
		W[i+1] = s1(W[i - 1]) + W[i - 6] + s0(W[i - 14]) + W[i - 15];
	}

	/* 2. Initialize working variables. */
	memcpy(S, state, 32);

	/* 3. Mix. */
	RNDr(S, W,  0);
	RNDr(S, W,  1);
	RNDr(S, W,  2);
	RNDr(S, W,  3);
	RNDr(S, W,  4);
	RNDr(S, W,  5);
	RNDr(S, W,  6);
	RNDr(S, W,  7);
	RNDr(S, W,  8);
	RNDr(S, W,  9);
	RNDr(S, W, 10);
	RNDr(S, W, 11);
	RNDr(S, W, 12);
	RNDr(S, W, 13);
	RNDr(S, W, 14);
	RNDr(S, W, 15);
	RNDr(S, W, 16);
	RNDr(S, W, 17);
	RNDr(S, W, 18);
	RNDr(S, W, 19);
	RNDr(S, W, 20);
	RNDr(S, W, 21);
	RNDr(S, W, 22);
	RNDr(S, W, 23);
	RNDr(S, W, 24);
	RNDr(S, W, 25);
	RNDr(S, W, 26);
	RNDr(S, W, 27);
	RNDr(S, W, 28);
	RNDr(S, W, 29);
	RNDr(S, W, 30);
	RNDr(S, W, 31);
	RNDr(S, W, 32);
	RNDr(S, W, 33);
	RNDr(S, W, 34);
	RNDr(S, W, 35);
	RNDr(S, W, 36);
	RNDr(S, W, 37);
	RNDr(S, W, 38);
	RNDr(S, W, 39);
	RNDr(S, W, 40);
	RNDr(S, W, 41);
	RNDr(S, W, 42);
	RNDr(S, W, 43);
	RNDr(S, W, 44);
	RNDr(S, W, 45);
	RNDr(S, W, 46);
	RNDr(S, W, 47);
	RNDr(S, W, 48);
	RNDr(S, W, 49);
	RNDr(S, W, 50);
	RNDr(S, W, 51);
	RNDr(S, W, 52);
	RNDr(S, W, 53);
	RNDr(S, W, 54);
	RNDr(S, W, 55);
	RNDr(S, W, 56);
	RNDr(S, W, 57);
	RNDr(S, W, 58);
	RNDr(S, W, 59);
	RNDr(S, W, 60);
	RNDr(S, W, 61);
	RNDr(S, W, 62);
	RNDr(S, W, 63);

	/* 4. Mix local working variables into global state */
	for (i = 0; i < 8; i++)
		state[i] += S[i];
}

static const uint32_t sha256d_hash1[16] = {
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x80000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000100
};

static inline void be32enc(void *pp, uint32_t x)
{
	uint8_t *p = (uint8_t *)pp;
	p[3] = x & 0xff;
	p[2] = (x >> 8) & 0xff;
	p[1] = (x >> 16) & 0xff;
	p[0] = (x >> 24) & 0xff;
}

void sha256d(unsigned char *hash, const unsigned char *data, int len)
{
	uint32_t S[16], T[16];
	int i, r;

	sha256_init(S);
	for (r = len; r > -9; r -= 64) {
		if (r < 64)
			memset(T, 0, 64);
		memcpy(T, data + len - r, r > 64 ? 64 : (r < 0 ? 0 : r));
		if (r >= 0 && r < 64)
			((unsigned char *)T)[r] = 0x80;
		for (i = 0; i < 16; i++)
			T[i] = be32dec(T + i);
		if (r < 56)
			T[15] = 8 * len;
		sha256_transform(S, T, 0);
	}
	memcpy(S + 8, sha256d_hash1 + 8, 32);
	sha256_init(T);
	sha256_transform(T, S, 0);
	for (i = 0; i < 8; i++)
		be32enc((uint32_t *)hash + i, T[i]);
}

std::string doubleSHA256OpenSSL(const std::string& data) {
    unsigned char hash[32];
    sha256d(hash, (const unsigned char *)data.c_str(), data.length());
    return hexStr(hash,  32);
}

void JustCheck() {
    // const char* input = "hello world";
    // auto s1 = doubleSHA256(input);
    // auto s2 = doubleSHA256OpenSSL(input);
    // assert(s1 == s2);
    //2344b7a9b50f3cc2761a40722c05361f73119f4d5d6cc129da369e0db8d462bc
    //bc62d4b80d9e36da29c16c5d4d9f11731f36052c72401a76c23c0fb5a9b74423
    //bc62d4b80d9e36da29c16c5d4d9f11731f36052c72401a76c23c0fb5a9b74423
}

uint256 RxWorkMiner::sha256dKeyBlock(const CBlockHeader& block) {
    // serialize header without the nonce field
    CHashWriter keyss(PROTOCOL_VERSION);
    //change the key approximately every 345678 seconds(~4days)
    keyss << block.nVersion << block.nTime/345678 << block.nBits << uint32_t(0);
    uint256 key256 = keyss.GetHash();
    return key256;
}


bool RxWorkMiner::Mine(uint256* pHash, uint32_t* pNonce, bool (*ShutdownRequested)())
{
    LOCK(mMutex);
    const CBlockHeader& block = mBlockHeader;
    const int WIDTH = 32;
    uint32_t nonce = block.nNonce;
    unsigned char d1[80] = {0};
    WriteLE32(&d1[0], block.nVersion);
    memcpy(&d1[4], block.hashPrevBlock.begin(), 32);
    memcpy(&d1[36], block.hashMerkleRoot.begin(), 32);
    WriteLE32(&d1[68], block.nTime);
    WriteLE32(&d1[72], block.nBits);
    
    uint32_t nBits = block.nBits;
    arith_uint256 bnTarget;
    bnTarget.SetCompact(nBits, nullptr, nullptr);

    uint256 hash;
    uint8_t result[WIDTH];
    CSHA256 sha;
    uint256 input;
    arith_uint256 arith256;
    int nHash = 0;
    Stopwatch sw(true);
    do {
        if (ShutdownRequested && ShutdownRequested()) {
            LogPrintf("RxWorkMiner shutdown requested, aborting\n");
            return false;
        }
        WriteLE32(&d1[76], nonce++);
        
        randomx_calculate_hash(mVm, d1, 80, result);
        nHash++;
        if (nHash % 20000 == 0) {
            std::cout << "RxWorkMiner: time " << sw.getElapsed()*1000/20000 << " ms/hash nonce: " << nonce << std::endl;
            // LogPrintf("RxWorkMiner: time %d ms/hash nonce: %ld\n", sw.getElapsed()*1000/20000, nonce);
            fflush(stdout);
            sw.restart();
        }
        hash = HashBytesToUnit256(result);
        arith256 = UintToArith256(hash);
    } while( arith256 > bnTarget);
    *pHash = hash;
    *pNonce = nonce - 1;
    return true;
}