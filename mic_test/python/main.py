import sys
import time
import threading
import numpy as np
import sounddevice as sd
from arduino.app_utils import Bridge, App

SAMPLING_RATE = 44100
CHANNELS = 4
BLOCK_SIZE = 8192
FRAME_DELAY = 0.050  # 20 FPS

# Adjust this sensitivity based on your Behringer gain knobs (higher = more sensitive)
GAIN_MULTIPLIER = 500.0

# Thread-safe storage for latest volume levels
latest_levels = [0, 0, 0, 0]
levels_lock = threading.Lock()

print("Python process initializing...", flush=True)
time.sleep(4)  # Give the Uno Q plenty of time to clear setup()


def audio_callback(indata, frames, time_info, status):
    global latest_levels
    if status:
        print(f"ALSA Sound Warning: {status}", file=sys.stderr, flush=True)

    # Calculate RMS for all 4 channels at once using vectorized numpy math
    norm = indata.astype(np.float32) / 32768.0
    rms_values = np.sqrt(np.mean(norm * norm, axis=0))
    # Scale to 0 - 56 range to match the Arduino matrix input
    scaled = np.clip(rms_values * GAIN_MULTIPLIER, 0, 56).astype(int)

    with levels_lock:
        latest_levels[0] = int(scaled[0])
        latest_levels[1] = int(scaled[1])
        latest_levels[2] = int(scaled[2])
        latest_levels[3] = int(scaled[3])


def get_device():
    sd._terminate()  # refresh PortAudio so replugged USB devices show up
    sd._initialize()
    for dev in ('hw:1,0', 'hw:0,0', 'hw:2,0'):
        try:
            sd.check_input_settings(device=dev, channels=CHANNELS, samplerate=SAMPLING_RATE, dtype='int16')
            return dev
        except Exception:
            pass
    raise RuntimeError("Behringer not found")


def main_loop():
    lastangle = None
    while True:
        try:
            dev = get_device()
            print(f"Opening audio stream via {dev}...", flush=True)
            with sd.InputStream(device=dev,
                                channels=CHANNELS,
                                samplerate=SAMPLING_RATE,
                                blocksize=BLOCK_SIZE,
                                latency=0.05,
                                dtype='int16',
                                callback=audio_callback):
                print("Audio link active. Processing...", flush=True)

                while True:
                    loop_start = time.time()

                    with levels_lock:
                        v1, v2, v3, v4 = latest_levels

                    try:
                        angle = Bridge.notify("update_matrix_audio", v1, v2, v3, v4)
                        if angle is not None and angle != lastangle:
                            lastangle = angle
                            #print(f"Angle: {angle:.2f}°", flush=True)
                    except Exception as rpc_err:
                        print(f"BUS EXCEPTION: {rpc_err}", file=sys.stderr, flush=True)

                    # Maintain steady 20 FPS without blocking the audio callback
                    elapsed = time.time() - loop_start
                    sleep_time = FRAME_DELAY - elapsed
                    if sleep_time > 0:
                        time.sleep(sleep_time)
            print(a)
        except Exception as e:
            print(f"Audio Stream Error: {e}. Retrying in 2 seconds...", file=sys.stderr, flush=True)
            time.sleep(2)


App.run(user_loop=main_loop)
