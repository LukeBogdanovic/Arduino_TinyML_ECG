'''
:file: main.py

Sets up the web application for analysis of incoming ECG signals from the AD8232.
Provides callbacks for the MCU on the Arduino Uno Q and handlers for socket connections
to the JavaScript frontend.
'''
import os
import json
from collections import deque

import numpy as np
from scipy.signal import iirfilter, freqz

from arduino.app_utils import App, Bridge
from arduino.app_bricks.web_ui import WebUI
from ecgdetectors import Detectors


ui = WebUI() # Setup web application interface

SAMPLE_RATE = 128 # Sampling rate for ECG data
WINDOW = SAMPLE_RATE * 10 # Length of window for calculating the Heart rate of person
EXPECTED_RAW = 256 # Expected length of the raw ECG buffer
EXPECTED_FILTERED = 256 # Expected length of the filtered ECG buffer
EXPECTED_FFT = 128 # Expected number of FFT bins

LOWCUT_HZ = 0.5 # Remove very low frequencies
HIGHCUT_HZ = 40 # Remove 50Hz noise and other possible frequencies from muscle movement etc...

SUPPORTED_FILTERS = {"butter", "cheby1", "cheby2", "ellip"}

MAX_FILTER_ORDER = 20 # Arbitrary value chosen, can be adjusted as needed

FILTER_CONFIG_PATH = os.path.join(os.path.dirname(__file__), "filter_config.json") # JSON file path for filter config

DEFAULT_FILTER_CONFIG = { # Default config for digital filter
    "type": "butter",
    "order": 4,
}

rolling_buffer = deque(maxlen=WINDOW) # Double ended queue of length window
detectors = Detectors(SAMPLE_RATE) # Setup the pan-tompkins library


def compute_coefficients(filter_type: str, order: int) -> tuple[np.ndarray, np.ndarray]:
    '''
    Compute b and a coefficients for a bandpass IIR filter.
    Uses input from user for the filter type and order of the filter to be designed.

    :param filter_type: IIR filter type to be used for the design
    :param order: Filter order to be used for the design
    :returns: b and a coefficients of the filter
    '''
    if filter_type not in SUPPORTED_FILTERS: # Guard for compatible filter to design
        raise ValueError(
            f"Unsupported filter type '{filter_type}'."
            f"Must be one of: {sorted(SUPPORTED_FILTERS)}"
                         )
    if not isinstance(order, int) or order < 1 or order > MAX_FILTER_ORDER: # Guard for checking value and data type of order
        raise ValueError(f"Filter order must be a positive integer, got {order}")

    nyq = SAMPLE_RATE / 2 # Find nyquist frequency
    low = LOWCUT_HZ / nyq # Normalize lowcut using nyquist
    high = HIGHCUT_HZ / nyq # Normalize highcut using nyquist

    kwargs = {}
    if filter_type in ("cheby1", "ellip"):
        kwargs["rp"] = 1.0 # Passband ripple allowed for cheby1 and ellip designs
    if filter_type in ("cheby2", "ellip"):
        kwargs["rs"] = 40.0 # Stopband ripple allowed for cheby2 and ellip designs

    b, a = iirfilter(order, [low, high], btype="band", ftype=filter_type, output="ba", **kwargs) # Design filter using generic filter design function
    return b, a


def compute_pole_zero(b:np.ndarray, a:np.ndarray) -> dict:
    '''
    Compute poles and zeros of the filter for stability display on dashboard.

    :param b: b coefficients of the filter
    :param a: a coefficients of the filter
    :returns: Dictionary with poles, zeros, and stability of the filter
    '''
    zeros = np.roots(b)
    poles = np.roots(a)
    stable = bool(np.all(np.abs(poles) < 1.0))
    return {
        "zeros": [{"re": float(z.real), "img": float(z.imag)} for z in zeros],
        "poles": [{"re": float(p.real), "img": float(p.imag)} for p in poles],
        "stable" : stable,
        "max_pole_radius": float(np.max(np.abs(poles))),
    }


def validate_coefficients(b: np.ndarray, a:np.ndarray) -> tuple[bool, str | None]:
    '''
    Validate b and a coefficient array.
    Checks the length of the b and a coefficents are equal.
    Checks if filter order is valid according to the defined constraints.
    Checks that all coefficients are valid values (not NaN or Infinite values).
    Checks the stability of the filter through the poles. Only validates if the design
    is stable. Does not accept marginal stability as valid design.

    :param b: b coefficients of the filter
    :param a: a coefficients of the filter
    :returns: Validity of the design and an error message if required
    '''
    if len(b) != len(a):
        return False, f"b and a must have equal length, got {len(b)} and {len(a)}"
    
    order = len(b) - 1

    if order < 1 or order > MAX_FILTER_ORDER:
        return False, f"Filter order {order} out of range. Must be between 1 and {MAX_FILTER_ORDER}."
    
    if not np.all(np.isfinite(b)) or not np.all(np.isfinite(a)):
        return False, "Coefficients contain NaN or Inf values."

    poles = np.roots(a)
    if np.any(np.abs(poles) >= 1.0):
        return False, f"Filter is unstable. Poles outside or on the unit circle: {np.abs(poles)}."

    return True, None


def compute_frequency_response(b: np.ndarray, a:np.ndarray) -> dict:
    '''
    Compute the frequency response of a filter for the dashboard.

    :param b: b coefficients of the filter
    :param a: a coefficients of the filter
    :returns: All relevant information for displaying frequency response 
    '''
    wor_n = 512
    w, h = freqz(b=b,a=a, worN=wor_n, fs=SAMPLE_RATE)
    mag_db:np.ndarray = 20 * np.log10(np.abs(h) + 1e-12)

    return {
        "frequencies": w.tolist(),
        "magnitude_db": mag_db.tolist(),
        "lowcut_hz": LOWCUT_HZ,
        "highcut_hz": HIGHCUT_HZ,
        "nyquist_hz": SAMPLE_RATE/2,
    }


def coefficients_to_bridge_format(b: np.ndarray, a: np.ndarray) -> list:
    '''
    Convert b, a coefficients arrays to a list for Bridge transmission.

    :param b: b coefficients of the filter
    :param a: a coefficients of the filter
    :returns: Flattend list for transferring between MPU and MCU
    '''
    order = len(b) - 1
    flat = [float(order)]
    flat.extend(b.tolist())
    flat.extend(a.tolist())
    return flat


def save_filter_config(filter_type: str, order: int, b: np.ndarray, a: np.ndarray):
    '''
    Persist filter configuration to JSON file. Allows the state of the filter to persist
    across device boots.

    :param filter_type: IIR filter type designed
    :param order: Order of the filter designed
    :param b: b coefficients of the filter
    :param a: a coefficients of the filter
    :returns:
    '''
    config = {
        "type": filter_type,
        "order": order,
        "b": b.tolist(),
        "a": a.tolist(),
    }
    with open(FILTER_CONFIG_PATH, "w", encoding="utf-8") as f:
        json.dump(config, f, indent=2)
    print(f"Filter config saved: {filter_type} order: {order}", flush=True)


def load_filter_config() -> tuple[str, int, np.ndarray, np.ndarray]:
    '''
    Load filter configuration from JSON file.

    :returns: Tuple of the filter information from JSON file.
    '''
    if os.path.exists(FILTER_CONFIG_PATH):
        try:
            with open(FILTER_CONFIG_PATH, "r", encoding="utf-8") as f:
                config = json.load(f)
            filter_type = config["type"]
            order = config["order"]
            b = np.array(config["b"])
            a = np.array(config["a"])
            valid, err = validate_coefficients(b, a)
            if not valid:
                raise ValueError(f"Loaded coefficients failed validation: {err}")
            print(f"Loaded filter config: {filter_type} order: {order}", flush=True)
            return filter_type, order, b, a
        except IOError as e:
            print(f"Failed to load filter config, using default: {e}", flush=True)

    filter_type = DEFAULT_FILTER_CONFIG["type"]
    order = DEFAULT_FILTER_CONFIG["order"]
    b, a = compute_coefficients(filter_type, order)
    return filter_type, order, b, a


def send_filter_to_mcu(b: np.ndarray, a: np.ndarray):
    '''
    Send SOS coefficients to MCU via Bridge notify.
    Uses notify instead of call to avoid blocking and per-argument
    size limits on Bridge.call.

    :param b: b coefficients of the filter
    :param a: a coefficients of the filter
    :returns:
    '''
    payload = coefficients_to_bridge_format(b, a)
    Bridge.notify("setFilterCoeffs", payload)
    print(f"Sent filter to MCU: order {len(b)-1}", flush=True)


def send_filter_state_to_ui(filter_type: str, order: int, b: np.ndarray, a: np.ndarray):
    '''
    Send current filter configuration and frequency responses to dashboard.

    :param filter_type: Active IIR filter type
    :param order: Active filter order
    :param b: Active b coefficients
    :param a: Active a coefficients
    :returns:
    '''
    freq_response = compute_frequency_response(b, a)
    pole_zero = compute_pole_zero(b, a)
    ui.send_message("filter_state", {
        "type": filter_type,
        "order": order,
        "freq_response": freq_response,
        "pole_zero": pole_zero,
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

    :param samples: Tuple of raw and filtered samples as well as FFT bins of the filtered ECG
    :returns:
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
    Clears rolling buffer on clear to avoid stale data.

    :param _sid: Socket ID (unused)
    :param state: State the sensor should be set to, True for enable, False for disable
    :returns:
    '''
    print(f"Received set_ecg_enabled: {state}", flush=True)
    Bridge.call("setECGEnabled", state)
    if not state:
        rolling_buffer.clear()
    ui.send_message("ecg_state", state)


def handle_set_filter(_sid, data):
    '''
    Handler for filter configuration requests from web dashboard.
    Triggers computing and validation of filter coefficients and design.
    Saves to JSON and sends filter state to MCU and dashboard.

    :param _sid: Socket ID (unused)
    :param data: Request payload containing the filter type and order
    :returns:
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

    if not isinstance(order, int) or order < 1 or order > MAX_FILTER_ORDER:
        ui.send_message("filter_error", {
            "message": f"Filter order must be a between 1 and {MAX_FILTER_ORDER}"
        })
        return

    try:
        b, a = compute_coefficients(filter_type, order)
    except ValueError as e:
        ui.send_message("filter_error", {"message": f"Filter design failed: {e}"})
        return

    valid, err = validate_coefficients(b, a)
    if not valid:
        ui.send_message("filter_error", {"message": f"Filter validation failed: {err}"})
        return

    save_filter_config(filter_type, order, b, a)
    send_filter_to_mcu(b, a)
    send_filter_state_to_ui(filter_type, order, b, a)

    print(f"Filter updated: {filter_type} order {order}",flush=True)


def handle_get_filter(_sid, _data):
    '''
    Handler for dashboard requests to fetch the current filter state.

    :param _sid: Socket ID (unused)
    :param _data: Request payload (unused)
    :returns:
    '''
    send_filter_state_to_ui(active_filter_type, active_filter_order, active_b, active_a)



ui.on_message("set_ecg_enabled", handle_set_ecg_enabled) # Socket check for set_ecg_enabled message
ui.on_message("set_filter", handle_set_filter)
ui.on_message("get_filter", handle_get_filter)
Bridge.provide("ecg_packet", ecg_callback) # Provide the ECG callback to the MCU

active_filter_type, active_filter_order, active_b, active_a = load_filter_config()
send_filter_to_mcu(active_b, active_a)

App.run() # Run the Python application
