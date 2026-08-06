/*
  LISA (v0.2.0)

  Copyright (c) 2026 Dr Schlange
  Licensed under GNU GPLv3

  Based on VIJA by Vadims Maksimovs (ledlaux.github.com)
*/
#pragma once
// clang-format off
#include <pico/stdlib.h>
#include <BRAIDS.h>
#include "constants_config.h"
#include "wavetable_streaming.h"
#include "global_state.h"
// clang-format on

#define VOICE_ACTIVE 0b00000001
#define VOICE_LAST_TRIG 0b00000010
#define VOICE_SUSTAINED 0b00000100
#define VOICE_SECONDARY 0b00001000

#define set_active(flags) (flags |= VOICE_ACTIVE)
#define is_active(flags) (flags & VOICE_ACTIVE)
#define reset_active(flags) (flags &= ~VOICE_ACTIVE)

#define set_last_trig(flags) (flags |= VOICE_LAST_TRIG)
#define is_last_trig(flags) (flags & VOICE_LAST_TRIG)
#define reset_last_trig(flags) (flags &= ~VOICE_LAST_TRIG)

#define set_sustained(flags) (flags |= VOICE_SUSTAINED)
#define is_sustained(flags) (flags & VOICE_SUSTAINED)
#define reset_sustained(flags) (flags &= ~VOICE_SUSTAINED)

#define set_secondary(flags) (flags |= VOICE_SECONDARY)
#define is_secondary(flags) (flags & VOICE_SECONDARY)
#define reset_secondary(flags) (flags &= ~VOICE_SECONDARY)

#define ENV_EPSILON_Q15 4            // ~0.0001 * 32767
#define ABS_SLEW_EPSILON_Q15 164     // ~0.005 * 32767
#define ABS_DIFF_EPSILON_Q15 328     // ~0.01 * 32767
#define ABS_DIFF2_EPSILON_Q15 33     // ~0.001 * 32767
#define TIMB_COLOR_SLEW_COEF_Q15 328 // ~0.01 * 32767

enum VoiceMode {
  VOICE_POLY,
  VOICE_UNISON,
  VOICE_MONO,
  NUM_VOICE_MODE,
};

static uint32_t global_age = 0;

class Voice {
public:
  WavetableStreamingOscillator osc;
  uint8_t flags;
  int16_t pitch;
  int16_t velocity;
  int16_t env;
  uint32_t age;

  Voice() {
    flags = 0b00000000;
    osc.Init(SAMPLE_RATE);
  }

  inline void setup(int16_t pitch_, int16_t velocity_) {
    pitch = pitch_;
    velocity = velocity_;
    env = 0;
    set_active(flags);
    reset_secondary(flags);
    age = global_age++;
  }

  inline void render(int32_t mix[AUDIO_BLOCK], int16_t timbre, int16_t color,
                     int16_t timb_slew, int16_t color_slew, int16_t fm_slew,
                     float unison_detune, int16_t attackCoef,
                     int16_t releaseCoef, int32_t block_gain) {
    if (!is_active(flags) && !is_sustained(flags) && env < ENV_EPSILON_Q15)
      return;

    // >> 2 == * 0.25
    vel_smoothed_ += (int16_t)((int32_t)velocity - (int32_t)vel_smoothed_) >> 2;

    // 12 semitones * 128 (braids note scale unit)
    int16_t oscpitch = pitch + (int16_t)(((int32_t)fm_slew * 12 * 128) >> 15);
    if (is_secondary(flags)) {
      oscpitch += (int16_t)((unison_detune - 0.5f) * 128.0f);
    }
    osc.set_pitch(oscpitch);

    int32_t t = constrain((int32_t)timbre + (int32_t)timb_slew, 0, 32767);
    int32_t m = constrain((int32_t)color + (int32_t)color_slew, 0, 32767);
    osc.set_parameters((int16_t)t, (int16_t)m);

    if (is_active(flags) && !is_last_trig(flags))
      osc.Strike();

    if (is_active(flags)) {
      set_last_trig(flags);
    } else {
      reset_last_trig(flags);
    }
    osc.Render(sync_buffer_, buffer_, AUDIO_BLOCK);

    int16_t envTarget = (is_active(flags) || is_sustained(flags)) ? 32767 : 0;
    int16_t coef = envTarget ? attackCoef : releaseCoef;

    const int32_t amp =
        (((int32_t)env * (int32_t)vel_smoothed_) >> 15) * block_gain >> 15;

    for (int i = 0; i < AUDIO_BLOCK; i++) {
      env += ((int16_t)((int32_t)envTarget - (int32_t)env)) * coef >> 15;
      if (envTarget == 0 && env < ENV_EPSILON_Q15)
        env = 0;

      mix[i] += (buffer_[i] * amp) >> 15;
    }
  }

  inline void resetPhase() { osc.reset_phase(); }

private:
  int16_t vel_smoothed_;
  int16_t buffer_[AUDIO_BLOCK];
  uint8_t sync_buffer_[AUDIO_BLOCK];
};

class VoiceAllocator {
public:
  VoiceAllocator(const RuntimeState *gstate) : gstate_(gstate) {
    mode_ = VOICE_POLY;
    for (int v = 0; v < MAX_VOICES; v++) {
      voices_[v].osc.Init(SAMPLE_RATE);
      reset_active(voices_[v].flags);
    }
  }

  inline void setMode(VoiceMode mode) {
    if (mode_ != mode) {
      mode_ = mode;
      resetAllVoices();
    }
  }

  inline void renderAllVoices(int32_t mix[AUDIO_BLOCK]) {
    const RuntimeState *gstate = gstate_;
    const int32_t block_gain =
        (int32_t)(gstate->master_volume.value * gstate->gain.value /
                  MAX_VOICES * 32767.0f);
    float atk_knob = gstate->env_attack.value;
    float rel_knob = gstate->env_release.value;
    if (atk_knob != last_atk_) {
      float atk = 0.001f * powf(2000.f, atk_knob);
      attackCoef_ =
          (int16_t)((1.0f - expf(-1.0f / (SAMPLE_RATE * atk))) * 32767.f);
      last_atk_ = atk_knob;
    }
    if (rel_knob != last_rel_) {
      float rel = 0.005f * powf(1000.f, rel_knob);
      releaseCoef_ =
          (int16_t)((1.0f - expf(-1.0f / (SAMPLE_RATE * rel))) * 32767.f);
      last_rel_ = rel_knob;
    }
    int16_t fm_mod = (gstate->midi_enabled || !gstate->cv_mod1_enabled)
                         ? (int16_t)(gstate->fm_mod.value * 32767.f)
                         : 0;
    applyStableSlew(fm_slew_, fm_mod,
                    (int16_t)(gstate->fm_slew.value * 0.06f * 32767.f));
    applyStableSlew(timb_slew_, (int16_t)(gstate->timbre_mod.value * 32767.f),
                    TIMB_COLOR_SLEW_COEF_Q15);
    applyStableSlew(color_slew_, (int16_t)(gstate->color_mod.value * 32767.f),
                    TIMB_COLOR_SLEW_COEF_Q15);
    int16_t timb_q15 = (int16_t)(gstate->timbre.value * 32767.f);
    int16_t col_q15 = (int16_t)(gstate->color.value * 32767.f);
    for (int v = 0; v < MAX_VOICES; v++) {
      voices_[v].render(mix, timb_q15, col_q15, timb_slew_, color_slew_,
                        fm_slew_, gstate->unison_detune.value, attackCoef_,
                        releaseCoef_, block_gain);
    }
  }

  inline void setLiveMode(bool activate) {
    for (int v = 0; v < MAX_VOICES; v++) {
      voices_[v].osc.setLiveMode(activate);
    }
  }

  inline void setEngine(braids::MacroOscillatorShape shape) {
    for (int v = 0; v < MAX_VOICES; v++) {
      voices_[v].osc.set_shape(shape);
    }
  }

  inline void resetAllSustain() {
    for (int i = 0; i < MAX_VOICES; i++) {
      if (is_sustained(voices_[i].flags)) {
        reset_active(voices_[i].flags);
        reset_sustained(voices_[i].flags);
      }
    }
  }

  inline void freeVoice(int16_t pitch, bool sustain_enabled) {
    switch (mode_) {
    case VOICE_POLY:
      freeVoiceByPitch(pitch, sustain_enabled);
      break;
    case VOICE_UNISON:
      freeVoiceUnison(pitch, sustain_enabled);
      break;
    case VOICE_MONO:
      freeVoiceMono(sustain_enabled);
      break;
    }
  }

  inline void allocateVoice(int16_t pitch, int16_t velocity) {
    switch (mode_) {
    case VOICE_POLY:
      allocateOldestVoice(pitch, velocity);
      break;
    case VOICE_UNISON:
      allocateVoiceUnison(pitch, velocity);
      break;
    case VOICE_MONO:
      allocateVoiceMono(pitch, velocity);
      break;
    }
  }

  inline void resetPhases() {
    for (uint8_t i = 0; i < MAX_VOICES; i++) {
      voices_[i].resetPhase();
    }
  }

private:
  const RuntimeState *gstate_;
  Voice voices_[MAX_VOICES];
  VoiceMode mode_;
  int16_t fm_slew_ = 0;
  int16_t timb_slew_ = 0;
  int16_t color_slew_ = 0;
  int16_t attackCoef_ = 0;
  int16_t releaseCoef_ = 0;
  float last_atk_ = -1.f;
  float last_rel_ = -1.f;

  static inline void applyStableSlew(int16_t &current, int16_t target,
                                     int16_t coefficient) {
    int16_t diff = (int16_t)((int32_t)target - (int32_t)current);
    int16_t abs_diff = abs(diff);
    if (abs_diff < ABS_SLEW_EPSILON_Q15) {
      if (target == 0 && abs_diff < ABS_DIFF_EPSILON_Q15)
        current = 0;
      return;
    }
    if (abs_diff < ABS_DIFF2_EPSILON_Q15)
      current = target;
    else
      current += (int16_t)(((int32_t)diff * (int32_t)coefficient) >> 15);
  }

  inline int findFreeVoice(int16_t pitch) {
    int oldest = 0;
    uint32_t old_age = voices_[0].age;
    for (int i = 0; i < MAX_VOICES; i++) {
      const Voice *voice = voices_ + i;
      if (!is_active(voice->flags) && voice->pitch == pitch) {
        return i;
      }
      if (!is_active(voice->flags) && voice->env == 0) {
        return i;
      }
      if (voice->age < old_age) {
        old_age = voice->age;
        oldest = i;
      }
    }
    return oldest;
  }

  inline Voice *allocateOldestVoice(int16_t pitch, int16_t velocity) {
    int i = findFreeVoice(pitch);
    Voice *voice = voices_ + i;
    voice->setup(pitch, velocity);
    return voice;
  }

  inline void freeVoiceByPitch(int16_t pitch, int sustain_enabled) {
    int i = findVoiceByPitch(pitch);
    if (i >= 0) {
      if (sustain_enabled) {
        set_sustained(voices_[i].flags);
        reset_active(voices_[i].flags);
      } else {
        reset_active(voices_[i].flags);
        reset_sustained(voices_[i].flags);
      }
    }
  }

  inline Voice *allocateVoiceUnison(int16_t pitch, int16_t velocity) {
    Voice *primary = allocateOldestVoice(pitch, velocity);
    Voice *secondary = primary + 1;
    secondary->setup(pitch, velocity);
    set_secondary(secondary->flags);
    return primary;
  }

  inline void freeVoiceUnison(int16_t pitch, int sustain_enabled) {
    int i = findVoiceByPitch(pitch);
    if (i >= 0) {
      Voice *primary = voices_ + i;
      Voice *secondary = primary + 1;
      if (sustain_enabled) {
        set_sustained(primary->flags);
        set_sustained(secondary->flags);
        reset_active(primary->flags);
        reset_active(secondary->flags);
      } else {
        reset_sustained(primary->flags);
        reset_sustained(secondary->flags);
        reset_active(primary->flags);
        reset_active(secondary->flags);
        reset_secondary(secondary->flags);
      }
    }
  }

  inline void allocateVoiceMono(int16_t pitch, int16_t velocity) {
    static Voice *last = voices_ + (MAX_VOICES - 1);
    static Voice *head = last;
    reset_sustained(head->flags);
    reset_active(head->flags);

    if (head == last) {
      head = voices_; // circle to first voice
    } else {
      head++;
    }

    head->setup(pitch, velocity);
  }

  inline void freeVoiceMono(bool sustained) {
    for (uint8_t i = 0; i < MAX_VOICES; i++) {
      if (is_active(voices_[i].flags)) {
        if (sustained) {
          set_sustained(voices_[i].flags);
          reset_active(voices_[i].flags);
        } else {
          reset_active(voices_[i].flags);
          reset_sustained(voices_[i].flags);
        }
        break;
      }
    }
  }

  inline void resetAllVoices() {
    for (uint8_t i = 0; i < MAX_VOICES; i++) {
      Voice &voice = voices_[i];
      reset_active(voice.flags);
      reset_sustained(voice.flags);
      reset_last_trig(voice.flags);
      reset_secondary(voice.flags);
      voice.env = 0;
    }
  }

  inline int findVoiceByPitch(int16_t pitch) {
    for (int i = 0; i < MAX_VOICES; i++)
      if (is_active(voices_[i].flags) && voices_[i].pitch == pitch)
        return i;
    return -1;
  }
};
