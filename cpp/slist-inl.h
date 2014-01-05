#include "slist.h"

namespace Netbufs {

namespace SList {

IteratorEx
List::beginEx() const
{
    IteratorEx iter;
    iter.cur = first;
    iter.prev = (Node *)&first;

    if (first != NULL) {
        iter.next = first->next;
    } else {
        iter.next = NULL;
    }
    return iter;
}

void
List::append(Node *item)
{
    if (empty()) {
        first = last = item;

    } else {
        last->next = item;
        last = item;
    }

    item->next = NULL;
}

void
List::prepend(Node *item)
{
    if (empty()) {
        first = last = item;
    } else {
        item->next = first;
        first = item;
    }
}

bool
List::remove(Node *item)
{
    for (IteratorEx iter = beginEx(); !iter.end(); iter.inc()) {
        if (item == iter.cur) {
            iter.remove(this);
            return true;
        }
    }
    return false;
}

void
List::removeFirst()
{
    if (first == NULL) {
        return;
    }

    first = first->next;
    if (first == NULL) {
        last = NULL;
    }
}

void
IteratorEx::remove(List *parent)
{
    if (prev == NULL) {
        parent->removeFirst();
        return;
    }



    prev->next = next;
    if (cur == parent->last) {
        parent->last = NULL;
    }
}

}
}
