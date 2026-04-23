# TinyML ECG on Arduino Uno Q

[![Compile Arduino Sketch](https://github.com/LukeBogdanovic/Arduino_TinyML_ECG/actions/workflows/Arduino_compile.yml/badge.svg)](https://github.com/LukeBogdanovic/Arduino_TinyML_ECG/actions/workflows/Arduino_compile.yml)
[![arduino/arduino-lint-action](https://github.com/LukeBogdanovic/Arduino_TinyML_ECG/actions/workflows/Arduino_lint.yml/badge.svg)](https://github.com/LukeBogdanovic/Arduino_TinyML_ECG/actions/workflows/Arduino_lint.yml)
[![Pylint](https://github.com/LukeBogdanovic/Arduino_TinyML_ECG/actions/workflows/pylint.yml/badge.svg)](https://github.com/LukeBogdanovic/Arduino_TinyML_ECG/actions/workflows/pylint.yml)
[![ESLint](https://github.com/LukeBogdanovic/Arduino_TinyML_ECG/actions/workflows/eslint.yml/badge.svg)](https://github.com/LukeBogdanovic/Arduino_TinyML_ECG/actions/workflows/eslint.yml)

This repository contains code for a program that runs on the Arduino Uno Q. This is designed to run on the 2GB RAM variant but can also run on the 4GB variant.
A dockerfile that enables the development of the program from a container is also available in this repo. The container has the git repo and arduino-cli installed
as part of the creation of the docker image. Libraries are also installed as part of this process.

This repo allows has CI/CD pipelines enabled through GitHub Actions that lint the Python, Arduino sketch, and Javascript files. Verification of the pipelines passing
can be seen through the verification badges above for each of the CI/CD pipelines.

To run this program, it must be installed on an Arduino Uno Q with an AD8232 single-lead heart rate monitor front end connected.
The output of the AD8232 should be connected to pin A0 on the Uno Q, and Lo- and Lo+ must be connected to digital pins 11 and 10 respectively.
Additionally, the shutdown pin on the AD8232 is configured to enable shutdown of the sensor when not in use. This should be connected to the Uno Q
on digital pin 9.

## Data Acquisition

Data acquisition is performed using the AD8232 electrocardiogram (ECG) sensor as mentioned previously. This is connected to ADC1 on the STM32 MCU on the Uno Q board.
Sampling is performed using a hardware timer that triggers an interrupt service routine based on the desired sampling rate.
Raw samples from the AD8232 are stored in a buffer before being provided to the Python program running on the MPU on the Uno Q.

## Digital Signal Processing

Signals from the AD8232 are processed with the hardware filters present on the sensor before they are ingested by the MCU.
Additional filtering is performed using a digital bandpass filter to further remove noise from the signal, such as noise that
results from the movement of a person attached to the ECG sensor through ECG electrodes.
Additionally a fast fourier transform (FFT) is performed to analyse the frequencies of the ECG signal and to confirm that
the expected frequencies are present in the signal after hardware and software filtering has occurred.

## MCU Machine Learning

TODO: Update information on TFLite model running on MCU

## Data Dashboard

A data visualisation dashboard is provided for the visualisation of the raw, and filtered signals as well as the FFT of the signal.
The heart rate of the person is calculated using the Pan-Tompkins algorithm and provided here along with the peak frequency, the number
of samples and the sound to noise (SNR) ratio of the signal
The turning on/shutting down of the ECG sensor is performed from the dashboard also.
