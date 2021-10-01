#ifndef JSON_RPC_SERVER_
#define JSON_RPC_SERVER_

#include <functional>
#include <initializer_list>
#include <sstream>
#include <string>
#include <unordered_map>

#include <absl/status/status.h>
#include <absl/strings/str_cat.h>
#include <absl/strings/string_view.h>

#include <nlohmann/json.hpp>

#include "lsp-protocol.h"
#include "message-stream-splitter.h"

class JsonRpcServer {
public:
  using RPCHandler = std::function<nlohmann::json (const nlohmann::json& r)>;
  using RPCNotification = std::function<void (const nlohmann::json& r)>;

  // Some statistical counters.
  using StatsMap = std::map<std::string, int>;

  using WriteFun = std::function<void(absl::string_view response)>;

  // Receive data from the "source", a message stream splitter that calls
  // our DispatchMessage(). If "source" is not given DispatchMessage() can
  // be called manually.
  // Responses are written using the "out" write function.
  JsonRpcServer(const WriteFun &out) : write_fun_(out) {
  }

  void AddRequestHandler(const std::string& method_name,
                         const RPCHandler &fun) {
    handlers_.insert({method_name, fun});
  }

  void AddNotificationHandler(const std::string& method_name,
                              const RPCNotification &fun) {
    notifications_.insert({method_name, fun});
  }

  const StatsMap& GetStatCounters() const { return statistic_counters_; }

  // Dispatch incoming message, a string view with json data.
  void DispatchMessage(absl::string_view data) {
    nlohmann::json request;
    try {
      request = nlohmann::json::parse(data);
    }
    catch (const std::exception &e) {
      statistic_counters_[e.what()]++;
      SendReply(CreateError(request, kParseError, e.what()));
      return;
    }

    const bool is_notification = (request.find("id") == request.end());
    if (request.find("method") == request.end()) {
      SendReply(CreateError(request, kMethodNotFound,
                            "Method required in request"));
      statistic_counters_["Request without method"]++;
      return;
    }
    const std::string &method = request["method"];

    // Direct dispatch, later maybe send to thread-pool ?
    bool handled = false;
    if (is_notification) {
      handled = CallNotification(request, method);
    } else {
      handled = CallRequestHandler(request, method);
    }
    statistic_counters_[method +
                        (handled ? "" : " (unhandled)") +
                        (is_notification ? " ." : " R")
                        ]++;
  }

private:
  static constexpr int kParseError = -32700;
  static constexpr int kMethodNotFound= -32601;

  bool CallNotification(const nlohmann::json &req, const std::string &method) {
    const auto& found = notifications_.find(method);
    if (found == notifications_.end()) return false;
    try {
      found->second(req["params"]);
      return true;
    }
    catch (const std::exception &e) {
      // Issue while implicitly converting from json to type.
      statistic_counters_[method + " : " + e.what()]++;
    }
    return false;
  }

  bool CallRequestHandler(const nlohmann::json &req,
                          const std::string &method) {
    const auto& found = handlers_.find(method);
    if (found != handlers_.end()) {
      try {
        SendReply(found->second(req["params"]));  // TODO: wrap result.
        return true;
      }
      catch (const std::exception &e) {
        statistic_counters_[method + " : " + e.what()]++;
      }
    } else {
      SendReply(CreateError(req, kMethodNotFound,
                            "method '" + method + "' not found."));
    }
    return false;
  }

  static nlohmann::json CreateError(const nlohmann::json &request,
                                    int code, absl::string_view message) {
    nlohmann::json result = {
      {"jsonrpc", "2.0"},
    };
    result["error"] = { { "code", code } };
    if (!message.empty()) {
      result["error"]["message"] = message;
    }

    if (request.find("id") != request.end()) {
      result["id"] = request["id"];
    }

    return result;
  }

  void SendReply(const nlohmann::json &response) {
    std::stringstream out_bytes;
    out_bytes << response << "\n";
    write_fun_(out_bytes.str());
  }

  const WriteFun write_fun_;

  std::unordered_map<std::string, RPCHandler> handlers_;
  std::unordered_map<std::string, RPCNotification> notifications_;
  StatsMap statistic_counters_;
};

#endif  // JSON_RPC_SERVER_
