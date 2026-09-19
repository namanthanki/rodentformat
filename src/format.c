/**
 * @file format.c
 * @brief Binary Encoding and Decoding for RodentFormat (.rodf and .rodfb)
 *
 * Mode A: Stream Game Representation (.rodf)
 * ==========================================
 * An ultra-compact binary stream of complete games.
 *
 * 1. Packed 8-Byte Game Header:
 *   Bit:  0       10      20  21  23           33                            63
 *         +-------+-------+---+---+------------+------------------------------+
 *         | W_IDX | B_IDX |STM|RES| PLY_COUNT  |           RESERVED           |
 *         | (10b) | (10b) |1b |2b |  (10b)     |            (31b)             |
 *         +-------+-------+---+---+------------+------------------------------+
 *         W_IDX / B_IDX: DFRC Scharnagl indices (0..959)
 *         STM:           Side to move (0=White, 1=Black)
 *         RES:           Game result (0=Loss, 1=Draw, 2=Win)
 *         PLY_COUNT:     Number of sequential plies (0..1023)
 *
 * 2. Plies Array (4 Bytes per Ply):
 *   Bit:  0          6          12       14       16                        31
 *         +----------+----------+--------+--------+--------------------------+
 *         | FROM_SQ  |  TO_SQ   | PROMO  | TYPE   |       SCORE_CP           |
 *         |  (6b)    |   (6b)   |  (2b)  |  (2b)  |         (16b)            |
 *         +----------+----------+--------+--------+--------------------------+
 *         PROMO: 0=N, 1=B, 2=R, 3=Q
 *         TYPE:  0=Normal, 1=EnPassant, 2=Castle, 3=Promotion
 *         SCORE: Signed 16-bit centipawn evaluation (White-relative)
 *
 * Mode B: Direct SIMD Training Record (.rodfb)
 * ============================================
 * 16-byte fixed aligned record for GPU/CPU tensor batch loading.
 *
 *   Byte 0..3:   white_occ (Lower 32-bit occupancy: Ranks 1..4)
 *   Byte 4..7:   black_occ (Upper 32-bit occupancy: Ranks 5..8)
 *   Byte 8..11:  piece_data[4] (4-bit nibbles for active pieces)
 *   Byte 12..13: score_cp (Signed 16-bit centipawn score)
 *   Byte 14:     wdl_u8 (0=Loss, 128=Draw, 255=Win)
 *   Byte 15:     flags:
 *                - Bit 0:    stm (0=White, 1=Black)
 *                - Bits 1..6: STM King Square (0..63)
 *                - Bit 7:    Reserved
 */

#include "rodentformat.h"
#include <string.h>

/*
 * Encode a full game structure into a binary buffer.
 */
size_t rodf_encode_game(const rodf_game_t *game, uint8_t *out_buf, size_t max_size) {
    if (!game || !out_buf) return 0;

    uint16_t plies = (uint16_t)game->header.ply_count;
    size_t needed_bytes = 8 + (size_t)plies * 4;
    if (max_size < needed_bytes) return 0;

    /* 1. Pack 8-byte Header into a single 64-bit Little-Endian integer */
    uint64_t hdr = 0;
    hdr |= ((uint64_t)(game->header.white_dfrc_idx & 0x3FF));
    hdr |= ((uint64_t)(game->header.black_dfrc_idx & 0x3FF)) << 10;
    hdr |= ((uint64_t)(game->header.stm & 1)) << 20;
    hdr |= ((uint64_t)(game->header.result & 3)) << 21;
    hdr |= ((uint64_t)(game->header.ply_count & 0x3FF)) << 23;

    memcpy(out_buf, &hdr, 8);

    /* 2. Pack Plies (4 bytes each) */
    uint8_t *ptr = out_buf + 8;
    for (uint16_t i = 0; i < plies; i++) {
        uint16_t packed_move = 0;
        packed_move |= (uint16_t)(game->plies[i].move.from_sq & 0x3F);
        packed_move |= (uint16_t)((game->plies[i].move.to_sq & 0x3F) << 6);
        packed_move |= (uint16_t)((game->plies[i].move.promo_type & 0x03) << 12);
        packed_move |= (uint16_t)((game->plies[i].move.move_type & 0x03) << 14);

        int16_t score = game->plies[i].score;

        memcpy(ptr, &packed_move, 2);
        memcpy(ptr + 2, &score, 2);
        ptr += 4;
    }

    return needed_bytes;
}

/*
 * Decode a binary buffer in .rodf format into a rodf_game_t structure.
 */
size_t rodf_decode_game(const uint8_t *in_buf, size_t buf_size, rodf_game_t *out_game) {
    if (!in_buf || !out_game || buf_size < 8) return 0;

    /* 1. Unpack 8-byte Header */
    uint64_t hdr;
    memcpy(&hdr, in_buf, 8);

    out_game->header.white_dfrc_idx = (uint32_t)(hdr & 0x3FF);
    out_game->header.black_dfrc_idx = (uint32_t)((hdr >> 10) & 0x3FF);
    out_game->header.stm            = (uint32_t)((hdr >> 20) & 1);
    out_game->header.result         = (uint32_t)((hdr >> 21) & 3);
    out_game->header.ply_count      = (uint32_t)((hdr >> 23) & 0x3FF);
    out_game->header.reserved       = 0;

    uint16_t plies = (uint16_t)out_game->header.ply_count;
    if (plies > RODF_MAX_PLIES) return 0;

    size_t needed_bytes = 8 + (size_t)plies * 4;
    if (buf_size < needed_bytes) return 0;

    /* 2. Unpack Plies */
    const uint8_t *ptr = in_buf + 8;
    for (uint16_t i = 0; i < plies; i++) {
        uint16_t packed_move;
        int16_t score;

        memcpy(&packed_move, ptr, 2);
        memcpy(&score, ptr + 2, 2);

        out_game->plies[i].move.from_sq    = (uint16_t)(packed_move & 0x3F);
        out_game->plies[i].move.to_sq      = (uint16_t)((packed_move >> 6) & 0x3F);
        out_game->plies[i].move.promo_type = (uint16_t)((packed_move >> 12) & 0x03);
        out_game->plies[i].move.move_type  = (uint16_t)((packed_move >> 14) & 0x03);
        out_game->plies[i].score           = score;

        ptr += 4;
    }

    return needed_bytes;
}

/*
 * Convert a single board position into a 16-byte SIMD training record.
 */
void rodf_board_to_simd(const rodf_board_t *board, int16_t score_cp, uint8_t wdl_result, rodf_simd_record_t *out_rec) {
    if (!board || !out_rec) return;

    memset(out_rec, 0, sizeof(rodf_simd_record_t));

    /* 1. Occupancy: Lower 32 squares (Ranks 1..4) & Upper 32 squares (Ranks 5..8) */
    uint64_t occ = board->color_bb[RODF_WHITE] | board->color_bb[RODF_BLACK];
    out_rec->white_occ = (uint32_t)(occ & 0xFFFFFFFF);
    out_rec->black_occ = (uint32_t)(occ >> 32);

    /* 2. Nibble-pack piece types for active pieces (up to 8 pieces in 4 bytes) */
    int piece_count = 0;
    for (int sq = 0; sq < 64 && piece_count < 8; sq++) {
        if (occ & (1ULL << sq)) {
            uint8_t nibble = (uint8_t)(((board->colors[sq] & 1) << 3) | (board->pieces[sq] & 0x07));
            if (piece_count % 2 == 0) {
                out_rec->piece_data[piece_count / 2] |= (uint8_t)(nibble << 4);
            } else {
                out_rec->piece_data[piece_count / 2] |= (uint8_t)(nibble & 0x0F);
            }
            piece_count++;
        }
    }

    /* 3. Centipawn evaluation and WDL outcome */
    out_rec->score_cp = score_cp;
    out_rec->wdl_u8   = (wdl_result == RODF_RESULT_WIN) ? 255 : (wdl_result == RODF_RESULT_DRAW ? 128 : 0);

    /* 4. Flags: Bit 0: STM, Bits 1..6: STM King Square (0..63), Bit 7: Reserved */
    uint8_t stm_king_sq = 0;
    uint64_t king_bb = board->piece_bb[RODF_KING] & board->color_bb[board->stm];
    if (king_bb) {
        stm_king_sq = (uint8_t)__builtin_ctzll(king_bb);
    }

    out_rec->flags = (uint8_t)((board->stm & 1) | ((stm_king_sq & 0x3F) << 1));
}
