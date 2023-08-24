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
// #include "exprtk.hpp"

using namespace std;
using namespace std::chrono;
using json = nlohmann::json;

extern "C" {
    #include <dpu.h>
    #include <dpu_management.h>
    #include <dpu_target_macros.h>
}

#define DPU_BINARY "dpu"

#define DPU_PER_RANK 64

int nr_iters, nr_ranks, nr_dpus;
dpu_set_t dpu_set;

auto GetConfigFilename(int argc, char* argv[]) {
    argparse::ArgumentParser program("parser");

    program.add_argument("--config_file")
        .help("configuration json file required.")
        .required();

    try {
        program.parse_args(argc, argv);  // Example: ./main --color orange
    } catch (const std::runtime_error& err) {
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
    for (; multiplications > 0; multiplications --) {
        busy_wait_a = busy_wait_a * busy_wait_b;
    }
}

void BusyLoopTest() {
    // test the skewness of BusyLoop
    // result : 4e-8 second
    auto start_time = high_resolution_clock::now();
    double total_time = 10.0;
    double each_time = 1e-7;
    for (int i = 0; i < total_time/each_time; i ++) {
        BusyLoopInSec(each_time);
    }
    auto current_time = high_resolution_clock::now();
    auto d = duration_cast<duration<double>>(current_time - start_time);
    cout<<d.count()<<endl;
}

auto initBuffer(auto& workload) {
    int workload_size = workload.size();
    assert(workload_size > 0);
    // assert(workload[workload_size - 1]["type"] == "sync");

    uint8_t*** buffers = (uint8_t***)calloc(workload_size, sizeof(uint8_t**));
    int heap_offset = 0;

    for (int i = 0; i < workload_size; i ++) {
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
            
            cout<<i<<" "<<type<<" "<<totalLength<<endl;
            int bufferLength = totalLength / nr_dpus;
            bufferLength = ((bufferLength - 1) / 8 + 1) * 8;
            workload[i]["buffer_length"] = bufferLength;
            workload[i]["offset"] = heap_offset;
            heap_offset += bufferLength;
            buffers[i] = (uint8_t**)calloc(nr_dpus, sizeof(uint8_t*));
            for (int j = 0; j < nr_dpus; j ++) {
                buffers[i][j] = (uint8_t*)calloc(bufferLength, sizeof(uint8_t));
            }
        }
    }
    assert(heap_offset < (2<<20)); // no more than 2 MB
    return buffers;
}

void clearBuffer(auto& workload, auto buffers) {
    int workload_size = workload.size();
    for (int i = 0; i < workload_size; i ++) {
        string type = workload[i]["type"];
        if (type == "send" || type == "receive") {
            int bufferLength = workload[i]["buffer_length"];
            for (int j = 0; j < nr_dpus; j ++) {
                for (int k = 0; k < bufferLength; k ++) {
                    buffers[i][j][k] = (uint8_t)rand();
                }
            }
        }
    }
}

auto runOneRound(auto& workload, auto& buffers) {
    int workload_size = workload.size();
    vector<double> timeSpent;
    uint32_t each_dpu;
    dpu_set_t dpu;

    for (int i = 0; i < workload_size; i ++) {
        string type = workload[i]["type"];

        auto start_time = high_resolution_clock::now();
        if (type == "send") {
            int heapOffset = workload[i]["offset"];
            int bufferLength = workload[i]["buffer_length"];
            DPU_FOREACH(dpu_set, dpu, each_dpu) {
                DPU_ASSERT(dpu_prepare_xfer(dpu, buffers[i][each_dpu]));
            }
            auto sync_setup = (workload[i]["mode"] == "sync") ? DPU_XFER_DEFAULT : DPU_XFER_ASYNC;
            DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU,
                                         DPU_MRAM_HEAP_POINTER_NAME,
                                         heapOffset, bufferLength,
                                         sync_setup));
        } else if (type == "receive") {
            int heapOffset = workload[i]["offset"];
            int bufferLength = workload[i]["buffer_length"];
            DPU_FOREACH(dpu_set, dpu, each_dpu) {
                DPU_ASSERT(dpu_prepare_xfer(dpu, buffers[i][each_dpu]));
            }
            auto sync_setup = (workload[i]["mode"] == "sync") ? DPU_XFER_DEFAULT : DPU_XFER_ASYNC;
            DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_FROM_DPU,
                                         DPU_MRAM_HEAP_POINTER_NAME,
                                         heapOffset, bufferLength,
                                         sync_setup));
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

int main(int argc, char* argv[]) {
    string configJson = GetConfigFilename(argc, argv);
    auto configFile = ifstream(configJson);
    json config = json::parse(configFile);

    nr_iters = config["nr_iters"];
    nr_ranks = config["nr_ranks"];
    nr_dpus = nr_ranks * DPU_PER_RANK;

    DPU_ASSERT(dpu_alloc_ranks(nr_ranks, "nrJobsPerRank=256", &dpu_set));
    // DPU_ASSERT(dpu_alloc_ranks(nr_ranks, "nrThreadsPerRank=1", &dpu_set));
    DPU_ASSERT(dpu_load(dpu_set, DPU_BINARY, NULL));

    {
        // uint32_t nr_of_dpus = 0, nr_of_ranks = 0;
        DPU_ASSERT(dpu_get_nr_dpus(dpu_set, (uint32_t*)&nr_dpus));
        DPU_ASSERT(dpu_get_nr_ranks(dpu_set, (uint32_t*)&nr_ranks));
        printf("Allocated %d DPU(s)\n", nr_dpus);
        printf("Allocated %d Ranks(s)\n", nr_ranks);
    }

    auto& workload = config["workload"];
    int workload_size = workload.size();
    assert(workload.is_array());
    uint8_t*** buffers = initBuffer(workload);

    vector<double> totalTimeSpent(workload_size);
    for (int T = 0; T < nr_iters; T ++) {
        clearBuffer(workload, buffers);
        vector<double> timeSpent = runOneRound(workload, buffers);
        json timeInfo(timeSpent);
        cout<<timeInfo<<endl;
        for (int i = 0; i < workload_size; i ++) {
            totalTimeSpent[i] += timeSpent[i];
        }
    }

    for (int i = 0; i < workload_size; i ++) {
        cout<<workload[i]<<endl;
        cout<<(totalTimeSpent[i] / nr_iters)<<endl;
        config["workload"][i]["result"] = (totalTimeSpent[i] / nr_iters);
    }
    
    config["average_time"] = (accumulate(totalTimeSpent.begin(), totalTimeSpent.end(), 0.0) / nr_iters);
    cout<<(accumulate(totalTimeSpent.begin(), totalTimeSpent.end(), 0.0) / nr_iters)<<endl;

    auto resultFile = ofstream(configJson + "_result.json");
    resultFile << config.dump(4) << endl;
    cout<<config.dump(4)<<endl;

    cout<<busy_wait_a<<endl;

    return 0;

    
}