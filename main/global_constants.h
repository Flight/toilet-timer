#ifndef GLOBAL_CONSTANTS_H
#define GLOBAL_CONSTANTS_H

#define FLASH_BASE_PATH "/flash/"
#define VERSION_FILE_PATH FLASH_BASE_PATH "version.txt"

#define LOG_BUFFER_SIZE 20480

/* Font Dimensions (9x15 font, rotated 90 degrees) */
#define FONT_CHAR_WIDTH 15  /* Original height (15) becomes width after rotation */
#define FONT_CHAR_HEIGHT 9  /* Original width (9) becomes height after rotation */
#define FONT_CHAR_SPACING 10 /* Spacing for rotated text (9 + 1 pixel gap) */

#endif // GLOBAL_CONSTANTS_H
