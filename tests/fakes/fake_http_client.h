#pragma once

#include <functional>
#include <string>
#include <vector>

#include "propr/net/http_client.h"

namespace propr::test_support {

// In-process fake HTTP client. Stub responses for unit tests — no network.
class FakeHttpClient final : public net::HttpClient {
 public:
  FakeHttpClient(net::HttpClient::Config cfg, risk::RateLimiter& limiter)
      : net::HttpClient(std::move(cfg), limiter) {}

  struct Call {
    std::string method;
    std::string path;
    nlohmann::json body;
  };

  std::vector<Call> calls;

  using Responder = std::function<core::Result<nlohmann::json>(const Call&)>;
  Responder responder;

 protected:
  core::Result<nlohmann::json> do_request_(
      std::string_view method,
      std::string_view path,
      const std::unordered_map<std::string, std::string>& /*query*/,
      const nlohmann::json* body,
      risk::RateLimiter::Class /*cls*/) override {
    Call c{std::string(method), std::string(path), body ? *body : nlohmann::json{}};
    calls.push_back(c);
    if (responder) return responder(c);
    return nlohmann::json::object();
  }
};

}  // namespace propr::test_support
