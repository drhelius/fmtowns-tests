#include <stdarg.h>
#include "ui.h"
#include "fmt.h"
#include "input.h"
#include "screen.h"
#include "shell.h"
#include "timebase.h"
#include "towns.h"

static int s_pad_only;
static uint8_t s_buttons;
static uint32_t s_shown;

void ui_pad_only(int on)
{
    s_pad_only = on;
    s_buttons = 0x33;
}

void ui_prompt(const char* first, const char* second)
{
    scr_clear_rows(SHELL_BODY_TOP, SHELL_BODY_BOTTOM);
    shell_line(UI_PROMPT_ROW, COLOR_YELLOW, "%s", first);

    if (second != 0)
        shell_line(UI_PROMPT_ROW + 1, COLOR_YELLOW, "%s", second);

    s_shown = 0xFFFFFFFFu;
    scr_present();
}

void ui_note(int line, uint8_t color, const char* format, ...)
{
    char text[SCREEN_SAFE_COLUMNS + 1];
    va_list arguments;
    va_start(arguments, format);
    fmt_vformat(text, sizeof(text), format, arguments);
    va_end(arguments);
    scr_clear_rows(UI_NOTE_ROW + line, UI_NOTE_ROW + line);
    shell_line(UI_NOTE_ROW + line, color, "%s", text);
}

// Port 2 always by its own edges; port 1 and the keyboard through the shell's input unless the keyboard is the test's
int ui_poll(void)
{
    uint8_t buttons = (uint8_t)((~in8(0x04D0) & 0x30) | ((~in8(0x04D2) & 0x30) >> 4));
    uint8_t pressed = buttons & (uint8_t)~s_buttons;
    s_buttons = buttons;

    if (!s_pad_only)
    {
        int event = input_poll();

        if (event == INPUT_A)
            return UI_A;

        if (event == INPUT_B)
            return UI_B;

        pressed &= 0x03;
    }

    if ((pressed & 0x11) != 0)
        return UI_A;

    if ((pressed & 0x22) != 0)
        return UI_B;

    return UI_NONE;
}

int ui_wait(uint32_t seconds, const char* hint)
{
    uint32_t start = tb_now();
    uint32_t limit = seconds * 1000000u;

    s_buttons = 0x33;
    (void)ui_poll();

    while (tb_now() - start < limit)
    {
        int event = ui_poll();

        if (event != UI_NONE)
            return event;

        ui_countdown(start, limit, hint);
    }

    return UI_NONE;
}

void ui_countdown(uint32_t start, uint32_t microseconds, const char* hint)
{
    uint32_t elapsed = tb_now() - start;
    uint32_t left = elapsed < microseconds ? (microseconds - elapsed + 999999u) / 1000000u : 0;

    if (left == s_shown)
        return;

    s_shown = left;
    scr_clear_rows(UI_COUNTDOWN_ROW, UI_COUNTDOWN_ROW);
    shell_line(UI_COUNTDOWN_ROW, COLOR_WHITE, "%3u S  %s", left, hint != 0 ? hint : "");
    scr_present();
}
