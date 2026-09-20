#pragma once

// Needed for the GL 2.0 entry points declared in glext.h; libGL exports them.
// Must come before any GL header in this translation unit.
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif

#include <GL/gl.h>
#include <GL/glx.h>
#include <X11/Xlib.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "config.h"
#include "desktop.h"
#include "log.h"
#include "wave.h"

namespace xmbwave {

// Uploads the CPU wave field once per frame and draws the active surfaces.
// All surfaces share one GLX context, so the GL objects live here.
class Renderer {
 public:
  Renderer() = default;
  ~Renderer();

  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;

  bool init(Desktop& desktop, const Config& config, std::string* error);
  void shutdown();

  // Only surfaces with Surface::active set are touched.
  void frame(Desktop& desktop, const Config& config, float animTimeSec);

 private:
  bool buildPrograms(std::string* error);
  void buildMesh(int gridW, int gridH);
  void ensureParticles(int count);
  void drawSurface(::Display* xdisplay, const Surface& surface,
                   const Config& config, float animTimeSec);
  void drawParticles(const Surface& surface, const Config& config,
                     float animTimeSec);

  GLuint progBg_ = 0;
  GLuint progWave_ = 0;
  GLuint progParticles_ = 0;
  GLuint vboBg_ = 0;
  GLuint vboGrid_ = 0;
  GLuint vboDisp_ = 0;
  GLuint vboSeeds_ = 0;
  GLuint ibo_ = 0;
  GLsizei indexCount_ = 0;
  int vertexCount_ = 0;
  int gridW_ = 0;
  int gridH_ = 0;
  int particleCount_ = 0;

  GLint uBgTop_ = -1;
  GLint uBgBot_ = -1;
  GLint uBgDir_ = -1;
  GLint uBgTMin_ = -1;
  GLint uBgTSpan_ = -1;
  GLint uColor_ = -1;
  GLint uOpacity_ = -1;
  GLint uBrightness_ = -1;
  GLint uFresnelPower_ = -1;
  GLint uFresnelScale_ = -1;
  GLint uWaveBody_ = -1;

  GLint uPTime_ = -1;
  GLint uPFlow_ = -1;
  GLint uPRatio_ = -1;
  GLint uPOpacity_ = -1;
  GLint uPSizeBase_ = -1;
  GLint uPSizeVar_ = -1;

  GLXContext context_ = nullptr;

  std::vector<float> field_;
};

}  // namespace xmbwave
