#include "rodentformat.h"
#include <string.h>

/*
 * Knight Pair Combination Table: C(5, 2) = 10 pairings
 *
 * Once bishops and queen are placed, 5 empty squares remain.
 * This table maps index 0..9 to the two chosen empty square indices:
 *
 *   Index | Empty Slots
 *   ------+------------
 *     0   | (0, 1)
 *     1   | (0, 2)
 *     2   | (0, 3)
 *     3   | (0, 4)
 *     4   | (1, 2)
 *     5   | (1, 3)
 *     6   | (1, 4)
 *     7   | (2, 3)
 *     8   | (2, 4)
 *     9   | (3, 4)
 */
static const uint8_t knight_table[10][2] = {
    {0, 1}, {0, 2}, {0, 3}, {0, 4},
    {1, 2}, {1, 3}, {1, 4},
    {2, 3}, {2, 4},
    {3, 4}   
};

/*
 * Decode an FRC 960 index (0..959) into an 8-piece array for Rank 1.
 *
 * Scharnagl Decomposition Algorithm:
 *
 * Initial Rank 1:
 *   File:    a   b   c   d   e   f   g   h
 *   Square: [0] [1] [2] [3] [4] [5] [6] [7]
 *   State:   .   .   .   .   .   .   .   .   (8 empty squares)
 *
 * Step 1: Light-Squared Bishop (b, d, f, h)
 *   bl = 2 * (idx % 4) + 1  -> Places B on file 1, 3, 5, or 7
 *   n1 = idx / 4            -> Range: 0..239
 *
 * Step 2: Dark-Squared Bishop (a, c, e, g)
 *   bd = 2 * (n1 % 4)       -> Places B on file 0, 2, 4, or 6
 *   n2 = n1 / 4             -> Range: 0..59
 *
 * Step 3: Queen (6 empty squares remaining)
 *   q_pos = n2 % 6          -> Places Q on the q_pos-th empty square
 *   n3 = n2 / 6             -> Range: 0..9
 *
 * Step 4: Knights (5 empty squares remaining)
 *   Pair (k1, k2) = knight_table[n3] -> Places N on k1-th and k2-th empty squares
 *
 * Step 5: Rooks & King (3 empty squares remaining)
 *   Remaining slots filled in fixed order: [Rook 1] ... [King] ... [Rook 2]
 *   (Guarantees King is strictly between the two Rooks for castling)
 */
static void decode_frc_rank1(uint16_t idx, uint8_t rank1[8], uint8_t *rook1_file, uint8_t *rook2_file) {
    memset(rank1, RODF_NONE, 8);

    /* 1. Light-Squared Bishop (Files 1, 3, 5, 7) */
    uint8_t bl = 2 * (idx % 4) + 1;
    rank1[bl] = RODF_BISHOP;
    uint16_t n1 = idx / 4;

    /* 2. Dark-Squared Bishop (Files 0, 2, 4, 6) */
    uint8_t bd = 2 * (n1 % 4);
    rank1[bd] = RODF_BISHOP;
    uint16_t n2 = n1 / 4;

    /* 3. Queen (Placed on q_pos-th empty square among 6) */
    uint8_t q_pos = n2 % 6;
    uint16_t n3 = n2 / 6;

    uint8_t empty_count = 0;
    for (uint8_t sq = 0; sq < 8; sq++) {
        if (rank1[sq] == RODF_NONE) {
            if (empty_count == q_pos) {
                rank1[sq] = RODF_QUEEN;
                break;
            }
            empty_count++;
        }
    }

    /* 4. Knights (Placed on k1 and k2 empty squares among 5) */
    uint8_t k1_idx = knight_table[n3 % 10][0];
    uint8_t k2_idx = knight_table[n3 % 10][1];

    empty_count = 0;
    for (uint8_t sq = 0; sq < 8; sq++) {
        if (rank1[sq] == RODF_NONE) {
            if (empty_count == k1_idx || empty_count == k2_idx) {
                rank1[sq] = RODF_KNIGHT;
            }
            empty_count++;
        }
    }

    /* 5. Remaining 3 squares: Left Rook -> King -> Right Rook */
    uint8_t r_idx = 0;
    for (uint8_t sq = 0; sq < 8; sq++) {
        if (rank1[sq] == RODF_NONE) {
            if (r_idx == 0) {
                rank1[sq] = RODF_ROOK;
                if (rook1_file) *rook1_file = sq;
            } else if (r_idx == 1) {
                rank1[sq] = RODF_KING;
            } else if (r_idx == 2) {
                rank1[sq] = RODF_ROOK;
                if (rook2_file) *rook2_file = sq;
            }
            r_idx++;
        }
    }
}

/*
 * Initialize a board from White & Black DFRC / Chess960 indices.
 *
 * Board Layout:
 *   Rank 8 (sq 56..63): Black Pieces (from black_idx)
 *   Rank 7 (sq 48..55): Black Pawns
 *   Rank 3..6:          Empty
 *   Rank 2 (sq  8..15): White Pawns
 *   Rank 1 (sq  0.. 7): White Pieces (from white_idx)
 */
bool rodf_board_from_dfrc(rodf_board_t *board, uint16_t white_idx, uint16_t black_idx) {
    if (white_idx >= 960 || black_idx >= 960 || !board) {
        return false;
    }

    memset(board, 0, sizeof(rodf_board_t));
    memset(board->pieces, RODF_NONE, 64);

    board->white_dfrc_idx = white_idx;
    board->black_dfrc_idx = black_idx;

    uint8_t w_rank1[8];
    uint8_t b_rank1[8];
    uint8_t w_r1 = 0, w_r2 = 0, b_r1 = 0, b_r2 = 0;

    decode_frc_rank1(white_idx, w_rank1, &w_r1, &w_r2);
    decode_frc_rank1(black_idx, b_rank1, &b_r1, &b_r2);

    /* Setup White Pieces (Rank 1: sq 0..7, Rank 2: sq 8..15) */
    for (int f = 0; f < 8; f++) {
        uint8_t pt = w_rank1[f];
        board->pieces[f] = pt;
        board->colors[f] = RODF_WHITE;
        board->piece_bb[pt] |= (1ULL << f);
        board->color_bb[RODF_WHITE] |= (1ULL << f);

        /* White Pawns */
        board->pieces[8 + f] = RODF_PAWN;
        board->colors[8 + f] = RODF_WHITE;
        board->piece_bb[RODF_PAWN] |= (1ULL << (8 + f));
        board->color_bb[RODF_WHITE] |= (1ULL << (8 + f));
    }

    /* Setup Black Pieces (Rank 8: sq 56..63, Rank 7: sq 48..55) */
    for (int f = 0; f < 8; f++) {
        uint8_t pt = b_rank1[f];
        uint8_t sq = 56 + f;
        board->pieces[sq] = pt;
        board->colors[sq] = RODF_BLACK;
        board->piece_bb[pt] |= (1ULL << sq);
        board->color_bb[RODF_BLACK] |= (1ULL << sq);

        /* Black Pawns */
        board->pieces[48 + f] = RODF_PAWN;
        board->colors[48 + f] = RODF_BLACK;
        board->piece_bb[RODF_PAWN] |= (1ULL << (48 + f));
        board->color_bb[RODF_BLACK] |= (1ULL << (48 + f));
    }

    board->stm = RODF_WHITE;
    board->ep_sq = 64; /* None */
    board->castling_rights = 0x0F; /* All 4 castling rights available */
    board->halfmove_clock = 0;
    board->fullmove_number = 1;

    /* Store Rook Squares for DFRC Castling Logic */
    board->castling_rook_sq[RODF_WHITE][0] = w_r2;      /* Kingside / H-side */
    board->castling_rook_sq[RODF_WHITE][1] = w_r1;      /* Queenside / A-side */
    board->castling_rook_sq[RODF_BLACK][0] = 56 + b_r2; /* Kingside / H-side */
    board->castling_rook_sq[RODF_BLACK][1] = 56 + b_r1; /* Queenside / A-side */

    return true;
}

/*
 * Convert starting 1st rank pieces back to FRC 960 index (0..959).
 *
 * Inverse Scharnagl Encoding:
 *   Index = (Knight_Pair_Index * 6 + Queen_Slot) * 16 + Dark_Bishop_Slot * 4 + Light_Bishop_Slot
 *
 * Worked Example (Standard Chess: R N B Q K B N R):
 *   1. Light Bishop on f1 (file 5) -> bl = (5 - 1) / 2 = 2
 *   2. Dark Bishop  on c1 (file 2) -> bd = 2 / 2 = 1
 *   3. Non-bishop slots: [R0, N1, Q2, K3, N4, R5] -> Queen is at slot 2 (q = 2)
 *   4. Non-queen/bishop slots: [R0, N1, K2, N3, R4] -> Knights are at slots (1, 3)
 *      knight_table index for (1, 3) is 5.
 *   5. Index = (5 * 6 + 2) * 16 + 1 * 4 + 2 = 32 * 16 + 6 = 518
 */
uint16_t rodf_rank1_to_frc(const uint8_t rank1[8]) {
    uint8_t bl = 0, bd = 0, q = 0, n1 = 0, n2 = 0;
    uint8_t k_count = 0;
    uint8_t b_light_count = 0, b_dark_count = 0, q_count = 0, n_count = 0;

    /* 1 & 2. Find Bishop positions on Rank 1 */
    for (uint8_t sq = 0; sq < 8; sq++) {
        if (rank1[sq] == RODF_BISHOP) {
            if (sq % 2 == 1) { bl = (sq - 1) / 2; b_light_count++; }
            else             { bd = sq / 2;       b_dark_count++; }
        } else if (rank1[sq] == RODF_QUEEN) {
            q_count++;
        } else if (rank1[sq] == RODF_KNIGHT) {
            n_count++;
        }
    }

    /* Fallback to standard chess (518) if rank 1 is not a standard full piece set */
    if (b_light_count != 1 || b_dark_count != 1 || q_count != 1 || n_count != 2) {
        return 518;
    }

    /* 3. Find Queen position among remaining 6 squares */
    uint8_t remaining6[6];
    uint8_t idx6 = 0;
    for (uint8_t sq = 0; sq < 8 && idx6 < 6; sq++) {
        if (rank1[sq] != RODF_BISHOP) {
            if (rank1[sq] == RODF_QUEEN) {
                q = idx6;
            }
            remaining6[idx6++] = rank1[sq];
        }
    }

    /* 4. Find Knights among remaining 5 squares */
    uint8_t idx5 = 0;
    for (uint8_t i = 0; i < 6 && idx5 < 5; i++) {
        if (i != q) {
            if (remaining6[i] == RODF_KNIGHT) {
                if (k_count == 0) n1 = idx5;
                else n2 = idx5;
                k_count++;
            }
            idx5++;
        }
    }

    /* Find Knight combination index (0..9) */
    uint8_t knight_idx = 0;
    for (uint8_t i = 0; i < 10; i++) {
        if (knight_table[i][0] == n1 && knight_table[i][1] == n2) {
            knight_idx = i;
            break;
        }
    }

    return (knight_idx * 6 + q) * 16 + bd * 4 + bl;
}