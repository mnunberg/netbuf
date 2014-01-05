#include "slist.h"
#include <stdlib.h>
#include <assert.h>

#ifndef INLINE
#ifdef _MSC_VER
#define INLINE __inline__
#elif __GNUC__
#define INLINE __inline__
#else
#define INLINE inline
#endif /* MSC_VER */
#endif /* !INLINE */


static INLINE int
slist_contains(slist_root *list, slist_node *item)
{
    slist_node *ll;
    SLIST_FOREACH(list, ll) {
        if (item == ll) {
            return 1;
        }
    }
    return 0;
}

/* #define NETBUFS_SLIST_DEBUG */

#ifdef NETBUFS_SLIST_DEBUG
#define slist_sanity_insert(l, n) assert(!slist_contains(l, n))
#else
#define slist_sanity_insert(l, n)
#endif


static INLINE void
slist_iter_init(const slist_root *list, slist_iterator *iter)
{
    iter->cur = list->first;
    iter->prev = (slist_node *)&list->first;
    iter->removed = 0;

    if (iter->cur) {
        iter->next = iter->cur->next;
    } else {
        iter->next = NULL;
    }
}

static INLINE void
slist_iter_incr(slist_root *list, slist_iterator *iter)
{
    if (!iter->removed) {
        iter->prev = iter->prev->next;
    } else {
        iter->removed = 0;
    }

    if ((iter->cur = iter->next)) {
        iter->next = iter->cur->next;
    } else {
        iter->next = NULL;
    }

    assert(iter->cur != iter->prev);

    (void)list;
}

static INLINE void
slist_iter_remove(slist_root *list, slist_iterator *iter)
{
    iter->prev->next = iter->next;
    if (!list->first) {
        list->last = NULL;
    }
    iter->removed = 1;
}

static INLINE void
slist_remove_head(slist_root *list)
{
    if (!list->first) {
        return;
    }

    list->first = list->first->next;

    if (!list->first) {
        list->last = NULL;
    }
}

static INLINE void
slist_append(slist_root *list, slist_node *item)
{
    if (SLIST_IS_EMPTY(list)) {
        list->first = list->last = item;
        item->next = NULL;
    } else {
        slist_sanity_insert(list, item);
        list->last->next = item;
        list->last = item;
    }
    item->next = NULL;
}

static INLINE void
slist_prepend(slist_root *list, slist_node *item)
{
    if (SLIST_IS_EMPTY(list)) {
        list->first = list->last = item;
    } else {
        slist_sanity_insert(list, item);
        item->next = list->first;
        list->first = item;
    }
}

