// -*- c++ -*-
#pragma once

#include <functional>
#include <vector>
#include <memory>
#include <iostream>

#include <absl/strings/string_view.h>
#include <absl/strings/str_split.h>

#include "lsp-protocol.h"

class EditTextBuffer;

class BufferCollection {
public:
  ~BufferCollection();
  void EventOpen(const DidOpenTextDocumentParams &o);
  void EventSave(const DidSaveTextDocumentParams &){}
  void EventClose(const DidCloseTextDocumentParams &o);
  void EventChange(const DidChangeTextDocumentParams &o);

private:
  std::unordered_map<std::string, EditTextBuffer*> buffers_;
};

class EditTextBuffer {
public:
  EditTextBuffer(absl::string_view initial_text) {
    ReplaceDocument(initial_text);
    std::cerr << "> Got file with " << lines_.size() << " lines\n";
  }

  ~EditTextBuffer() {
    std::cerr << "< Closing buffer with " << edit_count_ << " edits\n";
    ProcessContent([](absl::string_view s) {
      std::cerr << "Final file content\n" << s;
    });
  }

  // Requst to call function "processor" that gets a string_view that is
  // valid for the duration of the call.
  void ProcessContent(const std::function<void(absl::string_view)> &processor);

  void EditLine(const TextDocumentContentChangeEvent &c, std::string *str) {
    int end_char = c.range.end.character;
    if (end_char > (int)str->length() - 2)  // before newline, so -2
      end_char = str->length() - 2;
    if (c.range.start.character > end_char) return;  // mmh
    str->replace(c.range.start.character, end_char, c.text);
    document_length_ = document_length_
      + c.text.length()
      - (end_char - c.range.start.character);
  }

  void ApplyChanges(const std::vector<TextDocumentContentChangeEvent> &changes) {
    for (const auto &c : changes) {
      if (c.has_range) {
        while (c.range.start.line >= (int)lines_.size()) {
          std::cerr << "insert new line\n";
          lines_.emplace_back(new std::string("\n"));
        }
        if (c.range.start.line == c.range.end.line) {
          EditLine(c, lines_[c.range.start.line].get());
        } else {
          std::cerr << "multiline "
                    << c.range.start.line << ":" << c.range.start.character
                    << "-"
                    << c.range.end.line << ":" << c.range.end.character
                    << " len:" << c.text.length() << "'" << c.text << "'\n";
          // we edit first line and
        }
      } else {
        ReplaceDocument(c.text);
      }
      ++edit_count_;
    }
  }

private:
  void ReplaceDocument(absl::string_view content) {
    std::cerr << "ReplaceDocument()\n";
    document_length_ = content.length();
    lines_.clear();
    for (const absl::string_view s : absl::StrSplit(content, '\n')) {
      lines_.emplace_back(new std::string(s));
      lines_.back()->append("\n");  // So that flattening works
    }
  }

  int64_t edit_count_ = 0;
  int64_t document_length_ = 0;  // might be approximate
  std::vector<std::unique_ptr<std::string>> lines_;
};


inline void BufferCollection::EventOpen(const DidOpenTextDocumentParams &o) {
    auto inserted = buffers_.insert({o.textDocument.uri, nullptr});
    if (inserted.second) {
      std::cerr << "Open " << o.textDocument.uri << "\n";
      inserted.first->second = new EditTextBuffer(o.textDocument.text);
    }
  }

inline void BufferCollection::EventClose(const DidCloseTextDocumentParams &o) {
  auto found = buffers_.find(o.textDocument.uri);
  if (found == buffers_.end()) return;
  std::cerr << "Closing " << o.textDocument.uri << "\n";
  delete found->second;
  buffers_.erase(found);
}

inline void BufferCollection::EventChange(const DidChangeTextDocumentParams &o)
{
  auto found = buffers_.find(o.textDocument.uri);
  if (found == buffers_.end()) return;
  found->second->ApplyChanges(o.contentChanges);
}

inline BufferCollection::~BufferCollection() {
  for (const auto &b : buffers_) delete b.second;
}

void EditTextBuffer::ProcessContent(
  const std::function<void(absl::string_view)> &processor) {
  std::string flat_view;
  flat_view.reserve(document_length_ + 65535);
  for (const auto &l : lines_) flat_view.append(*l);
  processor(flat_view);
}
