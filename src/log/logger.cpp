#include "propr/log/logger.h"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <mutex>

namespace propr::log {
namespace {
std::shared_ptr<spdlog::logger> g_logger;
std::once_flag g_init_flag;
}  // namespace

void init(const std::string& path, spdlog::level::level_enum lvl) {
  std::call_once(g_init_flag, [&]() {
    constexpr std::size_t kRotateSize = 100ull * 1024 * 1024;  // 100 MiB
    constexpr std::size_t kMaxFiles = 5;
    auto file = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        path, kRotateSize, kMaxFiles);
    auto console = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    file->set_pattern(R"({"ts":"%Y-%m-%dT%H:%M:%S.%fZ","lvl":"%l","msg":%v})");
    console->set_pattern("%H:%M:%S.%e %^%l%$ %v");
    g_logger = std::make_shared<spdlog::logger>(
        "propr", spdlog::sinks_init_list{file, console});
    g_logger->set_level(lvl);
    g_logger->flush_on(spdlog::level::warn);
    spdlog::register_logger(g_logger);
  });
}

std::shared_ptr<spdlog::logger> logger() {
  if (!g_logger) {
    // Fallback so tests that forget init() still work.
    g_logger = spdlog::stderr_color_mt("propr");
  }
  return g_logger;
}

}  // namespace propr::log
