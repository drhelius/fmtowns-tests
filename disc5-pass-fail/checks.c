#include <stdarg.h>
#include <stddef.h>
#include "checks.h"
#include "baseline.h"
#include "cpu.h"
#include "fmt.h"
#include "font.h"
#include "input.h"
#include "layout.h"
#include "record.h"
#include "runtime.h"
#include "screen.h"
#include "timebase.h"
#include "towns.h"

#define BEACON_MAGIC 0x53544D46u
#define STATE_RUNNING 2
#define STATE_RESULTS 3
#define STATE_SUMMARY 4
#define LIST_ROWS (SHELL_BODY_BOTTOM - SHELL_BODY_TOP)
#define SEPARATOR_ROW SHELL_BODY_BOTTOM
#define NAME_WIDTH 30
#define VERDICT_COLUMN (SCREEN_LEFT + 32)
#define MAX_LINES 160

#define ITEM_HEX 0
#define ITEM_VALUE 1
#define ITEM_RANGE 2
#define ITEM_TRUE 3
#define ITEM_SKIP 4
#define ITEM_SIGNED 5

#define VERDICT_NONE 0
#define VERDICT_PASS 1
#define VERDICT_SKIP 2
#define VERDICT_FAIL 3
#define VERDICT_FAULT 4

// Where the emulator runners find the shell's state and the records, as on the other discs
typedef struct
{
    uint32_t magic;
    uint8_t disc;
    uint8_t state;
    uint8_t current;
    uint8_t run_all_count;
    uint16_t run;
    uint16_t record_count;
    uint32_t records[CHECK_MAX_RUNS];
} beacon_t;

typedef struct
{
    const test_t* test;
    uint32_t steps;
    int used;
    record_header_t* record;
    cpu_fault_t fault;
    char step[40];
} run_t;

typedef struct
{
    const char* name;
    uint8_t kind;
    uint8_t pass;
    uint32_t got;
    uint32_t want;
    uint32_t maximum;
} item_t;

typedef struct
{
    char text[SCREEN_SAFE_COLUMNS + 1];
    uint8_t color;
    uint8_t verdict;
} line_t;

extern uint8_t __data_start[];
extern uint8_t __data_end[];
extern uint8_t __bss_start[];
extern uint8_t __bss_end[];

static const check_disc_t* s_disc SHELL_STATE;
static run_t s_runs[CHECK_MAX_RUNS] SHELL_STATE;
static int s_run_count SHELL_STATE;
static int8_t s_group_run[CHECK_MAX_GROUPS] SHELL_STATE;
static uint8_t s_page_first[CHECK_MAX_GROUPS + 1] SHELL_STATE;
static int s_pages SHELL_STATE;
static item_t s_items[CHECK_MAX_ITEMS] SHELL_STATE;
static int s_item_count SHELL_STATE;
static line_t s_lines[MAX_LINES] SHELL_STATE;
static int s_line_count SHELL_STATE;
static int s_screen_mode SHELL_STATE;
static int s_cursor SHELL_STATE;
static int s_current SHELL_STATE;
static uint16_t s_run SHELL_STATE;
static uint8_t s_status SHELL_STATE;
static char s_step[40] SHELL_STATE;

static beacon_t* const s_beacon = (beacon_t*)STORE_BASE;

static void setup(void);
static void paginate(void);
static void prepare(void);
static void run_all(void);
static void run_test(int index);
static void results(void);
static void view_group(int index);
static int evaluate(int index);
static void add_item(const char* name, uint8_t kind, int pass, uint32_t got, uint32_t want, uint32_t maximum);
static void build_lines(int index, int verdict);
static void add_line(uint8_t color, uint8_t verdict, const char* format, ...);
static int group_count(void);
static int is_section(int index);
static int applies(int index);
static int page_of(int index);
static int first_group(int page);
static int step_group(int index, int direction);
static void draw_header(const char* title, const char* subtitle, int page, int pages);
static void draw_progress(int current);
static void draw_list(int page);
static void draw_totals(void);
static void draw_footer(const char* hints);
static const char* verdict_name(int verdict);
static uint8_t verdict_color(int verdict);
static void toggle_screen(void);

void check_main(const check_disc_t* disc)
{
    machine_capture();
    crc32_init();
    machine_identify();
    font_init();
    cpu_reset();
    tb_detect();

    s_disc = disc;
    s_screen_mode = SCREEN_MODE_15K;
    s_current = -1;
    setup();
    paginate();
    s_cursor = first_group(0);

    run_all();
    results();

    for (;;)
        ;
}

void shell_step(const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    fmt_vformat(s_step, sizeof(s_step), format, arguments);
    va_end(arguments);
    scr_clear_rows(SHELL_STATUS_ROW, SHELL_STATUS_ROW);

    if (s_current >= 0)
        scr_printf(SCREEN_LEFT, SHELL_STATUS_ROW, COLOR_YELLOW, "RUNNING %s %s", s_runs[s_current].test->number,
            s_step);

    scr_present();
}

void shell_set_status(uint8_t status)
{
    if (status > s_status)
        s_status = status;
}

const void* shell_result(const char* number, uint32_t* steps)
{
    for (int i = 0; i < s_run_count; i++)
    {
        const char* a = s_runs[i].test->number;
        const char* b = number;

        while (*a != 0 && *a == *b)
        {
            a++;
            b++;
        }

        if (*a != 0 || *b != 0 || s_runs[i].record->status == RECORD_STATUS_NONE)
            continue;

        if (steps != 0)
            *steps = s_runs[i].record->steps;

        return s_runs[i].record + 1;
    }

    return 0;
}

void shell_line(int row, uint8_t color, const char* format, ...)
{
    char text[SCREEN_SAFE_COLUMNS + 1];
    va_list arguments;
    va_start(arguments, format);
    fmt_vformat(text, sizeof(text), format, arguments);
    va_end(arguments);
    scr_text(SCREEN_LEFT, row, color, text);
}

void check_hex(const char* name, uint32_t got, uint32_t want)
{
    add_item(name, ITEM_HEX, got == want, got, want, 0);
}

void check_value(const char* name, uint32_t got, uint32_t want)
{
    add_item(name, ITEM_VALUE, got == want, got, want, 0);
}

void check_range(const char* name, uint32_t got, uint32_t minimum, uint32_t maximum)
{
    add_item(name, ITEM_RANGE, got >= minimum && got <= maximum, got, minimum, maximum);
}

void check_signed(const char* name, int32_t got, int32_t minimum, int32_t maximum)
{
    add_item(name, ITEM_SIGNED, got >= minimum && got <= maximum, (uint32_t)got, (uint32_t)minimum, (uint32_t)maximum);
}

void check_true(const char* name, int pass)
{
    add_item(name, ITEM_TRUE, pass != 0, 0, 0, 0);
}

void check_skip(const char* name)
{
    add_item(name, ITEM_SKIP, 1, 0, 0, 0);
}

int check_marty(void)
{
    return g_machine.model == MODEL_MARTY;
}

int check_386(void)
{
    return g_machine.cpu == CPU_TYPE_386DX || g_machine.cpu == CPU_TYPE_386SX;
}

int check_386sx(void)
{
    return g_machine.cpu == CPU_TYPE_386SX;
}

// One run per test, with the steps every group of it needs, and its record in the store
static void setup(void)
{
    uint32_t address = STORE_BASE + sizeof(beacon_t);

    for (int i = 0; i < group_count(); i++)
    {
        const check_group_t* group = s_disc->groups[i];
        int run = -1;

        s_group_run[i] = -1;

        if (group->test == 0)
            continue;

        for (int j = 0; j < s_run_count; j++)
        {
            if (s_runs[j].test == group->test)
                run = j;
        }

        if (run < 0 && s_run_count < CHECK_MAX_RUNS)
        {
            run = s_run_count++;
            s_runs[run].test = group->test;
        }

        if (run >= 0)
        {
            s_group_run[i] = (int8_t)run;

            if (applies(i))
            {
                s_runs[run].steps |= group->steps;
                s_runs[run].used = 1;
            }
        }
    }

    s_beacon->magic = BEACON_MAGIC;
    s_beacon->disc = s_disc->number;
    s_beacon->state = STATE_RUNNING;
    s_beacon->current = 0xFF;
    s_beacon->run_all_count = 0;
    s_beacon->run = 0;

    for (int i = 0; i < s_run_count; i++)
    {
        address = (address + 3) & ~3u;

        if (address + sizeof(record_header_t) + s_runs[i].test->size > STORE_END)
        {
            s_run_count = i;
            break;
        }

        s_runs[i].record = (record_header_t*)address;
        s_beacon->records[i] = address;
        memset(s_runs[i].record, 0, sizeof(record_header_t));
        address += sizeof(record_header_t) + s_runs[i].test->size;
    }

    s_beacon->record_count = (uint16_t)s_run_count;

    for (int i = 0; i < group_count(); i++)
    {
        if (s_group_run[i] >= s_run_count)
            s_group_run[i] = -1;
    }
}

// A section title takes a blank row above it and never ends a page
static void paginate(void)
{
    int row = 0;

    s_pages = 0;
    s_page_first[0] = 0;

    for (int i = 0; i < group_count(); i++)
    {
        int section = is_section(i);
        int rows = section && row > 0 ? 2 : 1;

        if (row + rows + section > LIST_ROWS)
        {
            s_page_first[++s_pages] = (uint8_t)i;
            row = 0;
            rows = 1;
        }

        row += rows;
    }

    s_page_first[++s_pages] = (uint8_t)group_count();
}

// The aseptic sequence of the other discs, always on the 15 kHz screen the hardware results were taken on
static void prepare(void)
{
    memcpy(__data_start, (const void*)PRISTINE_BASE, (size_t)(__data_end - __data_start));
    memset(__bss_start, 0, (size_t)(__bss_end - __bss_start));
    cpu_reset();
    cpu_clear_unexpected();
    baseline_apply(SCREEN_MODE_15K);
    input_reset();
}

static void run_all(void)
{
    for (int i = 0; i < s_run_count; i++)
        s_runs[i].record->status = RECORD_STATUS_NONE;

    for (int i = 0; i < s_run_count; i++)
    {
        if (s_runs[i].used)
            run_test(i);
    }

    s_beacon->run_all_count++;

    if (s_screen_mode != SCREEN_MODE_15K)
        scr_init(s_screen_mode, g_machine.vram);
}

static void run_test(int index)
{
    run_t* run = &s_runs[index];
    const test_t* test = run->test;
    record_header_t* record = run->record;

    memset(record + 1, 0, test->size);
    record->status = RECORD_STATUS_NONE;
    prepare();
    s_run++;
    s_current = index;
    s_status = RECORD_STATUS_OK;
    s_step[0] = 0;
    s_beacon->state = STATE_RUNNING;
    s_beacon->current = (uint8_t)index;
    s_beacon->run = s_run;
    run->fault.valid = 0;
    run->step[0] = 0;

    draw_progress(index);
    draw_footer("PLEASE WAIT");
    scr_present();
    cpu_clear_fault();

    if (cpu_run_test((void*)test->run, record + 1, run->steps) != 0)
    {
        run->fault = *cpu_last_fault();
        fmt_copy(run->step, s_step, sizeof(run->step));
        s_status = RECORD_STATUS_FAULT;
    }

    s_current = -1;
    prepare();

    record->disc = s_disc->number;
    record->test = (uint8_t)index;
    record->version = test->version;
    record->status = s_status;
    record->machine_id = g_machine.id;
    record->time_base = (uint8_t)tb_source();
    record->leak = 0;
    record->build = g_machine.build;
    record->run = s_run;
    record->length = (uint16_t)test->size;
    record->steps = run->steps;
    record_seal(record);
    s_beacon->current = 0xFF;
}

static void results(void)
{
    for (;;)
    {
        int page = page_of(s_cursor);

        s_beacon->state = STATE_SUMMARY;
        draw_list(page);
        draw_footer("A INFO <> PAGE RUN AGAIN SEL SCREEN");
        int event = input_wait();

        if (event == INPUT_UP)
            s_cursor = step_group(s_cursor, -1);
        else if (event == INPUT_DOWN)
            s_cursor = step_group(s_cursor, 1);
        else if (event == INPUT_LEFT)
            s_cursor = first_group((page + s_pages - 1) % s_pages);
        else if (event == INPUT_RIGHT)
            s_cursor = first_group((page + 1) % s_pages);
        else if (event == INPUT_A)
            view_group(s_cursor);
        else if (event == INPUT_RUN)
            run_all();
        else if (event == INPUT_SELECT)
            toggle_screen();
    }
}

static void view_group(int index)
{
    int page = 0;

    for (;;)
    {
        const check_group_t* group = s_disc->groups[index];
        int verdict = evaluate(index);
        int failed = 0;
        int counted = 0;

        build_lines(index, verdict);

        for (int i = 0; i < s_item_count; i++)
        {
            if (s_items[i].kind != ITEM_SKIP)
            {
                counted++;
                failed += !s_items[i].pass;
            }
        }

        int pages = (s_line_count + LIST_ROWS - 1) / LIST_ROWS;

        if (pages == 0)
            pages = 1;

        if (page >= pages)
            page = pages - 1;

        s_beacon->state = STATE_RESULTS;
        s_beacon->current = (uint8_t)(s_group_run[index] >= 0 ? s_group_run[index] : 0xFF);
        char subtitle[SCREEN_SAFE_COLUMNS + 1];
        fmt_format(subtitle, sizeof(subtitle), "FROM %s %s", group->test->number, group->test->name);
        draw_header(group->name, subtitle, page + 1, pages);

        for (int i = page * LIST_ROWS; i < s_line_count && i < (page + 1) * LIST_ROWS; i++)
        {
            int row = SHELL_BODY_TOP + i - page * LIST_ROWS;
            scr_text(SCREEN_LEFT, row, s_lines[i].color, s_lines[i].text);

            if (s_lines[i].verdict != VERDICT_NONE)
                scr_text(VERDICT_COLUMN, row, verdict_color(s_lines[i].verdict), verdict_name(s_lines[i].verdict));
        }

        scr_text(SCREEN_LEFT, SEPARATOR_ROW, COLOR_GRAY, "--------------------------------------");

        if (verdict == VERDICT_PASS || verdict == VERDICT_FAIL)
            scr_printf(SCREEN_LEFT, SHELL_STATUS_ROW, verdict_color(verdict), "%s %u OF %u", verdict_name(verdict),
                verdict == VERDICT_PASS ? counted - failed : failed, counted);
        else
            scr_text(SCREEN_LEFT, SHELL_STATUS_ROW, verdict_color(verdict), verdict == VERDICT_NONE ? "NOT RUN" :
                verdict_name(verdict));

        draw_footer("B BACK <> PAGE ^V NEXT SEL SCREEN");
        int event = input_wait();

        if (event == INPUT_LEFT && page > 0)
            page--;
        else if (event == INPUT_RIGHT && page + 1 < pages)
            page++;
        else if (event == INPUT_UP || event == INPUT_DOWN)
        {
            index = step_group(index, event == INPUT_UP ? -1 : 1);
            s_cursor = index;
            page = 0;
        }
        else if (event == INPUT_A)
        {
            if (page + 1 >= pages)
                return;

            page++;
        }
        else if (event == INPUT_B)
            return;
        else if (event == INPUT_SELECT)
            toggle_screen();
    }
}

// Runs the group's checks on its record and gives its verdict: a fault, any failed item, else a pass if anything
// was checked
static int evaluate(int index)
{
    const check_group_t* group = s_disc->groups[index];
    int run = s_group_run[index];

    s_item_count = 0;

    if (run >= 0 && !applies(index))
        return VERDICT_SKIP;

    if (run < 0 || s_runs[run].record->status == RECORD_STATUS_NONE)
        return VERDICT_NONE;

    const record_header_t* record = s_runs[run].record;
    group->evaluate(record + 1, record->steps);

    if (record->status == RECORD_STATUS_FAULT)
        return VERDICT_FAULT;

    int verdict = VERDICT_SKIP;

    for (int i = 0; i < s_item_count; i++)
    {
        if (s_items[i].kind == ITEM_SKIP)
            continue;

        if (!s_items[i].pass)
            return VERDICT_FAIL;

        verdict = VERDICT_PASS;
    }

    return verdict;
}

static void add_item(const char* name, uint8_t kind, int pass, uint32_t got, uint32_t want, uint32_t maximum)
{
    if (s_item_count >= CHECK_MAX_ITEMS)
        return;

    item_t* item = &s_items[s_item_count++];
    item->name = name;
    item->kind = kind;
    item->pass = (uint8_t)(pass != 0);
    item->got = got;
    item->want = want;
    item->maximum = maximum;
}

static void build_lines(int index, int verdict)
{
    int run = s_group_run[index];

    s_line_count = 0;

    if (run >= 0 && !applies(index))
    {
        add_line(COLOR_GRAY, VERDICT_NONE, "NOT CHECKED ON THIS MACHINE:");
        add_line(COLOR_GRAY, VERDICT_NONE, "NO HARDWARE RESULT FOR IT YET");
        return;
    }

    if (verdict == VERDICT_FAULT)
    {
        const cpu_fault_t* fault = &s_runs[run].fault;
        add_line(COLOR_RED, VERDICT_NONE, "FAULT %02X ERROR %08X", fault->frame.vector, fault->frame.error);
        add_line(COLOR_WHITE, VERDICT_NONE, "CS:EIP %04X:%08X", fault->frame.cs & 0xFFFF, fault->frame.eip);
        add_line(COLOR_WHITE, VERDICT_NONE, "IN %s", s_runs[run].step);
        add_line(COLOR_WHITE, VERDICT_NONE, "");
    }
    else if (run >= 0 && s_runs[run].record->status == RECORD_STATUS_TIMEOUT)
    {
        add_line(COLOR_YELLOW, VERDICT_NONE, "A WAIT IN THE TEST TIMED OUT");
        add_line(COLOR_WHITE, VERDICT_NONE, "");
    }

    for (int i = 0; i < s_item_count; i++)
    {
        const item_t* item = &s_items[i];
        int item_verdict = item->kind == ITEM_SKIP ? VERDICT_SKIP : item->pass ? VERDICT_PASS : VERDICT_FAIL;

        add_line(COLOR_WHITE, (uint8_t)item_verdict, "%s", item->name);

        if (item->pass || item->kind == ITEM_TRUE || item->kind == ITEM_SKIP)
            continue;

        if (item->kind == ITEM_HEX)
        {
            uint32_t largest = item->got > item->want ? item->got : item->want;
            int digits = largest > 0xFFFF ? 8 : largest > 0xFF ? 4 : 2;
            char format[24];
            fmt_format(format, sizeof(format), "  GOT %%0%uX WANT %%0%uX", digits, digits);
            add_line(COLOR_GRAY, VERDICT_NONE, format, item->got, item->want);
        }
        else if (item->kind == ITEM_VALUE)
            add_line(COLOR_GRAY, VERDICT_NONE, "  GOT %u WANT %u", item->got, item->want);
        else if (item->kind == ITEM_SIGNED)
            add_line(COLOR_GRAY, VERDICT_NONE, "  GOT %d WANT %d TO %d", (int32_t)item->got, (int32_t)item->want,
                (int32_t)item->maximum);
        else
            add_line(COLOR_GRAY, VERDICT_NONE, "  GOT %u WANT %u-%u", item->got, item->want, item->maximum);
    }

    if (s_item_count == 0 && verdict != VERDICT_NONE)
        add_line(COLOR_GRAY, VERDICT_NONE, "NOTHING TO CHECK ON THIS MACHINE");
}

static void add_line(uint8_t color, uint8_t verdict, const char* format, ...)
{
    if (s_line_count >= MAX_LINES)
        return;

    line_t* line = &s_lines[s_line_count++];
    va_list arguments;
    va_start(arguments, format);
    fmt_vformat(line->text, verdict != VERDICT_NONE ? NAME_WIDTH + 1 : (int)sizeof(line->text), format, arguments);
    va_end(arguments);
    line->color = color;
    line->verdict = verdict;
}

static int group_count(void)
{
    return s_disc->count < CHECK_MAX_GROUPS ? s_disc->count : CHECK_MAX_GROUPS;
}

static int is_section(int index)
{
    return s_disc->groups[index]->test == 0;
}

static int applies(int index)
{
    const check_group_t* group = s_disc->groups[index];
    return group->applies == 0 || group->applies();
}

static int page_of(int index)
{
    for (int page = 0; page < s_pages; page++)
    {
        if (index < s_page_first[page + 1])
            return page;
    }

    return 0;
}

static int first_group(int page)
{
    for (int i = s_page_first[page]; i < group_count(); i++)
    {
        if (!is_section(i))
            return i;
    }

    return s_cursor;
}

static int step_group(int index, int direction)
{
    int count = group_count();

    for (int i = 1; i < count; i++)
    {
        int next = (index + direction * i + count) % count;

        if (!is_section(next))
            return next;
    }

    return index;
}

static void draw_header(const char* title, const char* subtitle, int page, int pages)
{
    scr_clear();
    scr_printf(SCREEN_LEFT, SCREEN_TOP, COLOR_WHITE, "%s", title);

    if (pages > 0)
        scr_printf(SCREEN_RIGHT - 4, SCREEN_TOP, COLOR_WHITE, "%2u/%-2u", page, pages);

    if (subtitle != 0)
        scr_text(SCREEN_LEFT, SCREEN_TOP + 1, COLOR_GRAY, subtitle);
    else
        scr_printf(SCREEN_LEFT, SCREEN_TOP + 1, COLOR_GRAY, "%04X %s %s %uM %s", g_machine.id,
            machine_model_name(g_machine.model), machine_cpu_name(g_machine.cpu), g_machine.ram_mb,
            scr_mode_name(s_screen_mode));
    scr_text(SCREEN_LEFT, SCREEN_TOP + 2, COLOR_GRAY, "--------------------------------------");
}

static void draw_progress(int current)
{
    int first = current > LIST_ROWS - 4 ? current - (LIST_ROWS - 4) : 0;
    char title[SCREEN_SAFE_COLUMNS + 1];

    fmt_format(title, sizeof(title), "FMTT %u %s", s_disc->number, s_disc->name);
    draw_header(title, 0, 0, 0);
    scr_printf(SCREEN_LEFT, SHELL_BODY_TOP, COLOR_YELLOW, "RUNNING TEST %u OF %u", current + 1, s_run_count);

    for (int i = first; i < s_run_count && i - first < LIST_ROWS - 2; i++)
    {
        const run_t* run = &s_runs[i];
        int row = SHELL_BODY_TOP + 2 + i - first;
        uint8_t status = run->record->status;
        const char* state = i == current ? "..." : !run->used ? "SKIP" : status == RECORD_STATUS_FAULT ? "FAULT" :
            status != RECORD_STATUS_NONE ? "DONE" : "";

        scr_printf(SCREEN_LEFT, row, i == current ? COLOR_YELLOW : COLOR_WHITE, " %-4s %-25s", run->test->number,
            run->test->name);
        scr_text(VERDICT_COLUMN, row, i == current ? COLOR_YELLOW : status == RECORD_STATUS_FAULT ? COLOR_RED :
            COLOR_GRAY, state);
    }
}

static void draw_list(int page)
{
    char title[SCREEN_SAFE_COLUMNS + 1];
    int row = 0;

    fmt_format(title, sizeof(title), "FMTT %u %s", s_disc->number, s_disc->name);
    draw_header(title, 0, page + 1, s_pages);

    for (int i = s_page_first[page]; i < s_page_first[page + 1]; i++)
    {
        const check_group_t* group = s_disc->groups[i];

        if (is_section(i))
        {
            row += row > 0 ? 1 : 0;
            scr_text(SCREEN_LEFT, SHELL_BODY_TOP + row++, COLOR_CYAN, group->name);
            continue;
        }

        int verdict = evaluate(i);
        scr_printf(SCREEN_LEFT, SHELL_BODY_TOP + row, i == s_cursor ? COLOR_YELLOW : COLOR_WHITE, "%c%s",
            i == s_cursor ? '>' : ' ', group->name);
        scr_text(VERDICT_COLUMN, SHELL_BODY_TOP + row++, verdict_color(verdict), verdict_name(verdict));
    }

    scr_text(SCREEN_LEFT, SEPARATOR_ROW, COLOR_GRAY, "--------------------------------------");
    draw_totals();
}

static void draw_totals(void)
{
    int counts[VERDICT_FAULT + 1] = { 0 };

    for (int i = 0; i < group_count(); i++)
    {
        if (!is_section(i))
            counts[evaluate(i)]++;
    }

    int failed = counts[VERDICT_FAIL] + counts[VERDICT_FAULT];
    scr_printf(SCREEN_LEFT, SHELL_STATUS_ROW, COLOR_GREEN, "PASS %u", counts[VERDICT_PASS]);
    scr_printf(SCREEN_LEFT + 10, SHELL_STATUS_ROW, failed != 0 ? COLOR_RED : COLOR_GRAY, "FAIL %u", failed);
    scr_printf(SCREEN_LEFT + 20, SHELL_STATUS_ROW, COLOR_GRAY, "SKIP %u", counts[VERDICT_SKIP]);
}

static void draw_footer(const char* hints)
{
    scr_clear_rows(SHELL_FOOTER_ROW, SHELL_FOOTER_ROW);
    scr_text(SCREEN_LEFT, SHELL_FOOTER_ROW, COLOR_GRAY, hints);
}

static const char* verdict_name(int verdict)
{
    switch (verdict)
    {
        case VERDICT_PASS:
            return "PASS";
        case VERDICT_SKIP:
            return "SKIP";
        case VERDICT_FAIL:
            return "FAIL";
        case VERDICT_FAULT:
            return "FAULT";
        default:
            return "-";
    }
}

static uint8_t verdict_color(int verdict)
{
    switch (verdict)
    {
        case VERDICT_PASS:
            return COLOR_GREEN;
        case VERDICT_FAIL:
        case VERDICT_FAULT:
            return COLOR_RED;
        default:
            return COLOR_GRAY;
    }
}

static void toggle_screen(void)
{
    s_screen_mode = (s_screen_mode + 1) % SCREEN_MODE_COUNT;
    scr_init(s_screen_mode, g_machine.vram);
}
