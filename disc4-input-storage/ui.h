#ifndef UI_H
#define UI_H

#include <stdint.h>

// Prompts for the tests that need someone at the machine: A or B on either pad, or Return and Esc on the keyboard
// unless the test owns the keyboard, and a countdown so no prompt waits for ever

#define UI_NONE 0
#define UI_A 1
#define UI_B 2

#define UI_PROMPT_ROW 6
#define UI_COUNTDOWN_ROW 9
#define UI_NOTE_ROW 11

void ui_pad_only(int on);
void ui_prompt(const char* first, const char* second);
void ui_note(int line, uint8_t color, const char* format, ...);
int ui_poll(void);
int ui_wait(uint32_t seconds, const char* hint);
void ui_countdown(uint32_t start, uint32_t microseconds, const char* hint);

#endif /* UI_H */
