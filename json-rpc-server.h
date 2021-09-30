#ifndef JSON_RPC_SERVER_
#define JSON_RPC_SERVER_

#include <cassert>

#include <functional>
#include <initializer_list>
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
  using StatusMap = std::map<std::string, int>;

  // A function that reads from some source and writes result into the
  // buffer. Returns the number of bytes read.
  // Blocks until there is content or returns '0' on end-of-file. Values
  // below zero indicate errors.
  // Only the amount of bytes available at the time of the call are filled
  // into the buffer, so the return value can be less than size.
  // So essentially: this behaves like the standard read() system call.
  using ReadFun = std::function<int(char *buf, int size)>;

  // Write a list of string_views to the output.
  // If possible, combine the writes in one write.
  // Behave a bit like the writev() system call.
  // No return value, function is assumed to always write fully.
  using WriteFun =
    std::function<void(std::initializer_list<absl::string_view>)>;

  // Read using an internal buffer of "read_buffer_size", which must be larger
  // than the largest expected message.
  // Responses are written using the "out" write function.
  JsonRpcServer(size_t read_buffer_size, const WriteFun &out)
    : read_buffer_size_(read_buffer_size),
      read_buffer_(new char [read_buffer_size]),
      write_fun_(out) {
  }

  ~JsonRpcServer() { delete [] read_buffer_; }

  // Process next input. Calls read_fun() exactly once to get the next
  // amount of data and processes all requests that are in the incoming data.
  absl::Status ProcessInput(const ReadFun &read_fun) {
    return ReadInput(read_fun,
                     [this](absl::string_view data) -> absl::Status {
                       return Dispatch(data);
                     });
  }

  void AddHandler(const std::string& method_name, const RPCHandler &fun) {
    handlers_.insert({method_name, fun});
  }

  const StatusMap& GetStatCounters() const { return statistic_counters_; }

private:
  static constexpr absl::string_view kEndHeaderMarker = "\r\n\r\n";
  static constexpr absl::string_view kContentLengthHeader = "Content-Length: ";

  static ResponseMessage MethodNotFound(const RequestMessage &req,
                                        absl::string_view msg) {
    ResponseMessage response;
    response.id = req.id;
    response.error.code = 42;
    response.error.message.assign(msg.data(), msg.size());
    return response;
  }

  void SendReply(const ResponseMessage &response) {
    nlohmann::json j = response;
    std::stringstream out_bytes;
    out_bytes << j << "\n";
    const std::string& body = out_bytes.str();
    const std::string size_str = std::to_string(body.size());
    write_fun_({ kContentLengthHeader, size_str, kEndHeaderMarker, body });
  }

  absl::Status Dispatch(absl::string_view data) {
    RequestMessage request;
    try {
      request = nlohmann::json::parse(data);
    }
    catch (const std::exception &e) {
      statistic_counters_[e.what()]++;
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
    statistic_counters_[request.method]++;
    return absl::OkStatus();
  }

  // Return -1 if header is incomplete. Return -2 if header complete, but
  // issue reading Content-Length header.
  int ParseHeader(absl::string_view data, int *body_size) {
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
  // Returns ok() status unless header is corrupted or one of the process()
  // call fails.
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
      if (body_offset < 0 || content_size > (int)data->size())
        return absl::OkStatus();  // Only insufficient partial buffer available.
      absl::string_view body(data->data() + body_offset, body_size);
      if (auto status = process(body); !status.ok()) {
        return status;
      }

      int* largest = &statistic_counters_["LargestProcessedBody"];
      *largest = std::max((int)body.size(), *largest);

      *data = { data->data() + content_size, data->size() - content_size };
    }
    return absl::OkStatus();
  }

absl::Status ReadInput(const ReadFun &read_fun, const ReadCallback& process) {
    char *begin_of_read = read_buffer_;
    int available_read_space = read_buffer_size_;

    // Get all we had left from last time to the beginning of the buffer.
    // This is in the same buffer, so we need to memmove()
    if (!pending_data_.empty()) {
      memmove(begin_of_read, pending_data_.data(), pending_data_.size());
      begin_of_read += pending_data_.size();
      available_read_space -= pending_data_.size();
    }

    int partial_read = read_fun(begin_of_read, available_read_space);
    if (partial_read <= 0) {
      return absl::UnavailableError(absl::StrCat("read() returned ",
                                                 partial_read));
    }
    statistic_counters_["TotalBytesRead"] += partial_read;

    absl::string_view data(read_buffer_, pending_data_.size() + partial_read);
    if (auto status = ProcessAllRequests(&data, process); !status.ok()) {
      return status;
    }

    pending_data_ = data;  // Remember for next round.

    return absl::OkStatus();
  }

  const size_t read_buffer_size_;
  char *const read_buffer_;
  const WriteFun write_fun_;

  std::unordered_map<std::string, RPCHandler> handlers_;
  StatusMap statistic_counters_;
  absl::string_view pending_data_;
};

#endif  // JSON_RPC_SERVER_
