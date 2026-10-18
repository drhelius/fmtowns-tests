#include "machine.h"
#include "layout.h"
#include "pic.h"
#include "record.h"
#include "towns.h"

#define RAM_PROBE_OFFSET 0x0000FFF0u
#define RAM_PROBE_TAG 0x52414D00u
#define RAM_PROBE_SX_MB 10
#define RAM_PROBE_DX_MB 64

typedef struct
{
    uint8_t model;
    const char* name;
} machine_name_t;

static const machine_name_t k_models[] =
{
    { 0x01, "MODEL 1/2" }, { 0x02, "1F/2F" }, { 0x03, "UX" }, { 0x04, "10F/20F" }, { 0x05, "CX" },
    { 0x06, "UG" }, { 0x07, "HR" }, { 0x08, "HG" }, { 0x09, "UR" }, { 0x0B, "MA" }, { 0x0C, "MX" },
    { 0x0D, "ME" }, { 0x0F, "MF/FRESH" }, { 0x11, "HC" }, { 0x4A, "MARTY" }
};

static const char* const k_cpus[8] = { "286", "386DX", "486", "386SX", "CPU4", "CPU5", "CPU6", "CPU7" };

extern const uint8_t __data_end[];

boot_capture_t g_boot SHELL_STATE;
machine_t g_machine SHELL_STATE;

static uint16_t latch_counter(uint16_t control_port, int counter);
static uint8_t probe_ram_mb(void);

void machine_capture(void)
{
    g_boot.cr0 = g_boot_cr0;
    g_boot.eflags = g_boot_eflags;
    g_boot.boot_device = g_boot_device;
    g_boot.port_0020 = in8(0x0020);
    g_boot.port_0028 = in8(0x0028);
    g_boot.port_05c0 = in8(0x05C0);
    g_boot.port_05c2 = in8(0x05C2);
    g_boot.port_04da = in8(0x04DA);
    g_boot.port_04de = in8(0x04DE);
    g_boot.port_04dc = in8(0x04DC);
    g_boot.port_04d8 = in8(0x04D8);
    g_boot.imr_master = in8(PIC_MASTER_DATA);
    g_boot.imr_slave = in8(PIC_SLAVE_DATA);
    out8(PIC_MASTER_COMMAND, 0x0A);
    out8(PIC_SLAVE_COMMAND, 0x0A);
    g_boot.irr_master = in8(PIC_MASTER_COMMAND);
    g_boot.irr_slave = in8(PIC_SLAVE_COMMAND);
    out8(PIC_MASTER_COMMAND, 0x0B);
    out8(PIC_SLAVE_COMMAND, 0x0B);
    g_boot.isr_master = in8(PIC_MASTER_COMMAND);
    g_boot.isr_slave = in8(PIC_SLAVE_COMMAND);
    out8(PIC_MASTER_COMMAND, 0x0A);
    out8(PIC_SLAVE_COMMAND, 0x0A);
    g_boot.port_0060 = in8(0x0060);
    g_boot.port_0068 = in8(0x0068);
    g_boot.port_006a = in8(0x006A);
    g_boot.port_006b = in8(0x006B);
    g_boot.port_0208 = in8(0x0208);
    g_boot.port_0404 = in8(0x0404);
    g_boot.port_0480 = in8(0x0480);
    g_boot.port_05e0 = in8(0x05E0);
    g_boot.port_05e2 = in8(0x05E2);
    g_boot.port_05e6 = in8(0x05E6);
    g_boot.port_05e8 = in8(0x05E8);
    g_boot.port_05ec = in8(0x05EC);

    for (int i = 0; i < 3; i++)
    {
        g_boot.pit[i] = latch_counter(0x0046, i);
        g_boot.pit[3 + i] = latch_counter(0x0056, i);
    }

    g_boot.timer_0026 = in16(0x0026);
    g_boot.machine_id = (uint16_t)(in8(0x0030) | (in8(0x0031) << 8));
}

void machine_identify(void)
{
    g_machine.id = g_boot.machine_id;
    g_machine.model = (uint8_t)(g_machine.id >> 8);
    g_machine.cpu = (uint8_t)(g_machine.id & 0x07);
    g_machine.sx = g_machine.cpu == CPU_TYPE_386SX;
    g_machine.ram_mb = g_boot.port_05e8 != 0xFF ? (uint8_t)(g_boot.port_05e8 & 0x1F) : probe_ram_mb();
    g_machine.has_timer2 = g_boot.port_0068 != 0xFF;
    g_machine.vram = g_machine.sx ? 0x00A00000u : 0x80000000u;
    g_machine.sprite_ram = g_machine.sx ? 0x00C00000u : 0x81000000u;
    g_machine.pcm_window = g_machine.sx ? 0x00F80000u : 0xC2200000u;
    g_machine.build = crc32((const void*)PAYLOAD_BASE, (uint32_t)__data_end - PAYLOAD_BASE);
}

const char* machine_model_name(uint8_t model)
{
    for (unsigned i = 0; i < COUNT_OF(k_models); i++)
    {
        if (k_models[i].model == model)
            return k_models[i].name;
    }

    return "UNKNOWN";
}

const char* machine_cpu_name(uint8_t cpu)
{
    return k_cpus[cpu & 0x07];
}

// Without a RAM size register (05E8h reads FFh on the Marty), a MiB counts if a word in it keeps what was written and
// the MiBs below keep theirs, so neither an empty bus nor a mirror counts. Everything is put back in reverse order
static uint8_t probe_ram_mb(void)
{
    uint32_t limit = g_machine.sx ? RAM_PROBE_SX_MB : RAM_PROBE_DX_MB;
    uint32_t saved[RAM_PROBE_DX_MB];
    uint32_t mb;

    for (mb = 0; mb < limit; mb++)
    {
        volatile uint32_t* word = (volatile uint32_t*)((mb << 20) + RAM_PROBE_OFFSET);
        int kept = 1;

        saved[mb] = *word;
        *word = RAM_PROBE_TAG | mb;

        for (uint32_t lower = 0; lower < mb; lower++)
            kept &= *(volatile uint32_t*)((lower << 20) + RAM_PROBE_OFFSET) == (RAM_PROBE_TAG | lower);

        if (!kept || *word != (RAM_PROBE_TAG | mb))
            break;
    }

    for (int i = (int)(mb < limit ? mb : limit - 1); i >= 0; i--)
        *(volatile uint32_t*)(((uint32_t)i << 20) + RAM_PROBE_OFFSET) = saved[i];

    return (uint8_t)mb;
}

static uint16_t latch_counter(uint16_t control_port, int counter)
{
    uint16_t data_port = (uint16_t)(control_port - 6 + counter * 2);
    out8(control_port, (uint8_t)(counter << 6));
    uint8_t low = in8(data_port);
    uint8_t high = in8(data_port);
    return (uint16_t)(low | (high << 8));
}
