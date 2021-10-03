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

  // Requst to call function "processor" that gets a string_view that is
  // valid for the duration of the call.
  void ProcessContent(const std::function<void(absl::string_view)> &processor);

  void ApplyChanges(const std::vector<TextDocumentContentChangeEvent> &cc) {
    for (const auto &c : cc) ApplyChange(c);
  }

  bool ApplyChange(const TextDocumentContentChangeEvent &c) {
    ++edit_count_;
    if (!c.has_range) {
      ReplaceDocument(c.text);
      return true;
    }

    if (c.range.end.line >= (int)lines_.size()) {
      lines_.emplace_back(new std::string(""));
    }

    if (c.range.start.line == c.range.end.line &&
        c.text.find_first_of('\n') == std::string::npos) {
      return EditLine(c, lines_[c.range.start.line].get());
    } else {
      // Multiline edit.
      const absl::string_view start_line = *lines_[c.range.start.line];
      const auto before = start_line.substr(0, c.range.start.character);

      const absl::string_view end_line = *lines_[c.range.end.line];
      const auto behind = end_line.substr(c.range.end.character);
      const std::string new_content = absl::StrCat(before, c.text, behind);

      const auto before_begin = lines_.begin() + c.range.start.line;
      const auto before_end  = lines_.begin() + c.range.end.line + 1;
      document_length_ -= std::accumulate(
        before_begin, before_end, 0,
        [](int sum, const LineVector::value_type &line) {
          return sum + line->length();
        });
      document_length_ += new_content.length();
      LineVector regenerated_lines = GenerateLines(new_content);
      lines_.erase(before_begin, before_end);
      lines_.insert(lines_.begin() + c.range.start.line,
                    regenerated_lines.begin(), regenerated_lines.end());
      return true;
    }
  }

  size_t lines() const { return lines_.size(); }
  int64_t document_length() const { return document_length_; }

  // Number of edits applied to this document since the start. Can be used
  // as an ever increasing 'version number' of sorts.
  int64_t edit_count() const { return edit_count_; }

private:
  // TODO: this should be unique_ptr, but assignment in the insert() command
  // will not work with that.
  using LineVector = std::vector<std::shared_ptr<std::string>>;

  static LineVector GenerateLines(absl::string_view content) {
    LineVector result;
    for (const absl::string_view s : absl::StrSplit(content, '\n')) {
      result.emplace_back(new std::string(s));
      result.back()->append("\n");
    }

    // Files that do or do not have a newline file-ending: represent correctly.
    if (!content.empty() && content.back() == '\n') {
      result.pop_back();
    } else {
      result.back()->pop_back();
    }

    return result;
  }

  void ReplaceDocument(absl::string_view content) {
    document_length_ = content.length();
    if (content.empty()) return;
    lines_ = GenerateLines(content);
  }

  bool EditLine(const TextDocumentContentChangeEvent &c, std::string *str) {
    int end_char = c.range.end.character;

    const int str_end = str->back() == '\n' ? str->length()-1 : str->length();
    if (c.range.start.character > str_end) return false;
    if (end_char > str_end) end_char = str_end;
    if (end_char < c.range.start.character) return false;
    document_length_ -= str->length();
    const absl::string_view assembly = *str;
    const auto before = assembly.substr(0, c.range.start.character);
    const auto behind = assembly.substr(end_char);
    *str = absl::StrCat(before, c.text, behind);
    document_length_ += str->length();
    return true;
  }

  int64_t edit_count_ = 0;
  int64_t document_length_ = 0;  // might be approximate
  LineVector lines_;
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
