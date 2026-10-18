#ifndef CPU_H
#define CPU_H

#include <stdint.h>

#define CPU_VECTOR_DE 0
#define CPU_VECTOR_DB 1
#define CPU_VECTOR_NMI 2
#define CPU_VECTOR_BP 3
#define CPU_VECTOR_UD 6
#define CPU_VECTOR_NM 7
#define CPU_VECTOR_DF 8
#define CPU_VECTOR_TS 10
#define CPU_VECTOR_NP 11
#define CPU_VECTOR_SS 12
#define CPU_VECTOR_GP 13
#define CPU_VECTOR_PF 14
#define CPU_VECTOR_MF 16
#define CPU_VECTOR_AC 17
#define CPU_VECTOR_IRQ0 0x40

#define CPU_GATE_INTERRUPT 0x8E
#define CPU_GATE_TRAP 0x8F
#define CPU_GATE_TASK 0x85

typedef struct
{
    uint32_t gs;
    uint32_t fs;
    uint32_t es;
    uint32_t ds;
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp_unused;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    uint32_t vector;
    uint32_t error;
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
    uint32_t esp;
    uint32_t ss;
} cpu_frame_t;

typedef struct
{
    uint32_t ebx;
    uint32_t esi;
    uint32_t edi;
    uint32_t ebp;
    uint32_t esp;
    uint32_t eip;
    uint32_t eflags;
} cpu_jmp_t;

typedef struct
{
    uint32_t vector;
    uint32_t error;
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
    uint32_t cr2;
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t esi;
    uint32_t edi;
} cpu_exception_t;

typedef struct
{
    uint32_t valid;
    cpu_frame_t frame;
    uint32_t cr0;
    uint32_t cr2;
    uint32_t cr3;
    uint32_t stack_address;
    uint32_t stack[8];
} cpu_fault_t;

typedef int (*cpu_handler_t)(cpu_frame_t* frame);
typedef void (*cpu_irq_handler_t)(int irq);

int cpu_setjmp(cpu_jmp_t* jump);
void cpu_longjmp(cpu_jmp_t* jump, int value) __attribute__((noreturn));
void cpu_call_on_stack(void* function, uint32_t first, uint32_t second, uint32_t stack_top);

void cpu_reset(void);
uint32_t cpu_gdt_base(void);
uint32_t cpu_idt_base(void);
uint32_t cpu_tss_base(void);
void cpu_set_descriptor(uint16_t selector, uint32_t base, uint32_t limit, uint8_t access, uint8_t flags);
void cpu_set_gate(int vector, uint16_t selector, uint32_t offset, uint8_t type);
void cpu_restore_gate(int vector);
void cpu_set_handler(int vector, cpu_handler_t handler);
void cpu_set_irq_handler(int irq, cpu_irq_handler_t handler);
int cpu_catch(void (*function)(void*), void* argument, cpu_exception_t* exception);
int cpu_run_test(void* function, void* data, uint32_t steps);
const cpu_fault_t* cpu_last_fault(void);
void cpu_clear_fault(void);
uint32_t cpu_unexpected_count(int vector);
void cpu_clear_unexpected(void);

#endif /* CPU_H */
