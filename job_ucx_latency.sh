#!/bin/bash
#SBATCH --job-name=ucx_latency_test
#SBATCH --partition=helios
#SBATCH --nodes=2
#SBATCH --ntasks-per-node=1
#SBATCH --time=01:00:00
#SBATCH --output=ucx_latency_test_%j.log
#SBATCH --chdir=/global/home/users/rdmaworkshop08/wdc/ucx-latency-benchmark

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
