/*
  LISA (v0.0.1)

  Copyright (c) 2026 Dr Schlange
  Licensed under GNU GPLv3

  Based on VIJA by Vadims Maksimovs (ledlaux.github.com)
*/
#pragma once
#include <pico/stdlib.h>
#include <Wire.h>
#include "global_state.h"
#include "constants_config.h"

#if SSD1306
#include <Adafruit_SSD1306.h>
#define SCREEN_WHITE SSD1306_WHITE
#endif

#if SH110X
#include <Adafruit_SH110X.h>
#define SCREEN_WHITE SH110X_WHITE
#endif


// Splash screen
const uint8_t lisa_logo_bitmap[] PROGMEM = {
  0b00000000, 0b00000000, 0b00000000, 0b00000000,
  0b00000111, 0b10000000, 0b00000011, 0b11000000,
  0b00000100, 0b01110000, 0b00011100, 0b01000000,
  0b00000100, 0b00001000, 0b00100000, 0b01000000,
  0b00000100, 0b00000000, 0b00000000, 0b01000000,
  0b00000110, 0b00000111, 0b11000000, 0b11000000,
  0b00000011, 0b00010000, 0b00010001, 0b10000000,
  0b00000001, 0b11100000, 0b00001111, 0b00000000,
  0b00000000, 0b01111000, 0b00111100, 0b00000000,
  0b00000000, 0b00110000, 0b00011000, 0b00000000,
  0b00000000, 0b10000011, 0b10000010, 0b00000000,
  0b00000000, 0b10100001, 0b00001010, 0b00000000,
  0b00000000, 0b00100000, 0b00001000, 0b00000000,
  0b00000000, 0b00001010, 0b10100000, 0b00000000,
  0b00000000, 0b00010111, 0b11010000, 0b00000000,
  0b00000000, 0b00001000, 0b00100000, 0b00000000
};


struct UIState {
  int last_engine_draw;
  unsigned long last_draw_time;
  volatile bool scope_ready;
  volatile float scope_buffer[SCOPE_WIDTH];
  volatile float scope_buffer_front[SCOPE_WIDTH];
  volatile float scope_buffer_back[SCOPE_WIDTH];
};

#define UIStateNew() \
  { \
    .last_engine_draw = -1, .last_draw_time = 0, .scope_ready = false, .scope_buffer = { 0 }, .scope_buffer_front = { 0 }, .scope_buffer_back = { 0 } \
  }


#if USE_SCREEN

#if SSD1306
Adafruit_SSD1306 display(128, 64, &Wire, -1);
#endif

#if SH110X
Adafruit_SH1106G display = Adafruit_SH1106G(128, 64, &Wire, -1);
#endif

void draw_splash() {
  int16_t x1, y1;
  uint16_t w, h;

  display.clearDisplay();
  display.drawBitmap((128 - 32) / 2, 0, lisa_logo_bitmap, 32, 16, SSD1306_WHITE);

  const char *title = "LISA";
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((128 - w) / 2, 18);
  display.println(title);

  const char *subtitle = "synthesizer";
  display.setTextSize(1);
  display.getTextBounds(subtitle, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((128 - w) / 2, 40);
  display.println(subtitle);

  const char *version = LISA_VERSION;
  display.getTextBounds(version, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((128 - w) / 2, 54);
  display.println(version);
  display.display();
}

static inline void setup_display() {
  Wire.setSDA(OLED_SDA);
  Wire.setSCL(OLED_SCL);
  Wire.begin();
  Wire.setClock(400000);

#if SSD1306
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
#endif
#ifdef SH110X
  display.begin(0x3C, true);
#endif
  draw_splash();
  delay(4000);
  display.clearDisplay();
  display.display();
}

inline void draw_scope(UIState *uistate) {
  if (!uistate->scope_ready) return;

  display.clearDisplay();

  const float midY = 40.0f;
  const float current_gain = 150.0f;

  for (int i = 0; i < SCOPE_WIDTH - 1; i++) {
    int16_t y1 = (int16_t)(midY - (uistate->scope_buffer_back[i] * current_gain));
    int16_t y2 = (int16_t)(midY - (uistate->scope_buffer_back[i + 1] * current_gain));
    if (y1 < 0) y1 = 0;
    if (y1 > 63) y1 = 63;
    if (y2 < 0) y2 = 0;
    if (y2 > 63) y2 = 63;
    display.drawLine(i, y1, i + 1, y2, SSD1306_WHITE);
  }

  display.display();
  uistate->scope_ready = false;
}

void draw_engine_ui(RuntimeState *gstate, UIState *uistate) {
  if (gstate->show_saved_flag) return;  // Don't redraw while saving
  display.clearDisplay();
  int16_t x1, y1;
  uint16_t w, h;

  const char *name = engine_names[gstate->engine_idx];
  display.setTextSize(4);
  display.setTextColor(SSD1306_WHITE);

  char idxBuf[8];
  sprintf(idxBuf, "%d", gstate->engine_idx + 1);
  display.setTextSize(2);
  display.setCursor(0, 15);
  display.print(idxBuf);

  display.setTextSize(4);
  display.getTextBounds(name, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(128 - w - 2, 15);
  display.println(name);

  display.setTextSize(1);
  char menuBuf[32] = "";
  switch (gstate->encoder.state) {
    case VOLUME_ADJUST: sprintf(menuBuf, "VOL:%3d", int(gstate->master_volume.value * 100)); break;
    case ATTACK_ADJUST: sprintf(menuBuf, "A:%.2f", gstate->env_attack.value); break;
    case RELEASE_ADJUST: sprintf(menuBuf, "R:%.2f", gstate->env_release.value); break;
    case FILTER_TOGGLE: sprintf(menuBuf, "FLT:%s", gstate->filter_enabled ? "ON" : "OFF"); break;
    case CV_MOD1: sprintf(menuBuf, "CV1:%s", gstate->cv_mod1 ? "ON" : "OFF"); break;
    case CV_MOD2: sprintf(menuBuf, "CV2:%s", gstate->cv_mod2 ? "ON" : "OFF"); break;
    case MIDI_MOD: sprintf(menuBuf, "MIDI:%s", gstate->midi_enabled ? "ON" : "OFF"); break;
    case MIDI_CH:
      sprintf(menuBuf, "MIDICH:%d", gstate->midi_ch);
      SCHEDULE_REFRESH(gstate);
      break;
    case SCOPE_TOGGLE: sprintf(menuBuf, "SCOPE:%s", gstate->oscilloscope_enabled ? "ON" : "OFF"); break;
    default:
      if (gstate->timbre.locked && gstate->color.locked) strcpy(menuBuf, "ALL-MIDI");
      else if (gstate->timbre.locked) strcpy(menuBuf, "T-MIDI");
      else if (gstate->color.locked) strcpy(menuBuf, "C-MIDI");
      else strcpy(menuBuf, "");
      break;
  }
  if (menuBuf[0] != '\0') {
    display.setCursor(0, 55);
    display.print(menuBuf);
  }

  if (!gstate->cv_mod1) {
    char buf[16];
    int tVal = int((gstate->timbre.locked ? gstate->timbre.value : gstate->pot_timbre) * 127);
    int mVal = int((gstate->color.locked ? gstate->color.value : gstate->pot_color) * 127);
    sprintf(buf, "T:%3d C:%3d", tVal, mVal);

    display.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
    display.setCursor(128 - w - 2, 55);
    display.print(buf);
  }
  display.display();
}



static inline void draw_ui(RuntimeState *gstate, UIState *uistate) {
  if (millis() - uistate->last_draw_time > SCREEN_REFRESH_TIME) {
    uistate->last_draw_time = millis();
    unsigned long idle = millis() - gstate->encoder.last_encoder_activity;

    if (gstate->display_state == ENGINE_SETTINGS_CONFIG && idle > IDLETIME_BEFORE_ENGINE_SELECT_MS) {
      gstate->display_state = ENGINE_SELECT_MODE;
      gstate->encoder.state = ENGINE_SELECT;
      SCHEDULE_REFRESH(gstate);
      uistate->last_engine_draw = -1;
    } else if (gstate->display_state == ENGINE_SELECT_MODE && idle > IDLETIME_BEFORE_SCOPE_DISPLAY_MS && gstate->oscilloscope_enabled) {
      gstate->display_state = OSCILLOSCOPE_MODE;
      SCHEDULE_REFRESH(gstate);
      uistate->last_engine_draw = -1;
    }
    switch (gstate->display_state) {
      case OSCILLOSCOPE_MODE:
        draw_scope(uistate);
        break;
      case ENGINE_SELECT_MODE:
      case ENGINE_SETTINGS_CONFIG:
        if (REFRESH_IS_SCHEDULED(gstate) || gstate->engine_idx != uistate->last_engine_draw) {
          draw_engine_ui(gstate, uistate);
          uistate->last_engine_draw = gstate->engine_idx;
          gstate->engine_updated = false;
        }
        break;
    }
  }
}

void check_saved_feedback(RuntimeState *gstate) {
  if (!gstate->show_saved_flag) return;

  unsigned long now = millis();
  if (now - gstate->saved_start_time < SAVED_DISPLAY_MS) {
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);

    const char *msg = "Saved!";
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((128 - w) / 2, (64 - h) / 2);
    display.println(msg);
    display.display();
  } else {
    gstate->show_saved_flag = false;
    SCHEDULE_REFRESH(gstate);
  }
}


static inline void scope_fill(UIState *uistate, float *mix, bool enabled) {
  static int scope_idx = 0;
  static float scopeSmooth = 0.0f;
  if (!enabled || uistate->scope_ready) return;
  for (int i = 0; i < AUDIO_BLOCK; i += 4) {
    scopeSmooth += (mix[i] - scopeSmooth) * 0.25f;
    uistate->scope_buffer_front[scope_idx++] = scopeSmooth;
    if (scope_idx >= SCOPE_WIDTH) {
      memcpy((void *)uistate->scope_buffer_back,
             (const void *)uistate->scope_buffer_front,
             sizeof(uistate->scope_buffer_back));
      uistate->scope_ready = true;
      scope_idx = 0;
      break;
    }
  }
}
#endif
