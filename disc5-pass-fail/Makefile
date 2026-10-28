NAME = disc5-pass-fail
SHELL_OBJECTS =
CHECKS = $(OBJDIR)/c11_census.o $(OBJDIR)/c13_bus.o $(OBJDIR)/c14_timer2.o $(OBJDIR)/c16_protect.o \
    $(OBJDIR)/c17_flags.o $(OBJDIR)/c18_except.o $(OBJDIR)/c19_fpu.o $(OBJDIR)/c21_sync.o $(OBJDIR)/c23_irq.o \
    $(OBJDIR)/c24_crtc.o $(OBJDIR)/c29_sprite.o $(OBJDIR)/c210_regs.o $(OBJDIR)/c33_ym.o $(OBJDIR)/c313_irq.o \
    $(OBJDIR)/c39_transfer.o $(OBJDIR)/c44_drive.o $(OBJDIR)/c46_cmos.o $(OBJDIR)/c48_ports.o $(OBJDIR)/c49_card.o
TESTED = $(OBJDIR)/t13_loops.o $(OBJDIR)/t17_ops.o $(OBJDIR)/t18_except_asm.o $(OBJDIR)/t19_fpu_asm.o \
    $(OBJDIR)/video.o $(OBJDIR)/video_asm.o $(OBJDIR)/canvas.o $(OBJDIR)/raster.o $(OBJDIR)/t23_irq_asm.o \
    $(OBJDIR)/t24_crtc_asm.o $(OBJDIR)/t29_sprite_asm.o $(OBJDIR)/cd.o $(OBJDIR)/sound.o $(OBJDIR)/fdc.o \
    $(OBJDIR)/fdc_asm.o $(OBJDIR)/ui.o $(OBJDIR)/ports_asm.o
OBJECTS = $(OBJDIR)/main.o $(OBJDIR)/checks.o $(CHECKS) $(TESTED)
ALLOW_486 = $(OBJDIR)/t17_ops.o $(OBJDIR)/t19_fpu_asm.o
IMAGE_OPTIONS = --sectors 3000

include ../shared/towns.mak

# The checks include the tests of the other discs, which run unchanged
$(CHECKS): $(wildcard ../disc[1-4]-*/*.c ../disc[1-4]-*/*.h)

$(OBJDIR)/%.o: ../disc1-cpu-board/%.asm $(SHARED)/layout.inc | $(OBJDIR)
	$(NASM) $(NASMFLAGS) $< -o $@

$(OBJDIR)/%.o: ../disc2-video/%.asm $(SHARED)/layout.inc | $(OBJDIR)
	$(NASM) $(NASMFLAGS) $< -o $@

$(OBJDIR)/%.o: ../disc4-input-storage/%.asm $(SHARED)/layout.inc | $(OBJDIR)
	$(NASM) $(NASMFLAGS) $< -o $@

$(OBJDIR)/%.o: ../disc2-video/%.c $(wildcard $(SHARED)/*.h ../disc2-video/*.h) | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/%.o: ../disc3-sound-cd/%.c $(wildcard $(SHARED)/*.h ../disc3-sound-cd/*.h) | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/%.o: ../disc4-input-storage/%.c $(wildcard $(SHARED)/*.h ../disc4-input-storage/*.h) | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

CFLAGS += -ffunction-sections -fdata-sections
LDFLAGS += --gc-sections
