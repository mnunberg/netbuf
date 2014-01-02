#ifndef NETBUFS_DEFS_H
#define NETBUFS_DEFS_H

typedef struct netbufs_st nb_MGR;
typedef unsigned int nb_SIZE;

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


typedef struct {
    nb_SIZE sndq_cacheblocks;
    nb_SIZE sndq_basealloc;
    nb_SIZE dea_cacheblocks;
    nb_SIZE dea_basealloc;
    nb_SIZE data_cacheblocks;
    nb_SIZE data_basealloc;
} nb_SETTINGS;

/** TODO: Use a standardized structure */
typedef struct {
    void *iov_base;
    nb_SIZE iov_len;
} nb_IOV;

#endif /* NETBUFS_DEFS_H */
