#psp-ge-measure: built with pspdev's toolchain (psp-config on the PATH). See main.c.
TARGET = gemeasure
OBJS = main.o
CFLAGS = -O2 -G0 -Wall
ifdef SMOKE
CFLAGS += -DSMOKE  #a version for an emulator (see main.c)
endif
LIBS = -lpspgu

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = GE measure

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
