/**
 * @file test_dfrc.c
 * @brief Unit tests for DFRC 960 mathematical indexing
 */

#include "rodentformat.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

int main(void) {
    printf("[TEST] Running DFRC 960 Bijection Test...\n");

    for (uint16_t idx = 0; idx < 960; idx++) {
        rodf_board_t board;
        bool ok = rodf_board_from_dfrc(&board, idx, idx);
        assert(ok);

        /* Verify White Rank 1 */
        uint8_t rank1[8];
        for (int f = 0; f < 8; f++) {
            rank1[f] = board.pieces[f];
        }

        uint16_t reconstructed_idx = rodf_rank1_to_frc(rank1);
        if (reconstructed_idx != idx) {
            fprintf(stderr, "Mismatch at idx %u: got %u\n", idx, reconstructed_idx);
            return 1;
        }

        /* Verify standard chess is index 518 */
        if (idx == 518) {
            assert(rank1[0] == RODF_ROOK);
            assert(rank1[1] == RODF_KNIGHT);
            assert(rank1[2] == RODF_BISHOP);
            assert(rank1[3] == RODF_QUEEN);
            assert(rank1[4] == RODF_KING);
            assert(rank1[5] == RODF_BISHOP);
            assert(rank1[6] == RODF_KNIGHT);
            assert(rank1[7] == RODF_ROOK);
        }
    }

    printf("[TEST] PASS: All 960 DFRC positions verified with 100%% bijective consistency!\n");
    return 0;
}
