// #include <dpu.h>
#include <stdlib.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include "argparse/argparse.hpp"
#include <iostream>
#include <fstream>
#include <chrono>
#include "nlohmann/json.hpp"
#include <x86intrin.h>
#include <immintrin.h>
// #include "exprtk.hpp"


#define BANK_START(dpu_id) (0x40000 * ((dpu_id) % 4) + ((dpu_id >= 4) ? 0x40 : 0))
#define BANK_OFFSET_NEXT_DATA(i) (i * 16) // For each 64bit word, you must jump 16 * 64bit (2 cache lines)
#define BANK_CHUNK_SIZE 0x20000
#define BANK_NEXT_CHUNK_OFFSET 0x100000

using namespace std;
using namespace std::chrono;
using json = nlohmann::json;

extern "C" {
    #include <dpu.h>
    #include <dpu_management.h>
    #include <dpu_target_macros.h>
    #include <dpu_types.h>
    #include <dpu_rank.h>
    #include <dpu_program.h>
    #include "dpu_region_address_translation.h"
    #include "hw_dpu_sysfs.h"

    typedef struct _fpga_allocation_parameters_t {
        bool activate_ila;
        bool activate_filtering_ila;
        bool activate_mram_bypass;
        bool activate_mram_refresh_emulation;
        unsigned int mram_refresh_emulation_period;
        char *report_path;
        bool cycle_accurate;
    } fpga_allocation_parameters_t;

    typedef struct _hw_dpu_rank_allocation_parameters_t {
        struct dpu_rank_fs rank_fs;
        struct dpu_region_address_translation translate;
        uint64_t region_size;
        uint8_t mode, dpu_chip_id, backend_id;
        uint8_t channel_id;
        uint8_t *ptr_region;
        bool bypass_module_compatibility;
        /* Backends specific */
        fpga_allocation_parameters_t fpga;
    } * hw_dpu_rank_allocation_parameters_t;
}

#define DPU_BINARY "dpu"

#define DPU_PER_RANK 64

void byte_interleave_avx2(uint64_t *input, uint64_t *output) {
        __m256i tm = _mm256_set_epi8(
            15, 11, 7, 3, 14, 10, 6, 2, 13, 9, 5, 1, 12, 8, 4, 0,

            15, 11, 7, 3, 14, 10, 6, 2, 13, 9, 5, 1, 12, 8, 4, 0);
        char *src1 = (char *)input, *dst1 = (char *)output;

        __m256i vindex = _mm256_setr_epi32(0, 8, 16, 24, 32, 40, 48, 56);
        __m256i perm = _mm256_setr_epi32(0, 4, 1, 5, 2, 6, 3, 7);

        __m256i load0 = _mm256_i32gather_epi32((int *)src1, vindex, 1);
        __m256i load1 = _mm256_i32gather_epi32((int *)(src1 + 4), vindex, 1);

        __m256i transpose0 = _mm256_shuffle_epi8(load0, tm);
        __m256i transpose1 = _mm256_shuffle_epi8(load1, tm);

        __m256i final0 = _mm256_permutevar8x32_epi32(transpose0, perm);
        __m256i final1 = _mm256_permutevar8x32_epi32(transpose1, perm);

        _mm256_storeu_si256((__m256i *)&dst1[0], final0);
        _mm256_storeu_si256((__m256i *)&dst1[32], final1);
}

static uint32_t apply_address_translation_on_mram_offset(uint32_t byte_offset) {
        /* We have observed that, within the 26 address bits of the MRAM
         * address, we need to apply an address translation:
         *
         * virtual[13: 0] = physical[13: 0]
         * virtual[20:14] = physical[21:15]
         * virtual[   21] = physical[   14]
         * virtual[25:22] = physical[25:22]
         *
         * This function computes the "virtual" mram address based on the given
         * "physical" mram address.
         */

        uint32_t mask_21_to_15 = ((1 << (21 - 15 + 1)) - 1) << 15;
        uint32_t mask_21_to_14 = ((1 << (21 - 14 + 1)) - 1) << 14;
        uint32_t bits_21_to_15 = (byte_offset & mask_21_to_15) >> 15;
        uint32_t bit_14 = (byte_offset >> 14) & 1;
        uint32_t unchanged_bits = byte_offset & ~mask_21_to_14;

        return unchanged_bits | (bits_21_to_15 << 14) | (bit_14 << 21);
}

int nr_iters, nr_ranks, nr_dpus;
dpu_set_t dpu_set;

auto GetConfigFilename(int argc, char *argv[]) {
        argparse::ArgumentParser program("parser");

        program.add_argument("--config_file")
            .help("configuration json file required.")
            .required();

        try {
            program.parse_args(argc, argv);  // Example: ./main --color orange
        } catch (const std::runtime_error &err) {
            std::cerr << err.what() << std::endl;
            std::cerr << program;
            std::exit(1);
        }
        return program.get<string>("--config_file");
        // return {dpu_nr_ranks, nr_iters};
}

// auto ParseConfig(string fileName) {
//     auto ifile = ifstream(fileName);
//     json data = json::parse(ifile);
//     cout<<data<<endl;
//     cout<<data["nr_iters"]<<endl;
//     cout<<data["nr_ranks"]<<endl;
//     cout<<data["workload"]<<endl;
//     auto workload = data["workload"];
//     // return {data["nr_iters"], data["nr_ranks"], data["workload"]};
// }

void BusyLoopInSec(double Seconds) {
        auto start_time = high_resolution_clock::now();
        while (true) {
            auto current_time = high_resolution_clock::now();
            auto d = duration_cast<duration<double>>(current_time - start_time);
            if (d.count() > Seconds) {
                break;
            }
        }
}

int64_t busy_wait_a = 1e9 + 7;
int64_t busy_wait_b = 1e9 + 9;

void BusyLoopInInst(int64_t multiplications) {
        for (; multiplications > 0; multiplications--) {
            busy_wait_a = busy_wait_a * busy_wait_b;
        }
}

void BusyLoopTest() {
        // test the skewness of BusyLoop
        // result : 4e-8 second
        auto start_time = high_resolution_clock::now();
        double total_time = 10.0;
        double each_time = 1e-7;
        for (int i = 0; i < total_time / each_time; i++) {
            BusyLoopInSec(each_time);
        }
        auto current_time = high_resolution_clock::now();
        auto d = duration_cast<duration<double>>(current_time - start_time);
        cout << d.count() << endl;
}

auto initBuffer(auto &workload) {
        int workload_size = workload.size();
        assert(workload_size > 0);
        // assert(workload[workload_size - 1]["type"] == "sync");

        uint8_t ***buffers =
            (uint8_t ***)calloc(workload_size, sizeof(uint8_t **));
        int heap_offset = 0;

        for (int i = 0; i < workload_size; i++) {
            string type = workload[i]["type"];
            if (type == "send" || type == "receive") {
                auto lenObject = workload[i]["total_length"];
                assert(!lenObject.is_null());
                int totalLength = 0;
                if (lenObject.is_number_integer()) {
                    totalLength = lenObject;
                } else {
                    assert(false);
                }

                cout << i << " " << type << " " << totalLength << endl;
                int bufferLength = totalLength / nr_dpus;
                bufferLength = ((bufferLength - 1) / 8 + 1) * 8;
                workload[i]["buffer_length"] = bufferLength;
                workload[i]["offset"] = heap_offset;
                heap_offset += bufferLength;
                buffers[i] = (uint8_t **)calloc(nr_dpus, sizeof(uint8_t *));
                for (int j = 0; j < nr_dpus; j++) {
                    buffers[i][j] =
                        (uint8_t *)calloc(bufferLength, sizeof(uint8_t));
                }
            }
        }
        assert(heap_offset < (2 << 20));  // no more than 2 MB
        return buffers;
}

void clearBuffer(auto &workload, auto buffers) {
        int workload_size = workload.size();
        for (int i = 0; i < workload_size; i++) {
            string type = workload[i]["type"];
            if (type == "send" || type == "receive") {
                int bufferLength = workload[i]["buffer_length"];
                for (int j = 0; j < nr_dpus; j++) {
                    for (int k = 0; k < bufferLength; k++) {
                        buffers[i][j][k] = (uint8_t)rand();
                    }
                }
            }
        }
}

auto runOneRound(auto &workload, auto &buffers) {
        int workload_size = workload.size();
        vector<double> timeSpent;
        uint32_t each_dpu;
        dpu_set_t dpu;

        for (int i = 0; i < workload_size; i++) {
            string type = workload[i]["type"];

            auto start_time = high_resolution_clock::now();
            if (type == "send") {
                int heapOffset = workload[i]["offset"];
                int bufferLength = workload[i]["buffer_length"];
                DPU_FOREACH(dpu_set, dpu, each_dpu) {
                    DPU_ASSERT(dpu_prepare_xfer(dpu, buffers[i][each_dpu]));
                }
                auto sync_setup = (workload[i]["mode"] == "sync")
                                      ? DPU_XFER_DEFAULT
                                      : DPU_XFER_ASYNC;
                DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU,
                                         DPU_MRAM_HEAP_POINTER_NAME, heapOffset,
                                         bufferLength, sync_setup));
                // unfinished
            } else if (type == "receive") {
                int heapOffset = workload[i]["offset"];
                int bufferLength = workload[i]["buffer_length"];
                DPU_FOREACH(dpu_set, dpu, each_dpu) {
                    DPU_ASSERT(dpu_prepare_xfer(dpu, buffers[i][each_dpu]));
                }
                auto sync_setup = (workload[i]["mode"] == "sync")
                                      ? DPU_XFER_DEFAULT
                                      : DPU_XFER_ASYNC;
                DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_FROM_DPU,
                                         DPU_MRAM_HEAP_POINTER_NAME, heapOffset,
                                         bufferLength, sync_setup));
            } else if (type == "sync") {
                DPU_ASSERT(dpu_sync(dpu_set));
            } else if (type == "exec") {
                DPU_ASSERT(dpu_launch(dpu_set, DPU_ASYNCHRONOUS));
            } else if (type == "busywait") {
                int inst = workload[i]["inst"];
                BusyLoopInInst(inst);
            } else {
                assert(false);
            }
            auto current_time = high_resolution_clock::now();
            auto d = duration_cast<duration<double>>(current_time - start_time);
            timeSpent.push_back(d.count());
        }
        return timeSpent;
}


void PrintCacheline(uint64_t* ptr) {
    __builtin_ia32_clflushopt((void*)ptr);
    uint64_t cache_line[8], cache_line_interleave[8];
    for (int k = 0; k < 8; k ++) {
        cache_line[k] = ptr[k];
        // printf("%llu ", ptr[j + k]);
    }
    byte_interleave_avx2(cache_line, cache_line_interleave);
    for (int k = 0; k < 8; k ++) {
        printf("%016llx ", cache_line_interleave[k]);
    }
    cout<<endl;
}

void PrintCachelines(uint64_t* ptr, int count = 128) {
    int r = 8 * count;
    for (int j = 0; j < r; j += 8) {
        PrintCacheline(ptr + j);
    }
    cout<<endl;
}

int main(int argc, char *argv[]) {
        string configJson = GetConfigFilename(argc, argv);
        auto configFile = ifstream(configJson);
        json config = json::parse(configFile);

        nr_iters = config["nr_iters"];
        nr_ranks = config["nr_ranks"];
        nr_dpus = nr_ranks * DPU_PER_RANK;

        // DPU_ASSERT(dpu_alloc_ranks(nr_ranks, "nrJobsPerRank=256", &dpu_set));
        DPU_ASSERT(dpu_alloc_ranks(nr_ranks, "nrThreadsPerRank=1", &dpu_set));
        DPU_ASSERT(dpu_load(dpu_set, DPU_BINARY, NULL));

        {
            uint32_t nr_of_dpus = 0, nr_of_ranks = 0;
            DPU_ASSERT(dpu_get_nr_dpus(dpu_set, &nr_of_dpus));
            DPU_ASSERT(dpu_get_nr_ranks(dpu_set, &nr_of_ranks));
            printf("Allocated %d DPU(s)\n", nr_of_dpus);
            printf("Allocated %d Ranks(s)\n", nr_of_ranks);

            uint64_t ids[256][1];
            uint32_t each_dpu;
            dpu_set_t dpu;
            for (int i = 0; i < nr_of_dpus; i++) {
                ids[i][0] = i;
            }
            DPU_FOREACH(dpu_set, dpu, each_dpu) {
                DPU_ASSERT(dpu_prepare_xfer(dpu, ids[each_dpu]));
            }
            DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU, "DPU_ID", 0,
                                     sizeof(uint64_t), DPU_XFER_DEFAULT));
        }

        DPU_ASSERT(dpu_launch(dpu_set, DPU_SYNCHRONOUS));

        {
            dpu_set_t dpu;
            DPU_FOREACH(dpu_set, dpu) {
                DPU_ASSERT(dpu_log_read(dpu, stdout));
            }
        }


        {
            const int bufferLength = (1 << 10) * sizeof(uint64_t);
            uint64_t buffer[256][1<<10];
            uint32_t each_dpu;
            dpu_set_t dpu;
            DPU_FOREACH(dpu_set, dpu, each_dpu) {
                DPU_ASSERT(dpu_prepare_xfer(dpu, buffer[each_dpu]));
            }
            DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_FROM_DPU,
                                            DPU_MRAM_HEAP_POINTER_NAME,
                                            0, bufferLength,
                                            DPU_XFER_DEFAULT));
            for (int i = 0; i < 10; i ++) {
                for (int j = 0; j < 20; j ++) {
                    printf("buffer[%d][%d] = %llu\n", i, j, buffer[i][j]);
                }
            }
        }

        dpu_mem_max_addr_t offset_address;
        {
            uint32_t each_dpu;
            dpu_set_t dpu;
            dpu_program_t *program = nullptr;
            DPU_FOREACH(dpu_set, dpu, each_dpu) {
                assert(dpu.kind == DPU_SET_DPU);
                dpu_t* dpuptr = dpu.dpu;
                if (!dpu_is_enabled(dpuptr)) {
                    continue;
                }
                auto this_program = dpu_get_program(dpuptr);
                if (program == nullptr) {
                    program = this_program;
                }
                assert(program == nullptr || program == this_program);
            }

            dpu_symbol_t symbol;
            const char* symbol_name = DPU_MRAM_HEAP_POINTER_NAME;
            DPU_ASSERT(dpu_get_symbol(program, symbol_name, &symbol));
            offset_address = symbol.address;
        }
        
        printf("offset_address = %llx\n", offset_address);
        offset_address ^= 0x8000000;
        offset_address = 0x100000;

        dpu_rank_t **ranks = dpu_set.list.ranks;
        hw_dpu_rank_allocation_parameters_t params[4];
        for (int T = 0; T < 4; T ++) {
            params[T] = (hw_dpu_rank_allocation_parameters_t)(ranks[T]->description->_internals.data);
        }

        for (int i = 1; i <= 7; i ++) {
            if (i == 3) continue;
            uint8_t *memory = params[1]->ptr_region;
            printf("i=%d memory=%012llx\n", i, memory);
            PrintCachelines((uint64_t*)memory);
        }

        return 0;

        auto &workload = config["workload"];
        int workload_size = workload.size();
        assert(workload.is_array());
        uint8_t ***buffers = initBuffer(workload);

        vector<double> totalTimeSpent(workload_size);
        for (int T = 0; T < nr_iters; T++) {
            clearBuffer(workload, buffers);
            vector<double> timeSpent = runOneRound(workload, buffers);
            json timeInfo(timeSpent);
            cout << timeInfo << endl;
            for (int i = 0; i < workload_size; i++) {
                totalTimeSpent[i] += timeSpent[i];
            }
        }

        for (int i = 0; i < workload_size; i++) {
            cout << workload[i] << endl;
            cout << (totalTimeSpent[i] / nr_iters) << endl;
            config["workload"][i]["result"] = (totalTimeSpent[i] / nr_iters);
        }

        config["average_time"] =
            (accumulate(totalTimeSpent.begin(), totalTimeSpent.end(), 0.0) /
             nr_iters);
        cout << (accumulate(totalTimeSpent.begin(), totalTimeSpent.end(), 0.0) /
                 nr_iters)
             << endl;

        auto resultFile = ofstream(configJson + "_result.json");
        resultFile << config.dump(4) << endl;
        cout << config.dump(4) << endl;

        cout << busy_wait_a << endl;

        return 0;
}