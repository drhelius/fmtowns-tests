#ifndef FDC_H
#define FDC_H

#include <stdint.h>

// The MB8877 and the drive latches driven directly: drive 0 only, DMA channel 0 into scratch RAM, every wait bounded

#define FDC_COMMAND 0x0200
#define FDC_TRACK 0x0202
#define FDC_SECTOR 0x0204
#define FDC_DATA 0x0206
#define FDC_DRIVE 0x0208
#define FDC_SELECT 0x020C

#define FDC_IRQ_ON 0x01
#define FDC_MFM 0x02
#define FDC_SIDE_BIT 0x04
#define FDC_MOTOR 0x10
#define FDC_SLOW 0x20

#define FDC_BUSY 0x01
#define FDC_INDEX 0x02
#define FDC_DRQ 0x02
#define FDC_TRACK0 0x04
#define FDC_LOST 0x04
#define FDC_CRC 0x08
#define FDC_RNF 0x10
#define FDC_NOT_READY 0x80

#define FDC_READY 0x02
#define FDC_TIMEOUT 0xFFu
#define FDC_BUFFER 0x00100000u
#define FDC_MODES 3
#define FDC_NO_MODE 0xFF

typedef struct
{
    uint8_t slow;
    uint8_t high_speed;
    const char* name;
} fdc_mode_t;

typedef struct
{
    uint8_t mode;
    uint8_t side_bit;
    uint8_t found;
    uint8_t reserved;
    uint8_t id[6];
    uint8_t status;
    uint8_t reserved2;
    uint32_t period;
} fdc_media_t;

extern const fdc_mode_t g_fdc_modes[FDC_MODES];

void fdc_select(int drive, int high_speed);
void fdc_control(uint8_t value);
uint8_t fdc_control_value(void);
void fdc_motor(int on);
void fdc_set_mode(int mode);
void fdc_side(int side_bit);
void fdc_command(uint8_t command);
uint8_t fdc_wait(uint32_t timeout, uint32_t* elapsed);
uint8_t fdc_force(void);
uint32_t fdc_index_edge(uint32_t timeout);
uint32_t fdc_period(void);
uint8_t fdc_read_address(uint8_t command, uint8_t* id, uint32_t timeout, uint32_t* end);
uint8_t fdc_read_dma(uint8_t command, uint16_t count, uint32_t timeout, uint32_t* elapsed, uint16_t* moved);
uint8_t fdc_restore(uint8_t rate, uint32_t* elapsed);
uint8_t fdc_seek(uint8_t from, uint8_t to, uint8_t command, uint32_t* elapsed);
int fdc_spin_up(uint32_t timeout);
int fdc_detect(fdc_media_t* media);
void fdc_off(void);

#endif /* FDC_H */
