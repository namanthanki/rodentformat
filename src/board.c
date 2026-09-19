/**
 * @file board.c
 * @brief Board State Representation, FEN Parser, Serializer, and Move Application
 *
 * Square Coordinate System:
 *   Rank 8: 56 57 58 59 60 61 62 63  (a8..h8)
 *   Rank 7: 48 49 50 51 52 53 54 55  (a7..h7)
 *   ...
 *   Rank 2:  8  9 10 11 12 13 14 15  (a2..h2)
 *   Rank 1:  0  1  2  3  4  5  6  7  (a1..h1)
 *            a  b  c  d  e  f  g  h
 */

#include "rodentformat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static const char piece_chars[] = "PNBRQK";

/*
 * Parse a standard FEN string into a rodf_board_t structure.
 *
 * Example FEN:
 *   "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
 *
 * Sections:
 *   1. Piece placement by ranks from 8 down to 1
 *   2. Active color ('w' or 'b')
 *   3. Castling rights ('K', 'Q', 'k', 'q' or '-')
 *   4. En-passant target square (e.g. "e3" or '-')
 *   5. Halfmove clock (50-move rule counter)
 *   6. Fullmove number
 */
bool rodf_board_from_fen(rodf_board_t *board, const char *fen) {
    if (!board || !fen) return false;
    memset(board, 0, sizeof(rodf_board_t));
    memset(board->pieces, RODF_NONE, 64);

    int sq = 56; /* Start at A8 */
    const char *p = fen;

    /* 1. Parse Piece Placement */
    while (*p && *p != ' ') {
        if (*p == '/') {
            sq -= 16; /* Drop down one rank (e.g., from 64 to 48) */
        } else if (isdigit((unsigned char)*p)) {
            sq += (*p - '0');
        } else {
            char c = *p;
            rodf_color_t color = isupper((unsigned char)c) ? RODF_WHITE : RODF_BLACK;
            char upper_c = (char)toupper((unsigned char)c);
            rodf_piece_type_t pt = RODF_NONE;

            for (int i = 0; i < 6; i++) {
                if (piece_chars[i] == upper_c) {
                    pt = (rodf_piece_type_t)i;
                    break;
                }
            }

            if (pt != RODF_NONE && sq >= 0 && sq < 64) {
                board->pieces[sq] = pt;
                board->colors[sq] = color;
                board->piece_bb[pt] |= (1ULL << sq);
                board->color_bb[color] |= (1ULL << sq);
                sq++;
            }
        }
        p++;
    }

    if (!*p) return true;
    p++; /* Skip space */

    /* 2. Side to Move */
    board->stm = (*p == 'b') ? RODF_BLACK : RODF_WHITE;
    while (*p && *p != ' ') p++;
    if (!*p) return true;
    p++; /* Skip space */

    /* 3. Castling Rights */
    board->castling_rights = 0;
    while (*p && *p != ' ') {
        if (*p == 'K') board->castling_rights |= 1;
        else if (*p == 'Q') board->castling_rights |= 2;
        else if (*p == 'k') board->castling_rights |= 4;
        else if (*p == 'q') board->castling_rights |= 8;
        p++;
    }

    if (!*p) return true;
    p++; /* Skip space */

    /* 4. En-Passant Square */
    board->ep_sq = 64; /* None */
    if (*p && *p != '-') {
        int file = p[0] - 'a';
        int rank = p[1] - '1';
        if (file >= 0 && file < 8 && rank >= 0 && rank < 8) {
            board->ep_sq = (uint8_t)(rank * 8 + file);
        }
        p += 2;
    } else if (*p == '-') {
        p++;
    }

    /* 5 & 6. Halfmove Clock & Fullmove Number */
    board->halfmove_clock = 0;
    board->fullmove_number = 1;
    while (*p && *p == ' ') p++;
    if (*p) {
        int halfmove = 0, fullmove = 1;
        if (sscanf(p, "%d %d", &halfmove, &fullmove) >= 1) {
            board->halfmove_clock = (uint8_t)halfmove;
            board->fullmove_number = (uint16_t)(fullmove > 0 ? fullmove : 1);
        }
    }

    /* Deduce DFRC starting ranks and rook squares */
    uint8_t w_rank1[8], b_rank1[8];
    for (int f = 0; f < 8; f++) {
        w_rank1[f] = board->pieces[f];
        b_rank1[f] = board->pieces[56 + f];
    }
    board->white_dfrc_idx = rodf_rank1_to_frc(w_rank1);
    board->black_dfrc_idx = rodf_rank1_to_frc(b_rank1);

    /* Default standard chess rook squares */
    board->castling_rook_sq[RODF_WHITE][0] = 7;  /* h1 (kingside) */
    board->castling_rook_sq[RODF_WHITE][1] = 0;  /* a1 (queenside) */
    board->castling_rook_sq[RODF_BLACK][0] = 63; /* h8 (kingside) */
    board->castling_rook_sq[RODF_BLACK][1] = 56; /* a8 (queenside) */

    /* If king and rooks are on rank 1 / rank 8, find their exact files for FRC */
    int w_king_sq = -1, b_king_sq = -1;
    for (int f = 0; f < 8; f++) {
        if (board->pieces[f] == RODF_KING && board->colors[f] == RODF_WHITE) w_king_sq = f;
        if (board->pieces[56 + f] == RODF_KING && board->colors[56 + f] == RODF_BLACK) b_king_sq = 56 + f;
    }

    if (w_king_sq >= 0) {
        for (int f = 7; f > w_king_sq; f--) {
            if (board->pieces[f] == RODF_ROOK && board->colors[f] == RODF_WHITE) {
                board->castling_rook_sq[RODF_WHITE][0] = (uint8_t)f;
                break;
            }
        }
        for (int f = 0; f < w_king_sq; f++) {
            if (board->pieces[f] == RODF_ROOK && board->colors[f] == RODF_WHITE) {
                board->castling_rook_sq[RODF_WHITE][1] = (uint8_t)f;
                break;
            }
        }
    }

    if (b_king_sq >= 0) {
        for (int f = 63; f > b_king_sq; f--) {
            if (board->pieces[f] == RODF_ROOK && board->colors[f] == RODF_BLACK) {
                board->castling_rook_sq[RODF_BLACK][0] = (uint8_t)f;
                break;
            }
        }
        for (int f = 56; f < b_king_sq; f++) {
            if (board->pieces[f] == RODF_ROOK && board->colors[f] == RODF_BLACK) {
                board->castling_rook_sq[RODF_BLACK][1] = (uint8_t)f;
                break;
            }
        }
    }

    return true;
}

/*
 * Export a rodf_board_t to a standard FEN string.
 */
bool rodf_board_to_fen(const rodf_board_t *board, char *fen_buf, size_t buf_size) {
    if (!board || !fen_buf || buf_size < 128) return false;
    char *out = fen_buf;

    /* 1. Piece Placement */
    for (int rank = 7; rank >= 0; rank--) {
        int empty = 0;
        for (int file = 0; file < 8; file++) {
            int sq = rank * 8 + file;
            if (board->pieces[sq] == RODF_NONE) {
                empty++;
            } else {
                if (empty > 0) {
                    *out++ = (char)('0' + empty);
                    empty = 0;
                }
                char c = piece_chars[board->pieces[sq]];
                if (board->colors[sq] == RODF_BLACK) c = (char)tolower((unsigned char)c);
                *out++ = c;
            }
        }
        if (empty > 0) *out++ = (char)('0' + empty);
        if (rank > 0) *out++ = '/';
    }

    /* 2. Side to Move */
    *out++ = ' ';
    *out++ = (board->stm == RODF_WHITE) ? 'w' : 'b';
    *out++ = ' ';

    /* 3. Castling Rights */
    if (board->castling_rights == 0) {
        *out++ = '-';
    } else {
        if (board->castling_rights & 1) *out++ = 'K';
        if (board->castling_rights & 2) *out++ = 'Q';
        if (board->castling_rights & 4) *out++ = 'k';
        if (board->castling_rights & 8) *out++ = 'q';
    }

    /* 4. En-Passant Square */
    *out++ = ' ';
    if (board->ep_sq >= 64) {
        *out++ = '-';
    } else {
        *out++ = (char)('a' + (board->ep_sq % 8));
        *out++ = (char)('1' + (board->ep_sq / 8));
    }

    /* 5 & 6. Halfmove Clock & Fullmove Number */
    sprintf(out, " %u %u",
            (unsigned)board->halfmove_clock,
            (unsigned)(board->fullmove_number > 0 ? board->fullmove_number : 1));

    return true;
}

/*
 * Apply a rodf_move_t to the board state.
 *
 * DFRC / Chess960 Castling Mechanism:
 *   Castling is represented as King-takes-Rook (from = King square, to = Rook square).
 *   Regardless of starting squares, the final landing squares are identical to standard chess:
 *
 *     White Kingside:  King -> g1 (sq 6),  Rook -> f1 (sq 5)
 *     White Queenside: King -> c1 (sq 2),  Rook -> d1 (sq 3)
 *     Black Kingside:  King -> g8 (sq 62), Rook -> f8 (sq 61)
 *     Black Queenside: King -> c8 (sq 58), Rook -> d8 (sq 59)
 *
 * En-Passant Capture:
 *   Target square 'to' is the empty square jumped by opponent pawn on the previous ply.
 *   Captured pawn is at (to - 8) for White or (to + 8) for Black.
 */
bool rodf_make_move(rodf_board_t *board, rodf_move_t move) {
    if (!board) return false;

    uint8_t from = (uint8_t)move.from_sq;
    uint8_t to   = (uint8_t)move.to_sq;
    uint8_t stm  = board->stm;
    uint8_t enemy = (uint8_t)(stm ^ 1);

    uint8_t pt = board->pieces[from];
    if (pt == RODF_NONE || board->colors[from] != stm) {
        return false;
    }

    /* Reset en-passant square for this move */
    board->ep_sq = 64;

    /* Flag to reset halfmove clock on pawn move or capture */
    bool reset_halfmove = (pt == RODF_PAWN);

    /* 1. Handle Castling */
    if (move.move_type == RODF_MOVE_CASTLE) {
        uint8_t k_dst, r_dst, r_from;
        if (stm == RODF_WHITE) {
            bool is_kingside = (to == board->castling_rook_sq[RODF_WHITE][0]);
            k_dst = is_kingside ? 6 : 2; /* g1 or c1 */
            r_dst = is_kingside ? 5 : 3; /* f1 or d1 */
            r_from = is_kingside ? board->castling_rook_sq[RODF_WHITE][0] : board->castling_rook_sq[RODF_WHITE][1];
            board->castling_rights &= ~3; /* Clear both white rights */
        } else {
            bool is_kingside = (to == board->castling_rook_sq[RODF_BLACK][0]);
            k_dst = is_kingside ? 62 : 58; /* g8 or c8 */
            r_dst = is_kingside ? 61 : 59; /* f8 or d8 */
            r_from = is_kingside ? board->castling_rook_sq[RODF_BLACK][0] : board->castling_rook_sq[RODF_BLACK][1];
            board->castling_rights &= ~12; /* Clear both black rights */
        }

        /* Clear King & Rook from source squares first (handles potential piece square overlap) */
        board->piece_bb[RODF_KING] &= ~(1ULL << from);
        board->color_bb[stm]       &= ~(1ULL << from);
        board->pieces[from] = RODF_NONE;

        board->piece_bb[RODF_ROOK] &= ~(1ULL << r_from);
        board->color_bb[stm]       &= ~(1ULL << r_from);
        board->pieces[r_from] = RODF_NONE;

        /* Place King & Rook at destination squares */
        board->piece_bb[RODF_KING] |= (1ULL << k_dst);
        board->color_bb[stm]       |= (1ULL << k_dst);
        board->pieces[k_dst] = RODF_KING;
        board->colors[k_dst] = stm;

        board->piece_bb[RODF_ROOK] |= (1ULL << r_dst);
        board->color_bb[stm]       |= (1ULL << r_dst);
        board->pieces[r_dst] = RODF_ROOK;
        board->colors[r_dst] = stm;

        board->halfmove_clock++;
        if (stm == RODF_BLACK) board->fullmove_number++;
        board->stm = enemy;
        return true;
    }

    /* 2. Handle En-Passant Capture */
    if (move.move_type == RODF_MOVE_ENPASSANT) {
        uint8_t cap_sq = (stm == RODF_WHITE) ? (uint8_t)(to - 8) : (uint8_t)(to + 8);
        board->piece_bb[RODF_PAWN] &= ~(1ULL << cap_sq);
        board->color_bb[enemy]     &= ~(1ULL << cap_sq);
        board->pieces[cap_sq] = RODF_NONE;
        reset_halfmove = true;
    }

    /* 3. Handle Normal Capture on Destination */
    if (board->pieces[to] != RODF_NONE) {
        uint8_t capt_pt = board->pieces[to];
        board->piece_bb[capt_pt] &= ~(1ULL << to);
        board->color_bb[enemy]   &= ~(1ULL << to);
        reset_halfmove = true;

        /* If captured piece was an opponent rook on castling square, cancel that castling right */
        if (capt_pt == RODF_ROOK) {
            if (to == board->castling_rook_sq[enemy][0]) board->castling_rights &= ~(1 << (enemy * 2));
            if (to == board->castling_rook_sq[enemy][1]) board->castling_rights &= ~(2 << (enemy * 2));
        }
    }

    /* 4. Move Active Piece */
    board->piece_bb[pt] &= ~(1ULL << from);
    board->color_bb[stm] &= ~(1ULL << from);
    board->pieces[from] = RODF_NONE;

    uint8_t final_pt = pt;
    if (move.move_type == RODF_MOVE_PROMO) {
        /* promo_type: 0=Knight, 1=Bishop, 2=Rook, 3=Queen */
        final_pt = (uint8_t)(move.promo_type + 1);
    }

    board->piece_bb[final_pt] |= (1ULL << to);
    board->color_bb[stm]      |= (1ULL << to);
    board->pieces[to] = final_pt;
    board->colors[to] = stm;

    /* 5. Check for Double-Push Pawn to set En-Passant Square */
    if (pt == RODF_PAWN) {
        if (stm == RODF_WHITE && ((int)to - (int)from) == 16) {
            board->ep_sq = (uint8_t)(from + 8);
        } else if (stm == RODF_BLACK && ((int)from - (int)to) == 16) {
            board->ep_sq = (uint8_t)(from - 8);
        }
    }

    /* 6. Update Castling Rights for Moving King / Rook */
    if (pt == RODF_KING) {
        board->castling_rights &= (stm == RODF_WHITE) ? ~3 : ~12;
    } else if (pt == RODF_ROOK) {
        if (from == board->castling_rook_sq[stm][0]) board->castling_rights &= ~(1 << (stm * 2));
        if (from == board->castling_rook_sq[stm][1]) board->castling_rights &= ~(2 << (stm * 2));
    }

    /* 7. Update Clocks and Turn */
    if (reset_halfmove) {
        board->halfmove_clock = 0;
    } else {
        board->halfmove_clock++;
    }

    if (stm == RODF_BLACK) {
        board->fullmove_number++;
    }

    board->stm = enemy;
    return true;
}