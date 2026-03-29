/*
  LISA (v0.0.1)

  Copyright (c) 2026 Dr Schlange
  Licensed under GNU GPLv3
*/
#pragma once
#include <pico/stdlib.h>
#include <Arduino.h>
#include "constants_config.h"


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

enum SwState {
  SW_IDLE,
  SW_DEBOUNCING_DOWN,
  SW_PRESSED_DOWN,
  SW_DEBOUNCING_UP,
  SW_RELEASED,
  SW_WAIT_RELEASE
};

enum EncoderStatus {
  NO_ACTION,
  PRESSED,
  DBL_PRESSED,
  LONG_PRESSED
};



struct Encoder {
  uint8_t clk;
  uint8_t dt;
  uint8_t sw;
  int8_t _count;
  uint8_t last_state;
  SwState sw_state;
  uint8_t click_count;
  bool longpress_handled;
  unsigned long last_debounce_time;
  unsigned long press_time;
  unsigned long release_time;
  unsigned long last_encoder_activity;
  volatile EncoderState state;
};


#define EncoderNew(clk_, dt_, sw_) \
  { \
    .clk = clk_, \
    .dt = dt_, \
    .sw = sw_, \
    ._count = 0, \
    .last_state = 0, \
    .sw_state = SW_IDLE, \
    .click_count = 0, \
    .longpress_handled = false, \
    .last_debounce_time = 0, \
    .press_time = 0, \
    .release_time = 0, \
    .last_encoder_activity = 0, \
    .state = ENGINE_SELECT \
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
    encoder->last_encoder_activity = millis();
    return -1;
  } else if (encoder->_count <= -ENCODER_DEBOUNCE_COUNT) {
    encoder->_count = 0;
    encoder->last_encoder_activity = millis();
    return 1;
  }
  return 0;
}

static inline EncoderStatus encoder_sw_status(Encoder *encoder) {
  const PinStatus sw = digitalRead(encoder->sw);
  const unsigned long now = millis();
  const SwState state = encoder->sw_state;

  switch (state) {

    case SW_IDLE:
      if (sw == LOW) {
        encoder->sw_state = SW_DEBOUNCING_DOWN;
        encoder->last_debounce_time = now;
      }
      break;

    case SW_DEBOUNCING_DOWN:
      if (sw == HIGH) {
        encoder->sw_state = SW_IDLE;
      } else if (now - encoder->last_debounce_time >= ENCODER_DEBOUNCE_MS) {
        encoder->sw_state = SW_PRESSED_DOWN;
        encoder->press_time = now;
        encoder->longpress_handled = false;
      }
      break;

    case SW_PRESSED_DOWN:
      if (!encoder->longpress_handled
          && (now - encoder->press_time >= ENCODER_LONGPRESS_MS)) {
        encoder->longpress_handled = true;
        encoder->click_count = 0;
        encoder->sw_state = SW_WAIT_RELEASE;
        return LONG_PRESSED;
      }
      if (sw == HIGH) {
        encoder->sw_state = SW_DEBOUNCING_UP;
        encoder->last_debounce_time = now;
      }
      break;

    case SW_DEBOUNCING_UP:
      if (sw == LOW) {
        encoder->sw_state = SW_PRESSED_DOWN;
      } else if (now - encoder->last_debounce_time >= ENCODER_DEBOUNCE_MS) {
        encoder->sw_state = SW_RELEASED;
        encoder->release_time = now;
        if (!encoder->longpress_handled)
          encoder->click_count++;
      }
      break;

    case SW_RELEASED:
      if (sw == LOW) {
        encoder->sw_state = SW_DEBOUNCING_DOWN;
        encoder->last_debounce_time = now;
        break;
      }
      if (encoder->click_count == 2
          && (now - encoder->release_time) <= ENCODER_PRESS_MS) {
        encoder->click_count = 0;
        encoder->sw_state = SW_IDLE;
        encoder->last_encoder_activity = now;
        return DBL_PRESSED;
      }
      if (encoder->click_count == 1
          && (now - encoder->release_time) > ENCODER_PRESS_MS) {
        encoder->click_count = 0;
        encoder->sw_state = SW_IDLE;
        encoder->last_encoder_activity = now;
        return PRESSED;
      }
      break;

    case SW_WAIT_RELEASE:
      if (sw == HIGH) {
        encoder->sw_state = SW_IDLE;
      }
      break;
  }

  return NO_ACTION;
}
