#include "checks.h"
#include "../disc1-cpu-board/t18_except.c"

#define DELIVERY_CASES 7

static const char* const k_write_names[TRANSFERS] =
{
    "WRITE AFTER FAR JMP", "WRITE AFTER FAR CALL", "WRITE AFTER RETF", "WRITE AFTER IRET"
};

static const char* const k_read_names[TRANSFERS] =
{
    "READ AFTER FAR JMP", "READ AFTER FAR CALL", "READ AFTER RETF", "READ AFTER IRET"
};

static const char* const k_task_names[DELIVERY_CASES] =
{
    "#DE: TASK SWITCHED TO", "#TS: TASK SWITCHED TO", "#NP: TASK SWITCHED TO", "#SS: TASK SWITCHED TO",
    "#GP: TASK SWITCHED TO", "TSS NP: TASK SWITCHED TO", "TSS GP: TASK SWITCHED TO"
};

static const char* const k_frame_names[DELIVERY_CASES] =
{
    "#DE: TR, LINK, CS, ERROR, CR2", "#TS: TR, LINK, CS, ERROR, CR2", "#NP: TR, LINK, CS, ERROR, CR2",
    "#SS: TR, LINK, CS, ERROR, CR2", "#GP: TR, LINK, CS, ERROR, CR2", "TSS NP: TR, LINK, CS, ERR, CR2",
    "TSS GP: TR, LINK, CS, ERR, CR2"
};

static void evaluate_transfers(const void* data, uint32_t steps)
{
    const exceptions_t* result = (const exceptions_t*)data;
    (void)steps;

    for (int i = 0; i < TRANSFERS; i++)
    {
        const access_t* write = &result->writes[i];
        const access_t* read = &result->reads[i];
        check_true(k_write_names[i], write->vector == 0 && write->done == 1 && write->value == 0x5A);
        check_true(k_read_names[i], read->vector == 0 && read->done == 1 && read->value == 0xC3);
    }
}

// A page fault on the ring-0 stack while delivering each exception: the #PF task, but a double fault for #TS
static void evaluate_delivery(const void* data, uint32_t steps)
{
    const exceptions_t* result = (const exceptions_t*)data;
    (void)steps;

    for (int i = 0; i < DELIVERY_CASES; i++)
    {
        const ht42_t* entry = &result->cases[i];
        int double_fault = i == 1;

        check_value(k_task_names[i], entry->task_vector, double_fault ? CPU_VECTOR_DF : CPU_VECTOR_PF);
        check_true(k_frame_names[i], entry->caught_vector == 0 && entry->continued == 0 &&
            entry->task_tr == (double_fault ? HT42_DF_TSS : HT42_PF_TSS) && entry->back_link == SEL_TSS &&
            entry->saved_cs == HT42_USER_CS && (entry->error & 0xFFFF) == (double_fault ? 0x0000 : 0x0002) &&
            entry->cr2 == 0x00006FFC);
    }
}

static const test_t k_test = CHECK_TEST(g_test_exceptions);

const check_group_t g_check_transfers = { "CS WRITES, PE SET, NO FAR JMP", &k_test, 0x0001, check_386,
    evaluate_transfers };
const check_group_t g_check_delivery = { "PAGE FAULT DURING DELIVERY", &k_test, 0x0002, check_386,
    evaluate_delivery };
