#psp-measure: built with pspdev's toolchain (psp-config on the PATH). See main.c.
TARGET = pspmeasure
OBJS = main.o results.o vfpu.o ge.o
CFLAGS = -O2 -G0 -Wall
ifdef SMOKE
CFLAGS += -DSMOKE  #a quick version for an emulator (see measure.h)
endif
ASFLAGS = $(CFLAGS)
LIBS = -lpspgu -lpsppower

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = PSP measure

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak

#after build.mak, so that its first rule stays the one make builds; build.mak doesn't follow headers itself
$(OBJS): measure.h
vfpu.o: ops.h ops3.h
