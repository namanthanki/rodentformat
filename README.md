# RodentFormat

RodentFormat is a compact binary dataset format and C99 library for chess engines and neural network training. It includes native support for Double Fischer Random Chess (DFRC / Chess960).

## Why RodentFormat?

Most chess data formats either store verbose text FENs or heavy board structures at the start of every game. RodentFormat uses mathematical Scharnagl indexing to compress any DFRC starting position into two 10-bit numbers, keeping game headers down to 8 bytes.

* **Compact Games (`.rodf`)**: 8-byte game header and 4 bytes per ply. An 80-ply game takes only 328 bytes.
* **16-Byte Training Records (`.rodfb`)**: Flat, fixed-size records designed for fast streaming into neural network trainers.
* **Native DFRC / Chess960**: Full support for all 921,600 DFRC setups with king-takes-rook castling.
* **Pure C99**: Small codebase with zero external dependencies.

---

## Format Overview

### 1. Game Stream Format (`.rodf`)
A `.rodf` file is a series of games stored back-to-back:
* **8-Byte Header**: White DFRC index (10 bits), Black DFRC index (10 bits), side to move (1 bit), outcome (2 bits), ply count (10 bits), and reserved bits.
* **Plies (4 bytes each)**: Packed move (16 bits) and signed centipawn evaluation (16 bits).

### 2. Flat SIMD Record (`.rodfb`)
A 16-byte fixed record for training loaders:
* Bytes 0..3: Lower board occupancy (ranks 1 to 4)
* Bytes 4..7: Upper board occupancy (ranks 5 to 8)
* Bytes 8..11: Packed 4-bit piece types
* Bytes 12..13: Evaluation score (signed 16-bit integer)
* Byte 14: Game outcome (0 = loss, 128 = draw, 255 = win)
* Byte 15: Flags (Bit 0: side to move, Bits 1..6: side-to-move king square 0..63)

For complete bit diagrams and rules, see [SPECIFICATION.md](SPECIFICATION.md).

---

## Comparison

| Format | Start Position Size | Ply Size | 80-Ply Game Size | DFRC Start Position |
| :--- | :--- | :--- | :--- | :--- |
| **Binpack** | ~32 bytes | ~32 bytes | ~2,560 bytes | Not supported |
| **Viriformat** | 36 bytes | 4 bytes | 356 bytes | Packed board bitboard |
| **RodentFormat** | **8 bytes** | **4 bytes** | **328 bytes** | **20-bit Scharnagl index** |

---

## Building and Testing

The project uses a standard Makefile. It compiles with GCC or Clang on Windows and Linux.

```bash
# Build the CLI tool and run all unit tests
mingw32-make all
```

To run individual test suites:
```bash
mingw32-make test-dfrc        # Tests all 960 DFRC bijections
mingw32-make test-board       # Tests FEN parsing and move rules
mingw32-make test-roundtrip   # Tests binary serialization
mingw32-make test-convert     # Tests Viriformat conversion
```

---

## CLI Tool (`rodf-tool`)

The build creates a single executable `bin/rodf-tool` with five main commands:

### 1. Convert from Viriformat
Convert an existing `.vf` dataset to `.rodf`:
```bash
./bin/rodf-tool convert --input games.vf --output games.rodf
```

### 2. View Dataset Statistics
Inspect game counts, position counts, outcome ratios, and DFRC opening coverage:
```bash
./bin/rodf-tool stats --input games.rodf
```

### 3. Dump Games as Text
Print games in readable text with starting FEN and moves:
```bash
./bin/rodf-tool dump --input games.rodf --limit 5
```

### 4. Benchmark Replay Speed
Measure decoding and move application speed on your CPU:
```bash
./bin/rodf-tool bench --input games.rodf
```

### 5. Unpack to Training Records
Convert games into 16-byte records for neural network loaders:
```bash
./bin/rodf-tool unpack --input games.rodf --output dataset.rodfb
```
