// -*- c++ -*-
#pragma once

#include <functional>
#include <vector>
#include <memory>
#include <iostream>

#include <absl/strings/str_cat.h>
#include <absl/strings/str_split.h>
#include <absl/strings/string_view.h>

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
  }

  ~EditTextBuffer() {
  }

  // Requst to call function "processor" that gets a string_view that is
  // valid for the duration of the call.
  void ProcessContent(const std::function<void(absl::string_view)> &processor);

  void ApplyChanges(const std::vector<TextDocumentContentChangeEvent> &cc) {
    for (const auto &c : cc) ApplyChange(c);
  }

  void ApplyChange(const TextDocumentContentChangeEvent &c) {
    ++edit_count_;
    if (!c.has_range) {
      ReplaceDocument(c.text);
      return;
    }

    if (c.range.start.line >= (int)lines_.size() ||
        c.range.end.line >= (int)lines_.size()) {
      return;  // mmh
    }

    if (c.range.start.line == c.range.end.line) {
      EditLine(c, lines_[c.range.start.line].get());
    } else {
      std::cerr << "multiline " << c.range.start.line << ":"
                << c.range.start.character << "-" << c.range.end.line << ":"
                << c.range.end.character << " len:" << c.text.length() << "'"
                << c.text << "'\n";
      // we edit first line and
    }
  }

  size_t lines() const { return lines_.size(); }
  int64_t document_length() const { return document_length_; }

private:
  void ReplaceDocument(absl::string_view content) {
    document_length_ = content.length();
    lines_.clear();
    if (content.empty()) return;
    for (const absl::string_view s : absl::StrSplit(content, '\n')) {
      lines_.emplace_back(new std::string(s));
      lines_.back()->append("\n");  // So that flattening works
    }
    // Files that do or do not have a newline file-ending: represent correctly.
    if (content.back() == '\n') {
      lines_.pop_back();
    } else {
      lines_.back()->pop_back();
    }
  }

  void EditLine(const TextDocumentContentChangeEvent &c, std::string *str) {
    int end_char = c.range.end.character;

    const int str_end = str->back() == '\n' ? str->length()-1 : str->length();
    if (c.range.start.character > str_end) return;  // TODO: error state ?
    if (end_char > str_end) end_char = str_end;

    document_length_ -= str->length();
    absl::string_view assembly = *str;
    absl::string_view before = assembly.substr(0, c.range.start.character);
    absl::string_view behind = assembly.substr(end_char);
    *str = absl::StrCat(before, c.text, behind);
    document_length_ += str->length();
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
