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

class WavetableStreamingOscillator : public braids::MacroOscillator {
public:
  enum CaptureMode : uint8_t {
    CAPTURE_FORWARD = 0,         // fill 0 to 127
    CAPTURE_REVERSE,             // fill 127 to 0
    CAPTURE_ROLLING,             // always most recent 128 samples on buffer 0
    CAPTURE_REV_FORWARD,         // fill 127 to 0, going forward on buffers
    CAPTURE_FOR_BACKWARD,        // fill 0 to 127, going backward on buffers
    CAPTURE_ROLLING_BACKWARD,    // always most recent 128 samples on buffer 0 write in reverse
    CAPTURE_INDEPENDENT_BUFFER,  // independent buffers (4 streaming entries)
    CAPTURE_NUMBER,
  };
  inline void Init(float sr) {
    braids::MacroOscillator::Init(sr);
    srFactor_ = 96000.f / sr;
    phase_ = 0;
    pitch_ = 0;
    p1_ = 0;
    p2_ = 0;
  }

  inline void set_pitch(int16_t pitch) {
    pitch_ = pitch;
    braids::MacroOscillator::set_pitch(pitch);
  }

  inline void set_parameters(int16_t p1, int16_t p2) {
    p1_ = p1;
    p2_ = p2;
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

    // Snapshot waveforms once per block to avoid mid-block weird artifacts
    uint8_t wave_a[129], wave_b[129];
    uint16_t xfade;

    if (capture_mode_ == CAPTURE_ROLLING) {
      // Read ring buffer as it is so the waveform is stable between
      // blocks and morphs smoothly as new samples overwrite old ones in-place
      memcpy(wave_a, (const uint8_t*)buffers_[write_buf_], 129);
      memcpy(wave_b, wave_a, 129);
      xfade = 0;
    } else {
      uint32_t pos = (uint32_t)p1_ * 3;
      uint8_t wi = pos >> 15;       // 0, 1, or 2  (which buffer pair)
      xfade = (pos & 0x7fff) << 1;  // 0 to 65534 within each pair
      memcpy(wave_a, (const uint8_t*)buffers_[wi], 129);
      memcpy(wave_b, (const uint8_t*)buffers_[wi + 1], 129);
    }

    // Color (p2): one-pole lowpass
    // Floor at 256 to keep the cutoff above more or less 40Hz
    int32_t t = 32767 - p2_;
    int32_t lp_coeff = t * t >> 15;      // 32766 to 0, squared
    if (lp_coeff < 256) lp_coeff = 256;  // floor around 40Hz at 32kHz

    uint32_t phase_increment = ComputePhaseIncrement(pitch_) >> 1;
    while (size--) {
      int16_t sample;
      phase_ += phase_increment;
      if (*sync++) phase_ = 0;
      sample = Crossfade(wave_a, wave_b, (phase_ + phase_offset_) >> 1, xfade) >> 1;
      phase_ += phase_increment;
      sample += Crossfade(wave_a, wave_b, (phase_ + phase_offset_) >> 1, xfade) >> 1;
      lpState_ += ((int32_t)sample - lpState_) * lp_coeff >> 15;
      *buffer++ = (int16_t)lpState_;
    }
  }

  // Used for UI/display, called from Core 0 only
  inline static void CopyBuffers(uint8_t dst[4][129]) {
    for (int b = 0; b < 4; b++)
      memcpy(dst[b], (const uint8_t*)buffers_[b], 129);
  }
  inline static uint8_t GetWriteBuf() {
    return write_buf_;
  }
  inline static uint8_t GetWritePos() {
    return write_pos_[0];
  }

  // Called from the MIDI handler in Core 0 only
  inline static void PushSample(uint8_t value) {
    if (freeze_) return;

    uint8_t pos = write_pos_[0];
    uint8_t buf = write_buf_;
    buffers_[buf][pos] = value;

    if (capture_mode_ == CAPTURE_ROLLING || capture_mode_ == CAPTURE_ROLLING_BACKWARD) {
      uint8_t direction = capture_mode_ == CAPTURE_ROLLING_BACKWARD ? -1 : 1;
      write_pos_[0] = (pos + direction) & 0x7f;  // ring stays in buffer 0, wraps 0-127
      buffers_[0][128] = buffers_[0][0];
      return;
    }

    if (capture_mode_ == CAPTURE_REVERSE || capture_mode_ == CAPTURE_REV_FORWARD) {
      if (pos == 0) {
        buffers_[buf][128] = buffers_[buf][0];
        uint8_t next_buffer = capture_mode_ == CAPTURE_REV_FORWARD ? 1 : -1;
        write_buf_ = (buf + next_buffer) & 0x3;
        write_pos_[0] = 127;
      } else {
        write_pos_[0] = pos - 1;
      }
      return;
    }

    // CAPTURE_FORWARD / CAPTURE_FOR_BACKWARD
    if (pos == 0) {
      uint8_t prev = capture_mode_ == CAPTURE_FOR_BACKWARD
                       ? (buf + 1) & 0x3
                       : (buf - 1) & 0x3;
      buffers_[prev][128] = value;
    }
    if (++pos >= 128) {
      pos = 0;
      buffers_[buf][128] = buffers_[buf][0];  // self-wrap until next buf's first sample arrives
      uint8_t next_buffer = capture_mode_ == CAPTURE_FOR_BACKWARD ? -1 : 1;
      write_buf_ = (buf + next_buffer) & 0x3;
    }
    write_pos_[0] = pos;
  }

  inline static void PushSampleInBuffer(uint8_t value, uint8_t buf) {
    if (freeze_) return;

    uint8_t pos = write_pos_[buf];
    buffers_[buf][pos] = value;

    if (++pos >= 128) {
      pos = 0;
      buffers_[buf][128] = buffers_[buf][0]; // wraps
    }
    write_pos_[buf] = pos;
  }

  // Other public API for the live wavetable mode
  inline static void setRetrigger(boolean retrigger) {
    retrigger_ = retrigger;
  }
  inline static void freezeBuffers(boolean freeze) {
    freeze_ = freeze;
  }
  inline static void setPhaseOffset(int32_t offset) {
    phase_offset_ = offset;
  }
  inline static void set_live_mode(bool on) {
    live_ = on;
  }
  inline static void setCaptureMode(CaptureMode mode) {
    capture_mode_ = mode;
    write_buf_ = 0;
    write_pos_[0] = (mode == CAPTURE_REVERSE || mode == CAPTURE_REV_FORWARD || mode == CAPTURE_ROLLING_BACKWARD) ? 127 : 0;
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
  uint32_t phase_ = 0;
  int32_t lpState_ = 0;
  float srFactor_ = 1.f;

  // Shared across Cores
  static volatile bool live_;
  static volatile CaptureMode capture_mode_;
  static volatile uint8_t buffers_[4][129];
  static volatile uint8_t write_buf_;
  static volatile uint8_t write_pos_[4];
  static volatile bool retrigger_;
  static volatile bool freeze_;
  static volatile int32_t phase_offset_;
};


volatile uint8_t WavetableStreamingOscillator::buffers_[4][129] = {};
volatile uint8_t WavetableStreamingOscillator::write_buf_ = 0;
volatile uint8_t WavetableStreamingOscillator::write_pos_[4] = {};
volatile bool WavetableStreamingOscillator::retrigger_ = false;
volatile bool WavetableStreamingOscillator::freeze_ = false;
volatile int32_t WavetableStreamingOscillator::phase_offset_ = 0;
volatile bool WavetableStreamingOscillator::live_ = false;
volatile WavetableStreamingOscillator::CaptureMode WavetableStreamingOscillator::capture_mode_ = WavetableStreamingOscillator::CAPTURE_FORWARD;
