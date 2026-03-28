/*
  LISA (v0.0.1)

  Copyright (c) 2025 Dr Schlange
  Licensed under GNU GPLv3
*/
#pragma once
#include <cstdint>
#include <pico/stdlib.h>
#include "constants_config.h"
#include "encoder.h"

enum DisplayMode { ENGINE_SELECT_MODE,
                   ENGINE_SETTINGS_CONFIG,
                   OSCILLOSCOPE_MODE };


enum MidiControllerMode { CONTROLLER_MODE_OFF,
                          CONTROLLER_BOTH,
                          CONTROLLER_ONLY
};

struct Parameter {
  volatile float value;
  uint8_t last_value;
  float smoothed;
  uint8_t gpio;
  PotMode mode;
  uint8_t midi_cc;
  ResolutionMode resolution_mode;
  bool locked;
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

#define ParameterNew(gpio_, midi_cc_, val_) \
  { \
    .value = 0, \
    .last_value = 0, \
    .smoothed = val_, \
    .gpio = gpio_, \
    .mode = POT_NORMAL, \
    .midi_cc = midi_cc_, \
    .resolution_mode = RES_RAW, \
    .locked = false, \
    .kinetic_params = { \
      .velocity = 0, \
      .damping = 0.5, \
      .stiffness = 0.5 \
    }, \
    .attenuator_params = { \
      .min = 0, \
      .center = 64, \
      .max = 127 \
    } \
  }


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


  bool engine_updated;
  volatile bool env_params_changed;
  unsigned long last_param_change;

  volatile bool midi_enabled;
  volatile bool cv_mod1;
  volatile bool cv_mod2;

  bool sustain_enabled;
  volatile bool filter_enabled;
  float filter_mix;

  bool show_saved_flag;
  unsigned long saved_start_time;

  DisplayMode display_state;
  volatile bool oscilloscope_enabled;

  Encoder encoder;
  float pot_timbre;
  float pot_color;

  // MIDI controller mode
  MidiControllerMode controller_mode;

  // Parameters
  Parameter timbre;
  Parameter color;
  Parameter cutoff;
  Parameter resonance;
  Parameter timbre_mod;
  Parameter color_mod;
  Parameter fm_mod;
  Parameter master_volume;
  Parameter env_attack;
  Parameter env_release;
  volatile bool system_ready;
};

#define GlobalStateNew() \
  { \
    .midi_ch = 1, \
    .engine_idx = 1, \
    .last_engine_idx = -1, \
    .engine_updated = true, \
    .env_params_changed = true, \
    .last_param_change = 0, \
    .midi_enabled = true, \
    .cv_mod1 = false, \
    .cv_mod2 = false, \
    .sustain_enabled = false, \
    .filter_enabled = true, \
    .filter_mix = 1.0f, \
    .show_saved_flag = false, \
    .saved_start_time = 0, \
    .display_state = ENGINE_SELECT_MODE, \
    .oscilloscope_enabled = true, \
    .encoder = EncoderNew(ENCODER_CLK, ENCODER_DT, ENCODER_SW), \
    .pot_timbre = 0.5f, \
    .pot_color = 0.5f, \
    .controller_mode = CONTROLLER_BOTH, \
    .timbre = ParameterNew(POT_TIMBRE, MIDI_TIMBRE, 0.4f), \
    .color = ParameterNew(POT_COLOR, MIDI_COLOR, 0.3f), \
    .cutoff = ParameterNew(POT_CUTOFF, MIDI_CUTOFF, 0.5f), \
    .resonance = ParameterNew(POT_RESONANCE, MIDI_RESONANCE, 0.25f), \
    .timbre_mod = ParameterNew(POT_TIMBRE, MIDI_TIMBRE_MOD, 0.f), \
    .color_mod = ParameterNew(POT_COLOR, MIDI_COLOR_MOD, 0.f), \
    .fm_mod = ParameterNew(POT_CUTOFF, MIDI_FM_MOD, 0.f), \
    .master_volume = ParameterNew(POT_CUTOFF, MIDI_MASTER_VOL, 0.7f), \
    .env_attack = ParameterNew(POT_TIMBRE, MIDI_ATTACK, 0.009f), \
    .env_release = ParameterNew(POT_COLOR, MIDI_RELEASE, 0.01f), \
    .system_ready = false, \
  };
