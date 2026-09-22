# Benchmark: 1.33B Position Conversion

This document records the results of running `rodf-tool convert` on a real 5.50 GiB (5.90 GB) Viriformat (`.vf`) dataset.

## Dataset Information

* **Input file**: `512hl_1b_dfrc.vf`
* **Output file**: `512hl_1b_dfrc.rodf`
* **Total games**: 15,851,683
* **Total positions**: 1,332,466,050 (1.332 billion)

---

## File Size Comparison

| Format | File Size (Bytes) | Size (GiB) | Size (GB) | Difference |
| :--- | :--- | :--- | :--- | :--- |
| Viriformat (`.vf`) | 5,900,524,788 | 5.50 GiB | 5.90 GB | Baseline |
| RodentFormat (`.rodf`) | 5,456,677,664 | 5.08 GiB | 5.46 GB | -423.3 MiB (-443.8 MB) |

The 423.3 MiB (443.8 MB) reduction comes entirely from replacing Viriformat's 36-byte starting board headers with RodentFormat's 8-byte Scharnagl DFRC headers.

---

## Conversion Speed

* **Total time**: 34.15 seconds
* **Throughput**: 39,016,897 positions per second (~39.0M pos/sec)
* **Memory usage**: Under 32 MB RAM (using 16 MB chunked streaming buffer)

---

## Dataset Statistics

Output from running `rodf-tool stats`:

```
Total Games        : 15,851,683
Total Positions    : 1,332,466,050
Avg Plies/Game     : 84.06
Unique DFRC Setups : 381,349 / 921,600 (41.38%)
Outcomes           : Wins: 5,117,015 (32.3%) | Draws: 6,437,462 (40.6%) | Losses: 4,297,206 (27.1%)
```

---

## How to Reproduce

```bash
# 1. Convert .vf to .rodf
./bin/rodf-tool convert --input 512hl_1b_dfrc.vf --output 512hl_1b_dfrc.rodf

# 2. Inspect dataset statistics
./bin/rodf-tool stats --input 512hl_1b_dfrc.rodf

# 3. Print sample games
./bin/rodf-tool dump --input 512hl_1b_dfrc.rodf --limit 2
```
