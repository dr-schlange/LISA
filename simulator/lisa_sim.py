import numpy as np
import sounddevice as sd
from nallely.experimental.lisa_pico import Lisa as BaseLisa

# Display information to check for the soundcard to select later
print(sd.query_devices())
print("Default output:", sd.default.device)


class Wavetable:
    def __init__(self):
        self.write_pos = 0
        self.read_pos = 0
        self.buffer = np.zeros(257, dtype=np.int16)
        self.mode = "circular"

    def push_sample(self, value):
        if self.mode == "circular":
            self.buffer[self.write_pos] = value
            if self.write_pos == 0:
                self.buffer[256] = self.buffer[0]
            self.write_pos += 1
            if self.write_pos >= 256:
                self.write_pos = 0


class Voice:
    def __init__(self):
        self.pitch = 0.0
        self.active = False
        self.age = 0
        self.env = 0.0
        self.velocity = 0.0


class VoicesPool:
    def __init__(self):
        self.voices = [Voice(), Voice(), Voice(), Voice(), Voice(), Voice()]

    def allocate_voice(self, midi_note, velocity, mode):
        return getattr(self, f"allocate_{mode}_voice")(midi_note, velocity)

    def allocate_poly_voice(self, midi_note, velocity): ...

    def allocate_unison_voice(self, midi_note, velocity): ...

    def allocate_mono_voice(self, midi_note, velocity):
        voice = self.voices[0]
        # midi to freq conversion
        voice.pitch = 440.0 * 2 ** ((midi_note - 69) / 12.0)
        voice.active = True
        voice.velocity = velocity / 127.0
        return voice

    def deallocate_voice(self, midi_note, velocity, mode):
        return getattr(self, f"deallocate_{mode}_voice")(midi_note)

    def deallocate_poly_voice(self, midi_note, velocity): ...

    def deallocate_unison_voice(self, midi_note, velocity): ...

    def deallocate_mono_voice(self, _, velocity):
        voice = self.voices[0]
        voice.active = False
        return voice

    def __iter__(self):
        return iter(self.voices)

    def __len__(self):
        return len(self.voices)

    def __getitem__(self, i):
        return self.voices[i]


class Envelope:
    def __init__(self, sr):
        self.level = 0.0
        self.state = "idle"
        self.attack_rate = 1.0 / (0.01 * sr)
        self.release_rate = 1.0 / (0.3 * sr)


class LisaSim(BaseLisa):
    def __init__(self, *args, **kwargs):
        self.sr = 48000
        self.out_stream = sd.OutputStream(
            samplerate=self.sr,
            channels=2,
            dtype="int16",
            blocksize=256,
            callback=self.audio_out,
            device=8,
        )
        self.phase = 0
        self.env = Envelope(self.sr)
        self.wavetables = [Wavetable(), Wavetable(), Wavetable(), Wavetable()]
        self.voices_pool = VoicesPool()
        self.w1, self.w2, self.w3, self.w4 = self.bilinear_mapping_weight_computation(
            0.5, 0.5
        )
        self.gain, self.mastervol = 1.0, 1.0

        kwargs["autoconnect"] = False
        kwargs["device_name"] = "Lisa"
        super().__init__(*args, **kwargs)

        self.out_stream.start()

    def audio_out(self, outdata, frames, time, status):
        mix = np.zeros(frames, dtype=np.float32)
        active = 0
        gain = self.gain * self.mastervol

        for voice in self.voices_pool:
            freq = voice.pitch
            envl = self.env

            if not voice.active and voice.env < 0.0001:
                continue
            active += 1

            # compute phase increment from freq
            phase_inc = freq / self.sr * 256.0
            phases = (self.phase + np.arange(frames) * phase_inc) % 256
            idx = phases.astype(np.int32)
            frac = phases - idx

            # Interpolation
            # weights are updated when the related CC is received
            wave1 = self.wavetables[0].buffer.astype(np.float32)
            wave2 = self.wavetables[1].buffer.astype(np.float32)
            wave3 = self.wavetables[2].buffer.astype(np.float32)
            wave4 = self.wavetables[3].buffer.astype(np.float32)

            samples = (
                self.interpolate(wave1, idx, frac) * self.w1
                + self.interpolate(wave2, idx, frac) * self.w2
                + self.interpolate(wave3, idx, frac) * self.w3
                + self.interpolate(wave4, idx, frac) * self.w4
            )
            # envelope
            env = np.empty(frames, dtype=np.float32)
            for i in range(frames):
                if envl.state == "attack":
                    envl.level = min(1.0, envl.level + envl.attack_rate)
                    if envl.level >= 1.0:
                        # just to skip the attack computation
                        envl.state = "sustain"
                elif envl.state == "release":
                    envl.level = max(0.0, envl.level - envl.release_rate)
                    if envl.level <= 0.0:
                        envl.state = "idle"
                        voice.active = False
                voice.env = envl.level
                env[i] = envl.level

            self.phase = (self.phase + frames * phase_inc) % 256

            # write result
            mix += samples * env * voice.velocity

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

        self.env.state = "attack"
        self.voices_pool.allocate_voice(note, velocity, "mono")

    def note_off(self, note, velocity=127 // 2, channel=None):
        channel = channel if channel is not None else self.channel
        note = round(note)
        if note > 127:
            note = 127
        elif note < 0:
            note = 0

        self.env.state = "release"
        self.voices_pool.deallocate_voice(note, velocity, "mono")

    def allocate_voice(self, pitch): ...

    def control_change(self, control, value=0, channel=None):
        channel = channel if channel is not None else self.channel
        value = round(value)
        if value > 127:
            value = 127
        elif value < 0:
            value = 0
        self._update_state(control, value)

        if control == self.envelope.attack.parameter.cc_note:
            self.env.attack_rate = 1.0 / (max(0.001, value / 127.0 * 2.0) * 48000)
        elif control == self.envelope.release.parameter.cc_note:
            self.env.release_rate = 1.0 / (max(0.001, value / 127.0 * 2.0) * 48000)
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

    def interpolate(self, table, idx, phase):
        return table[idx] * (1 - phase) + table[(idx + 1) % 256] * phase


TermuxLisa = LisaSim
