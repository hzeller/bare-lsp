#ifndef JSON_RPC_SERVER_
#define JSON_RPC_SERVER_

#include <functional>
#include <initializer_list>
#include <sstream>
#include <string>
#include <unordered_map>

//
#include <absl/status/status.h>
#include <absl/strings/str_cat.h>
#include <absl/strings/string_view.h>
#include <nlohmann/json.hpp>

class JsonRpcServer {
 public:
  // A notification receives a request, but does not return anything
  using RPCNotification = std::function<void(const nlohmann::json &r)>;

  // A RPC call receives a request and returns a response.
  // If we ever have a meaningful set of error conditions to convey, maybe
  // change this to StatusOr<nlohmann::json> as return value.
  using RPCCallHandler = std::function<nlohmann::json(const nlohmann::json &)>;

  // Some statistical counters.
  using StatsMap = std::map<std::string, int>;

  using WriteFun = std::function<void(absl::string_view response)>;

  // Receive data from the "source", a message stream splitter that calls
  // our DispatchMessage(). If "source" is not given DispatchMessage() can
  // be called manually.
  // Responses are written using the "out" write function.
  JsonRpcServer(const WriteFun &out) : write_fun_(out) {}
  JsonRpcServer(const JsonRpcServer &) = delete;

  void AddRequestHandler(const std::string &method_name,
                         const RPCCallHandler &fun) {
    handlers_.insert({method_name, fun});
  }

  void AddNotificationHandler(const std::string &method_name,
                              const RPCNotification &fun) {
    notifications_.insert({method_name, fun});
  }

  const StatsMap &GetStatCounters() const { return statistic_counters_; }

  // Dispatch incoming message, a string view with json data.
  // Call this with the content of exactly one message. If this is an
  // RPC call, it will call the WriteFun with the output.
  void DispatchMessage(absl::string_view data) {
    nlohmann::json request;
    try {
      request = nlohmann::json::parse(data);
    } catch (const std::exception &e) {
      statistic_counters_[e.what()]++;
      SendReply(CreateError(request, kParseError, e.what()));
      return;
    }

    if (request.find("method") == request.end()) {
      SendReply(
          CreateError(request, kMethodNotFound, "Method required in request"));
      statistic_counters_["Request without method"]++;
      return;
    }
    const std::string &method = request["method"];

    // Direct dispatch, later maybe send to thread-pool ?
    const bool is_notification = (request.find("id") == request.end());
    bool handled = false;
    if (is_notification) {
      handled = CallNotification(request, method);
    } else {
      handled = CallRequestHandler(request, method);
    }
    statistic_counters_[method + (handled ? "" : " (unhandled)") +
                        (is_notification ? "  ev" : " RPC")]++;
  }

 private:
  static constexpr int kParseError = -32700;
  static constexpr int kMethodNotFound = -32601;
  static constexpr int kInternalError = -32603;

  bool CallNotification(const nlohmann::json &req, const std::string &method) {
    const auto &found = notifications_.find(method);
    if (found == notifications_.end()) return false;
    try {
      found->second(req["params"]);
      return true;
    } catch (const std::exception &e) {
      // Issue while implicitly converting from json to type.
      statistic_counters_[method + " : " + e.what()]++;
    }
    return false;
  }

  bool CallRequestHandler(const nlohmann::json &req,
                          const std::string &method) {
    const auto &found = handlers_.find(method);
    if (found == handlers_.end()) {
      SendReply(CreateError(req, kMethodNotFound,
                            "method '" + method + "' not found."));
      return false;
    }

    try {
      SendReply(MakeResponse(req, found->second(req["params"])));
      return true;
    } catch (const std::exception &e) {
      statistic_counters_[method + " : " + e.what()]++;
      SendReply(CreateError(req, kInternalError, e.what()));
    }
    return false;
  }

  static nlohmann::json CreateError(const nlohmann::json &request, int code,
                                    absl::string_view message) {
    nlohmann::json result = {
        {"jsonrpc", "2.0"},
    };
    result["error"] = {{"code", code}};
    if (!message.empty()) {
      result["error"]["message"] = message;
    }

    if (request.find("id") != request.end()) {
      result["id"] = request["id"];
    }

    return result;
  }

  static nlohmann::json MakeResponse(const nlohmann::json &request,
                                     const nlohmann::json &call_result) {
    nlohmann::json result = {
        {"jsonrpc", "2.0"},
    };
    result["id"] = request["id"];
    result["result"] = call_result;
    return result;
  }

  void SendReply(const nlohmann::json &response) {
    std::stringstream out_bytes;
    out_bytes << response << "\n";
    write_fun_(out_bytes.str());
  }

  const WriteFun write_fun_;

  std::unordered_map<std::string, RPCCallHandler> handlers_;
  std::unordered_map<std::string, RPCNotification> notifications_;
  StatsMap statistic_counters_;
};

#endif  // JSON_RPC_SERVER_
