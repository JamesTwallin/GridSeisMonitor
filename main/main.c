/**
 * @file main.c
 * @brief GridSeis - Grid Frequency Monitor with Servo Dial
 *
 * Simple firmware that detects 50Hz mains frequency from ambient EM fields
 * and displays the frequency on a physical dial using an MG996R servo.
 *
 * Hardware:
 * - ESP32 DevKit
 * - MG996R servo on GPIO 4 (PWM output)
 * - Floating ADC pin (GPIO 34 / ADC1_CH6) for 50Hz pickup
 *   (optionally attach 10-30cm wire antenna)
 */

#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "gridseis";

/* ----- Configuration ----- */

/* ADC Configuration */
#define ADC_CHANNEL         ADC_CHANNEL_6   /* GPIO 34 on ESP32 */
#define ADC_SAMPLE_RATE     1000            /* 1000 Hz sampling */
#define ADC_SAMPLES_1SEC    1000            /* Samples per measurement */

/* Servo Configuration (MG996R) */
#define SERVO_GPIO          4               /* PWM output pin */
#define SERVO_FREQ_HZ       50              /* 50 Hz PWM frequency */
#define SERVO_RESOLUTION    LEDC_TIMER_14_BIT
#define SERVO_DUTY_MIN      410             /* ~1ms pulse (0 degrees) */
#define SERVO_DUTY_MAX      2048            /* ~2ms pulse (180 degrees) */
#define SERVO_DUTY_CENTER   1229            /* ~1.5ms pulse (90 degrees) */

/* Grid Frequency Parameters */
#define NOMINAL_FREQ        50.0f           /* UK/EU grid nominal */
#define FREQ_RANGE          0.15f           /* +/- 0.15 Hz display range (covers normal grid variation) */

/* IQ Demodulation Tables */
#define TWO_PI              (2.0f * M_PI)
static float sin_table[ADC_SAMPLES_1SEC];
static float cos_table[ADC_SAMPLES_1SEC];

/* ADC Handle */
static adc_oneshot_unit_handle_t adc_handle = NULL;

/* Phase tracking for frequency measurement */
static float last_phase = 0.0f;
static bool first_measurement = true;

/**
 * @brief Initialize IQ demodulation lookup tables
 */
static void init_iq_tables(void)
{
    for (int i = 0; i < ADC_SAMPLES_1SEC; i++) {
        float t = (float)i / (float)ADC_SAMPLE_RATE;
        float phase = TWO_PI * NOMINAL_FREQ * t;
        sin_table[i] = sinf(phase);
        cos_table[i] = cosf(phase);
    }
    ESP_LOGI(TAG, "IQ tables initialized for %.1f Hz detection", NOMINAL_FREQ);
}

/**
 * @brief Initialize ADC for oneshot sampling
 */
static esp_err_t init_adc(void)
{
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle));

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_CHANNEL, &config));

    ESP_LOGI(TAG, "ADC initialized on channel %d (GPIO 34)", ADC_CHANNEL);
    return ESP_OK;
}

/**
 * @brief Initialize servo PWM using LEDC
 */
static esp_err_t init_servo(void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = SERVO_RESOLUTION,
        .freq_hz = SERVO_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    ledc_channel_config_t channel_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = SERVO_GPIO,
        .duty = SERVO_DUTY_CENTER,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel_cfg));

    ESP_LOGI(TAG, "Servo initialized on GPIO %d", SERVO_GPIO);
    return ESP_OK;
}

/**
 * @brief Set servo position
 * @param angle Angle in degrees (0-180)
 */
static void set_servo_angle(float angle)
{
    if (angle < 0.0f) angle = 0.0f;
    if (angle > 180.0f) angle = 180.0f;

    uint32_t duty = SERVO_DUTY_MIN +
                    (uint32_t)((angle / 180.0f) * (SERVO_DUTY_MAX - SERVO_DUTY_MIN));

    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

/**
 * @brief Map frequency to servo angle (centered on 90°, ±45° range)
 *
 * 49.85 Hz -> 135 degrees
 * 50.0 Hz -> 90 degrees (center)
 * 50.15 Hz -> 45 degrees
 */
static float freq_to_angle(float freq)
{
    float deviation = freq - NOMINAL_FREQ;
    /* Map deviation to ±45° range centered on 90° */
    return 90.0f - (deviation / FREQ_RANGE) * 45.0f;
}

/**
 * @brief Measure grid frequency using IQ demodulation
 *
 * Samples ADC for 1 second using oneshot reads and correlates with
 * reference 50Hz sine/cosine to extract phase. Frequency is derived
 * from phase change between measurements.
 */
static esp_err_t measure_frequency(float *freq_hz, float *amplitude)
{
    float samples[ADC_SAMPLES_1SEC];
    int sample_count = 0;

    /* Sample at 1kHz for 1 second using oneshot ADC */
    int64_t sample_interval_us = 1000000 / ADC_SAMPLE_RATE;  /* 1000us = 1ms */
    int64_t next_sample_time = esp_timer_get_time();

    while (sample_count < ADC_SAMPLES_1SEC) {
        int64_t now = esp_timer_get_time();
        if (now >= next_sample_time) {
            int raw_value;
            esp_err_t ret = adc_oneshot_read(adc_handle, ADC_CHANNEL, &raw_value);
            if (ret == ESP_OK) {
                samples[sample_count++] = (float)raw_value - 2048.0f;
            }
            next_sample_time += sample_interval_us;
        }
        /* Small yield to prevent watchdog */
        if (sample_count % 100 == 0) {
            vTaskDelay(1);
        }
    }

    /* IQ demodulation */
    float i_sum = 0.0f, q_sum = 0.0f;
    for (int i = 0; i < sample_count; i++) {
        i_sum += samples[i] * cos_table[i];
        q_sum += samples[i] * sin_table[i];
    }
    i_sum /= (float)sample_count;
    q_sum /= (float)sample_count;

    /* Phase and amplitude */
    float phase = atan2f(q_sum, i_sum);
    *amplitude = sqrtf(i_sum * i_sum + q_sum * q_sum) / 2048.0f;

    /* Frequency from phase difference */
    if (first_measurement) {
        first_measurement = false;
        last_phase = phase;
        *freq_hz = NOMINAL_FREQ;
        return ESP_OK;
    }

    /* Unwrap phase */
    float phase_diff = phase - last_phase;
    if (phase_diff > M_PI) phase_diff -= TWO_PI;
    if (phase_diff < -M_PI) phase_diff += TWO_PI;

    /* Phase change per second = frequency offset (negative because phase lags when freq > nominal) */
    *freq_hz = NOMINAL_FREQ - (phase_diff / TWO_PI);
    last_phase = phase;

    return ESP_OK;
}

/**
 * @brief Main application entry point
 */
void app_main(void)
{
    ESP_LOGI(TAG, "GridSeis - Grid Frequency Monitor");
    ESP_LOGI(TAG, "================================");

    /* Initialize subsystems */
    init_iq_tables();
    init_adc();
    init_servo();

    /* Startup sequence - show frequency bounds */
    float freq_min = NOMINAL_FREQ - FREQ_RANGE;
    float freq_max = NOMINAL_FREQ + FREQ_RANGE;

    ESP_LOGI(TAG, "========== FREQUENCY BOUNDS ==========");
    ESP_LOGI(TAG, "  MIN: %.3f Hz (servo 135°)", freq_min);
    ESP_LOGI(TAG, "  NOM: %.3f Hz (servo 90°)", NOMINAL_FREQ);
    ESP_LOGI(TAG, "  MAX: %.3f Hz (servo 45°)", freq_max);
    ESP_LOGI(TAG, "=======================================");

    /* Show min bound with double bounce to identify */
    ESP_LOGI(TAG, "Showing MIN: %.3f Hz (double bounce)", freq_min);
    set_servo_angle(135.0f);
    vTaskDelay(pdMS_TO_TICKS(3000));
    set_servo_angle(120.0f);
    vTaskDelay(pdMS_TO_TICKS(300));
    set_servo_angle(135.0f);
    vTaskDelay(pdMS_TO_TICKS(300));
    set_servo_angle(120.0f);
    vTaskDelay(pdMS_TO_TICKS(300));
    set_servo_angle(135.0f);
    vTaskDelay(pdMS_TO_TICKS(3000));

    /* Show center (nominal) */
    ESP_LOGI(TAG, "Showing NOM: %.3f Hz", NOMINAL_FREQ);
    set_servo_angle(90.0f);
    vTaskDelay(pdMS_TO_TICKS(2000));

    /* Show max bound */
    ESP_LOGI(TAG, "Showing MAX: %.3f Hz", freq_max);
    set_servo_angle(45.0f);
    vTaskDelay(pdMS_TO_TICKS(7000));

    /* Return to center */
    set_servo_angle(90.0f);
    vTaskDelay(pdMS_TO_TICKS(1000));

    ESP_LOGI(TAG, "Starting frequency measurement...");
    ESP_LOGI(TAG, "Range: %.2f - %.2f Hz", NOMINAL_FREQ - FREQ_RANGE, NOMINAL_FREQ + FREQ_RANGE);

    /* Main loop - measure frequency every second */
    float smoothed_freq = NOMINAL_FREQ;
    const float freq_alpha = 0.3f;  /* Frequency smoothing factor (for logging) */

    while (1) {
        float freq, amplitude;
        esp_err_t ret = measure_frequency(&freq, &amplitude);

        if (ret == ESP_OK) {
            /* Apply exponential smoothing to frequency (for logging) */
            smoothed_freq = freq_alpha * freq + (1.0f - freq_alpha) * smoothed_freq;

            /* Map instantaneous frequency directly to servo angle */
            float target_angle = freq_to_angle(freq);
            set_servo_angle(target_angle);

            /* Log measurement as JSON for data capture */
            int64_t timestamp_ms = esp_timer_get_time() / 1000;
            printf("{\"t\":%lld,\"freq\":%.4f,\"smoothed\":%.4f,\"signal\":%.3f}\n",
                     timestamp_ms, freq, smoothed_freq, amplitude);
        } else {
            ESP_LOGW(TAG, "Measurement failed");
        }

        vTaskDelay(pdMS_TO_TICKS(100));  /* Short delay between measurements */
    }
}
