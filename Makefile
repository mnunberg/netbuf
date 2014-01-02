CFLAGS=-Wall -std=c89 -ggdb3
LFLAGS=-Wl,-rpath='$$ORIGIN' -L$(shell pwd)

all: libnetbuf.so libnetbuf32.so test test32

clean:
	rm -f libnetbuf.so libnetbuf32.so test test32

libnetbuf.so: netbufs.c
	$(CC) $(CFLAGS) -shared -o $@ -fPIC $^

libnetbuf32.so: netbufs.c
	$(CC) -m32 $(CFLAGS) -shared -o $@ -fPIC $^

test: test.c libnetbuf.so
	$(CC) $(CFLAGS) -std=c99 -o $@ test.c $(LFLAGS) -lnetbuf

test32: test.c libnetbuf32.so
	$(CC) -m32 $(CFLAGS) -std=c99 -o $@ test.c $(LFLAGS) -lnetbuf32

check: test test32
	./test
	./test32
