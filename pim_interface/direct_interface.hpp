#pragma once

#include "pim_interface.hpp"
#include <iostream>

// #define NOINLINE __attribute__((noinline))
#define NOINLINE

extern "C" {
#include <dpu_management.h>
#include <dpu_program.h>
#include <dpu_rank.h>
#include <dpu_target_macros.h>
#include <dpu_types.h>

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
} *hw_dpu_rank_allocation_parameters_t;

#define DPU_REGION_MODE_UNDEFINED 0
#define DPU_REGION_MODE_PERF 1
#define DPU_REGION_MODE_SAFE 2
#define DPU_REGION_MODE_HYBRID 3

dpu_error_t dpu_switch_mux_for_rank(struct dpu_rank_t *rank,
				    bool set_mux_for_host);

}

class DirectPIMInterface : public PIMInterface {
   public:
    DirectPIMInterface() : PIMInterface() {
        ranks = nullptr;
        base_addrs = nullptr;
    }

    void SendToPIM(uint8_t **buffers, std::string symbol_name,
                   uint32_t symbol_offset, uint32_t length,
                   bool async_transfer) {
        // Please make sure buffers don't overflow
        (void)buffers;
        (void)symbol_name;
        (void)symbol_offset;
        (void)length;
        (void)async_transfer;
        assert(false);
    }

    inline bool aligned(uint64_t offset, uint64_t factor) {
        return (offset % factor == 0);
    }

    static uint32_t apply_address_translation_on_mram_offset(uint32_t byte_offset)
    {
        /* We have observed that, within the 26 address bits of the MRAM address, we need to apply an address translation:
        *
        * virtual[13: 0] = physical[13: 0]
        * virtual[20:14] = physical[21:15]
        * virtual[   21] = physical[   14]
        * virtual[25:22] = physical[25:22]
        *
        * This function computes the "virtual" mram address based on the given "physical" mram address.
        */

        uint32_t mask_21_to_15 = ((1 << (21 - 15 + 1)) - 1) << 15;
        uint32_t mask_21_to_14 = ((1 << (21 - 14 + 1)) - 1) << 14;
        uint32_t bits_21_to_15 = (byte_offset & mask_21_to_15) >> 15;
        uint32_t bit_14 = (byte_offset >> 14) & 1;
        uint32_t unchanged_bits = byte_offset & ~mask_21_to_14;

        return unchanged_bits | (bits_21_to_15 << 14) | (bit_14 << 21);
    }

    inline uint64_t get_correct_offset_fast(uint64_t address_offset, uint32_t dpu_id) {
        // uint64_t fastoffset = get_correct_offset_fast(address_offset, dpu_id);

        uint64_t mask_move_7 = (~((1 << 22) - 1)) + (1 << 13); // 31..22, 13
        uint64_t mask_move_6 = ((1 << 22) - (1 << 15)); // 21..15
        uint64_t mask_move_14 = (1 << 14); // 14
        uint64_t mask_move_4 = (1 << 13) - 1; // 12 .. 0

        return ((address_offset & mask_move_7) << 7) | ((address_offset & mask_move_6) << 6) | ((address_offset & mask_move_14) << 14) | ((address_offset & mask_move_4) << 4) | (dpu_id << 18);
    }

    NOINLINE inline uint64_t get_correct_offset_golden(uint64_t address_offset, uint32_t dpu_id) {
        // uint64_t fastoffset = get_correct_offset_fast(address_offset, dpu_id);
        uint64_t offset = 0;

        // 1 : address_offset < 64MB
        offset += (512ll << 20) * (address_offset >> 22);
        address_offset &= (1ll << 22) - 1;

        // 2 : address_offset < 4MB
        if (address_offset & (16 << 10)) {
            offset += (256ll << 20);
        }
        offset += (2ll << 20) * (address_offset / (32 << 10));
        address_offset %= (16 << 10);

        // 3 : address_offset < 16K
        if (address_offset & (8 << 10)) {
            offset += (1ll << 20);
        }
        address_offset %= (8 << 10);
        
        offset += address_offset * 16;

        // 4 : address_offset < 8K
        offset += (dpu_id & 3) * (256 << 10);

        // 5
        if (dpu_id >= 4) {
            offset += 64;
        }

        return offset;
    }

    NOINLINE inline uint64_t get_correct_offset(uint64_t address_offset, uint32_t dpu_id) {
        // uint64_t v1 = get_correct_offset_golden(address_offset, dpu_id);
        // uint64_t v2 = get_correct_offset_fast(address_offset, dpu_id);
        // assert(v1 == v2);
        // return v2;
        return get_correct_offset_fast(address_offset, dpu_id);
    }

    NOINLINE void LoadData(uint64_t* cache_line, uint8_t* ptr_dest) {
        // printf("%016llx - %016llx\n", ptr_dest + offset, ptr_dest + offset + 0x40);
        // memcpy(cache_line, ptr_dest, sizeof(uint64_t) * 8);
        cache_line[0] =
            *((volatile uint64_t *)((uint8_t *)ptr_dest +
                                    0 * sizeof(uint64_t)));
        cache_line[1] =
            *((volatile uint64_t *)((uint8_t *)ptr_dest +
                                    1 * sizeof(uint64_t)));
        cache_line[2] =
            *((volatile uint64_t *)((uint8_t *)ptr_dest +
                                    2 * sizeof(uint64_t)));
        cache_line[3] =
            *((volatile uint64_t *)((uint8_t *)ptr_dest +
                                    3 * sizeof(uint64_t)));
        cache_line[4] =
            *((volatile uint64_t *)((uint8_t *)ptr_dest +
                                    4 * sizeof(uint64_t)));
        cache_line[5] =
            *((volatile uint64_t *)((uint8_t *)ptr_dest +
                                    5 * sizeof(uint64_t)));
        cache_line[6] =
            *((volatile uint64_t *)((uint8_t *)ptr_dest +
                                    6 * sizeof(uint64_t)));
        cache_line[7] =
            *((volatile uint64_t *)((uint8_t *)ptr_dest +
                                    7 * sizeof(uint64_t)));
    }

    NOINLINE void ReceiveFromRank(uint8_t **buffers, uint32_t symbol_offset,
                         uint8_t *ptr_dest, uint32_t length) {
        assert(aligned(symbol_offset, sizeof(uint64_t)));
        assert(aligned(length, sizeof(uint64_t)));
        assert((uint64_t)symbol_offset + length <= MRAM_SIZE);

        uint64_t cache_line[8], cache_line_interleave[8];

        for (uint32_t dpu_id = 0; dpu_id < 4; ++dpu_id) {
            for (uint32_t i = 0; i < length / sizeof(uint64_t); ++i) {
                // 8 shards of DPUs
                uint64_t offset = get_correct_offset(symbol_offset + (i * 8), dpu_id);
                __builtin_ia32_clflushopt((void *)(ptr_dest + offset));
                offset += 0x40;
                __builtin_ia32_clflushopt((void *)(ptr_dest + offset));
            }
        }
        __builtin_ia32_mfence();

        for (uint32_t dpu_id = 0; dpu_id < 4; ++dpu_id) {
            for (uint32_t i = 0; i < length / sizeof(uint64_t); ++i) {
                if ((i % 8 == 0) && (i + 8 < length / sizeof(uint64_t))) {
                    for (int j = 0; j < 16; j ++) {
                        __builtin_prefetch(((uint64_t *)buffers[j * 4 + dpu_id]) + i + 8);
                    }
                }
                uint64_t offset = get_correct_offset(symbol_offset + (i * 8), dpu_id);
                __builtin_prefetch(ptr_dest + offset + 0x40 * 6);
                __builtin_prefetch(ptr_dest + offset + 0x40 * 7);

                LoadData(cache_line, ptr_dest + offset);
                byte_interleave_avx2(cache_line, cache_line_interleave);
                for (int j = 0; j < 8; j++) {
                    *(((uint64_t *)buffers[j * 8 + dpu_id]) + i) = cache_line_interleave[j];
                }

                offset += 0x40;
                LoadData(cache_line, ptr_dest + offset);
                byte_interleave_avx2(cache_line, cache_line_interleave);
                for (int j = 0; j < 8; j++) {
                    *(((uint64_t *)buffers[j * 8 + dpu_id + 4]) + i) = cache_line_interleave[j];
                }
            }
        }
        // exit(0);
    }


    void ReceiveFromPIM(uint8_t **buffers, std::string symbol_name,
                        uint32_t symbol_offset, uint32_t length,
                        bool async_transfer) {
        // Please make sure buffers don't overflow

        // Only support heap pointer at present
        assert(symbol_name == DPU_MRAM_HEAP_POINTER_NAME);

        // Only suport synchronous transfer
        assert(!async_transfer);

        for (int i = 0; i < nr_of_ranks; i ++) {
            assert(params[i]->mode == DPU_REGION_MODE_PERF);
        }


        // if (offset_list.find())

        // Find heap pointer address offset
        dpu_symbol_t symbol;
        DPU_ASSERT(
            dpu_get_symbol(program, DPU_MRAM_HEAP_POINTER_NAME, &symbol));
        assert(symbol.address &
               MRAM_ADDRESS_SPACE);  // should be a MRAM address
        symbol_offset += symbol.address ^ MRAM_ADDRESS_SPACE;

        // printf("Symbol Offset = %llx\n", symbol_offset);

        // Find physical address for each rank
        for (uint32_t i = 0; i < nr_of_ranks; i++) {
            uint8_t **rank_buffers = &buffers[i * DPU_PER_RANK];
            // dpu_lock_rank(ranks[i]);
            DPU_ASSERT(dpu_switch_mux_for_rank(ranks[i], true)); // 2us
            ReceiveFromRank(rank_buffers, symbol_offset, base_addrs[i], length);
            // dpu_unlock_rank(ranks[i]);
        }
    }

    void allocate(uint32_t nr_of_ranks, std::string binary) {
        PIMInterface::allocate(nr_of_ranks, binary);
        assert(this->nr_of_ranks == nr_of_ranks);

        ranks = new dpu_rank_t *[nr_of_ranks];
        params = new hw_dpu_rank_allocation_parameters_t [nr_of_ranks];
        base_addrs = new uint8_t *[nr_of_ranks];
        for (uint32_t i = 0; i < nr_of_ranks; i++) {
            ranks[i] = dpu_set.list.ranks[i];
            params[i] = 
                ((hw_dpu_rank_allocation_parameters_t)(ranks[i]->description->_internals.data));
            base_addrs[i] = params[i]->ptr_region;
        }

        // find program pointer
        DPU_FOREACH(dpu_set, dpu, each_dpu) {
            assert(dpu.kind == DPU_SET_DPU);
            dpu_t *dpuptr = dpu.dpu;
            if (!dpu_is_enabled(dpuptr)) {
                continue;
            }
            dpu_program_t *this_program = dpu_get_program(dpuptr);
            if (program == nullptr) {
                program = this_program;
            }
            assert(program == nullptr || program == this_program);
        }
    }

    void deallocate() {
        if (ranks != nullptr) {
            delete[] ranks;
        }
        if (base_addrs != nullptr) {
            delete[] base_addrs;
        }
    }

    ~DirectPIMInterface() { deallocate(); }

   private:
    NOINLINE void byte_interleave_avx2(uint64_t *input, uint64_t *output) {
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

    const int MRAM_ADDRESS_SPACE = 0x8000000;
    dpu_rank_t **ranks;
    hw_dpu_rank_allocation_parameters_t *params;
    uint8_t **base_addrs;
    dpu_program_t *program;
    // map<std::string, uint32_t> offset_list;
};