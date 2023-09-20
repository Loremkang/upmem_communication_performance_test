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
#include "parlay/parallel.h"

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

    for (int i = 0; i < workload_size; i++) {
        string type = workload[i]["type"];
        string target;
        if (workload[i].contains("target")) {
            target = workload[i]["target"];
        }

        // auto start_time = high_resolution_clock::now();
        auto start_time = get_timestamp();
        if (type == "send") {
            int heapOffset = workload[i]["offset"];
            int bufferLength = workload[i]["buffer_length"];
            bool async = (workload[i]["mode"] != "sync");
            // start_time = high_resolution_clock::now();
            start_time = get_timestamp();
            if (target == "WRAM") {
                // unimplemented
                assert(false);
            } else if (target == "MRAM") {
                interface->SendToPIM(buffers[i], DPU_MRAM_HEAP_POINTER_NAME, heapOffset + (4 << 20), bufferLength, async);
            } else {
                assert(false);
            }
        } else if (type == "receive") {
            int heapOffset = workload[i]["offset"];
            int bufferLength = workload[i]["buffer_length"];
            bool async = (workload[i]["mode"] != "sync");
            start_time = get_timestamp();
            
            if (target == "WRAM") {
                assert(bufferLength < (10 << 10));
                interface->ReceiveFromPIM(buffers[i], "wram_buffer", 0, bufferLength, async);
            } else {
                interface->ReceiveFromPIM(buffers[i], DPU_MRAM_HEAP_POINTER_NAME, heapOffset + (4 << 20), bufferLength, async);
            }
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

void InitDPUID(PIMInterface *interface) {
    // assert(interface->nr_of_dpus == 256);
    uint8_t **ids = new uint8_t *[interface->nr_of_dpus];
    for (uint32_t i = 0; i < interface->nr_of_dpus; i++) {
        ids[i] = new uint8_t[8];
        uint64_t *addr = (uint64_t *)ids[i];
        *addr = i;
    }

    interface->SendToPIMByUPMEM(ids, "DPU_ID", 0, sizeof(uint64_t), false);
    
    for (uint32_t i = 0; i < interface->nr_of_dpus; i ++) {
        uint64_t *addr = (uint64_t *)ids[i];
        *addr = 0;
    }

    interface->ReceiveFromPIMByUPMEM(ids, "DPU_ID", 0, sizeof(uint64_t),
                                        false);

    for (uint32_t i = 0; i < interface->nr_of_dpus; i++) {
        uint64_t *addr = (uint64_t *)ids[i];
        assert(*addr == i);
    }

    for (uint32_t i = 0; i < interface->nr_of_dpus; i++) {
        uint64_t *addr = (uint64_t *)ids[i];
        *addr = 0;
    }

    interface->SendToPIMByUPMEM(ids, "MRAM_TEST", 0, sizeof(uint64_t), false);
    interface->SendToPIMByUPMEM(ids, "WRAM_TEST", 0, sizeof(uint64_t), false);

    for (uint32_t i = 0; i < interface->nr_of_dpus; i ++) {
        delete[] ids[i];
    }
    delete[] ids;
}

void MRAMReceiveValidation(PIMInterface *interface) {

    {
        uint8_t **ids = new uint8_t *[interface->nr_of_dpus];
        for (uint32_t i = 0; i < interface->nr_of_dpus; i++) {
            ids[i] = new uint8_t[8];
            uint64_t *addr = (uint64_t *)ids[i];
            *addr = 1;
        }
        interface->SendToPIMByUPMEM(ids, "MRAM_TEST", 0, sizeof(uint64_t), false);
        for (uint32_t i = 0; i < interface->nr_of_dpus; i ++) {
            delete[] ids[i];
        }
        delete[] ids;
    }

    uint32_t COUNT = 1024 * 1024;               // 1M
    uint32_t SIZE = COUNT * sizeof(uint64_t);   // 1MB

    {
        // assert(interface->nr_of_dpus == 256);
        uint8_t **ids = new uint8_t *[interface->nr_of_dpus];
        parlay::parallel_for(0, interface->nr_of_dpus, [&](size_t i) {
            ids[i] = new uint8_t[SIZE];
            uint64_t *addr = (uint64_t *)ids[i];
            for (uint32_t k = 0; k < COUNT; k++) {
                addr[k] = parlay::hash64(k) * i;
            }
        });

        interface->SendToPIMByUPMEM(ids, DPU_MRAM_HEAP_POINTER_NAME, 10485760,
                                    SIZE, false);
        for (uint32_t i = 0; i < interface->nr_of_dpus; i++) {
            delete[] ids[i];
        }
        delete[] ids;
    }

    interface->Launch(false);
    interface->PrintLog();

    {
        uint8_t **buffers = new uint8_t *[interface->nr_of_dpus];
        parlay::parallel_for(0, interface->nr_of_dpus, [&](size_t i) {
            buffers[i] = new uint8_t[SIZE];
        });
        interface->ReceiveFromPIM(buffers, DPU_MRAM_HEAP_POINTER_NAME,
                                    10485760, SIZE, false);
        parlay::parallel_for(0, interface->nr_of_dpus, [&](size_t i) {
            uint64_t *addr = (uint64_t *)buffers[i];
            for (uint32_t k = 0; k < COUNT; k++) {
                uint64_t val = ((uint64_t)i << 48) + 11ll * 1024 * 1024 +
                                k * 8 + parlay::hash64(k) * i;
                assert(addr[k] == val);
                if (k < 2) {
                    printf("buffers[%d][%d]=%16llx\n", i, k, addr[k]);
                }
            }
        });
        for (uint32_t i = 0; i < interface->nr_of_dpus; i++) {
            delete[] buffers[i];
        }
        delete[] buffers;
    }
}

void WRAMReceiveValidation(PIMInterface *interface) {
    uint8_t **ids = new uint8_t *[interface->nr_of_dpus];
    for (uint32_t i = 0; i < interface->nr_of_dpus; i++) {
        ids[i] = new uint8_t[8];
        uint64_t *addr = (uint64_t *)ids[i];
        *addr = 1;
    }
    interface->SendToPIMByUPMEM(ids, "WRAM_TEST", 0, sizeof(uint64_t), false);
    for (uint32_t i = 0; i < interface->nr_of_dpus; i ++) {
        delete[] ids[i];
    }
    delete[] ids;

    uint32_t SIZE = 10 << 10;                   // 10 KB
    uint32_t COUNT = SIZE / sizeof(uint64_t);   // 1.25 K

    {
        // assert(interface->nr_of_dpus == 256);
        uint8_t **ids = new uint8_t *[interface->nr_of_dpus];
        parlay::parallel_for(0, interface->nr_of_dpus, [&](size_t i) {
            ids[i] = new uint8_t[SIZE];
            uint64_t *addr = (uint64_t *)ids[i];
            for (uint32_t k = 0; k < COUNT; k++) {
                addr[k] = i;
            }
        });

        interface->SendToPIMByUPMEM(ids, "wram_buffer", 0,
                                    SIZE, false);
                                    
        for (uint32_t i = 0; i < interface->nr_of_dpus; i++) {
            delete[] ids[i];
        }
        delete[] ids;
    }

    interface->Launch(false);
    interface->PrintLog();

    {
        uint8_t **buffers = new uint8_t *[interface->nr_of_dpus];
        parlay::parallel_for(0, interface->nr_of_dpus, [&](size_t i) {
            buffers[i] = new uint8_t[SIZE];
        });
        DirectPIMInterface* di = (DirectPIMInterface*)interface;
        di->ReceiveFromPIM(buffers, "wram_buffer",
                                    0, SIZE, false);
        auto f = [&](size_t i) {
            uint64_t *addr = (uint64_t *)buffers[i];
            for (uint32_t k = 0; k < COUNT; k++) {
                uint64_t val = ((uint64_t)i << 48) + 0x220 +
                                k * 8 + i;
                if(!(addr[k] == val)) {
                    printf("buffers[%d][%d]=%16llx, val=%16llx\n", i, k, addr[k], val);
                    fflush(stdout);
                    assert(false);
                }
                if (k < 2) {
                    printf("buffers[%d][%d]=%16llx\n", i, k, addr[k]);
                }
            }
        };
        for (int i = 0; i < interface->nr_of_dpus; i ++) {
            f(i);
        }
        // parlay::parallel_for(0, interface->nr_of_dpus, f);
        for (uint32_t i = 0; i < interface->nr_of_dpus; i++) {
            delete[] buffers[i];
        }
        delete[] buffers;
    }
}

void experiments(PIMInterface *interface) {
    InitDPUID(interface);
    // MRAMReceiveValidation(interface);
    WRAMReceiveValidation(interface);
}

int main(int argc, char *argv[]) {
    string configJson = GetConfigFilename(argc, argv);
    auto configFile = ifstream(configJson);
    json config = json::parse(configFile);

    int nr_iters = config["nr_iters"];

    PIMInterface *pimInterface = nullptr;
    if (config["interface_type"] == "direct") {
        pimInterface = new DirectPIMInterface();
    } else if (config["interface_type"] == "UPMEM") {
        pimInterface = new UPMEMInterface();
        // assert(false);
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
        config["workload"][i]["latency"] = (totalTimeSpent[i] / nr_iters);
        uint64_t buf_len = config["workload"][i]["buffer_length"];
        uint64_t communication =
            buf_len * pimInterface->nr_of_ranks * MAX_NR_DPUS_PER_RANK;
        config["workload"][i]["bandwidth"] =
            communication / (totalTimeSpent[i] / nr_iters) / 1e9;
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

    // cout << "Total MUX: ";
    // pimInterface->t.print();
    // cout << "t_mux_select: "; t_mux_select.print();
    // cout << "t_mux_read: "; t_mux_read.print();
    // cout << "Total t1: ";
    // t1.print();
    // cout << "Total t2: ";
    // t2.print();
    // cout << "Total t3: ";
    // t3.print();

    return 0;
}