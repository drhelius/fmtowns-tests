#include "boot.h"
#include "machine.h"
#include "shell.h"
#include "timebase.h"
#include "towns.h"

typedef struct
{
    boot_capture_t boot;
    machine_t machine;
    uint8_t time_base;
    uint8_t reserved[3];
} boot_result_t;

static void run(void* data, uint32_t steps);
static int pages(const void* data, uint32_t steps);
static void page(const void* data, uint32_t steps, int number);
static void summary(const void* data, uint32_t steps, char* line, uint8_t* color);

const test_t g_boot_test =
{
    "0", "BOOT STATE", 0, 0, 1, sizeof(boot_result_t), 0, run, pages, page, summary
};

static void run(void* data, uint32_t steps)
{
    boot_result_t* result = (boot_result_t*)data;
    (void)steps;
    result->boot = g_boot;
    result->machine = g_machine;
    result->time_base = (uint8_t)tb_source();
}

static int pages(const void* data, uint32_t steps)
{
    (void)data;
    (void)steps;
    return 1;
}

static void page(const void* data, uint32_t steps, int number)
{
    const boot_result_t* result = (const boot_result_t*)data;
    const boot_capture_t* boot = &result->boot;
    int row = SHELL_BODY_TOP;
    (void)steps;
    (void)number;

    shell_line(row++, COLOR_WHITE, "MODEL %s ID %04X %s", machine_model_name(result->machine.model),
        result->machine.id, machine_cpu_name(result->machine.cpu));
    shell_line(row++, COLOR_WHITE, "BOOT %s (%04X)  05E8H %02X", boot->boot_device == 8 ? "CD" :
        boot->boot_device == 2 ? "FD" : "??", boot->boot_device, boot->port_05e8);
    shell_line(row++, COLOR_WHITE, "CR0 %08X  EFLAGS %08X", boot->cr0, boot->eflags);
    row++;
    shell_line(row++, COLOR_CYAN, "ONE-SHOT READS IN THIS ORDER");
    shell_line(row++, COLOR_WHITE, "0020H %02X  0028H %02X  05C0H %02X", boot->port_0020, boot->port_0028,
        boot->port_05c0);
    shell_line(row++, COLOR_WHITE, "05C2H %02X  04DAH %02X  04DEH %02X", boot->port_05c2, boot->port_04da,
        boot->port_04de);
    shell_line(row++, COLOR_WHITE, "04DCH %02X  04D8H %02X", boot->port_04dc, boot->port_04d8);
    shell_line(row++, COLOR_WHITE, "IMR %02X %02X  IRR %02X %02X  ISR %02X %02X", boot->imr_master, boot->imr_slave,
        boot->irr_master, boot->irr_slave, boot->isr_master, boot->isr_slave);
    shell_line(row++, COLOR_WHITE, "0060H %02X  0068H %02X  0208H %02X", boot->port_0060, boot->port_0068,
        boot->port_0208);
    shell_line(row++, COLOR_WHITE, "0404H %02X  0480H %02X  05E0H %02X", boot->port_0404, boot->port_0480,
        boot->port_05e0);
    shell_line(row++, COLOR_WHITE, "05E2H %02X  05E6H %02X  05ECH %02X", boot->port_05e2, boot->port_05e6,
        boot->port_05ec);
    shell_line(row++, COLOR_WHITE, "PIT0 %04X %04X %04X", boot->pit[0], boot->pit[1], boot->pit[2]);
    shell_line(row++, COLOR_WHITE, "PIT1 %04X %04X %04X", boot->pit[3], boot->pit[4], boot->pit[5]);
    shell_line(row++, COLOR_WHITE, "0026H %04X  CLOCK %s", boot->timer_0026,
        result->time_base == TB_SOURCE_0026 ? "0026H 1 US" : "PIT 3.26 US");
    row++;
    shell_line(row++, COLOR_GRAY, "VRAM %08X SPRITES %08X", result->machine.vram, result->machine.sprite_ram);
    shell_line(row++, COLOR_GRAY, "PCM %08X BUILD %08X", result->machine.pcm_window, result->machine.build);
}

static void summary(const void* data, uint32_t steps, char* line, uint8_t* color)
{
    const boot_result_t* result = (const boot_result_t*)data;
    (void)steps;
    fmt_format(line, 34, "ID %04X CR0 %08X 0020 %02X", result->machine.id, result->boot.cr0,
        result->boot.port_0020);
    *color = COLOR_WHITE;
}
