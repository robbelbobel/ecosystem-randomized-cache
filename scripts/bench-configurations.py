# Robbe Titeca 2026
# Tests multiple configurations of the randomized-proteus-core

import os, subprocess
from dataclasses import dataclass
from enum import Enum

### DATA STRUCTURES ###
class ReplacementPolicy(Enum):
    """ Replacement Policy """
    PLRU = "PLRU" # Pseudo-LRU
    RAN = "RAN" # Randomized

class SkewApproach(Enum):
    """ Skew Approach """
    RS = "RS" # Random Selection
    LA = "LA" # Load Aware

class EvictionPolicy(Enum):
    """ Eviction Policy """
    LE = "LE" # Local Eviction
    GE = "GE" # Global Eviction

@dataclass
class Configuration:
    """ Represents a cache configuration """
    sets: int
    ways: int
    skews: int
    replacement_policy: ReplacementPolicy
    skew_approach: SkewApproach
    invalid_tags: int
    eviction_policy: EvictionPolicy

### CONFIGURATIONS ###
configurations: list[Configuration] = [
    # Base
    Configuration(16, 2, 1, ReplacementPolicy.PLRU, SkewApproach.RS, 0, EvictionPolicy.LE),
    Configuration(16, 2, 1, ReplacementPolicy.RAN,  SkewApproach.RS, 0, EvictionPolicy.LE),

    # Ways Sweep (baseline)
    Configuration(16, 1,  1, ReplacementPolicy.PLRU, SkewApproach.RS, 0, EvictionPolicy.LE),
    Configuration(16, 4,  1, ReplacementPolicy.PLRU, SkewApproach.RS, 0, EvictionPolicy.LE),
    Configuration(16, 8,  1, ReplacementPolicy.PLRU, SkewApproach.RS, 0, EvictionPolicy.LE),
    Configuration(16, 16, 1, ReplacementPolicy.PLRU, SkewApproach.RS, 0, EvictionPolicy.LE),
    Configuration(16, 32, 1, ReplacementPolicy.PLRU, SkewApproach.RS, 0, EvictionPolicy.LE),

    # Sets Sweep (baseline)
    Configuration(4,   2, 1, ReplacementPolicy.PLRU, SkewApproach.RS, 0, EvictionPolicy.LE),
    Configuration(64,  2, 1, ReplacementPolicy.PLRU, SkewApproach.RS, 0, EvictionPolicy.LE),
    Configuration(256, 2, 1, ReplacementPolicy.PLRU, SkewApproach.RS, 0, EvictionPolicy.LE),

    # Skew Count (RS vs LA)
    Configuration(16, 2, 2,  ReplacementPolicy.PLRU, SkewApproach.RS, 0, EvictionPolicy.LE),
    Configuration(16, 2, 4,  ReplacementPolicy.PLRU, SkewApproach.RS, 0, EvictionPolicy.LE),
    Configuration(16, 2, 8,  ReplacementPolicy.PLRU, SkewApproach.RS, 0, EvictionPolicy.LE),
    Configuration(16, 2, 16, ReplacementPolicy.PLRU, SkewApproach.RS, 0, EvictionPolicy.LE),

    Configuration(16, 2, 2,  ReplacementPolicy.PLRU, SkewApproach.LA, 0, EvictionPolicy.LE),
    Configuration(16, 2, 4,  ReplacementPolicy.PLRU, SkewApproach.LA, 0, EvictionPolicy.LE),
    Configuration(16, 2, 8,  ReplacementPolicy.PLRU, SkewApproach.LA, 0, EvictionPolicy.LE),
    Configuration(16, 2, 16, ReplacementPolicy.PLRU, SkewApproach.LA, 0, EvictionPolicy.LE),

    # Invalid Tags (RS)
    Configuration(16, 2, 2, ReplacementPolicy.PLRU, SkewApproach.RS, 2,  EvictionPolicy.LE),
    Configuration(16, 2, 2, ReplacementPolicy.PLRU, SkewApproach.RS, 8,  EvictionPolicy.LE),
    Configuration(16, 2, 2, ReplacementPolicy.PLRU, SkewApproach.RS, 16, EvictionPolicy.LE),
    Configuration(16, 2, 2, ReplacementPolicy.PLRU, SkewApproach.RS, 32, EvictionPolicy.LE),
    Configuration(16, 2, 4, ReplacementPolicy.PLRU, SkewApproach.RS, 32, EvictionPolicy.LE),
    Configuration(16, 2, 8, ReplacementPolicy.PLRU, SkewApproach.RS, 32, EvictionPolicy.LE),

    # Invalid Tags (LA)
    Configuration(16, 2, 2, ReplacementPolicy.PLRU, SkewApproach.LA, 2,  EvictionPolicy.LE),
    Configuration(16, 2, 2, ReplacementPolicy.PLRU, SkewApproach.LA, 8,  EvictionPolicy.LE),
    Configuration(16, 2, 2, ReplacementPolicy.PLRU, SkewApproach.LA, 16, EvictionPolicy.LE),
    Configuration(16, 2, 2, ReplacementPolicy.PLRU, SkewApproach.LA, 32, EvictionPolicy.LE),

    # Eviction Policy (LE vs GE) x Ways
    Configuration(16, 2,  2, ReplacementPolicy.PLRU, SkewApproach.RS, 16, EvictionPolicy.LE),
    Configuration(16, 4,  2, ReplacementPolicy.PLRU, SkewApproach.RS, 16, EvictionPolicy.LE),
    Configuration(16, 8,  2, ReplacementPolicy.PLRU, SkewApproach.RS, 16, EvictionPolicy.LE),
    Configuration(16, 16, 2, ReplacementPolicy.PLRU, SkewApproach.RS, 16, EvictionPolicy.LE),

    Configuration(16, 2,  2, ReplacementPolicy.PLRU, SkewApproach.RS, 16, EvictionPolicy.GE),
    Configuration(16, 4,  2, ReplacementPolicy.PLRU, SkewApproach.RS, 16, EvictionPolicy.GE),
    Configuration(16, 8,  2, ReplacementPolicy.PLRU, SkewApproach.RS, 16, EvictionPolicy.GE),
    Configuration(16, 16, 2, ReplacementPolicy.PLRU, SkewApproach.RS, 16, EvictionPolicy.GE),

    Configuration(16, 2, 2, ReplacementPolicy.PLRU, SkewApproach.LA, 16, EvictionPolicy.GE),
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
    for j in range(0, 10):
        for idx, i in enumerate(configurations):
            # Modify Configuration
            print("Modifying Core.scala")
            os.chdir ("/ecosystem/core/src/main/scala/riscv/")

            with open("Core.scala", 'r', encoding='UTF-8') as file:
                lines = file.readlines()

            lines[69] = f"sets = {i.sets},\n"
            lines[70] = f"ways = {i.ways},\n"
            lines[71] = f"skews = {i.skews},\n"

            lines[73] = f"replacementPolicy = ReplacementPolicy.{i.replacement_policy.name},\n"
            lines[74] = f"skewApproach = SkewApproach.{i.skew_approach.name},\n"
            lines[75] = f"invalidTags = {i.invalid_tags},\n"
            lines[76] = f"evictionPolicy = EvictionPolicy.{i.eviction_policy.name},\n"

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
                    with open(f"/ecosystem/scripts/output/cost/cost_{i.sets}{i.ways}{i.skews}{i.randomized}{i.replacement_policy}{i.skew_approach}{i.invalid_tags}{i.eviction_policy}.txt", 'w', encoding="UTF-8") as file:
                        subprocess.run(['/ecosystem/.venv/bin/python3', '/ecosystem/eval-hd/eval-hd.py', '--cell-library', './eval-hd/freepdk-45nm/stdcells.lib', '/ecosystem/core/Core.v'], check=True, stdout=file)
                except Exception as e:
                    print(f"Cost analysis error: {e}\n")
                    cost_error += 1

            print(f"Starting benchmark {idx} iteration {j}")
            os.chdir("/ecosystem/benchmarks/embench")
            try:
                with open(f"/ecosystem/scripts/output/bench/{j}/bench_{i.sets}_{i.ways}_{i.skews}{i.randomized}{i.replacement_policy}{i.skew_approach}{i.invalid_tags}{i.eviction_policy}.txt", 'w', encoding="UTF-8") as file:
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