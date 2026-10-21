#ifndef TESTS_H
#define TESTS_H

#include <stdint.h>
#include "cdc.h"
#include "cpu.h"
#include "fdc.h"
#include "fmt.h"
#include "input.h"
#include "layout.h"
#include "machine.h"
#include "pic.h"
#include "record.h"
#include "runtime.h"
#include "screen.h"
#include "shell.h"
#include "timebase.h"
#include "towns.h"
#include "ui.h"

extern const test_t g_test_pad;
extern const test_t g_test_mouse;
extern const test_t g_test_keyboard;
extern const test_t g_test_drive;
extern const test_t g_test_sectors;
extern const test_t g_test_cmos;
extern const test_t g_test_scsi;
extern const test_t g_test_ports;
extern const test_t g_test_card;

#endif /* TESTS_H */
