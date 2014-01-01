#include "slist.h"
#include <stdlib.h>

#ifndef INLINE
#ifdef _MSC_VER
#define INLINE __inline__
#elif __GNUC__
#define INLINE __inline__
#else
#define INLINE inline
#endif /* MSC_VER */
#endif /* !INLINE */

static INLINE void
slist_iter_init(const slist_root *list, slist_iterator *iter)
{
    iter->cur = list->first;
    iter->prev = NULL;
    if (iter->cur) {
        iter->next = iter->cur->next;
    }
}

static INLINE void
slist_iter_incr(slist_root *list, slist_iterator *iter)
{
    if (iter->prev) {
        iter->prev = iter->prev->next;
    } else {
        iter->prev = list->first;
    }

    iter->cur = iter->next;
    if (iter->cur) {
        iter->next = iter->cur->next;
    } else {
        iter->next = NULL;
    }
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
slist_iter_remove(slist_root *list, slist_iterator *iter)
{
    if (!iter->prev) {
        slist_remove_head(list);
        return;
    }

    iter->prev->next = iter->next;

    if (iter->cur == list->last) {
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
        item->next = list->first;
        list->first = item;
    }
}
