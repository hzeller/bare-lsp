// -*- c++ -*-
#ifndef MESSAGE_STREAM_SPLITTER_H
#define MESSAGE_STREAM_SPLITTER_H

#include <functional>
#include <memory>
#include <string>

//
#include <absl/status/status.h>
#include <absl/strings/numbers.h>
#include <absl/strings/str_cat.h>
#include <absl/strings/string_view.h>

// Splits messages that are formatted as header + body coming from some
// abstracted input stream and calls a handler for each complete message it
// receives.
//
// The MessageStreamSplitter does not read data directly from a source but
// gets handed a read function to get the data from. This allows to use this
// in different environments from testing to using it with a filedescriptor
// event dispatcher (select()).
// The simplest implementation of the "ReadFun" just wraps a system read() call.
//
// The header data MUST contain a Content-Length header.
class MessageStreamSplitter {
 public:
  // A function that reads from some source and writes up to "size" bytes
  // into the buffer. Returns the number of bytes read.
  // Blocks until there is content or returns '0' on end-of-file. Values
  // below zero indicate errors.
  // Only the amount of bytes available at the time of the call are filled
  // into the buffer, so the return value can be less than size.
  // So essentially: this behaves like the standard read() system call.
  using ReadFun = std::function<int(char *buf, int size)>;

  // Function called with each complete message that has been extracted from
  // the stream.
  using MessageProcessFun =
      std::function<void(absl::string_view header, absl::string_view body)>;

  // Read using an internal buffer of "read_buffer_size", which must be larger
  // than the largest expected message.
  MessageStreamSplitter(size_t read_buffer_size)
      : read_buffer_size_(read_buffer_size),
        read_buffer_(new char[read_buffer_size]) {}
  MessageStreamSplitter(const MessageStreamSplitter &) = delete;

  // Set the function that will receive extracted message bodies.
  void SetMessageProcessor(const MessageProcessFun &message_processor) {
    message_processor_ = message_processor;
  }

  // The passed "read_fun()" is called exactly _once_ to get
  // the next amount of data and calls the message processor for each complete
  // message found. Partial data received is retained to be re-considered on
  // the next call to PullFrom().
  //
  // Within the context of this method, the message processor might be
  // called zero to multiple times depending on how much data arrives from
  // the read.
  //
  // Note: The once-call behaviour allows to hook this into some file-descriptor
  // event dispatcher (e.g using select()).
  //
  // Returns with an ok status until EOF or some error occurs.
  // Code
  //  - kUnavailable     : regular EOF, no data pending. A 'good' non-ok status.
  //  - kDataloss        : got EOF, but still incomplete data pending.
  //  - kInvalidargument : stream corrupted, couldn't read header.
  absl::Status PullFrom(const ReadFun &read_fun);

  // -- Statistical data

  uint64_t StatLargestBodySeen() const { return stats_largest_body_; }
  uint64_t StatTotalBytesRead() const { return stats_total_bytes_read_; }

 private:
  int ParseHeaderGetBodyOffset(absl::string_view data, int *body_size);
  absl::Status ProcessContainedMessages(absl::string_view *data);
  absl::Status ReadInput(const ReadFun &read_fun);

  const size_t read_buffer_size_;
  std::unique_ptr<char[]> read_buffer_;

  MessageProcessFun message_processor_;

  size_t stats_largest_body_ = 0;
  uint64_t stats_total_bytes_read_ = 0;
  absl::string_view pending_data_;
};

#endif  // MESSAGE_STREAM_SPLITTER_H
