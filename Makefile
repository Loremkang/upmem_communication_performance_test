CC = g++
INCLUDE_LIBS = -Ipim_interface -Ipim_interface/sdk_internals_2023.2.0
INCLUDE_THIRD_PARTY = -Ithird_party/exprtk/include -Ithird_party/argparse/include -Ithird_party/json/include -Ithird_party/parlaylib/include
CCFLAGS = -Wall -Wextra -O3 `dpu-pkg-config --cflags --libs dpu` -std=c++17 -march=native -pthread -fconcepts
CCDPU = dpu-upmem-dpurte-clang

.PHONY: all debug clean
all: host dpu

debug: CCFLAGS += -DXFER_BACK
# debug: CCFLAGS += -g -DXFER_BACK
debug: host dpu

host: host.cpp pim_interface
	$(CC) -o host host.cpp $(INCLUDE_LIBS) $(INCLUDE_THIRD_PARTY) $(CCFLAGS)

dpu: dpu.c
	$(CCDPU) -o dpu dpu.c

clean:
	rm -f host dpu