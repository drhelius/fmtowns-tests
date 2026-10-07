# Shared build rules for the FM Towns test suites
# A suite Makefile sets NAME and OBJECTS (object files under obj/), then includes this file

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
    -fno-asynchronous-unwind-tables -fcf-protection=none -fno-common -Wall -Wextra -I$(SHARED)
NASMFLAGS = -f elf32 -I$(SHARED)/
LDFLAGS = -m elf_i386 -nostdlib --no-warn-rwx-segments -T $(SHARED)/payload.ld

SHARED_OBJECTS = $(OBJDIR)/crt0.o $(OBJDIR)/runtime.o $(OBJDIR)/video.o

# Instructions the 80386 doesn't have
NOT_386 = bswap|cmpxchg[a-z0-9]*|xadd[a-z]*|cmov[a-z]+|fcmov[a-z]+|fu?comip?|invd|wbinvd|invlpg|cpuid|rdtsc|rdmsr|wrmsr|rsm

all: $(NAME).img $(NAME).sym

$(NAME).img: $(OBJDIR)/ipl.bin $(OBJDIR)/$(NAME).bin
	$(PYTHON) $(SHARED)/mkimage.py $(OBJDIR)/ipl.bin $(OBJDIR)/$(NAME).bin $(NAME)

# Addresses only, assembler constants are left out
$(NAME).sym: $(NAME).elf
	$(NM) -n $< | grep -v ' [aA] ' > $@

$(OBJDIR)/$(NAME).bin: $(NAME).elf
	$(OBJCOPY) -O binary $< $@

$(NAME).elf: $(SHARED_OBJECTS) $(OBJECTS)
	$(LD) $(LDFLAGS) -Map $(OBJDIR)/$(NAME).map -o $@ $^
	@if $(OBJDUMP) -d $@ | grep -Eqw '$(NOT_386)'; then echo "error: $@ uses instructions the 80386 doesn't have"; exit 1; fi

$(OBJDIR)/ipl.bin: $(SHARED)/ipl.asm $(SHARED)/layout.inc | $(OBJDIR)
	$(NASM) -f bin -I$(SHARED)/ $< -o $@ -l $(OBJDIR)/ipl.lst

$(OBJDIR)/%.o: %.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/%.o: %.asm | $(OBJDIR)
	$(NASM) $(NASMFLAGS) $< -o $@

$(OBJDIR)/%.o: $(SHARED)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/%.o: $(SHARED)/%.asm $(SHARED)/layout.inc | $(OBJDIR)
	$(NASM) $(NASMFLAGS) $< -o $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

clean:
	$(RM) -rf $(OBJDIR) $(NAME).elf $(NAME).sym $(NAME).img $(NAME).iso $(NAME).cue

.PHONY: all clean
