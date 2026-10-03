/*
   LISA (v0.3.0)

   Copyright (c) 2026 Dr Schlange
   Licensed under GNU GPLv3

 */
#pragma once
// clang-format off
#include "wavetable_streaming.h"
#include <pico/stdlib.h>
#include <Adafruit_TinyUSB.h>
#include "voices.h"
#include "constants_config.h"
// clang-format on

static Adafruit_USBD_CDC usb_hr;

// Frame format
// 8b => msgcount
// 32b => msg: 8b CC 16b value 8b flags
// flags =>
//      b0: range [0, 1] or [-1, 1]
//      b1: "pitchwheel" (secondary shorter table), for compatibility with MIDI

#define IS_PITCHWHEEL(flags) ((flags & 0b10) > 0)
#define IS_UNIPOLAR(flags) ((flags & 0b01) == 0)
#define IS_BIPOLAR(flags) ((flags & 0b01) == 1)

#define UNIPOLAR_FLOAT(value) (value / 65535.0f)
#define BIPOLAR_FLOAT(value) (((float)msg->value / 32767.5f) - 1.0f)

typedef struct {
  uint8_t cc;
  uint16_t value;
  uint8_t flags;
} HRMessage;

// Handshake
static const uint8_t HR_HELLO[] = {'H', 'R', '0', '1'};
static const uint8_t HR_READY[] = {'R', 'D', 'Y', '1'};

static bool hr_handshake_done = false;
static uint8_t hello_pos = 0;

static inline void setup_HR() {
  usb_hr.setStringDescriptor("LISA HighResolution");
  usb_hr.begin(115200);
  if (TinyUSBDevice.mounted()) {
    TinyUSBDevice.detach();
    delay(10);
    TinyUSBDevice.attach();
  }
}

static inline void handle_HR_handshake() {
  if (!usb_hr.dtr()) {
    hr_handshake_done = false;
    hello_pos = 0;
    return;
  }
  if (hr_handshake_done) {
    return;
  }
  while (usb_hr.available()) {
    uint8_t byte = usb_hr.read();

    if (byte == HR_HELLO[hello_pos]) {
      hello_pos++;

      if (hello_pos == sizeof(HR_HELLO)) {
        usb_hr.write(HR_READY, sizeof(HR_READY));
        usb_hr.flush();

        hr_handshake_done = true;
        hello_pos = 0;
        return;
      }
    } else {
      hello_pos = 0;
    }
  }
}

static inline void parse_message(HRMessage *msg) {
  msg->cc = usb_hr.read();
  msg->value = ((uint16_t)usb_hr.read()) | ((uint16_t)usb_hr.read() << 8);
  msg->flags = usb_hr.read();
}

static inline void route_message(RuntimeState *gstate, VoiceAllocator *voices,
                                 HRMessage *msg);

static inline void handle_HR(RuntimeState *gstate, VoiceAllocator *voices) {
  handle_HR_handshake();
  if (!hr_handshake_done) {
    return;
  }
  static uint8_t count = 0;
  static bool waiting_data = false;
  static uint8_t messages_read = 0;

  if (!waiting_data) {
    if (usb_hr.available() < 1) {
      return;
    }
    count = usb_hr.read();
    if (count == 0) {
      waiting_data = false;
      return;
    }
    waiting_data = true;
    messages_read = 0;
  }

  while (messages_read < count && usb_hr.available() >= 4) {
    HRMessage msg;

    parse_message(&msg);
    route_message(gstate, voices, &msg);

    messages_read++;
  }

  if (messages_read == count) {
    waiting_data = false;
  }
}

static inline void route_message(RuntimeState *gstate, VoiceAllocator *voices,
                                 HRMessage *msg) {
  // Pitchwheel equivalent routing
  if (IS_PITCHWHEEL(msg->flags)) {
    const uint8_t channel = msg->cc;
    const int16_t raw = (int32_t)msg->value - 32768;
    if (channel == MIDI_PB_CH_PITCH_BEND) {
      gstate->pitch_bend = (int16_t)((raw * MIDI_PB_BEND_RANGE_UNITS) >> 15);
      return;
    }
    if (WavetableStreamingOscillator::isLiveMode()) {
      // wavetable samples, one channel per wavetable
      if (channel < MIDI_PB_CH_PITCH_BEND) {
        WavetableStreamingOscillator::PushSampleInBuffer(channel, raw);
        return;
      }
      if (channel >= MIDI_PB_CH_RATIO_FIRST &&
          channel < MIDI_PB_CH_RATIO_FIRST + 4) {
        // ratio handling: ratio = 1 + raw / 4096 (hr value 32768 -> 1.0), in
        // Q16. Same -7..9 range as the 14-bit midi path, with 4x finer steps.
        int32_t ratio_delta = ((int32_t)raw) << 4;
        WavetableStreamingOscillator::setTableRatio(
            channel - MIDI_PB_CH_RATIO_FIRST, WT_RATIO_ONE + ratio_delta);
        return;
      }
      if (channel >= MIDI_PB_CH_OFFSET_FIRST &&
          channel < MIDI_PB_CH_OFFSET_FIRST + 4) {
        // phase offset handling: HR value must spreads and scale over
        // -0.5..+0.5 cycle (2^32 = one cycle)
        int32_t offset = ((int32_t)raw) << 16;
        WavetableStreamingOscillator::setTableOffset(
            channel - MIDI_PB_CH_OFFSET_FIRST, offset);
        return;
      }
    }
  }
  switch (msg->cc) {
  case MIDI_GAIN:
    gstate->gain.value = UNIPOLAR_FLOAT(msg->value);
    break;
  case MIDI_MASTER_VOL:
    gstate->master_volume.value = UNIPOLAR_FLOAT(msg->value);
    break;
  case MIDI_TIMBRE:
    gstate->timbre.value = UNIPOLAR_FLOAT(msg->value);
    break;
  case MIDI_COLOR:
    gstate->color.value = UNIPOLAR_FLOAT(msg->value);
    break;
  case MIDI_ATTACK:
    gstate->env_attack.value = UNIPOLAR_FLOAT(msg->value);
    break;
  case MIDI_RELEASE:
    gstate->env_release.value = UNIPOLAR_FLOAT(msg->value);
    break;
  case MIDI_RESONANCE:
    gstate->resonance.value = UNIPOLAR_FLOAT(msg->value);
    break;
  case MIDI_CUTOFF:
    gstate->cutoff.value = UNIPOLAR_FLOAT(msg->value);
    break;
  case MIDI_FM_MOD:
    gstate->fm_mod.value = UNIPOLAR_FLOAT(msg->value);
    break;
  case MIDI_FM_SLEW:
    gstate->fm_slew.value = UNIPOLAR_FLOAT(msg->value);
    break;
  case MIDI_TIMBRE_MOD:
    gstate->timbre_mod.value = UNIPOLAR_FLOAT(msg->value);
    break;
  case MIDI_COLOR_MOD:
    gstate->color_mod.value = UNIPOLAR_FLOAT(msg->value);
    break;
  case MIDI_UNISON_DETUNE:
    gstate->unison_detune.value = UNIPOLAR_FLOAT(msg->value);
    break;
  case MIDI_PANNING:
    gstate->panning.value = UNIPOLAR_FLOAT(msg->value);
    break;
  case MIDI_WT_AUTO_BLEND_K:
    WavetableStreamingOscillator::setBlendK(
        map(msg->value, 0, 65535, BLEND_K_MIN, BLEND_K_MAX));
    break;
  case MIDI_WT_BLEND_LEVEL:
    WavetableStreamingOscillator::setAllSnapshotBlendPosition(msg->value);
    break;
  case MIDI_WT_LEVEL_TABLE1:
  case MIDI_WT_LEVEL_TABLE2:
  case MIDI_WT_LEVEL_TABLE3:
  case MIDI_WT_LEVEL_TABLE4:
    WavetableStreamingOscillator::setBufferLevel(msg->cc - MIDI_WT_LEVEL_TABLE1,
                                                 msg->value * 255 / 65535);
    break;
  case MIDI_WT_PHASE_OFFSET:
    WavetableStreamingOscillator::setPhaseOffset(((int32_t)msg->value - 32768)
                                                 << 17);
    break;
  }
  SCHEDULE_REFRESH(gstate);
  gstate->last_param_change = millis();
}
