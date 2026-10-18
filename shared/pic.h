#ifndef PIC_H
#define PIC_H

#include <stdint.h>

#define PIC_MASTER_COMMAND 0x0000
#define PIC_MASTER_DATA 0x0002
#define PIC_SLAVE_COMMAND 0x0010
#define PIC_SLAVE_DATA 0x0012
#define PIC_CASCADE 7

void pic_init(uint16_t machine_id);
void pic_mask_all(void);
void pic_unmask(int irq);
void pic_mask(int irq);
void pic_eoi(int irq);
uint16_t pic_read_imr(void);
uint16_t pic_read_irr(void);
uint16_t pic_read_isr(void);

#endif /* PIC_H */
