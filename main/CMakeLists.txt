idf_component_register(SRCS "display_epaper/display.c"
                            "display_epaper/utf8.c"
                            "display_epaper/graphics.c"
                            "display_epaper/driver/epd_driver_gdew0102t4.c"
                            "display_epaper/fonts/font.c"
                            "main.c"
                       INCLUDE_DIRS "." "display_epaper" "display_epaper/driver" "display_epaper/fonts"
                       PRIV_REQUIRES esp_driver_spi esp_driver_gpio)
