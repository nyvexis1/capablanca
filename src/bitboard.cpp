// Capablanca - attack tables and magic bitboards (magics generated at startup).
#include "chess.h"
#include <cstdlib>

Bitboard KnightAttacks[64], KingAttacks[64], PawnAttacks[2][64];
Bitboard BetweenBB[64][64], LineBB[64][64];
int Distance[64][64];

namespace {

struct Magic {
    Bitboard mask;
    Bitboard magic;
    Bitboard* attacks;
    int shift;
    unsigned index(Bitboard occ) const { return unsigned(((occ & mask) * magic) >> shift); }
};

Magic RookMagics[64], BishopMagics[64];
Bitboard RookTable[0x19000];
Bitboard BishopTable[0x1480];

Bitboard slide(int sq, Bitboard occ, const int dirs[4][2]) {
    Bitboard result = 0;
    for (int d = 0; d < 4; d++) {
        int f = file_of(sq), r = rank_of(sq);
        while (true) {
            f += dirs[d][0];
            r += dirs[d][1];
            if (f < 0 || f > 7 || r < 0 || r > 7) break;
            int s = make_sq(f, r);
            result |= bb(s);
            if (occ & bb(s)) break;
        }
    }
    return result;
}

const int RookDirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
const int BishopDirs[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

uint64_t rngState = 0x9E3779B97F4A7C15ULL;
uint64_t rand64() {
    rngState ^= rngState >> 12;
    rngState ^= rngState << 25;
    rngState ^= rngState >> 27;
    return rngState * 2685821657736338717ULL;
}

void init_magics(Magic* magics, Bitboard* table, const int dirs[4][2]) {
    Bitboard occupancy[4096], reference[4096];
    int epoch[4096] = {0};
    int attempt = 0;
    Bitboard* base = table;

    for (int sq = 0; sq < 64; sq++) {
        Bitboard edges = ((RANK_1 | RANK_8) & ~rank_bb(rank_of(sq))) |
                         ((FILE_A | FILE_H) & ~file_bb(file_of(sq)));
        Magic& m = magics[sq];
        m.mask = slide(sq, 0, dirs) & ~edges;
        int bits = popcount(m.mask);
        m.shift = 64 - bits;
        m.attacks = base;

        // Enumerate all subsets of the mask (Carry-Rippler)
        int size = 0;
        Bitboard b = 0;
        do {
            occupancy[size] = b;
            reference[size] = slide(sq, b, dirs);
            size++;
            b = (b - m.mask) & m.mask;
        } while (b);

        while (true) {
            m.magic = rand64() & rand64() & rand64();
            if (popcount((m.mask * m.magic) >> 56) < 6) continue;
            attempt++;
            bool ok = true;
            for (int i = 0; i < size; i++) {
                unsigned idx = m.index(occupancy[i]);
                if (epoch[idx] < attempt) {
                    epoch[idx] = attempt;
                    m.attacks[idx] = reference[i];
                } else if (m.attacks[idx] != reference[i]) {
                    ok = false;
                    break;
                }
            }
            if (ok) break;
        }
        base += size;
    }
}

} // namespace

Bitboard rook_attacks(int sq, Bitboard occ) {
    const Magic& m = RookMagics[sq];
    return m.attacks[m.index(occ)];
}

Bitboard bishop_attacks(int sq, Bitboard occ) {
    const Magic& m = BishopMagics[sq];
    return m.attacks[m.index(occ)];
}

Bitboard attacks_by_type(int pt, int sq, Bitboard occ) {
    switch (pt) {
    case KNIGHT: return KnightAttacks[sq];
    case BISHOP: return bishop_attacks(sq, occ);
    case ROOK: return rook_attacks(sq, occ);
    case QUEEN: return queen_attacks(sq, occ);
    case KING: return KingAttacks[sq];
    default: return 0;
    }
}

void init_bitboards() {
    for (int sq = 0; sq < 64; sq++) {
        int f = file_of(sq), r = rank_of(sq);
        const int kn[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
        const int kg[8][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
        KnightAttacks[sq] = KingAttacks[sq] = 0;
        for (int i = 0; i < 8; i++) {
            int nf = f + kn[i][0], nr = r + kn[i][1];
            if (nf >= 0 && nf < 8 && nr >= 0 && nr < 8) KnightAttacks[sq] |= bb(make_sq(nf, nr));
            nf = f + kg[i][0];
            nr = r + kg[i][1];
            if (nf >= 0 && nf < 8 && nr >= 0 && nr < 8) KingAttacks[sq] |= bb(make_sq(nf, nr));
        }
        PawnAttacks[WHITE][sq] = pawn_attacks_bb(WHITE, bb(sq));
        PawnAttacks[BLACK][sq] = pawn_attacks_bb(BLACK, bb(sq));
    }
    for (int a = 0; a < 64; a++)
        for (int b = 0; b < 64; b++) {
            int df = abs(file_of(a) - file_of(b)), dr = abs(rank_of(a) - rank_of(b));
            Distance[a][b] = df > dr ? df : dr;
        }

    init_magics(RookMagics, RookTable, RookDirs);
    init_magics(BishopMagics, BishopTable, BishopDirs);

    for (int a = 0; a < 64; a++)
        for (int b = 0; b < 64; b++) {
            BetweenBB[a][b] = LineBB[a][b] = 0;
            if (a == b) continue;
            if (rook_attacks(a, 0) & bb(b)) {
                LineBB[a][b] = (rook_attacks(a, 0) & rook_attacks(b, 0)) | bb(a) | bb(b);
                BetweenBB[a][b] = rook_attacks(a, bb(b)) & rook_attacks(b, bb(a));
            } else if (bishop_attacks(a, 0) & bb(b)) {
                LineBB[a][b] = (bishop_attacks(a, 0) & bishop_attacks(b, 0)) | bb(a) | bb(b);
                BetweenBB[a][b] = bishop_attacks(a, bb(b)) & bishop_attacks(b, bb(a));
            }
        }
}
