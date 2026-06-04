import os
import re
import csv
from dataclasses import dataclass
from enum import Enum

print("Starting parse...")

def parse_filename(filename: str) -> list[str]:
    ''' Parses a filename into a Configuration '''
    split = filename.split('_')[1:] # Remove bench_ or cost_

    split[-1] = split[-1][:-4] # Remove .txt

    for i in range(0, len(split)):
        if '.' in split[i]:
            split[i] = split[i].split('.')[-1] # Handle Enums
    
    return split

# Bench
print("Benchmarks")
with open(os.path.join(os.getcwd(), "bench.csv"), 'w') as bench_csv:
    writer = csv.writer(bench_csv)
    writer.writerow(['iteration', 'sets', 'ways', 'skews', 'eviction policy', 'insertion policy', 'skew approach', 'extra invalid tags', 'geometric mean', 'geometric sd', 'geometric range'])

    for iteration in os.listdir(os.path.join(os.getcwd(), 'output', 'bench')):
        if iteration[0] == '.':
            continue # Handle hidden files

        for filename in os.listdir(os.path.join(os.getcwd(), 'output', 'bench', iteration)):
            if filename[0] == '.':
                continue # Handle hidden files

            parsed = parse_filename(filename)

            mean = 0
            sd = 0
            rnge = 0

            print(str(os.path.join(os.getcwd(), 'output', 'bench', iteration, filename)))
            with open(os.path.join(os.getcwd(), 'output', 'bench', iteration, filename), 'r', encoding='ascii') as file:
                for line in file.readlines():
                    if "Geometric mean" in line:
                        mean = int(line.split(' ')[-1].replace(',', ''))
                    elif "Geometric SD" in line:
                        sd = float(line.split(' ')[-1])
                    elif "Geometric range" in line:
                        rnge = int(line.split(' ')[-1].replace(',', ''))
            
            writer.writerow([iteration] + parsed + [mean, sd, rnge])
        
        writer.writerow([])
print("cost")

with open(os.path.join(os.getcwd(), "cost.csv"), 'w') as cost_csv:
    writer = csv.writer(cost_csv)
    writer.writerow(['sets', 'ways', 'skews', 'eviction policy', 'insertion policy', 'skew approach', 'extra invalid tags', 'chip area'])

    for filename in os.listdir(os.path.join(os.getcwd(), 'output', 'cost')):
        if filename[0] == '.':
            continue # Handle hidden files

        parsed = parse_filename(filename) 
        cost = 0.0

        print(str(os.path.join(os.getcwd(), 'output', 'cost', filename)))
        with open(os.path.join(os.getcwd(), 'output', 'cost', filename), 'r', encoding='ascii') as file:
            for line in file.readlines():
                if "Chip area" in line:
                    cost = float(line.split(' ')[-1])

        writer.writerow(parsed + [cost])
