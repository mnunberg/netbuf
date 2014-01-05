#ifndef NETBUFSPP_H
#define NETBUFSPP_H

#include "netbufs-defs.h"
#include "netbufs-block.h"

namespace Netbufs {

struct Block;
typedef unsigned int Size;
#define NETBUFS_INVALID_OFFSET (Size)-1

struct Alloc {
    // The parent block
    Block *parent;

    // The offset into the block at which the data begins. If this block is
    // standalone, then the offset is set to NETBUFS_INVALID_OFFSET
    Size offset;

    // The size of the data
    Size size;

    // Get the buffer to the data segment
    void *getBuffer() const {
        if (offset == NETBUFS_INVALID_OFFSET) {
            return parent;
        } else {
            return parent->root + offset;
        }
    }

    // Create a new empty Alloc suitable for being populated later on
    Alloc() : parent(NULL), offset(0), size(0) {}

    Alloc(Size sz) {
        size = sz;
    }

    Size getSize() const { return size; }

    // Create an alllocation tied to a block
    Alloc(Block *blk, Size off, Size len) : parent(blk), offset(off), size(len) {}

    // Create a standalone allocation segement
    Alloc(char *buf, Size length) :
        parent((Block *)buf), offset(NETBUFS_INVALID_OFFSET), size(length) {}
};

// Item within the send queue
struct SendItem {
    // The next item in the list
    SList::SListNode slnode;
    // The buffer
    char *base;
    // Length of the buffer
    Size len;
};


typedef SList::SList<SendItem, &SendItem::slnode> SendList;
typedef SendList::FastIterator SendFIter;


// Queue of items to send
struct SendQueue {
    inline void allocSendInfo(const IOVector *iov);
    void enqueue(const IOVector *iov);

    // See Manager::startFlush()
    Size startFlush(IOVector *, int);

    // See Manager::endFlush()
    void endFlush(Size nflushed);

    // See Manager::getIOVCount()
    Size getIOVCount() const;


    SendQueue() : last_requested(NULL), last_offset(0) {}

    SendList pending;
    SendItem *last_requested;
    Size last_offset;
    Pool elempool;
};

struct Manager {
    /**
     * Reserve a contiguous region of memory, in-order for a given Alloc. The
     * Alloc will be reserved from the last block to be flushed to the network.
     *
     * The contents of the Alloc are guaranteed to be contiguous
     * (though not aligned) and are available via .getBuffer() member function
     *
     * @param alloc the target Alloc to hold the allocation information.
     *
     * @return true if successful, false on error
     */
    bool reserve(Alloc *alloc) {
        return datapool.reserve(alloc);
    }

    /**
     * Release an Alloc previously allocated via reserve. It is assumed that the
     * contents of the span have either:
     *
     * (1) been successfully sent to the network
     * (2) have just been scheduled (and are being removed due to error handling)
     * (3) have been partially sent to a connection which is being closed.
     *
     * @param alloc the Alloc to release
     */
    void release(Alloc *alloc) {
        datapool.release(alloc);
    }

    /**
     * Schedules an IOV to be placed inside the send queue. The storage of the
     * underlying buffer must not be freed or otherwise modified until it has
     * been sent.
     *
     * With the current usage model, flush status is implicitly completed once
     * a response has arrived.
     */
    void enqueue(const IOVector *iov) {
        sendq.enqueue(iov);
    }

    void enqueue(const Alloc *alloc) {
        IOVector iov;
        iov.assign(alloc->getBuffer(), alloc->size);
        enqueue(&iov);
    }

    Size getIOVCount() const {
        return sendq.getIOVCount();
    }

    Size startFlush(IOVector * iov, int niov) {
        return sendq.startFlush(iov, niov);
    }

    void endFlush(Size nflushed) {
        sendq.endFlush(nflushed);
    }

    Size getLength() const;
    Size getNextSize() const;

    static void defaultSettings(Settings *out) {
        out->data.basealloc = NB_DATA_BASEALLOC;
        out->data.cacheblocks = NB_DATA_CACHEBLOCKS;
        out->dealloc.basealloc = NB_MBDEALLOC_BASEALLOC;
        out->dealloc.cacheblocks = NB_MBDEALLOC_CACHEBLOCKS;
        out->sendq.basealloc = NB_SNDQ_BASEALLOC;
        out->sendq.cacheblocks = NB_SNDQ_CACHEBLOCKS;
    }

    void dump() const;

    Manager(const Settings* settings = NULL);


    SendQueue sendq;
    Pool datapool;
    Settings settings;

    unsigned int total_allocs;
    unsigned int total_bytes;
};

}

#endif
