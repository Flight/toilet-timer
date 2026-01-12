#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_log.h>

#include "sdkconfig.h"
#include "global_event_group.h"

#include "battery_level.h"

static const char *TAG = "Battery";
static const gpio_num_t BATTERY_LEVEL_GPIO = CONFIG_BATTERY_LEVEL_GPIO;

// As ESP32 ADC is not very stable, we use moving average to smooth the readings
#define MOVING_AVERAGE_SIZE 10

int global_battery_level = 100;

uint16_t adc_low_battery = 1800;
uint16_t adc_high_battery = 2450;

void battery_level_task(void *pvParameter)
{
#ifdef CONFIG_IS_BATTERY_LEVEL_ENABLED
  ESP_LOGI(TAG, "Is enabled");
  adc_channel_t channel;
  adc_unit_t unit;
  int adc_values[MOVING_AVERAGE_SIZE] = {0}; // Buffer to store recent readings
  int adc_index = 0;                         // Current index for the buffer
  int adc_sum = 0;                           // Sum of the buffer values

  esp_err_t err = adc_oneshot_io_to_channel(BATTERY_LEVEL_GPIO, &unit, &channel);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Pin %d is not ADC pin!", BATTERY_LEVEL_GPIO);
    vTaskDelete(NULL);
  }
  else
  {
    ESP_LOGI(TAG, "ADC channel: %d", channel);
  }
  adc_oneshot_unit_handle_t adc_handle;
  adc_oneshot_unit_init_cfg_t init_config = {
      .unit_id = unit,
  };
  adc_oneshot_new_unit(&init_config, &adc_handle);

  adc_oneshot_chan_cfg_t config = {
      .bitwidth = ADC_BITWIDTH_DEFAULT,
      .atten = ADC_ATTEN_DB_12,
  };
  adc_oneshot_config_channel(adc_handle, channel, &config);

  ESP_LOGI(TAG, "Init end");

  while (true)
  {
    int adc_value;
    adc_oneshot_read(adc_handle, channel, &adc_value);

    // Update buffer
    adc_sum -= adc_values[adc_index];  // Subtract the old value
    adc_values[adc_index] = adc_value; // Add the new value
    adc_sum += adc_value;              // Update the sum

    // Update index
    adc_index = (adc_index + 1) % MOVING_AVERAGE_SIZE;

    // Calculate moving average
    int adc_average = adc_sum / MOVING_AVERAGE_SIZE;

    if (adc_average < adc_low_battery)
    {
      global_battery_level = 0;
    }
    else if (adc_average > adc_high_battery)
    {
      global_battery_level = 100;
    }
    else
    {
      global_battery_level = (adc_average - adc_low_battery) * 100 / (adc_high_battery - adc_low_battery);
    }

    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
#else
  ESP_LOGI(TAG, "Is disabled in SDK config");
  vTaskDelete(NULL);
#endif
}