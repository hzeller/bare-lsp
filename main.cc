#include <ctype.h>
#include <signal.h>
#include <unistd.h>

#include "fd-mux.h"
#include "json-rpc-dispatcher.h"
#include "lsp-protocol.h"
#include "lsp-text-buffer.h"
#include "message-stream-splitter.h"

// A signal that should trigger exiting the loop
static volatile sig_atomic_t caught_exit_trigger_signal = 0;

static void receive_signal(int signo) {
  static const char msg[] = "Caught signal. Shutting down ASAP.\n";
  if (!caught_exit_trigger_signal) {  // only print message once.
    write(STDERR_FILENO, msg, sizeof(msg));
  }
  caught_exit_trigger_signal = 1;
}

static void arm_signal_handler();

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
      {"documentFormattingProvider", true},
      {"documentRangeFormattingProvider", true},
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
        ++start;
        result.range.start.character = start;

        int end = col;
        while (end < (int)line.length() && !isspace(line[end])) {
          ++end;
        }
        result.range.end.character = end;
        word_length = end - start;
      });
  if (word_length < 0) return nullptr;

  result.contents.value =
      "A word with **" + std::to_string(word_length) + "** letters";
  result.has_range = true;

  return result;
}

// Formatting example: center text.
std::vector<TextEdit> HandleFormattingRequest(
    const BufferCollection &buffers, const DocumentFormattingParams &p) {
  const EditTextBuffer *buffer = buffers.findBufferByUri(p.textDocument.uri);
  if (!buffer) return {};

  std::vector<TextEdit> result;
  buffer->RequestContent([&p, &result](absl::string_view content) {
    const std::vector<absl::string_view> lines = absl::StrSplit(content, '\n');
    const int start_line = p.has_range ? p.range.start.line : 0;
    const int end_line = p.has_range ? p.range.end.line : (int)lines.size();
    int longest_line = 0;
    for (int i = start_line; i < end_line; ++i) {
      absl::string_view just_text = absl::StripAsciiWhitespace(lines[i]);
      longest_line = std::max(longest_line, (int)just_text.length());
    }
    for (int i = start_line; i < end_line; ++i) {
      absl::string_view just_text = absl::StripAsciiWhitespace(lines[i]);
      const int needs_spaces = (longest_line - just_text.length()) / 2;
      int existing_spaces = 0;
      for (const char c : lines[i]) {
        if (!isspace(c)) break;
        ++existing_spaces;
      }
      if (existing_spaces == needs_spaces) continue;

      result.emplace_back(TextEdit{
          .range = {{i, 0}, {i, existing_spaces}},
          .newText = std::string(needs_spaces, ' '),
      });
    }
  });
  return result;
}

void RunDiagnostics(const std::string &uri, const EditTextBuffer &buffer,
                    JsonRpcDispatcher *dispatcher) {
  // We complain about all words that are ... "wrong" :)
  static constexpr absl::string_view kComplainWord = "wrong";
  PublishDiagnosticsParams params;
  params.uri = uri;
  buffer.RequestContent([&](absl::string_view content) {
    int pos_line = 0;
    for (absl::string_view line : absl::StrSplit(content, '\n')) {
      size_t col = 0;
      while ((col = line.find(kComplainWord, col)) != absl::string_view::npos) {
        params.diagnostics.emplace_back(Diagnostic{
            .range = {{pos_line, (int)col},
                      {pos_line, (int)(col + kComplainWord.length())}},
            .message = "That word is wrong :)",
        });
        col += kComplainWord.length();
      }
      pos_line++;
    }
  });
  if (!params.diagnostics.empty()) {
    dispatcher->SendNotification("textDocument/publishDiagnostics", params);
  }
}

int main(int argc, char *argv[]) {
  std::cerr << "Greetings! bare-lsp started.\n";

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
  dispatcher.AddRequestHandler("textDocument/formatting",
                               [&buffers](const DocumentFormattingParams &p) {
                                 return HandleFormattingRequest(buffers, p);
                               });
  dispatcher.AddRequestHandler("textDocument/rangeFormatting",
                               [&buffers](const DocumentFormattingParams &p) {
                                 return HandleFormattingRequest(buffers, p);
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
  static constexpr int kIdleTimeoutMs = 300;
  FDMultiplexer file_multiplexer(kIdleTimeoutMs);

  // Whenever there is something to read from stdin, feed our message
  // to the stream splitter which will in turn call the JSON rpc dispatcher
  file_multiplexer.RunOnReadable(in_fd, [&]() {
    auto status = stream_splitter.PullFrom([&](char *buf, int size) -> int {  //
      return read(in_fd, buf, size);
    });
    if (!status.ok()) {
      std::cerr << status.message() << "\n";
    }
    return status.ok() && !shutdown_requested && !caught_exit_trigger_signal;
  });

  // Run diagnostics in idle time, but make sure to only look at buffers that
  // have changed since our last visit.
  int64_t last_version_processed = 0;
  file_multiplexer.RunOnIdle([&]() {
    if (buffers.global_version() == last_version_processed) return true;
    buffers.Map([&](const std::string &uri, const EditTextBuffer &buffer) {
      if (buffer.last_global_version() <= last_version_processed) return;
      RunDiagnostics(uri, buffer, &dispatcher);
    });
    last_version_processed = buffers.global_version();
    return true;
  });

  arm_signal_handler();
  file_multiplexer.Loop();

  if (caught_exit_trigger_signal) {
    std::cerr << "Exiting due to signal\n";
  }
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

static void arm_signal_handler() {
  caught_exit_trigger_signal = 0;

  struct sigaction sa = {};
  sa.sa_handler = receive_signal;

  // We might get multiple signals on shutdown, so not using SA_RESETHAND
  sa.sa_flags = 0;
  sigaction(SIGTERM, &sa, NULL);  // Regular kill
  sigaction(SIGINT, &sa, NULL);   // Ctrl-C

  sigaction(SIGSEGV, &sa, NULL);
  sigaction(SIGBUS, &sa, NULL);
  sigaction(SIGFPE, &sa, NULL);
}
