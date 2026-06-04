# Robbe Titeca 2026
# Tests multiple configurations of the randomized-proteus-core

import os, subprocess
from dataclasses import dataclass
from enum import Enum

### DATA STRUCTURES ###
class EvictionPolicy(Enum):
    """ Eviction Policy """
    GRAN = "GRAN"
    GLRU = "GLRU"
    LRAN = "LRAN"
    LLRU = "LLRU"

class InsertionPolicy(Enum):
    """ Replacement Policy """
    LRU = "LRU" # LRU
    RAN = "RAN" # Randomized

class SkewApproach(Enum):
    """ Skew Approach """
    RS = "RS" # Random Selection
    LA = "LA" # Load Aware

@dataclass
class Configuration:
    """ Represents a cache configuration """
    sets: int
    ways: int
    skews: int
    eviction_policy: EvictionPolicy
    insertion_policy: InsertionPolicy
    skew_approach: SkewApproach
    invalid_tags: int

### CONFIGURATIONS ###
configurations = [
]

### LOGIC ###
# Cache Configuration Backup
print("Making a backup of Core.scala...")
subprocess.run(["cp", "/ecosystem/core/src/main/scala/riscv/Core.scala", "/ecosystem/scripts/Core.bak.scala"], check=True)

simulation_error = 0
cost_error = 0
bench_error = 0

try:
    # Make Directories
    print("Inializing directories...")
    os.makedirs('/ecosystem/scripts/output/bench', exist_ok=True)
    os.makedirs('/ecosystem/scripts/output/cost', exist_ok=True)

    # Make Benchmarks
    print("Making benchmarks...")
    os.chdir("/ecosystem/benchmarks/embench")
    subprocess.run(['make'], check=True, stdout=subprocess.DEVNULL)

    for j in range(0, 3):
        os.makedirs(f'/ecosystem/scripts/output/bench/{j}', exist_ok=True)
        for idx, i in enumerate(configurations):
            # Modify Configuration
            print("Modifying Core.scala")
            os.chdir ("/ecosystem/core/src/main/scala/riscv/")

            with open("Core.scala", 'r', encoding='UTF-8') as file:
                lines = file.readlines()

            lines[69] = f"sets = {i.sets},\n"
            lines[70] = f"ways = {i.ways},\n"
            lines[71] = f"skews = {i.skews},\n"

            lines[73] = f"evictionPolicy = EvictionPolicy.{i.eviction_policy.name},\n"
            lines[74] = f"insertionPolicy = InsertionPolicy.{i.insertion_policy.name},\n"
            lines[75] = f"skewApproach = SkewApproach.{i.skew_approach.name},\n"
            lines[76] = f"invalidTags = {i.invalid_tags},\n"

            with open("Core.scala", 'w', encoding='UTF-8') as file:
                file.writelines(lines)

            print(f"Building simulation for configuration {idx}...")
            os.chdir("/ecosystem/")
            try:
                subprocess.run(["make", "-C", "simulation"], check=True)
            except Exception as e:
                print(f"Simulation error: {e}\n Continuing...\n")
                simulation_error += 1
                continue
             
            print(f"Starting cost analysis {idx}")
            os.chdir("/ecosystem")
            if j == 0:
                try:
                    with open(f"/ecosystem/scripts/output/cost/cost_{i.sets}_{i.ways}_{i.skews}_{i.eviction_policy}_{i.insertion_policy}_{i.skew_approach}_{i.invalid_tags}.txt", 'w', encoding="UTF-8") as file:
                        subprocess.run(['/ecosystem/.venv/bin/python3', '/ecosystem/eval-hd/eval-hd.py', '--cell-library', './eval-hd/freepdk-45nm/stdcells.lib', '/ecosystem/core/Core.v'], check=True, stdout=file)

                except Exception as e:
                    print(f"Cost analysis error: {e}\n")
                    cost_error += 1
            print(f"Starting benchmark {idx} iteration {j}")
            os.chdir("/ecosystem/benchmarks/embench")
            try:
                with open(f"/ecosystem/scripts/output/bench/{j}/bench_{i.sets}_{i.ways}_{i.skews}_{i.eviction_policy}_{i.insertion_policy}_{i.skew_approach}_{i.invalid_tags}.txt", 'w', encoding="UTF-8") as file:
                    subprocess.run(['python3',
                                    'benchmark_speed.py',
                                    '--target-module=run_proteus', 
                                    '--timeout=5400', 
                                    '--absolute'], check=True, stdout=file)
            except Exception as e:
                print(f"Benchmark error: {e}\n")
                bench_error += 1
            
except Exception as e:
    print(f"Fatal error: {e}\nStopping Execution...\n")
finally:
    os.chdir ("/ecosystem/core/src/main/scala/riscv/")
    os.remove("./Core.scala")
    subprocess.run(["mv", "/ecosystem/scripts/Core.bak.scala", "/ecosystem/core/src/main/scala/riscv/Core.scala"], check=True)
    print(f"Evaluation finished!\nSimulation errors: {simulation_error}\nCost errors: {cost_error}\nBench errors: {bench_error}\n")