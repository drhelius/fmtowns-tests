#include <stdarg.h>
#include <stddef.h>
#include "shell.h"
#include "baseline.h"
#include "boot.h"
#include "cpu.h"
#include "fmt.h"
#include "font.h"
#include "input.h"
#include "layout.h"
#include "machine.h"
#include "qr.h"
#include "runtime.h"
#include "timebase.h"
#include "towns.h"

#define BEACON_MAGIC 0x53544D46u
#define STATE_BOOT 0
#define STATE_MENU 1
#define STATE_RUNNING 2
#define STATE_RESULTS 3
#define STATE_SUMMARY 4
#define STATE_QR 5
#define QR_ORIGIN 8
#define QR_PIXELS 224
#define QR_CHUNK_HEADER 6
#define QR_CHUNK_MAX 700

// Where the emulator runners find the shell's state and the records, at the start of the record store
typedef struct
{
    uint32_t magic;
    uint8_t disc;
    uint8_t state;
    uint8_t current;
    uint8_t run_all_count;
    uint16_t run;
    uint16_t record_count;
    uint32_t records[SHELL_MAX_TESTS];
} shell_beacon_t;

typedef struct
{
    cpu_fault_t fault;
    char step[40];
    uint16_t leak_port;
    uint8_t leak_before;
    uint8_t leak_after;
    uint32_t unexpected;
    uint8_t unexpected_vector;
    uint8_t leak;
} shell_info_t;

extern uint8_t __data_start[];
extern uint8_t __data_end[];
extern uint8_t __bss_start[];
extern uint8_t __bss_end[];

static const disc_t* s_disc SHELL_STATE;
static const test_t* s_tests[SHELL_MAX_TESTS] SHELL_STATE;
static int s_count SHELL_STATE;
static record_header_t* s_records[SHELL_MAX_TESTS] SHELL_STATE;
static shell_info_t s_info[SHELL_MAX_TESTS] SHELL_STATE;
static baseline_snapshot_t s_reference SHELL_STATE;
static baseline_snapshot_t s_snapshot SHELL_STATE;
static int s_screen_mode SHELL_STATE;
static int s_cursor SHELL_STATE;
static int s_reverse SHELL_STATE;
static uint16_t s_run SHELL_STATE;
static int s_qr_scale SHELL_STATE;
static int s_qr_mask SHELL_STATE;
static int s_current SHELL_STATE;
static uint8_t s_status SHELL_STATE;
static char s_step[40] SHELL_STATE;
static qr_code_t s_qr SHELL_STATE;
static char s_qr_text[QR_CHUNK_MAX * 2] SHELL_STATE;
static uint8_t s_qr_chunk[QR_CHUNK_MAX] SHELL_STATE;

static shell_beacon_t* const s_beacon = (shell_beacon_t*)STORE_BASE;

static int find_test(const char* number);
static void setup_records(void);
static void prepare(void);
static void cleanup(int index);
static void run_test(int index, uint32_t steps, int clear);
static uint32_t automatic_steps(const test_t* test);
static void menu(void);
static void draw_menu(void);
static int menu_items(void);
static void open_item(int item);
static void open_test(int index);
static int confirm(const test_t* test, const char* what, int risky);
static void run_all(void);
static void view_results(int index);
static void view_summary(void);
static void view_qr(int first, int last);
static void draw_qr_page(int first, int last, int part, int parts, int chunk, int version);
static uint32_t stream_size(int first, int last);
static void stream_read(int first, int last, uint32_t offset, uint8_t* output, uint32_t size);
static void draw_header(const char* number, const char* title, int page, int pages);
static void draw_footer(const char* hints);
static void draw_fault(int index);
static void draw_status(int index);
static void reboot(void);
static const char* status_name(uint8_t status);
static uint8_t status_color(uint8_t status);
static void toggle_screen(void);

void shell_main(const disc_t* disc)
{
    machine_capture();
    crc32_init();
    machine_identify();
    font_init();
    cpu_reset();
    tb_detect();

    s_disc = disc;
    s_tests[0] = &g_boot_test;
    s_count = 1;

    for (int i = 0; i < disc->count && s_count < SHELL_MAX_TESTS; i++)
        s_tests[s_count++] = disc->tests[i];

    s_screen_mode = SCREEN_MODE_15K;
    s_qr_scale = 2;
    s_qr_mask = 2;
    s_current = -1;
    setup_records();

    prepare();
    baseline_take(&s_reference);
    run_test(0, 1, 1);
    s_beacon->state = STATE_BOOT;
    view_results(0);
    menu();
    reboot();

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
        scr_printf(SCREEN_LEFT, SHELL_STATUS_ROW, COLOR_YELLOW, "RUNNING %s %s", s_tests[s_current]->number, s_step);

    scr_present();
}

void shell_set_status(uint8_t status)
{
    if (status > s_status)
        s_status = status;
}

const void* shell_result(const char* number, uint32_t* steps)
{
    int index = find_test(number);

    if (index < 0 || s_records[index]->status == RECORD_STATUS_NONE)
        return 0;

    if (steps != 0)
        *steps = s_records[index]->steps;

    return s_records[index] + 1;
}

// A line longer than the safe area is cut there, with a mark past its edge for the runners' safe area check
void shell_line(int row, uint8_t color, const char* format, ...)
{
    char text[SCREEN_SAFE_COLUMNS + 1];
    va_list arguments;
    va_start(arguments, format);
    int length = fmt_vformat(text, sizeof(text), format, arguments);
    va_end(arguments);
    scr_text(SCREEN_LEFT, row, color, text);

    if (length > SCREEN_SAFE_COLUMNS)
        scr_text(SCREEN_RIGHT + 1, row, COLOR_RED, ">");
}

static int find_test(const char* number)
{
    for (int i = 0; i < s_count; i++)
    {
        const char* a = s_tests[i]->number;
        const char* b = number;

        while (*a != 0 && *a == *b)
        {
            a++;
            b++;
        }

        if (*a == 0 && *b == 0)
            return i;
    }

    return -1;
}

static void setup_records(void)
{
    uint32_t address = STORE_BASE + sizeof(shell_beacon_t);

    s_beacon->magic = BEACON_MAGIC;
    s_beacon->disc = s_disc->number;
    s_beacon->state = STATE_BOOT;
    s_beacon->current = 0xFF;
    s_beacon->run_all_count = 0;
    s_beacon->run = 0;
    s_beacon->record_count = (uint16_t)s_count;

    for (int i = 0; i < s_count; i++)
    {
        address = (address + 3) & ~3u;
        s_records[i] = (record_header_t*)address;
        s_beacon->records[i] = address;
        memset(s_records[i], 0, sizeof(record_header_t));
        address += sizeof(record_header_t) + s_tests[i]->size;

        if (address > STORE_END)
        {
            s_count = i;
            s_beacon->record_count = (uint16_t)s_count;
            break;
        }
    }
}

// The aseptic sequence: the tests' data back to pristine, fresh descriptor tables and every device at its baseline
static void prepare(void)
{
    memcpy(__data_start, (const void*)PRISTINE_BASE, (size_t)(__data_end - __data_start));
    memset(__bss_start, 0, (size_t)(__bss_end - __bss_start));
    cpu_reset();
    cpu_clear_unexpected();
    baseline_apply(s_screen_mode);
    input_reset();
}

static void cleanup(int index)
{
    shell_info_t* info = &s_info[index];

    info->unexpected = 0;
    info->unexpected_vector = 0;

    for (int vector = 0; vector < 256; vector++)
    {
        uint32_t count = cpu_unexpected_count(vector);

        if (count != 0 && info->unexpected == 0)
            info->unexpected_vector = (uint8_t)vector;

        info->unexpected += count;
    }

    prepare();
    baseline_take(&s_snapshot);
    info->leak = (uint8_t)baseline_difference(&s_reference, &s_snapshot, &info->leak_port, &info->leak_before,
        &info->leak_after);
}

static void run_test(int index, uint32_t steps, int clear)
{
    const test_t* test = s_tests[index];
    record_header_t* record = s_records[index];
    uint8_t* data = (uint8_t*)(record + 1);
    shell_info_t* info = &s_info[index];

    if (clear || record->status == RECORD_STATUS_NONE)
    {
        memset(data, 0, test->size);
        record->steps = 0;
    }

    prepare();
    s_run++;
    s_current = index;
    s_status = RECORD_STATUS_OK;
    s_step[0] = 0;
    s_beacon->state = STATE_RUNNING;
    s_beacon->current = (uint8_t)index;
    s_beacon->run = s_run;
    info->fault.valid = 0;
    info->step[0] = 0;

    draw_header(test->number, test->name, 0, 0);
    shell_line(SHELL_BODY_TOP, COLOR_YELLOW, "RUNNING %s", test->number);
    draw_footer("PLEASE WAIT");
    scr_present();
    cpu_clear_fault();

    if (cpu_run_test((void*)test->run, data, steps) != 0)
    {
        info->fault = *cpu_last_fault();
        fmt_copy(info->step, s_step, sizeof(info->step));
        s_status = RECORD_STATUS_FAULT;
    }

    s_current = -1;
    cleanup(index);

    record->disc = s_disc->number;
    record->test = (uint8_t)index;
    record->version = test->version;
    record->status = s_status;
    record->machine_id = g_machine.id;
    record->time_base = (uint8_t)tb_source();
    record->leak = info->leak;
    record->build = g_machine.build;
    record->run = s_run;
    record->length = (uint16_t)test->size;
    record->steps |= steps;
    record_seal(record);
    s_beacon->current = 0xFF;
}

static uint32_t automatic_steps(const test_t* test)
{
    uint32_t steps = 0;

    if (test->steps == 0)
        return 1;

    for (int i = 0; test->steps[i].name != 0 && i < SHELL_MAX_STEPS; i++)
    {
        if ((test->steps[i].flags & (TEST_CONFIRM | TEST_RISKY)) == 0)
            steps |= 1u << i;
    }

    return steps;
}

static void menu(void)
{
    for (;;)
    {
        s_beacon->state = STATE_MENU;
        draw_menu();
        int event = input_wait();
        int items = menu_items();

        if ((event & INPUT_CHAR) != 0)
        {
            char c = (char)(event & 0xFF);

            if (c == 'A')
                s_cursor = s_count;
            else if (c == 'S')
                s_cursor = s_count + 1;
            else if (c == 'R')
                s_cursor = s_count + 2;
            else if (c >= '0' && c <= '9')
            {
                for (int i = 1; i < s_count; i++)
                {
                    const char* number = s_tests[i]->number;
                    int length = fmt_length(number);

                    if (number[length - 1] == c)
                        s_cursor = i;
                }
            }

            continue;
        }

        switch (event)
        {
            case INPUT_UP:
                s_cursor = (s_cursor + items - 1) % items;
                break;
            case INPUT_DOWN:
                s_cursor = (s_cursor + 1) % items;
                break;
            case INPUT_LEFT:
            case INPUT_RIGHT:
                if (s_cursor == s_count)
                    s_reverse = !s_reverse;
                else if (s_cursor < s_count && s_records[s_cursor]->status != RECORD_STATUS_NONE)
                    view_results(s_cursor);
                break;
            case INPUT_A:
                open_item(s_cursor);
                break;
            case INPUT_RUN:
                if (s_cursor < s_count && s_records[s_cursor]->status != RECORD_STATUS_NONE)
                    view_qr(s_cursor, s_cursor);
                else
                    view_qr(0, s_count - 1);
                break;
            case INPUT_SELECT:
                toggle_screen();
                break;
            default:
                break;
        }
    }
}

static int menu_items(void)
{
    return s_count + 3;
}

static void draw_menu(void)
{
    int row = SHELL_BODY_TOP;

    scr_clear();
    scr_printf(SCREEN_LEFT, SCREEN_TOP, COLOR_WHITE, "FMTT %u %s", s_disc->number, s_disc->name);
    scr_printf(SCREEN_RIGHT - 9, SCREEN_TOP, COLOR_WHITE, "%s %07X", s_disc->version, g_machine.build >> 4);
    scr_printf(SCREEN_LEFT, SCREEN_TOP + 1, COLOR_WHITE, "%04X %s %s %uM %s %s", g_machine.id, machine_model_name(g_machine.model),
        machine_cpu_name(g_machine.cpu), g_machine.ram_mb, tb_source() == TB_SOURCE_0026 ? "0026" : "PIT",
        scr_mode_name(s_screen_mode));
    scr_text(SCREEN_LEFT, SCREEN_TOP + 2, COLOR_GRAY, "--------------------------------------");

    for (int i = 0; i < s_count; i++)
    {
        const test_t* test = s_tests[i];
        const record_header_t* record = s_records[i];
        uint8_t color = COLOR_GRAY;
        const char* right = test->tags != 0 ? test->tags : "";

        if (record->status != RECORD_STATUS_NONE)
        {
            right = s_info[i].leak ? "LEAK" : status_name(record->status);
            color = s_info[i].leak ? COLOR_RED : status_color(record->status);
        }

        scr_printf(SCREEN_LEFT, row, i == s_cursor ? COLOR_YELLOW : COLOR_WHITE, "%c%-4s %-22s",
            i == s_cursor ? '>' : ' ', test->number, test->name);
        scr_printf(SCREEN_LEFT + 29, row, color, "%s", right);
        row++;
    }

    scr_printf(SCREEN_LEFT, row++, s_cursor == s_count ? COLOR_YELLOW : COLOR_WHITE, "%cA    RUN ALL AUTOMATIC %s",
        s_cursor == s_count ? '>' : ' ', s_reverse ? "REV" : "FWD");
    scr_printf(SCREEN_LEFT, row++, s_cursor == s_count + 1 ? COLOR_YELLOW : COLOR_WHITE, "%cS    SUMMARY",
        s_cursor == s_count + 1 ? '>' : ' ');
    scr_printf(SCREEN_LEFT, row++, s_cursor == s_count + 2 ? COLOR_YELLOW : COLOR_WHITE, "%cR    REBOOT",
        s_cursor == s_count + 2 ? '>' : ' ');
    scr_text(SCREEN_LEFT, row, COLOR_GRAY, "--------------------------------------");
    draw_footer("A RUN SEL SCREEN RUN QR");
}

static void open_item(int item)
{
    if (item < s_count)
        open_test(item);
    else if (item == s_count)
        run_all();
    else if (item == s_count + 1)
        view_summary();
    else if (confirm(0, "REBOOT THE MACHINE", 0))
        reboot();
}

static void open_test(int index)
{
    const test_t* test = s_tests[index];

    if (index == 0)
    {
        view_results(0);
        return;
    }

    if (test->steps == 0)
    {
        if ((test->flags & TEST_CONFIRM) != 0 && !confirm(test, test->tags, 0))
            return;

        run_test(index, 1, 1);
        view_results(index);
        return;
    }

    int cursor = 0;

    for (;;)
    {
        int count = 0;

        while (test->steps[count].name != 0)
            count++;

        draw_header(test->number, test->name, 0, 0);

        for (int i = 0; i <= count; i++)
        {
            const char* name = i == 0 ? "ALL STEPS" : test->steps[i - 1].name;
            uint32_t flags = i > 0 ? test->steps[i - 1].flags : 0;
            const char* tag = (flags & TEST_RISKY) != 0 ? "RISKY" : (flags & TEST_CONFIRM) != 0 ? test->tags : "";
            uint8_t color = i == cursor ? COLOR_YELLOW : COLOR_WHITE;
            scr_printf(SCREEN_LEFT, SHELL_BODY_TOP + i, color, "%c%-2u %-26s %s", i == cursor ? '>' : ' ', i, name,
                tag != 0 ? tag : "");
        }

        draw_footer("A RUN B BACK");
        int event = input_wait();

        if ((event & INPUT_CHAR) != 0 && (event & 0xFF) >= '0' && (event & 0xFF) <= '0' + count)
            cursor = (event & 0xFF) - '0';
        else if (event == INPUT_UP)
            cursor = (cursor + count) % (count + 1);
        else if (event == INPUT_DOWN)
            cursor = (cursor + 1) % (count + 1);
        else if (event == INPUT_B)
            return;
        else if (event == INPUT_SELECT)
            toggle_screen();
        else if (event == INPUT_A)
        {
            if (cursor == 0)
            {
                uint32_t steps = 0;

                for (int i = 0; i < count; i++)
                {
                    if ((test->steps[i].flags & TEST_RISKY) == 0)
                        steps |= 1u << i;
                }

                if (steps != automatic_steps(test) && !confirm(test, test->tags, 0))
                    continue;

                run_test(index, steps, 1);
                view_results(index);
                return;
            }

            uint32_t flags = test->steps[cursor - 1].flags;

            if ((flags & (TEST_CONFIRM | TEST_RISKY)) != 0 &&
                !confirm(test, test->steps[cursor - 1].name, (flags & TEST_RISKY) != 0))
                continue;

            run_test(index, 1u << (cursor - 1), 0);
            view_results(index);
            return;
        }
    }
}

static int confirm(const test_t* test, const char* what, int risky)
{
    draw_header(test != 0 ? test->number : "", test != 0 ? test->name : "CONFIRM", 0, 0);
    shell_line(SHELL_BODY_TOP, COLOR_YELLOW, "%s", risky ? "THIS STEP MAY HANG OR RESET THE" :
        "THIS STEP CHANGES STATE THAT SURVIVES");
    shell_line(SHELL_BODY_TOP + 1, COLOR_YELLOW, "%s", risky ? "MACHINE. IF IT DOES, NOTE WHERE." :
        "A RESET. IT PUTS IT BACK WHEN DONE.");
    shell_line(SHELL_BODY_TOP + 3, COLOR_WHITE, "%s", what != 0 ? what : "");
    shell_line(SHELL_BODY_TOP + 5, COLOR_WHITE, "PRESS A AGAIN TO GO ON, B TO CANCEL");
    draw_footer("A GO ON B CANCEL");

    for (;;)
    {
        int event = input_wait();

        if (event == INPUT_A)
            return 1;

        if (event == INPUT_B)
            return 0;
    }
}

static void run_all(void)
{
    for (int k = 1; k < s_count; k++)
    {
        int index = s_reverse ? s_count - k : k;
        const test_t* test = s_tests[index];

        if ((test->flags & TEST_RUN_ALL) == 0 || (test->flags & TEST_CONFIRM) != 0)
            continue;

        run_test(index, automatic_steps(test), 1);
    }

    s_beacon->run_all_count++;
    view_summary();
}

static void view_results(int index)
{
    const test_t* test = s_tests[index];
    const record_header_t* record = s_records[index];
    const void* data = record + 1;
    int fault = s_info[index].fault.valid ? 1 : 0;
    int pages = test->pages(data, record->steps) + fault;
    int page = 0;

    for (;;)
    {
        s_beacon->state = STATE_RESULTS;
        s_beacon->current = (uint8_t)index;
        draw_header(test->number, test->name, page + 1, pages);

        if (page < fault)
            draw_fault(index);
        else
            test->page(data, record->steps, page - fault);

        draw_status(index);
        draw_footer("<> PAGE A NEXT B MENU RUN QR");
        int event = input_wait();

        if (event == INPUT_LEFT && page > 0)
            page--;
        else if (event == INPUT_RIGHT && page + 1 < pages)
            page++;
        else if (event == INPUT_A)
        {
            if (page + 1 >= pages)
                return;

            page++;
        }
        else if (event == INPUT_B)
            return;
        else if (event == INPUT_RUN)
            view_qr(index, index);
        else if (event == INPUT_SELECT)
            toggle_screen();
    }
}

static void view_summary(void)
{
    for (;;)
    {
        int row = SHELL_BODY_TOP;
        s_beacon->state = STATE_SUMMARY;
        draw_header("", "SUMMARY", 0, 0);

        for (int i = 0; i < s_count && row <= SHELL_BODY_BOTTOM; i++)
        {
            const record_header_t* record = s_records[i];
            char line[40];
            uint8_t color = COLOR_GRAY;

            if (record->status == RECORD_STATUS_NONE)
            {
                scr_printf(SCREEN_LEFT, row++, COLOR_GRAY, "%-4s -", s_tests[i]->number);
                continue;
            }

            line[0] = 0;
            s_tests[i]->summary(record + 1, record->steps, line, &color);

            if (record->status != RECORD_STATUS_OK)
                color = status_color(record->status);

            if (s_info[i].leak)
            {
                fmt_format(line, sizeof(line), "LEAK %04X %02X>%02X", s_info[i].leak_port, s_info[i].leak_before,
                    s_info[i].leak_after);
                color = COLOR_RED;
            }

            scr_printf(SCREEN_LEFT, row, COLOR_WHITE, "%-4s %s", s_tests[i]->number,
                record->status == RECORD_STATUS_OK ? "" : status_name(record->status));
            scr_printf(SCREEN_LEFT + (record->status == RECORD_STATUS_OK ? 5 : 13), row++, color, "%s", line);
        }

        draw_footer("B MENU SEL SCREEN RUN QR");
        int event = input_wait();

        if (event == INPUT_B || event == INPUT_A)
            return;

        if (event == INPUT_RUN)
            view_qr(0, s_count - 1);
        else if (event == INPUT_SELECT)
            toggle_screen();
    }
}

// The records of the range back to back, split over QR codes that each start with a six-byte part header:
// 51h, disc, first record, part, parts, run
static void view_qr(int first, int last)
{
    int part = 0;

    s_beacon->state = STATE_QR;

    for (;;)
    {
        int version = qr_largest_version(QR_PIXELS, s_qr_scale);
        int chunk = (qr_capacity(version) * 5) / 8 - QR_CHUNK_HEADER;
        uint32_t total = stream_size(first, last);

        if (chunk > QR_CHUNK_MAX - QR_CHUNK_HEADER)
            chunk = QR_CHUNK_MAX - QR_CHUNK_HEADER;

        int parts = (int)((total + (uint32_t)chunk - 1) / (uint32_t)chunk);

        if (parts == 0)
            return;

        if (part >= parts)
            part = parts - 1;

        draw_qr_page(first, last, part, parts, chunk, version);
        int event = input_wait();

        if (event == INPUT_LEFT && part > 0)
            part--;
        else if ((event == INPUT_RIGHT || event == INPUT_A) && part + 1 < parts)
            part++;
        else if (event == INPUT_UP || event == INPUT_DOWN)
            s_qr_mask = (s_qr_mask + (event == INPUT_UP ? 1 : 7)) & 7;
        else if (event == INPUT_RUN)
        {
            s_qr_scale = s_qr_scale >= 4 ? 2 : s_qr_scale + 1;
            part = 0;
        }
        else if (event == INPUT_B)
            return;
        else if (event == INPUT_SELECT)
            toggle_screen();
    }
}

static void draw_qr_page(int first, int last, int part, int parts, int chunk, int version)
{
    uint32_t total = stream_size(first, last);
    uint32_t offset = (uint32_t)part * (uint32_t)chunk;
    uint32_t size = total - offset < (uint32_t)chunk ? total - offset : (uint32_t)chunk;

    s_qr_chunk[0] = 0x51;
    s_qr_chunk[1] = s_disc->number;
    s_qr_chunk[2] = (uint8_t)(first == last ? first : 0xFF);
    s_qr_chunk[3] = (uint8_t)part;
    s_qr_chunk[4] = (uint8_t)parts;
    s_qr_chunk[5] = (uint8_t)s_run;
    stream_read(first, last, offset, s_qr_chunk + QR_CHUNK_HEADER, size);
    int length = base32_encode(s_qr_text, s_qr_chunk, (int)size + QR_CHUNK_HEADER);
    scr_clear();

    if (qr_encode(&s_qr, s_qr_text, length, version, s_qr_mask) != 0)
    {
        shell_line(SHELL_BODY_TOP, COLOR_RED, "QR ENCODING FAILED");
        return;
    }


    int scale = s_qr_scale;
    int pixels = (s_qr.size + 2 * QR_QUIET_ZONE) * scale;
    int x0 = QR_ORIGIN;
    int y0 = (SCREEN_HEIGHT - pixels) / 2;

    scr_fill(x0, y0, pixels, pixels, COLOR_WHITE);

    for (int y = 0; y < s_qr.size; y++)
    {
        for (int x = 0; x < s_qr.size; x++)
        {
            if ((s_qr.modules[y][x] & 1) != 0)
                scr_fill(x0 + (x + QR_QUIET_ZONE) * scale, y0 + (y + QR_QUIET_ZONE) * scale, scale, scale,
                    COLOR_BLACK);
        }
    }

    int column = (x0 + pixels + 15) / 8;
    int row = SCREEN_TOP;

    if (first == last)
        scr_printf(column, row++, COLOR_WHITE, "%s", s_tests[first]->number);
    else
        scr_text(column, row++, COLOR_WHITE, "ALL");

    scr_printf(column, row++, COLOR_WHITE, "QR %u/%u", part + 1, parts);
    scr_printf(column, row++, COLOR_GRAY, "V%u X%u", s_qr.version, scale);
    scr_printf(column, row++, COLOR_GRAY, "M%u R%u", s_qr.mask, s_run);
    scr_printf(column, row++, COLOR_GRAY, "%uB", size);
    row++;
    scr_text(column, row++, COLOR_GRAY, "<> PART");
    scr_text(column, row++, COLOR_GRAY, "^V MASK");
    scr_text(column, row++, COLOR_GRAY, "RUN SIZE");
    scr_text(column, row++, COLOR_GRAY, "B BACK");
}

static uint32_t stream_size(int first, int last)
{
    uint32_t total = 0;

    for (int i = first; i <= last; i++)
    {
        if (s_records[i]->status != RECORD_STATUS_NONE)
            total += sizeof(record_header_t) + s_records[i]->length;
    }

    return total;
}

static void stream_read(int first, int last, uint32_t offset, uint8_t* output, uint32_t size)
{
    uint32_t position = 0;

    for (int i = first; i <= last && size > 0; i++)
    {
        if (s_records[i]->status == RECORD_STATUS_NONE)
            continue;

        uint32_t length = sizeof(record_header_t) + s_records[i]->length;
        const uint8_t* bytes = (const uint8_t*)s_records[i];

        for (uint32_t j = 0; j < length && size > 0; j++, position++)
        {
            if (position >= offset)
            {
                *output++ = bytes[j];
                size--;
            }
        }
    }
}

static void draw_header(const char* number, const char* title, int page, int pages)
{
    scr_clear();
    scr_printf(SCREEN_LEFT, SCREEN_TOP, COLOR_WHITE, "%s %s", number, title);

    if (pages > 0)
        scr_printf(SCREEN_RIGHT - 4, SCREEN_TOP, COLOR_WHITE, "%2u/%-2u", page, pages);

    scr_printf(SCREEN_LEFT, SCREEN_TOP + 1, COLOR_GRAY, "%04X %s %uM %s %s %07X R%02u", g_machine.id,
        machine_cpu_name(g_machine.cpu), g_machine.ram_mb, tb_source() == TB_SOURCE_0026 ? "0026" : "PIT",
        scr_mode_name(s_screen_mode), g_machine.build >> 4, s_run);
    scr_text(SCREEN_LEFT, SCREEN_TOP + 2, COLOR_GRAY, "--------------------------------------");
}

static void draw_footer(const char* hints)
{
    scr_clear_rows(SHELL_FOOTER_ROW, SHELL_FOOTER_ROW);
    scr_printf(SCREEN_LEFT, SHELL_FOOTER_ROW, COLOR_GRAY, "CHK %04X %s", scr_checksum(0, SHELL_STATUS_ROW), hints);
}

static void draw_fault(int index)
{
    const shell_info_t* info = &s_info[index];
    const cpu_frame_t* frame = &info->fault.frame;
    int row = SHELL_BODY_TOP;

    shell_line(row++, COLOR_RED, "FAULT %02X ERROR %08X", frame->vector, frame->error);
    shell_line(row++, COLOR_WHITE, "IN %s", info->step);
    shell_line(row++, COLOR_WHITE, "CS:EIP %04X:%08X FL %08X", frame->cs & 0xFFFF, frame->eip, frame->eflags);
    shell_line(row++, COLOR_WHITE, "CR0 %08X CR2 %08X", info->fault.cr0, info->fault.cr2);
    shell_line(row++, COLOR_WHITE, "CR3 %08X SS:ESP %04X:%08X", info->fault.cr3, frame->ss & 0xFFFF, frame->esp);
    shell_line(row++, COLOR_WHITE, "EAX %08X EBX %08X", frame->eax, frame->ebx);
    shell_line(row++, COLOR_WHITE, "ECX %08X EDX %08X", frame->ecx, frame->edx);
    shell_line(row++, COLOR_WHITE, "ESI %08X EDI %08X", frame->esi, frame->edi);
    shell_line(row++, COLOR_WHITE, "EBP %08X DS %04X ES %04X", frame->ebp, frame->ds & 0xFFFF, frame->es & 0xFFFF);
    shell_line(row++, COLOR_WHITE, "FS %04X GS %04X", frame->fs & 0xFFFF, frame->gs & 0xFFFF);
    row++;
    shell_line(row++, COLOR_CYAN, "STACK AT %08X", info->fault.stack_address);

    for (int i = 0; i < 8; i += 2)
        shell_line(row++, COLOR_WHITE, "%08X %08X", info->fault.stack[i], info->fault.stack[i + 1]);

    row++;
    shell_line(row++, COLOR_GRAY, "THE TEST'S OWN PAGES FOLLOW");
}

static void draw_status(int index)
{
    const shell_info_t* info = &s_info[index];
    const record_header_t* record = s_records[index];

    scr_clear_rows(SHELL_STATUS_ROW, SHELL_STATUS_ROW);

    if (info->leak)
        scr_printf(SCREEN_LEFT, SHELL_STATUS_ROW, COLOR_RED, "LEAK %04X %02X>%02X", info->leak_port, info->leak_before,
            info->leak_after);
    else if (info->unexpected != 0)
        scr_printf(SCREEN_LEFT, SHELL_STATUS_ROW, COLOR_RED, "UNEXPECTED INT %02X X%u", info->unexpected_vector,
            info->unexpected);
    else
        scr_printf(SCREEN_LEFT, SHELL_STATUS_ROW, status_color(record->status), "%s RUN %u STEPS %X",
            status_name(record->status), record->run, record->steps);
}

static void reboot(void)
{
    cli();
    out8(0x0020, 0x01);

    for (;;)
        __asm__ volatile ("hlt");
}

static const char* status_name(uint8_t status)
{
    switch (status)
    {
        case RECORD_STATUS_OK:
            return "OK";
        case RECORD_STATUS_FAULT:
            return "FAULT";
        case RECORD_STATUS_TIMEOUT:
            return "TIMEOUT";
        case RECORD_STATUS_SKIP:
            return "SKIP";
        default:
            return "";
    }
}

static uint8_t status_color(uint8_t status)
{
    switch (status)
    {
        case RECORD_STATUS_OK:
            return COLOR_GREEN;
        case RECORD_STATUS_FAULT:
            return COLOR_RED;
        case RECORD_STATUS_TIMEOUT:
            return COLOR_YELLOW;
        default:
            return COLOR_GRAY;
    }
}

static void toggle_screen(void)
{
    s_screen_mode = (s_screen_mode + 1) % SCREEN_MODE_COUNT;
    scr_init(s_screen_mode, g_machine.vram);
}
