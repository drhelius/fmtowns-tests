NAME = disc3-sound-cd
OBJECTS = $(OBJDIR)/main.o $(OBJDIR)/cd.o $(OBJDIR)/sound.o $(OBJDIR)/t31_buzzer.o $(OBJDIR)/t32_ssg.o \
    $(OBJDIR)/t33_ym.o $(OBJDIR)/t34_coupling.o $(OBJDIR)/t35_stop.o $(OBJDIR)/t36_pregap.o $(OBJDIR)/t37_state.o \
    $(OBJDIR)/t38_timing.o $(OBJDIR)/t39_transfer.o $(OBJDIR)/t310_errors.o $(OBJDIR)/t311_lid.o $(OBJDIR)/t312_play.o \
    $(OBJDIR)/t313_irq.o
IMAGE_OPTIONS = --sectors 40000 --audio $(OBJDIR)/tracks/tracks.txt

include ../shared/towns.mak

$(NAME).bin: $(OBJDIR)/tracks/tracks.txt

$(OBJDIR)/tracks/tracks.txt: tracks/mktracks.py | $(OBJDIR)
	$(PYTHON) tracks/mktracks.py $(OBJDIR)/tracks
