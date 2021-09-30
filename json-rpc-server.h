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

  // Some statistical counters.
  using StatsMap = std::map<std::string, int>;

  // Write a list of string_views to the output. Each call to this write
  // function represents a complete response.
  // Behave a bit like the writev() system call.
  // No return value, function is assumed to always write fully.
  using WriteFun =
    std::function<void(std::initializer_list<absl::string_view>)>;

  // Receive data from the "source", a message stream splitter that calls
  // our DispatchMessage(). If "source" is not given DispatchMessage() can
  // be called manually.
  // Responses are written using the "out" write function.
  JsonRpcServer(const WriteFun &out, MessageStreamSplitter *source = nullptr)
    : write_fun_(out) {
    if (source) {
      source->SetBodyProcessor([this](absl::string_view data) {
        return DispatchMessage(data);
      });
    }
  }

  void AddHandler(const std::string& method_name, const RPCHandler &fun) {
    handlers_.insert({method_name, fun});
  }

  const StatsMap& GetStatCounters() const { return statistic_counters_; }

  // Dispatch incoming message, a string view with json data.
  //
  // This function is typically called by the MessageStreamSplitter provided
  // at construction time, but can also be called manually (e.g. for testing).
  absl::Status DispatchMessage(absl::string_view data) {
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

  const WriteFun write_fun_;

  std::unordered_map<std::string, RPCHandler> handlers_;
  StatsMap statistic_counters_;
};

#endif  // JSON_RPC_SERVER_
