#ifndef FONT_H
#define FONT_H

#include <stdint.h>

#define FONT_BLOCK 0x7F

void font_init(void);
const uint8_t* font_glyph(uint8_t c);

#endif /* FONT_H */
