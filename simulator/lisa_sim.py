import numpy as np
import sounddevice as sd
from nallely.experimental.lisa_pico import Lisa as BaseLisa

# Display information to check for the soundcard to select later
print(sd.query_devices())
print("Default output:", sd.default.device)


def interpolate(table, idx, phase):
    return table[idx] * (1 - phase) + table[(idx + 1) % 256] * phase


class Wavetable:
    def __init__(self):
        self.write_pos = 0
        self.read_pos = 0
        self.buffer = np.zeros(257, dtype=np.int16)
        self.mode = "circular"
        self.freeze = False
        self.last_write_pos = 0
        self.last_write_value = 0

    def push_sample(self, value):
        if self.freeze:
            return
        match self.mode:
            case "circular":
                self.buffer[self.write_pos] = value
                if self.write_pos == 0:
                    self.buffer[256] = self.buffer[0]
                self.write_pos += 1
                if self.write_pos >= 256:
                    self.write_pos = 0
            case "scroll":
                self.buffer[:256] = self.buffer[1:256]
                self.buffer[255] = value
                self.buffer[256] = self.buffer[0]
            case "manual":
                self.buffer[self.write_pos] = value
                self.buffer[self.write_pos + 1] = value
                if self.write_pos == 0:
                    self.buffer[256] = value
            case "manual_interpolated":
                span = abs(self.write_pos - self.last_write_pos)
                if span == 0:
                    self.buffer[self.write_pos] = value
                    return
                diff = value - self.last_write_value
                direction = 1 if self.write_pos > self.last_write_pos else -1
                t = self.last_write_pos
                while t != self.write_pos:
                    self.buffer[t] = (
                        self.last_write_value
                        + diff * abs(t - self.last_write_pos) / span
                    )
                    if t == 0:
                        self.buffer[256] = self.buffer[t]
                    t += direction
                self.buffer[self.write_pos] = value
                if self.write_pos == 0:
                    self.buffer[256] = value
                self.last_write_pos = self.write_pos
                self.last_write_value = value

    def clear(self):
        self.buffer.fill(0)

    def reset_write_pos(self):
        self.write_pos = 0


class Envelope:
    def __init__(self, voice):
        self.level = 0.0
        self.state = "idle"
        self.voice = voice

    def render(self, frames, attack_rate, release_rate):
        env = np.empty(frames, dtype=np.float32)
        voice = self.voice
        for i in range(frames):
            if self.state == "attack":
                self.level = min(1.0, self.level + attack_rate)
                if self.level >= 1.0:
                    # just to skip the attack computation
                    self.state = "sustain"
            elif self.state == "release":
                self.level = max(0.0, self.level - release_rate)
                if self.level <= 0.0:
                    self.state = "idle"
                    voice.active = False
            voice.env = self.level
            env[i] = self.level
        return env


class Voice:
    def __init__(
        self,
    ):
        self.pitch = 0.0
        self.active = False
        self.age = 0
        self.env = 0.0
        self.velocity = 0.0
        self.secondary = False
        self.envelope = Envelope(self)
        self.phase = 0.0
        self.interpolate = interpolate

    def render_envelope(self, frames, attack_rate, release_rate):
        return self.envelope.render(frames, attack_rate, release_rate)

    def render(self, weighted_waves, frames, sr, attack, release, detune):
        # compute phase increment from freq
        pitch = self.pitch
        if self.secondary:
            pitch += (detune - 0.5) * 100 * 1.28
        phase_inc = pitch / sr * 256.0
        phases = (self.phase + np.arange(frames) * phase_inc) % 256
        idx = phases.astype(np.int32)
        frac = phases - idx
        self.phase = (self.phase + frames * phase_inc) % 256

        # Interpolation
        # weights are updated when the related CC is received
        waves, weights, levels = weighted_waves
        interpolate = self.interpolate
        samples = (
            interpolate(waves[0], idx, frac) * weights[0] * levels[0]
            + interpolate(waves[1], idx, frac) * weights[1] * levels[1]
            + interpolate(waves[2], idx, frac) * weights[2] * levels[2]
            + interpolate(waves[3], idx, frac) * weights[3] * levels[3]
        )
        # envelope
        env = self.render_envelope(frames, attack, release)

        return samples * env * self.velocity


class VoicesPool:
    def __init__(self):
        self.voices = [Voice(), Voice(), Voice(), Voice(), Voice(), Voice()]
        self.ages = 0

    def allocate_voice(self, midi_note, velocity, mode):
        pitch = 440.0 * 2 ** ((midi_note - 69) / 12.0)
        return getattr(self, f"allocate_{mode}_voice")(pitch, velocity)

    def find_free_voice(self, pitch):
        oldest = (self.voices[0], 0)
        for i, voice in enumerate(self.voices):
            if not voice.active and voice.pitch == pitch:
                return (voice, i)
            if not voice.active and voice.env == 0:
                return (voice, i)
            if voice.age < oldest[0].age:
                oldest = (voice, i)
        return oldest

    def setup_voice(self, voice, pitch, velocity):
        voice.envelope.state = "attack"
        voice.pitch = pitch
        voice.active = True
        voice.env = 0.0
        voice.secondary = False
        voice.velocity = velocity / 127.0
        self.ages += 1
        voice.age = self.ages
        return voice

    def allocate_poly_voice(self, pitch, velocity):
        # allocates the oldest voice
        voice, _ = self.find_free_voice(pitch)
        return self.setup_voice(voice, pitch, velocity)

    def allocate_unison_voice(self, pitch, velocity):
        # find the oldest voice and its neigboor
        voice, i = self.find_free_voice(pitch)
        # we ensure we get a correct pair in case of voice stealing
        i = min(i, len(self.voices) - 2)
        voice = self.setup_voice(voice, pitch, velocity)
        voice2 = self.setup_voice(self.voices[i + 1], pitch, velocity)
        voice2.secondary = True
        return voice

    def allocate_mono_voice(self, pitch, velocity):
        voice = self.voices[0]
        voice.envelope.state = "attack"
        voice.pitch = pitch
        voice.active = True
        voice.velocity = velocity / 127.0
        return voice

    def deallocate_voice(self, midi_note, velocity, mode):
        pitch = 440.0 * 2 ** ((midi_note - 69) / 12.0)
        return getattr(self, f"deallocate_{mode}_voice")(pitch, velocity)

    def deallocate_poly_voice(self, pitch, velocity):
        voice = next((v for v in self.voices if v.pitch == pitch), None)
        if not voice:
            return None
        voice.active = False
        voice.envelope.state = "release"
        return voice

    def deallocate_unison_voice(self, pitch, velocity):
        voice, i = next(
            ((v, i) for i, v in enumerate(self.voices) if v.pitch == pitch), (None, -1)
        )
        if not voice:
            return None
        voice.active = False
        voice.envelope.state = "release"
        voice2 = self.voices[i + 1]
        voice2.active = False
        voice2.envelope.state = "release"

    def deallocate_mono_voice(self, _, velocity):
        voice = self.voices[0]
        voice.active = False
        voice.envelope.state = "release"
        return voice

    def disable_all(self):
        for voice in self.voices:
            voice.active = False
            voice.secondary = False
            voice.age = self.ages
            voice.envelope.state = "release"
            # voice.env = 0.0

    def __iter__(self):
        return iter(self.voices)

    def __len__(self):
        return len(self.voices)

    def __getitem__(self, i):
        return self.voices[i]


class LisaSim(BaseLisa):
    def __init__(self, *args, **kwargs):
        self.sr = 48000
        self.out_stream = sd.OutputStream(
            samplerate=self.sr,
            channels=2,
            dtype="int16",
            blocksize=256,
            callback=self.audio_out,
            # device=8,
        )
        self.wavetables = [Wavetable(), Wavetable(), Wavetable(), Wavetable()]
        self.voices_pool = VoicesPool()
        self.w1, self.w2, self.w3, self.w4 = self.bilinear_mapping_weight_computation(
            0.5, 0.5
        )
        self.levels = [1.0] * 4
        self.gain, self.mastervol = 1.0, 1.0
        self.attack_rate = 1.0 / (0.01 * self.sr)
        self.release_rate = 1.0 / (0.3 * self.sr)
        self.detune = 0.8

        kwargs["autoconnect"] = False
        kwargs["device_name"] = "Lisa"
        super().__init__(*args, **kwargs)

        self.out_stream.start()

    def audio_out(self, outdata, frames, time, status):
        mix = np.zeros(frames, dtype=np.float32)
        active = 0
        gain = self.gain * self.mastervol
        attack = self.attack_rate
        release = self.release_rate
        wavetables = (
            self.wavetables[0].buffer.astype(np.float32),
            self.wavetables[1].buffer.astype(np.float32),
            self.wavetables[2].buffer.astype(np.float32),
            self.wavetables[3].buffer.astype(np.float32),
        )
        weights = (self.w1, self.w2, self.w3, self.w4)
        levels = self.levels
        sr = self.sr
        detune = self.detune
        for voice in self.voices_pool:
            if not voice.active and voice.env < 0.0001:
                continue
            active += 1

            mix += voice.render(
                (wavetables, weights, levels), frames, sr, attack, release, detune
            )

        # final mix of all the voices
        if active > 0:
            mix /= active
        result = np.clip(mix * gain, -32768, 32767).astype(np.int16)
        outdata[:, 0] = result
        outdata[:, 1] = result

    def stop(self):
        self.out_stream.stop()
        super().stop()

    def pitchwheel(self, pitch, channel=None):
        channel = channel if channel is not None else self.channel
        pitch = round(pitch)
        if pitch > 8191:
            pitch = 8191
        elif pitch < -8192:
            pitch = -8192
        if 0 <= channel <= 3:
            self.wavetables[channel].push_sample(pitch)

    def note_on(self, note, velocity=127 // 2, channel=None):
        channel = channel if channel is not None else self.channel
        note = round(note)
        if note > 127:
            note = 127
        elif note < 0:
            note = 0

        mode = str(self.general.voice_mode)
        self.voices_pool.allocate_voice(note, velocity, mode)

    def note_off(self, note, velocity=127 // 2, channel=None):
        channel = channel if channel is not None else self.channel
        note = round(note)
        if note > 127:
            note = 127
        elif note < 0:
            note = 0

        mode = str(self.general.voice_mode)
        self.voices_pool.deallocate_voice(note, velocity, mode)

    def control_change(self, control, value=0, channel=None):
        channel = channel if channel is not None else self.channel
        value = round(value)
        if value > 127:
            value = 127
        elif value < 0:
            value = 0
        self._update_state(control, value)

        if control == self.envelope.attack.parameter.cc_note:
            self.attack_rate = 1.0 / (max(0.001, value / 127.0 * 2.0) * 48000)
        elif control == self.envelope.release.parameter.cc_note:
            self.release_rate = 1.0 / (max(0.001, value / 127.0 * 2.0) * 48000)
        elif control == self.modulation.timbre.parameter.cc_note:
            self.bilinear_mapping_weight_computation(
                value / 127.0, self.modulation.color / 127.0
            )
        elif control == self.modulation.color.parameter.cc_note:
            self.bilinear_mapping_weight_computation(
                self.modulation.timbre / 127.0, value / 127.0
            )
        elif control == self.general.master_volume.parameter.cc_note:
            self.mastervol = value / 127.0
        elif control == self.general.gain.parameter.cc_note:
            self.gain = value / 64.0
        elif control == self.wavetable.freeze_all.parameter.cc_note:
            for table in self.wavetables:
                table.freeze = value > 64
        elif (
            self.wavetable.freeze_wt1.parameter.cc_note
            <= control
            <= self.wavetable.freeze_wt4.parameter.cc_note
        ):
            i = self.wavetable.freeze_wt1.parameter.cc_note
            self.wavetables[control - i].freeze = value > 64
        elif control == self.general.voice_mode.parameter.cc_note:
            self.voices_pool.disable_all()
        elif control == self.general.detune.parameter.cc_note:
            self.detune = value / 127.0
        elif (
            self.wavetable.mode_wt1.parameter.cc_note
            <= control
            <= self.wavetable.mode_wt4.parameter.cc_note
        ):
            i = self.wavetable.mode_wt1.parameter.cc_note
            mode = self.wavetable.mode_wt1.parameter.map2accepted_values(value)
            self.wavetables[control - i].mode = mode
        elif (
            self.wavetable.index_wt1.parameter.cc_note
            <= control
            <= self.wavetable.index_wt4.parameter.cc_note
        ):
            i = self.wavetable.index_wt1.parameter.cc_note
            self.wavetables[control - i].write_pos = value * 2
        elif control == self.wavetable.reset_all_wt.parameter.cc_note:
            for table in self.wavetables:
                table.clear()
        elif control == self.wavetable.reset_all_write_idx.parameter.cc_note:
            for table in self.wavetables:
                table.reset_write_pos()
        elif (
            self.wavetable.level_table1.parameter.cc_note
            <= control
            <= self.wavetable.level_table4.parameter.cc_note
        ):
            i = self.wavetable.level_table1.parameter.cc_note
            self.levels[control - i] = value / 127.0

    def bilinear_mapping_weight_computation(self, x, y):
        x = 0.5 + (x - 0.5) * 0.5
        y = 0.5 + (y - 0.5) * 0.5

        x = x * x
        y = y * y

        inv_x = 1 - x
        inv_y = 1 - y

        self.w1 = inv_x * inv_y
        self.w2 = x * inv_y
        self.w3 = inv_x * y
        self.w4 = x * y
        return self.w1, self.w2, self.w3, self.w4

    def force_all_notes_off(self, times=1):
        self.voices_pool.disable_all()


TermuxLisa = LisaSim
