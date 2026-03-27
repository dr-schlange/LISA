/*
  LISA (v0.0.1)

  Copyright (c) 2025 Dr Schlange
  Licensed under GNU GPLv3

  Based on VIJA by Vadims Maksimovs (ledlaux.github.com)
*/
#pragma once
#include <pico/stdlib.h>
#include <Adafruit_TinyUSB.h>
#include "global_state.h"
#include "voices.h"
#include "constants_config.h"

#define IS_MIDI_NOTE_OFF(status, value) (((status & 0xF0) == 0x80) || ((status & 0xF0) == 0x90 && value == 0))
#define IS_MIDI_NOTE_ON(status) ((status & 0xF0) == 0x90)
#define IS_MIDI_CC(status) ((status & 0xF0) == 0xB0)


static Adafruit_USBD_MIDI usb_midi;
static uint32_t global_age = 0;


static inline void setup_USB() {
  TinyUSBDevice.setManufacturerDescriptor("dr-schlange");
  TinyUSBDevice.setProductDescriptor("LISA Synth");
  TinyUSBDevice.setSerialDescriptor("NLYHW_00");

  usb_midi.begin();
}

static inline void send_midi_cc(uint8_t cc, uint8_t value, uint8_t channel) {
#if USE_UART_MIDI
  Serial1.write(0xB0 | (channel - 1));
  Serial1.write(cc);
  Serial1.write(value);
#else
  uint8_t packet[4] = {
    0x0B,  // Cable 0, CIN = Control Change
    (uint8_t)(0xB0 | (channel - 1)),
    cc,
    value
  };
  usb_midi.writePacket(packet);
#endif
}


static inline void handle_MIDI(RuntimeState *gstate, Voice *voices) {
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
  cc_value = (expected_len == 2) ? data_bytes[1] : 0;
  data_idx = 0;
  has_msg = true;

  // --- Special CC64 sustain handling ---
  if (IS_MIDI_CC(status) && pitch_or_cc == 64) {
    if (cc_value >= 64) {
      gstate->sustain_enabled = true;
    } else {
      gstate->sustain_enabled = false;
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
  if ((status & 0x0F) != (gstate->midi_ch - 1)) return;

  if (IS_MIDI_NOTE_OFF(status, cc_value)) {
    int i = find_voice_by_pitch(voices, pitch_or_cc);
    if (i >= 0) {
      if (gstate->sustain_enabled) {
        voices[i].sustained = true;
        voices[i].active = false;
      } else {
        voices[i].active = false;
        voices[i].sustained = false;
      }
    }
  } else if (IS_MIDI_NOTE_ON(status)) {
    int i = find_free_voice(voices);
    voices[i].pitch = pitch_or_cc;
    voices[i].velocity = cc_value / 127.f;
    voices[i].active = true;
    voices[i].age = global_age++;
  } else if (IS_MIDI_CC(status)) {
    switch (pitch_or_cc) {
      case 7: gstate->master_volume = cc_value / 127.f; break;
      case 8: gstate->engine_idx = map(cc_value, 0, 127, 0, NUM_ENGINES - 1); break;
      case 9:  // Timbre
        gstate->timbre_in = cc_value / 127.f;
        gstate->filter_midi_owned = true;
        gstate->timbre_locked = true;
        gstate->last_midi_lock_time = millis();
        break;
      case 10:  // Color
        gstate->color_in = cc_value / 127.f;
        gstate->filter_midi_owned = true;
        gstate->color_locked = true;
        gstate->last_midi_lock_time = millis();
        break;
      case 11: gstate->env_attack_s = 0.01f + (cc_value / 127.f) * 2.f; break;
      case 12: gstate->env_release_s = 0.01f + (cc_value / 127.f) * 3.f; break;
      case 71:
        gstate->filter_resonance_cc = cc_value;
        gstate->filter_midi_owned = true;
        break;
      case 74:
        gstate->filter_cutoff_cc = cc_value;
        gstate->filter_midi_owned = true;
        break;
      case 15: gstate->fm_mod = cc_value / 127.f; break;
      case 16: gstate->timb_mod_midi = cc_value / 127.f; break;
      case 17: gstate->color_mod_midi = cc_value / 127.f; break;
      case 127:
        if (cc_value == 127) reset_usb_boot(0, 0);
        if (cc_value == 126) watchdog_reboot(0, 0, 0);
        break;
    }
    SCHEDULE_REFRESH(gstate);
    gstate->last_param_change = millis();
  }
}
