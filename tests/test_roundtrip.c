/**
 * @file test_roundtrip.c
 * @brief Unit tests for binary game encoding/decoding and SIMD record generation
 */

#include "rodentformat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static void test_game_roundtrip(void) {
    printf("[TEST] Testing Binary Game Encode & Decode Roundtrip...\n");

    rodf_game_t game_in;
    memset(&game_in, 0, sizeof(rodf_game_t));

    game_in.header.white_dfrc_idx = 518;
    game_in.header.black_dfrc_idx = 959;
    game_in.header.stm = RODF_WHITE;
    game_in.header.result = RODF_RESULT_WIN;
    game_in.header.ply_count = 6;

    /* 6 Sample Plies */
    game_in.plies[0].move.from_sq = 12; /* e2 */
    game_in.plies[0].move.to_sq = 28;   /* e4 */
    game_in.plies[0].move.promo_type = 0;
    game_in.plies[0].move.move_type = RODF_MOVE_NORMAL;
    game_in.plies[0].score = 15;

    game_in.plies[1].move.from_sq = 52; /* e7 */
    game_in.plies[1].move.to_sq = 36;   /* e5 */
    game_in.plies[1].move.promo_type = 0;
    game_in.plies[1].move.move_type = RODF_MOVE_NORMAL;
    game_in.plies[1].score = 12;

    game_in.plies[2].move.from_sq = 6;  /* g1 */
    game_in.plies[2].move.to_sq = 21;  /* f3 */
    game_in.plies[2].move.promo_type = 0;
    game_in.plies[2].move.move_type = RODF_MOVE_NORMAL;
    game_in.plies[2].score = 25;

    game_in.plies[3].move.from_sq = 57; /* b8 */
    game_in.plies[3].move.to_sq = 42;  /* c6 */
    game_in.plies[3].move.promo_type = 0;
    game_in.plies[3].move.move_type = RODF_MOVE_NORMAL;
    game_in.plies[3].score = 20;

    game_in.plies[4].move.from_sq = 4;  /* e1 */
    game_in.plies[4].move.to_sq = 7;   /* h1 (Castling) */
    game_in.plies[4].move.promo_type = 0;
    game_in.plies[4].move.move_type = RODF_MOVE_CASTLE;
    game_in.plies[4].score = 40;

    game_in.plies[5].move.from_sq = 48; /* a7 */
    game_in.plies[5].move.to_sq = 56;  /* a8 (Promotion) */
    game_in.plies[5].move.promo_type = 3; /* Queen */
    game_in.plies[5].move.move_type = RODF_MOVE_PROMO;
    game_in.plies[5].score = 900;

    uint8_t buffer[256];
    size_t written = rodf_encode_game(&game_in, buffer, sizeof(buffer));
    assert(written == 8 + 6 * 4); /* 32 bytes */

    rodf_game_t game_out;
    memset(&game_out, 0, sizeof(rodf_game_t));
    size_t consumed = rodf_decode_game(buffer, written, &game_out);
    assert(consumed == written);

    /* Verify Header */
    assert(game_out.header.white_dfrc_idx == 518);
    assert(game_out.header.black_dfrc_idx == 959);
    assert(game_out.header.stm == RODF_WHITE);
    assert(game_out.header.result == RODF_RESULT_WIN);
    assert(game_out.header.ply_count == 6);

    /* Verify Plies */
    for (uint16_t i = 0; i < 6; i++) {
        assert(game_out.plies[i].move.from_sq == game_in.plies[i].move.from_sq);
        assert(game_out.plies[i].move.to_sq == game_in.plies[i].move.to_sq);
        assert(game_out.plies[i].move.promo_type == game_in.plies[i].move.promo_type);
        assert(game_out.plies[i].move.move_type == game_in.plies[i].move.move_type);
        assert(game_out.plies[i].score == game_in.plies[i].score);
    }

    printf("[TEST] PASS: Game encoding and decoding is bit-exact!\n");
}

static void test_simd_record(void) {
    printf("[TEST] Testing 16-Byte Direct Training SIMD Record...\n");

    /* 1. Size and Alignment */
    assert(sizeof(rodf_simd_record_t) == 16);

    /* 2. Setup Standard Chess Board */
    rodf_board_t board;
    bool ok = rodf_board_from_dfrc(&board, 518, 518);
    assert(ok);

    rodf_simd_record_t rec_w;
    rodf_board_to_simd(&board, 42, RODF_RESULT_WIN, &rec_w);

    assert(rec_w.score_cp == 42);
    assert(rec_w.wdl_u8 == 255);
    assert(rec_w.white_occ == 0x0000FFFF); /* Ranks 1 & 2 */
    assert(rec_w.black_occ == 0xFFFF0000); /* Ranks 7 & 8 */

    /* Verify Flags: STM = White (0), King Square = e1 (sq 4) */
    assert((rec_w.flags & 1) == RODF_WHITE);
    uint8_t w_king_sq = (rec_w.flags >> 1) & 0x3F;
    assert(w_king_sq == 4); /* e1 */

    /* 3. Flip to Black's turn and test Black King Square */
    board.stm = RODF_BLACK;
    rodf_simd_record_t rec_b;
    rodf_board_to_simd(&board, -42, RODF_RESULT_LOSS, &rec_b);

    assert(rec_b.score_cp == -42);
    assert(rec_b.wdl_u8 == 0);
    assert((rec_b.flags & 1) == RODF_BLACK);
    uint8_t b_king_sq = (rec_b.flags >> 1) & 0x3F;
    assert(b_king_sq == 60); /* e8 */

    printf("[TEST] PASS: 16-byte SIMD record layout and STM king square indexing verified!\n");
}

int main(void) {
    test_game_roundtrip();
    test_simd_record();
    printf("\n[ALL TESTS PASSED] format.c is rock solid!\n");
    return 0;
}
