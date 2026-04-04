/*
  LISA (v0.0.1)

  Copyright (c) 2026 Dr Schlange
  Licensed under GNU GPLv3
*/
#pragma once
#include "constants_config.h"
#include "midi.h"
#include "encoder.h"
#include "global_state.h"
#include <cstdint>

static inline void midi_cc_forward(RuntimeState *gstate, uint8_t cc, uint8_t raw_value) {
  if (gstate->controller_mode == CONTROLLER_BOTH || gstate->controller_mode == CONTROLLER_ONLY)
    send_midi_cc(cc, raw_value, gstate->midi_ch);
}

static inline void midi_cc_forward_(uint8_t cc, uint8_t raw_value, uint8_t midi_ch, MidiControllerMode mode) {
  if (mode == CONTROLLER_BOTH || mode == CONTROLLER_ONLY)
    send_midi_cc(cc, raw_value, midi_ch);
}


static inline void set_parameter(RuntimeState *gstate, volatile float *field, float val) {
  if (gstate->controller_mode == CONTROLLER_BOTH || gstate->controller_mode == CONTROLLER_MODE_OFF)
    *field = val;
}

static inline void set_parameter_(Parameter *param, volatile float val, MidiControllerMode mode) {
  if (mode == CONTROLLER_BOTH || mode == CONTROLLER_MODE_OFF) {
    param->value = val;
  }
}

static inline uint8_t peek_pot_quantized(Parameter *param, float smoothing_factor) {
  if (param == NULL) {
    return 0;
  }
  float new_value = analogRead(param->gpio) / 1023.f;

  // smoothing the noise
  param->smoothed += (new_value - param->smoothed) * smoothing_factor;
  if (param->smoothed > 0.999f) param->smoothed = 1.0f;
  if (param->smoothed < 0.001f) param->smoothed = 0.0f;

  // quantizing for comparison
  return (uint8_t)(param->smoothed * 127.f);
}

#define CLOSENESS 0.01f

static inline void handle_pot_parameter(Parameter *param, RuntimeState *gstate, float smoothing_factor) {
  if (param == NULL) {
    return;
  }

  // quantizing for comparison
  uint8_t quantized = peek_pot_quantized(param, smoothing_factor);
  if (quantized != param->last_value) {
    if (param->screen_locked && param->resolution_mode == RES_RAW) {
      return;
    }
    if (param->screen_locked && param->resolution_mode == RES_CATCHUP) {
      // catchup!
      if (fabsf(param->smoothed - param->value) < CLOSENESS) {
        param->screen_locked = false;
      }
      return;
    }
    param->last_value = quantized;

    // Kinetic movement
    if (param->extended && ((ExtParameter *)param)->mode == POT_KINETIC) {
      ExtParameter *p = (ExtParameter *)param;
      float diff = (param->smoothed - p->kinetic.last_pos) * 20.f;
      p->kinetic.velocity += diff;
      p->kinetic.last_pos = param->smoothed;
      p->kinetic.last_move_time = millis();
      param->value = param->smoothed;
    }

    const MidiControllerMode mode = gstate->controller_mode;

    // RAW
    set_parameter_(param, param->smoothed, mode);
    if (param->extended) {
      midi_cc_forward_(((ExtParameter *)param)->midi_cc, quantized, gstate->midi_ch, mode);
    }
  }

  SCHEDULE_REFRESH(gstate);
}

static inline void update_kinetic_physics(RuntimeState *gstate, ExtParameter *param) {
  if (param->mode != POT_KINETIC) return;
  unsigned long now = micros();
  if (millis() - param->kinetic.last_move_time < 30) {
    param->kinetic.last_update_time = now;
    return;
  }

  // Compute dt
  float dt = (now - param->kinetic.last_update_time) / 1000000.f;
  param->kinetic.last_update_time = now;

  if (dt <= 0.f || dt > 0.1f) return;

  float k = param->kinetic.stiffness.value * 100.f;
  float c = param->kinetic.damping.value * 2.5f;
  float m = 0.01f + (param->kinetic.mass.value * 0.5f);

  // Compute spring force & damping
  float error = param->smoothed - param->value;
  float spring_force = error * k;
  float damping_force = -param->kinetic.velocity * c;

  // Acceleration (a = F / m)
  float acceleration = (spring_force + damping_force) / m;

  // Euler integration
  param->kinetic.velocity += acceleration * dt;
  float next_value = param->value + (param->kinetic.velocity * dt);

  if (next_value > 1.0f) {
    next_value = 1.0f;
    param->kinetic.velocity *= -0.2f;  // potential bounce against the limit
  } else if (next_value < 0.0f) {
    next_value = 0.0f;
    param->kinetic.velocity *= -0.2f;  // potential bounce against the limit
  }
  param->value = next_value;


  // Update the value if needed
  if (fabsf(param->kinetic.velocity) > 0.0001f || fabsf(error) > 0.0001f) {
    set_parameter_((Parameter *)param, param->value, gstate->controller_mode);
    midi_cc_forward_(param->midi_cc, (uint8_t)(param->value * 127.f), gstate->midi_ch, gstate->controller_mode);
  } else {
    param->value = param->smoothed;
    param->kinetic.velocity = 0;
  }
}

#define SMOOTH_POT 0.06f

void handle_control(RuntimeState *gstate) {
  static float smoothT = 0.5f;
  static float smoothC = 0.5f;
  static float smoothTMod = 0.0f;
  static float smoothCMod = 0.0f;
  static float smoothCut = 0.5f;
  static float smoothRes = 0.25f;
  static unsigned long last_pot_read = 0;

  if (millis() - last_pot_read <= POT_READ_POLL_MS) {
    return;
  }

  last_pot_read = millis();

  float rT = analogRead(POT_TIMBRE) / 1023.0f;
  float rC = analogRead(POT_COLOR) / 1023.0f;
  float srcT = analogRead(POT_TIMBRE_MOD) / 1023.0f;
#if HAS_4_POTS
  float srcC = analogRead(POT_COLOR_MOD) / 1023.0f;
#else
  float srcC = 0.5f;
#endif

  if (!gstate->midi_enabled) {
    gstate->timbre.locked = false;
    gstate->color.locked = false;
    gstate->cutoff.locked = false;
    gstate->resonance.locked = false;
    SCHEDULE_REFRESH(gstate);
  }

  if (gstate->display_state == GLOBAL_SETTINGS && gstate->glob_settings_edit_param == NULL && glob_get_pot_mode(gstate) == POT_KINETIC) {
    sync_all_kinetic_values(gstate);
  }

  if (gstate->display_state != GLOBAL_SETTINGS) {
    ExtParameter *p = &(gstate->timbre);
    for (int i = 0; i < ALL_PARAMETERS_NUM; i++) {
      if (p[i].mode == POT_KINETIC) {
        update_kinetic_physics(gstate, &p[i]);
      }
    }
  }

  float p1_smooth_pot, p2_smooth_pot, p3_smooth_pot;
  if (gstate->display_state == ALL_PARAMS_MODE) {
    switch (gstate->pots_row_state) {
      case ROW_GENERAL:
        p1_smooth_pot = p2_smooth_pot = p3_smooth_pot = SMOOTH_POT;
        break;
      case ROW_TIMBRE:
        p1_smooth_pot = p2_smooth_pot = p3_smooth_pot = SMOOTH_POT;
        break;
      case ROW_COLOR:
        p1_smooth_pot = p2_smooth_pot = p3_smooth_pot = SMOOTH_POT;
        break;
      case ROW_FILTER:
        p1_smooth_pot = p2_smooth_pot = p3_smooth_pot = SMOOTH_POT;
        break;
      case ROW_ENVELOPE:
        p1_smooth_pot = p2_smooth_pot = p3_smooth_pot = SMOOTH_POT;
        break;
    }
  } else {
    p1_smooth_pot = p2_smooth_pot = SMOOTH_POT;
    p3_smooth_pot = SMOOTH_POT;
  }

  if (gstate->cv_mod1_enabled) {
    // --- Smooth the potentiometer inputs (depth controls) ---
    smoothT += (rT - smoothT) * 0.15f;
    smoothC += (rC - smoothC) * 0.15f;

    // --- Smooth the modulation sources ---
    smoothTMod += (srcT - smoothTMod) * 0.1f;  // slower smoothing
    smoothCMod += (srcC - smoothCMod) * 0.1f;

    // --- Apply modulation depth with soft scaling ---
    gstate->timbre_mod.value += ((smoothT * smoothTMod) - gstate->timbre_mod.value) * 0.05f;
    gstate->color_mod.value += ((smoothC * smoothCMod) - gstate->color_mod.value) * 0.05f;

    // --- Set base values for other modes ---
    gstate->timbre.value = 0.5f;
    gstate->color.value = 0.5f;
    SCHEDULE_REFRESH(gstate);

  } else if (gstate->midi_enabled) {
    handle_pot_parameter(gstate->A, gstate, p1_smooth_pot);
    handle_pot_parameter(gstate->B, gstate, p2_smooth_pot);
    SCHEDULE_REFRESH(gstate);
  }

  if (gstate->C) {
    handle_pot_parameter(gstate->C, gstate, p3_smooth_pot);
    SCHEDULE_REFRESH(gstate);
  }

  if (gstate->filter_enabled) {
#if HAS_4_POTS
    handle_pot_parameter(&(gstate->resonance), gstate, 0.15f);
#endif
    SCHEDULE_REFRESH(gstate);
  }

  else if (gstate->cv_mod2_enabled) {
    smoothT += (rT - smoothT) * 0.08f;
    smoothC += (rC - smoothC) * 0.08f;
    gstate->timbre.value = smoothT;
    gstate->color.value = smoothC;

    gstate->color.value = smoothC;

    // --- 2. Rolling Average Filter ---
    static float historyT[16];
    static float historyC[16];
    static int histIdx = 0;

    historyT[histIdx] = srcT;
    historyC[histIdx] = srcC;
    histIdx = (histIdx + 1) % 16;

    float avgT = 0, avgC = 0;
    for (int i = 0; i < 16; i++) {
      avgT += historyT[i];
      avgC += historyC[i];
    }
    avgT /= 16.0f;
    avgC /= 16.0f;

    smoothTMod += (avgT - smoothTMod) * 0.05f;
    smoothCMod += (avgC - smoothCMod) * 0.05f;

    // --- 3. Strict Deadzone ---
    const float CV_DEADZONE = 0.15f;
    bool cvT_active = (smoothTMod > CV_DEADZONE);
    bool cvC_active = (smoothCMod > CV_DEADZONE);

    // --- 4. Large-Band Hysteresis for Engines ---
    static float lockT = -1.0f;
    const float ENG_HYST = 0.10f;

    if (cvT_active) {
      if (fabsf(smoothTMod - lockT) > ENG_HYST) {
        float norm = (smoothTMod - CV_DEADZONE) / (1.0f - CV_DEADZONE);
        int new_idx = (int)(norm * (float)NUM_ENGINES);
        new_idx = constrain(new_idx, 0, NUM_ENGINES - 1);

        if (new_idx != gstate->engine_idx) {
          gstate->engine_idx = new_idx;
          lockT = smoothTMod;
          SCHEDULE_REFRESH(gstate);
        }
      }
    } else {
      lockT = -1.0f;
    }

    // --- 5. Large-Band Hysteresis for FM ---
    static float lockC = 0.0f;
    const float FM_HYST = 0.1f;

    if (cvC_active) {
      if (fabsf(smoothCMod - lockC) > FM_HYST) {
        float target_fm = (smoothCMod - CV_DEADZONE) / (1.0f - CV_DEADZONE);
        gstate->fm_mod.value = constrain(target_fm, 0.0f, 1.0f);
        lockC = smoothCMod;
      }
    } else {
      gstate->fm_mod.value *= 0.5f;
      if (gstate->fm_mod.value < 0.01f) {
        gstate->fm_mod.value = 0.0f;
        lockC = 0.0f;
      }
    }

    gstate->timbre.locked = false;
    gstate->color.locked = false;
  } else if (!gstate->midi_enabled) {
    smoothT += (rT - smoothT) * 0.08f;
    smoothC += (rC - smoothC) * 0.08f;
    gstate->timbre.value = smoothT;
    gstate->color.value = smoothC;
    gstate->color.value = smoothC;

    // Zero out all CV-related variables
    // gstate->timbre.value = 0.0f;
    // gstate->color_mod.value = 0.0f;
    // gstate->fm_mod = 0.0f;

    gstate->timbre.locked = false;
    gstate->color.locked = false;
    gstate->resonance.locked = false;
    gstate->cutoff.locked = false;
    SCHEDULE_REFRESH(gstate);
  }
}
