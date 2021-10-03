#include <ctype.h>
#include <unistd.h>

#include "fd-mux.h"
#include "json-rpc-dispatcher.h"
#include "lsp-protocol.h"
#include "lsp-text-buffer.h"
#include "message-stream-splitter.h"

void PrintStats(const MessageStreamSplitter &source,
                const JsonRpcDispatcher &server);

// The "initialize" method requests server capabilities.
InitializeResult InitializeServer(const nlohmann::json params) {
  // Ignore passed client capabilities right now, just announce what we do.
  InitializeResult result;
  result.serverInfo = {
      .name = "Henner Zeller bare-lsp",
      .version = "0.1",
  };
  result.capabilities = {
      {
          "textDocumentSync",
          {
              {"openClose", true},  // Want open/close events
              {"change", 2},        // Incremental updates
          },
      },
      {"hoverProvider", true},  // We provide textDocument/hover
  };
  return result;
}

// Example of a simple hover request: we just report how long the word
// is we're hovering over.
nlohmann::json HandleHoverRequest(const BufferCollection &buffers,
                                  const HoverParams &p) {
  const EditTextBuffer *buffer = buffers.findBufferByUri(p.textDocument.uri);
  if (!buffer) return nullptr;

  const int col = p.position.character;
  int word_length = -1;
  Hover result;
  result.range.start.line = result.range.end.line = p.position.line;
  result.range.start.character = result.range.end.character = col;
  buffer->RequestLine(
      p.position.line, [&word_length, &result, col](absl::string_view line) {
        if (col >= (int)line.length()) return;

        // mark range.
        int start = col;
        while (start >= 0 && !isspace(line[start])) {
          --start;
        }
        result.range.start.character = start;

        int end = col;
        while (end <= (int)line.length() && !isspace(line[end])) {
          ++end;
        }
        result.range.end.character = end;
        word_length = end - start - 1;
      });
  if (word_length < 0) return nullptr;

  result.contents.value =
      "A word with **" + std::to_string(word_length) + "** letters";
  result.has_range = true;

  return result;
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

  // The buffer collection keeps track of all the buffers opened in the editor
  // passes edit events it receives from the dispatcher to it.
  BufferCollection buffers(&dispatcher);

  // Exchange of capabilities.
  dispatcher.AddRequestHandler("initialize", InitializeServer);
  dispatcher.AddNotificationHandler("initialized", [](const nlohmann::json &) {
    std::cerr << "From client confirmed: Initialized!\n";
  });

  // The server will tell use to shut down but also notifies us on exit. Use
  // any of these as hints to finish our service.
  bool shutdown_requested = false;
  dispatcher.AddRequestHandler("shutdown",
                               [&shutdown_requested](const nlohmann::json &) {
                                 shutdown_requested = true;
                                 return nullptr;
                               });
  dispatcher.AddNotificationHandler(
      "exit", [&shutdown_requested](const nlohmann::json &) {
        shutdown_requested = true;
        return nullptr;
      });

  dispatcher.AddRequestHandler("textDocument/hover",
                               [&buffers](const HoverParams &p) {
                                 return HandleHoverRequest(buffers, p);
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
  file_multiplexer.RunOnReadable(in_fd, [&]() {
    auto status = stream_splitter.PullFrom([&](char *buf, int size) -> int {  //
      return read(in_fd, buf, size);
    });
    return status.ok() && !shutdown_requested;
  });

  file_multiplexer.RunOnIdle([]() {
    // No editing going on in a while, so TODO let's go through all the buffers
    // and do some linting and send some diagnosis.
    // std::cerr << "Idle call\n";  // TODO: actually do something :)
    return true;
  });

  file_multiplexer.Loop();

  PrintStats(stream_splitter, dispatcher);
  return 0;
}

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
