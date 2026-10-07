#include <stdint.h>
#include "towns.h"
#include "video.h"

typedef struct
{
    uint8_t id;
    const char* name;
} machine_name_t;

static const machine_name_t k_machine_names[] =
{
    { 0x01, "MODEL 1/2" },
    { 0x02, "1F/2F/1H/2H" },
    { 0x03, "II UX" },
    { 0x04, "10F/20F/40H/80H" },
    { 0x05, "II CX" },
    { 0x06, "II UG" },
    { 0x07, "II HR" },
    { 0x08, "II HG" },
    { 0x09, "II UR" },
    { 0x0B, "II MA" },
    { 0x0C, "II MX" },
    { 0x0D, "II ME" },
    { 0x0F, "II MF/FRESH" },
    { 0x4A, "MARTY" }
};

static const char* k_cpu_names[8] =
{
    "80286", "80386DX", "80486", "80386SX", "UNKNOWN", "UNKNOWN", "UNKNOWN", "UNKNOWN"
};

static const char* machine_name(uint8_t id);
static void print_field(const char* name);

void main(void)
{
    uint16_t machine_id = (uint16_t)(in8(0x0031) << 8) | in8(0x0030);
    uint8_t ram_size = in8(0x05E8);

    video_init();
    video_color(COLOR_LIGHT_YELLOW);
    video_print("FM TOWNS TESTS - MACHINE ID\n\n");

    print_field("MACHINE ID");
    video_print_hex(machine_id, 4);
    video_print("\n");

    print_field("MODEL");
    video_print(machine_name((uint8_t)(machine_id >> 8)));
    video_print("\n");

    print_field("CPU");
    video_print(k_cpu_names[machine_id & 0x07]);
    video_print("\n");

    print_field("BOOT DEVICE");

    if (g_boot_device == BOOT_DEVICE_FLOPPY)
        video_print("FLOPPY");
    else if (g_boot_device == BOOT_DEVICE_CD)
        video_print("CD-ROM");
    else
        video_print_hex(g_boot_device, 4);

    video_print("\n");

    // 05E8h exists from the 1F on; the Model 1/2 floats it high
    print_field("RAM 05E8H");
    video_print_hex(ram_size, 2);
    video_print("\n");
}

static const char* machine_name(uint8_t id)
{
    for (unsigned i = 0; i < sizeof(k_machine_names) / sizeof(k_machine_names[0]); i++)
    {
        if (k_machine_names[i].id == id)
            return k_machine_names[i].name;
    }

    return "UNKNOWN";
}

static void print_field(const char* name)
{
    int length = 0;

    while (name[length] != 0)
        length++;

    video_color(COLOR_LIGHT_CYAN);
    video_print(name);

    for (int i = length; i < 14; i++)
        video_print(" ");

    video_color(COLOR_BRIGHT_WHITE);
}
