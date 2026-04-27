#include "ecgProcessing.h"
#include <string.h>
#include <math.h>

static const float DEFAULT_SOS[2][6] = {
    {0.40967467f, 0.81934935f, 0.40967467f, 1.0f, 0.45547271f, 0.21225762f},
    {1.0f, -2.0f, 1.0f, 1.0f, -1.96529295f, 0.96589475f}};

float applyBiquad(Biquad &f, float x0)
{
    f.x[2] = f.x[1];
    f.x[1] = f.x[0];
    f.x[0] = x0;

    float y0 = f.b[0] * f.x[0] + f.b[1] * f.x[1] + f.b[2] * f.x[2] - f.a[1] * f.y[0] - f.a[2] * f.y[1];

    f.y[2] = f.y[1];
    f.y[1] = f.y[0];
    f.y[0] = y0;
    return y0;
}

void resetBiquad(Biquad &f)
{
    memset(f.x, 0, sizeof(f.x));
    memset(f.y, 0, sizeof(f.y));
}

void initFilterCascade(FilterCascade &fc)
{
    fc.nStages = 2;
    for (size_t s = 0; s < fc.nStages; s++)
    {
        for (int i = 0; i < 3; i++)
            fc.stages[s].b[i] = DEFAULT_SOS[s][i];
        for (int i = 0; i < 3; i++)
            fc.stages[s].a[i] = DEFAULT_SOS[s][i + 3];
        resetBiquad(fc.stages[s]);
    }
    for (size_t s = fc.nStages; s < MAX_SOS_STAGES; s++)
    {
        memset(&fc.stages[s], 0, sizeof(Biquad));
    }
}

bool updateFilterCoeffs(FilterCascade &fc, const float *coeffs, size_t len)
{
    if (len < 1)
        return false;

    size_t nStages = (size_t)coeffs[0];

    if (nStages == 0 || nStages > MAX_SOS_STAGES)
        return false;
    if (len != 1 + nStages * 6)
        return false;

    // Validate all values are finite before applying
    for (size_t i = 1; i < len; i++)
    {
        if (!isfinite(coeffs[i]))
            return false;
    }

    fc.nStages = nStages;
    for (size_t s = 0; s < nStages; s++)
    {
        size_t offset = 1 + s * 6;
        fc.stages[s].b[0] = coeffs[offset];
        fc.stages[s].b[1] = coeffs[offset + 1];
        fc.stages[s].b[2] = coeffs[offset + 2];
        fc.stages[s].a[0] = coeffs[offset + 3];
        fc.stages[s].a[1] = coeffs[offset + 4];
        fc.stages[s].a[2] = coeffs[offset + 5];
        resetBiquad(fc.stages[s]);
    }
    for (size_t s = nStages; s < MAX_SOS_STAGES; s++)
    {
        memset(&fc.stages[s], 0, sizeof(Biquad));
    }

    return true;
}

float applyFilterCascade(FilterCascade &fc, float x0)
{
    float y = x0;
    for (size_t s = 0; s < fc.nStages; s++)
    {
        y = applyBiquad(fc.stages[s], y);
    }
    return y;
}

void resetFilterCascade(FilterCascade &fc)
{
    for (size_t s = 0; s < fc.nStages; s++)
    {
        resetBiquad(fc.stages[s]);
    }
}

void initBuffers(ECGBuffers &bufs)
{
    memset(bufs.ecgBuffer, 0, sizeof(bufs.ecgBuffer));
    memset(bufs.filteredBuffer, 0, sizeof(bufs.filteredBuffer));
    memset(bufs.fftBuffer, 0, sizeof(bufs.fftBuffer));
    bufs.sampleIdx = 0;
    bufs.bufferReady = false;
}

void pushSample(ECGBuffers &bufs, uint16_t raw, float filtered)
{
    bufs.ecgBuffer[bufs.sampleIdx] = raw;
    bufs.filteredBuffer[bufs.sampleIdx] = filtered;
    bufs.sampleIdx++;

    if (bufs.sampleIdx >= BUFFER_SIZE)
    {
        bufs.bufferReady = true;
        bufs.sampleIdx = 0;
    }
}

void resetAfterSend(ECGBuffers &bufs)
{
    bufs.bufferReady = false;
    memset(bufs.ecgBuffer, 0, sizeof(bufs.ecgBuffer));
    memset(bufs.filteredBuffer, 0, sizeof(bufs.filteredBuffer));
    memset(bufs.fftBuffer, 0, sizeof(bufs.fftBuffer));
}

float computeMean(const float *arr, size_t len)
{
    double sum = 0.0;
    for (size_t i = 0; i < len; i++)
        sum += arr[i];
    return (float)(sum / len);
}

float clampMagnitude(double mag)
{
    if (mag < 0.0)
        mag = 0.0;
    if (mag > 65535.0)
        mag = 65535.0;
    return (float)mag;
}