CXX=g++
CXXFLAGS=-std=c++17 -O3 -W -Wall -Wextra -Wno-unused-parameter
LDFLAGS=-labsl_strings -labsl_status -labsl_throw_delegate
GTEST_LDFLAGS=-lgtest -lgtest_main -lpthread
SCHEMA_COMPILER=../jcxxgen/jcxxgen

all: lsp-server lsp-text-buffer_test

lsp-server: main.o
	$(CXX) -o $@ $^ $(LDFLAGS)

lsp-text-buffer_test: lsp-text-buffer_test.cc lsp-text-buffer.h
	$(CXX) -o $@ $< $(CXXFLAGS) $(LDFLAGS) $(GTEST_LDFLAGS)

main.o: main.cc lsp-protocol.h json-rpc-server.h lsp-protocol.h message-stream-splitter.h lsp-text-buffer.h

main.cc:

json-rpc-server.h: lsp-protocol.h

lsp-protocol.h: lsp-protocol.yaml

%.h : %.yaml $(SCHEMA_COMPILER)
	$(SCHEMA_COMPILER) $< -o $@

clean:
	rm -f main.o lsp-protocol.h lsp-server lsp-text-buffer_test
