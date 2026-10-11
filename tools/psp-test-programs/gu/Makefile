#gu: built with pspdev's toolchain (psp-config on the PATH). make gives a static executable (gu.elf) and an EBOOT.PBP
#holding it. See ../build.sh.
TARGET = gu
OBJS = main.o
CFLAGS = -O2 -G0 -Wall
LIBS = -lpspgu

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = gu

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
