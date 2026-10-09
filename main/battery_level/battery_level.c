/**
 * @file battery_level.c
 * @brief Battery voltage measurement through the on-board divider
 */

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_log.h>

#include "sdkconfig.h"
#include "battery_level.h"

static const char *TAG = "Battery";

#ifdef CONFIG_IS_BATTERY_LEVEL_ENABLED

static const gpio_num_t BATTERY_LEVEL_GPIO = CONFIG_BATTERY_LEVEL_GPIO;

/* The ESP32 ADC is noisy: average a burst of samples */
#define SAMPLE_COUNT 16
#define SAMPLE_DELAY_MS 2

/* Waveshare ESP32-S3-ePaper-1.54: BAT_ADC sits behind a 200K/200K divider, so VBAT = 2 x V(ADC) */
#define BATTERY_DIVIDER_RATIO 2

/* Typical single-cell lithium open-circuit voltage curve (mV -> %), interpolated linearly.
 * The measured voltage is first stretched so that CONFIG_BATTERY_FULL_MV maps to
 * the curve's 4200 mV (100 %) point while 3300 mV stays 0 %.
 * Note: while USB is connected the charger holds the terminal voltage, so the
 * figure is only meaningful when running on the battery. */
typedef struct {
    int mv;
    int percent;
} ocv_point_t;

static const ocv_point_t OCV_CURVE[] = {
    { 3300, 0 },  { 3500, 3 },  { 3600, 8 },  { 3650, 14 }, { 3700, 22 },
    { 3750, 33 }, { 3800, 45 }, { 3850, 55 }, { 3900, 64 }, { 3950, 72 },
    { 4000, 80 }, { 4050, 87 }, { 4100, 93 }, { 4150, 97 }, { 4200, 100 },
};
#define OCV_POINTS (sizeof(OCV_CURVE) / sizeof(OCV_CURVE[0]))
#define OCV_EMPTY_MV 3300
#define OCV_FULL_MV  4200

/* Fallback raw-to-millivolt conversion when calibration is unavailable (12 dB attenuation, 12-bit) */
#define ADC_FALLBACK_FULL_SCALE_MV 3100
#define ADC_FALLBACK_MAX_RAW 4095

static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static adc_cali_handle_t s_cali_handle = NULL;
static adc_channel_t s_channel;
static bool s_initialized = false;

static esp_err_t battery_level_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    adc_unit_t unit;
    esp_err_t err = adc_oneshot_io_to_channel(BATTERY_LEVEL_GPIO, &unit, &s_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Pin %d is not an ADC pin!", BATTERY_LEVEL_GPIO);
        return err;
    }

    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = unit,
    };
    err = adc_oneshot_new_unit(&init_config, &s_adc_handle);
    if (err != ESP_OK) {
        return err;
    }

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    err = adc_oneshot_config_channel(s_adc_handle, s_channel, &config);
    if (err != ESP_OK) {
        return err;
    }

    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = unit,
        .chan = s_channel,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_config, &s_cali_handle) != ESP_OK) {
        ESP_LOGW(TAG, "ADC calibration unavailable, using linear approximation");
        s_cali_handle = NULL;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "ADC unit %d channel %d ready", unit, s_channel);
    return ESP_OK;
}

int battery_level_read_percent(int *millivolts)
{
    if (battery_level_init() != ESP_OK) {
        return -1;
    }

    int sum_mv = 0;
    int samples = 0;
    for (int i = 0; i < SAMPLE_COUNT; i++) {
        int raw = 0;
        if (adc_oneshot_read(s_adc_handle, s_channel, &raw) != ESP_OK) {
            continue;
        }
        int adc_mv = 0;
        if (s_cali_handle == NULL || adc_cali_raw_to_voltage(s_cali_handle, raw, &adc_mv) != ESP_OK) {
            adc_mv = raw * ADC_FALLBACK_FULL_SCALE_MV / ADC_FALLBACK_MAX_RAW;
        }
        sum_mv += adc_mv;
        samples++;
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_DELAY_MS));
    }

    if (samples == 0) {
        return -1;
    }

    int battery_mv = (sum_mv / samples) * BATTERY_DIVIDER_RATIO;
    if (millivolts != NULL) {
        *millivolts = battery_mv;
    }

    /* Calibration: stretch the measured range [3300, CONFIG_BATTERY_FULL_MV] onto the curve's [3300, 4200] */
    int curve_mv = OCV_EMPTY_MV + (battery_mv - OCV_EMPTY_MV) * (OCV_FULL_MV - OCV_EMPTY_MV) / (CONFIG_BATTERY_FULL_MV - OCV_EMPTY_MV);

    int percent = 100;
    if (curve_mv <= OCV_CURVE[0].mv) {
        percent = 0;
    } else {
        for (size_t i = 1; i < OCV_POINTS; i++) {
            if (curve_mv <= OCV_CURVE[i].mv) {
                const ocv_point_t *lo = &OCV_CURVE[i - 1];
                const ocv_point_t *hi = &OCV_CURVE[i];
                percent = lo->percent + (curve_mv - lo->mv) * (hi->percent - lo->percent) / (hi->mv - lo->mv);
                break;
            }
        }
    }

    ESP_LOGI(TAG, "Battery %d mV (%d%%, full = %d mV)", battery_mv, percent, CONFIG_BATTERY_FULL_MV);
    return percent;
}

#else /* CONFIG_IS_BATTERY_LEVEL_ENABLED */

int battery_level_read_percent(int *millivolts)
{
    ESP_LOGI(TAG, "Is disabled in SDK config");
    if (millivolts != NULL) {
        *millivolts = 0;
    }
    return -1;
}

#endif /* CONFIG_IS_BATTERY_LEVEL_ENABLED */
