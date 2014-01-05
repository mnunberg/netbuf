namespace SList {

#include <cstddef>
#include <cstdlib>

struct SListNode {
    SListNode()
        : next(NULL) {
    }

    SListNode *next;
};

template <typename T, SListNode T::*M>
class SList {
public:
    class FastIterator {
        friend class SList<T, M>;
    private:
        FastIterator()
            : cur(NULL) {
        }

        FastIterator(SListNode *start)
            : cur(start) {
        }

        SListNode *cur;

    public:
        T & operator*() const {
            return *fromNode(cur);
        }

        T * operator->() const {
            return fromNode(cur);
        }

        operator T*() {
            return fromNode(cur);
        }

        bool operator==(const FastIterator &other) const {
            return cur == other.cur;
        }

        bool operator!=(const FastIterator &other) const {
            return cur != other.cur;
        }

        void operator++() {
            cur = cur->next;
        }

        void operator++(int) {
            operator++();
        }

    };

    class Iterator {
        friend class SList<T, M>;
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

        operator T*() {
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
        void operator++(int) {
            operator++();
        }
    };

    // Helpers, static so they are compile-time calculated
    static SListNode * toNode(T *item) {
        return (SListNode*)((char*)item + ((size_t)(&(((T*)0)->*M))));
    }
    static T * fromNode(SListNode *node) {
        return (T*)((char*)node - ((size_t)(&(((T*)0)->*M))));
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

    FastIterator fbegin() const {
        return FastIterator(first);
    }

    FastIterator fend() const {
        return FastIterator();
    }

    FastIterator ffrom(T * item) const {
        SListNode *node = toNode(item);
        return FastIterator(node);
    }

    T * front() const {
        if (!first) {
            return NULL;
        }
        return fromNode(first);
    }

    T * back() const {
        if (!last) {
            return NULL;
        }
        return fromNode(last);
    }

    bool empty() const {
        return first == NULL;
    }

    bool remove(T * item) {
        for (Iterator it = begin(); it != end(); ++it) {
            if ((T*)it == item) {
                erase(it);
                return true;
            }
        }
        return false;
    }

    void erase(Iterator iter) {
        if (iter.cur == first) {
            first = iter.next;
        } else {
            iter.prev->next = iter.next;
        }

        if (iter.cur == last) {
            last = iter.prev;
        }
    }

    void clear() {
        first = NULL;
        last = NULL;
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
            node->next = first;
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
        return fromNode(item);
    }

private:
    SListNode *first;
    SListNode *last;
};
}
