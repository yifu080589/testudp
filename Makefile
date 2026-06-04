###############################################################################
# IPC / XVR Unified Makefile (NVT Style)
###############################################################################

MODULE_IPC = ipc
MODULE_XVR = xvr

CC = arm-ca9-linux-uclibcgnueabihf-gcc
# 👉 如果要 PC 測試改成：CC = gcc
NBUS_PATH = /code/hdal/nbus

CFLAGS  += -I$(NBUS_PATH)/inc \
           -I$(NBUS_PATH)

LDFLAGS += -L$(NBUS_PATH) -lnbus \
           -Wl,-rpath-link=$(NBUS_PATH)
CFLAGS = -Wall -O2 -fPIC

EXTRA_INCLUDE = -I.

.PHONY: all clean xvr ipc

###############################################################################
# default
###############################################################################
all: xvr ipc xvr

###############################################################################
# IPC (ARM / IPC side)
###############################################################################
ipc: ipc_udp_server.c
	@echo "Building IPC..."
	@$(CC) $(CFLAGS) ipc_udp_server.c -o $(MODULE_IPC)

###############################################################################
# XVR (PC or ARM)
###############################################################################
xvr: xvr_discover.c
	@echo "Building XVR..."
	@$(CC) $(CFLAGS) xvr_discover.c -o $(MODULE_XVR)

###############################################################################
# clean
###############################################################################
clean:
	@rm -f ipc xvr *.o