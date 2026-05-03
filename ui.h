/*
  LISA (v0.0.1)

  Copyright (c) 2026 Dr Schlange
  Licensed under GNU GPLv3

  Based on VIJA by Vadims Maksimovs (ledlaux.github.com)
*/
#pragma once
// clang-format off
#include <pico/stdlib.h>
#include <Wire.h>
#include "global_state.h"
#include "constants_config.h"
#include "wavetable_streaming.h"
#include "braids/settings.h"
// clang-format on

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
    0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000111, 0b10000000,
    0b00000011, 0b11000000, 0b00000100, 0b01110000, 0b00011100, 0b01000000,
    0b00000100, 0b00001000, 0b00100000, 0b01000000, 0b00000100, 0b00000000,
    0b00000000, 0b01000000, 0b00000110, 0b00000111, 0b11000000, 0b11000000,
    0b00000011, 0b00010000, 0b00010001, 0b10000000, 0b00000001, 0b11100000,
    0b00001111, 0b00000000, 0b00000000, 0b01111000, 0b00111100, 0b00000000,
    0b00000000, 0b00110000, 0b00011000, 0b00000000, 0b00000000, 0b10000011,
    0b10000010, 0b00000000, 0b00000000, 0b10100001, 0b00001010, 0b00000000,
    0b00000000, 0b00100000, 0b00001000, 0b00000000, 0b00000000, 0b00001010,
    0b10100000, 0b00000000, 0b00000000, 0b00010111, 0b11010000, 0b00000000,
    0b00000000, 0b00001000, 0b00100000, 0b00000000};

struct UIState {
  int last_engine_draw;
  unsigned long last_draw_time;
  volatile bool scope_ready;
  volatile int16_t scope_buffer_front[SCOPE_WIDTH];
  volatile int16_t scope_buffer_back[SCOPE_WIDTH];
};

#define UIStateNew()                                                           \
  {.last_engine_draw = -1,                                                     \
   .last_draw_time = 0,                                                        \
   .scope_ready = false,                                                       \
   .scope_buffer_front = {0},                                                  \
   .scope_buffer_back = {0}}

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
  display.drawBitmap((128 - 32) / 2, 0, lisa_logo_bitmap, 32, 16, SCREEN_WHITE);

  const char *title = "LISA";
  display.setTextSize(2);
  display.setTextColor(SCREEN_WHITE);
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
  if (!uistate->scope_ready)
    return;

  display.clearDisplay();

  for (int i = 0; i < SCOPE_WIDTH - 1; i++) {
    int16_t y1 = 40 - ((uistate->scope_buffer_back[i] * 150) >> 15);
    int16_t y2 = 40 - ((uistate->scope_buffer_back[i + 1] * 150) >> 15);
    y1 = constrain(y1, 0, 63);
    y2 = constrain(y2, 0, 63);
    display.drawLine(i, y1, i + 1, y2, SCREEN_WHITE);
  }

  display.display();
  uistate->scope_ready = false;
}

inline void draw_live_scope(UIState *uistate) {
  if (!uistate->scope_ready)
    return;
  display.clearDisplay();

  // Upper half
  int16_t bufs[4][257];
  WavetableStreamingOscillator::CopyBuffers(bufs);

  for (int b = 0; b < 4; b++) {
    int xoff = b * 32;
    for (int i = 0; i < 31; i++) {

      int16_t s1 = bufs[b][i * 8];
      int16_t s2 = bufs[b][(i + 1) * 8];

      int16_t y1 = 15 - (s1 >> 11);
      int16_t y2 = 15 - (s2 >> 11);

      display.drawLine(xoff + i, y1, xoff + i + 1, y2, SCREEN_WHITE);
    }
  }

  const uint8_t dash_height = 3;
  const uint8_t gap_height = 3;
  const uint8_t line_length = 32;
  for (int b = 1; b < 4; b++) {
    for (int y = 0; y < 32; y += (dash_height + gap_height)) {
      int16_t current_dash = min(dash_height, line_length - y);
      display.drawFastVLine(b * 32, y, current_dash, SCREEN_WHITE);
    }
  }

  // Lower half
  for (int i = 0; i < SCOPE_WIDTH - 1; i++) {
    int16_t y1 = 48 - ((uistate->scope_buffer_back[i] * 100) >> 15);
    int16_t y2 = 48 - ((uistate->scope_buffer_back[i + 1] * 100) >> 15);
    y1 = constrain(y1, 32, 63);
    y2 = constrain(y2, 32, 63);
    display.drawLine(i, y1, i + 1, y2, SCREEN_WHITE);
  }

  display.display();
  uistate->scope_ready = false;
}

void draw_engine_ui(RuntimeState *gstate, UIState *uistate) {
  if (gstate->show_saved_flag)
    return; // Don't redraw while saving
  display.clearDisplay();
  int16_t x1, y1;
  uint16_t w, h;

  const char *name = engine_names[gstate->engine_idx];
  display.setTextSize(4);
  display.setTextColor(SCREEN_WHITE);

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
  case VOLUME_ADJUST:
    sprintf(menuBuf, "VOL:%3d", int(gstate->master_volume.value * 100));
    break;
  case ATTACK_ADJUST:
    sprintf(menuBuf, "A:%.2f", gstate->env_attack.value);
    break;
  case RELEASE_ADJUST:
    sprintf(menuBuf, "R:%.2f", gstate->env_release.value);
    break;
  case FILTER_TOGGLE:
    sprintf(menuBuf, "FLT:%s", gstate->filter_enabled ? "ON" : "OFF");
    break;
  case CV_MOD1:
    sprintf(menuBuf, "CV1:%s", gstate->cv_mod1_enabled ? "ON" : "OFF");
    break;
  case CV_MOD2:
    sprintf(menuBuf, "CV2:%s", gstate->cv_mod2_enabled ? "ON" : "OFF");
    break;
  case MIDI_MOD:
    sprintf(menuBuf, "MIDI:%s", gstate->midi_enabled ? "ON" : "OFF");
    break;
  case MIDI_CH:
    sprintf(menuBuf, "MIDICH:%d", gstate->midi_ch);
    SCHEDULE_REFRESH(gstate);
    break;
  case SCOPE_TOGGLE:
    sprintf(menuBuf, "SCOPE:%s", gstate->oscilloscope_enabled ? "ON" : "OFF");
    break;
  default:
    if (gstate->timbre.locked && gstate->color.locked)
      strcpy(menuBuf, "ALL-MIDI");
    else if (gstate->timbre.locked)
      strcpy(menuBuf, "T-MIDI");
    else if (gstate->color.locked)
      strcpy(menuBuf, "C-MIDI");
    else
      strcpy(menuBuf, "");
    break;
  }
  if (menuBuf[0] != '\0') {
    display.setCursor(0, 55);
    display.print(menuBuf);
  }

  if (!gstate->cv_mod1_enabled) {
    char buf[16];
    int tVal = gstate->timbre.value * 127;
    int cVal = gstate->color.value * 127;
    sprintf(buf, "T:%3d C:%3d", tVal, cVal);

    display.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
    display.setCursor(128 - w - 2, 55);
    display.print(buf);
  }
  display.display();
}

static inline void invert_rect(int x, int y, int w, int h) {
  for (int j = y; j < y + h; j++) {
    for (int i = x; i < x + w; i++) {
      uint16_t color = display.getPixel(i, j);
      display.drawPixel(i, j, color > 0 ? 0 : 1);
    }
  }
}

static inline void draw_param(uint8_t x, uint8_t y, const char *name,
                              Parameter *p) {
  display.setCursor(x, y);
  if (p == NULL) {
    display.print(" - ");
    return;
  }
  display.print(name);
  display.drawRect(x - 3, y - 1, 23, 10, p->screen_locked ? 0 : 1);
  invert_rect(x - 2, y, p->value * 21, 8);
}

static inline void draw_all_parameters(UIState *uistate, RuntimeState *gstate) {
  uint8_t row = gstate->pots_row_state;
  display.clearDisplay();

  display.setTextSize(1);
  display.setTextColor(SCREEN_WHITE);

  display.setTextWrap(false);
  display.setCursor(60, 1);
  display.print("A");

  display.setCursor(85, 1);
  display.print("B");

  display.setCursor(109, 1);
  display.print("C");

  // Pointer
  display.setCursor(1, row * 11);
  display.print(">");

  // Engine name
  display.setCursor(12 + 10, 1);
  display.print(engine_names[gstate->engine_idx]);

  // General
  display.setCursor(12, 11);
  display.print("gnrl");
  draw_param(54, 10, "vol", (Parameter *)&(gstate->master_volume));
  draw_param(79, 10, "b1", (Parameter *)&(gstate->b1));
  draw_param(104, 10, "b2", (Parameter *)&(gstate->b2));

  // timbre
  display.setCursor(12, 22);
  display.print("tmbr");
  draw_param(54, 21, "amt", (Parameter *)&(gstate->timbre));
  draw_param(79, 21, "mod", (Parameter *)&(gstate->timbre_mod));
  draw_param(104, 21, "fm", (Parameter *)&(gstate->fm_mod));

  // color
  display.setCursor(12, 33);
  display.print("colr");
  draw_param(54, 32, "amt", (Parameter *)&(gstate->color));
  draw_param(79, 32, "mod", (Parameter *)&(gstate->color_mod));
  draw_param(104, 32, "b3", (Parameter *)&(gstate->b3));

  // filter
  display.setCursor(12, 44);
  display.print("fltr");
  draw_param(54, 43, "ctf", (Parameter *)&(gstate->cutoff));
  draw_param(79, 43, "res", (Parameter *)&(gstate->resonance));
  draw_param(104, 43, "typ", (Parameter *)&(gstate->filter_type));

  // Envelope
  display.setCursor(12, 54);
  display.print("envl");
  draw_param(54, 54, "atk", (Parameter *)&(gstate->env_attack));
  draw_param(79, 54, "rel", (Parameter *)&(gstate->env_release));
  draw_param(104, 54, "b4", (Parameter *)&(gstate->b4));

  display.display();
}

static inline void draw_global_settings(UIState *uistate,
                                        RuntimeState *gstate) {
  uint8_t col_offset = gstate->glob_settings_state > SETTING_NUM ? 1 : 0;
  uint8_t row =
      (((uint8_t)gstate->glob_settings_state) - col_offset) % (SETTING_NUM);
  display.clearDisplay();

  display.setTextSize(1);
  display.setTextColor(SCREEN_WHITE);
  display.setTextWrap(false);

  // Pointer
  display.setCursor(1 + (col_offset * 59), (row) * 11);
  display.print(">");

  // Parameter
  display.setCursor(12, 1);
  display.print("param");
  display.setCursor(70, 1);
  if (gstate->glob_settings_edit_param == NULL) {
    display.print("ALL");
  } else {
    // get the parameter offset
    uint8_t offset = (gstate->glob_settings_edit_param) - (&(gstate->timbre));
    display.print(all_parameters[offset]);
  }

  // Resolution mode
  display.setCursor(12, 11);
  display.print("resol");
  display.setCursor(70, 11);
  const ResolutionMode res_mode = glob_get_res_mode(gstate);
  if (res_mode == RES_UNKNOWN) {
    display.print("--");
  } else {
    display.print(res_mode == RES_CATCHUP ? "catchup" : "raw");
  }

  // Mode row
  display.setCursor(12, 22);
  display.print("mode");
  display.setCursor(70, 22);
  const PotMode pot_mode = glob_get_pot_mode(gstate);
  if (pot_mode == POT_UNKNOWN) {
    display.print("--");
  } else {
    display.print(modes[pot_mode]);
  }

  // Kinetic parameters
  if (pot_mode == POT_KINETIC) {
    ExtParameter *param = gstate->glob_settings_edit_param;
    if (param == NULL) {
      draw_param(14 + 10, 44, "mas",
                 (Parameter *)&(gstate->timbre.kinetic.mass));
      display.setCursor(20 + 10, 55);
      display.print("A");
      draw_param(43 + 10, 44, "dmp",
                 (Parameter *)&(gstate->timbre.kinetic.damping));
      display.setCursor(49 + 10, 55);
      display.print("B");
      draw_param(72 + 10, 44, "stf",
                 (Parameter *)&(gstate->timbre.kinetic.stiffness));
      display.setCursor(78 + 10, 55);
      display.print("C");
    } else {
      draw_param(14 + 10, 44, "mas", (Parameter *)&(param->kinetic.mass));
      display.setCursor(20 + 10, 55);
      display.print("A");
      draw_param(43 + 10, 44, "dmp", (Parameter *)&(param->kinetic.damping));
      display.setCursor(49 + 10, 55);
      display.print("B");
      draw_param(72 + 10, 44, "stf", (Parameter *)&(param->kinetic.stiffness));
      display.setCursor(78 + 10, 55);
      display.print("C");
    }
  }

  display.display();
}

static inline bool some_parameter_changed(RuntimeState *gstate) {
  Parameter *p = (Parameter *)&(gstate->timbre);
  for (int i = 0; i < ALL_PARAMETERS_NUM; i++) {
    if (p[i].last_value != p[i].value) {
      return true;
    }
  }
  return false;
}

static inline void draw_ui(RuntimeState *gstate, UIState *uistate) {
  if (millis() - uistate->last_draw_time > SCREEN_REFRESH_TIME) {
    uistate->last_draw_time = millis();
    unsigned long idle = millis() - gstate->encoder.last_encoder_activity;

    if (gstate->display_state == ENGINE_SETTINGS_CONFIG &&
        idle > IDLETIME_BEFORE_ENGINE_SELECT_MS) {
      gstate->display_state = ENGINE_SELECT_MODE;
      gstate->encoder.state = ENGINE_SELECT;
      SCHEDULE_REFRESH(gstate);
      uistate->last_engine_draw = -1;
    } else if (gstate->display_state == ENGINE_SELECT_MODE &&
               idle > IDLETIME_BEFORE_SCOPE_DISPLAY_MS &&
               gstate->oscilloscope_enabled) {
      gstate->display_state = OSCILLOSCOPE_MODE;
      SCHEDULE_REFRESH(gstate);
      uistate->last_engine_draw = -1;
    }
    switch (gstate->display_state) {
    case OSCILLOSCOPE_MODE:
      if (gstate->engine_idx >= braids::MACRO_OSC_SHAPE_LAST)
        draw_live_scope(uistate);
      else
        draw_scope(uistate);
      break;
    case ENGINE_SELECT_MODE:
    case ENGINE_SETTINGS_CONFIG:
      if (REFRESH_IS_SCHEDULED(gstate) ||
          gstate->engine_idx != uistate->last_engine_draw) {
        draw_engine_ui(gstate, uistate);
        uistate->last_engine_draw = gstate->engine_idx;
        gstate->engine_updated = false;
      }
      break;
    case ALL_PARAMS_MODE:
      if (REFRESH_IS_SCHEDULED(gstate)) {
        draw_all_parameters(uistate, gstate);
        gstate->engine_updated = false;
      }
      break;
    case GLOBAL_SETTINGS:
      if (REFRESH_IS_SCHEDULED(gstate)) {
        draw_global_settings(uistate, gstate);
        gstate->engine_updated = false;
      }
      break;
    }
  }
}

static inline void saved_feedback(RuntimeState *gstate) {
  if (!gstate->show_saved_flag)
    return;

  unsigned long now = millis();
  if (now - gstate->saved_start_time < SAVED_DISPLAY_MS) {
    gstate->oscilloscope_enabled =
        false; // we remove the scope the time to draw the message
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SCREEN_WHITE);

    const char *msg = "Saved!";
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((128 - w) / 2, (64 - h) / 2);
    display.println(msg);
    display.display();
  } else {
    gstate->show_saved_flag = false;
    gstate->oscilloscope_enabled = true; // we reactivate the scope
    SCHEDULE_REFRESH(gstate);
  }
}

static inline void scope_fill(UIState *uistate, int32_t *mix, bool enabled) {
  static int scope_idx = 0;
  static int32_t scopeSmooth = 0;
  if (!enabled || uistate->scope_ready)
    return;
  const int32_t factor = (int32_t)(0.25f * 32767.f);
  for (int i = 0; i < AUDIO_BLOCK; i += 4) {
    scopeSmooth += ((mix[i] - scopeSmooth) * factor) >> 15;
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
