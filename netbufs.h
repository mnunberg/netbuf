#ifndef NETBUFS_H
#define NETBUFS_H

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 ******************************************************************************
 ** Introduction                                                             **
 ******************************************************************************
 ******************************************************************************/

/**
 * NETBUF - Efficient write buffers
 * ================================
 *
 * GOALS
 * =====
 *
 * (1) provide a simple buffer allocation API
 *     From a logic perspective it's simplest to deal with a straight
 *     contiguous buffer per packet.
 *
 * (2) provide an efficient way of sending multiple contiguous packets. This
 *     will reduce IOV fragmentation and reduce the number of trips to the
 *     I/O plugin for multiple writes. Currently this is done very efficiently
 *     with the ringbuffer - however this comes at the cost of copying all
 *     request data to the ringbuffer itself. Our aim is to reduce the
 *     number of copies while still maintaining a packed buffer.
 *
 * (3) Allow a pluggable method by which user-provided data can be plugged
 *     into the span/cursor/flush architecture.
 */

#include "slist.h"
#include "netbufs-defs.h"
#include "netbufs-mblock.h"

/**
 * XXX: It is recommended that you maintain the individual fields in your
 * own structure and then re-create them as needed. The span structure is 16
 * bytes on 64 bit systems, but can be reduced to 12 if needed. Additionally,
 * you may already have the 'size' field stored/calculated elsewhere.
 */
typedef struct {
    /** PRIVATE: Parent block */
    nb_MBLOCK *parent;

    /** PRIVATE: Offset from root at which this buffer begins */
    nb_SIZE offset;

    /** PUBLIC, write-once: Allocation size */
    nb_SIZE size;
} nb_SPAN;

#define CREATE_STANDALONE_SPAN(span, buf, len) \
    (span)->parent = (nb_MBLOCK *)buf; \
    (span)->offset = (nb_SIZE)-1; \
    (span)->size = len;


typedef struct {
    slist_node slnode;
    char *base;
    nb_SIZE len;
} nb_SNDQELEM;

typedef struct {
    /** Linked list of pending spans to send */
    slist_root pending;

    /** The last window which was part of the previous fill call */
    nb_SNDQELEM *last_requested;

    /**
     * Number of bytes enqueued in the 'last request' element. This is needed
     * because it is possible for the last element to grow in length during
     * a subsequent flush.
     */
    nb_SIZE last_offset;

    /** Pool of elements to utilize */
    nb_MBPOOL elempool;
} nb_SENDQ;


struct netbufs_st {
    /** Send Queue */
    nb_SENDQ sendq;

    /** Pool for variable-size data */
    nb_MBPOOL datapool;

    nb_SETTINGS settings;

    /** Total number of allocations */
    unsigned int total_allocs;

    /** Total number of bytes allocated */
    unsigned int total_bytes;
};

/**
 * Retrieves a pointer to the buffer related to this span.
 */
#define SPAN_BUFFER(span) ((span)->parent->root + (span)->offset)

/**
 * Reserve a contiguous region of memory, in-order for a given span. The
 * span will be reserved from the last block to be flushed to the network.
 *
 * The contents of the span are guaranteed to be contiguous (though not aligned)
 * and are available via the SPAN_BUFFER macro.
 *
 * @return 0 if successful, -1 on error
 */
int
netbuf_mblock_reserve(nb_MGR *mgr, nb_SPAN *span);

/**
 * Release a span previously allocated via reserve_span. It is assumed that the
 * contents of the span have either:
 *
 * (1) been successfully sent to the network
 * (2) have just been scheduled (and are being removed due to error handling)
 * (3) have been partially sent to a connection which is being closed.
 *
 * @param mgr the manager in which this span is reserved
 * @param span the span
 */
void
netbuf_mblock_release(nb_MGR *mgr, nb_SPAN *span);

/**
 * Schedules an IOV to be placed inside the send queue. The storage of the
 * underlying buffer must not be freed or otherwise modified until it has
 * been sent.
 *
 * With the current usage model, flush status is implicitly completed once
 * a response has arrived.
 *
 * Note that you may create the IOV from a SPAN object like so:
 * iov->iov_len = span->size;
 * iov->iov_base = SPAN_BUFFER(span);
 */
void
netbuf_enqueue(nb_MGR *mgr, const nb_IOV *bufinfo);

void
netbuf_enqueue_span(nb_MGR *mgr, nb_SPAN *span);

/**
 * Gets the number of IOV structures required to flush the entire contents of
 * all buffers.
 */
unsigned int
netbuf_get_niov(nb_MGR *mgr);

/**
 * Populates an iovec structure for flushing a set of bytes from the various
 * blocks.
 *
 * You may call this function mutltiple times, so long as each call to
 * start_flush is eventually mapped with a call to end_flush.
 *
 * netbuf_start_flush(mgr, iov1, niov1);
 * netbuf_start_flush(mgr, iov2, niov2);
 * ...
 * netbuf_end_flush(mgr, nbytes1);
 * netbuf_end_flush(mgr, nbytes2);
 *
 * Additionally, only the LAST end_flush call may be supplied an nflushed
 * parameter which is smaller than the size returned by start_flush.
 *
 * @param mgr the manager object
 * @param iov an array of iovec structures
 * @param niov the number of iovec structures allocated.
 *
 * @return the number of bytes which can be flushed in this IOV. If the
 * return value is 0 then there are no more bytes to flush.
 *
 * Note that the return value is limited by the number of IOV structures
 * provided and should not be taken as an indicator of how many bytes are
 * used overall.
 */
nb_SIZE
netbuf_start_flush(nb_MGR *mgr, nb_IOV *iov, int niov);

/**
 * Indicate that a number of bytes have been flushed. This should be called after
 * the data retrieved by get_flushing_iov has been flushed to the TCP buffers.
 *
 * @param mgr the manager object
 * @param nflushed how much data in bytes was flushed to the network.
 */
void
netbuf_end_flush(nb_MGR *mgr, nb_SIZE nflushed);

/**
 * Informational function to get the total size of all data in the
 * buffers. This traverses all blocks, so call this for debugging only.
 */
nb_SIZE
netbuf_get_size(const nb_MGR *mgr);

/**
 * Get the maximum size of a span which can be satisfied without using an
 * additional block.
 *
 * @param allow_wrap
 * Whether to take into consideration wrapping. If this is true then the span
 * size will allow wrapping. If disabled, then only the packed size will be
 * available. Consider:
 *
 * [ ooooooo{S:10}xxxxxxxxx{C:10}ooooo{A:5} ]
 *
 * If wrapping is allowed, then the maximum span size will be 10, from 0..10
 * but the last 5 bytes at the end will be lost for the duration of the block.
 *
 * If wrapping is not allowed then the maximum span size will be 5.
 *
 * @return
 * the maximum span size without requiring additional blocks.
 */
nb_SIZE
netbuf_mblock_get_next_size(const nb_MGR *mgr, int allow_wrap);

/**
 * Initializes an nb_MGR structure
 * @param mgr the manager to initialize
 */
void
netbuf_init(nb_MGR *mgr, const nb_SETTINGS *settings);

/**
 * Frees up any allocated resources for a given manager
 * @param mgr the manager for which to release resources
 */
void
netbuf_cleanup(nb_MGR *mgr);

/**
 * Populates the settings structure with the default settings. This structure
 * may then be modified or tuned and passed to netbuf_init()
 */
void
netbuf_default_settings(nb_SETTINGS *settings);

/**
 * Dump the internal structure of the manager to the screen. Useful for
 * debugging.
 */
void
netbuf_dump_status(nb_MGR *mgr);

#ifdef __cplusplus
}
#endif

#endif /* LCB_PACKET_H */
