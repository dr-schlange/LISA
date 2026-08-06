/*
  LISA (v0.3.0)

  Copyright (c) 2026 Dr Schlange
  Licensed under GNU GPLv3

  Based on VIJA by Vadims Maksimovs (ledlaux.github.com)
*/
#pragma once
// clang-format off
#include <pico/stdlib.h>
#include <pico/util/queue.h>
#include <BRAIDS.h>
#include <stmlib/stmlib.h>
#include <stmlib/utils/dsp.h>
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

enum VoiceCommandType : uint8_t {
  CMD_NOTE_ON,
  CMD_NOTE_OFF,
  CMD_SET_MODE,
  CMD_RESET_ALL_SUSTAIN,
  CMD_RESET_PHASES,
};

struct VoiceCommand {
  VoiceCommandType type;
  int16_t pitch;        // NOTE_ON / NOTE_OFF
  int16_t velocity;     // NOTE_ON
  bool sustain_enabled; // NOTE_OFF
  VoiceMode mode;       // SET_MODE
};

#define VOICE_CMD_QUEUE_DEPTH 32

static uint32_t global_age = 0;

// Same state-variable filter recurrence as braids::Svf, but with its own
// cutoff/damp tables generated for the actual SAMPLE_RATE instead of the
// 96000Hz reference baked into BRAIDS' shared lut_svf_cutoff/lut_svf_damp.
// This keeps the vendored library untouched; the tradeoff is that the
// internal digital-filter engine shapes (ZLPF/ZPKF/ZBPF/ZHPF) still use the
// original, uncorrected tables since they're internal to DigitalOscillator.
class LisaFilter {
public:
  static const int kTableSize = 258; // 257 + 1 guard entry for interpolation

  LisaFilter() {}

  static void InitTables() {
    if (tables_ready_)
      return;
    for (int i = 0; i < kTableSize - 1; i++) {
      float cutoff_hz = 440.0f * powf(2.0f, (i - 69) / 12.0f);
      float f = cutoff_hz / (float)SAMPLE_RATE;
      if (f > 1.0f / 8.0f)
        f = 1.0f / 8.0f;
      f = 2.0f * sinf(PI * f);
      float resonance_norm = i / 260.0f;
      float damp = fminf(2.0f * (1.0f - powf(resonance_norm, 0.25f)),
                         fminf(2.0f, 2.0f / f - f * 0.5f));
      cutoff_table_[i] = (uint16_t)(f * 32767.0f);
      damp_table_[i] = (uint16_t)(damp * 32767.0f);
    }
    cutoff_table_[kTableSize - 1] = cutoff_table_[kTableSize - 2];
    damp_table_[kTableSize - 1] = damp_table_[kTableSize - 2];
    tables_ready_ = true;
  }

  inline void Init() {
    lp_ = 0;
    bp_ = 0;
    frequency_ = 33 << 7;
    resonance_ = 16384;
    dirty_ = true;
    mode_ = braids::SVF_MODE_LP;
  }

  inline void set_frequency(int16_t frequency) {
    dirty_ = dirty_ || (frequency_ != frequency);
    frequency_ = frequency;
  }

  inline void set_resonance(int16_t resonance) {
    resonance_ = resonance;
    dirty_ = true;
  }

  inline void set_mode(braids::SvfMode mode) { mode_ = mode; }

  inline int32_t Process(int32_t in) {
    if (dirty_) {
      f_ = stmlib::Interpolate824(cutoff_table_, (uint32_t)frequency_ << 17);
      damp_ = stmlib::Interpolate824(damp_table_, (uint32_t)resonance_ << 17);
      dirty_ = false;
    }
    int32_t f = f_;
    int32_t damp = damp_;
    int32_t notch = in - (bp_ * damp >> 15);
    lp_ += f * bp_ >> 15;
    CLIP(lp_)
    int32_t hp = notch - lp_;
    bp_ += f * hp >> 15;
    CLIP(bp_)
    return mode_ == braids::SVF_MODE_BP
               ? bp_
               : (mode_ == braids::SVF_MODE_HP ? hp : lp_);
  }

private:
  static uint16_t cutoff_table_[kTableSize];
  static uint16_t damp_table_[kTableSize];
  static bool tables_ready_;

  bool dirty_;
  int16_t frequency_;
  int16_t resonance_;
  int32_t f_;
  int32_t damp_;
  int32_t lp_;
  int32_t bp_;
  braids::SvfMode mode_;
};

uint16_t LisaFilter::cutoff_table_[LisaFilter::kTableSize];
uint16_t LisaFilter::damp_table_[LisaFilter::kTableSize];
bool LisaFilter::tables_ready_ = false;

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
    env = 0;
    vel_smoothed_ = 0;
    osc.Init(SAMPLE_RATE);
    LisaFilter::InitTables();
    filter_.Init();
  }

  inline void setup(int16_t pitch_, int16_t velocity_) {
    pitch = pitch_;
    velocity = velocity_;
    set_active(flags);
    reset_secondary(flags);
    age = global_age++;
  }

  inline void render(int32_t mix[AUDIO_BLOCK], int16_t timbre, int16_t color,
                     int16_t timb_slew, int16_t color_slew, int16_t fm_slew,
                     float unison_detune, int16_t attackCoef,
                     int16_t releaseCoef, int32_t block_gain, int32_t cut_slew,
                     int32_t res_slew, braids::SvfMode filter_type,
                     int32_t dry_scale, int32_t wet_scale) {
    if (!is_active(flags) && !is_sustained(flags) && env < ENV_EPSILON_Q15)
      return;

    filter_.set_frequency((int16_t)cut_slew);
    filter_.set_resonance((int16_t)res_slew);
    filter_.set_mode(filter_type);

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

    for (int i = 0; i < AUDIO_BLOCK; i++) {
      env += ((int16_t)((int32_t)envTarget - (int32_t)env)) * coef >> 15;
      if (envTarget == 0 && env < ENV_EPSILON_Q15)
        env = 0;

      const int32_t amp =
          (((int32_t)env * (int32_t)vel_smoothed_) >> 15) * block_gain >> 15;

      const int32_t dry_sample = buffer_[i];
      const int32_t wet_sample = filter_.Process(dry_sample);
      const int32_t filtered =
          ((dry_sample * dry_scale) >> 15) + ((wet_sample * wet_scale) >> 15);

      mix[i] += (filtered * amp) >> 15;
    }
  }

  inline void resetPhase() { osc.reset_phase(); }

private:
  LisaFilter filter_;
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
    queue_init(&cmd_queue_, sizeof(VoiceCommand), VOICE_CMD_QUEUE_DEPTH);
  }

  inline void enqueueNoteOn(int16_t pitch, int16_t velocity) {
    VoiceCommand cmd{CMD_NOTE_ON, pitch, velocity, false, VOICE_POLY};
    queue_try_add(&cmd_queue_, &cmd);
  }

  inline void enqueueNoteOff(int16_t pitch, bool sustain_enabled) {
    VoiceCommand cmd{CMD_NOTE_OFF, pitch, 0, sustain_enabled, VOICE_POLY};
    queue_try_add(&cmd_queue_, &cmd);
  }

  inline void enqueueSetMode(VoiceMode mode) {
    VoiceCommand cmd{CMD_SET_MODE, 0, 0, false, mode};
    queue_try_add(&cmd_queue_, &cmd);
  }

  inline void enqueueResetAllSustain() {
    VoiceCommand cmd{CMD_RESET_ALL_SUSTAIN, 0, 0, false, VOICE_POLY};
    queue_try_add(&cmd_queue_, &cmd);
  }

  inline void enqueueResetPhases() {
    VoiceCommand cmd{CMD_RESET_PHASES, 0, 0, false, VOICE_POLY};
    queue_try_add(&cmd_queue_, &cmd);
  }

  inline void renderAllVoices(int32_t mix[AUDIO_BLOCK]) {
    VoiceCommand cmd;
    while (queue_try_remove(&cmd_queue_, &cmd)) {
      applyCommand(cmd);
    }

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

    const int32_t cut_t = (int32_t)(gstate->cutoff.value * 32767.f);
    const int32_t res_t = (int32_t)(gstate->resonance.value * 32767.f);
    const int32_t mix_t = gstate->filter_enabled ? 32767 : 0;

    cut_slew_ += ((cut_t - cut_slew_) * 1638) >> 15; // 1638 = 0.05f * 32767
    res_slew_ += ((res_t - res_slew_) * 1638) >> 15;
    mix_slew_ += ((mix_t - mix_slew_) * 327) >> 15; // 327 = 0.01f * 32767

    if (gstate->filter_type.value != last_filter_type_knob_) {
      filter_type_ =
          (braids::SvfMode)midi_get_group(gstate->filter_type.value * 127.f, 3);
      last_filter_type_knob_ = gstate->filter_type.value;
    }

    const int32_t dry_scale = 32767 - mix_slew_;
    const int32_t wet_scale = mix_slew_;

    for (int v = 0; v < MAX_VOICES; v++) {
      voices_[v].render(mix, timb_q15, col_q15, timb_slew_, color_slew_,
                        fm_slew_, gstate->unison_detune.value, attackCoef_,
                        releaseCoef_, block_gain, cut_slew_, res_slew_,
                        filter_type_, dry_scale, wet_scale);
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

private:
  const RuntimeState *gstate_;
  Voice voices_[MAX_VOICES];
  VoiceMode mode_;
  queue_t cmd_queue_;
  int16_t fm_slew_ = 0;
  int16_t timb_slew_ = 0;
  int16_t color_slew_ = 0;
  int16_t attackCoef_ = 0;
  int16_t releaseCoef_ = 0;
  float last_atk_ = -1.f;
  float last_rel_ = -1.f;
  int32_t cut_slew_ = 0;
  int32_t res_slew_ = 0;
  int32_t mix_slew_ = 0;
  braids::SvfMode filter_type_ = braids::SVF_MODE_LP;
  float last_filter_type_knob_ = -1.f;

  inline void applyCommand(const VoiceCommand &cmd) {
    switch (cmd.type) {
    case CMD_NOTE_ON:
      allocateVoice(cmd.pitch, cmd.velocity);
      break;
    case CMD_NOTE_OFF:
      freeVoice(cmd.pitch, cmd.sustain_enabled);
      break;
    case CMD_SET_MODE:
      setMode(cmd.mode);
      break;
    case CMD_RESET_ALL_SUSTAIN:
      resetAllSustain();
      break;
    case CMD_RESET_PHASES:
      resetPhases();
      break;
    }
  }

  inline void setMode(VoiceMode mode) {
    if (mode_ != mode) {
      mode_ = mode;
      resetAllVoices();
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

  inline int findFreeVoicePair(int16_t pitch) {
    int oldest = 0;
    uint32_t old_age = voices_[0].age;
    for (int i = 0; i < MAX_VOICES; i += 2) {
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

  inline int findVoicePairByPitch(int16_t pitch) {
    for (int i = 0; i < MAX_VOICES; i += 2)
      if (is_active(voices_[i].flags) && voices_[i].pitch == pitch)
        return i;
    return -1;
  }

  inline Voice *allocateVoiceUnison(int16_t pitch, int16_t velocity) {
    int i = findFreeVoicePair(pitch);
    Voice *primary = voices_ + i;
    Voice *secondary = primary + 1;
    primary->setup(pitch, velocity);
    secondary->setup(pitch, velocity);
    set_secondary(secondary->flags);
    return primary;
  }

  inline void freeVoiceUnison(int16_t pitch, int sustain_enabled) {
    int i = findVoicePairByPitch(pitch);
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
