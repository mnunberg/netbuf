CXX=clang++
CXXFLAGS=-Wall -ggdb3
LFLAGS=-Wl,-rpath='$$ORIGIN' -L. -lnetbuf++

all: libnetbuf++.so test

libnetbuf++.so: netbufs.cc
	$(CXX) $(CXXFLAGS) -o $@ -shared -fPIC $^ -std=c++98


test: test-new.cc libnetbuf++.so
	$(CXX) $^ $(CXXFLAGS) -o $@ $(LFLAGS)
