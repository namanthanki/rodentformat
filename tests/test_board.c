/**
 * @file test_board.c
 * @brief Unit tests for board representation, FEN parser/serializer, and move application
 */

#include "rodentformat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static void test_fen_roundtrip(void) {
    printf("[TEST] Testing FEN Parser & Serializer Roundtrip...\n");

    const char *start_fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    rodf_board_t board;
    bool ok = rodf_board_from_fen(&board, start_fen);
    assert(ok);

    assert(board.stm == RODF_WHITE);
    assert(board.castling_rights == 0x0F);
    assert(board.ep_sq == 64);
    assert(board.halfmove_clock == 0);
    assert(board.fullmove_number == 1);
    assert(board.white_dfrc_idx == 518);
    assert(board.black_dfrc_idx == 518);

    char fen_out[128];
    ok = rodf_board_to_fen(&board, fen_out, sizeof(fen_out));
    assert(ok);
    assert(strcmp(start_fen, fen_out) == 0);

    /* Test arbitrary mid-game FEN */
    const char *mid_fen = "r1bqk2r/pp2bppp/2n1pn2/2pp4/2PP4/2N1PN2/PP2BPPP/R1BQK2R w KQkq - 4 7";
    ok = rodf_board_from_fen(&board, mid_fen);
    assert(ok);
    ok = rodf_board_to_fen(&board, fen_out, sizeof(fen_out));
    assert(ok);
    assert(strcmp(mid_fen, fen_out) == 0);

    printf("[TEST] PASS: FEN Roundtrip bit-exact!\n");
}

static void test_move_application(void) {
    printf("[TEST] Testing Move Application & Castling...\n");

    rodf_board_t board;
    rodf_board_from_fen(&board, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

    /* 1. e4 (e2 -> e4: sq 12 -> 28) */
    rodf_move_t m_e4 = { .from_sq = 12, .to_sq = 28, .promo_type = 0, .move_type = RODF_MOVE_NORMAL };
    assert(rodf_make_move(&board, m_e4));
    assert(board.stm == RODF_BLACK);
    assert(board.ep_sq == 20); /* e3 */
    assert(board.pieces[28] == RODF_PAWN);
    assert(board.pieces[12] == RODF_NONE);

    /* 1... e5 (e7 -> e5: sq 52 -> 36) */
    rodf_move_t m_e5 = { .from_sq = 52, .to_sq = 36, .promo_type = 0, .move_type = RODF_MOVE_NORMAL };
    assert(rodf_make_move(&board, m_e5));
    assert(board.stm == RODF_WHITE);
    assert(board.ep_sq == 44); /* e6 */
    assert(board.fullmove_number == 2);

    /* 2. Nf3 (g1 -> f3: sq 6 -> 21) */
    rodf_move_t m_nf3 = { .from_sq = 6, .to_sq = 21, .promo_type = 0, .move_type = RODF_MOVE_NORMAL };
    assert(rodf_make_move(&board, m_nf3));

    /* 2... Nc6 (b8 -> c6: sq 57 -> 42) */
    rodf_move_t m_nc6 = { .from_sq = 57, .to_sq = 42, .promo_type = 0, .move_type = RODF_MOVE_NORMAL };
    assert(rodf_make_move(&board, m_nc6));

    /* 3. Bc4 (f1 -> c4: sq 5 -> 26) */
    rodf_move_t m_bc4 = { .from_sq = 5, .to_sq = 26, .promo_type = 0, .move_type = RODF_MOVE_NORMAL };
    assert(rodf_make_move(&board, m_bc4));

    /* 3... Bc5 (f8 -> c5: sq 61 -> 34) */
    rodf_move_t m_bc5 = { .from_sq = 61, .to_sq = 34, .promo_type = 0, .move_type = RODF_MOVE_NORMAL };
    assert(rodf_make_move(&board, m_bc5));

    /* 4. O-O (White Kingside Castle: e1 -> h1: sq 4 -> 7, King-takes-Rook) */
    rodf_move_t m_castle = { .from_sq = 4, .to_sq = 7, .promo_type = 0, .move_type = RODF_MOVE_CASTLE };
    assert(rodf_make_move(&board, m_castle));
    assert(board.pieces[6] == RODF_KING);  /* g1 */
    assert(board.pieces[5] == RODF_ROOK);  /* f1 */
    assert(board.pieces[4] == RODF_NONE);  /* e1 */
    assert(board.pieces[7] == RODF_NONE);  /* h1 */
    assert((board.castling_rights & 3) == 0); /* White rights cleared */

    printf("[TEST] PASS: Normal moves & King-takes-Rook castling verified!\n");
}

static void test_en_passant(void) {
    printf("[TEST] Testing En-Passant Capture...\n");

    /* Position: White pawn on e5 (sq 36), Black to move with pawn on d7 */
    rodf_board_t board;
    rodf_board_from_fen(&board, "rnbqkbnr/pppppppp/8/4P3/8/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1");

    /* Black plays d5 */
    rodf_move_t m_d5 = { .from_sq = 51, .to_sq = 35, .promo_type = 0, .move_type = RODF_MOVE_NORMAL };
    assert(rodf_make_move(&board, m_d5));
    assert(board.ep_sq == 43); /* d6 */

    /* White plays exd6 e.p. (sq 36 -> 43) */
    rodf_move_t m_ep = { .from_sq = 36, .to_sq = 43, .promo_type = 0, .move_type = RODF_MOVE_ENPASSANT };
    assert(rodf_make_move(&board, m_ep));
    assert(board.pieces[43] == RODF_PAWN);
    assert(board.colors[43] == RODF_WHITE);
    assert(board.pieces[35] == RODF_NONE); /* Black pawn on d5 removed */
    assert(board.pieces[36] == RODF_NONE); /* White pawn on e5 vacated */

    printf("[TEST] PASS: En-passant capture verified!\n");
}

int main(void) {
    test_fen_roundtrip();
    test_move_application();
    test_en_passant();
    printf("\n[ALL TESTS PASSED] board.c is rock solid!\n");
    return 0;
}
