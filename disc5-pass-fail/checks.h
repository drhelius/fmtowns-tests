#ifndef CHECKS_H
#define CHECKS_H

#include <stdint.h>
#include "machine.h"
#include "shell.h"
#include "towns.h"

// The pass/fail shell: each group runs a test of the other discs, unchanged, and checks its record against what
// real hardware gives. A group with no test is a section title

#define CHECK_MAX_GROUPS 64
#define CHECK_MAX_RUNS 24
#define CHECK_MAX_ITEMS 64

#define MODEL_10F_20F 0x04
#define MODEL_MARTY 0x4A

// A group whose applies() says no is skipped, and a test none of its groups apply to doesn't run
typedef struct
{
    const char* name;
    const test_t* test;
    uint32_t steps;
    int (*applies)(void);
    void (*evaluate)(const void* data, uint32_t steps);
} check_group_t;

typedef struct
{
    uint8_t number;
    const char* name;
    const char* version;
    const check_group_t* const* groups;
    int count;
} check_disc_t;

// A test as a check runs it, without its pages, so the linker drops the code only the other discs show
#define CHECK_TEST(test) { (test).number, (test).name, 0, 0, (test).version, (test).size, 0, (test).run, 0, 0, 0 }

void check_main(const check_disc_t* disc) __attribute__((noreturn));

void check_hex(const char* name, uint32_t got, uint32_t want);
void check_value(const char* name, uint32_t got, uint32_t want);
void check_range(const char* name, uint32_t got, uint32_t minimum, uint32_t maximum);
void check_signed(const char* name, int32_t got, int32_t minimum, int32_t maximum);
void check_true(const char* name, int pass);
void check_skip(const char* name);

// Microseconds a window widens by when time comes from PIT counter 0 (3.26 us counts, read twice)
#define CHECK_PIT_SLACK 7

int check_marty(void);
int check_386(void);
int check_386sx(void);

#endif /* CHECKS_H */
