#pragma once

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "log.h"

namespace xmbwave {

struct Rgb {
  float r = 0.0f;
  float g = 0.0f;
  float b = 0.0f;
};

struct Config {
  int fps = 60;
  int vsync = 0;
  int msaa = 4;  // multisample count, 0 disables
  int gridW = 256;
  int gridH = 96;
  int pauseUnfocused = 1;
  int allMonitors = 0;
  int focusPollMs = 250;
  int logLevel = 2;

  float speed = 1.0f;
  float amplitude = 1.0f;
  float tension = 0.12f;
  float detail = 4.5f;
  float ribbonScale = 0.5f;
  float softClip = 0.22f;
  float zDetailScale = 0.08f;
  float brightness = 0.98f;
  float opacity = 0.7f;
  float fresnelPower = 4.0f;
  float fresnelScale = 0.5f;
  float waveBody = 0.0f;

  Rgb colorTop = {0.00f, 0.01f, 0.37f};
  Rgb colorBot = {0.10f, 0.61f, 0.82f};
  Rgb waveColor = {0.26f, 0.52f, 0.74f};
  // Degrees from vertical; the reference background is a 2D linear gradient
  // tilted so it brightens toward the bottom-right.
  float gradientAngle = 15.1f;

  int particles = 220;
  float particleOpacity = 0.55f;
  float particleSize = 1.5f;
  float particleSizeVar = 4.0f;
  float particleSpeed = 1.0f;

  std::string configPath;
};

enum class SettingResult {
  Applied,
  Unknown,
  Invalid,
};

std::string configDefaultPath();

// Same keys and ranges for the config file and for --set.
SettingResult configSet(Config& config, std::string_view key,
                        std::string_view value);

// Applies every "key = value" line of a file. A missing file is not an error.
bool configLoad(Config& config, const std::string& path);

}  // namespace xmbwave
