#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

#if defined(__AVX2__)
#include <immintrin.h>
#endif

namespace xmbwave {

// Wave parameters and the field entry points.
//
// Uniform cubic B-spline through 28 control points per depth row, using the
// reference defaults from ps3xmbwave/spline-settings.js.

inline constexpr float kPi = 3.14159265358979323846f;

// vertex-side wave
inline constexpr float kWaveCosAmp = 0.09f;
inline constexpr float kWaveBias = -0.1f;
inline constexpr float kDamping = 0.0001f;
inline constexpr float kFlowSpeed = 0.18f;
inline constexpr float kTimeStep = 1.0f;
inline constexpr float kWaveLength = 0.306001f;
inline constexpr float kWaveSpacing = 407.658f;
inline constexpr float kPerturbation = 0.0998587f;
inline constexpr float kPerturbationScale = 0.07f;

// spline layer
inline constexpr float kBandAmplitude = 0.200f;
inline constexpr float kBandSecondaryFreq = 7.0f;
inline constexpr float kBandSecondaryAmp = 0.025f;
inline constexpr float kTravelSpeed1 = 0.25f;
inline constexpr float kTravelAmp1 = 0.014f;
inline constexpr float kTravelSpeed2 = 0.15f;
inline constexpr float kTravelAmp2 = 0.008f;

inline constexpr int kControlPoints = 28;

inline constexpr float kPipelineBlend = 0.45f;
inline constexpr float kKernelGain = 0.04f;

struct WaveParams {
  float amplitude = 1.0f;      // overall gain on both displacements
  float tension = 0.12f;       // reference `tension`
  float detail = 4.5f;         // value-noise frequency, the kernel stand-in
  float ribbonScale = 0.5f;    // reference `waveHeightScale`
  float softClip = 0.22f;      // reference `waveSoftClip`
  float zDetailScale = 0.08f;  // reference `zDetailScale`
};

// Computes a gridW x gridH field of vertex displacements for `timeSec`.
//
// The grid spans [-1, 1] on both axes, matching the reference: x is the screen
// axis and y the depth axis. Output is planar and tightly packed: out[0 .. N)
// holds the vertical displacement (dy) and out[N .. 2N) the depth displacement
// (dz), where N = gridW * gridH and vertices are row-major.
void waveField(const WaveParams& params, float timeSec, int gridW, int gridH,
               float* out);

// Portable reference used by --selftest and as the fallback kernel.
void waveFieldScalar(const WaveParams& params, float timeSec, int gridW,
                     int gridH, float* out);

const char* waveKernelName();

#if defined(__AVX2__)
// AVX2/FMA kernel; same contract as waveFieldScalar. Exposed so --selftest can
// compare it against the scalar reference.
void waveFieldAvx2(const WaveParams& params, float timeSec, int gridW,
                   int gridH, float* out);
#endif

}  // namespace xmbwave
