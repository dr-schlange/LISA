/*
  LISA (v0.0.1)

  Copyright (c) 2025 Dr Schlange
  Licensed under GNU GPLv3
*/
#pragma once
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "global_state.h"

#define SETTINGS_FILE "/lisa_settings.json"


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

static SettingsSnapshot last_snapshot;

inline bool save_settings(const RuntimeState *gstate) {
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
