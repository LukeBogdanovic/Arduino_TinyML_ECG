/**
 * @file ecgProcessing.hpp
 * @author Luke Bogdanovic
 * @date 30/04/2026
 *
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define BUFFER_SIZE 256            // Size of the buffer for raw and filtered ECGs
#define FFT_BINS (BUFFER_SIZE / 2) // Number of bins for the FFT
#define SAMPLE_RATE 128            // Sampling rate for the system
#define MAX_FILTER_ORDER 20        // Maximum allowed filter order to design - Arbitrary value

/**
 * Generic IIR filter struct. Allows for design of any IIRFilter and storing of b and a coefficients.
 * Stores the b and a coefficients, x (input) and y (output) values of the filter, and the order of the
 * designed filter.
 */
typedef struct
{
    float b[MAX_FILTER_ORDER + 1];
    float a[MAX_FILTER_ORDER + 1];
    float x[MAX_FILTER_ORDER + 1];
    float y[MAX_FILTER_ORDER + 1];
    size_t order;
} IIRFilter;

/**
 * Struct that holds the buffers for raw and filtered ECGs as well as FFT bins.
 * Contains a sample index that tracks the number of samples in the buffer.
 * Also contains a boolean value tracking if the buffer is ready to send
 * to the MPU.
 */
typedef struct
{
    uint16_t ecgBuffer[BUFFER_SIZE];
    float filteredBuffer[BUFFER_SIZE];
    float fftBuffer[FFT_BINS];
    size_t sampleIdx;
    bool bufferReady;
} ECGBuffers;

/**
 * @brief Initialises the filter using the default values. Sets all filter coefficients and input and output values
 * to 0 before setting default b and a coefficients.
 *
 * @param f IIRFilter struct passed that contains the b and a coefficients for the designed filter
 */
void initFilter(IIRFilter &f);

/**
 * @brief Applies the designed filter to a single raw sample from the ADC.
 *
 * @param f IIRFilter struct passed that contains the b and a coefficients for the designed filter
 * @param x0 Raw sample from the ADC
 */
float applyFilter(IIRFilter &f, float x0);

/**
 * @brief Resets the state of the filter. Resets only x and y values in the IIRFilter struct.
 *
 * @param f IIRFilter struct passed that contains the b and a coefficients for the designed filter
 */
void resetFilter(IIRFilter &f);

/**
 * @brief Compute the FFT of the filtered signal. Returns the computed values in the FFT buffer.
 *
 * @param bufs Reference to the ecgBuffer struct containing the filtered ECG buffer
 */
void computeFFT(ECGBuffers &bufs);

/**
 * @brief
 *
 * @param f IIRFilter struct passed that contains the b and a coefficients for the designed filter
 * @param coeffs Pointer to the coefficients of the new filter used to update the stored filter coefficients
 * @param len Length of the designed filter
 */
bool updateFilterCoeffs(IIRFilter &f, const float *coeffs, size_t len);

/**
 * @brief Initialises the buffer by setting all values in buffers to 0, sample index set to 0 and
 * sets the ready state of the buffers to false.
 *
 * @param bufs ECG buffer struct that holds the raw and filtered ECGs and the FFT bins
 */
void initBuffers(ECGBuffers &bufs);

/**
 * @brief Pushes the raw and filtered samples to the respective buffers. Checks if the buffers are
 * ready
 *
 * @param bufs ECG buffer struct that holds the raw and filtered ECGs and the FFT bins
 * @param raw Raw sample from the ADC
 * @param filtered Sample that has been filtered by the designed filtered
 */
void pushSample(ECGBuffers &bufs, uint16_t raw, float filtered);

/**
 * @brief Resets state of the buffers after sending data from MCU to MPU
 *
 * @param bufs ECG buffer struct that holds the raw and filtered ECGs and the FFT bins
 */
void resetAfterSend(ECGBuffers &bufs);

/**
 * @brief Helper function to compute the mean of the filtered ECG buffers
 *
 * @param arr Filtered ECG buffer
 * @param len Length of the ECG buffer
 */
float computeMean(const float *arr, size_t len);

/**
 * @brief Helper function to clamp the FFT magnitude values between 0 and (2^16)-1
 *
 * @param mag Magnitude value to be clamped
 */
float clampMagnitude(double mag);