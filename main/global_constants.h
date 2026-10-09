#ifndef GLOBAL_CONSTANTS_H
#define GLOBAL_CONSTANTS_H

#define FLASH_BASE_PATH "/flash/"
#define VERSION_FILE_PATH FLASH_BASE_PATH "version.txt"

#define LOG_BUFFER_SIZE 20480

/* Font Dimensions (10x20 font) */
#define FONT_CHAR_WIDTH 10
#define FONT_CHAR_HEIGHT 20
#define FONT_CHAR_SPACING 11 /* Character advance (10 + 1 pixel gap) */
#define FONT_LINE_SPACING 2  /* Extra pixels between text lines */

/* Screen layout */
#define HEADLINE_TEXT_SCALE 2 /* Integer zoom of the "days ago" headline */
#define SCREEN_SECTION_GAP 12 /* Pixels between the headline and the date/time block */

/* Daily display refresh */
#define DAILY_WAKEUP_HOUR 1                 /* Local hour of the nightly wake-up (1:00 AM) */
#define DAILY_WAKEUP_FALLBACK_DELAY_HOURS 2 /* ESP32 timer fallback fires this many hours after the RTC alarm */

#endif // GLOBAL_CONSTANTS_H
