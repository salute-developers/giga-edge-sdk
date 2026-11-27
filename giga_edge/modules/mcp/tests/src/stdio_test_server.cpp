#include <json/json.h>

#include <csignal>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {

void Send(const Json::Value& message) {
  Json::StreamWriterBuilder writer;
  writer["indentation"] = "";
  std::cout << Json::writeString(writer, message) << '\n' << std::flush;
}

} // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    return 1;
  }
  const std::string failing_method = argv[1];
  if (failing_method == "stubborn" && std::signal(SIGTERM, SIG_IGN) == SIG_ERR) {
    return 2;
  }
  Json::Value counts(Json::objectValue);
  Json::Value pending;
  std::string subscribed_uri;
  int version = 1;
  std::string line;
  while (std::getline(std::cin, line)) {
    Json::Value request;
    Json::Reader reader;
    if (!reader.parse(line, request)) {
      continue;
    }
    const auto method = request["method"].asString();
    counts[method] = counts.get(method, 0).asInt() + 1;
    if (method == "test/pause_reading") {
      // The server does not read stdin for the given time and then continues: the writer blocks on the pipe and waits.
      ::usleep(static_cast<useconds_t>(request["params"]["ms"].asUInt()) * 1000U);
      continue;
    }
    if (method == "test/block") {
      Json::Value notification;
      notification["jsonrpc"] = "2.0";
      notification["method"] = "test/blocked";
      Send(notification);
      for (;;) {
        ::pause();
      }
    }
    if (method == "test/exit") {
      return 0;
    }
    if (method == "test/notify" && !subscribed_uri.empty()) {
      ++version;
      Json::Value notification;
      notification["jsonrpc"] = "2.0";
      notification["method"] = "notifications/resources/updated";
      notification["params"]["uri"] = subscribed_uri;
      notification["params"]["version"] = version;
      Send(notification);
    }
    if (method == "test/malformed") {
      Json::Value bad_method;
      bad_method["jsonrpc"] = "2.0";
      bad_method["method"]["nested"] = true;
      Send(bad_method);
      Json::Value bad_params;
      bad_params["jsonrpc"] = "2.0";
      bad_params["method"] = "notifications/resources/updated";
      bad_params["params"].append(1);
      Send(bad_params);
      Json::Value bad_error;
      bad_error["jsonrpc"] = "2.0";
      bad_error["id"] = request["id"];
      bad_error["error"]["code"] = "HOME_VIEW_UNAVAILABLE";
      bad_error["error"]["message"]["text"] = "not a string";
      Send(bad_error);
      continue;
    }
    if (method == "test/empty_error") {
      Json::Value empty_error;
      empty_error["jsonrpc"] = "2.0";
      empty_error["id"] = request["id"];
      empty_error["error"]["code"] = -32000;
      empty_error["error"]["message"] = "";
      Send(empty_error);
      continue;
    }
    if (method == "test/reply_pending" && !pending.isNull()) {
      Send(pending);
      pending = Json::Value();
    }
    if (!request.isMember("id")) {
      continue;
    }
    Json::Value response;
    response["jsonrpc"] = "2.0";
    response["id"] = request["id"];
    if (method == failing_method) {
      response["error"]["code"] = -31901;
      response["error"]["message"] = "HOME_VIEW_UNAVAILABLE";
    } else {
      auto& result = response["result"];
      result = Json::Value(Json::objectValue);
      if (method == "initialize") {
        result["protocolVersion"] = "2024-11-05";
        result["capabilities"]["resources"]["subscribe"] = true;
      } else if (method == "tools/list") {
        result["tools"] = Json::Value(Json::arrayValue);
      } else if (method == "resources/list") {
        result["resources"][0]["uri"] = "house://devices/state";
      } else if (method == "resources/templates/list") {
        result["resourceTemplates"] = Json::Value(Json::arrayValue);
      } else if (method == "test/counts") {
        result = counts;
      } else if (method == "resources/read") {
        result["contents"][0]["uri"] = request["params"]["uri"];
        result["contents"][0]["mimeType"] = "application/json";
        result["contents"][0]["text"] = "{\"version\":" + std::to_string(version) + "}";
      } else if (method == "resources/subscribe") {
        subscribed_uri = request["params"]["uri"].asString();
      } else if (method == "resources/unsubscribe") {
        subscribed_uri.clear();
      }
    }
    if (method == "test/pending") {
      pending = std::move(response);
      continue;
    }
    Send(response);
  }
}
