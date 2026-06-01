#pragma once

#include <string>
#include <tl/expected.hpp>

namespace propr::core {

struct Error {
  enum class Code {
    NetworkError,
    HttpError,
    AuthError,
    RateLimited,
    JsonParseError,
    ConfigError,
    StateError,
    NotFound,
    Unknown,
  };
  Code code{Code::Unknown};
  std::string message;
  int http_status{0};
};

template <typename T>
using Result = tl::expected<T, Error>;

inline tl::unexpected<Error> make_error(Error::Code c, std::string msg) {
  return tl::unexpected<Error>{Error{c, std::move(msg), 0}};
}
inline tl::unexpected<Error> make_http_error(int status, std::string msg) {
  return tl::unexpected<Error>{Error{Error::Code::HttpError, std::move(msg), status}};
}

}  // namespace propr::core
