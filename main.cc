#include <unistd.h>

#include "fd-mux.h"
#include "json-rpc-dispatcher.h"
#include "lsp-protocol.h"
#include "lsp-text-buffer.h"
#include "message-stream-splitter.h"

void PrintStats(const MessageStreamSplitter &source,
                const JsonRpcDispatcher &server) {
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

int main(int argc, char *argv[]) {
  // Input and output is stdin and stdout
  static constexpr int in_fd = STDIN_FILENO;
  JsonRpcDispatcher::WriteFun write_fun = [](absl::string_view reply) {
    // Output formatting as header/body chunk as required by LSP spec.
    std::cout << "Content-Length: " << reply.size() << "\r\n\r\n";
    std::cout << reply << std::flush;
  };

  MessageStreamSplitter stream_splitter(1 << 20);
  JsonRpcDispatcher dispatcher(write_fun);

  // All bodies the stream splitter extracts are pushed to the json dispatcher
  stream_splitter.SetMessageProcessor(
      [&dispatcher](absl::string_view /*header*/, absl::string_view body) {
        return dispatcher.DispatchMessage(body);
      });

  BufferCollection buffers;

  // Wire up the client notifications to the buffer
  dispatcher.AddNotificationHandler(
      "textDocument/didOpen", [&buffers](const DidOpenTextDocumentParams &p) {
        buffers.didOpenEvent(p);
      });
  dispatcher.AddNotificationHandler(
      "textDocument/didSave", [&buffers](const DidSaveTextDocumentParams &p) {
        buffers.didSaveEvent(p);
      });
  dispatcher.AddNotificationHandler(
      "textDocument/didClose", [&buffers](const DidCloseTextDocumentParams &p) {
        buffers.didCloseEvent(p);
      });
  dispatcher.AddNotificationHandler(
      "textDocument/didChange",
      [&buffers](const DidChangeTextDocumentParams &p) {
        buffers.didChangeEvent(p);
      });

  /* For the actual processing, we want to do extra diagnostics in idle time
   * whenever we don't get updates for a while (i.e. user stopped typing)
   * and use that to analyze things that don't need immediage attention (e.g.
   * linting warnings).
   *
   * Using a simple event manager that watches the input stream and calls
   * on idle is achieving this task and will also allow us to work
   * single-threaded easily.
   */
  static constexpr int kIdleTimeoutMs = 100;
  FDMultiplexer file_multiplexer(kIdleTimeoutMs);

  // Whenever there is something to read from stdin, feed our message
  // to the stream splitter which will in turn call the JSON rpc dispatcher
  file_multiplexer.RunOnReadable(in_fd, [&stream_splitter]() {
    auto status = stream_splitter.PullFrom([](char *buf, int size) -> int {  //
      return read(in_fd, buf, size);
    });
    return status.ok();
  });

  file_multiplexer.RunOnIdle([]() {
    // No editing going on in a while, so TODO let's go through all the buffers
    // and do some linting and send some diagnosis.
    std::cerr << "Idle call\n";  // TODO: actually do something :)
    return true;
  });

  file_multiplexer.Loop();

  PrintStats(stream_splitter, dispatcher);

  return 0;
}
