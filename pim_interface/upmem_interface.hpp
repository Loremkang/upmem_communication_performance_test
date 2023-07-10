#pragma once

#include "pim_interface.hpp"

class UPMEMInterface : public PIMInterface {
    public:
    void SendToPIM(uint8_t** buffers, std::string symbol_name, uint32_t symbol_offset, uint32_t length, bool async_transfer) {
        // Please make sure buffers don't overflow
        DPU_FOREACH(dpu_set, dpu, each_dpu) {
            DPU_ASSERT(dpu_prepare_xfer(dpu, buffers[each_dpu]));
        }
        auto sync_setup = async_transfer ? DPU_XFER_ASYNC : DPU_XFER_DEFAULT;
        DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU,
                                 symbol_name.c_str(), symbol_offset,
                                 length, sync_setup));
    }

    void ReceiveFromPIM(uint8_t** buffers, std::string symbol_name, uint32_t symbol_offset, uint32_t length, bool async_transfer) {
        // Please make sure buffers don't overflow
        DPU_FOREACH(dpu_set, dpu, each_dpu) {
            DPU_ASSERT(dpu_prepare_xfer(dpu, buffers[each_dpu]));
        }
        auto sync_setup = async_transfer ? DPU_XFER_ASYNC : DPU_XFER_DEFAULT;
        DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_FROM_DPU,
                                 symbol_name.c_str(), symbol_offset,
                                 length, sync_setup));
    }
};