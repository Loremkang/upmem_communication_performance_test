#pragma once

#include <string>
#include <cstdio>
#include <cassert>

extern "C" {
    #include <dpu.h>
}

const uint32_t DPU_PER_RANK = 64;
const uint64_t MRAM_SIZE = (64 << 20);

class PIMInterface {
    public:
    PIMInterface() {
        nr_of_ranks = nr_of_dpus = 0;
    }

    virtual void allocate(uint32_t nr_of_ranks, std::string binary) {
        assert(this->nr_of_ranks == 0 && this->nr_of_dpus == 0);
        DPU_ASSERT(dpu_alloc_ranks(nr_of_ranks, "nrThreadsPerRank=1", &dpu_set));
        DPU_ASSERT(dpu_load(dpu_set, binary.c_str(), NULL));
        DPU_ASSERT(dpu_get_nr_dpus(dpu_set, &this->nr_of_dpus));
        DPU_ASSERT(dpu_get_nr_ranks(dpu_set, &this->nr_of_ranks));
        std::printf("Allocated %d DPU(s)\n", this->nr_of_dpus);
        std::printf("Allocated %d Ranks(s)\n", this->nr_of_ranks);
        assert(this->nr_of_ranks == nr_of_ranks);
        assert(this->nr_of_dpus == nr_of_ranks * DPU_PER_RANK);
    }

    virtual void deallocate() {
        assert(nr_of_dpus == nr_of_ranks * DPU_PER_RANK);
        if (nr_of_ranks > 0) {
            DPU_ASSERT(dpu_free(dpu_set));
            nr_of_ranks = nr_of_dpus = 0;
        }
    }

    void Launch(bool async) {
        auto async_parameter = async ? DPU_ASYNCHRONOUS : DPU_SYNCHRONOUS;
        DPU_ASSERT(dpu_launch(dpu_set, async_parameter));
    }

    void sync() {
        DPU_ASSERT(dpu_sync(dpu_set));
    }

    void PrintLog() {
        DPU_FOREACH(dpu_set, dpu) { DPU_ASSERT(dpu_log_read(dpu, stdout)); }
    }

    virtual void SendToPIM(uint8_t** buffers, std::string symbol_name, uint32_t symbol_offset, uint32_t length, bool async) = 0;
    virtual void ReceiveFromPIM(uint8_t** buffers, std::string symbol_name, uint32_t symbol_offset, uint32_t length, bool async) = 0;

    ~PIMInterface() {
        deallocate();
    }

    uint32_t nr_of_ranks, nr_of_dpus;

    protected:
    dpu_set_t dpu_set, dpu;
    uint32_t each_dpu;
};