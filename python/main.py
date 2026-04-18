from arduino.app_utils import App, Bridge
from arduino.app_bricks.web_ui import WebUI
from ecgdetectors import Detectors
from collections import deque

ui = WebUI() # Setup web application interface
ecg_enabled = False # ECG enabled flag

SAMPLE_RATE = 200 # Sampling rate for ECG data
WINDOW = SAMPLE_RATE * 10 # Length of window for calculating the Heart rate of person
EXPECTED_RAW = 256 # Expected length of the raw ECG buffer
EXPECTED_FILTERED = 256 # Expected length of the filtered ECG buffer
EXPECTED_FFT = 128 # Expected number of FFT bins

rolling_buffer = deque(maxlen=WINDOW) # Double ended queue of length window
detectors = Detectors(SAMPLE_RATE) # Setup the pan-tompkins library

'''
ECG callback provided to the MCU for transferring data from MCU to MPU
Accepts a tuple of raw, filtered, and FFT for ECG data. Adds buffers to
a rolling window for checking heart rate of person.
Sends raw, filtered, and FFT to the web application dashboard for display.
Sends heart rate and frequency information to the dashboard once pan tompkins
has run successfully.

:param: samples - Tuple of raw and filtered samples as well as FFT bins of the filtered ECG
:return:
'''
def ecg_callback(samples):
    raw_ecg, filtered_ecg, fft_ecg = samples
    if len(raw_ecg) != EXPECTED_RAW:
        print(f"Bad raw ECG buffer: expected {EXPECTED_RAW}, got {len(raw_ecg)}", flush=True)
        return
    if len(filtered_ecg) != EXPECTED_FILTERED:
        print(f"Bad filtered ECG buffer: expected {EXPECTED_FILTERED}, got {len(filtered_ecg)}", flush=True)
        return
    if len(fft_ecg) != EXPECTED_FFT:
        print(f"Bad FFT buffer: expected {EXPECTED_FFT}, got {len(fft_ecg)}", flush=True)
        return

    rolling_buffer.extend(filtered_ecg)

    if len(rolling_buffer) == WINDOW:
        try:
            r_peaks = detectors.pan_tompkins_detector(list(rolling_buffer))
            if len(r_peaks) >= 2:
                rr_intervals = [r_peaks[i+1] - r_peaks[i] for i in range(len(r_peaks)-1)]
                avg_rr = sum(rr_intervals) / len(rr_intervals)
                bpm = round((SAMPLE_RATE / avg_rr) * 60)
                peak_freq = round(SAMPLE_RATE / avg_rr, 2)
                ui.send_message("heart_rate", {"bpm": bpm, "peak_freq": peak_freq})
        except Exception as e:
            print(f"Detection error: {e}", flush=True)
    
    ui.send_message("raw_ecg", raw_ecg)
    ui.send_message("filtered_ecg", filtered_ecg)
    ui.send_message("fft_ecg", fft_ecg)
    
'''
Handler for the start/shutdown of the ECG sensor.
Receives request from the web application and sends to the MCU.
Sends updated ECG sensor state to the web application.
'''
def handle_set_ecg_enabled(sid, state):
    print(f"Received set_ecg_enabled: {state}", flush=True)
    Bridge.call("setECGEnabled", state)
    ui.send_message("ecg_state", state)


ui.on_message("set_ecg_enabled", handle_set_ecg_enabled) # Socket check for set_ecg_enabled message
Bridge.provide("ecg_packet", ecg_callback) # Provide the ECG callback to the MCU
App.run() # Run the Python application
