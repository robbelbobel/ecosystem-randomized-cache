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
    pattern = (
        r"bench_(\d+)_(\d+)_(\d+)"                 # sets, ways, skews
        r"ReplacementPolicy\.(\w+)"                # replacement policy
        r"SkewApproach\.(\w+)"                    # skew approach
        r"(\d+)"                                  # invalid tags
        r"EvictionPolicy\.(\w+)\.txt$"            # eviction policy
    )

    match = re.match(pattern, filename)
    if not match:
        raise ValueError(f"Filename does not match expected format: {filename}")

    sets, ways, skews, rp, sa, invalid_tags, ep = match.groups()

    return Configuration(
        sets=int(sets),
        ways=int(ways),
        skews=int(skews),
        replacement_policy=ReplacementPolicy(rp),
        skew_approach=SkewApproach(sa),
        invalid_tags=int(invalid_tags),
        eviction_policy=EvictionPolicy(ep),
    )

# Bench
for iteration in os.listdir(os.path.join(os.getcwd(), 'output', 'bench')):
    for filename in os.listdir(os.path.join(os.getcwd(), 'output', 'bench', iteration)):
        with open(os.path.join(os.getcwd(), 'output', 'bench', iteration, filename), 'r') as bench_file:
            parsed = parse_filename(filename)

#            with open(os.path.join(os.getcwd(), 'benchmarks.csv', 'w')) as bench_csv:

