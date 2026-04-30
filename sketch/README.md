# ECG sketch

Arduino sketch for the TinyML ECG project running on the Arduino Uno Q.
Reads ECG data from the AD8232 sensor connected to the 14-bit ADC on pin A0.
Applies filtering and FFT processing and communicates results with the Python
MPU application via the Arduino RouterBridge RPC library.

## Requirements

- Arduino UNO Q
- Arduino App Lab
- See `sketch.yaml` for full library dependencies