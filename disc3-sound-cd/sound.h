#ifndef SOUND_H
#define SOUND_H

#include <stdint.h>

// FM, PCM, buzzer and mixer helpers for the recorded tests, and the schedule they play: a sync burst of three 50 ms
// FM tones 200 ms apart, then each step with silence after it, shown on screen with the time since the burst

#define SND_PCM_RATE 20833u
#define SND_GATE_FM 0x02
#define SND_GATE_PCM 0x01
#define SND_OUTPUT 0x40
#define REC_MAX_STEPS 24

typedef struct
{
    uint8_t port_04d5;
    uint8_t port_04ec;
    uint8_t volume_command;
    uint8_t volume_data;
} snd_saved_t;

typedef struct
{
    const char* name;
    uint16_t milliseconds;
    uint16_t gap;
    uint8_t id;
    uint8_t argument;
} rec_step_t;

typedef void (*rec_action_t)(int id, int argument, int on);

void snd_open(snd_saved_t* saved);
void snd_close(const snd_saved_t* saved);
void snd_ym(int bank, uint8_t reg, uint8_t value);
void snd_fm_tone(int channel, uint8_t block, uint16_t fnum, uint8_t tl);
void snd_fm_key(int channel, int on);
void snd_fm_silence(void);
void snd_pcm_write(uint32_t offset, const uint8_t* data, uint32_t size);
void snd_pcm_fill(uint32_t offset, uint8_t value, uint32_t size);
void snd_pcm_channel(int channel, uint8_t start, uint16_t loop, uint16_t step, uint8_t envelope, uint8_t pan);
void snd_pcm_on(uint8_t mask);
void snd_pcm_square(uint32_t offset, uint32_t half, uint8_t high, uint8_t low);
void snd_buzzer_pit(int mode, uint16_t count);
void snd_buzzer_sound(int on);
int snd_buzzer_cff98(int on);
void snd_cdda_volume(uint8_t data);
int rec_play(const char* title, const rec_step_t* steps, int count, rec_action_t action, uint32_t* starts);
int rec_draw(int row, const char* title, const rec_step_t* steps, int count, const uint32_t* starts);

#endif /* SOUND_H */
