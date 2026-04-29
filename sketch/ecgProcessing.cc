#include "ecgProcessing.h"
#include <string.h>
#include <math.h>

static const size_t DEFAULT_ORDER = 4;
static const float DEFAULT_B[5] = {0.20254323f, 0.0f, -0.40508647f, 0.0f, 0.20254323f};
static const float DEFAULT_A[5] = {1.0f, -2.36106054f, 1.93885424f, -0.77592369f, 0.19833429f};

void initFilter(IIRFilter &f)
{
    f.order = DEFAULT_ORDER;

    memset(f.b, 0, sizeof(f.b));
    memset(f.a, 0, sizeof(f.a));
    memset(f.x, 0, sizeof(f.x));
    memset(f.y, 0, sizeof(f.y));

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
        f.x[i] = f.x[i - 1];
    }
    f.x[0] = x0;
    float y0 = 0.0f;
    for (size_t i = 0; i <= f.order; i++)
    {
        y0 += f.b[i] * f.x[i];
    }
    for (size_t i = 1; i <= f.order; i++)
    {
        y0 -= f.a[i] * f.y[i - 1];
    }

    for (size_t i = f.order; i > 0; i--)
    {
        f.y[i] = f.y[i - 1];
    }
    f.y[0] = y0;
    return y0;
}

void resetFilter(IIRFilter &f)
{
    memset(f.x, 0, sizeof(f.x));
    memset(f.y, 0, sizeof(f.y));
}

bool updateFilterCoeffs(IIRFilter &f, const float *coeffs, size_t len)
{
    if (len < 1)
    {
        return false;
    }
    size_t order = (size_t)coeffs[0];

    if (order < 1 || order > MAX_FILTER_ORDER)
    {
        return false;
    }

    size_t expected = 1 + 2 * (order + 1);
    if (len != expected)
    {
        return false;
    }

    for (size_t i = 1; i < len; i++)
    {
        if (!isfinite(coeffs[i]))
        {
            return false;
        }
    }

    f.order = order;
    memset(f.b, 0, sizeof(f.b));
    memset(f.a, 0, sizeof(f.a));

    for (size_t i = 0; i <= order; i++)
    {
        f.b[i] = coeffs[1 + i];
    }

    for (size_t i = 0; i <= order; i++)
    {
        f.a[i] = coeffs[1 + (order + 1) + i];
    }
    resetFilter(f);
    return true;
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
    {
        sum += arr[i];
    }
    return (float)(sum / len);
}

float clampMagnitude(double mag)
{
    if (mag < 0.0)
    {
        mag = 0.0;
    }
    if (mag > 65535.0)
    {
        mag = 65535.0;
    }
    return (float)mag;
}