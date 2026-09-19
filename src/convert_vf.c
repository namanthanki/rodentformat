/**
 * @file convert_vf.c
 * @brief Viriformat (.vf) to RodentFormat (.rodf) Conversion Engine
 *
 * Viriformat (.vf) Game Structure:
 * ================================
 * Each game starts with a 32-byte PackedBoard followed by 4-byte plies,
 * terminated by a 4-byte zero sentinel.
 *
 * 1. PackedBoard (32 Bytes):
 *   Offset  Size  Field        Description
 *   ------+-----+------------+-----------------------------------------------
 *     0      8    occupied     64-bit bitboard of all occupied pieces on board
 *     8     16    pieces[16]   32 x 4-bit nibbles mapped to set bits of occupied
 *    24      1    stm_ep       Bit 7: STM (0=W, 1=B), Bits 0..6: EP square
 *    25      1    halfmove     Halfmove clock (50-move counter)
 *    26      2    fullmove     Fullmove number
 *    28      2    score        Position evaluation score
 *    30      1    result       0=Black Win, 1=Draw, 2=White Win
 *    31      1    extra        Unused padding byte
 *
 * 2. Piece Nibble Structure (4 Bits):
 *   Bit 3:    Color (0 = White, 1 = Black)
 *   Bits 0..2: Type  (0=Pawn, 1=Knight, 2=Bishop, 3=Rook, 4=Queen, 5=King, 6=Unmoved Castling Rook)
 *
 * 3. Scharnagl DFRC Bridge:
 *   Extract pieces on Rank 1 (White, sq 0..7) and Rank 8 (Black, sq 56..63),
 *   convert them via rodf_rank1_to_frc(), and store as 10-bit Scharnagl indices!
 */

#include "rodentformat.h"
#include <string.h>

#pragma pack(push, 1)
typedef struct {
    uint64_t occupied;     /* Bitboard of occupied squares */
    uint8_t  pieces[16];   /* 32 x 4-bit piece nibbles */
    uint8_t  stm_ep;       /* MSB: STM (0=White, 1=Black), Lower 7 bits: EP square */
    uint8_t  halfmove;     /* Halfmove clock */
    uint16_t fullmove;     /* Fullmove counter */
    int16_t  score;        /* Root score */
    uint8_t  result;       /* 0=Black win, 1=Draw, 2=White win */
    uint8_t  extra;        /* Padding byte */
} vf_packed_board_t;
#pragma pack(pop)

/*
 * Convert a Viriformat (.vf) game buffer into a rodf_game_t structure.
 */
size_t rodf_convert_from_viriformat(const uint8_t *vf_buf, size_t vf_len, rodf_game_t *out_game) {
    if (!vf_buf || !out_game || vf_len < 32) return 0;

    const vf_packed_board_t *pb = (const vf_packed_board_t *)vf_buf;
    if (pb->occupied == 0) return 0; /* Reserved extension or invalid game */

    memset(out_game, 0, sizeof(rodf_game_t));

    /* 1. Extract pieces on Rank 1 (White) and Rank 8 (Black) */
    uint8_t w_rank1[8] = {RODF_NONE, RODF_NONE, RODF_NONE, RODF_NONE, RODF_NONE, RODF_NONE, RODF_NONE, RODF_NONE};
    uint8_t b_rank1[8] = {RODF_NONE, RODF_NONE, RODF_NONE, RODF_NONE, RODF_NONE, RODF_NONE, RODF_NONE, RODF_NONE};

    uint64_t occ = pb->occupied;
    int piece_idx = 0;

    for (int sq = 0; sq < 64 && occ > 0; sq++) {
        if (occ & (1ULL << sq)) {
            /* Viriformat U4Array32: even index in low nibble (bits 0..3), odd in high nibble (bits 4..7) */
            uint8_t nibble = (uint8_t)((pb->pieces[piece_idx / 2] >> ((piece_idx % 2) * 4)) & 0x0F);
            uint8_t pt = (uint8_t)(nibble & 0x07);
            if (pt == 6) pt = RODF_ROOK; /* Type 6 is unmoved castling rook in Viriformat */

            uint8_t is_black = (uint8_t)((nibble >> 3) & 1);

            if (sq < 8 && !is_black) {
                w_rank1[sq] = pt;
            } else if (sq >= 56 && is_black) {
                b_rank1[sq - 56] = pt;
            }

            piece_idx++;
        }
    }

    /* 2. Compute Scharnagl DFRC indices */
    out_game->header.white_dfrc_idx = rodf_rank1_to_frc(w_rank1);
    out_game->header.black_dfrc_idx = rodf_rank1_to_frc(b_rank1);
    out_game->header.stm            = (uint32_t)((pb->stm_ep >> 7) & 1);
    out_game->header.result         = (uint32_t)(pb->result & 3);
    out_game->header.reserved       = 0;

    /* 3. Read Plies until 4-zero terminator */
    const uint8_t *ptr = vf_buf + 32;
    size_t consumed = 32;
    uint16_t ply_cnt = 0;

    /* Check for optional 4-byte zero separator after 32-byte header */
    if (consumed + 4 <= vf_len) {
        uint32_t val;
        memcpy(&val, ptr, 4);
        if (val == 0) {
            ptr += 4;
            consumed += 4;
        }
    }

    while (consumed + 4 <= vf_len && ply_cnt < RODF_MAX_PLIES) {
        uint32_t val;
        memcpy(&val, ptr, 4);
        if (val == 0) {
            consumed += 4;
            break; /* 4-zero sentinel terminates game */
        }

        uint16_t vf_move;
        int16_t vf_score;
        memcpy(&vf_move, ptr, 2);
        memcpy(&vf_score, ptr + 2, 2);

        out_game->plies[ply_cnt].move.from_sq    = (uint16_t)(vf_move & 0x3F);
        out_game->plies[ply_cnt].move.to_sq      = (uint16_t)((vf_move >> 6) & 0x3F);
        out_game->plies[ply_cnt].move.promo_type = (uint16_t)((vf_move >> 12) & 0x03);
        out_game->plies[ply_cnt].move.move_type  = (uint16_t)((vf_move >> 14) & 0x03);
        out_game->plies[ply_cnt].score           = vf_score;

        ply_cnt++;
        ptr += 4;
        consumed += 4;
    }

    out_game->header.ply_count = ply_cnt;
    return consumed;
}
