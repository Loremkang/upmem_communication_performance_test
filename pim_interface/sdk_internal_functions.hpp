#include "ufi_ci_types.h"
#include "dpu_attributes.h"
#include "dpu_internals.h"
#include "dpu_error.h"
#include "timer.hpp"
#include <iostream>

#define LOG_RANK(lvl, rank, fmt, ...) {}

u32 ufi_select_all_even_disabled(struct dpu_rank_t *rank, u8 *ci_mask);
u32 ufi_set_mram_mux(struct dpu_rank_t *rank, u8 ci_mask, dpu_ci_bitfield_t ci_mux_pos);
u32 ufi_write_dma_ctrl(struct dpu_rank_t *rank, u8 ci_mask, u8 address, u8 data);
u32 ufi_clear_dma_ctrl(struct dpu_rank_t *rank, u8 ci_mask);
u32 ufi_select_dpu(struct dpu_rank_t *rank, u8 *ci_mask, u8 dpu);
u32 ufi_select_dpu_even_disabled(struct dpu_rank_t *rank, u8 *ci_mask, u8 dpu);
u32 ufi_read_dma_ctrl(struct dpu_rank_t *rank, u8 ci_mask, u8 *data);

u32 ufi_wram_write(struct dpu_rank_t *rank, u8 ci_mask, u32 **src, u16 offset,
		   u16 len);
u32 ufi_wram_read(struct dpu_rank_t *rank, u8 ci_mask, u32 **dst, u16 offset,
		  u16 len);

// dpu_error_t dpu_check_wavegen_mux_status_for_rank(struct dpu_rank_t *rank, uint8_t expected);

#define ALL_CIS ((1u << DPU_MAX_NR_CIS) - 1u)
#define CI_MASK_ONE(ci) (1u << (ci))

/* Bit set when the DPU has the control of the bank */
#define MUX_DPU_BANK_CTRL (1 << 0)

/* Bit set when the DPU can write to the bank */
#define MUX_DPU_WRITE_CTRL (1 << 1)

/* Bit set when the DPU owns refresh */
#define MUX_DPU_REFRESH_CTRL (1 << 2)

/* Bit set when the host or the DPU wrote to the bank without permission */
#define MUX_COLLISION_ERR (1 << 7)

#define WAVEGEN_MUX_HOST_EXPECTED 0x00
#define WAVEGEN_MUX_DPU_EXPECTED (MUX_DPU_BANK_CTRL | MUX_DPU_WRITE_CTRL)

#define TIMEOUT_MUX_STATUS 100
#define CMD_GET_MUX_CTRL 0x02

// internal_timer t_mux_read, t_mux_select;

static dpu_error_t
dpu_check_wavegen_mux_status_for_rank_expr(struct dpu_rank_t *rank, uint8_t expected)
{
	dpu_error_t status;
	uint8_t dpu_dma_ctrl;
	uint8_t result_array[DPU_MAX_NR_CIS];
	uint8_t ci_mask = ALL_CIS, mask;
	uint8_t nr_dpus =
		rank->description->hw.topology.nr_of_dpus_per_control_interface;
	uint8_t nr_cis =
		rank->description->hw.topology.nr_of_control_interfaces;
	bool should_retry;
	dpu_member_id_t each_dpu;

	// int total_loop = 0;

	LOG_RANK(VERBOSE, rank, "");

	/* ci_mask retains the real disabled CIs, whereas mask does not take
     * care of disabled dpus (and then CIs) since it should switch mux of
     * disabled dpus: but not in the case a CI is completely deactivated.
     */

	// Check Mux control through dma_rdat_ctrl of fetch1
	// 1 - Select WaveGen Read register @0xFF and set it @0x02  (mux and collision ctrl)
	// 2 - Flush readop2 (Pipeline to DMA cfg data path)
	// 3 - Read dpu_dma_ctrl
	ci_mask = ALL_CIS;
	FF(ufi_select_all_even_disabled(rank, &ci_mask));
	FF(ufi_write_dma_ctrl(rank, ci_mask, 0xFF, CMD_GET_MUX_CTRL));
	FF(ufi_clear_dma_ctrl(rank, ci_mask));

	for (each_dpu = 0; each_dpu < nr_dpus; ++each_dpu) {
		uint32_t timeout = TIMEOUT_MUX_STATUS;

		do {
			dpu_slice_id_t each_slice;
			should_retry = false;

			mask = ALL_CIS;
			// t_mux_select.start();
			FF(ufi_select_dpu_even_disabled(rank, &mask, each_dpu));
			// t_mux_select.end();
			// t_mux_read.start();
			FF(ufi_read_dma_ctrl(rank, mask, result_array));
			// t_mux_read.end();

			for (each_slice = 0; each_slice < nr_cis;
			     ++each_slice) {
				if (!CI_MASK_ON(ci_mask, each_slice))
					continue;

				dpu_dma_ctrl = result_array[each_slice];

				// Do not check MUX_DPU_REFRESH_CTRL bit
				if ((dpu_dma_ctrl & 0x7B) != expected) {
					LOG_RANK(VERBOSE, rank,
						 "DPU (%d, %d) failed",
						 each_slice, each_dpu);
					should_retry = true;
				}
			}

			timeout--;
		} while (timeout &&
			 should_retry); // Do not check Collision Error bit	

		// total_loop += (TIMEOUT_MUX_STATUS - timeout);

		if (!timeout) {
			LOG_RANK(WARNING, rank,
				 "Timeout waiting for result to be correct");
			return rank->description->configuration
					       .disable_api_safe_checks ?
					     DPU_OK :
					     DPU_ERR_TIMEOUT;
		}
	}

	// printf("nr_of_dpus = %d\n", (int)nr_dpus);
	// printf("average loop = %lf\n", (double)total_loop / nr_dpus);

end:
	// t_mux_select.end();
	// t_mux_read.end();
	return status;
}

internal_timer t1, t2, t3;
static dpu_error_t host_handle_access_for_rank_expr(struct dpu_rank_t *rank,
					       bool set_mux_for_host)
{
	dpu_error_t status;

	uint8_t mask = ALL_CIS;

	t1.start();
	FF(ufi_select_all_even_disabled(rank, &mask));
	t1.end();
	t2.start();
	FF(ufi_set_mram_mux(rank, mask, set_mux_for_host ? 0xFF : 0x0));
	t2.end();
	t3.start();
	FF(dpu_check_wavegen_mux_status_for_rank_expr(
		rank, set_mux_for_host ? WAVEGEN_MUX_HOST_EXPECTED :
					       WAVEGEN_MUX_DPU_EXPECTED));
	t3.end();

end:
	t1.end();
	t2.end();
	t3.end();
	return status;
}

__API_SYMBOL__ dpu_error_t dpu_switch_mux_for_rank_expr(struct dpu_rank_t *rank,
						   bool set_mux_for_host)
{
	dpu_error_t status = DPU_OK;

	dpu_description_t desc = rank->description;
	uint8_t nr_cis = desc->hw.topology.nr_of_control_interfaces;
	uint8_t nr_dpus_per_ci =
		desc->hw.topology.nr_of_dpus_per_control_interface;
	bool switch_mux = false;
	dpu_slice_id_t each_slice;

	LOG_RANK(VERBOSE, rank, "");

	dpu_lock_rank(rank);
	if (rank->runtime.run_context.nb_dpu_running > 0) {
		LOG_RANK(
			WARNING, rank,
			"Host can not get access to the MRAM because %u DPU%s running.",
			rank->runtime.run_context.nb_dpu_running,
			rank->runtime.run_context.nb_dpu_running > 1 ? "s are" :
									     " is");
		status = DPU_ERR_MRAM_BUSY;
		goto end;
	}

	for (each_slice = 0; each_slice < nr_cis; ++each_slice) {
		if ((set_mux_for_host &&
		     __builtin_popcount(rank->runtime.control_interface
						.slice_info[each_slice]
						.host_mux_mram_state) <
			     nr_dpus_per_ci) ||
		    (!set_mux_for_host &&
		     rank->runtime.control_interface.slice_info[each_slice]
			     .host_mux_mram_state)) {
			LOG_RANK(
				VERBOSE, rank,
				"At least CI %d has mux in the wrong direction (0x%x), must switch rank.",
				each_slice,
				rank->runtime.control_interface
					.slice_info[each_slice]
					.host_mux_mram_state);
			switch_mux = true;
			break;
		}
	}

	if (!switch_mux) {
		LOG_RANK(VERBOSE, rank,
			 "Mux is in the right direction, nothing to do.");
		goto end;
	}

	/* We record the state before actually placing the mux in this state. If we
     * did record the state after setting the mux, we could be interrupted between
     * the setting of the mux and the recording of the state, and then the debugger
     * would miss a mux state.
     */
	for (each_slice = 0; each_slice < nr_cis; ++each_slice)
		rank->runtime.control_interface.slice_info[each_slice]
			.host_mux_mram_state =
			set_mux_for_host ? (1 << nr_dpus_per_ci) - 1 : 0x0;

	if (!rank->description->configuration.api_must_switch_mram_mux &&
	    !rank->description->configuration.init_mram_mux) {
		status = DPU_OK;
		goto end;
	}

	FF(host_handle_access_for_rank_expr(rank, set_mux_for_host));

end:
	dpu_unlock_rank(rank);

	return status;
}
