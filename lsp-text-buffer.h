// -*- c++ -*-
#pragma once

#include <functional>
#include <iostream>
#include <memory>
#include <vector>

//
#include <absl/strings/str_cat.h>
#include <absl/strings/str_split.h>
#include <absl/strings/string_view.h>

#include "lsp-protocol.h"

class EditTextBuffer;

class BufferCollection {
 public:
  void EventOpen(const DidOpenTextDocumentParams &o);
  void EventSave(const DidSaveTextDocumentParams &) {}
  void EventClose(const DidCloseTextDocumentParams &o);
  void EventChange(const DidChangeTextDocumentParams &o);

 private:
  std::unordered_map<std::string, std::unique_ptr<EditTextBuffer>> buffers_;
};

class EditTextBuffer {
 public:
  using ContentProcessFun = std::function<void(absl::string_view)>;

  EditTextBuffer(absl::string_view initial_text);

  // Requst to call function "processor" that gets a string_view with the
  // current state that is valid for the duration of the call.
  void ProcessContent(const ContentProcessFun &processor) const;

  void ApplyChanges(const std::vector<TextDocumentContentChangeEvent> &cc);

  // Apply a LSP edit operatation.
  bool ApplyChange(const TextDocumentContentChangeEvent &c);

  // Lines in this document.
  size_t lines() const { return lines_.size(); }

  // Length of document in bytes.
  int64_t document_length() const { return document_length_; }

  // Number of edits applied to this document since the start. Can be used
  // as an ever increasing 'version number' of sorts.
  int64_t edit_count() const { return edit_count_; }

 private:
  // TODO: this should be unique_ptr, but assignment in the insert() command
  // will not work. Needs to be formulated with something something std::move
  using LineVector = std::vector<std::shared_ptr<std::string>>;

  static LineVector GenerateLines(absl::string_view content);
  void ReplaceDocument(absl::string_view content);
  bool EditLine(const TextDocumentContentChangeEvent &c, std::string *str);

  int64_t edit_count_ = 0;
  int64_t document_length_ = 0;  // might be approximate
  LineVector lines_;
};
