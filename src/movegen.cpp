// Capablanca - pseudo-legal move generation, perft, UCI move strings.
#include "chess.h"
#include <chrono>
#include <iostream>

static void add_promotions(MoveList& list, int from, int to, bool capture, bool tacticalOnly) {
    int base = capture ? PC_KNIGHT : PR_KNIGHT;
    list.add(make_move(from, to, base + 3)); // queen
    if (tacticalOnly) return;
    list.add(make_move(from, to, base + 0));
    list.add(make_move(from, to, base + 1));
    list.add(make_move(from, to, base + 2));
}

void generate_moves(const Position& pos, MoveList& list, bool tacticalOnly) {
    Color us = pos.stm, them = ~us;
    Bitboard own = pos.occ[us], enemy = pos.occ[them], empty = ~pos.all;
    Bitboard pawns = pos.pieces[us][PAWN];
    Bitboard promoRank = us == WHITE ? RANK_8 : RANK_1;
    Bitboard thirdRank = us == WHITE ? rank_bb(2) : rank_bb(5);
    int up = us == WHITE ? 8 : -8;

    // Pawn pushes
    Bitboard single = pawn_push(us, pawns) & empty;
    Bitboard dbl = pawn_push(us, single & thirdRank) & empty;
    Bitboard b = single & promoRank;
    while (b) { int to = pop_lsb(b); add_promotions(list, to - up, to, false, tacticalOnly); }
    if (!tacticalOnly) {
        b = single & ~promoRank;
        while (b) { int to = pop_lsb(b); list.add(make_move(to - up, to, QUIET)); }
        b = dbl;
        while (b) { int to = pop_lsb(b); list.add(make_move(to - 2 * up, to, DOUBLE_PUSH)); }
    }
    // Pawn captures
    Bitboard p = pawns;
    while (p) {
        int from = pop_lsb(p);
        Bitboard att = PawnAttacks[us][from] & enemy;
        while (att) {
            int to = pop_lsb(att);
            if (bb(to) & promoRank) add_promotions(list, from, to, true, tacticalOnly);
            else list.add(make_move(from, to, CAPTURE));
        }
        if (pos.ep != NO_SQ && (PawnAttacks[us][from] & bb(pos.ep)))
            list.add(make_move(from, pos.ep, EP_CAPTURE));
    }

    // Pieces
    for (int pt = KNIGHT; pt <= KING; pt++) {
        Bitboard pcs = pos.pieces[us][pt];
        while (pcs) {
            int from = pop_lsb(pcs);
            Bitboard att = attacks_by_type(pt, from, pos.all) & ~own;
            Bitboard caps = att & enemy;
            while (caps) list.add(make_move(from, pop_lsb(caps), CAPTURE));
            if (!tacticalOnly) {
                Bitboard quiets = att & empty;
                while (quiets) list.add(make_move(from, pop_lsb(quiets), QUIET));
            }
        }
    }

    // Castling
    if (!tacticalOnly && pos.castling) {
        if (us == WHITE) {
            if ((pos.castling & WK) && !(pos.all & (bb(5) | bb(6))) &&
                !pos.attacked(4, them) && !pos.attacked(5, them) && !pos.attacked(6, them))
                list.add(make_move(4, 6, KING_CASTLE));
            if ((pos.castling & WQ) && !(pos.all & (bb(1) | bb(2) | bb(3))) &&
                !pos.attacked(4, them) && !pos.attacked(3, them) && !pos.attacked(2, them))
                list.add(make_move(4, 2, QUEEN_CASTLE));
        } else {
            if ((pos.castling & BK) && !(pos.all & (bb(61) | bb(62))) &&
                !pos.attacked(60, them) && !pos.attacked(61, them) && !pos.attacked(62, them))
                list.add(make_move(60, 62, KING_CASTLE));
            if ((pos.castling & BQ) && !(pos.all & (bb(57) | bb(58) | bb(59))) &&
                !pos.attacked(60, them) && !pos.attacked(59, them) && !pos.attacked(58, them))
                list.add(make_move(60, 58, QUEEN_CASTLE));
        }
    }
}

void generate_legal(const Position& pos, MoveList& list) {
    MoveList pseudo;
    generate_moves(pos, pseudo, false);
    list.size = 0;
    for (int i = 0; i < pseudo.size; i++) {
        Position p = pos;
        if (p.make(pseudo.moves[i])) list.add(pseudo.moves[i]);
    }
}

std::string move_to_uci(Move m) {
    if (m == MOVE_NONE) return "0000";
    std::string s;
    s += char('a' + file_of(from_sq(m)));
    s += char('1' + rank_of(from_sq(m)));
    s += char('a' + file_of(to_sq(m)));
    s += char('1' + rank_of(to_sq(m)));
    if (is_promotion(m)) s += "nbrq"[promo_type(m) - KNIGHT];
    return s;
}

Move parse_uci_move(const Position& pos, const std::string& s) {
    MoveList list;
    generate_legal(pos, list);
    for (int i = 0; i < list.size; i++)
        if (move_to_uci(list.moves[i]) == s) return list.moves[i];
    return MOVE_NONE;
}

uint64_t perft(Position& pos, int depth) {
    MoveList list;
    generate_moves(pos, list, false);
    uint64_t nodes = 0;
    for (int i = 0; i < list.size; i++) {
        Position p = pos;
        if (!p.make(list.moves[i])) continue;
        nodes += depth == 1 ? 1 : perft(p, depth - 1);
    }
    return nodes;
}

void perft_divide(Position& pos, int depth) {
    auto t0 = std::chrono::steady_clock::now();
    MoveList list;
    generate_legal(pos, list);
    uint64_t total = 0;
    for (int i = 0; i < list.size; i++) {
        Position p = pos;
        p.make(list.moves[i]);
        uint64_t n = depth <= 1 ? 1 : perft(p, depth - 1);
        total += n;
        std::cout << move_to_uci(list.moves[i]) << ": " << n << "\n";
    }
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::cout << "\nNodes searched: " << total << "\n";
    std::cerr << "time " << ms << " ms, nps " << uint64_t(total / (ms / 1000.0 + 1e-9)) << "\n";
    std::cout.flush();
}
