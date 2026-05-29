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

#define VOICE_ACTIVE 0b00000001
#define VOICE_LAST_TRIG 0b00000010
#define VOICE_SUSTAINED 0b00000100
#define VOICE_SECONDARY 0b00001000

#define set_active(flags) (flags |= VOICE_ACTIVE)
#define is_active(flags) (flags & VOICE_ACTIVE)
#define reset_active(flags) (flags &= ~VOICE_ACTIVE)

#define set_last_trig(flags) (flags |= VOICE_LAST_TRIG)
#define is_last_trig(flags) (flags & VOICE_LAST_TRIG)
#define reset_last_trig(flags) (flags &= ~VOICE_LAST_TRIG)

#define set_sustained(flags) (flags |= VOICE_SUSTAINED)
#define is_sustained(flags) (flags & VOICE_SUSTAINED)
#define reset_sustained(flags) (flags &= ~VOICE_SUSTAINED)

#define set_secondary(flags) (flags |= VOICE_SECONDARY)
#define is_secondary(flags) (flags & VOICE_SECONDARY)
#define reset_secondary(flags) (flags &= ~VOICE_SECONDARY)

struct Voice {
  WavetableStreamingOscillator osc;
  float pitch;
  float velocity;
  float vel_smoothed;
  float env;
  int16_t buffer[AUDIO_BLOCK];
  uint8_t sync_buffer[AUDIO_BLOCK];
  uint32_t age;
  uint8_t flags;
};

static uint32_t global_age = 0;

inline int find_free_voice(Voice *voices, float for_pitch) {
  int oldest = 0;
  uint32_t old_age = voices[0].age;
  for (int i = 0; i < MAX_VOICES; i++) {
    const Voice *voice = voices + i;
    if (!is_active(voice->flags) && voice->pitch == for_pitch) {
      return i;
    }
    if (!is_active(voice->flags) && voice->env == 0.f) {
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
    if (is_active(voices[i].flags) && voices[i].pitch == pitch)
      return i;
  return -1;
}

static inline void setup_voice(Voice *voice, float pitch, float velocity) {
  voice->pitch = pitch;
  voice->velocity = velocity;
  voice->env = 0.f;
  set_active(voice->flags);
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
      set_sustained(voices[i].flags);
      reset_active(voices[i].flags);
    } else {
      reset_active(voices[i].flags);
      reset_sustained(voices[i].flags);
    }
  }
}

inline Voice *allocate_voice_unison(Voice *voices, float pitch,
                                    float velocity) {
  Voice *primary = allocate_oldest_voice(voices, pitch, velocity);
  Voice *secondary = primary + 1;
  // setup_voice(secondary, pitch * 1.00289, velocity);
  setup_voice(secondary, pitch, velocity);
  set_secondary(secondary->flags);
  return primary;
}

inline void free_voice_unison(Voice *voices, float pitch, int sustain_enabled) {
  int i = find_voice_by_pitch(voices, pitch);
  if (i >= 0) {
    Voice *primary = voices + i;
    Voice *secondary = primary + 1;
    if (sustain_enabled) {
      set_sustained(primary->flags);
      set_sustained(secondary->flags);
      reset_active(primary->flags);
      reset_active(secondary->flags);
    } else {
      reset_sustained(primary->flags);
      reset_sustained(secondary->flags);
      reset_active(primary->flags);
      reset_active(secondary->flags);
    }
  }
}

inline void reset_all_voices(Voice *voices) {
  for (uint8_t i = 0; i < MAX_VOICES; i++) {
    reset_active(voices[i].flags);
    reset_sustained(voices[i].flags);
    reset_last_trig(voices[i].flags);
    voices[i].env = 0.f;
  }
}

inline void allocate_voice_mono(Voice *voices, float pitch, float velocity) {
  static Voice *last = voices + (MAX_VOICES - 1);
  static Voice *head = last;
  reset_sustained(head->flags);
  reset_active(head->flags);

  if (head == last) {
    head = voices; // circle to first voice
  } else {
    head++;
  }
  setup_voice(head, pitch, velocity);
}

inline void free_voice_mono(Voice *voices, float _pitch, bool sustained) {
  // we keep pitch to be kind of polymorphic, perhaps loading later the
  // functions in a table
  for (uint8_t i = 0; i < MAX_VOICES; i++) {
    if (is_active(voices[i].flags)) {
      if (sustained) {
        set_sustained(voices[i].flags);
        reset_active(voices[i].flags);
      } else {
        reset_active(voices[i].flags);
        reset_sustained(voices[i].flags);
      }
      break;
    }
  }
}
