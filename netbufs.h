#ifndef NETBUFS_H
#define NETBUFS_H

#include "slist.h"
#ifdef __cplusplus
extern "C" {
#endif
/**
 *
 * NETBUF - Efficient write buffers
 * ================================
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
 *
 *
 * Basic terminology and API
 * =========================
 *
 * ~~~ SPAN ~~~
 *
 * a SPAN is a region of contiguous memory; a span is user allocated.
 * A span is initialized via NETBUF_SPAN_INIT which sets the size the span
 * should cover.
 *
 * Once the span has been set, it must be _reserved_. Once a span has been
 * reserved, it will guarantee access to a buffer which may be obtained
 * via SPAN_BUFFER. This buffer is guaranteed to contain exactly size bytes
 * and may be written to or read from using memcpy. Note that the span's buffer
 * is not aligned.
 *
 * Additionally, spans are effectively ordered in sequential memory. This means
 * that it can be effectively relied upon that if span_A is reserved and then
 * span_B is reserved, that span_A will be ordered before span_B. This will
 * make more sense later on when reading about FLUSH.
 *
 * ~~~ BLOCK ~~~
 *
 * A block contains a chunk of memory and offset variables. The chunk of
 * memory belonging to a block is fixed (by default to 32k). A block maintains
 * a sequence of one or more _effectively contiguous_ spans. The spans are
 * ordered in such a manner that, at most, two buffer pointers
 * (e.g. char * pointers) will be required to obtain a sequential representation
 * of all spans contained therein. This allows for optimization of grouping
 * many spans into larger blocks of packed spans.
 *
 * When a block does not have space for additional spans, a new block is obtained
 * (either allocated, or retrieved from a cache). Blocks are ordered as a
 * super-sequence of spans; thus:
 *
 * [ BLOCK 1      ] [ BLOCK 2          ]
 *  { S1, S2, S3 }   { S4, S5, S6, S7 }
 *
 *
 * Note that blocks are not aware of the spans they contain. Blocks only contain
 * bound offsets which effectively represent the first and last span contained
 * in them. This makes the block structures smaller and easier to maintain.
 *
 * ~~~ MANAGER ~~~~
 *
 * The manager controls the assignment of spans to blocks. Thus it is aware
 * of the block order.
 *
 *
 * ~~~ FLUSH ~~~~
 * Flush is the act of consuming data from the manager. Flush represents an
 * internal cursor located within the blocks. This cursor is non-repeatable
 * (it cannot be rewound) and represents a position within a specific block.
 * All data before this position is considered to be "flushed" or "consumed"
 * (typically via a send() call), and all data after the cursor is considered
 * to be "unflushed" - i.e. it has not been sent over the network yet.
 *
 * API-wise, flush is performed by populating a set of IOV structures which
 * may be sent (this does not modify internals) via fill_iov(). Once the
 * IOV has been sent, the set_flushed() function is called indicating how
 * many bytes have been flushed. The internal cursor is incremented by this
 * amount of bytes.
 *
 * Flush begins at the first block and ends at the last active block.
 * In this use pattern, it is assumed that under normal circumstances a span
 * will not be released until it has been flushed - and releasing a span
 * before it has been flushed will corrupt the internal offsets as well as
 * resulting in having garbled data placed within the TCP stream.
 *
 * It is safe to release spans which have been flushed; once a block has been
 * flushed and all its spans have been released, the block is considered
 * available (or freed to libc, depending on allocation constraints).
 *
 * Memcached Packet Construction
 * =============================
 *
 * From libcouchbase, the intended architecture is to maintain a manager
 * object per server structure. Packets sent to the server will be allocated
 * in packed order and will be shipped off to the socket via an IOV structure.
 *
 * It is assumed that there will be a metadata packet structure containing
 * the header, user cookie, start time, etc which will also contain an
 * embedded SPAN structure containing the offsets for the specific packet.
 *
 * As the SPAN is contiguous, the key will also be embedded within the span
 * as well.
 *
 * User Allocated Packets
 * ======================
 *
 * With this scheme it will also be possible to employ use allocated data
 * packets.
 *
 * This will require a specialized packet structure (for the metadata
 * book-keeping).
 *
 * Specifically, in order to support user-allocated data, each separate region
 * in memory must be encapsulated into a customized block structure which has
 * non-trivial overhead (see below for the memory layout of the block
 * structure).
 *
 * An example command request may look like the following:
 */
#if 0
struct packet_request_sample {
    /* Incoming header. 24+extras. Aligned */
    const char *header;

    /** Key/Value payload */
    struct lcb_iovec_st *iov;
    unsigned char niov;
};
#endif

/**
 * The corresponding internal structure would look like this
 */
#if 0
struct internal_userpacket_sample {
    struct packet_request_sample *user;
    nb_SPAN *spans;
    unsigned char nspans;
    /* ... */
};
#endif

/**
 * Internally, each IOV region would receive its own block structure which
 * must be allocated (or retrieved from a cache). This block structure
 * currently tallies at 48 bytes
 */

/**
 * DIAGRAM LEGEND
 * In the following comments (and within the source as well) we will try to
 * display diagrams of blocks. The following symbols will be used:
 *
 * {$:NN} = This represents a position marker, $ will be the position type,
 *          and NN is the offset value.
 *
 * The following are the position types:
 *
 * [S]tart       Start of the buffer (block->start)
 * [W]rap        Wrapping and end of the first segment (block->wrap)
 * [C]ursor      End of the current segment (block->cursor)
 * [A]lloc       Allocation limit of the buffer (block->nalloc)
 * [F]lush       Flush cursor (block->flushcur)
 *
 * Note that in some cases two position types may share the same offset.
 *
 * Between any of the offsets, there are data bytes (or just "Data"). These
 * may be one of the following:
 *
 * [x]           Used data. This data is owned by a span
 * [o]           Unused data, but available for usage
 * [-]           Unreachable data. This is not used but cannot be reserved
 */

/**
 * A block contains a single allocated buffer. The buffer itself may be
 * divided among multiple spans. We divide our buffers like so:
 *
 * Initially:
 *
 * [ {FS:0}xxxxxxx{CW:10}ooo{A:12} ]
 *
 * After flushing some data:
 *
 * [ {S:0}xx{F:5}xxxx{CW:10}oo{A:12} ]
 * Note how the flush cursor is incremented
 *
 *
 * Typically, once data is flushed, the user will release the segment, and thus
 * will look something like this:
 *
 * [ ooo{SF:6}xxxx{CW:10}oooo{A:12} ]
 *
 * Appending data to a buffer (or reserving a span) depends on the span
 * size requirements. In this case, if a span's size is 2 bytes or lower,
 * it is appended at the end of the first segment, like so:
 * [ ooo{SF:16}xxxxxx{CWA:12} ]
 *
 * Otherwise, it is wrapped around, like so:
 *
 * [ xx{C:3}oo{SF:6}xxxx{W:10}--{A:12} ]
 *
 * Note that [C] has been wrapped around to start at 3.
 *
 *
 * The total size of the block's used portion is as follows:
 *
 * (1) The number of bytes between [S]tart and [Wrap]
 * (2) If [C] != [W], then also add the value of [C]
 */

typedef struct netbufs_st nb_MGR;
typedef struct netbuf_block_st nb_BLOCK;
typedef struct netbuf_span_st nb_SPAN;
typedef unsigned int nb_SIZE;

typedef struct {
    const void *iov_base;
    nb_SIZE iov_len;
} nb_IOV;

enum {
    /** Block is part of the manager structure */
    NETBUF_BLOCK_MANAGED = 0,

    /** Block has been allocated by the manager, but is not part of its structure */
    NETBUF_BLOCK_STANDALONE,

    /** Block is user provided */
    NETBUF_BLOCK_USER
};

struct netbuf_span_st {
    /** PRIVATE: Parent block */
    nb_BLOCK *parent;

    /** PRIVATE: Offset from root at which this buffer begins */
    nb_SIZE offset;

    /** PUBLIC, write-once: Allocation size */
    nb_SIZE size;
};

#define NETBUF_SPAN_INIT(span, size) (span)->size = size

#define NETBUF_DEALLOC_CACHE 32
typedef struct {
    slist_node slnode;
    nb_SIZE offset;
    nb_SIZE size;
    int unmanaged;
} nb_QDEALLOC;

typedef struct {
    slist_root pending;
    nb_SIZE min_offset;
    nb_QDEALLOC _avail[NETBUF_DEALLOC_CACHE];
} nb_DEALLOC_QUEUE;

struct netbuf_block_st {
    /** slist pointer */
    slist_node slnode;

    /** Header type */
    char type;

    /** Start position for data */
    nb_SIZE start;

    /**
     * Wrap/End position for data. If the block has only one segment,
     * this is always equal to cursor (see below)
     * and will mark the position at which the unused portion of the
     * buffer begins.
     *
     * If the block has two segments, this marks the end of the first segment.
     *
     * In both cases:
     *  I. wrap is always > start
     *  II. wrap-start is the length of the first segment of data
     */
    nb_SIZE wrap;

    /**
     * End position for data. This always contains the position at which
     * the unused data begins.
     *
     * If the block only has a single segment then both the following are true:
     *
     *  I. cursor == wrap
     *  II. cursor > start (if not empty)
     *
     * If the block has two segments, then both the following are true:
     *
     *  I. cursor != wrap
     *  II. cursor < start
     *
     * If the block is empty:
     *  cursor == start
     */
    nb_SIZE cursor;

    /**
     * The flush position for the data. This is the offset of the first
     * unflushed byte in the block. The flush cursor is updated via end_flush().
     *
     *  I. If the block has not been flushed, then flushcur == start
     *  II. If the block has been completely flushed, then flushcur == cursor
     *  III. If the block has been partially flushed, then
     *       both (I) and (II) should be false.
     */
    nb_SIZE flushcur;

    /**
     * Total number of bytes allocated in root. This represents the absolute
     * limit on how much data can be supplied
     */
    nb_SIZE nalloc;


    /**
     * Actual allocated buffer. This remains constant for the duration of the
     * block's lifetime
     */
    char *root;

    /**
     * Pointer to a DEALLOC_QUEUE structure. This is only valid if an
     * out-of-order dealloc has been performed on this block.
     */
    nb_DEALLOC_QUEUE *deallocs;
};

typedef void (*nb_ublock_callback)(nb_BLOCK *);

typedef struct netbuf_ublock_st {
    slist_node slnode;

    /** NETBUF_BLOCK_USER */
    char type;

    /** Number of IOVs */
    char niov;

    /** Current flush position within the IOVs */
    nb_SIZE flushcur;
    /** Total size of data */
    nb_SIZE total;

    /** IOV Structures */
    nb_IOV *iov;

    /** Callback when all spans have been released */
    nb_ublock_callback callback;
} nb_UBLOCK;

#define MIN_BLOCK_COUNT 32
#define ALLOC_HIST_BUCKETS 24

struct netbufs_st {
    /** Blocks which are enqueued in the network */
    slist_root active_blocks;

    /** Fully free blocks */
    slist_root avail_blocks;

    /** Fixed allocation size */
    unsigned int basealloc;

    unsigned int maxblocks;
    unsigned int blockcount;
    unsigned int total_allocs;

    /** Contiguous block heads for cache locality */
    nb_BLOCK _blocks[MIN_BLOCK_COUNT];
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
 * The 'size' property of the span parameter should be set prior to calling
 * this function
 *
 * @return 0 if successful, -1 on error
 */
int netbuf_reserve_span(nb_MGR *mgr, nb_SPAN *span);


#define NETBUF_FLUSHED_PARTIAL -1
#define NETBUF_FLUSHED_FULL 1
#define NETBUF_FLUSHED_NONE 0
/**
 * Indicate whether the specified span has been flushed to the network.
 * @return one of
 *  NETBUF_FLUSHED_PARTIAL: Part of the span has been written
 *  NETBUF_FLUSHED_FULL: The entire span has been written
 *  NETBUF_FLUSHED_NONE: None of the span has been written.
 */
int netbuf_get_flush_status(const nb_MGR *mgr, const nb_SPAN *span);

/**
 * Release a span previously allocated via reserve_span. It is assumed that the
 * contents of the span have either:
 *
 * (1) been successfully sent to the network
 * (2) have just been scheduled (and are being removed due to error handling)
 * (3) have been partially sent to a connection which is being closed.
 *
 * Additionally, the span must currently be located either at the very beginning
 * or the very end of the buffer. This should never be a problem in normal
 * situations, where packets are enqueued in order.
 *
 * TODO: This is a bit weird. Any ideas about this?
 */
void netbuf_release_span(nb_MGR *mgr, nb_SPAN *span);


/**
 * Gets the number of IOV structures required to flush the entire contents of
 * all buffers.
 */
unsigned int netbuf_get_niov(nb_MGR *mgr);

/**
 * Populates an iovec structure for flushing a set of bytes from the various
 * blocks.
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
nb_SIZE netbuf_start_flush(nb_MGR *mgr, nb_IOV *iov, int niov);

/**
 * Indicate that a number of bytes have been flushed. This should be called after
 * the data retrieved by get_flushing_iov has been flushed to the TCP buffers.
 *
 * @param mgr the manager object
 * @param nflushed how much data in bytes was flushed to the network.
 */
void netbuf_end_flush(nb_MGR *mgr, nb_SIZE nflushed);

/**
 * Resets any flushing state.
 */
#define netbuf_reset_flush(mgr) \
    do { \
        mgr->flushing_block = NULL; \
        mgr->flushing_pos = 0; \
    } while (0);


/**
 * Informational function to get the total size of all data in the
 * buffers. This traverses all blocks, so call this for debugging only.
 */
nb_SIZE netbuf_get_size(const nb_MGR *mgr);

/**
 * Get the maximum size of a span which can be satisfied without using an
 * additional block.
 *
 * @param allow_wrap
 * Whether to take into consideration wrapping. If this is true then the span
 * size will allow wrapping. If disabled, then only the packed size will be
 * available. Consider:
 *
 * R=root
 * o=unused
 * x=used
 * A=allocated
 * E=end
 *
 * [ {R:0}ooooooo{S:10}xxxxxxxxx{E:10}ooooo{A:5} ]
 *
 * If wrapping is allowed, then the maximum span size will be 10, from 0..10
 * but the last 5 bytes at the end will be lost for the duration of the block.
 *
 * If wrapping is not allowed then the maximum span size will be 5.
 *
 * @return
 * the maximum span size without requiring additional blocks.
 */
nb_SIZE netbuf_get_max_span_size(const nb_MGR *mgr, int allow_wrap);

void netbuf_init(nb_MGR *mgr);
void netbuf_cleanup(nb_MGR *mgr);
void netbuf_dump_status(nb_MGR *mgr);


/**
 * ABI Safety.
 *
 * Get the size required for allocation of a user block. This is runtime
 * dependent and may change between versions. Does sizeof(nb_BLOCK)
 *
 * Alignment constraints apply.
 */
nb_SIZE netbuf_get_ublock_alloc_size(void);

/**
 * Initialize a user block.
 * @param block a block allocated with at least ublock_alloc_size bytes
 * available.
 *
 * @param root the root chunk of memory for this block
 * @param cursor the offset at which the block's data ends
 * @param start the offset at which the block's data begins. This should
 *  normally be 0 if the
 */
void netbuf_init_ublock(nb_MGR *mgr,
                        nb_BLOCK *block,
                        const void *root,
                        nb_SIZE cursor,
                        nb_SIZE start,
                        nb_SIZE wrap,
                        nb_ublock_callback callback);

#ifdef __cplusplus
}
#endif

#endif /* LCB_PACKET_H */
