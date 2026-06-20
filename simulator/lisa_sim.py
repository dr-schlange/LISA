import numpy as np
import sounddevice as sd
from nallely.experimental.lisa_pico import Lisa as BaseLisa

print(sd.query_devices())
print("Default output:", sd.default.device)


class TermuxLisa(BaseLisa):
    def __init__(self, *args, **kwargs):
        kwargs["autoconnect"] = False
        kwargs["device_name"] = "Lisa"
        super().__init__(*args, **kwargs)

        self.out_stream = sd.OutputStream(
            samplerate=48000,
            channels=1,
            dtype="int16",
            blocksize=256,
            callback=self.audio_out,
        )
        self.phase = [0]
        self.out_stream.start()

    def audio_out(self, outdata, frames, time, status):
        t = (np.arange(frames) + self.phase[0]) / 48000
        outdata[:, 0] = (np.sin(2 * np.pi * 440 * t) * 32767).astype(np.int16)
        self.phase[0] += frames

    def stop(self):
        self.out_stream.stop()
        super().stop()
