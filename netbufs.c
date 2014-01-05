#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "netbufs.h"
#include "slist-inl.h"

#ifndef lcb_assert
#include <assert.h>
#define lcb_assert assert
#endif

/******************************************************************************
 ******************************************************************************
 ** Handy Macros                                                             **
 ******************************************************************************
 ******************************************************************************/
#define MINIMUM(a, b) a < b ? a : b
#define MAXIMUM(a, b) a > b ? a : b

#define BLOCK_IS_EMPTY(block) ((block)->start == (block)->cursor)

#define FIRST_BLOCK(pool) \
    (SLIST_ITEM((pool)->active.first, nb_MBLOCK, slnode))

#define LAST_BLOCK(mgr) \
    (SLIST_ITEM((mgr)->active_blocks.last, nb_BLOCKHDR, slnode))

#define NEXT_BLOCK(block) \
    (SLIST_ITEM((block)->slnode.next, nb_BLOCKHDR, slnode))

#define BLOCK_HAS_DEALLOCS(block) \
    ((block)->deallocs && SLIST_IS_EMPTY(&(block)->deallocs->pending))


#define MALLOC_WITH_STATS(p, size, mgr) \
    p = malloc(size); \
    mgr->total_allocs++; \
    mgr->total_bytes += size;

#define CALLOC_WITH_STATS(p, n, elemsz, mgr) \
    p = calloc(n, elemsz); \
    mgr->total_allocs++; \
    mgr->total_bytes += (n) * (elemsz);

/** Static forward decls */
static void mblock_release_data(nb_MBPOOL*,nb_MBLOCK*,nb_SIZE,nb_SIZE);
static void mblock_release_ptr(nb_MBPOOL*,char*,nb_SIZE);
static void mblock_init(nb_MBPOOL*);
static void mblock_cleanup(nb_MBPOOL*);

/******************************************************************************
 ******************************************************************************
 ** Allocation/Reservation                                                   **
 ******************************************************************************
 ******************************************************************************/

/**
 * Determines whether the block is allocated as a standalone block, or if it's
 * part of a larger allocation
 */
static int
mblock_is_standalone(nb_MBLOCK *block)
{
    return block->parent == NULL;
}

/**
 * Allocates a new block with at least the given capacity and places it
 * inside the active list.
 */
static nb_MBLOCK*
alloc_new_block(nb_MBPOOL *pool, nb_SIZE capacity)
{
    unsigned int ii;
    nb_MBLOCK *ret = NULL;

    for (ii = 0; ii < pool->ncacheblocks; ii++) {
        if (!pool->cacheblocks[ii].nalloc) {
            ret = pool->cacheblocks + ii;
            break;
        }
    }

    if (!ret) {
        CALLOC_WITH_STATS(ret, 1, sizeof(*ret), pool->mgr);
    }

    if (!ret) {
        return NULL;
    }

    ret->nalloc = pool->basealloc;

    while (ret->nalloc < capacity) {
        ret->nalloc *= 2;
    }

    ret->wrap = 0;
    ret->cursor = 0;
    MALLOC_WITH_STATS(ret->root, ret->nalloc, pool->mgr);

    if (!ret->root) {
        if (mblock_is_standalone(ret)) {
            free(ret);
        }
        return NULL;
    }

    return ret;
}

/**
 * Finds an available block within the available list. The block will have
 * room for at least capacity bytes.
 */
static nb_MBLOCK*
find_free_block(nb_MBPOOL *pool, nb_SIZE capacity)
{
    slist_iterator iter;
    SLIST_ITERFOR(&pool->avail, &iter) {
        nb_MBLOCK *cur = SLIST_ITEM(iter.cur, nb_MBLOCK, slnode);
        if (cur->nalloc >= capacity) {
            slist_iter_remove(&pool->avail, &iter);

            if (mblock_is_standalone(cur)) {
                pool->curblocks--;
            }

            return cur;
        }
    }

    return NULL;
}

/**
 * Find a new block for the given span and initialize it for a reserved size
 * correlating to the span.
 * The block may either be popped from the available section or allocated
 * as a standalone depending on current constraints.
 */
static int
reserve_empty_block(nb_MBPOOL *pool, nb_SPAN *span)
{
    nb_MBLOCK *block;

    if ( (block = find_free_block(pool, span->size)) == NULL) {
        block = alloc_new_block(pool, span->size);
    }

    if (!block) {
        return -1;
    }

    span->parent = block;
    span->offset = 0;
    block->start = 0;
    block->wrap = span->size;
    block->cursor = span->size;

    block->deallocs = NULL;

    slist_append(&pool->active, &block->slnode);
    return 0;
}

/**
 * Attempt to reserve space from the currently active block for the given
 * span.
 * @return 0 if the active block had enough space and the span was initialized
 * and nonzero otherwise.
 */
static int
reserve_active_block(nb_MBLOCK *block, nb_SPAN *span)
{
    if (BLOCK_HAS_DEALLOCS(block)) {
        return -1;
    }

    if (block->cursor > block->start) {
        if (block->nalloc - block->cursor >= span->size) {
            span->offset = block->cursor;
            block->cursor += span->size;
            block->wrap = block->cursor;
            return 0;

        } else if (block->start >= span->size) {
            /** Wrap around the wrap */
            span->offset = 0;
            block->cursor = span->size;
            return 0;
        } else {
            return -1;
        }

    } else {
        /* Already wrapped */
        if (block->start - block->cursor >= span->size) {
            span->offset = block->cursor;
            block->cursor += span->size;
            return 0;
        } else {
            return -1;
        }
    }
}

static int
mblock_reserve_data(nb_MBPOOL *pool, nb_SPAN *span)
{
    nb_MBLOCK *block;
    int rv;

#ifdef NETBUFS_LIBC_PROXY
    block = malloc(sizeof(*block) + span->size);
    block->root = ((char *)block) + sizeof(*block);
    span->parent = block;
    span->offset = 0;
    return 0;
#endif

    if (SLIST_IS_EMPTY(&pool->active)) {
        return reserve_empty_block(pool, span);

    } else {
        block = SLIST_ITEM(pool->active.last, nb_MBLOCK, slnode);
        rv = reserve_active_block(block, span);

        if (rv != 0) {
            return reserve_empty_block(pool, span);
        }

        span->parent = block;
        return rv;
    }
}

/******************************************************************************
 ******************************************************************************
 ** Out-Of-Order Deallocation Functions                                      **
 ******************************************************************************
 ******************************************************************************/
static void
ooo_queue_dealoc(nb_MGR *mgr, nb_MBLOCK *block, nb_SPAN *span)
{
    nb_QDEALLOC *qd;
    nb_DEALLOC_QUEUE *queue;
    nb_SPAN qespan;

    if (!block->deallocs) {
        CALLOC_WITH_STATS(queue, 1, sizeof(*queue), mgr);
        queue->qpool.basealloc = sizeof(*qd) * mgr->settings.dea_basealloc;
        queue->qpool.ncacheblocks = mgr->settings.dea_cacheblocks;
        queue->qpool.mgr = mgr;

        mblock_init(&queue->qpool);
        block->deallocs = queue;
    }

    queue = block->deallocs;
    qespan.size = sizeof(*qd);
    mblock_reserve_data(&queue->qpool, &qespan);

    qd = (nb_QDEALLOC *)SPAN_BUFFER(&qespan);
    qd->offset = span->offset;
    qd->size = span->size;
    if (queue->min_offset > qd->offset) {
        queue->min_offset = qd->offset;
    }
    slist_append(&queue->pending, &qd->slnode);
}

static void
ooo_apply_dealloc(nb_MBLOCK *block, nb_SIZE offset)
{
    nb_SIZE min_next = -1;
    slist_iterator iter;
    nb_DEALLOC_QUEUE *queue = block->deallocs;

    SLIST_ITERFOR(&queue->pending, &iter) {
        nb_QDEALLOC *cur = SLIST_ITEM(iter.cur, nb_QDEALLOC, slnode);
        if (cur->offset == offset) {
            slist_iter_remove(&block->deallocs->pending, &iter);
            mblock_release_ptr(&queue->qpool, (char *)cur, sizeof(*cur));
        } else if (cur->offset < min_next) {
            min_next = cur->offset;
        }
    }
    queue->min_offset = min_next;
}


static void
mblock_release_data(nb_MBPOOL *pool,
                    nb_MBLOCK *block, nb_SIZE size, nb_SIZE offset)
{
    if (offset == block->start) {
        /** Removing from the beginning */
        block->start += size;

        if (block->deallocs && block->deallocs->min_offset == block->start) {
            ooo_apply_dealloc(block, block->start);
        }

        if (!BLOCK_IS_EMPTY(block) && block->start == block->wrap) {
            block->wrap = block->cursor;
            block->start = 0;
        }

    } else if (offset + size == block->cursor) {
        /** Removing from the end */
        if (block->cursor == block->wrap) {
            /** Single region, no wrap */
            block->cursor -= size;
            block->wrap -= size;

        } else {
            block->cursor -= size;
            if (!block->cursor) {
                /** End has reached around */
                block->cursor = block->wrap;
            }
        }

    } else {
        nb_SPAN span = { block, offset, size };
        ooo_queue_dealoc(pool->mgr, block, &span);
        return;
    }

    if (!BLOCK_IS_EMPTY(block)) {
        return;
    }

    {
        slist_iterator iter;
        SLIST_ITERFOR(&pool->active, &iter) {
            if (&block->slnode == iter.cur) {
                slist_iter_remove(&pool->active, &iter);
                break;
            }
        }
    }

    if (pool->curblocks < pool->maxblocks) {
        slist_append(&pool->avail, &block->slnode);
        pool->curblocks++;

    } else {
        free(block->root);
        pool->mgr->total_bytes -= block->nalloc;
        block->root = NULL;
        if (mblock_is_standalone(block)) {
            pool->mgr->total_bytes -= sizeof(*block);
            free(block);
        }
    }
}

static void
mblock_release_ptr(nb_MBPOOL *pool, char * ptr, nb_SIZE size)
{
    nb_MBLOCK *block;
    nb_SIZE offset;
    slist_node *ll;

#ifdef NETBUFS_LIBC_PROXY
    block = ptr - sizeof(*block);
    free(block);
    return;
#endif


    SLIST_FOREACH(&pool->active, ll) {
        block = SLIST_ITEM(ll, nb_MBLOCK, slnode);
        if (block->root > ptr) {
            continue;
        }
        if (block->root + block->nalloc <= ptr) {
            continue;
        }
        offset = ptr - block->root;
        mblock_release_data(pool, block, size, offset);
        return;
    }

    abort();
}

static int
mblock_get_next_size(const nb_MBPOOL *pool, int allow_wrap)
{
    if (SLIST_IS_EMPTY(&pool->avail)) {
        return 0;
    }

    nb_MBLOCK *block = FIRST_BLOCK(pool);

    if (BLOCK_HAS_DEALLOCS(block)) {
        return 0;
    }

    if (!block->start) {
        /** Plain 'ole buffer */
        return block->nalloc - block->cursor;
    }

    if (block->cursor != block->wrap) {
        /** Already in second region */
        return block->start - block->cursor;
    }

    if (allow_wrap) {
        return MINIMUM(block->nalloc - block->wrap, block->start);
    }

    return block->nalloc - block->wrap;
}

static void
free_blocklist(nb_MBPOOL *pool, slist_root *list)
{
    slist_iterator iter;
    SLIST_ITERFOR(list, &iter) {
        nb_MBLOCK *block = SLIST_ITEM(iter.cur, nb_MBLOCK, slnode);

        if (block->root) {
            free(block->root);
            pool->mgr->total_bytes -= block->nalloc;
        }

        if (block->deallocs) {
            slist_iterator dea_iter;
            nb_DEALLOC_QUEUE *queue = block->deallocs;

            SLIST_ITERFOR(&queue->pending, &dea_iter) {
                nb_QDEALLOC *qd = SLIST_ITEM(dea_iter.cur, nb_QDEALLOC, slnode);
                mblock_release_ptr(&queue->qpool, (char *)qd, sizeof(*qd));
            }

            mblock_cleanup(&queue->qpool);
            free(queue);
            block->deallocs = NULL;
            pool->mgr->total_bytes -= sizeof(*block->deallocs);
        }

        if (mblock_is_standalone(block)) {
            pool->mgr->total_bytes -= sizeof(*block);
            free(block);
        }
    }
}


static void
mblock_cleanup(nb_MBPOOL *pool)
{
    free_blocklist(pool, &pool->active);
    free_blocklist(pool, &pool->avail);
    free(pool->cacheblocks);
    pool->mgr->total_bytes -= sizeof(*pool->cacheblocks) * pool->ncacheblocks;
}

static void
mblock_init(nb_MBPOOL *pool)
{
    unsigned int ii;
    pool->cacheblocks = calloc(pool->ncacheblocks, sizeof(*pool->cacheblocks));
    for (ii = 0; ii < pool->ncacheblocks; ii++) {
        pool->cacheblocks[ii].parent = pool;
    }
}

int
netbuf_mblock_reserve(nb_MGR *mgr, nb_SPAN *span)
{
    return mblock_reserve_data(&mgr->datapool, span);
}

/******************************************************************************
 ******************************************************************************
 ** Informational Routines                                                   **
 ******************************************************************************
 ******************************************************************************/
nb_SIZE
netbuf_mblock_get_next_size(const nb_MGR *mgr, int allow_wrap)
{
    return mblock_get_next_size(&mgr->datapool, allow_wrap);
}

unsigned int
netbuf_get_niov(nb_MGR *mgr)
{
    slist_node *ll;
    unsigned int ret = 0;
    SLIST_FOREACH(&mgr->sendq.pending, ll) {
        ret++;
    }

    return ret;
}

/******************************************************************************
 ******************************************************************************
 ** Flush Routines                                                           **
 ******************************************************************************
 ******************************************************************************/
static nb_SNDQELEM *
get_sendqe(nb_SENDQ* sq, const nb_IOV *bufinfo)
{
    nb_SNDQELEM *sndqe;
    nb_SPAN span;
    span.size = sizeof(*sndqe);
    mblock_reserve_data(&sq->elempool, &span);
    sndqe = (nb_SNDQELEM *)SPAN_BUFFER(&span);

    sndqe->base = bufinfo->iov_base;
    sndqe->len = bufinfo->iov_len;
    return sndqe;
}

void
netbuf_enqueue(nb_MGR *mgr, const nb_IOV *bufinfo)
{
    nb_SENDQ *q = &mgr->sendq;
    nb_SNDQELEM *win;

    if (SLIST_IS_EMPTY(&q->pending)) {
        win = get_sendqe(q, bufinfo);
        slist_append(&q->pending, &win->slnode);

    } else {
        win = SLIST_ITEM(q->pending.last, nb_SNDQELEM, slnode);
        if (win->base + win->len == bufinfo->iov_base) {
            win->len += bufinfo->iov_len;

        } else {
            win = get_sendqe(q, bufinfo);
            slist_append(&q->pending, &win->slnode);
        }
    }
}

void
netbuf_enqueue_span(nb_MGR *mgr, nb_SPAN *span)
{
    nb_IOV spinfo = { SPAN_BUFFER(span), span->size };
    netbuf_enqueue(mgr, &spinfo);
}

nb_SIZE
netbuf_start_flush(nb_MGR *mgr, nb_IOV *iovs, int niov)
{
    nb_SIZE ret = 0;
    nb_IOV *iov_end = iovs + niov + 1;
    nb_IOV *iov = iovs;
    slist_node *ll;
    nb_SENDQ *sq = &mgr->sendq;
    nb_SNDQELEM *win = NULL;

    if (sq->last_requested) {
        if (sq->last_offset != sq->last_requested->len) {
            win = sq->last_requested;
            assert(win->len > sq->last_offset);

            iov->iov_len = win->len - sq->last_offset;
            iov->iov_base = win->base + sq->last_offset;
            ret += iov->iov_len;
            iov++;
        }

        ll = sq->last_requested->slnode.next;

    } else {
        ll = sq->pending.first;
    }

    while (ll && iov != iov_end) {
        win = SLIST_ITEM(ll, nb_SNDQELEM, slnode);
        iov->iov_len = win->len;
        iov->iov_base = win->base;

        ret += iov->iov_len;
        iov++;
        ll = ll->next;
    }

    if (win) {
        sq->last_requested = win;
        sq->last_offset = win->len;
    }

    return ret;
}

void
netbuf_end_flush(nb_MGR *mgr, unsigned int nflushed)
{
    nb_SENDQ *q = &mgr->sendq;
    slist_iterator iter;
    SLIST_ITERFOR(&q->pending, &iter) {
        nb_SNDQELEM *win = SLIST_ITEM(iter.cur, nb_SNDQELEM, slnode);
        nb_SIZE to_chop = MINIMUM(win->len, nflushed);

        win->len -= to_chop;
        nflushed -= to_chop;
        if (win == q->last_requested) {
            q->last_requested = NULL;
            q->last_offset = 0;
        }

        if (!win->len) {
            slist_iter_remove(&q->pending, &iter);
            mblock_release_ptr(&mgr->sendq.elempool, (char *)win, sizeof(*win));

        } else {
            win->base +=  to_chop;
        }

        if (!nflushed) {
            break;
        }
    }
}


/******************************************************************************
 ******************************************************************************
 ** Release                                                                  **
 ******************************************************************************
 ******************************************************************************/
void
netbuf_mblock_release(nb_MGR *mgr, nb_SPAN *span)
{
#ifdef NETBUFS_LIBC_PROXY
    free(span->parent);
#else
    mblock_release_data(&mgr->datapool, span->parent, span->size, span->offset);
#endif
}

/******************************************************************************
 ******************************************************************************
 ** Init/Cleanup                                                             **
 ******************************************************************************
 ******************************************************************************/
void netbuf_default_settings(nb_SETTINGS *settings)
{
    settings->data_basealloc = NB_DATA_BASEALLOC;
    settings->data_cacheblocks = NB_DATA_CACHEBLOCKS;
    settings->dea_basealloc = NB_MBDEALLOC_BASEALLOC;
    settings->dea_cacheblocks = NB_MBDEALLOC_CACHEBLOCKS;
    settings->sndq_basealloc = NB_SNDQ_BASEALLOC;
    settings->sndq_cacheblocks = NB_SNDQ_CACHEBLOCKS;
}

void
netbuf_init(nb_MGR *mgr, const nb_SETTINGS *user_settings)
{
    memset(mgr, 0, sizeof(*mgr));
    nb_MBPOOL *sqpool = &mgr->sendq.elempool;
    nb_MBPOOL *bufpool = &mgr->datapool;

    if (user_settings) {
        mgr->settings = *user_settings;
    } else {
        netbuf_default_settings(&mgr->settings);
    }

    /** Set our defaults */
    sqpool->basealloc = sizeof(nb_SNDQELEM) * mgr->settings.sndq_basealloc;
    sqpool->ncacheblocks = mgr->settings.sndq_cacheblocks;
    sqpool->mgr = mgr;
    mblock_init(sqpool);

    bufpool->basealloc = mgr->settings.data_basealloc;
    bufpool->ncacheblocks = mgr->settings.data_cacheblocks;
    bufpool->mgr = mgr;
    mblock_init(bufpool);
}


void
netbuf_cleanup(nb_MGR *mgr)
{
    slist_iterator iter;

    SLIST_ITERFOR(&mgr->sendq.pending, &iter) {
        nb_SNDQELEM *e = SLIST_ITEM(iter.cur, nb_SNDQELEM, slnode);
        slist_iter_remove(&mgr->sendq.pending, &iter);
        mblock_release_ptr(&mgr->sendq.elempool, (char *)e, sizeof(*e));
    }

    mblock_cleanup(&mgr->sendq.elempool);
    mblock_cleanup(&mgr->datapool);
}

/******************************************************************************
 ******************************************************************************
 ** Block Dumping                                                            **
 ******************************************************************************
 ******************************************************************************/

static void
dump_managed_block(nb_MBLOCK *block)
{
    const char *indent = "  ";
    printf("%sBLOCK(MANAGED)=%p; BUF=%p, %uB\n", indent,
           (void *)block, block->root, block->nalloc);
    indent = "     ";

    printf("%sUSAGE:\n", indent);
    printf("%s", indent);
    if (BLOCK_IS_EMPTY(block)) {
        printf("EMPTY\n");
        return;
    }

    printf("[");

    if (block->cursor == block->wrap) {
        if (block->start) {
            printf("ooo{S:%u}xxx", block->start);
        } else {
            printf("{S:0}xxxxxx");
        }

        if (block->nalloc > block->cursor) {
            printf("{CW:%u}ooo{A:%u}", block->cursor, block->nalloc);
        } else {
            printf("xxx{CWA:%u)}", block->cursor);
        }
    } else {
        printf("xxx{C:%u}ooo{S:%u}xxx", block->cursor, block->start);
        if (block->wrap != block->nalloc) {
            printf("{W:%u}ooo{A:%u}", block->wrap, block->nalloc);
        } else {
            printf("xxx{WA:%u}", block->wrap);
        }
    }
    printf("]\n");
}

static void dump_sendq(nb_SENDQ *q)
{
    const char *indent = "  ";
    slist_node *ll;
    printf("Send Queue\n");
    SLIST_FOREACH(&q->pending, ll) {
        nb_SNDQELEM *e = SLIST_ITEM(ll, nb_SNDQELEM, slnode);
        printf("%s[Base=%p, Len=%u]\n", indent, e->base, e->len);
        if (q->last_requested == e) {
            printf("%s<Current Flush Limit @%u^^^>\n", indent, q->last_offset);
        }
    }
}

void
netbuf_dump_status(nb_MGR *mgr)
{
    slist_node *ll;
    printf("Status for MGR=%p [nallocs=%u]\n", (void *)mgr, mgr->total_allocs);
    printf("ACTIVE:\n");

    SLIST_FOREACH(&mgr->datapool.active, ll) {
        nb_MBLOCK *block = SLIST_ITEM(ll, nb_MBLOCK, slnode);
        dump_managed_block(block);
    }
    dump_sendq(&mgr->sendq);
}
