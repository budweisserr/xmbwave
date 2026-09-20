#include "desktop.h"

namespace xmbwave {

Desktop::~Desktop() {
  close();
}

//  lifecycle

bool Desktop::open(std::string* error) {
  close();

  dpy_ = XOpenDisplay(nullptr);
  if (!dpy_) {
    *error = "cannot open X display (is DISPLAY set?)";
    return false;
  }
  screen_ = DefaultScreen(dpy_);
  root_ = RootWindow(dpy_, screen_);

  XRRSelectInput(dpy_, root_, RRScreenChangeNotifyMask);
  refreshMonitors();
  return true;
}

void Desktop::close() {
  if (dpy_) {
    glXMakeCurrent(dpy_, None, nullptr);
    for (Surface& surface : surfaces_) {
      if (surface.window)
        XDestroyWindow(dpy_, surface.window);
    }
    surfaces_.clear();
    if (context_) {
      glXDestroyContext(dpy_, context_);
      context_ = nullptr;
    }
    if (visual_) {
      XFree(visual_);
      visual_ = nullptr;
    }
    XCloseDisplay(dpy_);
    dpy_ = nullptr;
  }
  monitors_.clear();
}

bool Desktop::createContext(const XVisualInfo* visual, std::string* error) {
  if (context_) {
    glXDestroyContext(dpy_, context_);
    context_ = nullptr;
  }
  context_ =
      glXCreateContext(dpy_, const_cast<XVisualInfo*>(visual), nullptr, True);
  if (!context_) {
    *error = "glXCreateContext failed for the window visual";
    return false;
  }
  return true;
}

//  windows

bool Desktop::createWindows(std::string* error) {
  if (!dpy_) {
    *error = "display not open";
    return false;
  }

  // Ask for a multisampled config first; the ribbon's strand edges are hard
  // aliased without it and no grid resolution fixes that.
  int fbAttribs[] = {GLX_X_RENDERABLE,
                     True,
                     GLX_DRAWABLE_TYPE,
                     GLX_WINDOW_BIT,
                     GLX_RENDER_TYPE,
                     GLX_RGBA_BIT,
                     GLX_RED_SIZE,
                     8,
                     GLX_GREEN_SIZE,
                     8,
                     GLX_BLUE_SIZE,
                     8,
                     GLX_DOUBLEBUFFER,
                     True,
                     GLX_SAMPLE_BUFFERS,
                     (samples_ > 0) ? 1 : 0,
                     GLX_SAMPLES,
                     samples_,
                     None};

  int configCount = 0;
  GLXFBConfig* configs =
      glXChooseFBConfig(dpy_, screen_, fbAttribs, &configCount);
  if ((!configs || configCount == 0) && samples_ > 0) {
    // No multisampled config available: fall back to single-sample.
    if (configs)
      XFree(configs);
    XMB_LOG_WARN("no {}-sample GLX config, falling back to no MSAA", samples_);
    fbAttribs[15] = 0;
    fbAttribs[17] = 0;
    configs = glXChooseFBConfig(dpy_, screen_, fbAttribs, &configCount);
  }
  if (!configs || configCount == 0) {
    if (configs)
      XFree(configs);
    *error = "no double-buffered GLX framebuffer configuration";
    return false;
  }
  XVisualInfo* visual = glXGetVisualFromFBConfig(dpy_, configs[0]);
  XFree(configs);
  if (!visual) {
    *error = "glXGetVisualFromFBConfig failed";
    return false;
  }
  if (!createContext(visual, error)) {
    XFree(visual);
    return false;
  }
  visual_ = visual;

  const Colormap colormap =
      XCreateColormap(dpy_, root_, visual->visual, AllocNone);
  if (!colormap) {
    *error = "XCreateColormap failed";
    return false;
  }

  for (const Monitor& monitor : monitors_) {
    XSetWindowAttributes attrs;
    std::memset(&attrs, 0, sizeof(attrs));
    attrs.colormap = colormap;
    attrs.background_pixel = 0;
    attrs.override_redirect = True;
    attrs.event_mask = 0;

    const Window window = XCreateWindow(
        dpy_, root_, monitor.rect.x, monitor.rect.y,
        (unsigned int)monitor.rect.w, (unsigned int)monitor.rect.h, 0,
        visual->depth, InputOutput, visual->visual,
        CWColormap | CWBackPixel | CWOverrideRedirect | CWEventMask, &attrs);
    if (!window) {
      *error = "XCreateWindow failed";
      return false;
    }

    setDesktopWindowHints(window);
    XMapWindow(dpy_, window);
    XLowerWindow(dpy_, window);
    // Empty input region: pointer and keyboard events fall through to the
    // root window, so the wallpaper never steals clicks from the WM.
    XShapeCombineRectangles(dpy_, window, ShapeInput, 0, 0, nullptr, 0,
                            ShapeSet, Unsorted);

    Surface surface;
    surface.monitorIndex = monitor.index;
    surface.window = window;
    surface.rect = monitor.rect;
    surfaces_.push_back(surface);
  }

  XSync(dpy_, False);
  return true;
}

void Desktop::setDesktopWindowHints(Window window) const {
  const Atom wmType = XInternAtom(dpy_, "_NET_WM_WINDOW_TYPE", False);
  const Atom wmTypeDesktop =
      XInternAtom(dpy_, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
  const Atom wmState = XInternAtom(dpy_, "_NET_WM_STATE", False);
  const Atom stateBelow = XInternAtom(dpy_, "_NET_WM_STATE_BELOW", False);
  const Atom stateSticky = XInternAtom(dpy_, "_NET_WM_STATE_STICKY", False);
  const Atom stateSkipTaskbar =
      XInternAtom(dpy_, "_NET_WM_STATE_SKIP_TASKBAR", False);
  const Atom stateSkipPager =
      XInternAtom(dpy_, "_NET_WM_STATE_SKIP_PAGER", False);
  const Atom wmDesktop = XInternAtom(dpy_, "_NET_WM_DESKTOP", False);

  const Atom type = wmTypeDesktop;
  XChangeProperty(dpy_, window, wmType, XA_ATOM, 32, PropModeReplace,
                  (const unsigned char*)&type, 1);

  const long states[4] = {(long)stateBelow, (long)stateSticky,
                          (long)stateSkipTaskbar, (long)stateSkipPager};
  XChangeProperty(dpy_, window, wmState, XA_ATOM, 32, PropModeReplace,
                  (const unsigned char*)states, 4);

  const long allDesktops = 0xFFFFFFFFl;
  XChangeProperty(dpy_, window, wmDesktop, XA_CARDINAL, 32, PropModeReplace,
                  (const unsigned char*)&allDesktops, 1);

  XStoreName(dpy_, window, "xmbwave");
  XClassHint hint;
  hint.res_name = const_cast<char*>("xmbwave");
  hint.res_class = const_cast<char*>("xmbwave");
  XSetClassHint(dpy_, window, &hint);
}

//  monitors

bool Desktop::refreshMonitors() {
  if (!dpy_)
    return false;

  int count = 0;
  XRRMonitorInfo* info = XRRGetMonitors(dpy_, root_, True, &count);
  std::vector<Monitor> next;
  if (info && count > 0) {
    next.reserve((size_t)count);
    for (int i = 0; i < count; ++i) {
      Monitor monitor;
      monitor.index = i;
      monitor.rect.x = info[i].x;
      monitor.rect.y = info[i].y;
      monitor.rect.w = info[i].width;
      monitor.rect.h = info[i].height;
      next.push_back(std::move(monitor));
    }
  }
  if (info)
    XRRFreeMonitors(info);

  if (next.empty()) {
    Monitor monitor;
    monitor.index = 0;
    monitor.rect = {0, 0, DisplayWidth(dpy_, screen_),
                    DisplayHeight(dpy_, screen_)};
    next.push_back(std::move(monitor));
  }

  bool changed = next.size() != monitors_.size();
  if (!changed) {
    for (size_t i = 0; i < next.size(); ++i) {
      if (next[i].rect != monitors_[i].rect) {
        changed = true;
        break;
      }
    }
  }

  monitors_ = std::move(next);
  if (changed && !surfaces_.empty())
    applyMonitorLayout();
  return changed;
}

void Desktop::applyMonitorLayout() {
  for (Surface& surface : surfaces_) {
    if (surface.monitorIndex < 0 ||
        surface.monitorIndex >= (int)monitors_.size())
      continue;
    const Rect& rect = monitors_[(size_t)surface.monitorIndex].rect;
    XMoveResizeWindow(dpy_, surface.window, rect.x, rect.y,
                      (unsigned int)rect.w, (unsigned int)rect.h);
    surface.rect = rect;
  }
  XFlush(dpy_);
}

Rect Desktop::windowRootRect(Window window) const {
  Rect rect;
  XWindowAttributes attrs;
  if (!XGetWindowAttributes(dpy_, window, &attrs))
    return rect;

  int rootX = 0;
  int rootY = 0;
  Window child = 0;
  if (!XTranslateCoordinates(dpy_, window, root_, 0, 0, &rootX, &rootY,
                             &child)) {
    rootX = attrs.x;
    rootY = attrs.y;
  }
  rect.x = rootX;
  rect.y = rootY;
  rect.w = attrs.width;
  rect.h = attrs.height;
  return rect;
}

int Desktop::monitorForRect(const Rect& rect) const {
  if (monitors_.empty())
    return -1;

  const int centerX = rect.x + rect.w / 2;
  const int centerY = rect.y + rect.h / 2;
  for (const Monitor& monitor : monitors_) {
    const Rect& r = monitor.rect;
    if (centerX >= r.x && centerX < r.x + r.w && centerY >= r.y &&
        centerY < r.y + r.h) {
      return monitor.index;
    }
  }

  return monitors_[0].index;
}

//  focus

bool Desktop::windowIsFullscreen(Window window) const {
  const Atom stateAtom = XInternAtom(dpy_, "_NET_WM_STATE", True);
  const Atom fullscreenAtom =
      XInternAtom(dpy_, "_NET_WM_STATE_FULLSCREEN", True);
  if (stateAtom == None || fullscreenAtom == None)
    return false;

  Atom type = None;
  int format = 0;
  unsigned long count = 0;
  unsigned long remaining = 0;
  unsigned char* data = nullptr;
  if (XGetWindowProperty(dpy_, window, stateAtom, 0, 16, False, XA_ATOM, &type,
                         &format, &count, &remaining, &data) != Success) {
    return false;
  }
  if (!data)
    return false;

  bool fullscreen = false;
  const Atom* atoms = (const Atom*)data;
  for (unsigned long i = 0; i < count; ++i) {
    if (atoms[i] == fullscreenAtom) {
      fullscreen = true;
      break;
    }
  }
  XFree(data);
  return fullscreen;
}

FocusState Desktop::queryFocus() const {
  FocusState focus;
  if (!dpy_)
    return focus;

  const Atom activeAtom = XInternAtom(dpy_, "_NET_ACTIVE_WINDOW", True);
  if (activeAtom == None)
    return focus;

  Atom type = None;
  int format = 0;
  unsigned long count = 0;
  unsigned long remaining = 0;
  unsigned char* data = nullptr;
  if (XGetWindowProperty(dpy_, root_, activeAtom, 0, 1, False, XA_WINDOW, &type,
                         &format, &count, &remaining, &data) != Success) {
    return focus;
  }
  if (!data || type != XA_WINDOW || count == 0) {
    if (data)
      XFree(data);
    return focus;
  }

  const Window active = *(Window*)data;
  XFree(data);
  if (active == 0 || active == root_)
    return focus;

  focus.active = active;
  focus.activeValid = true;
  focus.fullscreen = windowIsFullscreen(active);
  focus.monitorIndex = monitorForRect(windowRootRect(active));
  return focus;
}

bool shouldRenderSurface(bool pauseUnfocused, bool allMonitors,
                         int surfaceMonitor, const FocusState& focus) {
  if (!pauseUnfocused)
    return true;
  if (surfaceMonitor < 0)
    return true;
  if (!focus.activeValid)
    return true;  // desktop is visible everywhere

  if (allMonitors) {
    return !(focus.fullscreen && focus.monitorIndex == surfaceMonitor);
  }
  if (focus.fullscreen)
    return false;  // focused monitor is fully covered
  return surfaceMonitor == focus.monitorIndex;
}

bool Desktop::shouldRender(const Surface& surface,
                           const FocusState& focus) const {
  return shouldRenderSurface(pauseUnfocused_, allMonitors_,
                             surface.monitorIndex, focus);
}

}  // namespace xmbwave
