#include "sound.h"
#include "input.h"
#include "machine.h"
#include "screen.h"
#include "shell.h"
#include "timebase.h"
#include "towns.h"

#define YM_DELAY 50
#define PCM_BANK_SIZE 0x1000u
#define BUZZER_REGISTER 0x000CFF98u
#define SYNC_TONES 3
#define SYNC_ON_MS 50
#define SYNC_PERIOD_MS 200
#define SYNC_TAIL_MS 500
#define LIST_TOP (SHELL_BODY_TOP + 2)

static int s_started;
static uint32_t s_origin;

static void wait_ms(uint32_t start, uint32_t milliseconds, int row);

void snd_open(snd_saved_t* saved)
{
    saved->port_04d5 = in8(0x04D5);
    saved->port_04ec = in8(0x04EC);
    saved->volume_command = in8(0x04E3);
    saved->volume_data = in8(0x04E2);
    out8(0x04D5, SND_GATE_FM | SND_GATE_PCM);
    out8(0x04EC, (uint8_t)(saved->port_04ec | SND_OUTPUT));
}

void snd_close(const snd_saved_t* saved)
{
    snd_fm_silence();
    out8(0x04F8, 0xFF);
    out8(0x04F7, 0x00);
    snd_buzzer_sound(0);
    snd_buzzer_cff98(0);
    out8(0x0046, 0xB6);
    out8(0x0044, 0x00);
    out8(0x0044, 0x00);

    for (uint8_t channel = 0; channel < 2; channel++)
    {
        out8(0x04E3, (uint8_t)(channel | (saved->volume_command & 0x1C)));
        out8(0x04E2, saved->volume_data);
    }

    out8(0x04E3, saved->volume_command);
    out8(0x04D5, saved->port_04d5);
    out8(0x04EC, saved->port_04ec);
}

// Fixed waits instead of the busy flag, so no write reads 04D8h
void snd_ym(int bank, uint8_t reg, uint8_t value)
{
    uint16_t address = bank != 0 ? 0x04DC : 0x04D8;
    out8(address, reg);
    tb_delay(YM_DELAY);
    out8((uint16_t)(address + 2), value);
    tb_delay(YM_DELAY);
}

// Algorithm 7 with operator 4 alone at TL tl, MUL 1, AR 31, no decay, RR 15, both pans
void snd_fm_tone(int channel, uint8_t block, uint16_t fnum, uint8_t tl)
{
    static const uint8_t k_operators[] = { 0x00, 0x04, 0x08, 0x0C };

    for (unsigned i = 0; i < COUNT_OF(k_operators); i++)
    {
        uint8_t offset = (uint8_t)(k_operators[i] + channel);
        snd_ym(0, (uint8_t)(0x30 + offset), 0x01);
        snd_ym(0, (uint8_t)(0x40 + offset), k_operators[i] == 0x0C ? tl : 0x7F);
        snd_ym(0, (uint8_t)(0x50 + offset), 0x1F);
        snd_ym(0, (uint8_t)(0x60 + offset), 0x00);
        snd_ym(0, (uint8_t)(0x70 + offset), 0x00);
        snd_ym(0, (uint8_t)(0x80 + offset), 0x0F);
        snd_ym(0, (uint8_t)(0x90 + offset), 0x00);
    }

    snd_ym(0, (uint8_t)(0xB0 + channel), 0x07);
    snd_ym(0, (uint8_t)(0xB4 + channel), 0xC0);
    snd_ym(0, (uint8_t)(0xA4 + channel), (uint8_t)((block << 3) | (fnum >> 8)));
    snd_ym(0, (uint8_t)(0xA0 + channel), (uint8_t)fnum);
}

void snd_fm_key(int channel, int on)
{
    snd_ym(0, 0x28, (uint8_t)((on ? 0xF0 : 0x00) | channel));
}

void snd_fm_silence(void)
{
    static const uint8_t k_channels[] = { 0, 1, 2, 4, 5, 6 };

    for (unsigned i = 0; i < COUNT_OF(k_channels); i++)
        snd_ym(0, 0x28, k_channels[i]);

    snd_ym(0, 0x2B, 0x00);
}

void snd_pcm_write(uint32_t offset, const uint8_t* data, uint32_t size)
{
    for (uint32_t i = 0; i < size; i++)
    {
        uint32_t address = offset + i;

        if (i == 0 || (address & (PCM_BANK_SIZE - 1)) == 0)
            out8(0x04F7, (uint8_t)(0x80 | (address >> 12)));

        MMIO8(g_machine.pcm_window + (address & (PCM_BANK_SIZE - 1))) = data[i];
    }
}

void snd_pcm_fill(uint32_t offset, uint8_t value, uint32_t size)
{
    for (uint32_t i = 0; i < size; i++)
        snd_pcm_write(offset + i, &value, 1);
}

// A loop of half samples of high, half of low, then the loop marker
void snd_pcm_square(uint32_t offset, uint32_t half, uint8_t high, uint8_t low)
{
    static const uint8_t k_end = 0xFF;

    snd_pcm_fill(offset, high, half);
    snd_pcm_fill(offset + half, low, half);
    snd_pcm_write(offset + 2 * half, &k_end, 1);
}

void snd_pcm_channel(int channel, uint8_t start, uint16_t loop, uint16_t step, uint8_t envelope, uint8_t pan)
{
    out8(0x04F7, (uint8_t)(0xC0 | channel));
    out8(0x04F0, envelope);
    out8(0x04F1, pan);
    out8(0x04F2, (uint8_t)step);
    out8(0x04F3, (uint8_t)(step >> 8));
    out8(0x04F4, (uint8_t)loop);
    out8(0x04F5, (uint8_t)(loop >> 8));
    out8(0x04F6, start);
}

void snd_pcm_on(uint8_t mask)
{
    out8(0x04F7, 0x80);
    out8(0x04F8, (uint8_t)~mask);
}

// Channel 2 of the first PIT, LSB then MSB, binary
void snd_buzzer_pit(int mode, uint16_t count)
{
    out8(0x0046, (uint8_t)(0xB0 | (mode << 1)));
    out8(0x0044, (uint8_t)count);
    out8(0x0044, (uint8_t)(count >> 8));
}

void snd_buzzer_sound(int on)
{
    out8(0x0060, on ? 0x04 : 0x00);
}

// Reading CFF98h turns the buzzer on and writing it turns it off, where the FM-R registers are mapped
int snd_buzzer_cff98(int on)
{
    if ((g_boot.port_0404 & 0x80) != 0)
        return 0;

    if (on)
        (void)MMIO8(BUZZER_REGISTER);
    else
        MMIO8(BUZZER_REGISTER) = 0x00;

    return 1;
}

// Both CD-DA channels of the second MB87078, enabled at 0 dB steps of data
void snd_cdda_volume(uint8_t data)
{
    for (uint8_t channel = 0; channel < 2; channel++)
    {
        out8(0x04E3, (uint8_t)(0x04 | channel));
        out8(0x04E2, data);
    }
}

// The first schedule of a run waits for A (B skips it); later ones in the same run start after a second
int rec_play(const char* title, const rec_step_t* steps, int count, rec_action_t action, uint32_t* starts)
{
    uint32_t planned = SYNC_TONES * SYNC_PERIOD_MS + SYNC_TAIL_MS;
    int row = SHELL_BODY_TOP;

    scr_clear_rows(SHELL_BODY_TOP, SHELL_BODY_BOTTOM);
    shell_line(row++, COLOR_CYAN, "%s", title);
    shell_line(row++, COLOR_GRAY, "    STEP                 START   LEN");

    for (int i = 0; i < count && i < REC_MAX_STEPS; i++)
    {
        shell_line(LIST_TOP + i, COLOR_WHITE, "%2u  %-19s %3u.%01u %2u.%01u", i + 1, steps[i].name, planned / 1000,
            planned % 1000 / 100, steps[i].milliseconds / 1000, steps[i].milliseconds % 1000 / 100);
        planned += steps[i].milliseconds + steps[i].gap;
    }

    int prompt = LIST_TOP + count + 1;

    if (!s_started)
    {
        shell_line(prompt, COLOR_YELLOW, "START THE RECORDER, A PLAY, B SKIP");
        scr_present();

        for (;;)
        {
            int event = input_wait();

            if (event == INPUT_B)
                return 0;

            if (event == INPUT_A)
                break;
        }

        s_started = 1;
    }
    else
        tb_delay(1000000u);

    scr_clear_rows(prompt, prompt);
    shell_line(prompt, COLOR_YELLOW, "SYNC BURST");
    scr_present();

    snd_fm_tone(0, 5, 1180, 0x00);
    uint32_t origin = tb_now();
    s_origin = origin;

    for (int i = 0; i < SYNC_TONES; i++)
    {
        snd_fm_key(0, 1);
        wait_ms(origin + (uint32_t)i * SYNC_PERIOD_MS * 1000u, SYNC_ON_MS, -1);
        snd_fm_key(0, 0);
        wait_ms(origin + (uint32_t)i * SYNC_PERIOD_MS * 1000u, SYNC_PERIOD_MS, -1);
    }

    wait_ms(origin, SYNC_TONES * SYNC_PERIOD_MS + SYNC_TAIL_MS, -1);

    for (int i = 0; i < count && i < REC_MAX_STEPS; i++)
    {
        shell_line(LIST_TOP + i, COLOR_YELLOW, ">");
        uint32_t start = tb_now();
        starts[i] = start - origin;
        action(steps[i].id, steps[i].argument, 1);
        wait_ms(start, steps[i].milliseconds, prompt);
        action(steps[i].id, steps[i].argument, 0);
        wait_ms(start, (uint32_t)steps[i].milliseconds + steps[i].gap, prompt);
        shell_line(LIST_TOP + i, COLOR_GREEN, " ");
    }

    scr_clear_rows(prompt, prompt);
    shell_line(prompt, COLOR_GREEN, "DONE AT %u MS", (tb_now() - origin) / 1000);
    scr_present();
    return 1;
}

// A schedule as it ran: each step's start in ms after the burst's first tone
int rec_draw(int row, const char* title, const rec_step_t* steps, int count, const uint32_t* starts)
{
    shell_line(row++, COLOR_CYAN, "%s", title);

    if (starts[0] == 0)
    {
        shell_line(row++, COLOR_GRAY, "NOT PLAYED");
        return row;
    }

    for (int i = 0; i < count && i < REC_MAX_STEPS; i++)
        shell_line(row++, COLOR_WHITE, "%2u %-22s %6u.%03u", i + 1, steps[i].name, starts[i] / 1000000u,
            starts[i] / 1000u % 1000u);

    return row;
}

static void wait_ms(uint32_t start, uint32_t milliseconds, int row)
{
    uint32_t shown = 0xFFFFFFFFu;

    while (tb_now() - start < milliseconds * 1000u)
    {
        uint32_t tenths = (tb_now() - s_origin) / 100000u;

        if (row >= 0 && tenths != shown)
        {
            shown = tenths;
            scr_clear_rows(row, row);
            shell_line(row, COLOR_YELLOW, "PLAYING, %u.%u S SINCE THE BURST", tenths / 10, tenths % 10);
            scr_present();
        }
    }
}
