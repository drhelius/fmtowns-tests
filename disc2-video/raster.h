#ifndef RASTER_H
#define RASTER_H

#include <stdint.h>
#include "video.h"

// Register writes at set lines of every frame, with interrupts off. Line n starts n measured line periods after the
// VSYNC rise, at the HSYNC rise found from half a line before; a delay in spin passes follows it. Where the sync line
// is much longer than the mode's own (the Marty runs 31 kHz modes two CRTC lines to a TV line), lines are the mode's:
// the write goes to the HSYNC rise of the sync line that holds it, then spins the rest. The time of each write after
// the VSYNC rise is kept, in microseconds

#define RASTER_CRTC 0
#define RASTER_CRTC_LOW 1
#define RASTER_CRTC_HIGH 2
#define RASTER_PORT 3
#define RASTER_PALETTE 4
#define RASTER_OUTPUT 5
#define RASTER_MMIO 6
#define RASTER_SPRITE 7
#define RASTER_CAPACITY 8

typedef struct
{
    uint16_t line;
    uint16_t delay;
    uint8_t kind;
    uint8_t reg;
    uint16_t value;
    uint32_t address;
} raster_event_t;

typedef struct
{
    uint32_t line_ns;
    uint32_t sync_ns;
    uint32_t pass_ps;
    uint32_t frame_lines;
    uint32_t times[RASTER_CAPACITY];
} raster_t;

void raster_setup(raster_t* raster, const video_mode_t* mode);
int raster_frame(raster_t* raster, const raster_event_t* events, int count);
void raster_apply(const raster_event_t* event);
uint32_t raster_delay_ns(const raster_t* raster, uint32_t passes);

#endif /* RASTER_H */
