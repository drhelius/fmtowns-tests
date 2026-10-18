#include "cpu.h"
#include "layout.h"
#include "pic.h"
#include "towns.h"

#define CATCH_DEPTH 4

typedef struct
{
    cpu_jmp_t jump;
    cpu_exception_t* exception;
} cpu_catch_t;

typedef struct __attribute__((packed))
{
    uint16_t limit;
    uint32_t base;
} cpu_table_pointer_t;

extern const uint32_t cpu_stub_table[256];
extern void cpu_load_tables(const void* gdtr, const void* idtr);
extern void cpu_double_fault_entry(void);

static uint64_t s_gdt[GDT_ENTRIES] SHELL_STATE __attribute__((aligned(8)));
static uint64_t s_idt[256] SHELL_STATE __attribute__((aligned(8)));
static uint32_t s_tss[26] SHELL_STATE;
static cpu_table_pointer_t s_gdtr SHELL_STATE;
static cpu_table_pointer_t s_idtr SHELL_STATE;
static cpu_handler_t s_handlers[256] SHELL_STATE;
static cpu_irq_handler_t s_irq_handlers[16] SHELL_STATE;
static uint32_t s_unexpected[256] SHELL_STATE;
static cpu_catch_t s_catch[CATCH_DEPTH] SHELL_STATE;
static int s_catch_depth SHELL_STATE;
static cpu_jmp_t s_test_jump SHELL_STATE;
static int s_test_active SHELL_STATE;
static cpu_fault_t s_fault SHELL_STATE;
static uint32_t s_df_tss[26] SHELL_STATE;
static uint32_t s_df_stack[512] SHELL_STATE;
static cpu_frame_t s_df_frame SHELL_STATE;

void cpu_dispatch(cpu_frame_t* frame);
void cpu_double_fault(void);
static uint64_t descriptor(uint32_t base, uint32_t limit, uint8_t access, uint8_t flags);
static uint64_t gate(uint16_t selector, uint32_t offset, uint8_t type);
static void record_fault(cpu_frame_t* frame);
static int frame_has_stack(const cpu_frame_t* frame);

// Fresh tables every time, so whatever a test changed or marked busy is gone
void cpu_reset(void)
{
    uint32_t cr0 = 0x00000001 | (g_boot_cr0 & 0x00000010);

    for (int i = 0; i < GDT_ENTRIES; i++)
        s_gdt[i] = 0;

    s_gdt[SEL_CODE32 >> 3] = descriptor(0, 0xFFFFF, 0x9A, 0xC);
    s_gdt[SEL_DATA32 >> 3] = descriptor(0, 0xFFFFF, 0x92, 0xC);
    s_gdt[SEL_CODE16 >> 3] = descriptor(0, 0xFFFF, 0x9A, 0x0);
    s_gdt[SEL_DATA16 >> 3] = descriptor(0, 0xFFFF, 0x92, 0x0);
    s_gdt[SEL_TSS >> 3] = descriptor((uint32_t)s_tss, sizeof(s_tss) - 1, 0x89, 0x0);

    for (int i = 0; i < 26; i++)
        s_tss[i] = 0;

    s_tss[1] = RING0_STACK_TOP;
    s_tss[2] = SEL_DATA32;
    s_tss[25] = (uint32_t)sizeof(s_tss) << 16;

    // A double fault switches to its own task and stack, so a broken stack or segment is reported, not a reset
    for (int i = 0; i < 26; i++)
        s_df_tss[i] = 0;

    s_df_tss[8] = (uint32_t)cpu_double_fault_entry;
    s_df_tss[9] = 0x00000002;
    s_df_tss[14] = (uint32_t)&s_df_stack[512];
    s_df_tss[18] = SEL_DATA32;
    s_df_tss[19] = SEL_CODE32;
    s_df_tss[20] = SEL_DATA32;
    s_df_tss[21] = SEL_DATA32;
    s_df_tss[22] = SEL_DATA32;
    s_df_tss[23] = SEL_DATA32;
    s_df_tss[25] = (uint32_t)sizeof(s_df_tss) << 16;
    s_gdt[SEL_DF_TSS >> 3] = descriptor((uint32_t)s_df_tss, sizeof(s_df_tss) - 1, 0x89, 0x0);

    for (int i = 0; i < 256; i++)
    {
        s_idt[i] = gate(SEL_CODE32, cpu_stub_table[i], CPU_GATE_INTERRUPT);
        s_handlers[i] = 0;
    }

    s_idt[CPU_VECTOR_DF] = gate(SEL_DF_TSS, 0, CPU_GATE_TASK);

    for (int i = 0; i < 16; i++)
        s_irq_handlers[i] = 0;

    s_gdtr.limit = sizeof(s_gdt) - 1;
    s_gdtr.base = (uint32_t)s_gdt;
    s_idtr.limit = sizeof(s_idt) - 1;
    s_idtr.base = (uint32_t)s_idt;
    s_catch_depth = 0;

    write_cr0(cr0);
    write_cr3(0);
    __asm__ volatile ("movl %0, %%dr7" : : "r"(0) : "memory");
    cpu_load_tables(&s_gdtr, &s_idtr);
    write_eflags(0x00000002);
}

uint32_t cpu_gdt_base(void)
{
    return (uint32_t)s_gdt;
}

uint32_t cpu_idt_base(void)
{
    return (uint32_t)s_idt;
}

uint32_t cpu_tss_base(void)
{
    return (uint32_t)s_tss;
}

void cpu_set_descriptor(uint16_t selector, uint32_t base, uint32_t limit, uint8_t access, uint8_t flags)
{
    s_gdt[(selector >> 3) & (GDT_ENTRIES - 1)] = descriptor(base, limit, access, flags);
}

void cpu_set_gate(int vector, uint16_t selector, uint32_t offset, uint8_t type)
{
    s_idt[vector & 0xFF] = gate(selector, offset, type);
}

void cpu_restore_gate(int vector)
{
    s_idt[vector & 0xFF] = gate(SEL_CODE32, cpu_stub_table[vector & 0xFF], CPU_GATE_INTERRUPT);
}

void cpu_set_handler(int vector, cpu_handler_t handler)
{
    s_handlers[vector & 0xFF] = handler;
}

void cpu_set_irq_handler(int irq, cpu_irq_handler_t handler)
{
    s_irq_handlers[irq & 0x0F] = handler;
}

int cpu_catch(void (*function)(void*), void* argument, cpu_exception_t* exception)
{
    volatile int depth = s_catch_depth;

    if (depth >= CATCH_DEPTH)
        return -1;

    s_catch[depth].exception = exception;
    s_catch_depth = depth + 1;

    if (cpu_setjmp(&s_catch[depth].jump) == 0)
    {
        function(argument);
        s_catch_depth = depth;
        return 0;
    }

    s_catch_depth = depth;
    return (int)exception->vector + 1;
}

int cpu_run_test(void* function, void* data, uint32_t steps)
{
    s_catch_depth = 0;

    if (cpu_setjmp(&s_test_jump) != 0)
    {
        s_test_active = 0;
        s_catch_depth = 0;
        return 1;
    }

    s_test_active = 1;
    cpu_call_on_stack(function, (uint32_t)data, steps, TEST_STACK_TOP);
    s_test_active = 0;
    return 0;
}

const cpu_fault_t* cpu_last_fault(void)
{
    return &s_fault;
}

void cpu_clear_fault(void)
{
    s_fault.valid = 0;
}

uint32_t cpu_unexpected_count(int vector)
{
    return s_unexpected[vector & 0xFF];
}

void cpu_clear_unexpected(void)
{
    for (int i = 0; i < 256; i++)
        s_unexpected[i] = 0;
}

void cpu_dispatch(cpu_frame_t* frame)
{
    uint32_t vector = frame->vector & 0xFF;

    if (vector >= CPU_VECTOR_IRQ0 && vector < CPU_VECTOR_IRQ0 + 16)
    {
        int irq = (int)(vector - CPU_VECTOR_IRQ0);

        if (s_irq_handlers[irq] != 0)
        {
            s_irq_handlers[irq](irq);
            return;
        }

        s_unexpected[vector]++;
        pic_mask(irq);
        pic_eoi(irq);
        return;
    }

    if (s_handlers[vector] != 0 && s_handlers[vector](frame))
        return;

    if (vector == CPU_VECTOR_NMI || vector >= 32)
    {
        s_unexpected[vector]++;
        return;
    }

    if (s_catch_depth > 0)
    {
        cpu_catch_t* current = &s_catch[s_catch_depth - 1];
        cpu_exception_t* exception = current->exception;
        exception->vector = vector;
        exception->error = frame->error;
        exception->eip = frame->eip;
        exception->cs = frame->cs;
        exception->eflags = frame->eflags;
        exception->cr2 = read_cr2();
        exception->eax = frame->eax;
        exception->ecx = frame->ecx;
        exception->edx = frame->edx;
        exception->ebx = frame->ebx;
        exception->esp = frame_has_stack(frame) ? frame->esp : (uint32_t)&frame->esp;
        exception->esi = frame->esi;
        exception->edi = frame->edi;
        cpu_longjmp(&current->jump, 1);
    }

    record_fault(frame);

    if (s_test_active)
        cpu_longjmp(&s_test_jump, 1);

    for (;;)
        __asm__ volatile ("hlt");
}

static void record_fault(cpu_frame_t* frame)
{
    s_fault.valid = 1;
    s_fault.frame = *frame;
    s_fault.cr0 = read_cr0();
    s_fault.cr2 = read_cr2();
    s_fault.cr3 = read_cr3();
    s_fault.stack_address = frame_has_stack(frame) ? frame->esp : (uint32_t)&frame->esp;

    if (!frame_has_stack(frame))
    {
        uint16_t ss;
        __asm__ volatile ("movw %%ss, %0" : "=r"(ss));
        s_fault.frame.esp = s_fault.stack_address;
        s_fault.frame.ss = ss;
    }

    for (int i = 0; i < 8; i++)
        s_fault.stack[i] = ((const volatile uint32_t*)s_fault.stack_address)[i];
}

// The faulting context is in the TSS the task switch saved it to, the one the back link names
void cpu_double_fault(void)
{
    uint64_t entry = s_gdt[(s_df_tss[0] >> 3) & (GDT_ENTRIES - 1)];
    const uint32_t* tss = (const uint32_t*)(uint32_t)(((entry >> 16) & 0x00FFFFFF) | ((entry >> 32) & 0xFF000000));

    s_df_frame.vector = CPU_VECTOR_DF;
    s_df_frame.error = 0;
    s_df_frame.eip = tss[8];
    s_df_frame.eflags = tss[9];
    s_df_frame.eax = tss[10];
    s_df_frame.ecx = tss[11];
    s_df_frame.edx = tss[12];
    s_df_frame.ebx = tss[13];
    s_df_frame.esp = tss[14];
    s_df_frame.ebp = tss[15];
    s_df_frame.esi = tss[16];
    s_df_frame.edi = tss[17];
    s_df_frame.es = tss[18];
    s_df_frame.cs = tss[19];
    s_df_frame.ss = tss[20];
    s_df_frame.ds = tss[21];
    s_df_frame.fs = tss[22];
    s_df_frame.gs = tss[23];
    cpu_dispatch(&s_df_frame);
}

static int frame_has_stack(const cpu_frame_t* frame)
{
    return (frame->cs & 3) != 0 || frame == &s_df_frame;
}

static uint64_t descriptor(uint32_t base, uint32_t limit, uint8_t access, uint8_t flags)
{
    uint64_t value = limit & 0xFFFF;
    value |= (uint64_t)(base & 0xFFFFFF) << 16;
    value |= (uint64_t)access << 40;
    value |= (uint64_t)((limit >> 16) & 0x0F) << 48;
    value |= (uint64_t)(flags & 0x0F) << 52;
    value |= (uint64_t)(base >> 24) << 56;
    return value;
}

static uint64_t gate(uint16_t selector, uint32_t offset, uint8_t type)
{
    uint64_t value = offset & 0xFFFF;
    value |= (uint64_t)selector << 16;
    value |= (uint64_t)type << 40;
    value |= (uint64_t)(offset >> 16) << 48;
    return value;
}
