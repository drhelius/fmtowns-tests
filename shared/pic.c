#include "pic.h"
#include "towns.h"

// Level triggered with vectors 40h-4Fh as the system ROMs program them; the Model 1/2 ROM leaves SFNM off
void pic_init(uint16_t machine_id)
{
    uint8_t master_icw4 = (machine_id >> 8) == 0x01 ? 0x0D : 0x1D;

    out8(PIC_MASTER_COMMAND, 0x19);
    out8(PIC_MASTER_DATA, 0x40);
    out8(PIC_MASTER_DATA, 0x80);
    out8(PIC_MASTER_DATA, master_icw4);
    out8(PIC_MASTER_DATA, 0xFF);
    out8(PIC_SLAVE_COMMAND, 0x19);
    out8(PIC_SLAVE_DATA, 0x48);
    out8(PIC_SLAVE_DATA, 0x07);
    out8(PIC_SLAVE_DATA, 0x09);
    out8(PIC_SLAVE_DATA, 0xFF);
}

void pic_mask_all(void)
{
    out8(PIC_MASTER_DATA, 0xFF);
    out8(PIC_SLAVE_DATA, 0xFF);
}

void pic_unmask(int irq)
{
    if (irq >= 8)
    {
        out8(PIC_SLAVE_DATA, in8(PIC_SLAVE_DATA) & (uint8_t)~(1 << (irq - 8)));
        out8(PIC_MASTER_DATA, in8(PIC_MASTER_DATA) & (uint8_t)~(1 << PIC_CASCADE));
    }
    else
        out8(PIC_MASTER_DATA, in8(PIC_MASTER_DATA) & (uint8_t)~(1 << irq));
}

void pic_mask(int irq)
{
    if (irq >= 8)
        out8(PIC_SLAVE_DATA, in8(PIC_SLAVE_DATA) | (uint8_t)(1 << (irq - 8)));
    else
        out8(PIC_MASTER_DATA, in8(PIC_MASTER_DATA) | (uint8_t)(1 << irq));
}

void pic_eoi(int irq)
{
    if (irq >= 8)
        out8(PIC_SLAVE_COMMAND, 0x20);

    out8(PIC_MASTER_COMMAND, 0x20);
}

uint16_t pic_read_imr(void)
{
    return (uint16_t)(in8(PIC_MASTER_DATA) | (in8(PIC_SLAVE_DATA) << 8));
}

uint16_t pic_read_irr(void)
{
    out8(PIC_MASTER_COMMAND, 0x0A);
    out8(PIC_SLAVE_COMMAND, 0x0A);
    return (uint16_t)(in8(PIC_MASTER_COMMAND) | (in8(PIC_SLAVE_COMMAND) << 8));
}

uint16_t pic_read_isr(void)
{
    uint16_t value;

    out8(PIC_MASTER_COMMAND, 0x0B);
    out8(PIC_SLAVE_COMMAND, 0x0B);
    value = (uint16_t)(in8(PIC_MASTER_COMMAND) | (in8(PIC_SLAVE_COMMAND) << 8));
    out8(PIC_MASTER_COMMAND, 0x0A);
    out8(PIC_SLAVE_COMMAND, 0x0A);
    return value;
}
