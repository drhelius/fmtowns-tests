#include <stdint.h>
#include "towns.h"
#include "video.h"

#define TEST_COUNT 8
#define ITERATIONS 1024
#define UNROLL 16

extern void run_tests(void);
extern volatile uint32_t g_results[TEST_COUNT];

static void paint_results(void);
static void paint_debug_results(void);
static void print_clocks(uint32_t hundredths);

static const char* k_test_names[TEST_COUNT] =
{
    "LOOP BASE",
    "NOP",
    "MOV R32,[RAM]",
    "MOV [RAM],R32",
    "MOV R16,[RAM]",
    "MOV R32,[RAM+1]",
    "MOV [VRAM],R8",
    "MOV R8,[VRAM]"
};

// Intel 80386 table clocks (zero wait states), x100. Zero where the table doesn't apply
static const uint32_t k_table_clocks[TEST_COUNT] =
{
    0, 300, 400, 200, 400, 0, 200, 400
};

void main(void)
{
    run_tests();
    video_init();
    paint_results();
    paint_debug_results();
}

static void paint_results(void)
{
    uint32_t base = g_results[0];

    video_color(COLOR_LIGHT_YELLOW);
    video_print("FM TOWNS TESTS - TIMING\n\n");
    video_color(COLOR_GRAY);
    video_print("TEST              TICKS   CLK/INS   386 TABLE\n");

    for (int t = 0; t < TEST_COUNT; t++)
    {
        uint32_t ticks = g_results[t];
        uint32_t net = (t > 0 && ticks > base) ? ticks - base : 0;

        // 16 MHz clocks per instruction x100: ticks * (16000000 / 307200) * 100 / (ITERATIONS * UNROLL)
        // 65535 ticks at most, so the product fits in 32 bits
        uint32_t hundredths = net * 62500u / (12u * ITERATIONS * UNROLL);

        video_color(COLOR_LIGHT_YELLOW);
        video_locate(0, 3 + t);
        video_print(k_test_names[t]);

        video_color(COLOR_BRIGHT_WHITE);
        video_locate(18, 3 + t);
        video_print_dec(ticks);

        if (t > 0)
        {
            video_locate(26, 3 + t);
            print_clocks(hundredths);

            if (k_table_clocks[t] != 0)
            {
                video_color(COLOR_LIGHT_CYAN);
                video_locate(36, 3 + t);
                print_clocks(k_table_clocks[t]);
            }
        }
    }

    video_color(COLOR_GRAY);
    video_locate(0, 3 + TEST_COUNT + 1);
    video_print("CLK/INS ASSUMES A 16 MHZ CPU, LOOP BASE SUBTRACTED");
}

static void paint_debug_results(void)
{
    video_color(COLOR_BRIGHT_WHITE);
    video_locate(0, 3 + TEST_COUNT + 3);

    for (int i = 0; i < TEST_COUNT; i++)
    {
        video_print_hex(g_results[i], 8);
        video_print(" ");
    }
}

static void print_clocks(uint32_t hundredths)
{
    video_print_dec(hundredths / 100);
    video_print(".");

    if (hundredths % 100 < 10)
        video_print("0");

    video_print_dec(hundredths % 100);
}
