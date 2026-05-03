/*
  LISA (v0.0.1)

  Copyright (c) 2026 Dr Schlange
  Licensed under GNU GPLv3

  Based on VIJA by Vadims Maksimovs (ledlaux.github.com)
*/
#pragma once
// clang-format off
#include <pico/stdlib.h>
#include <BRAIDS.h>
#include "constants_config.h"
#include "wavetable_streaming.h"
// clang-format on

struct Voice {
  WavetableStreamingOscillator osc;
  float pitch;
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

static uint32_t global_age = 0;

inline int find_free_voice(Voice *voices, float for_pitch) {
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

inline int find_voice_by_pitch(Voice *voices, float pitch) {
  for (int i = 0; i < MAX_VOICES; i++)
    if (voices[i].active && voices[i].pitch == pitch)
      return i;
  return -1;
}

static inline void setup_voice(Voice *voice, float pitch, float velocity) {
  voice->pitch = pitch;
  voice->velocity = velocity;
  voice->env = 0.f;
  voice->active = true;
  voice->age = global_age++;
}

inline Voice *allocate_oldest_voice(Voice *voices, float pitch,
                                    float velocity) {
  int i = find_free_voice(voices, pitch);
  Voice *voice = voices + i;
  setup_voice(voice, pitch, velocity);
  return voice;
}

inline void free_voice_by_pitch(Voice *voices, float pitch,
                                int sustain_enabled) {
  int i = find_voice_by_pitch(voices, pitch);
  if (i >= 0) {
    if (sustain_enabled) {
      voices[i].sustained = true;
      voices[i].active = false;
    } else {
      voices[i].active = false;
      voices[i].sustained = false;
    }
  }
}

inline Voice *allocate_voice_unison(Voice *voices, float pitch,
                                    float velocity) {
  Voice *primary = allocate_oldest_voice(voices, pitch, velocity);
  Voice *secondary = primary + 1;
  setup_voice(secondary, pitch * 1.00289, velocity);
  return primary;
}

inline void free_voice_unison(Voice *voices, float pitch, int sustain_enabled) {
  int i = find_voice_by_pitch(voices, pitch);
  if (i >= 0) {
    Voice *primary = voices + i;
    Voice *secondary = primary + 1;
    if (sustain_enabled) {
      primary->sustained = true;
      secondary->sustained = true;
      primary->active = false;
      secondary->active = false;
    } else {
      primary->sustained = false;
      secondary->sustained = false;
      primary->active = false;
      secondary->active = false;
    }
  }
}

inline void reset_all_voices(Voice *voices) {
  for (uint8_t i = 0; i < MAX_VOICES; i++) {
    voices[i].active = false;
    voices[i].sustained = false;
    voices[i].last_trig = false;
    voices[i].env = 0.f;
  }
}
