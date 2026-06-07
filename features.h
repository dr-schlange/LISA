/*
  LISA (v0.2.0)

  Copyright (c) 2026 Dr Schlange
  Licensed under GNU GPLv3
*/
#pragma once
#include <stdint.h>

static volatile uint8_t feat_peak_ = 0;

// Called from core1 with the mix buffer
inline void features_compute_peak(int32_t *mix, size_t size) {
  static int32_t peak_hold = 0;

  int32_t peak = 0;
  for (size_t i = 0; i < size; ++i) {
    int32_t v = mix[i] < 0 ? -mix[i] : mix[i];
    if (v > peak)
      peak = v;
  }

  if (peak > peak_hold) {
    peak_hold = peak;
  } else {
    peak_hold -= peak_hold >> 7; // slow exponential decay
  }

  uint8_t scaled = (uint8_t)(peak_hold >> 8);
  if (scaled > 127)
    scaled = 127;
  feat_peak_ = scaled;
}

// Called from core0 (MIDI loop)
inline void features_send(uint8_t midi_channel) {
  static unsigned long last_send = 0;
  unsigned long now = millis();
  // max 20Hz to avoid flood
  if (now - last_send < 50) {
    return;
  }
  last_send = now;
  send_midi_cc(MIDI_FEAT_PEAK, feat_peak_, midi_channel);
}
