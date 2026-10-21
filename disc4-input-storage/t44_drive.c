#include "tests.h"

// 4.4 Floppy drive signals (docs/12, HT-28, HT-6, HT-41), drive 0. 0208h is read with the drive selected; 0200h after
// a forced end, so it carries the type I meanings. Swaps and DSKCHG need someone at the drive; the masked request
// works with or without a disk; the motor, index and seek steps want a disk in, and say so when there is none

#define CHANGES 48
#define SWAP_US 30000000u
#define POLL_US 1000u
#define REQUEST_CASES 6
#define REQUEST_CHANGES 8
#define REQUEST_US 1000000u
#define REQUEST_POLL_US 10
#define PULSES 8
#define SPIN_LIMIT_US 3000000u
#define SPIN_DOWN_US 2000000u
#define RATES 4
#define HEAD_LOADS 3
#define DSKCHG_READS 6
#define STATUS_MASK 0xC4
#define SEEK_TRACK 10
#define NEVER 0xFFFFFFFFu

typedef struct
{
    uint32_t time;
    uint8_t drive;
    uint8_t status;
    uint8_t reserved[2];
} drive_change_t;

typedef struct
{
    uint8_t count;
    uint8_t ran;
    uint8_t first_drive;
    uint8_t first_status;
    drive_change_t changes[CHANGES];
} swap_log_t;

typedef struct
{
    uint32_t time;
    uint8_t value;
    uint8_t reserved[3];
} request_change_t;

typedef struct
{
    uint8_t count;
    uint8_t status;
    uint8_t after_status;
    uint8_t after_isr;
    uint8_t after_clear;
    uint8_t first;
    uint8_t reserved[2];
    uint32_t polls;
    request_change_t changes[REQUEST_CHANGES];
} request_log_t;

typedef struct
{
    uint8_t before;
    uint8_t pulses;
    uint8_t rises_after_off;
    uint8_t reserved;
    uint32_t ready;
    uint32_t first_index;
    uint32_t periods[PULSES];
    uint32_t widths[PULSES];
    uint32_t last_rise_after_off;
    uint32_t not_ready_after_off;
} spin_t;

typedef struct
{
    uint8_t seek_status[2][RATES];
    uint8_t restore_status[2][RATES];
    uint32_t seek_time[2][RATES];
    uint32_t restore_time[2][RATES];
    uint8_t verify_status[2];
    uint8_t load_status[2][HEAD_LOADS];
    uint32_t verify_time[2];
    uint32_t load_time[2][HEAD_LOADS];
} seeks_t;

typedef struct
{
    uint8_t ran;
    uint8_t disk;
    uint8_t dskchg_ran;
    uint8_t reserved;
    fdc_media_t media;
    swap_log_t swaps[2];
    uint8_t dskchg_drive[DSKCHG_READS];
    uint8_t dskchg_status[DSKCHG_READS];
    request_log_t requests[REQUEST_CASES];
    spin_t spin[2];
    seeks_t seeks;
} drive_t;

static void run(void* data, uint32_t steps);
static int pages(const void* data, uint32_t steps);
static void page(const void* data, uint32_t steps, int number);
static void summary(const void* data, uint32_t steps, char* line, uint8_t* color);
static void step_swaps(drive_t* result);
static void step_dskchg(drive_t* result);
static void step_requests(drive_t* result);
static void step_spin(drive_t* result);
static void step_seeks(drive_t* result);
static void watch(swap_log_t* log, int motor);
static void request(request_log_t* log, int variant);
static void spin(spin_t* entry, int high_speed);
static uint32_t average(const uint32_t* values, int count);
static void milliseconds(char* text, int size, uint32_t microseconds);
static uint32_t not_ready_time(const swap_log_t* log, uint32_t* longest);

static const test_step_t k_steps[] =
{
    { "READY DURING A SWAP", TEST_PHOTO },
    { "DSKCHG ACROSS A SWAP", TEST_PHOTO },
    { "MASKED FDC REQUEST", 0 },
    { "MOTOR AND INDEX", 0 },
    { "SEEKS AND HEAD LOAD", 0 },
    { 0, 0 }
};

static const char* const k_request_names[REQUEST_CASES] =
{
    "IRQMSK 1, IMR MASKED", "IRQMSK 0, IMR MASKED", "IRQMSK 1, IMR OPEN, IF 0", "POLL, IMR MASKED",
    "POLL, IMR OPEN", "FORCE D8H, IMMEDIATE"
};

const test_t g_test_drive =
{
    "4.4", "FLOPPY DRIVE SIGNALS", "DISK", TEST_RUN_ALL, 1, sizeof(drive_t), k_steps, run, pages, page, summary
};

static void run(void* data, uint32_t steps)
{
    drive_t* result = (drive_t*)data;

    ui_pad_only(0);

    if ((steps & 0x01) != 0)
        step_swaps(result);

    if ((steps & 0x02) != 0)
        step_dskchg(result);

    if ((steps & 0x04) != 0)
        step_requests(result);

    if ((steps & 0x08) != 0)
        step_spin(result);

    if ((steps & 0x10) != 0)
        step_seeks(result);

    fdc_off();
    out8(PIC_MASTER_COMMAND, 0x20);
    scr_clear_rows(SHELL_BODY_TOP, SHELL_BODY_BOTTOM);
}

// 0208h and 0200h every millisecond, each change kept with its time
static void watch(swap_log_t* log, int motor)
{
    uint32_t start;
    uint32_t next;
    uint32_t drawn;
    uint8_t drive;
    uint8_t status;

    fdc_select(0, 1);
    fdc_control((uint8_t)(FDC_MFM | (motor ? FDC_MOTOR : 0)));
    fdc_force();
    ui_prompt(motor ? "MOTOR ON: EJECT AND REINSERT THE" : "MOTOR OFF: EJECT AND REINSERT THE",
        "DISK 3 TIMES, 2 S OUT EACH TIME");
    start = tb_now();
    next = start;
    drawn = start;
    drive = in8(FDC_DRIVE);
    status = in8(FDC_COMMAND) & STATUS_MASK;
    log->first_drive = drive;
    log->first_status = status;
    log->ran = 1;
    (void)ui_poll();

    while (tb_now() - start < SWAP_US)
    {
        while ((int32_t)(tb_now() - next) < 0)
            ;

        next += POLL_US;

        uint8_t now_drive = in8(FDC_DRIVE);
        uint8_t now_status = in8(FDC_COMMAND) & STATUS_MASK;

        if ((now_drive != drive || now_status != status) && log->count < CHANGES)
        {
            drive_change_t* change = &log->changes[log->count++];
            change->time = tb_now() - start;
            change->drive = now_drive;
            change->status = now_status;
        }

        drive = now_drive;
        status = now_status;

        if (tb_now() - drawn >= 100000u)
        {
            if (ui_poll() == UI_A)
                break;

            ui_note(0, COLOR_WHITE, "0208H %02X 0200H %02X, %u CHANGES", drive, status, log->count);
            ui_countdown(start, SWAP_US, "A WHEN DONE");
            drawn = tb_now();
            next = drawn;
        }
    }

    fdc_control(FDC_MFM);
}

static void step_swaps(drive_t* result)
{
    shell_step("READY DURING A SWAP");
    ui_prompt("A DISK IN THE DRIVE. THE NEXT TWO", "PROMPTS ASK FOR THREE SWAPS EACH");

    if (ui_wait(30, "A STARTS, B SKIPS") != UI_A)
        return;

    watch(&result->swaps[0], 0);
    watch(&result->swaps[1], 1);
    result->ran |= 0x01;
}

// Bit 0 read before the swap, twice after it, after a Restore, after a seek to track 2 and half a second later
static void step_dskchg(drive_t* result)
{
    shell_step("DSKCHG");
    fdc_select(0, 1);
    fdc_control((uint8_t)(FDC_MFM | FDC_MOTOR));
    tb_delay(500000u);
    fdc_force();
    result->dskchg_drive[0] = in8(FDC_DRIVE);
    result->dskchg_status[0] = in8(FDC_COMMAND);
    ui_prompt("EJECT THE DISK, WAIT 2 S, REINSERT", "IT, THEN PRESS A");

    if (ui_wait(60, "A AFTER THE SWAP, B SKIPS") != UI_A)
        return;

    result->dskchg_drive[1] = in8(FDC_DRIVE);
    result->dskchg_status[1] = in8(FDC_COMMAND);
    result->dskchg_drive[2] = in8(FDC_DRIVE);
    result->dskchg_status[2] = in8(FDC_COMMAND);
    tb_delay(500000u);
    result->dskchg_status[3] = fdc_restore(3, 0);
    result->dskchg_drive[3] = in8(FDC_DRIVE);
    result->dskchg_status[4] = fdc_seek(0, 2, 0x13, 0);
    result->dskchg_drive[4] = in8(FDC_DRIVE);
    tb_delay(500000u);
    result->dskchg_drive[5] = in8(FDC_DRIVE);
    result->dskchg_status[5] = in8(FDC_COMMAND);
    result->dskchg_ran = 1;
    result->ran |= 0x02;
    fdc_restore(3, 0);
}

// The end of a Restore from track 10 (or a forced interrupt) watched in the master PIC every 10 us for a second,
// without touching 0200h; then 0200h is read and the request looked at again
static void request(request_log_t* log, int variant)
{
    int poll = variant == 3 || variant == 4;
    uint8_t control = (uint8_t)(FDC_MFM | FDC_MOTOR | (variant == 1 ? 0 : FDC_IRQ_ON));
    uint8_t last;
    uint32_t start;
    uint32_t next;

    fdc_control((uint8_t)(FDC_MFM | FDC_MOTOR));
    fdc_restore(3, 0);
    fdc_seek(0, SEEK_TRACK, 0x13, 0);
    (void)in8(FDC_COMMAND);
    fdc_control(control);

    if (variant == 2 || variant == 4)
        pic_unmask(6);
    else
        pic_mask(6);

    out8(PIC_MASTER_COMMAND, poll ? 0x0C : 0x0A);
    last = in8(PIC_MASTER_COMMAND);
    log->first = last;
    out8(FDC_COMMAND, variant == 5 ? 0xD8 : 0x03);
    start = tb_now();
    next = start;

    while (tb_now() - start < (variant == 5 ? 20000u : REQUEST_US))
    {
        uint8_t value;

        while ((int32_t)(tb_now() - next) < 0)
            ;

        next += REQUEST_POLL_US;

        if (poll)
        {
            out8(PIC_MASTER_COMMAND, 0x0C);
            value = in8(PIC_MASTER_COMMAND);
        }
        else
        {
            out8(PIC_MASTER_COMMAND, 0x0A);
            value = in8(PIC_MASTER_COMMAND);
        }

        log->polls++;

        if (value != last && log->count < REQUEST_CHANGES)
        {
            log->changes[log->count].time = tb_now() - start;
            log->changes[log->count].value = value;
            log->count++;
        }

        last = value;

        if (poll && (value & 0x80) != 0)
            out8(PIC_MASTER_COMMAND, 0x20);
    }

    log->status = in8(FDC_COMMAND);
    tb_delay(20);
    out8(PIC_MASTER_COMMAND, 0x0A);
    log->after_status = in8(PIC_MASTER_COMMAND);
    out8(PIC_MASTER_COMMAND, 0x0B);
    log->after_isr = in8(PIC_MASTER_COMMAND);
    out8(PIC_MASTER_COMMAND, 0x0A);

    if (variant == 5)
    {
        fdc_force();
        tb_delay(20);
        out8(PIC_MASTER_COMMAND, 0x0A);
        log->after_clear = in8(PIC_MASTER_COMMAND);
    }

    out8(PIC_MASTER_COMMAND, 0x20);
    pic_mask(6);
    fdc_control((uint8_t)(FDC_MFM | FDC_MOTOR));
    (void)in8(FDC_COMMAND);
}

static void step_requests(drive_t* result)
{
    fdc_select(0, 1);
    fdc_control((uint8_t)(FDC_MFM | FDC_MOTOR));
    tb_delay(300000u);

    for (int i = 0; i < REQUEST_CASES; i++)
    {
        shell_step("MASKED REQUEST %u/%u", i + 1, REQUEST_CASES);
        request(&result->requests[i], i);
    }

    fdc_restore(3, 0);
    result->ran |= 0x04;
}

// Motor off for 1.5 s, then on: the first ready and index, eight revolutions after a second, then off again
static void spin(spin_t* entry, int high_speed)
{
    uint32_t start;
    uint32_t rise = 0;
    int index;
    int ready = 0;
    int count = 0;

    entry->ready = NEVER;
    entry->first_index = NEVER;
    entry->not_ready_after_off = NEVER;
    fdc_select(0, high_speed);
    fdc_control(FDC_MFM);
    tb_delay(1500000u);
    fdc_force();
    entry->before = in8(FDC_DRIVE);
    index = (in8(FDC_COMMAND) & FDC_INDEX) != 0;
    fdc_control((uint8_t)(FDC_MFM | FDC_MOTOR));
    start = tb_now();

    while (tb_now() - start < SPIN_LIMIT_US && count <= PULSES)
    {
        uint32_t now = tb_now();
        int now_index = (in8(FDC_COMMAND) & FDC_INDEX) != 0;

        if (!ready && (in8(FDC_DRIVE) & FDC_READY) != 0)
        {
            ready = 1;
            entry->ready = now - start;
        }

        if (now_index && !index)
        {
            if (entry->first_index == NEVER)
                entry->first_index = now - start;

            if (now - start >= 1000000u)
            {
                if (count > 0)
                    entry->periods[count - 1] = now - rise;

                count++;
            }

            rise = now;
        }
        else if (!now_index && index && count > 0 && count <= PULSES)
            entry->widths[count - 1] = now - rise;

        index = now_index;
    }

    entry->pulses = (uint8_t)(count > 0 ? count - 1 : 0);
    fdc_control(FDC_MFM);
    start = tb_now();
    ready = (in8(FDC_DRIVE) & FDC_READY) != 0;

    if (!ready)
        entry->not_ready_after_off = 0;

    while (tb_now() - start < SPIN_DOWN_US)
    {
        uint32_t now = tb_now();
        int now_index = (in8(FDC_COMMAND) & FDC_INDEX) != 0;

        if (ready && (in8(FDC_DRIVE) & FDC_READY) == 0)
        {
            ready = 0;
            entry->not_ready_after_off = now - start;
        }

        if (now_index && !index)
        {
            entry->rises_after_off++;
            entry->last_rise_after_off = now - start;
        }

        index = now_index;
    }
}

static void step_spin(drive_t* result)
{
    shell_step("MOTOR AND INDEX, 360 RPM");
    spin(&result->spin[0], 1);
    shell_step("MOTOR AND INDEX, 300 RPM");
    spin(&result->spin[1], 0);
    result->disk = result->spin[0].pulses != 0 || result->spin[1].pulses != 0;
    result->ran |= 0x08;
}

// Seeks to track 10 and Restores at each rate with both FDC clocks; verified seeks and the head load delay in the
// disk's own mode
static void step_seeks(drive_t* result)
{
    seeks_t* seeks = &result->seeks;

    shell_step("SEEKS, DETECTING THE DISK");
    fdc_detect(&result->media);
    fdc_select(0, 1);

    for (int clock = 0; clock < 2; clock++)
    {
        fdc_control((uint8_t)(FDC_MFM | FDC_MOTOR | (clock != 0 ? FDC_SLOW : 0)));
        fdc_restore(3, 0);

        for (int rate = 0; rate < RATES; rate++)
        {
            shell_step("SEEKS, %s CLOCK, RATE %u", clock != 0 ? "1 MHZ" : "2 MHZ", rate);
            seeks->seek_status[clock][rate] = fdc_seek(0, SEEK_TRACK, (uint8_t)(0x10 | rate),
                &seeks->seek_time[clock][rate]);
            seeks->restore_status[clock][rate] = fdc_restore((uint8_t)rate, &seeks->restore_time[clock][rate]);
        }
    }

    if (result->media.found)
    {
        fdc_control((uint8_t)(FDC_MFM | FDC_MOTOR));
        fdc_set_mode(result->media.mode);
        tb_delay(500000u);
        fdc_restore(0, 0);
        shell_step("SEEKS, VERIFIED");
        seeks->verify_status[0] = fdc_seek(0, 5, 0x14, &seeks->verify_time[0]);
        out8(FDC_TRACK, 5);
        out8(FDC_COMMAND, 0x04);
        tb_delay(60);
        seeks->verify_status[1] = fdc_wait(3000000u, &seeks->verify_time[1]);

        for (int load = 0; load < 2; load++)
        {
            for (int i = 0; i < HEAD_LOADS; i++)
            {
                uint8_t id[6];
                uint32_t end;

                shell_step("HEAD LOAD E=%u, %u/%u", load, i + 1, HEAD_LOADS);
                fdc_force();
                uint32_t edge = fdc_index_edge(500000u);
                seeks->load_status[load][i] = fdc_read_address((uint8_t)(0xC0 | (load << 2)), id, 1200000u, &end);
                seeks->load_time[load][i] = edge != 0 ? end - edge : 0;
            }
        }
    }

    fdc_restore(3, 0);
    result->ran |= 0x10;
}

static void milliseconds(char* text, int size, uint32_t microseconds)
{
    if (microseconds == NEVER)
        fmt_copy(text, "NEVER", size);
    else
        fmt_format(text, size, "%u.%u", microseconds / 1000, microseconds / 100 % 10);
}

static uint32_t average(const uint32_t* values, int count)
{
    uint32_t sum = 0;

    if (count == 0)
        return 0;

    for (int i = 0; i < count; i++)
        sum += values[i];

    return sum / (uint32_t)count;
}

// The not-ready spans of a swap log: their number, and the longest
static uint32_t not_ready_time(const swap_log_t* log, uint32_t* longest)
{
    uint32_t spans = 0;
    uint32_t start = 0;
    int ready = (log->first_drive & FDC_READY) != 0;

    *longest = 0;

    for (int i = 0; i < log->count; i++)
    {
        int now_ready = (log->changes[i].drive & FDC_READY) != 0;

        if (ready && !now_ready)
            start = log->changes[i].time;
        else if (!ready && now_ready)
        {
            uint32_t span = log->changes[i].time - start;
            spans++;

            if (span > *longest)
                *longest = span;
        }

        ready = now_ready;
    }

    return spans;
}

static int pages(const void* data, uint32_t steps)
{
    (void)data;
    (void)steps;
    return 6;
}

static void page(const void* data, uint32_t steps, int number)
{
    const drive_t* result = (const drive_t*)data;
    int row = SHELL_BODY_TOP;
    (void)steps;

    if (number < 2)
    {
        const swap_log_t* log = &result->swaps[number];
        uint32_t longest;
        uint32_t spans;

        shell_line(row++, COLOR_CYAN, "SWAPS, MOTOR %s: 0208H 0200H, MS", number == 0 ? "OFF" : "ON");

        if (!log->ran)
        {
            shell_line(row++, COLOR_GRAY, "NOT RUN");
            return;
        }

        spans = not_ready_time(log, &longest);
        shell_line(row++, COLOR_GREEN, "%u NOT READY SPANS, LONGEST %u MS", spans, longest / 1000);
        shell_line(row++, COLOR_WHITE, "START  %02X %02X", log->first_drive, log->first_status);

        for (int i = 0; i < log->count && row <= SHELL_BODY_BOTTOM - 1; i += 2)
        {
            if (i + 1 < log->count)
                shell_line(row++, COLOR_WHITE, "%6u %02X %02X  %6u %02X %02X", log->changes[i].time / 1000,
                    log->changes[i].drive, log->changes[i].status, log->changes[i + 1].time / 1000,
                    log->changes[i + 1].drive, log->changes[i + 1].status);
            else
                shell_line(row++, COLOR_WHITE, "%6u %02X %02X", log->changes[i].time / 1000, log->changes[i].drive,
                    log->changes[i].status);
        }

        shell_line(row++, COLOR_GRAY, "0208H BIT 1 READY; 0200H & C4H");
        return;
    }

    if (number == 2)
    {
        static const char* const k_points[DSKCHG_READS] =
        {
            "BEFORE THE SWAP", "FIRST READ AFTER", "SECOND READ", "AFTER RESTORE", "AFTER SEEK TO 2", "0.5 S LATER"
        };

        shell_line(row++, COLOR_CYAN, "DSKCHG: 0208H AND 0200H");

        if (!result->dskchg_ran)
            shell_line(row++, COLOR_GRAY, "NOT RUN");
        else
        {
            for (int i = 0; i < DSKCHG_READS; i++)
                shell_line(row++, COLOR_WHITE, "%-17s %02X %02X  BIT0 %u", k_points[i], result->dskchg_drive[i],
                    result->dskchg_status[i], result->dskchg_drive[i] & 1);
        }

        shell_line(row++, COLOR_GRAY, "BIT 0: DSKCHG; BIT 1: READY");
        return;
    }

    if (number == 3)
    {
        shell_line(row++, COLOR_CYAN, "MASKED FDC REQUEST: IRR BIT 6");

        if ((result->ran & 0x04) == 0)
        {
            shell_line(row++, COLOR_GRAY, "NOT RUN");
            return;
        }

        for (int i = 0; i < REQUEST_CASES; i++)
        {
            const request_log_t* log = &result->requests[i];
            char line[40];
            int length = 0;

            for (int k = 0; k < log->count && k < 3; k++)
            {
                char time[12];
                milliseconds(time, sizeof(time), log->changes[k].time);
                length += fmt_format(line + length, (int)sizeof(line) - length, "%02X@%s ", log->changes[k].value, time);
            }

            shell_line(row++, COLOR_WHITE, "%s", k_request_names[i]);
            shell_line(row++, COLOR_WHITE, " %02X> %s", log->first, log->count != 0 ? line : "NO CHANGE");
            if (i == 5)
                shell_line(row++, COLOR_WHITE, " ST %02X IRR %02X ISR %02X, D0H IRR %02X", log->status,
                    log->after_status, log->after_isr, log->after_clear);
            else
                shell_line(row++, COLOR_WHITE, " ST %02X IRR %02X ISR %02X", log->status, log->after_status,
                    log->after_isr);
        }

        shell_line(row++, COLOR_GRAY, "VALUE@MS; ST, IRR, ISR AFTER 0200H");
        return;
    }

    if (number == 4)
    {
        shell_line(row++, COLOR_CYAN, "MOTOR AND INDEX, MS");

        if ((result->ran & 0x08) == 0)
        {
            shell_line(row++, COLOR_GRAY, "NOT RUN");
            return;
        }

        if (!result->disk)
            shell_line(row++, COLOR_YELLOW, "NO INDEX PULSES: NO DISK?");

        for (int s = 0; s < 2; s++)
        {
            const spin_t* entry = &result->spin[s];
            uint32_t period = average(entry->periods, entry->pulses);
            uint32_t width = average(entry->widths, entry->pulses);

            char ready[12];
            char index[12];
            char off[12];

            milliseconds(ready, sizeof(ready), entry->ready);
            milliseconds(index, sizeof(index), entry->first_index);
            milliseconds(off, sizeof(off), entry->not_ready_after_off);
            shell_line(row++, COLOR_WHITE, "%s RPM, MOTOR OFF 0208H %02X", s == 0 ? "360" : "300", entry->before);
            shell_line(row++, COLOR_WHITE, " READY %s INDEX %s", ready, index);
            shell_line(row++, COLOR_GREEN, " PERIOD %u US WIDTH %u US X%u", period, width, entry->pulses);
            shell_line(row++, COLOR_WHITE, " %u %u %u %u", entry->periods[0], entry->periods[1], entry->periods[2],
                entry->periods[3]);
            shell_line(row++, COLOR_WHITE, " OFF: %u PULSES, LAST %u.%u", entry->rises_after_off,
                entry->last_rise_after_off / 1000, entry->last_rise_after_off / 100 % 10);
            shell_line(row++, COLOR_WHITE, " OFF: NOT READY AT %s", off);
        }

        shell_line(row++, COLOR_GRAY, "TIMES FROM THE MOTOR WRITE");
        return;
    }

    const seeks_t* seeks = &result->seeks;

    shell_line(row++, COLOR_CYAN, "SEEK 0>10 AND RESTORE, MS");

    if ((result->ran & 0x10) == 0)
    {
        shell_line(row++, COLOR_GRAY, "NOT RUN");
        return;
    }

    shell_line(row++, COLOR_WHITE, "MEDIA %s",
        result->media.found ? g_fdc_modes[result->media.mode].name : result->media.mode == FDC_NO_MODE ? "NONE" : "?");

    if (result->media.found)
        shell_line(row++, COLOR_WHITE, "ID %02X %02X %02X %02X, TURN %u US", result->media.id[0], result->media.id[1],
            result->media.id[2], result->media.id[3], result->media.period);

    shell_line(row++, COLOR_GRAY, "CLK R  SEEK     ST  RESTORE  ST");

    for (int clock = 0; clock < 2; clock++)
    {
        for (int rate = 0; rate < RATES; rate++)
            shell_line(row++, COLOR_WHITE, "%s  %u %4u.%u   %02X  %4u.%u   %02X", clock != 0 ? "1M" : "2M", rate,
                seeks->seek_time[clock][rate] / 1000, seeks->seek_time[clock][rate] / 100 % 10,
                seeks->seek_status[clock][rate], seeks->restore_time[clock][rate] / 1000,
                seeks->restore_time[clock][rate] / 100 % 10, seeks->restore_status[clock][rate]);
    }

    if (result->media.found)
    {
        row++;
        shell_line(row++, COLOR_WHITE, "VERIFIED 0>5 %u.%u ST %02X", seeks->verify_time[0] / 1000,
            seeks->verify_time[0] / 100 % 10, seeks->verify_status[0]);
        shell_line(row++, COLOR_WHITE, "VERIFIED RESTORE %u.%u ST %02X", seeks->verify_time[1] / 1000,
            seeks->verify_time[1] / 100 % 10, seeks->verify_status[1]);

        for (int load = 0; load < 2; load++)
            shell_line(row++, COLOR_WHITE, "INDEX TO ID, E=%u: %u.%u %u.%u %u.%u", load,
                seeks->load_time[load][0] / 1000, seeks->load_time[load][0] / 100 % 10,
                seeks->load_time[load][1] / 1000, seeks->load_time[load][1] / 100 % 10,
                seeks->load_time[load][2] / 1000, seeks->load_time[load][2] / 100 % 10);
    }
}

static void summary(const void* data, uint32_t steps, char* line, uint8_t* color)
{
    const drive_t* result = (const drive_t*)data;
    (void)steps;

    *color = COLOR_WHITE;

    if ((result->ran & 0x08) != 0 && result->disk)
        fmt_format(line, 34, "TURN %u US, MASKED IRR %s", average(result->spin[0].periods, result->spin[0].pulses),
            (result->ran & 0x04) == 0 ? "-" : result->requests[0].count != 0 ? "YES" : "NO");
    else if ((result->ran & 0x04) != 0)
        fmt_format(line, 34, "NO DISK, MASKED REQ %s", result->requests[0].count != 0 ? "SHOWN" : "NOT SHOWN");
    else
        fmt_copy(line, "NOT RUN", 34);
}
