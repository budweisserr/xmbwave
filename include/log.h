#pragma once

// Compile every SPDLOG_* macro; filtering happens at the runtime level.
#ifndef SPDLOG_ACTIVE_LEVEL
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#endif

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <memory>

namespace xmbwave {

// 0=error 1=warn 2=info 3=debug, an int so it can come straight from config.
// the config file or the command line unchanged.
inline void logInit() {
  auto sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
  auto logger = std::make_shared<spdlog::logger>("xmbwave", std::move(sink));
  logger->set_pattern("[%H:%M:%S] [%^%l%$] %v");
  logger->flush_on(spdlog::level::warn);
  spdlog::set_default_logger(std::move(logger));
  spdlog::set_level(spdlog::level::info);
}

inline void logSetLevel(int level) {
  static const spdlog::level::level_enum levels[] = {
      spdlog::level::err,
      spdlog::level::warn,
      spdlog::level::info,
      spdlog::level::debug,
  };
  const int clamped = level < 0 ? 0 : (level > 3 ? 3 : level);
  spdlog::set_level(levels[clamped]);
}

}  // namespace xmbwave

#define XMB_LOG_ERROR(...) SPDLOG_ERROR(__VA_ARGS__)
#define XMB_LOG_WARN(...) SPDLOG_WARN(__VA_ARGS__)
#define XMB_LOG_INFO(...) SPDLOG_INFO(__VA_ARGS__)
#define XMB_LOG_DEBUG(...) SPDLOG_DEBUG(__VA_ARGS__)
