'''
:file: main.py

Sets up the web application for analysis of incoming ECG signals from the AD8232.
Provides callbacks for the MCU on the Arduino Uno Q and handlers for socket connections
to the JavaScript frontend.
'''
import os
import json
import math
from collections import deque

import numpy as np
from scipy.signal import iirfilter, sosfilt, freqz_sos
from numpy.linalg import eigvals

from arduino.app_utils import App, Bridge
from arduino.app_bricks.web_ui import WebUI
from ecgdetectors import Detectors


ui = WebUI() # Setup web application interface

SAMPLE_RATE = 128 # Sampling rate for ECG data
WINDOW = SAMPLE_RATE * 10 # Length of window for calculating the Heart rate of person
EXPECTED_RAW = 256 # Expected length of the raw ECG buffer
EXPECTED_FILTERED = 256 # Expected length of the filtered ECG buffer
EXPECTED_FFT = 128 # Expected number of FFT bins

LOWCUT_HZ = 0.5
HIGHCUT_HZ = 40

SUPPORTED_FILTERS = {"butter", "cheby1", "cheby2", "ellip"}

MAX_SOS_STAGES = 33

FILTER_CONFIG_PATH = os.path.join(
    os.path.dirname(__file__), "filter_config.json"
)

DEFAULT_FILTER_CONFIG = {
    "type": "butter",
    "order": 4,
}

rolling_buffer = deque(maxlen=WINDOW) # Double ended queue of length window
detectors = Detectors(SAMPLE_RATE) # Setup the pan-tompkins library


def compute_sos(filter_type: str, order: int) -> np.ndarray:
    '''
    Compute SOS coefficients for a bandpass IIR filter.
    '''
    if filter_type not in SUPPORTED_FILTERS:
        raise ValueError(
            f"Unsupported filter type '{filter_type}'."
            f"Must be one of: {sorted(SUPPORTED_FILTERS)}"
                         )
    if not isinstance(order, int) or order < 1:
        raise ValueError(f"Filter order must be a positive integer, got {order}")
    
    nyq = SAMPLE_RATE / 2
    low = LOWCUT_HZ / nyq
    high = HIGHCUT_HZ / nyq

    kwargs = {}
    if filter_type in ("cheby1", "ellip"):
        kwargs["rp"] = 1.0
    if filter_type in ("cheby2", "ellip"):
        kwargs["rs"] = 40.0

    sos = iirfilter(
        order,
        [low, high],
        btype="band",
        ftype=filter_type,
        output="sos",
        **kwargs
    )
    return sos


def validate_sos(sos: np.ndarray) -> tuple[bool, str | None]:
    '''
    Validate SOS coefficient array.
    '''
    if sos.ndim != 2 or sos.shape[1] != 6:
        return False, f"SOS array must have shape (n_stages, 6), got {sos.shape}"
    if not np.all(np.isfinite(sos)):
        return False, "SOS coefficients contain NaN or Inf values"
    if sos.shape[0] > MAX_SOS_STAGES:
        return False, (
            f"Filter requires {sos.shape[0]} SOS stages which exceeds "
            f"the Bridge message size limit of {MAX_SOS_STAGES} stages. "
            f"Reduce the filter order."
        )
    for i, section in enumerate(sos):
        a_coeffs = section[3:]
        poles = np.roots(a_coeffs)
        if np.any(np.abs(poles) >= 1.0):
            return False, (
                f"Filter is unstable. SOS stage {i} has poles outside or on "
                f"the unit circle: {np.abs(poles)}."
            )
    return True, None


def compute_frequency_response(sos: np.ndarray) -> dict:
    '''
    Compute the frequency response of a filter for the dashboard.
    '''
    worN = 512
    w, h = freqz_sos(sos, worN=worN, fs=SAMPLE_RATE)
    mag_db = 20 * np.log10(np.abs(h) + 1e-12)

    return {
        "frequencies": w.tolist(),
        "magnitude_db": mag_db.tolist(),
        "lowcut_hz": LOWCUT_HZ,
        "highcut_hz": HIGHCUT_HZ,
        "nyquist_hz": SAMPLE_RATE/2,
    }


def sos_to_bridge_format(sos: np.ndarray) -> list:
    '''
    Convert SOS array to a list for Bridge transmission.
    '''
    flat = [int(sos.shape[0])]
    for stage in sos:
        flat.extend(stage.tolist())
    return flat
    

def save_filter_config(filter_type: str, order: int, sos: np.ndarray):
    '''
    Persist filter configuration to JSON file.
    '''
    config = {
        "type": filter_type,
        "order": order,
        "sos": sos.tolist(),
    }
    with open(FILTER_CONFIG_PATH, "w") as f:
        json.dump(config, f, indent=2)
    print(f"Filter config saved: {filter_type} order: {order}", flush=True)


def load_filter_config() -> tuple[str, int, np.ndarray]:
    '''
    Load filter configuration from JSON file.
    '''
    if os.path.exists(FILTER_CONFIG_PATH):
        try:
            with open(FILTER_CONFIG_PATH, "r") as f:
                config = json.load(f)
            filter_type = config["type"]
            order = config["order"]
            sos = np.array(config["sos"])
            valid, err = validate_sos(sos)
            if not valid:
                raise ValueError(f"Loaded SOS failed validation: {err}")
            print(f"Loaded filter config: {filter_type} order: {order}", flush=True)
            return filter_type, order, sos
        except Exception as e:
            print(f"Failed to load filter config, using default: {e}", flush=True)
    
    filter_type = DEFAULT_FILTER_CONFIG["type"]
    order = DEFAULT_FILTER_CONFIG["order"]
    sos = compute_sos(filter_type, order)
    return filter_type, order, sos


def send_filter_to_mcu(sos: np.ndarray):
    '''
    Send SOS coefficients to MCU via Bridge notify.
    Uses notify instead of call to avoid blocking and per-argument
    size limits on Bridge.call.
    '''
    payload = sos_to_bridge_format(sos)
    Bridge.notify("setFilterCoeffs", payload)
    print(f"Sent filter to MCU: {sos.shape[0]} SOS stages", flush=True)
    

def send_filter_state_to_ui(filter_type: str, order: int, sos: np.ndarray):
    '''
    Send current filter configuration and frequency responses to dashboard.
    '''
    freq_response = compute_frequency_response(sos)
    ui.send_message("filter_state", {
        "type": filter_type,
        "order": order,
        "n_stages": int(sos.shape[0]),
        "freq_response": freq_response,
        "lowcut_hz": LOWCUT_HZ,
        "highcut_hz": HIGHCUT_HZ,
    })


def ecg_callback(samples):
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
    raw_ecg, filtered_ecg, fft_ecg = samples
    if len(raw_ecg) != EXPECTED_RAW:
        print(f"Bad raw ECG buffer: expected {EXPECTED_RAW}, got {len(raw_ecg)}", flush=True)
        return
    if len(filtered_ecg) != EXPECTED_FILTERED:
        print(
            f"Bad filtered ECG buffer: expected {EXPECTED_FILTERED}, got {len(filtered_ecg)}",
            flush=True
        )
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
        except ValueError as e:
            print(f"Detection error: {e}", flush=True)

    ui.send_message("raw_ecg", raw_ecg)
    ui.send_message("filtered_ecg", filtered_ecg)
    ui.send_message("fft_ecg", fft_ecg)


def handle_set_ecg_enabled(_sid, state):
    '''
    Handler for the start/shutdown of the ECG sensor.
    Receives request from the web application and sends to the MCU.
    Sends updated ECG sensor state to the web application.
    '''
    print(f"Received set_ecg_enabled: {state}", flush=True)
    Bridge.call("setECGEnabled", state)
    ui.send_message("ecg_state", state)


def handle_set_filter(_sid, data):
    '''
    Handler for filter configuration requests from web dashboard.
    '''
    filter_type = data.get("type", "").strip().lower()
    order = data.get("order")

    if filter_type not in SUPPORTED_FILTERS:
        ui.send_message("filter_error", {
            "message": (
                f"Unsupported filter type '{filter_type}'."
                f"Supported: {sorted(SUPPORTED_FILTERS)}"
                )
        })
        return

    if not isinstance(order, int) or order < 1:
        ui.send_message("filter_error", {
            "message": "Filter order must be a positive integer greater than 0"
        })
        return
    
    try:
        sos = compute_sos(filter_type, order)
    except Exception as e:
        ui.send_message("filter_error", {"message": f"Filter design failed: {e}"})
        return
    
    valid, err = validate_sos(sos)
    if not valid:
        ui.send_message("filter_error", {"message": f"Filter validation failed: {err}"})
        return
    
    save_filter_config(filter_type, order, sos)
    send_filter_to_mcu(sos)
    send_filter_state_to_ui(filter_type, order, sos)

    print(
        f"Filter updated: {filter_type} order {order}, "
        f"{sos.shape[0]} SOS stages",
        flush=True
    )


def handle_get_filter(_sid, _data):
    '''
    Handler for dashboard requests to fetch the current filter state.
    '''
    send_filter_state_to_ui(active_filter_type, active_filter_order, active_sos)



ui.on_message("set_ecg_enabled", handle_set_ecg_enabled) # Socket check for set_ecg_enabled message
ui.on_message("set_filter", handle_set_filter)
ui.on_message("get_filter", handle_get_filter)
Bridge.provide("ecg_packet", ecg_callback) # Provide the ECG callback to the MCU

active_filter_type, active_filter_order, active_sos = load_filter_config()
send_filter_to_mcu(active_sos)

App.run() # Run the Python application
