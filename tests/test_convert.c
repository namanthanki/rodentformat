/**
 * @file test_convert.c
 * @brief Unit tests for Viriformat (.vf) to RodentFormat (.rodf) conversion
 */

#include "rodentformat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static void test_synthetic_vf_conversion(void) {
    printf("[TEST] Testing Synthetic Viriformat (.vf) Game Conversion...\n");

    /*
     * Build Viriformat buffer based on Viridithas specification:
     * - Occupancy: standard startpos (ranks 1, 2, 7, 8)
     * - Pieces: RNBQKBNR for White & Black (castling rooks = type 6)
     * - Plies: 1. e4 (+10cp), 1... e5 (+20cp), followed by 4-zero terminator
     */
    uint8_t vf_data[32 + 4 * 2 + 4];
    memset(vf_data, 0, sizeof(vf_data));

    /* 1. Occupancy: 0xFFFF00000000FFFF */
    uint64_t occ = 0xFFFF00000000FFFFULL;
    memcpy(vf_data, &occ, 8);

    /* 2. Pieces:
     * White Rank 1: 0x16, 0x42, 0x25, 0x61  (R, N, B, Q, K, B, N, R with R=6)
     * White Rank 2: 0x00, 0x00, 0x00, 0x00  (Pawns = 0)
     * Black Rank 7: 0x88, 0x88, 0x88, 0x88  (Black Pawns = 8)
     * Black Rank 8: 0x9e, 0xca, 0xad, 0xe9  (Black Pieces with MSB 8)
     */
    uint8_t pieces[16] = {
        0x16, 0x42, 0x25, 0x61,
        0x00, 0x00, 0x00, 0x00,
        0x88, 0x88, 0x88, 0x88,
        0x9e, 0xca, 0xad, 0xe9
    };
    memcpy(vf_data + 8, pieces, 16);

    /* 3. State: STM=White, EP=none (64 -> 0x40 in lower 7 bits), Result=Win (2) */
    vf_data[24] = 0x40; /* STM=White (bit 7=0), EP=64 */
    vf_data[25] = 0;    /* Halfmove */
    vf_data[26] = 1;    /* Fullmove = 1 */
    vf_data[27] = 0;
    vf_data[28] = 0;    /* Score = 0 */
    vf_data[29] = 0;
    vf_data[30] = 2;    /* Result: White Win */
    vf_data[31] = 0;    /* Extra */

    /* 4. Plies */
    /* Ply 1: e2 -> e4 (from=12, to=28, promo=0, type=0 -> 0x070C), score = +10 */
    uint16_t move1 = 12 | (28 << 6);
    int16_t score1 = 10;
    memcpy(vf_data + 32, &move1, 2);
    memcpy(vf_data + 34, &score1, 2);

    /* Ply 2: e7 -> e5 (from=52, to=36, promo=0, type=0 -> 0x0934), score = +20 */
    uint16_t move2 = 52 | (36 << 6);
    int16_t score2 = 20;
    memcpy(vf_data + 36, &move2, 2);
    memcpy(vf_data + 38, &score2, 2);

    /* Terminator (4 zero bytes) already set by memset at 40..43 */

    rodf_game_t game;
    size_t consumed = rodf_convert_from_viriformat(vf_data, sizeof(vf_data), &game);
    assert(consumed == sizeof(vf_data));

    /* Verify DFRC starting position */
    assert(game.header.white_dfrc_idx == 518);
    assert(game.header.black_dfrc_idx == 518);
    assert(game.header.stm == RODF_WHITE);
    assert(game.header.result == RODF_RESULT_WIN);
    assert(game.header.ply_count == 2);

    /* Verify Plies */
    assert(game.plies[0].move.from_sq == 12);
    assert(game.plies[0].move.to_sq == 28);
    assert(game.plies[0].score == 10);

    assert(game.plies[1].move.from_sq == 52);
    assert(game.plies[1].move.to_sq == 36);
    assert(game.plies[1].score == 20);

    /* Verify Roundtrip into .rodf binary stream */
    uint8_t rodf_buf[128];
    size_t rodf_bytes = rodf_encode_game(&game, rodf_buf, sizeof(rodf_buf));
    assert(rodf_bytes == 8 + 2 * 4); /* 16 bytes for 2 plies */

    rodf_game_t decoded_game;
    size_t dec_bytes = rodf_decode_game(rodf_buf, rodf_bytes, &decoded_game);
    assert(dec_bytes == rodf_bytes);
    assert(decoded_game.header.white_dfrc_idx == 518);
    assert(decoded_game.header.black_dfrc_idx == 518);
    assert(decoded_game.header.ply_count == 2);

    printf("[TEST] PASS: Viriformat converted to RodentFormat successfully with bit-exact plies!\n");
}

static void test_real_vf_file_if_available(void) {
    const char *vf_path = "../rodentformat-draft/data_1786939968654560500_t4.vf";
    FILE *f = fopen(vf_path, "rb");
    if (!f) {
        printf("[TEST] INFO: Real sample .vf file not found at '%s', skipping file test.\n", vf_path);
        return;
    }

    printf("[TEST] Testing Real Viriformat File: %s...\n", vf_path);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *buffer = (uint8_t *)malloc((size_t)size);
    assert(buffer != NULL);
    size_t read_bytes = fread(buffer, 1, (size_t)size, f);
    fclose(f);
    assert(read_bytes == (size_t)size);

    size_t offset = 0;
    size_t games_converted = 0;
    size_t total_plies = 0;

    while (offset < (size_t)size) {
        rodf_game_t game;
        size_t consumed = rodf_convert_from_viriformat(buffer + offset, (size_t)size - offset, &game);
        if (consumed == 0) break;

        games_converted++;
        total_plies += game.header.ply_count;
        offset += consumed;
    }

    free(buffer);
    assert(games_converted > 0);
    printf("[TEST] PASS: Converted %zu real Viriformat games (%zu total positions)!\n", games_converted, total_plies);
}

int main(void) {
    test_synthetic_vf_conversion();
    test_real_vf_file_if_available();
    printf("\n[ALL TESTS PASSED] convert_vf.c is rock solid!\n");
    return 0;
}
