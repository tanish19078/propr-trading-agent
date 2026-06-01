#include "propr/net/http_client.h"

#include <cpr/cpr.h>

#include <stdexcept>

namespace propr::net {

HttpClient::HttpClient(Config cfg, risk::RateLimiter& limiter)
    : cfg_(std::move(cfg)), limiter_(limiter) {}

core::Result<HttpClient::Json> HttpClient::get(
    std::string_view path, const std::unordered_map<std::string, std::string>& query) {
  return do_request_("GET", path, query, nullptr, risk::RateLimiter::Class::Normal);
}

core::Result<HttpClient::Json> HttpClient::post(std::string_view path, const Json& body) {
  return do_request_("POST", path, {}, &body, risk::RateLimiter::Class::Normal);
}

core::Result<HttpClient::Json> HttpClient::post_reserved(std::string_view path,
                                                        const Json& body) {
  return do_request_("POST", path, {}, &body, risk::RateLimiter::Class::Reserved);
}

core::Result<HttpClient::Json> HttpClient::del(std::string_view path) {
  return do_request_("DELETE", path, {}, nullptr, risk::RateLimiter::Class::Normal);
}

core::Result<HttpClient::Json> HttpClient::delete_reserved(std::string_view path) {
  return do_request_("DELETE", path, {}, nullptr, risk::RateLimiter::Class::Reserved);
}

core::Result<HttpClient::Json> HttpClient::do_request_(
    std::string_view method,
    std::string_view path,
    const std::unordered_map<std::string, std::string>& query,
    const Json* body,
    risk::RateLimiter::Class cls) {
  if (!limiter_.try_take(cls)) {
    return core::make_error(core::Error::Code::RateLimited,
                            "local rate limiter exhausted");
  }

  const std::string url = cfg_.base_url + std::string(path);
  cpr::Header headers{
      {"X-API-Key", cfg_.api_key},
      {"Accept", "application/json"},
      {"Content-Type", "application/json"},
  };
  cpr::Parameters params;
  for (const auto& [k, v] : query) params.Add({k, v});

  cpr::Response r;
  const auto timeout = cpr::Timeout{cfg_.timeout_ms};
  if (method == "GET") {
    r = cpr::Get(cpr::Url{url}, headers, params, timeout);
  } else if (method == "POST") {
    r = cpr::Post(cpr::Url{url}, headers,
                  cpr::Body{body ? body->dump() : std::string{}}, timeout);
  } else if (method == "DELETE") {
    r = cpr::Delete(cpr::Url{url}, headers, timeout);
  } else {
    return core::make_error(core::Error::Code::Unknown, "unsupported method");
  }

  if (r.error) {
    return core::make_error(core::Error::Code::NetworkError, r.error.message);
  }

  if (r.status_code == 429) {
    return core::make_http_error(429, "server rate-limited");
  }
  if (r.status_code == 401 || r.status_code == 403) {
    return core::make_http_error(r.status_code, "auth error");
  }
  if (r.status_code >= 400) {
    return core::make_http_error(r.status_code, r.text);
  }

  try {
    return r.text.empty() ? Json{} : Json::parse(r.text);
  } catch (const std::exception& e) {
    return core::make_error(core::Error::Code::JsonParseError, e.what());
  }
}

}  // namespace propr::net
