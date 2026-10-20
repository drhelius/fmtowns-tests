#ifndef TIMEBASE_H
#define TIMEBASE_H

#include <stdint.h>

// Microseconds from the 0026h counter (UX, Marty and later) or PIT counter 0 (307.2 kHz) on every model
// Both are 16 bits wide, so tb_now() must run at least every 65 ms to extend them

#define TB_SOURCE_0026 0
#define TB_SOURCE_PIT 1

void tb_detect(void);
void tb_sync(void);
int tb_source(void);
uint32_t tb_now(void);
uint32_t tb_extend(uint16_t count);
void tb_delay(uint32_t microseconds);
void tb_pit_program(void);
uint16_t tb_pit_read(void);

#endif /* TIMEBASE_H */
