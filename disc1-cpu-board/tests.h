#ifndef TESTS_H
#define TESTS_H

#include <stdint.h>
#include "cpu.h"
#include "fmt.h"
#include "layout.h"
#include "machine.h"
#include "runtime.h"
#include "shell.h"
#include "timebase.h"
#include "towns.h"

extern const test_t g_test_census;
extern const test_t g_test_memory;
extern const test_t g_test_bus;
extern const test_t g_test_timer2;
extern const test_t g_test_rtc;
extern const test_t g_test_protect;
extern const test_t g_test_flags;
extern const test_t g_test_exceptions;
extern const test_t g_test_fpu_id;
extern const test_t g_test_fpu;

int fpu_id_present(const void* data);
int fpu_detect(void);

#endif /* TESTS_H */
