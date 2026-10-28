#include "checks.h"
#include "../disc2-video/t21_sync.c"

// The modes the Marty 2 measured: the census has grown since, and the new modes have no hardware result
#define MEASURED_MODES 39

#define SHOWN_MODES 8

static char s_names[SHOWN_MODES][SCREEN_SAFE_COLUMNS + 1];

static int hsyncs_ok(const sync_t* mode)
{
    for (int i = 0; i < PERIODS; i++)
    {
        if (mode->hsyncs[i] != 262)
            return 0;
    }

    return 1;
}

static int in_window(uint32_t value, uint32_t minimum, uint32_t maximum)
{
    return value >= minimum && value <= maximum;
}

static int field_ok(const sync_t* mode)
{
    return mode->status == 0 && in_window(mean(mode->frame), 16670, 16695) && in_window(mean(mode->vsync), 183, 199) &&
        in_window(line_ns(mode), 63480, 63640) && hsyncs_ok(mode);
}

// The first thing that differs in a mode, named after it
static void check_mode(int index, const sync_t* mode, int shown)
{
    char* name = s_names[shown];
    const char* mode_name = g_video_modes[index].name;

    if (mode->status != 0)
    {
        fmt_format(name, SCREEN_SAFE_COLUMNS + 1, "%s: SYNC TIMED OUT", mode_name);
        check_hex(name, mode->status, 0);
    }
    else if (!in_window(mean(mode->frame), 16670, 16695))
    {
        fmt_format(name, SCREEN_SAFE_COLUMNS + 1, "%s: FIELD, US", mode_name);
        check_range(name, mean(mode->frame), 16670, 16695);
    }
    else if (!in_window(mean(mode->vsync), 183, 199))
    {
        fmt_format(name, SCREEN_SAFE_COLUMNS + 1, "%s: VSYNC WIDTH, US", mode_name);
        check_range(name, mean(mode->vsync), 183, 199);
    }
    else if (!in_window(line_ns(mode), 63480, 63640))
    {
        fmt_format(name, SCREEN_SAFE_COLUMNS + 1, "%s: LINE, NS", mode_name);
        check_range(name, line_ns(mode), 63480, 63640);
    }
    else
    {
        fmt_format(name, SCREEN_SAFE_COLUMNS + 1, "%s: HSYNCS IN A FIELD", mode_name);
        check_true(name, 0);
    }
}

static void evaluate_field(const void* data, uint32_t steps)
{
    const sync_census_t* result = (const sync_census_t*)data;
    int count = result->mode_count < MEASURED_MODES ? result->mode_count : MEASURED_MODES;
    uint32_t matching = 0;
    int shown = 0;
    (void)steps;

    for (int i = 0; i < count; i++)
        matching += (uint32_t)field_ok(&result->modes[i]);

    check_value("MODES WITH THE NTSC FIELD", matching, MEASURED_MODES);

    for (int i = 0; i < count && shown < SHOWN_MODES; i++)
    {
        if (!field_ok(&result->modes[i]))
            check_mode(i, &result->modes[i], shown++);
    }
}

static uint32_t high_permille(const sync_t* mode)
{
    uint32_t samples = mode->samples;
    uint32_t high = mode->high_samples;

    while (samples > 4000000u)
    {
        samples >>= 1;
        high >>= 1;
    }

    return samples != 0 ? high * 1000u / samples : 0;
}

static void evaluate_hsync(const void* data, uint32_t steps)
{
    const sync_census_t* result = (const sync_census_t*)data;
    int count = result->mode_count < MEASURED_MODES ? result->mode_count : MEASURED_MODES;
    uint32_t matching = 0;
    (void)steps;

    for (int i = 0; i < count; i++)
        matching += (uint32_t)in_window(high_permille(&result->modes[i]), 900, 950);

    check_value("MODES WITH BIT 1 LOW IN HSYNC", matching, MEASURED_MODES);

    for (int i = 0, shown = 0; i < count && shown < SHOWN_MODES; i++)
    {
        uint32_t share = high_permille(&result->modes[i]);

        if (in_window(share, 900, 950))
            continue;

        fmt_format(s_names[shown], SCREEN_SAFE_COLUMNS + 1, "%s: BIT 1 HIGH, PER MILLE", g_video_modes[i].name);
        check_range(s_names[shown++], share, 900, 950);
    }
}

static const test_t k_test = CHECK_TEST(g_test_sync);

const check_group_t g_check_field = { "NTSC FIELD, ALL MODES (MARTY)", &k_test, 1, check_marty, evaluate_field };
const check_group_t g_check_hsync = { "FDA0H HSYNC BIT (MARTY)", &k_test, 1, check_marty, evaluate_hsync };
