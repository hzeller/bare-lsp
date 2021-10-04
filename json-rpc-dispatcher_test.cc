#include "json-rpc-dispatcher.h"

#include <absl/status/status.h>
#include <absl/strings/string_view.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <exception>

using nlohmann::json;

TEST(JsonRpcDispatcherTest, Call_MissingMethodInRequest) {
  // If the request does not contain a method name, it is malformed.
  int write_fun_called = 0;
  int notification_fun_called = 0;

  JsonRpcDispatcher dispatcher([&](absl::string_view s) {
    const json j = json::parse(s);
    EXPECT_TRUE(j.find("error") != j.end());
    EXPECT_EQ(j["error"]["code"], -32601) << s;  // Method not found.
    ++write_fun_called;
  });
  dispatcher.AddNotificationHandler("foo", [&](const json &j) {  //
    ++notification_fun_called;
  });

  dispatcher.DispatchMessage(
      R"({"jsonrpc":"2.0","params":{"hello": "world"}})");

  EXPECT_EQ(notification_fun_called, 0);
  EXPECT_EQ(write_fun_called, 1);  // Complain about missing method.
}

TEST(JsonRpcDispatcherTest, CallNotification) {
  int write_fun_called = 0;
  int notification_fun_called = 0;

  JsonRpcDispatcher dispatcher([&](absl::string_view s) {
    std::cerr << s;
    ++write_fun_called;
  });
  dispatcher.AddNotificationHandler("foo", [&](const json &j) {
    ++notification_fun_called;
    EXPECT_EQ(j, json::parse(R"({ "hello", "world"})"));
  });

  dispatcher.DispatchMessage(
      R"({"jsonrpc":"2.0","method":"foo","params":{"hello": "world"}})");

  EXPECT_EQ(notification_fun_called, 1);
  EXPECT_EQ(write_fun_called, 0);  // Notifications don't have responses.
}

TEST(JsonRpcDispatcherTest, CallNotification_MissingMethodImplemented) {
  // A notification whose method is not registered must be silently ignored.
  // No response with error.
  int write_fun_called = 0;

  JsonRpcDispatcher dispatcher([&](absl::string_view s) {  //
    ++write_fun_called;
  });

  dispatcher.DispatchMessage(
      R"({"jsonrpc":"2.0","method":"foo","params":{"hello": "world"}})");

  EXPECT_EQ(write_fun_called, 0);
}

TEST(JsonRpcDispatcherTest, CallRpcHandler) {
  int write_fun_called = 0;
  int rpc_fun_called = 0;

  JsonRpcDispatcher dispatcher([&](absl::string_view s) {
    const json j = json::parse(s);
    EXPECT_EQ(std::string(j["result"]["some"]), "response");
    EXPECT_TRUE(j.find("error") == j.end());
    ++write_fun_called;
  });
  dispatcher.AddRequestHandler("foo", [&](const json &j) -> json {
    ++rpc_fun_called;
    EXPECT_EQ(j, json::parse(R"({ "hello":"world"})"));
    return json::parse(R"({ "some": "response"})");
  });

  dispatcher.DispatchMessage(
      R"({"jsonrpc":"2.0","id":1,"method":"foo","params":{"hello":"world"}})");

  EXPECT_EQ(rpc_fun_called, 1);
  EXPECT_EQ(write_fun_called, 1);
}

TEST(JsonRpcDispatcherTest, CallRpcHandler_ReportInternalEror) {
  int write_fun_called = 0;
  int rpc_fun_called = 0;

  JsonRpcDispatcher dispatcher([&](absl::string_view s) {
    const json j = json::parse(s);
    EXPECT_TRUE(j.find("error") != j.end());
    EXPECT_EQ(j["error"]["code"], -32603) << s;  // reported as internal error
    ++write_fun_called;
  });

  // This method does not complete but throws an exception.
  dispatcher.AddRequestHandler("foo", [&](const json &j) -> json {
    ++rpc_fun_called;
    throw std::runtime_error("Okay, Houston, we've had a problem here");
  });

  dispatcher.DispatchMessage(
      R"({"jsonrpc":"2.0","id":1,"method":"foo","params":{"hello":"world"}})");

  EXPECT_EQ(rpc_fun_called, 1);
  EXPECT_EQ(write_fun_called, 1);
}

TEST(JsonRpcDispatcherTest, CallRpcHandler_MissingMethodImplemented) {
  int write_fun_called = 0;

  JsonRpcDispatcher dispatcher([&](absl::string_view s) {
    const json j = json::parse(s);
    EXPECT_TRUE(j.find("error") != j.end());
    EXPECT_EQ(j["error"]["code"], -32601) << s;  // Method not found.
    ++write_fun_called;
  });

  dispatcher.DispatchMessage(
      R"({"jsonrpc":"2.0","id":1,"method":"foo","params":{"hello":"world"}})");

  EXPECT_EQ(write_fun_called, 1);  // Reported error.
}
