#ifndef SHELL_H
#define SHELL_H

#include <stdint.h>
#include "record.h"
#include "screen.h"

// The menu shell shared by the test discs (docs/12, "The suite")
// A test fills its result struct, which lives in the record store until reboot, and draws its pages from it

#define TEST_RUN_ALL 0x0001
#define TEST_CONFIRM 0x0002
#define TEST_RISKY 0x0004
#define TEST_PHOTO 0x0008

#define SHELL_BODY_TOP 5
#define SHELL_BODY_BOTTOM 26
#define SHELL_STATUS_ROW 27
#define SHELL_FOOTER_ROW 28
#define SHELL_MAX_TESTS 16
#define SHELL_MAX_STEPS 16

typedef struct
{
    const char* name;
    uint32_t flags;
} test_step_t;

typedef struct
{
    const char* number;
    const char* name;
    const char* tags;
    uint32_t flags;
    uint8_t version;
    uint32_t size;
    const test_step_t* steps;
    void (*run)(void* data, uint32_t steps);
    int (*pages)(const void* data, uint32_t steps);
    void (*page)(const void* data, uint32_t steps, int page);
    void (*summary)(const void* data, uint32_t steps, char* line, uint8_t* color);
} test_t;

typedef struct
{
    uint8_t number;
    const char* name;
    const char* version;
    const test_t* const* tests;
    int count;
} disc_t;

void shell_main(const disc_t* disc) __attribute__((noreturn));

void shell_step(const char* format, ...);
void shell_set_status(uint8_t status);
const void* shell_result(const char* number, uint32_t* steps);

void shell_line(int row, uint8_t color, const char* format, ...);

#endif /* SHELL_H */
