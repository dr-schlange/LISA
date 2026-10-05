/*
   LISA (v0.3.0)

   Copyright (c) 2026 Dr Schlange
   Licensed under GNU GPLv3

   Based on VIJA by Vadims Maksimovs (ledlaux.github.com)
 */
#pragma once
// clang-format off
#include "wavetable_streaming.h"
#include <pico/stdlib.h>
#include <Adafruit_TinyUSB.h>
#include "voices.h"
#include "constants_config.h"
#include "freezedebug.h"
#include "usblock.h"
// clang-format on

#define IS_MIDI_NOTE_OFF(status, value)                                        \
  (((status & 0xF0) == 0x80) || ((status & 0xF0) == 0x90 && value == 0))
#define IS_MIDI_NOTE_ON(status) ((status & 0xF0) == 0x90)
#define IS_MIDI_CC(status) ((status & 0xF0) == 0xB0)
#define IS_MIDI_PITCHWHEEL(status) ((status & 0xF0) == 0xE0)
#define MIDI_CHANNEL(status) (status & 0x0F)

static Adafruit_USBD_MIDI usb_midi;

static inline void setup_MIDI() { usb_midi.begin(); }

static inline void send_midi_cc(uint8_t cc, uint8_t value, uint8_t channel) {
#if USE_UART_MIDI
  Serial1.write(0xB0 | (channel - 1));
  Serial1.write(cc);
  Serial1.write(value);
#else
  uint8_t packet[4] = {0x0B, // Cable 0, CIN = Control Change
                       (uint8_t)(0xB0 | (channel - 1)), cc, value};
  acquire_usb_lock();
  usb_midi.writePacket(packet);
  release_usb_lock();
#endif
}

static inline void send_all_cc_values(RuntimeState *gstate) {
  Parameter *p = (Parameter *)&(gstate->timbre);
  for (uint8_t i = 0; i < ALL_PARAMETERS_NUM; i++) {
    send_midi_cc(p->midi_cc, (uint8_t)(p->value * 127.f), gstate->midi_ch);
    if (p->extended) {
      p = (Parameter *)((ExtParameter *)p + 1);
    } else {
      p++;
    }
  }
  send_midi_cc(MIDI_ENGINE_SEL,
               map(gstate->engine_idx, 0, NUM_ENGINES - 1, 0, 127),
               gstate->midi_ch);
}

static inline void handle_MIDI(RuntimeState *gstate, VoiceAllocator *voices) {
  static uint8_t running_status = 0;
  static uint8_t data_bytes[2] = {0};
  static uint8_t data_idx = 0;

  uint8_t status = 0, pitch_or_cc = 0, cc_value = 0;
  bool has_msg = false;

#if USE_UART_MIDI
  if (Serial1.available() == 0)
    return;

  uint8_t byte = Serial1.read();

  if (byte >= 0xF8)
    return;

  if (byte & 0x80) {
    running_status = byte;
    data_idx = 0;
    return;
  }

  if (running_status == 0)
    return;
  if (data_idx < 2)
    data_bytes[data_idx++] = byte;
  uint8_t type = running_status & 0xF0;
  uint8_t expected_len = (type == 0xC0 || type == 0xD0) ? 1 : 2;

  if (data_idx < expected_len)
    return;

  status = running_status;
  pitch_or_cc = data_bytes[0];
  cc_value = (expected_len == 2) ? data_bytes[1] : 0;
  data_idx = 0;
  has_msg = true;
#else // USB MIDI
  uint8_t packet[4];
  FREEZE_STAGE(FZ_MIDI_READ);
  acquire_usb_lock();
  const bool got_packet = usb_midi.readPacket(packet);
  release_usb_lock();
  if (!got_packet)
    return;

  uint8_t cin = packet[0] & 0x0F;
  if (cin < 0x8 || cin > 0xE)
    return;

  status = packet[1];
  pitch_or_cc = packet[2];
  cc_value = packet[3];
  has_msg = true;
  FREEZE_LAST_MIDI(status, pitch_or_cc, cc_value);
#endif

  if (millis() - gstate->last_param_change >= 1000.f) {
    gstate->color.locked = false;
    gstate->timbre.locked = false;
    SCHEDULE_REFRESH(gstate);
  }

  if (!has_msg)
    return;
  if ((status & 0x80) == 0)
    return;

  // Pitchwheel channel routing
  if (IS_MIDI_PITCHWHEEL(status)) {
    FREEZE_STAGE(FZ_MIDI_PITCHWHEEL);
    const uint8_t channel = MIDI_CHANNEL(status);
    const int16_t raw = (int16_t)(((uint16_t)cc_value << 7) | pitch_or_cc) -
                        8192; // -8192..8191
    if (channel == MIDI_PB_CH_PITCH_BEND) {
      gstate->pitch_bend = (int16_t)((raw * MIDI_PB_BEND_RANGE_UNITS) >> 13);
      return;
    }
    if (WavetableStreamingOscillator::isLiveMode()) {
      // wavetable samples, one channel per wavetable
      if (channel < MIDI_PB_CH_PITCH_BEND) {
        WavetableStreamingOscillator::PushSampleInBuffer(channel,
                                                         (int16_t)(raw << 2));
        return;
      }
      // ratio handling: ratio = 1 + raw / 1024 (midi value 0 -> 1.0), in Q16
      if (channel >= MIDI_PB_CH_RATIO_FIRST &&
          channel < MIDI_PB_CH_RATIO_FIRST + 4) {
        WavetableStreamingOscillator::setTableRatio(
            channel - MIDI_PB_CH_RATIO_FIRST, WT_RATIO_ONE + (raw << 6));
        return;
      }
      // phase offset handling: midi value spreads over -0.5..+0.5 cycle (2^32 =
      // one cycle)
      if (channel >= MIDI_PB_CH_OFFSET_FIRST &&
          channel < MIDI_PB_CH_OFFSET_FIRST + 4) {
        WavetableStreamingOscillator::setTableOffset(
            channel - MIDI_PB_CH_OFFSET_FIRST,
            (int32_t)((uint32_t)(int32_t)raw << 18));
        return;
      }
    }
  }

  if (MIDI_CHANNEL(status) != (gstate->midi_ch - 1))
    return;

  // --- Special CC64 sustain handling ---
  if (IS_MIDI_CC(status) && pitch_or_cc == 64) {
    if (cc_value >= 64) {
      gstate->sustain_enabled = true;
    } else {
      gstate->sustain_enabled = false;
      voices->enqueueResetAllSustain();
    }
    return;
  }

  if (IS_MIDI_NOTE_OFF(status, cc_value)) {
    FREEZE_STAGE(FZ_MIDI_NOTE_OFF);
    voices->enqueueNoteOff((int16_t)pitch_or_cc * 128, gstate->sustain_enabled);
    return;
  }

  if (IS_MIDI_NOTE_ON(status)) {
    FREEZE_STAGE(FZ_MIDI_NOTE_ON);
    voices->enqueueNoteOn((int16_t)pitch_or_cc * 128,
                          (int16_t)((uint32_t)cc_value * 32767 / 127));
    return;
  }

  // static VoiceMode prev_voice_mode = gstate->voice_mode;
  VoiceMode mode;
  uint8_t wt_mode;
  if (IS_MIDI_CC(status)) {
    FREEZE_STAGE(FZ_MIDI_CC);
    switch (pitch_or_cc) {
    case MIDI_VOICE_MODE:
      mode = (VoiceMode)midi_get_group(cc_value, NUM_VOICE_MODE);
      voices->enqueueSetMode(mode);
      break;
    case MIDI_GAIN:
      gstate->gain.value = cc_value / 127.f;
      break;
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
      gstate->filter_type.value = cc_value / 127.f;
      break;
    case MIDI_FM_MOD:
      gstate->fm_mod.value = cc_value / 127.f;
      break;
    case MIDI_FM_SLEW:
      gstate->fm_slew.value = cc_value / 127.f;
      break;
    case MIDI_TIMBRE_MOD:
      gstate->timbre_mod.value = cc_value / 127.f;
      break;
    case MIDI_COLOR_MOD:
      gstate->color_mod.value = cc_value / 127.f;
      break;
    case MIDI_UNISON_DETUNE:
      gstate->unison_detune.value = cc_value / 127.f;
      break;
    case MIDI_PANNING:
      gstate->panning.value = cc_value / 127.f;
      break;
    case MIDI_DEV:
      switch (cc_value) {
      case 127:
        reset_usb_boot(0, 0);
        break;
      case 126:
        watchdog_reboot(0, 0, 0);
        break;
      case 1:
        send_all_cc_values(gstate);
        break;
      }
      break;
    case MIDI_WT_SNAPSHOT_DEPTH:
      WavetableStreamingOscillator::setAllSnapshotDepth(
          map(cc_value, 0, 127, 2, MAX_SNAPSHOTS));
      break;
    case MIDI_WT_SNAPSHOT_ACTIVATE:
      WavetableStreamingOscillator::setSnapshotMode(cc_value < 64);
      break;
    case MIDI_WT_AUTO_BLEND_ACTIVATE:
      WavetableStreamingOscillator::setAllSnapshotAutoBlend(cc_value >= 64);
      break;
    case MIDI_WT_AUTO_BLEND_K:
      WavetableStreamingOscillator::setBlendK(
          map(cc_value, 0, 127, BLEND_K_MIN, BLEND_K_MAX));
      break;
    case MIDI_WT_BLEND_LEVEL:
      WavetableStreamingOscillator::setAllSnapshotBlendPosition(
          (uint16_t)((uint32_t)cc_value * 65535 / 127));
      break;
    case MIDI_WT_BLEND_DIRECTION:
      WavetableStreamingOscillator::setBlendDirection(cc_value >= 64 ? 1 : -1);
      break;
    case MIDI_WT_LEVEL_TABLE1:
    case MIDI_WT_LEVEL_TABLE2:
    case MIDI_WT_LEVEL_TABLE3:
    case MIDI_WT_LEVEL_TABLE4:
      WavetableStreamingOscillator::setBufferLevel(
          pitch_or_cc - MIDI_WT_LEVEL_TABLE1,
          (uint8_t)(cc_value * 255 / 127)); // convert to fp8
      break;
    case MIDI_WT_MODE_TABLE1:
    case MIDI_WT_MODE_TABLE2:
    case MIDI_WT_MODE_TABLE3:
    case MIDI_WT_MODE_TABLE4:
      WavetableStreamingOscillator::setMode(pitch_or_cc - MIDI_WT_MODE_TABLE1,
                                            midi_get_group(cc_value, 4));
      break;
    case MIDI_WT_INDEX_TABLE1:
    case MIDI_WT_INDEX_TABLE2:
    case MIDI_WT_INDEX_TABLE3:
    case MIDI_WT_INDEX_TABLE4:
      WavetableStreamingOscillator::setWriteIndex(
          pitch_or_cc - MIDI_WT_INDEX_TABLE1, cc_value << 1);
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
      WavetableStreamingOscillator::setPhaseOffset((int32_t)(cc_value - 64)
                                                   << 25);
      break;
    case MIDI_WT_PHASE_RESET:
      voices->enqueueResetPhases();
      break;
    case MIDI_WT_HARD_SYNC:
      WavetableStreamingOscillator::setHardSync(cc_value >= 64);
      break;
    case MIDI_WT_FREEZE_TABLE1:
    case MIDI_WT_FREEZE_TABLE2:
    case MIDI_WT_FREEZE_TABLE3:
    case MIDI_WT_FREEZE_TABLE4:
      WavetableStreamingOscillator::freezeBuffer(
          pitch_or_cc - MIDI_WT_FREEZE_TABLE1, cc_value >= 64);
      break;
    case MIDI_WT_FREEZE_ALL:
      WavetableStreamingOscillator::freezeAllBuffers(cc_value >= 64);
      break;
    }
    SCHEDULE_REFRESH(gstate);
    gstate->last_param_change = millis();
  }
}
