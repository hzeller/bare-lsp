// Copyright 2021 Henner Zeller <h.zeller@acm.org>
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "json-rpc-dispatcher.h"

void JsonRpcDispatcher::DispatchMessage(absl::string_view data) {
  nlohmann::json request;
  try {
    request = nlohmann::json::parse(data);
  } catch (const std::exception &e) {
    statistic_counters_[e.what()]++;
    ++exception_count_;
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

  // Direct dispatch, later maybe send to an executor that returns futures ?
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
    ++exception_count_;
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
    ++exception_count_;
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
