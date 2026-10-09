/**
 * @file rtc_pcf85063.c
 * @brief PCF85063 real-time clock driver (I2C)
 */

#include <sdkconfig.h>
#include <string.h>
#include <sys/time.h>
#include <esp_log.h>
#include <driver/i2c_master.h>

#include "rtc_pcf85063.h"
#include "../board_i2c/board_i2c.h"
#include "../time_utils/time_utils.h"

static const char *TAG = "rtc_pcf85063";

#define PCF85063_I2C_ADDRESS     0x51
#define PCF85063_I2C_SPEED_HZ    100000
#define PCF85063_I2C_TIMEOUT_MS  100

/* Register map */
#define REG_CONTROL_1        0x00
#define REG_CONTROL_2        0x01
#define REG_SECONDS          0x04
#define REG_SECOND_ALARM     0x0B

#define CTRL1_STOP           (1 << 5)
#define CTRL1_12_24          (1 << 1)

#define CTRL2_AIE            (1 << 7) /* Alarm interrupt enable */
#define CTRL2_AF             (1 << 6) /* Alarm flag */
#define CTRL2_COF_OFF        0x07     /* CLKOUT disabled */

#define SECONDS_OS           (1 << 7) /* Oscillator stop: clock integrity not guaranteed */
#define ALARM_DISABLED       (1 << 7) /* AEN_x bit: 1 = this field is ignored */

#define RTC_MIN_VALID_YEAR   2025     /* Anything earlier means the RTC was never set */

static i2c_master_dev_handle_t s_dev = NULL;
static bool s_available = false;

static uint8_t bcd_to_bin(uint8_t value)
{
    return (uint8_t)(((value >> 4) * 10) + (value & 0x0F));
}

static uint8_t bin_to_bcd(uint8_t value)
{
    return (uint8_t)(((value / 10) << 4) | (value % 10));
}

static esp_err_t rtc_read(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, data, len, PCF85063_I2C_TIMEOUT_MS);
}

static esp_err_t rtc_write(uint8_t reg, const uint8_t *data, size_t len)
{
    uint8_t tx[1 + 8];
    if (len > sizeof(tx) - 1) {
        return ESP_ERR_INVALID_SIZE;
    }
    tx[0] = reg;
    memcpy(&tx[1], data, len);
    return i2c_master_transmit(s_dev, tx, len + 1, PCF85063_I2C_TIMEOUT_MS);
}

/* UTC broken-down time -> seconds since the epoch (days-from-civil algorithm) */
static time_t utc_to_time_t(const struct tm *t)
{
    int year = t->tm_year + 1900;
    int month = t->tm_mon + 1;
    int day = t->tm_mday;

    year -= (month <= 2);
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned year_of_era = (unsigned)(year - era * 400);
    const unsigned day_of_year = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    const int64_t days = (int64_t)era * 146097 + (int64_t)day_of_era - 719468;

    return (time_t)(days * 86400 + t->tm_hour * 3600 + t->tm_min * 60 + t->tm_sec);
}

esp_err_t rtc_pcf85063_init(void)
{
    if (s_available) {
        return ESP_OK;
    }

    if (s_dev == NULL) {
        i2c_master_bus_handle_t bus;
        esp_err_t err = board_i2c_get_bus(&bus);
        if (err != ESP_OK) {
            return err;
        }

        i2c_device_config_t dev_config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = PCF85063_I2C_ADDRESS,
            .scl_speed_hz = PCF85063_I2C_SPEED_HZ,
        };
        err = i2c_master_bus_add_device(bus, &dev_config, &s_dev);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add RTC device: %s", esp_err_to_name(err));
            return err;
        }
    }

    uint8_t control[2];
    if (rtc_read(REG_CONTROL_1, control, sizeof(control)) != ESP_OK) {
        ESP_LOGW(TAG, "PCF85063 not responding");
        return ESP_ERR_NOT_FOUND;
    }

    /* 24-hour mode, oscillator running */
    uint8_t ctrl1 = control[0] & ~(CTRL1_STOP | CTRL1_12_24);
    if (ctrl1 != control[0]) {
        rtc_write(REG_CONTROL_1, &ctrl1, 1);
    }

    /* Keep the alarm enable bit, clear a pending alarm flag (releases the
     * interrupt line after an alarm wake-up) and switch CLKOUT off to save power */
    uint8_t ctrl2 = (control[1] & CTRL2_AIE) | CTRL2_COF_OFF;
    if (ctrl2 != control[1]) {
        rtc_write(REG_CONTROL_2, &ctrl2, 1);
    }

    s_available = true;
    ESP_LOGI(TAG, "PCF85063 ready");
    return ESP_OK;
}

esp_err_t rtc_pcf85063_get_time(struct tm *utc_tm)
{
    if (!s_available || utc_tm == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t regs[7];
    esp_err_t err = rtc_read(REG_SECONDS, regs, sizeof(regs));
    if (err != ESP_OK) {
        return err;
    }

    if (regs[0] & SECONDS_OS) {
        ESP_LOGW(TAG, "Oscillator stop flag set: RTC time is not reliable");
        return ESP_ERR_INVALID_STATE;
    }

    memset(utc_tm, 0, sizeof(*utc_tm));
    utc_tm->tm_sec = bcd_to_bin(regs[0] & 0x7F);
    utc_tm->tm_min = bcd_to_bin(regs[1] & 0x7F);
    utc_tm->tm_hour = bcd_to_bin(regs[2] & 0x3F);
    utc_tm->tm_mday = bcd_to_bin(regs[3] & 0x3F);
    utc_tm->tm_wday = regs[4] & 0x07;
    utc_tm->tm_mon = bcd_to_bin(regs[5] & 0x1F) - 1;
    utc_tm->tm_year = bcd_to_bin(regs[6]) + 100; /* 2000-based */

    if (utc_tm->tm_sec > 59 || utc_tm->tm_min > 59 || utc_tm->tm_hour > 23 ||
        utc_tm->tm_mday < 1 || utc_tm->tm_mday > 31 || utc_tm->tm_mon < 0 || utc_tm->tm_mon > 11) {
        ESP_LOGW(TAG, "RTC registers out of range");
        return ESP_ERR_INVALID_STATE;
    }

    return ESP_OK;
}

esp_err_t rtc_pcf85063_set_time(const struct tm *utc_tm)
{
    if (!s_available || utc_tm == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t regs[7] = {
        bin_to_bcd(utc_tm->tm_sec),            /* OS flag cleared */
        bin_to_bcd(utc_tm->tm_min),
        bin_to_bcd(utc_tm->tm_hour),
        bin_to_bcd(utc_tm->tm_mday),
        (uint8_t)(utc_tm->tm_wday & 0x07),
        bin_to_bcd(utc_tm->tm_mon + 1),
        bin_to_bcd((utc_tm->tm_year + 1900) % 100),
    };

    return rtc_write(REG_SECONDS, regs, sizeof(regs));
}

esp_err_t rtc_pcf85063_restore_system_time(void)
{
    struct tm utc_tm;
    esp_err_t err = rtc_pcf85063_get_time(&utc_tm);
    if (err != ESP_OK) {
        return err;
    }

    if (utc_tm.tm_year + 1900 < RTC_MIN_VALID_YEAR) {
        ESP_LOGW(TAG, "RTC has never been set (year %d)", utc_tm.tm_year + 1900);
        return ESP_ERR_INVALID_STATE;
    }

    struct timeval tv = {
        .tv_sec = utc_to_time_t(&utc_tm),
        .tv_usec = 0,
    };
    settimeofday(&tv, NULL);

    ESP_LOGI(TAG, "System time restored from RTC: %04d-%02d-%02d %02d:%02d:%02d UTC",
             utc_tm.tm_year + 1900, utc_tm.tm_mon + 1, utc_tm.tm_mday,
             utc_tm.tm_hour, utc_tm.tm_min, utc_tm.tm_sec);
    return ESP_OK;
}

esp_err_t rtc_pcf85063_save_system_time(void)
{
    if (!time_utils_is_valid()) {
        return ESP_ERR_INVALID_STATE;
    }

    time_t now = 0;
    struct tm utc_tm = {0};
    time(&now);
    gmtime_r(&now, &utc_tm);

    esp_err_t err = rtc_pcf85063_set_time(&utc_tm);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "RTC set to %04d-%02d-%02d %02d:%02d:%02d UTC",
                 utc_tm.tm_year + 1900, utc_tm.tm_mon + 1, utc_tm.tm_mday,
                 utc_tm.tm_hour, utc_tm.tm_min, utc_tm.tm_sec);
    }
    return err;
}

esp_err_t rtc_pcf85063_arm_alarm_at(time_t target)
{
    if (!s_available) {
        return ESP_ERR_INVALID_STATE;
    }

    time_t now = 0;
    time(&now);

    struct tm utc_tm = {0};
    gmtime_r(&target, &utc_tm);

    uint8_t alarm_regs[5] = {
        bin_to_bcd(utc_tm.tm_sec),    /* second alarm */
        bin_to_bcd(utc_tm.tm_min),    /* minute alarm */
        bin_to_bcd(utc_tm.tm_hour),   /* hour alarm */
        ALARM_DISABLED,               /* day alarm ignored */
        ALARM_DISABLED,               /* weekday alarm ignored */
    };
    esp_err_t err = rtc_write(REG_SECOND_ALARM, alarm_regs, sizeof(alarm_regs));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write alarm registers: %s", esp_err_to_name(err));
        return err;
    }

    /* Enable the alarm interrupt and clear the alarm flag (releases INT) */
    uint8_t ctrl2 = CTRL2_AIE | CTRL2_COF_OFF;
    err = rtc_write(REG_CONTROL_2, &ctrl2, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable alarm: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Alarm armed for %02d:%02d:%02d UTC, in %lld seconds",
             utc_tm.tm_hour, utc_tm.tm_min, utc_tm.tm_sec, (long long)(target - now));
    return ESP_OK;
}

esp_err_t rtc_pcf85063_arm_daily_alarm(int local_hour, int local_minute)
{
    if (!s_available || !time_utils_is_valid()) {
        return ESP_ERR_INVALID_STATE;
    }

    time_utils_init_timezone();

    time_t now = 0;
    struct tm local_tm = {0};
    time(&now);
    localtime_r(&now, &local_tm);

    /* Next occurrence of local_hour:local_minute in local time */
    struct tm target_tm = local_tm;
    target_tm.tm_hour = local_hour;
    target_tm.tm_min = local_minute;
    target_tm.tm_sec = 0;
    target_tm.tm_isdst = -1;
    time_t target = mktime(&target_tm);
    if (target <= now) {
        target_tm.tm_mday += 1;
        target_tm.tm_isdst = -1;
        target = mktime(&target_tm);
    }

    ESP_LOGI(TAG, "Daily alarm at %02d:%02d local", local_hour, local_minute);
    return rtc_pcf85063_arm_alarm_at(target);
}
