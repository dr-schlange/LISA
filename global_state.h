/*
  LISA (v0.0.1)

  Copyright (c) 2026 Dr Schlange
  Licensed under GNU GPLv3
*/
#pragma once
#include <cstdint>
#include <pico/stdlib.h>
#include "constants_config.h"
#include "encoder.h"

enum DisplayMode { ENGINE_SELECT_MODE,
                   ENGINE_SETTINGS_CONFIG,
                   OSCILLOSCOPE_MODE,
                   ALL_PARAMS_MODE,
                   GLOBAL_SETTINGS
};


enum MidiControllerMode { CONTROLLER_MODE_OFF,
                          CONTROLLER_BOTH,
                          CONTROLLER_ONLY
};


enum ResolutionMode {
  RES_RAW,
  RES_CATCHUP,
  RES_ATTENUATOR
};

enum PotMode {
  POT_NORMAL,
  POT_KINETIC
};

enum PotsRow {
  ROW_ENGINE_SELECT,
  ROW_GENERAL,
  ROW_TIMBRE,
  ROW_COLOR,
  ROW_FILTER,
  ROW_ENVELOPE,
  ROW_NUM,
  ROW_EDIT_ENGINE,  // hidden state
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
  bool screen_locked;
  uint8_t quantized;
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
    .screen_locked = false, \
    .quantized = 0, \
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
  PotsRow pots_row_state;


  bool engine_updated;
  volatile bool env_params_changed;
  unsigned long last_param_change;

  volatile bool midi_enabled;
  volatile bool cv_mod1_enabled;
  volatile bool cv_mod2_enabled;

  bool sustain_enabled;
  volatile bool filter_enabled;
  float filter_mix;

  bool show_saved_flag;
  unsigned long saved_start_time;

  DisplayMode display_state;
  volatile bool oscilloscope_enabled;

  Encoder encoder;
  EncoderStatus encoder_status;

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
  // Extra params
  Parameter b1;
  Parameter b2;
  Parameter b3;
  Parameter b4;
  Parameter b5;

  // Mapped pots
  Parameter *A;
  Parameter *B;
  Parameter *C;
  volatile bool system_ready;
};


static inline void init_global_state(RuntimeState *gstate) {
  gstate->midi_ch = 1;
  gstate->engine_idx = 0;
  gstate->last_engine_idx = -1;
  gstate->pots_row_state = ROW_GENERAL;
  gstate->engine_updated = true;
  gstate->env_params_changed = true;
  gstate->last_param_change = 0;
  gstate->midi_enabled = true;
  gstate->cv_mod1_enabled = false;
  gstate->cv_mod2_enabled = false;
  gstate->sustain_enabled = false;
  gstate->filter_enabled = true;
  gstate->filter_mix = 1.f;
  gstate->show_saved_flag = false;
  gstate->saved_start_time = 0;
  gstate->display_state = ENGINE_SELECT_MODE;
  gstate->oscilloscope_enabled = true;
  gstate->encoder = EncoderNew(ENCODER_CLK, ENCODER_DT, ENCODER_SW);
  gstate->encoder_status = NO_ACTION;
  gstate->controller_mode = CONTROLLER_BOTH;
  gstate->timbre = ParameterNew(POT_A, MIDI_TIMBRE, 0.4f);
  gstate->color = ParameterNew(POT_B, MIDI_COLOR, 0.3f);
  gstate->cutoff = ParameterNew(POT_C, MIDI_CUTOFF, 0.5f);
  gstate->resonance = ParameterNew(POT_B, MIDI_RESONANCE, 0.25f);
  gstate->timbre_mod = ParameterNew(POT_B, MIDI_TIMBRE_MOD, 0.f);
  gstate->color_mod = ParameterNew(POT_B, MIDI_COLOR_MOD, 0.f);
  gstate->fm_mod = ParameterNew(POT_C, MIDI_FM_MOD, 0.f);
  gstate->master_volume = ParameterNew(POT_A, MIDI_MASTER_VOL, 0.7f);
  gstate->env_attack = ParameterNew(POT_A, MIDI_ATTACK, 0.009f);
  gstate->env_release = ParameterNew(POT_B, MIDI_RELEASE, 0.01f);

  gstate->b1 = ParameterNew(POT_B, MIDI_B1, 0.01f);
  gstate->b2 = ParameterNew(POT_C, MIDI_B2, 0.01f);
  gstate->b3 = ParameterNew(POT_C, MIDI_B3, 0.01f);
  gstate->b4 = ParameterNew(POT_C, MIDI_B4, 0.01f);
  gstate->b5 = ParameterNew(POT_C, MIDI_B5, 0.01f);


  gstate->A = &(gstate->timbre);
  gstate->B = &(gstate->color);
  gstate->C = &(gstate->cutoff);
  gstate->system_ready = false;
}

static inline void lock_all_parameters(RuntimeState *gstate, bool status) {
  gstate->timbre.screen_locked = status;
  gstate->color.screen_locked = status;
  gstate->cutoff.screen_locked = status;
  gstate->resonance.screen_locked = status;
  gstate->timbre_mod.screen_locked = status;
  gstate->color_mod.screen_locked = status;
  gstate->fm_mod.screen_locked = status;
  gstate->master_volume.screen_locked = status;
  gstate->env_attack.screen_locked = status;
  gstate->env_release.screen_locked = status;
  gstate->b1.screen_locked = status;
  gstate->b2.screen_locked = status;
  gstate->b3.screen_locked = status;
  gstate->b4.screen_locked = status;
  gstate->b5.screen_locked = status;
}

static inline void lock_mapped_pots(RuntimeState *gstate, bool status) {
  gstate->A->screen_locked = status;
  gstate->B->screen_locked = status;
  gstate->C->screen_locked = status;
}

static inline void map_abc_pots(RuntimeState *gstate, Parameter *A, Parameter *B, Parameter *C) {
  if (gstate->A = A) {
    gstate->A->gpio = POT_A;
  }
  if (gstate->B = B) {
    gstate->B->gpio = POT_B;
  }
  if (gstate->C = C) {
    gstate->C->gpio = POT_C;
  }
}
