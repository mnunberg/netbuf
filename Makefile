all: libnetbuf.so

CFLAGS=-Wall -std=c89
LFLAGS=-Wl,-rpath='$$ORIGIN' -L$(shell pwd)

libnetbuf.so: netbufs.c
	$(CC) $(CFLAGS) -shared -o $@ -fPIC $^

test: test.c libnetbuf.so
	$(CC) $(CFLAGS) -std=c99 -o test test.c $(LFLAGS) -lnetbuf
