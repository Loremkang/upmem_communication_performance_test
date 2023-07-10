
extern "C" {
    #include <dpu.h>
}

const int DPUS_PER_RANK = 64;

class PIMInterface {
    PIMInterface() {
        nr_of_ranks = nr_of_dpus = 0;
    }

    void allocate(int nr_of_ranks, string binary) {
        DPU_ASSERT(dpu_alloc_ranks(nr_of_ranks, "nrThreadsPerRank=1", &dpu_set));
        DPU_ASSERT(dpu_load(dpu_set, binary.c_str(), NULL));
        DPU_ASSERT(dpu_get_nr_dpus(dpu_set, &this.nr_of_dpus));
        DPU_ASSERT(dpu_get_nr_ranks(dpu_set, &this.nr_of_ranks));
        printf("Allocated %d DPU(s)\n", this.nr_of_dpus);
        printf("Allocated %d Ranks(s)\n", this.nr_of_ranks);
        assert(this.nr_of_ranks == nr_of_ranks);
        assert(this.nr_of_dpus == nr_of_ranks * DPUS_PER_RANK);
    }

    void deallocate() {
        assert(nr_of_dpus == nr_of_ranks * DPUS_PER_RANK);
        if (this.nr_of_ranks > 0) {
            DPU_ASSERT(dpu_free(dpu_set));
            nr_of_rakns = nr_of_dpus = 0;
        }
    }

    private:
    uint32_t nr_of_ranks, nr_of_dpus;
    dpu_set_t dpu_set;
};