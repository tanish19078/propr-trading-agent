#include "propr/core/ulid.h"

#include <chrono>
#include <cstring>

namespace propr::core {

namespace {
constexpr char kCrockford[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

std::uint64_t now_ms() {
  using namespace std::chrono;
  return static_cast<std::uint64_t>(
      duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}
}  // namespace

Ulid::Ulid() : Ulid(std::random_device{}()) {}

Ulid::Ulid(std::uint64_t seed) : rng_(seed) {}

std::string Ulid::next() {
  std::lock_guard<std::mutex> lock(mu_);
  const std::uint64_t ts = now_ms();
  std::array<std::uint8_t, 10> rand{};

  if (ts == last_ts_ms_) {
    // Same millisecond: increment last randomness (big-endian) for monotonicity.
    rand = last_rand_;
    int i = static_cast<int>(rand.size()) - 1;
    while (i >= 0) {
      if (++rand[i] != 0) break;
      --i;
    }
    // If we wrapped all 80 bits, reseed — astronomically unlikely.
    if (i < 0) {
      for (auto& b : rand) b = static_cast<std::uint8_t>(rng_() & 0xFF);
    }
  } else {
    for (auto& b : rand) b = static_cast<std::uint8_t>(rng_() & 0xFF);
  }

  last_ts_ms_ = ts;
  last_rand_ = rand;
  return encode_(ts, rand);
}

std::string Ulid::encode_(std::uint64_t ts_ms,
                          const std::array<std::uint8_t, 10>& rand) const {
  // ULID layout: 48-bit timestamp + 80-bit randomness, total 128 bits.
  // Encoded as 26 Crockford base32 chars (10 for ts, 16 for rand).
  std::string out;
  out.resize(26);

  // Encode 48-bit ts into 10 chars (5 bits per char, big-endian).
  out[0] = kCrockford[(ts_ms >> 45) & 0x1F];
  out[1] = kCrockford[(ts_ms >> 40) & 0x1F];
  out[2] = kCrockford[(ts_ms >> 35) & 0x1F];
  out[3] = kCrockford[(ts_ms >> 30) & 0x1F];
  out[4] = kCrockford[(ts_ms >> 25) & 0x1F];
  out[5] = kCrockford[(ts_ms >> 20) & 0x1F];
  out[6] = kCrockford[(ts_ms >> 15) & 0x1F];
  out[7] = kCrockford[(ts_ms >> 10) & 0x1F];
  out[8] = kCrockford[(ts_ms >> 5) & 0x1F];
  out[9] = kCrockford[ts_ms & 0x1F];

  // Encode 80-bit rand into 16 chars.
  // Pack the 10 bytes (80 bits) as a big-endian __int128, then peel off 5 bits at a time.
  __uint128_t r = 0;
  for (auto b : rand) {
    r = (r << 8) | b;
  }
  for (int i = 15; i >= 0; --i) {
    out[10 + i] = kCrockford[static_cast<std::size_t>(r & 0x1F)];
    r >>= 5;
  }
  return out;
}

}  // namespace propr::core
