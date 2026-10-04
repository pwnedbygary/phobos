#psp-vfpu-measure: built with pspdev's toolchain (psp-config on the PATH). See main.c.
TARGET = vfpumeasure
OBJS = main.o
CFLAGS = -O2 -G0 -Wall
ifdef SMOKE
CFLAGS += -DSMOKE  #a quick version for an emulator (see main.c)
endif
ASFLAGS = $(CFLAGS)
LIBS = -lpsppower

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = VFPU measure

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
