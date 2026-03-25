
/*
  LISA (v0.0.1)

  Original work on VIJA:
  Copyright (c) 2025 Vadims Maksimovs

  Modifications:
  Copyright (c) 2026 Dr Schlange

  This file has been modified from the original version VIJA v1.0.2.
  Licensed under GNU GPLv3.

  Raspberry PICO polyphonic synthesizer based on Mutable Instruments Braids macro oscillator
  in semi-modular format.

  Features:
  - 40+ digital oscillator engines
  - Polyphonic, per-sample AR envelopes
  - USB or UART MIDI input
  - Filter (SVF)
  - OLED display with menu system & oscilloscope
  - Synth controls via potentiometers or MIDI CC

  Hardware:
  - RP2040 or RP2350 board, I2S PCM5102 DAC, SSD1306 OLED, rotary encoder with button, 2 pots, 2 cv jacks or 2 more pots
  - MIDI via USB or UART

  For this project I use RP2040 Zero model, so adjust GPIO numbers to your board.

  Compilation:

  RP2040: - Optimize: Optimize Even More (-O3)
          - CPU Speed: 200-240mhz (Overclock) depending on the sample rate and needed voice count
          - Sample rate: 32000 (4 voices) / 44100 (3 voices)
  RP2350:
         - Optimize: Optimize Even More (-O3)
         - Sample rate: 48000

  Software:
  - BRAIDS and STMLIB libraries ported by Mark Washeim:
    https://github.com/poetaster/arduinoMI
    MIT License

  - stmlib, braids source libs
    Copyright (c) 2020 (emilie.o.gillet@gmail.com)
    MIT License
*/

#include <Arduino.h>
#include <I2S.h>
#include <Adafruit_TinyUSB.h>
#include <STMLIB.h>
#include <BRAIDS.h>
#include <pico/stdlib.h>
#include <Wire.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "buttons.h"

#define LISA_VERSION "v0.0.1"

#define DEBUG false
#if DEBUG
#define DEBUG_PRINTLN(...) Serial.println(__VA_ARGS__)
#else
#define DEBUG_PRINTLN(...)
#endif

#define SSD1306 1  //ssd1306 display
// #define SH110X 1// sh110x display

#if SSD1306
#include <Adafruit_SSD1306.h>
#define SCREEN_WHITE SSD1306_WHITE
#endif

#if SH110X
#include <Adafruit_SH110X.h>
#define SCREEN_WHITE SH110X_WHITE
#endif

#define I2S_DATA_PIN 9
#define I2S_BCLK_PIN 10
#define SAMPLE_RATE 32000
#define AUDIO_BLOCK 32
#define MAX_VOICES 4

#define INIT_CUTOFF (32767 / 4)
#define INIT_RESONANCE (32767 / 2)

#define USE_POTS 1
#define POT_TIMBRE A0      // GPIO26
#define POT_COLOR A1       // GPIO27
#define POT_TIMBRE_MOD A2  // GPIO28
#define POT_COLOR_MOD A3   // GPIO29
#define HAS_A3 0           // RPI Pico W doen't have GPIO29

#define ENCODER_CLK 2
#define ENCODER_DT 3
#define ENCODER_SW 4
// #define BUTTON_DEBOUNCE_MS 200
#define LONG_PRESS_MS 1000

#define USE_UART_MIDI 0  // 0 = USB MIDI, 1 = UART MIDI
#define MIDI_UART_RX 13

#define USE_SCREEN 1
#define OLED_SDA 0
#define OLED_SCL 1
#define SCOPE_WIDTH 128

#define SCREEN_REFRESH_TIME 60
#define IDLETIME_BEFORE_ENGINE_SELECT_MS 5000
#define IDLETIME_BEFORE_SCOPE_DISPLAY_MS 10000
#define SAVED_DISPLAY_MS 800

// MIDI stuffs
#define IS_MIDI_NOTE_OFF(status, value) (((status & 0xF0) == 0x80) || ((status & 0xF0) == 0x90 && value == 0))
#define IS_MIDI_NOTE_ON(status) ((status & 0xF0) == 0x90)
#define IS_MIDI_CC(status) ((status & 0xF0) == 0xB0)


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


const char *SETTINGS_FILE = "/lisa_settings.json";

enum DisplayMode { ENGINE_SELECT_MODE,
                   SETTINGS_MODE,
                   OSCILLOSCOPE_MODE };

// For UI updates
float pot_timbre = 0.5f;
float pot_color = 0.5f;

struct Voice {
  braids::MacroOscillator osc;
  int pitch;
  float velocity;
  float vel_smoothed;
  bool active;
  bool last_trig;
  float env;
  int16_t buffer[AUDIO_BLOCK];
  uint8_t sync_buffer[AUDIO_BLOCK];
  uint32_t age;
  bool sustained;
};



struct RuntimeState {
  volatile uint8_t midi_ch;
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
  volatile bool sustain_enabled;

  volatile bool engine_updated;
  volatile bool env_params_changed;
  unsigned long last_param_change;
  unsigned long last_midi_lock_time;

  volatile bool midi_mod;
  volatile bool cv_mod1;
  volatile bool cv_mod2;

  volatile bool timbre_locked;
  volatile bool color_locked;

  volatile bool filter_enabled;
  float filter_mix;
  volatile uint8_t filter_cutoff_cc;
  bool filter_midi_owned;
  volatile uint8_t filter_resonance_cc;

  bool show_saved_flag;
  unsigned long saved_start_time;

  volatile bool oscilloscope_enabled;
  volatile float scope_buffer_front[SCOPE_WIDTH];
  volatile float scope_buffer_back[SCOPE_WIDTH];

  DisplayMode display_mode;
  Encoder encoder;

  volatile bool system_ready;
#if USE_SCREEN
  int last_engine_draw = -1;
  unsigned long last_draw_time = 0;
  volatile float scope_buffer[SCOPE_WIDTH];
  volatile bool scope_ready;
#endif
};

static RuntimeState runtime_state = {
  .midi_ch = 1,
  .engine_idx = 1,
  .last_engine_idx = -1,
  .timbre_in = 0.4f,
  .color_in = 0.3f,
  .fm_mod = 0.0f,
  .timb_mod_midi = 0.0f,
  .color_mod_midi = 0.0f,
  .timb_mod_cv = 0.0f,
  .color_mod_cv = 0.0f,
  .fm_target = 0.0f,

  .master_volume = 0.7f,
  .env_attack_s = 0.009f,
  .env_release_s = 0.01f,
  .attackCoef = 0.0f,
  .releaseCoef = 0.0f,
  .sustain_enabled = false,

  .engine_updated = true,
  .env_params_changed = true,
  .last_param_change = 0,
  .last_midi_lock_time = 0,

  .midi_mod = true,
  .cv_mod1 = false,
  .cv_mod2 = false,

  .timbre_locked = false,
  .color_locked = false,

  .filter_enabled = true,
  .filter_mix = 1.0f,
  .filter_cutoff_cc = 64,
  .filter_midi_owned = false,
  .filter_resonance_cc = 32,

  .show_saved_flag = false,
  .saved_start_time = 0,

  .oscilloscope_enabled = true,
  .scope_buffer_front = { 0 },
  .scope_buffer_back = { 0 },

  .display_mode = ENGINE_SELECT_MODE,
  .encoder = EncoderNew(ENCODER_CLK, ENCODER_DT, ENCODER_SW),

  .system_ready = false,
#if USE_SCREEN
  .last_engine_draw = -1,
  .last_draw_time = 0,
  .scope_buffer = { 0 },
  .scope_ready = false,
#endif
};
#define ENGINE_UPDATED(runtime) (runtime)->engine_updated = true
#define SET_SYSTEM_READY(runtime) (runtime)->system_ready = true

// Snapshot/settings
typedef struct __attribute__((packed)) {
  float master_volume, env_attack_s, env_release_s;
  float timbre_in, color_in, timb_mod_cv, color_mod_cv;
  int engine_idx;
  uint8_t midi_ch;
  uint8_t filter_enabled, midi_mod, cv_mod1, cv_mod2;
  uint8_t oscilloscope_enabled;
  uint8_t enc_state;
} SettingsSnapshot;

static inline SettingsSnapshot snapshot_from(const RuntimeState *s) {
  return (SettingsSnapshot){
    s->master_volume, s->env_attack_s, s->env_release_s,
    s->timbre_in, s->color_in, s->timb_mod_cv, s->color_mod_cv,
    s->engine_idx, s->midi_ch,
    s->filter_enabled, s->midi_mod, s->cv_mod1, s->cv_mod2,
    s->oscilloscope_enabled, (uint8_t)s->encoder.state
  };
}
static SettingsSnapshot last_snapshot = snapshot_from(&runtime_state);

static Voice voices[MAX_VOICES];
uint32_t global_age = 0;

I2S i2s_output(OUTPUT);

braids::Svf global_filter;
Adafruit_USBD_MIDI usb_midi;

#if USE_SCREEN
#if SSD1306
Adafruit_SSD1306 display(128, 64, &Wire, -1);
#endif

#if SH110X
Adafruit_SH1106G display = Adafruit_SH1106G(128, 64, &Wire, -1);
#endif
#endif

const char *const engine_names[] = {
  "CSAW", "/\\-_", "//-_", "FOLD", "uuuu", "SUB-", "SUB/", "SYN-", "SYN/",
  "//x3", "-_x3", "/\\x3", "SIx3", "RING", "////", "//uu", "TOY*", "ZLPF", "ZPKF",
  "ZBPF", "ZHPF", "VOSM", "VOWL", "VFOF", "HARM", "-FM-", "FBFM", "WTFM",
  "PLUK", "BOWD", "BLOW", "FLUT", "BELL", "DRUM", "KICK", "CYMB", "SNAR",
  "WTBL", "WMAP", "WLIN", "WTx4", "NOIS", "TWNQ", "CLKN", "CLOU", "PRTC",
  "QPSK", "????"
};

constexpr int NUM_ENGINES = sizeof(engine_names) / sizeof(engine_names[0]);


int findFreeVoice() {
  int oldest = 0;
  uint32_t old_age = voices[0].age;
  for (int i = 0; i < MAX_VOICES; i++) {
    if (!voices[i].active && voices[i].env == 0.f)
      return i;
    if (voices[i].age < old_age) {
      old_age = voices[i].age;
      oldest = i;
    }
  }
  return oldest;
}


int findVoiceByPitch(int pitch) {
  for (int i = 0; i < MAX_VOICES; i++)
    if (voices[i].active && voices[i].pitch == pitch) return i;
  return -1;
}


void __not_in_flash_func(updateAudio)() {

  if (runtime_state.engine_idx != runtime_state.last_engine_idx) {
    braids::MacroOscillatorShape shape =
      (braids::MacroOscillatorShape)runtime_state.engine_idx;

    for (int v = 0; v < MAX_VOICES; v++)
      voices[v].osc.set_shape(shape);

    runtime_state.last_engine_idx = runtime_state.engine_idx;
  }

  if (runtime_state.env_params_changed) {
    runtime_state.attackCoef = 1.0f - expf(-1.0f / (SAMPLE_RATE * runtime_state.env_attack_s));
    runtime_state.releaseCoef = 1.0f - expf(-1.0f / (SAMPLE_RATE * runtime_state.env_release_s));
    runtime_state.env_params_changed = false;
  }

  float mix[AUDIO_BLOCK] = { 0 };

  static float fm_slew = 0.0f;
  static float timb_slew = 0.0f;
  static float color_slew = 0.0f;

  if (runtime_state.midi_mod) {
    runtime_state.fm_target = runtime_state.fm_mod;
  } else if (runtime_state.cv_mod1) {
    runtime_state.fm_target = 0.0f;
  } else if (runtime_state.filter_enabled) {
    runtime_state.fm_target = 0.0f;
  } else {
    runtime_state.fm_target = runtime_state.fm_mod;
  }

  float timb_target = runtime_state.midi_mod  ? runtime_state.timb_mod_midi
                      : runtime_state.cv_mod1 ? runtime_state.timb_mod_cv
                                              : 0.0f;

  float color_target = runtime_state.midi_mod  ? runtime_state.color_mod_midi
                       : runtime_state.cv_mod1 ? runtime_state.color_mod_cv
                                               : 0.0f;

  auto apply_stable_slew = [](float &current, float target, float coefficient) {
    float diff = target - current;
    float abs_diff = fabsf(diff);

    if (abs_diff < 0.005f) {
      if (target == 0.0f && abs_diff < 0.01f) current = 0.0f;
      return;
    }

    if (abs_diff < 0.001f) {
      current = target;
    } else {
      current += diff * coefficient;
    }
  };

  apply_stable_slew(fm_slew, runtime_state.fm_target, 0.05f);
  apply_stable_slew(timb_slew, timb_target, 0.01f);
  apply_stable_slew(color_slew, color_target, 0.01f);

  const float block_gain = runtime_state.master_volume * 0.25f;

  for (int v = 0; v < MAX_VOICES; v++) {
    Voice &voice = voices[v];

    if (!voice.active && !voice.sustained && voice.env < 0.0001f)
      continue;

    voice.vel_smoothed += (voice.velocity - voice.vel_smoothed) * 0.25f;

    float pitch = voice.pitch * 128.0f + fm_slew * 1536.0f;
    voice.osc.set_pitch(pitch);

    float t = constrain(runtime_state.timbre_in + timb_slew, 0.0f, 1.0f);
    float m = constrain(runtime_state.color_in + color_slew, 0.0f, 1.0f);
    voice.osc.set_parameters(t * 32767.0f, m * 32767.0f);

    if (voice.active && !voice.last_trig)
      voice.osc.Strike();

    voice.last_trig = voice.active;
    voice.osc.Render(voice.sync_buffer, voice.buffer, AUDIO_BLOCK);

    float envTarget = (voice.active || voice.sustained) ? 1.0f : 0.0f;
    float coef = envTarget ? runtime_state.attackCoef : runtime_state.releaseCoef;

    for (int i = 0; i < AUDIO_BLOCK; i++) {
      voice.env += (envTarget - voice.env) * coef;
      if (voice.env < 0.0001f) voice.env = 0.0f;

      mix[i] += (voice.buffer[i] * 0.000030517578125f) * (voice.env * voice.vel_smoothed * block_gain);
    }
  }


  static int scope_idx = 0;
  static float scopeSmooth = 0.0f;
  if (runtime_state.oscilloscope_enabled && !runtime_state.scope_ready) {
    for (int i = 0; i < AUDIO_BLOCK; i += 4) {
      scopeSmooth += (mix[i] - scopeSmooth) * 0.25f;
      runtime_state.scope_buffer_front[scope_idx++] = scopeSmooth;
      if (scope_idx >= SCOPE_WIDTH) {
        memcpy((void *)runtime_state.scope_buffer_back,
               (const void *)runtime_state.scope_buffer_front,
               sizeof(runtime_state.scope_buffer_back));
        runtime_state.scope_ready = true;
        scope_idx = 0;
        break;
      }
    }
  }

  static float cut_slew = 0.0f;
  static float res_slew = 0.0f;
  static float mix_slew = 0.0f;

  float cut_t = runtime_state.filter_cutoff_cc * (32767.0f / 127.0f);
  float res_t = runtime_state.filter_resonance_cc * (32767.0f / 127.0f);
  float mix_t = runtime_state.filter_enabled ? 1.0f : 0.0f;

  cut_slew += (cut_t - cut_slew) * 0.05f;
  res_slew += (res_t - res_slew) * 0.05f;
  mix_slew += (mix_t - mix_slew) * 0.01f;

  global_filter.set_frequency((uint16_t)cut_slew);
  global_filter.set_resonance((uint16_t)res_slew);

  const float dry_scale = (1.0f - mix_slew) * 32767.0f;
  const float wet_scale = mix_slew;

  for (int i = 0; i < AUDIO_BLOCK; i++) {
    float dry_f = mix[i];
    int32_t dry_int = (int32_t)(dry_f * 32767.0f);
    float wet_f = global_filter.Process(dry_int);
    float mixed_signal = (dry_f * dry_scale) + (wet_f * wet_scale);
    int16_t s = (int16_t)fmaxf(-32767.0f, fminf(32767.0f, mixed_signal));
    i2s_output.write16(s, s);
  }
}


void drawScope() {
#if USE_SCREEN
  if (!runtime_state.scope_ready) return;

  display.clearDisplay();

  const float midY = 40.0f;
  const float current_gain = 150.0f;

  for (int i = 0; i < SCOPE_WIDTH - 1; i++) {
    int16_t y1 = (int16_t)(midY - (runtime_state.scope_buffer_back[i] * current_gain));
    int16_t y2 = (int16_t)(midY - (runtime_state.scope_buffer_back[i + 1] * current_gain));
    if (y1 < 0) y1 = 0;
    if (y1 > 63) y1 = 63;
    if (y2 < 0) y2 = 0;
    if (y2 > 63) y2 = 63;
    display.drawLine(i, y1, i + 1, y2, SSD1306_WHITE);
  }

  display.display();
  runtime_state.scope_ready = false;
#endif
}


#if USE_SCREEN
void draw_engine_ui(RuntimeState *gstate) {
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
    case VOLUME_ADJUST: sprintf(menuBuf, "VOL:%3d", int(gstate->master_volume * 100)); break;
    case ATTACK_ADJUST: sprintf(menuBuf, "A:%.2f", gstate->env_attack_s); break;
    case RELEASE_ADJUST: sprintf(menuBuf, "R:%.2f", gstate->env_release_s); break;
    case FILTER_TOGGLE: sprintf(menuBuf, "FLT:%s", gstate->filter_enabled ? "ON" : "OFF"); break;
    case CV_MOD1: sprintf(menuBuf, "CV1:%s", gstate->cv_mod1 ? "ON" : "OFF"); break;
    case CV_MOD2: sprintf(menuBuf, "CV2:%s", gstate->cv_mod2 ? "ON" : "OFF"); break;
    case MIDI_MOD: sprintf(menuBuf, "MIDI:%s", gstate->midi_mod ? "ON" : "OFF"); break;
    case MIDI_CH:
      sprintf(menuBuf, "MIDICH:%d", gstate->midi_ch);
      ENGINE_UPDATED(gstate);
      break;
    case SCOPE_TOGGLE: sprintf(menuBuf, "SCOPE:%s", gstate->oscilloscope_enabled ? "ON" : "OFF"); break;
    default:
      if (gstate->timbre_locked && gstate->color_locked) strcpy(menuBuf, "ALL-MIDI");
      else if (gstate->timbre_locked) strcpy(menuBuf, "T-MIDI");
      else if (gstate->color_locked) strcpy(menuBuf, "C-MIDI");
      else strcpy(menuBuf, "");
      break;
  }
  if (menuBuf[0] != '\0') {
    display.setCursor(0, 55);
    display.print(menuBuf);
  }

  if (!gstate->cv_mod1) {
    char buf[16];
    int tVal = int((gstate->timbre_locked ? gstate->timbre_in : pot_timbre) * 127);
    int mVal = int((gstate->color_locked ? gstate->color_in : pot_color) * 127);
    sprintf(buf, "T:%3d C:%3d", tVal, mVal);

    display.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
    display.setCursor(128 - w - 2, 55);
    display.print(buf);
  }
  display.display();
}


void drawSplash() {
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


static inline void draw_ui(RuntimeState *gstate) {
  if (millis() - gstate->last_draw_time > SCREEN_REFRESH_TIME) {
    gstate->last_draw_time = millis();
    unsigned long idle = millis() - gstate->encoder.last_encoder_activity;

    if (gstate->display_mode == SETTINGS_MODE && idle > IDLETIME_BEFORE_ENGINE_SELECT_MS) {
      gstate->display_mode = ENGINE_SELECT_MODE;
      gstate->encoder.state = ENGINE_SELECT;
      ENGINE_UPDATED(gstate);
      gstate->last_engine_draw = -1;
    } else if (gstate->display_mode == ENGINE_SELECT_MODE && idle > IDLETIME_BEFORE_SCOPE_DISPLAY_MS && gstate->oscilloscope_enabled) {
      gstate->display_mode = OSCILLOSCOPE_MODE;
      ENGINE_UPDATED(gstate);
      gstate->last_engine_draw = -1;
    }
    switch (gstate->display_mode) {
      case OSCILLOSCOPE_MODE:
        drawScope();
        break;
      case ENGINE_SELECT_MODE:
      case SETTINGS_MODE:
        if (gstate->engine_updated || gstate->engine_idx != gstate->last_engine_draw) {
          draw_engine_ui(gstate);
          gstate->last_engine_draw = gstate->engine_idx;
          gstate->engine_updated = false;
        }
        break;
    }
  }
}
#else
static inline void draw_ui(const RuntimeState *state) {
  // Do nothing, it should be inlined/removed by the optimisation phase (hopefully)
}
#endif


void __not_in_flash_func(handle_MIDI)() {
  static uint8_t running_status = 0;
  static uint8_t data_bytes[2] = { 0 };
  static uint8_t data_idx = 0;

  uint8_t status = 0, pitch_or_cc = 0, cc_value = 0;
  bool has_msg = false;

#if USE_UART_MIDI
  if (Serial1.available() == 0) return;

  uint8_t byte = Serial1.read();

  if (byte >= 0xF8) return;

  if (byte & 0x80) {
    running_status = byte;
    data_idx = 0;
    return;
  }

  if (running_status == 0) return;
  if (data_idx < 2) data_bytes[data_idx++] = byte;
  uint8_t type = running_status & 0xF0;
  uint8_t expected_len = (type == 0xC0 || type == 0xD0) ? 1 : 2;

  if (data_idx < expected_len) return;

  status = running_status;
  pitch_or_cc = data_bytes[0];
  d2 = (expected_len == 2) ? data_bytes[1] : 0;
  data_idx = 0;
  has_msg = true;

  // --- Special CC64 sustain handling ---
  if (IS_MIDI_CC(status) && pitch_or_cc == 64) {
    if (d2 >= 64) {
      runtime_state.sustain_enabled = true;
    } else {
      runtime_state.sustain_enabled = false;
      for (int i = 0; i < MAX_VOICES; i++) {
        if (voices[i].sustained) {
          voices[i].active = false;
          voices[i].sustained = false;
        }
      }
    }
    return;
  }

#else  // USB MIDI
  uint8_t packet[4];
  if (!usb_midi.readPacket(packet)) return;

  uint8_t cin = packet[0] & 0x0F;
  if (cin < 0x8 || cin > 0xE) return;

  status = packet[1];
  pitch_or_cc = packet[2];
  cc_value = packet[3];
  has_msg = true;
#endif

  if (!has_msg) return;
  if ((status & 0x80) == 0) return;
  if ((status & 0x0F) != (runtime_state.midi_ch - 1)) return;

  if (IS_MIDI_NOTE_OFF(status, cc_value)) {
    int i = findVoiceByPitch(pitch_or_cc);
    if (i >= 0) {
      if (runtime_state.sustain_enabled) {
        voices[i].sustained = true;
        voices[i].active = false;
      } else {
        voices[i].active = false;
        voices[i].sustained = false;
      }
    }
  } else if (IS_MIDI_NOTE_ON(status)) {
    int i = findFreeVoice();
    voices[i].pitch = pitch_or_cc;
    voices[i].velocity = cc_value / 127.f;
    voices[i].active = true;
    voices[i].age = global_age++;
  } else if (IS_MIDI_CC(status)) {
    switch (pitch_or_cc) {
      case 7: runtime_state.master_volume = cc_value / 127.f; break;
      case 8: runtime_state.engine_idx = map(cc_value, 0, 127, 0, NUM_ENGINES - 1); break;
      case 9:  // Timbre
        runtime_state.timbre_in = cc_value / 127.f;
        runtime_state.filter_midi_owned = true;
        runtime_state.timbre_locked = true;
        runtime_state.last_midi_lock_time = millis();
        break;
      case 10:  // Color
        runtime_state.color_in = cc_value / 127.f;
        runtime_state.filter_midi_owned = true;
        runtime_state.color_locked = true;
        runtime_state.last_midi_lock_time = millis();
        break;
      case 11: runtime_state.env_attack_s = 0.01f + (cc_value / 127.f) * 2.f; break;
      case 12: runtime_state.env_release_s = 0.01f + (cc_value / 127.f) * 3.f; break;
      case 71:
        runtime_state.filter_resonance_cc = cc_value;
        runtime_state.filter_midi_owned = true;
        break;
      case 74:
        runtime_state.filter_cutoff_cc = cc_value;
        runtime_state.filter_midi_owned = true;
        break;
      case 15: runtime_state.fm_mod = cc_value / 127.f; break;
      case 16: runtime_state.timb_mod_midi = cc_value / 127.f; break;
      case 17: runtime_state.color_mod_midi = cc_value / 127.f; break;
      case 127:
        if (cc_value == 127) reset_usb_boot(0, 0);
        if (cc_value == 126) watchdog_reboot(0, 0, 0);
        break;
    }
    ENGINE_UPDATED(&runtime_state);
    runtime_state.last_param_change = millis();
  }
}


// Saving settings
bool save_settings(const RuntimeState *gstate) {
  SettingsSnapshot current = snapshot_from(gstate);

  if (memcmp(&current, &last_snapshot, sizeof(SettingsSnapshot)) == 0) {
    return false;
  }

  if (!LittleFS.begin()) return false;

  JsonDocument doc;
  doc["vol"] = gstate->master_volume;
  doc["atk"] = gstate->env_attack_s;
  doc["rel"] = gstate->env_release_s;
  doc["eng"] = gstate->engine_idx;
  doc["filt"] = gstate->filter_enabled;
  doc["mod"] = gstate->midi_mod;
  doc["cv1"] = gstate->cv_mod1;
  doc["cv2"] = gstate->cv_mod2;
  doc["timb"] = gstate->timbre_in;
  doc["color"] = gstate->color_in;
  doc["tcv"] = gstate->timb_mod_cv;
  doc["mcv"] = gstate->color_mod_cv;
  doc["ch"] = gstate->midi_ch;
  doc["enc"] = (int)gstate->encoder.state;
  doc["osc"] = gstate->oscilloscope_enabled;

  File f = LittleFS.open(SETTINGS_FILE, "w");
  if (!f) return false;

  if (serializeJson(doc, f) != 0) {
    last_snapshot = current;
  }
  f.close();
  return true;
}


inline void load_settings(RuntimeState *gstate) {
  if (!LittleFS.begin() || !LittleFS.exists(SETTINGS_FILE)) return;

  File f = LittleFS.open(SETTINGS_FILE, "r");
  if (!f) return;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return;

  gstate->master_volume = doc["vol"] | 0.7f;
  gstate->env_attack_s = doc["atk"] | 0.001f;
  gstate->env_release_s = doc["rel"] | 0.03f;
  gstate->engine_idx = doc["eng"] | 1;
  gstate->filter_enabled = doc["filt"] | true;
  gstate->midi_mod = doc["mod"] | true;
  gstate->cv_mod1 = doc["cv1"] | false;
  gstate->cv_mod2 = doc["cv2"] | false;
  gstate->timbre_in = doc["timb"] | 0.4f;
  gstate->color_in = doc["color"] | 0.3f;
  gstate->timb_mod_cv = doc["tcv"] | 0.0f;
  gstate->color_mod_cv = doc["mcv"] | 0.0f;
  gstate->midi_ch = doc["ch"] | 1;
  gstate->encoder.state = (EncoderState)(doc["enc"] | 0);
  gstate->oscilloscope_enabled = doc["osc"] | true;

  last_snapshot = snapshot_from(gstate);
  ENGINE_UPDATED(gstate);
}


#if USE_SCREEN
void checkSavedFeedback() {
  if (!runtime_state.show_saved_flag) return;

  unsigned long now = millis();
  if (now - runtime_state.saved_start_time < SAVED_DISPLAY_MS) {
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
    runtime_state.show_saved_flag = false;
    ENGINE_UPDATED(&runtime_state);
  }
}
#endif


static inline void handle_save(RuntimeState *gstate) {
  int btn = digitalRead(ENCODER_SW);
  static int last_btn_state = HIGH;
  static unsigned long button_press_start = 0;
  static bool has_saved_this_press = false;

  if (btn == LOW && last_btn_state == HIGH) {
    button_press_start = millis();
    has_saved_this_press = false;
  }

  if (btn == LOW && !has_saved_this_press) {
    if (millis() - button_press_start >= LONG_PRESS_MS) {
      // Update struct with current live values


      if (save_settings(gstate)) {
        gstate->show_saved_flag = true;
        gstate->saved_start_time = millis();
      }
      has_saved_this_press = true;
    }
  }

  if (btn == HIGH && last_btn_state == LOW) {
    has_saved_this_press = false;
  }
  last_btn_state = btn;

#if USE_SCREEN
  checkSavedFeedback();
#endif
}


void handle_control(RuntimeState *gstate) {
  static float smoothT = 0.5f;
  static float smoothC = 0.5f;
  static float smoothTMod = 0.0f;
  static float smoothCMod = 0.0f;
  static float smoothCut = 0.5f;
  static float smoothRes = 0.25f;
  static unsigned long last_pot_read = 0;

  if (millis() - last_pot_read > 4) {
    last_pot_read = millis();

    float rT = analogRead(POT_TIMBRE) / 1023.0f;
    float rC = analogRead(POT_COLOR) / 1023.0f;
    float srcT = analogRead(POT_TIMBRE_MOD) / 1023.0f;
#if HAS_A3
    float srcC = analogRead(POT_COLOR_MOD) / 1023.0f;
#else
    float srcC = 0.5f;
#endif

    const float SMOOTH_POT = 0.06f;
    pot_timbre += (rT - pot_timbre) * SMOOTH_POT;
    pot_color += (rC - pot_color) * SMOOTH_POT;

    if (pot_timbre > 0.999f) pot_timbre = 1.0f;
    if (pot_timbre < 0.001f) pot_timbre = 0.0f;

    if (pot_color > 0.999f) pot_color = 1.0f;
    if (pot_color < 0.001f) pot_color = 0.0f;

    int valT = (int)(pot_timbre * 127.0f + 0.5f);
    int valC = (int)(pot_color * 127.0f + 0.5f);

    if (!gstate->midi_mod) {
      gstate->timbre_locked = false;
      gstate->color_locked = false;
      ENGINE_UPDATED(gstate);
    }

    if (gstate->cv_mod1) {

      // --- Smooth the potentiometer inputs (depth controls) ---
      smoothT += (rT - smoothT) * 0.15f;
      smoothC += (rC - smoothC) * 0.15f;

      // --- Smooth the modulation sources ---
      smoothTMod += (srcT - smoothTMod) * 0.1f;  // slower smoothing
      smoothCMod += (srcC - smoothCMod) * 0.1f;

      // --- Apply modulation depth with soft scaling ---
      gstate->timb_mod_cv += ((smoothT * smoothTMod) - gstate->timb_mod_cv) * 0.05f;
      gstate->color_mod_cv += ((smoothC * smoothCMod) - gstate->color_mod_cv) * 0.05f;

      // --- Set base values for other modes ---
      gstate->timbre_in = 0.5f;
      gstate->color_in = 0.5f;
      ENGINE_UPDATED(gstate);

    } else if (gstate->midi_mod) {
      smoothT += (rT - smoothT) * 0.15f;
      smoothC += (rC - smoothC) * 0.15f;

      // TIMBRE
      if (gstate->timbre_locked) {
        // catchup!
        // if (fabsf(smoothT - gstate->timbre_in) < 0.02f) {
        //   gstate->timbre_locked = false;
        // }
        // raw!
        static uint8_t last_pot_timbre = 0;
        uint8_t new_timbre = (uint8_t)(smoothT * 127.0f);
        if (new_timbre != last_pot_timbre) {
          gstate->timbre_locked = false;
          last_pot_timbre = new_timbre;
        }
      }
      if (!gstate->timbre_locked) {
        gstate->timbre_in = smoothT;
      }

      // COLOR
      if (gstate->color_locked) {
        // catchup!
        // if (fabsf(smoothC - gstate->color_in) < 0.02f) {
        //   gstate->color_locked = false;
        // }
        // raw!
        static uint8_t last_pot_color = 0;
        uint8_t new_color = (uint8_t)(smoothC * 127.0f);
        if (new_color != last_pot_color) {
          gstate->color_locked = false;
          last_pot_color = new_color;
        }
      }
      if (!gstate->color_locked) {
        gstate->color_in = smoothC;
      }
      ENGINE_UPDATED(gstate);
    }

    if (gstate->filter_enabled) {
      // --- Update filter CVs from modulation pots ---
      smoothCut += (srcT - smoothCut) * 0.1f;
      smoothRes += (srcC - smoothRes) * 0.1f;

      static uint8_t last_pot_cut = 0;
      static uint8_t last_pot_res = 0;
      uint8_t new_cut = (uint8_t)(smoothCut * 127.0f);
      if (new_cut != last_pot_cut) {
        gstate->filter_cutoff_cc = new_cut;
        gstate->filter_midi_owned = false;
        last_pot_cut = new_cut;
      }
      uint8_t new_res = (uint8_t)(smoothRes * 127.0f);
      if (new_res != last_pot_res) {
        gstate->filter_resonance_cc = new_res;
        gstate->filter_midi_owned = false;
        last_pot_res = new_res;
      }

      // --- Keep Timbre and Color pots working as default ---
      smoothT += (rT - smoothT) * 0.08f;
      smoothC += (rC - smoothC) * 0.08f;

      // gstate->timbre_in = smoothT;
      // gstate->color_in = smoothC;

      // --- Decay any modulation CV influence smoothly ---
      // gstate->timb_mod_cv *= 0.9f;
      // gstate->color_mod_cv *= 0.9f;

      // --- FM is inactive in filter mode ---  <-- not sure why
      // fm_target = 0.0f;
      ENGINE_UPDATED(gstate);
    }

    else if (gstate->cv_mod2) {
      smoothT += (rT - smoothT) * 0.08f;
      smoothC += (rC - smoothC) * 0.08f;
      gstate->timbre_in = smoothT;
      gstate->color_in = smoothC;

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
            ENGINE_UPDATED(gstate);
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
          gstate->fm_mod = constrain(target_fm, 0.0f, 1.0f);
          lockC = smoothCMod;
        }
      } else {
        gstate->fm_mod *= 0.5f;
        if (gstate->fm_mod < 0.01f) {
          gstate->fm_mod = 0.0f;
          lockC = 0.0f;
        }
      }

      gstate->timbre_locked = false;
      gstate->color_locked = false;
    } else if (!gstate->midi_mod) {
      smoothT += (rT - smoothT) * 0.08f;
      smoothC += (rC - smoothC) * 0.08f;
      gstate->timbre_in = smoothT;
      gstate->color_in = smoothC;

      // Zero out all CV-related variables
      gstate->timb_mod_cv = 0.0f;
      gstate->color_mod_cv = 0.0f;
      gstate->fm_mod = 0.0f;

      gstate->timbre_locked = false;
      gstate->color_locked = false;
      ENGINE_UPDATED(gstate);
    }
  }
}

void handle_menu(RuntimeState *runtime_state) {
  Encoder *encoder = &(runtime_state->encoder);
  const int8_t step = encoder_decode_step(encoder);

  if (step) {
    switch (runtime_state->display_mode) {
      case ENGINE_SELECT_MODE:
        runtime_state->engine_idx = (runtime_state->engine_idx + step + NUM_ENGINES) % NUM_ENGINES;
        runtime_state->engine_updated = true;
        encoder->last_encoder_activity = millis();
        break;

      case SETTINGS_MODE:
        switch (encoder->state) {
          case VOLUME_ADJUST:
            runtime_state->master_volume = constrain(runtime_state->master_volume + step * 0.01f, 0.f, 1.f);
            break;
          case ATTACK_ADJUST:
            runtime_state->env_attack_s = constrain(runtime_state->env_attack_s + step * 0.01f, 0.001f, 1.f);
            runtime_state->env_params_changed = true;
            break;
          case RELEASE_ADJUST:
            runtime_state->env_release_s = constrain(runtime_state->env_release_s + step * 0.01f, 0.01f, 2.f);
            runtime_state->env_params_changed = true;
            break;
          case FILTER_TOGGLE:
            runtime_state->filter_enabled = !runtime_state->filter_enabled;
            // midi_mod = false;
            runtime_state->cv_mod1 = false;
            runtime_state->cv_mod2 = false;
            break;
          case MIDI_MOD:
            runtime_state->midi_mod = !runtime_state->midi_mod;
            // if (midi_mod) {
            //   cv_mod1 = false;
            //   cv_mod2 = false;
            // }
            break;
          case CV_MOD1:
            runtime_state->cv_mod1 = !runtime_state->cv_mod1;
            runtime_state->filter_enabled = false;
            runtime_state->cv_mod2 = false;
            // if (cv_mod1) midi_mod = false;
            break;
          case CV_MOD2:
            runtime_state->cv_mod2 = !runtime_state->cv_mod2;
            runtime_state->cv_mod1 = false;
            runtime_state->filter_enabled = false;
            // if (cv_mod2) midi_mod = false;
            break;
          case MIDI_CH:
            runtime_state->midi_ch = constrain(runtime_state->midi_ch + step, 1, 16);
            break;
          case SCOPE_TOGGLE:
            runtime_state->oscilloscope_enabled = !runtime_state->oscilloscope_enabled;
            if (!runtime_state->oscilloscope_enabled && runtime_state->display_mode == OSCILLOSCOPE_MODE) {
              runtime_state->display_mode = ENGINE_SELECT_MODE;
              runtime_state->scope_ready = false;
            }
            break;
          default:
            runtime_state->display_mode = ENGINE_SELECT_MODE;
            runtime_state->midi_mod = true;
            runtime_state->cv_mod1 = false;
            runtime_state->cv_mod2 = false;
            runtime_state->filter_enabled = false;
            runtime_state->engine_updated = true;
            break;
        }
        encoder->last_encoder_activity = millis();
        runtime_state->engine_updated = true;
        break;

      case OSCILLOSCOPE_MODE:
        runtime_state->display_mode = ENGINE_SELECT_MODE;
        runtime_state->engine_updated = true;
        encoder->last_encoder_activity = millis();
        break;
    }
  }

  if (encoder_sw_pressed(encoder)) {
    switch (runtime_state->display_mode) {
      case ENGINE_SELECT_MODE:
        runtime_state->display_mode = SETTINGS_MODE;
        runtime_state->encoder.state = ENGINE_SELECT;
        runtime_state->engine_updated = true;
        break;

      case SETTINGS_MODE:
        runtime_state->encoder.state = (EncoderState)((runtime_state->encoder.state + 1) % (ENCODER_STATE_NUM - 1));
        runtime_state->engine_updated = true;
        break;

      case OSCILLOSCOPE_MODE:
        runtime_state->display_mode = SETTINGS_MODE;
        runtime_state->encoder.state = (EncoderState)((runtime_state->encoder.state + 1) % (ENCODER_STATE_NUM - 1));
        runtime_state->engine_updated = true;
        break;
    }
  }
  draw_ui(runtime_state);
}

//===============================
// Setup and main loop for Core0
//===============================
#if DEBUG
static inline void setup_debug_serial() {
  Serial.begin(115200);
}
#endif

static inline void setup_serial() {
  Serial1.setRX(MIDI_UART_RX);
  Serial1.begin(31250);
}

static inline void setup_pins() {
  pinMode(ENCODER_CLK, INPUT_PULLUP);
  pinMode(ENCODER_DT, INPUT_PULLUP);
  pinMode(ENCODER_SW, INPUT_PULLUP);
}

#if USE_SCREEN
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
  drawSplash();
  delay(4000);
  display.clearDisplay();
  display.display();
}
#endif

static inline void setup_USB() {
  TinyUSBDevice.setManufacturerDescriptor("dr-schlange");
  TinyUSBDevice.setProductDescriptor("LISA Synth");
  TinyUSBDevice.setSerialDescriptor("NLYHW_00");

  usb_midi.begin();
}

static inline bool setup_LittleFS() {
  bool fs_ready = false;
  LittleFS.format();
  if (!LittleFS.begin()) {
    DEBUG_PRINTLN("LittleFS Mount Failed. Attempting to format...");
    LittleFS.format();
    if (LittleFS.begin()) {
      DEBUG_PRINTLN("LittleFS Formatted and Mounted successfully.");
      return true;
    } else {
      DEBUG_PRINTLN("LittleFS Critical Error: Hardware issue or Flash size not set!");
      return false;
    }
  }
  DEBUG_PRINTLN("LittleFS Mounted.");
  return true;
}

void setup() {
#if DEBUG
  setup_debug_serial();
#endif
  setup_LittleFS();
  setup_USB();
  setup_serial();
  setup_pins();
#if USE_SCREEN
  setup_display();
#endif
  load_settings(&runtime_state);
  SET_SYSTEM_READY(&runtime_state);
}

void loop() {
  if (!runtime_state.system_ready) {
    yield();  // Wait for Core 1 to finish DSP initialisation
    return;
  }

  handle_save(&runtime_state);
  handle_control(&runtime_state);
  handle_menu(&runtime_state);
  handle_MIDI();

  yield();
}

//===============================
// Setup and main loop for Core1
//===============================

static inline void setup_soundcard() {
  i2s_output.setFrequency(SAMPLE_RATE);
  i2s_output.setDATA(I2S_DATA_PIN);
  i2s_output.setBCLK(I2S_BCLK_PIN);
  i2s_output.begin();
}

static inline void setup_voices() {
  for (int v = 0; v < MAX_VOICES; v++) {
    voices[v].osc.Init(SAMPLE_RATE);
    voices[v].active = false;
  }
}

static inline void setup_global_filter() {
  global_filter.Init();
  global_filter.set_mode(braids::SVF_MODE_LP);
  global_filter.set_frequency(INIT_CUTOFF);
  global_filter.set_resonance(INIT_RESONANCE);
}

void setup1() {
  setup_soundcard();
  setup_voices();
  setup_global_filter();
}

void loop1() {
  if (i2s_output.availableForWrite() >= AUDIO_BLOCK * 4) {
    updateAudio();
  }
  SET_SYSTEM_READY(&runtime_state);
}
