#include "netbufs++.h"
#include <stdio.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

using namespace Netbufs;

#define BIG_BUF_SIZE 5000
#define SMALL_BUF_SIZE 50
#define ASSERT_EQ(a, b) assert((a) == (b))
#define ASSERT_TRUE(a) assert(a)

/**
 * Reads a series of IOVs to a buffer. Note that buf must be large enough
 * to contain the data
 * @return the length of the buffer
 */

static void test_basic(void)
{
    int rv;
    int ii;
    int n_bigspans = 20;
    int n_smallspans = 2000;
    bool brv;

    Manager mgr;
    Alloc spans_big[20];
    Alloc spans_small[2000];

    for (ii = 0; ii < n_bigspans; ii++) {
        int filler = 'a' + ii;
        Alloc *alloc = spans_big + ii;
        alloc->size = BIG_BUF_SIZE;
        brv = mgr.reserve(alloc);
        ASSERT_TRUE(brv);
        memset(alloc->getBuffer(), filler, alloc->size);
    }

    for (ii = 0; ii < n_smallspans; ii++) {
        Alloc *alloc = spans_small + ii;
        int filler = ii;
        alloc->size = SMALL_BUF_SIZE;
        brv = mgr.reserve(alloc);
        ASSERT_TRUE(brv);
        filler = ii;
        memset(alloc->getBuffer(), filler, alloc->size);
    }

    for (ii = 0; ii < n_bigspans; ii++) {
        char expected[BIG_BUF_SIZE];
        char *curbuf = (char *)spans_big[ii].getBuffer();
        memset(expected, 'a' + ii, BIG_BUF_SIZE);
        ASSERT_EQ(0, memcmp(curbuf, expected, BIG_BUF_SIZE));
        mgr.release(spans_big + ii);
    }

    for (ii = 0; ii < n_smallspans; ii++) {
        char expected[SMALL_BUF_SIZE];
        char *curbuf = (char *)spans_small[ii].getBuffer();
        memset(expected, ii, SMALL_BUF_SIZE);
        ASSERT_EQ(0, memcmp(curbuf, expected, SMALL_BUF_SIZE));
        mgr.release(spans_small + ii);
    }

    IOVector iov[20];
    mgr.startFlush(iov, 20);
}


static void test_flush()
{
    Settings settings;
    Manager::defaultSettings(&settings);
    settings.data.basealloc = 8;

    Manager mgr(&settings);

    Alloc alloc(32);
    Alloc allocs[3];
    IOVector iov[10];

    bool rv;
    rv = mgr.reserve(&alloc);
    ASSERT_TRUE(rv);
    ASSERT_EQ(32, alloc.getSize());
    mgr.enqueue(&alloc);

    Size sz;
    sz = mgr.startFlush(iov, 10);
    ASSERT_EQ(32, sz);
    mgr.endFlush(20);

    sz = mgr.startFlush(iov, 1);
    ASSERT_EQ(12, sz);
    mgr.endFlush(12);
    mgr.release(&alloc);


    for (int ii = 0; ii < 3; ii++) {
        Alloc *a = allocs + ii;
        a->size = 50;
        rv = mgr.reserve(a);
        ASSERT_TRUE(rv);
    }

    for (int ii = 0; ii < 3; ii++) {
        mgr.enqueue(allocs + ii);
    }

    sz = mgr.startFlush(iov, 10);
    ASSERT_EQ(150, sz);
    mgr.endFlush(75);
    mgr.release(&allocs[0]);

    allocs[0].size = 20;
    rv = mgr.reserve(&allocs[0]);
    ASSERT_TRUE(rv);

}

static void test_wrapped()
{
    Settings settings;
    Manager::defaultSettings(&settings);
    settings.data.basealloc = 40;
    Manager mgr(&settings);
    bool rv;
    Alloc span1, span2, span3;

    span1.size = 16;
    span2.size = 16;

    rv = mgr.reserve(&span1);
    ASSERT_TRUE(rv);
    rv = mgr.reserve(&span2);
    ASSERT_TRUE(rv);

    ASSERT_EQ(span1.parent, span2.parent);
    ASSERT_EQ(0, span1.offset);
    ASSERT_EQ(16, span2.offset);

    /* Wewease Wodewick! */
    mgr.release(&span1);
    ASSERT_EQ(16, span2.parent->start);

    /* So we have 8 bytes at the end.. */
    ASSERT_EQ(32, span2.parent->wrap);
    span3.size = 10;

    rv = mgr.reserve(&span3);
    ASSERT_TRUE(rv);

    ASSERT_EQ(10, span2.parent->cursor);
    ASSERT_EQ(0, span3.offset);
    ASSERT_EQ(10, span3.parent->cursor);
    ASSERT_EQ(16, span3.parent->start);

    mgr.release(&span2);
    ASSERT_EQ(0, span3.parent->start);
    mgr.release(&span3);


    span1.size = 20;
    rv = mgr.reserve(&span1);

    ASSERT_EQ(0, span1.offset);
    ASSERT_EQ(20, span1.parent->cursor);
    ASSERT_EQ(0, span1.parent->start);
    ASSERT_EQ(20, span1.parent->wrap);
}

static void assert_iov_eq(IOVector *iov, Size offset, char expected)
{
    char *buf = (char *)iov->base;
    ASSERT_EQ(expected, buf[offset]);
}

static void test_multi_flush(void)
{

    Size sz;
    Alloc span1, span2, span3;
    IOVector iov[10];
    Manager mgr(NULL);
    bool rv;

    span1.size = 50;
    span2.size = 50;
    span3.size = 50;

    rv = mgr.reserve(&span1);
    ASSERT_TRUE(rv);
    rv = mgr.reserve(&span2);
    ASSERT_TRUE(rv);
    rv = mgr.reserve(&span3);
    ASSERT_TRUE(rv);

    mgr.enqueue(&span1);
    mgr.enqueue(&span2);

    sz = mgr.startFlush(iov, 10);
    ASSERT_EQ(100, sz);

    memset(span1.getBuffer(), 'A', span1.size);
    memset(span2.getBuffer(), 'B', span2.size);
    memset(span3.getBuffer(), 'C', span3.size);

    ASSERT_EQ(100, iov->length);
    assert_iov_eq(iov, 0, 'A');
    assert_iov_eq(iov, 50, 'B');

    mgr.enqueue(&span3);
    sz = mgr.startFlush(&iov[1], 0);
    ASSERT_EQ(sz, 50);
    assert_iov_eq(&iov[1], 0, 'C');
    ASSERT_EQ(50, iov[1].length);

    mgr.endFlush(100);
    mgr.endFlush(50);

    sz = mgr.startFlush(iov, 10);
    ASSERT_EQ(0, sz);

    mgr.release(&span1);
    mgr.release(&span2);
    mgr.release(&span3);
}


int main(void)
{
    test_basic();
    test_flush();
    test_wrapped();
    test_multi_flush();
    return 0;
}
