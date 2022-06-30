#include <dpu.h>
#include <dpu_management.h>
#include <stdlib.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <numa.h>
#include <numaif.h>
// #include <utmpx.h>
#include <sys/mman.h>

// #define __USE_GNU
// #include <sched.h>

#include <dpu_target_macros.h>

#define DPU_BINARY "dpu"

// Number of iteration of transfer to/from DPU
#define DPU_NB_ITER 100UL // 200UL

// Number of ranks to allocate
#define DPU_NB_RANKS 32

// the size in bytes of the transfer to the DP0U
#define XFER_SIZE 16 * 1024 //(256 * 1024)

// the size in bytes of the transfer from the DPU
#define XFER_SIZE_FROM XFER_SIZE //(0 * 1024 * 1024)

// get time in secs
static inline double my_clock(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC_RAW, &t);
    return (1.0e-9 * t.tv_nsec + t.tv_sec);
}

// generate random input buffers to be sent to DPU
static void gen_rnd_buffers(uint8_t** buffers, uint32_t nr_dpus)
{
    for (uint32_t each_dpu = 0; each_dpu < nr_dpus; ++each_dpu) {
        for (uint32_t i = 0; i < XFER_SIZE; ++i) {
            buffers[each_dpu][i] = (uint8_t)rand();
        }
    }
}

// size_t get_numa_node_id(dpu_rank_id_t rank_id) { return rank_id / 8; }

dpu_error_t thread_alloc_buffers(struct dpu_set_t rank, uint32_t rank_id, void* args)
{
    // int cpu = sched_getcpu();
    // int node_internal = numa_node_of_cpu(cpu);
    uint8_t** buffers = ((uint8_t***)args)[0];
    uint8_t** buffers_recv = ((uint8_t***)args)[1];

    // int physical_rank_id = dpu_get_rank_id(dpu_rank_from_set(rank)) & DPU_TARGET_MASK;
    // int node = get_numa_node_id(physical_rank_id);
    int numa_node_id = dpu_get_rank_numa_node(dpu_rank_from_set(rank));

    // if(node_internal != node) {
    //     printf("[%d] Warning: DPU rank %d is on node %d, but thread %d is on node %d\n",
    //            rank_id, physical_rank_id, node, cpu, node_internal);
    //     exit(1);
    // }

    // printf("Rank %d is running on CPU %d, NUMA node %d\n", physical_rank_id,
    // cpu, node);

    // printf("Rank %d has physical id %d\n", rank_id, physical_rank_id);

    // struct dpu_set_t dpu;
    // unsigned each_dpu;
    // DPU_FOREACH(rank, dpu, each_dpu)
    // {
    //     // printf("alloc, each_dpu = %d\n", each_dpu);
    //     // if (rank_id == 31) {
    //     //     printf("rank %d, dpu %d\n", rank_id, each_dpu);
    //     // }
    //     buffers[64 * rank_id + each_dpu] = (uint8_t*)calloc(XFER_SIZE, sizeof(uint8_t));
    //     buffers_recv[64 * rank_id + each_dpu]
    //         = (uint8_t*)calloc(XFER_SIZE_FROM, sizeof(uint8_t));
    //     // fill with random data
    //     // printf("fill\n");
    //     // gen_rnd_buffers(buffers + 64 * rank_id + each_dpu, 1);
    //     for (uint32_t i = 0; i < XFER_SIZE; ++i) {
    //         buffers[64 * rank_id + each_dpu][i] = (uint8_t)rand();
    //     }
    // }

    for (unsigned each_dpu = 0; each_dpu < 64; ++each_dpu)
    {
        // buffers[64 * rank_id + each_dpu] = (uint8_t*)calloc(XFER_SIZE, sizeof(uint8_t));
        buffers[64 * rank_id + each_dpu] = (uint8_t*)numa_alloc_onnode(XFER_SIZE * sizeof(uint8_t), numa_node_id);
        for (uint32_t i = 0; i < XFER_SIZE; i++)
        {
            buffers[64 * rank_id + each_dpu][i] = (uint8_t)rand();
        }

        // buffers_recv[64 * rank_id + each_dpu]
        //     = (uint8_t*)calloc(XFER_SIZE, sizeof(uint8_t));
        buffers_recv[64 * rank_id + each_dpu]
            = (uint8_t*)numa_alloc_onnode(XFER_SIZE_FROM * sizeof(uint8_t), numa_node_id);
    }

    // struct dpu_set_t dpu;
    // struct dpu_set_t rank;
    // unsigned each_dpu = 0;
    // DPU_FOREACH(dpu_set, dpu, each_dpu)
    // {
    //     dpu_rank_id_t rank_id
    //         = dpu_get_rank_id(dpu_get_rank(dpu_from_set(dpu))) & DPU_TARGET_MASK;
    //     size_t numa_node_id = get_numa_node_id(rank_id);

    //     // uint32_t i = dpu_get_id(dpu_from_set(dpu));
    //     buffers[each_dpu]
    //         = (uint8_t*)numa_alloc_onnode(XFER_SIZE * sizeof(uint8_t), numa_node_id);
    //     buffers_recv[each_dpu] = (uint8_t*)numa_alloc_onnode(
    //         XFER_SIZE_FROM * sizeof(uint8_t), numa_node_id);
    // }

    dpu_error_t err = DPU_OK;
    return err;
}

dpu_error_t thread_fill_buffers(struct dpu_set_t rank, uint32_t rank_id, void* args)
{
    uint8_t** buffers = ((uint8_t***)args)[0];
    uint8_t** buffers_recv = ((uint8_t***)args)[1];

    for (unsigned each_dpu = 0; each_dpu < 64; ++each_dpu)
    {
        for (uint32_t i = 0; i < XFER_SIZE; i++)
        {
            buffers[64 * rank_id + each_dpu][i] = (uint8_t)rand();
        }
    }

    return DPU_OK;
}


void prepare_xfer(struct dpu_set_t dpu_set, uint8_t** buffers)
{
    unsigned each_dpu, each_rank;
    struct dpu_set_t dpu, rank;
    DPU_RANK_FOREACH(dpu_set, rank, each_rank)
    {
        DPU_FOREACH(rank, dpu, each_dpu)
        {
            for (uint32_t i = 0; i < XFER_SIZE; ++i) {
                buffers[64 * each_rank + each_dpu][i] = (uint8_t)rand();
            }
            DPU_ASSERT(dpu_prepare_xfer(dpu, buffers[64 * each_rank + each_dpu]));
        }
    }
}

dpu_error_t callback_prepare_xfer(struct dpu_set_t rank, uint32_t rank_id, void* args)
{
    uint8_t** buffers = (uint8_t**)args;

    struct dpu_set_t dpu;
    unsigned each_dpu;
    DPU_FOREACH(rank, dpu, each_dpu)
    {
        DPU_ASSERT(dpu_prepare_xfer(dpu, buffers[64 * rank_id + each_dpu]));
    }

    dpu_error_t err = DPU_OK;
    return err;
}

dpu_error_t
callback_prepare_push_xfer(struct dpu_set_t rank, uint32_t rank_id, void* args)
{
    uint8_t** buffers = (uint8_t**)args;

    struct dpu_set_t dpu;
    unsigned each_dpu;
    DPU_FOREACH(rank, dpu, each_dpu)
    {
        DPU_ASSERT(dpu_prepare_xfer(dpu, buffers[64 * rank_id + each_dpu]));
    }
    DPU_ASSERT(dpu_push_xfer(
        rank,
        DPU_XFER_TO_DPU,
        DPU_MRAM_HEAP_POINTER_NAME,
        0,
        XFER_SIZE,
        DPU_XFER_DEFAULT));

    dpu_error_t err = DPU_OK;
    return err;
}

void assert_equals(uint8_t** buf1, uint8_t** buf2, struct dpu_set_t dpu_set)
{
    struct dpu_set_t rank, dpu;
    unsigned each_dpu, each_rank;

    DPU_RANK_FOREACH(dpu_set, rank, each_rank)
    {
        DPU_FOREACH(rank, dpu, each_dpu)
        {
            for (uint32_t i = 0; i < XFER_SIZE; ++i) {
                if (buf1[64 * each_rank + each_dpu][i]
                    != buf2[64 * each_rank + each_dpu][i]) {
                    printf(
                        "buf1[%d][%d] = %d, buf2[%d][%d] = %d\n",
                        each_rank,
                        each_dpu,
                        buf1[64 * each_rank + each_dpu][i],
                        each_rank,
                        each_dpu,
                        buf2[64 * each_rank + each_dpu][i]);
                    exit(1);
                }
            }
        }
    }
}

// uint8_t buffers[2048][16 * 1024 * sizeof(uint8_t)];

int main()
{
    struct dpu_set_t dpu_set;
    DPU_ASSERT(dpu_alloc_ranks(DPU_NB_RANKS, "nrJobsPerRank=256", &dpu_set));
    DPU_ASSERT(dpu_load(dpu_set, DPU_BINARY, NULL));

    uint32_t nr_of_dpus = 0, nr_of_ranks = 0;
    DPU_ASSERT(dpu_get_nr_dpus(dpu_set, &nr_of_dpus));
    DPU_ASSERT(dpu_get_nr_ranks(dpu_set, &nr_of_ranks));
    printf("Allocated %d DPU(s)\n", nr_of_dpus);
    printf("Allocated %d Ranks(s)\n", nr_of_ranks);

    // numa initialization
    {
        int available = numa_available();
        if (available == -1)
        {
            printf("numa_available() failed\n");
            exit(EXIT_FAILURE);
        }
    }

    int randomData = open("/dev/urandom", O_RDONLY);
    if(randomData < 0)
    {
        printf("Error opening /dev/urandom\n");
        exit(1);
    }

    // allocate buffers for each DPU, one for the input and one for the output
    /* uint8_t **buffers = (uint8_t **)calloc(nr_of_dpus, sizeof(uint8_t *));
    uint8_t **buffers_recv = (uint8_t **)calloc(nr_of_dpus, sizeof(uint8_t *)); */
    /* uint8_t **buffers = (uint8_t **)numa_alloc_interleaved(nr_of_dpus *
    sizeof(uint8_t *)); uint8_t **buffers_recv = (uint8_t
    **)numa_alloc_interleaved(nr_of_dpus * sizeof(uint8_t *)); */
    /* buffers[0] = (uint8_t *)numa_alloc_interleaved(nr_of_dpus * XFER_SIZE *
    sizeof(uint8_t)); buffers_recv[0] = (uint8_t
    *)numa_alloc_interleaved(nr_of_dpus * XFER_SIZE_FROM * sizeof(uint8_t)); */

    uint8_t** buffers = (uint8_t**)calloc(64 * nr_of_ranks, sizeof(uint8_t*));
    uint8_t** buffers_recv = (uint8_t**)calloc(64 * nr_of_ranks, sizeof(uint8_t*));

    // for (uint32_t i = 0; i < nr_of_dpus; ++i) {
    //     buffers[i] = (uint8_t *)calloc(XFER_SIZE, sizeof(uint8_t));
    //     buffers_recv[i] = (uint8_t *)calloc(XFER_SIZE_FROM, sizeof(uint8_t));
    //     // buffers[i] = (uint8_t*)numa_alloc_onnode(XFER_SIZE * sizeof(uint8_t), i %
    //     2);
    //     // buffers_recv[i]
    //     //     = (uint8_t*)numa_alloc_onnode(XFER_SIZE_FROM * sizeof(uint8_t), i %
    //     2);
    //     /* buffers[i+1] = buffers[i] + XFER_SIZE;
    //     buffers_recv[i+1] = buffers_recv[i] + XFER_SIZE_FROM; */
    // }

    // alloc buffers by DPU
    // {
    //     struct dpu_set_t dpu;
    //     unsigned each_dpu = 0;
    //     // DPU_FOREACH(dpu_set, dpu, each_dpu)
    //     for(unsigned each_dpu = 0; each_dpu < 64 * nr_of_ranks; ++each_dpu)
    //     {
    //         // dpu_rank_id_t rank_id
    //         //     = dpu_get_rank_id(dpu_get_rank(dpu_from_set(dpu))) & 
    //         //DPU_TARGET_MASK;
    //         // size_t numa_node_id = get_numa_node_id(rank_id);

    //         // uint32_t i = dpu_get_id(dpu_from_set(dpu));
    //         // buffers[each_dpu] = (uint8_t*)numa_alloc_onnode(
    //         //     XFER_SIZE * sizeof(uint8_t), numa_node_id);
    //         // buffers_recv[each_dpu] = (uint8_t*)numa_alloc_onnode(
    //         //     XFER_SIZE_FROM * sizeof(uint8_t), numa_node_id);
    //         buffers[each_dpu] = (uint8_t*)calloc(XFER_SIZE, sizeof(uint8_t));
    //         buffers_recv[each_dpu] = (uint8_t*)calloc(XFER_SIZE_FROM, sizeof(uint8_t));
    //         // for(int i = 0; i < XFER_SIZE; i++)
    //         //     buffers[each_dpu][i] = (uint8_t)rand();
    //     }
    // }

    // alloc buffers by rank
    {
        struct dpu_set_t dpu;
        struct dpu_set_t rank;
        unsigned each_dpu, rank_id;
        DPU_RANK_FOREACH(dpu_set, rank, rank_id)
        {
            // dpu_rank_id_t physical_rank_id
            //     = dpu_get_rank_id(dpu_rank_from_set(rank)) & DPU_TARGET_MASK;
            // size_t numa_node_id = get_numa_node_id(physical_rank_id);
            int numa_node_id = dpu_get_rank_numa_node(dpu_rank_from_set(rank));
            // printf("rank_id: %d, physical_rank_id: %d, numa_node_id: %ld, numa_api: %d\n",
            //     rank_id, physical_rank_id, numa_node_id, numa_api);
            for(unsigned each_dpu = 0; each_dpu < 64; ++each_dpu)
            // DPU_FOREACH(rank, dpu, each_dpu)
            {
                // uint32_t i = dpu_get_id(dpu_from_set(dpu));

                // alloc one buffer per existing DPUs:
                // buffers[each_dpu] = (uint8_t*)numa_alloc_onnode(
                //     XFER_SIZE * sizeof(uint8_t), numa_node_id);
                // buffers_recv[each_dpu] = (uint8_t*)numa_alloc_onnode(
                //     XFER_SIZE_FROM * sizeof(uint8_t), numa_node_id);

                // alloc 64 buffers per rank on numa node
                // buffers[64 * rank_id + each_dpu] = (uint8_t*)numa_alloc_onnode(
                //     XFER_SIZE * sizeof(uint8_t), numa_node_id);
                // buffers_recv[64 * rank_id + each_dpu] = (uint8_t*)numa_alloc_onnode(
                //     XFER_SIZE_FROM * sizeof(uint8_t), numa_node_id);

                // alloc 64 buffers per rank on wrong numa node
                // buffers[64 * rank_id + each_dpu] = (uint8_t*)numa_alloc_onnode(
                //     XFER_SIZE * sizeof(uint8_t), (numa_node_id + 1) % 4);
                // buffers_recv[64 * rank_id + each_dpu] = (uint8_t*)numa_alloc_onnode(
                //     XFER_SIZE_FROM * sizeof(uint8_t), (numa_node_id + 1) % 4);

                // alloc 64 buffers per rank on numa node 0
                // buffers[64 * rank_id + each_dpu] = (uint8_t*)numa_alloc_onnode(
                //     XFER_SIZE * sizeof(uint8_t), 0);
                // buffers_recv[64 * rank_id + each_dpu] = (uint8_t*)numa_alloc_onnode(
                //     XFER_SIZE_FROM * sizeof(uint8_t), (numa_node_id + 1) % 4);


                // alloc 64 buffers per rank interleaved on numa nodes
                // buffers[64 * rank_id + each_dpu] = (uint8_t*)numa_alloc_interleaved(
                //     XFER_SIZE * sizeof(uint8_t));
                // buffers_recv[64 * rank_id + each_dpu] = (uint8_t*)numa_alloc_interleaved(
                //     XFER_SIZE_FROM * sizeof(uint8_t));

                // lock buffers
                // mlock(buffers[64 * rank_id + each_dpu], XFER_SIZE * sizeof(uint8_t));
                // mlock(buffers_recv[64 * rank_id + each_dpu], XFER_SIZE_FROM * sizeof(uint8_t));

                // regular alloc
                buffers[64 * rank_id + each_dpu]
                    = (uint8_t*)malloc(512 * 1024 * sizeof(uint8_t));
                buffers_recv[64 * rank_id + each_dpu]
                    = (uint8_t*)malloc(XFER_SIZE_FROM * sizeof(uint8_t));

                // fill buffers with random ints
                for(int i = 0; i < XFER_SIZE; i++) {
                    // buffers[64 * rank_id + each_dpu][i] = (uint8_t)rand();
                    int dpuid = 64 * rank_id + each_dpu;
                    buffers[dpuid][i] = (uint8_t)(i + dpuid);
                }
                // read(randomData, buffers[64 * rank_id + each_dpu], XFER_SIZE);
            }
            // int numa_node = -1;
            // get_mempolicy(&numa_node, NULL, 0, (void*)buffers[64 * rank_id + 50],
            //     MPOL_F_NODE | MPOL_F_ADDR);
            // printf("rank %d numa_node %d (intended %ld)\n", rank_id, numa_node, numa_node_id);
        }
    }

    // alloc one interleaved buffer and map buffers to it
    // {
    //     struct dpu_set_t rank;
    //     unsigned rank_id;
    //     buffers[0] = (uint8_t*)numa_alloc_interleaved(XFER_SIZE * sizeof(uint8_t) * 64 * nr_of_ranks);
    //     buffers_recv[0] = (uint8_t*)numa_alloc_interleaved(XFER_SIZE_FROM * sizeof(uint8_t) * 64 * nr_of_ranks);
    //     // buffers[0] = (uint8_t*)malloc(XFER_SIZE * sizeof(uint8_t) * 64 * nr_of_ranks);
    //     // buffers_recv[0] = (uint8_t*)malloc(XFER_SIZE_FROM * sizeof(uint8_t) * 64 * nr_of_ranks);
    //     DPU_RANK_FOREACH(dpu_set, rank, rank_id)
    //     {
    //         for(unsigned each_dpu = 0; each_dpu < 64; ++each_dpu)
    //         {
    //             buffers[64 * rank_id + each_dpu]
    //                 // = (uint8_t*)numa_alloc_interleaved(XFER_SIZE * sizeof(uint8_t));
    //                 = &buffers[0][(64 * rank_id + each_dpu) * XFER_SIZE];
    //             for(int i = 0; i < XFER_SIZE; i++)
    //                 buffers[64 * rank_id + each_dpu][i] = (uint8_t)rand();

    //             buffers_recv[64 * rank_id + each_dpu] = &buffers_recv[0][(64 * rank_id + each_dpu) * XFER_SIZE_FROM];
    //         }
    //     }
    // }

    // alloc buffers with callback
    // {
    //     uint8_t** args[2];
    //     args[0] = buffers;
    //     args[1] = buffers_recv;
    //     DPU_ASSERT(
    //         dpu_callback(dpu_set, thread_alloc_buffers, (void*)args,
    //         DPU_CALLBACK_DEFAULT));
    //     // struct dpu_set_t dpu, rank;
    //     // unsigned each_dpu, rank_id;
    //     // DPU_RANK_FOREACH(dpu_set, rank, rank_id)
    //     // {
    //     //     DPU_FOREACH(rank, dpu, each_dpu)
    //     //     {
    //     //         for(int i = 0; i < XFER_SIZE; i++)
    //     //             buffers[64 * rank_id + each_dpu][i] = (uint8_t)rand();
    //     //     }
    //     // }
    // }

    // fill buffers with callback
    // {
    //     uint8_t** args[2];
    //     args[0] = buffers;
    //     args[1] = buffers_recv;
    //     DPU_ASSERT(
    //         dpu_callback(dpu_set, thread_fill_buffers, (void*)args,
    //         DPU_CALLBACK_DEFAULT));
    // }

    // show numa node of buffers
    // {
    //     struct dpu_set_t dpu;
    //     struct dpu_set_t rank;
    //     unsigned rank_id;
    //     DPU_RANK_FOREACH(dpu_set, rank, rank_id)
    //     {
    //         dpu_rank_id_t physical_rank_id
    //             = dpu_get_rank_id(dpu_rank_from_set(rank)) & DPU_TARGET_MASK;
    //         size_t numa_node_id = get_numa_node_id(physical_rank_id);
    //         int numa_node = -1;
    //         get_mempolicy(&numa_node, NULL, 0, (void*)buffers[64 * rank_id + 50],
    //             MPOL_F_NODE | MPOL_F_ADDR);
    //         printf("rank %d numa_node %d (intended %ld)\n", rank_id, numa_node, numa_node_id);
    //     }
    // } 

    // gen_rnd_buffers(buffers, 64 * nr_of_ranks);
    // gen_rnd_buffers(buffers, nr_of_dpus);
    printf("generated input buffers\n");

    printf("transfer buffers to/from DPUs\n");
    struct dpu_set_t dpu;
    unsigned each_dpu;
    unsigned each_rank;
    struct dpu_set_t rank;
    // double start = my_clock();
    double time = 0;
    for (uint64_t i = 0; i < DPU_NB_ITER; ++i) {
        printf("iteration %ld\n", i);
        // scramble buffers to avoid cache effects
        DPU_RANK_FOREACH(dpu_set, rank, each_rank)
        {
            DPU_FOREACH(rank, dpu, each_dpu)
            {
                for(int i = 0; i < XFER_SIZE; i++)
                    buffers[64 * each_rank + each_dpu][i] += 1;
            }
        }

        double start = my_clock();
        // send to DPU
        // DPU_FOREACH(dpu_set, dpu, each_dpu)
        // {
        //     // uint32_t j = dpu_get_id(dpu_from_set(dpu));
        //     DPU_ASSERT(dpu_prepare_xfer(dpu, buffers[each_dpu]));
        //     // DPU_ASSERT(dpu_prepare_xfer(dpu, buffers[each_dpu * NR_BUFFERS /
        //     //nr_of_dpus]));
        //     // DPU_ASSERT(dpu_prepare_xfer(dpu, buffers[0]));
        // }

        DPU_RANK_FOREACH(dpu_set, rank, each_rank)
        {
            DPU_FOREACH(rank, dpu, each_dpu)
            {
                DPU_ASSERT(dpu_prepare_xfer(dpu, buffers[64 * each_rank + each_dpu]));
                // DPU_ASSERT(dpu_prepare_xfer(dpu, buffers[0]));
            }
        }
        // prepare_xfer(dpu_set, buffers);

        // DPU_ASSERT(dpu_callback(
        //     dpu_set, callback_prepare_xfer, (void*)buffers, DPU_CALLBACK_DEFAULT));
        // DPU_ASSERT(dpu_sync(dpu_set));

        // start = my_clock();
        DPU_ASSERT(dpu_push_xfer(
            dpu_set,
            DPU_XFER_TO_DPU,
            DPU_MRAM_HEAP_POINTER_NAME,
            0,
            XFER_SIZE,
            DPU_XFER_DEFAULT));
        // DPU_ASSERT(dpu_callback(
        //     dpu_set, callback_prepare_push_xfer, (void*)buffers,
        //     DPU_CALLBACK_ASYNC));
        // DPU_ASSERT(dpu_sync(dpu_set));

#ifdef XFER_BACK
        // get from DPU
        /* DPU_FOREACH(dpu_set, dpu, each_dpu)
        {
            DPU_ASSERT(dpu_prepare_xfer(dpu, buffers_recv[each_dpu]));
        }*/
        DPU_RANK_FOREACH(dpu_set, rank, each_rank)
        {
            DPU_FOREACH(rank, dpu, each_dpu)
            {
                DPU_ASSERT(
                    dpu_prepare_xfer(dpu, buffers_recv[64 * each_rank + each_dpu]));
            }
        }

        // prepare_xfer(dpu_set, buffers_recv);

        // DPU_ASSERT(dpu_callback(
        //     dpu_set, callback_prepare_xfer, (void*)buffers_recv,
        //     DPU_CALLBACK_DEFAULT));

        DPU_ASSERT(dpu_push_xfer(
            dpu_set,
            DPU_XFER_FROM_DPU,
            DPU_MRAM_HEAP_POINTER_NAME,
            0,
            XFER_SIZE_FROM,
            DPU_XFER_DEFAULT));
        // assert_equals(buffers, buffers_recv, dpu_set);
#endif
        double end = my_clock();
        time += end - start;
        printf("Time: %e\n", end - start);
    }
    DPU_ASSERT(dpu_sync(dpu_set));
    // double end = my_clock();
    // double time = end - start;
#ifdef XFER_BACK
    uint64_t size = DPU_NB_ITER * (XFER_SIZE + XFER_SIZE_FROM);
#else
    uint64_t size = DPU_NB_ITER * XFER_SIZE;
#endif
    printf(
        "Time: %e secs. size: %lu BW: %e B/s\n",
        time,
        size,
        (double)nr_of_dpus * size / (time));
    printf("BW per rank: %e B/s\n", (double)nr_of_dpus * size / (nr_of_ranks * (time)));

    // for (uint32_t i = 0; i < nr_of_dpus; ++i)
    // {
    //     numa_free(buffers[i], XFER_SIZE * sizeof(uint8_t));
    //     // numa_free(buffers_recv[i], XFER_SIZE_FROM * sizeof(uint8_t));
    // }
    // free(buffers);
    // free(buffers_recv);
    /* numa_free(buffers[0], nr_of_dpus * XFER_SIZE * sizeof(uint8_t));
    numa_free(buffers_recv[0], nr_of_dpus * XFER_SIZE_FROM * sizeof(uint8_t)); */
    /* numa_free(buffers, nr_of_dpus * sizeof(uint8_t *));
    numa_free(buffers_recv, nr_of_dpus * sizeof(uint8_t *)); */
    DPU_ASSERT(dpu_free(dpu_set));

    return 0;
}
