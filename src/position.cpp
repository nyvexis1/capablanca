// Capablanca - position state, FEN, make move, attack queries, SEE.
#include "chess.h"
#include <algorithm>
#include <cstring>
#include <sstream>

namespace Zobrist {
Key psq[12][64], castling[16], ep[8], side;
void init() {
    uint64_t s = 0x1234567890ABCDEFULL;
    auto next = [&]() {
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        return s * 2685821657736338717ULL;
    };
    for (auto& p : psq)
        for (auto& k : p) k = next();
    for (auto& k : castling) k = next();
    for (auto& k : ep) k = next();
    side = next();
}
} // namespace Zobrist

// Castling rights kept after a move touches a square
static int CastleMask[64];
static struct CastleInit {
    CastleInit() {
        for (int& m : CastleMask) m = 15;
        CastleMask[0] = 15 & ~WQ;  // a1
        CastleMask[7] = 15 & ~WK;  // h1
        CastleMask[4] = 15 & ~(WK | WQ);
        CastleMask[56] = 15 & ~BQ; // a8
        CastleMask[63] = 15 & ~BK; // h8
        CastleMask[60] = 15 & ~(BK | BQ);
    }
} castleInit;

void Position::clear() {
    memset(pieces, 0, sizeof(pieces));
    occ[0] = occ[1] = all = 0;
    for (auto& b : board) b = EMPTY;
    stm = WHITE;
    ep = NO_SQ;
    castling = 0;
    halfmove = 0;
    fullmove = 1;
    key = pawnKey = 0;
}

void Position::put_piece(int pc, int sq) {
    Color c = color_of(pc);
    int pt = type_of(pc);
    pieces[c][pt] |= bb(sq);
    occ[c] |= bb(sq);
    all |= bb(sq);
    board[sq] = pc;
    key ^= Zobrist::psq[pc][sq];
    if (pt == PAWN || pt == KING) pawnKey ^= Zobrist::psq[pc][sq];
}

void Position::remove_piece(int sq) {
    int pc = board[sq];
    Color c = color_of(pc);
    int pt = type_of(pc);
    pieces[c][pt] &= ~bb(sq);
    occ[c] &= ~bb(sq);
    all &= ~bb(sq);
    board[sq] = EMPTY;
    key ^= Zobrist::psq[pc][sq];
    if (pt == PAWN || pt == KING) pawnKey ^= Zobrist::psq[pc][sq];
}

void Position::set_fen(const std::string& fen) {
    clear();
    std::istringstream ss(fen);
    std::string placement, side, cast, eps;
    ss >> placement >> side >> cast >> eps;
    int r = 7, f = 0;
    const std::string chars = "PNBRQKpnbrqk";
    for (char ch : placement) {
        if (ch == '/') { r--; f = 0; }
        else if (ch >= '1' && ch <= '8') f += ch - '0';
        else {
            size_t idx = chars.find(ch);
            if (idx != std::string::npos) put_piece(int(idx), make_sq(f, r));
            f++;
        }
    }
    stm = side == "b" ? BLACK : WHITE;
    for (char ch : cast) {
        if (ch == 'K') castling |= WK;
        if (ch == 'Q') castling |= WQ;
        if (ch == 'k') castling |= BK;
        if (ch == 'q') castling |= BQ;
    }
    if (eps.size() == 2 && eps[0] >= 'a' && eps[0] <= 'h') {
        int sq = make_sq(eps[0] - 'a', eps[1] - '1');
        // Only keep ep if a capture is actually possible (keeps hashes consistent)
        if (PawnAttacks[~stm][sq] & pieces[stm][PAWN]) ep = sq;
    }
    if (!(ss >> halfmove)) halfmove = 0;
    if (!(ss >> fullmove)) fullmove = 1;
    if (stm == BLACK) key ^= Zobrist::side;
    key ^= Zobrist::castling[castling];
    if (ep != NO_SQ) key ^= Zobrist::ep[file_of(ep)];
}

std::string Position::fen() const {
    const std::string chars = "PNBRQKpnbrqk";
    std::string s;
    for (int r = 7; r >= 0; r--) {
        int empty = 0;
        for (int f = 0; f < 8; f++) {
            int pc = board[make_sq(f, r)];
            if (pc == EMPTY) { empty++; continue; }
            if (empty) { s += char('0' + empty); empty = 0; }
            s += chars[pc];
        }
        if (empty) s += char('0' + empty);
        if (r) s += '/';
    }
    s += stm == WHITE ? " w " : " b ";
    std::string c;
    if (castling & WK) c += 'K';
    if (castling & WQ) c += 'Q';
    if (castling & BK) c += 'k';
    if (castling & BQ) c += 'q';
    s += c.empty() ? "-" : c;
    s += ' ';
    if (ep == NO_SQ) s += '-';
    else { s += char('a' + file_of(ep)); s += char('1' + rank_of(ep)); }
    s += ' ' + std::to_string(halfmove) + ' ' + std::to_string(fullmove);
    return s;
}

Bitboard Position::attackers_to(int sq, Bitboard occupied) const {
    return (PawnAttacks[BLACK][sq] & pieces[WHITE][PAWN]) |
           (PawnAttacks[WHITE][sq] & pieces[BLACK][PAWN]) |
           (KnightAttacks[sq] & pcs(KNIGHT)) |
           (KingAttacks[sq] & pcs(KING)) |
           (bishop_attacks(sq, occupied) & (pcs(BISHOP) | pcs(QUEEN))) |
           (rook_attacks(sq, occupied) & (pcs(ROOK) | pcs(QUEEN)));
}

bool Position::attacked(int sq, Color by) const {
    if (PawnAttacks[~by][sq] & pieces[by][PAWN]) return true;
    if (KnightAttacks[sq] & pieces[by][KNIGHT]) return true;
    if (KingAttacks[sq] & pieces[by][KING]) return true;
    if (bishop_attacks(sq, all) & (pieces[by][BISHOP] | pieces[by][QUEEN])) return true;
    if (rook_attacks(sq, all) & (pieces[by][ROOK] | pieces[by][QUEEN])) return true;
    return false;
}

bool Position::make(Move m) {
    Color us = stm, them = ~stm;
    int from = from_sq(m), to = to_sq(m), flag = move_flag(m);
    int pc = board[from];
    int pt = type_of(pc);

    key ^= Zobrist::castling[castling];
    if (ep != NO_SQ) key ^= Zobrist::ep[file_of(ep)];
    ep = NO_SQ;
    halfmove++;

    if (flag == EP_CAPTURE) {
        remove_piece(us == WHITE ? to - 8 : to + 8);
        halfmove = 0;
    } else if (is_capture(m)) {
        remove_piece(to);
        halfmove = 0;
    }

    remove_piece(from);
    put_piece(is_promotion(m) ? make_piece(us, promo_type(m)) : pc, to);

    if (pt == PAWN) {
        halfmove = 0;
        if (flag == DOUBLE_PUSH) {
            int epSq = us == WHITE ? to - 8 : to + 8;
            if (PawnAttacks[us][epSq] & pieces[them][PAWN]) {
                ep = epSq;
                key ^= Zobrist::ep[file_of(ep)];
            }
        }
    } else if (flag == KING_CASTLE) {
        int rf = us == WHITE ? 7 : 63, rt = us == WHITE ? 5 : 61;
        remove_piece(rf);
        put_piece(make_piece(us, ROOK), rt);
    } else if (flag == QUEEN_CASTLE) {
        int rf = us == WHITE ? 0 : 56, rt = us == WHITE ? 3 : 59;
        remove_piece(rf);
        put_piece(make_piece(us, ROOK), rt);
    }

    castling &= CastleMask[from] & CastleMask[to];
    key ^= Zobrist::castling[castling];

    stm = them;
    key ^= Zobrist::side;
    if (us == BLACK) fullmove++;

    return !attacked(king_sq(us), them);
}

void Position::make_null() {
    if (ep != NO_SQ) key ^= Zobrist::ep[file_of(ep)];
    ep = NO_SQ;
    stm = ~stm;
    key ^= Zobrist::side;
    halfmove++;
}

bool Position::gives_check(Move m) const {
    // Simple and exact: play it on a copy.
    Position p = *this;
    p.make(m);
    return p.in_check();
}

bool Position::is_pseudo_legal(Move m) const {
    if (m == MOVE_NONE) return false;
    int from = from_sq(m), to = to_sq(m), flag = move_flag(m);
    int pc = board[from];
    if (pc == EMPTY || color_of(pc) != stm) return false;
    int pt = type_of(pc);
    // Fast path for ordinary piece moves; pawns and castling use the generator.
    if (pt != PAWN && flag <= CAPTURE && !is_castle(m) && flag != DOUBLE_PUSH) {
        if (!(attacks_by_type(pt, from, all) & bb(to))) return false;
        int target = board[to];
        if (flag == CAPTURE) return target != EMPTY && color_of(target) != stm;
        return target == EMPTY;
    }
    MoveList list;
    generate_moves(*this, list, false);
    for (int i = 0; i < list.size; i++)
        if (list.moves[i] == m) return true;
    return false;
}

static const int SeeValue[7] = {100, 320, 330, 500, 950, 20000, 0};

int Position::see(Move m) const {
    int from = from_sq(m), to = to_sq(m);
    if (is_castle(m)) return 0;
    int gain[34], d = 0;
    Bitboard occupied = all;
    int captured = captured_type(m);
    gain[0] = captured == NO_PIECE_TYPE ? 0 : SeeValue[captured];
    int attackerType = type_of(board[from]);
    if (is_promotion(m)) {
        gain[0] += SeeValue[promo_type(m)] - SeeValue[PAWN];
        attackerType = promo_type(m);
    }
    if (move_flag(m) == EP_CAPTURE) occupied ^= bb(stm == WHITE ? to - 8 : to + 8);
    occupied ^= bb(from);
    Bitboard attackers = attackers_to(to, occupied) & occupied;
    Color side = ~stm;
    Bitboard diag = pcs(BISHOP) | pcs(QUEEN), straight = pcs(ROOK) | pcs(QUEEN);

    while (true) {
        d++;
        gain[d] = SeeValue[attackerType] - gain[d - 1];
        Bitboard mine = attackers & occ[side];
        if (!mine) break;
        if (std::max(-gain[d - 1], gain[d]) < 0) break;
        int pt = PAWN;
        for (; pt <= KING; pt++)
            if (mine & pieces[side][pt]) break;
        // A king may not capture into a still-defended square
        if (pt == KING && (attackers & occ[~side])) break;
        int sq = lsb(mine & pieces[side][pt]);
        occupied ^= bb(sq);
        attackers |= (bishop_attacks(to, occupied) & diag) | (rook_attacks(to, occupied) & straight);
        attackers &= occupied;
        attackerType = pt;
        side = ~side;
        if (d >= 32) break;
    }
    while (--d) gain[d - 1] = -std::max(-gain[d - 1], gain[d]);
    return gain[0];
}
