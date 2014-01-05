#ifndef NETBUFS_SLIST_H
#define NETBUFS_SLIST_H

#include <stddef.h>
#include <cstdlib>

namespace Netbufs {
namespace SList {

struct Node;
struct List;
struct IteratorEx;
struct Iterator;

struct Node {
    Node * next;
};

struct Iterator {
    Node *cur;
    bool end() const { return cur == NULL; }
    void inc() { cur = cur->next; }

    Iterator& operator++(int) { inc(); return *this; }
};

struct IteratorEx : Iterator {
    Node *next;
    Node *prev;

    inline void inc() {
        prev = prev->next;
        cur = next;
        next = cur ? cur->next : NULL;
    }

    inline void remove(List *);

    IteratorEx& operator++(int) { inc(); return *this; }
};

struct List {
    inline IteratorEx beginEx() const;

    Iterator begin() const {
        Iterator iter;
        iter.cur = first;
        return iter;
    }

    inline void append(Node *);
    inline void prepend(Node *);
    inline bool remove(Node *node);
    inline bool contains(Node *node);
    inline void removeFirst();

    bool empty() { return first == NULL && last == NULL; }

    List() : first(NULL), last(NULL) {}

    Node * first;
    Node *last;
};

}
} // namespace
#endif /* NETBUFS_SLIST_H */
