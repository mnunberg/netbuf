#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include "netbufs.h"

#define LIMIT 3000000
#define JLIMIT 20
static unsigned int ntotal_alloc = 0;

const char foo[100] = { 'f', 'o', 'o' };

int main(void)
{
    int ii;
    nb_MGR mgr;
    nb_SETTINGS settings;
    netbuf_default_settings(&settings);
    settings.data_cacheblocks = 0;
    netbuf_init(&mgr, &settings);

    for (ii = 0; ii < LIMIT; ii++) {
        int jj;
        nb_SPAN spans[JLIMIT];

        for (jj = 0; jj < JLIMIT; jj++) {
            spans[jj].size = 200 * (jj+1);
            netbuf_mblock_reserve(&mgr, spans + jj);
            ntotal_alloc += spans[jj].size;
            memcpy(SPAN_BUFFER(spans + jj), foo, sizeof(foo));
        }

        for (jj = 0; jj < JLIMIT; jj++) {
            netbuf_mblock_release(&mgr, spans + jj);
        }
    }
    netbuf_cleanup(&mgr);
    return 0;
}
