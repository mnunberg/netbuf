#include <stdlib.h>

namespace Netbufs {
namespace SList {

struct SListNode {
    SListNode()
        : next(NULL) {
    }

    SListNode *next;
};

template <typename T, SListNode T::*M>
class SList {
public:
    class Iterator {
        friend SList;
    private:
        Iterator()
            : prev(NULL), cur(NULL), next(NULL) {
        }

        Iterator(SListNode *first_node)
            : prev(NULL), cur(first_node), next(first_node ? first_node->next : NULL) {
        }

        SListNode *prev;
        SListNode *cur;
        SListNode *next;

    public:
        T & operator*() const {
            return *fromNode(cur);
        }

        T * operator->() const {
            return fromNode(cur);
        }

        bool operator==(const Iterator &other) const {
            return cur == other.cur;
        }

        bool operator!=(const Iterator &other) const {
            return cur != other.cur;
        }

        void operator++() {
            prev = cur;
            cur = next;
            if (cur) {
                next = cur->next;
            } else {
                next = NULL;
            }
        }
    };

    // Helpers, static so they are compile-time calculated
    static SListNode * toNode(T *item) {
        return (SListNode*)((char*)item + offsetof(T, *M));
    }
    static T * fromNode(SListNode *node) {
        return (T*)((char*)node - offsetof(T, *M));
    }

    SList()
        : first(NULL), last(NULL) {
    }

    ~SList() {

    }

    Iterator begin() const {
        return Iterator(first);
    }

    Iterator end() const {
        return Iterator();
    }

    T * front() const {
        if (!front) {
            return NULL;
        }
        return fromNode(front);
    }

    T * back() const {
        if (!back) {
            return NULL;
        }
        return fromNode(back);
    }

    void erase(Iterator iter) {
        if (iter.cur == first) {
            first = iter.next;
        } else {
            iter.prev.next = iter.next;
        }

        if (iter.cur == last) {
            last = iter.prev;
        }
    }

    void push_back(T *item) {
        SListNode *node = toNode(item);
        if (first) {
            last->next = node;
            last = node;
        } else {
            first = node;
            last = node;
        }
    }

    void push_front(T *item) {
        SListNode *node = toNode(item);
        if (first) {
            item->next = first;
            first = node;
        } else {
            first = node;
            last = node;
        }
    }

    T * pop_front() {
        if (first == NULL) {
            return NULL;
        }

        SListNode *item = first;
        if (first != last) {
            first = item->next;
        } else {
            first = NULL;
            last = NULL;
        }
        return fromNode(node);
    }

    bool empty() const {
        return first == NULL && last == NULL;
    }

private:
    SListNode *first;
    SListNode *last;
};

}
}
