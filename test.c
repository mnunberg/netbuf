#include "netbufs.h"
#include <stdio.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>


#define BIG_BUF_SIZE 5000
#define SMALL_BUF_SIZE 50
#define ASSERT_EQ(a, b) assert((a) == (b))

static void test_basic(void)
{
    nb_MGR mgr;
    netbuf_init(&mgr);
    netbuf_cleanup(&mgr);
    netbuf_init(&mgr);

    int n_bigspans = 20;
    int n_smallspans = 2000;
    nb_SPAN spans_big[20];
    nb_SPAN spans_small[2000];

    for (int ii = 0; ii < n_bigspans; ii++) {
        nb_SPAN *span = spans_big + ii;
        span->size = BIG_BUF_SIZE;
        int rv = netbuf_reserve_span(&mgr, span);
        ASSERT_EQ(0, rv);
        int filler = 'a' + ii;
        memset(SPAN_BUFFER(span), filler, span->size);
    }

    for (int ii = 0; ii < n_smallspans; ii++) {
        nb_SPAN *span = spans_small + ii;
        span->size = SMALL_BUF_SIZE;
        int rv = netbuf_reserve_span(&mgr, span);
        ASSERT_EQ(0, rv);
        int filler = ii;
        memset(SPAN_BUFFER(span), filler, span->size);
    }

    for (int ii = 0; ii < n_bigspans; ii++) {
        char expected[BIG_BUF_SIZE];
        memset(expected, 'a' + ii, BIG_BUF_SIZE);
        char *curbuf = SPAN_BUFFER(spans_big + ii);
        ASSERT_EQ(0, memcmp(curbuf, expected, BIG_BUF_SIZE));

        netbuf_release_span(&mgr, spans_big + ii);
    }

    for (int ii = 0; ii < n_smallspans; ii++) {
        char expected[SMALL_BUF_SIZE];
        char *curbuf = SPAN_BUFFER(spans_small + ii);
        memset(expected, ii, SMALL_BUF_SIZE);
        ASSERT_EQ(0, memcmp(curbuf, expected, SMALL_BUF_SIZE));
        netbuf_release_span(&mgr, spans_small + ii);
    }

    nb_IOV iov[20];
    netbuf_start_flush(&mgr, iov, 20);
    netbuf_cleanup(&mgr);
}

static void test_flush(void)
{
    nb_MGR mgr;
    netbuf_init(&mgr);

    mgr.basealloc = 8;

    nb_SPAN span;
    span.size = 32;
    int rv = netbuf_reserve_span(&mgr, &span);
    ASSERT_EQ(rv, 0);

    nb_IOV iov[10];
    unsigned int sz = netbuf_start_flush(&mgr, iov, 1);
    ASSERT_EQ(32, sz);
    ASSERT_EQ(32, iov[0].iov_len);

    netbuf_end_flush(&mgr, 20);

    sz = netbuf_start_flush(&mgr, iov, 1);
    ASSERT_EQ(12, sz);
    netbuf_end_flush(&mgr, 12);
    netbuf_release_span(&mgr, &span);

    nb_SPAN spans[3];
    for (int ii = 0; ii < 3; ii++) {
        spans[ii].size = 50;
        ASSERT_EQ(0, netbuf_reserve_span(&mgr, spans + ii));
    }

    sz = netbuf_start_flush(&mgr, iov, 10);
    ASSERT_EQ(150, sz);
    netbuf_end_flush(&mgr, 75);
    netbuf_release_span(&mgr, &spans[0]);

    spans[0].size = 20;
    rv = netbuf_reserve_span(&mgr, &spans[0]);
    ASSERT_EQ(0, rv);
    netbuf_dump_status(&mgr);
    netbuf_cleanup(&mgr);
}

static void test_wrapped(void)
{
    nb_MGR mgr;
    netbuf_init(&mgr);
    mgr.basealloc = 40;

    nb_SPAN span1, span2, span3;
    span1.size = 16;
    span2.size = 16;

    int rv;
    rv = netbuf_reserve_span(&mgr, &span1);
    ASSERT_EQ(0, rv);
    rv = netbuf_reserve_span(&mgr, &span2);
    ASSERT_EQ(0, rv);

    ASSERT_EQ(span1.parent, span2.parent);
    ASSERT_EQ(0, span1.offset);
    ASSERT_EQ(16, span2.offset);

    // Wewease Wodewick!
    netbuf_release_span(&mgr, &span1);
    ASSERT_EQ(16, span2.parent->start);

    // So we have 8 bytes at the end..
    ASSERT_EQ(32, span2.parent->wrap);
    span3.size = 10;
    rv = netbuf_reserve_span(&mgr, &span3);

    ASSERT_EQ(0, rv);
    ASSERT_EQ(10, span2.parent->cursor);
    ASSERT_EQ(0, span3.offset);
    ASSERT_EQ(10, span3.parent->cursor);
    ASSERT_EQ(16, span3.parent->start);

    netbuf_release_span(&mgr, &span2);
    ASSERT_EQ(0, span3.parent->start);
    netbuf_release_span(&mgr, &span3);

    netbuf_dump_status(&mgr);

    span1.size = 20;
    rv = netbuf_reserve_span(&mgr, &span1);
    ASSERT_EQ(0, span1.offset);
    ASSERT_EQ(20, span1.parent->cursor);
    ASSERT_EQ(0, span1.parent->start);
    ASSERT_EQ(20, span1.parent->wrap);

    netbuf_dump_status(&mgr);
    netbuf_cleanup(&mgr);
}

static void test_flush2(void)
{
    nb_MGR mgr;
    netbuf_init(&mgr);
    mgr.basealloc = 30;
    nb_SPAN span1, span2, span3;

    span1.size = 10;
    span2.size = 10;
    span3.size = 10;

    int rv;
    rv = netbuf_reserve_span(&mgr, &span1);
    ASSERT_EQ(0, rv);
    rv = netbuf_reserve_span(&mgr, &span2);
    ASSERT_EQ(0, rv);
    rv = netbuf_reserve_span(&mgr, &span3);
    ASSERT_EQ(0, rv);

    ASSERT_EQ(NETBUF_FLUSHED_NONE, netbuf_get_flush_status(&mgr, &span1));
    ASSERT_EQ(NETBUF_FLUSHED_NONE, netbuf_get_flush_status(&mgr, &span2));
    ASSERT_EQ(NETBUF_FLUSHED_NONE, netbuf_get_flush_status(&mgr, &span3));

    nb_IOV iov[10];
    nb_SIZE sz;
    sz = netbuf_start_flush(&mgr, iov, 10);
    ASSERT_EQ(30, sz);

    netbuf_end_flush(&mgr, 15);
    ASSERT_EQ(NETBUF_FLUSHED_FULL, netbuf_get_flush_status(&mgr, &span1));
    ASSERT_EQ(NETBUF_FLUSHED_PARTIAL, netbuf_get_flush_status(&mgr, &span2));
    ASSERT_EQ(NETBUF_FLUSHED_NONE, netbuf_get_flush_status(&mgr, &span3));

    sz = netbuf_start_flush(&mgr, iov, 10);
    ASSERT_EQ(15, sz);
    netbuf_end_flush(&mgr, 6);
    ASSERT_EQ(NETBUF_FLUSHED_FULL, netbuf_get_flush_status(&mgr, &span1));
    ASSERT_EQ(NETBUF_FLUSHED_FULL, netbuf_get_flush_status(&mgr, &span2));
    ASSERT_EQ(NETBUF_FLUSHED_PARTIAL, netbuf_get_flush_status(&mgr, &span3));

    netbuf_release_span(&mgr, &span1);
    span1.size = 5;

    rv = netbuf_reserve_span(&mgr, &span1);
    netbuf_dump_status(&mgr);
    ASSERT_EQ(0, rv);
    ASSERT_EQ(NETBUF_FLUSHED_NONE, netbuf_get_flush_status(&mgr, &span1));
    ASSERT_EQ(NETBUF_FLUSHED_FULL, netbuf_get_flush_status(&mgr, &span2));
    ASSERT_EQ(NETBUF_FLUSHED_PARTIAL, netbuf_get_flush_status(&mgr, &span3));

    sz = netbuf_start_flush(&mgr, iov, 10);
    ASSERT_EQ(14, sz);

    // 9 bytes from span3, 2 bytes from span1
    netbuf_end_flush(&mgr, 11);

    ASSERT_EQ(NETBUF_FLUSHED_FULL, netbuf_get_flush_status(&mgr, &span3));

    sz = netbuf_start_flush(&mgr, iov, 10);
    ASSERT_EQ(3, sz);

    netbuf_end_flush(&mgr, 3);
    sz = netbuf_start_flush(&mgr, iov, 10);
    ASSERT_EQ(0, sz);

    netbuf_release_span(&mgr, &span2);
    netbuf_release_span(&mgr, &span3);
    netbuf_release_span(&mgr, &span1);
    netbuf_cleanup(&mgr);
}

static void test_ooo(void)
{
    nb_MGR mgr;
    netbuf_init(&mgr);

    nb_SPAN spans[3];
    for (int ii = 0; ii < 3; ii++) {
        spans[ii].size = 10;
        int rv = netbuf_reserve_span(&mgr, spans + ii);
        ASSERT_EQ(0, rv);
    }

    netbuf_release_span(&mgr, &spans[1]);
    spans[1].size = 5;
    netbuf_reserve_span(&mgr, &spans[1]);
    for (int ii = 0; ii < 3; ii++) {
        netbuf_release_span(&mgr, spans + ii);
    }

    netbuf_cleanup(&mgr);
}

static void test_structure_sizes(void)
{
    size_t blocksize, spansize;
#ifdef _LP64
    blocksize = 48;
    spansize = 16;
#else
    blocksize = 36;
    spansize = 12;
#endif


    ASSERT_EQ(blocksize, sizeof(nb_BLOCK));
    ASSERT_EQ(spansize, sizeof(nb_SPAN));
}

int main(void)
{
    test_basic();
    test_wrapped();
    test_ooo();
    test_flush();
    test_flush2();
    test_structure_sizes();
    return 0;
}
