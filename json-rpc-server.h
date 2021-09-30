#ifndef JSON_RPC_SERVER_
#define JSON_RPC_SERVER_

#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <sys/select.h>
#include <unistd.h>

#include <functional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>

#include <absl/status/statusor.h>
#include <absl/strings/numbers.h>
#include <absl/strings/str_cat.h>
#include <absl/strings/string_view.h>

#include <nlohmann/json.hpp>

#include "lsp-protocol.h"

class JsonRpcServer {
public:
  using RPCHandler = std::function<ResponseMessage (const RequestMessage& r)>;

  // Read from input file descriptor, write to output stream.
  JsonRpcServer(int inputfd, std::ostream &out)
    : input_fd_(inputfd), output_(&out) {
    EnsureBuffer(65535);
  }
  ~JsonRpcServer() { delete scratch_buffer_; }

  absl::Status Run() {
    fd_set rd_fds;
    FD_ZERO(&rd_fds);
    for (;;) {
      FD_SET(input_fd_, &rd_fds);
      int sret = select(input_fd_ + 1, &rd_fds, nullptr, nullptr, nullptr);
      if (sret < 0) return absl::UnknownError(
        absl::StrCat("select() on fd=", input_fd_, ": ", strerror(errno)));

      auto status = ReadInput(input_fd_,
                              [this](absl::string_view data) -> absl::Status {
                                return Dispatch(data);
                              });

      if (!status.ok()) return status;
    }
  }

  void AddHandler(const std::string& method_name, const RPCHandler &fun) {
    handlers_.insert({method_name, fun});
  }

private:
  static ResponseMessage MethodNotFound(const RequestMessage &req,
                                        absl::string_view msg) {
    ResponseMessage response;
    response.id = req.id;
    response.error = {
      .code = 42,
      .message{msg},
    };
    return response;
  }

  void SendReply(const ResponseMessage &response) {
    nlohmann::json j = response;
    std::stringstream out_bytes;
    out_bytes << j << "\n";
    const std::string& body = out_bytes.str();
    *output_ << "Content-Length: " << body.size() << "\r\n\r\n";
    *output_ << body << std::flush;
  }

  absl::Status Dispatch(absl::string_view data) {
    RequestMessage request;
    try {
      request = nlohmann::json::parse(data);
    }
    catch (const std::exception &e) {
      SendReply(MethodNotFound(request, e.what()));
    }

    // Direct dispatch, later maybe send to thread-pool
    const auto& found = handlers_.find(request.method);
    if (found != handlers_.end()) {
      SendReply(found->second(request));
    } else {
      SendReply(MethodNotFound(request, absl::StrCat(request.method,
                                                     ": not implemented")));
    }
    return absl::OkStatus();
  }

  // Return -1 if header is incomplete. Return -2 if header complete, but
  // issue reading Content-Length header.
  int ParseHeader(absl::string_view data, int *body_size) {
    static constexpr absl::string_view kEndHeaderMarker = "\r\n\r\n";
    static constexpr absl::string_view kContentLengthHeader = "Content-Length:";

    auto end_of_header = data.find(kEndHeaderMarker);
    if (end_of_header == absl::string_view::npos) return -1; // incomplete

    // Very dirty search for header - we don't check if starts on line.
    const absl::string_view header_content(data.data(), end_of_header);
    auto found_ContentLength_header = header_content.find(kContentLengthHeader);
    if (found_ContentLength_header == absl::string_view::npos) return -2;
    size_t end_key = found_ContentLength_header + kContentLengthHeader.size();
    absl::string_view body_size_number = {
      header_content.data() + end_key,
      header_content.size() - end_key };
    if (!absl::SimpleAtoi(body_size_number, body_size)) {
      return -2;
    }
    return end_of_header + kEndHeaderMarker.size();
  }

  using ReadCallback = std::function<absl::Status (absl::string_view data)>;

  // Read from data and process all fully available content header+bodies
  // found in data. Updates "data" to return the remaining unprocessed data.
  // Returns ok() status unless one of the process() call fails.
  absl::Status ProcessAllRequests(absl::string_view *data,
                                  const ReadCallback& process) {
    while (!data->empty()) {
      int body_size;
      const int body_offset = ParseHeader(*data, &body_size);
      if (body_offset == -2) {
        absl::string_view limited_view(data->data(),
                                       std::min(data->size(), (size_t)256));
        return absl::InvalidArgumentError(
          absl::StrCat("No `Content-Length:` header. '", limited_view, "...'"));
      }

      const int content_size = body_offset + body_size;
      if (body_offset < 0 || content_size > data->size())
        return absl::OkStatus();  // Only insufficient partial buffer available.
      absl::string_view body(data->data() + body_offset, body_size);
      if (auto status = process(body); !status.ok()) {
        return status;
      }
      *data = { data->data() + content_size, data->size() - content_size };
    }
    return absl::OkStatus();
  }

  absl::Status ReadInput(int fd, const ReadCallback& process) {
    char *begin_of_buffer = scratch_buffer_;
    size_t available = scratch_buffer_size_;

    ssize_t partial_read = read(input_fd_, begin_of_buffer, available);
    if (partial_read <= 0) return absl::UnavailableError(
      absl::StrCat("read() on fd=", input_fd_, ": ", strerror(errno)));

    absl::string_view data(begin_of_buffer, partial_read);
    if (auto status = ProcessAllRequests(&data, process); !status.ok()) {
      return status;
    }

    // TODO: simplified assumption that with one read() we get a number
    // of complete results. In reality, we might have a partial result as
    // our buffer might be smaller than fits the last message.
    // Might need to copy to front, maybe realloc etc.
    assert(data.empty());

    return absl::OkStatus();
  }

  char *EnsureBuffer(size_t size) {
    static constexpr uint32_t kBlockSizeBits = 12;
    static constexpr uint32_t kBlockSizePattern = ((1 << kBlockSizeBits) - 1);
    if (!scratch_buffer_ || scratch_buffer_size_ < size) {
      const size_t new_size = (size + kBlockSizePattern) & ~kBlockSizePattern;
      fprintf(stderr, "new size: %d\n", (int)new_size);
      scratch_buffer_ = (char*)realloc(scratch_buffer_, new_size);
      scratch_buffer_size_ = new_size;
    }
    return scratch_buffer_;
  }

  const int input_fd_;
  std::ostream *output_;
  std::unordered_map<std::string, RPCHandler> handlers_;
  char *scratch_buffer_ = nullptr;
  size_t scratch_buffer_size_ = 0;
};

#endif  // JSON_RPC_SERVER_
