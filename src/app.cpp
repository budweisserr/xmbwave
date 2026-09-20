#include "app.h"

namespace xmbwave {
namespace {

volatile std::sig_atomic_t g_quit = 0;

void onSignal(int) {
  g_quit = 1;
}

struct Setting {
  std::string key;
  std::string value;
};

struct Options {
  bool help = false;
  bool version = false;
  bool selftest = false;
  std::string configPath;
  std::vector<Setting> overrides;
};

void printUsage(std::FILE* out) {
  std::fprintf(
      out,
      "\n"
      "Usage: xmbwave [options]\n"
      "\n"
      "Creates one override-redirect desktop window per monitor. Without "
      "--config\n"
      "the file below is loaded when present; --set overrides it.\n"
      "\n"
      "Options:\n"
      "  --config <path>       config file (default: %s)\n"
      "  --set <key>=<value>   override one setting, repeatable\n"
      "  --selftest            run the wave and policy checks, then exit\n"
      "  --version             print the version and exit\n"
      "  --help                print this help and exit\n"
      "\n"
      "Keys accepted by --set are the ones documented in config/xmbwave.conf,\n"
      "for example: --set fps=30 --set grid=320x120 --set pauseUnfocused=0\n",
      configDefaultPath().c_str());
}

bool needValue(int argc, char** argv, int& index, const char* flag,
               std::string& out) {
  if (index + 1 >= argc) {
    XMB_LOG_ERROR("xmbwave: {} expects a value\n", flag);
    return false;
  }
  out = argv[++index];
  return true;
}

bool parseArgs(int argc, char** argv, Options& opts) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    std::string value;

    if (arg == "--help" || arg == "-h") {
      opts.help = true;
    } else if (arg == "--version") {
      opts.version = true;
    } else if (arg == "--selftest") {
      opts.selftest = true;
    } else if (arg == "--config") {
      if (!needValue(argc, argv, i, "--config", value))
        return false;
      opts.configPath = value;
    } else if (arg == "--set") {
      if (!needValue(argc, argv, i, "--set", value))
        return false;
      const size_t equals = value.find('=');
      if (equals == std::string::npos || equals == 0) {
        std::fprintf(stderr, "xmbwave: --set expects key=value, got '%s'\n",
                     value.c_str());
        return false;
      }
      opts.overrides.push_back(
          {value.substr(0, equals), value.substr(equals + 1)});
    } else {
      std::fprintf(stderr, "xmbwave: unknown option '%s'\n", arg.c_str());
      return false;
    }
  }
  return true;
}

int runPolicyTest() {
  struct Case {
    bool pauseUnfocused;
    bool allMonitors;
    int surfaceMonitor;
    bool activeValid;
    bool fullscreen;
    int focusMonitor;
    bool expect;
    const char* name;
  };

  const Case cases[] = {
      {true, false, 1, false, false, -1, true,
       "desktop focused, nothing covered"},
      {false, false, 1, true, true, 0, true, "pause disabled"},
      {true, false, 1, true, false, 1, true, "focused monitor animates"},
      {true, false, 0, true, false, 1, false, "unfocused monitor pauses"},
      {true, false, 1, true, true, 1, false,
       "fullscreen hides focused monitor"},
      {true, false, 0, true, true, 1, false, "fullscreen pauses every monitor"},
      {true, true, 1, true, true, 1, false,
       "allMonitors: covered monitor pauses"},
      {true, true, 0, true, true, 1, true,
       "allMonitors: other monitor animates"},
      {true, true, 0, true, false, 1, true, "allMonitors: normal window"},
      {true, false, -1, true, false, 1, true,
       "surface without a known monitor"},
  };

  int failures = 0;
  XMB_LOG_INFO("visibility policy:");
  for (const Case& test : cases) {
    FocusState focus;
    focus.activeValid = test.activeValid;
    focus.fullscreen = test.fullscreen;
    focus.monitorIndex = test.focusMonitor;
    const bool got = shouldRenderSurface(test.pauseUnfocused, test.allMonitors,
                                         test.surfaceMonitor, focus);
    const bool ok = (got == test.expect);
    if (!ok)
      ++failures;
    XMB_LOG_INFO("  {:<38} {}", test.name, ok ? "ok" : "FAIL");
  }
  return failures;
}

int runSelfTest() {
  const WaveParams params;
  const int gridW = 257;  // odd dimensions exercise the SIMD tail
  const int gridH = 65;
  const size_t count = (size_t)gridW * (size_t)gridH;
  const float times[] = {0.0f, 1.0f, 12.5f, 4000.0f, 123456.0f};

  std::vector<float> reference(count * 2);
  std::vector<float> candidate(count * 2);

  XMB_LOG_INFO("wave kernel {} on a {}x{} grid", waveKernelName(), gridW,
               gridH);

  int failures = 0;
  for (float time : times) {
    waveFieldScalar(params, time, gridW, gridH, reference.data());

    float maxError = 0.0f;
#if defined(__AVX2__)
    waveFieldAvx2(params, time, gridW, gridH, candidate.data());
    for (size_t i = 0; i < reference.size(); ++i) {
      maxError = std::max(maxError, std::fabs(reference[i] - candidate[i]));
    }
#endif

    bool finite = true;
    float maxAbs = 0.0f;
    for (size_t i = 0; i < reference.size(); ++i) {
      if (!std::isfinite(reference[i]))
        finite = false;
      maxAbs = std::max(maxAbs, std::fabs(reference[i]));
    }

    const bool ok = finite && maxError <= 1e-4f && maxAbs < 4.0f;
    if (!ok)
      ++failures;
    XMB_LOG_INFO("  t={:<10.2f} maxErr={:.3e} maxAbs={:.4f} {}", time, maxError,
                 maxAbs, ok ? "ok" : "FAIL");
  }

  failures += runPolicyTest();

  XMB_LOG_INFO("selftest: {}", failures == 0 ? "PASS" : "FAIL");
  return failures == 0 ? 0 : 1;
}

}  // namespace

int runApplication(int argc, char** argv) {
  logInit();

  Options opts;
  if (!parseArgs(argc, argv, opts)) {
    printUsage(stderr);
    return 2;
  }
  if (opts.help) {
    printUsage(stdout);
    return 0;
  }
  if (opts.version) {
    XMB_LOG_INFO("wave kernel {}\n", waveKernelName());
    return 0;
  }
  if (opts.selftest)
    return runSelfTest();

  Config config;
  config.configPath =
      opts.configPath.empty() ? configDefaultPath() : opts.configPath;
  configLoad(config, config.configPath);

  for (const Setting& setting : opts.overrides) {
    if (configSet(config, setting.key, setting.value) !=
        SettingResult::Applied) {
      XMB_LOG_ERROR("xmbwave: invalid --set {}={}\n", setting.key.c_str(),
                    setting.value.c_str());
      return 2;
    }
  }
  logSetLevel(config.logLevel);

  XMB_LOG_INFO("xmbwave starting, wave kernel {}", waveKernelName());

  std::string error;
  Desktop desktop;
  if (!desktop.open(&error)) {
    XMB_LOG_ERROR("{}", error);
    return 1;
  }
  desktop.setSamples(config.msaa);
  if (!desktop.createWindows(&error)) {
    XMB_LOG_ERROR("{}", error);
    return 1;
  }
  desktop.setPauseUnfocused(config.pauseUnfocused != 0);
  desktop.setAllMonitors(config.allMonitors != 0);
  XMB_LOG_INFO("created {} desktop window(s)", desktop.surfaces().size());

  Renderer renderer;
  if (!renderer.init(desktop, config, &error)) {
    XMB_LOG_ERROR("{}", error);
    return 1;
  }

  std::signal(SIGINT, onSignal);
  std::signal(SIGTERM, onSignal);
  std::signal(SIGPIPE, SIG_IGN);

  using Clock = std::chrono::steady_clock;
  auto lastTick = Clock::now();
  auto lastFocusPoll = lastTick;
  auto nextFrame = lastTick;

  FocusState focus = desktop.queryFocus();
  float animTime = 0.0f;
  bool quit = false;

  while (!quit && !g_quit) {
    const auto now = Clock::now();
    float dt = std::chrono::duration<float>(now - lastTick).count();
    lastTick = now;
    if (dt > 0.25f)
      dt = 0.25f;  // survive suspend/resume without a jump
    animTime += dt * config.speed;

    if (now - lastFocusPoll >= std::chrono::milliseconds(config.focusPollMs)) {
      desktop.refreshMonitors();
      focus = desktop.queryFocus();
      lastFocusPoll = now;
    }

    bool anyActive = false;
    for (Surface& surface : desktop.surfaces()) {
      surface.active = desktop.shouldRender(surface, focus);
      anyActive = anyActive || surface.active;
    }

    if (!anyActive) {
      // Nothing visible: sleep past the next focus poll, no CPU wave work.
      std::this_thread::sleep_for(
          std::chrono::milliseconds(std::max(config.focusPollMs / 2, 50)));
      lastTick = Clock::now();
      nextFrame = lastTick;
      continue;
    }

    renderer.frame(desktop, config, animTime);

    // Pace in software always; with vsync the swap already blocks.
    // blocks and this simply does not sleep.
    const auto frameDuration = std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>(1.0 / (double)std::max(config.fps, 1)));
    nextFrame += frameDuration;
    const auto current = Clock::now();
    if (nextFrame > current) {
      std::this_thread::sleep_until(nextFrame);
    } else {
      nextFrame = current;  // fell behind, resynchronise
    }
  }

  renderer.shutdown();
  desktop.close();
  XMB_LOG_INFO("xmbwave stopped");
  return 0;
}

}  // namespace xmbwave
