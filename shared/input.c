#include "input.h"
#include "screen.h"
#include "timebase.h"
#include "towns.h"

#define PAD_PORT1 0x04D0
#define PAD_PORT2 0x04D2
#define KEYBOARD_DATA 0x0600
#define KEYBOARD_STATUS 0x0602
#define REPEAT_DELAY 400000
#define REPEAT_RATE 100000
#define SECOND_BYTE_TIMEOUT 20000

typedef struct
{
    uint8_t code;
    int event;
} input_key_t;

static const input_key_t k_keys[] =
{
    { 0x4D, INPUT_UP }, { 0x50, INPUT_DOWN }, { 0x4F, INPUT_LEFT }, { 0x51, INPUT_RIGHT },
    { 0x1D, INPUT_A }, { 0x35, INPUT_A }, { 0x45, INPUT_A }, { 0x01, INPUT_B }, { 0x10, INPUT_SELECT },
    { 0x02, INPUT_CHAR | '1' }, { 0x03, INPUT_CHAR | '2' }, { 0x04, INPUT_CHAR | '3' }, { 0x05, INPUT_CHAR | '4' },
    { 0x06, INPUT_CHAR | '5' }, { 0x07, INPUT_CHAR | '6' }, { 0x08, INPUT_CHAR | '7' }, { 0x09, INPUT_CHAR | '8' },
    { 0x0A, INPUT_CHAR | '9' }, { 0x0B, INPUT_CHAR | '0' },
    { 0x11, INPUT_RUN }, { 0x12, INPUT_CHAR | 'W' }, { 0x13, INPUT_CHAR | 'E' }, { 0x14, INPUT_CHAR | 'R' },
    { 0x15, INPUT_CHAR | 'T' }, { 0x16, INPUT_CHAR | 'Y' }, { 0x17, INPUT_CHAR | 'U' }, { 0x18, INPUT_CHAR | 'I' },
    { 0x19, INPUT_CHAR | 'O' }, { 0x1A, INPUT_CHAR | 'P' }, { 0x1E, INPUT_CHAR | 'A' }, { 0x1F, INPUT_CHAR | 'S' },
    { 0x20, INPUT_CHAR | 'D' }, { 0x21, INPUT_CHAR | 'F' }, { 0x22, INPUT_CHAR | 'G' }, { 0x23, INPUT_CHAR | 'H' },
    { 0x24, INPUT_CHAR | 'J' }, { 0x25, INPUT_CHAR | 'K' }, { 0x26, INPUT_CHAR | 'L' }, { 0x2A, INPUT_CHAR | 'Z' },
    { 0x2B, INPUT_CHAR | 'X' }, { 0x2C, INPUT_CHAR | 'C' }, { 0x2D, INPUT_CHAR | 'V' }, { 0x2E, INPUT_CHAR | 'B' },
    { 0x2F, INPUT_CHAR | 'N' }, { 0x30, INPUT_CHAR | 'M' }
};

static uint8_t s_previous SHELL_STATE;
static uint8_t s_repeat_button SHELL_STATE;
static uint32_t s_repeat_time SHELL_STATE;

static int keyboard_event(void);

void input_reset(void)
{
    s_previous = input_pad_state();
    s_repeat_button = 0;
}

uint8_t input_pad_raw(int port)
{
    return in8(port == 0 ? PAD_PORT1 : PAD_PORT2);
}

// Run reads as Left and Right together, Select as Up and Down together
uint8_t input_pad_state(void)
{
    uint8_t pressed = (uint8_t)(~input_pad_raw(0) & 0x3F);
    uint8_t state = pressed & (PAD_A | PAD_B);

    if ((pressed & 0x0C) == 0x0C)
        state |= PAD_RUN;
    else
        state |= pressed & (PAD_LEFT | PAD_RIGHT);

    if ((pressed & 0x03) == 0x03)
        state |= PAD_SELECT;
    else
        state |= pressed & (PAD_UP | PAD_DOWN);

    return state;
}

int input_poll(void)
{
    static const uint8_t k_order[] = { PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT, PAD_A, PAD_B, PAD_RUN, PAD_SELECT };
    int event = keyboard_event();

    if (event != INPUT_NONE)
        return event;

    uint8_t state = input_pad_state();
    uint8_t pressed = state & (uint8_t)~s_previous;
    uint32_t now = tb_now();
    s_previous = state;

    for (int i = 0; i < 8; i++)
    {
        if ((pressed & k_order[i]) != 0)
        {
            s_repeat_button = k_order[i] & (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT);
            s_repeat_time = now + REPEAT_DELAY;
            return i + 1;
        }
    }

    if (s_repeat_button != 0 && (state & s_repeat_button) != 0 && (int32_t)(now - s_repeat_time) >= 0)
    {
        s_repeat_time = now + REPEAT_RATE;

        for (int i = 0; i < 4; i++)
        {
            if (k_order[i] == s_repeat_button)
                return i + 1;
        }
    }

    return INPUT_NONE;
}

int input_wait(void)
{
    int event;

    scr_present();

    while ((event = input_poll()) == INPUT_NONE)
        ;

    return event;
}

// Two bytes per key: the make, break or repeat flags with the shift state, then the key code
static int keyboard_event(void)
{
    if ((in8(KEYBOARD_STATUS) & 0x01) == 0)
        return INPUT_NONE;

    uint8_t flags = in8(KEYBOARD_DATA);

    if ((flags & 0x80) == 0)
        return INPUT_NONE;

    uint32_t start = tb_now();

    while ((in8(KEYBOARD_STATUS) & 0x01) == 0)
    {
        if (tb_now() - start > SECOND_BYTE_TIMEOUT)
            return INPUT_NONE;
    }

    uint8_t code = in8(KEYBOARD_DATA) & 0x7F;

    if ((flags & 0xF0) != 0xA0 && (flags & 0xF0) != 0xF0)
        return INPUT_NONE;

    for (unsigned i = 0; i < COUNT_OF(k_keys); i++)
    {
        if (k_keys[i].code == code)
            return k_keys[i].event;
    }

    return INPUT_NONE;
}
