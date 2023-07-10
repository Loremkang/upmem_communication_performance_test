#include "pim_interface.hpp"

extern "C" {
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

class DirectPIMInterface {

    void SendToPIM(uint8_t** buffer, uint64_t pim_offset, uint8_t length) {
        assert(false);
    }

    void ReceiveFromPIM(uint8_t** buffer, uint64_t pim_offset, uint8_t length) {

    }
}