# Readme

This program is to test the CPU-PIM communication performance (latency and bandwidth) under different workload setups (by both sync/async API).

## How to Run (Not tested on simulators)

1. choose a config file you want (or write your own config file)
2. make
3. ./host --config_file YOURFILE.json

## Config File

The config file defines the (communication related) workload you want.
See the existing configurations as examples.