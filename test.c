#include <stdio.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include "netbufs.h"


#define BIG_BUF_SIZE 5000
#define SMALL_BUF_SIZE 50
#define ASSERT_EQ(a, b) if ((a) != (b)) { *(char *)0x00 = 'A'; }

static void test_basic(void)
{
    nb_MGR mgr;
    int rv;
    int ii;
    int n_bigspans = 20;
    int n_smallspans = 2000;

    nb_SPAN spans_big[20];
    nb_SPAN spans_small[2000];
    netbuf_init(&mgr, NULL);
    netbuf_cleanup(&mgr);
    netbuf_init(&mgr, NULL);


    for (ii = 0; ii < n_bigspans; ii++) {
        int filler = 'a' + ii;
        nb_SPAN *span = spans_big + ii;
        span->size = BIG_BUF_SIZE;
        rv = netbuf_mblock_reserve(&mgr, span);
        ASSERT_EQ(0, rv);
        memset(SPAN_BUFFER(span), filler, span->size);
    }

    for (ii = 0; ii < n_smallspans; ii++) {
        nb_SPAN *span = spans_small + ii;
        int filler = ii;
        span->size = SMALL_BUF_SIZE;
        rv = netbuf_mblock_reserve(&mgr, span);
        ASSERT_EQ(0, rv);
        filler = ii;
        memset(SPAN_BUFFER(span), filler, span->size);
    }

    for (ii = 0; ii < n_bigspans; ii++) {
        char expected[BIG_BUF_SIZE];
        char *curbuf = SPAN_BUFFER(spans_big + ii);

        memset(expected, 'a' + ii, BIG_BUF_SIZE);
        ASSERT_EQ(0, memcmp(curbuf, expected, BIG_BUF_SIZE));

        netbuf_mblock_release(&mgr, spans_big + ii);
    }

    for (ii = 0; ii < n_smallspans; ii++) {
        char expected[SMALL_BUF_SIZE];
        char *curbuf = SPAN_BUFFER(spans_small + ii);
        memset(expected, ii, SMALL_BUF_SIZE);
        ASSERT_EQ(0, memcmp(curbuf, expected, SMALL_BUF_SIZE));
        netbuf_mblock_release(&mgr, spans_small + ii);
    }

    {
        nb_IOV iov[20];
        netbuf_start_flush(&mgr, iov, 20, NULL);
    }
    netbuf_cleanup(&mgr);
}

static void test_flush(void)
{
    nb_MGR mgr;
    nb_SETTINGS settings;
    nb_SPAN span;
    nb_SPAN spans[3];

    int ii;
    int rv;
    nb_IOV iov[10];
    unsigned int sz;

    netbuf_default_settings(&settings);
    settings.data_basealloc = 8;
    netbuf_init(&mgr, &settings);

    span.size = 32;
    rv = netbuf_mblock_reserve(&mgr, &span);
    ASSERT_EQ(rv, 0);

    netbuf_enqueue_span(&mgr, &span);
    sz = netbuf_start_flush(&mgr, iov, 1, NULL);
    ASSERT_EQ(32, sz);
    ASSERT_EQ(32, iov[0].iov_len);
    netbuf_end_flush(&mgr, 20);

    sz = netbuf_start_flush(&mgr, iov, 1, NULL);
    ASSERT_EQ(12, sz);
    netbuf_end_flush(&mgr, 12);
    netbuf_mblock_release(&mgr, &span);

    for (ii = 0; ii < 3; ii++) {
        spans[ii].size = 50;
        ASSERT_EQ(0, netbuf_mblock_reserve(&mgr, spans + ii));
    }

    for (ii = 0; ii < 3; ii++) {
        netbuf_enqueue_span(&mgr, spans + ii);
    }

    sz = netbuf_start_flush(&mgr, iov, 10, NULL);
    ASSERT_EQ(150, sz);
    netbuf_end_flush(&mgr, 75);
    netbuf_mblock_release(&mgr, &spans[0]);

    spans[0].size = 20;
    rv = netbuf_mblock_reserve(&mgr, &spans[0]);
    ASSERT_EQ(0, rv);
    netbuf_mblock_release(&mgr, &spans[0]);

    for (ii = 1; ii < 3; ii++) {
        netbuf_mblock_release(&mgr, spans + ii);
    }

    netbuf_dump_status(&mgr);
    netbuf_cleanup(&mgr);
}

static void test_wrapped(void)
{
    nb_MGR mgr;
    nb_SETTINGS settings;
    int rv;
    nb_SPAN span1, span2, span3;

#ifdef NETBUFS_LIBC_PROXY
    return;
#endif

    netbuf_default_settings(&settings);
    settings.data_basealloc = 40;
    netbuf_init(&mgr, &settings);

    span1.size = 16;
    span2.size = 16;

    rv = netbuf_mblock_reserve(&mgr, &span1);
    ASSERT_EQ(0, rv);
    rv = netbuf_mblock_reserve(&mgr, &span2);
    ASSERT_EQ(0, rv);

    ASSERT_EQ(span1.parent, span2.parent);
    ASSERT_EQ(0, span1.offset);
    ASSERT_EQ(16, span2.offset);

    /* Wewease Wodewick! */
    netbuf_mblock_release(&mgr, &span1);
    ASSERT_EQ(16, span2.parent->start);

    /* So we have 8 bytes at the end.. */
    ASSERT_EQ(32, span2.parent->wrap);
    span3.size = 10;
    rv = netbuf_mblock_reserve(&mgr, &span3);

    ASSERT_EQ(0, rv);
    ASSERT_EQ(10, span2.parent->cursor);
    ASSERT_EQ(0, span3.offset);
    ASSERT_EQ(10, span3.parent->cursor);
    ASSERT_EQ(16, span3.parent->start);

    netbuf_mblock_release(&mgr, &span2);
    ASSERT_EQ(0, span3.parent->start);
    netbuf_mblock_release(&mgr, &span3);

    netbuf_dump_status(&mgr);

    span1.size = 20;
    rv = netbuf_mblock_reserve(&mgr, &span1);
    ASSERT_EQ(0, span1.offset);
    ASSERT_EQ(20, span1.parent->cursor);
    ASSERT_EQ(0, span1.parent->start);
    ASSERT_EQ(20, span1.parent->wrap);

    netbuf_dump_status(&mgr);
    netbuf_cleanup(&mgr);
}

static void assert_iov_eq(nb_IOV *iov, nb_SIZE offset, char expected)
{
    char *buf = (char *)iov->iov_base;
    ASSERT_EQ(expected, buf[offset]);
}

static void test_multi_flush(void)
{
    nb_SETTINGS settings;
    nb_MGR mgr;
    int rv;
    nb_SIZE sz;
    nb_SPAN span1, span2, span3;
    nb_IOV iov[10];

    netbuf_default_settings(&settings);
    netbuf_init(&mgr, &settings);

    span1.size = 50;
    span2.size = 50;
    span3.size = 50;

    rv = netbuf_mblock_reserve(&mgr, &span1);
    ASSERT_EQ(0, rv);
    rv = netbuf_mblock_reserve(&mgr, &span2);
    ASSERT_EQ(0, rv);
    rv = netbuf_mblock_reserve(&mgr, &span3);
    ASSERT_EQ(0, rv);

    netbuf_enqueue_span(&mgr, &span1);
    netbuf_enqueue_span(&mgr, &span2);

    sz = netbuf_start_flush(&mgr, iov, 10, NULL);
    ASSERT_EQ(100, sz);

    memset(SPAN_BUFFER(&span1), 'A', span1.size);
    memset(SPAN_BUFFER(&span2), 'B', span2.size);
    memset(SPAN_BUFFER(&span3), 'C', span3.size);

#ifndef NETBUFS_LIBC_PROXY
    ASSERT_EQ(100, iov->iov_len);
    assert_iov_eq(iov, 0, 'A');
    assert_iov_eq(iov, 50, 'B');

    netbuf_enqueue_span(&mgr, &span3);
    sz = netbuf_start_flush(&mgr, &iov[1], 0, NULL);
    ASSERT_EQ(sz, 50);
    assert_iov_eq(&iov[1], 0, 'C');
    ASSERT_EQ(50, iov[1].iov_len);

    netbuf_dump_status(&mgr);

    netbuf_end_flush(&mgr, 100);
    netbuf_dump_status(&mgr);

    netbuf_end_flush(&mgr, 50);
    sz = netbuf_start_flush(&mgr, iov, 10, NULL);
    ASSERT_EQ(0, sz);
#endif

    netbuf_mblock_release(&mgr, &span1);
    netbuf_mblock_release(&mgr, &span2);
    netbuf_mblock_release(&mgr, &span3);
    netbuf_cleanup(&mgr);
}

static void test_flush2(void)
{
#if 0
    nb_MGR mgr;
    nb_SETTINGS settings;
    netbuf_default_settings(&settings);
    settings.data_basealloc = 30;
    netbuf_init(&mgr, &settings);
    nb_SPAN span1, span2, span3;

    span1.size = 10;
    span2.size = 10;
    span3.size = 10;

    int rv;
    rv = netbuf_mblock_reserve(&mgr, &span1);
    ASSERT_EQ(0, rv);
    rv = netbuf_mblock_reserve(&mgr, &span2);
    ASSERT_EQ(0, rv);
    rv = netbuf_mblock_reserve(&mgr, &span3);
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

    netbuf_mblock_release(&mgr, &span1);
    span1.size = 5;

    rv = netbuf_mblock_reserve(&mgr, &span1);
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

    netbuf_mblock_release(&mgr, &span2);
    netbuf_mblock_release(&mgr, &span3);
    netbuf_mblock_release(&mgr, &span1);
    netbuf_cleanup(&mgr);
#endif
}

static void test_ooo(void)
{
    nb_MGR mgr;
    nb_SPAN spans[3];
    int ii;

    netbuf_init(&mgr, NULL);

    for (ii = 0; ii < 3; ii++) {
        int rv;
        spans[ii].size = 10;
        rv = netbuf_mblock_reserve(&mgr, spans + ii);
        ASSERT_EQ(0, rv);
    }

    netbuf_mblock_release(&mgr, &spans[1]);
    spans[1].size = 5;
    netbuf_mblock_reserve(&mgr, &spans[1]);
    for (ii = 0; ii < 3; ii++) {
        netbuf_mblock_release(&mgr, spans + ii);
    }

    netbuf_cleanup(&mgr);
}
int main(void)
{
    test_basic();
    test_wrapped();
    test_ooo();
    test_flush();
    test_multi_flush();
    test_flush2();
    return 0;
}
