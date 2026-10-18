#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>

// Pad in port 1 (Towns or Marty pad) and the Towns keyboard: arrows, Return or Space for A, Esc for B, Tab for
// Select, Q for Run, and letters and digits as shortcuts

#define INPUT_NONE 0
#define INPUT_UP 1
#define INPUT_DOWN 2
#define INPUT_LEFT 3
#define INPUT_RIGHT 4
#define INPUT_A 5
#define INPUT_B 6
#define INPUT_RUN 7
#define INPUT_SELECT 8
#define INPUT_CHAR 0x100

#define PAD_UP 0x01
#define PAD_DOWN 0x02
#define PAD_LEFT 0x04
#define PAD_RIGHT 0x08
#define PAD_A 0x10
#define PAD_B 0x20
#define PAD_RUN 0x40
#define PAD_SELECT 0x80

void input_reset(void);
int input_poll(void);
int input_wait(void);
uint8_t input_pad_state(void);
uint8_t input_pad_raw(int port);

#endif /* INPUT_H */
