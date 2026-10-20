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
#include "video.h"

extern const test_t g_test_sync;
extern const test_t g_test_cards;
extern const test_t g_test_irq;
extern const test_t g_test_crtc;
extern const test_t g_test_lines;
extern const test_t g_test_wrap;
extern const test_t g_test_page;
extern const test_t g_test_midline;
extern const test_t g_test_sprite;
extern const test_t g_test_regs;

#endif /* TESTS_H */
