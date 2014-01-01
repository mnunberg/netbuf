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

#define MINIMUM(a, b) a < b ? a : b
#define MAXIMUM(a, b) a > b ? a : b
#define BASEALLOC 32768

#define BLOCK_IS_FLUSHED(block) ((block)->flush_start == (block)->cursor)
#define BLOCK_IS_EMPTY(block) ((block)->start == (block)->cursor)

#define FIRST_BLOCK(mgr) (SLIST_ITEM((mgr)->active_blocks.first, nb_BLOCK, slnode))
#define LAST_BLOCK(mgr) (SLIST_ITEM((mgr)->active_blocks.last, nb_BLOCK, slnode))

#define NEXT_BLOCK(block) (SLIST_ITEM((block)->slnode.next, nb_BLOCK, slnode))
#define BLOCK_HAS_DEALLOCS(block) \
    ((block)->deallocs && SLIST_IS_EMPTY(&(block)->deallocs->pending))


static nb_SIZE get_block_size(nb_BLOCK *block)
{
    nb_SIZE ret;
    ret = block->wrap - block->start;
    if (block->cursor < block->start) {
        ret += block->cursor;
    }

    return ret;
}

static nb_BLOCK* alloc_new_block(nb_MGR *mgr, nb_SIZE capacity)
{
    int ii;
    nb_BLOCK *ret;

    for (ii = 0; ii < MIN_BLOCK_COUNT; ii++) {
        if (!mgr->_blocks[ii].nalloc) {
            ret = mgr->_blocks + ii;
            break;
        }
    }

    if (!ret) {
        ret = calloc(1, sizeof(*ret));
        mgr->total_allocs++;
        ret->type = NETBUF_BLOCK_STANDALONE;
    }

    if (!ret) {
        return NULL;
    }

    ret->nalloc = mgr->basealloc;

    while (ret->nalloc < capacity) {
        ret->nalloc *= 2;
    }

    ret->wrap = 0;
    ret->cursor = 0;
    ret->root = malloc(ret->nalloc);
    mgr->total_allocs++;

    if (!ret->root) {
        if (ret->type == NETBUF_BLOCK_STANDALONE) {
            free(ret);
        }
        return NULL;
    }

    return ret;
}

static nb_BLOCK* find_free_block(nb_MGR *mgr, nb_SIZE capacity)
{
    slist_iterator iter;

    SLIST_ITERFOR(&mgr->avail_blocks, &iter) {

        nb_BLOCK *cur = SLIST_ITEM(iter.cur, nb_BLOCK, slnode);
        if (cur->nalloc >= capacity) {
            slist_iter_remove(&mgr->avail_blocks, &iter);
            if (cur->type != NETBUF_BLOCK_MANAGED) {
                mgr->blockcount--;
            }
            return cur;
        }
    }
    return NULL;
}

static int reserve_empty_block(nb_MGR *mgr, nb_SPAN *span)
{
    nb_BLOCK *block;

    if ( (block = find_free_block(mgr, span->size)) == NULL) {
        block = alloc_new_block(mgr, span->size);
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

    slist_append(&mgr->active_blocks, &block->slnode);
    return 0;
}

static int reserve_active_block(nb_BLOCK *block, nb_SPAN *span)
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

int netbuf_reserve_span(nb_MGR *mgr, nb_SPAN *span)
{
    nb_BLOCK *block;
    int rv;

    if (SLIST_IS_EMPTY(&mgr->active_blocks)) {
        return reserve_empty_block(mgr, span);

    } else {
        block = SLIST_ITEM(mgr->active_blocks.last, nb_BLOCK, slnode);
        rv = reserve_active_block(block,span);

        if (rv != 0) {
            return reserve_empty_block(mgr, span);
        }

        span->parent = block;
        return rv;
    }
}

nb_SIZE netbuf_get_size(const nb_MGR *mgr)
{
    nb_SIZE ret = 0;
    slist_node *ll;

    SLIST_FOREACH(&mgr->active_blocks, ll) {
        ret += get_block_size(SLIST_ITEM(ll, nb_BLOCK, slnode));
    }

    return ret;
}

nb_SIZE netbuf_get_max_span_size(const nb_MGR *mgr, int allow_wrap)
{
    nb_BLOCK *block;

    if (SLIST_IS_EMPTY(&mgr->avail_blocks)) {
        return 0;
    }

    block = LAST_BLOCK(mgr);
    if (block->deallocs) {
        return 0;
    }

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

nb_SIZE netbuf_get_ublock_alloc_size(void)
{
    return sizeof(nb_UBLOCK);
}

unsigned int netbuf_get_niov(nb_MGR *mgr)
{
    slist_node *ll;
    unsigned int ret = 0;

    SLIST_FOREACH(&mgr->active_blocks, ll) {
        nb_BLOCK *cur = SLIST_ITEM(ll, nb_BLOCK, slnode);
        if (BLOCK_IS_EMPTY(cur)) {
            continue;
        }

        ret++;
        if (cur->cursor < cur->start) {
            ret++;
        }
    }

    return ret;
}


nb_SIZE netbuf_start_flush(nb_MGR *mgr, nb_IOV *iovs, int niov)
{
    nb_SIZE ret = 0;
    nb_IOV *iov_end = iovs + niov + 1;
    nb_IOV *iov = iovs;
    nb_BLOCK *block;
    slist_node *ll;

    #define SET_IOV_LEN(len) iov->iov_len = len; ret += len;

    /** If there's nothing to flush, return immediately */
    if (SLIST_IS_EMPTY(&mgr->active_blocks)) {
        iov[0].iov_base = NULL;
        iov[0].iov_len = 0;
        return 0;
    }

    SLIST_FOREACH(&mgr->active_blocks, ll) {
        block = SLIST_ITEM(ll, nb_BLOCK, slnode);
        if (block->flushcur == block->cursor || BLOCK_IS_EMPTY(block)) {
            continue;
        }

        /** Flush cursor is either in the first region or the second region */
        if (block->cursor == block->wrap) {
            /** Only one region */

            iov->iov_base = block->root + block->flushcur;
            SET_IOV_LEN(block->wrap - block->flushcur);
            continue;

        } else {
            /** Two regions, but we may have flushed the first one */
            if (block->flushcur > block->cursor) {
                /** First one isn't flushed completely */
                iov->iov_base = block->root + block->flushcur;
                SET_IOV_LEN(block->wrap - block->flushcur);

                if (!block->cursor) {
                    continue;
                }

                if (++iov == iov_end) {
                    break;
                }
                iov->iov_base = block->root;
                SET_IOV_LEN(block->cursor);
            } else {
                iov->iov_base = block->root + block->flushcur;
                SET_IOV_LEN(block->cursor - block->flushcur);
            }
        }
    }

    #undef SET_IOV_LEN
    return ret;
}

/**
 * Here we modify the flush offsets, which should always be a subset of the
 * usage offsets.
 */
void netbuf_end_flush(nb_MGR *mgr, unsigned int nflushed)
{

    slist_node *ll;
    SLIST_FOREACH(&mgr->active_blocks, ll) {
        nb_SIZE to_chop;
        nb_BLOCK *block = SLIST_ITEM(ll, nb_BLOCK, slnode);

        if (block->flushcur >= block->start) {
            /** [xxxxxSxxxxxFxxxxxCW] */
            to_chop = MINIMUM(nflushed, block->wrap - block->flushcur);
            block->flushcur += to_chop;
            nflushed -= to_chop;
            if (block->flushcur == block->wrap && block->cursor != block->wrap) {

                /** [xxxxCoooooSxxxxxFW] */
                if (!nflushed) {
                    block->flushcur = 0;
                    return;
                }

                to_chop = MINIMUM(nflushed, block->cursor);
                nflushed -= to_chop;
                block->flushcur += to_chop;
            }
        } else {
            /** [xxxxxFxxxCoooooSxxxxxW] */

            /** Flush cursor is less than start. Second segment */
            to_chop = MINIMUM(nflushed, block->cursor - block->flushcur);
            block->flushcur += to_chop;
            nflushed -= to_chop;
        }

        if (!nflushed) {
            break;
        }

    }
}

int netbuf_get_flush_status(const nb_MGR *mgr, const nb_SPAN *span)
{
    nb_BLOCK *block = span->parent;
    if (block->cursor == block->flushcur) {
        /** Entire block flushed */
        return NETBUF_FLUSHED_FULL;
    }

    if (span->offset >= block->start) {
        /** first region */
        if (block->flushcur < block->start) {
            /** cursor already in second region */
            return NETBUF_FLUSHED_FULL;
        }
    } else {
        /** second region */
        if (block->flushcur > block->start) {
            /** cursor still in first region */
            return NETBUF_FLUSHED_NONE;
        }
    }

    if (block->flushcur <= span->offset) {
        return NETBUF_FLUSHED_NONE;
    }

    if (block->flushcur > span->offset + span->size) {
        return NETBUF_FLUSHED_FULL;
    }

    (void)mgr;

    return NETBUF_FLUSHED_PARTIAL;
}

static void ooo_queue_dealoc(nb_BLOCK *block, nb_SPAN *span)
{
    int ii;

    nb_QDEALLOC *qd;
    nb_DEALLOC_QUEUE *queue;

    if (!block->deallocs) {
        block->deallocs = calloc(1, sizeof(*block->deallocs));
    }
    queue = block->deallocs;

    for (ii = 0; ii < NETBUF_DEALLOC_CACHE; ii++) {
        if (queue->_avail[ii].size == 0) {
            qd = queue->_avail + ii;
            break;
        }
    }

    if (!qd) {
        qd = calloc(1, sizeof(*qd));
        qd->unmanaged = 1;
    }

    qd->offset = span->offset;
    qd->size = span->size;
    if (queue->min_offset > qd->offset) {
        queue->min_offset = qd->offset;
    }
    slist_append(&queue->pending, &qd->slnode);
}

static void ooo_apply_dealloc(nb_BLOCK *block, nb_SIZE offset)
{
    nb_SIZE min_next = -1;
    slist_iterator iter;
    nb_DEALLOC_QUEUE *queue = block->deallocs;

    SLIST_ITERFOR(&queue->pending, &iter) {
        nb_QDEALLOC *cur = SLIST_ITEM(iter.cur, nb_QDEALLOC, slnode);
        if (cur->offset == offset) {
            slist_iter_remove(&block->deallocs->pending, &iter);
            block->start += cur->size;
            if (cur->unmanaged) {
                free(cur);
            } else {
                cur->size = 0;
            }

        } else if (cur->offset < min_next) {
            min_next = cur->offset;
        }
    }
    queue->min_offset = min_next;
}

void netbuf_release_span(nb_MGR *mgr, nb_SPAN *span)
{
    nb_BLOCK *block = span->parent;

    if (span->offset == block->start) {
        /** Removing from the beginning */
        block->start += span->size;

        if (block->deallocs && block->deallocs->min_offset == block->start) {
            ooo_apply_dealloc(block, block->start);
        }

        if (!BLOCK_IS_EMPTY(block) && block->start == block->wrap) {
            block->wrap = block->cursor;
            block->start = 0;
        }

    } else if (span->offset + span->size == block->cursor) {
        /** Removing from the end */
        if (block->cursor == block->wrap) {
            /** Single region, no wrap */
            block->cursor -= span->size;
            block->wrap -= span->size;

        } else {
            block->cursor -= span->size;
            if (!block->cursor) {
                /** End has reached around */
                block->cursor = block->wrap;
            }
        }

    } else {
        ooo_queue_dealoc(block, span);
        return;
    }

    if (!BLOCK_IS_EMPTY(block)) {
        return;
    }

    {
        slist_iterator iter;
        SLIST_ITERFOR(&mgr->active_blocks, &iter) {
            if (&block->slnode == iter.cur) {
                slist_iter_remove(&mgr->active_blocks, &iter);
                break;
            }
        }
    }

    if (mgr->blockcount < mgr->maxblocks) {
        slist_append(&mgr->avail_blocks, &block->slnode);
        mgr->blockcount++;

    } else {
        free(block->root);
        block->root = NULL;

        if (block->type == NETBUF_BLOCK_STANDALONE) {
            free(block);
        }
    }
}


void netbuf_init(nb_MGR *mgr)
{
    memset(mgr, 0, sizeof(*mgr));
    mgr->basealloc = BASEALLOC;
    mgr->maxblocks = MIN_BLOCK_COUNT * 2;
    mgr->blockcount = MIN_BLOCK_COUNT;
}

static void free_blocklist(slist_root *list)
{
    slist_iterator iter;
    SLIST_ITERFOR(list, &iter) {
        nb_BLOCK *block = SLIST_ITEM(iter.cur, nb_BLOCK, slnode);
        slist_iter_remove(list, &iter);

        if (block->root) {
            free(block->root);
        }

        if (block->deallocs) {
            slist_iterator dea_iter;
            SLIST_ITERFOR(&block->deallocs->pending, &dea_iter) {
                nb_QDEALLOC *qd = SLIST_ITEM(dea_iter.cur, nb_QDEALLOC, slnode);
                if (qd->unmanaged) {
                    free(qd);
                }
            }

            free(block->deallocs);
            block->deallocs = NULL;
        }

        if (block->type == NETBUF_BLOCK_STANDALONE) {
            free(block);
        }
    }
}

void netbuf_cleanup(nb_MGR *mgr)
{
    free_blocklist(&mgr->active_blocks);
    free_blocklist(&mgr->avail_blocks);
}

void netbuf_dump_status(nb_MGR *mgr)
{
    slist_node *ll;
    const char *indent = "";
    printf("Status for MGR=%p [nallocs=%u]\n", (void *)mgr, mgr->total_allocs);
    printf("ACTIVE:\n");

    SLIST_FOREACH(&mgr->active_blocks, ll) {
        nb_BLOCK *block = SLIST_ITEM(ll, nb_BLOCK, slnode);

        indent = "  ";
        printf("%sBLOCK=%p; BUF=%p, %uB\n", indent,
               (void *)block, block->root, block->nalloc);
        indent = "     ";

        printf("%sUSAGE:\n", indent);
        printf("%s", indent);
        if (BLOCK_IS_EMPTY(block)) {
            printf("EMPTY\n");
            continue;
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
}