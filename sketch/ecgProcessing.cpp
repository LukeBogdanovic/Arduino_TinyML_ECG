/**
 * @file ecgProcessing.cpp
 * @author @LukeBogdanovic
 * @date 30/04/2026
 *
 */
#include "ecgProcessing.hpp"
#include <string.h>
#include <math.h>
#include <arduinoFFT.h>

static const size_t DEFAULT_ORDER = 4;
static const float DEFAULT_B[5] = {0.20254323f, 0.0f, -0.40508647f, 0.0f, 0.20254323f};
static const float DEFAULT_A[5] = {1.0f, -2.36106054f, 1.93885424f, -0.77592369f, 0.19833429f};
double vReal[BUFFER_SIZE];                                                                  // Buffer for working during FFT for real component
double vImag[BUFFER_SIZE];                                                                  // Buffer for working during FFT for imaginary component
ArduinoFFT<double> FFT = ArduinoFFT<double>(vReal, vImag, BUFFER_SIZE, (float)SAMPLE_RATE); // Setup for FFT library, provides buffers for real and imaginary parts, sizes of the buffers and sampling rate

void initFilter(IIRFilter &f)
{
    f.order = DEFAULT_ORDER; // Set filter order to the default

    // Set bytes for each buffer to 0
    memset(f.b, 0, sizeof(f.b));
    memset(f.a, 0, sizeof(f.a));
    memset(f.x, 0, sizeof(f.x));
    memset(f.y, 0, sizeof(f.y));

    // Loop through and set the coefficients
    for (size_t i = 0; i <= DEFAULT_ORDER; i++)
    {
        f.b[i] = DEFAULT_B[i];
        f.a[i] = DEFAULT_A[i];
    }
}

float applyFilter(IIRFilter &f, float x0)
{
    for (size_t i = f.order; i > 0; i--)
    {
        f.x[i] = f.x[i - 1]; // Set x[i] value to value of previous input
    }
    f.x[0] = x0; // Set x[0] value to the input value
    float y0 = 0.0f;
    // Feedforward (b coefficients)
    for (size_t i = 0; i <= f.order; i++)
    {
        y0 += f.b[i] * f.x[i];
    }
    // Feedback (a coefficients)
    for (size_t i = 1; i <= f.order; i++)
    {
        y0 -= f.a[i] * f.y[i - 1];
    }
    // Set y[i] value to the value of the previous output
    for (size_t i = f.order; i > 0; i--)
    {
        f.y[i] = f.y[i - 1];
    }
    f.y[0] = y0; // set y[0] value to the new output value
    return y0;
}

void resetFilter(IIRFilter &f)
{
    // Reset values to 0 for
    memset(f.x, 0, sizeof(f.x));
    memset(f.y, 0, sizeof(f.y));
}

void computeFFT(ECGBuffers &bufs)
{
    float mean = computeMean(bufs.filteredBuffer, BUFFER_SIZE); // Compute mean for filtered values
    for (size_t i = 0; i < BUFFER_SIZE; i++)
    {
        vReal[i] = (double)bufs.filteredBuffer[i] - mean;
        vImag[i] = 0.0; // Used during FFT process, required for FFT library
    }
    FFT.windowing(FFTWindow::Hamming, FFTDirection::Forward); // Perform windowing using the Hamming window function
    FFT.compute(FFTDirection::Forward);                       // Perform the FFT
    FFT.complexToMagnitude();                                 // Get magnitude values for the FFT
    for (size_t i = 0; i < FFT_BINS; i++)
    {
        bufs.fftBuffer[i] = clampMagnitude(vReal[i]); // Clamp the magnitude of the FFT values between 0 and 2^16-1
    }
}

bool updateFilterCoeffs(IIRFilter &f, const float *coeffs, size_t len)
{
    if (len < 1)
    {
        return false;
    }
    size_t order = (size_t)coeffs[0]; // Find the filter order

    if (order < 1 || order > MAX_FILTER_ORDER) // Check that the order is between 1 and the defined max
    {
        return false;
    }

    size_t expected = 1 + 2 * (order + 1);
    if (len != expected) // Check the length of the filter is same as the expected value
    {
        return false;
    }

    for (size_t i = 1; i < len; i++)
    {
        if (!isfinite(coeffs[i])) // Check the filter coefficients for NaN/Inf values
        {
            return false;
        }
    }

    f.order = order;
    // Set b and a coefficients to 0 values in filter struct
    memset(f.b, 0, sizeof(f.b));
    memset(f.a, 0, sizeof(f.a));

    for (size_t i = 0; i <= order; i++)
    {
        f.b[i] = coeffs[1 + i]; // Set b coefficients in filter from 1 -> order
    }

    for (size_t i = 0; i <= order; i++)
    {
        f.a[i] = coeffs[1 + (order + 1) + i]; // Set a coefficients in filter from order+2 -> end of data
    }
    resetFilter(f); // Reset filter state
    return true;
}

void initBuffers(ECGBuffers &bufs)
{
    // Set all values in buffers to 0
    memset(bufs.ecgBuffer, 0, sizeof(bufs.ecgBuffer));
    memset(bufs.filteredBuffer, 0, sizeof(bufs.filteredBuffer));
    memset(bufs.fftBuffer, 0, sizeof(bufs.fftBuffer));
    bufs.sampleIdx = 0;       // Set sample index to 0
    bufs.bufferReady = false; // Set buffers to not ready for tx
}

void pushSample(ECGBuffers &bufs, uint16_t raw, float filtered)
{
    bufs.ecgBuffer[bufs.sampleIdx] = raw;           // Push raw sample to ecgBuffer
    bufs.filteredBuffer[bufs.sampleIdx] = filtered; // Push filtered sample to filteredBuffer
    bufs.sampleIdx++;                               // Increment sampleIdx

    // Check if buffers are full
    if (bufs.sampleIdx >= BUFFER_SIZE)
    {
        bufs.bufferReady = true; // Set buffer flag to ready
        bufs.sampleIdx = 0;      // Reset index for buffers
    }
}

void resetAfterSend(ECGBuffers &bufs)
{
    bufs.bufferReady = false; // Reset buffer flag to not ready
    // Set bytes in all buffers to 0
    memset(bufs.ecgBuffer, 0, sizeof(bufs.ecgBuffer));
    memset(bufs.filteredBuffer, 0, sizeof(bufs.filteredBuffer));
    memset(bufs.fftBuffer, 0, sizeof(bufs.fftBuffer));
}

float computeMean(const float *arr, size_t len)
{
    double sum = 0.0;
    // Sum all values in the array
    for (size_t i = 0; i < len; i++)
    {
        sum += arr[i];
    }
    return (float)(sum / len); // Return float cast mean of
}

float clampMagnitude(double mag)
{
    // Check if FFT magnitude is less than 0
    if (mag < 0.0)
    {
        mag = 0.0;
    }
    // Check if FFT magnitude is greater than (2^16)- 1
    if (mag > 65535.0)
    {
        mag = 65535.0;
    }
    return (float)mag;
}