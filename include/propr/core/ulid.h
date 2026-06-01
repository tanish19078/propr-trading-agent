#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <random>
#include <string>

namespace propr::core {

// Monotonic ULID generator. Thread-safe. If multiple ULIDs are minted in the same
// millisecond, the randomness field is incremented to preserve sortability — the
// canonical "monotonic ULID" trick from the spec.
class Ulid {
 public:
  Ulid();
  explicit Ulid(std::uint64_t seed);

  std::string next();

 private:
  std::string encode_(std::uint64_t ts_ms, const std::array<std::uint8_t, 10>& rand) const;

  std::mutex mu_;
  std::mt19937_64 rng_;
  std::uint64_t last_ts_ms_{0};
  std::array<std::uint8_t, 10> last_rand_{};
};

}  // namespace propr::core
