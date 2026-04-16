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
  float cutoff, resonance, type;
  float b1, b2, b3, b4, b5;
  // kinetic config
  float mv_kinetic_mass, mv_kinetic_damping, mv_kinetic_stiffness;
  float envatk_kinetic_mass, envatk_kinetic_damping, envatk_kinetic_stiffness;
  float envrel_kinetic_mass, envrel_kinetic_damping, envrel_kinetic_stiffness;
  float timbre_kinetic_mass, timbre_kinetic_damping, timbre_kinetic_stiffness;
  float color_kinetic_mass, color_kinetic_damping, color_kinetic_stiffness;
  float timbmod_kinetic_mass, timbmod_kinetic_damping, timbmod_kinetic_stiffness;
  float colormod_kinetic_mass, colormod_kinetic_damping, colormod_kinetic_stiffness;
  float cutoff_kinetic_mass, cutoff_kinetic_damping, cutoff_kinetic_stiffness;
  float resonance_kinetic_mass, resonance_kinetic_damping, resonance_kinetic_stiffness;
  float b1_kinetic_mass, b1_kinetic_damping, b1_kinetic_stiffness;
  float b2_kinetic_mass, b2_kinetic_damping, b2_kinetic_stiffness;
  float b3_kinetic_mass, b3_kinetic_damping, b3_kinetic_stiffness;
  float b4_kinetic_mass, b4_kinetic_damping, b4_kinetic_stiffness;
  float b5_kinetic_mass, b5_kinetic_damping, b5_kinetic_stiffness;
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
    s->cutoff.value, s->resonance.value, s->filter_type.value,
    s->b1.value, s->b2.value, s->b3.value, s->b4.value, s->b5.value,
    s->master_volume.kinetic.mass.value, s->master_volume.kinetic.damping.value, s->master_volume.kinetic.stiffness.value,
    s->env_attack.kinetic.mass.value, s->env_attack.kinetic.damping.value, s->env_attack.kinetic.stiffness.value,
    s->env_release.kinetic.mass.value, s->env_release.kinetic.damping.value, s->env_release.kinetic.stiffness.value,
    s->timbre.kinetic.mass.value, s->timbre.kinetic.damping.value, s->timbre.kinetic.stiffness.value,
    s->color.kinetic.mass.value, s->color.kinetic.damping.value, s->color.kinetic.stiffness.value,
    s->timbre_mod.kinetic.mass.value, s->timbre_mod.kinetic.damping.value, s->timbre_mod.kinetic.stiffness.value,
    s->color_mod.kinetic.mass.value, s->color_mod.kinetic.damping.value, s->color_mod.kinetic.stiffness.value,
    s->cutoff.kinetic.mass.value, s->cutoff.kinetic.damping.value, s->cutoff.kinetic.stiffness.value,
    s->resonance.kinetic.mass.value, s->resonance.kinetic.damping.value, s->resonance.kinetic.stiffness.value,
    s->b1.kinetic.mass.value, s->b1.kinetic.damping.value, s->b1.kinetic.stiffness.value,
    s->b2.kinetic.mass.value, s->b2.kinetic.damping.value, s->b2.kinetic.stiffness.value,
    s->b3.kinetic.mass.value, s->b3.kinetic.damping.value, s->b3.kinetic.stiffness.value,
    s->b4.kinetic.mass.value, s->b4.kinetic.damping.value, s->b4.kinetic.stiffness.value,
    s->b5.kinetic.mass.value, s->b5.kinetic.damping.value, s->b5.kinetic.stiffness.value,
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
  doc["flttype"] = gstate->filter_type.value;
  doc["ch"] = gstate->midi_ch;
  doc["enc"] = (int)gstate->encoder.state;
  doc["osc"] = gstate->oscilloscope_enabled;
  doc["b1"] = gstate->b1.value;
  doc["b2"] = gstate->b2.value;
  doc["b3"] = gstate->b3.value;
  doc["b4"] = gstate->b4.value;
  doc["b5"] = gstate->b5.value;

  doc["mvkinmas"] = gstate->master_volume.kinetic.mass.value;
  doc["mvkindmp"] = gstate->master_volume.kinetic.damping.value;
  doc["mvkinstf"] = gstate->master_volume.kinetic.stiffness.value;
  doc["atkkinmas"] = gstate->env_attack.kinetic.mass.value;
  doc["atkkindmp"] = gstate->env_attack.kinetic.damping.value;
  doc["atkkinstf"] = gstate->env_attack.kinetic.stiffness.value;
  doc["relkinmas"] = gstate->env_release.kinetic.mass.value;
  doc["relkindmp"] = gstate->env_release.kinetic.damping.value;
  doc["relkinstf"] = gstate->env_release.kinetic.stiffness.value;
  doc["tmbrkinmas"] = gstate->timbre.kinetic.mass.value;
  doc["tmbrkindmp"] = gstate->timbre.kinetic.damping.value;
  doc["tmbrkinstf"] = gstate->timbre.kinetic.stiffness.value;
  doc["colrkinmas"] = gstate->color.kinetic.mass.value;
  doc["colrkindmp"] = gstate->color.kinetic.damping.value;
  doc["colrkinstf"] = gstate->color.kinetic.stiffness.value;
  doc["tmodkinmas"] = gstate->timbre_mod.kinetic.mass.value;
  doc["tmodkindmp"] = gstate->timbre_mod.kinetic.damping.value;
  doc["tmodkinstf"] = gstate->timbre_mod.kinetic.stiffness.value;
  doc["cmodkinmas"] = gstate->color_mod.kinetic.mass.value;
  doc["cmodkindmp"] = gstate->color_mod.kinetic.damping.value;
  doc["cmodkinstf"] = gstate->color_mod.kinetic.stiffness.value;
  doc["ctfkinmas"] = gstate->cutoff.kinetic.mass.value;
  doc["ctfkindmp"] = gstate->cutoff.kinetic.damping.value;
  doc["ctfkinstf"] = gstate->cutoff.kinetic.stiffness.value;
  doc["reskinmas"] = gstate->resonance.kinetic.mass.value;
  doc["reskindmp"] = gstate->resonance.kinetic.damping.value;
  doc["reskinstf"] = gstate->resonance.kinetic.stiffness.value;
  doc["b1kinmas"] = gstate->b1.kinetic.mass.value;
  doc["b1kindmp"] = gstate->b1.kinetic.damping.value;
  doc["b1kinstf"] = gstate->b1.kinetic.stiffness.value;
  doc["b2kinmas"] = gstate->b2.kinetic.mass.value;
  doc["b2kindmp"] = gstate->b2.kinetic.damping.value;
  doc["b2kinstf"] = gstate->b2.kinetic.stiffness.value;
  doc["b3kinmas"] = gstate->b3.kinetic.mass.value;
  doc["b3kindmp"] = gstate->b3.kinetic.damping.value;
  doc["b3kinstf"] = gstate->b3.kinetic.stiffness.value;
  doc["b4kinmas"] = gstate->b4.kinetic.mass.value;
  doc["b4kindmp"] = gstate->b4.kinetic.damping.value;
  doc["b4kinstf"] = gstate->b4.kinetic.stiffness.value;
  doc["b5kinmas"] = gstate->b5.kinetic.mass.value;
  doc["b5kindmp"] = gstate->b5.kinetic.damping.value;
  doc["b5kinstf"] = gstate->b5.kinetic.stiffness.value;

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
  gstate->filter_type.value = doc["flttype"] | 0.f;
  gstate->midi_ch = doc["ch"] | 1;
  gstate->encoder.state = (EncoderState)(doc["enc"] | 0);
  gstate->oscilloscope_enabled = doc["osc"] | true;
  gstate->b1.value = doc["b1"] | 0.001f;
  gstate->b2.value = doc["b2"] | 0.001f;
  gstate->b3.value = doc["b3"] | 0.001f;
  gstate->b4.value = doc["b4"] | 0.001f;
  gstate->b5.value = doc["b5"] | 0.001f;

  gstate->master_volume.kinetic.mass.value = doc["mvkinmas"] | 0.2f;
  gstate->master_volume.kinetic.damping.value = doc["mvkindmp"] | 0.4f;
  gstate->master_volume.kinetic.stiffness.value = doc["mvkinstf"] | 0.4f;
  gstate->env_attack.kinetic.mass.value = doc["atkkinmas"] | 0.2f;
  gstate->env_attack.kinetic.damping.value = doc["atkkindmp"] | 0.4f;
  gstate->env_attack.kinetic.stiffness.value = doc["atkkinstf"] | 0.4f;
  gstate->env_release.kinetic.mass.value = doc["relkinmas"] | 0.2f;
  gstate->env_release.kinetic.damping.value = doc["relkindmp"] | 0.4f;
  gstate->env_release.kinetic.stiffness.value = doc["relkinstf"] | 0.4f;
  gstate->timbre.kinetic.mass.value = doc["tmbrkinmas"] | 0.2f;
  gstate->timbre.kinetic.damping.value = doc["tmbrkindmp"] | 0.4f;
  gstate->timbre.kinetic.stiffness.value = doc["tmbrkinstf"] | 0.4f;
  gstate->color.kinetic.mass.value = doc["colrkinmas"] | 0.2f;
  gstate->color.kinetic.damping.value = doc["colrkindmp"] | 0.4f;
  gstate->color.kinetic.stiffness.value = doc["colrkinstf"] | 0.4f;
  gstate->timbre_mod.kinetic.mass.value = doc["tmodkinmas"] | 0.2f;
  gstate->timbre_mod.kinetic.damping.value = doc["tmodkindmp"] | 0.4f;
  gstate->timbre_mod.kinetic.stiffness.value = doc["tmodkinstf"] | 0.4f;
  gstate->color_mod.kinetic.mass.value = doc["cmodkinmas"] | 0.2f;
  gstate->color_mod.kinetic.damping.value = doc["cmodkindmp"] | 0.4f;
  gstate->color_mod.kinetic.stiffness.value = doc["cmodkinstf"] | 0.4f;
  gstate->cutoff.kinetic.mass.value = doc["ctfkinmas"] | 0.2f;
  gstate->cutoff.kinetic.damping.value = doc["ctfkindmp"] | 0.4f;
  gstate->cutoff.kinetic.stiffness.value = doc["ctfkinstf"] | 0.4f;
  gstate->resonance.kinetic.mass.value = doc["reskinmas"] | 0.2f;
  gstate->resonance.kinetic.damping.value = doc["reskindmp"] | 0.4f;
  gstate->resonance.kinetic.stiffness.value = doc["reskinstf"] | 0.4f;
  gstate->b1.kinetic.mass.value = doc["b1kinmas"] | 0.2f;
  gstate->b1.kinetic.damping.value = doc["b1kindmp"] | 0.4f;
  gstate->b1.kinetic.stiffness.value = doc["b1kinstf"] | 0.4f;
  gstate->b2.kinetic.mass.value = doc["b2kinmas"] | 0.2f;
  gstate->b2.kinetic.damping.value = doc["b2kindmp"] | 0.4f;
  gstate->b2.kinetic.stiffness.value = doc["b2kinstf"] | 0.4f;
  gstate->b3.kinetic.mass.value = doc["b3kinmas"] | 0.2f;
  gstate->b3.kinetic.damping.value = doc["b3kindmp"] | 0.4f;
  gstate->b3.kinetic.stiffness.value = doc["b3kinstf"] | 0.4f;
  gstate->b4.kinetic.mass.value = doc["b4kinmas"] | 0.2f;
  gstate->b4.kinetic.damping.value = doc["b4kindmp"] | 0.4f;
  gstate->b4.kinetic.stiffness.value = doc["b4kinstf"] | 0.4f;
  gstate->b5.kinetic.mass.value = doc["b5kinmas"] | 0.2f;
  gstate->b5.kinetic.damping.value = doc["b5kindmp"] | 0.4f;
  gstate->b5.kinetic.stiffness.value = doc["b5kinstf"] | 0.4f;

  last_snapshot = snapshot_from(gstate);
  SCHEDULE_REFRESH(gstate);
}

static inline bool setup_LittleFS() {
  bool fs_ready = false;
  // LittleFS.format();
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
  if (gstate->encoder_status == LONG_PRESSED) {
    if (save_settings(gstate)) {
      gstate->show_saved_flag = true;
      gstate->saved_start_time = millis();
    }
  }
#if USE_SCREEN
  saved_feedback(gstate);
#endif
}
