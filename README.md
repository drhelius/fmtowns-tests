# FM Towns Hardware Tests

[![License](https://img.shields.io/github/license/drhelius/fmtowns-tests)](https://github.com/drhelius/fmtowns-tests/blob/main/LICENSE)
[![Twitter Follow](https://img.shields.io/twitter/follow/drhelius)](https://x.com/drhelius)

Hardware tests for the FM Towns family made by analyzing actual hardware.

## Test Suites

Each suite boots from floppy or CD on any FM Towns model, runs its tests once at startup and shows the results on screen. Raw diagnostic values are shown below the test list.

### machine-id/
**Machine Identification**

Shows the machine ID (ports `0030h`/`0031h`) with its model and CPU type, the boot device, and the RAM size register (`05E8h`) that the 1F and later models provide.

---

### timing/
**Instruction Timing**

Times loops of a single instruction, unrolled 16 times, with counter 0 of the first PIT (307.2 kHz). Each test shows the elapsed ticks and the clocks per instruction of a 16 MHz CPU next to the Intel 80386 table value, which assumes zero wait states.

- **Test 1 – LOOP BASE**: Times the empty loop, which is subtracted from the other tests.
- **Test 2 – NOP**: Register-only reference.
- **Test 3 – MOV R32,[RAM]**: Aligned 32-bit loads from main RAM.
- **Test 4 – MOV [RAM],R32**: Aligned 32-bit stores to main RAM.
- **Test 5 – MOV R16,[RAM]**: Aligned 16-bit loads from main RAM.
- **Test 6 – MOV R32,[RAM+1]**: Misaligned 32-bit loads from main RAM.
- **Test 7 – MOV [VRAM],R8**: Byte stores to VRAM through the FM-R planes, below the displayed lines.
- **Test 8 – MOV R8,[VRAM]**: Byte loads from VRAM through the FM-R planes.

---

## Building

Each test directory contains its own Makefile. To build a specific test:

```bash
cd <test-directory>
make
```

To build them all, run `./build-all.sh` (`clean` and `rebuild` are also accepted).

The build system uses [NASM](https://www.nasm.us) for the boot record and the test code, a GCC cross compiler for i686-elf generating 80386 code (`-march=i386`) for the C parts, and Python 3 to build the media images. On macOS:

```bash
brew install nasm i686-elf-gcc
```

Another i386 ELF cross toolchain can be used by setting `CROSS` to its prefix, for example `make CROSS=i686-linux-gnu-`. The build stops if the linked code contains instructions the 80386 doesn't have.

Each suite produces:

- `<suite>.img`: a raw 2HD floppy image, 77 cylinders, 2 heads and 8 sectors of 1024 bytes (the same physical format as PC-98 2HD disks).
- `<suite>.iso` and `<suite>.cue`: a CD image with one `MODE1/2048` data track.
- `<suite>.elf` and `<suite>.sym`: the linked payload and its symbols as `nm` output.

## Running

Load the `.img` as a floppy or the `.cue` as a CD in an emulator. On real hardware, write the `.img` to a 2HD disk with a flux-level floppy writer or burn the `.cue` to a CD-R.

The boot record in `shared/ipl.asm` is loaded by the BIOS to `B0000h` and reads the payload from the following sectors to `80000h` (up to 192 KB). The tests then run in flat 32-bit protected mode with interrupts off. Text is drawn with the ANK font into the FM-R compatible planes the BIOS leaves on screen, which sit at the same low-memory addresses on every model, 80386DX and 80386SX alike.
