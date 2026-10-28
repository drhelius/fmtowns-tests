#include "checks.h"

extern const check_group_t g_check_division, g_check_undefined, g_check_delivery, g_check_transfers, g_check_esc;
extern const check_group_t g_check_cr0, g_check_0026, g_check_timer2, g_check_protect, g_check_nmi, g_check_bus;
extern const check_group_t g_check_edges, g_check_entries, g_check_field, g_check_hsync, g_check_start;
extern const check_group_t g_check_stopped, g_check_entry, g_check_cases, g_check_busy, g_check_contention;
extern const check_group_t g_check_regs, g_check_ym_latch, g_check_ym_busy, g_check_ym_timers, g_check_mixer;
extern const check_group_t g_check_pcm, g_check_fm_irq, g_check_adc, g_check_wave, g_check_cdc_dma, g_check_cdc_ends;
extern const check_group_t g_check_cdc_ready, g_check_cdc_cpu, g_check_fdc_request, g_check_fdc_seeks, g_check_backup;
extern const check_group_t g_check_cmos, g_check_card, g_check_ports, g_check_port_times;

static const check_group_t k_cpu = { "CPU", 0, 0, 0, 0 };
static const check_group_t k_board = { "BOARD", 0, 0, 0, 0 };
static const check_group_t k_video = { "VIDEO", 0, 0, 0, 0 };
static const check_group_t k_sprites = { "SPRITES", 0, 0, 0, 0 };
static const check_group_t k_sound = { "SOUND", 0, 0, 0, 0 };
static const check_group_t k_cdc = { "CD-ROM CONTROLLER", 0, 0, 0, 0 };
static const check_group_t k_storage = { "FLOPPY, CMOS, IC CARD", 0, 0, 0, 0 };
static const check_group_t k_ports = { "PORT ACCESS TIMES", 0, 0, 0, 0 };

static const check_group_t* const k_groups[] =
{
    &k_cpu, &g_check_division, &g_check_undefined, &g_check_delivery, &g_check_transfers, &g_check_esc, &g_check_cr0,
    &k_board, &g_check_0026, &g_check_timer2, &g_check_protect, &g_check_nmi, &g_check_bus,
    &k_video, &g_check_edges, &g_check_entries, &g_check_field, &g_check_hsync, &g_check_start, &g_check_stopped,
    &k_sprites, &g_check_entry, &g_check_cases, &g_check_busy, &g_check_contention, &g_check_regs,
    &k_sound, &g_check_ym_latch, &g_check_ym_busy, &g_check_ym_timers, &g_check_mixer, &g_check_pcm, &g_check_fm_irq,
    &g_check_adc, &g_check_wave,
    &k_cdc, &g_check_cdc_dma, &g_check_cdc_ends, &g_check_cdc_ready, &g_check_cdc_cpu,
    &k_storage, &g_check_fdc_request, &g_check_fdc_seeks, &g_check_backup, &g_check_cmos, &g_check_card,
    &k_ports, &g_check_ports, &g_check_port_times
};

static const check_disc_t k_disc = { 5, "PASS FAIL", "V1", k_groups, COUNT_OF(k_groups) };

void main(void)
{
    check_main(&k_disc);
}
