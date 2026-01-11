# E-Paper Display Module

This module contains all display-related functionality for the e-paper display.

## Structure

```
display_epaper/
├── driver/
│   ├── epd_driver_gdew0102t4.c  # Hardware-specific driver for GDEW0102T4
│   └── epd_driver_gdew0102t4.h  # (can be replaced for other display models)
├── fonts/
│   ├── font.c                    # Font data (5x7 ASCII + Ukrainian/Cyrillic)
│   └── font.h
├── display.c                     # High-level display interface
├── display.h
├── graphics.c                    # Graphics primitives (pixel, text drawing)
├── graphics.h
├── utf8.c                        # UTF-8 string decoding
└── utf8.h
```

## Architecture

- **driver/**: Hardware-specific e-paper controller driver (UC8175 for GDEW0102T4)
  - Isolated for easy replacement when changing display models
  - Contains low-level SPI communication and display commands

- **display.c/h**: High-level display interface
  - Manages framebuffer allocation
  - Provides simple API for application code
  - Depends on driver layer

- **graphics.c/h**: Rendering functions
  - Pixel drawing
  - Text rendering with font support
  - Hardware-agnostic (works with any driver)

- **utf8.c/h**: UTF-8 text support
  - Decodes UTF-8 strings for international character support
  - Used by graphics layer for text rendering

- **fonts/**: Font data
  - Contains bitmap font definitions
  - 5x7 pixel ASCII characters + Ukrainian/Cyrillic glyphs
  - Used by graphics layer for text rendering

## Replacing the Display Driver

To use a different e-paper display model:

1. Create a new driver file (e.g., `epd_driver_newmodel.c/h`) in the `driver/` folder
2. Implement the same interface as defined in the current driver header
3. Update `display.c` to include the new driver header
4. Update `CMakeLists.txt` to reference the new driver file

The rest of the code (graphics, display interface, utf8) should work without modification.
