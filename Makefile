CFLAGS=-Wall -std=c89 -ggdb3 -O2 -Wstrict-aliasing -Wextra 
PROXYFLAGS=-DNETBUFS_LIBC_PROXY
LFLAGS=-Wl,-rpath='$$ORIGIN' -L$(shell pwd)

all: libnetbuf.so libnetbuf32.so test test32 libnetbuf-proxy.so test-proxy

clean:
	rm -f libnetbuf.so libnetbuf32.so test test32

libnetbuf.so: netbufs.c
	$(CC) $(CFLAGS) -shared -o $@ -fPIC $^

libnetbuf32.so: netbufs.c
	$(CC) -m32 $(CFLAGS) -shared -o $@ -fPIC $^

libnetbuf-proxy.so: netbufs.c
	$(CC) $(CFLAGS) $(PROXYFLAGS) -shared -o $@ -fPIC $^


test: test.c libnetbuf.so
	$(CC) $(CFLAGS) -o $@ test.c $(LFLAGS) -lnetbuf

test-proxy: test.c libnetbuf-proxy.so
	$(CC) $(CFLAGS) $(PROXYFLAGS) -o $@ test.c $(LFLAGS) -lnetbuf-proxy

test32: test.c libnetbuf32.so
	$(CC) -m32 $(CFLAGS) -o $@ test.c $(LFLAGS) -lnetbuf32


bench: bench.c libnetbuf.so
	$(CC) $(CFLAGS) -O0 -o $@ bench.c $(LFLAGS) -lnetbuf

bench-proxy: bench.c libnetbuf-proxy.so
	$(CC) $(CFLAGS) -O0 -o $@ bench.c $(LFLAGS) -lnetbuf-proxy

check: test test32
	./test
	./test32
	./test-proxy
