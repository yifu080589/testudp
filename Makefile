CC = arm-ca9-linux-uclibcgnueabihf-gcc

NBUS_PATH = /home/rd7/Project/NT9856x/code/hdal/nbus

CFLAGS = -Wall -O2 -fPIC \
         -I$(NBUS_PATH)/inc \
         -I.

LDFLAGS = -L$(NBUS_PATH) -lnbus \
          -Wl,-rpath-link=$(NBUS_PATH)

MODULE_IPC = ipc
MODULE_XVR = xvr

.PHONY: all clean ipc xvr

all: ipc xvr

ipc: ipc_udp_server.c
	$(CC) $(CFLAGS) $< -o $(MODULE_IPC) $(LDFLAGS)

xvr: xvr_discover.c
	$(CC) $(CFLAGS) $< -o $(MODULE_XVR)

clean:
	rm -f ipc xvr