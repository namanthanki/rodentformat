#ifndef RODENTFORMAT_H
#define RODENTFORMAT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Constants */

#define RODF_VERSION_MAJOR 0
#define RODF_VERSION_MINOR 1
#define RODF_VERSION_PATCH 0

#define RODF_MAX_PLIES     1024
#define RODF_MAX_PIECES    32

/* Piece Types */
typedef enum {
    RODF_PAWN   = 0,
    RODF_KNIGHT = 1,
    RODF_BISHOP = 2,
    RODF_ROOK   = 3,
    RODF_QUEEN  = 4,
    RODF_KING   = 5,
    RODF_NONE   = 6
} rodf_piece_type_t;

/* Colors */
typedef enum {
    RODF_WHITE = 0,
    RODF_BLACK = 1
} rodf_color_t;

/* Game Outcomes */
typedef enum {
    RODF_RESULT_LOSS = 0, /* White Loss / Black Win */
    RODF_RESULT_DRAW = 1, /* Draw */
    RODF_RESULT_WIN  = 2  /* White Win / Black Loss */
} rodf_result_t;

/* Move Types */
typedef enum {
    RODF_MOVE_NORMAL = 0,
    RODF_MOVE_ENPASSANT = 1,
    RODF_MOVE_CASTLE = 2,
    RODF_MOVE_PROMO = 3
} rodf_move_type_t;

/* Packed Move (16-bit) */
typedef struct {
    uint16_t from_sq    : 6; /* 0..63 */
    uint16_t to_sq      : 6; /* 0..63 */
    uint16_t promo_type : 2; /* 0=N, 1=B, 2=R, 3=Q */
    uint16_t move_type  : 2; /* rodf_move_type_t */
} rodf_move_t;

/* Move + Score Entry (4 bytes) */
typedef struct {
    rodf_move_t move;
    int16_t     score; /* Centipawns (White-relative) */
} rodf_ply_t;

/* Move + Score Entry (4 bytes) */
typedef struct {
    uint32_t white_dfrc_idx: 10; /* 0..959 */
    uint32_t black_dfrc_idx: 10; /* 0..959 */
    uint32_t stm           : 1;  /* 0=White, 1=Black */
    uint32_t result        : 2;  /* rodf_result_t */
    uint32_t ply_count     : 10; /* 0..1023 */
    uint32_t reserved      : 31; /* Reserved extensions */
} rodf_game_header_t;

/* Game Representation */
typedef struct {
    rodf_game_header_t header;
    rodf_ply_t         plies[RODF_MAX_PLIES];
} rodf_game_t;

/* Board Representation */
typedef struct {
    uint64_t piece_bb[6];      /* Pawns, Knights, Bishops, Rooks, Queens, Kings */
    uint64_t color_bb[2];      /* White, Black */
    uint8_t  pieces[64];       /* Piece types on each square, or RODF_NONE */
    uint8_t  colors[64];       /* Color of piece on square (0=White, 1=Black) */
    
    uint8_t  stm;              /* Side to move */
    uint8_t  ep_sq;            /* En-passant square or 64 if none */
    uint8_t  castling_rights;  /* Bit 0: W-Kingside, Bit 1: W-Queenside, Bit 2: B-Kingside, Bit 3: B-Queenside */
    uint8_t  halfmove_clock;
    uint16_t fullmove_number;
    
    /* DFRC Castling Rookie Squares */
    uint8_t  castling_rook_sq[2][2]; /* [Color][0=Kingside/H-rook, 1=Queenside/A-rook] */
    uint16_t white_dfrc_idx;
    uint16_t black_dfrc_idx;
} rodf_board_t;

/* 16-Byte Direct Training SIMD Record */
#pragma pack(push, 1)
typedef struct {
    uint32_t white_occ;  /* 32-bit packed lower/upper occupancy */
    uint32_t black_occ;  /* 32-bit packed lower/upper occupancy */
    uint8_t  piece_data[4]; /* Nibble-packed piece types */
    int16_t  score_cp;   /* Centipawn score */
    uint8_t  wdl_u8;     /* 0=Loss, 128=Draw, 255=Win */
    uint8_t  flags;      /* Bit 0: stm (0=White, 1=Black), Bits 1..6: STM King Square (0..63), Bit 7: Reserved */
} rodf_simd_record_t;
#pragma pack(pop)

/**
 * @brief Initialize a board from White & Black DFRC / Chess960 indices (0..959).
 */
bool rodf_board_from_dfrc(rodf_board_t *board, uint16_t white_idx, uint16_t black_idx);

/**
 * @brief Convert starting 1st rank pieces back to FRC 960 index (0..959).
 */
uint16_t rodf_rank1_to_frc(const uint8_t rank1[8]);

/**
 * @brief Convert standard FEN string to rodf_board_t.
 */
bool rodf_board_from_fen(rodf_board_t *board, const char *fen);

/**
 * @brief Export rodf_board_t to FEN string.
 */
bool rodf_board_to_fen(const rodf_board_t *board, char *fen_buf, size_t buf_size);

/**
 * @brief Apply a rodf_move_t to the board state.
 */
bool rodf_make_move(rodf_board_t *board, rodf_move_t move);

#ifdef __cplusplus
}
#endif

#endif /* RODENTFORMAT_H */