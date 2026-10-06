#psp-flash0-dump: built with pspdev's toolchain (psp-config on the PATH). See main.c.
TARGET = flash0dump
OBJS = main.o sha256.o
CFLAGS = -O2 -G0 -Wall
ifdef SMOKE
CFLAGS += -DSMOKE  #a version for an emulator: no buttons to wait for (see main.c)
endif
ASFLAGS = $(CFLAGS)
LIBS = -lpsppower

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = flash0 dump

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak

#after build.mak, so that its first rule stays the one make builds; build.mak doesn't follow headers itself
$(OBJS): sha256.h
