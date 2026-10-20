#include "raster.h"
#include "timebase.h"
#include "towns.h"
#include "video.h"

#define SPIN_PASSES 50000u
#define LINE_SKIP 20
#define LINE_TAKE 100
#define HSYNC_POLLS 2000u
#define LINE_POLLS 1000000u

// The sync line period over 100 lines of the display, the frame from two VSYNC rises, and the spin pass
void raster_setup(raster_t* raster, const video_mode_t* mode)
{
    uint32_t high;
    uint32_t first;
    uint32_t second;

    raster->line_ns = 0;
    raster->sync_ns = 0;
    raster->frame_lines = 0;

    if (video_vsync_rise(&first) && video_vsync_rise(&second))
    {
        uint32_t frame = second - first;

        if (video_wait_bit(VIDEO_VSYNC, 0, LINE_POLLS) != 0 && video_time_hsyncs(LINE_SKIP, LINE_POLLS, &high) != 0)
        {
            uint32_t start = tb_now();

            if (video_time_hsyncs(LINE_TAKE, LINE_POLLS, &high) != 0)
                raster->sync_ns = (tb_now() - start) * 1000u / LINE_TAKE;
        }

        uint32_t book = video_line_ns(mode);
        raster->line_ns = raster->sync_ns * 2 > book * 3 ? book : raster->sync_ns;

        if (raster->line_ns != 0)
            raster->frame_lines = (frame * 1000u + raster->line_ns / 2) / raster->line_ns;
    }

    uint32_t start = tb_now();
    video_spin(SPIN_PASSES);
    raster->pass_ps = (tb_now() - start) * (1000000u / SPIN_PASSES);
}

// One frame: the events in line order from the next VSYNC rise
int raster_frame(raster_t* raster, const raster_event_t* events, int count)
{
    uint32_t start;
    int line = -1;

    if (!video_vsync_rise(&start))
        return 0;

    for (int i = 0; i < count; i++)
    {
        const raster_event_t* event = &events[i];
        uint32_t extra = 0;

        if (event->line != 0 && event->line != line)
        {
            uint32_t at = event->line * raster->line_ns;
            uint32_t index = raster->sync_ns != 0 ? at / raster->sync_ns : 0;
            int32_t offset = (int32_t)(index * raster->sync_ns) - (int32_t)(raster->sync_ns / 2);
            uint32_t target = start + (uint32_t)(offset / 1000);

            extra = (at - index * raster->sync_ns) * 1000u / (raster->pass_ps + 1);

            while ((int32_t)(tb_now() - target) < 0)
                ;

            video_wait_bit(VIDEO_HSYNC, 0, HSYNC_POLLS);
            video_wait_bit(VIDEO_HSYNC, VIDEO_HSYNC, HSYNC_POLLS);
        }

        line = event->line;
        video_spin(event->delay + extra);
        raster_apply(event);
        raster->times[i] = tb_now() - start;
    }

    return 1;
}

void raster_apply(const raster_event_t* event)
{
    switch (event->kind)
    {
        case RASTER_CRTC:
            video_crtc(event->reg, event->value);
            break;
        case RASTER_CRTC_LOW:
            out8(CRTC_ADDRESS, event->reg);
            out8(CRTC_DATA, (uint8_t)event->value);
            break;
        case RASTER_CRTC_HIGH:
            out8(CRTC_ADDRESS, event->reg);
            out8(CRTC_DATA + 1, (uint8_t)event->value);
            break;
        case RASTER_PORT:
            out8((uint16_t)event->address, (uint8_t)event->value);
            break;
        case RASTER_PALETTE:
            out8(0xFD90, event->reg);
            out8(0xFD92, (uint8_t)event->address);
            out8(0xFD94, (uint8_t)(event->address >> 16));
            out8(0xFD96, (uint8_t)(event->address >> 8));
            break;
        case RASTER_OUTPUT:
            out8(OUTPUT_ADDRESS, event->reg);
            out8(OUTPUT_DATA, (uint8_t)event->value);
            break;
        case RASTER_MMIO:
            MMIO8(event->address) = (uint8_t)event->value;
            break;
        case RASTER_SPRITE:
            out8(0x0450, event->reg);
            out8(0x0452, (uint8_t)event->value);
            break;
    }
}

uint32_t raster_delay_ns(const raster_t* raster, uint32_t passes)
{
    return passes * raster->pass_ps / 1000u;
}
