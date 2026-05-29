import os
import re
import csv
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

print("Starting parse...")

def parse_filename(filename: str) -> Configuration:
    ''' Parses a filename into a Configuration '''
    spl = filename.split('_')

    return Configuration(
        sets=int(spl[1]),
        ways=int(spl[2]),
        skews=int(spl[3]),
        replacement_policy=ReplacementPolicy(spl[4]),
        skew_approach=SkewApproach(spl[5]),
        invalid_tags=int(spl[6]),
        eviction_policy=EvictionPolicy(spl[7]),
    )

# Bench
for iteration in os.listdir(os.path.join(os.getcwd(), 'output', 'bench')):
    for filename in os.listdir(os.path.join(os.getcwd(), 'output', 'bench', iteration)):
        with open(os.path.join(os.getcwd(), 'output', 'bench', iteration, filename), 'r') as bench_file:
            parsed = parse_filename(filename)

#            with open(os.path.join(os.getcwd(), 'benchmarks.csv', 'w')) as bench_csv:

