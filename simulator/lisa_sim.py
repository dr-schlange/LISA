import numpy as np
import sounddevice as sd
from nallely.experimental.lisa_pico import Lisa as BaseLisa

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


class Envelope:
    def __init__(self, sr):
        self.level = 0.0
        self.state = "idle"
        self.attack_rate = 1.0 / (0.01 * sr)
        self.release_rate = 1.0 / (0.3 * sr)


class TermuxLisa(BaseLisa):
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
        self.phase = 0
        self.env = Envelope(self.sr)
        self.wavetables = [Wavetable(), Wavetable(), Wavetable(), Wavetable()]
        self.voices = [Voice(), Voice(), Voice(), Voice(), Voice(), Voice()]

        kwargs["autoconnect"] = False
        kwargs["device_name"] = "Lisa"
        super().__init__(*args, **kwargs)

        self.out_stream.start()

    def audio_out(self, outdata, frames, time, status):
        voice = self.voices[0]
        freq = voice.pitch

        # compute phase increment from freq
        phase_inc = freq / self.sr * 256.0
        phases = (self.phase + np.arange(frames) * phase_inc) % 256
        idx = phases.astype(np.int32)
        frac = phases - idx

        # perform bilinear mix here
        table = self.wavetables[0].buffer.astype(np.float32)

        # interpolation
        samples = table[idx] * (1 - frac) + table[(idx + 1) % 256] * frac

        # envelope
        env = np.empty(frames, dtype=np.float32)
        envl = self.env
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
            env[i] = envl.level

        self.phase = (self.phase + frames * phase_inc) % 256

        # write result & increment phase
        result = (samples * env).astype(np.int16)
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

        # midi to freq conversion
        voice = self.voices[0]
        voice.pitch = 440.0 * 2 ** ((note - 69) / 12.0)
        voice.active = True

    def note_off(self, note, velocity=127 // 2, channel=None):
        channel = channel if channel is not None else self.channel
        note = round(note)
        if note > 127:
            note = 127
        elif note < 0:
            note = 0

        self.env.state = "release"
        voice = self.voices[0]
        voice.active = False

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
