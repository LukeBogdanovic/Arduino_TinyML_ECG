#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define BUFFER_SIZE 256
#define FFT_BINS (BUFFER_SIZE / 2)
#define SAMPLE_RATE 128

#define MAX_SOS_STAGES 33

typedef struct
{
    float b[3];
    float a[3];
    float x[3];
    float y[3];
} Biquad;

float applyBiquad(Biquad &f, float x0);
void resetBiquad(Biquad &f);

typedef struct
{
    Biquad stages[MAX_SOS_STAGES];
    size_t nStages;
} FilterCascade;

void initFilterCascade(FilterCascade &fc);
bool updateFilterCoeffs(FilterCascade &fc, const float *coeffs, size_t len);
float applyFilterCascade(FilterCascade &fc, float x0);
void resetFilterCascade(FilterCascade &fc);

typedef struct
{
    uint16_t ecgBuffer[BUFFER_SIZE];
    float filteredBuffer[BUFFER_SIZE];
    float fftBuffer[FFT_BINS];
    size_t sampleIdx;
    bool bufferReady;
} ECGBuffers;

void initBuffers(ECGBuffers &bufs);
void pushSample(ECGBuffers &bufs, uint16_t raw, float filtered);
void resetAfterSend(ECGBuffers &bufs);
float computeMean(const float *arr, size_t len);
float clampMagnitude(double mag);