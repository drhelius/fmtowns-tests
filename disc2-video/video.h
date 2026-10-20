#ifndef VIDEO_H
#define VIDEO_H

#include <stdint.h>

// What disc 2 shares: the Data Book presets and a few more modes, the CRTC, and edges of the FMR status port

#define VIDEO_STATUS 0xFDA0
#define VIDEO_VSYNC 0x01
#define VIDEO_HSYNC 0x02
#define VIDEO_IRQ 0x04
#define VIDEO_VSYNC_CLEAR 0x05CA
#define CRTC_ADDRESS 0x0440
#define CRTC_DATA 0x0442
#define OUTPUT_ADDRESS 0x0448
#define OUTPUT_DATA 0x044A
#define VIDEO_SPRITE_STATUS 0x044C
#define VIDEO_SPRITE_PAGE 0x01
#define VIDEO_SPRITE_BUSY 0x02
#define VIDEO_SPRITE_INDEX 0x0450
#define VIDEO_SPRITE_DATA 0x0452

#define CRTC_HSW1 0x00
#define CRTC_HST 0x04
#define CRTC_VST1 0x05
#define CRTC_VST2 0x06
#define CRTC_VST 0x08
#define CRTC_HDS0 0x09
#define CRTC_HDE0 0x0A
#define CRTC_VDS0 0x0D
#define CRTC_VDE0 0x0E
#define CRTC_FA0 0x11
#define CRTC_HAJ0 0x12
#define CRTC_FO0 0x13
#define CRTC_LO0 0x14
#define CRTC_ZOOM 0x1B
#define CRTC_CR0 0x1C
#define CRTC_CR1 0x1D
#define CRTC_START 0x8000

#define VIDEO_TIMEOUT 0
#define VIDEO_NO_CHANGE 0xFFFFFFFFu

typedef struct
{
    const char* name;
    uint16_t crtc[30];
    uint8_t output[2];
} video_mode_t;

extern const video_mode_t g_video_modes[];
extern const int g_video_mode_count;

void video_crtc(uint8_t reg, uint16_t value);
uint16_t video_register(const video_mode_t* mode, int reg);
void video_program(const video_mode_t* mode);
const video_mode_t* video_find(const char* name);
uint32_t video_dot_khz(const video_mode_t* mode);
uint32_t video_line_ns(const video_mode_t* mode);
uint32_t video_frame_us(const video_mode_t* mode);
uint32_t video_vsync_us(const video_mode_t* mode);
uint32_t video_hsync_ns(const video_mode_t* mode);
int video_wait(uint8_t mask, uint8_t value, uint32_t timeout_us, uint32_t* time);
int video_wait_rise(uint8_t mask, uint32_t timeout_us, uint32_t* time);
int video_wait_fall(uint8_t mask, uint32_t timeout_us, uint32_t* time);
uint32_t video_count_hsyncs(uint32_t max_polls);
uint32_t video_time_hsyncs(uint32_t rises, uint32_t max_polls, uint32_t* high);
uint32_t video_wait_bit(uint32_t mask, uint32_t value, uint32_t max_polls);
uint32_t video_wait_port(uint32_t port, uint32_t mask, uint32_t value, uint32_t max_polls);
void video_clock_loop(uint32_t count);
uint32_t video_scan(uint32_t max_polls, uint32_t* changes, uint32_t capacity, uint32_t mask);
void video_count_edges(uint32_t polls, uint32_t* counts);
void video_spin(uint32_t passes);
int video_vsync_rise(uint32_t* time);
uint32_t video_scan_polls(uint32_t us, uint32_t mask);
int32_t video_poll_ns(int32_t polls, uint32_t elapsed_us, uint32_t done);
void video_sprite_reg(uint8_t reg, uint8_t value);
int video_sprite_wait(uint8_t value, uint32_t* time);
uint32_t video_clock_khz(void);

#endif /* VIDEO_H */
