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
    randomized: bool
    replacement_policy: ReplacementPolicy
    skew_approach: SkewApproach
    invalid_tags: int
    eviction_policy: EvictionPolicy

### CONFIGURATIONS ###
configurations: list[Configuration] = [
    Configuration(4, 2, 2, True,
                    ReplacementPolicy.PLRU,
                    SkewApproach.RS,
                    0,
                    EvictionPolicy.LE),
    Configuration(16, 2, 2, True,
                    ReplacementPolicy.PLRU,
                    SkewApproach.RS,
                    0,
                    EvictionPolicy.LE),
    Configuration(32, 2, 2, True,
                    ReplacementPolicy.PLRU,
                    SkewApproach.RS,
                    0,
                    EvictionPolicy.LE),
]

### LOGIC ###
# Cache Configuration Backup
print("Making a backup of Core.scala...")
os.chdir ("/ecosystem/core/src/main/scala/riscv")
subprocess.run(["cp", "./Core.scala", "/ecosystem/scripts/Core.bak.scala"], check=True)

# Make Benchmarks
try:
    print("Activating python3 virtual environment...")
    os.chdir("/ecosystem")
    subprocess.run(['source', '.venv/bin/activate'], check=True)

    print("Making benchmarks...")
    os.chdir("/ecosystem/benchmarks/embench")
    subprocess.run(['make'], check=True, stdout=subprocess.DEVNULL)

    for idx, i in enumerate(configurations):
        # Modify Configuration
        print("Modifying Core.scala")
        os.chdir ("/ecosystem/core/src/main/scala/riscv/")

        with open("Core.scala", 'r', encoding='UTF-8') as file:
            lines = file.readlines()

        lines[69] = f"sets = {i.sets},\n"
        lines[70] = f"ways = {i.ways},\n"
        lines[71] = f"skews = {i.skews},\n"
        lines[73] = f"randomizedSetIndexing = {str(i.randomized).lower()},\n"
        lines[74] = f"replacementPolicy = ReplacementPolicy.{i.replacement_policy.name},\n"
        lines[75] = f"skewApproach = SkewApproach.{i.skew_approach.name},\n"
        lines[76] = f"invalidTags = {i.invalid_tags},\n"
        lines[77] = f"evictionPolicy = EvictionPolicy.{i.eviction_policy.name},\n"

        with open("Core.scala", 'w', encoding='UTF-8') as file:
            file.writelines(lines)

        print("Building simulation...")
        os.chdir("/ecosystem/")
        subprocess.run(["make", "-C", "simulation"], check=True)
        
        print(f"Starting cost analysis {idx}")
        os.chdir("/ecosystem")
        with open(f"cost_{i.sets}{i.ways}{i.skews}{i.randomized}{i.replacement_policy}{i.skew_approach}{i.invalid_tags}{i.eviction_policy}.txt", 'w', encoding="UTF-8") as file:
            subprocess.run(['./eval-hd/eval-hd.py', '--cell-library', './eval-hd/freepdk-45nm/stdcells.lib'], check=True, stdout=file)

        print(f"Starting benchmark {idx}...")
        os.chdir("/ecosystem/benchmarks/embench")
        with open(f"bench_{i.sets}{i.ways}{i.skews}{i.randomized}{i.replacement_policy}{i.skew_approach}{i.invalid_tags}{i.eviction_policy}.txt", 'w', encoding="UTF-8") as file:
            subprocess.run(['python3',
                            'benchmark_speed.py',
                            '--target-module=run_proteus', 
                            '--timeout=5400', 
                            '--absolute'], check=True, stdout=file)
            
        os.chdir("")
except Exception as e:
    print(f"ERROR! {e}")
finally:
    print("Exiting...")
    os.chdir ("/ecosystem/core/src/main/scala/riscv/")
    os.remove("./Core.scala")
    subprocess.run(["mv", "/ecosystem/scripts/Core.bak.scala", "./Core.scala"], check=True)