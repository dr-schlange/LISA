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


enum MidiControllerMode { CONTROLLER_MODE_OFF,
                          CONTROLLER_BOTH,
                          CONTROLLER_ONLY
};

#define REFRESH_IS_SCHEDULED(runtime) (runtime)->engine_updated
#define SCHEDULE_REFRESH(runtime) (runtime)->engine_updated = true
#define CLEAR_REFRESH(runtime) (runtime)->engine_updated = false

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

  volatile bool midi_enabled;
  volatile bool cv_mod1;
  volatile bool cv_mod2;

  bool timbre_locked;
  bool color_locked;

  volatile bool filter_enabled;
  float filter_mix;
  volatile float filter_cutoff;
  volatile float filter_resonance;

  bool show_saved_flag;
  unsigned long saved_start_time;

  DisplayMode display_state;
  volatile bool oscilloscope_enabled;

  Encoder encoder;
  float pot_timbre;
  float pot_color;

  // MIDI controller mode
  MidiControllerMode controller_mode;

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
    .midi_enabled = true, \
    .cv_mod1 = false, \
    .cv_mod2 = false, \
    .timbre_locked = false, \
    .color_locked = false, \
    .filter_enabled = true, \
    .filter_mix = 1.0f, \
    .filter_cutoff = 0.5f, \
    .filter_resonance = 0.25f, \
    .show_saved_flag = false, \
    .saved_start_time = 0, \
    .display_state = ENGINE_SELECT_MODE, \
    .oscilloscope_enabled = true, \
    .encoder = EncoderNew(ENCODER_CLK, ENCODER_DT, ENCODER_SW), \
    .pot_timbre = 0.5f, \
    .pot_color = 0.5f, \
    .controller_mode = CONTROLLER_BOTH, \
    .system_ready = false, \
  };


struct Parameter {
  volatile float value;
  uint8_t gpio;
  PotMode mode;
  ResolutionMode resolution_mode;
  uint8_t last_value;
  bool midi_locked;
  struct {
    float velocity;
    float damping;
    float stiffness;
  } kinetic_params;
  struct {
    uint8_t min;
    uint8_t center;
    uint8_t max;
  } attenuator_params;
};

#define ParameterNew(gpio_) \
  { \
    .value = 0, .gpio = gpio_, mode = POT_NORMAL, resolution_mode = RES_RAM, .last_value = 0, .midi_locked = false, \
    .kinetic_params = { .velocity = 0, .damping = 0.5, .stiffness = 0.5 }, \
    .attenuator_params = {.min = 0, \
                          .center = 64, \
                          .max = 127 } \
  }



static inline void set_parameter(RuntimeState *gstate, volatile float *field, float val) {
  if (gstate->controller_mode == CONTROLLER_BOTH || gstate->controller_mode == CONTROLLER_MODE_OFF)
    *field = val;
}
