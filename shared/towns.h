#ifndef TOWNS_H
#define TOWNS_H

#include <stdint.h>

#define BOOT_DEVICE_FLOPPY 2
#define BOOT_DEVICE_CD 8

#define CPU_TYPE_286 0
#define CPU_TYPE_386DX 1
#define CPU_TYPE_486 2
#define CPU_TYPE_386SX 3

#define MMIO8(address) (*(volatile uint8_t*)(address))
#define MMIO16(address) (*(volatile uint16_t*)(address))
#define MMIO32(address) (*(volatile uint32_t*)(address))

#define SHELL_STATE __attribute__((section(".bss.shell")))
#define COUNT_OF(array) (sizeof(array) / sizeof((array)[0]))

extern uint32_t g_boot_device;
extern uint32_t g_boot_cr0;
extern uint32_t g_boot_eflags;

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

static inline uint32_t in32(uint16_t port)
{
    uint32_t value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
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

static inline void out32(uint16_t port, uint32_t value)
{
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint32_t read_cr0(void)
{
    uint32_t value;
    __asm__ volatile ("movl %%cr0, %0" : "=r"(value));
    return value;
}

static inline void write_cr0(uint32_t value)
{
    __asm__ volatile ("movl %0, %%cr0" : : "r"(value) : "memory");
}

static inline uint32_t read_cr2(void)
{
    uint32_t value;
    __asm__ volatile ("movl %%cr2, %0" : "=r"(value));
    return value;
}

static inline uint32_t read_cr3(void)
{
    uint32_t value;
    __asm__ volatile ("movl %%cr3, %0" : "=r"(value));
    return value;
}

static inline void write_cr3(uint32_t value)
{
    __asm__ volatile ("movl %0, %%cr3" : : "r"(value) : "memory");
}

static inline uint32_t read_eflags(void)
{
    uint32_t value;
    __asm__ volatile ("pushfl\n\tpopl %0" : "=r"(value));
    return value;
}

static inline void write_eflags(uint32_t value)
{
    __asm__ volatile ("pushl %0\n\tpopfl" : : "r"(value) : "memory", "cc");
}

static inline void cli(void)
{
    __asm__ volatile ("cli" : : : "memory");
}

static inline void sti(void)
{
    __asm__ volatile ("sti" : : : "memory");
}

#endif /* TOWNS_H */
