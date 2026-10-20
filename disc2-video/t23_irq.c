#include "tests.h"
#include "pic.h"

// 2.3 VSYNC interrupt and timing (docs/12, HT-8 q2-3), in the shell's 15 kHz and 31 kHz modes: the IRQ11 request
// in the slave IRR against FDA0h bit 0 and what clears it, the time to a handler, and how often Flashback's IN loop
// sees the edge while an IRQ11 handler that outlasts the pulse competes for it. That share of a loop pass is how far
// into the IN its data latches, plus however long the request takes to reach INTR. Then what an IN and an OUT cost on
// each of a few ports, unrolled so code fetch drops out, the parts of an interrupt entry (INT through the shell's
// dispatcher, INT to a gate that only returns, IRQ11 to a bare handler that reads 0026h first), segment loads, and
// register-only loops of three sizes at eight alignments for the prefetcher

#define MODES 2
#define REPEATS 8
#define ALIGNMENTS 8
#define LOOPS (ALIGNMENTS + 1)
#define CHANGES 8
#define LOOP_FRAMES 500
#define FLASHBACK_FRAMES 1000
#define CALIBRATION_FRAMES 2
#define FRAME_TIMEOUT_US 100000u
#define WAIT_POLLS 1000000u
#define SPIN_LIMIT 5000000u
#define NO_EDGE 0x7FFFFFFF
#define IRQ_VSYNC 11
#define PORTS 15
#define PORT_PASSES 256
#define PORT_ACCESSES (PORT_PASSES * 16)
#define PORT_READ 0x01
#define PORT_READ_WORD 0x02
#define PORT_WRITE 0x04
#define TIMING_REPEATS 4
#define INT_COUNT 256
#define VECTOR_SOFT 0x30
#define VECTOR_BARE 0x31
#define SHAPES 3
#define ALIGN_COUNT 8192u

#define STEP_EDGES 0x01
#define STEP_LATENCY 0x02
#define STEP_SAMPLING 0x04
#define STEP_PORTS 0x08
#define STEP_ENTRY 0x10
#define STEP_ALIGN 0x20

#define EDGE_SET_AFTER 0x01
#define EDGE_SET_AFTER_READ 0x02
#define EDGE_SET_AFTER_WRITE 0x04
#define EDGE_SET_AT_START 0x08
#define EDGE_NO_VSYNC 0x80

typedef struct
{
    int32_t irr_rise;
    int32_t vsync_fall;
    int32_t irr_fall;
    uint16_t poll_ns;
    uint8_t flags;
    uint8_t reserved;
} edge_t;

typedef struct
{
    int32_t latency;
    uint8_t status;
    uint8_t taken;
    uint8_t reserved[2];
} entry_t;

typedef struct
{
    uint16_t frames;
    uint16_t seen;
    uint32_t iterations;
    uint32_t elapsed;
} sample_t;

typedef struct
{
    edge_t edges[REPEATS];
    entry_t entries[REPEATS];
    sample_t samples[LOOPS];
} irq_mode_t;

typedef struct
{
    uint16_t port;
    uint8_t access;
    uint8_t value;
} port_case_t;

typedef struct
{
    uint32_t clock_khz;
    irq_mode_t modes[MODES];
    uint32_t port_us[PORTS];
    uint32_t port_write_us[PORTS];
    uint32_t soft_us;
    uint32_t bare_us;
    uint32_t segment_us;
    uint32_t pop_us;
    int32_t bare_latency[REPEATS];
    uint32_t align_us[SHAPES][ALIGNMENTS];
} irq_result_t;

extern uint32_t (*const irq_in_loops[ALIGNMENTS])(void);
void irq_flashback_loop(void);
void irq_port_reads(uint32_t port, uint32_t passes);
void irq_port_reads16(uint32_t port, uint32_t passes);
void irq_port_writes(uint32_t port, uint32_t value, uint32_t passes);
void irq_segment_loads(uint32_t passes);
void irq_segment_pops(uint32_t passes);
extern void (*const irq_align_loops[SHAPES][ALIGNMENTS])(uint32_t);
void irq_soft_ints(uint32_t count);
void irq_bare_ints(uint32_t count);
void irq_bare_iret(void);
void irq_bare_vsync(void);
extern volatile uint32_t irq_bare_time;
extern volatile uint32_t irq_bare_count;

static void run(void* data, uint32_t steps);
static int pages(const void* data, uint32_t steps);
static void page(const void* data, uint32_t steps, int number);
static void summary(const void* data, uint32_t steps, char* line, uint8_t* color);
static void latency_handler(int irq);
static void sample_handler(int irq);
static void scan_edge(const video_mode_t* mode, edge_t* edge);
static void measure_latency(entry_t* entry);
static void measure_sampling(const video_mode_t* mode, irq_mode_t* result);
static void measure_ports(irq_result_t* result);
static void measure_entry(irq_result_t* result);
static int soft_handler(cpu_frame_t* frame);
static uint32_t time_ints(void (*function)(uint32_t));
static void measure_bare(int32_t* latency);
static void measure_alignment(irq_result_t* result);
static void draw_ports(const irq_result_t* result, uint32_t steps, int row);
static void draw_costs(const irq_result_t* result, uint32_t steps, int row);
static uint32_t loop_ns(const sample_t* sample);
static uint32_t latch_ns(const sample_t* sample);
static int edge_range(const irq_mode_t* result, int field, int32_t* low, int32_t* high);
static uint32_t mean_latch_ns(const irq_mode_t* result);
static int irq_verdict(const irq_mode_t* entry, char* text, int size);

static const char* const k_mode_names[MODES] = { "S15", "S31" };
// Each write leaves things as they were: the layer enables as set, index registers, OCW3 reading the IRR, the YM
// address, FA0's own low byte, the 1 us wait register
static const port_case_t k_ports[PORTS] =
{
    { 0xFDA0, PORT_READ | PORT_WRITE, 0x0F }, { 0x044C, PORT_READ, 0 }, { 0x0440, PORT_READ | PORT_WRITE, 0x1E },
    { 0x0442, PORT_READ | PORT_WRITE, 0 }, { 0x0450, PORT_READ | PORT_WRITE, 0 }, { 0xFD90, PORT_READ | PORT_WRITE, 0 },
    { 0x0010, PORT_READ | PORT_WRITE, 0x0A }, { 0x04D8, PORT_READ | PORT_WRITE, 0x30 }, { 0x0026, PORT_READ_WORD, 0 },
    { 0x006C, PORT_READ | PORT_WRITE, 0 }, { 0x04DA, PORT_READ, 0 }, { 0x04C0, PORT_READ, 0 },
    { 0x04D0, PORT_READ, 0 }, { 0x0060, PORT_READ, 0 }, { 0x0602, PORT_READ, 0 }
};

static const uint8_t k_shape_bytes[SHAPES] = { 3, 6, 10 };

static const test_step_t k_steps[] =
{
    { "IRQ11 EDGE AND 05CAH", 0 },
    { "LATENCY TO THE HANDLER", 0 },
    { "IN SAMPLE POINT", 0 },
    { "PORT READS AND WRITES", 0 },
    { "INTERRUPTS, SEGMENT LOADS", 0 },
    { "LOOP ALIGNMENT", 0 },
    { 0, 0 }
};

const test_t g_test_irq =
{
    "2.3", "VSYNC IRQ AND TIMING", 0, TEST_RUN_ALL, 4, sizeof(irq_result_t), k_steps, run, pages, page, summary
};

static volatile uint32_t s_entry_time;
static volatile uint8_t s_entry_status;
static volatile uint32_t s_entries;
static volatile uint32_t s_frames;
static volatile uint32_t s_budget;
static volatile int s_forced;
static uint32_t s_seed;

static void run(void* data, uint32_t steps)
{
    irq_result_t* result = (irq_result_t*)data;

    result->clock_khz = video_clock_khz();

    for (int m = 0; m < MODES; m++)
    {
        const video_mode_t* mode = video_find(k_mode_names[m]);
        irq_mode_t* entry = &result->modes[m];

        video_program(mode);
        tb_delay(50000);

        if ((steps & STEP_EDGES) != 0)
        {
            shell_step("%s IRQ11 EDGE", mode->name);

            for (int k = 0; k < REPEATS; k++)
                scan_edge(mode, &entry->edges[k]);
        }

        if ((steps & STEP_LATENCY) != 0)
        {
            shell_step("%s LATENCY", mode->name);

            for (int k = 0; k < REPEATS; k++)
                measure_latency(&entry->entries[k]);
        }

        if ((steps & STEP_SAMPLING) != 0)
        {
            shell_step("%s IN SAMPLING", mode->name);
            measure_sampling(mode, entry);
        }
    }

    if ((steps & STEP_PORTS) != 0)
    {
        shell_step("PORT READS AND WRITES");
        measure_ports(result);
    }

    if ((steps & STEP_ENTRY) != 0)
    {
        shell_step("INTERRUPTS, SEGMENT LOADS");
        measure_entry(result);
    }

    if ((steps & STEP_ALIGN) != 0)
    {
        shell_step("LOOP ALIGNMENT");
        measure_alignment(result);
    }
}

static void latency_handler(int irq)
{
    s_entry_time = tb_now();
    s_entry_status = in8(VIDEO_STATUS);
    s_entries++;
    out8(VIDEO_VSYNC_CLEAR, 0);
    pic_eoi(irq);
}

// Holds the CPU past the end of the pulse, then a random delay so the next edge falls anywhere in a loop pass
static void sample_handler(int irq)
{
    out8(VIDEO_VSYNC_CLEAR, 0);

    if (s_frames >= s_budget)
        s_forced = 1;
    else
    {
        s_frames++;
        video_wait_bit(VIDEO_VSYNC, 0, WAIT_POLLS);
        s_seed = s_seed * 1103515245u + 12345u;

        for (volatile uint32_t i = (s_seed >> 16) & 0x3F; i != 0; i--)
            ;
    }

    pic_eoi(irq);
}

// From the display after a VSYNC, with the cause cleared, 1.3 frames of the two signals
static void scan_edge(const video_mode_t* mode, edge_t* edge)
{
    uint32_t changes[CHANGES];
    uint32_t time;

    uint32_t polls = video_scan_polls(video_frame_us(mode) * 13 / 10, VIDEO_VSYNC | VIDEO_IRQ);

    edge->irr_rise = NO_EDGE;
    edge->vsync_fall = NO_EDGE;
    edge->irr_fall = NO_EDGE;

    if (polls == 0 || !video_wait_fall(VIDEO_VSYNC, FRAME_TIMEOUT_US, &time))
    {
        edge->flags = EDGE_NO_VSYNC;
        return;
    }

    out8(VIDEO_VSYNC_CLEAR, 0);

    for (int i = 0; i < CHANGES; i++)
        changes[i] = VIDEO_NO_CHANGE;

    uint32_t start = tb_now();
    uint32_t done = video_scan(polls, changes, CHANGES, VIDEO_VSYNC | VIDEO_IRQ);
    uint32_t elapsed = tb_now() - start;

    if ((pic_read_irr() & (1 << IRQ_VSYNC)) != 0)
        edge->flags |= EDGE_SET_AFTER;

    in8(VIDEO_VSYNC_CLEAR);

    if ((pic_read_irr() & (1 << IRQ_VSYNC)) != 0)
        edge->flags |= EDGE_SET_AFTER_READ;

    out8(VIDEO_VSYNC_CLEAR, 0);

    if ((pic_read_irr() & (1 << IRQ_VSYNC)) != 0)
        edge->flags |= EDGE_SET_AFTER_WRITE;

    edge->poll_ns = (uint16_t)(done != 0 ? elapsed * 1000u / done : 0);

    if ((changes[0] & VIDEO_IRQ) != 0)
        edge->flags |= EDGE_SET_AT_START;

    int32_t rise = -1;
    int32_t fall = -1;
    int32_t irr_rise = -1;
    int32_t irr_fall = -1;
    uint8_t previous = (uint8_t)changes[0];

    for (int i = 1; i < CHANGES && changes[i] != VIDEO_NO_CHANGE; i++)
    {
        uint8_t state = (uint8_t)changes[i];
        int32_t poll = (int32_t)(changes[i] >> 8);
        uint8_t up = (uint8_t)(state & ~previous);
        uint8_t down = (uint8_t)(previous & ~state);

        if ((up & VIDEO_VSYNC) != 0 && rise < 0)
            rise = poll;

        if ((down & VIDEO_VSYNC) != 0 && rise >= 0 && fall < 0)
            fall = poll;

        if ((up & VIDEO_IRQ) != 0 && irr_rise < 0)
            irr_rise = poll;

        if ((down & VIDEO_IRQ) != 0 && irr_rise >= 0 && irr_fall < 0)
            irr_fall = poll;

        previous = state;
    }

    if (rise < 0 || done == 0)
    {
        edge->flags |= EDGE_NO_VSYNC;
        return;
    }

    if (irr_rise >= 0)
        edge->irr_rise = video_poll_ns(irr_rise - rise, elapsed, done);

    if (fall >= 0)
        edge->vsync_fall = video_poll_ns(fall - rise, elapsed, done);

    if (irr_fall >= 0)
        edge->irr_fall = video_poll_ns(irr_fall - rise, elapsed, done);
}

// Two VSYNC rises give the next one; the handler takes the time base on entry
static void measure_latency(entry_t* entry)
{
    uint32_t first;
    uint32_t second;

    entry->latency = NO_EDGE;

    if (!video_vsync_rise(&first) || !video_vsync_rise(&second) || video_wait_bit(VIDEO_VSYNC, 0, WAIT_POLLS) == 0)
        return;

    out8(VIDEO_VSYNC_CLEAR, 0);
    s_entries = 0;
    cpu_set_irq_handler(IRQ_VSYNC, latency_handler);
    pic_unmask(IRQ_VSYNC);
    sti();

    for (uint32_t spins = 0; s_entries == 0 && spins < SPIN_LIMIT; spins++)
        ;

    video_wait_bit(VIDEO_VSYNC, 0, WAIT_POLLS);
    cli();
    pic_mask(IRQ_VSYNC);
    cpu_set_irq_handler(IRQ_VSYNC, 0);
    out8(VIDEO_VSYNC_CLEAR, 0);

    entry->taken = (uint8_t)(s_entries < 0xFF ? s_entries : 0xFF);
    entry->status = s_entry_status;

    if (s_entries != 0)
        entry->latency = (int32_t)(s_entry_time - second - (second - first));
}

// Each pass length with interrupts off over whole frames, then the share of frames each loop sees
static void measure_sampling(const video_mode_t* mode, irq_mode_t* result)
{
    for (int k = 0; k < ALIGNMENTS; k++)
    {
        sample_t* sample = &result->samples[k];

        for (int f = 0; f < CALIBRATION_FRAMES; f++)
        {
            if (video_wait_bit(VIDEO_VSYNC, VIDEO_VSYNC, WAIT_POLLS) == 0 ||
                video_wait_bit(VIDEO_VSYNC, 0, WAIT_POLLS) == 0)
                return;

            uint32_t start = tb_now();
            sample->iterations += irq_in_loops[k]();
            sample->elapsed += tb_now() - start;
        }
    }

    s_seed = 0x2468ACE1u;
    cpu_set_irq_handler(IRQ_VSYNC, sample_handler);

    for (int c = 0; c < LOOPS; c++)
    {
        sample_t* sample = &result->samples[c];
        uint32_t exits = 0;
        uint32_t seen = 0;

        shell_step("%s IN SAMPLING %u/%u", mode->name, c + 1, LOOPS);
        video_wait_bit(VIDEO_VSYNC, 0, WAIT_POLLS);
        out8(VIDEO_VSYNC_CLEAR, 0);
        s_frames = 0;
        s_forced = 0;
        s_budget = c < ALIGNMENTS ? LOOP_FRAMES : FLASHBACK_FRAMES;
        pic_unmask(IRQ_VSYNC);
        sti();

        while (!s_forced && exits <= s_budget)
        {
            if (c < ALIGNMENTS)
                irq_in_loops[c]();
            else
                irq_flashback_loop();

            exits++;
            seen += !s_forced;
        }

        cli();
        pic_mask(IRQ_VSYNC);
        out8(VIDEO_VSYNC_CLEAR, 0);
        sample->frames = (uint16_t)s_frames;
        sample->seen = (uint16_t)seen;
    }

    cpu_set_irq_handler(IRQ_VSYNC, 0);
}

// 4,096 accesses of each port in runs of 16, the fastest of four, in the 31 kHz mode; 0442h reads FR's low byte and
// writes FA0's
static void measure_ports(irq_result_t* result)
{
    const video_mode_t* mode = video_find(k_mode_names[MODES - 1]);

    video_program(mode);
    tb_delay(50000);

    for (int i = 0; i < PORTS; i++)
    {
        const port_case_t* entry = &k_ports[i];
        uint32_t read = 0xFFFFFFFFu;
        uint32_t write = 0xFFFFFFFFu;
        uint8_t value = entry->port == CRTC_DATA ? (uint8_t)video_register(mode, CRTC_FA0) : entry->value;

        for (int k = 0; k < TIMING_REPEATS; k++)
        {
            out8(CRTC_ADDRESS, 0x1E);
            uint32_t start = tb_now();

            if ((entry->access & PORT_READ_WORD) != 0)
                irq_port_reads16(entry->port, PORT_PASSES);
            else
                irq_port_reads(entry->port, PORT_PASSES);

            uint32_t elapsed = tb_now() - start;
            read = elapsed < read ? elapsed : read;

            if ((entry->access & PORT_WRITE) == 0)
                continue;

            out8(CRTC_ADDRESS, CRTC_FA0);
            start = tb_now();
            irq_port_writes(entry->port, value, PORT_PASSES);
            elapsed = tb_now() - start;
            write = elapsed < write ? elapsed : write;
        }

        result->port_us[i] = read;
        result->port_write_us[i] = (entry->access & PORT_WRITE) != 0 ? write : 0;
    }

    out8(CRTC_ADDRESS, 0x1E);
}

// The bare IRQ11 handler reads 0026h, which only some models have
static void measure_entry(irq_result_t* result)
{
    cpu_set_handler(VECTOR_SOFT, soft_handler);
    cpu_set_gate(VECTOR_BARE, SEL_CODE32, (uint32_t)irq_bare_iret, CPU_GATE_INTERRUPT);
    result->soft_us = time_ints(irq_soft_ints);
    result->bare_us = time_ints(irq_bare_ints);
    result->segment_us = time_ints(irq_segment_loads);
    result->pop_us = time_ints(irq_segment_pops);
    cpu_set_handler(VECTOR_SOFT, 0);
    cpu_restore_gate(VECTOR_BARE);

    for (int k = 0; k < REPEATS; k++)
        result->bare_latency[k] = NO_EDGE;

    if (tb_source() != TB_SOURCE_0026)
        return;

    video_program(video_find(k_mode_names[0]));
    tb_delay(50000);
    cpu_set_gate(CPU_VECTOR_IRQ0 + IRQ_VSYNC, SEL_CODE32, (uint32_t)irq_bare_vsync, CPU_GATE_INTERRUPT);

    for (int k = 0; k < REPEATS; k++)
        measure_bare(&result->bare_latency[k]);

    cpu_restore_gate(CPU_VECTOR_IRQ0 + IRQ_VSYNC);
}

// Each loop's fastest of four runs of 8,192 passes
static void measure_alignment(irq_result_t* result)
{
    for (int shape = 0; shape < SHAPES; shape++)
    {
        for (int k = 0; k < ALIGNMENTS; k++)
        {
            uint32_t best = 0xFFFFFFFFu;

            for (int repeat = 0; repeat < TIMING_REPEATS; repeat++)
            {
                uint32_t start = tb_now();
                irq_align_loops[shape][k](ALIGN_COUNT);
                uint32_t elapsed = tb_now() - start;
                best = elapsed < best ? elapsed : best;
            }

            result->align_us[shape][k] = best;
        }
    }
}

static int soft_handler(cpu_frame_t* frame)
{
    (void)frame;
    return 1;
}

static uint32_t time_ints(void (*function)(uint32_t))
{
    uint32_t best = 0xFFFFFFFFu;

    for (int k = 0; k < TIMING_REPEATS; k++)
    {
        uint32_t start = tb_now();
        function(INT_COUNT);
        uint32_t elapsed = tb_now() - start;
        best = elapsed < best ? elapsed : best;
    }

    return best;
}

// As the latency step, from the predicted VSYNC to the handler's 0026h read
static void measure_bare(int32_t* latency)
{
    uint32_t first;
    uint32_t second;

    if (!video_vsync_rise(&first) || !video_vsync_rise(&second) || video_wait_bit(VIDEO_VSYNC, 0, WAIT_POLLS) == 0)
        return;

    out8(VIDEO_VSYNC_CLEAR, 0);
    irq_bare_count = 0;
    pic_unmask(IRQ_VSYNC);
    sti();

    for (uint32_t spins = 0; irq_bare_count == 0 && spins < SPIN_LIMIT; spins++)
        ;

    cli();

    uint32_t entry = tb_extend((uint16_t)irq_bare_time);

    pic_mask(IRQ_VSYNC);
    out8(VIDEO_VSYNC_CLEAR, 0);

    if (irq_bare_count != 0)
        *latency = (int32_t)(entry - second - (second - first));
}

static uint32_t loop_ns(const sample_t* sample)
{
    return sample->iterations != 0 ? sample->elapsed * 1000u / sample->iterations : 0;
}

static uint32_t latch_ns(const sample_t* sample)
{
    return sample->frames != 0 ? loop_ns(sample) * sample->seen / sample->frames : 0;
}

// Field 0: IRR rise, 1: VSYNC fall, 2: IRR fall; returns how many repeats had it
static int edge_range(const irq_mode_t* result, int field, int32_t* low, int32_t* high)
{
    int count = 0;

    *low = NO_EDGE;
    *high = -NO_EDGE;

    for (int k = 0; k < REPEATS; k++)
    {
        const edge_t* edge = &result->edges[k];
        int32_t value = field == 0 ? edge->irr_rise : field == 1 ? edge->vsync_fall : edge->irr_fall;

        if (value == NO_EDGE)
            continue;

        *low = value < *low ? value : *low;
        *high = value > *high ? value : *high;
        count++;
    }

    return count;
}

static uint32_t mean_latch_ns(const irq_mode_t* result)
{
    uint32_t sum = 0;
    int count = 0;

    for (int k = 0; k < ALIGNMENTS; k++)
    {
        if (result->samples[k].frames != 0)
        {
            sum += latch_ns(&result->samples[k]);
            count++;
        }
    }

    return count != 0 ? sum / (uint32_t)count : 0;
}

static int pages(const void* data, uint32_t steps)
{
    (void)data;
    (void)steps;
    return MODES + 3;
}

static void page(const void* data, uint32_t steps, int number)
{
    const irq_result_t* result = (const irq_result_t*)data;
    int row = SHELL_BODY_TOP;

    if (number < MODES)
    {
        const irq_mode_t* entry = &result->modes[number];
        const video_mode_t* mode = video_find(k_mode_names[number]);
        int32_t low;
        int32_t high;

        shell_line(row++, COLOR_CYAN, "%s, LINE %u NS, VSYNC %u US", mode->name, video_line_ns(mode),
            video_vsync_us(mode));

        if ((steps & STEP_EDGES) != 0)
        {
            int after = 0;
            int after_read = 0;
            int after_write = 0;
            int at_start = 0;
            int no_vsync = 0;

            for (int k = 0; k < REPEATS; k++)
            {
                uint8_t flags = entry->edges[k].flags;
                after += (flags & EDGE_SET_AFTER) != 0;
                after_read += (flags & EDGE_SET_AFTER_READ) != 0;
                after_write += (flags & EDGE_SET_AFTER_WRITE) != 0;
                at_start += (flags & EDGE_SET_AT_START) != 0;
                no_vsync += (flags & EDGE_NO_VSYNC) != 0;
            }

            shell_line(row++, COLOR_GRAY, "FROM THE VSYNC RISE, IN NS, POLL %u", entry->edges[0].poll_ns);

            if (edge_range(entry, 0, &low, &high) != 0)
                shell_line(row++, COLOR_WHITE, "IRR SETS   %d..%d", low, high);
            else
                shell_line(row++, COLOR_YELLOW, "IRR NEVER SET");

            if (edge_range(entry, 1, &low, &high) != 0)
                shell_line(row++, COLOR_WHITE, "VSYNC ENDS %d..%d", low, high);

            if (edge_range(entry, 2, &low, &high) != 0)
                shell_line(row++, COLOR_WHITE, "IRR CLEARS %d..%d", low, high);
            else
                shell_line(row++, COLOR_WHITE, "IRR STAYS SET IN THE SCAN");

            shell_line(row++, COLOR_WHITE, "SET AFTER %u/8 READ %u/8 WRITE %u/8", after, after_read, after_write);

            if (at_start != 0 || no_vsync != 0)
                shell_line(row++, COLOR_YELLOW, "SET AT START %u, NO VSYNC %u", at_start, no_vsync);
        }

        if ((steps & STEP_LATENCY) != 0)
        {
            int count = 0;
            int high_status = 0;
            int repeated = 0;

            low = NO_EDGE;
            high = -NO_EDGE;

            for (int k = 0; k < REPEATS; k++)
            {
                const entry_t* item = &entry->entries[k];

                if (item->latency == NO_EDGE)
                    continue;

                low = item->latency < low ? item->latency : low;
                high = item->latency > high ? item->latency : high;
                high_status += (item->status & VIDEO_VSYNC) != 0;
                repeated += item->taken > 1;
                count++;
            }

            if (count != 0)
            {
                shell_line(row++, COLOR_WHITE, "HANDLER %d..%d US, BIT 0 SET %u/%u", low, high, high_status, count);

                if (repeated != 0)
                    shell_line(row++, COLOR_YELLOW, "IRQ11 AGAIN IN THE PULSE %u/%u", repeated, count);
            }
            else
                shell_line(row++, COLOR_YELLOW, "NO IRQ11 TAKEN");
        }

        if ((steps & STEP_SAMPLING) != 0)
        {
            shell_line(row++, COLOR_GRAY, "ALIGN FRAMES  SEEN  LOOP  LATCH");

            for (int k = 0; k < LOOPS; k++)
            {
                const sample_t* sample = &entry->samples[k];
                uint32_t share = sample->frames != 0 ? sample->seen * 1000u / sample->frames : 0;

                if (k < ALIGNMENTS)
                    shell_line(row++, COLOR_WHITE, "  %u   %4u %3u.%u %5u %5u", k, sample->frames, share / 10,
                        share % 10, loop_ns(sample), latch_ns(sample));
                else
                    shell_line(row++, COLOR_WHITE, "NO INC %4u %3u.%u", sample->frames, share / 10, share % 10);
            }
        }

        return;
    }

    if (number == MODES)
    {
        draw_ports(result, steps, row);
        return;
    }

    if (number == MODES + 1)
    {
        draw_costs(result, steps, row);
        return;
    }

    shell_line(row++, COLOR_CYAN, "VERDICTS");
    shell_line(row++, COLOR_WHITE, "CPU %u.%02u MHZ BY AAM", result->clock_khz / 1000, result->clock_khz % 1000 / 10);
    row++;

    for (int m = 0; m < MODES; m++)
    {
        const irq_mode_t* entry = &result->modes[m];
        char text[24];

        if ((steps & STEP_EDGES) != 0)
        {
            irq_verdict(entry, text, sizeof(text));
            shell_line(row++, COLOR_WHITE, "%s IRQ11 %s", k_mode_names[m], text);
        }

        if ((steps & STEP_SAMPLING) != 0)
        {
            uint32_t latch = mean_latch_ns(entry);
            uint32_t loop = loop_ns(&entry->samples[0]);
            uint32_t clocks = latch * (result->clock_khz / 100) / 1000;
            uint32_t loop_clocks = loop * (result->clock_khz / 100) / 1000;

            shell_line(row++, COLOR_WHITE, "%s IN LATCH %u NS = %u.%u CLOCKS", k_mode_names[m], latch, clocks / 10,
                clocks % 10);
            shell_line(row++, COLOR_WHITE, "    OF A %u.%u-CLOCK LOOP", loop_clocks / 10, loop_clocks % 10);
        }
    }
}

static void draw_ports(const irq_result_t* result, uint32_t steps, int row)
{
    uint32_t khz = result->clock_khz;

    shell_line(row++, COLOR_CYAN, "PORTS, NS AND CLOCKS, 16 IN A RUN");

    if ((steps & STEP_PORTS) == 0)
        shell_line(row++, COLOR_GRAY, "NOT RUN");
    else
    {
        shell_line(row++, COLOR_GRAY, "PORT     READ  CLK   WRITE  CLK");

        for (int i = 0; i < PORTS; i++)
        {
            const port_case_t* entry = &k_ports[i];
            uint32_t tenths = result->port_us[i] * 10000u / PORT_ACCESSES;
            uint32_t clocks = tenths * (khz / 100) / 10000;
            char write[16];

            if ((entry->access & PORT_WRITE) != 0)
            {
                uint32_t write_tenths = result->port_write_us[i] * 10000u / PORT_ACCESSES;
                uint32_t write_clocks = write_tenths * (khz / 100) / 10000;
                fmt_format(write, sizeof(write), "%5u.%u %3u.%u", write_tenths / 10, write_tenths % 10,
                    write_clocks / 10, write_clocks % 10);
            }
            else
                fmt_copy(write, "      -", sizeof(write));

            shell_line(row++, COLOR_WHITE, "%04XH%s %5u.%u %3u.%u %s", entry->port,
                (entry->access & PORT_READ_WORD) != 0 ? "W" : " ", tenths / 10, tenths % 10, clocks / 10, clocks % 10,
                write);
        }
    }
}

// Nanoseconds and clocks per interrupt and segment load, the bare IRQ11 latency, and clocks per loop pass
static void draw_costs(const irq_result_t* result, uint32_t steps, int row)
{
    uint32_t khz = result->clock_khz;

    shell_line(row++, COLOR_CYAN, "INTERRUPTS, SEGMENT LOADS");

    if ((steps & STEP_ENTRY) == 0)
    {
        shell_line(row++, COLOR_GRAY, "NOT RUN");
        return;
    }

    uint32_t soft = result->soft_us * 1000u / INT_COUNT;
    uint32_t bare = result->bare_us * 1000u / INT_COUNT;

    shell_line(row++, COLOR_WHITE, "INT, DISPATCHER %5u NS %4u CLK", soft, soft * (khz / 100) / 10000);
    shell_line(row++, COLOR_WHITE, "INT, IRETD ONLY %5u NS %4u CLK", bare, bare * (khz / 100) / 10000);

    uint32_t load = result->segment_us * 10000u / (INT_COUNT * 16);
    uint32_t load_clocks = load * (khz / 100) / 10000;

    shell_line(row++, COLOR_WHITE, "MOV DS,AX %6u.%u NS %3u.%u CLK", load / 10, load % 10, load_clocks / 10,
        load_clocks % 10);

    uint32_t pair = result->pop_us * 10000u / (INT_COUNT * 16);
    uint32_t pair_clocks = pair * (khz / 100) / 10000;

    shell_line(row++, COLOR_WHITE, "PUSH+POP DS %4u.%u NS %3u.%u CLK", pair / 10, pair % 10, pair_clocks / 10,
        pair_clocks % 10);

    int32_t low = NO_EDGE;
    int32_t high = -NO_EDGE;

    for (int k = 0; k < REPEATS; k++)
    {
        int32_t value = result->bare_latency[k];

        if (value == NO_EDGE)
            continue;

        low = value < low ? value : low;
        high = value > high ? value : high;
    }

    if (low == NO_EDGE)
        shell_line(row++, COLOR_GRAY, "BARE IRQ11: NO 0026H");
    else
        shell_line(row++, COLOR_WHITE, "BARE IRQ11 %s %d..%d US", k_mode_names[0], low, high);

    row++;
    shell_line(row++, COLOR_CYAN, "CLOCKS A LOOP PASS BY ALIGNMENT");

    if ((steps & STEP_ALIGN) == 0)
    {
        shell_line(row++, COLOR_GRAY, "NOT RUN");
        return;
    }

    shell_line(row++, COLOR_GRAY, "ALIGN  %2u BYTES %2u BYTES %2u BYTES", k_shape_bytes[0], k_shape_bytes[1],
        k_shape_bytes[2]);

    for (int k = 0; k < ALIGNMENTS; k++)
    {
        uint32_t clocks[SHAPES];

        for (int shape = 0; shape < SHAPES; shape++)
            clocks[shape] = result->align_us[shape][k] * khz / (100u * ALIGN_COUNT);

        shell_line(row++, COLOR_WHITE, "  %u    %5u.%u   %5u.%u   %5u.%u", k, clocks[0] / 10, clocks[0] % 10,
            clocks[1] / 10, clocks[1] % 10, clocks[2] / 10, clocks[2] % 10);
    }
}

// Where the request rises against bit 0, within two polls and a microsecond
static int irq_verdict(const irq_mode_t* entry, char* text, int size)
{
    int32_t low;
    int32_t high;
    int32_t fall_low;
    int32_t fall_high;
    int32_t slack = 2 * entry->edges[0].poll_ns + 1000;

    if (edge_range(entry, 0, &low, &high) == 0)
    {
        fmt_copy(text, "NEVER REQUESTED", size);
        return 0;
    }

    int falls = edge_range(entry, 1, &fall_low, &fall_high);

    if (low >= -slack && high <= slack)
        fmt_copy(text, "AT VSYNC START", size);
    else if (falls != 0 && low >= fall_low - slack && high <= fall_high + slack)
        fmt_copy(text, "AT VSYNC END", size);
    else if (high < 0)
        fmt_format(text, size, "%d US BEFORE VSYNC", -high / 1000);
    else
        fmt_format(text, size, "%d US INTO VSYNC", low / 1000);

    return 1;
}

static void summary(const void* data, uint32_t steps, char* line, uint8_t* color)
{
    const irq_result_t* result = (const irq_result_t*)data;
    uint32_t clocks = mean_latch_ns(&result->modes[0]) * (result->clock_khz / 100) / 1000;
    char text[24];

    *color = COLOR_WHITE;

    if ((steps & STEP_EDGES) == 0)
    {
        fmt_format(line, 34, "IN LATCH %u.%u CLOCKS", clocks / 10, clocks % 10);
        return;
    }

    if (!irq_verdict(&result->modes[0], text, sizeof(text)))
        *color = COLOR_YELLOW;

    fmt_format(line, 34, "%s, IN %u.%u CLK", text, clocks / 10, clocks % 10);
}
