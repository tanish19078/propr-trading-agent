#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace propr::core {

// Standalone SHA-256 (no dependency on openssl). Public-domain style implementation.
// Adequate for internal in-process command authentication.
class Sha256 {
 public:
  static constexpr std::size_t kDigestSize = 32;

  Sha256();
  void update(const void* data, std::size_t len);
  void update(std::string_view s) { update(s.data(), s.size()); }
  std::array<std::uint8_t, kDigestSize> finalize();

  static std::array<std::uint8_t, kDigestSize> digest(std::string_view s);
  static std::string hex(std::string_view s);

 private:
  void transform_(const std::uint8_t* block);
  std::uint32_t state_[8];
  std::uint64_t bit_count_{0};
  std::uint8_t buf_[64];
  std::size_t buf_len_{0};
};

// HMAC-SHA256 with a binary key. Output is hex-encoded.
std::string hmac_sha256_hex(std::string_view key, std::string_view message);

// Lowercase hex of an arbitrary byte buffer.
std::string to_hex(const std::uint8_t* data, std::size_t n);

}  // namespace propr::core
