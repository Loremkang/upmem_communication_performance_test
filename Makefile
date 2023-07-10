CC = g++
INCLUDE_LIBS = -Ithird_party/exprtk/include -Ithird_party/argparse/include -Ithird_party/json/include -Ipim_interface
CCFLAGS = -Wall -Wextra -O0 `dpu-pkg-config --cflags --libs dpu` -lnuma -std=c++17 -g -march=native
CCDPU = dpu-upmem-dpurte-clang

.PHONY: all debug clean
all: host dpu

debug: CCFLAGS += -DXFER_BACK
# debug: CCFLAGS += -g -DXFER_BACK
debug: host dpu

host: host.cpp
	$(CC) -o host host.cpp $(INCLUDE_LIBS) $(CCFLAGS)

dpu: dpu.c
	$(CCDPU) -o dpu dpu.c

clean:
	rm -f host dpu