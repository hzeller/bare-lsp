CXX=g++
CXXFLAGS=-std=c++17 -O3 -W -Wall -Wextra -Wno-unused-parameter
LDFLAGS=-labsl_strings -labsl_status -labsl_throw_delegate
GTEST_LDFLAGS=-lgtest -lgtest_main -lpthread
OBJECTS=fd-mux.o message-stream-splitter.o json-rpc-dispatcher.o lsp-text-buffer.o

SCHEMA_COMPILER=third_party/jcxxgen/jcxxgen

all: lsp-server

test:  lsp-text-buffer_test
	for f in $^ ; do ./$$f ; done

lsp-server: main.o $(OBJECTS)
	$(CXX) -o $@ $^ $(LDFLAGS)

lsp-text-buffer_test: lsp-text-buffer_test.o $(OBJECTS)
	$(CXX) -o $@ $^ $(LDFLAGS) $(GTEST_LDFLAGS);

main.o: main.cc lsp-protocol.h json-rpc-dispatcher.h message-stream-splitter.h lsp-text-buffer.h

lsp-protocol.h: lsp-protocol.yaml
lsp-text-buffer.o: lsp-protocol.h

format:
	clang-format -i *.cc *.h

%.h : %.yaml $(SCHEMA_COMPILER)
	$(SCHEMA_COMPILER) $< -o $@

$(SCHEMA_COMPILER):
	$(MAKE) -C third_party/jcxxgen

clean:
	rm -f $(OBJECTS) lsp-protocol.h lsp-server lsp-text-buffer_test
