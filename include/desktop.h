#pragma once

// Needed for the GL 2.0 entry points declared in glext.h; libGL exports them.
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif

#include <GL/gl.h>
#include <GL/glx.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/shape.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "log.h"

namespace xmbwave {

struct Rect {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;

  bool operator==(const Rect& other) const {
    return x == other.x && y == other.y && w == other.w && h == other.h;
  }

  bool operator!=(const Rect& other) const { return !(*this == other); }
};

struct Monitor {
  int index = 0;
  Rect rect;
};

// One desktop window the wave is painted on, one per monitor.
struct Surface {
  int monitorIndex = -1;
  Window window = 0;
  Rect rect;
  bool active = true;
};

struct FocusState {
  Window active = 0;
  bool activeValid = false;
  bool fullscreen = false;
  int monitorIndex = -1;
};

// Whether a surface animates: the focused monitor only, and off while a
// fullscreen window covers it. Split out so --selftest can hit it with no WM.
bool shouldRenderSurface(bool pauseUnfocused, bool allMonitors,
                         int surfaceMonitor, const FocusState& focus);

// X connection, monitors, output windows and one shared GLX context.
// Focus and layout are polled, not subscribed; a wallpaper can live with that.
class Desktop {
 public:
  Desktop() = default;
  ~Desktop();

  Desktop(const Desktop&) = delete;
  Desktop& operator=(const Desktop&) = delete;

  bool open(std::string* error);
  void close();
  bool createWindows(std::string* error);

  // Re-queries RandR. Returns true if the layout changed; the windows are
  // moved and resized in that case.
  bool refreshMonitors();

  FocusState queryFocus() const;
  bool shouldRender(const Surface& surface, const FocusState& focus) const;

  void setSamples(int value) { samples_ = value; }

  void setPauseUnfocused(bool value) { pauseUnfocused_ = value; }

  void setAllMonitors(bool value) { allMonitors_ = value; }

  ::Display* xdisplay() const { return dpy_; }

  Window root() const { return root_; }

  GLXContext context() const { return context_; }

  const std::vector<Monitor>& monitors() const { return monitors_; }

  std::vector<Surface>& surfaces() { return surfaces_; }

 private:
  bool createContext(const XVisualInfo* visual, std::string* error);
  void applyMonitorLayout();
  void setDesktopWindowHints(Window window) const;
  bool windowIsFullscreen(Window window) const;
  Rect windowRootRect(Window window) const;
  int monitorForRect(const Rect& rect) const;

  ::Display* dpy_ = nullptr;
  Window root_ = 0;
  int screen_ = 0;
  GLXContext context_ = nullptr;
  XVisualInfo* visual_ = nullptr;

  std::vector<Monitor> monitors_;
  std::vector<Surface> surfaces_;

  int samples_ = 0;
  bool pauseUnfocused_ = true;
  bool allMonitors_ = false;
};

}  // namespace xmbwave
