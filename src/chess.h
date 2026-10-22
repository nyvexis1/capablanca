// Capablanca - core board representation, bitboards and move generation.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

using Bitboard = uint64_t;
using Key = uint64_t;

enum Color : int { WHITE = 0, BLACK = 1 };
enum PieceType : int { PAWN = 0, KNIGHT, BISHOP, ROOK, QUEEN, KING, NO_PIECE_TYPE };
// Piece = color * 6 + type; EMPTY = 12
constexpr int EMPTY = 12;
constexpr int NO_SQ = 64;

inline Color operator~(Color c) { return Color(c ^ 1); }
inline int make_piece(Color c, int pt) { return c * 6 + pt; }
inline int type_of(int pc) { return pc % 6; }
inline Color color_of(int pc) { return Color(pc / 6); }

inline int file_of(int sq) { return sq & 7; }
inline int rank_of(int sq) { return sq >> 3; }
inline int relative_rank(Color c, int sq) { return c == WHITE ? rank_of(sq) : 7 - rank_of(sq); }
inline int make_sq(int f, int r) { return r * 8 + f; }

inline Bitboard bb(int sq) { return 1ULL << sq; }
inline int lsb(Bitboard b) { return __builtin_ctzll(b); }
inline int msb(Bitboard b) { return 63 - __builtin_clzll(b); }
inline int popcount(Bitboard b) { return __builtin_popcountll(b); }
inline int pop_lsb(Bitboard& b) { int s = lsb(b); b &= b - 1; return s; }
inline bool more_than_one(Bitboard b) { return b & (b - 1); }

constexpr Bitboard FILE_A = 0x0101010101010101ULL;
constexpr Bitboard FILE_H = FILE_A << 7;
constexpr Bitboard RANK_1 = 0xFFULL;
constexpr Bitboard RANK_8 = RANK_1 << 56;
inline Bitboard file_bb(int f) { return FILE_A << f; }
inline Bitboard rank_bb(int r) { return RANK_1 << (8 * r); }

inline Bitboard shift_north(Bitboard b) { return b << 8; }
inline Bitboard shift_south(Bitboard b) { return b >> 8; }
inline Bitboard shift_east(Bitboard b) { return (b & ~FILE_H) << 1; }
inline Bitboard shift_west(Bitboard b) { return (b & ~FILE_A) >> 1; }
inline Bitboard pawn_push(Color c, Bitboard b) { return c == WHITE ? shift_north(b) : shift_south(b); }
inline Bitboard pawn_attacks_bb(Color c, Bitboard b) {
    Bitboard f = pawn_push(c, b);
    return shift_east(f) | shift_west(f);
}

// ---- Attack tables (bitboard.cpp) ----
extern Bitboard KnightAttacks[64], KingAttacks[64], PawnAttacks[2][64];
extern Bitboard BetweenBB[64][64], LineBB[64][64];
extern int Distance[64][64];
void init_bitboards();
Bitboard rook_attacks(int sq, Bitboard occ);
Bitboard bishop_attacks(int sq, Bitboard occ);
inline Bitboard queen_attacks(int sq, Bitboard occ) { return rook_attacks(sq, occ) | bishop_attacks(sq, occ); }
Bitboard attacks_by_type(int pt, int sq, Bitboard occ);

// ---- Moves ----
// 16-bit move: from(6) | to(6) | flags(4)
using Move = uint16_t;
constexpr Move MOVE_NONE = 0;
enum MoveFlag : int {
    QUIET = 0, DOUBLE_PUSH = 1, KING_CASTLE = 2, QUEEN_CASTLE = 3, CAPTURE = 4, EP_CAPTURE = 5,
    PR_KNIGHT = 8, PR_BISHOP = 9, PR_ROOK = 10, PR_QUEEN = 11,
    PC_KNIGHT = 12, PC_BISHOP = 13, PC_ROOK = 14, PC_QUEEN = 15
};
inline Move make_move(int from, int to, int flag) { return Move(from | (to << 6) | (flag << 12)); }
inline int from_sq(Move m) { return m & 63; }
inline int to_sq(Move m) { return (m >> 6) & 63; }
inline int move_flag(Move m) { return m >> 12; }
inline bool is_capture(Move m) { return move_flag(m) & CAPTURE; }
inline bool is_promotion(Move m) { return move_flag(m) & 8; }
inline int promo_type(Move m) { return (move_flag(m) & 3) + KNIGHT; }
inline bool is_castle(Move m) { int f = move_flag(m); return f == KING_CASTLE || f == QUEEN_CASTLE; }
inline bool is_tactical(Move m) { return is_capture(m) || (is_promotion(m) && promo_type(m) == QUEEN); }

struct MoveList {
    Move moves[256];
    int scores[256];
    int size = 0;
    void add(Move m) { moves[size++] = m; }
};

// ---- Zobrist ----
namespace Zobrist {
extern Key psq[12][64], castling[16], ep[8], side;
void init();
}

// Castling rights bits
enum : int { WK = 1, WQ = 2, BK = 4, BQ = 8 };

// NNUE accumulator support (weights live in nnue.cpp). Positions keep their accumulators up to
// date in put_piece/remove_piece whenever a network is loaded.
#ifndef NNUE_HIDDEN
#define NNUE_HIDDEN 256 // build with -DNNUE_HIDDEN=512 for a wider net (the file must match)
#endif
constexpr int NNUE_H = NNUE_HIDDEN;
namespace Nnue {
extern const int16_t* W0;   // [768][NNUE_H], null when no network is loaded
extern const int16_t* B0;   // [NNUE_H]
extern uint32_t NetId;      // changes whenever a network is (re)loaded
inline int feature(Color persp, int pc, int sq) {
    return (((pc >= 6) != (persp == BLACK)) ? 384 : 0) + (pc % 6) * 64 + (persp == WHITE ? sq : sq ^ 56);
}
} // namespace Nnue

struct Position {
    Bitboard pieces[2][6];
    Bitboard occ[2];
    Bitboard all;
    uint8_t board[64];
    Color stm;
    int ep;          // en-passant target square or NO_SQ
    int castling;
    int halfmove;
    int fullmove;
    Key key;
    Key pawnKey;
    // NNUE accumulators, white's and black's perspective; valid when accNet == Nnue::NetId
    mutable uint32_t accNet = 0;
    alignas(32) mutable int16_t acc[2][NNUE_H];

    void set_fen(const std::string& fen);
    std::string fen() const;
    void clear();

    Bitboard pcs(Color c, int pt) const { return pieces[c][pt]; }
    Bitboard pcs(int pt) const { return pieces[WHITE][pt] | pieces[BLACK][pt]; }
    int king_sq(Color c) const { return lsb(pieces[c][KING]); }
    int piece_on(int sq) const { return board[sq]; }

    Bitboard attackers_to(int sq, Bitboard occupied) const;
    bool attacked(int sq, Color by) const;
    bool in_check() const { return attacked(king_sq(stm), ~stm); }

    // Applies move in place. Returns false if it leaves own king in check (position then invalid).
    bool make(Move m);
    void make_null();
    bool gives_check(Move m) const;
    bool is_pseudo_legal(Move m) const;
    bool non_pawn_material(Color c) const {
        return pieces[c][KNIGHT] | pieces[c][BISHOP] | pieces[c][ROOK] | pieces[c][QUEEN];
    }
    int see(Move m) const;
    bool see_ge(Move m, int threshold) const { return see(m) >= threshold; }
    int captured_type(Move m) const {
        if (move_flag(m) == EP_CAPTURE) return PAWN;
        int p = board[to_sq(m)];
        return p == EMPTY ? NO_PIECE_TYPE : type_of(p);
    }

    void put_piece(int pc, int sq);
    void remove_piece(int sq);
};

void generate_moves(const Position& pos, MoveList& list, bool tacticalOnly);
void generate_legal(const Position& pos, MoveList& list);
std::string move_to_uci(Move m);
Move parse_uci_move(const Position& pos, const std::string& s);
uint64_t perft(Position& pos, int depth);
void perft_divide(Position& pos, int depth);
