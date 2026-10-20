// Capablanca - hand-crafted evaluation with a strong, asymmetric king-attack model.
#include "engine.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

StyleConfig Style;
Color EngineColor = WHITE;

namespace {

struct S {
    int mg = 0, eg = 0;
    S() = default;
    S(int m, int e) : mg(m), eg(e) {}
    S operator+(S o) const { return {mg + o.mg, eg + o.eg}; }
    S operator-(S o) const { return {mg - o.mg, eg - o.eg}; }
    S operator*(int k) const { return {mg * k, eg * k}; }
    S& operator+=(S o) { mg += o.mg; eg += o.eg; return *this; }
    S& operator-=(S o) { mg -= o.mg; eg -= o.eg; return *this; }
};

S PieceValue[6] = {{90, 115}, {330, 310}, {355, 330}, {480, 560}, {980, 1010}, {0, 0}};
const int PhaseWeight[6] = {0, 1, 1, 2, 4, 0};

S PST[6][64]; // from white's point of view

// Mobility: bonus per reachable square minus a baseline
S MobilityWeight[6] = {{0, 0}, {4, 4}, {5, 5}, {2, 4}, {1, 2}, {0, 0}};
int MobilityBase[6] = {0, 4, 6, 7, 14, 0};

// King attack weights per attacking piece
int AttackWeight[6] = {0, 25, 20, 30, 60, 0};

S PassedBonus[8] = {{0, 0}, {5, 10}, {8, 15}, {15, 28}, {30, 55}, {55, 95}, {90, 150}, {0, 0}};
// Passed-pawn conversion: safe next step and free path scale with advancement; rook behind is flat
S PassedSafeStop = {2, 6}, PassedFreePath = {4, 12}, RookBehindPassed = {8, 20};
S Doubled = {10, 20}, Isolated = {10, 12}, Phalanx = {6, 6}, Supported = {7, 5};
S BishopPair = {30, 50};
S RookOpen = {25, 10}, RookSemiOpen = {12, 8};
S KnightOutpost = {18, 8};
S ThreatByPawn = {45, 35}, Hanging = {25, 15};
S AdvancedPiece = {4, 0};
S Tempo = {18, 8};

// King-safety constants (tunable)
enum { KS_SHIELD2, KS_SHIELD3, KS_SHIELDNONE, KS_SEMIOPEN, KS_OPEN, KS_STORM, KS_ZONEHIT, KS_QCHECK, KS_RCHECK,
       KS_BCHECK, KS_NCHECK, KS_WEAK, KS_SHIELDMUL, KS_NOQUEEN, KS_KNIGHTDEF, KS_DANGERDIV, KS_EGDIV, KS_COUNT };
int KS[KS_COUNT] = {12, 22, 32, 10, 22, 8, 8, 75, 60, 40, 60, 18, 2, 180, 20, 560, 10};

int centre_dist(int sq) {
    int f = file_of(sq), r = rank_of(sq);
    int fd = f < 4 ? 3 - f : f - 4, rd = r < 4 ? 3 - r : r - 4;
    return std::max(fd, rd);
}

struct EvalInfo {
    Bitboard attacked[2] = {0, 0};
    Bitboard attacked2[2] = {0, 0};
    Bitboard attackedBy[2][6] = {};
    Bitboard kingZone[2] = {0, 0};
    int kingAttackers[2] = {0, 0};     // number of pieces of color c attacking enemy king zone
    int kingAttackWeight[2] = {0, 0};
    int kingZoneHits[2] = {0, 0};
};

inline void add_attacks(EvalInfo& ei, Color c, int pt, Bitboard att) {
    ei.attacked2[c] |= ei.attacked[c] & att;
    ei.attacked[c] |= att;
    ei.attackedBy[c][pt] |= att;
}

S eval_pawns(const Position& pos, const EvalInfo& ei, Color c) {
    S s;
    Color them = ~c;
    Bitboard own = pos.pieces[c][PAWN], enemy = pos.pieces[them][PAWN];
    Bitboard b = own;
    while (b) {
        int sq = pop_lsb(b);
        int f = file_of(sq), rr = relative_rank(c, sq);
        Bitboard adjFiles = (f > 0 ? file_bb(f - 1) : 0) | (f < 7 ? file_bb(f + 1) : 0);
        // squares in front of the pawn (towards promotion)
        Bitboard front = 0;
        for (int r = rank_of(sq) + (c == WHITE ? 1 : -1); r >= 0 && r <= 7; r += (c == WHITE ? 1 : -1))
            front |= rank_bb(r);
        if (!(own & adjFiles)) s -= Isolated;
        if (own & file_bb(f) & front) s -= Doubled;
        if (own & adjFiles & rank_bb(rank_of(sq))) s += Phalanx * std::max(1, rr - 1);
        if (PawnAttacks[them][sq] & own) s += Supported;
        bool passed = !(enemy & (file_bb(f) | adjFiles) & front);
        if (passed && !(own & file_bb(f) & front)) {
            S bonus = PassedBonus[rr];
            int stop = c == WHITE ? sq + 8 : sq - 8;
            if (pos.all & bb(stop)) bonus = S(bonus.mg / 2, bonus.eg / 2);
            s += bonus;
            // king proximity in the endgame
            int ownK = pos.king_sq(c), theirK = pos.king_sq(them);
            int w = std::max(0, rr - 2);
            s += S(0, (Distance[theirK][stop] * 5 - Distance[ownK][stop] * 2) * w);
            // Can it actually run? Squares the opponent controls and we don't are blocked.
            Bitboard path = file_bb(f) & front;
            Bitboard unsafe = pos.all | (ei.attacked[them] & ~ei.attacked[c]);
            if (!(unsafe & bb(stop))) s += PassedSafeStop * w;
            if (!(unsafe & path)) s += PassedFreePath * w;
            // A rook behind the passer pushes it home
            Bitboard behind = file_bb(f) & ~front & ~bb(sq);
            if (pos.pieces[c][ROOK] & behind) {
                int r = c == WHITE ? msb(pos.pieces[c][ROOK] & behind) : lsb(pos.pieces[c][ROOK] & behind);
                if (!(BetweenBB[r][sq] & pos.all)) s += RookBehindPassed;
            }
        }
    }
    return s;
}

// King safety of color c (penalty returned as positive S, to be subtracted from c), in percent
// of the neutral weight: 100 is neutral, the style settings change it.
S king_danger(const Position& pos, const EvalInfo& ei, Color c, int scale) {
    Color a = ~c;
    int k = pos.king_sq(c);
    S pen;

    // Pawn shield and storm (middlegame structure)
    int kf = std::clamp(file_of(k), 1, 6);
    int shield = 0;
    for (int f = kf - 1; f <= kf + 1; f++) {
        Bitboard ownOnFile = pos.pieces[c][PAWN] & file_bb(f);
        Bitboard enemyOnFile = pos.pieces[a][PAWN] & file_bb(f);
        // closest own pawn in front of king
        int best = 8;
        Bitboard t = ownOnFile;
        while (t) {
            int sq = pop_lsb(t);
            int d = relative_rank(c, sq) - relative_rank(c, k);
            if (d > 0) best = std::min(best, d);
        }
        if (best == 1) shield += 0;
        else if (best == 2) shield += KS[KS_SHIELD2];
        else if (best == 3) shield += KS[KS_SHIELD3];
        else shield += KS[KS_SHIELDNONE];
        if (!ownOnFile) shield += enemyOnFile ? KS[KS_SEMIOPEN] : KS[KS_OPEN]; // (semi-)open file towards our king
        // enemy pawn storm
        t = enemyOnFile;
        while (t) {
            int sq = pop_lsb(t);
            int d = relative_rank(c, sq) - relative_rank(c, k);
            if (d > 0 && d <= 3) shield += (4 - d) * KS[KS_STORM];
        }
    }
    pen.mg += shield;

    // Attack units
    int attackers = ei.kingAttackers[a];
    bool hasQueen = pos.pieces[a][QUEEN] != 0;
    Bitboard safe = ~pos.occ[a] & (~ei.attacked[c] | (ei.attacked2[a] & ~ei.attacked2[c] & ~ei.attackedBy[c][PAWN]));
    Bitboard rookRays = rook_attacks(k, pos.all), bishopRays = bishop_attacks(k, pos.all);
    int qChecks = popcount((rookRays | bishopRays) & ei.attackedBy[a][QUEEN] & safe);
    int rChecks = popcount(rookRays & ei.attackedBy[a][ROOK] & safe);
    int bChecks = popcount(bishopRays & ei.attackedBy[a][BISHOP] & safe);
    int nChecks = popcount(KnightAttacks[k] & ei.attackedBy[a][KNIGHT] & safe);
    // undefended squares around the king attacked by the enemy
    Bitboard weak = KingAttacks[k] & ei.attacked[a] & ~ei.attacked2[c];

    int units = ei.kingAttackWeight[a] * attackers / 2
              + ei.kingZoneHits[a] * KS[KS_ZONEHIT]
              + std::min(qChecks, 2) * KS[KS_QCHECK] + std::min(rChecks, 2) * KS[KS_RCHECK]
              + std::min(bChecks, 2) * KS[KS_BCHECK] + std::min(nChecks, 2) * KS[KS_NCHECK]
              + popcount(weak) * KS[KS_WEAK]
              + shield * KS[KS_SHIELDMUL]
              - (hasQueen ? 0 : KS[KS_NOQUEEN])
              - popcount(ei.attackedBy[c][KNIGHT] & KingAttacks[k]) * KS[KS_KNIGHTDEF];
    units = std::clamp(units, 0, 900);
    int danger;
    if (attackers >= 2 || qChecks) danger = units * units / std::max(100, KS[KS_DANGERDIV]);
    else danger = units / 4;
    pen += S(danger, units / std::max(1, KS[KS_EGDIV]));

    pen.mg = pen.mg * scale / 100;
    pen.eg = pen.eg * scale / 100;
    return pen;
}

S eval_side(const Position& pos, EvalInfo& ei, Color c, int& phase) {
    S s;
    Color them = ~c;
    Bitboard mobilityArea = ~(pos.pieces[c][PAWN] | pos.pieces[c][KING] | ei.attackedBy[them][PAWN]);
    Bitboard enemyZone = ei.kingZone[them];
    Bitboard enemyHalf = c == WHITE ? 0xFFFFFFFF00000000ULL : 0x00000000FFFFFFFFULL;

    for (int pt = PAWN; pt <= KING; pt++) {
        Bitboard b = pos.pieces[c][pt];
        while (b) {
            int sq = pop_lsb(b);
            int psq = c == WHITE ? sq : sq ^ 56;
            s += PieceValue[pt] + PST[pt][psq];
            phase += PhaseWeight[pt];
            if (pt == PAWN || pt == KING) continue;

            // X-ray through own queens/rooks for sliders so batteries count as attackers
            Bitboard occ = pos.all;
            if (pt == BISHOP) occ ^= pos.pieces[c][QUEEN];
            if (pt == ROOK) occ ^= pos.pieces[c][QUEEN] | pos.pieces[c][ROOK];
            Bitboard att = attacks_by_type(pt, sq, occ);
            add_attacks(ei, c, pt, att);

            int mob = popcount(att & mobilityArea);
            s += MobilityWeight[pt] * (mob - MobilityBase[pt]);

            if (att & enemyZone) {
                ei.kingAttackers[c]++;
                ei.kingAttackWeight[c] += AttackWeight[pt];
                ei.kingZoneHits[c] += popcount(att & enemyZone);
            }
            if (bb(sq) & enemyHalf) s += AdvancedPiece;

            if (pt == ROOK) {
                Bitboard fileB = file_bb(file_of(sq));
                if (!(pos.pieces[c][PAWN] & fileB))
                    s += (pos.pieces[them][PAWN] & fileB) ? RookSemiOpen : RookOpen;
            }
            if (pt == KNIGHT || pt == BISHOP) {
                int rr = relative_rank(c, sq);
                if (rr >= 3 && rr <= 5 && (PawnAttacks[them][sq] & pos.pieces[c][PAWN]))
                    s += pt == KNIGHT ? KnightOutpost : S(KnightOutpost.mg / 2, KnightOutpost.eg / 2);
            }
        }
    }
    if (more_than_one(pos.pieces[c][BISHOP])) s += BishopPair;
    return s;
}

S threats(const Position& pos, const EvalInfo& ei, Color c) {
    // penalties against c's pieces
    S s;
    Color them = ~c;
    Bitboard nonPawn = pos.occ[c] & ~pos.pieces[c][PAWN] & ~pos.pieces[c][KING];
    s -= ThreatByPawn * popcount(nonPawn & ei.attackedBy[them][PAWN]);
    Bitboard hanging = pos.occ[c] & ~pos.pieces[c][KING] & ei.attacked[them] & ~ei.attacked[c];
    s -= Hanging * popcount(hanging);
    return s;
}

} // namespace

void init_eval() {
    for (int sq = 0; sq < 64; sq++) {
        int f = file_of(sq), r = rank_of(sq), cd = centre_dist(sq);
        // Pawns: advance, especially in the centre; huge in the endgame
        const int pawnRankMg[8] = {0, 0, 2, 5, 10, 18, 30, 0};
        const int pawnRankEg[8] = {0, 0, 4, 10, 20, 38, 65, 0};
        int centreFile = (f == 3 || f == 4) ? 1 : (f == 2 || f == 5) ? 0 : -1;
        PST[PAWN][sq] = S(pawnRankMg[r] + (r >= 2 && r <= 4 ? centreFile * 10 : 0), pawnRankEg[r]);
        if (r == 0 || r == 7) PST[PAWN][sq] = S(0, 0);
        PST[KNIGHT][sq] = S(18 - 9 * cd - (r == 0 ? 8 : 0), 14 - 8 * cd);
        PST[BISHOP][sq] = S(8 - 4 * cd - (r == 0 ? 6 : 0), 8 - 4 * cd);
        PST[ROOK][sq] = S((r == 6 ? 15 : 0) + (f == 3 || f == 4 ? 4 : 0), r == 6 ? 18 : 0);
        PST[QUEEN][sq] = S(-2 * cd, 12 - 5 * cd);
        const int kingBack[8] = {15, 28, 8, -10, -5, 8, 30, 18};
        int kmg = r == 0 ? kingBack[f] : r == 1 ? kingBack[f] / 2 - 18 : -40 - 10 * r;
        PST[KING][sq] = S(kmg, 30 - 12 * cd);
    }
#if __has_include("tuned.inc")
    struct TunedEntry { const char* name; std::vector<int> v; };
    static const TunedEntry Tuned[] = {
#include "tuned.inc"
    };
    for (const TuneParam& p : tune_params())
        for (const TunedEntry& t : Tuned)
            if (std::string(t.name) == p.name && int(t.v.size()) == p.count)
                for (int i = 0; i < p.count; i++) p.values[i] = t.v[i];
#endif
}

bool insufficient_material(const Position& pos) {
    if (pos.pcs(PAWN) | pos.pcs(ROOK) | pos.pcs(QUEEN)) return false;
    int w = popcount(pos.occ[WHITE]) - 1, b = popcount(pos.occ[BLACK]) - 1;
    return (w == 0 && b <= 1) || (b == 0 && w <= 1);
}

// Style weight (percent) for the king of color c: attacking the opponent's king uses aggression,
// our own king uses defense.
int style_scale(Color c) { return c == EngineColor ? Style.defense : Style.aggression; }

void init_attacks(const Position& pos, EvalInfo& ei) {
    for (Color c : {WHITE, BLACK}) {
        int k = pos.king_sq(c);
        ei.kingZone[c] = KingAttacks[k] | bb(k);
        ei.kingZone[c] |= pawn_push(c, ei.kingZone[c]);
        Bitboard pushed = pawn_push(c, pos.pieces[c][PAWN]);
        add_attacks(ei, c, PAWN, shift_east(pushed));
        add_attacks(ei, c, PAWN, shift_west(pushed));
        add_attacks(ei, c, KING, KingAttacks[k]);
    }
}

// The style's extra king-safety weight on top of a neutral evaluation (used with NNUE):
// danger at (scale - 100)% for each king. Side to move's point of view.
int style_delta(const Position& pos) {
    if (Style.aggression == 100 && Style.defense == 100) return 0;
    EvalInfo ei;
    init_attacks(pos, ei);
    int phase = 0;
    for (Color c : {WHITE, BLACK}) eval_side(pos, ei, c, phase);
    S d = king_danger(pos, ei, BLACK, style_scale(BLACK) - 100) - king_danger(pos, ei, WHITE, style_scale(WHITE) - 100);
    phase = std::min(phase, 24);
    int score = (d.mg * phase + d.eg * (24 - phase)) / 24;
    return pos.stm == WHITE ? score : -score;
}

static int evaluate_impl(const Position& pos, bool trace) {
    EvalInfo ei;
    init_attacks(pos, ei);
    int phase = 0;
    S side[2];
    for (Color c : {WHITE, BLACK}) side[c] = eval_side(pos, ei, c, phase);
    S pawns[2] = {eval_pawns(pos, ei, WHITE), eval_pawns(pos, ei, BLACK)};
    S king[2] = {king_danger(pos, ei, WHITE, style_scale(WHITE)), king_danger(pos, ei, BLACK, style_scale(BLACK))};
    S threat[2] = {threats(pos, ei, WHITE), threats(pos, ei, BLACK)};

    S total = side[WHITE] - side[BLACK] + pawns[WHITE] - pawns[BLACK]
            - king[WHITE] + king[BLACK] + threat[WHITE] - threat[BLACK];
    total += pos.stm == WHITE ? Tempo : S(-Tempo.mg, -Tempo.eg);

    phase = std::min(phase, 24);
    int score = (total.mg * phase + total.eg * (24 - phase)) / 24;

    // Drawish endgames: the stronger side has no pawns and little extra material
    Color strong = score > 0 ? WHITE : BLACK;
    if (!pos.pieces[strong][PAWN]) {
        int matStrong = 0, matWeak = 0;
        for (int pt = KNIGHT; pt <= QUEEN; pt++) {
            matStrong += popcount(pos.pieces[strong][pt]) * PieceValue[pt].eg;
            matWeak += popcount(pos.pieces[~strong][pt]) * PieceValue[pt].eg;
        }
        if (matStrong - matWeak < 400) score /= (matStrong <= 330 ? 16 : 4);
    }
    // Fifty-move rule approach shrinks the score
    score = score * (200 - pos.halfmove) / 200;

    if (trace) {
        auto pr = [](const char* n, S w, S b) {
            std::cout << n << "  white " << w.mg << "/" << w.eg << "  black " << b.mg << "/" << b.eg << "\n";
        };
        pr("material+pst+mob", side[WHITE], side[BLACK]);
        pr("pawns           ", pawns[WHITE], pawns[BLACK]);
        pr("king danger     ", king[WHITE], king[BLACK]);
        pr("threats         ", threat[WHITE], threat[BLACK]);
        std::cout << "phase " << phase << "  final (white pov) " << score << "\n";
    }
    return pos.stm == WHITE ? score : -score;
}

bool UseNNUE = true;

int evaluate(const Position& pos) {
    if (UseNNUE && nnue_ready()) {
        int score = nnue_evaluate(pos) + style_delta(pos);
        return score * (200 - pos.halfmove) / 200;
    }
    return evaluate_impl(pos, false);
}

std::vector<TuneParam> tune_params() {
    auto sArr = [](const char* n, S* p, int count) { return TuneParam{n, &p->mg, count * 2, true}; };
    return {
        sArr("PieceValue", PieceValue, 5),
        TuneParam{"PST", &PST[0][0].mg, 6 * 64 * 2, true},
        sArr("MobilityWeight", MobilityWeight, 6),
        TuneParam{"AttackWeight", AttackWeight, 6, false},
        sArr("PassedBonus", PassedBonus, 8),
        sArr("PassedSafeStop", &PassedSafeStop, 1), sArr("PassedFreePath", &PassedFreePath, 1),
        sArr("RookBehindPassed", &RookBehindPassed, 1),
        sArr("Doubled", &Doubled, 1), sArr("Isolated", &Isolated, 1), sArr("Phalanx", &Phalanx, 1),
        sArr("Supported", &Supported, 1), sArr("BishopPair", &BishopPair, 1), sArr("RookOpen", &RookOpen, 1),
        sArr("RookSemiOpen", &RookSemiOpen, 1), sArr("KnightOutpost", &KnightOutpost, 1),
        sArr("ThreatByPawn", &ThreatByPawn, 1), sArr("Hanging", &Hanging, 1),
        sArr("AdvancedPiece", &AdvancedPiece, 1), sArr("Tempo", &Tempo, 1),
        TuneParam{"KS", KS, KS_COUNT, false},
    };
}
void eval_trace(const Position& pos) { evaluate_impl(pos, true); }
