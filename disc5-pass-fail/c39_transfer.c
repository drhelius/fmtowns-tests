#include "checks.h"
#include "../disc3-sound-cd/t39_transfer.c"

#define STEP_TIMELINE 0x001
#define STEP_DMA 0x002
#define STEP_CASES 0x004
#define STEP_CLEARS 0x008
#define STEP_DEFERRED 0x010
#define STEP_UNDRAINED 0x020
#define STEP_SIZES 0x080
#define STEP_CPU 0x100

#define KIND_CHANGE 0
#define KIND_PACKET 1
#define KIND_END 3
#define DRY 0x01
#define DEI 0x40
#define READ_LBA (CD_PAD_LBA + 130)

// The drive answers in the status packets' second byte and in SIRQ (04C0h bit 7): the Marty 2's drive misreads, so
// only what the controller does with its buffer is checked. A read the drive never delivered isn't counted
static const char* const k_dma_checks[DMA_CASES] = { "SECTOR DMA, IDLE, US", "SECTOR DMA, CPU ON VRAM, US",
    "SECTOR DMA, CPU ON RAM, US" };
static const uint8_t k_tag[8] = { 0x46, 0x4D, 0x54, 0x33, (uint8_t)READ_LBA, (uint8_t)(READ_LBA >> 8), 0x00, 0x00 };
static const uint8_t k_raw_header[4] = { 0x00, 0x17, 0x05, 0x01 };

static int mark_kind(const mark_t* mark)
{
    return (int)(mark->stamp & 0xFF);
}

static uint8_t mark_master(const mark_t* mark)
{
    return mark_kind(mark) == KIND_CHANGE ? mark->bytes[0] : mark->bytes[2];
}

static int bytes_equal(const uint8_t* a, const uint8_t* b, int count)
{
    for (int i = 0; i < count; i++)
    {
        if (a[i] != b[i])
            return 0;
    }

    return 1;
}

static void evaluate_dma(const void* data, uint32_t steps)
{
    const transfer_t* result = (const transfer_t*)data;
    (void)steps;

    for (int i = 0; i < DMA_CASES; i++)
    {
        if (result->dma_failed[i] >= DMA_REPEATS)
        {
            check_true(k_dma_checks[i], 0);
            continue;
        }

        if (check_marty())
        {
            check_range(k_dma_checks[i], result->dma_low[i], i == 0 ? 3900 : 3850, i == 0 ? 3960 : 4050);
            check_range(k_dma_checks[i], result->dma_high[i], i == 0 ? 3900 : 3850, i == 0 ? 3960 : 4050);
        }
    }

    check_value("2048-BYTE FORM MOVES", result->sizes[0], 2048);
    check_value("2340-BYTE FORM MOVES", result->sizes[1], 2340);
    check_value("2336-BYTE FORM MOVES", result->sizes[2], 2336);
    check_true("FORMS END WITHOUT ERRORS", result->size_flags[0] == 0 && result->size_flags[1] == 0 &&
        result->size_flags[2] == 0);
    check_true("2048: THE SECTOR'S DATA", bytes_equal(result->heads[0], k_tag, 8));
    check_true("2340: HEADER, THEN THE DATA", bytes_equal(result->heads[1], k_raw_header, 4) &&
        bytes_equal(result->heads[1] + 4, k_tag, 8));
    check_true("2336: THE SECTOR'S DATA", bytes_equal(result->heads[2], k_tag, 8));
}

// Every DMA end shows DEI and DRY, the sector expected, and IRQ9 when DEIM is set
static void evaluate_ends(const void* data, uint32_t steps)
{
    const transfer_t* result = (const transfer_t*)data;
    uint32_t ends = 0;
    uint32_t sectors = 0;
    uint32_t flags = 0;
    uint32_t irqs = 0;
    int dry = 1;
    (void)steps;

    for (int i = 0; i < CASES; i++)
    {
        const case_t* entry = &result->cases[i];

        for (int k = 0; k < entry->count && k < CASE_MARKS; k++)
        {
            const mark_t* mark = &entry->marks[k];

            if (mark_kind(mark) != KIND_END || mark->bytes[0] != 0xDE || mark->bytes[1] != 0x00)
                continue;

            ends++;
            flags += (mark->bytes[2] & (DEI | DRY)) == (DEI | DRY);
            irqs += mark->bytes[3] == ((entry->masks & 0x01) != 0);
        }
    }

    for (int k = 0, packets = 0; k < result->timelines[1].count && k < MARKS; k++)
    {
        const mark_t* mark = &result->timelines[1].marks[k];
        int kind = mark_kind(mark);

        if (kind == KIND_END)
            sectors += mark->bytes[0] == 0xDE && mark->bytes[1] == (uint8_t)sectors;

        if (packets > 0 && (kind == KIND_CHANGE || kind == KIND_PACKET || kind == KIND_END))
            dry &= (mark_master(mark) & DRY) != 0;

        packets += kind == KIND_PACKET;
    }

    check_range("IRQ CASES READ (OF 16)", ends, 8, CASES);
    check_value("DEI AND DRY AT EVERY DMA END", flags, ends);
    check_value("IRQ9 AT THE END IFF DEIM", irqs, ends);
    check_value("3 SECTORS IN ORDER", sectors, 3);
    check_true("DRY FROM THE FIRST PACKET ON", dry);
}

static void evaluate_data_ready(const void* data, uint32_t steps)
{
    const transfer_t* result = (const transfer_t*)data;
    uint32_t pending = 0;
    uint32_t waited = 0;
    (void)steps;

    for (int i = 2; i < DEFERRED; i++)
    {
        const timeline_t* timeline = &result->deferred[i];

        for (int k = 0, ready = 0; k < timeline->count && k < MARKS; k++)
        {
            const mark_t* mark = &timeline->marks[k];

            if (mark_kind(mark) == KIND_PACKET && mark->bytes[0] == 0x22 && ++ready == 2)
            {
                pending += (mark->bytes[2] & DEI) != 0;
                break;
            }
        }
    }

    for (int i = 0; i < UNDRAINED; i++)
    {
        waited += result->undrained_lba[i] == CD_PAD_LBA + 100 + 4 * (uint32_t)i &&
            (!check_marty() || (result->undrained_dma[i] >= 3950 && result->undrained_dma[i] <= 4100));
    }

    check_true("DEI ACKED BY 80H, IRQ9 STAYS", (result->clears[4] & (DEI | DRY)) == (DEI | DRY) &&
        result->clears_irq[4] == 1);
    check_true("DEI KEPT BY A 06H READ", (result->clears[5] & (DEI | DRY)) == (DEI | DRY) && result->clears_irq[5] == 1);
    check_true("DEI ACKED BY 40H", (result->clears[6] & 0x7F) == DRY && result->clears_irq[6] == 0);
    check_value("DATA READY WITH DEI PENDING", pending, DEFERRED - 2);
    check_value("UNDRAINED SECTORS WAIT, OF 6", waited, UNDRAINED);
}

static void evaluate_cpu(const void* data, uint32_t steps)
{
    const transfer_t* result = (const transfer_t*)data;
    (void)steps;

    check_value("BYTES READ THROUGH 04C4H", result->cpu_bytes, 2048);
    check_hex("04C0H WITH 04C6H = 08H", result->cpu_master[0] & 0x7F, 0x21);
    check_hex("04C0H AT THE END, DEI DRY", result->cpu_master[1] & (DEI | DRY), DRY);
    check_value("SECTOR READ", result->cpu_lba, CD_PAD_LBA + 150);
}

static const test_t k_test = CHECK_TEST(g_test_transfer);

const check_group_t g_check_cdc_dma = { "CDC DMA AND READ FORMS", &k_test, STEP_DMA | STEP_SIZES, 0,
    evaluate_dma };
const check_group_t g_check_cdc_ends = { "CDC DMA END, DEI, DRY, IRQ9", &k_test, STEP_TIMELINE | STEP_CASES, 0,
    evaluate_ends };
const check_group_t g_check_cdc_ready = { "CDC DATA READY AND DEI", &k_test,
    STEP_CLEARS | STEP_DEFERRED | STEP_UNDRAINED, 0, evaluate_data_ready };
const check_group_t g_check_cdc_cpu = { "CDC CPU TRANSFER", &k_test, STEP_CPU, 0, evaluate_cpu };
