/**
 * @file rodf_tool.c
 * @brief Command-Line Tool for RodentFormat (.rodf) Dataset Operations
 *
 * Subcommands:
 *   stats   - Display dataset metrics (game count, plies, DFRC coverage, W/D/L)
 *   convert - Convert Viriformat (.vf) dataset directly into RodentFormat (.rodf)
 *   unpack  - Unpack .rodf games into 16-byte SIMD training records (.rodfb)
 *   dump    - Pretty-print games in human-readable UCI notation with FENs
 *   bench   - Measure decoder, board replay, and SIMD extraction throughput
 */

#include "rodentformat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void print_usage(const char *prog) {
    printf("RodentFormat CLI Tool (RODF v%d.%d.%d)\n\n", 
           RODF_VERSION_MAJOR, RODF_VERSION_MINOR, RODF_VERSION_PATCH);
    printf("Usage:\n");
    printf("  %s stats   --input <file.rodf>\n", prog);
    printf("  %s convert --input <file.vf> --output <file.rodf>\n", prog);
    printf("  %s unpack  --input <file.rodf> --output <file.rodfb>\n", prog);
    printf("  %s dump    --input <file.rodf> [--limit <n>]\n", prog);
    printf("  %s bench   --input <file.rodf>\n", prog);
}

static void sq_to_uci(uint8_t sq, char *out) {
    out[0] = (char)('a' + (sq % 8));
    out[1] = (char)('1' + (sq / 8));
    out[2] = '\0';
}

static int do_stats(const char *input_path) {
    FILE *f = fopen(input_path, "rb");
    if (!f) {
        fprintf(stderr, "Error: Cannot open '%s'\n", input_path);
        return 1;
    }

    uint64_t total_games = 0;
    uint64_t total_plies = 0;
    uint64_t wins = 0, draws = 0, losses = 0;
    uint64_t dfrc_unique_setups = 0;
    uint8_t (*dfrc_seen)[960] = (uint8_t (*)[960])calloc(960, sizeof(*dfrc_seen));
    if (!dfrc_seen) {
        fprintf(stderr, "Error: Memory allocation failed for DFRC tracker\n");
        fclose(f);
        return 1;
    }

    uint8_t header_buf[8];
    while (fread(header_buf, 1, 8, f) == 8) {
        uint64_t hdr;
        memcpy(&hdr, header_buf, 8);

        uint16_t w_idx = (uint16_t)(hdr & 0x3FF);
        uint16_t b_idx = (uint16_t)((hdr >> 10) & 0x3FF);
        uint8_t res = (uint8_t)((hdr >> 21) & 3);
        uint16_t plies = (uint16_t)((hdr >> 23) & 0x3FF);

        total_games++;
        total_plies += plies;

        if (res == RODF_RESULT_WIN) wins++;
        else if (res == RODF_RESULT_DRAW) draws++;
        else losses++;

        if (w_idx < 960 && b_idx < 960 && !dfrc_seen[w_idx][b_idx]) {
            dfrc_seen[w_idx][b_idx] = 1;
            dfrc_unique_setups++;
        }

        /* Skip ply data */
        fseek(f, (long)(plies * 4), SEEK_CUR);
    }

    free(dfrc_seen);
    fclose(f);

    printf("=== RodentFormat Dataset Stats: %s ===\n", input_path);
    printf("Total Games        : %llu\n", (unsigned long long)total_games);
    printf("Total Positions    : %llu\n", (unsigned long long)total_plies);
    printf("Avg Plies/Game     : %.2f\n", total_games > 0 ? (double)total_plies / total_games : 0.0);
    printf("Unique DFRC Setups : %llu / 921,600 (%.2f%%)\n", 
           (unsigned long long)dfrc_unique_setups, (double)dfrc_unique_setups * 100.0 / 921600.0);
    printf("Outcomes           : Wins: %llu (%.1f%%) | Draws: %llu (%.1f%%) | Losses: %llu (%.1f%%)\n",
           (unsigned long long)wins, total_games ? ((double)wins * 100.0 / total_games) : 0.0,
           (unsigned long long)draws, total_games ? ((double)draws * 100.0 / total_games) : 0.0,
           (unsigned long long)losses, total_games ? ((double)losses * 100.0 / total_games) : 0.0);

    return 0;
}

static int do_convert(const char *in_vf, const char *out_rodf) {
    FILE *fin = fopen(in_vf, "rb");
    if (!fin) {
        fprintf(stderr, "Error: Cannot open '%s'\n", in_vf);
        return 1;
    }

    FILE *fout = fopen(out_rodf, "wb");
    if (!fout) {
        fclose(fin);
        fprintf(stderr, "Error: Cannot create '%s'\n", out_rodf);
        return 1;
    }

    fseek(fin, 0, SEEK_END);
    long file_sz = ftell(fin);
    fseek(fin, 0, SEEK_SET);
    printf("Reading '%s' (%ld bytes)...\n", in_vf, file_sz);

    uint8_t *vf_data = (uint8_t *)malloc((size_t)file_sz);
    if (!vf_data) {
        fprintf(stderr, "Error: Memory allocation failed for %ld bytes\n", file_sz);
        fclose(fin);
        fclose(fout);
        return 1;
    }

    size_t read_bytes = fread(vf_data, 1, (size_t)file_sz, fin);
    fclose(fin);

    if (read_bytes != (size_t)file_sz) {
        fprintf(stderr, "Warning: read %zu bytes, expected %ld\n", read_bytes, file_sz);
    }

    size_t offset = 0;
    uint64_t converted_games = 0;
    uint64_t converted_plies = 0;
    uint8_t out_buf[8 + RODF_MAX_PLIES * 4];

    clock_t start = clock();

    while (offset < read_bytes) {
        rodf_game_t game;
        size_t consumed = rodf_convert_from_viriformat(vf_data + offset, read_bytes - offset, &game);
        if (consumed == 0) {
            offset += 4;
            continue;
        }

        size_t encoded_bytes = rodf_encode_game(&game, out_buf, sizeof(out_buf));
        if (encoded_bytes > 0) {
            fwrite(out_buf, 1, encoded_bytes, fout);
            converted_games++;
            converted_plies += game.header.ply_count;
        }

        offset += consumed;
    }

    free(vf_data);
    fclose(fout);

    double elapsed = (double)(clock() - start) / CLOCKS_PER_SEC;
    printf("Converted %llu games (%llu positions) from .vf to .rodf in %.2fs (%.0f pos/sec)\n",
           (unsigned long long)converted_games, (unsigned long long)converted_plies,
           elapsed, elapsed > 0 ? ((double)converted_plies / elapsed) : 0.0);

    return 0;
}

static int do_dump(const char *in_rodf, int limit) {
    FILE *f = fopen(in_rodf, "rb");
    if (!f) {
        fprintf(stderr, "Error: Cannot open '%s'\n", in_rodf);
        return 1;
    }

    fseek(f, 0, SEEK_END);
    long file_sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *data = (uint8_t *)malloc((size_t)file_sz);
    if (!data || fread(data, 1, (size_t)file_sz, f) != (size_t)file_sz) {
        free(data);
        fclose(f);
        fprintf(stderr, "Error: Failed reading '%s'\n", in_rodf);
        return 1;
    }
    fclose(f);

    size_t offset = 0;
    int game_idx = 0;

    while (offset < (size_t)file_sz && game_idx < limit) {
        rodf_game_t game;
        size_t consumed = rodf_decode_game(data + offset, (size_t)file_sz - offset, &game);
        if (consumed == 0) break;

        printf("\n==================================================\n");
        printf("Game #%d | DFRC Setup: [W: %u, B: %u] | Plies: %u | Outcome: %s\n",
               game_idx + 1, game.header.white_dfrc_idx, game.header.black_dfrc_idx,
               game.header.ply_count,
               game.header.result == RODF_RESULT_WIN ? "1-0 (White Win)" :
               (game.header.result == RODF_RESULT_DRAW ? "1/2-1/2 (Draw)" : "0-1 (Black Win)"));

        rodf_board_t board;
        rodf_board_from_dfrc(&board, (uint16_t)game.header.white_dfrc_idx, (uint16_t)game.header.black_dfrc_idx);

        char fen[128];
        rodf_board_to_fen(&board, fen, sizeof(fen));
        printf("Start FEN: %s\n\n", fen);

        for (uint16_t p = 0; p < game.header.ply_count; p++) {
            char from_str[4], to_str[4];
            sq_to_uci((uint8_t)game.plies[p].move.from_sq, from_str);
            sq_to_uci((uint8_t)game.plies[p].move.to_sq, to_str);

            char promo_char = '\0';
            if (game.plies[p].move.move_type == RODF_MOVE_PROMO) {
                const char p_chars[] = "nbrq";
                promo_char = p_chars[game.plies[p].move.promo_type & 3];
            }

            if (p % 2 == 0) {
                printf("%3d. %s%s%c (%+d cp)", (p / 2) + 1, from_str, to_str, promo_char ? promo_char : ' ', game.plies[p].score);
            } else {
                printf("  ... %s%s%c (%+d cp)\n", from_str, to_str, promo_char ? promo_char : ' ', game.plies[p].score);
            }

            rodf_make_move(&board, game.plies[p].move);
        }
        if (game.header.ply_count % 2 != 0) printf("\n");

        offset += consumed;
        game_idx++;
    }

    free(data);
    return 0;
}

static int do_bench(const char *in_rodf) {
    FILE *f = fopen(in_rodf, "rb");
    if (!f) {
        fprintf(stderr, "Error: Cannot open '%s'\n", in_rodf);
        return 1;
    }

    fseek(f, 0, SEEK_END);
    long file_sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *data = (uint8_t *)malloc((size_t)file_sz);
    if (!data || fread(data, 1, (size_t)file_sz, f) != (size_t)file_sz) {
        free(data);
        fclose(f);
        fprintf(stderr, "Error: Failed reading '%s'\n", in_rodf);
        return 1;
    }
    fclose(f);

    printf("Starting RodentFormat Benchmark on '%s' (%.2f MB)...\n", in_rodf, (double)file_sz / (1024.0 * 1024.0));

    clock_t start = clock();
    size_t offset = 0;
    uint64_t total_games = 0;
    uint64_t total_positions = 0;
    uint64_t checksum = 0;

    rodf_simd_record_t simd_rec;

    while (offset < (size_t)file_sz) {
        rodf_game_t game;
        size_t consumed = rodf_decode_game(data + offset, (size_t)file_sz - offset, &game);
        if (consumed == 0) break;

        rodf_board_t board;
        rodf_board_from_dfrc(&board, (uint16_t)game.header.white_dfrc_idx, (uint16_t)game.header.black_dfrc_idx);

        for (uint16_t p = 0; p < game.header.ply_count; p++) {
            rodf_board_to_simd(&board, game.plies[p].score, (uint8_t)game.header.result, &simd_rec);
            checksum += (uint64_t)(simd_rec.white_occ ^ simd_rec.score_cp);
            rodf_make_move(&board, game.plies[p].move);
            total_positions++;
        }

        total_games++;
        offset += consumed;
    }

    free(data);

    double elapsed = (double)(clock() - start) / CLOCKS_PER_SEC;
    double pos_per_sec = elapsed > 0 ? ((double)total_positions / elapsed) : 0.0;
    double mb_per_sec = elapsed > 0 ? (((double)file_sz / (1024.0 * 1024.0)) / elapsed) : 0.0;

    printf("\n=== Benchmark Results ===\n");
    printf("Total Games Replayed    : %llu\n", (unsigned long long)total_games);
    printf("Total Positions Extracted: %llu\n", (unsigned long long)total_positions);
    printf("Elapsed Time            : %.3f seconds\n", elapsed);
    printf("Decoding & Replay Speed : %.2f Million pos/sec (%.0f pos/s)\n", pos_per_sec / 1e6, pos_per_sec);
    printf("File Throughput         : %.2f MB/sec\n", mb_per_sec);
    printf("Integrity Checksum      : 0x%llx\n", (unsigned long long)checksum);

    return 0;
}

static int do_unpack(const char *in_rodf, const char *out_rodfb) {
    FILE *fin = fopen(in_rodf, "rb");
    if (!fin) {
        fprintf(stderr, "Error: Cannot open '%s'\n", in_rodf);
        return 1;
    }

    FILE *fout = fopen(out_rodfb, "wb");
    if (!fout) {
        fclose(fin);
        fprintf(stderr, "Error: Cannot create '%s'\n", out_rodfb);
        return 1;
    }

    fseek(fin, 0, SEEK_END);
    long file_sz = ftell(fin);
    fseek(fin, 0, SEEK_SET);

    uint8_t *data = (uint8_t *)malloc((size_t)file_sz);
    if (!data || fread(data, 1, (size_t)file_sz, fin) != (size_t)file_sz) {
        free(data);
        fclose(fin);
        fclose(fout);
        fprintf(stderr, "Error: Failed reading '%s'\n", in_rodf);
        return 1;
    }
    fclose(fin);

    size_t offset = 0;
    uint64_t total_records = 0;
    rodf_simd_record_t rec;

    clock_t start = clock();

    while (offset < (size_t)file_sz) {
        rodf_game_t game;
        size_t consumed = rodf_decode_game(data + offset, (size_t)file_sz - offset, &game);
        if (consumed == 0) break;

        rodf_board_t board;
        rodf_board_from_dfrc(&board, (uint16_t)game.header.white_dfrc_idx, (uint16_t)game.header.black_dfrc_idx);

        for (uint16_t p = 0; p < game.header.ply_count; p++) {
            rodf_board_to_simd(&board, game.plies[p].score, (uint8_t)game.header.result, &rec);
            fwrite(&rec, 1, sizeof(rodf_simd_record_t), fout);
            rodf_make_move(&board, game.plies[p].move);
            total_records++;
        }

        offset += consumed;
    }

    free(data);
    fclose(fout);

    double elapsed = (double)(clock() - start) / CLOCKS_PER_SEC;
    printf("Unpacked %llu 16-byte SIMD records to '%s' in %.2fs (%.0f pos/sec)\n",
           (unsigned long long)total_records, out_rodfb, elapsed, elapsed > 0 ? ((double)total_records / elapsed) : 0.0);

    return 0;
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *cmd = argv[1];
    const char *input = NULL;
    const char *output = NULL;
    int limit = 5;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
            input = argv[++i];
        } else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            output = argv[++i];
        } else if (strcmp(argv[i], "--limit") == 0 && i + 1 < argc) {
            limit = atoi(argv[++i]);
        }
    }

    if (strcmp(cmd, "stats") == 0) {
        if (!input) {
            fprintf(stderr, "Error: --input required for stats\n");
            return 1;
        }
        return do_stats(input);
    } else if (strcmp(cmd, "convert") == 0) {
        if (!input || !output) {
            fprintf(stderr, "Error: --input and --output required for convert\n");
            return 1;
        }
        return do_convert(input, output);
    } else if (strcmp(cmd, "bench") == 0) {
        if (!input) {
            fprintf(stderr, "Error: --input required for bench\n");
            return 1;
        }
        return do_bench(input);
    } else if (strcmp(cmd, "unpack") == 0) {
        if (!input || !output) {
            fprintf(stderr, "Error: --input and --output required for unpack\n");
            return 1;
        }
        return do_unpack(input, output);
    } else if (strcmp(cmd, "dump") == 0) {
        if (!input) {
            fprintf(stderr, "Error: --input required for dump\n");
            return 1;
        }
        return do_dump(input, limit);
    } else {
        print_usage(argv[0]);
        return 1;
    }
}
