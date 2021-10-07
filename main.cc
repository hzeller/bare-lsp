#include <ctype.h>
#include <signal.h>
#include <unistd.h>

#include "file-event-dispatcher.h"
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
      {"documentFormattingProvider", true},
      {"documentRangeFormattingProvider", true},
      {"documentHighlightProvider", true},
      {"documentSymbolProvider", true},
      {"codeActionProvider", true},
  };
  return result;
}

// Looks at the surroundings of word for surroundings of non-space.
static absl::string_view ExtractWordAtPos(absl::string_view line, int pos) {
  if (pos >= (int)line.length()) return {line.data() + line.size() - 1, 0};

  // TODO: this probably would be nicer with some std::-algorithms.
  int start = pos;
  while (start >= 0 && !isspace(line[start])) {
    --start;
  }
  ++start;

  int end = pos;
  while (end < (int)line.length() && !isspace(line[end])) {
    ++end;
  }
  return line.substr(start, end - start);
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
        auto w = ExtractWordAtPos(line, col);
        result.range.start.character = w.data() - line.data();
        result.range.end.character = result.range.start.character + w.length();
        word_length = w.length();
      });
  if (word_length < 0) return nullptr;

  result.contents.value =
      "A word with **" + std::to_string(word_length) + "** letters";
  result.has_range = true;

  return result;
}

nlohmann::json HandleHighlightRequest(const BufferCollection &buffers,
                                      const DocumentHighlightParams &p) {
  const EditTextBuffer *buffer = buffers.findBufferByUri(p.textDocument.uri);
  if (!buffer) return nullptr;

  std::vector<DocumentHighlight> result;
  buffer->RequestContent([&](absl::string_view content) {
    const std::vector<absl::string_view> lines = absl::StrSplit(content, '\n');
    // First, let's extract the word we're currently on.
    if (p.position.line >= (int)lines.size()) return;
    auto word = ExtractWordAtPos(lines[p.position.line], p.position.character);
    if (word.empty()) return;
    for (int row = 0; row < (int)lines.size(); ++row) {
      const auto &line = lines[row];
      size_t col = 0;
      while ((col = line.find(word, col)) != absl::string_view::npos) {
        const size_t eow = col + word.length();
        // Only if we're surrounded by space, this is a full word.
        const bool is_word = ((col == 0 || isspace(line[col - 1])) &&
                              (eow == line.length() || isspace(line[eow])));
        if (is_word) {
          result.emplace_back(DocumentHighlight{
              .range = {{row, (int)col}, {row, (int)eow}},
          });
          col = eow;
        } else {
          col += 1;
        }
      }
    }
  });
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
      const absl::string_view line = lines[i];
      const absl::string_view just_text = absl::StripAsciiWhitespace(line);
      const int needs_spaces = (longest_line - just_text.length()) / 2;
      result.emplace_back(TextEdit{
          .range = {{i, 0}, {i, (int)(just_text.begin() - line.begin())}},
          .newText = std::string(needs_spaces, ' '),
      });
    }
  });
  return result;
}

std::vector<DiagnosticFixPair> RunLint(const EditTextBuffer &buffer) {
  // We complain about all words that are ... "wrong" :)
  static constexpr absl::string_view kComplainWord = "wrong";
  std::vector<DiagnosticFixPair> result;
  buffer.RequestContent([&](absl::string_view content) {
    int pos_line = 0;
    for (absl::string_view line : absl::StrSplit(content, '\n')) {
      size_t col = 0;
      while ((col = line.find(kComplainWord, col)) != absl::string_view::npos) {
        Range r = {{pos_line, (int)col},
                   {pos_line, (int)(col + kComplainWord.length())}};
        result.emplace_back(DiagnosticFixPair{
            .diagnostic =
                {
                    .range = r,
                    .message = "That word is wrong :)",
                },
            .fixes = {},
        });
        result.back().fixes.emplace_back(
            TitledFix{.title = "Better Word",
                      .edit = {{.range = r, .newText = "correct"}}});
        result.back().fixes.emplace_back(
            TitledFix{.title = "Ambiguous but same length",
                      .edit = {{.range = r, .newText = "right"}}});
        col += kComplainWord.length();
      }
      pos_line++;
    }
  });
  return result;
}

void RunDiagnostics(const std::string &uri, const EditTextBuffer &buffer,
                    JsonRpcDispatcher *dispatcher) {
  PublishDiagnosticsParams params;
  params.uri = uri;
  const auto &lint_result = RunLint(buffer);
  if (lint_result.empty()) return;
  for (const auto &fix_pair : lint_result) {
    params.diagnostics.emplace_back(fix_pair.diagnostic);
  }
  dispatcher->SendNotification("textDocument/publishDiagnostics", params);
}

bool operator<(const Position &a, const Position &b) {
  if (a.line > b.line) return false;
  if (a.line < b.line) return true;
  return a.character < b.character;
}
bool rangeOverlap(const Range &a, const Range &b) {
  return (a.start < b.end && b.start < a.end);
}
std::vector<CodeAction> HandleCodeAction(const BufferCollection &buffers,
                                         const CodeActionParams &p) {
  const EditTextBuffer *buffer = buffers.findBufferByUri(p.textDocument.uri);
  if (!buffer) return {};
  const auto &lint_result = RunLint(*buffer);
  if (lint_result.empty()) return {};
  std::vector<CodeAction> result;
  for (const auto &fix_pair : lint_result) {
    if (!rangeOverlap(fix_pair.diagnostic.range, p.range)) continue;
    bool preferred_fix = true;
    for (const auto &fix : fix_pair.fixes) {
      result.emplace_back(CodeAction{
          .title = fix.title,
          .kind = "quickfix",
          .diagnostics = {fix_pair.diagnostic},
          .isPreferred = preferred_fix,
          // The following is translated from json, map uri -> edits.
          .edit = {.changes = {{p.textDocument.uri, fix.edit}}},
      });
      preferred_fix = false;  // only the first is preferred.
    }
  }
  return result;
}

enum class SymbolKind {
  File = 1,
  // ...
  Namespace = 3,
  // ...
  Variable = 13,
  // ...
};

std::vector<DocumentSymbol> HandleDocumentSymbol(
    const BufferCollection &buffers, const DocumentSymbolParams &p) {
  const EditTextBuffer *buffer = buffers.findBufferByUri(p.textDocument.uri);
  if (!buffer) return {};
  std::vector<DocumentSymbol> result;
  buffer->RequestContent([&p, &result, buffer](absl::string_view content) {
    int line_no = 0;
    result.emplace_back(
      DocumentSymbol{.name = "All the things",
        .kind = static_cast<int>(SymbolKind::File),
        .range = {{0, 0}, {(int)buffer->lines(), 0}},
        .selectionRange = {{0, 0}, {(int)buffer->lines(), 0}},
        .children = nlohmann::json::array(),
        .has_children = true
      });
    nlohmann::json &append_to = result.back().children;
    for (absl::string_view line : absl::StrSplit(content, '\n')) {
      for (absl::string_view word : absl::StrSplit(line, ' ')) {
        const int col = word.data() - line.data();
        const int eow = col + word.length();
        if (word == "world") {
          append_to.push_back(
              DocumentSymbol{.name = "World",
                .kind = static_cast<int>(SymbolKind::Namespace),
                .range = {{line_no, col}, {line_no, eow}},
                .selectionRange = {{line_no, col}, {line_no, eow}},
                .children = nullptr,
                .has_children = false,
              });
        } else if (word == "variable") {
          append_to.push_back(
              DocumentSymbol{
                .name = "Some Variable",
                .kind = static_cast<int>(SymbolKind::Variable),
                .range = {{line_no, col}, {line_no, eow}},
                .selectionRange = {{line_no, col}, {line_no, eow}},
                .children = nullptr,
                .has_children = false,
              });
        }

      }
      ++line_no;
    }
  });
  return result;
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
  bool client_initialized = false;
  dispatcher.AddNotificationHandler("initialized", [&client_initialized](const nlohmann::json &) {
    client_initialized = true;
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
  dispatcher.AddRequestHandler("textDocument/documentHighlight",
                               [&buffers](const DocumentHighlightParams &p) {
                                 return HandleHighlightRequest(buffers, p);
                               });
  dispatcher.AddRequestHandler("textDocument/codeAction",
                               [&buffers](const CodeActionParams &p) {
                                 return HandleCodeAction(buffers, p);
                               });
  dispatcher.AddRequestHandler("textDocument/documentSymbol",
                               [&buffers](const DocumentSymbolParams &p) {
                                 return HandleDocumentSymbol(buffers, p);
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
  FileEventDispatcher file_multiplexer(kIdleTimeoutMs);

  // Whenever there is something to read from stdin, feed our message
  // to the stream splitter which will in turn call the JSON rpc dispatcher
  file_multiplexer.RunOnReadable(in_fd, [&]() {
    auto status = stream_splitter.PullFrom([&](char *buf, int size) -> int {  //
      return read(in_fd, buf, size);
    });
    if (!status.ok()) std::cerr << status.message() << "\n";
    return status.ok() && !shutdown_requested;
  });

  // Run diagnostics in idle time, but make sure to only look at buffers that
  // have changed since our last visit.
  int64_t last_version_processed = 0;
  file_multiplexer.RunOnIdle([&]() {
    if (!client_initialized) return true;
    buffers.MapBuffersChangedSince(
        last_version_processed,
        [&](const std::string &uri, const EditTextBuffer &buffer) {
          RunDiagnostics(uri, buffer, &dispatcher);
        });
    last_version_processed = buffers.global_version();
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
