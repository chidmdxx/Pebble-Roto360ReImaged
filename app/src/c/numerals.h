#pragma once
#include <pebble.h>

// Loads the digit atlas resource. Returns false if it is missing or malformed.
bool numerals_load(void);
void numerals_unload(void);

// Draws a string of digits centred on `center_q4` (1/16 pixel units), rotated
// clockwise by `angle` so the top of the text faces outward on a dial. The
// glyphs are `digit_height` pixels tall and antialiased against whatever is
// already in the frame buffer. Only colour (8-bit) frame buffers are supported.
void numerals_draw(GBitmap *fb, const char *digits, GPoint center_q4, int32_t angle,
                   int digit_height, GColor color);
