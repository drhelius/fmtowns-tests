# Shared build rules for the FM Towns test discs
# A disc Makefile sets NAME and OBJECTS (object files under obj/), then includes this file
# Objects listed in ALLOW_486 may use 486 instructions; they only run on a 486 or behind an #UD handler
# IMAGE_OPTIONS go to mkimage.py (a larger data track, audio tracks)
# FLOPPY = yes also builds <name>.img, a 2HD floppy image that boots the same payload

CROSS ?= i686-elf-
CC = $(CROSS)gcc
LD = $(CROSS)ld
OBJCOPY = $(CROSS)objcopy
OBJDUMP = $(CROSS)objdump
NM = $(CROSS)nm
NASM ?= nasm
PYTHON ?= python3
RM = rm

SHARED = ../shared
OBJDIR = obj

CFLAGS = -std=gnu99 -march=i386 -mtune=i386 -Os -ffreestanding -fno-pic -fno-pie -fno-stack-protector \
    -fno-asynchronous-unwind-tables -fcf-protection=none -fno-common -fno-tree-loop-distribute-patterns -fno-delete-null-pointer-checks \
    -Wall -Wextra -I$(SHARED)
NASMFLAGS = -f elf32 -I$(SHARED)/
LDFLAGS = -m elf_i386 -nostdlib --no-warn-rwx-segments -T $(SHARED)/payload.ld

SHARED_OBJECTS = $(OBJDIR)/crt0.o $(OBJDIR)/cpu_entry.o $(OBJDIR)/cpu.o $(OBJDIR)/runtime.o $(OBJDIR)/fmt.o \
    $(OBJDIR)/font.o $(OBJDIR)/screen.o $(OBJDIR)/input.o $(OBJDIR)/timebase.o $(OBJDIR)/pic.o $(OBJDIR)/machine.o \
    $(OBJDIR)/record.o $(OBJDIR)/baseline.o $(OBJDIR)/qr.o $(OBJDIR)/shell.o $(OBJDIR)/boot.o $(OBJDIR)/cdc.o

CHECK_386 = $(SHARED_OBJECTS) $(filter-out $(ALLOW_486),$(OBJECTS))

# Instructions the 80386 doesn't have
NOT_386 = bswap|cmpxchg[a-z0-9]*|xadd[a-z]*|cmov[a-z]+|fcmov[a-z]+|fu?comip?|invd|wbinvd|invlpg|cpuid|rdtsc|rdmsr|wrmsr|rsm

all: $(NAME).bin $(if $(FLOPPY),$(NAME).img) $(NAME).sym

$(NAME).bin: $(OBJDIR)/ipl.bin $(OBJDIR)/$(NAME).payload
	$(PYTHON) $(SHARED)/mkimage.py $(OBJDIR)/ipl.bin $(OBJDIR)/$(NAME).payload $(NAME) $(IMAGE_OPTIONS)

$(NAME).img: $(OBJDIR)/ipl.bin $(OBJDIR)/$(NAME).payload
	$(PYTHON) $(SHARED)/mkimage.py $(OBJDIR)/ipl.bin $(OBJDIR)/$(NAME).payload $(NAME) --floppy

# Addresses only, assembler constants are left out
$(NAME).sym: $(NAME).elf
	$(NM) -n $< | grep -v ' [aA] ' > $@

$(OBJDIR)/$(NAME).payload: $(NAME).elf
	$(OBJCOPY) -O binary -R .shell -R .bss $< $@

$(NAME).elf: $(SHARED_OBJECTS) $(OBJECTS)
	$(LD) $(LDFLAGS) -Map $(OBJDIR)/$(NAME).map -o $@ $^
	@for object in $(CHECK_386); do \
		if $(OBJDUMP) -d $$object | grep -Eqw '$(NOT_386)'; then \
			echo "error: $$object uses instructions the 80386 doesn't have"; exit 1; \
		fi; \
	done

$(OBJDIR)/ipl.bin: $(SHARED)/ipl.asm $(SHARED)/layout.inc | $(OBJDIR)
	$(NASM) -f bin -I$(SHARED)/ $< -o $@ -l $(OBJDIR)/ipl.lst

$(OBJDIR)/%.o: %.c $(wildcard $(SHARED)/*.h) $(wildcard *.h) | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/%.o: %.asm $(SHARED)/layout.inc | $(OBJDIR)
	$(NASM) $(NASMFLAGS) $< -o $@

$(OBJDIR)/%.o: $(SHARED)/%.c $(wildcard $(SHARED)/*.h) | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/%.o: $(SHARED)/%.asm $(SHARED)/layout.inc | $(OBJDIR)
	$(NASM) $(NASMFLAGS) $< -o $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

clean:
	$(RM) -rf $(OBJDIR) $(NAME).elf $(NAME).sym $(NAME).bin $(NAME).cue $(NAME).img

.PHONY: all clean
