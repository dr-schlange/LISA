/*
  LISA (v0.0.1)

  Copyright (c) 2025 Dr Schlange
  Licensed under GNU GPLv3
*/
#pragma once
#include <pico/stdlib.h>
#include "buttons.h"
#include "constants_config.h"

enum DisplayMode { ENGINE_SELECT_MODE,
                   ENGINE_SETTINGS_CONFIG,
                   OSCILLOSCOPE_MODE };

#define SET_SYSTEM_READY(runtime) (runtime)->system_ready = true
#define SWITCHTO_ENGINE_SELECT_MODE(runtime) (runtime)->display_state = ENGINE_SELECT_MODE
#define SWITCHTO_ENGINE_SETTINGS_CONFIG(runtime) (runtime)->display_state = ENGINE_SETTINGS_CONFIG
#define SWITCHTO_OSCILLOSCOPE_MODE(runtime) (runtime)->display_state = OSCILLOSCOPE_MODE
#define IS_OSCILLOSCOPE_ON(runtime) (runtime)->oscilloscope_enabled
#define TOGGLE_OSCILLOSCOPE(runtime) ((runtime)->oscilloscope_enabled = !(runtime)->oscilloscope_enabled)
#define IS_OSCILLOSCOPE_OFF(runtime) (!(runtime)->oscilloscope_enabled)
#define IS_OSCILLOSCOPE_MODE(runtime) (runtime)->display_state == OSCILLOSCOPE_MODE
#define IS_ENGINE_SETTINGS_CONFIG(runtime) (runtime)->display_state == ENGINE_SETTINGS_CONFIG
#define IS_ENGINE_SELECT_MODE(runtime) (runtime)->display_state == ENGINE_SELECT_MODE

struct RuntimeState {
  uint8_t midi_ch;
  volatile int engine_idx;
  int last_engine_idx;
  volatile float timbre_in;
  volatile float color_in;
  volatile float fm_mod;
  volatile float timb_mod_midi;
  volatile float color_mod_midi;
  volatile float timb_mod_cv;
  volatile float color_mod_cv;
  volatile float fm_target;

  volatile float master_volume;
  volatile float env_attack_s;
  volatile float env_release_s;
  float attackCoef;
  float releaseCoef;
  bool sustain_enabled;

  bool engine_updated;
  volatile bool env_params_changed;
  unsigned long last_param_change;
  unsigned long last_midi_lock_time;

  volatile bool midi_mod;
  volatile bool cv_mod1;
  volatile bool cv_mod2;

  bool timbre_locked;
  bool color_locked;

  volatile bool filter_enabled;
  float filter_mix;
  volatile uint8_t filter_cutoff_cc;
  bool filter_midi_owned;
  volatile uint8_t filter_resonance_cc;

  bool show_saved_flag;
  unsigned long saved_start_time;

  DisplayMode display_state;
  volatile bool oscilloscope_enabled;

  Encoder encoder;
  float pot_timbre;
  float pot_color;

  volatile bool system_ready;
};

#define GlobalStateNew() \
  { \
    .midi_ch = 1, \
    .engine_idx = 1, \
    .last_engine_idx = -1, \
    .timbre_in = 0.4f, \
    .color_in = 0.3f, \
    .fm_mod = 0.0f, \
    .timb_mod_midi = 0.0f, \
    .color_mod_midi = 0.0f, \
    .timb_mod_cv = 0.0f, \
    .color_mod_cv = 0.0f, \
    .fm_target = 0.0f, \
    .master_volume = 0.7f, \
    .env_attack_s = 0.009f, \
    .env_release_s = 0.01f, \
    .attackCoef = 0.0f, \
    .releaseCoef = 0.0f, \
    .sustain_enabled = false, \
    .engine_updated = true, \
    .env_params_changed = true, \
    .last_param_change = 0, \
    .last_midi_lock_time = 0, \
    .midi_mod = true, \
    .cv_mod1 = false, \
    .cv_mod2 = false, \
    .timbre_locked = false, \
    .color_locked = false, \
    .filter_enabled = true, \
    .filter_mix = 1.0f, \
    .filter_cutoff_cc = 64, \
    .filter_midi_owned = false, \
    .filter_resonance_cc = 32, \
    .show_saved_flag = false, \
    .saved_start_time = 0, \
    .display_state = ENGINE_SELECT_MODE, \
    .oscilloscope_enabled = true, \
    .encoder = EncoderNew(ENCODER_CLK, ENCODER_DT, ENCODER_SW), \
    .pot_timbre = 0.5f, \
    .pot_color = 0.5f, \
    .system_ready = false, \
  };
