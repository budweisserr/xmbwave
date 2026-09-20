#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "config.h"
#include "desktop.h"
#include "log.h"
#include "renderer.h"
#include "wave.h"

namespace xmbwave {

// Parses the command line, loads the config file, brings up the desktop
// windows and runs the render loop until a signal arrives.
int runApplication(int argc, char** argv);

}  // namespace xmbwave
