/*
  LISA (v0.0.1)

  Copyright (c) 2026 Dr Schlange
  Licensed under GNU GPLv3

  Based on VIJA by Vadims Maksimovs (ledlaux.github.com)
*/
#pragma once
#include <pico/stdlib.h>
#include <BRAIDS.h>
#include "constants_config.h"
#include "wavetable_streaming.h"

struct Voice {
  WavetableStreamingOscillator osc;
  int pitch;
  float velocity;
  float vel_smoothed;
  bool active;
  bool last_trig;
  float env;
  int16_t buffer[AUDIO_BLOCK];
  uint8_t sync_buffer[AUDIO_BLOCK];
  uint32_t age;
  bool sustained;
};



int find_free_voice(Voice *voices, int for_pitch) {
  int oldest = 0;
  uint32_t old_age = voices[0].age;
  for (int i = 0; i < MAX_VOICES; i++) {
    const Voice *voice = voices + i;
    if (!voice->active && voices[i].pitch == for_pitch) {
      return i;
    }
    if (!voice->active && voice->env == 0.f) {
      return i;
    }
    if (voice->age < old_age) {
      old_age = voice->age;
      oldest = i;
    }
  }
  return oldest;
}


int find_voice_by_pitch(Voice *voices, int pitch) {
  for (int i = 0; i < MAX_VOICES; i++)
    if (voices[i].active && voices[i].pitch == pitch) return i;
  return -1;
}
