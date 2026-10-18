#include "tests.h"
#include "pic.h"

// 1.9 Coprocessor and CPU identification (docs/12, HT-12, HT-33 to HT-35, HT-37, HT-39, HT-40)
// Each ESC runs inside a catch with its step on screen first, so a machine that hangs on it shows where. With no
// coprocessor answering FNINIT, FNSTSW and FNSTCW the waiting ones are skipped: FLD1 FADD FSTP hung the Marty
// (2026-10-18), and step 7 shows which of them hangs, each named for two seconds before it runs

#define CR0_MP 0x02u
#define CR0_EM 0x04u
#define CR0_TS 0x08u
#define CR0_NE 0x20u
#define PATTERN 0xA5
#define PROBE 0x5A5A
#define TIMING_REPEATS 8
#define LIMIT_OFFSETS 9
#define LIMIT_SELECTOR SEL_FIRST_FREE
#define HANG_PROBES 5
#define HANG_NOTICE_US 2000000u

#define STEP_IDENTITY 0x01
#define STEP_ESC 0x02
#define STEP_MP 0x04
#define STEP_TIMING 0x08
#define STEP_FPU 0x10
#define STEP_486 0x20
#define STEP_HANG 0x40

typedef struct
{
    uint8_t fninit;
    uint8_t fnstsw_m16;
    uint8_t fnstcw_m16;
    uint8_t fnstsw_ax;
    uint8_t fadd;
    uint8_t fstp32;
    uint8_t fstp64;
    uint8_t fstp80;
    uint8_t fnsave;
    uint8_t wait;
    uint8_t skipped;
    uint8_t reserved;
    uint16_t status_m16;
    uint16_t control_m16;
    uint16_t status_ax;
    uint8_t fadd64[8];
    uint8_t m32[4];
    uint8_t m64[8];
    uint8_t m80[10];
    uint8_t save[108];
} esc_run_t;

typedef struct
{
    uint32_t boot_cr0;
    uint32_t cr0;
    uint32_t eflags;
    uint8_t cpu_type;
    uint8_t ac_toggles;
    uint8_t id_toggles;
    uint8_t fpu_present;
    esc_run_t esc[2];
    uint8_t nm_em;
    uint8_t nm_ts;
    uint8_t nm_wait;
    uint8_t timed;
    uint16_t ticks_fninit;
    uint16_t ticks_fnstsw;
    uint16_t ticks_nop;
    uint8_t error_first;
    uint8_t error_level;
    uint8_t error_before_wait;
    uint8_t error_wait_vector;
    uint8_t limit_vector[LIMIT_OFFSETS];
    uint8_t limit_bytes[LIMIT_OFFSETS][10];
    uint8_t ne0_first;
    uint8_t ne0_level;
    uint8_t ne0_timeout;
    uint8_t cmpxchg[4];
    uint8_t cpuid_run;
    uint8_t test_registers_vector;
    uint32_t cpuid0[4];
    uint32_t cpuid1[4];
    uint32_t test_registers[11];
    uint8_t hang_reached;
    uint8_t hang_vector[HANG_PROBES];
    uint8_t reserved[2];
    uint8_t hang_bytes[8];
} fpu_id_t;

typedef struct
{
    void (*function)(void*);
    void* argument;
} call_t;

extern void fpu_fninit(void);
extern void fpu_fnstsw_m16(void*), fpu_fnstcw_m16(void*), fpu_fnstsw_ax(void*), fpu_fadd_fstp64(void*);
extern void fpu_fstp32(void*), fpu_fstp64(void*), fpu_fstp80(void*), fpu_fnsave(void*), fpu_wait(void*);
extern void fpu_fld1(void*), fpu_fadd_m64(void*), fpu_fstp_m64(void*);
extern uint32_t fpu_time(int kind);
extern void fpu_divide_zero(void*);
extern void fpu_fstp80_at(void*);
extern void cpu_cmpxchg_b0(void*), cpu_cmpxchg_b1(void*), cpu_cmpxchg_a6(void*), cpu_cmpxchg_a7(void*);
extern void cpu_cpuid(uint32_t leaf, uint32_t* out);
extern void cpu_test_registers(void*);

static const test_step_t k_steps[] =
{
    { "IDENTITY, NO ESC", 0 },
    { "ESC WITH EM 0 MP 0", 0 },
    { "MP 1, THEN EM, TS AND WAIT", 0 },
    { "TIMING", 0 },
    { "FPU ERRORS AND LIMITS", 0 },
    { "486 ONLY", 0 },
    { "NO FPU: WHICH ESC HANGS", TEST_RISKY },
    { 0, 0 }
};

static volatile int s_first;
static volatile int s_level;
static volatile int s_before_wait;
static volatile int s_marker;

static void run(void* data, uint32_t steps);
static int pages(const void* data, uint32_t steps);
static void page(const void* data, uint32_t steps, int number);
static void summary(const void* data, uint32_t steps, char* line, uint8_t* color);
static uint8_t caught(void (*function)(void*), void* argument);
static void fninit_call(void* argument);
static void esc_sequence(esc_run_t* run);
static int fpu_answers(const esc_run_t* run);
static void hang_probe(fpu_id_t* result);
static int toggles(uint32_t bit);
static void error_irq(int irq);
static int error_handler(cpu_frame_t* frame);
static void fpu_errors(fpu_id_t* result);
static void fpu_limits(fpu_id_t* result);
static void cpu486(fpu_id_t* result);
static int bytes_changed(const uint8_t* bytes, int count);

static inline void load_es(uint16_t selector)
{
    __asm__ volatile ("movw %0, %%es" : : "r"(selector) : "memory");
}

const test_t g_test_fpu_id =
{
    "1.9", "FPU AND CPU ID", 0, TEST_RUN_ALL, 2, sizeof(fpu_id_t), k_steps, run, pages, page, summary
};

static void run(void* data, uint32_t steps)
{
    fpu_id_t* result = (fpu_id_t*)data;
    uint32_t cr0 = read_cr0() & ~(CR0_MP | CR0_EM | CR0_TS);

    shell_step("STEP 1/7 IDENTITY");
    result->boot_cr0 = g_boot.cr0;
    result->cr0 = read_cr0();
    result->eflags = read_eflags();
    result->cpu_type = g_machine.cpu;
    result->ac_toggles = (uint8_t)toggles(1u << 18);
    result->id_toggles = (uint8_t)toggles(1u << 21);

    if ((steps & STEP_ESC) != 0)
    {
        write_cr0(cr0);
        esc_sequence(&result->esc[0]);
        result->fpu_present = (uint8_t)fpu_answers(&result->esc[0]);
    }

    if ((steps & STEP_MP) != 0)
    {
        write_cr0(cr0 | CR0_MP);
        esc_sequence(&result->esc[1]);
        shell_step("STEP 3/7 FNINIT WITH EM 1");
        write_cr0(cr0 | CR0_EM);
        result->nm_em = caught(fninit_call, 0);
        shell_step("STEP 3/7 FNINIT WITH TS 1");
        write_cr0(cr0 | CR0_TS);
        result->nm_ts = caught(fninit_call, 0);
        shell_step("STEP 3/7 WAIT WITH MP 1 TS 1");
        write_cr0(cr0 | CR0_MP | CR0_TS);
        result->nm_wait = caught(fpu_wait, 0);
        write_cr0(cr0);
    }

    if ((steps & STEP_TIMING) != 0)
    {
        shell_step("STEP 4/7 TIMING 64 FNINIT");
        write_cr0(cr0);
        result->timed = 1;
        result->ticks_fninit = 0xFFFF;
        result->ticks_fnstsw = 0xFFFF;
        result->ticks_nop = 0xFFFF;

        for (int i = 0; i < TIMING_REPEATS; i++)
        {
            uint16_t fninit = (uint16_t)fpu_time(0);
            uint16_t fnstsw = (uint16_t)fpu_time(1);
            uint16_t nop = (uint16_t)fpu_time(2);

            if (fninit < result->ticks_fninit)
                result->ticks_fninit = fninit;

            if (fnstsw < result->ticks_fnstsw)
                result->ticks_fnstsw = fnstsw;

            if (nop < result->ticks_nop)
                result->ticks_nop = nop;
        }

        tb_sync();
    }

    if ((steps & STEP_FPU) != 0 && result->fpu_present)
    {
        write_cr0(cr0 | CR0_MP);
        fpu_errors(result);
        fpu_limits(result);
    }

    if ((steps & STEP_486) != 0 && result->ac_toggles)
        cpu486(result);

    if ((steps & STEP_HANG) != 0)
    {
        write_cr0(cr0);
        hang_probe(result);
    }

    write_cr0(cr0);
}

static void call_function(void* argument)
{
    call_t* call = (call_t*)argument;
    call->function(call->argument);
}

// The vector plus one if the call faulted
static uint8_t caught(void (*function)(void*), void* argument)
{
    cpu_exception_t exception;
    call_t call = { function, argument };
    int vector = cpu_catch(call_function, &call, &exception);
    return (uint8_t)(vector > 0 ? vector : 0);
}

static void fninit_call(void* argument)
{
    (void)argument;
    fpu_fninit();
}

// The stores land over known patterns, so the bytes that changed show what the coprocessor bus returned
static void esc_sequence(esc_run_t* run)
{
    uint16_t word;

    shell_step("STEP 2/7 FNINIT");
    run->fninit = caught(fninit_call, 0);
    shell_step("STEP 2/7 FNSTSW M16");
    word = PROBE;
    run->fnstsw_m16 = caught(fpu_fnstsw_m16, &word);
    run->status_m16 = word;
    shell_step("STEP 2/7 FNSTCW M16");
    word = PROBE;
    run->fnstcw_m16 = caught(fpu_fnstcw_m16, &word);
    run->control_m16 = word;
    shell_step("STEP 2/7 FNSTSW AX");
    word = PROBE;
    run->fnstsw_ax = caught(fpu_fnstsw_ax, &word);
    run->status_ax = word;

    if (!fpu_answers(run))
    {
        run->skipped = 1;
        return;
    }

    shell_step("STEP 2/7 FLD1 FADD FSTP M64");
    memset(run->fadd64, PATTERN, sizeof(run->fadd64));
    run->fadd = caught(fpu_fadd_fstp64, run->fadd64);
    shell_step("STEP 2/7 FSTP M32");
    memset(run->m32, PATTERN, sizeof(run->m32));
    run->fstp32 = caught(fpu_fstp32, run->m32);
    shell_step("STEP 2/7 FSTP M64");
    memset(run->m64, PATTERN, sizeof(run->m64));
    run->fstp64 = caught(fpu_fstp64, run->m64);
    shell_step("STEP 2/7 FSTP M80");
    memset(run->m80, PATTERN, sizeof(run->m80));
    run->fstp80 = caught(fpu_fstp80, run->m80);
    shell_step("STEP 2/7 FNSAVE");
    memset(run->save, PATTERN, sizeof(run->save));
    run->fnsave = caught(fpu_fnsave, run->save);
    shell_step("STEP 2/7 WAIT");
    run->wait = caught(fpu_wait, 0);
}

// The detection of the 486 PRM, 10.2: after FNINIT the status word stores as 0 and the control word as 037Fh
static int fpu_answers(const esc_run_t* run)
{
    return run->fnstsw_m16 == 0 && run->status_m16 == 0 && (run->control_m16 & 0x103F) == 0x003F;
}

static void hang_probe(fpu_id_t* result)
{
    static const char* const k_names[HANG_PROBES] = { "WAIT", "FLD1", "FADD M64", "FSTP M64", "FNSAVE" };
    static void (*const k_functions[HANG_PROBES])(void*) = { fpu_wait, fpu_fld1, fpu_fadd_m64, fpu_fstp_m64,
        fpu_fnsave };
    static uint8_t s_save[108];

    memset(result->hang_bytes, PATTERN, sizeof(result->hang_bytes));
    caught(fninit_call, 0);

    for (int i = 0; i < HANG_PROBES; i++)
    {
        shell_step("STEP 7/7 %s NEXT", k_names[i]);
        tb_delay(HANG_NOTICE_US);
        result->hang_reached = (uint8_t)(i + 1);
        result->hang_vector[i] = caught(k_functions[i], i == 4 ? (void*)s_save : (void*)result->hang_bytes);
    }

    result->hang_reached = HANG_PROBES + 1;
}

static int toggles(uint32_t bit)
{
    uint32_t original = read_eflags();
    write_eflags(original ^ bit);
    uint32_t changed = read_eflags();
    write_eflags(original);
    return ((original ^ changed) & bit) != 0;
}

static void error_irq(int irq)
{
    if (s_first == 0)
    {
        s_first = 0x40 + irq;
        s_level = irq;
        s_before_wait = !s_marker;
    }

    __asm__ volatile ("fnclex");
    pic_mask(irq);
    pic_eoi(irq);
}

static int error_handler(cpu_frame_t* frame)
{
    if (s_first == 0)
    {
        s_first = (int)frame->vector;
        s_before_wait = !s_marker;
    }

    __asm__ volatile ("fnclex");
    return 1;
}

// 1 / 0 with ZM clear leaves an error pending; whichever vector or IRQ level comes first, and whether it comes before
// the WAIT that should report it
static void fpu_errors(fpu_id_t* result)
{
    static uint16_t control = 0x037B;

    shell_step("STEP 5/7 DIVIDE BY ZERO, WAIT");
    s_first = 0;
    s_level = -1;
    s_marker = 0;

    for (int irq = 0; irq < 16; irq++)
        cpu_set_irq_handler(irq, error_irq);

    cpu_set_handler(CPU_VECTOR_MF, error_handler);
    cpu_set_handler(9, error_handler);
    pic_init(g_machine.id);

    for (int irq = 0; irq < 16; irq++)
    {
        if (irq != 11)
            pic_unmask(irq);
    }

    sti();
    caught(fpu_divide_zero, &control);
    tb_delay(1000);
    s_marker = 1;
    result->error_wait_vector = caught(fpu_wait, 0);
    tb_delay(1000);
    cli();
    pic_mask_all();
    result->error_first = (uint8_t)s_first;
    result->error_level = (uint8_t)s_level;
    result->error_before_wait = (uint8_t)s_before_wait;

    if (result->ac_toggles)
    {
        shell_step("STEP 5/7 NE 0 DIVIDE BY ZERO, WAIT");
        write_cr0(read_cr0() & ~CR0_NE);
        s_first = 0;
        s_level = -1;
        s_marker = 0;
        pic_init(g_machine.id);
        pic_unmask(0);

        for (int irq = 1; irq < 16; irq++)
        {
            if (irq != 11)
                pic_unmask(irq);
        }

        out8(0x0060, 0x81);
        sti();
        caught(fpu_divide_zero, &control);
        s_marker = 1;
        caught(fpu_wait, 0);
        cli();
        out8(0x0060, 0x80);
        pic_mask_all();
        result->ne0_first = (uint8_t)s_first;
        result->ne0_level = (uint8_t)s_level;
        result->ne0_timeout = s_level == 0;
        write_cr0(read_cr0() | CR0_NE);
    }

    __asm__ volatile ("fninit");
}

// FSTP m80 through a 16-byte segment, ending 0 to 8 bytes past its limit
static void fpu_limits(fpu_id_t* result)
{
    static uint8_t buffer[32];

    cpu_set_descriptor(LIMIT_SELECTOR, (uint32_t)buffer, 0x0F, 0x92, 0x4);
    load_es(LIMIT_SELECTOR);

    for (int k = 0; k < LIMIT_OFFSETS; k++)
    {
        uint32_t offset = 6 + (uint32_t)k;
        shell_step("STEP 5/7 FSTP M80 AT %u", offset);
        memset(buffer, PATTERN, sizeof(buffer));
        result->limit_vector[k] = caught(fpu_fstp80_at, (void*)offset);

        for (int i = 0; i < 10; i++)
            result->limit_bytes[k][i] = buffer[offset + i];
    }

    load_es(SEL_DATA32);
    __asm__ volatile ("fninit");
}

static void cpuid_call(void* argument)
{
    uint32_t* out = (uint32_t*)argument;
    cpu_cpuid(out[0], out);
}

static void cpu486(fpu_id_t* result)
{
    static void (*const k_cmpxchg[4])(void*) = { cpu_cmpxchg_b0, cpu_cmpxchg_b1, cpu_cmpxchg_a6, cpu_cmpxchg_a7 };

    for (int i = 0; i < 4; i++)
    {
        shell_step("STEP 6/7 CMPXCHG %u", i);
        result->cmpxchg[i] = caught(k_cmpxchg[i], 0);
    }

    if (result->id_toggles)
    {
        shell_step("STEP 6/7 CPUID");
        result->cpuid_run = 1;
        result->cpuid0[0] = 0;
        caught(cpuid_call, result->cpuid0);
        result->cpuid1[0] = 1;
        caught(cpuid_call, result->cpuid1);
    }

    shell_step("STEP 6/7 TR3-TR7");
    uint32_t* registers = result->test_registers;
    registers[0] = 0x12345678u;
    registers[1] = 0x00000000u;
    registers[2] = 0x00000000u;
    registers[6] = 0x00100000u | 0x10 | (2 << 2);
    registers[7] = 0x00400000u | 0x800 | 0x400 | 0x100 | 0x40;
    registers[8] = 0x00400000u | 0x800 | 0x01;
    write_cr0(read_cr0() | 0x40000000u);
    result->test_registers_vector = caught(cpu_test_registers, registers);
    write_cr0(read_cr0() & ~0x40000000u);
}

static int bytes_changed(const uint8_t* bytes, int count)
{
    int changed = 0;

    for (int i = 0; i < count; i++)
        changed += bytes[i] != PATTERN;

    return changed;
}

int fpu_id_present(const void* data)
{
    return ((const fpu_id_t*)data)->fpu_present;
}

// Only FNINIT, FNSTSW and FNSTCW, which a 386 with no coprocessor completes
int fpu_detect(void)
{
    esc_run_t run;
    uint16_t word;
    uint32_t cr0 = read_cr0();

    memset(&run, 0, sizeof(run));
    write_cr0(cr0 & ~(CR0_MP | CR0_EM | CR0_TS));
    run.fninit = caught(fninit_call, 0);
    word = PROBE;
    run.fnstsw_m16 = caught(fpu_fnstsw_m16, &word);
    run.status_m16 = word;
    word = PROBE;
    run.fnstcw_m16 = caught(fpu_fnstcw_m16, &word);
    run.control_m16 = word;
    write_cr0(cr0);
    return fpu_answers(&run);
}

static int pages(const void* data, uint32_t steps)
{
    (void)data;
    (void)steps;
    return 6;
}

static void page(const void* data, uint32_t steps, int number)
{
    const fpu_id_t* result = (const fpu_id_t*)data;
    int row = SHELL_BODY_TOP;

    if (number == 0)
    {
        shell_line(row++, COLOR_CYAN, "IDENTITY");
        shell_line(row++, COLOR_WHITE, "CR0 AT BOOT %08X NOW %08X", result->boot_cr0, result->cr0);
        shell_line(row++, COLOR_WHITE, "EFLAGS %08X CPU %s", result->eflags, machine_cpu_name(result->cpu_type));
        shell_line(row++, COLOR_WHITE, "AC TOGGLES %s, ID TOGGLES %s", result->ac_toggles ? "YES" : "NO",
            result->id_toggles ? "YES" : "NO");
        shell_line(row++, COLOR_WHITE, "BOOT EM %u MP %u ET %u", (result->boot_cr0 >> 2) & 1, (result->boot_cr0 >> 1) & 1,
            (result->boot_cr0 >> 4) & 1);
        row++;

        if ((steps & STEP_ESC) != 0)
        {
            const esc_run_t* run = &result->esc[0];
            shell_line(row++, COLOR_CYAN, "DETECTION (PRM486 10.2)");
            shell_line(row++, COLOR_WHITE, "FNSTSW %04X FNSTCW %04X", run->status_m16, run->control_m16);
            shell_line(row++, result->fpu_present ? COLOR_GREEN : COLOR_WHITE, "%s",
                result->fpu_present ? "FPU PRESENT" : "NO FPU");
        }

        if (result->timed)
        {
            uint32_t fninit = result->ticks_fninit > result->ticks_nop ? result->ticks_fninit - result->ticks_nop : 0;
            uint32_t fnstsw = result->ticks_fnstsw > result->ticks_nop ? result->ticks_fnstsw - result->ticks_nop : 0;
            row++;
            shell_line(row++, COLOR_CYAN, "64 OF EACH, PIT TICKS, NOP SUBTRACTED");
            shell_line(row++, COLOR_WHITE, "NOP %u FNINIT +%u FNSTSW AX +%u", result->ticks_nop, fninit, fnstsw);
            shell_line(row++, COLOR_WHITE, "CLK AT 16 MHZ: FNINIT %u FNSTSW %u", fninit * 625u * 16u / (192u * 64u),
                fnstsw * 625u * 16u / (192u * 64u));
        }

        return;
    }

    if (number == 1 || number == 2)
    {
        const esc_run_t* run = &result->esc[number - 1];

        if ((steps & (number == 1 ? STEP_ESC : STEP_MP)) == 0)
        {
            shell_line(row, COLOR_GRAY, "STEP NOT RUN");
            return;
        }

        shell_line(row++, COLOR_CYAN, "ESC WITH EM 0, MP %u: V = VECTOR+1", number - 1);
        shell_line(row++, COLOR_WHITE, "FNSTSW M16 %04X>%04X V%u%s", PROBE, run->status_m16, run->fnstsw_m16,
            run->status_m16 == PROBE ? " NO STORE" : "");
        shell_line(row++, COLOR_WHITE, "FNSTCW M16 %04X>%04X V%u%s", PROBE, run->control_m16, run->fnstcw_m16,
            run->control_m16 == PROBE ? " NO STORE" : "");
        shell_line(row++, COLOR_WHITE, "FNSTSW AX  %04X>%04X V%u%s", PROBE, run->status_ax, run->fnstsw_ax,
            run->status_ax == PROBE ? " NO STORE" : "");
        if (run->skipped)
        {
            shell_line(row++, COLOR_WHITE, "FNINIT V%u", run->fninit);
            row++;
            shell_line(row++, COLOR_YELLOW, "NO FPU ANSWERED: FLD, FADD, FSTP,");
            shell_line(row++, COLOR_YELLOW, "FNSAVE AND WAIT SKIPPED, THEY HANG");
            shell_line(row++, COLOR_YELLOW, "A MARTY. STEP 7 SHOWS WHICH ONE");
            return;
        }

        shell_line(row++, COLOR_WHITE, "FNINIT V%u WAIT V%u", run->fninit, run->wait);
        shell_line(row++, COLOR_WHITE, "FADD M64 %02X%02X%02X%02X%02X%02X%02X%02X V%u", run->fadd64[7], run->fadd64[6],
            run->fadd64[5], run->fadd64[4], run->fadd64[3], run->fadd64[2], run->fadd64[1], run->fadd64[0], run->fadd);
        shell_line(row++, COLOR_WHITE, "FSTP M32 %02X%02X%02X%02X V%u %u CHANGED", run->m32[3], run->m32[2], run->m32[1],
            run->m32[0], run->fstp32, bytes_changed(run->m32, 4));
        shell_line(row++, COLOR_WHITE, "FSTP M64 %02X%02X%02X%02X%02X%02X%02X%02X V%u", run->m64[7], run->m64[6],
            run->m64[5], run->m64[4], run->m64[3], run->m64[2], run->m64[1], run->m64[0], run->fstp64);
        shell_line(row++, COLOR_WHITE, "FSTP M80 %02X%02X%02X%02X%02X%02X%02X%02X%02X%02X V%u", run->m80[9],
            run->m80[8], run->m80[7], run->m80[6], run->m80[5], run->m80[4], run->m80[3], run->m80[2], run->m80[1],
            run->m80[0], run->fstp80);
        shell_line(row++, COLOR_WHITE, "FNSAVE V%u %u OF 108 CHANGED", run->fnsave, bytes_changed(run->save, 108));

        for (int i = 0; i < 28 && row <= SHELL_BODY_BOTTOM; i += 14)
            shell_line(row++, COLOR_WHITE, "%02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X", run->save[i],
                run->save[i + 1], run->save[i + 2], run->save[i + 3], run->save[i + 4], run->save[i + 5],
                run->save[i + 6], run->save[i + 7], run->save[i + 8], run->save[i + 9], run->save[i + 10],
                run->save[i + 11], run->save[i + 12], run->save[i + 13]);

        if (number == 2)
        {
            row++;
            shell_line(row++, COLOR_CYAN, "EXPECTING #NM (V8)");
            shell_line(row++, result->nm_em == 8 ? COLOR_GREEN : COLOR_YELLOW, "FNINIT EM 1 V%u", result->nm_em);
            shell_line(row++, result->nm_ts == 8 ? COLOR_GREEN : COLOR_YELLOW, "FNINIT TS 1 V%u", result->nm_ts);
            shell_line(row++, result->nm_wait == 8 ? COLOR_GREEN : COLOR_YELLOW, "WAIT MP 1 TS 1 V%u", result->nm_wait);
        }

        return;
    }

    if (number == 3)
    {
        if (!result->fpu_present)
        {
            shell_line(row++, COLOR_WHITE, "NO FPU: ERROR DELIVERY AND LIMIT");
            shell_line(row++, COLOR_WHITE, "STORES NEED A 387 OR A 486DX");
            return;
        }

        shell_line(row++, COLOR_CYAN, "1/0 WITH ZM CLEAR, THEN WAIT");
        shell_line(row++, COLOR_WHITE, "FIRST %02X LEVEL %d %s THE WAIT", result->error_first,
            (int)(int8_t)result->error_level, result->error_before_wait ? "BEFORE" : "AT");
        shell_line(row++, COLOR_WHITE, "WAIT CAUGHT V%u", result->error_wait_vector);

        if (result->ac_toggles)
            shell_line(row++, COLOR_WHITE, "NE 0: FIRST %02X LEVEL %d%s", result->ne0_first,
                (int)(int8_t)result->ne0_level, result->ne0_timeout ? " PIT TIMEOUT" : "");

        row++;
        shell_line(row++, COLOR_CYAN, "FSTP M80 PAST A 16-BYTE LIMIT");

        for (int k = 0; k < LIMIT_OFFSETS; k++)
        {
            const uint8_t* bytes = result->limit_bytes[k];
            shell_line(row++, COLOR_WHITE, "%2u V%-2u %02X%02X%02X%02X%02X%02X%02X%02X%02X%02X", 6 + k,
                result->limit_vector[k], bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7],
                bytes[8], bytes[9]);
        }

        return;
    }

    if (number == 5)
    {
        static const char* const k_names[HANG_PROBES] = { "WAIT", "FLD1", "FADD M64", "FSTP M64", "FNSAVE" };

        if ((steps & STEP_HANG) == 0)
        {
            shell_line(row, COLOR_GRAY, "STEP 7 NOT RUN");
            return;
        }

        shell_line(row++, COLOR_CYAN, "EM 0 MP 0, IN THIS ORDER: V = VECTOR+1");

        for (int i = 0; i < HANG_PROBES; i++)
            shell_line(row++, COLOR_WHITE, "%-9s %s V%u", k_names[i], result->hang_reached > i + 1 ? "DONE" :
                "NOT REACHED", result->hang_vector[i]);

        const uint8_t* bytes = result->hang_bytes;
        shell_line(row++, COLOR_WHITE, "M64 %02X%02X%02X%02X%02X%02X%02X%02X", bytes[7], bytes[6], bytes[5], bytes[4],
            bytes[3], bytes[2], bytes[1], bytes[0]);
        return;
    }

    if (!result->ac_toggles)
    {
        shell_line(row++, COLOR_WHITE, "386: AC DOESN'T TOGGLE, NO 486 ITEMS");
        return;
    }

    shell_line(row++, COLOR_CYAN, "486 ONLY");
    shell_line(row++, COLOR_WHITE, "CMPXCHG B0 V%u B1 V%u A6 V%u A7 V%u", result->cmpxchg[0], result->cmpxchg[1],
        result->cmpxchg[2], result->cmpxchg[3]);

    if (result->cpuid_run)
    {
        shell_line(row++, COLOR_WHITE, "CPUID 0 %08X %08X", result->cpuid0[0], result->cpuid0[1]);
        shell_line(row++, COLOR_WHITE, "        %08X %08X", result->cpuid0[2], result->cpuid0[3]);
        shell_line(row++, COLOR_WHITE, "CPUID 1 %08X %08X", result->cpuid1[0], result->cpuid1[1]);
        shell_line(row++, COLOR_WHITE, "        %08X %08X", result->cpuid1[2], result->cpuid1[3]);
    }
    else
        shell_line(row++, COLOR_WHITE, "ID DOESN'T TOGGLE, NO CPUID");

    const uint32_t* registers = result->test_registers;
    shell_line(row++, COLOR_WHITE, "TR3-5 V%u %08X %08X %08X", result->test_registers_vector, registers[3],
        registers[4], registers[5]);
    shell_line(row++, COLOR_WHITE, "TLB LOOKUP TR7 %08X TR6 %08X", registers[9], registers[10]);
}

static void summary(const void* data, uint32_t steps, char* line, uint8_t* color)
{
    const fpu_id_t* result = (const fpu_id_t*)data;
    (void)steps;
    fmt_format(line, 34, "%s SW %04X CW %04X NM %u%u%u", result->fpu_present ? "FPU" : "NO FPU", result->esc[0].status_m16,
        result->esc[0].control_m16, result->nm_em, result->nm_ts, result->nm_wait);
    *color = COLOR_WHITE;
}
