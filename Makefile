CC = gcc
CCFLAGS = -Wall -Wextra -O3 `dpu-pkg-config --cflags --libs dpu` -lnuma
CCDPU = dpu-upmem-dpurte-clang

.PHONY: all debug clean
all: host dpu

debug: CCFLAGS += -DXFER_BACK
# debug: CCFLAGS += -g -DXFER_BACK
debug: host dpu

host: host.c
	$(CC) -o host host.c $(CCFLAGS)

dpu: dpu.c
	$(CCDPU) -o dpu dpu.c

clean:
	rm -f host dpu