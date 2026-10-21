#ifndef CD_H
#define CD_H

#include <stdint.h>

// The CDC driven directly: commands with their eight parameters, every status packet logged with its time from the
// command write, DMA started at each data ready, sectors checked by the tag mkimage.py writes into them

#define CD_MASTER 0x04C0
#define CD_COMMAND 0x04C2
#define CD_PARAMETER 0x04C4
#define CD_TRANSFER 0x04C6

#define CD_MASTER_DRY 0x01
#define CD_MASTER_SRQ 0x02
#define CD_MASTER_DTS 0x10
#define CD_MASTER_STS 0x20
#define CD_MASTER_DEI 0x40
#define CD_MASTER_SIRQ 0x80

#define CD_CONTROL_SIRQ 0x80
#define CD_CONTROL_DEI 0x40
#define CD_CONTROL_SRST 0x04
#define CD_CONTROL_SMIM 0x02
#define CD_CONTROL_DEIM 0x01

#define CD_STATUS 0x20
#define CD_IRQ 0x40

#define CD_SEEK 0x00
#define CD_MODE2_READ 0x01
#define CD_READ 0x02
#define CD_RAW_READ 0x03
#define CD_PLAY 0x04
#define CD_TOC 0x05
#define CD_SUBQ 0x06
#define CD_STATE 0x80
#define CD_SET 0x81
#define CD_STOP 0x84
#define CD_PAUSE 0x85
#define CD_RESUME 0x87

#define CD_TRACKS 7
#define CD_DATA_SECTORS 40000u
#define CD_PAD_LBA 1000u
#define CD_TAG_MAGIC 0x33544D46u
#define CD_TAG_MIX 0x5A5A5A5Au
#define CD_BUFFER 0x00100000u
#define CD_NO_LBA 0x00FFFFFFu

#define CD_LOG_PACKETS 24
#define CD_PACKET_DMA_NONE 0xDD
#define CD_PACKET_DMA 0xDE
#define CD_PACKET_DMA_TIMEOUT 0xDF

#define CD_LOG_NOT_READY 0x01
#define CD_LOG_FULL 0x02
#define CD_LOG_LIMIT 0x04
#define CD_LOG_DMA_TIMEOUT 0x08
#define CD_LOG_BAD_DATA 0x10
#define CD_LOG_ERROR 0x20
#define CD_LOG_DONE 0x40
#define CD_LOG_STALE 0x80

#define CD_DMA 0x01
#define CD_UNTIL_END 0x02
#define CD_UNTIL_SEEK 0x04
#define CD_NO_WAIT 0x08
#define CD_HOLD_DMA 0x10

// Pseudo packets CD_PACKET_DMA and CD_PACKET_DMA_TIMEOUT mark a DMA end, bytes 1-3 the LBA its data carried;
// CD_PACKET_DMA_NONE a DMA that DTS never showed
typedef struct
{
    uint32_t time;
    uint8_t bytes[4];
} cd_packet_t;

typedef struct
{
    uint8_t command;
    uint8_t count;
    uint8_t total;
    uint8_t flags;
    uint32_t lba;
    uint32_t start;
    uint32_t end;
    uint32_t ready_wait;
    uint16_t sectors;
    uint16_t ready;
    uint8_t stale[4];
    cd_packet_t packets[CD_LOG_PACKETS];
} cd_log_t;

// Sequences as the records keep them: the first four packets and their times, and the packet count
typedef struct
{
    uint8_t command;
    uint8_t count;
    uint8_t flags;
    uint8_t attempts;
    uint8_t bytes[4][4];
    uint32_t times[4];
} cd_brief_t;

// A log as the records keep it, the first CD_KEEP packets
#define CD_KEEP 10

typedef struct
{
    uint8_t command;
    uint8_t count;
    uint8_t flags;
    uint8_t stale;
    cd_packet_t packets[CD_KEEP];
} cd_keep_t;

// The same command run several times: the first run's sequence, how many runs matched it, and the times of the
// first and the last packet
typedef struct
{
    cd_brief_t first;
    uint8_t runs;
    uint8_t same;
    uint8_t failed;
    uint8_t reserved;
    uint32_t first_min;
    uint32_t first_max;
    uint32_t last_min;
    uint32_t last_max;
} cd_series_t;

typedef struct
{
    uint8_t valid;
    uint8_t first;
    uint8_t last;
    uint8_t packets;
    uint32_t leadout;
    uint32_t start[CD_TRACKS + 1];
    uint8_t control[CD_TRACKS + 1];
} cd_toc_t;

typedef struct
{
    uint8_t valid;
    uint8_t status;
    uint8_t track;
    uint8_t index;
    uint32_t relative;
    uint32_t absolute;
    uint8_t raw[16];
} cd_subq_t;

extern cd_toc_t g_cd_toc;

void cd_masks(uint8_t masks);
void cd_sink(uint32_t* times, int capacity);
void cd_stop_ready(int count);
uint8_t cd_master(void);
int cd_used(void);
const cd_keep_t* cd_first_read(uint32_t* when);
void cd_msf(uint32_t lba, uint8_t* msf);
uint32_t cd_lba(const uint8_t* msf);
uint8_t cd_bcd(uint32_t value);
uint32_t cd_from_bcd(uint8_t value);
void cd_range(uint8_t* parameters, uint32_t first, uint32_t last);
void cd_begin(cd_log_t* log, uint8_t command);
void cd_send(cd_log_t* log, uint8_t command, const uint8_t* parameters, uint32_t options);
void cd_send_raw(cd_log_t* log, uint8_t command, const uint8_t* parameters, int count, uint32_t options);
void cd_collect(cd_log_t* log, uint32_t first, uint32_t quiet, uint32_t limit, uint32_t options);
void cd_poll(cd_log_t* log, uint32_t microseconds);
uint32_t cd_drain(cd_log_t* log, uint32_t index);
void cd_arm(uint8_t command, uint32_t count);
void cd_command(cd_log_t* log, uint8_t command, const uint8_t* parameters);
void cd_read(cd_log_t* log, uint8_t command, uint32_t lba, uint32_t count);
int cd_read_retry(cd_log_t* log, uint8_t command, uint32_t lba, uint32_t count, int attempts);
int cd_read_ok(const cd_log_t* log, uint32_t count);
void cd_play(cd_log_t* log, uint32_t first, uint32_t end, int repeat);
void cd_state(cd_log_t* log);
void cd_stop(cd_log_t* log);
int cd_subq(cd_subq_t* subq);
int cd_load_toc(cd_log_t* log);
uint32_t cd_track(int track);
uint32_t cd_track_end(int track);
void cd_reset(void);
void cd_quiet(void);
uint32_t cd_check_sector(uint32_t address, uint32_t expected, int full);
const cd_packet_t* cd_find(const cd_log_t* log, uint8_t first, int nth);
uint32_t cd_time_of(const cd_log_t* log, uint8_t first, int nth);
void cd_brief(cd_brief_t* brief, const cd_log_t* log);
int cd_brief_same(const cd_brief_t* a, const cd_brief_t* b);
uint8_t cd_irq_line(void);
void cd_keep(cd_keep_t* keep, const cd_log_t* log);
int cd_draw_keep(int row, const char* label, const cd_keep_t* keep);
int cd_draw_packets(int row, const cd_packet_t* packets, int count);
void cd_series_add(cd_series_t* series, const cd_log_t* log, int failed);
int cd_draw_brief(int row, const char* label, const cd_brief_t* brief);
const char* cd_flags_name(uint8_t flags);

#endif /* CD_H */
