// Wave displacement field.
//
// Uniform cubic B-spline through control points, the shape the firmware uses.
// The basis came out of spline.elf's own lookup table; see
// docs/xmb-firmware-findings.md.
//
// b300/b380 are fed in by the PPU at run time and we don't have them, so the
// control-point values here are the analytic band/travel terms, with value
// noise standing in for the kernel. The spline evaluation is the firmware's.

#include "wave.h"

namespace xmbwave {
namespace {

// spline

// Uniform cubic B-spline weights, verified against spline.elf rodata 0x8a10.
inline void bsplineWeights(float t, float* w) {
  const float t2 = t * t;
  const float t3 = t2 * t;
  const float inv6 = 1.0f / 6.0f;
  w[0] = t3 * inv6;
  w[1] = (-3.0f * t3 + 3.0f * t2 + 3.0f * t + 1.0f) * inv6;
  w[2] = (3.0f * t3 - 6.0f * t2 + 4.0f) * inv6;
  w[3] = (1.0f - t) * (1.0f - t) * (1.0f - t) * inv6;
}

// Cubic B-spline through `n` control points at u in [0,1].
// Segment indices run 0..segments-1 and t reaches 1 on the last one, so the
// clamp is against `segments` - clamping to segments-1 freezes the final
// segment and leaves a dead straight run at the end of the ribbon.
inline float evalSpline(const float* cp, int n, float u) {
  const int segments = n - 3;
  float s = u * (float)segments;
  if (s < 0.0f)
    s = 0.0f;

  int seg = (int)s;
  if (seg < 0)
    seg = 0;
  if (seg > segments - 1)
    seg = segments - 1;

  float t = s - (float)seg;
  if (t < 0.0f)
    t = 0.0f;
  if (t > 1.0f)
    t = 1.0f;

  float w[4];
  bsplineWeights(t, w);
  // Sample index runs backwards relative to the basis order, matching the
  // firmware table's layout.
  return w[3] * cp[seg] + w[2] * cp[seg + 1] + w[1] * cp[seg + 2] +
         w[0] * cp[seg + 3];
}

//  noise

inline float latticeHash(int32_t x, int32_t y, int32_t z) {
  uint32_t h = (uint32_t)x * 0x8da6b343u;
  h ^= (uint32_t)y * 0xd8163841u;
  h ^= (uint32_t)z * 0xcb1ab31fu;
  h ^= h >> 16;
  h *= 0x7feb352du;
  h ^= h >> 15;
  h *= 0x846ca68bu;
  h ^= h >> 16;
  return (float)(h >> 8) * (1.0f / 16777216.0f);
}

inline float smootherstep01(float t) {
  return t * t * (3.0f - 2.0f * t);
}

inline float valueNoise(float x, float y, float z) {
  const float fx = std::floor(x);
  const float fy = std::floor(y);
  const float fz = std::floor(z);
  const int32_t ix = (int32_t)fx;
  const int32_t iy = (int32_t)fy;
  const int32_t iz = (int32_t)fz;
  const float ux = smootherstep01(x - fx);
  const float uy = smootherstep01(y - fy);
  const float uz = smootherstep01(z - fz);

  const float n000 = latticeHash(ix, iy, iz);
  const float n100 = latticeHash(ix + 1, iy, iz);
  const float n010 = latticeHash(ix, iy + 1, iz);
  const float n110 = latticeHash(ix + 1, iy + 1, iz);
  const float n001 = latticeHash(ix, iy, iz + 1);
  const float n101 = latticeHash(ix + 1, iy, iz + 1);
  const float n011 = latticeHash(ix, iy + 1, iz + 1);
  const float n111 = latticeHash(ix + 1, iy + 1, iz + 1);

  const float x00 = n000 + (n100 - n000) * ux;
  const float x10 = n010 + (n110 - n010) * ux;
  const float x01 = n001 + (n101 - n001) * ux;
  const float x11 = n011 + (n111 - n011) * ux;
  const float y0 = x00 + (x10 - x00) * uy;
  const float y1 = x01 + (x11 - x01) * uy;
  return y0 + (y1 - y0) * uz;
}

//  time

// Every time term is k*flow + const, so each phase is wrapped mod 2*pi on its
// own. Unwrapped they grow forever and the wave quantises after hours.
struct Phases {
  float baseWave;  // t * 0.5 * timeStep
  float quarter;   // flow * 0.25
  float struct1;   // flow * 0.7
  float struct2;   // flow * 0.35
  float band;      // flow * 0.09
  float travel1;   // flow * travelSpeed1
  float travel2;   // flow * travelSpeed2
  float pert;      // flow * 0.6
  float scroll;    // flow * 0.04, mod 1 (fract() argument)
};

inline double wrapPeriod(double value, double period) {
  const double r = std::fmod(value, period);
  return r < 0.0 ? r + period : r;
}

inline Phases makePhases(float timeSec) {
  constexpr double kTwoPi = 6.283185307179586476925286766559;
  const double t = (double)timeSec;
  const double flow = t * kFlowSpeed * kTimeStep;
  Phases ph;
  ph.baseWave = (float)wrapPeriod(t * 0.5 * kTimeStep, kTwoPi);
  ph.quarter = (float)wrapPeriod(flow * 0.25, kTwoPi);
  ph.struct1 = (float)wrapPeriod(flow * 0.7, kTwoPi);
  ph.struct2 = (float)wrapPeriod(flow * 0.35, kTwoPi);
  ph.band = (float)wrapPeriod(flow * 0.09, kTwoPi);
  ph.travel1 = (float)wrapPeriod(flow * kTravelSpeed1, kTwoPi);
  ph.travel2 = (float)wrapPeriod(flow * kTravelSpeed2, kTwoPi);
  ph.pert = (float)wrapPeriod(flow * 0.6, kTwoPi);
  ph.scroll = (float)wrapPeriod(flow * 0.04, 1.0);
  return ph;
}

//  control points

// One control point: x across the ribbon [0,1], vz depth [-1,1].
// x and z are coupled with mixed signs, which is what makes the strands weave.
inline float controlPoint(const Phases& ph, const WaveParams& p, float x,
                          float vz) {
  const float nx = x * 6.0f * p.detail;
  const float nz = vz * 6.0f * p.detail;
  const float rowPhase = ph.quarter + vz * 1.7f;

  const float core = valueNoise(nx, nz, 0.0f) * kKernelGain +
                     std::sin(rowPhase + x * 6.2f) * kBandAmplitude +
                     std::cos(vz * kBandSecondaryFreq + x * 4.8f + ph.band) *
                         kBandSecondaryAmp;

  const float legacy =
      std::sin((x * kPi * 1.3f + vz * 0.8f) - ph.travel1) * kTravelAmp1 *
          p.tension +
      std::sin((x * kPi * 2.8f - vz * 1.2f) + ph.travel2) * kTravelAmp2 +
      kPerturbation * kPerturbationScale *
          std::sin((x * (4.0f + kWaveLength * 2.0f) + vz * 4.0f - ph.pert) *
                   kWaveSpacing * 0.01f);

  return core * kPipelineBlend + legacy * (1.0f - kPipelineBlend);
}

// Vertex-side wave terms (spline.js wave.vert).
inline void vertexWave(const Phases& ph, const WaveParams& p, float vx,
                       float vz, float& totalWave) {
  float baseWave = std::cos(vx * 2.0f - ph.baseWave) * kWaveCosAmp + kWaveBias;
  baseWave *= (1.0f - kDamping);
  baseWave += p.tension * std::sin(vx * kWaveLength + ph.quarter);

  const float structured =
      kPerturbation * kPerturbationScale *
      (std::sin((vx * kWaveLength * 6.0f + vz * 0.5f) * kWaveSpacing * 0.01f +
                ph.struct1) *
           0.5f +
       std::sin((vx * kWaveLength * 10.0f - vz * 0.8f) * kWaveSpacing * 0.005f -
                ph.struct2) *
           0.25f);

  totalWave = (baseWave + structured) * p.ribbonScale;
  const float clip = (p.softClip > 1e-4f) ? p.softClip : 1e-4f;
  totalWave = clip * std::tanh(totalWave / clip);
}

inline void vertexScalar(const Phases& ph, const WaveParams& p, float vx,
                         float vz, const float* cp, int cpCount, float& dy,
                         float& dz) {
  const float u = vx * 0.5f + 0.5f;
  const float spline = evalSpline(cp, cpCount, u);

  float totalWave = 0.0f;
  vertexWave(ph, p, vx, vz, totalWave);

  // Depth offset from a horizontally scrolled second spline sample.
  float u2 = u - ph.scroll;
  u2 -= std::floor(u2);
  const float spline2 = evalSpline(cp, cpCount, u2);

  dy = (spline - totalWave) * p.amplitude;
  dz = (-spline2 * p.zDetailScale) * p.amplitude;
}

void fillScalar(const Phases& ph, const WaveParams& p, int gw, int gh,
                float* out) {
  const int n = gw * gh;
  float* dyPlane = out;
  float* dzPlane = out + n;
  const float xStep = (gw > 1) ? 2.0f / (float)(gw - 1) : 0.0f;
  const float yStep = (gh > 1) ? 2.0f / (float)(gh - 1) : 0.0f;

  float cp[kControlPoints];

  for (int row = 0; row < gh; ++row) {
    const float vz = -1.0f + (float)row * yStep;
    for (int i = 0; i < kControlPoints; ++i) {
      const float x = (float)i / (float)(kControlPoints - 1);
      cp[i] = controlPoint(ph, p, x, vz);
    }

    float* dyRow = dyPlane + (size_t)row * (size_t)gw;
    float* dzRow = dzPlane + (size_t)row * (size_t)gw;
    for (int col = 0; col < gw; ++col) {
      const float vx = -1.0f + (float)col * xStep;
      vertexScalar(ph, p, vx, vz, cp, kControlPoints, dyRow[col], dzRow[col]);
    }
  }
}

#if defined(__AVX2__)

//  AVX2

inline __m256 cosAvx2(__m256 x) {
  constexpr float kPiF = 3.14159265358979323846f;
  constexpr float kPiHi = 3.140625f;  // exactly representable
  constexpr float kPiLo = kPiF - 3.140625f;

  const __m256 n =
      _mm256_round_ps(_mm256_mul_ps(x, _mm256_set1_ps(1.0f / kPiF)),
                      _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
  __m256 r = _mm256_fnmadd_ps(n, _mm256_set1_ps(kPiHi), x);
  r = _mm256_fnmadd_ps(n, _mm256_set1_ps(kPiLo), r);

  const __m256i odd = _mm256_slli_epi32(_mm256_cvttps_epi32(n), 31);

  const __m256 z = _mm256_mul_ps(r, r);
  __m256 poly = _mm256_fmadd_ps(z, _mm256_set1_ps(2.443315711809948e-5f),
                                _mm256_set1_ps(-1.388731625493765e-3f));
  poly = _mm256_fmadd_ps(z, poly, _mm256_set1_ps(4.166664568298827e-2f));
  poly = _mm256_mul_ps(_mm256_mul_ps(z, z), poly);
  __m256 c = _mm256_fnmadd_ps(_mm256_set1_ps(0.5f), z, _mm256_set1_ps(1.0f));
  c = _mm256_add_ps(c, poly);
  return _mm256_xor_ps(c, _mm256_castsi256_ps(odd));
}

inline __m256 sinAvx2(__m256 x) {
  return cosAvx2(_mm256_sub_ps(x, _mm256_set1_ps(kPi * 0.5f)));
}

inline __m256 expAvx2(__m256 x) {
  const __m256 kLog2e = _mm256_set1_ps(1.4426950408889634f);
  const __m256 kLn2 = _mm256_set1_ps(0.6931471805599453f);
  x = _mm256_max_ps(_mm256_min_ps(x, _mm256_set1_ps(60.0f)),
                    _mm256_set1_ps(-60.0f));

  const __m256 n = _mm256_round_ps(
      _mm256_mul_ps(x, kLog2e), _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
  const __m256 r = _mm256_fnmadd_ps(n, kLn2, x);

  __m256 poly = _mm256_set1_ps(1.0f / 720.0f);
  poly = _mm256_fmadd_ps(poly, r, _mm256_set1_ps(1.0f / 120.0f));
  poly = _mm256_fmadd_ps(poly, r, _mm256_set1_ps(1.0f / 24.0f));
  poly = _mm256_fmadd_ps(poly, r, _mm256_set1_ps(1.0f / 6.0f));
  poly = _mm256_fmadd_ps(poly, r, _mm256_set1_ps(0.5f));
  poly = _mm256_fmadd_ps(poly, r, _mm256_set1_ps(1.0f));
  poly = _mm256_fmadd_ps(poly, r, _mm256_set1_ps(1.0f));

  __m256i scale = _mm256_cvttps_epi32(n);
  scale = _mm256_add_epi32(scale, _mm256_set1_epi32(127));
  scale = _mm256_slli_epi32(scale, 23);
  return _mm256_mul_ps(poly, _mm256_castsi256_ps(scale));
}

inline __m256 tanhAvx2(__m256 x) {
  x = _mm256_max_ps(_mm256_min_ps(x, _mm256_set1_ps(15.0f)),
                    _mm256_set1_ps(-15.0f));
  const __m256 e = expAvx2(_mm256_add_ps(x, x));
  return _mm256_div_ps(_mm256_sub_ps(e, _mm256_set1_ps(1.0f)),
                       _mm256_add_ps(e, _mm256_set1_ps(1.0f)));
}

// The spline itself stays scalar (it is a handful of ops per vertex and only
// the per-row control points feed it); the trigonometric vertex terms - the
// expensive part - are evaluated eight lanes at a time.
inline __m256 vertexWaveAvx2(const Phases& ph, const WaveParams& p, __m256 vx,
                             __m256 vz) {
  const __m256 vTwo = _mm256_set1_ps(2.0f);
  __m256 baseWave = _mm256_add_ps(
      _mm256_mul_ps(
          cosAvx2(_mm256_fnmadd_ps(vx, vTwo, _mm256_set1_ps(ph.baseWave))),
          _mm256_set1_ps(kWaveCosAmp)),
      _mm256_set1_ps(kWaveBias));
  baseWave = _mm256_mul_ps(baseWave, _mm256_set1_ps(1.0f - kDamping));
  baseWave =
      _mm256_fmadd_ps(_mm256_set1_ps(p.tension),
                      sinAvx2(_mm256_fmadd_ps(vx, _mm256_set1_ps(kWaveLength),
                                              _mm256_set1_ps(ph.quarter))),
                      baseWave);

  const __m256 structured = _mm256_mul_ps(
      _mm256_set1_ps(kPerturbation * kPerturbationScale),
      _mm256_add_ps(
          _mm256_mul_ps(
              sinAvx2(_mm256_add_ps(
                  _mm256_mul_ps(_mm256_fmadd_ps(
                                    vz, _mm256_set1_ps(0.5f),
                                    _mm256_mul_ps(vx, _mm256_set1_ps(
                                                          kWaveLength * 6.0f))),
                                _mm256_set1_ps(kWaveSpacing * 0.01f)),
                  _mm256_set1_ps(ph.struct1))),
              _mm256_set1_ps(0.5f)),
          _mm256_mul_ps(
              sinAvx2(_mm256_sub_ps(
                  _mm256_mul_ps(
                      _mm256_fnmadd_ps(
                          vz, _mm256_set1_ps(0.8f),
                          _mm256_mul_ps(vx,
                                        _mm256_set1_ps(kWaveLength * 10.0f))),
                      _mm256_set1_ps(kWaveSpacing * 0.005f)),
                  _mm256_set1_ps(ph.struct2))),
              _mm256_set1_ps(0.25f))));

  __m256 total = _mm256_mul_ps(_mm256_add_ps(baseWave, structured),
                               _mm256_set1_ps(p.ribbonScale));
  const float clipScalar = (p.softClip > 1e-4f) ? p.softClip : 1e-4f;
  const __m256 vClip = _mm256_set1_ps(clipScalar);
  return _mm256_mul_ps(vClip, tanhAvx2(_mm256_div_ps(total, vClip)));
}

void fillAvx2(const Phases& ph, const WaveParams& p, int gw, int gh,
              float* out) {
  const int n = gw * gh;
  float* dyPlane = out;
  float* dzPlane = out + n;
  const float xStep = (gw > 1) ? 2.0f / (float)(gw - 1) : 0.0f;
  const float yStep = (gh > 1) ? 2.0f / (float)(gh - 1) : 0.0f;

  const __m256 vAmp = _mm256_set1_ps(p.amplitude);
  const __m256 vZDetail = _mm256_set1_ps(-p.zDetailScale * p.amplitude);

  float cp[kControlPoints];

  for (int row = 0; row < gh; ++row) {
    const float vzF = -1.0f + (float)row * yStep;
    const __m256 vz = _mm256_set1_ps(vzF);
    for (int i = 0; i < kControlPoints; ++i) {
      const float x = (float)i / (float)(kControlPoints - 1);
      cp[i] = controlPoint(ph, p, x, vzF);
    }

    float* dyRow = dyPlane + (size_t)row * (size_t)gw;
    float* dzRow = dzPlane + (size_t)row * (size_t)gw;

    int col = 0;
    for (; col + 8 <= gw; col += 8) {
      const __m256 vx = _mm256_set_ps(
          -1.0f + (float)(col + 7) * xStep, -1.0f + (float)(col + 6) * xStep,
          -1.0f + (float)(col + 5) * xStep, -1.0f + (float)(col + 4) * xStep,
          -1.0f + (float)(col + 3) * xStep, -1.0f + (float)(col + 2) * xStep,
          -1.0f + (float)(col + 1) * xStep, -1.0f + (float)(col + 0) * xStep);

      const __m256 totalWave = vertexWaveAvx2(ph, p, vx, vz);

      // Spline samples are cheap and per-lane; evaluate scalar and pack.
      float spline[8];
      float spline2[8];
      for (int lane = 0; lane < 8; ++lane) {
        float u = (-1.0f + (float)(col + lane) * xStep) * 0.5f + 0.5f;
        spline[lane] = evalSpline(cp, kControlPoints, u);
        float u2 = u - ph.scroll;
        u2 -= std::floor(u2);
        spline2[lane] = evalSpline(cp, kControlPoints, u2);
      }
      const __m256 vSpline =
          _mm256_set_ps(spline[7], spline[6], spline[5], spline[4], spline[3],
                        spline[2], spline[1], spline[0]);
      const __m256 vSpline2 =
          _mm256_set_ps(spline2[7], spline2[6], spline2[5], spline2[4],
                        spline2[3], spline2[2], spline2[1], spline2[0]);

      const __m256 dy = _mm256_mul_ps(_mm256_sub_ps(vSpline, totalWave), vAmp);
      const __m256 dz = _mm256_mul_ps(vSpline2, vZDetail);
      _mm256_storeu_ps(dyRow + col, dy);
      _mm256_storeu_ps(dzRow + col, dz);
    }
    for (; col < gw; ++col) {
      const float vx = -1.0f + (float)col * xStep;
      vertexScalar(ph, p, vx, vzF, cp, kControlPoints, dyRow[col], dzRow[col]);
    }
  }
}

#endif  // __AVX2__

}  // namespace

const char* waveKernelName() {
#if defined(__AVX2__)
  return "avx2+fma";
#else
  return "scalar";
#endif
}

void waveFieldScalar(const WaveParams& params, float timeSec, int gridW,
                     int gridH, float* out) {
  fillScalar(makePhases(timeSec), params, gridW, gridH, out);
}

#if defined(__AVX2__)
void waveFieldAvx2(const WaveParams& params, float timeSec, int gridW,
                   int gridH, float* out) {
  fillAvx2(makePhases(timeSec), params, gridW, gridH, out);
}
#endif

void waveField(const WaveParams& params, float timeSec, int gridW, int gridH,
               float* out) {
  const Phases phases = makePhases(timeSec);
#if defined(__AVX2__)
  fillAvx2(phases, params, gridW, gridH, out);
#else
  fillScalar(phases, params, gridW, gridH, out);
#endif
}

}  // namespace xmbwave
