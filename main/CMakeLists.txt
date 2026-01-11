idf_component_register(SRCS "display.c" "utf8.c" "main.c" "epd_driver.c" "graphics.c" "font.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_driver_spi esp_driver_gpio)
