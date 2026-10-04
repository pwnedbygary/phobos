#system: built with pspdev's toolchain (psp-config on the PATH). make gives a static executable (system.elf) and an
#EBOOT.PBP holding it. See ../build.sh.
TARGET = system
OBJS = main.o
CFLAGS = -O2 -G0 -Wall
LIBS =

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = system

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
