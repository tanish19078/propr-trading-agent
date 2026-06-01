#pragma once

#include <nlohmann/json.hpp>

#include <memory>
#include <string>
#include <unordered_map>

#include "propr/core/result.h"
#include "propr/risk/rate_limiter.h"

namespace propr::net {

// Thin async-ish HTTP client. Backed by cpr (libcurl). Every request is rate-limited
// via the RateLimiter passed in (Normal class by default; flatten path uses Reserved).
class HttpClient {
 public:
  struct Config {
    std::string base_url;       // e.g. "https://api.propr.xyz/v1"
    std::string api_key;        // pk_live_...
    int timeout_ms{30000};
  };

  HttpClient(Config cfg, risk::RateLimiter& limiter);
  virtual ~HttpClient() = default;

  using Json = nlohmann::json;

  virtual core::Result<Json> get(std::string_view path,
                                 const std::unordered_map<std::string, std::string>& query = {});
  virtual core::Result<Json> post(std::string_view path, const Json& body);
  virtual core::Result<Json> post_reserved(std::string_view path, const Json& body);
  virtual core::Result<Json> del(std::string_view path);
  virtual core::Result<Json> delete_reserved(std::string_view path);

 protected:
  // Hook for tests — fakes override to short-circuit without rate-limit or cpr.
  virtual core::Result<Json> do_request_(std::string_view method,
                                         std::string_view path,
                                         const std::unordered_map<std::string, std::string>& query,
                                         const Json* body,
                                         risk::RateLimiter::Class cls);

 private:
  Config cfg_;
  risk::RateLimiter& limiter_;
};

}  // namespace propr::net
