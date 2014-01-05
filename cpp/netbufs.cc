#include <stddef.h>
#include "netbufs++.h"
#include "slist-inl.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <cstdio>

using namespace Netbufs;
using namespace SList;

template <typename T> T* llCast(SList::Node *n)
{
    return reinterpret_cast<T*>((char *)n - offsetof(T, slnode));
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
/// Alloc Reservation                                                        ///
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
Block *
Pool::createNew(Size sz)
{
    Block *block = NULL;

    for (int ii = 0; ii < cacheblocks.size(); ii++) {
        if (!cacheblocks[ii].nalloc) {
            block = &cacheblocks[ii];
            break;
        }
    }

    if (block == NULL) {
        block = new Block();
    }

    block->nalloc = settings.basealloc;
    while (sz > block->nalloc) {
        block->nalloc *= 2;
    }

    block->root = new char[block->nalloc];
    return block;
}

bool
Pool::reserveEmpty(Alloc *alloc)
{
    Block *block = NULL;

    for (IteratorEx iter = available.beginEx(); !iter.end(); iter.inc()) {
        Block *block = llCast<Block>(iter.cur);

        if (alloc->size > block->nalloc) {
            continue;
        }

        iter.remove(&available);
        break;
    }

    if (block == NULL) {
        block = createNew(alloc->size);
        if (block == NULL) {
            return false;
        }
    }

    if (block->isStandalone()) {
        curblocks--;
    }

    alloc->parent = block;
    alloc->offset = 0;

    block->start = 0;
    block->wrap = alloc->size;
    block->cursor = alloc->size;
    block->deallocs = NULL;

    active.append(&block->slnode);
    return true;
}

bool
Pool::reserveActive(Alloc *alloc)
{
    Block *block = llCast<Block>(active.first);
    alloc->parent = block;


    if (block == NULL) {
        return false;
    }

    if (block->hasDeallocs()) {
        return false;
    }

    if (block->cursor > block->start) {
        if (block->nalloc - block->cursor >= alloc->size) {
            alloc->offset = block->cursor;
            block->cursor += alloc->size;
            block->wrap = block->cursor;
            return true;

        } else if (block->start >= alloc->size) {
            alloc->offset= 0;
            block->cursor = alloc->size;
            return true;

        } else {
            return false;
        }
    } else {
        if (block->start - block->cursor >= alloc->size) {
            alloc->offset = block->cursor;
            block->cursor += alloc->size;
            return true;
        } else {
            return false;
        }
    }
}

bool
Pool::reserve(Alloc *alloc)
{
    if (reserveActive(alloc)) {
        return true;
    }

    return reserveEmpty(alloc);

}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
/// Alloc Releases                                                           ///
////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
void
Pool::release(Alloc *alloc)
{
    // Find the block
    Block *block = alloc->parent;

    if (alloc->offset == block->start) {
        block->start += alloc->size;

        if (block->hasDeallocs()) {
            block->applyDeallocs(block->start);
        }

        if (block->empty() == false && block->start == block->wrap) {
            block->wrap = block->cursor;
            block->start = 0;
        }
    } else if (alloc->offset + alloc->size == block->cursor) {
        if (block->cursor == block->wrap) {
            block->cursor -= alloc->size;
            block->wrap -= alloc->size;
        } else {
            block->cursor -= alloc->size;
            if (!block->cursor) {
                block->cursor = block->wrap;
            }
        }
    } else {
        if (block->deallocs == NULL) {
            block->deallocs = new DeallocQueue(deaSettings);
        }
        block->queueDealloc(alloc);
    }

    if (block->empty()) {
        relocateEmpty(block);
    }
}

void
Pool::relocateEmpty(Block *block)
{
    assert(active.remove(&block->slnode));
    if (!block->isStandalone()) {
        available.append(&block->slnode);

    } else if(curblocks < max_alloc_blocks) {
        curblocks++;
        available.append(&block->slnode);

    } else {
        delete block;
    }

}

void
Pool::release(char *ptr, size_t len)
{
    // Find the corresponding block
    for (Iterator iter = active.begin(); !iter.end(); iter.inc()) {
        Block *block = llCast<Block>(iter.cur);
        if (!block->isOwnerOf(ptr, len)) {
            continue;
        }
        Size offset = ptr - block->root;
        Alloc alloc(block, offset, len);
        release(&alloc);
        return;
    }

    abort();
}

bool
Block::hasDeallocs() const
{
    return deallocs != NULL && deallocs->ll.empty() == false;
}

void
Block::queueDealloc(const Alloc *alloc)
{
    Alloc infoAlloc;
    DeallocInfo *info;
    infoAlloc.size = sizeof(*info);
    deallocs->qpool.reserve(&infoAlloc);
    info = reinterpret_cast<DeallocInfo *>(infoAlloc.getBuffer());
    info->size = alloc->size;
    info->offset = alloc->offset;
    if (deallocs->minoffset > info->offset) {
        deallocs->minoffset = info->offset;
    }
    deallocs->ll.append(&info->slnode);
}

void
Block::applyDeallocs(Size curstart)
{
    Size min_next = -1;
    for (IteratorEx iter = deallocs->ll.beginEx(); !iter.end(); iter.inc()) {
        DeallocInfo *info = llCast<DeallocInfo>(iter.cur);
        if (info->offset == curstart) {
            iter.remove(&deallocs->ll);
            deallocs->qpool.release((char *)info, sizeof(*info));
        } else {
            min_next = std::min(min_next, info->offset);
        }
    }
    deallocs->minoffset = min_next;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
/// Send Queue                                                               ///
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
Size
Block::getNextSize(bool allow_wrap) const
{
    if (empty()) {
        return 0;
    }
    if (hasDeallocs()) {
        return 0;
    }

    if (start == 0) {
        // Plain old buffer
        return nalloc - cursor;
    }

    if (cursor != wrap) {
        // Already in second region
        return start - cursor;
    }

    if (allow_wrap) {
        return std::min(nalloc - wrap, start);
    }

    return nalloc - wrap;
}

Size
SendQueue::startFlush(IOVector *iov, int niov)
{
    Size ret = 0;
    IOVector *iov_end = iov + (niov + 1);
    SendItem *win = NULL;
    SList::Node *ll;

    if (last_requested != NULL) {
        if (last_offset != last_requested->len) {
            win = last_requested;
            assert(win->len > last_offset);

            iov->assign(win->base + last_offset, win->len - last_offset);
            ret += iov->length;
            iov++;
        }
        ll = &last_requested->slnode;

    } else {
        ll = pending.first;
    }

    while (ll && iov != iov_end) {
        win = llCast<SendItem>(ll);
        iov->assign(win->base, win->len);
        ret += win->len;
        iov++;
        ll = ll->next;
    }

    if (win) {
        last_requested = win;
        last_offset = win->len;
    }

    return ret;
}

void
SendQueue::endFlush(Size nflushed)
{
    for (IteratorEx iter = pending.beginEx(); !iter.end(); iter++) {

        SendItem *elem = llCast<SendItem>(iter.cur);
        Size to_chop = std::min(elem->len, nflushed);
        elem->len -= to_chop;
        nflushed -= to_chop;

        if (elem == last_requested) {
            last_requested = NULL;
            last_offset = 0;
        }

        if (elem->len == 0) {
            iter.remove(&pending);
            elempool.release((char *)elem, sizeof(*elem));
        } else {
            elem->base += to_chop;
        }

        if (nflushed == 0) {
            break;
        }
    }
}

void
SendQueue::allocSendInfo(const IOVector *iov)
{
    Alloc siAlloc(sizeof(SendItem));
    elempool.reserve(&siAlloc);

    SendItem *curitem = (SendItem *)siAlloc.getBuffer();
    curitem->base = (char *)iov->base;
    curitem->len = iov->length;

    pending.append(&curitem->slnode);
}

void
SendQueue::enqueue(const IOVector *iov)
{
    SendItem *curitem;
    assert(iov->length);

    if (pending.empty()) {
        allocSendInfo(iov);
        return;
    }

    curitem = llCast<SendItem>(pending.last);
    if (curitem->base + curitem->len == iov->base) {
        curitem->len += iov->length;
    } else {
        allocSendInfo(iov);
    }
}
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
/// Constructors/Destructors                                                 ///
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void Pool::init()
{
    cacheblocks.reserve(settings.cacheblocks);
    for (int ii = 0; ii < settings.cacheblocks; ii++) {
        cacheblocks.push_back(Block());
    }

    for (int ii = 0; ii < cacheblocks.size(); ii++) {
        memset(&cacheblocks[ii], 0, sizeof(Block));
        cacheblocks[ii].parent = this;
    }

    max_alloc_blocks = settings.cacheblocks;
}

Block::~Block()
{
    if (root) {
        delete[] root;
    }

    if (deallocs) {
        delete deallocs;
    }
}

DeallocQueue::~DeallocQueue()
{
    for (Iterator iter = ll.begin(); !iter.end(); iter.inc()) {
        DeallocInfo *info = llCast<DeallocInfo>(iter.cur);
        qpool.release((char *)info, sizeof(*info));
    }
}

void
Pool::freeBlocklist(List& ll)
{
    for (IteratorEx iter = ll.beginEx(); !iter.end(); iter++) {
        Block* block = llCast<Block>(iter.cur);

        if (block->isStandalone()) {
            iter.remove(&ll);
            delete block;
        }
    }
}

Pool::~Pool()
{
    freeBlocklist(available);
    freeBlocklist(active);
}

Manager::Manager(const Settings* stgs)
{
    if (stgs == NULL) {
        defaultSettings(&settings);
    } else {
        settings = *stgs;
    }

    sendq.elempool.settings = settings.sendq;
    sendq.elempool.init();

    datapool.settings = settings.data;
    datapool.init();
}


void
Manager::dump() const
{
    const Pool* dp = &datapool;
}
