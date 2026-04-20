/*
  LISA (v0.0.1)

  Copyright (c) 2026 Dr Schlange
  Licensed under GNU GPLv3
*/
#pragma once

#include <cstring>
#include "braids/macro_oscillator.h"
#include "braids/resources.h"
#include "stmlib/utils/dsp.h"

using namespace stmlib;

class LiveWavetable {
public:
  LiveWavetable() {
    write_pos_ = 0;
    freeze_ = false;
    double_buffer_ = false;
    read_idx_ = 0;
    write_idx_ = 1;
    reset();
  }

  inline void freeze(boolean freeze) {
    freeze_ = freeze;
  }

  inline void resetWriteIndex() {
    write_pos_ = 0;
  }

  inline void setDoubleBuffer(bool on) {
    double_buffer_ = on;
  }

  inline void pushSample(int16_t value) {
    if (freeze_) return;
    uint16_t pos = write_pos_;
    if (double_buffer_) {
      buffers_[write_idx_][pos] = value;
      if (pos == 0) buffers_[write_idx_][256] = value;
      if (++pos >= 256) {
        pos = 0;
        read_idx_ = write_idx_;
        write_idx_ = 1 - write_idx_;
      }
    } else {
      buffers_[read_idx_][pos] = value;
      if (pos == 0) buffers_[read_idx_][256] = value;
      if (++pos >= 256) pos = 0;
    }
    write_pos_ = pos;
  }

  inline void reset() {
    for (uint8_t b = 0; b < 2; ++b) {
      for (uint16_t i = 0; i < 257; ++i) {
        buffers_[b][i] = 0;
      }
    }
  }

  inline void copyTable(int16_t dst[257]) {
    memcpy(dst, (const int16_t*)buffers_[read_idx_], 257 * sizeof(int16_t));
  }

private:
  volatile uint16_t write_pos_;
  volatile boolean freeze_;
  volatile bool double_buffer_;
  volatile int16_t buffers_[2][257];
  volatile uint8_t read_idx_;
  volatile uint8_t write_idx_;
};


class WavetableStreamingOscillator : public braids::MacroOscillator {
public:
  enum CaptureMode : uint8_t {
    CAPTURE_INDEPENDENT_BUFFER = 0,  // independent buffers (4 streaming entries)
    CAPTURE_FORWARD,                 // fill 0 to 127
    CAPTURE_REVERSE,                 // fill 127 to 0
    CAPTURE_ROLLING,                 // always most recent 128 samples on buffer 0
    CAPTURE_REV_FORWARD,             // fill 127 to 0, going forward on buffers
    CAPTURE_FOR_BACKWARD,            // fill 0 to 127, going backward on buffers
    CAPTURE_ROLLING_BACKWARD,        // always most recent 128 samples on buffer 0 write in reverse
    CAPTURE_NUMBER,
  };
  inline void Init(float sr) {
    braids::MacroOscillator::Init(sr);
    srFactor_ = 96000.f / sr;
    phase_ = 0;
    pitch_ = 0;
    p1_ = 0;
    p2_ = 0;
    w1_ = 0;
    w2_ = 0;
    w3_ = 0;
    w4_ = 0;
  }

  inline void set_pitch(int16_t pitch) {
    pitch_ = pitch;
    braids::MacroOscillator::set_pitch(pitch);
  }

  inline void set_parameters(int16_t p1, int16_t p2) {
    p1_ = p1;
    p2_ = p2;

    int32_t t = p1;  // already converted to fp
    int32_t m = p2;

    // pull towards the middle
    // strength: >>1 = mild, >>2 = stronger
    t = 16384 + ((t - 16384) >> 1);
    m = 16384 + ((m - 16384) >> 1);

    // nonlinear curves the corners
    t = (t * t) >> 15;
    m = (m * m) >> 15;

    // bilinear mapping
    int32_t inv_t = 32767 - t;
    int32_t inv_m = 32767 - m;

    w1_ = (inv_t * inv_m) >> 15;
    w2_ = (t * inv_m) >> 15;
    w3_ = (inv_t * m) >> 15;
    w4_ = (t * m) >> 15;

    braids::MacroOscillator::set_parameters(p1, p2);
  }

  inline void reset_phase() {
    phase_ = 0;
  }

  inline void Strike() {
    if (retrigger_) {
      phase_ = 0;
    }
    braids::MacroOscillator::Strike();
  }

  inline void Render(const uint8_t* sync, int16_t* buffer, size_t size) {
    if (!live_) {
      braids::MacroOscillator::Render(sync, buffer, size);
      return;
    }

    if (capture_mode_ == CAPTURE_INDEPENDENT_BUFFER) {
      RenderMixing(sync, buffer, size);
      return;
    }
    // TODO: rewrite the other modes
  }

  inline int16_t ReadMixedSample(int16_t waves[4][257], uint32_t phase) {
    int32_t mix =
      Interpolate824(waves[0], phase) * w1_ + Interpolate824(waves[1], phase) * w2_ + Interpolate824(waves[2], phase) * w3_ + Interpolate824(waves[3], phase) * w4_;

    return mix >> 15;  // go back to normal world
  }

  inline void RenderMixing(const uint8_t* sync, int16_t* output, size_t size) {
    int16_t waves[4][257];

    for (uint8_t i = 0; i < 4; ++i) {
      tables_[i].copyTable(waves[i]);
    }

    uint32_t phase_increment = ComputePhaseIncrement(pitch_);
    while (size--) {
      phase_ += phase_increment;
      if (*sync++) phase_ = 0;
      *output++ = ReadMixedSample(waves, phase_ + phase_offset_);
    }
  }

  // Used for UI/display, called from Core 0 only
  inline static void CopyBuffers(int16_t dst[4][257]) {
    for (uint8_t i = 0; i < 4; ++i) {
      tables_[i].copyTable(dst[i]);
    }
  }

  inline static void PushSampleInBuffer(uint8_t idx, int16_t value) {
    tables_[idx].pushSample(value);
  }
  inline static void setDoubleBuffer(bool on) {
    for (uint8_t i = 0; i < 4; ++i) tables_[i].setDoubleBuffer(on);
  }

  // ===
  // Other public API for the live wavetable mode
  // ===

  inline static void setRetrigger(boolean retrigger) {
    retrigger_ = retrigger;
  }
  inline static void resetWriteIndex(boolean reset) {
    for (uint8_t i = 0; i < 4; ++i) {
      if (reset) {
        tables_[i].resetWriteIndex();
      }
    }
  }
  inline static void resetAllWavetables(boolean reset) {
    for (uint8_t i = 0; i < 4; ++i) {
      if (reset) {
        tables_[i].reset();
      }
    }
  }
  inline static void freezeAllBuffers(boolean freeze) {
    for (uint8_t i = 0; i < 4; ++i) {
      freezeBuffer(i, freeze);
    }
  }
  inline static void freezeBuffer(uint8_t idx, boolean freeze) {
    tables_[idx].freeze(freeze);
  }
  inline static void setPhaseOffset(int32_t offset) {
    phase_offset_ = offset;
  }
  inline static void setLiveMode(bool on) {
    live_ = on;
  }
  inline static boolean isLiveMode() {
    return live_;
  }
  inline static CaptureMode getCaptureMode() {
    return capture_mode_;
  }
private:
  static const uint16_t kPitchTableStart = 128 * 128;
  static const uint16_t kOctave = 12 * 128;

  uint32_t ComputePhaseIncrement(int16_t pitch) {
    int32_t ref = pitch - kPitchTableStart;
    size_t shifts = 0;
    while (ref < 0) {
      ref += kOctave;
      ++shifts;
    }
    uint32_t a = braids::lut_oscillator_increments[ref >> 4];
    uint32_t b = braids::lut_oscillator_increments[(ref >> 4) + 1];
    uint32_t inc = a + (static_cast<int32_t>(b - a) * (ref & 0xf) >> 4);
    return static_cast<uint32_t>(inc * srFactor_ + 0.5f) >> shifts;
  }

  int16_t pitch_ = 0;
  int16_t p1_ = 0;
  int16_t p2_ = 0;
  int16_t w1_ = 0;
  int16_t w2_ = 0;
  int16_t w3_ = 0;
  int16_t w4_ = 0;

  uint32_t phase_ = 0;
  int32_t lpState_ = 0;
  float srFactor_ = 1.f;

  // Shared across Cores
  static volatile bool live_;
  static volatile CaptureMode capture_mode_;
  static volatile uint8_t write_buf_;
  static volatile bool retrigger_;
  static LiveWavetable tables_[4];
  static volatile int32_t phase_offset_;
};


volatile uint8_t WavetableStreamingOscillator::write_buf_ = 0;
volatile bool WavetableStreamingOscillator::retrigger_ = false;
volatile int32_t WavetableStreamingOscillator::phase_offset_ = 0;
volatile bool WavetableStreamingOscillator::live_ = false;
volatile WavetableStreamingOscillator::CaptureMode WavetableStreamingOscillator::capture_mode_ = WavetableStreamingOscillator::CAPTURE_INDEPENDENT_BUFFER;
LiveWavetable WavetableStreamingOscillator::tables_[4] = {};