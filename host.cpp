// #include <dpu.h>
#include <fcntl.h>
#include <immintrin.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <x86intrin.h>
#include "pim_interface_header.hpp"

#include <chrono>
#include <fstream>
#include <iostream>

#include "argparse/argparse.hpp"
#include "nlohmann/json.hpp"
// #include "exprtk.hpp"

using namespace std;
using namespace std::chrono;
using json = nlohmann::json;

extern "C" {
#include <dpu.h>
}

#define DPU_BINARY "dpu"

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

auto initBuffer(auto &workload, int nr_buffers) {
    int workload_size = workload.size();
    assert(workload_size > 0);
    // assert(workload[workload_size - 1]["type"] == "sync");

    uint8_t ***buffers = (uint8_t ***)calloc(workload_size, sizeof(uint8_t **));
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
            int bufferLength = totalLength / nr_buffers;
            bufferLength = ((bufferLength - 1) / 8 + 1) * 8;
            workload[i]["buffer_length"] = bufferLength;
            workload[i]["offset"] = heap_offset;
            heap_offset += bufferLength;
            buffers[i] = (uint8_t **)calloc(nr_buffers, sizeof(uint8_t *));
            for (int j = 0; j < nr_buffers; j++) {
                buffers[i][j] =
                    (uint8_t *)calloc(bufferLength, sizeof(uint8_t));
            }
        }
    }
    assert(heap_offset < (2 << 20));  // no more than 2 MB
    return buffers;
}

void clearBuffer(auto &workload, uint8_t*** buffers, int nr_buffers) {
    int workload_size = workload.size();
    for (int i = 0; i < workload_size; i++) {
        string type = workload[i]["type"];
        if (type == "send" || type == "receive") {
            int bufferLength = workload[i]["buffer_length"];
            for (int j = 0; j < nr_buffers; j++) {
                for (int k = 0; k < bufferLength; k++) {
                    buffers[i][j][k] = (uint8_t)rand();
                }
            }
        }
    }
}

auto runOneRound(auto &workload, uint8_t ***buffers) {
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
            // DPU_FOREACH(dpu_set, dpu, each_dpu) {
            //     DPU_ASSERT(dpu_prepare_xfer(dpu, buffers[i][each_dpu]));
            // }
            // auto sync_setup = (workload[i]["mode"] == "sync") ? DPU_XFER_DEFAULT
            //                                                   : DPU_XFER_ASYNC;
            // DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU,
            //                          DPU_MRAM_HEAP_POINTER_NAME, heapOffset,
            //                          bufferLength, sync_setup));
        } else if (type == "receive") {
            int heapOffset = workload[i]["offset"];
            int bufferLength = workload[i]["buffer_length"];
            // DPU_FOREACH(dpu_set, dpu, each_dpu) {
            //     DPU_ASSERT(dpu_prepare_xfer(dpu, buffers[i][each_dpu]));
            // }
            // auto sync_setup = (workload[i]["mode"] == "sync") ? DPU_XFER_DEFAULT
            //                                                   : DPU_XFER_ASYNC;
            // DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_FROM_DPU,
            //                          DPU_MRAM_HEAP_POINTER_NAME, heapOffset,
            //                          bufferLength, sync_setup));
        } else if (type == "sync") {
            // DPU_ASSERT(dpu_sync(dpu_set));
        } else if (type == "exec") {
            // DPU_ASSERT(dpu_launch(dpu_set, DPU_ASYNCHRONOUS));
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

DirectPIMInterface* pimInterface = nullptr;

int main(int argc, char *argv[]) {
    string configJson = GetConfigFilename(argc, argv);
    auto configFile = ifstream(configJson);
    json config = json::parse(configFile);

    int nr_iters = config["nr_iters"];
    int nr_ranks = config["nr_ranks"];
    int nr_dpus = nr_ranks * DPU_PER_RANK;

    pimInterface = new DirectPIMInterface();
    pimInterface->allocate(nr_ranks, DPU_BINARY);

    // DPU_ASSERT(dpu_alloc_ranks(nr_ranks, "nrJobsPerRank=256", &dpu_set));
    // DPU_ASSERT(dpu_alloc_ranks(nr_ranks, "nrThreadsPerRank=1", &dpu_set));
    // DPU_ASSERT(dpu_load(dpu_set, DPU_BINARY, NULL));

    {
        assert(pimInterface->nr_of_dpus == 256);
        uint8_t **ids = new uint8_t*[pimInterface->nr_of_dpus];
        for (uint32_t i = 0; i < pimInterface->nr_of_dpus; i++) {
            ids[i] = new uint8_t[8];
            uint64_t* addr = (uint64_t*)ids[i];
            *addr = i;
        }
        
        pimInterface->SendToPIMByUPMEM(ids, "DPU_ID", 0, sizeof(uint64_t), false);

        for (uint32_t i = 0; i < pimInterface->nr_of_dpus; i ++) {
            delete [] ids[i];
        }
        delete [] ids;
    }

    pimInterface->Launch(false);

    pimInterface->PrintLog();

    {   
        uint32_t COUNT = 8;
        uint32_t SIZE = COUNT * sizeof(uint64_t);
        // {
        //     uint8_t** buffers = new uint8_t*[pimInterface->nr_of_dpus];
        //     for (uint32_t i = 0; i < pimInterface->nr_of_dpus; i++) {
        //         buffers[i] = new uint8_t[SIZE];
        //         memset(buffers[i], 0, sizeof(buffers[i]));
        //     }
        //     pimInterface->ReceiveFromPIMByUPMEM(buffers, DPU_MRAM_HEAP_POINTER_NAME, 0, SIZE, false);
        //     for (uint32_t i = 0; i < 10; i++) {
        //         uint64_t* head = (uint64_t*) buffers[i];
        //         for (int j = 0; j < COUNT; j ++) {
        //             printf("buffers[%d][%d]=%16llx\n", i, j, head[j]);
        //         }
        //         printf("\n");
        //     }
        // }

        {
            uint8_t** buffers = new uint8_t*[pimInterface->nr_of_dpus];
            for (uint32_t i = 0; i < pimInterface->nr_of_dpus; i++) {
                buffers[i] = new uint8_t[SIZE];
                memset(buffers[i], 0, sizeof(buffers[i]));
            }
            pimInterface->ReceiveFromPIM(buffers, DPU_MRAM_HEAP_POINTER_NAME, 0, SIZE, false);
            // for (uint32_t i = 0; i < 10; i++) {
            //     uint64_t* head = (uint64_t*) buffers[i];
            //     for (int j = 0; j < COUNT; j ++) {
            //         printf("buffers[%d][%d]=%16llx\n", i, j, head[j]);
            //     }
            //     printf("\n");
            // }
        }
    }

    return 0;

    // dpu_mem_max_addr_t offset_address;
    // {
    //     uint32_t each_dpu;
    //     dpu_set_t dpu;
    //     dpu_program_t *program = nullptr;
    //     DPU_FOREACH(dpu_set, dpu, each_dpu) {
    //         assert(dpu.kind == DPU_SET_DPU);
    //         dpu_t *dpuptr = dpu.dpu;
    //         if (!dpu_is_enabled(dpuptr)) {
    //             continue;
    //         }
    //         auto this_program = dpu_get_program(dpuptr);
    //         if (program == nullptr) {
    //             program = this_program;
    //         }
    //         assert(program == nullptr || program == this_program);
    //     }

    //     dpu_symbol_t symbol;
    //     const char *symbol_name = DPU_MRAM_HEAP_POINTER_NAME;
    //     DPU_ASSERT(dpu_get_symbol(program, symbol_name, &symbol));
    //     offset_address = symbol.address;
    // }

    // printf("offset_address = %llx\n", offset_address);
    // offset_address ^= 0x8000000;
    // offset_address = 0x100000;

    // dpu_rank_t **ranks = dpu_set.list.ranks;
    // hw_dpu_rank_allocation_parameters_t params[4];
    // for (int T = 0; T < 4; T++) {
    //     params[T] =
    //         (hw_dpu_rank_allocation_parameters_t)(ranks[T]
    //                                                   ->description->_internals
    //                                                   .data);
    // }

    // for (int i = 1; i <= 7; i++) {
    //     if (i == 3) continue;
    //     uint8_t *memory = params[1]->ptr_region;
    //     printf("i=%d memory=%012llx\n", i, memory);
    //     PrintCachelines((uint64_t *)memory);
    // }

    // return 0;

    // auto &workload = config["workload"];
    // int workload_size = workload.size();
    // assert(workload.is_array());
    // uint8_t ***buffers = initBuffer(workload);

    // vector<double> totalTimeSpent(workload_size);
    // for (int T = 0; T < nr_iters; T++) {
    //     clearBuffer(workload, buffers);
    //     vector<double> timeSpent = runOneRound(workload, buffers);
    //     json timeInfo(timeSpent);
    //     cout << timeInfo << endl;
    //     for (int i = 0; i < workload_size; i++) {
    //         totalTimeSpent[i] += timeSpent[i];
    //     }
    // }

    // for (int i = 0; i < workload_size; i++) {
    //     cout << workload[i] << endl;
    //     cout << (totalTimeSpent[i] / nr_iters) << endl;
    //     config["workload"][i]["result"] = (totalTimeSpent[i] / nr_iters);
    // }

    // config["average_time"] =
    //     (accumulate(totalTimeSpent.begin(), totalTimeSpent.end(), 0.0) /
    //      nr_iters);
    // cout << (accumulate(totalTimeSpent.begin(), totalTimeSpent.end(), 0.0) /
    //          nr_iters)
    //      << endl;

    // auto resultFile = ofstream(configJson + "_result.json");
    // resultFile << config.dump(4) << endl;
    // cout << config.dump(4) << endl;

    // cout << busy_wait_a << endl;

    return 0;
}