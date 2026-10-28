# FM Towns Hardware Tests

[![GitHub Workflow Status](https://img.shields.io/github/actions/workflow/status/drhelius/fmtowns-tests/build.yml)](https://github.com/drhelius/fmtowns-tests/actions/workflows/build.yml)
[![GitHub Releases](https://img.shields.io/github/v/tag/drhelius/fmtowns-tests?label=version)](https://github.com/drhelius/fmtowns-tests/releases)
[![License](https://img.shields.io/github/license/drhelius/fmtowns-tests)](https://github.com/drhelius/fmtowns-tests/blob/main/LICENSE)
[![Twitter Follow](https://img.shields.io/twitter/follow/drhelius)](https://x.com/drhelius)

Hardware tests for the FM Towns family made by analyzing actual hardware.

## Test Discs

Each disc boots from CD on any FM Towns model. Discs 1 to 4 boot into a menu: a test runs on its own and the menu
comes back afterwards. Disc 5 runs a selection of their tests at once and shows each check as PASS or FAIL. Every
device is put back to the same baseline before and after each test, so no test sees what another one left.

### The menu

- **Pad in port 1** (Towns or Marty pad): Up/Down choose, A runs, B goes back, Left/Right turn pages, Run shows the QR codes, Select switches between 15 kHz and 31 kHz. A Towns keyboard works as well: arrows, Return, Esc, Tab, Q for Run, digits to pick a test and A to pick Run all.
- **Run all** runs every test that needs nothing and changes nothing that survives a reset, then shows a summary with one line per test. Left/Right on it runs them in reverse.
- **Tags**: `RTC!` and `CMOS!` mark tests or steps that change state that survives a reset (they put it back when done) and ask for A twice. `FPU` marks a test that needs a coprocessor; `PHOTOS`, `REC` and `LID` mark tests outside Run all that need a camera, a recorder from the line output, or someone at the CD drive; `PAD`, `MOUSE`, `KEYBOARD`, `DISK`, `SCSI` and `CARD` name the device a test needs.
- **Steps**: a test with steps opens a list, so one step can be run again.
- Every page has a header with the machine ID, CPU, RAM, time base (`0026` or `PIT`), line rate, build and run number, and a `CHK` code at the bottom, a CRC-16 of the page's text to check a transcription of a photo.
- **QR codes** (Run on a result page or the summary): every raw value of the results, including those the pages leave out, as numbered QR codes to photograph and decode with `tools/records.py`. Run cycles the module size (2, 3 or 4 pixels) and Up/Down the mask.
- A fault shows a page with the vector, the registers, the stack and the step that was running; a test that hangs leaves its step on the screen.

### disc1-cpu-board/
**CPU, bus and board**

- **0 – Boot state**: the values captured before the shell touches anything: CR0 and EFLAGS at boot, `0020h`, `0028h`, `05C0h`, `05C2h`, the FM ports in read order, the PIC masks and requests, the PITs and the timers.
- **1.1 – Register census**: every register a model might have, read once, the `0026h` counter's rate and coherence, `0028h`, the serial ROM, CR0 ET and the wait registers.
- **1.2 – Memory map and ROMs**: a CRC-32 of each 64 KiB block, named from the collection's ROM files, the 24-bit wrap, the CMOS windows and the backup RAM. Its two write steps are tagged `CMOS!`.
- **1.3 – Bus timing**: one instruction unrolled 16 times over RAM, code fetches, VRAM with the display on and off, FM-R planes, sprite RAM, ROMs, CMOS, PCM RAM and I/O including `006Ch`, in every speed mode the machine has.
- **1.4 – Interval Timer II**: the IRQ level, whether disabling masks or stops the count, stale requests, start and reload, accuracy and readback.
- **1.5 – RTC pulses** (`RTC!`): the reference pulses of the MSM58321 against READY and the counters; puts the time back when done.
- **1.6 – Write protect and 05C0h**: which bytes `WRPROT` keeps from byte, straddling and DMA writes, and how the expansion NMI ports read and latch.
- **1.7 – Integer flags**: DIV and IDIV against the 386EX loop models, and digests of the results and flags of instructions the manuals leave undefined, with a digest per eight operands in the record to narrow a difference down.
- **1.8 – Exceptions**: writes and reads through a real-mode CS after setting PE without a far jump, and page faults while delivering other exceptions.
- **1.9 – FPU and CPU ID**: ESC instructions with no coprocessor, #NM, error delivery and limit checks with one, and the 486 items.
- **1.10 – FPU results** (`FPU`): transcendental and remainder results in every precision and rounding mode.

### disc2-video/
**Video**

- **2.1 – Sync timing census**: frame, VSYNC, line and HSYNC timing of every display mode from `FDA0h`.
- **2.2 – Display mode cards** (`PHOTOS`): a test card in each mode, to see which modes reach the output.
- **2.3 – VSYNC IRQ and IN sampling**: where IRQ11 and `IN` sample the sync bits, port access times, interrupt entry.
- **2.4 – CRTC start and timing registers**: what `START` and the timing registers do while running, and the CRTC's own counters.
- **2.5-2.8** (`PHOTOS`): line start addresses, horizontal wrap, the FM-R display page and writes during a line, as pictures.
- **2.9 – Sprite transfer timing**: BUSY and the transfer time by mode, sprite count and bus use.
- **2.10 – Sprite registers while busy**: sprite registers and sprite RAM written during a transfer.

### disc3-sound-cd/
**Sound and CD-ROM**

The data track is 40,000 sectors, each one after the payload carrying its own LBA, followed by six audio tracks the
build generates (`tracks/mktracks.py`): tones, DC steps and squares, a track with audio in its INDEX 00 pregap, a
pre-emphasized track and its twin without the flag, and a minute of stepped tones. Burn it disc-at-once with the CUE.

- **3.1, 3.2, 3.4** (`REC`): schedules to record from the line output, each after a sync burst: the buzzer and the
  level of each source and gate, YM3438 SSG-EG inversion, and the output's coupling.
- **3.3 – YM3438 read ports**: what `04DAh`, `04DCh` and `04DEh` read, the latch and its hold time, busy time and the
  timer periods.
- **3.5 – CD-DA stop and status**: the packets of a stop, pause and resume, and of every BIOS command from idle.
- **3.6 – CD-DA pregap and emphasis**: the TOC, sub-Q through pregaps, and a recorded emphasis schedule (`REC`).
- **3.7 – CDC drive state**: the status bytes of reads after boot, back to back, after idle times and around CD-DA.
- **3.8 – CD read timing**: sector period, read-ahead depth, seek times by distance and command acceptance.
- **3.9 – CDC transfer and IRQ**: `04C0h` through a read, DMA and CPU transfers, the IRQ9 matrix, undrained sectors.
- **3.10 – CDC errors**: reads outside the data, bad parameters, commands without STATUS, unknown commands, SRST.
- **3.11 – Lid and disc change** (`LID`): statuses while the lid opens and closes, with someone at the drive.
- **3.12 – CD-DA play and end**: start latency, sub-Q over a minute, end of play with and without repeat.
- **3.13 – Sound registers and IRQ13**: mixer and PCM registers, the PCM rate from its block interrupts, IRQ13.

### disc4-input-storage/
**Input, storage and CMOS**

Tests that need someone at the machine prompt on screen with a countdown and take A or B from either pad. Nothing on
this disc writes the floppy, the CMOS, an IC memory card or a SCSI device.

- **4.1 – Pad ports** (`PAD`): both ports in four output states, a live view and 3-second prompts per button,
  Marty pad Zoom included.
- **4.2 – Mouse strobe timing** (`MOUSE`): T1, T2, T3 and T4 swept with the mouse in port 2, what a timeout clears,
  and how a packet holds fast motion.
- **4.3 – Keyboard** (`KEYBOARD`): buffer depth and overflow, breaks, typematic, IRQ1 pacing, `A0h`/`A1h` and
  masking a pending request.
- **4.4 – Floppy drive signals** (`DISK`): ready during a swap, `DSKCHG`, the FDC request with IRQ6 masked, motor
  and index timing, seeks at each rate and the head load delay.
- **4.5 – Floppy sector reads** (`DISK`): the ID list with sector positions, one sector read 256 times (Columns'
  weak bytes when its disk is in), Read Track, a missing sector and a drive not ready.
- **4.6 – CMOS contents**: the 2 KiB of ports and the 8 KiB window, the header and SETUP checks, the backup RAM map.
- **4.7 – SCSI interface** (`SCSI`): read-only commands where the interface exists.
- **4.8 – Port timing**: access times of this disc's ports, and of one time base read.
- **4.9 – IC memory card** (`CARD`): the card status and bank registers, the window and attribute space, and the
  status while the card goes out and back in.

### disc5-pass-fail/
**Pass/fail checks**

The most telling results of discs 1 to 4 as PASS or FAIL, to compare an emulator with a real machine. The disc runs
their tests unchanged, with the steps it needs, and checks every result against what a Marty 2 measured. It runs
everything at boot, in about 3 minutes on the 15 kHz screen the results were taken on, and needs no pad, disk or
anyone at the machine; then it lists one line per check, page by page. A check marked `(MARTY)` holds values only the
Marty has (its board timing, its NTSC sync) and shows SKIP on other models, as do the checks for a CPU or a device the
machine doesn't have.

- **Pad or keyboard**: Left/Right turn pages, Up/Down choose a check, A shows its items with what each one got and
  wanted, B goes back, Run runs everything again, Select switches between 15 kHz and 31 kHz.
- **CPU** (1.1, 1.7-1.9): DIV and IDIV results and flags, the undefined flags of 60 instruction variants, page
  faults while delivering exceptions, writes through CS after setting PE, ESC instructions with no coprocessor, the
  386SX's CR0.
- **Board** (1.1, 1.3, 1.4, 1.6): the `0026h` counter, Interval Timer II, `WRPROT` on byte, straddling and DMA
  writes, `05C0h` and `05C2h`, bus timing and the `006Ch` wait.
- **Video and sprites** (2.1, 2.3, 2.4, 2.9, 2.10): IRQ11 at the VSYNC rise and held until `05CAh` is written, the
  NTSC field in every mode, the HSYNC bit, the sync while `START` is 0, the sprite entry time, BUSY at the start of
  VSYNC, what the CPU costs the sprites, PAGE and sprite register 6.
- **Sound** (3.3, 3.13): the YM3438 status latch, busy flag and timers, the mixer registers, the PCM block interrupts
  and rate, IRQ13, the ADC ports and the wave RAM window.
- **CD-ROM controller** (3.9): the sector DMA and the read forms, DEI, DRY and IRQ9 at the end of a DMA, data ready
  with DEI pending, undrained sectors and CPU transfers, on LBA-tagged sectors as on disc 3. What the drive itself
  answers isn't checked.
- **Floppy, CMOS, IC card and ports** (2.3, 4.4, 4.6, 4.8, 4.9): the FDC request with IRQ6 masked and the PIC poll
  word, seek and Restore times with no disk in the drive, the backup RAM's bytes, the CMOS window, the empty card
  slot, and port access times.

---

## Building

Each disc directory contains its own Makefile. To build a specific disc:

```bash
cd <disc-directory>
make
```

To build them all, run `./build-all.sh` (`clean` and `rebuild` are also accepted).

The build system uses [NASM](https://www.nasm.us) for the boot record and the test code, a GCC cross compiler for i686-elf generating 80386 code (`-march=i386`) for the C parts, and Python 3 to build the disc images. On macOS:

```bash
brew install nasm i686-elf-gcc
```

Another i386 ELF cross toolchain can be used by setting `CROSS` to its prefix, for example `make CROSS=i686-linux-gnu-`. The build stops if the linked code contains instructions the 80386 doesn't have, except in the objects a disc lists in `ALLOW_486`, which only run on a 486 or behind a handler for #UD.

Each disc produces:

- `<disc>.bin` and `<disc>.cue`: a `MODE1/2352` data track of raw sectors, with their EDC and ECC, and on disc 3
  the audio tracks after it.
- `<disc>.elf` and `<disc>.sym`: the linked payload and its symbols as `nm` output.

`disc1-cpu-board/rom_slices.c` is generated by `tools/romslices.py` from a local ROM collection: CRCs and names only.

## Running

Load the `.cue` in an emulator, or burn it to a CD-R disc-at-once.

The boot record in `shared/ipl.asm` is loaded by the BIOS to `B0000h` and reads the payload from the following sectors to `80000h` (up to 192 KB). The shell runs in flat 32-bit protected mode with interrupts off and never calls the BIOS: it programs its own screen, a 320x240 layer at 15 kHz or 31 kHz, and drives every device directly.

## Tools

- `tools/records.py`: prints the records from the text `zbarimg --raw` prints for photos of the QR codes (`zbarimg --raw -q *.jpg > codes.txt`), or from a RAM dump, where it finds them by their magic and CRC-32.
- `tools/romslices.py`: writes the ROM slice table test 1.2 names blocks with, from the ROM files it finds under a folder.
