/*
  LISA (v0.0.1)

  Copyright (c) 2026 Dr Schlange
  Licensed under GNU GPLv3
*/
#pragma once
#include "constants_config.h"
#include "encoder.h"
#include <cstdint>
#include <pico/stdlib.h>

enum DisplayMode {
  ENGINE_SELECT_MODE,
  ENGINE_SETTINGS_CONFIG,
  OSCILLOSCOPE_MODE,
  ALL_PARAMS_MODE,
  GLOBAL_SETTINGS
};

enum MidiControllerMode {
  CONTROLLER_MODE_OFF,
  CONTROLLER_BOTH,
  CONTROLLER_ONLY,
  CONTROLLER_NUM,
};

enum ResolutionMode {
  RES_RAW,
  RES_CATCHUP,
  RES_NUM,
  RES_UNKNOWN,
};

enum PotMode {
  POT_NORMAL,
  POT_KINETIC,
  POT_MODE_NUM,
  POT_ATTENUATOR,
  POT_UNKNOWN,
};

enum PotsRow {
  ROW_ENGINE_SELECT,
  ROW_GENERAL,
  ROW_TIMBRE,
  ROW_COLOR,
  ROW_FILTER,
  ROW_ENVELOPE,
  ROW_NUM,
  ROW_EDIT_ENGINE, // hidden state edit engine
  ROW_PAGE_SELECT, // hidden state set all parameters page
};

enum GlobalSettings {
  SETTING_PARAMETER,
  SETTING_GLOB_RES,
  SETTING_GLOB_MODE,
  SETTING_NUM,
  SETTING_EDIT_PARAMETER,
  SETTING_EDIT_RES,
  SETTING_EDIT_MODE,
};

#define BASE_PARAMETER                                                         \
  bool extended;                                                               \
  volatile float value;                                                        \
  uint8_t last_value;                                                          \
  float smoothed;                                                              \
  uint8_t gpio;                                                                \
  bool screen_locked;                                                          \
  ResolutionMode resolution_mode

struct Parameter {
  BASE_PARAMETER;
};

#define ParameterNew(gpio_, midi_cc_, val_)                                    \
  {.extended = false,                                                          \
   .value = val_,                                                              \
   .last_value = (uint8_t)val_,                                                \
   .smoothed = val_,                                                           \
   .gpio = gpio_,                                                              \
   .screen_locked = false,                                                     \
   .resolution_mode = RES_CATCHUP}

struct ExtParameter {
  BASE_PARAMETER;
  PotMode mode;
  uint8_t midi_cc;
  bool locked;
  union {
    struct {
      Parameter mass;
      Parameter damping;
      Parameter stiffness;
      float last_pos;
      float velocity;
      unsigned long last_update_time;
      unsigned long last_move_time;
      unsigned long last_midi_send;
    } kinetic;
    struct {
      Parameter min;
      Parameter center;
      Parameter max;
    } attenuator;
  };
};

#define ExtParameterNew(gpio_, midi_cc_, val_)                                 \
  {                                                                            \
    .extended = true, .value = val_, .last_value = (uint8_t)val_,              \
    .smoothed = val_, .gpio = gpio_, .screen_locked = false,                   \
    .resolution_mode = RES_CATCHUP, .mode = POT_NORMAL, .midi_cc = midi_cc_,   \
    .locked = false, .kinetic = {                                              \
      .mass =                                                                  \
          {                                                                    \
              .extended = false,                                               \
              .value = 0.2f,                                                   \
              .last_value = (uint8_t)0.2f,                                     \
              .smoothed = 0.2f,                                                \
              .gpio = gpio_,                                                   \
              .screen_locked = false,                                          \
              .resolution_mode = RES_CATCHUP,                                  \
          },                                                                   \
      .damping =                                                               \
          {                                                                    \
              .extended = false,                                               \
              .value = 0.4f,                                                   \
              .last_value = (uint8_t)0.4,                                      \
              .smoothed = 0.4f,                                                \
              .gpio = gpio_,                                                   \
              .screen_locked = false,                                          \
              .resolution_mode = RES_CATCHUP,                                  \
          },                                                                   \
      .stiffness =                                                             \
          {                                                                    \
              .extended = false,                                               \
              .value = 0.4f,                                                   \
              .last_value = (uint8_t)0.4f,                                     \
              .smoothed = 0.4f,                                                \
              .gpio = gpio_,                                                   \
              .screen_locked = false,                                          \
              .resolution_mode = RES_CATCHUP,                                  \
          },                                                                   \
      .last_pos = 0.f,                                                         \
      .velocity = 0.f,                                                         \
      .last_update_time = 0,                                                   \
      .last_move_time = 0,                                                     \
      .last_midi_send = 0,                                                     \
    }                                                                          \
  }

#define REFRESH_IS_SCHEDULED(runtime) (runtime)->engine_updated
#define SCHEDULE_REFRESH(runtime) (runtime)->engine_updated = true
#define CLEAR_REFRESH(runtime) (runtime)->engine_updated = false

#define SET_SYSTEM_READY(runtime) (runtime)->system_ready = true
#define IS_OSCILLOSCOPE_ON(runtime) (runtime)->oscilloscope_enabled
#define TOGGLE_OSCILLOSCOPE(runtime)                                           \
  ((runtime)->oscilloscope_enabled = !(runtime)->oscilloscope_enabled)
#define IS_OSCILLOSCOPE_OFF(runtime) (!(runtime)->oscilloscope_enabled)
#define IS_OSCILLOSCOPE_MODE(runtime)                                          \
  (runtime)->display_state == OSCILLOSCOPE_MODE
#define IS_ENGINE_SETTINGS_CONFIG(runtime)                                     \
  (runtime)->display_state == ENGINE_SETTINGS_CONFIG
#define IS_ENGINE_SELECT_MODE(runtime)                                         \
  (runtime)->display_state == ENGINE_SELECT_MODE

struct RuntimeState {
  uint8_t midi_ch;
  volatile int engine_idx;
  int last_engine_idx;
  PotsRow pots_row_state;
  GlobalSettings glob_settings_state;
  ExtParameter *glob_settings_edit_param;

  bool engine_updated;
  unsigned long last_param_change;

  volatile bool midi_enabled;
  volatile bool cv_mod1_enabled;
  volatile bool cv_mod2_enabled;

  bool sustain_enabled;
  volatile bool filter_enabled;

  bool show_saved_flag;
  unsigned long saved_start_time;

  DisplayMode display_state;
  volatile bool oscilloscope_enabled;

  Encoder encoder;
  EncoderStatus encoder_status;

  // MIDI controller mode
  MidiControllerMode controller_mode;

  // Parameters
  ExtParameter timbre;
  ExtParameter color;
  ExtParameter cutoff;
  ExtParameter resonance;
  ExtParameter timbre_mod;
  ExtParameter color_mod;
  ExtParameter fm_mod;
  ExtParameter master_volume;
  ExtParameter env_attack;
  ExtParameter env_release;
  Parameter filter_type;
  Parameter gain;
  Parameter fm_slew;
  // Extra params
  ExtParameter b1;
  ExtParameter b2;
  ExtParameter b3;
  ExtParameter b4;
  ExtParameter b5;

  // Mapped pots
  Parameter *A;
  Parameter *B;
  Parameter *C;

  volatile bool system_ready;
};

const char *const all_parameters[] = {
    "tmbr amt", "colr amt", "cutoff",   "resonance", "tmbr mod",   "colr mod",
    "fm mod",   "mast vol", "envl atk", "envl rel",  "filt. type", "gain",
    "fm_slew",  "b1",       "b2",       "b3",        "b4",         "b5",
};
constexpr uint8_t ALL_PARAMETERS_NUM =
    sizeof(all_parameters) / sizeof(all_parameters[0]);

static inline void init_global_state(RuntimeState *gstate) {
  gstate->midi_ch = 1;
  gstate->engine_idx = 0;
  gstate->last_engine_idx = -1;
  gstate->pots_row_state = ROW_GENERAL;
  gstate->glob_settings_state = SETTING_PARAMETER;
  gstate->engine_updated = true;
  gstate->last_param_change = 0;
  gstate->midi_enabled = true;
  gstate->cv_mod1_enabled = false;
  gstate->cv_mod2_enabled = false;
  gstate->sustain_enabled = false;
  gstate->filter_enabled = true;
  gstate->show_saved_flag = false;
  gstate->saved_start_time = 0;
  gstate->display_state = ENGINE_SELECT_MODE;
  gstate->oscilloscope_enabled = true;
  gstate->encoder = EncoderNew(ENCODER_CLK, ENCODER_DT, ENCODER_SW);
  gstate->encoder_status = NO_ACTION;
  gstate->controller_mode = CONTROLLER_BOTH;
  gstate->timbre = ExtParameterNew(POT_A, MIDI_TIMBRE, 0.4f);
  gstate->color = ExtParameterNew(POT_B, MIDI_COLOR, 0.3f);
  gstate->cutoff = ExtParameterNew(POT_C, MIDI_CUTOFF, 0.5f);
  gstate->resonance = ExtParameterNew(POT_B, MIDI_RESONANCE, 0.25f);
  gstate->timbre_mod = ExtParameterNew(POT_B, MIDI_TIMBRE_MOD, 0.f);
  gstate->color_mod = ExtParameterNew(POT_B, MIDI_COLOR_MOD, 0.f);
  gstate->fm_mod = ExtParameterNew(POT_C, MIDI_FM_MOD, 0.f);
  gstate->master_volume = ExtParameterNew(POT_A, MIDI_MASTER_VOL, 0.7f);
  gstate->env_attack = ExtParameterNew(POT_A, MIDI_ATTACK, 0.009f);
  gstate->env_release = ExtParameterNew(POT_B, MIDI_RELEASE, 0.01f);

  gstate->b1 = ExtParameterNew(POT_B, MIDI_B1, 0.01f);
  gstate->b2 = ExtParameterNew(POT_C, MIDI_B2, 0.01f);
  gstate->b3 = ExtParameterNew(POT_C, MIDI_B3, 0.01f);
  gstate->b4 = ExtParameterNew(POT_C, MIDI_B4, 0.01f);
  gstate->b5 = ExtParameterNew(POT_C, MIDI_B5, 0.01f);

  gstate->filter_type = ParameterNew(POT_A, MIDI_FILTER_TYPE, 0.f);
  gstate->gain = ParameterNew(POT_B, MIDI_GAIN, 0.25f);
  gstate->fm_slew = ParameterNew(POT_C, MIDI_FM_SLEW, 0.05f);

  gstate->A = (Parameter *)&(gstate->timbre);
  gstate->B = (Parameter *)&(gstate->color);
  gstate->C = (Parameter *)&(gstate->cutoff);
  gstate->glob_settings_edit_param = NULL;
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
  gstate->filter_type.screen_locked = status;
  gstate->gain.screen_locked = status;
  gstate->fm_slew.screen_locked = status;
}

static inline void lock_mapped_pots(RuntimeState *gstate, bool status) {
  gstate->A->screen_locked = status;
  gstate->B->screen_locked = status;
  gstate->C->screen_locked = status;
}

static inline void map_abc_pots(RuntimeState *gstate, Parameter *A,
                                Parameter *B, Parameter *C) {
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

static inline void set_parameter_resolution(Parameter *param,
                                            ResolutionMode mode) {
  param->resolution_mode = mode;
}

static inline void set_ext_param_resolution(ExtParameter *param,
                                            ResolutionMode mode) {
  set_parameter_resolution((Parameter *)param, mode);
  set_parameter_resolution(&(param->kinetic.damping), mode);
  set_parameter_resolution(&(param->kinetic.stiffness), mode);
  set_parameter_resolution(&(param->kinetic.mass), mode);
}

static inline void set_all_resolution(RuntimeState *gstate,
                                      ResolutionMode mode) {
  set_ext_param_resolution(&(gstate->timbre), mode);
  set_ext_param_resolution(&(gstate->color), mode);
  set_ext_param_resolution(&(gstate->cutoff), mode);
  set_ext_param_resolution(&(gstate->resonance), mode);
  set_ext_param_resolution(&(gstate->timbre_mod), mode);
  set_ext_param_resolution(&(gstate->color_mod), mode);
  set_ext_param_resolution(&(gstate->fm_mod), mode);
  set_ext_param_resolution(&(gstate->master_volume), mode);
  set_ext_param_resolution(&(gstate->env_attack), mode);
  set_ext_param_resolution(&(gstate->env_release), mode);
  set_ext_param_resolution(&(gstate->b1), mode);
  set_ext_param_resolution(&(gstate->b2), mode);
  set_ext_param_resolution(&(gstate->b3), mode);
  set_ext_param_resolution(&(gstate->b4), mode);
  set_ext_param_resolution(&(gstate->b5), mode);
}

static inline void set_all_mode(RuntimeState *gstate, PotMode mode) {
  gstate->timbre.mode = mode;
  gstate->color.mode = mode;
  gstate->cutoff.mode = mode;
  gstate->resonance.mode = mode;
  gstate->timbre_mod.mode = mode;
  gstate->color_mod.mode = mode;
  gstate->fm_mod.mode = mode;
  gstate->master_volume.mode = mode;
  gstate->env_attack.mode = mode;
  gstate->env_release.mode = mode;
  gstate->b1.mode = mode;
  gstate->b2.mode = mode;
  gstate->b3.mode = mode;
  gstate->b4.mode = mode;
  gstate->b5.mode = mode;
}

static inline void set_pot_mode(RuntimeState *gstate, PotMode mode) {
  if (gstate->glob_settings_edit_param == NULL) {
    set_all_mode(gstate, mode);
    return;
  }
  gstate->glob_settings_edit_param->mode = mode;
}

static inline void set_resolution_mode(RuntimeState *gstate,
                                       ResolutionMode mode) {
  if (gstate->glob_settings_edit_param == NULL) {
    set_all_resolution(gstate, mode);
    return;
  }
  set_ext_param_resolution(gstate->glob_settings_edit_param, mode);
}

static inline PotMode glob_get_pot_mode(RuntimeState *gstate) {
  ExtParameter *p = gstate->glob_settings_edit_param;
  if (p == NULL) {
    uint8_t state = gstate->timbre.mode;
    p = &(gstate->timbre);
    for (uint8_t i = 1; i < ALL_PARAMETERS_NUM; i++) {
      if (state != p[i].mode) {
        return POT_UNKNOWN;
      }
    }
    return (PotMode)state;
  }
  return p->mode;
}

static inline ResolutionMode glob_get_res_mode(RuntimeState *gstate) {
  ExtParameter *p = gstate->glob_settings_edit_param;
  if (p == NULL) {
    uint8_t state = gstate->timbre.resolution_mode;
    p = &(gstate->timbre);
    for (uint8_t i = 1; i < ALL_PARAMETERS_NUM; i++) {
      if (state != p[i].resolution_mode) {
        return RES_UNKNOWN;
      }
    }
    return (ResolutionMode)state;
  }
  return p->resolution_mode;
}

static inline void sync_all_kinetic_values(RuntimeState *gstate) {
  ExtParameter *p = &(gstate->timbre);
  for (uint8_t i = 1; i < ALL_PARAMETERS_NUM; i++) {
    if (!p->kinetic.mass.screen_locked) {
      p[i].kinetic.mass.value = p->kinetic.mass.value;
    }
    if (!p->kinetic.damping.screen_locked) {
      p[i].kinetic.damping.value = p->kinetic.damping.value;
    }
    if (!p->kinetic.stiffness.screen_locked) {
      p[i].kinetic.stiffness.value = p->kinetic.stiffness.value;
    }
  }
}
