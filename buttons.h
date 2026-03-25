/*
  LISA (v0.0.1)

  Copyright (c) 2025 Dr Schlange
  Licensed under GNU GPLv3
*/
#pragma once
#include <pico/stdlib.h>
#include <Arduino.h>

#define BUTTON_DEBOUNCE_MS 200
#define ENCODER_DEBOUNCE_COUNT 4



enum EncoderState { ENGINE_SELECT,
                   VOLUME_ADJUST,
                   ATTACK_ADJUST,
                   RELEASE_ADJUST,
                   FILTER_TOGGLE,
                   MIDI_MOD,
                   CV_MOD1,
                   CV_MOD2,
                   MIDI_CH,
                   SCOPE_TOGGLE,
                   ENCODER_STATE_NUM };


struct Encoder {
  uint8_t clk;
  uint8_t dt;
  uint8_t sw;
  int8_t _count;
  uint8_t last_state;
  PinStatus sw_status;
  unsigned long last_sw_time;
  unsigned long last_encoder_activity;
  volatile EncoderState state;
};


#define EncoderNew(clk_, dt_, sw_) \
  { .clk = clk_, .dt = dt_, .sw = sw_, ._count = 0, .last_state = 0, .sw_status = HIGH, .last_sw_time = 0, .last_encoder_activity = 0, .state = ENGINE_SELECT  }


enum ResolutionMode {
  RES_RAW,
  RES_CATCHUP,
  RES_ATTENUATOR
};

enum PotMode {
  POT_NORMAL,
  POT_KINETIC
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


inline int8_t encoder_decode_step(Encoder *encoder) {
  const uint8_t state = (digitalRead(encoder->clk) << 1) | digitalRead(encoder->dt);
  const uint8_t combined = (encoder->last_state << 2) | state;
  encoder->last_state = state;

  // Tried all the tricks to deal with my cheap encoder...
  switch (combined) {
    case 0b0001:
    case 0b0111:
    case 0b1110:
    case 0b1000:
      if (encoder->_count < 0) encoder->_count = 0;
      encoder->_count++;
      break;
    case 0b0010:
    case 0b1011:
    case 0b1101:
    case 0b0100:
      if (encoder->_count > 0) encoder->_count = 0;
      encoder->_count--;
      break;
  }
  if (encoder->_count >= ENCODER_DEBOUNCE_COUNT) {
    encoder->_count = 0;
    return -1;
  } else if (encoder->_count <= -ENCODER_DEBOUNCE_COUNT) {
    encoder->_count = 0;
    return 1;
  }
  return 0;
}

inline bool encoder_sw_pressed(Encoder *encoder) {
  const PinStatus last = encoder->sw_status;
  const PinStatus current = digitalRead(encoder->sw);
  encoder->sw_status = current;

  if (last == HIGH && current == LOW && (millis() - encoder->last_sw_time) > BUTTON_DEBOUNCE_MS) {
    encoder->last_sw_time = millis();
    encoder->last_encoder_activity = millis();
    return true;
  }
  return false;
}
