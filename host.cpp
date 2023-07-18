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
            assert((bufferLength % 8) == 0);
            for (int j = 0; j < nr_buffers; j++) {
                uint64_t* arr = (uint64_t*)buffers[i][j];
                for (int k = 0; k < bufferLength / 8; k++) {
                    // buffers[i][j][k] = (uint8_t)rand();
                    arr[k] ++;
                    // buffers[i][j][k]++;
                }
            }
        }
    }
}

inline double get_timestamp(){
    timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

auto runOneRound(auto &workload, uint8_t ***buffers, PIMInterface* interface) {
    int workload_size = workload.size();
    vector<double> timeSpent;
    uint32_t each_dpu;
    dpu_set_t dpu;

    for (int i = 0; i < workload_size; i++) {
        string type = workload[i]["type"];

        // auto start_time = high_resolution_clock::now();
        auto start_time = get_timestamp();
        if (type == "send") {
            int heapOffset = workload[i]["offset"];
            int bufferLength = workload[i]["buffer_length"];
            bool async = (workload[i]["mode"] != "sync");
            // start_time = high_resolution_clock::now();
            start_time = get_timestamp();
            interface->SendToPIM(buffers[i], DPU_MRAM_HEAP_POINTER_NAME, heapOffset + (4 << 20), bufferLength, async);
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
            bool async = (workload[i]["mode"] != "sync");
            // start_time = high_resolution_clock::now();
            start_time = get_timestamp();
            interface->ReceiveFromPIM(buffers[i], DPU_MRAM_HEAP_POINTER_NAME, heapOffset + (4 << 20), bufferLength, async);
            // DPU_FOREACH(dpu_set, dpu, each_dpu) {
            //     DPU_ASSERT(dpu_prepare_xfer(dpu, buffers[i][each_dpu]));
            // }
            // auto sync_setup = (workload[i]["mode"] == "sync") ? DPU_XFER_DEFAULT
            //                                                   : DPU_XFER_ASYNC;
            // DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_FROM_DPU,
            //                          DPU_MRAM_HEAP_POINTER_NAME, heapOffset,
            //                          bufferLength, sync_setup));
        } else if (type == "sync") {
            interface->sync();
            // DPU_ASSERT(dpu_sync(dpu_set));
        } else if (type == "exec") {
            interface->Launch(false);
            // DPU_ASSERT(dpu_launch(dpu_set, DPU_ASYNCHRONOUS));
        } else if (type == "busywait") {
            int inst = workload[i]["inst"];
            BusyLoopInInst(inst);
        } else {
            assert(false);
        }
        // auto current_time = high_resolution_clock::now();
        // auto d = (duration_cast<duration<double>>(current_time - start_time)).count;
        auto d = get_timestamp() - start_time;
        timeSpent.push_back(d);
    }
    return timeSpent;
}


void experiments(PIMInterface* interface) {
    {
        assert(interface->nr_of_dpus == 256);
        uint8_t **ids = new uint8_t *[interface->nr_of_dpus];
        for (uint32_t i = 0; i < interface->nr_of_dpus; i++) {
            ids[i] = new uint8_t[8];
            uint64_t *addr = (uint64_t *)ids[i];
            *addr = i;
        }

        interface->SendToPIMByUPMEM(ids, "DPU_ID", 0, sizeof(uint64_t),
                                       false);
        // interface->ReceiveFromPIMByUPMEM(ids, "DPU_ID", 0,
        // sizeof(uint64_t), false);

        for (uint32_t i = 0; i < interface->nr_of_dpus; i++) {
            // uint64_t* addr = (uint64_t*)ids[i];
            // assert(*addr == i);
            delete[] ids[i];
        }
        delete[] ids;
    }

    {
        uint32_t COUNT = 8;
        uint32_t SIZE = COUNT * sizeof(uint64_t);

        interface->Launch(false);

        interface->PrintLog();

        {
            uint8_t **buffers = new uint8_t *[interface->nr_of_dpus];
            for (uint32_t i = 0; i < interface->nr_of_dpus; i++) {
                buffers[i] = new uint8_t[SIZE];
                memset(buffers[i], 0, sizeof(buffers[i]));
            }
            interface->ReceiveFromPIM(buffers, DPU_MRAM_HEAP_POINTER_NAME,
                                         10485760, SIZE, false);
            for (uint32_t i = 0; i < 10; i++) {
                uint64_t *head = (uint64_t *)buffers[i];
                for (uint32_t j = 0; j < COUNT; j++) {
                    printf("buffers[%d][%d]=%16llx\n", i, j, head[j]);
                }
                printf("\n");
            }
        }
    }
}

int main(int argc, char *argv[]) {
    string configJson = GetConfigFilename(argc, argv);
    auto configFile = ifstream(configJson);
    json config = json::parse(configFile);

    int nr_iters = config["nr_iters"];

    DirectPIMInterface *pimInterface = nullptr;
    if (config["interface_type"] == "direct") {
        pimInterface = new DirectPIMInterface();
    } else if (config["interface_type"] == "UPMEM") {
        // pimInterface = new UPMEMInterface();
        assert(false);
    }
    // printf("%016llx\n", pimInterface->get_correct_offset(0x12345678, 0));
    // exit(0);
    assert(pimInterface != nullptr);
    pimInterface->allocate(config["nr_ranks"], DPU_BINARY);

    // experiments(pimInterface);
    // exit(0);

    auto &workload = config["workload"];
    int workload_size = workload.size();
    assert(workload.is_array());
    uint8_t ***buffers = initBuffer(workload, pimInterface->nr_of_dpus);

    vector<double> totalTimeSpent(workload_size);
    for (int T = 0; T < nr_iters; T++) {
        clearBuffer(workload, buffers, pimInterface->nr_of_dpus);
        vector<double> timeSpent = runOneRound(workload, buffers, pimInterface);
        json timeInfo(timeSpent);
        cout << T << " " << timeInfo << endl;
        for (int i = 0; i < workload_size; i++) {
            totalTimeSpent[i] += timeSpent[i];
        }
    }

    for (int i = 0; i < workload_size; i++) {
        cout << workload[i] << endl;
    }
    for (int i = 0; i < workload_size; i++) {
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


    cout << "Total MUX: "; pimInterface->t.print();
    cout << "Total t1: "; t1.print();
    cout << "Total t2: "; t2.print();
    cout << "Total t3: "; t3.print();

    return 0;
}