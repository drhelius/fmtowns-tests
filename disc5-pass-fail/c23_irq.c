#include "checks.h"
#define g_test_irq g_test_vsync_irq
#include "../disc2-video/t23_irq.c"

typedef struct
{
    uint8_t port;
    uint8_t write;
    uint16_t minimum;
    uint16_t maximum;
} port_window_t;

// Microseconds for 4,096 accesses on the Marty 2, plus or minus 2%
static const port_window_t k_port_windows[] =
{
    { 0, 0, 4480, 4665 }, { 1, 0, 3575, 3720 }, { 2, 0, 3575, 3720 }, { 3, 0, 3575, 3720 }, { 4, 0, 3575, 3720 },
    { 5, 0, 4480, 4665 }, { 6, 0, 3575, 3720 }, { 7, 0, 4305, 4480 }, { 8, 0, 3575, 3720 }, { 9, 0, 3575, 3720 },
    { 10, 0, 4305, 4480 }, { 11, 0, 5810, 6045 }, { 12, 0, 3575, 3720 }, { 13, 0, 4305, 4480 }, { 14, 0, 3575, 3720 },
    { 0, 1, 3710, 3860 }, { 2, 1, 2735, 2850 }, { 3, 1, 2735, 2850 }, { 4, 1, 2735, 2850 }, { 5, 1, 3710, 3860 },
    { 6, 1, 2735, 2850 }, { 7, 1, 3530, 3675 }, { 9, 1, 5040, 5245 }
};

static const char* const k_read_names[PORTS] =
{
    "READ FDA0H", "READ 044CH", "READ 0440H", "READ 0442H", "READ 0450H", "READ FD90H", "READ 0010H", "READ 04D8H",
    "READ 0026H (WORD)", "READ 006CH", "READ 04DAH", "READ 04C0H", "READ 04D0H", "READ 0060H", "READ 0602H"
};

static const char* const k_write_names[PORTS] =
{
    "WRITE FDA0H", "", "WRITE 0440H", "WRITE 0442H", "WRITE 0450H", "WRITE FD90H", "WRITE 0010H", "WRITE 04D8H", "",
    "WRITE 006CH", "", "", "", "", ""
};

static const char* const k_rise_names[MODES] = { "15K: IRQ11 AT THE VSYNC RISE", "31K: IRQ11 AT THE VSYNC RISE" };
static const char* const k_hold_names[MODES] = { "15K: HELD UNTIL 05CAH WRITE", "31K: HELD UNTIL 05CAH WRITE" };
static const char* const k_entry_names[MODES] = { "15K: HANDLER SEES VSYNC, ONCE", "31K: HANDLER SEES VSYNC, ONCE" };
static const char* const k_latency_names[MODES] = { "15K: HANDLER LATENCY, US", "31K: HANDLER LATENCY, US" };

static void evaluate_edges(const void* data, uint32_t steps)
{
    const irq_result_t* result = (const irq_result_t*)data;
    (void)steps;

    for (int m = 0; m < MODES; m++)
    {
        uint32_t rises = 0;
        uint32_t holds = 0;

        for (int k = 0; k < REPEATS; k++)
        {
            const edge_t* edge = &result->modes[m].edges[k];
            int32_t window = 2 * edge->poll_ns + 1000;

            rises += (edge->flags & (EDGE_NO_VSYNC | EDGE_SET_AT_START)) == 0 && edge->irr_rise != NO_EDGE &&
                edge->irr_rise >= -window && edge->irr_rise <= window;
            holds += edge->irr_fall == NO_EDGE && (edge->flags & 0x07) == (EDGE_SET_AFTER | EDGE_SET_AFTER_READ);
        }

        check_value(k_rise_names[m], rises, REPEATS);
        check_value(k_hold_names[m], holds, REPEATS);
    }
}

static void evaluate_entries(const void* data, uint32_t steps)
{
    const irq_result_t* result = (const irq_result_t*)data;
    (void)steps;

    for (int m = 0; m < MODES; m++)
    {
        uint32_t good = 0;
        int32_t low = 0x7FFFFFFF;
        int32_t high = -0x7FFFFFFF;

        for (int k = 0; k < REPEATS; k++)
        {
            const entry_t* entry = &result->modes[m].entries[k];
            good += entry->latency != NO_EDGE && (entry->status & VIDEO_VSYNC) != 0 && entry->taken == 1;
            low = entry->latency < low ? entry->latency : low;
            high = entry->latency > high ? entry->latency : high;
        }

        check_value(k_entry_names[m], good, REPEATS);

        if (check_marty())
        {
            check_signed(k_latency_names[m], low, 15, 45);
            check_signed(k_latency_names[m], high, 15, 45);
        }
    }
}

static void evaluate_ports(const void* data, uint32_t steps)
{
    const irq_result_t* result = (const irq_result_t*)data;
    (void)steps;

    for (unsigned i = 0; i < COUNT_OF(k_port_windows); i++)
    {
        const port_window_t* window = &k_port_windows[i];
        uint32_t time = window->write ? result->port_write_us[window->port] : result->port_us[window->port];
        check_range(window->write ? k_write_names[window->port] : k_read_names[window->port], time, window->minimum,
            window->maximum);
    }
}

static const test_t k_test = CHECK_TEST(g_test_vsync_irq);

const check_group_t g_check_edges = { "IRQ11 AND THE VSYNC BIT", &k_test, STEP_EDGES, 0, evaluate_edges };
const check_group_t g_check_entries = { "IRQ11 HANDLER ENTRY", &k_test, STEP_LATENCY, 0, evaluate_entries };
const check_group_t g_check_ports = { "VIDEO, YM, CDC PORTS (MARTY)", &k_test, STEP_PORTS, check_marty,
    evaluate_ports };
