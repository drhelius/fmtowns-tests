#include "tests.h"

// 2.9 Sprite transfer timing (docs/12, HT-9), the Sprite Probe's method with interrupts off: BUSY (044Ch bit 1) from
// rise to fall with N entries, first index 1024 - N, in Cameltry's 15 kHz mode. Then what the probe left open: small
// and full N with a least-squares line, the mode effect, when PAGE (bit 0) flips, CPU accesses during a transfer, the
// 10F/20F speed modes, and three of the probe's cases so the numbers line up with its photos. The mode cases also swap
// the display windows and zoom between the two 31 kHz modes, turn the layers off, and add more 15, 24 and 31 kHz
// modes; the CPU cases write and read both memories, and plain RAM as a control

#define SPRITE_ENTRIES 1024
#define PATTERN_OPAQUE 0x84
#define PATTERN_DIRECT 0x88
#define COLOR_TABLE 0x100

#define SWEEP 9
#define PAIR 2
#define TRANSFERS 8
#define MODE_CASES 17
#define OWN_OUTPUT 0xFF
#define OWN_ZOOM -1
#define PROBE_CASES 3
#define CONTENTION 6
#define SPEEDS 2
#define SCAN_CHANGES 24
#define SCAN_ROWS 14
#define CHUNK_POLLS 2000u
#define CONTEND_DWORDS 32
#define CONTEND_CHUNKS 1000000u
#define VS_BUSY_POLLS 100000u
#define NO_TIME 0xFFFF

#define STEP_SWEEP 0x01
#define STEP_MODES 0x02
#define STEP_PAGE 0x04
#define STEP_CONTENTION 0x08
#define STEP_SPEED 0x10
#define STEP_PROBE 0x20

#define CASE_OPAQUE 0
#define CASE_HIDDEN 1
#define CASE_DIRECT 2

typedef struct
{
    const char* label;
    const char* mode;
    uint8_t output0;
    uint8_t fda0;
    const char* window;
    int32_t zoom;
} mode_case_t;

typedef struct
{
    uint32_t sweep[SWEEP][TRANSFERS];
    uint32_t modes[MODE_CASES][PAIR][TRANSFERS];
    uint32_t probe[PROBE_CASES][PAIR][TRANSFERS];
    uint32_t contention[CONTENTION][TRANSFERS];
    uint32_t speed[SPEEDS][PAIR][TRANSFERS];
    uint16_t vs_busy[TRANSFERS];
    uint8_t speed_count;
    uint8_t reserved[3];
    uint32_t scan[SCAN_CHANGES];
    uint32_t scan_elapsed;
    uint32_t scan_done;
} sprite_result_t;

uint32_t sprite_scan(uint32_t max_polls, uint32_t* changes, uint32_t capacity);
uint32_t sprite_contend(uint32_t kind, uint32_t address, uint32_t dwords, uint32_t max_chunks);

static void run(void* data, uint32_t steps);
static int pages(const void* data, uint32_t steps);
static void page(const void* data, uint32_t steps, int number);
static void summary(const void* data, uint32_t steps, char* line, uint8_t* color);
static void set_mode(const mode_case_t* entry);
static void fill_sprites(int kind);
static void start_sprites(int n);
static void measure(int n, uint32_t* times);
static void measure_vs_busy(sprite_result_t* result);
static void scan_page(sprite_result_t* result);
static void measure_contention(int kind, uint32_t* times);
static uint32_t total(const uint32_t* times);
static uint32_t divide(uint64_t value, uint32_t divisor);
static void tenths(char* text, int32_t ns);
static int fit(const sprite_result_t* result, int32_t* per_ns, int32_t* clear_ns);
static void pair_line(const uint32_t pair[PAIR][TRANSFERS], int32_t* per_ns, int32_t* clear_ns);
static void draw_pair(int row, const char* label, const uint32_t pair[PAIR][TRANSFERS]);

static const int k_sweep_n[SWEEP] = { 1, 2, 16, 64, 128, 256, 384, 512, 1024 };
static const int k_pair_n[PAIR] = { 128, 384 };

static const mode_case_t k_mode_cases[MODE_CASES] =
{
    { "15K", "CAM", 0x1F, 0x0F, 0, OWN_ZOOM },
    { "15K 20CH", "CAMC", 0x1F, 0x0F, 0, OWN_ZOOM },
    { "31K LS", "LS", 0x1F, 0x0F, 0, OWN_ZOOM },
    { "31K P20", "P20", 0x1F, 0x0F, 0, OWN_ZOOM },
    { "L0 OFF", "CAM", 0x1C, 0x0F, 0, OWN_ZOOM },
    { "OUTPUT OFF", "CAM", 0x1F, 0x00, 0, OWN_ZOOM },
    { "LS ZOOM 1X", "LS", 0x1F, 0x0F, 0, 0x0000 },
    { "LS P20 WIN", "LS", 0x1F, 0x0F, "P20", OWN_ZOOM },
    { "P20 LS WIN", "P20", 0x1F, 0x0F, "LS", OWN_ZOOM },
    { "LS OUT OFF", "LS", 0x1F, 0x00, 0, OWN_ZOOM },
    { "31K S31", "S31", OWN_OUTPUT, 0x0F, 0, OWN_ZOOM },
    { "24K P2", "P2", OWN_OUTPUT, 0x0F, 0, OWN_ZOOM },
    { "15K P18", "P18", 0x1F, 0x0F, 0, OWN_ZOOM },
    { "24K P6", "P6", OWN_OUTPUT, 0x0F, 0, OWN_ZOOM },
    { "24K P8", "P8", OWN_OUTPUT, 0x0F, 0, OWN_ZOOM },
    { "15K P14", "P14", OWN_OUTPUT, 0x0F, 0, OWN_ZOOM },
    { "15K P3N", "P3N", OWN_OUTPUT, 0x0F, 0, OWN_ZOOM }
};

static const char* const k_probe_names[PROBE_CASES] = { "OPAQUE 256", "HIDDEN", "32K OPAQUE" };
static const char* const k_contention_names[CONTENTION] = { "IDLE", "STOSD VRAM", "LODSD SPRITES", "LODSD VRAM",
    "STOSD SPRITES", "STOSD MAIN RAM" };

static const test_step_t k_steps[] =
{
    { "N SWEEP", 0 },
    { "MODE EFFECT", 0 },
    { "VS>BUSY AND PAGE", 0 },
    { "CPU CONTENTION", 0 },
    { "SPEED MODES", 0 },
    { "PROBE CASES", 0 },
    { 0, 0 }
};

const test_t g_test_sprite =
{
    "2.9", "SPRITE TRANSFER TIMING", 0, TEST_RUN_ALL, 3, sizeof(sprite_result_t), k_steps, run, pages, page, summary
};

static void run(void* data, uint32_t steps)
{
    sprite_result_t* result = (sprite_result_t*)data;
    volatile uint32_t* vram = (volatile uint32_t*)g_machine.vram;

    for (uint32_t i = 0; i < 0x80000 / 4; i++)
        vram[i] = 0;

    fill_sprites(CASE_OPAQUE);
    set_mode(&k_mode_cases[0]);

    if ((steps & STEP_SWEEP) != 0)
    {
        for (int i = 0; i < SWEEP; i++)
        {
            shell_step("N %u", k_sweep_n[i]);
            measure(k_sweep_n[i], result->sweep[i]);
        }
    }

    if ((steps & STEP_PAGE) != 0)
    {
        shell_step("VS>BUSY AND PAGE");
        measure_vs_busy(result);
        scan_page(result);
    }

    if ((steps & STEP_CONTENTION) != 0)
    {
        for (int kind = 0; kind < CONTENTION; kind++)
        {
            shell_step("%s", k_contention_names[kind]);
            measure_contention(kind, result->contention[kind]);
        }
    }

    if ((steps & STEP_SPEED) != 0 && (g_boot.port_05e2 != 0xFF || g_boot.port_05e6 != 0xFF))
    {
        static const uint8_t k_speeds[SPEEDS][2] = { { 0x83, 0x86 }, { 0x00, 0x03 } };

        result->speed_count = SPEEDS;

        for (int speed = 0; speed < SPEEDS; speed++)
        {
            shell_step("SPEED %02X/%02X", k_speeds[speed][0], k_speeds[speed][1]);
            out8(0x05E2, k_speeds[speed][0]);
            out8(0x05E6, k_speeds[speed][1]);

            for (int k = 0; k < PAIR; k++)
                measure(k_pair_n[k], result->speed[speed][k]);
        }

        out8(0x05E2, g_boot.port_05e2);
        out8(0x05E6, g_boot.port_05e6);
    }

    if ((steps & STEP_PROBE) != 0)
    {
        for (int kind = 0; kind < PROBE_CASES; kind++)
        {
            shell_step("%s", k_probe_names[kind]);
            fill_sprites(kind);

            for (int k = 0; k < PAIR; k++)
                measure(k_pair_n[k], result->probe[kind][k]);
        }

        fill_sprites(CASE_OPAQUE);
    }

    if ((steps & STEP_MODES) != 0)
    {
        for (int i = 0; i < MODE_CASES; i++)
        {
            shell_step("%s", k_mode_cases[i].label);
            set_mode(&k_mode_cases[i]);

            for (int k = 0; k < PAIR; k++)
                measure(k_pair_n[k], result->modes[i][k]);
        }
    }

    video_sprite_reg(1, 0x00);
}

// A window mode lends its display windows (HDS0 to VDE1)
static void set_mode(const mode_case_t* entry)
{
    const video_mode_t* mode = video_find(entry->mode);

    video_program(mode);

    if (entry->window != 0)
    {
        const video_mode_t* window = video_find(entry->window);

        for (int reg = CRTC_HDS0; reg <= CRTC_VDE0 + 2; reg++)
            video_crtc((uint8_t)reg, video_register(window, reg));
    }

    if (entry->zoom != OWN_ZOOM)
        video_crtc(CRTC_ZOOM, (uint16_t)entry->zoom);

    out8(OUTPUT_ADDRESS, 0x00);
    out8(OUTPUT_DATA, entry->output0 == OWN_OUTPUT ? mode->output[0] : entry->output0);
    out8(VIDEO_STATUS, entry->fda0);
    tb_delay(50000);
}

// The probe's layout: a 16-wide grid, color table 256 in 16 greys, a 16-color pattern with every pixel opaque and a
// 32,768-color one; written with the engine stopped
static void fill_sprites(int kind)
{
    volatile uint16_t* ram = (volatile uint16_t*)g_machine.sprite_ram;
    uint32_t time;

    video_sprite_reg(1, 0x00);
    video_sprite_wait(0, &time);

    for (int i = 0; i < 16; i++)
        ram[COLOR_TABLE * 16 + i] = (uint16_t)(i * 2 * 0x0421);

    for (int i = 0; i < 64; i++)
        ram[PATTERN_OPAQUE * 64 + i] = 0xFFFF;

    for (int i = 0; i < 256; i++)
        ram[PATTERN_DIRECT * 64 + i] = 0x7FFF;

    for (int i = 0; i < SPRITE_ENTRIES; i++)
    {
        volatile uint16_t* entry = ram + i * 4;
        entry[0] = (uint16_t)(16 * (i % 16));
        entry[1] = (uint16_t)(16 + 16 * ((i / 16) % 13));
        entry[2] = kind == CASE_DIRECT ? PATTERN_DIRECT : PATTERN_OPAQUE;
        entry[3] = kind == CASE_DIRECT ? 0x0000 : (uint16_t)(0x8000 | COLOR_TABLE | (kind == CASE_HIDDEN ? 0x2000 : 0));
    }
}

static void start_sprites(int n)
{
    uint16_t first = (uint16_t)((SPRITE_ENTRIES - n) & 0x3FF);
    video_sprite_reg(0, (uint8_t)first);
    video_sprite_reg(1, (uint8_t)(0x80 | (first >> 8)));
}

// One transfer to take the new first index, then eight from BUSY rise to fall
static void measure(int n, uint32_t* times)
{
    uint32_t start;
    uint32_t end;

    start_sprites(n);

    if (!video_sprite_wait(0, &start) || !video_sprite_wait(VIDEO_SPRITE_BUSY, &start) || !video_sprite_wait(0, &end))
        return;

    for (int k = 0; k < TRANSFERS; k++)
    {
        if (!video_sprite_wait(0, &start) || !video_sprite_wait(VIDEO_SPRITE_BUSY, &start) ||
            !video_sprite_wait(0, &end))
            return;

        times[k] = end - start;
    }
}

static void measure_vs_busy(sprite_result_t* result)
{
    uint32_t start;
    uint32_t end;

    start_sprites(16);

    if (!video_sprite_wait(VIDEO_SPRITE_BUSY, &start) || !video_sprite_wait(0, &end))
        return;

    for (int k = 0; k < TRANSFERS; k++)
    {
        result->vs_busy[k] = NO_TIME;

        if (!video_vsync_rise(&start) ||
            video_wait_port(VIDEO_SPRITE_STATUS, VIDEO_SPRITE_BUSY, VIDEO_SPRITE_BUSY, VS_BUSY_POLLS) == 0)
            continue;

        end = tb_now();
        result->vs_busy[k] = (uint16_t)(end - start);
        video_sprite_wait(0, &end);
    }
}

// From the display, after a transfer, 2.3 frames of VSYNC, BUSY and PAGE
static void scan_page(sprite_result_t* result)
{
    uint32_t changes[8];
    uint32_t time;
    uint32_t start = tb_now();
    uint32_t done = sprite_scan(CHUNK_POLLS, changes, 8);
    uint32_t elapsed = tb_now() - start;
    uint32_t polls = elapsed != 0 ? video_frame_us(video_find("CAM")) * 23 / 10 * done / elapsed : 0;

    for (int i = 0; i < SCAN_CHANGES; i++)
        result->scan[i] = VIDEO_NO_CHANGE;

    if (polls == 0 || !video_sprite_wait(VIDEO_SPRITE_BUSY, &time) || !video_sprite_wait(0, &time))
        return;

    start = tb_now();
    result->scan_done = sprite_scan(polls, result->scan, SCAN_CHANGES);
    result->scan_elapsed = tb_now() - start;
}

static void measure_contention(int kind, uint32_t* times)
{
    uint32_t start;
    uint32_t end;

    if (kind == 0)
    {
        measure(384, times);
        return;
    }

    start_sprites(384);

    if (!video_sprite_wait(0, &start) || !video_sprite_wait(VIDEO_SPRITE_BUSY, &start) || !video_sprite_wait(0, &end))
        return;

    for (int k = 0; k < TRANSFERS; k++)
    {
        if (!video_sprite_wait(0, &start) || !video_sprite_wait(VIDEO_SPRITE_BUSY, &start))
            return;

        static uint32_t s_main_ram[CONTEND_DWORDS];
        int vram = kind == 1 || kind == 3;
        uint32_t address = kind == 5 ? (uint32_t)s_main_ram : vram ? g_machine.vram : g_machine.sprite_ram + 0x4000;
        int write = kind == 1 || kind == 4 || kind == 5;
        uint32_t chunks = sprite_contend(write ? 0 : 1, address, CONTEND_DWORDS, CONTEND_CHUNKS);
        end = tb_now();

        if (chunks != 0)
            times[k] = end - start;
    }
}

// The sum of the eight, 0 if any timed out
static uint32_t total(const uint32_t* times)
{
    uint32_t sum = 0;

    for (int k = 0; k < TRANSFERS; k++)
    {
        if (times[k] == 0)
            return 0;

        sum += times[k];
    }

    return sum;
}

// Shift and subtract, as there's no libgcc for a 64-bit division
static uint32_t divide(uint64_t value, uint32_t divisor)
{
    uint64_t quotient = 0;
    uint64_t remainder = 0;

    for (int bit = 63; bit >= 0; bit--)
    {
        remainder = (remainder << 1) | ((value >> bit) & 1);

        if (remainder >= divisor)
        {
            remainder -= divisor;
            quotient |= (uint64_t)1 << bit;
        }
    }

    return (uint32_t)quotient;
}

// Least squares over the sweep means in ns, the slope from N centered on its mean (scaled by the count)
static int fit(const sprite_result_t* result, int32_t* per_ns, int32_t* clear_ns)
{
    int32_t sum_n = 0;
    int32_t sum_t = 0;
    int count = 0;

    for (int i = 0; i < SWEEP; i++)
    {
        uint32_t sum = total(result->sweep[i]);

        if (sum == 0)
            continue;

        sum_n += k_sweep_n[i];
        sum_t += (int32_t)(sum * (1000u / TRANSFERS));
        count++;
    }

    if (count < 3)
        return 0;

    int64_t numerator = 0;
    int64_t denominator = 0;

    for (int i = 0; i < SWEEP; i++)
    {
        uint32_t sum = total(result->sweep[i]);

        if (sum == 0)
            continue;

        int32_t centered = count * k_sweep_n[i] - sum_n;
        numerator += (int64_t)centered * (int64_t)(sum * (1000u / TRANSFERS));
        denominator += (int64_t)centered * k_sweep_n[i];
    }

    if (numerator <= 0 || denominator <= 0 || denominator > 0x7FFFFFFF)
        return 0;

    *per_ns = (int32_t)divide((uint64_t)numerator, (uint32_t)denominator);
    *clear_ns = (sum_t - *per_ns * sum_n) / count;
    return 1;
}

static void tenths(char* text, int32_t ns)
{
    int32_t value = (ns < 0 ? -ns : ns) / 100;
    fmt_format(text, 12, "%s%d.%d", ns < 0 ? "-" : "", value / 10, value % 10);
}

static void pair_line(const uint32_t pair[PAIR][TRANSFERS], int32_t* per_ns, int32_t* clear_ns)
{
    uint32_t low = total(pair[0]);
    uint32_t high = total(pair[1]);

    *per_ns = 0;
    *clear_ns = 0;

    if (low == 0 || high == 0)
        return;

    *per_ns = (int32_t)((high - low) * 1000u / TRANSFERS / (uint32_t)(k_pair_n[1] - k_pair_n[0]));
    *clear_ns = (int32_t)(low * 1000u / TRANSFERS) - *per_ns * k_pair_n[0];
}

static void draw_pair(int row, const char* label, const uint32_t pair[PAIR][TRANSFERS])
{
    int32_t per;
    int32_t clear;
    char text[12];

    pair_line(pair, &per, &clear);
    tenths(text, clear);
    shell_line(row, COLOR_WHITE, "%-11s %5u %5u %5d %6s", label, total(pair[0]) / TRANSFERS,
        total(pair[1]) / TRANSFERS, per, text);
}

static int pages(const void* data, uint32_t steps)
{
    (void)data;
    (void)steps;
    return 4;
}

static void page(const void* data, uint32_t steps, int number)
{
    const sprite_result_t* result = (const sprite_result_t*)data;
    int row = SHELL_BODY_TOP;

    if (number == 0)
    {
        int32_t per;
        int32_t clear;

        shell_line(row++, COLOR_CYAN, "N SWEEP IN CAMELTRY'S MODE, US");

        if ((steps & STEP_SWEEP) == 0)
        {
            shell_line(row++, COLOR_GRAY, "NOT RUN");
            return;
        }

        int fitted = fit(result, &per, &clear);

        shell_line(row++, COLOR_GRAY, "   N   MEAN SPREAD  RESIDUAL");

        for (int i = 0; i < SWEEP; i++)
        {
            uint32_t sum = total(result->sweep[i]);
            uint32_t low = 0xFFFFFFFFu;
            uint32_t high = 0;

            for (int k = 0; k < TRANSFERS; k++)
            {
                low = result->sweep[i][k] < low ? result->sweep[i][k] : low;
                high = result->sweep[i][k] > high ? result->sweep[i][k] : high;
            }

            if (sum == 0)
            {
                shell_line(row++, COLOR_YELLOW, "%4u TIMEOUT", k_sweep_n[i]);
                continue;
            }

            char text[12];
            tenths(text, fitted ? (int32_t)(sum * (1000u / TRANSFERS)) - clear - per * k_sweep_n[i] : 0);
            shell_line(row++, COLOR_WHITE, "%4u %6u %4u %9s", k_sweep_n[i], sum / TRANSFERS, high - low, text);
        }

        row++;

        if (fitted)
        {
            char text[12];
            tenths(text, clear);
            shell_line(row++, COLOR_WHITE, "LINE %u.%03u US PER ENTRY", per / 1000, per % 1000);
            shell_line(row++, COLOR_WHITE, "     + %s US", text);
        }

        uint32_t low = total(result->sweep[4]);
        uint32_t high = total(result->sweep[6]);

        if (low != 0 && high != 0)
        {
            uint32_t probe = (high - low) * 1000u / TRANSFERS / 256;
            shell_line(row++, COLOR_WHITE, "PROBE PER (384-128)/256 %u.%03u", probe / 1000, probe % 1000);
        }

        return;
    }

    if (number == 1)
    {
        shell_line(row++, COLOR_CYAN, "MODE EFFECT, US");

        if ((steps & STEP_MODES) == 0)
        {
            shell_line(row++, COLOR_GRAY, "NOT RUN");
            return;
        }

        shell_line(row++, COLOR_GRAY, "CASE         T128  T384 PERNS CLEAR");

        for (int i = 0; i < MODE_CASES; i++)
            draw_pair(row++, k_mode_cases[i].label, result->modes[i]);

        return;
    }

    if (number == 2)
    {
        shell_line(row++, COLOR_CYAN, "VS>BUSY AND PAGE, N 16");

        if ((steps & STEP_PAGE) == 0)
        {
            shell_line(row++, COLOR_GRAY, "NOT RUN");
            return;
        }

        uint16_t low = NO_TIME;
        uint16_t high = 0;

        for (int k = 0; k < TRANSFERS; k++)
        {
            if (result->vs_busy[k] == NO_TIME)
                continue;

            low = result->vs_busy[k] < low ? result->vs_busy[k] : low;
            high = result->vs_busy[k] > high ? result->vs_busy[k] : high;
        }

        if (low == NO_TIME)
            shell_line(row++, COLOR_YELLOW, "VS>BUSY: NO BUSY AFTER VSYNC");
        else
            shell_line(row++, COLOR_WHITE, "VS>BUSY %u-%u US", low, high);

        int32_t origin = -1;

        for (int i = 1; i < SCAN_CHANGES && result->scan[i] != VIDEO_NO_CHANGE; i++)
        {
            if ((result->scan[i] & 0x01) != 0 && (result->scan[i - 1] & 0x01) == 0)
            {
                origin = (int32_t)(result->scan[i] >> 8);
                break;
            }
        }

        shell_line(row++, COLOR_GRAY, "US FROM VSYNC  VSYNC BUSY PAGE");

        for (int i = 0; i < SCAN_CHANGES && i < SCAN_ROWS && result->scan[i] != VIDEO_NO_CHANGE; i++)
        {
            uint32_t state = result->scan[i] & 0xFF;
            int32_t time = video_poll_ns((int32_t)(result->scan[i] >> 8) - (origin >= 0 ? origin : 0),
                result->scan_elapsed, result->scan_done) / 1000;

            shell_line(row++, COLOR_WHITE, "%8d %8u %4u %4u", time, state & 0x01, (state >> 2) & 0x01,
                (state >> 1) & 0x01);
        }

        return;
    }

    shell_line(row++, COLOR_CYAN, "CPU, SPEED AND PROBE CASES, US");
    shell_line(row++, COLOR_GRAY, "CPU DURING N 384");

    for (int kind = 0; kind < CONTENTION; kind++)
    {
        if ((steps & STEP_CONTENTION) == 0)
            break;

        uint32_t sum = total(result->contention[kind]);
        shell_line(row++, COLOR_WHITE, "%-14s %6u", k_contention_names[kind], sum / TRANSFERS);
    }

    shell_line(row++, COLOR_GRAY, "CASE         T128  T384 PERNS CLEAR");

    if ((steps & STEP_SPEED) != 0)
    {
        if (result->speed_count == 0)
            shell_line(row++, COLOR_GRAY, "NO SPEED REGISTERS");

        for (int speed = 0; speed < result->speed_count; speed++)
            draw_pair(row++, speed == 0 ? "SPEED 83/86" : "SPEED 00/03", result->speed[speed]);
    }

    if ((steps & STEP_PROBE) != 0)
    {
        for (int kind = 0; kind < PROBE_CASES; kind++)
            draw_pair(row++, k_probe_names[kind], result->probe[kind]);
    }
}

static void summary(const void* data, uint32_t steps, char* line, uint8_t* color)
{
    const sprite_result_t* result = (const sprite_result_t*)data;
    int32_t per;
    int32_t clear;

    char text[12];

    *color = COLOR_WHITE;

    if ((steps & STEP_SWEEP) != 0 && fit(result, &per, &clear))
    {
        tenths(text, clear);
        fmt_format(line, 34, "%u.%03u US PER ENTRY + %s", per / 1000, per % 1000, text);
    }
    else
        fmt_copy(line, "NO SWEEP", 34);
}
