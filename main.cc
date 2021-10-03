#include <unistd.h>

#include "fd-mux.h"
#include "json-rpc-server.h"
#include "lsp-protocol.h"
#include "lsp-text-buffer.h"
#include "message-stream-splitter.h"

void PrintStats(const MessageStreamSplitter &source,
                const JsonRpcServer &server) {
  fprintf(stderr, "--------------- Statistic Counters Stats ---------------\n");
  fprintf(stderr, "Total bytes : %9ld\n", source.StatTotalBytesRead());
  fprintf(stderr, "Largest body: %9ld\n", source.StatLargestBodySeen());

  fprintf(stderr, "\n--- Methods called ---\n");
  int longest = 0;
  for (const auto &stats : server.GetStatCounters()) {
    longest = std::max(longest, (int)stats.first.length());
  }
  for (const auto &stats : server.GetStatCounters()) {
    fprintf(stderr, "%*s %9d\n", longest, stats.first.c_str(), stats.second);
  }
}

int main() {
  JsonRpcServer::WriteFun write_fun = [](absl::string_view reply) {
    std::cout << "Content-Length: " << reply.size() << "\r\n\r\n";
    std::cout << reply;
  };

  MessageStreamSplitter source(1 << 20);
  JsonRpcServer server(write_fun);

  source.SetMessageProcessor(
      [&server](absl::string_view /*header*/, absl::string_view body) {
        return server.DispatchMessage(body);
      });

  BufferCollection buffers;

  // Wire up the client notifications to the buffer
  server.AddNotificationHandler(
      "textDocument/didOpen",
      [&buffers](const DidOpenTextDocumentParams &p) { buffers.EventOpen(p); });
  server.AddNotificationHandler(
      "textDocument/didSave",
      [&buffers](const DidSaveTextDocumentParams &p) { buffers.EventSave(p); });
  server.AddNotificationHandler(
      "textDocument/didClose", [&buffers](const DidCloseTextDocumentParams &p) {
        buffers.EventClose(p);
      });
  server.AddNotificationHandler(
      "textDocument/didChange",
      [&buffers](const DidChangeTextDocumentParams &p) {
        buffers.EventChange(p);
      });

  static constexpr int kIdleTimeout = 100;
  FDMultiplexer file_multiplexer(kIdleTimeout);

  file_multiplexer.RunOnReadable(STDIN_FILENO, [&source]() {
    absl::Status status = source.PullFrom([](char *buf, int size) -> int {
      return read(STDIN_FILENO, buf, size);
    });
    return status.ok();
  });

  file_multiplexer.RunOnIdle([]() {
    // No editing going on in a while, so TODO let's go through all the buffers
    // and do some linting and send some unsolicited diagnosis.
    std::cerr << "Idle call\n";
    return true;
  });

  file_multiplexer.Loop();

  PrintStats(source, server);

  return 0;
}
