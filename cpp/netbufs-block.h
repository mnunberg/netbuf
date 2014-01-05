#ifndef NETBUFS_BLOCK_H
#define NETBUFS_BLOCK_H

#include "netbufs-defs.h"
#include <vector>
#include <cstdlib>
#include <stddef.h>

namespace Netbufs {

struct DeallocInfo;
struct DeallocQueue;
struct Manager;
struct Pool;

struct DeallocInfo {
    SList::Node slnode;
    Size offset;
    Size size;
};

struct Block {
    SList::Node slnode;
    Size start;
    Size wrap;
    Size cursor;
    Size nalloc;
    char* root;
    DeallocQueue *deallocs;
    Pool *parent;

    Block() : start(0),
            wrap(0),
            cursor(0),
            nalloc(0),
            root(NULL),
            deallocs(NULL), parent(NULL) {}

    inline void applyDeallocs(Size curstart);
    inline void queueDealloc(const Alloc *alloc);
    inline Size getNextSize(bool allow_wrap=true) const;

    bool isStandalone() const {
        return parent == NULL;
    }

    inline bool hasDeallocs() const;

    bool isOwnerOf(char *ptr, size_t len) const {
        if (ptr < root) {
            return false;
        }
        if (ptr > root + nalloc) {
            return false;
        }
        return true;
    }

    bool empty() const {
        return start == cursor;
    }
    ~Block();
};

struct Pool {
    SList::List active;
    SList::List available;
    Size max_alloc_blocks;
    Size curblocks;
    std::vector<Block> cacheblocks;

    bool reserve(Alloc *alloc);
    void release(Alloc *alloc);
    inline void release(char *ptr, size_t len);

    inline Block* createNew(Size);
    inline void relocateEmpty(Block *bloc);
    inline bool reserveEmpty(Alloc *);
    inline bool reserveActive(Alloc *);

    AllocationSettings settings;
    AllocationSettings deaSettings;

    ~Pool();
    Pool() : max_alloc_blocks(64), curblocks(0) {}

    // Initializes the pool to its default settings.
    void init();

    inline void freeBlocklist(SList::List &ll);
};

struct DeallocQueue {
    SList::List ll;
    Size minoffset;
    Pool qpool;
    DeallocQueue(const AllocationSettings& settings)
        : minoffset(0) {}

    ~DeallocQueue();
};

}

#endif
