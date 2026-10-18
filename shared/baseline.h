#ifndef BASELINE_H
#define BASELINE_H

#include <stdint.h>

// The state every test starts from and is put back to after it, and the readable registers whose checksum shows
// whether a test left something behind

#define BASELINE_VALUES 32

typedef struct
{
    uint16_t port;
    uint8_t mask;
} baseline_register_t;

typedef struct
{
    uint8_t values[BASELINE_VALUES];
    uint32_t cmos_crc;
    uint32_t backup_crc;
    uint32_t crc;
} baseline_snapshot_t;

void baseline_apply(int screen_mode);
void baseline_take(baseline_snapshot_t* snapshot);
int baseline_difference(const baseline_snapshot_t* reference, const baseline_snapshot_t* now, uint16_t* port,
    uint8_t* before, uint8_t* after);
void baseline_ym_write(int bank, uint8_t reg, uint8_t value);

#endif /* BASELINE_H */
