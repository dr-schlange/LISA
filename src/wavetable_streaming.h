/*
  LISA (v0.3.0)

  Copyright (c) 2026 Dr Schlange
  Licensed under GNU GPLv3
*/
#pragma once
// clang-format off
#include <cstring>
#include "braids/macro_oscillator.h"
#include "braids/resources.h"
#include "stmlib/utils/dsp.h"
// clang-format on

using namespace stmlib;

#define FIELD_FREEZE 0b1
#define FIELD_DBUFF 0b10
#define FIELD_MODE 0b1100
#define MODE_CIRCULAR 0b00
#define MODE_SCROLL 0b01
#define MODE_MANUAL 0b10
#define MODE_EXTRA 0b11
#define FIELD_WT_SNAPSHOTS 0b10000
#define MAX_SNAPSHOTS 16
// FLAGS
//       +-------- wavetable snapshots active
//       | +------ writing mode
//       | |  +--- dbuffering active
//       | |  | +- freeze active
//       | +- | |
// 0b000 0 00 0 0
#define FREEZE_ACTIVE(flags) (flags & 0b1)
#define DBUFF_ACTIVE(flags) (flags & 0b10)
#define CIRCULAR_ACTIVE(flags) (((flags & 0b1100) >> 2) == 0)
#define SCROLL_ACTIVE(flags) (((flags & 0b1100) >> 2) == 1)
#define MANUAL_IDX_ACTIVE(flags) (((flags & 0b1100) >> 2) == 2)
#define EXTRA_ACTIVE(flags) (((flags & 0b1100) >> 2) == 3)
#define WTABLES_SNAPSHOT_ACTIVE(flags) (flags & 0b10000)
#define LAP_TO_COMPLETE 2
#define BLEND_K_MIN 10
#define BLEND_K_MAX 1500

class LiveWavetable {
public:
  LiveWavetable() { reset(); }

  inline void freeze(bool f) {
    if (f) {
      flags_ |= FIELD_FREEZE;
    } else {
      flags_ &= ~FIELD_FREEZE;
    }
  }

  inline void resetWriteIndex() { write_pos_ = 0; }

  inline void setDoubleBuffer(bool on) {
    if (on) {
      flags_ |= FIELD_DBUFF;
    } else {
      flags_ &= ~FIELD_DBUFF;
    }
  }

  inline void setMode(uint8_t mode) {
    flags_ = (flags_ & ~FIELD_MODE) | (mode << 2);
  }

  inline void setSnapshotMode(bool on) {
    if (on) {
      flags_ |= FIELD_WT_SNAPSHOTS;
    } else {
      flags_ &= ~FIELD_WT_SNAPSHOTS;
    }
  }

  inline void setSnapshotDepth(uint8_t depth) {
    snapshots_depth_ = constrain(depth, 2, MAX_SNAPSHOTS);
  }

  inline uint8_t getSnapshotDepth() const { return snapshots_depth_; }

  inline bool snapshotModeActive() const {
    return WTABLES_SNAPSHOT_ACTIVE(flags_);
  }

  inline void setWritePos(uint16_t pos) { write_pos_ = pos; }

  inline uint8_t getReadFrame() const { return laps_read_ % snapshots_depth_; }

  inline uint8_t getWriteFrame() const { return write_idx_; }

  inline uint8_t getMode() const { return (flags_ & FIELD_MODE) >> 2; }

  inline void setBlendDirection(int8_t direction) {
    blend_direction_ = direction;
  }

  inline int8_t getBlendDirection() { return blend_direction_; }

  inline void pushSample(int16_t value) {
    if (FREEZE_ACTIVE(flags_)) {
      return;
    }
    uint8_t widx;
    if (WTABLES_SNAPSHOT_ACTIVE(flags_) && CIRCULAR_ACTIVE(flags_)) {
      widx = write_idx_;
    } else {
      widx = read_idx_;
    }

    if (MANUAL_IDX_ACTIVE(flags_)) {
      // manual
      buffers_[widx][write_pos_] = value;
      buffers_[widx][write_pos_ + 1] = value;
      if (write_pos_ == 0) {
        buffers_[widx][256] = value;
      }
    } else if (EXTRA_ACTIVE(flags_)) {
      // manual interpolated
      const int16_t span = abs((int16_t)write_pos_ - (int16_t)last_write_idx_);
      if (span == 0) {
        buffers_[widx][write_pos_] = value;
      } else {
        const int32_t diff = (value - last_write_value_);
        uint8_t t = last_write_idx_;
        const int8_t direction = write_pos_ > last_write_idx_ ? 1 : -1;
        while (t != write_pos_) {
          buffers_[widx][t] =
              last_write_value_ +
              diff * abs((int16_t)t - (int16_t)last_write_idx_) / span;
          if (t == 0) {
            buffers_[widx][256] = buffers_[widx][t];
          }

          t += direction;
        }
        buffers_[widx][write_pos_] = value;
        if (write_pos_ == 0) {
          buffers_[widx][256] = value;
        }
      }
      last_write_idx_ = write_pos_;
      last_write_value_ = value;
    } else if (SCROLL_ACTIVE(flags_)) {
      // scroll mode
      memmove((int16_t *)&buffers_[widx][0],
              (const int16_t *)&buffers_[widx][1], 255 * sizeof(int16_t));
      buffers_[widx][255] = value;
      buffers_[widx][256] = buffers_[widx][0];
    } else {
      uint16_t pos = write_pos_;
      if (DBUFF_ACTIVE(flags_)) {
        // DBuffering
        buffers_[write_idx_][pos] = value;
        if (pos == 0) {
          buffers_[write_idx_][256] = value;
        }
        if (++pos >= 256) {
          pos = 0;
          read_idx_ = write_idx_;
          write_idx_ = 1 - write_idx_;
        }
      } else {
        // normal mode
        buffers_[widx][pos] = value;
        if (pos == 0) {
          buffers_[widx][256] = value;
        }
        if (++pos >= 256) {
          pos = 0;
          write_idx_ = (write_idx_ + 1) % snapshots_depth_;
          ++laps_written_;
        }
      }
      write_pos_ = pos;
    }
    if (WTABLES_SNAPSHOT_ACTIVE(flags_) && CIRCULAR_ACTIVE(flags_)) {
      refreshRenderBuffer();
    }
  }

  inline void reset() {
    for (uint8_t b = 0; b < MAX_SNAPSHOTS; ++b) {
      for (uint16_t i = 0; i < 257; ++i) {
        buffers_[b][i] = 0;
      }
    }
    for (uint16_t i = 0; i < 257; ++i) {
      render_buf_[i] = 0;
    }
    slew_ = 65535; // settled, nothing to fade from
    laps_written_ = 0;
    laps_read_ = 0;
    external_control_ = false;
  }

  inline void setLevel(uint8_t level) { level_ = level; }

  inline uint8_t getLevel() { return level_; }

  inline void copyTable(int16_t dst[257]) {
    memcpy(dst, (const int16_t *)renderPointer(), 257 * sizeof(int16_t));
  }

  inline const int16_t *renderPointer() const {
    if (!WTABLES_SNAPSHOT_ACTIVE(flags_) || !CIRCULAR_ACTIVE(flags_)) {
      return (const int16_t *)buffers_[read_idx_];
    }
    return render_buf_;
  }

public:
  inline void setBlendPosition(uint16_t pos16) {
    if (!external_control_) {
      return;
    }
    if (laps_written_ < LAP_TO_COMPLETE) {
      return;
    }
    const uint32_t span = laps_written_ - 1;  // segments between captured laps
    uint32_t scaled = (uint32_t)pos16 * span; // 0 .. 65535*span
    uint32_t seg = scaled >> 16;
    uint16_t frac;
    if (seg >= span) {
      seg = span - 1;
      frac = 65535;
    } else {
      frac = (uint16_t)(scaled & 0xFFFF);
    }
    laps_read_ = LAP_TO_COMPLETE + seg;
    slew_ = frac;
    computeRenderBuffer();
  }

  inline void setAutoBlend(bool on) { external_control_ = !on; }

  static inline void setBlendK(uint16_t value) {
    kStepsPerFade = constrain(value, BLEND_K_MIN, BLEND_K_MAX);
    kLinearStep = 65536 / kStepsPerFade;
  }

  static inline uint16_t getBlendK() { return kStepsPerFade; }

private:
  inline void computeRenderBuffer() {
    const uint8_t curIdx = (uint8_t)(laps_read_ % snapshots_depth_);
    const uint8_t prevIdx =
        (uint8_t)((laps_read_ + blend_direction_) % snapshots_depth_);
    for (uint16_t i = 0; i < 257; ++i) {
      const int32_t prevv = buffers_[prevIdx][i];
      const int32_t curv = buffers_[curIdx][i];
      render_buf_[i] =
          (int16_t)(prevv + (((curv - prevv) * (int32_t)slew_) >> 16));
    }
  }

  inline void refreshRenderBuffer() {
    if (external_control_) {
      if (laps_written_ >= LAP_TO_COMPLETE && laps_read_ >= LAP_TO_COMPLETE) {
        computeRenderBuffer();
      }
      return;
    }
    if (laps_written_ < LAP_TO_COMPLETE) {
      return;
    }
    if (laps_read_ < LAP_TO_COMPLETE) {
      laps_read_ = LAP_TO_COMPLETE;
      slew_ = 0;
    }

    int32_t s = (int32_t)slew_ + kLinearStep;
    if (s >= 65535) {
      if (laps_read_ < laps_written_) {
        ++laps_read_;
        s = 0;
      } else {
        s = 65535;
      }
    }
    slew_ = (uint16_t)s;
    computeRenderBuffer();
  }

  inline static volatile int32_t kStepsPerFade = 300;
  inline static volatile int32_t kLinearStep = 65536 / kStepsPerFade;
  volatile uint16_t write_pos_ = 0;
  volatile uint8_t flags_ = 0b00000000;
  volatile uint8_t level_ = 255; // fp8
  volatile int16_t buffers_[MAX_SNAPSHOTS][257];
  int16_t render_buf_[257];
  uint16_t slew_ = 65535; // 0 = fully laps_read_-1, 65535 = fully laps_read_
  bool external_control_ = false;
  volatile uint8_t read_idx_ = 0;
  volatile uint8_t write_idx_ = 1;
  volatile uint8_t snapshots_depth_ = 8;
  uint32_t laps_written_ = 0; // total completed capture laps (monotonic)
  uint32_t laps_read_ = 0;    // lap the crossfade is currently fading into
  uint8_t last_write_idx_ = 0;
  int16_t last_write_value_ = 0;
  volatile int8_t blend_direction_ = -1;
};

// volatile int32_t LiveWavetable::kStepsPerFade = 300;

class WavetableStreamingOscillator : public braids::MacroOscillator {
public:
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

    int32_t t = p1; // already converted to fp
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

  inline void reset_phase() { phase_ = 0; }

  inline void Strike() {
    if (retrigger_) {
      phase_ = 0;
    }
    braids::MacroOscillator::Strike();
  }

  inline void Render(const uint8_t *sync, int16_t *buffer, size_t size) {
    if (!live_) {
      braids::MacroOscillator::Render(sync, buffer, size);
      return;
    }

    RenderMixing(sync, buffer, size);
  }

  inline int16_t ReadMixedSample(const int16_t *const waves[4], uint32_t phase,
                                 int32_t lw1, int32_t lw2, int32_t lw3,
                                 int32_t lw4) {
    // a + ((b - a) * static_cast<int32_t>(balance) >> 16);

    int32_t mix = Interpolate824(waves[0], phase) * lw1 +
                  Interpolate824(waves[1], phase) * lw2 +
                  Interpolate824(waves[2], phase) * lw3 +
                  Interpolate824(waves[3], phase) * lw4;
    return mix >> 15;
  }

  inline void RenderMixing(const uint8_t *sync, int16_t *output, size_t size) {
    const int16_t *waves[4];
    for (uint8_t i = 0; i < 4; ++i) {
      waves[i] = tables_[i].renderPointer();
    }

    // level is fp8
    int32_t lw1 = (w1_ * (int32_t)tables_[0].getLevel()) >> 8;
    int32_t lw2 = (w2_ * (int32_t)tables_[1].getLevel()) >> 8;
    int32_t lw3 = (w3_ * (int32_t)tables_[2].getLevel()) >> 8;
    int32_t lw4 = (w4_ * (int32_t)tables_[3].getLevel()) >> 8;

    uint32_t phase_increment = ComputePhaseIncrement(pitch_);
    while (size--) {
      phase_ += phase_increment;
      if (*sync++) {
        phase_ = 0;
      }
      *output++ =
          ReadMixedSample(waves, phase_ + phase_offset_, lw1, lw2, lw3, lw4);
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
    for (uint8_t i = 0; i < 4; ++i) {
      tables_[i].setDoubleBuffer(on);
    }
  }

  // ===
  // Other public API for the live wavetable mode
  // ===

  inline static void setRetrigger(bool retrigger) { retrigger_ = retrigger; }
  inline static void resetWriteIndex(bool reset) {
    for (uint8_t i = 0; i < 4; ++i) {
      if (reset) {
        tables_[i].resetWriteIndex();
      }
    }
  }
  inline static void resetAllWavetables(bool reset) {
    for (uint8_t i = 0; i < 4; ++i) {
      if (reset) {
        tables_[i].reset();
      }
    }
  }
  inline static void freezeAllBuffers(bool freeze) {
    for (uint8_t i = 0; i < 4; ++i) {
      freezeBuffer(i, freeze);
    }
  }
  inline static void freezeBuffer(uint8_t idx, bool freeze) {
    tables_[idx].freeze(freeze);
  }
  inline static void setBufferLevel(uint8_t idx, uint8_t level) {
    tables_[idx].setLevel(level);
  }
  inline static uint8_t getBufferLevel(uint8_t idx) {
    return tables_[idx].getLevel();
  }
  inline static void setMode(uint8_t idx, uint8_t mode) {
    tables_[idx].setMode(mode);
  }
  inline static uint8_t getTableMode(uint8_t idx) {
    return tables_[idx].getMode();
  }
  inline static void setWriteIndex(uint8_t idx, uint16_t pos) {
    tables_[idx].setWritePos(pos);
  }
  inline static void setSnapshotMode(uint8_t idx, bool on) {
    tables_[idx].setSnapshotMode(on);
  }
  inline static void setSnapshotBlendPosition(uint8_t idx, uint16_t pos16) {
    tables_[idx].setBlendPosition(pos16);
  }
  inline static void setAllSnapshotBlendPosition(uint16_t pos16) {
    for (uint8_t i = 0; i < 4; ++i) {
      tables_[i].setBlendPosition(pos16);
    }
  }
  inline static void setSnapshotAutoBlend(uint8_t idx, bool on) {
    tables_[idx].setAutoBlend(on);
  }
  inline static void setAllSnapshotAutoBlend(bool on) {
    for (uint8_t i = 0; i < 4; ++i) {
      tables_[i].setAutoBlend(on);
    }
  }
  inline static void setBlendK(uint16_t level) {
    LiveWavetable::setBlendK(level);
  }
  inline static void setAllSnapshotDepth(uint8_t depth) {
    for (uint8_t i = 0; i < 4; ++i) {
      tables_[i].setSnapshotDepth(depth);
    }
  }
  inline static void setSnapshotMode(bool activate) {
    for (uint8_t i = 0; i < 4; ++i) {
      tables_[i].setSnapshotMode(activate);
    }
  }
  inline static void setBlendDirection(int8_t direction) {
    for (uint8_t i = 0; i < 4; ++i) {
      tables_[i].setBlendDirection(direction);
    }
  }
  inline static int8_t getBlendDirection(uint8_t idx) {
    return tables_[idx].getBlendDirection();
  }
  inline static int16_t getBlendK() { return LiveWavetable::getBlendK(); }
  inline static uint8_t getSnapshotDepth(uint8_t idx) {
    return tables_[idx].getSnapshotDepth();
  }
  inline static bool snapshotModeActive(uint8_t idx) {
    return tables_[idx].snapshotModeActive();
  }
  inline static uint8_t getWriteFrame(uint8_t idx) {
    return tables_[idx].getWriteFrame();
  }
  inline static uint8_t getReadFrame(uint8_t idx) {
    return tables_[idx].getReadFrame();
  }
  inline static void setPhaseOffset(int32_t offset) { phase_offset_ = offset; }
  inline static void setLiveMode(bool on) { live_ = on; }
  inline static bool isLiveMode() { return live_; }

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
  inline static volatile bool live_ = false;
  // static volatile CaptureMode capture_mode_;
  inline static volatile uint8_t write_buf_ = 0;
  inline static volatile bool retrigger_ = false;
  inline static LiveWavetable tables_[4] = {};
  inline static volatile int32_t phase_offset_ = 0;
};

// volatile uint8_t WavetableStreamingOscillator::write_buf_ = 0;
// volatile bool WavetableStreamingOscillator::retrigger_ = false;
// volatile int32_t WavetableStreamingOscillator::phase_offset_ = 0;
// volatile bool WavetableStreamingOscillator::live_ = false;
// LiveWavetable WavetableStreamingOscillator::tables_[4] = {};
