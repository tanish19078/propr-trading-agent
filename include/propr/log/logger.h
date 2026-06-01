#pragma once

#include <spdlog/spdlog.h>

#include <memory>
#include <string>

namespace propr::log {

// Initialise the global logger with a JSON file sink at `path` plus a stderr sink.
// Idempotent: re-calling is a no-op.
void init(const std::string& path, spdlog::level::level_enum lvl = spdlog::level::info);

// Get the structured logger. Always non-null after init().
std::shared_ptr<spdlog::logger> logger();

}  // namespace propr::log

// Convenience macros — every call site is one line, includes structured kv pairs.
#define PROPR_LOG_INFO(...) ::propr::log::logger()->info(__VA_ARGS__)
#define PROPR_LOG_WARN(...) ::propr::log::logger()->warn(__VA_ARGS__)
#define PROPR_LOG_ERROR(...) ::propr::log::logger()->error(__VA_ARGS__)
#define PROPR_LOG_DEBUG(...) ::propr::log::logger()->debug(__VA_ARGS__)
