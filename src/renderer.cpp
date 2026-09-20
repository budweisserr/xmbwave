#include "renderer.h"

namespace xmbwave {
namespace {

const char* kBgVert = R"GLSL(#version 120
attribute vec2 aPos;
varying vec2 vUv;
void main() {
    vUv = aPos;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)GLSL";

const char* kBgFrag = R"GLSL(#version 120
varying vec2 vUv;
uniform vec3 uTop;
uniform vec3 uBot;
uniform vec2 uDir;
uniform float uTMin;
uniform float uTSpan;
void main() {
    float t = dot(vUv, uDir);
    float u = clamp((t - uTMin) / max(uTSpan, 1e-6), 0.0, 1.0);
    float g = u * u * (3.0 - 2.0 * u);
    gl_FragColor = vec4(mix(uTop, uBot, g), 1.0);
}
)GLSL";

const char* kWaveVert = R"GLSL(#version 120
attribute vec2 aGrid;
attribute float aDy;
attribute float aDz;
varying vec3 vEC;
void main() {
    vec3 v = vec3(aGrid.x, aDy, aGrid.y + aDz);
    vEC = v;
    gl_Position = vec4(v, 1.0);
}
)GLSL";

const char* kWaveFrag = R"GLSL(#version 120
varying vec3 vEC;
uniform vec3 uColor;
uniform float uOpacity;
uniform float uBrightness;
uniform float uFresnelPower;
uniform float uFresnelScale;
uniform float uWaveBody;
void main() {
    vec3 dx = dFdx(vEC);
    vec3 dy = dFdy(vEC);
    vec3 normal = normalize(cross(dx, dy));
    // Rim term: bright where the ribbon bends away from the viewer, plus a
    // floor so the flat parts of the curtain still read as a soft veil.
    // abs() keeps it independent of the mesh winding.
    float ndv = clamp(abs(dot(normal, vec3(0.0, 0.0, 1.0))), 0.0, 1.0);
    float fresnel = uWaveBody + uFresnelScale * pow(1.0 - ndv, uFresnelPower);
    float depthFade = smoothstep(-0.92, -0.52, vEC.z) * (1.0 - smoothstep(0.56, 0.94, vEC.z));
    float horizonFade = smoothstep(-0.08, 0.34, vEC.y) * (1.0 - smoothstep(0.58, 0.84, vEC.y));
    float intensity = fresnel * mix(0.52, 1.0, depthFade) * mix(0.72, 1.0, horizonFade);
    vec3 tint = mix(uColor, vec3(1.0), 0.58);
    vec3 hot = mix(tint, vec3(1.0), smoothstep(0.55, 1.0, intensity));
    gl_FragColor = vec4(hot * intensity * 0.74 * uBrightness, intensity * 0.55 * uOpacity);
}
)GLSL";

const char* kParticleVert = R"GLSL(#version 120
attribute vec3 aSeed;
uniform float uTime;
uniform float uFlow;
uniform float uRatio;
uniform float uSizeBase;
uniform float uSizeVar;
varying float vAlpha;
void main() {
    gl_PointSize = aSeed.z * uSizeVar + uSizeBase;
    float time = uTime * uFlow;
    float x = fract(time * (aSeed.x - 0.5) / 15.0 + aSeed.y * 50.0) * 2.0 - 1.0;
    float y = sin(sign(aSeed.y) * time * (aSeed.y + 1.5) / 4.0 + aSeed.x * 100.0)
            / ((6.0 - aSeed.x * 4.0 * aSeed.y) / uRatio);
    float opVar = mix(
        sin(time * (aSeed.x + 0.5) * 12.0 + aSeed.y * 10.0),
        sin(time * (aSeed.y + 1.5) * 6.0 + aSeed.x * 4.0),
        y * 0.5 + 0.5) * aSeed.x + aSeed.y;
    vAlpha = opVar * opVar * (1.0 - fract(aSeed.x + time * 0.00285));
    gl_Position = vec4(x, y, 0.0, 1.0);
}
)GLSL";

const char* kParticleFrag = R"GLSL(#version 120
varying float vAlpha;
uniform float uOpacity;
void main() {
    vec2 c = gl_PointCoord * 2.0 - 1.0;
    float d = dot(c, c);
    if (d > 1.0) discard;
    float sparkle = (1.0 - d) * (1.0 - d);
    float a = vAlpha * uOpacity * sparkle;
    gl_FragColor = vec4(vec3(a), 1.0);
}
)GLSL";

struct AttribBinding {
  GLuint index;
  const char* name;
};

GLuint compileShader(GLenum type, const char* source, std::string* error) {
  const GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);

  GLint ok = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[1024] = {0};
    glGetShaderInfoLog(shader, (GLsizei)sizeof(log) - 1, nullptr, log);
    *error = log;
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

GLuint linkProgram(const char* vertexSource, const char* fragmentSource,
                   const AttribBinding* attribs, int attribCount,
                   std::string* error) {
  const GLuint vertex = compileShader(GL_VERTEX_SHADER, vertexSource, error);
  if (!vertex)
    return 0;

  const GLuint fragment =
      compileShader(GL_FRAGMENT_SHADER, fragmentSource, error);
  if (!fragment) {
    glDeleteShader(vertex);
    return 0;
  }

  const GLuint program = glCreateProgram();
  glAttachShader(program, vertex);
  glAttachShader(program, fragment);
  for (int i = 0; i < attribCount; ++i) {
    glBindAttribLocation(program, attribs[i].index, attribs[i].name);
  }
  glLinkProgram(program);
  glDeleteShader(vertex);
  glDeleteShader(fragment);

  GLint ok = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[1024] = {0};
    glGetProgramInfoLog(program, (GLsizei)sizeof(log) - 1, nullptr, log);
    *error = log;
    glDeleteProgram(program);
    return 0;
  }
  return program;
}

using SwapIntervalProc = int (*)(unsigned int);

// Deterministic seed stream: the sparkle layout is identical on every run.
uint32_t nextSeed(uint32_t& state) {
  state += 0x9e3779b9u;
  uint32_t z = state;
  z = (z ^ (z >> 16)) * 0x21f0aaadu;
  z = (z ^ (z >> 15)) * 0x735a2d97u;
  return z ^ (z >> 15);
}

float seedUnit(uint32_t value) {
  return (float)(value >> 8) * (1.0f / 16777216.0f);
}

}  // namespace

Renderer::~Renderer() {
  shutdown();
}

// setup

bool Renderer::init(Desktop& desktop, const Config& config,
                    std::string* error) {
  if (desktop.surfaces().empty()) {
    *error = "no output window to render into";
    return false;
  }
  context_ = desktop.context();

  const Surface& first = desktop.surfaces().front();
  if (!glXMakeCurrent(desktop.xdisplay(), first.window, context_)) {
    *error = "glXMakeCurrent failed on the output window";
    return false;
  }

  if (!buildPrograms(error))
    return false;

  gridW_ = config.gridW;
  gridH_ = config.gridH;
  field_.assign((size_t)gridW_ * (size_t)gridH_ * 2, 0.0f);
  buildMesh(gridW_, gridH_);

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_BLEND);
  // The ribbon's strand edges are hard aliased without this.
  glEnable(GL_MULTISAMPLE);
  // Compatibility profile needs point sprites and coord replace enabled before
  // gl_PointCoord is defined in the fragment shader.
  glEnable(GL_POINT_SPRITE);
  glTexEnvi(GL_POINT_SPRITE, GL_COORD_REPLACE, GL_TRUE);
  glEnable(GL_PROGRAM_POINT_SIZE);
  {
    // One-shot: the swap interval is a context attribute, not live-tunable.
    const unsigned int interval = (config.vsync != 0) ? 1u : 0u;
    auto proc = reinterpret_cast<SwapIntervalProc>(
        glXGetProcAddressARB((const GLubyte*)"glXSwapIntervalMESA"));
    if (!proc) {
      proc = reinterpret_cast<SwapIntervalProc>(
          glXGetProcAddressARB((const GLubyte*)"glXSwapIntervalSGI"));
    }
    if (proc)
      proc(interval);
  }

  XMB_LOG_INFO("gl: grid {}x{}, kernel {}", gridW_, gridH_, waveKernelName());
  return true;
}

bool Renderer::buildPrograms(std::string* error) {
  const AttribBinding bgAttribs[] = {{0, "aPos"}};
  progBg_ = linkProgram(kBgVert, kBgFrag, bgAttribs, 1, error);
  if (!progBg_)
    return false;

  const AttribBinding waveAttribs[] = {{0, "aGrid"}, {1, "aDy"}, {2, "aDz"}};
  progWave_ = linkProgram(kWaveVert, kWaveFrag, waveAttribs, 3, error);
  if (!progWave_)
    return false;

  const AttribBinding particleAttribs[] = {{0, "aSeed"}};
  progParticles_ =
      linkProgram(kParticleVert, kParticleFrag, particleAttribs, 1, error);
  if (!progParticles_)
    return false;

  uBgTop_ = glGetUniformLocation(progBg_, "uTop");
  uBgBot_ = glGetUniformLocation(progBg_, "uBot");
  uBgDir_ = glGetUniformLocation(progBg_, "uDir");
  uBgTMin_ = glGetUniformLocation(progBg_, "uTMin");
  uBgTSpan_ = glGetUniformLocation(progBg_, "uTSpan");

  uColor_ = glGetUniformLocation(progWave_, "uColor");
  uOpacity_ = glGetUniformLocation(progWave_, "uOpacity");
  uBrightness_ = glGetUniformLocation(progWave_, "uBrightness");
  uFresnelPower_ = glGetUniformLocation(progWave_, "uFresnelPower");
  uFresnelScale_ = glGetUniformLocation(progWave_, "uFresnelScale");
  uWaveBody_ = glGetUniformLocation(progWave_, "uWaveBody");

  uPTime_ = glGetUniformLocation(progParticles_, "uTime");
  uPFlow_ = glGetUniformLocation(progParticles_, "uFlow");
  uPRatio_ = glGetUniformLocation(progParticles_, "uRatio");
  uPOpacity_ = glGetUniformLocation(progParticles_, "uOpacity");
  uPSizeBase_ = glGetUniformLocation(progParticles_, "uSizeBase");
  uPSizeVar_ = glGetUniformLocation(progParticles_, "uSizeVar");
  return true;
}

void Renderer::buildMesh(int gridW, int gridH) {
  vertexCount_ = gridW * gridH;

  std::vector<float> grid((size_t)vertexCount_ * 2);
  for (int row = 0; row < gridH; ++row) {
    for (int col = 0; col < gridW; ++col) {
      const float fx =
          (gridW > 1) ? ((float)col / (float)(gridW - 1)) * 2.0f - 1.0f : 0.0f;
      const float fy =
          (gridH > 1) ? ((float)row / (float)(gridH - 1)) * 2.0f - 1.0f : 0.0f;
      const size_t at = ((size_t)row * (size_t)gridW + (size_t)col) * 2;
      grid[at + 0] = fx;
      grid[at + 1] = fy;
    }
  }

  std::vector<uint32_t> indices;
  indices.reserve((size_t)std::max(gridW - 1, 0) *
                  (size_t)std::max(gridH - 1, 0) * 6);
  for (int row = 0; row + 1 < gridH; ++row) {
    for (int col = 0; col + 1 < gridW; ++col) {
      const uint32_t a = (uint32_t)(row * gridW + col);
      const uint32_t b = a + 1;
      const uint32_t c = a + (uint32_t)gridW;
      const uint32_t d = c + 1;
      indices.push_back(a);
      indices.push_back(c);
      indices.push_back(b);
      indices.push_back(b);
      indices.push_back(c);
      indices.push_back(d);
    }
  }
  indexCount_ = (GLsizei)indices.size();

  const float bgQuad[8] = {-1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f};

  glGenBuffers(1, &vboBg_);
  glBindBuffer(GL_ARRAY_BUFFER, vboBg_);
  glBufferData(GL_ARRAY_BUFFER, sizeof(bgQuad), bgQuad, GL_STATIC_DRAW);

  glGenBuffers(1, &vboGrid_);
  glBindBuffer(GL_ARRAY_BUFFER, vboGrid_);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(grid.size() * sizeof(float)),
               grid.data(), GL_STATIC_DRAW);

  glGenBuffers(1, &vboDisp_);
  glBindBuffer(GL_ARRAY_BUFFER, vboDisp_);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(field_.size() * sizeof(float)),
               nullptr, GL_DYNAMIC_DRAW);

  glGenBuffers(1, &ibo_);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               (GLsizeiptr)(indices.size() * sizeof(uint32_t)), indices.data(),
               GL_STATIC_DRAW);

  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

// drawing

void Renderer::frame(Desktop& desktop, const Config& config,
                     float animTimeSec) {
  Surface* firstActive = nullptr;
  for (Surface& surface : desktop.surfaces()) {
    if (surface.active) {
      firstActive = &surface;
      break;
    }
  }
  if (!firstActive)
    return;

  ::Display* xdisplay = desktop.xdisplay();
  if (!glXMakeCurrent(xdisplay, firstActive->window, context_))
    return;

  WaveParams params;
  params.amplitude = config.amplitude;
  params.tension = config.tension;
  params.detail = config.detail;
  params.ribbonScale = config.ribbonScale;
  params.softClip = config.softClip;
  params.zDetailScale = config.zDetailScale;
  waveField(params, animTimeSec, gridW_, gridH_, field_.data());

  glBindBuffer(GL_ARRAY_BUFFER, vboDisp_);
  glBufferSubData(GL_ARRAY_BUFFER, 0,
                  (GLsizeiptr)(field_.size() * sizeof(float)), field_.data());

  for (Surface& surface : desktop.surfaces()) {
    if (surface.active)
      drawSurface(xdisplay, surface, config, animTimeSec);
  }
}

void Renderer::ensureParticles(int count) {
  if (count == particleCount_ && vboSeeds_ != 0)
    return;
  if (vboSeeds_ == 0)
    glGenBuffers(1, &vboSeeds_);
  particleCount_ = count;
  if (count <= 0)
    return;

  std::vector<float> seeds((size_t)count * 3);
  uint32_t state = 0x9e3779b9u;
  for (int i = 0; i < count; ++i) {
    const float x = seedUnit(nextSeed(state));
    const float y = seedUnit(nextSeed(state));
    const float u = seedUnit(nextSeed(state));
    const float size = u * u * u * u * u * u * u * u;  // pow(u, 8)
    seeds[(size_t)i * 3 + 0] = x;
    seeds[(size_t)i * 3 + 1] = y;
    seeds[(size_t)i * 3 + 2] = size + 0.1f;
  }

  glBindBuffer(GL_ARRAY_BUFFER, vboSeeds_);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(seeds.size() * sizeof(float)),
               seeds.data(), GL_STATIC_DRAW);
}

void Renderer::drawParticles(const Surface& surface, const Config& config,
                             float animTimeSec) {
  if (config.particles <= 0)
    return;
  ensureParticles(config.particles);
  if (particleCount_ <= 0)
    return;

  const float aspect = (surface.rect.h > 0)
                           ? (float)surface.rect.w / (float)surface.rect.h
                           : 1.0f;
  const float ratio = std::max(1.0f, std::min(aspect, 2.0f)) * 0.375f;

  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE);  // additive sparkles
  glUseProgram(progParticles_);
  glUniform1f(uPTime_, animTimeSec);
  glUniform1f(uPFlow_, config.particleSpeed);
  glUniform1f(uPRatio_, ratio);
  glUniform1f(uPOpacity_, config.particleOpacity);
  glUniform1f(uPSizeBase_, config.particleSize);
  glUniform1f(uPSizeVar_, config.particleSizeVar);

  glBindBuffer(GL_ARRAY_BUFFER, vboSeeds_);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
  glDrawArrays(GL_POINTS, 0, particleCount_);
  glDisableVertexAttribArray(0);
}

void Renderer::drawSurface(::Display* xdisplay, const Surface& surface,
                           const Config& config, float animTimeSec) {
  if (!glXMakeCurrent(xdisplay, surface.window, context_))
    return;

  glViewport(0, 0, surface.rect.w, surface.rect.h);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  // Background: 2D linear gradient along gradientAngle degrees from vertical.
  glDisable(GL_BLEND);
  glUseProgram(progBg_);
  const float angle = config.gradientAngle * (3.14159265358979f / 180.0f);
  const float dirX = std::sin(angle);
  const float dirY = -std::cos(angle);
  const float tMin = -(std::fabs(dirX) + std::fabs(dirY));
  glUniform3f(uBgTop_, config.colorTop.r, config.colorTop.g, config.colorTop.b);
  glUniform3f(uBgBot_, config.colorBot.r, config.colorBot.g, config.colorBot.b);
  glUniform2f(uBgDir_, dirX, dirY);
  glUniform1f(uBgTMin_, tMin);
  glUniform1f(uBgTSpan_, -2.0f * tMin);
  glBindBuffer(GL_ARRAY_BUFFER, vboBg_);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glDisableVertexAttribArray(0);

  // Additive, matching OpenXMB: the fragment shader already folds intensity
  // into rgb, so straight alpha would multiply by it a second time.
  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_ONE, GL_ONE, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glUseProgram(progWave_);
  glUniform3f(uColor_, config.waveColor.r, config.waveColor.g,
              config.waveColor.b);
  glUniform1f(uOpacity_, config.opacity);
  glUniform1f(uBrightness_, config.brightness);
  glUniform1f(uFresnelPower_, config.fresnelPower);
  glUniform1f(uFresnelScale_, config.fresnelScale);
  glUniform1f(uWaveBody_, config.waveBody);

  glBindBuffer(GL_ARRAY_BUFFER, vboGrid_);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

  glBindBuffer(GL_ARRAY_BUFFER, vboDisp_);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 0, nullptr);
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(
      2, 1, GL_FLOAT, GL_FALSE, 0,
      (const void*)(uintptr_t)((size_t)vertexCount_ * sizeof(float)));

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
  glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);

  glDisableVertexAttribArray(1);
  glDisableVertexAttribArray(2);
  glDisableVertexAttribArray(0);

  drawParticles(surface, config, animTimeSec);

  glXSwapBuffers(xdisplay, surface.window);
}

void Renderer::shutdown() {
  if (context_ && glXGetCurrentContext() == context_) {
    if (progBg_)
      glDeleteProgram(progBg_);
    if (progWave_)
      glDeleteProgram(progWave_);
    if (progParticles_)
      glDeleteProgram(progParticles_);
    if (vboBg_)
      glDeleteBuffers(1, &vboBg_);
    if (vboGrid_)
      glDeleteBuffers(1, &vboGrid_);
    if (vboDisp_)
      glDeleteBuffers(1, &vboDisp_);
    if (vboSeeds_)
      glDeleteBuffers(1, &vboSeeds_);
    if (ibo_)
      glDeleteBuffers(1, &ibo_);
  }
  progBg_ = 0;
  progWave_ = 0;
  progParticles_ = 0;
  vboBg_ = 0;
  vboGrid_ = 0;
  vboDisp_ = 0;
  vboSeeds_ = 0;
  ibo_ = 0;
  particleCount_ = 0;
  context_ = nullptr;
  field_.clear();
}

}  // namespace xmbwave
