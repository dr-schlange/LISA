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
  float velocity;
  float env;
  uint32_t age;

  Voice() {
    flags = 0b00000000;
    osc.Init(SAMPLE_RATE);
  }

  inline void setup(int16_t pitch_, float velocity_) {
    pitch = pitch_;
    velocity = velocity_;
    env = 0.f;
    set_active(flags);
    reset_secondary(flags);
    age = global_age++;
  }

  inline void render(int32_t mix[AUDIO_BLOCK], float timbre, float color,
                     float timb_slew, float color_slew, float fm_slew,
                     float unison_detune, float attackCoef, float releaseCoef,
                     int32_t block_gain) {
    if (!is_active(flags) && !is_sustained(flags) && env < 0.0001f)
      return;

    vel_smoothed_ += (velocity - vel_smoothed_) * 0.25f;

    int16_t oscpitch = pitch + (int16_t)(fm_slew * 1536.0f);
    if (is_secondary(flags)) {
      oscpitch += (int16_t)((unison_detune - 0.5f) * 128.0f);
    }
    osc.set_pitch(oscpitch);

    float t = constrain(timbre + timb_slew, 0.0f, 1.0f);
    float m = constrain(color + color_slew, 0.0f, 1.0f);
    osc.set_parameters(t * 32767.0f, m * 32767.0f);

    if (is_active(flags) && !is_last_trig(flags))
      osc.Strike();

    if (is_active(flags)) {
      set_last_trig(flags);
    } else {
      reset_last_trig(flags);
    }
    osc.Render(sync_buffer_, buffer_, AUDIO_BLOCK);

    float envTarget = (is_active(flags) || is_sustained(flags)) ? 1.0f : 0.0f;
    float coef = envTarget ? attackCoef : releaseCoef;

    const int32_t env_q15 = (int32_t)(env * 32767.0f);
    const int32_t vel_q15 = (int32_t)(vel_smoothed_ * 32767.0f);
    const int32_t amp = ((env_q15 * vel_q15) >> 15) * block_gain >> 15;

    for (int i = 0; i < AUDIO_BLOCK; i++) {
      env += (envTarget - env) * coef;
      if (envTarget == 0.0f && env < 0.0001f)
        env = 0.0f;

      mix[i] += (buffer_[i] * amp) >> 15;
    }
  }

  inline void resetPhase() { osc.reset_phase(); }

private:
  float vel_smoothed_;
  int16_t buffer_[AUDIO_BLOCK];
  uint8_t sync_buffer_[AUDIO_BLOCK];
};

class VoiceAllocator {
public:
  VoiceAllocator() {
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

  inline void renderAllVoices(int32_t mix[AUDIO_BLOCK], float timbre,
                              float color, float timbre_mod, float color_mod,
                              float fm_mod, float fm_slew_rate,
                              float unison_detune, float attackCoef,
                              float releaseCoef, int32_t block_gain) {
    applyStableSlew(fm_slew_, fm_mod, fm_slew_rate * 0.06f);
    applyStableSlew(timb_slew_, timbre_mod, 0.01f);
    applyStableSlew(color_slew_, color_mod, 0.01f);
    for (int v = 0; v < MAX_VOICES; v++) {
      voices_[v].render(mix, timbre, color, timb_slew_, color_slew_, fm_slew_,
                        unison_detune, attackCoef, releaseCoef, block_gain);
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

  inline void allocateVoice(int16_t pitch, float velocity) {
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
  Voice voices_[MAX_VOICES];
  VoiceMode mode_;
  float fm_slew_ = 0.f;
  float timb_slew_ = 0.f;
  float color_slew_ = 0.f;

  static inline void applyStableSlew(float &current, float target,
                                     float coefficient) {
    float diff = target - current;
    float abs_diff = fabsf(diff);
    if (abs_diff < 0.005f) {
      if (target == 0.0f && abs_diff < 0.01f)
        current = 0.0f;
      return;
    }
    if (abs_diff < 0.001f)
      current = target;
    else
      current += diff * coefficient;
  }

  inline int findFreeVoice(int16_t pitch) {
    int oldest = 0;
    uint32_t old_age = voices_[0].age;
    for (int i = 0; i < MAX_VOICES; i++) {
      const Voice *voice = voices_ + i;
      if (!is_active(voice->flags) && voice->pitch == pitch) {
        return i;
      }
      if (!is_active(voice->flags) && voice->env == 0.f) {
        return i;
      }
      if (voice->age < old_age) {
        old_age = voice->age;
        oldest = i;
      }
    }
    return oldest;
  }

  inline Voice *allocateOldestVoice(int16_t pitch, float velocity) {
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

  inline Voice *allocateVoiceUnison(int16_t pitch, float velocity) {
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

  inline void allocateVoiceMono(int16_t pitch, float velocity) {
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
      voice.env = 0.f;
    }
  }

  inline int findVoiceByPitch(int16_t pitch) {
    for (int i = 0; i < MAX_VOICES; i++)
      if (is_active(voices_[i].flags) && voices_[i].pitch == pitch)
        return i;
    return -1;
  }
};
