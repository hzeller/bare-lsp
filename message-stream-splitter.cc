#include "message-stream-splitter.h"

static constexpr absl::string_view kEndHeaderMarker = "\r\n\r\n";
static constexpr absl::string_view kContentLengthHeader = "Content-Length: ";

absl::Status MessageStreamSplitter::PullFrom(const ReadFun &read_fun) {
  if (!message_processor_) {
    return absl::FailedPreconditionError(
        "MessageStreamSplitter: Message "
        "processor not yet set, needed "
        "before ProcessInput() called");
  }
  return ReadInput(read_fun);
}

// Return -1 if header is incomplete (not enough data yet).
// Return -2 if header complete, but does not contain a valid
//    Content-Length header (i.e. an actual problem)
// On success, returns the offset to the body and its size in "body_size"
int MessageStreamSplitter::ParseHeaderGetBodyOffset(absl::string_view data,
                                                    int *body_size) {
  auto end_of_header = data.find(kEndHeaderMarker);
  if (end_of_header == absl::string_view::npos) return -1;  // incomplete

  // Very dirty search for header - we don't check if starts with line.
  const absl::string_view header_content(data.data(), end_of_header);
  auto found_ContentLength_header = header_content.find(kContentLengthHeader);
  if (found_ContentLength_header == absl::string_view::npos) return -2;

  size_t end_key = found_ContentLength_header + kContentLengthHeader.size();
  absl::string_view body_size_start_of_value = {
      header_content.data() + end_key, header_content.size() - end_key};
  if (!absl::SimpleAtoi(body_size_start_of_value, body_size)) {
    return -2;
  }

  return end_of_header + kEndHeaderMarker.size();
}

// Read from data and process all fully available messages found in data.
// Updates "data" to return the remaining unprocessed data.
// Returns ok() status unless header is corrupted or one of the
// message processor call fails.
absl::Status MessageStreamSplitter::ProcessContainedMessages(
    absl::string_view *data) {
  while (!data->empty()) {
    int body_size;
    const int body_offset = ParseHeaderGetBodyOffset(*data, &body_size);
    if (body_offset == -2) {
      absl::string_view limited_view(data->data(),
                                     std::min(data->size(), (size_t)256));
      return absl::InvalidArgumentError(
          absl::StrCat("No `Content-Length:` header. '", limited_view, "...'"));
    }

    const int message_size = body_offset + body_size;
    if (body_offset < 0 || message_size > (int)data->size())
      return absl::OkStatus();  // Only insufficient partial buffer available.

    absl::string_view header(data->data(), body_offset);
    absl::string_view body(data->data() + body_offset, body_size);
    message_processor_(header, body);

    stats_largest_body_ = std::max(stats_largest_body_, body.size());

    *data = {data->data() + message_size, data->size() - message_size};
  }
  return absl::OkStatus();
}

// Read from "read_fun", fill internal buffer and call all available
// complete messages in it.
absl::Status MessageStreamSplitter::ReadInput(const ReadFun &read_fun) {
  char *begin_of_read = read_buffer_.get();
  int available_read_space = read_buffer_size_;

  // Get all we had left from last time to the beginning of the buffer.
  // This is in the same buffer, so we need to memmove()
  if (!pending_data_.empty()) {
    memmove(begin_of_read, pending_data_.data(), pending_data_.size());
    begin_of_read += pending_data_.size();
    available_read_space -= pending_data_.size();
  }

  int bytes_read = read_fun(begin_of_read, available_read_space);
  if (bytes_read <= 0) {
    return absl::UnavailableError(absl::StrCat("read() returned ", bytes_read));
  }
  stats_total_bytes_read_ += bytes_read;

  absl::string_view data(read_buffer_.get(), pending_data_.size() + bytes_read);
  if (auto status = ProcessContainedMessages(&data); !status.ok()) {
    return status;
  }

  pending_data_ = data;  // Remember for next round.

  return absl::OkStatus();
}
