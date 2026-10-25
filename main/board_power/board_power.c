/**
 * @file board_power.c
 * @brief Board power control for the Waveshare ESP32-S3-ePaper-1.54
 */

#include "sdkconfig.h"
#include "board_power.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <driver/usb_serial_jtag.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <esp_log.h>

#include "../nvs_utils/nvs_utils.h"
#include "../battery_level/battery_level.h"

static const char *TAG = "board_power";

#define LATCH_GPIO          ((gpio_num_t)CONFIG_BOARD_POWER_LATCH_GPIO)
#define AUDIO_GPIO          ((gpio_num_t)CONFIG_BOARD_AUDIO_POWER_GPIO)
#define EPD_GPIO            ((gpio_num_t)CONFIG_EPD_PIN_POWER)

#define LATCH_ON_LEVEL      1 /* BAT_Control HIGH keeps the 3.3V rail enabled */

/* Audio supply switch (PA_EN) is active LOW. It is always off in deep sleep;
 * while awake it normally stays on because the codec shares the I2C bus. */
#define AUDIO_OFF_LEVEL     1
#ifdef CONFIG_BOARD_AUDIO_POWER_KEEP_ON
#define AUDIO_AWAKE_LEVEL   0
#define AUDIO_AWAKE_TEXT    "on"
#else
#define AUDIO_AWAKE_LEVEL   1
#define AUDIO_AWAKE_TEXT    "off"
#endif

#ifdef CONFIG_EPD_POWER_ACTIVE_LOW
#define EPD_OFF_LEVEL       1
#else
#define EPD_OFF_LEVEL       0
#endif

#ifdef CONFIG_BOARD_POWER_LATCH_WEAK
/* Bookkeeping of the weak latch in NVS */
#define NVS_POWER_NAMESPACE    "board_power"
#define NVS_LATCH_STATE_KEY    "latch_state" /* latch_state_t */
#define NVS_LATCH_PHASE_KEY    "latch_phase" /* latch_phase_t: what the weak latch was doing when the power last went */
#define NVS_LATCH_TRIAL_KEY    "latch_trial" /* CONFIG_BOARD_POWER_LATCH_WEAK_TRIAL_ID the verdict belongs to */
#define NVS_SLEEP_MV_KEY       "sleep_mv"    /* Battery voltage measured on the wake-up before the last sleep */
#define LATCH_EMPTY_BATTERY_MV 3500          /* Below this a power loss is blamed on the battery, not on the latch */
#define LATCH_PROBE_SAMPLES    16
#define LATCH_PROBE_SETTLE_MS  20
#define LATCH_PROBE_MIN_MV     520           /* Base node below this: the transistor would not conduct enough */
#define LATCH_PROBE_USB_WAIT_MS 30           /* Let the USB connection monitor see a few ticks first */

typedef enum {
    LATCH_STATE_UNTESTED = 0,
    LATCH_STATE_CONFIRMED = 1, /* The weak latch carried the board through a deep sleep */
    LATCH_STATE_FAILED = 2,    /* The board lost power while on the weak latch */
} latch_state_t;

typedef enum {
    LATCH_PHASE_STRONG = 0,      /* Driven HIGH */
    LATCH_PHASE_WEAK_AWAKE = 1,  /* Running on the pull-up */
    LATCH_PHASE_WEAK_ASLEEP = 2, /* Sleeping on the pull-up */
} latch_phase_t;
#endif

static bool s_latch_weak = false;

/* Configure a pin as output without ever glitching it to the wrong level */
static void drive_output(gpio_num_t pin, int level)
{
    gpio_set_level(pin, level);

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(pin, level);
}

/* BAT_Control as an ordinary output driven HIGH (also returns the pad from
 * the RTC mux used by the weak latch; gpio_config() does that under the hood) */
static void latch_set_strong(void)
{
    rtc_gpio_deinit(LATCH_GPIO);
    drive_output(LATCH_GPIO, LATCH_ON_LEVEL);
    s_latch_weak = false;
}

void board_power_init_early(void)
{
    /* Battery latch: driven HIGH first. On a cold start the user is holding
     * the PWR key and the latch must take over before the key is released;
     * the weak latch, if any, is applied later when NVS is available. The
     * pad may still be held (and routed to the RTC mux by the weak latch)
     * from the previous sleep: everything is configured under the hold and
     * only then released, so the rail never sees a gap. */
    latch_set_strong();
    gpio_hold_dis(LATCH_GPIO);

    /* Audio codec supply (see CONFIG_BOARD_AUDIO_POWER_KEEP_ON) */
    drive_output(AUDIO_GPIO, AUDIO_AWAKE_LEVEL);
    gpio_hold_dis(AUDIO_GPIO);

    gpio_deep_sleep_hold_dis();

    ESP_LOGI(TAG, "Battery latch GPIO%d asserted, audio power GPIO%d %s",
             CONFIG_BOARD_POWER_LATCH_GPIO, CONFIG_BOARD_AUDIO_POWER_GPIO, AUDIO_AWAKE_TEXT);
}

#ifdef CONFIG_BOARD_POWER_LATCH_WEAK

/* Hand BAT_Control to the RTC mux as an input with the internal pull-up.
 * Pull-up and direction are written first and the mux switched last, so the
 * pad goes from "driven HIGH" straight to "pulled up" without floating. */
static void latch_set_weak(void)
{
    rtc_gpio_pulldown_dis(LATCH_GPIO);
    rtc_gpio_pullup_en(LATCH_GPIO);
    rtc_gpio_set_direction(LATCH_GPIO, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_init(LATCH_GPIO);
    s_latch_weak = true;
}

/* Measure the voltage the base node settles at when only the pull-up feeds
 * it. The latch is released for a moment, so this is only safe while USB
 * powers the board (the ADC setup floats the pad before the pull-up takes
 * over, and a transistor that lets go would drop the battery rail).
 * Returns millivolts, or -1 if the ADC could not be used. */
static int latch_probe_weak_bias_mv(void)
{
    adc_unit_t unit;
    adc_channel_t channel;
    if (adc_oneshot_io_to_channel(CONFIG_BOARD_POWER_LATCH_GPIO, &unit, &channel) != ESP_OK) {
        return -1;
    }

    adc_oneshot_unit_handle_t adc = NULL;
    adc_oneshot_unit_init_cfg_t unit_cfg = { .unit_id = unit };
    if (adc_oneshot_new_unit(&unit_cfg, &adc) != ESP_OK) {
        return -1;
    }
    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_0, /* 0 - ~950 mV: the base node sits well below 1 V */
    };
    int result = -1;
    if (adc_oneshot_config_channel(adc, channel, &chan_cfg) == ESP_OK) {
        adc_cali_handle_t cali = NULL;
        adc_cali_curve_fitting_config_t cali_cfg = {
            .unit_id = unit,
            .chan = channel,
            .atten = ADC_ATTEN_DB_0,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali) != ESP_OK) {
            cali = NULL;
        }

        /* The ADC setup left the pad floating: pull it up the way the weak latch does */
        latch_set_weak();
        vTaskDelay(pdMS_TO_TICKS(LATCH_PROBE_SETTLE_MS));

        int sum = 0;
        int samples = 0;
        for (int i = 0; i < LATCH_PROBE_SAMPLES; i++) {
            int raw = 0;
            if (adc_oneshot_read(adc, channel, &raw) != ESP_OK) {
                continue;
            }
            int mv = 0;
            if (cali == NULL || adc_cali_raw_to_voltage(cali, raw, &mv) != ESP_OK) {
                mv = raw * 950 / 4095;
            }
            sum += mv;
            samples++;
        }
        if (samples > 0) {
            result = sum / samples;
        }
        if (cali != NULL) {
            adc_cali_delete_scheme_curve_fitting(cali);
        }
    }
    adc_oneshot_del_unit(adc);

    /* Back to the driven HIGH before anything else happens */
    latch_set_strong();
    return result;
}

static const char *reset_reason_name(esp_reset_reason_t reason)
{
    switch (reason) {
        case ESP_RST_POWERON:   return "power-on";
        case ESP_RST_BROWNOUT:  return "brownout";
        case ESP_RST_DEEPSLEEP: return "deep-sleep wake-up";
        case ESP_RST_SW:        return "software";
        case ESP_RST_PANIC:     return "panic";
        case ESP_RST_USB:       return "USB";
        case ESP_RST_JTAG:      return "JTAG";
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:       return "watchdog";
        default:                return "other";
    }
}

static void write_u8_if_changed(const char *key, uint8_t current, uint8_t value)
{
    if (current != value) {
        nvs_utils_write_u8(NVS_POWER_NAMESPACE, key, value);
    }
}

void board_power_latch_apply_policy(void)
{
    uint8_t state = LATCH_STATE_UNTESTED;
    uint8_t phase = LATCH_PHASE_STRONG;
    uint8_t trial = 0;
    uint32_t sleep_mv = 0;
    nvs_utils_read_u8(NVS_POWER_NAMESPACE, NVS_LATCH_STATE_KEY, &state);
    nvs_utils_read_u8(NVS_POWER_NAMESPACE, NVS_LATCH_PHASE_KEY, &phase);
    nvs_utils_read_u8(NVS_POWER_NAMESPACE, NVS_LATCH_TRIAL_KEY, &trial);
    nvs_utils_read_u32(NVS_POWER_NAMESPACE, NVS_SLEEP_MV_KEY, &sleep_mv);

    if (trial != CONFIG_BOARD_POWER_LATCH_WEAK_TRIAL_ID) {
        /* A new trial number forgets any earlier verdict */
        if (state != LATCH_STATE_UNTESTED) {
            ESP_LOGW(TAG, "Weak latch trial %d: forgetting the earlier verdict", CONFIG_BOARD_POWER_LATCH_WEAK_TRIAL_ID);
        }
        write_u8_if_changed(NVS_LATCH_STATE_KEY, state, LATCH_STATE_UNTESTED);
        nvs_utils_write_u8(NVS_POWER_NAMESPACE, NVS_LATCH_TRIAL_KEY, CONFIG_BOARD_POWER_LATCH_WEAK_TRIAL_ID);
        state = LATCH_STATE_UNTESTED;
    }

    esp_reset_reason_t reset = esp_reset_reason();
    bool woke_from_sleep = (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_UNDEFINED);

    if (phase != LATCH_PHASE_STRONG) {
        /* The previous wake-up ran (and maybe slept) on the weak latch: how did it end? */
        if (woke_from_sleep) {
            if (state != LATCH_STATE_CONFIRMED) {
                ESP_LOGI(TAG, "Weak battery latch confirmed: it held through deep sleep");
                write_u8_if_changed(NVS_LATCH_STATE_KEY, state, LATCH_STATE_CONFIRMED);
                state = LATCH_STATE_CONFIRMED;
            }
        } else if (reset == ESP_RST_POWERON || reset == ESP_RST_BROWNOUT) {
            /* The board lost power. An almost empty battery explains that too. */
            int now_mv = 0;
            battery_level_read_percent(&now_mv);
            bool battery_empty = (phase == LATCH_PHASE_WEAK_ASLEEP && sleep_mv > 0 && sleep_mv < LATCH_EMPTY_BATTERY_MV) ||
                                 (now_mv > 0 && now_mv < LATCH_EMPTY_BATTERY_MV);
            if (battery_empty) {
                ESP_LOGW(TAG, "Power was lost while %s on the weak latch, but the battery was empty (%lu mV before sleep, %d mV now): not the latch's fault",
                         (phase == LATCH_PHASE_WEAK_ASLEEP) ? "asleep" : "awake", (unsigned long)sleep_mv, now_mv);
            } else {
                ESP_LOGE(TAG, "The board lost power (%s reset) while %s on the weak battery latch: driving BAT_Control HIGH from now on (~2.6 mA)",
                         reset_reason_name(reset), (phase == LATCH_PHASE_WEAK_ASLEEP) ? "asleep" : "awake");
                write_u8_if_changed(NVS_LATCH_STATE_KEY, state, LATCH_STATE_FAILED);
                state = LATCH_STATE_FAILED;
            }
        } else {
            ESP_LOGI(TAG, "%s reset after running on the weak latch: inconclusive", reset_reason_name(reset));
        }
    }

    if (state == LATCH_STATE_UNTESTED) {
        /* First trial. While a USB host powers the board the bias can be
         * measured without risk: too low a voltage means the transistor
         * would let go, and the trial is called off before it can hurt. */
        vTaskDelay(pdMS_TO_TICKS(LATCH_PROBE_USB_WAIT_MS));
        if (usb_serial_jtag_is_connected()) {
            int bias_mv = latch_probe_weak_bias_mv();
            if (bias_mv < 0) {
                ESP_LOGW(TAG, "Weak latch bias could not be measured, trying anyway");
            } else if (bias_mv < LATCH_PROBE_MIN_MV) {
                ESP_LOGE(TAG, "Weak latch bias is only %d mV (need %d): the pull-up cannot hold this board, keeping BAT_Control driven HIGH",
                         bias_mv, LATCH_PROBE_MIN_MV);
                write_u8_if_changed(NVS_LATCH_STATE_KEY, state, LATCH_STATE_FAILED);
                state = LATCH_STATE_FAILED;
            } else {
                ESP_LOGI(TAG, "Weak latch bias %d mV (need %d): starting the trial", bias_mv, LATCH_PROBE_MIN_MV);
            }
        } else {
            ESP_LOGI(TAG, "No USB host: starting the weak latch trial without a bias measurement");
        }
    }

    if (state == LATCH_STATE_FAILED) {
        write_u8_if_changed(NVS_LATCH_PHASE_KEY, phase, LATCH_PHASE_STRONG);
        ESP_LOGW(TAG, "Battery latch driven HIGH: the weak latch failed on this board (change the trial number to retry)");
        return;
    }

    /* Record that this wake-up runs on the weak latch, then switch over */
    write_u8_if_changed(NVS_LATCH_PHASE_KEY, phase, LATCH_PHASE_WEAK_AWAKE);
    latch_set_weak();
    ESP_LOGI(TAG, "Battery latch held by the internal pull-up (%s)",
             (state == LATCH_STATE_CONFIRMED) ? "confirmed on this board" : "trial");
}

#else /* CONFIG_BOARD_POWER_LATCH_WEAK */

void board_power_latch_apply_policy(void)
{
    ESP_LOGI(TAG, "Battery latch driven HIGH (weak latch disabled, ~2.6 mA)");
}

#endif /* CONFIG_BOARD_POWER_LATCH_WEAK */

bool board_power_latch_is_weak(void)
{
    return s_latch_weak;
}

void board_power_prepare_for_deep_sleep(void)
{
    /* E-paper supply off: the panel keeps its image without power */
    gpio_set_level(EPD_GPIO, EPD_OFF_LEVEL);
    gpio_hold_en(EPD_GPIO);

    /* Audio rail off: its load switch alone wastes 0.33 mA while on */
    gpio_set_level(AUDIO_GPIO, AUDIO_OFF_LEVEL);
    gpio_hold_en(AUDIO_GPIO);

#ifdef CONFIG_BOARD_POWER_LATCH_WEAK
    if (s_latch_weak) {
        int mv = battery_level_last_mv();
        if (mv > 0) {
            nvs_utils_write_u32(NVS_POWER_NAMESPACE, NVS_SLEEP_MV_KEY, (uint32_t)mv);
        }
        nvs_utils_write_u8(NVS_POWER_NAMESPACE, NVS_LATCH_PHASE_KEY, LATCH_PHASE_WEAK_ASLEEP);

        /* Freeze the pad (input + pull-up) and keep the RTC IO domain powered
         * so the internal pull-up stays active throughout deep sleep */
        rtc_gpio_hold_en(LATCH_GPIO);
        esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    } else
#endif
    {
        gpio_set_level(LATCH_GPIO, LATCH_ON_LEVEL);
        gpio_hold_en(LATCH_GPIO);
    }

    /* Required for the hold to survive deep sleep on digital (non-RTC) pads */
    gpio_deep_sleep_hold_en();

    ESP_LOGI(TAG, "Power pins held for deep sleep (latch %s, e-paper off, audio off)",
             s_latch_weak ? "on the pull-up" : "driven HIGH");
}
