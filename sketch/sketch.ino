/**
 * @file sketch.ino
 * @author Luke Bogdanovic
 * @date 30/04/2026
 *
 * @brief Entrypoint for the MCU program for the UNO Q. Contains main loop and setup operations for hardware.
 * Contains functions interacting with hardware from the web application.
 */
#include <Arduino_RouterBridge.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <array>
#include <string.h>
#include <tuple>
#include <vector>
#include "ecgProcessing.hpp"

static const struct device *adc_dev = DEVICE_DT_GET(DT_NODELABEL(adc1)); // Setup ADC as a device (14-bit ADC)

static const struct adc_channel_cfg adc_ch_cfg = {
    .gain = ADC_GAIN_1,                       // No amplification, input voltage passed through 1:1
    .reference = ADC_REF_INTERNAL,            // User internal voltage reference
    .acquisition_time = ADC_ACQ_TIME_DEFAULT, // Use driver default sampling time for the channel
    .channel_id = 9,                          // A0 = adc1 channel 9 maps to A0 on board
    .differential = 0,                        // Measure voltage relative to GND
};

static const struct adc_channel_cfg adc_ch2_cfg = {
    .gain = ADC_GAIN_1,
    .reference = ADC_REF_INTERNAL,
    .acquisition_time = ADC_ACQ_TIME_DEFAULT,
    .channel_id = 10,
    .differential = 0,
};

static int16_t adc_raw; // Buffer for the ADC value to use
static int16_t adc_raw2;
static struct adc_sequence adc_seq = {
    .channels = BIT(9),             // Bitmask for ADC channel to use
    .buffer = &adc_raw,             // Pointer to buffer where ADC value is written after conversion
    .buffer_size = sizeof(adc_raw), // Must be large enough to store one 16-bit sample
    .resolution = 12,               // ADC resolution
};

static struct adc_sequence adc_seq2 = {
    .channels = BIT(10),
    .buffer = &adc_raw2,
    .buffer_size = sizeof(adc_raw2),
    .resolution = 12,
};

static const struct device *gpio_b = DEVICE_DT_GET(DT_NODELABEL(gpiob)); // Setup GPIO pins for shutdown, Lo- and Lo+

#define PIN_SHUTDOWN 8  // PB8  = D9
#define PIN_LO_PLUS 9   // PB9  = D10
#define PIN_LO_MINUS 15 // PB15 = D11

bool ecgEnabled = false;         // Flag for enabling/disabling ECG sensor
volatile bool sampleNow = false; // Flag for when to sample, set by timer ISR

IIRFilter ecgFilter; // Generic struct for IIRFilter design
ECGBuffers bufs;     // Struct for holding ecg (raw and filtered) and FFT buffers

struct k_timer sampleTimer; // Setup variable for timer used for sampling

/**
 * @brief Set the filter coefficients using the values from filter designer HTML page.
 *
 * @param coeffs Vector of the b and a coefficients for the filter designed on the MPU
 */
void setFilterCoeffs(std::vector<float> coeffs)
{
    bool ok = updateFilterCoeffs(ecgFilter, coeffs.data(), coeffs.size()); // Updates the filter coefficients using the flattend vector

    if (ok) // Check if filter coefficients have been updated
    {
        Monitor.println(
            String("Filter updated: order ") +
            String((int)ecgFilter.order));
    }
    else
    {
        Monitor.println("Filter update failed — invalid coefficients");
    }
}

/**
 * @brief Set the ECG sensor to enabled or disbled based on user interaction with web application.
 *
 * @param enabled State the ECG sensor should be set to
 */
void setECGEnabled(bool enabled)
{
    ecgEnabled = enabled;                                // Set truth flag to the passed in value
    gpio_pin_set(gpio_b, PIN_SHUTDOWN, enabled ? 1 : 0); // Set shutdown pin to the value of enabled
    if (enabled)                                         // Check if the sensor is enabled
    {
        initBuffers(bufs);
        resetFilter(ecgFilter);                                        // Reset filter state
        k_timer_start(&sampleTimer, K_NSEC(7812500), K_NSEC(7812500)); // Start timer and start the first sample after 5ms after setup with each sample after coming at 5ms
        Monitor.println("ECG sensor has been turned ON!");
    }
    else
    {
        k_timer_stop(&sampleTimer); // Stop the timer
        Monitor.println("ECG sensor has been shutdown!");
    }
}

/**
 * @brief Interrupt service routine (ISR) for the timer being used for sampling.
 * Uses the provided timer as input to identify which timer the ISR
 * is related to. Sets a flag for when the ADC should sample to true.
 *
 * @param timer Pointer to the hardware timer the ISR is registered to
 * @return
 */
void sampleTimerCallback(struct k_timer *timer)
{
    sampleNow = true;
}

/**
 * @brief Used for setting up ADC, GPIO Pins, timer, and ISR for timer.
 * Runs once at start of MCU program.
 */
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
    int ret2 = adc_channel_setup(adc_dev, &adc_ch2_cfg);
    if (ret < 0) // Check if ADC channel has been setup correctly
    {
        Monitor.println("ERROR: ADC channel setup failed");
        return;
    }
    if (ret2 < 0)
    {
        Monitor.println("ERROR: ADC channel 2 setup failed");
    }
    gpio_pin_configure(gpio_b, PIN_SHUTDOWN, GPIO_OUTPUT_INACTIVE); // D9 pin for shutdown/start of ECG sensor
    gpio_pin_configure(gpio_b, PIN_LO_PLUS, GPIO_INPUT);            // D10 Lo+ lead off detection
    gpio_pin_configure(gpio_b, PIN_LO_MINUS, GPIO_INPUT);           // D11 Lo- lead off detection
    initFilter(ecgFilter);                                          // Load default Butterworth coefficients
    initBuffers(bufs);
    k_timer_init(&sampleTimer, sampleTimerCallback, NULL); // Initialise timer for sampling and timer interrupt service routine function
    Bridge.provide("setECGEnabled", setECGEnabled);        // Provide ECG sensor ON/OFF switch
    Bridge.provide("setFilterCoeffs", setFilterCoeffs);
    delay(5000); // Allow bridge to initialise by delaying for 5 seconds
}

/**
 * @brief Runs continuously. Used for sampling and checking if buffer is ready to be sent to MPU.
 *
 */
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
                int ret2 = adc_read(adc_dev, &adc_seq2);
                if (ret == 0 && ret2 == 0) // Check if ADC has read value
                {
                    uint16_t leadI = (uint16_t)adc_raw;                           // Sample from ADC1
                    uint16_t leadII = (uint16_t)adc_raw2;                         // Sample from ADC4
                    uint16_t leadIII = leadII - leadI;                            // Derived sample for 3rd lead
                    float filteredLeadI = applyFilter(ecgFilter, (float)leadI);   // Filter raw sample from ADC1
                    float filteredLeadII = applyFilter(ecgFilter, (float)leadII); // Filter raw sample from ADC4
                    float filteredleadIII = filteredLeadII - filteredLeadI;       // Derived filtered sample for 3rd lead
                    pushSample(bufs, leadI, filteredLeadI);                       // Push raw and filtered samples to buffers
                }
            }
        }
    }

    if (bufs.bufferReady)
    {                             // Check if buffer is ready
        bufs.bufferReady = false; // Set buffer to not ready
        Monitor.println("ECG buffer is ready!");
        computeFFT(bufs); // Compute FFT values for the filtered ECG data
        std::array<uint16_t, BUFFER_SIZE> rawArr;
        std::array<float, BUFFER_SIZE> filtArr;
        std::array<float, FFT_BINS> fftArr;
        memcpy(rawArr.data(), bufs.ecgBuffer, BUFFER_SIZE * sizeof(uint16_t));
        memcpy(filtArr.data(), bufs.filteredBuffer, BUFFER_SIZE * sizeof(float));
        memcpy(fftArr.data(), bufs.fftBuffer, FFT_BINS * sizeof(float));
        Bridge.notify("ecg_packet", std::make_tuple(rawArr, filtArr, fftArr)); // Send data to MPU without looking for return value
        Monitor.println("Sent raw, filtered, and FFT data to MPU.");
        resetAfterSend(bufs); // Reset buffers after data is sent to MPU
        Monitor.println("Buffer emptied.");
    }
}
