#ifndef LCB_SLIST_H
#define LCB_SLIST_H

struct slist_node_st;
typedef struct slist_node_st {
    struct slist_node_st *next;
} slist_node;

typedef struct {
    slist_node *first;
    slist_node *last;
} slist_root;

/**
 * Indicates whether the list is empty or not
 */
#define SLIST_IS_EMPTY(list) ((list)->last == NULL)

/**
 * Iterator for list. This can be used as the 'for' statement; as such this
 * macro should look like such:
 *
 *  slist_node *ii;
 *  SLIST_FOREACH(list, ii) {
 *      my_item *item = LCB_LIST_ITEM(my_item, ii, slnode);
 *  }
 *
 *  @param list the list to iterate
 *  @param pos a local variable to use as the iterator
 */
#define SLIST_FOREACH(list, pos) \
    for (pos = (list)->first; pos; pos = pos->next)


typedef struct slist_iterator_st {
    slist_node *cur;
    slist_node *prev;
    slist_node *next;
} slist_iterator;

#define slist_iter_end(list, iter) ((iter)->cur == NULL)

#define SLIST_ITEM(ptr, type, member) \
        ((type *) ((char *)(ptr) - offsetof(type, member)))

#define SLIST_ITERFOR(list, iter) \
    for (slist_iter_init(list, iter); \
            !slist_iter_end(list, iter); \
            slist_iter_incr(list, iter))

#endif
