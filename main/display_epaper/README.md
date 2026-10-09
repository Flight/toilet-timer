# E-Paper Display Module

This module contains all display-related functionality for the e-paper display.

## Structure

```
display_epaper/
├── driver/
│   ├── epd_driver_ssd1681.c      # Hardware-specific driver for the 1.54" 200x200 SSD1681 panel
│   └── epd_driver_ssd1681.h      # (can be replaced for other display models)
├── fonts/
│   ├── font_10x20.c              # Font data (10x20 ASCII + Ukrainian/Cyrillic), generated
│   ├── font_10x20.h
│   └── tools/bdf2font.py         # BDF -> C converter used to generate the font
├── display.c                     # High-level display interface
├── display.h
├── graphics.c                    # Graphics primitives (pixel, text drawing, rotation)
├── graphics.h
├── utf8.c                        # UTF-8 string decoding
└── utf8.h
```

## Architecture

- **driver/**: Hardware-specific e-paper controller driver (SSD1681 on the
  Waveshare ESP32-S3-ePaper-1.54 board)
  - Isolated for easy replacement when changing display models
  - Contains low-level SPI communication, the full-refresh waveform and the
    display commands; also switches the panel supply on and off

- **display.c/h**: High-level display interface
  - Manages the framebuffer (DMA-capable internal RAM)
  - Provides a simple API for application code (plain and centered text)
  - Depends on the driver layer

- **graphics.c/h**: Rendering functions
  - Pixel drawing with `CONFIG_DISPLAY_ROTATION` (0/90/180/270)
  - Text rendering with integer zoom (used for the big "days ago" headline)
  - Hardware-agnostic (works with any driver)

- **utf8.c/h**: UTF-8 text support
  - Decodes UTF-8 strings for international character support
  - Used by the graphics layer for text rendering

- **fonts/**: Font data
  - 10x20 pixel glyphs from the public domain X11 Misc-Fixed family
  - ASCII plus the Ukrainian alphabet
  - Regenerate with `python3 tools/bdf2font.py 10x20.bdf font_10x20.c`
    (the BDF comes from the xorg `font-misc-misc` package)

## Replacing the Display Driver

To use a different e-paper display model:

1. Create a new driver file (e.g., `epd_driver_newmodel.c/h`) in the `driver/` folder
2. Implement the same interface as defined in the current driver header
3. Update `display.c` to include the new driver header
4. Adjust the pin and size defaults in `main/Kconfig.projbuild`

The rest of the code (graphics, display interface, utf8) should work without modification.
