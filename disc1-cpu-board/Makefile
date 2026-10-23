NAME = disc1-cpu-board
OBJECTS = $(OBJDIR)/main.o $(OBJDIR)/rom_slices.o $(OBJDIR)/t11_census.o \
    $(OBJDIR)/t12_memory.o $(OBJDIR)/t13_bus.o $(OBJDIR)/t13_loops.o $(OBJDIR)/t14_timer2.o $(OBJDIR)/t15_rtc.o \
    $(OBJDIR)/t16_protect.o $(OBJDIR)/t17_flags.o $(OBJDIR)/t17_ops.o $(OBJDIR)/t18_except.o $(OBJDIR)/t18_except_asm.o \
    $(OBJDIR)/t19_fpu.o $(OBJDIR)/t19_fpu_asm.o $(OBJDIR)/t110_fpu.o $(OBJDIR)/t110_fpu_asm.o
ALLOW_486 = $(OBJDIR)/t17_ops.o $(OBJDIR)/t19_fpu_asm.o
FLOPPY = yes

include ../shared/towns.mak
