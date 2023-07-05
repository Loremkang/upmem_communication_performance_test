CC = g++
CCFLAGS = -Ithird_party -Ithird_party/argparse/include -Ithird_party/json/include -Wall -Wextra -O3 `dpu-pkg-config --cflags --libs dpu` -lnuma -std=c++17
CCDPU = dpu-upmem-dpurte-clang

.PHONY: all debug clean
all: host dpu

debug: CCFLAGS += -DXFER_BACK
# debug: CCFLAGS += -g -DXFER_BACK
debug: host dpu

host: host.cpp
	$(CC) -o host host.cpp $(CCFLAGS)

dpu: dpu.c
	$(CCDPU) -o dpu dpu.c

clean:
	rm -f host dpu