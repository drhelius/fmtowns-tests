#include "tests.h"

// 3.4 Output coupling (docs/12, HT-24): recorded with a DC-coupled input or a scope. DC steps from three sources: a
// PCM channel looping one 7Fh sample (the largest negative level) switched on and off, the buzzer held high by
// channel 2 in mode 0 and switched by SOUND, and track 3's DC steps and squares. The same decay in all three puts the
// coupling at the output; different decays put it in each source

#define WAVE_DC 0x0000
#define PCM_LOW 0x7F
#define PCM_HIGH 0xFE
#define FULL_STEP 0x0800
#define TONE_STEP 3146
#define TONE_HALF 16

enum
{
    A_PCM_DC,
    A_PCM_WAVE,
    A_BUZZER_DC,
    A_CDDA
};

typedef struct
{
    uint8_t start;
    uint16_t half;
    uint16_t step;
} wave_t;

typedef struct
{
    uint32_t starts[REC_MAX_STEPS];
} coupling_t;

static void run(void* data, uint32_t steps);
static int pages(const void* data, uint32_t steps);
static void page(const void* data, uint32_t steps, int number);
static void summary(const void* data, uint32_t steps, char* line, uint8_t* color);
static void action(int id, int argument, int on);

static const wave_t k_waves[] =
{
    { 0x01, 2083, FULL_STEP },
    { 0x12, 1042, FULL_STEP },
    { 0x1B, 521, FULL_STEP },
    { 0x20, 260, FULL_STEP },
    { 0x23, TONE_HALF, TONE_STEP }
};

static const rec_step_t k_schedule[] =
{
    { "PCM 7FH ON", 2000, 2000, A_PCM_DC, 0 },
    { "PCM 7FH ON", 2000, 2000, A_PCM_DC, 0 },
    { "PCM 7FH ON", 2000, 2000, A_PCM_DC, 0 },
    { "PCM SQUARE 5 HZ", 4000, 1000, A_PCM_WAVE, 0 },
    { "PCM SQUARE 10 HZ", 4000, 1000, A_PCM_WAVE, 1 },
    { "PCM SQUARE 20 HZ", 4000, 1000, A_PCM_WAVE, 2 },
    { "PCM SQUARE 40 HZ", 4000, 1000, A_PCM_WAVE, 3 },
    { "PCM SQUARE 1 KHZ", 4000, 1000, A_PCM_WAVE, 4 },
    { "BUZZER HIGH, SOUND ON", 2000, 2000, A_BUZZER_DC, 0 },
    { "BUZZER HIGH, SOUND ON", 2000, 2000, A_BUZZER_DC, 0 },
    { "BUZZER HIGH, SOUND ON", 2000, 2000, A_BUZZER_DC, 0 },
    { "CD-DA TRACK 3", 29000, 1000, A_CDDA, 0 }
};

const test_t g_test_coupling =
{
    "3.4", "OUTPUT COUPLING", "REC", 0, 1, sizeof(coupling_t), 0, run, pages, page, summary
};

static void run(void* data, uint32_t steps)
{
    static const uint8_t k_dc[2] = { PCM_LOW, 0xFF };
    coupling_t* result = (coupling_t*)data;
    snd_saved_t saved;
    cd_log_t log;
    (void)steps;

    cd_quiet();
    cd_load_toc(&log);
    snd_open(&saved);
    shell_step("WAVES");
    snd_pcm_write(WAVE_DC, k_dc, 2);

    for (unsigned i = 0; i < COUNT_OF(k_waves); i++)
        snd_pcm_square((uint32_t)k_waves[i].start << 8, k_waves[i].half, PCM_HIGH, PCM_LOW);

    shell_step("SCHEDULE");
    rec_play("DC STEPS AND SQUARES", k_schedule, COUNT_OF(k_schedule), action, result->starts);
    snd_close(&saved);
    cd_quiet();
}

static void action(int id, int argument, int on)
{
    cd_log_t log;

    switch (id)
    {
        case A_PCM_DC:
            snd_pcm_channel(0, WAVE_DC >> 8, WAVE_DC, FULL_STEP, 0xFF, 0xFF);
            snd_pcm_on(on ? 0x01 : 0x00);
            break;
        case A_PCM_WAVE:
            snd_pcm_channel(0, k_waves[argument].start, (uint16_t)(k_waves[argument].start << 8),
                k_waves[argument].step, 0xFF, 0xFF);
            snd_pcm_on(on ? 0x01 : 0x00);
            break;
        case A_BUZZER_DC:
            if (on)
            {
                snd_buzzer_pit(0, 2);
                tb_delay(100);
            }

            snd_buzzer_sound(on);
            break;
        case A_CDDA:
            if (on)
                cd_play(&log, cd_track(3), cd_track_end(3) + 1, 0);
            else
                cd_stop(&log);
            break;
        default:
            break;
    }
}

static int pages(const void* data, uint32_t steps)
{
    (void)data;
    (void)steps;
    return 1;
}

static void page(const void* data, uint32_t steps, int number)
{
    const coupling_t* result = (const coupling_t*)data;
    int row = SHELL_BODY_TOP;
    (void)steps;
    (void)number;

    row = rec_draw(row, "STARTS AFTER THE FIRST SYNC TONE", k_schedule, COUNT_OF(k_schedule), result->starts);
    row++;
    shell_line(row++, COLOR_CYAN, "THE RECORDING ANSWERS");
    shell_line(row++, COLOR_WHITE, "DECAY TAU OF EACH STEP: 1/(2 PI TAU)");
    shell_line(row++, COLOR_WHITE, "SAME IN PCM BUZZER CD-DA: AT OUTPUT");
}

static void summary(const void* data, uint32_t steps, char* line, uint8_t* color)
{
    const coupling_t* result = (const coupling_t*)data;
    (void)steps;

    fmt_format(line, 34, "%s", result->starts[0] != 0 ? "SCHEDULE PLAYED" : "NOT PLAYED");
    *color = COLOR_WHITE;
}
