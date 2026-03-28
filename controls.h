/*
  LISA (v0.0.1)

  Copyright (c) 2025 Dr Schlange
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

static inline void handle_pot_parameter(Parameter *param, RuntimeState *gstate) {
  const uint8_t old_value = param->last_value;
  float new_value = analogRead(param->gpio) / 1023.f;

  // smoothing the noise
  param->smoothed += (new_value - param->smoothed) * 0.15f;
  // quantizing for comparison
  uint8_t quantized = (uint8_t)(param->smoothed * 127.f);
  if (quantized != old_value) {
    param->last_value = quantized;

    const MidiControllerMode mode = gstate->controller_mode;

    // RAW
    set_parameter_(param, param->smoothed, mode);
    midi_cc_forward_(param->midi_cc, quantized, gstate->midi_ch, mode);
    // catchup!
    // if (fabsf(smoothT - gstate->timbre_in) < 0.02f) {
    //   gstate->timbre.locked = false;
    // }
  }

  SCHEDULE_REFRESH(gstate);
}
