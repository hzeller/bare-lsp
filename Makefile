CXX=g++
CXXFLAGS=-std=c++17 -O3 -W -Wall -Wextra
LDFLAGS=-labsl_strings -labsl_status
SCHEMA_COMPILER=../jcxxgen/schema-compiler

lsp-server: main.o
	$(CXX) -o $@ $^ $(LDFLAGS)

main.o: main.cc json-rpc-server.h lsp-protocol.h

json-rpc-server.h: lsp-protocol.h

lsp-protocol.h: lsp-protocol.yaml

%.h : %.yaml
	$(SCHEMA_COMPILER) $< > $@

clean:
	rm -f main.o lsp-server lsp-protocol.h
