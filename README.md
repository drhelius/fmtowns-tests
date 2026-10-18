# FM Towns Hardware Tests

[![GitHub Workflow Status](https://img.shields.io/github/actions/workflow/status/drhelius/fmtowns-tests/build.yml)](https://github.com/drhelius/fmtowns-tests/actions/workflows/build.yml)
[![GitHub Releases](https://img.shields.io/github/v/tag/drhelius/fmtowns-tests?label=version)](https://github.com/drhelius/fmtowns-tests/releases)
[![License](https://img.shields.io/github/license/drhelius/fmtowns-tests)](https://github.com/drhelius/fmtowns-tests/blob/main/LICENSE)
[![Twitter Follow](https://img.shields.io/twitter/follow/drhelius)](https://x.com/drhelius)

Hardware tests for the FM Towns family made by analyzing actual hardware.

## Test Discs

Each disc boots from CD on any FM Towns model into a menu. A test runs on its own and the menu comes back afterwards; every device is put back to the same baseline before and after each test, so no test sees what another one left. Results are laid out to be read from a photo of the screen, and every result can also be shown as QR codes that carry the raw values.

### The menu

- **Pad in port 1** (Towns or Marty pad): Up/Down choose, A runs, B goes back, Left/Right turn pages, Run shows the QR codes, Select switches between 15 kHz and 31 kHz. A Towns keyboard works as well: arrows, Return, Esc, Tab, Q for Run, digits to pick a test and A to pick Run all.
- **Run all** runs every test that needs nothing and changes nothing that survives a reset, then shows a summary with one line per test. Left/Right on it runs them in reverse.
- **Tags**: `RTC!` and `CMOS!` mark tests or steps that change state that survives a reset (they put it back when done) and ask for A twice. `FPU` marks a test that needs a coprocessor.
- **Steps**: a test with steps opens a list, so one step can be run again.
- Every page has a header with the machine ID, CPU, RAM, time base (`0026` or `PIT`), line rate, build and run number, and a `CHK` code at the bottom, a CRC-16 of the page's text to check a transcription of a photo.
- **QR codes** (Run on a result page or the summary): every raw value of the results, including those the pages leave out, as numbered QR codes to photograph and decode with `tools/records.py`, so exact values don't depend on reading digits off a photo. Run cycles the module size (2, 3 or 4 pixels) and Up/Down the mask, if a phone won't read one.
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

- `<disc>.bin` and `<disc>.cue`: one `MODE1/2352` data track of raw sectors, with their EDC and ECC.
- `<disc>.elf` and `<disc>.sym`: the linked payload and its symbols as `nm` output.

`disc1-cpu-board/rom_slices.c` is generated by `tools/romslices.py` from a local ROM collection: CRCs and names only.

## Running

Load the `.cue` in an emulator, or burn it to a CD-R disc-at-once.

The boot record in `shared/ipl.asm` is loaded by the BIOS to `B0000h` and reads the payload from the following sectors to `80000h` (up to 192 KB). The shell runs in flat 32-bit protected mode with interrupts off and never calls the BIOS: it programs its own screen, a 320x240 layer at 15 kHz or 31 kHz, and drives every device directly.

## Tools

- `tools/records.py`: prints the records from the text `zbarimg --raw` prints for photos of the QR codes (`zbarimg --raw -q *.jpg > codes.txt`), or from a RAM dump, where it finds them by their magic and CRC-32.
- `tools/romslices.py`: writes the ROM slice table test 1.2 names blocks with, from the ROM files it finds under a folder.
