#include "config.h"

namespace xmbwave {
namespace {

std::string trim(std::string_view text) {
  size_t begin = 0;
  size_t end = text.size();
  while (begin < end && std::isspace((unsigned char)text[begin]))
    ++begin;
  while (end > begin && std::isspace((unsigned char)text[end - 1]))
    --end;
  return std::string(text.substr(begin, end - begin));
}

bool parseFloat(std::string_view text, float& out) {
  const std::string tmp = trim(text);
  if (tmp.empty())
    return false;
  char* end = nullptr;
  const float value = std::strtof(tmp.c_str(), &end);
  if (end == tmp.c_str() || *end != '\0')
    return false;
  out = value;
  return true;
}

bool parseInt(std::string_view text, int& out) {
  const std::string tmp = trim(text);
  if (tmp.empty())
    return false;
  char* end = nullptr;
  const long value = std::strtol(tmp.c_str(), &end, 10);
  if (end == tmp.c_str() || *end != '\0')
    return false;
  out = (int)value;
  return true;
}

bool parseBool(std::string_view text, int& out) {
  const std::string tmp = trim(text);
  if (tmp == "1" || tmp == "true" || tmp == "yes" || tmp == "on") {
    out = 1;
    return true;
  }
  if (tmp == "0" || tmp == "false" || tmp == "no" || tmp == "off") {
    out = 0;
    return true;
  }
  return false;
}

bool parseRgb(std::string_view text, Rgb& out) {
  std::vector<float> parts;
  const std::string tmp = trim(text);
  size_t start = 0;
  while (start <= tmp.size()) {
    const size_t comma = tmp.find(',', start);
    const std::string piece = (comma == std::string::npos)
                                  ? tmp.substr(start)
                                  : tmp.substr(start, comma - start);
    float value = 0.0f;
    if (!parseFloat(piece, value))
      return false;
    parts.push_back(value);
    if (comma == std::string::npos)
      break;
    start = comma + 1;
  }
  if (parts.size() != 3)
    return false;

  // Accept both 0..1 and 0..255 spellings.
  bool byteScale = false;
  for (float value : parts) {
    if (value < 0.0f || value > 255.0f)
      return false;
    if (value > 1.0f)
      byteScale = true;
  }
  const float scale = byteScale ? (1.0f / 255.0f) : 1.0f;
  out.r = parts[0] * scale;
  out.g = parts[1] * scale;
  out.b = parts[2] * scale;
  return true;
}

bool inRange(float value, float low, float high) {
  return value >= low && value <= high;
}

}  // namespace

std::string configDefaultPath() {
  const char* xdg = std::getenv("XDG_CONFIG_HOME");
  if (xdg && *xdg)
    return std::string(xdg) + "/xmbwave/xmbwave.conf";
  const char* home = std::getenv("HOME");
  if (home && *home)
    return std::string(home) + "/.config/xmbwave/xmbwave.conf";
  return "/etc/xmbwave.conf";
}

SettingResult configSet(Config& config, std::string_view rawKey,
                        std::string_view rawValue) {
  const std::string key = trim(rawKey);
  const std::string value = trim(rawValue);
  if (key.empty())
    return SettingResult::Invalid;

  int intValue = 0;
  float floatValue = 0.0f;

  if (key == "fps") {
    if (!parseInt(value, intValue) || intValue < 1 || intValue > 1000)
      return SettingResult::Invalid;
    config.fps = intValue;
    return SettingResult::Applied;
  }
  if (key == "msaa") {
    if (!parseInt(value, intValue) || intValue < 0 || intValue > 16)
      return SettingResult::Invalid;
    config.msaa = intValue;
    return SettingResult::Applied;
  }
  if (key == "vsync") {
    if (!parseBool(value, intValue))
      return SettingResult::Invalid;
    config.vsync = intValue;
    return SettingResult::Applied;
  }
  if (key == "gridW") {
    if (!parseInt(value, intValue) || intValue < 16 || intValue > 1024)
      return SettingResult::Invalid;
    config.gridW = intValue;
    return SettingResult::Applied;
  }
  if (key == "gridH") {
    if (!parseInt(value, intValue) || intValue < 16 || intValue > 1024)
      return SettingResult::Invalid;
    config.gridH = intValue;
    return SettingResult::Applied;
  }
  if (key == "grid") {
    const size_t sep = value.find_first_of("xX*");
    if (sep == std::string::npos)
      return SettingResult::Invalid;
    int w = 0;
    int h = 0;
    if (!parseInt(value.substr(0, sep), w) ||
        !parseInt(value.substr(sep + 1), h))
      return SettingResult::Invalid;
    if (w < 16 || w > 1024 || h < 16 || h > 1024)
      return SettingResult::Invalid;
    config.gridW = w;
    config.gridH = h;
    return SettingResult::Applied;
  }
  if (key == "pauseUnfocused") {
    if (!parseBool(value, intValue))
      return SettingResult::Invalid;
    config.pauseUnfocused = intValue;
    return SettingResult::Applied;
  }
  if (key == "allMonitors") {
    if (!parseBool(value, intValue))
      return SettingResult::Invalid;
    config.allMonitors = intValue;
    return SettingResult::Applied;
  }
  if (key == "focusPollMs") {
    if (!parseInt(value, intValue) || intValue < 50 || intValue > 5000)
      return SettingResult::Invalid;
    config.focusPollMs = intValue;
    return SettingResult::Applied;
  }
  if (key == "logLevel") {
    if (!parseInt(value, intValue) || intValue < 0 || intValue > 3)
      return SettingResult::Invalid;
    config.logLevel = intValue;
    return SettingResult::Applied;
  }
  if (key == "speed") {
    if (!parseFloat(value, floatValue) || !inRange(floatValue, 0.0f, 20.0f))
      return SettingResult::Invalid;
    config.speed = floatValue;
    return SettingResult::Applied;
  }
  if (key == "amplitude") {
    if (!parseFloat(value, floatValue) || !inRange(floatValue, 0.0f, 5.0f))
      return SettingResult::Invalid;
    config.amplitude = floatValue;
    return SettingResult::Applied;
  }
  if (key == "detail") {
    if (!parseFloat(value, floatValue) || !inRange(floatValue, 0.05f, 8.0f))
      return SettingResult::Invalid;
    config.detail = floatValue;
    return SettingResult::Applied;
  }
  if (key == "tension") {
    if (!parseFloat(value, floatValue) || !inRange(floatValue, 0.0f, 1.0f))
      return SettingResult::Invalid;
    config.tension = floatValue;
    return SettingResult::Applied;
  }
  if (key == "ribbonScale") {
    if (!parseFloat(value, floatValue) || !inRange(floatValue, 0.0f, 4.0f))
      return SettingResult::Invalid;
    config.ribbonScale = floatValue;
    return SettingResult::Applied;
  }
  if (key == "softClip") {
    if (!parseFloat(value, floatValue) || !inRange(floatValue, 0.01f, 4.0f))
      return SettingResult::Invalid;
    config.softClip = floatValue;
    return SettingResult::Applied;
  }
  if (key == "zDetailScale") {
    if (!parseFloat(value, floatValue) || !inRange(floatValue, 0.0f, 2.0f))
      return SettingResult::Invalid;
    config.zDetailScale = floatValue;
    return SettingResult::Applied;
  }
  if (key == "brightness") {
    if (!parseFloat(value, floatValue) || !inRange(floatValue, 0.0f, 8.0f))
      return SettingResult::Invalid;
    config.brightness = floatValue;
    return SettingResult::Applied;
  }
  if (key == "opacity") {
    if (!parseFloat(value, floatValue) || !inRange(floatValue, 0.0f, 1.0f))
      return SettingResult::Invalid;
    config.opacity = floatValue;
    return SettingResult::Applied;
  }
  if (key == "fresnelPower") {
    if (!parseFloat(value, floatValue) || !inRange(floatValue, 0.05f, 8.0f))
      return SettingResult::Invalid;
    config.fresnelPower = floatValue;
    return SettingResult::Applied;
  }
  if (key == "fresnelScale") {
    if (!parseFloat(value, floatValue) || !inRange(floatValue, 0.0f, 4.0f))
      return SettingResult::Invalid;
    config.fresnelScale = floatValue;
    return SettingResult::Applied;
  }
  if (key == "waveBody") {
    if (!parseFloat(value, floatValue) || !inRange(floatValue, 0.0f, 2.0f))
      return SettingResult::Invalid;
    config.waveBody = floatValue;
    return SettingResult::Applied;
  }
  if (key == "gradientAngle") {
    if (!parseFloat(value, floatValue) || !inRange(floatValue, -180.0f, 180.0f))
      return SettingResult::Invalid;
    config.gradientAngle = floatValue;
    return SettingResult::Applied;
  }
  if (key == "colorTop" || key == "colorBot" || key == "waveColor") {
    Rgb rgb;
    if (!parseRgb(value, rgb))
      return SettingResult::Invalid;
    if (key == "colorTop")
      config.colorTop = rgb;
    else if (key == "colorBot")
      config.colorBot = rgb;
    else
      config.waveColor = rgb;
    return SettingResult::Applied;
  }
  if (key == "particles") {
    if (!parseInt(value, intValue) || intValue < 0 || intValue > 20000)
      return SettingResult::Invalid;
    config.particles = intValue;
    return SettingResult::Applied;
  }
  if (key == "particleOpacity") {
    if (!parseFloat(value, floatValue) || !inRange(floatValue, 0.0f, 1.0f))
      return SettingResult::Invalid;
    config.particleOpacity = floatValue;
    return SettingResult::Applied;
  }
  if (key == "particleSize") {
    if (!parseFloat(value, floatValue) || !inRange(floatValue, 0.0f, 64.0f))
      return SettingResult::Invalid;
    config.particleSize = floatValue;
    return SettingResult::Applied;
  }
  if (key == "particleSizeVar") {
    if (!parseFloat(value, floatValue) || !inRange(floatValue, 0.0f, 64.0f))
      return SettingResult::Invalid;
    config.particleSizeVar = floatValue;
    return SettingResult::Applied;
  }
  if (key == "particleSpeed") {
    if (!parseFloat(value, floatValue) || !inRange(floatValue, 0.0f, 20.0f))
      return SettingResult::Invalid;
    config.particleSpeed = floatValue;
    return SettingResult::Applied;
  }

  return SettingResult::Unknown;
}

bool configLoad(Config& config, const std::string& path) {
  std::ifstream file(path);
  if (!file) {
    XMB_LOG_INFO("config: {} not found, using defaults", path);
    return false;
  }

  std::string line;
  int lineNumber = 0;
  while (std::getline(file, line)) {
    ++lineNumber;
    const size_t comment = line.find('#');
    if (comment != std::string::npos)
      line.erase(comment);
    const std::string text = trim(line);
    if (text.empty())
      continue;

    size_t sep = text.find('=');
    if (sep == std::string::npos)
      sep = text.find_first_of(" \t");
    if (sep == std::string::npos) {
      XMB_LOG_WARN("config: {}:{}: expected 'key = value'", path, lineNumber);
      continue;
    }

    const std::string key = trim(text.substr(0, sep));
    const std::string value = trim(text.substr(sep + 1));
    switch (configSet(config, key, value)) {
      case SettingResult::Applied:
        break;
      case SettingResult::Unknown:
        XMB_LOG_WARN("config: {}:{}: unknown key '{}'", path, lineNumber, key);
        break;
      case SettingResult::Invalid:
        XMB_LOG_WARN("config: {}:{}: invalid value for '{}'", path, lineNumber,
                     key);
        break;
    }
  }
  return true;
}

}  // namespace xmbwave
