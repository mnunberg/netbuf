#ifndef NETBUFS_DEFS_H
#define NETBUFS_DEFS_H
#include <cstdlib>
#include "slist.h"

namespace Netbufs {

/**
 * The following settings control the default allocation policy.
 * Each allocator pool has both blocks and the amount of data per block.
 *
 * Multiple blocks help with cache locality when traversing, while large
 * data segements allow each individual element to be spaced near the next.
 */

/** How many blocks to preallocate for SNDQ elements, per manager */
#define NB_SNDQ_CACHEBLOCKS 4
/** How many SNDQELEM structures per block */
#define NB_SNDQ_BASEALLOC 128


/** How many dealloc blocks to allocated per MBLOCK */
#define NB_MBDEALLOC_CACHEBLOCKS 0
/** Number of dealloc structures per block */
#define NB_MBDEALLOC_BASEALLOC 24


/** How many data blocks to allocate per manager */
#define NB_DATA_CACHEBLOCKS 16

/** Default data allocation size */
#define NB_DATA_BASEALLOC 32768

struct Manager;
struct Alloc;

typedef unsigned int Size;

struct AllocationSettings {
    Size cacheblocks;
    Size basealloc;
};

struct Settings {
    AllocationSettings sendq;
    AllocationSettings dealloc;
    AllocationSettings data;
};

struct IOVector {
    void *base;
    Size length;

    void assign(void *base, Size length) {
        this->base = base;
        this->length = length;
    }

    IOVector() : base(NULL), length(0) { }
};

}

#endif /* NETBUFS_DEFS_H */
