#include "fdc.h"
#include "cdc.h"
#include "timebase.h"
#include "towns.h"

#define COMMAND_SETTLE_US 60
#define DMA_CHANNEL 0
#define DMA_MODE_READ 0x44
#define SPIN_UP_US 2000000u
#define ADDRESS_TIMEOUT_US 1200000u
#define PIO_PASSES_PER_MS 1000u
#define PIO_SETTLE_PASSES 40u

uint32_t fdc_pio_read(uint8_t* buffer, uint32_t capacity, uint32_t limit, uint32_t settle);

const fdc_mode_t g_fdc_modes[FDC_MODES] =
{
    { 0, 1, "2HD 360 RPM" }, { 0, 0, "2HD 300 RPM" }, { 1, 0, "2DD 300 RPM" }
};

static uint8_t s_control;
static int s_high_speed;

// Speed and in-use are latched as a drive gets selected, so the select goes out with them already set
void fdc_select(int drive, int high_speed)
{
    uint8_t speed = high_speed ? 0x40 : 0x00;

    s_high_speed = high_speed;
    out8(FDC_SELECT, speed);

    if (drive >= 0)
        out8(FDC_SELECT, (uint8_t)(speed | (1 << drive)));
}

void fdc_control(uint8_t value)
{
    s_control = value;
    out8(FDC_DRIVE, value);
}

uint8_t fdc_control_value(void)
{
    return s_control;
}

void fdc_motor(int on)
{
    fdc_control(on ? (uint8_t)(s_control | FDC_MOTOR) : (uint8_t)(s_control & ~FDC_MOTOR));
}

void fdc_set_mode(int mode)
{
    const fdc_mode_t* entry = &g_fdc_modes[mode];

    if (entry->high_speed != s_high_speed)
        fdc_select(0, entry->high_speed);

    fdc_control((uint8_t)((s_control & ~FDC_SLOW) | (entry->slow ? FDC_SLOW : 0)));
}

void fdc_side(int side_bit)
{
    fdc_control((uint8_t)((s_control & ~FDC_SIDE_BIT) | (side_bit ? FDC_SIDE_BIT : 0)));
}

void fdc_command(uint8_t command)
{
    out8(FDC_COMMAND, command);
    tb_delay(COMMAND_SETTLE_US);
}

// The status once BUSY clears, or FDC_TIMEOUT after a forced end
uint8_t fdc_wait(uint32_t timeout, uint32_t* elapsed)
{
    uint32_t start = tb_now();
    uint8_t status;

    while (((status = in8(FDC_COMMAND)) & FDC_BUSY) != 0)
    {
        if (tb_now() - start > timeout)
        {
            fdc_force();

            if (elapsed != 0)
                *elapsed = tb_now() - start;

            return FDC_TIMEOUT;
        }
    }

    if (elapsed != 0)
        *elapsed = tb_now() - start;

    return status;
}

// Force interrupt with no condition: whatever runs stops and the status turns to the type I meanings
uint8_t fdc_force(void)
{
    out8(FDC_COMMAND, 0xD0);
    tb_delay(COMMAND_SETTLE_US);
    return in8(FDC_COMMAND);
}

// The time of the next rise of the index bit, 0 if none came
uint32_t fdc_index_edge(uint32_t timeout)
{
    uint32_t start = tb_now();
    int previous = (in8(FDC_COMMAND) & FDC_INDEX) != 0;

    while (tb_now() - start < timeout)
    {
        int index = (in8(FDC_COMMAND) & FDC_INDEX) != 0;

        if (index && !previous)
        {
            uint32_t now = tb_now();
            return now != 0 ? now : 1;
        }

        previous = index;
    }

    return 0;
}

// One revolution from rise to rise, 0 without index pulses
uint32_t fdc_period(void)
{
    fdc_force();
    uint32_t first = fdc_index_edge(500000u);

    if (first == 0)
        return 0;

    uint32_t second = fdc_index_edge(500000u);
    return second != 0 ? second - first : 0;
}

// Read Address by programmed I/O; the six ID bytes and the status (40h when fewer came), the time it ended. The loop
// is in assembly: a data request comes every 16 us in 2HD
uint8_t fdc_read_address(uint8_t command, uint8_t* id, uint32_t timeout, uint32_t* end)
{
    uint32_t count;
    uint8_t status;

    out8(FDC_COMMAND, command);
    count = fdc_pio_read(id, 6, timeout * PIO_PASSES_PER_MS / 1000u, PIO_SETTLE_PASSES);

    if (end != 0)
        *end = tb_now();

    if (count == 0xFFFFFFFFu)
    {
        fdc_force();
        return FDC_TIMEOUT;
    }

    status = in8(FDC_COMMAND);
    return count == 6 ? status : (uint8_t)(status | 0x40);
}

// A type II or III read into the buffer by DMA; the status, the time and the bytes the DMA moved
uint8_t fdc_read_dma(uint8_t command, uint16_t count, uint32_t timeout, uint32_t* elapsed, uint16_t* moved)
{
    uint16_t left;
    uint8_t status;

    cdc_dma_setup(DMA_CHANNEL, FDC_BUFFER, (uint16_t)(count - 1), DMA_MODE_READ);
    out8(FDC_COMMAND, command);
    tb_delay(COMMAND_SETTLE_US);
    status = fdc_wait(timeout, elapsed);

    if (elapsed != 0)
        *elapsed += COMMAND_SETTLE_US;

    out8(0x00A1, DMA_CHANNEL);
    left = (uint16_t)(in8(0x00A2) | (in8(0x00A3) << 8));
    out8(0x00AF, 0x0F);

    if (moved != 0)
        *moved = (uint16_t)(count - 1 - left);

    return status;
}

uint8_t fdc_restore(uint8_t rate, uint32_t* elapsed)
{
    return fdc_type1((uint8_t)(0x00 | (rate & 0x03)), elapsed);
}

// A type I command as written, its time from the write to BUSY clear
uint8_t fdc_type1(uint8_t command, uint32_t* elapsed)
{
    out8(FDC_COMMAND, command);
    tb_delay(COMMAND_SETTLE_US);
    uint8_t status = fdc_wait(3000000u, elapsed);

    if (elapsed != 0)
        *elapsed += COMMAND_SETTLE_US;

    return status;
}

// Seek and its variants: the command byte carries h, V and the rate
uint8_t fdc_seek(uint8_t from, uint8_t to, uint8_t command, uint32_t* elapsed)
{
    out8(FDC_TRACK, from);
    out8(FDC_DATA, to);
    out8(FDC_COMMAND, command);
    tb_delay(COMMAND_SETTLE_US);
    uint8_t status = fdc_wait(3000000u, elapsed);

    if (elapsed != 0)
        *elapsed += COMMAND_SETTLE_US;

    return status;
}

// Drive 0 selected with the motor on until index pulses come; 0 when none did (no disk)
int fdc_spin_up(uint32_t timeout)
{
    uint32_t start = tb_now();

    fdc_motor(1);
    fdc_force();

    while (tb_now() - start < timeout)
    {
        if (fdc_index_edge(50000u) != 0)
            return 1;
    }

    return 0;
}

// Each mode in turn until a Read Address on cylinder 0 finds an ID; the drive stays in that mode with the motor on
int fdc_detect(fdc_media_t* media)
{
    uint8_t id[6];

    media->mode = FDC_NO_MODE;
    media->side_bit = 1;
    media->found = 0;
    fdc_select(0, 1);
    fdc_control(FDC_MFM);

    if (!fdc_spin_up(SPIN_UP_US))
        return 0;

    for (int mode = 0; mode < FDC_MODES; mode++)
    {
        fdc_set_mode(mode);
        tb_delay(mode == 0 ? 100000u : 600000u);
        fdc_restore(0, 0);
        uint8_t status = fdc_read_address(0xC0, id, ADDRESS_TIMEOUT_US, 0);

        if (status != FDC_TIMEOUT && (status & (FDC_RNF | FDC_CRC | 0x40)) == 0)
        {
            media->mode = (uint8_t)mode;
            media->found = 1;
            media->status = status;

            for (int i = 0; i < 6; i++)
                media->id[i] = id[i];

            media->period = fdc_period();
            return 1;
        }
    }

    return 1;
}

void fdc_off(void)
{
    fdc_force();
    out8(0x00AF, 0x0F);
    fdc_control(0x00);
    out8(FDC_SELECT, 0x00);
}
