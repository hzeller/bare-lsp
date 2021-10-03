CXX=g++
CXXFLAGS=-std=c++17 -O3 -W -Wall -Wextra -Wno-unused-parameter
LDFLAGS=-labsl_strings -labsl_status -labsl_throw_delegate
GTEST_LDFLAGS=-lgtest -lgtest_main -lpthread
OBJECTS=main.o fd-mux.o

SCHEMA_COMPILER=third_party/jcxxgen/jcxxgen

all: lsp-server

test:  lsp-text-buffer_test
	for f in $^ ; do ./$$f ; done

lsp-server: $(OBJECTS)
	$(CXX) -o $@ $(OBJECTS) $(LDFLAGS)

lsp-text-buffer_test: lsp-text-buffer_test.cc lsp-text-buffer.h lsp-protocol.h
	$(CXX) -o $@ $< $(CXXFLAGS) $(LDFLAGS) $(GTEST_LDFLAGS)

main.o: main.cc lsp-protocol.h json-rpc-server.h message-stream-splitter.h lsp-text-buffer.h

json-rpc-server.h: lsp-protocol.h

lsp-protocol.h: lsp-protocol.yaml

format:
	clang-format -i *.cc *.h

%.h : %.yaml $(SCHEMA_COMPILER)
	$(SCHEMA_COMPILER) $< -o $@

$(SCHEMA_COMPILER):
	$(MAKE) -C third_party/jcxxgen

clean:
	rm -f $(OBJECTS) lsp-protocol.h lsp-server lsp-text-buffer_test
