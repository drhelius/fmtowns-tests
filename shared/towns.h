#ifndef TOWNS_H
#define TOWNS_H

#include <stdint.h>

#define BOOT_DEVICE_FLOPPY 2
#define BOOT_DEVICE_CD 8

#define MMIO8(address) (*(volatile uint8_t*)(address))

extern uint32_t g_boot_device;

static inline uint8_t in8(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint16_t in16(uint16_t port)
{
    uint16_t value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void out8(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline void out16(uint16_t port, uint16_t value)
{
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

#endif /* TOWNS_H */
