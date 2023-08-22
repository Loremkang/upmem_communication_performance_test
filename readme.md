# Readme

This program is to test the CPU-PIM communication performance (latency and bandwidth) under different workload setups (by both sync/async API).

## How to Run (Not tested on simulators)

1. choose a config file you want (or write your own config file)
2. make
3. ./host --config_file YOURFILE.json

## Config File

The config file defines the (communication related) workload you want.
See the existing configurations as examples.




# Analysis in function call latency

dpu_switch_mux_for_rank
	host_handle_access_for_rank = 18 * exec_cmd + LOOP100(4 * exec_cmd)
		ufi_select_all_even_disabled = 2 * exec_cmd
			UFI_exec_void_frames
		
		ufi_set_mram_mux = 10 * exec_cmd
            ufi_write_dma_ctrl_datas = 2 * exec_cmd
            ufi_write_dma_ctrl = 2 * exec_cmd
                UFI_exec_void_frame
            ufi_write_dma_ctrl_datas = 2 * exec_cmd
            ufi_write_dma_ctrl_datas = 2 * exec_cmd
            ufi_clear_dma_ctrl = 2 * exec_cmd
                UFI_exec_void_frame

		dpu_check_wavegen_mux_status_for_rank = 6 * exec_cmd + LOOP100(4 * exec_cmd)
            ufi_select_all_even_disabled = 2 * exec_cmd
            ufi_write_dma_ctrl = 2 * exec_cmd
            ufi_clear_dma_ctrl = 2 * exec_cmd
            LOOP 8(dpus per chip):
                LOOP 100:
                    ufi_select_dpu_even_disabled = 2 * exec_cmd
                        UFI_exec_void_frames
                    ufi_read_dma_ctrl = 2 * exec_cmd
                        UFI_exec_8bit_frame
                            UFI_exec_write_structure
                            ci_exec_8bit_cmd
                                exec_cmd
                

UFI_exec_void_frames = 2 * exec_cmd
    UFI_exec_write_structures
        exec_cmd
    exec_cmd

ufi_write_dma_ctrl_datas = 2 * exec_cmd
    UFI_exec_write_structures
        exec_cmd
    exec_cmd

exec_cmd
    ci_commit_commands
        write_to_cis
    LOOP timeout=100:
        ci_update_commands
            read_from_cis
        

ci_exec_void_cmd = exec_cmd
UFI_exec_write_structures = exec_cmd 
