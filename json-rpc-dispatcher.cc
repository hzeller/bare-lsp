#include "json-rpc-dispatcher.h"

static constexpr int kParseError = -32700;
static constexpr int kMethodNotFound = -32601;
static constexpr int kInternalError = -32603;

// Dispatch incoming message, a string view with json data.
// Call this with the content of exactly one message.
// If this is an RPC call, it send the response via the WriteFun.
void JsonRpcDispatcher::DispatchMessage(absl::string_view data) {
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

bool JsonRpcDispatcher::CallNotification(const nlohmann::json &req,
                                         const std::string &method) {
  const auto &found = notifications_.find(method);
  if (found == notifications_.end()) return false;
  try {
    found->second(req["params"]);
    return true;
  } catch (const std::exception &e) {
    // Possibly issue while implicitly converting from json to type.
    statistic_counters_[method + " : " + e.what()]++;
  }
  return false;
}

bool JsonRpcDispatcher::CallRequestHandler(const nlohmann::json &req,
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

/*static*/ nlohmann::json JsonRpcDispatcher::CreateError(
    const nlohmann::json &request, int code, absl::string_view message) {
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

/*static*/ nlohmann::json JsonRpcDispatcher::MakeResponse(
    const nlohmann::json &request, const nlohmann::json &call_result) {
  nlohmann::json result = {
      {"jsonrpc", "2.0"},
  };
  result["id"] = request["id"];
  result["result"] = call_result;
  return result;
}

void JsonRpcDispatcher::SendReply(const nlohmann::json &response) {
  std::stringstream out_bytes;
  out_bytes << response << "\n";
  write_fun_(out_bytes.str());
}
