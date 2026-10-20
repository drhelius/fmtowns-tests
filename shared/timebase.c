#include "timebase.h"
#include "towns.h"

#define PIT0_COUNTER0 0x0040
#define PIT0_CONTROL 0x0046
#define FREE_RUN_TIMER 0x0026

static int s_source SHELL_STATE;
static uint16_t s_last SHELL_STATE;
static uint32_t s_microseconds SHELL_STATE;
static uint32_t s_remainder SHELL_STATE;

// The Sprite Probe's detection: the counter exists if it changes within 65,536 reads
void tb_detect(void)
{
    uint16_t first = in16(FREE_RUN_TIMER);

    s_source = TB_SOURCE_PIT;

    for (uint32_t i = 0; i < 65536; i++)
    {
        if (in16(FREE_RUN_TIMER) != first)
        {
            s_source = TB_SOURCE_0026;
            break;
        }
    }

    tb_pit_program();
    tb_sync();
}

void tb_sync(void)
{
    s_last = s_source == TB_SOURCE_0026 ? in16(FREE_RUN_TIMER) : tb_pit_read();
    s_remainder = 0;
}

int tb_source(void)
{
    return s_source;
}

// 1,000,000 / 307,200 = 625 / 192
uint32_t tb_now(void)
{
    if (s_source == TB_SOURCE_0026)
    {
        uint16_t now = in16(FREE_RUN_TIMER);
        s_microseconds += (uint16_t)(now - s_last);
        s_last = now;
        return s_microseconds;
    }

    uint16_t count = tb_pit_read();
    uint32_t scaled = (uint32_t)(uint16_t)(s_last - count) * 625 + s_remainder;
    s_last = count;
    s_microseconds += scaled / 192;
    s_remainder = scaled % 192;
    return s_microseconds;
}

// A 0026h count read after the last tb_now(), within 65 ms of it, on the same scale
uint32_t tb_extend(uint16_t count)
{
    return s_microseconds + (uint16_t)(count - s_last);
}

void tb_delay(uint32_t microseconds)
{
    uint32_t start = tb_now();

    while (tb_now() - start < microseconds)
        ;
}

// Mode 2, binary, count 65,536
void tb_pit_program(void)
{
    out8(PIT0_CONTROL, 0x34);
    out8(PIT0_COUNTER0, 0x00);
    out8(PIT0_COUNTER0, 0x00);
}

uint16_t tb_pit_read(void)
{
    out8(PIT0_CONTROL, 0x00);
    uint8_t low = in8(PIT0_COUNTER0);
    uint8_t high = in8(PIT0_COUNTER0);
    return (uint16_t)(low | (high << 8));
}
