#include "wavetable_streaming.h"
/*
  LISA (v0.0.1)

  Copyright (c) 2026 Dr Schlange
  Licensed under GNU GPLv3

  Based on VIJA by Vadims Maksimovs (ledlaux.github.com)
*/
#pragma once
#include <pico/stdlib.h>
#include <Adafruit_TinyUSB.h>
#include "voices.h"
#include "constants_config.h"

#define IS_MIDI_NOTE_OFF(status, value) (((status & 0xF0) == 0x80) || ((status & 0xF0) == 0x90 && value == 0))
#define IS_MIDI_NOTE_ON(status) ((status & 0xF0) == 0x90)
#define IS_MIDI_CC(status) ((status & 0xF0) == 0xB0)
#define IS_MIDI_PITCHWHEEL(status) ((status & 0xF0) == 0xE0)
#define MIDI_CHANNEL(status) (status & 0x0F)


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

  if (millis() - gstate->last_param_change >= 1000.f) {
    gstate->color.locked = false;
    gstate->timbre.locked = false;
    SCHEDULE_REFRESH(gstate);
  }

  if (!has_msg) return;
  if ((status & 0x80) == 0) return;

  // Special case for MIDI channel and pitchwheel
  if (WavetableStreamingOscillator::isLiveMode()
      && IS_MIDI_PITCHWHEEL(status)
      && MIDI_CHANNEL(status) >= 0
      && MIDI_CHANNEL(status) <= 3) {
    uint16_t raw = ((uint16_t)cc_value << 7) | pitch_or_cc;  // 0–16383
    int16_t sample = ((int32_t)raw - 8192) << 2;
    WavetableStreamingOscillator::PushSampleInBuffer(MIDI_CHANNEL(status), sample);
    return;
  }
  
  if (MIDI_CHANNEL(status) != (gstate->midi_ch - 1)) return;

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
    return;
  }

  if (IS_MIDI_NOTE_ON(status)) {
    int i = find_free_voice(voices);
    voices[i].pitch = pitch_or_cc;
    voices[i].velocity = cc_value / 127.f;
    voices[i].env = 0.f;
    voices[i].active = true;
    voices[i].age = global_age++;
    return;
  }

  if (IS_MIDI_CC(status)) {
    switch (pitch_or_cc) {
      case MIDI_MASTER_VOL:
        gstate->master_volume.value = cc_value / 127.f;
        break;
      case MIDI_ENGINE_SEL:
        gstate->engine_idx = map(cc_value, 0, 127, 0, NUM_ENGINES - 1);
        break;
      case MIDI_TIMBRE:
        gstate->timbre.value = cc_value / 127.f;
        gstate->timbre.locked = true;
        break;
      case MIDI_COLOR:
        gstate->color.value = cc_value / 127.f;
        gstate->color.locked = true;
        break;
      case MIDI_ATTACK:
        gstate->env_attack.value = cc_value / 127.f;
        break;
      case MIDI_RELEASE:
        gstate->env_release.value = cc_value / 127.f;
        break;
      case MIDI_RESONANCE:
        gstate->resonance.value = cc_value / 127.f;
        break;
      case MIDI_CUTOFF:
        gstate->cutoff.value = cc_value / 127.f;
        break;
      case MIDI_FILTER_TYPE:
        gstate->filter_type.value = (cc_value / 127.f) * 2.f;  // scale on the number of filters in braids
        break;
      case MIDI_FM_MOD:
        gstate->fm_mod.value = cc_value / 127.f;
        break;
      case MIDI_TIMBRE_MOD:
        gstate->timbre_mod.value = cc_value / 127.f;
        break;
      case MIDI_COLOR_MOD:
        gstate->color_mod.value = cc_value / 127.f;
        break;
      case MIDI_DEV:
        if (cc_value == 127) reset_usb_boot(0, 0);
        if (cc_value == 126) watchdog_reboot(0, 0, 0);
        break;
      // case MIDI_WT_CAPTURE_MODE:
      //   WavetableStreamingOscillator::setCaptureMode(
      //     (WavetableStreamingOscillator::CaptureMode)constrain(((int)cc_value * WavetableStreamingOscillator::CAPTURE_NUMBER + 63) / 127, 0, WavetableStreamingOscillator::CAPTURE_NUMBER - 1));
      //   break;
      case MIDI_WT_DOUBLE_BUFFER:
        WavetableStreamingOscillator::setDoubleBuffer(cc_value >= 64);
        break;
      case MIDI_WT_RESET_ALL_BUFFERS:
        WavetableStreamingOscillator::resetAllWavetables(cc_value >= 64);
        break;
      case MIDI_WT_RETRIGGER:
        WavetableStreamingOscillator::setRetrigger(cc_value >= 64);
        break;
      case MIDI_WT_RESET_WRITE_IDX:
        WavetableStreamingOscillator::resetWriteIndex(cc_value >= 64);
        break;
      case MIDI_WT_PHASE_OFFSET:
        WavetableStreamingOscillator::setPhaseOffset((int32_t)(cc_value - 64) << 25);
        break;
      case MIDI_WT_PHASE_RESET:
        for (uint8_t i = 0; i < MAX_VOICES; i++) {
          voices[i].osc.reset_phase();
        }
        break;
      case MIDI_WT_FREEZE_TABLE1:
      case MIDI_WT_FREEZE_TABLE2:
      case MIDI_WT_FREEZE_TABLE3:
      case MIDI_WT_FREEZE_TABLE4:
        WavetableStreamingOscillator::freezeBuffer(pitch_or_cc - MIDI_WT_FREEZE_TABLE1, cc_value >= 64);
        break;
      case MIDI_WT_FREEZE_ALL:
        WavetableStreamingOscillator::freezeAllBuffers(cc_value >= 64);
        break;
    }
    SCHEDULE_REFRESH(gstate);
    gstate->last_param_change = millis();
  } else if (IS_MIDI_PITCHWHEEL(status)) {
    // TODO for non live mode
  }
}
