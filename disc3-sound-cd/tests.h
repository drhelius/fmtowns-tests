#ifndef TESTS_H
#define TESTS_H

#include <stdint.h>
#include "cd.h"
#include "cdc.h"
#include "cpu.h"
#include "fmt.h"
#include "input.h"
#include "layout.h"
#include "machine.h"
#include "pic.h"
#include "runtime.h"
#include "screen.h"
#include "shell.h"
#include "sound.h"
#include "timebase.h"
#include "towns.h"

extern const test_t g_test_buzzer;
extern const test_t g_test_ssg;
extern const test_t g_test_ym;
extern const test_t g_test_coupling;
extern const test_t g_test_stop;
extern const test_t g_test_pregap;
extern const test_t g_test_state;
extern const test_t g_test_timing;
extern const test_t g_test_transfer;
extern const test_t g_test_errors;
extern const test_t g_test_lid;
extern const test_t g_test_play;
extern const test_t g_test_irq;

#endif /* TESTS_H */
