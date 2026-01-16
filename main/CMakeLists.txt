idf_component_register(
  SRC_DIRS "." "display_epaper" "display_epaper/driver" "display_epaper/fonts" "show_messages" "system_state" "wifi" "sntp" "ota_update" "battery_level" "deep_sleep"
  INCLUDE_DIRS "."
  EMBED_TXTFILES "ota_update/cert.pem"
  PRIV_REQUIRES driver esp_wifi esp_netif esp_http_client nvs_flash app_update esp_https_ota esp_adc mbedtls esp_driver_spi esp_driver_gpio
)
