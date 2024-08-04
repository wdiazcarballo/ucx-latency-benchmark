# UCX Ping-Pong Latency Benchmark

## Overview
This project contains the implementation of a UCX-based Ping-Pong Latency Benchmark. The goal of this benchmark is to measure the latency between two machines using UCX Remote Memory Access (RMA) semantics.

## Files
- `ucx_latency.c`: The source code for the UCX Ping-Pong Latency Benchmark.
- `ucx_latency_benchmark`: The compiled executable of the UCX Ping-Pong Latency Benchmark.
- `job_ucx_latency.sh`: A SLURM job script to run the latency benchmark on the `helios` partition.
- `ucx_latency_test_529581.log`: The output log file from the most recent benchmark test run.

## Compilation
To compile the UCX Ping-Pong Latency Benchmark, ensure that UCX is installed and properly configured on your system. Then, run the following command:

```sh
gcc -o ucx_latency_benchmark ucx_latency.c -lucs -lucp

# Running the Benchmark
## SLURM Job Script
The job_ucx_latency.sh script is used to run the latency benchmark on a multi-node cluster using SLURM. The script starts the server on one node and the client on another node.

job_ucx_latency.sh
```sh
#!/bin/bash
#SBATCH --job-name=ucx_latency_test
#SBATCH --partition=helios
#SBATCH --nodes=2
#SBATCH --ntasks-per-node=1
#SBATCH --time=01:00:00
#SBATCH --output=ucx_latency_test_%j.log
#SBATCH --chdir=/global/home/users/rdmaworkshop08/ucx-latency-benchmark

# Get the hostnames
NODELIST=($(scontrol show hostnames $SLURM_JOB_NODELIST))
SERVER_NODE=${NODELIST[0]}
CLIENT_NODE=${NODELIST[1]}

# Start the server on the first node
srun --nodes=1 --ntasks=1 --nodelist=$SERVER_NODE ./ucx_latency_benchmark &

# Give the server a moment to start
sleep 8

# Start the client on the second node
srun --nodes=1 --ntasks=1 --nodelist=$CLIENT_NODE ./ucx_latency_benchmark $SERVER_NODE

wait
```

# Submitting the Job
## To submit the job to SLURM, use the following command:

```sh
sbatch job_ucx_latency.sh
```

## Output
The output of the latency benchmark will be logged in a file named ucx_latency_test_<job_id>.log where <job_id> is the SLURM job ID.