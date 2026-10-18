#ifndef MACHINE_H
#define MACHINE_H

#include <stdint.h>

// What the boot left behind, read once before the shell touches anything, in the order docs/12 gives,
// and the machine the shell found

typedef struct
{
    uint32_t cr0;
    uint32_t eflags;
    uint32_t boot_device;
    uint16_t machine_id;
    uint8_t port_0020;
    uint8_t port_0028;
    uint8_t port_05c0;
    uint8_t port_05c2;
    uint8_t port_04da;
    uint8_t port_04de;
    uint8_t port_04dc;
    uint8_t port_04d8;
    uint8_t imr_master;
    uint8_t imr_slave;
    uint8_t irr_master;
    uint8_t irr_slave;
    uint8_t isr_master;
    uint8_t isr_slave;
    uint8_t port_0060;
    uint8_t port_0068;
    uint8_t port_0208;
    uint8_t port_0404;
    uint8_t port_0480;
    uint8_t port_05e0;
    uint8_t port_05e2;
    uint8_t port_05e6;
    uint8_t port_05e8;
    uint8_t port_05ec;
    uint16_t pit[6];
    uint16_t timer_0026;
    uint8_t port_006a;
    uint8_t port_006b;
} boot_capture_t;

typedef struct
{
    uint16_t id;
    uint8_t model;
    uint8_t cpu;
    uint8_t ram_mb;
    uint8_t sx;
    uint8_t has_timer2;
    uint8_t reserved;
    uint32_t vram;
    uint32_t sprite_ram;
    uint32_t pcm_window;
    uint32_t build;
} machine_t;

extern boot_capture_t g_boot;
extern machine_t g_machine;

void machine_capture(void);
void machine_identify(void);
const char* machine_model_name(uint8_t model);
const char* machine_cpu_name(uint8_t cpu);

#endif /* MACHINE_H */
