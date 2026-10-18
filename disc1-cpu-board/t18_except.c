#include "tests.h"

// 1.8 Exception edge cases (docs/12, HT-30, HT-42)

#define RM_BASE 0x00008000u
#define TRANSFERS 4
#define HT42_CASES 17
#define HT42_USER_CS 0x33
#define HT42_USER_SS 0x3B
#define HT42_PF_TSS 0x40
#define HT42_DF_TSS 0x48
#define HT42_NP_DATA 0x53
#define HT42_BAD_TSS 0x5B
#define HT42_NP_STACK 0x63
#define HT42_NP_TSS 0x6B
#define HT42_DPL0_TSS 0x73
#define HT42_RING0_STACK 0x00007000u
#define HT42_PF_STACK 0x0000B000u
#define HT42_DF_STACK 0x0000C000u
#define PAGE_RING0_STACK 6
#define PAGE_USER_NP 5
#define KIND_DIVIDE 0
#define KIND_TASK 1
#define KIND_NOT_PRESENT 2
#define KIND_STACK 3
#define KIND_PROTECTION 4
#define KIND_PAGE 5
#define KIND_UNDEFINED 6
#define KIND_INT3 7
#define KIND_TASK_NP 8
#define KIND_TASK_GP 9
#define KIND_KERNEL_PAGE 10
#define KIND_IDT_PAGE 11
#define KIND_GATE_NP 12
#define IDT_COPY_PAGE 0x0F
#define IDT_COPY_BASE (((uint32_t)IDT_COPY_PAGE << 12) - CPU_VECTOR_PF * 8)
#define GOT_GATE 0x80

typedef struct
{
    uint8_t vector;
    uint8_t done;
    uint8_t value;
    uint8_t reserved;
    uint32_t error;
} access_t;

typedef struct
{
    uint8_t task_vector;
    uint8_t caught_vector;
    uint8_t continued;
    uint8_t reserved;
    uint16_t task_tr;
    uint16_t back_link;
    uint16_t saved_cs;
    uint32_t error;
    uint32_t cr2;
    uint32_t saved_eip;
} ht42_t;

// Expected: 14 the #PF task, 8 the #DF task, GOT_GATE | vector the interrupt gate of that vector
typedef struct
{
    const char* name;
    uint8_t kind;
    uint8_t step;
    uint8_t pf_task_gate;
    uint8_t stack_present;
    uint8_t df_interrupt_gate;
    uint8_t expected;
} ht42_case_t;

typedef struct
{
    access_t writes[TRANSFERS];
    access_t reads[TRANSFERS];
    ht42_t cases[HT42_CASES];
    uint8_t paging_ok;
    uint8_t reserved[3];
} exceptions_t;

extern void rm_enter(void);
extern uint8_t rm_block[], rm_block_end[], rm_variant[], rm_access[], rm_read_value[], rm_data[];
extern void ht42_enter(uint32_t user_case, uint32_t page_directory);
extern void ht42_pf_task(void);
extern void ht42_df_task(void);
extern cpu_jmp_t* ht42_jump;
extern uint32_t ht42_task_vector, ht42_task_error, ht42_task_cr2, ht42_task_tr, ht42_continued;

static const char* const k_transfers[TRANSFERS] = { "FAR JMP", "FAR CALL", "RETF", "IRET" };

// The Intel table: a page fault while delivering a contributory or benign exception is delivered serially, a page
// fault while delivering a page fault is a double fault. Steps 3-12 are the controls, one each and in rising risk:
// on the Marty steps 7 to 10 reset the machine (2026-10-19). Steps 9 and 10 take the second #PF from the IDT entry
// of vector 0Eh, on a page that isn't present, with the stack present: the #DF goes to its task, or to its gate.
// Steps 11 and 12 raise #GP at CPL 0 with its gate not present, contributory then contributory, a #DF with no #PF
static const ht42_case_t k_cases[HT42_CASES] =
{
    { "#DE", KIND_DIVIDE, 1, 1, 0, 0, 14 },
    { "#TS", KIND_TASK, 1, 1, 0, 0, 14 },
    { "#NP", KIND_NOT_PRESENT, 1, 1, 0, 0, 14 },
    { "#SS", KIND_STACK, 1, 1, 0, 0, 14 },
    { "#GP", KIND_PROTECTION, 1, 1, 0, 0, 14 },
    { "TSS NP", KIND_TASK_NP, 1, 1, 0, 0, 14 },
    { "TSS GP", KIND_TASK_GP, 1, 1, 0, 0, 14 },
    { "#PF S", KIND_PAGE, 2, 0, 1, 0, GOT_GATE | 14 },
    { "#PF T", KIND_PAGE, 3, 1, 0, 0, 14 },
    { "#UD", KIND_UNDEFINED, 4, 1, 0, 0, 14 },
    { "INT3", KIND_INT3, 5, 1, 0, 0, 14 },
    { "#PF", KIND_PAGE, 6, 0, 0, 0, 8 },
    { "#PF 0", KIND_KERNEL_PAGE, 7, 0, 0, 0, 8 },
    { "#PF I", KIND_IDT_PAGE, 8, 0, 1, 0, 8 },
    { "#PF G", KIND_IDT_PAGE, 9, 0, 1, 1, GOT_GATE | 8 },
    { "#NP G", KIND_GATE_NP, 10, 0, 1, 1, GOT_GATE | 8 },
    { "#NP T", KIND_GATE_NP, 11, 0, 1, 0, 8 }
};

static uint32_t s_directory[1024] __attribute__((aligned(4096)));
static uint32_t s_table[1024] __attribute__((aligned(4096)));
static uint32_t s_tss_pf[26];
static uint32_t s_tss_df[26];
static cpu_jmp_t s_jump;

static void run(void* data, uint32_t steps);
static int pages(const void* data, uint32_t steps);
static void page(const void* data, uint32_t steps, int number);
static void summary(const void* data, uint32_t steps, char* line, uint8_t* color);
static void real_mode_call(void* argument);
static void transfer(int variant, int read, access_t* access);
static void task_state(uint32_t* tss, void (*entry)(void), uint32_t stack);
static void setup_case(const ht42_case_t* entry);
static void run_case(const ht42_case_t* entry, ht42_t* result);
static void case_call(void* argument);
static int outcome(const ht42_t* entry);
static int draw_cases(int row, const exceptions_t* result, uint32_t steps, int first_step, int last_step);

static const test_step_t k_steps[] =
{
    { "CS WRITES AFTER A FAR TRANSFER", 0 },
    { "PAGE FAULT DURING DELIVERY", 0 },
    { "#PF, RING-0 STACK PRESENT", TEST_RISKY },
    { "#PF THROUGH THE PF TASK", TEST_RISKY },
    { "CONTROL #UD", TEST_RISKY },
    { "CONTROL INT3", TEST_RISKY },
    { "CONTROL #PF, DF TASK", TEST_RISKY },
    { "#PF AT CPL 0, DF TASK", TEST_RISKY },
    { "#PF IN THE IDT, DF TASK", TEST_RISKY },
    { "#PF IN THE IDT, DF GATE", TEST_RISKY },
    { "GATE NOT PRESENT, DF GATE", TEST_RISKY },
    { "GATE NOT PRESENT, DF TASK", TEST_RISKY },
    { 0, 0 }
};

const test_t g_test_exceptions =
{
    "1.8", "EXCEPTIONS", 0, TEST_RUN_ALL, 2, sizeof(exceptions_t), k_steps, run, pages, page, summary
};

static void run(void* data, uint32_t steps)
{
    exceptions_t* result = (exceptions_t*)data;

    if ((steps & 0x01) != 0)
    {
        for (int variant = 0; variant < TRANSFERS; variant++)
        {
            shell_step("%s WRITE", k_transfers[variant]);
            transfer(variant, 0, &result->writes[variant]);
            shell_step("%s READ", k_transfers[variant]);
            transfer(variant, 1, &result->reads[variant]);
        }
    }

    for (int i = 0; i < HT42_CASES; i++)
    {
        if ((steps & (1u << k_cases[i].step)) == 0)
            continue;

        shell_step("%s DELIVERY FAULTS", k_cases[i].name);
        run_case(&k_cases[i], &result->cases[i]);
    }
}

static void real_mode_call(void* argument)
{
    (void)argument;
    rm_enter();
}

// The block runs at 08000h; its variant and access bytes are set in the copy
static void transfer(int variant, int read, access_t* access)
{
    uint32_t size = (uint32_t)(rm_block_end - rm_block);
    cpu_exception_t exception;

    memcpy((void*)RM_BASE, rm_block, size);
    MMIO8(RM_BASE + (uint32_t)(rm_variant - rm_block)) = (uint8_t)variant;
    MMIO8(RM_BASE + (uint32_t)(rm_access - rm_block)) = (uint8_t)read;
    MMIO8(RM_BASE + (uint32_t)(rm_read_value - rm_block)) = 0;

    int vector = cpu_catch(real_mode_call, 0, &exception);
    access->vector = (uint8_t)vector;
    access->error = vector > 0 ? exception.error : 0;
    access->done = vector == 0;
    access->value = read ? MMIO8(RM_BASE + (uint32_t)(rm_read_value - rm_block)) :
        MMIO8(RM_BASE + (uint32_t)(rm_data - rm_block));
    cpu_reset();
}

static void task_state(uint32_t* tss, void (*entry)(void), uint32_t stack)
{
    for (int i = 0; i < 26; i++)
        tss[i] = 0;

    tss[7] = (uint32_t)s_directory;
    tss[8] = (uint32_t)entry;
    tss[9] = 0x00000002;
    tss[14] = stack;
    tss[18] = SEL_DATA32;
    tss[19] = SEL_CODE32;
    tss[20] = SEL_DATA32;
    tss[21] = SEL_DATA32;
    tss[22] = SEL_DATA32;
    tss[23] = SEL_DATA32;
    tss[25] = 104u << 16;
}

// Identity map of the first 4 MiB, user accessible, with the ring-0 stack page and one user page not present
static void setup_case(const ht42_case_t* entry)
{
    uint32_t* main_tss = (uint32_t*)cpu_tss_base();

    for (int i = 0; i < 1024; i++)
    {
        s_directory[i] = 0;
        s_table[i] = ((uint32_t)i << 12) | 0x07;
    }

    s_directory[0] = (uint32_t)s_table | 0x07;
    s_table[PAGE_USER_NP] = 0;

    if (!entry->stack_present)
        s_table[PAGE_RING0_STACK] = 0;

    task_state(s_tss_pf, ht42_pf_task, HT42_PF_STACK);
    task_state(s_tss_df, ht42_df_task, HT42_DF_STACK);
    main_tss[1] = HT42_RING0_STACK;
    main_tss[2] = SEL_DATA32;

    cpu_set_descriptor(HT42_USER_CS, 0, 0xFFFFF, 0xFA, 0xC);
    cpu_set_descriptor(HT42_USER_SS, 0, 0xFFFFF, 0xF2, 0xC);
    cpu_set_descriptor(HT42_PF_TSS, (uint32_t)s_tss_pf, 103, 0x89, 0x0);
    cpu_set_descriptor(HT42_DF_TSS, (uint32_t)s_tss_df, 103, 0x89, 0x0);
    cpu_set_descriptor(HT42_NP_DATA, 0, 0xFFFFF, 0x72, 0xC);
    cpu_set_descriptor(HT42_BAD_TSS, (uint32_t)s_tss_pf, 0x40, 0xE9, 0x0);
    cpu_set_descriptor(HT42_NP_STACK, 0, 0xFFFFF, 0x72, 0xC);
    cpu_set_descriptor(HT42_NP_TSS, (uint32_t)s_tss_pf, 103, 0x69, 0x0);
    cpu_set_descriptor(HT42_DPL0_TSS, (uint32_t)s_tss_pf, 103, 0x89, 0x0);

    cpu_set_gate(CPU_VECTOR_DF, HT42_DF_TSS, 0, CPU_GATE_TASK);

    if (entry->pf_task_gate)
        cpu_set_gate(CPU_VECTOR_PF, HT42_PF_TSS, 0, CPU_GATE_TASK);
    else
        cpu_restore_gate(CPU_VECTOR_PF);

    if (entry->df_interrupt_gate)
        cpu_restore_gate(CPU_VECTOR_DF);

    if (entry->kind == KIND_IDT_PAGE)
    {
        memcpy((void*)IDT_COPY_BASE, (const void*)cpu_idt_base(), 256 * 8);
        s_table[IDT_COPY_PAGE] = 0;
    }

    if (entry->kind == KIND_GATE_NP)
    {
        uint64_t* idt = (uint64_t*)cpu_idt_base();
        idt[CPU_VECTOR_GP] &= ~((uint64_t)0x80 << 40);
    }

    if (entry->kind == KIND_INT3)
    {
        uint64_t* idt = (uint64_t*)cpu_idt_base();
        idt[CPU_VECTOR_BP] = (idt[CPU_VECTOR_BP] & ~((uint64_t)0xFF << 40)) | ((uint64_t)0xEE << 40);
    }
}

static void case_call(void* argument)
{
    const ht42_case_t* entry = (const ht42_case_t*)argument;

    if (cpu_setjmp(&s_jump) == 0)
    {
        if (entry->kind == KIND_IDT_PAGE)
        {
            struct __attribute__((packed)) { uint16_t limit; uint32_t base; } idtr = { 256 * 8 - 1, IDT_COPY_BASE };
            __asm__ volatile ("lidt %0" : : "m"(idtr));
        }

        ht42_enter(entry->kind, (uint32_t)s_directory);
    }
}

static void run_case(const ht42_case_t* entry, ht42_t* result)
{
    cpu_exception_t exception;

    setup_case(entry);
    ht42_jump = &s_jump;
    ht42_task_vector = 0;
    ht42_continued = 0;
    int vector = cpu_catch(case_call, (void*)entry, &exception);
    write_cr0(read_cr0() & 0x7FFFFFFFu);

    uint32_t* main_tss = (uint32_t*)cpu_tss_base();
    result->caught_vector = (uint8_t)vector;
    result->continued = (uint8_t)ht42_continued;
    result->task_vector = (uint8_t)ht42_task_vector;
    result->task_tr = (uint16_t)ht42_task_tr;

    if (ht42_task_vector != 0)
    {
        result->error = ht42_task_error;
        result->cr2 = ht42_task_cr2;
        result->back_link = (uint16_t)(ht42_task_vector == 14 ? s_tss_pf[0] : s_tss_df[0]);
        result->saved_eip = main_tss[8];
        result->saved_cs = (uint16_t)main_tss[19];
    }
    else if (vector > 0)
    {
        result->error = exception.error;
        result->cr2 = exception.cr2;
        result->saved_eip = exception.eip;
        result->saved_cs = (uint16_t)exception.cs;
    }

    cpu_reset();
}

static int outcome(const ht42_t* entry)
{
    if (entry->task_vector != 0)
        return entry->task_vector;

    return entry->caught_vector != 0 ? GOT_GATE | (entry->caught_vector - 1) : 0;
}

static int pages(const void* data, uint32_t steps)
{
    (void)data;
    (void)steps;
    return 3;
}

static int draw_cases(int row, const exceptions_t* result, uint32_t steps, int first_step, int last_step)
{
    shell_line(row++, COLOR_GRAY, "CASE   GOT CR2      ERROR LINK EIP");

    for (int i = 0; i < HT42_CASES; i++)
    {
        const ht42_case_t* entry = &k_cases[i];
        const ht42_t* got = &result->cases[i];

        if (entry->step < first_step || entry->step > last_step)
            continue;

        if ((steps & (1u << entry->step)) == 0)
        {
            shell_line(row++, COLOR_GRAY, "%-6s NOT RUN (STEP %u)", entry->name, entry->step + 1);
            continue;
        }

        int value = outcome(got);
        char text[4];

        if (value == 14 || value == 8)
            fmt_copy(text, value == 14 ? "PF" : "DF", sizeof(text));
        else if (value != 0)
            fmt_format(text, sizeof(text), "%02X", value & 0x7F);
        else
            fmt_copy(text, "--", sizeof(text));

        int expected = value == entry->expected && !got->continued;
        shell_line(row++, expected ? COLOR_GREEN : COLOR_YELLOW, "%-6s %-3s %08X %04X %04X %X", entry->name, text,
            got->cr2, got->error & 0xFFFF, got->back_link, got->saved_eip & 0xFFFFF);

        if (got->continued)
            shell_line(row++, COLOR_RED, "  DIDN'T FAULT");
    }

    return row;
}

static void page(const void* data, uint32_t steps, int number)
{
    const exceptions_t* result = (const exceptions_t*)data;
    int row = SHELL_BODY_TOP;

    if (number == 0)
    {
        if ((steps & 0x01) == 0)
        {
            shell_line(row, COLOR_GRAY, "STEP 1 NOT RUN");
            return;
        }

        shell_line(row++, COLOR_CYAN, "PE SET WITHOUT A FAR JUMP AFTER");
        shell_line(row++, COLOR_GRAY, "TRANSFER  WRITE CS:     READ CS:");

        for (int variant = 0; variant < TRANSFERS; variant++)
        {
            const access_t* write = &result->writes[variant];
            const access_t* read = &result->reads[variant];
            char write_text[16];
            char read_text[16];

            if (write->vector != 0)
                fmt_format(write_text, sizeof(write_text), "#%02X %04X", write->vector - 1, write->error & 0xFFFF);
            else
                fmt_format(write_text, sizeof(write_text), "OK %02X", write->value);

            if (read->vector != 0)
                fmt_format(read_text, sizeof(read_text), "#%02X %04X", read->vector - 1, read->error & 0xFFFF);
            else
                fmt_format(read_text, sizeof(read_text), "OK %02X", read->value);

            shell_line(row++, COLOR_WHITE, "%-9s %-12s %-10s", k_transfers[variant], write_text, read_text);
        }

        row++;
        shell_line(row++, COLOR_GRAY, "OK 5A = THE WRITE LANDED");
        shell_line(row++, COLOR_GRAY, "#XX EEEE = VECTOR AND ERROR CODE");
        return;
    }

    if (number == 1)
    {
        shell_line(row++, COLOR_CYAN, "RING-0 STACK NOT PRESENT, CPL 3");
        row = draw_cases(row, result, steps, 1, 1);
        row++;
        shell_line(row++, COLOR_GRAY, "GOT: PF OR DF TASK, XX = GATE XX");
        shell_line(row++, COLOR_GRAY, "TSS NP/GP: JMP TO A NOT-PRESENT OR");
        shell_line(row++, COLOR_GRAY, " A DPL 0 TSS");
        shell_line(row++, COLOR_GRAY, "INTEL TABLE: ALL PF, SERIAL");
        shell_line(row++, COLOR_GRAY, "GREEN = AS THE INTEL TABLE SAYS");
        return;
    }

    shell_line(row++, COLOR_CYAN, "CONTROLS, A RISKY STEP EACH");
    row = draw_cases(row, result, steps, 2, 11);
    shell_line(row++, COLOR_GRAY, "#PF S: STACK PRESENT, EXPECT GATE 0E");
    shell_line(row++, COLOR_GRAY, "#PF T: THE #PF GATE IS THE PF TASK");
    shell_line(row++, COLOR_GRAY, "#PF: STACK NOT PRESENT, EXPECT DF");
    shell_line(row++, COLOR_GRAY, "#PF 0: CPL 0, ESP IN THE MISSING PAGE");
    shell_line(row++, COLOR_GRAY, "#PF I: IDT ENTRY 0E MISSING, DF TASK");
    shell_line(row++, COLOR_GRAY, "#PF G: THE SAME, DF IS A GATE");
    shell_line(row++, COLOR_GRAY, "#NP G: #GP GATE MISSING, DF IS A GATE");
    shell_line(row++, COLOR_GRAY, "#NP T: THE SAME, DF IS A TASK");
    shell_line(row++, COLOR_GRAY, "GREEN = AS THE INTEL TABLE SAYS");
}

static void summary(const void* data, uint32_t steps, char* line, uint8_t* color)
{
    const exceptions_t* result = (const exceptions_t*)data;
    int matches = 0;
    int cases = 0;

    for (int i = 0; i < HT42_CASES; i++)
    {
        if ((steps & (1u << k_cases[i].step)) == 0)
            continue;

        cases++;
        matches += outcome(&result->cases[i]) == k_cases[i].expected && !result->cases[i].continued;
    }

    fmt_format(line, 34, "JMP W %s R %s TABLE %u/%u", result->writes[0].vector ? "GP" : "OK",
        result->reads[0].vector ? "GP" : "OK", matches, cases);
    *color = matches == cases ? COLOR_WHITE : COLOR_YELLOW;
}
