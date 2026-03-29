/*
  LISA (v0.0.1)

  Copyright (c) 2026 Dr Schlange
  Licensed under GNU GPLv3

  Based on VIJA by Vadims Maksimovs (ledlaux.github.com)
*/
#pragma once
#include <Arduino.h>
#include <pico/stdlib.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "constants_config.h"
#include "global_state.h"

typedef struct __attribute__((packed)) {
  float master_volume, env_attack, env_release;
  float timbre, color, timb_mod, color_mod;
  float cutoff, resonance;
  int engine_idx;
  uint8_t midi_ch;
  uint8_t filter_enabled, midi_enabled, cv_mod1_enabled, cv_mod2_enabled;
  uint8_t oscilloscope_enabled;
  uint8_t enc_state;
} SettingsSnapshot;

static inline SettingsSnapshot snapshot_from(const RuntimeState *s) {
  return (SettingsSnapshot){
    s->master_volume.value, s->env_attack.value, s->env_release.value,
    s->timbre.value, s->color.value, s->timbre_mod.value, s->color_mod.value,
    s->cutoff.value, s->resonance.value,
    s->engine_idx, s->midi_ch,
    s->filter_enabled, s->midi_enabled, s->cv_mod1_enabled, s->cv_mod2_enabled,
    s->oscilloscope_enabled, (uint8_t)s->encoder.state
  };
}

static SettingsSnapshot last_snapshot;

inline bool save_settings(const RuntimeState *gstate) {
  SettingsSnapshot current = snapshot_from(gstate);

  if (memcmp(&current, &last_snapshot, sizeof(SettingsSnapshot)) == 0) {
    return false;
  }

  if (!LittleFS.begin()) return false;

  JsonDocument doc;
  doc["vol"] = gstate->master_volume.value;
  doc["atk"] = gstate->env_attack.value;
  doc["rel"] = gstate->env_release.value;
  doc["eng"] = gstate->engine_idx;
  doc["filt"] = gstate->filter_enabled;
  doc["mod"] = gstate->midi_enabled;
  doc["cv1"] = gstate->cv_mod1_enabled;
  doc["cv2"] = gstate->cv_mod2_enabled;
  doc["timb"] = gstate->timbre.value;
  doc["color"] = gstate->color.value;
  doc["tmod"] = gstate->timbre_mod.value;
  doc["cmod"] = gstate->color_mod.value;
  doc["ctf"] = gstate->cutoff.value;
  doc["rsn"] = gstate->resonance.value;
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

  gstate->master_volume.value = doc["vol"] | 0.7f;
  gstate->env_attack.value = doc["atk"] | 0.001f;
  gstate->env_release.value = doc["rel"] | 0.03f;
  gstate->engine_idx = doc["eng"] | 1;
  gstate->filter_enabled = doc["filt"] | true;
  gstate->midi_enabled = doc["mod"] | true;
  gstate->cv_mod1_enabled = doc["cv1"] | false;
  gstate->cv_mod2_enabled = doc["cv2"] | false;
  gstate->timbre.value = doc["timb"] | 0.4f;
  gstate->color.value = doc["color"] | 0.3f;
  gstate->timbre_mod.value = doc["tmod"] | 0.0f;
  gstate->color_mod.value = doc["cmod"] | 0.0f;
  gstate->cutoff.value = doc["ctf"] | 0.5f;
  gstate->resonance.value = doc["rsn"] | 0.25f;
  gstate->midi_ch = doc["ch"] | 1;
  gstate->encoder.state = (EncoderState)(doc["enc"] | 0);
  gstate->oscilloscope_enabled = doc["osc"] | true;

  last_snapshot = snapshot_from(gstate);
  SCHEDULE_REFRESH(gstate);
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

#include "ui.h"
static inline void handle_save(RuntimeState *gstate) {
  // handle long press at global level (whatever the display mode)
  if (encoder_sw_longpressed(&(gstate->encoder), LONG_PRESS_MS))
    if (save_settings(gstate)) {
      gstate->show_saved_flag = true;
      gstate->saved_start_time = millis();
    }
#if USE_SCREEN
  saved_feedback(gstate);
#endif
}
