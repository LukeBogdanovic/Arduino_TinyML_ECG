/**
 *
 *
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define BUFFER_SIZE 256
#define FFT_BINS (BUFFER_SIZE / 2)
#define SAMPLE_RATE 128

#define MAX_FILTER_ORDER 20

typedef struct
{
    float b[MAX_FILTER_ORDER + 1];
    float a[MAX_FILTER_ORDER + 1];
    float x[MAX_FILTER_ORDER + 1];
    float y[MAX_FILTER_ORDER + 1];
    size_t order;
} IIRFilter;

/**
 *
 */
void initFilter(IIRFilter &f);

/**
 *
 */
float applyFilter(IIRFilter &f, float x0);

/**
 *
 */
void resetFilter(IIRFilter &f);

/**
 *
 */
bool updateFilterCoeffs(IIRFilter &f, const float *coeffs, size_t len);

typedef struct
{
    uint16_t ecgBuffer[BUFFER_SIZE];
    float filteredBuffer[BUFFER_SIZE];
    float fftBuffer[FFT_BINS];
    size_t sampleIdx;
    bool bufferReady;
} ECGBuffers;

/**
 *
 */
void initBuffers(ECGBuffers &bufs);

/**
 *
 */
void pushSample(ECGBuffers &bufs, uint16_t raw, float filtered);

/**
 *
 */
void resetAfterSend(ECGBuffers &bufs);

/**
 *
 */
float computeMean(const float *arr, size_t len);

/**
 *
 */
float clampMagnitude(double mag);