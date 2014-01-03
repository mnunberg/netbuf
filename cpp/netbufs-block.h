#ifndef NETBUFS_BLOCK_H
#define NETBUFS_BLOCK_H

#include "netbufs-defs.h"
#include <vector>
#include <cstdlib>
#include <stddef.h>

namespace Netbufs {

template <typename T> class SList {
    T* first;
    T* last;

    struct Node {
        Node *next;

        T * getItem() const {
            return (T *) (char *)this + sizeof(*this);
        }
    };

    struct Iterator {
        T* cur;
        T* prev;
        T* next;
        SList *parent;

        Iterator(SList *parent) : parent(parent), cur(parent->first), prev(NULL) {
            if (cur) {
                next = cur->next;
            }
        }

        bool isEnd() const {
            return cur == NULL;
        }

        void start() {

        }

        void operator++() {
            if (prev != NULL) {
                prev = prev->next;
            } else {
                prev = parent->first;
            }
            cur = next;
            if (cur != NULL) {
                next = cur->next;
            } else {
                next = NULL;
            }
        }
    };

    bool empty() const {
        return first == NULL && last == NULL;
    }

    void append(const T *item) {
        if (empty()) {
            first = last = item;
        } else {
            last->next = item;
            last = item;
        }
    }

    void prepend(const T *item) {
        if (empty()) {
            first = last = item;
        } else {
            item->next = first;
            first = item;
        }
    }
};

struct DeallocInfo;

struct DeallocInfo {
    SList<DeallocInfo>::Node next;
    Size offset;
    Size size;
};

struct Block {
    SList<Block>::Node next;
    Size start;
    Size wrap;
    Size cursor;
    char* root;
    DeallocQueue *deallocs;
};


struct Pool {
    SList<Block> active;
    SList<Block> available;
    Size basealloc;
    Size maxblocks;
    Size curblocks;
    std::vector<Block> cacheblocks;

    bool reserve(Alloc *alloc);
    void release(Alloc *alloc);
    Manager *mgr;
};

struct DeallocQueue {
    SList<DeallocInfo> ll;
    Size minoffset;
    Pool qpool;
};

}

#endif
