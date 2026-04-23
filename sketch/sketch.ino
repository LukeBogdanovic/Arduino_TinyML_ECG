#include <Arduino_RouterBridge.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <array>
#include <string.h>
#include <arduinoFFT.h>
#include <tuple>

#define BUFFER_SIZE 256            // Size of the buffer sent across RPC bridge
#define FFT_BINS (BUFFER_SIZE / 2) // Number of bins for FFT
#define SAMPLE_RATE 200            // Rate to sample at in Hz

static const struct device *adc_dev = DEVICE_DT_GET(DT_NODELABEL(adc1)); // Setup ADC as a device (14-bit ADC)

static const struct adc_channel_cfg adc_ch_cfg = {
    .gain = ADC_GAIN_1,                       // No amplification, input voltage passed through 1:1
    .reference = ADC_REF_INTERNAL,            // User internal voltage reference
    .acquisition_time = ADC_ACQ_TIME_DEFAULT, // Use driver default sampling time for the channel
    .channel_id = 9,                          // A0 = adc1 channel 9 maps to A0 on board
    .differential = 0,                        // Measure voltage relative to GND
};

static int16_t adc_raw; // Buffer for the ADC value to use
static struct adc_sequence adc_seq = {
    .channels = BIT(9),             // Bitmask for ADC channel to use
    .buffer = &adc_raw,             // Pointer to buffer where ADC value is written after conversion
    .buffer_size = sizeof(adc_raw), // Must be large enough to store one 16-bit sample
    .resolution = 12,               // ADC resolution
};

static const struct device *gpio_b = DEVICE_DT_GET(DT_NODELABEL(gpiob)); // Setup GPIO pins for shutdown, Lo- and Lo+

#define PIN_SHUTDOWN 8  // PB8  = D9
#define PIN_LO_PLUS 9   // PB9  = D10
#define PIN_LO_MINUS 15 // PB15 = D11

std::array<uint16_t, BUFFER_SIZE> ecgBuffer;                                                                     // Raw ECG buffer
std::array<float, BUFFER_SIZE> filteredOutBuffer;                                                                // Filtered ECG buffer
std::array<float, FFT_BINS> fftOutBuffer;                                                                        // FFT bins buffer
std::tuple<std::array<uint16_t, BUFFER_SIZE>, std::array<float, BUFFER_SIZE>, std::array<float, FFT_BINS>> data; // Tuple object for sending data to MPU
size_t sampleIdx = 0;                                                                                            // Tracking index for number of samples in buffer
bool ecgEnabled = false;                                                                                         // Flag for enabling/disabling ECG sensor
volatile bool bufferReady = false;                                                                               // Flag for tracking when buffer is full
volatile bool sampleNow = false;                                                                                 // Flag for when to sample, set by timer ISR

double vReal[BUFFER_SIZE]; // Buffer for working during FFT for real component
double vImag[BUFFER_SIZE]; // Buffer for working during FFT for imaginary component

ArduinoFFT<double> FFT = ArduinoFFT<double>(vReal, vImag, BUFFER_SIZE, 200.0f); // Setup for FFT library, provides buffers for real and imaginary parts, sizes of the buffers and sampling rate

/**
 * Struct for filter containing b and a coeffcients,
 * and state for inputs and outputs of the filter.
 */
typedef struct
{
    float b[5];
    float a[5];
    float x[5];
    float y[5];
} Filter;

Filter bpFilter = {
    {0.20254323f, 0.0f, -0.40508647f, 0.0f, 0.20254323f},         // b coefficients
    {1.0f, -2.36106054f, 1.93885424f, -0.77592369f, 0.19833429f}, // a coefficients
    {0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0}};

struct k_timer sampleTimer; // Setup variable for timer used for sampling

/**
 * Applies a 4th-order bandpass Butterworth IIR filter to one input sample and returns the filtered output.
 */
float applyFilter(Filter &f, float x0)
{
    f.x[4] = f.x[3];
    f.x[3] = f.x[2];
    f.x[2] = f.x[1];
    f.x[1] = f.x[0];
    f.x[0] = x0;
    // Difference equation
    float y0 = f.b[0] * f.x[0] + f.b[1] * f.x[1] + f.b[2] * f.x[2] + f.b[3] * f.x[3] + f.b[4] * f.x[4] - f.a[1] * f.y[0] - f.a[2] * f.y[1] - f.a[3] * f.y[2] - f.a[4] * f.y[3];

    f.y[3] = f.y[2];
    f.y[2] = f.y[1];
    f.y[1] = f.y[0];
    f.y[0] = y0;
    return y0;
}

/**
 * Reset the state of the filter after it has been used for filtering a sample
 * Only resets the state of the inputs (x) and outputs (y)
 */
void resetFilterState(Filter &f)
{
    memset(f.x, 0, sizeof(f.x));
    memset(f.y, 0, sizeof(f.y));
}

/**
 * Compute the FFT of the filtered signal
 */
void computeFFT()
{
    double mean = 0.0;
    for (size_t i = 0; i < BUFFER_SIZE; i++)
    {
        mean += filteredOutBuffer[i]; // Sum all values in the filtered buffer
    }
    mean /= BUFFER_SIZE; // Get mean of the sum of the filtered buffer

    for (size_t i = 0; i < BUFFER_SIZE; i++)
    {
        vReal[i] = (double)filteredOutBuffer[i] - mean;
        vImag[i] = 0.0; //
    }
    FFT.windowing(FFTWindow::Hamming, FFTDirection::Forward); // Perform windowing using the Hamming window function
    FFT.compute(FFTDirection::Forward);                       // Perform the FFT
    FFT.complexToMagnitude();                                 // Get magnitude values for the FFT
    for (size_t i = 0; i < FFT_BINS; i++)
    {
        double mag = vReal[i]; // Get current value for magnitude
        if (mag < 0)
            mag = 0; // Clamp value if below 0
        if (mag > 65535.0)
            mag = 65535.0;     // clamp value if above 65535
        fftOutBuffer[i] = mag; // Insert magnitude value in FFT buffer
    }
}

/**
 * Set the ECG sensor to enabled or disbled based on user interaction with web
 * application.
 */
void setECGEnabled(bool enabled)
{
    ecgEnabled = enabled;                                // Set truth flag to the passed in value
    gpio_pin_set(gpio_b, PIN_SHUTDOWN, enabled ? 1 : 0); // Set shutdown pin to the value of enabled
    if (enabled)                                         // Check if the sensor is enabled
    {
        sampleIdx = 0;                                                  // Reset index for sampling
        bufferReady = false;                                            // Reset buffer state
        memset(ecgBuffer.data(), 0, sizeof(ecgBuffer));                 // Reset raw buffer to 0's
        memset(filteredOutBuffer.data(), 0, sizeof(filteredOutBuffer)); // Reset filtered buffer to 0's
        memset(fftOutBuffer.data(), 0, sizeof(fftOutBuffer));           // Reset FFT buffer to 0's
        resetFilterState(bpFilter);                                     // Reset filter state
        k_timer_start(&sampleTimer, K_USEC(5000), K_USEC(5000));        // Start timer and start the first sample after 5ms after setup with each sample after coming at 5ms
        Monitor.println("ECG sensor has been turned ON!");
    }
    else
    {
        k_timer_stop(&sampleTimer); // Stop the timer
        Monitor.println("ECG sensor has been shutdown!");
    }
}

/**
 * Interrupt service routine (ISR) for the timer being used for sampling.
 * Uses the provided timer as input to identify which timer the ISR
 * is related to.
 *
 * Sets a flag for when the ADC should sample to true.
 */
void sampleTimerCallback(struct k_timer *timer)
{
    sampleNow = true;
}

void setup()
{
    Bridge.begin();      // Initialise Bridge RPC between MCU and MPU
    Monitor.begin(9600); // Initialise the serial communication with BAUD rate 9600

    if (!device_is_ready(adc_dev)) // Check if ADC device is ready
    {
        Monitor.println("ERROR: ADC device not ready");
        return;
    }
    if (!device_is_ready(gpio_b)) // Check if GPIO pins (Lo+, Lo-, SHUTDOWN) are ready
    {
        Monitor.println("ERROR: GPIOB device not ready");
        return;
    }
    int ret = adc_channel_setup(adc_dev, &adc_ch_cfg); // Setup channel for adc using declared config
    if (ret < 0)                                       // Check if ADC channle has been setup correctly
    {
        Monitor.println("ERROR: ADC channel setup failed");
        return;
    }
    gpio_pin_configure(gpio_b, PIN_SHUTDOWN, GPIO_OUTPUT_INACTIVE); // D9 pin for shutdown/start of ECG sensor
    gpio_pin_configure(gpio_b, PIN_LO_PLUS, GPIO_INPUT);            // D10 Lo+ lead off detection
    gpio_pin_configure(gpio_b, PIN_LO_MINUS, GPIO_INPUT);           // D11 Lo- lead off detection
    k_timer_init(&sampleTimer, sampleTimerCallback, NULL);          // Initialise timer for sampling and timer interrupt service routine function
    Bridge.provide("setECGEnabled", setECGEnabled);                 // Provide ECG sensor ON/OFF switch
    memset(ecgBuffer.data(), 0, sizeof(ecgBuffer));                 // Set ecgBuffer to 0's
    memset(filteredOutBuffer.data(), 0, sizeof(filteredOutBuffer)); // Set filtered ECG buffer to 0's
    memset(fftOutBuffer.data(), 0, sizeof(fftOutBuffer));           // Set fft buffer to 0's
    delay(5000);                                                    // Allow bridge to initialise by delaying for 5 seconds
}

void loop()
{
    if (sampleNow)
    {                      // Check sampling flag
        sampleNow = false; // Reset sampling flag

        if (ecgEnabled) // Check if the ECG has been enabled
        {
            int loPlus = gpio_pin_get(gpio_b, PIN_LO_PLUS);   // Get value of lead off lo- pin
            int loMinus = gpio_pin_get(gpio_b, PIN_LO_MINUS); // Get value of lead off lo+ pin
            if (loPlus == 0 && loMinus == 0)                  // Check if leads are connected to a person
            {
                int ret = adc_read(adc_dev, &adc_seq); // Read value from the ADC
                if (ret == 0)                          // Check if ADC has read value
                {
                    uint16_t rawSample = (uint16_t)adc_raw;                         // Receive value from ADC buffer and cast to unsigned int
                    float filteredSample = applyFilter(bpFilter, (float)rawSample); // Filter the received sample
                    ecgBuffer[sampleIdx] = rawSample;                               // Place raw sample in the buffer
                    filteredOutBuffer[sampleIdx] = filteredSample;                  // Place the filtered sample in the buffer
                    sampleIdx++;                                                    // Increment counter for number of samples in buffer
                    if (sampleIdx >= BUFFER_SIZE)                                   // Check if buffer is full
                    {
                        bufferReady = true; // Set buffer to ready for data transfer
                        sampleIdx = 0;      // Reset buffer index
                    }
                }
            }
        }
    }

    if (bufferReady)
    {                        // Check if buffer is ready
        bufferReady = false; // Set buffer to not ready
        Monitor.println("ECG buffer is ready!");
        computeFFT();                                                                             // Compute FFT values for the filtered ECG data
        Bridge.notify("ecg_packet", std::make_tuple(ecgBuffer, filteredOutBuffer, fftOutBuffer)); // Send data to MPU without looking for return value
        Monitor.println("Sent raw, filtered, and FFT data to MPU.");
        memset(ecgBuffer.data(), 0, BUFFER_SIZE * sizeof(uint16_t));      // Reset ecgBuffer to 0's
        memset(filteredOutBuffer.data(), 0, BUFFER_SIZE * sizeof(float)); // Reset filteredOutBuffer to 0's
        memset(fftOutBuffer.data(), 0, FFT_BINS * sizeof(float));         // Reset fftOutBuffer to 0's
        Monitor.println("Buffer emptied.");
    }
}
