# RodentFormat Binary Specification

Version: 1.0.0  
Status: Standard  

---

## 1. Conventions

* **Byte Order**: All multi-byte integers are Little-Endian.
* **Squares**: Indexed from 0 to 63 (`a1 = 0, h1 = 7, a8 = 56, h8 = 63`).
* **Piece Types**:
  * `0`: Pawn
  * `1`: Knight
  * `2`: Bishop
  * `3`: Rook
  * `4`: Queen
  * `5`: King
  * `6`: None / Empty
* **Colors**: `0`: White, `1`: Black.
* **Game Outcomes**: `0`: Black Win (White Loss), `1`: Draw, `2`: White Win.

---

## 2. DFRC 960 Indexing

Every starting position in Double Fischer Random Chess is defined by two 10-bit numbers:
* `white_dfrc_idx` (0 to 959)
* `black_dfrc_idx` (0 to 959)

### Decomposition (0..959 to Rank 1 Pieces)
Given an index N between 0 and 959:

1. **Light-squared Bishop**: Placed on file `2 * (N % 4) + 1` (files b, d, f, or h). Set `N1 = N / 4`.
2. **Dark-squared Bishop**: Placed on file `2 * (N1 % 4)` (files a, c, e, or g). Set `N2 = N1 / 4`.
3. **Queen**: Placed on the `(N2 % 6)`-th empty square. Set `N3 = N2 / 6`.
4. **Knights**: Two knights placed on remaining empty squares using `N3` (0 to 9):
   * 0: (0, 1)
   * 1: (0, 2)
   * 2: (0, 3)
   * 3: (0, 4)
   * 4: (1, 2)
   * 5: (1, 3)
   * 6: (1, 4)
   * 7: (2, 3)
   * 8: (2, 4)
   * 9: (3, 4)
5. **Rooks and King**: The remaining 3 empty squares are filled in order: Left Rook, King, Right Rook. This guarantees the King is always between the two rooks.

Standard chess setup (`RNBQKBNR`) is index **518** for both White and Black.

---

## 3. Game Stream Format (`.rodf`)

A `.rodf` file is a continuous sequence of game records without padding between games.

### 3.1 Game Header (8 Bytes)

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|   White DFRC Index (0..959)   |   Black DFRC Index (0..959)   |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|STM| Res | Plies (0..1023)     |           Reserved            |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

* **Bits 0..9** (10 bits): `white_dfrc_idx` (0 to 959).
* **Bits 10..19** (10 bits): `black_dfrc_idx` (0 to 959).
* **Bit 20** (1 bit): `stm` (0 = White to move, 1 = Black to move).
* **Bits 21..22** (2 bits): `result` (0 = Loss, 1 = Draw, 2 = Win).
* **Bits 23..32** (10 bits): `ply_count` (0 to 1023).
* **Bits 33..63** (31 bits): Reserved for extensions. Must be written as 0.

### 3.2 Ply Record (4 Bytes per Ply)

Immediately following the 8-byte header are `ply_count` sequential ply entries:

```
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|          Packed Move (16 bits)         |  Eval Score (16 bits)|
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

* **Packed Move (16 bits)**:
  * Bits 0..5 (6 bits): `from_sq` (0 to 63).
  * Bits 6..11 (6 bits): `to_sq` (0 to 63).
  * Bits 12..13 (2 bits): `promo_type` (0 = Knight, 1 = Bishop, 2 = Rook, 3 = Queen).
  * Bits 14..15 (2 bits): `move_type`:
    * `0`: Normal move or normal capture
    * `1`: En-passant capture
    * `2`: Castling (King-takes-Rook)
    * `3`: Promotion
* **Eval Score (16 bits)**: Signed 16-bit integer representing centipawn evaluation from White's perspective.

### 3.3 Castling Move Encoding
In Fischer Random Chess, castling moves can involve non-standard squares. RodentFormat uses King-takes-Rook encoding:
* `from_sq` is set to the King's current square.
* `to_sq` is set to the castling Rook's current square.
* `move_type` is set to 2 (`RODF_MOVE_CASTLE`).

The final squares after castling are always identical to standard chess:
* White Kingside: King to g1, Rook to f1
* White Queenside: King to c1, Rook to d1
* Black Kingside: King to g8, Rook to f8
* Black Queenside: King to c8, Rook to d8

---

## 4. Flat SIMD Training Record (`.rodfb`)

For neural network training loops, positions can be dumped as fixed 16-byte records.

```c
#pragma pack(push, 1)
typedef struct {
    uint32_t white_occ;     /* Lower board occupancy: Ranks 1 to 4 */
    uint32_t black_occ;     /* Upper board occupancy: Ranks 5 to 8 */
    uint8_t  piece_data[4]; /* 4-bit nibble piece types */
    int16_t  score_cp;      /* Centipawn score */
    uint8_t  wdl_u8;        /* Outcome: 0 = Loss, 128 = Draw, 255 = Win */
    uint8_t  flags;         /* State flags */
} rodf_simd_record_t;
#pragma pack(pop)
```

### Record Fields

| Byte Offset | Size | Field | Description |
| :--- | :--- | :--- | :--- |
| **0..3** | 4 bytes | `white_occ` | Bitboard of pieces on squares 0 to 31 (ranks 1 to 4). |
| **4..7** | 4 bytes | `black_occ` | Bitboard of pieces on squares 32 to 63 (ranks 5 to 8). |
| **8..11** | 4 bytes | `piece_data[4]` | Nibble-packed piece types for active pieces. |
| **12..13** | 2 bytes | `score_cp` | Signed 16-bit centipawn score. |
| **14** | 1 byte | `wdl_u8` | Game outcome (0 = Loss, 128 = Draw, 255 = Win). |
| **15** | 1 byte | `flags` | Bit 0: Side to move (0 = White, 1 = Black).<br>Bits 1..6: Side-to-move King square (0 to 63).<br>Bit 7: Reserved. |

### Flags Layout

```
Bit 0:      stm (0 = White, 1 = Black)
Bits 1..6:  stm_king_sq (0 to 63)
Bit 7:      Reserved (0)
```

Storing the exact King square allows neural network training dataloaders to map positions to any number of king buckets (4, 8, 16, 32, or 64) with a single lookup table.
