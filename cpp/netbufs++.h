#ifndef NETBUFSPP_H
#define NETBUFSPP_H

#include "netbufs-defs.h"
#include "netbufs-block.h"

namespace Netbufs {

struct Block;
typedef unsigned int Size;
#define NETBUFS_INVALID_OFFSET (Size)-1

struct Alloc {
    Block *parent;
    Size offset;
    Size length;

    void *getBuffer() const {
        if (offset == NETBUFS_INVALID_OFFSET) {
            return parent;
        } else {
            return parent->root + offset;
        }
    }
};

struct SendItem {
    SList<SendItem>::Node next;
    char *base;
    Size len;
};

struct SendQueue {
    SList<SendItem> pending;
    SendItem *last_requested;
    Size last_offset;
    Pool elempool;
};

struct Manager {
    SendQueue sendq;
    Pool datapool;
    Settings settings;
    unsigned int total_allocs;
    unsigned int total_bytes;

    bool reserve(Alloc *);
    bool release(Alloc *);
    void enqueue(const IOVector *);
    void enqueue(const Alloc *);
    Size getIOVCount() const;
    Size startFlush(IOVector *, int);
    void endFlush(Size);
    Size getLength();
    Size getNextSize();

    static void defaultSettings(Settings *out);
    void dump();

    Manager();
    ~Manager();
};

}

#endif
