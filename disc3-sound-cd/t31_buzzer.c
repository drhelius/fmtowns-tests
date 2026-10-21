#include "tests.h"

// 3.1 Buzzer and source levels (docs/12, HT-10): recorded from the line output. The buzzer is channel 2 of the first
// PIT (0044h) in mode 3 with count 256, 1,200 Hz at 307.2 kHz or 75 Hz at 19.2 kHz; SOUND is 0060h bit 2, CFF98h
// turns it on when read and off when written. Then the level of each source, and what the CD-DA volume, the 04D5h
// gates and 04ECh bit 6 do to it, the pans, PCM noise for its filter, the FM DAC, a quiet FM tone (a YM2612's
// ladder distortion shows there, a YM3438 has none) and the CD-DA volume steps

#define STEPS 5
#define COUNT 256
#define HALF_COUNT 128
#define CHANGE_MS 500
#define PCM_HALF 16
#define PCM_STEP 3146
#define NOISE_START 0x30
#define NOISE_SIZE 0x5000u
#define DAC_HALF_US 500u
#define DAC_US 2000000u

enum
{
    A_SOUND,
    A_CFF98,
    A_BOTH,
    A_NEITHER,
    A_SOUND_WRITE,
    A_MODE,
    A_CHANGE,
    A_SOURCE,
    A_VOLUME,
    A_GATE_FM,
    A_GATE_PCM,
    A_OUTPUT,
    A_CD_VOLUME
};

enum
{
    SOURCE_BUZZER,
    SOURCE_FM,
    SOURCE_PCM,
    SOURCE_CDDA,
    SOURCE_FM_QUIET,
    SOURCE_FM_LEFT,
    SOURCE_PCM_0F,
    SOURCE_PCM_F0,
    SOURCE_PCM_NOISE,
    SOURCE_FM_DAC
};

typedef struct
{
    uint8_t port_04d5;
    uint8_t port_04ec;
    uint8_t volume_command;
    uint8_t volume_data;
    uint8_t cff98_mapped;
    uint8_t reserved[3];
    uint32_t change_time;
    uint32_t starts[STEPS][REC_MAX_STEPS];
} buzzer_t;

static void run(void* data, uint32_t steps);
static int pages(const void* data, uint32_t steps);
static void page(const void* data, uint32_t steps, int number);
static void summary(const void* data, uint32_t steps, char* line, uint8_t* color);
static void action(int id, int argument, int on);
static void source(int which, int on);
static void noise(void);
static void fm_dac(void);

static const test_step_t k_steps[] =
{
    { "TONE AND ENABLES", 0 },
    { "COUNTER MODES", 0 },
    { "SOURCE LEVELS", 0 },
    { "SOURCES GATED", 0 },
    { "CD-DA VOLUME STEPS", 0 },
    { 0, 0 }
};

static const rec_step_t k_enables[] =
{
    { "SOUND ONLY", 1000, 500, A_SOUND, 0 },
    { "CFF98H READ ONLY", 1000, 500, A_CFF98, 0 },
    { "SOUND AND CFF98H", 1000, 500, A_BOTH, 0 },
    { "NEITHER", 1000, 500, A_NEITHER, 0 },
    { "SOUND, CFF98H WRITE", 1000, 500, A_SOUND_WRITE, 0 }
};

static const rec_step_t k_modes[] =
{
    { "MODE 0 COUNT 256", 1000, 500, A_MODE, 0 },
    { "MODE 2 COUNT 256", 1000, 500, A_MODE, 2 },
    { "MODE 3 COUNT 256", 1000, 500, A_MODE, 3 },
    { "MODE 3 256 TO 128", 1000, 500, A_CHANGE, 3 }
};

static const rec_step_t k_levels[] =
{
    { "BUZZER 1200 HZ", 2000, 500, A_SOURCE, SOURCE_BUZZER },
    { "FM 1 KHZ TL 0", 2000, 500, A_SOURCE, SOURCE_FM },
    { "PCM 1 KHZ SQUARE", 2000, 500, A_SOURCE, SOURCE_PCM },
    { "CD-DA 1 KHZ -6 DB", 3000, 500, A_SOURCE, SOURCE_CDDA },
    { "FM 1 KHZ TL 60", 2000, 500, A_SOURCE, SOURCE_FM_QUIET },
    { "FM PAN 80H", 2000, 500, A_SOURCE, SOURCE_FM_LEFT },
    { "PCM SQUARE PAN 0FH", 2000, 500, A_SOURCE, SOURCE_PCM_0F },
    { "PCM SQUARE PAN F0H", 2000, 500, A_SOURCE, SOURCE_PCM_F0 },
    { "PCM WHITE NOISE", 3000, 500, A_SOURCE, SOURCE_PCM_NOISE },
    { "FM DAC SQUARE 1 KHZ", 2000, 500, A_SOURCE, SOURCE_FM_DAC }
};

// The second MB87078's CD-DA channels: control bits EN 04h, C0 08h, C32 10h, and the 6-bit data
static const uint8_t k_volumes[][2] =
{
    { 0x04, 0x3F }, { 0x04, 0x30 }, { 0x04, 0x20 }, { 0x04, 0x10 }, { 0x04, 0x08 }, { 0x04, 0x00 },
    { 0x0C, 0x00 }, { 0x14, 0x3F }, { 0x00, 0x3F }
};

static const rec_step_t k_cd_volumes[] =
{
    { "CD EN DATA 3FH", 3000, 500, A_CD_VOLUME, 0 },
    { "CD EN DATA 30H", 3000, 500, A_CD_VOLUME, 1 },
    { "CD EN DATA 20H", 3000, 500, A_CD_VOLUME, 2 },
    { "CD EN DATA 10H", 3000, 500, A_CD_VOLUME, 3 },
    { "CD EN DATA 08H", 3000, 500, A_CD_VOLUME, 4 },
    { "CD EN DATA 00H", 3000, 500, A_CD_VOLUME, 5 },
    { "CD EN C0 DATA 00H", 3000, 500, A_CD_VOLUME, 6 },
    { "CD EN C32 DATA 3FH", 3000, 500, A_CD_VOLUME, 7 },
    { "CD EN OFF DATA 3FH", 3000, 500, A_CD_VOLUME, 8 }
};

static const rec_step_t k_gated[] =
{
    { "BUZZER CD VOLUME 0", 2000, 500, A_VOLUME, SOURCE_BUZZER },
    { "BUZZER 04D5H 01H", 2000, 500, A_GATE_FM, SOURCE_BUZZER },
    { "BUZZER 04D5H 02H", 2000, 500, A_GATE_PCM, SOURCE_BUZZER },
    { "BUZZER 04ECH BIT 6 0", 2000, 500, A_OUTPUT, SOURCE_BUZZER },
    { "FM CD VOLUME 0", 2000, 500, A_VOLUME, SOURCE_FM },
    { "FM 04D5H 01H", 2000, 500, A_GATE_FM, SOURCE_FM },
    { "FM 04D5H 02H", 2000, 500, A_GATE_PCM, SOURCE_FM },
    { "FM 04ECH BIT 6 0", 2000, 500, A_OUTPUT, SOURCE_FM },
    { "PCM CD VOLUME 0", 2000, 500, A_VOLUME, SOURCE_PCM },
    { "PCM 04D5H 01H", 2000, 500, A_GATE_FM, SOURCE_PCM },
    { "PCM 04D5H 02H", 2000, 500, A_GATE_PCM, SOURCE_PCM },
    { "PCM 04ECH BIT 6 0", 2000, 500, A_OUTPUT, SOURCE_PCM },
    { "CD-DA CD VOLUME 0", 3000, 500, A_VOLUME, SOURCE_CDDA },
    { "CD-DA 04D5H 01H", 3000, 500, A_GATE_FM, SOURCE_CDDA },
    { "CD-DA 04D5H 02H", 3000, 500, A_GATE_PCM, SOURCE_CDDA },
    { "CD-DA 04ECH BIT 6 0", 3000, 500, A_OUTPUT, SOURCE_CDDA }
};

static const rec_step_t* const k_schedules[STEPS] = { k_enables, k_modes, k_levels, k_gated, k_cd_volumes };
static const int k_counts[STEPS] =
{
    COUNT_OF(k_enables), COUNT_OF(k_modes), COUNT_OF(k_levels), COUNT_OF(k_gated), COUNT_OF(k_cd_volumes)
};

static buzzer_t* s_result;
static snd_saved_t s_saved;

const test_t g_test_buzzer =
{
    "3.1", "BUZZER AND LEVELS", "REC", 0, 1, sizeof(buzzer_t), k_steps, run, pages, page, summary
};

static void run(void* data, uint32_t steps)
{
    buzzer_t* result = (buzzer_t*)data;
    cd_log_t log;

    s_result = result;
    cd_quiet();
    cd_load_toc(&log);
    snd_open(&s_saved);
    result->port_04d5 = s_saved.port_04d5;
    result->port_04ec = s_saved.port_04ec;
    result->volume_command = s_saved.volume_command;
    result->volume_data = s_saved.volume_data;
    result->cff98_mapped = (uint8_t)((g_boot.port_0404 & 0x80) == 0);

    snd_pcm_square(0, PCM_HALF, 0xFE, 0x7F);
    noise();
    snd_fm_tone(1, 5, 1180, 0x00);

    for (int i = 0; i < STEPS; i++)
    {
        if ((steps & (1u << i)) == 0)
            continue;

        shell_step("%s", k_steps[i].name);
        snd_buzzer_pit(3, COUNT);

        if (!rec_play(k_steps[i].name, k_schedules[i], k_counts[i], action, result->starts[i]))
            break;
    }

    snd_close(&s_saved);
    cd_quiet();
}

static void action(int id, int argument, int on)
{
    switch (id)
    {
        case A_SOUND:
            snd_buzzer_sound(on);
            break;
        case A_CFF98:
            snd_buzzer_cff98(on);
            break;
        case A_BOTH:
            snd_buzzer_sound(on);
            snd_buzzer_cff98(on);
            break;
        case A_NEITHER:
            break;
        case A_SOUND_WRITE:
            snd_buzzer_sound(on);

            if (on)
                snd_buzzer_cff98(0);
            break;
        case A_MODE:
            if (on)
                snd_buzzer_pit(argument, COUNT);

            snd_buzzer_sound(on);
            break;
        case A_CHANGE:
            if (!on)
            {
                snd_buzzer_sound(0);
                break;
            }

            snd_buzzer_pit(argument, COUNT);
            snd_buzzer_sound(1);
            uint32_t start = tb_now();
            tb_delay(CHANGE_MS * 1000u);
            out8(0x0044, (uint8_t)HALF_COUNT);
            out8(0x0044, (uint8_t)(HALF_COUNT >> 8));
            s_result->change_time = tb_now() - start;
            break;
        case A_SOURCE:
            source(argument, on);
            break;
        case A_VOLUME:
            snd_cdda_volume(on ? 0x00 : s_saved.volume_data);
            source(argument, on);
            break;
        case A_GATE_FM:
            out8(0x04D5, on ? SND_GATE_PCM : (SND_GATE_FM | SND_GATE_PCM));
            source(argument, on);
            break;
        case A_GATE_PCM:
            out8(0x04D5, on ? SND_GATE_FM : (SND_GATE_FM | SND_GATE_PCM));
            source(argument, on);
            break;
        case A_OUTPUT:
            out8(0x04EC, on ? (uint8_t)(s_saved.port_04ec & ~SND_OUTPUT) : (uint8_t)(s_saved.port_04ec | SND_OUTPUT));
            source(argument, on);
            break;
        case A_CD_VOLUME:
            for (uint8_t channel = 0; channel < 2; channel++)
            {
                out8(0x04E3, (uint8_t)(channel | (on ? k_volumes[argument][0] : 0x04)));
                out8(0x04E2, on ? k_volumes[argument][1] : s_saved.volume_data);
            }

            source(SOURCE_CDDA, on);
            break;
        default:
            break;
    }
}

static void source(int which, int on)
{
    cd_log_t log;

    switch (which)
    {
        case SOURCE_BUZZER:
            snd_buzzer_pit(3, COUNT);
            snd_buzzer_sound(on);
            break;
        case SOURCE_FM:
            snd_fm_key(1, on);
            break;
        case SOURCE_PCM:
            if (on)
            {
                snd_pcm_channel(0, 0, 0, PCM_STEP, 0xFF, 0xFF);
                snd_pcm_on(0x01);
            }
            else
                snd_pcm_on(0x00);
            break;
        case SOURCE_CDDA:
            if (on)
                cd_play(&log, cd_track(2), cd_track(2) + 750, 0);
            else
                cd_stop(&log);
            break;
        case SOURCE_FM_QUIET:
            snd_ym(0, 0x4D, on ? 0x3C : 0x00);
            snd_fm_key(1, on);
            break;
        case SOURCE_FM_LEFT:
            snd_ym(0, 0xB5, on ? 0x80 : 0xC0);
            snd_fm_key(1, on);
            break;
        case SOURCE_PCM_0F:
        case SOURCE_PCM_F0:
            snd_pcm_channel(0, 0, 0, PCM_STEP, 0xFF, which == SOURCE_PCM_0F ? 0x0F : 0xF0);
            snd_pcm_on(on ? 0x01 : 0x00);
            break;
        case SOURCE_PCM_NOISE:
            snd_pcm_channel(0, NOISE_START, NOISE_START << 8, 0x0800, 0xFF, 0xFF);
            snd_pcm_on(on ? 0x01 : 0x00);
            break;
        case SOURCE_FM_DAC:
            if (on)
                fm_dac();
            break;
        default:
            break;
    }
}

// 20 KiB of sign-magnitude samples with random magnitudes, never the FFh loop marker, then the marker
static void noise(void)
{
    static const uint8_t k_end = 0xFF;
    uint32_t state = 0x1234567u;

    for (uint32_t i = 0; i < NOISE_SIZE; i++)
    {
        state = state * 1103515245u + 12345u;
        uint8_t value = (uint8_t)(state >> 16);

        if (value == 0xFF)
            value = 0xFE;

        snd_pcm_write((NOISE_START << 8) + i, &value, 1);
    }

    snd_pcm_write((NOISE_START << 8) + NOISE_SIZE, &k_end, 1);
}

// Channel 6 as the DAC, 2Ah written FFh and 00h every 500 us for 2 s, both pans
static void fm_dac(void)
{
    snd_ym(1, 0xB6, 0xC0);
    snd_ym(0, 0x2B, 0x80);
    out8(0x04D8, 0x2A);
    tb_delay(50);
    uint32_t start = tb_now();

    for (uint32_t half = 0; tb_now() - start < DAC_US; half++)
    {
        out8(0x04DA, (half & 1) != 0 ? 0x00 : 0xFF);

        while (tb_now() - start < (half + 1) * DAC_HALF_US)
            ;
    }

    out8(0x04DA, 0x80);
    tb_delay(50);
    snd_ym(0, 0x2B, 0x00);
}

static int pages(const void* data, uint32_t steps)
{
    (void)data;
    (void)steps;
    return 1 + STEPS;
}

static void page(const void* data, uint32_t steps, int number)
{
    const buzzer_t* result = (const buzzer_t*)data;
    int row = SHELL_BODY_TOP;
    (void)steps;

    if (number == 0)
    {
        shell_line(row++, COLOR_CYAN, "MIXER AS FOUND");
        shell_line(row++, COLOR_WHITE, "04D5H %02X  04ECH %02X", result->port_04d5, result->port_04ec);
        shell_line(row++, COLOR_WHITE, "04E3H %02X  04E2H %02X", result->volume_command, result->volume_data);
        shell_line(row++, COLOR_WHITE, "CFF98H %s", result->cff98_mapped ? "MAPPED" : "NOT MAPPED (0404H BIT 7)");
        row++;
        shell_line(row++, COLOR_CYAN, "THE RECORDING ANSWERS");
        shell_line(row++, COLOR_WHITE, "1200 HZ = PIT AT 307.2 KHZ");
        shell_line(row++, COLOR_WHITE, "75 HZ = PIT AT 19.2 KHZ");
        shell_line(row++, COLOR_WHITE, "ENABLES: OR, OR AND");
        shell_line(row++, COLOR_WHITE, "MODE 0 2 3: OUT IN ANY MODE, OR 3 ONLY");
        shell_line(row++, COLOR_WHITE, "256>128 AT %u US: AT ONCE OR LATER", result->change_time);
        shell_line(row++, COLOR_WHITE, "LEVELS AGAINST FM, PCM AND CD-DA");
        shell_line(row++, COLOR_WHITE, "GATES: WHICH ONES SILENCE WHAT");
        shell_line(row++, COLOR_WHITE, "TL 60 DISTORTED: YM2612 LADDER");
        row++;
        shell_line(row++, COLOR_GRAY, "STARTS IN SECONDS AFTER THE FIRST");
        shell_line(row++, COLOR_GRAY, "SYNC TONE ON THE NEXT PAGES");
        return;
    }

    rec_draw(row, k_steps[number - 1].name, k_schedules[number - 1], k_counts[number - 1],
        result->starts[number - 1]);
}

static void summary(const void* data, uint32_t steps, char* line, uint8_t* color)
{
    const buzzer_t* result = (const buzzer_t*)data;
    int played = 0;
    (void)steps;

    for (int i = 0; i < STEPS; i++)
        played += result->starts[i][0] != 0;

    fmt_format(line, 34, "%u OF %u SCHEDULES PLAYED", played, STEPS);
    *color = COLOR_WHITE;
}
