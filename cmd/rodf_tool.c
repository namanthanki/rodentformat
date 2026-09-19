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

#define CHUNK_SIZE (16 * 1024 * 1024) /* 16 MB streaming chunk buffer */

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

    uint8_t *chunk = (uint8_t *)malloc(CHUNK_SIZE);
    if (!chunk) {
        free(dfrc_seen);
        fclose(f);
        return 1;
    }

    size_t in_buf_len = 0;

    while (1) {
        size_t to_read = CHUNK_SIZE - in_buf_len;
        size_t nread = fread(chunk + in_buf_len, 1, to_read, f);
        in_buf_len += nread;

        if (in_buf_len == 0) break;

        size_t offset = 0;
        while (offset + 8 <= in_buf_len) {
            uint64_t hdr;
            memcpy(&hdr, chunk + offset, 8);

            uint16_t w_idx = (uint16_t)(hdr & 0x3FF);
            uint16_t b_idx = (uint16_t)((hdr >> 10) & 0x3FF);
            uint8_t res = (uint8_t)((hdr >> 21) & 3);
            uint16_t plies = (uint16_t)((hdr >> 23) & 0x3FF);

            size_t game_size = 8 + (size_t)plies * 4;
            if (offset + game_size > in_buf_len) {
                if (nread > 0) {
                    break; /* Fetch more data */
                } else {
                    offset = in_buf_len;
                    break;
                }
            }

            total_games++;
            total_plies += plies;

            if (res == RODF_RESULT_WIN) wins++;
            else if (res == RODF_RESULT_DRAW) draws++;
            else losses++;

            if (w_idx < 960 && b_idx < 960 && !dfrc_seen[w_idx][b_idx]) {
                dfrc_seen[w_idx][b_idx] = 1;
                dfrc_unique_setups++;
            }

            offset += game_size;
        }

        size_t remaining = in_buf_len - offset;
        if (remaining > 0 && offset > 0) {
            memmove(chunk, chunk + offset, remaining);
        }
        in_buf_len = remaining;

        if (nread == 0) break;
    }

    free(chunk);
    free(dfrc_seen);
    fclose(f);

    printf("=== RodentFormat Dataset Stats: %s ===\n", input_path);
    printf("Total Games        : %llu\n", (unsigned long long)total_games);
    printf("Total Positions    : %llu\n", (unsigned long long)total_plies);
    printf("Avg Plies/Game     : %.2f\n", total_games > 0 ? ((double)total_plies / total_games) : 0.0);
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

    /* 2 MB file buffers for fast I/O throughput */
    setvbuf(fin, NULL, _IOFBF, 2 * 1024 * 1024);
    setvbuf(fout, NULL, _IOFBF, 2 * 1024 * 1024);

    uint8_t *chunk = (uint8_t *)malloc(CHUNK_SIZE);
    if (!chunk) {
        fprintf(stderr, "Error: Memory allocation failed for streaming buffer\n");
        fclose(fin);
        fclose(fout);
        return 1;
    }

    uint8_t out_buf[8 + RODF_MAX_PLIES * 4];
    size_t in_buf_len = 0;
    uint64_t total_games = 0;
    uint64_t total_plies = 0;
    uint64_t total_bytes_read = 0;

    clock_t start = clock();
    clock_t last_report = start;

    printf("Streaming conversion: '%s' -> '%s'...\n", in_vf, out_rodf);

    while (1) {
        size_t to_read = CHUNK_SIZE - in_buf_len;
        size_t nread = fread(chunk + in_buf_len, 1, to_read, fin);
        in_buf_len += nread;
        total_bytes_read += nread;

        if (in_buf_len == 0) {
            break;
        }

        size_t offset = 0;
        while (offset < in_buf_len) {
            /* If near buffer end and more data remains, keep at least 16KB for max game length */
            if (in_buf_len - offset < 16384 && nread > 0) {
                break;
            }

            rodf_game_t game;
            size_t consumed = rodf_convert_from_viriformat(chunk + offset, in_buf_len - offset, &game);
            if (consumed == 0) {
                if (nread == 0) {
                    offset = in_buf_len;
                }
                break;
            }

            size_t enc_bytes = rodf_encode_game(&game, out_buf, sizeof(out_buf));
            if (enc_bytes > 0) {
                fwrite(out_buf, 1, enc_bytes, fout);
                total_games++;
                total_plies += game.header.ply_count;
            }

            offset += consumed;

            /* Progress report */
            if (total_plies % 500000 < 80) {
                clock_t now = clock();
                double elapsed = (double)(now - start) / CLOCKS_PER_SEC;
                if ((double)(now - last_report) / CLOCKS_PER_SEC >= 1.5) {
                    double mb_read = (double)total_bytes_read / (1024.0 * 1024.0);
                    double pos_sec = elapsed > 0 ? ((double)total_plies / elapsed) : 0.0;
                    printf("  Progress: %.1fM positions | %llu games | %.1f MB read | %.0f pos/s\n",
                           (double)total_plies / 1e6, (unsigned long long)total_games, mb_read, pos_sec);
                    last_report = now;
                }
            }
        }

        /* Move leftover bytes to front of buffer */
        size_t remaining = in_buf_len - offset;
        if (remaining > 0 && offset > 0) {
            memmove(chunk, chunk + offset, remaining);
        }
        in_buf_len = remaining;

        if (nread == 0 && (in_buf_len == 0 || remaining < 32)) {
            break;
        }
    }

    free(chunk);
    fclose(fin);
    fclose(fout);

    double total_time = (double)(clock() - start) / CLOCKS_PER_SEC;
    double final_speed = total_time > 0 ? ((double)total_plies / total_time) : 0.0;
    printf("\nFinished! Converted %llu games (%llu positions) in %.2fs (%.0f pos/sec)\n",
           (unsigned long long)total_games, (unsigned long long)total_plies, total_time, final_speed);

    return 0;
}

static int do_dump(const char *in_rodf, int limit) {
    FILE *f = fopen(in_rodf, "rb");
    if (!f) {
        fprintf(stderr, "Error: Cannot open '%s'\n", in_rodf);
        return 1;
    }
    setvbuf(f, NULL, _IOFBF, 2 * 1024 * 1024);

    int game_idx = 0;
    uint8_t header_buf[8];

    while (game_idx < limit && fread(header_buf, 1, 8, f) == 8) {
        uint64_t hdr;
        memcpy(&hdr, header_buf, 8);

        uint16_t w_idx = (uint16_t)(hdr & 0x3FF);
        uint16_t b_idx = (uint16_t)((hdr >> 10) & 0x3FF);
        uint8_t stm    = (uint8_t)((hdr >> 20) & 1);
        uint8_t res    = (uint8_t)((hdr >> 21) & 3);
        uint16_t plies = (uint16_t)((hdr >> 23) & 0x3FF);

        printf("\n==================================================\n");
        printf("Game #%d | DFRC Setup: [W: %u, B: %u] | Plies: %u | Outcome: %s\n",
               game_idx + 1, w_idx, b_idx, plies,
               res == RODF_RESULT_WIN ? "1-0 (White Win)" :
               (res == RODF_RESULT_DRAW ? "1/2-1/2 (Draw)" : "0-1 (Black Win)"));

        rodf_board_t board;
        rodf_board_from_dfrc(&board, w_idx, b_idx);
        board.stm = stm;

        char fen[128];
        rodf_board_to_fen(&board, fen, sizeof(fen));
        printf("Start FEN: %s\n\n", fen);

        for (uint16_t p = 0; p < plies; p++) {
            uint16_t packed_move;
            int16_t score;
            if (fread(&packed_move, 1, 2, f) != 2) break;
            if (fread(&score, 1, 2, f) != 2) break;

            rodf_move_t m;
            m.from_sq    = (uint16_t)(packed_move & 0x3F);
            m.to_sq      = (uint16_t)((packed_move >> 6) & 0x3F);
            m.promo_type = (uint16_t)((packed_move >> 12) & 0x03);
            m.move_type  = (uint16_t)((packed_move >> 14) & 0x03);

            char from_str[4], to_str[4];
            sq_to_uci((uint8_t)m.from_sq, from_str);
            sq_to_uci((uint8_t)m.to_sq, to_str);

            char promo_char = '\0';
            if (m.move_type == RODF_MOVE_PROMO) {
                const char p_chars[] = "nbrq";
                promo_char = p_chars[m.promo_type & 3];
            }

            if (p % 2 == 0) {
                printf("%3d. %s%s%c (%+d cp)", (p / 2) + 1, from_str, to_str, promo_char ? promo_char : ' ', score);
            } else {
                printf("  ... %s%s%c (%+d cp)\n", from_str, to_str, promo_char ? promo_char : ' ', score);
            }

            rodf_make_move(&board, m);
        }
        if (plies % 2 != 0) printf("\n");

        game_idx++;
    }

    fclose(f);
    return 0;
}

static int do_bench(const char *in_rodf) {
    FILE *f = fopen(in_rodf, "rb");
    if (!f) {
        fprintf(stderr, "Error: Cannot open '%s'\n", in_rodf);
        return 1;
    }
    setvbuf(f, NULL, _IOFBF, 2 * 1024 * 1024);

    printf("Starting RodentFormat Benchmark on '%s'...\n", in_rodf);

    clock_t start = clock();
    uint64_t total_games = 0;
    uint64_t total_positions = 0;
    uint64_t checksum = 0;

    rodf_simd_record_t simd_rec;
    uint8_t header_buf[8];

    while (fread(header_buf, 1, 8, f) == 8) {
        uint64_t hdr;
        memcpy(&hdr, header_buf, 8);

        uint16_t w_idx = (uint16_t)(hdr & 0x3FF);
        uint16_t b_idx = (uint16_t)((hdr >> 10) & 0x3FF);
        uint8_t stm    = (uint8_t)((hdr >> 20) & 1);
        uint8_t res    = (uint8_t)((hdr >> 21) & 3);
        uint16_t plies = (uint16_t)((hdr >> 23) & 0x3FF);

        rodf_board_t board;
        rodf_board_from_dfrc(&board, w_idx, b_idx);
        board.stm = stm;

        for (uint16_t p = 0; p < plies; p++) {
            uint16_t packed_move;
            int16_t score;
            if (fread(&packed_move, 1, 2, f) != 2) break;
            if (fread(&score, 1, 2, f) != 2) break;

            rodf_move_t m;
            m.from_sq    = (uint16_t)(packed_move & 0x3F);
            m.to_sq      = (uint16_t)((packed_move >> 6) & 0x3F);
            m.promo_type = (uint16_t)((packed_move >> 12) & 0x03);
            m.move_type  = (uint16_t)((packed_move >> 14) & 0x03);

            rodf_board_to_simd(&board, score, res, &simd_rec);
            checksum += (uint64_t)(simd_rec.white_occ ^ simd_rec.score_cp);

            rodf_make_move(&board, m);
            total_positions++;
        }

        total_games++;
    }

    fclose(f);

    double elapsed = (double)(clock() - start) / CLOCKS_PER_SEC;
    double pos_per_sec = elapsed > 0 ? ((double)total_positions / elapsed) : 0.0;

    printf("\n=== Benchmark Results ===\n");
    printf("Total Games Replayed    : %llu\n", (unsigned long long)total_games);
    printf("Total Positions Replayed: %llu\n", (unsigned long long)total_positions);
    printf("Elapsed Time            : %.3f seconds\n", elapsed);
    printf("Replay & SIMD Speed     : %.2f Million pos/sec (%.0f pos/s)\n", pos_per_sec / 1e6, pos_per_sec);
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

    setvbuf(fin, NULL, _IOFBF, 2 * 1024 * 1024);
    setvbuf(fout, NULL, _IOFBF, 2 * 1024 * 1024);

    uint64_t total_records = 0;
    rodf_simd_record_t rec;
    uint8_t header_buf[8];

    clock_t start = clock();
    clock_t last_report = start;

    while (fread(header_buf, 1, 8, fin) == 8) {
        uint64_t hdr;
        memcpy(&hdr, header_buf, 8);

        uint16_t w_idx = (uint16_t)(hdr & 0x3FF);
        uint16_t b_idx = (uint16_t)((hdr >> 10) & 0x3FF);
        uint8_t stm    = (uint8_t)((hdr >> 20) & 1);
        uint8_t res    = (uint8_t)((hdr >> 21) & 3);
        uint16_t plies = (uint16_t)((hdr >> 23) & 0x3FF);

        rodf_board_t board;
        rodf_board_from_dfrc(&board, w_idx, b_idx);
        board.stm = stm;

        for (uint16_t p = 0; p < plies; p++) {
            uint16_t packed_move;
            int16_t score;
            if (fread(&packed_move, 1, 2, fin) != 2) break;
            if (fread(&score, 1, 2, fin) != 2) break;

            rodf_move_t m;
            m.from_sq    = (uint16_t)(packed_move & 0x3F);
            m.to_sq      = (uint16_t)((packed_move >> 6) & 0x3F);
            m.promo_type = (uint16_t)((packed_move >> 12) & 0x03);
            m.move_type  = (uint16_t)((packed_move >> 14) & 0x03);

            rodf_board_to_simd(&board, score, res, &rec);
            fwrite(&rec, 1, sizeof(rodf_simd_record_t), fout);

            rodf_make_move(&board, m);
            total_records++;
        }

        if (total_records % 500000 < 80) {
            clock_t now = clock();
            if ((double)(now - last_report) / CLOCKS_PER_SEC >= 1.5) {
                double elapsed = (double)(now - start) / CLOCKS_PER_SEC;
                printf("  Unpacked: %.1fM records (%.0f pos/s)\n",
                       (double)total_records / 1e6, elapsed > 0 ? (total_records / elapsed) : 0.0);
                last_report = now;
            }
        }
    }

    fclose(fin);
    fclose(fout);

    double elapsed = (double)(clock() - start) / CLOCKS_PER_SEC;
    printf("\nFinished! Unpacked %llu 16-byte SIMD records to '%s' in %.2fs (%.0f pos/sec)\n",
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
