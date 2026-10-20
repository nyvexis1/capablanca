// Capablanca - alpha-beta search with "sacrifice vision".
//
// Normal engines prune moves that lose material (bad SEE) and reduce quiet moves heavily.
// Capablanca treats moves that strike at the enemy king - checks and sacrifices into the
// king zone - as candidates worth seeing through: they are exempt from SEE/futility
// pruning, reduced less, and material-losing ones get an extension so the search can
// verify whether the attack actually wins.
#include "engine.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <thread>

namespace {

// ---------------- Transposition table ----------------
enum Bound : uint8_t { BOUND_NONE = 0, BOUND_UPPER = 1, BOUND_LOWER = 2, BOUND_EXACT = 3 };

struct TTEntry {
    Key key;
    int16_t score;
    int16_t eval;
    Move move;
    uint8_t depth;
    uint8_t genBound; // gen << 2 | bound
};
static_assert(sizeof(TTEntry) == 16);

std::vector<TTEntry> TT;
uint64_t ttMask = 0;
uint8_t ttGen = 0;

TTEntry* tt_probe(Key key, bool& hit) {
    TTEntry* e = &TT[key & ttMask];
    hit = e->key == key;
    return e;
}

int score_to_tt(int s, int ply) { return s >= MATE_BOUND ? s + ply : s <= -MATE_BOUND ? s - ply : s; }
int score_from_tt(int s, int ply) { return s >= MATE_BOUND ? s - ply : s <= -MATE_BOUND ? s + ply : s; }

void tt_store(TTEntry* e, Key key, int score, int eval, Move m, int depth, Bound bound, int ply) {
    bool sameKey = e->key == key;
    if (!sameKey && (e->genBound >> 2) == (ttGen & 63) && e->depth > depth + 4 && bound != BOUND_EXACT) return;
    if (sameKey && bound != BOUND_EXACT && depth + 2 < e->depth) return;
    if (m || !sameKey) e->move = m;
    e->key = key;
    e->score = int16_t(score_to_tt(score, ply));
    e->eval = int16_t(eval);
    e->depth = uint8_t(std::max(depth, 0));
    e->genBound = uint8_t(((ttGen & 63) << 2) | bound);
}

// ---------------- Search state ----------------
constexpr Move NULL_MOVE = 0xFFFF;

struct StackEntry {
    Move move = MOVE_NONE;
    int piece = EMPTY;
    int staticEval = 0;
    Move killers[2] = {MOVE_NONE, MOVE_NONE};
    Move excluded = MOVE_NONE;
};

StackEntry stack[MAX_PLY + 8];
int history[2][64][64];
Move counterMoves[12][64];
Move pvTable[MAX_PLY + 1][MAX_PLY + 1];
int pvLength[MAX_PLY + 1];
int lmrTable[64][64];

std::vector<Key> keyStack;
std::atomic<bool> stopFlag{false};
std::atomic<bool> searching{false};
std::thread searchThread;
uint64_t nodes = 0;
int selDepth = 0;
int rootDepth = 0;
Move rootBest = MOVE_NONE;
SearchLimits limits;
std::chrono::steady_clock::time_point startTime;
int64_t softLimit = 0, hardLimit = 0;
bool silent = false;

int64_t elapsed_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime).count();
}

void check_time() {
    if ((nodes & 1023) != 0 || rootDepth <= 1) return;
    if (limits.nodes && nodes >= limits.nodes) stopFlag = true;
    if (hardLimit > 0 && elapsed_ms() >= hardLimit) stopFlag = true;
}

int draw_score(const Position& pos) {
    // Contempt: a draw is a bit of a disappointment for Capablanca.
    return pos.stm == EngineColor ? -Style.contempt : Style.contempt;
}

bool is_repetition(const Position& pos) {
    int n = int(keyStack.size());
    for (int i = 2; i <= pos.halfmove && i <= n; i += 2)
        if (keyStack[n - i] == pos.key) return true;
    return false;
}

const int MvvValue[7] = {100, 320, 330, 500, 950, 0, 0};

void update_history(int& h, int bonus) {
    h += bonus - h * std::abs(bonus) / 16384;
}

// Does this move strike at the enemy king zone? (target square near the king, or a slider landing on a line to it)
bool attacks_king_zone(const Position& pos, Move m) {
    int ek = pos.king_sq(~pos.stm);
    int to = to_sq(m);
    Bitboard zone = KingAttacks[ek] | bb(ek);
    zone |= pawn_push(~pos.stm, zone);
    if (bb(to) & zone) return true;
    int pt = type_of(pos.board[from_sq(m)]);
    if (is_promotion(m)) pt = promo_type(m);
    Bitboard occ = pos.all ^ bb(from_sq(m));
    return attacks_by_type(pt, to, occ) & KingAttacks[ek] & ~pos.occ[pos.stm];
}

void score_moves(const Position& pos, MoveList& list, Move ttMove, int ply) {
    Move counter = MOVE_NONE;
    if (ply > 0 && stack[ply - 1].move != MOVE_NONE && stack[ply - 1].move != NULL_MOVE)
        counter = counterMoves[stack[ply - 1].piece][to_sq(stack[ply - 1].move)];
    for (int i = 0; i < list.size; i++) {
        Move m = list.moves[i];
        int& sc = list.scores[i];
        if (m == ttMove) { sc = 10'000'000; continue; }
        if (is_tactical(m)) {
            int victim = pos.captured_type(m);
            int v = victim == NO_PIECE_TYPE ? 0 : MvvValue[victim];
            if (is_promotion(m)) v += 900;
            int attacker = type_of(pos.board[from_sq(m)]);
            int mvv = v * 10 - MvvValue[attacker] / 10;
            if (pos.see_ge(m, 0)) sc = 2'000'000 + mvv;
            else if (Style.sacrificeVision && attacks_king_zone(pos, m)) sc = 850'000 + mvv; // attacking sacrifice
            else sc = -1'000'000 + mvv;
        } else if (m == stack[ply].killers[0]) sc = 900'000;
        else if (m == stack[ply].killers[1]) sc = 890'000;
        else if (m == counter) sc = 880'000;
        else {
            sc = history[pos.stm][from_sq(m)][to_sq(m)];
            if (Style.sacrificeVision && attacks_king_zone(pos, m)) sc += 1500;
        }
    }
}

Move pick_move(MoveList& list, int idx) {
    int best = idx;
    for (int i = idx + 1; i < list.size; i++)
        if (list.scores[i] > list.scores[best]) best = i;
    std::swap(list.moves[idx], list.moves[best]);
    std::swap(list.scores[idx], list.scores[best]);
    return list.moves[idx];
}

int qsearch(const Position& pos, int alpha, int beta, int ply) {
    nodes++;
    check_time();
    if (stopFlag) return 0;
    if (ply > selDepth) selDepth = ply;
    if (ply >= MAX_PLY) return evaluate(pos);
    if (is_repetition(pos) || insufficient_material(pos)) return draw_score(pos);

    bool hit;
    TTEntry* tte = tt_probe(pos.key, hit);
    if (hit) {
        int s = score_from_tt(tte->score, ply);
        int bnd = tte->genBound & 3;
        if (bnd == BOUND_EXACT || (bnd == BOUND_LOWER && s >= beta) || (bnd == BOUND_UPPER && s <= alpha)) return s;
    }

    bool inCheck = pos.in_check();
    int standPat = -INF, bestScore = -INF;
    if (!inCheck) {
        standPat = hit ? tte->eval : evaluate(pos);
        bestScore = standPat;
        if (standPat >= beta) return standPat;
        if (standPat > alpha) alpha = standPat;
    }

    MoveList list;
    generate_moves(pos, list, !inCheck);
    score_moves(pos, list, hit ? tte->move : MOVE_NONE, ply);
    int legal = 0;
    Move bestMove = MOVE_NONE;
    int oldAlpha = alpha;
    for (int i = 0; i < list.size; i++) {
        Move m = pick_move(list, i);
        if (!inCheck) {
            if (!pos.see_ge(m, 0)) continue;
            int victim = pos.captured_type(m);
            int gain = (victim == NO_PIECE_TYPE ? 0 : MvvValue[victim]) + (is_promotion(m) ? 850 : 0);
            if (standPat + gain + 200 <= alpha) continue; // delta pruning
        }
        Position child = pos;
        if (!child.make(m)) continue;
        legal++;
        keyStack.push_back(pos.key);
        int score = -qsearch(child, -beta, -alpha, ply + 1);
        keyStack.pop_back();
        if (stopFlag) return 0;
        if (score > bestScore) {
            bestScore = score;
            if (score > alpha) {
                bestMove = m;
                alpha = score;
                if (score >= beta) break;
            }
        }
    }
    if (inCheck && legal == 0) return -MATE + ply;
    Bound b = bestScore >= beta ? BOUND_LOWER : bestScore > oldAlpha ? BOUND_EXACT : BOUND_UPPER;
    tt_store(tte, pos.key, bestScore, standPat == -INF ? 0 : standPat, bestMove, 0, b, ply);
    return bestScore;
}

int search(const Position& pos, int alpha, int beta, int depth, int ply, bool cutNode) {
    pvLength[ply] = ply;
    if (depth <= 0) return qsearch(pos, alpha, beta, ply);

    nodes++;
    check_time();
    if (stopFlag) return 0;
    if (ply > selDepth) selDepth = ply;

    bool root = ply == 0;
    bool pvNode = beta - alpha > 1;
    Move excluded = stack[ply].excluded;

    if (!root) {
        if (pos.halfmove >= 100 || is_repetition(pos) || insufficient_material(pos)) return draw_score(pos);
        if (ply >= MAX_PLY) return evaluate(pos);
        alpha = std::max(alpha, -MATE + ply);
        beta = std::min(beta, MATE - ply - 1);
        if (alpha >= beta) return alpha;
    }

    bool inCheck = pos.in_check();

    bool hit = false;
    TTEntry* tte = tt_probe(pos.key, hit);
    if (excluded) hit = false;
    Move ttMove = hit ? tte->move : MOVE_NONE;
    if (root && rootBest) ttMove = rootBest;
    int ttScore = hit ? score_from_tt(tte->score, ply) : 0;
    int ttDepth = hit ? tte->depth : -1;
    int ttBound = hit ? (tte->genBound & 3) : BOUND_NONE;
    if (ttMove && !pos.is_pseudo_legal(ttMove)) ttMove = MOVE_NONE;

    if (!pvNode && hit && ttDepth >= depth) {
        if (ttBound == BOUND_EXACT || (ttBound == BOUND_LOWER && ttScore >= beta) || (ttBound == BOUND_UPPER && ttScore <= alpha))
            return ttScore;
    }

    int staticEval;
    if (inCheck) staticEval = -INF;
    else {
        staticEval = hit ? tte->eval : evaluate(pos);
        if (!hit && !excluded) tt_store(tte, pos.key, 0, staticEval, MOVE_NONE, -1, BOUND_NONE, ply);
    }
    int eval = staticEval;
    if (!inCheck && hit && ((ttBound == BOUND_LOWER && ttScore > eval) || (ttBound == BOUND_UPPER && ttScore < eval)))
        eval = ttScore;
    stack[ply].staticEval = staticEval;
    bool improving = !inCheck && ply >= 2 && stack[ply - 2].staticEval != -INF && staticEval > stack[ply - 2].staticEval;
    stack[ply + 1].killers[0] = stack[ply + 1].killers[1] = MOVE_NONE;

    if (!pvNode && !inCheck && !excluded) {
        // Reverse futility pruning
        if (depth <= 7 && eval - 75 * (depth - improving) >= beta && std::abs(eval) < MATE_BOUND) return eval;
        // Null move pruning
        if (depth >= 3 && eval >= beta && ply > 0 && stack[ply - 1].move != NULL_MOVE &&
            pos.non_pawn_material(pos.stm) && beta > -MATE_BOUND) {
            int R = 3 + depth / 4 + std::min(3, (eval - beta) / 200);
            Position child = pos;
            child.make_null();
            stack[ply].move = NULL_MOVE;
            stack[ply].piece = EMPTY;
            keyStack.push_back(pos.key);
            int score = -search(child, -beta, -beta + 1, depth - R, ply + 1, !cutNode);
            keyStack.pop_back();
            if (stopFlag) return 0;
            if (score >= beta) return score >= MATE_BOUND ? beta : score;
        }
        // Razoring
        if (depth <= 3 && eval + 250 * depth < alpha) {
            int score = qsearch(pos, alpha, alpha + 1, ply);
            if (score <= alpha) return score;
        }
    }

    // Internal iterative reduction
    if (depth >= 4 && !ttMove && (pvNode || cutNode)) depth--;

    MoveList list;
    generate_moves(pos, list, false);
    score_moves(pos, list, ttMove, ply);

    int bestScore = -INF, moveCount = 0;
    Move bestMove = MOVE_NONE;
    Move quiets[64];
    int quietCount = 0;
    int oldAlpha = alpha;

    for (int i = 0; i < list.size; i++) {
        Move m = pick_move(list, i);
        if (m == excluded) continue;
        Position child = pos;
        if (!child.make(m)) continue;
        moveCount++;

        bool quiet = !is_tactical(m);
        bool givesCheck = child.in_check();
        bool kingStrike = Style.sacrificeVision && (givesCheck || attacks_king_zone(pos, m));
        int see = (quiet && !kingStrike) ? 0 : pos.see(m);
        bool attackingSac = kingStrike && see < -50;

        // ---- Pruning (never for attacking sacrifices: that is where brilliancies hide) ----
        if (!root && bestScore > -MATE_BOUND && pos.non_pawn_material(pos.stm) && !attackingSac) {
            if (quiet && !givesCheck) {
                int lmpLimit = (3 + depth * depth) / (2 - improving);
                if (depth <= 8 && moveCount > lmpLimit && !kingStrike) continue;
                if (depth <= 8 && !inCheck && staticEval + 100 + 90 * depth <= alpha && !kingStrike) continue;
                if (depth <= 8 && !pos.see_ge(m, -60 * depth)) continue;
            } else if (depth <= 6 && see < -100 * depth) {
                continue;
            }
        }

        // ---- Extensions ----
        int ext = 0;
        if (ply < 2 * rootDepth) {
            if (!root && depth >= 8 && m == ttMove && !excluded && ttDepth >= depth - 3 &&
                (ttBound & BOUND_LOWER) && std::abs(ttScore) < MATE_BOUND) {
                int sBeta = ttScore - 2 * depth;
                stack[ply].excluded = m;
                int s = search(pos, sBeta - 1, sBeta, (depth - 1) / 2, ply, cutNode);
                stack[ply].excluded = MOVE_NONE;
                if (stopFlag) return 0;
                if (s < sBeta) ext = 1;
                else if (sBeta >= beta) return sBeta; // multi-cut
            }
            if (!ext && givesCheck && (see >= 0 || kingStrike)) ext = 1;
            if (!ext && attackingSac && depth <= 10) ext = 1; // verify the sacrifice deeper
        }
        int newDepth = depth - 1 + ext;

        stack[ply].move = m;
        stack[ply].piece = pos.board[from_sq(m)];
        keyStack.push_back(pos.key);

        int score;
        if (depth >= 3 && moveCount > 1 + (pvNode ? 1 : 0) && (quiet || see < 0)) {
            int r = lmrTable[std::min(depth, 63)][std::min(moveCount, 63)];
            r += cutNode;
            r += !improving;
            r -= pvNode;
            if (m == stack[ply].killers[0] || m == stack[ply].killers[1]) r--;
            if (quiet) r -= history[pos.stm][from_sq(m)][to_sq(m)] / 6000;
            if (givesCheck) r--;
            if (kingStrike) r--;      // keep attacking ideas alive
            if (attackingSac) r -= 2; // and sacrifices even more so
            r = std::clamp(r, 0, newDepth - 1);
            score = -search(child, -alpha - 1, -alpha, newDepth - r, ply + 1, true);
            if (score > alpha && r > 0) score = -search(child, -alpha - 1, -alpha, newDepth, ply + 1, !cutNode);
        } else if (!pvNode || moveCount > 1) {
            score = -search(child, -alpha - 1, -alpha, newDepth, ply + 1, !cutNode);
        } else {
            score = alpha + 1; // force full window below
        }
        if (pvNode && (moveCount == 1 || score > alpha))
            score = -search(child, -beta, -alpha, newDepth, ply + 1, false);

        keyStack.pop_back();
        if (stopFlag) return 0;

        if (score > bestScore) {
            bestScore = score;
            if (score > alpha) {
                bestMove = m;
                alpha = score;
                pvTable[ply][ply] = m;
                for (int j = ply + 1; j < pvLength[ply + 1]; j++) pvTable[ply][j] = pvTable[ply + 1][j];
                pvLength[ply] = std::max(pvLength[ply + 1], ply + 1);
                if (root) rootBest = m;
                if (score >= beta) break;
            }
        }
        if (quiet && quietCount < 64) quiets[quietCount++] = m;
    }

    if (moveCount == 0) {
        if (excluded) return alpha;
        return inCheck ? -MATE + ply : 0;
    }

    if (bestScore >= beta && bestMove && !is_tactical(bestMove)) {
        if (stack[ply].killers[0] != bestMove) {
            stack[ply].killers[1] = stack[ply].killers[0];
            stack[ply].killers[0] = bestMove;
        }
        if (ply > 0 && stack[ply - 1].move != MOVE_NONE && stack[ply - 1].move != NULL_MOVE)
            counterMoves[stack[ply - 1].piece][to_sq(stack[ply - 1].move)] = bestMove;
        int bonus = std::min(depth * depth * 16, 1600);
        for (int j = 0; j < quietCount; j++) {
            Move q = quiets[j];
            update_history(history[pos.stm][from_sq(q)][to_sq(q)], q == bestMove ? bonus : -bonus);
        }
    }

    if (!excluded) {
        Bound b = bestScore >= beta ? BOUND_LOWER : (pvNode && bestScore > oldAlpha) ? BOUND_EXACT : BOUND_UPPER;
        tt_store(tte, pos.key, bestScore, staticEval == -INF ? 0 : staticEval, bestMove, depth, b, ply);
    }
    return bestScore;
}

std::string score_string(int s) {
    if (s >= MATE_BOUND) return "mate " + std::to_string((MATE - s + 1) / 2);
    if (s <= -MATE_BOUND) return "mate -" + std::to_string((MATE + s) / 2);
    return "cp " + std::to_string(s);
}

void compute_time(Color us) {
    softLimit = hardLimit = 0;
    if (limits.movetime > 0) {
        softLimit = hardLimit = std::max(1, limits.movetime - 20);
        return;
    }
    if (limits.infinite || limits.time[us] < 0) return;
    int64_t t = limits.time[us], inc = limits.inc[us];
    int mtg = limits.movestogo > 0 ? std::min(limits.movestogo, 40) : 30;
    int64_t overhead = 30;
    int64_t base = t / mtg + inc * 3 / 4;
    softLimit = std::max<int64_t>(1, base * 6 / 10);
    hardLimit = std::max<int64_t>(1, std::min(base * 3, t - overhead) );
    hardLimit = std::min(hardLimit, std::max<int64_t>(1, t / 2));
    softLimit = std::min(softLimit, hardLimit);
}

void iterate(Position pos) {
    nodes = 0;
    rootBest = MOVE_NONE;
    ttGen++;
    for (auto& s : stack) s = StackEntry();
    for (auto& side : history)
        for (auto& f : side)
            for (int& h : f) h /= 2;

    MoveList legal;
    generate_legal(pos, legal);
    Move best = legal.size ? legal.moves[0] : MOVE_NONE;
    int score = 0, prevScore = 0;
    Move prevBest = MOVE_NONE;
    int stability = 0;

    for (rootDepth = 1; rootDepth <= limits.depth && legal.size; rootDepth++) {
        selDepth = 0;
        int alpha = -INF, beta = INF, window = 25;
        if (rootDepth >= 5) {
            alpha = std::max(-INF, prevScore - window);
            beta = std::min(INF, prevScore + window);
        }
        while (true) {
            score = search(pos, alpha, beta, rootDepth, 0, false);
            if (stopFlag) break;
            if (score <= alpha) { beta = (alpha + beta) / 2; alpha = std::max(-INF, score - window); }
            else if (score >= beta) { beta = std::min(INF, score + window); }
            else break;
            window += window / 2 + 5;
        }
        if (stopFlag && rootDepth > 1) break;
        best = rootBest ? rootBest : best;
        prevScore = score;

        if (!silent) {
            int64_t ms = elapsed_ms();
            std::cout << "info depth " << rootDepth << " seldepth " << selDepth << " score " << score_string(score)
                      << " nodes " << nodes << " nps " << (ms > 0 ? nodes * 1000 / ms : nodes) << " time " << ms
                      << " pv";
            for (int j = 0; j < pvLength[0]; j++) std::cout << " " << move_to_uci(pvTable[0][j]);
            std::cout << std::endl;
        }

        stability = best == prevBest ? std::min(stability + 1, 6) : 0;
        prevBest = best;
        if (softLimit > 0) {
            // Stable best move -> stop earlier; unstable -> think longer.
            int64_t adj = softLimit * (14 - stability) / 10;
            if (elapsed_ms() >= std::min(adj, hardLimit)) break;
        }
        if (std::abs(score) >= MATE_BOUND && rootDepth > 10 && softLimit > 0) break;
    }

    while (limits.infinite && !stopFlag) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (!silent) std::cout << "bestmove " << move_to_uci(best) << std::endl;
}

} // namespace

void tt_resize(size_t mb) {
    size_t entries = mb * 1024 * 1024 / sizeof(TTEntry);
    size_t pow2 = 1;
    while (pow2 * 2 <= entries) pow2 *= 2;
    TT.assign(pow2, TTEntry{});
    ttMask = pow2 - 1;
}

void new_game() {
    std::fill(TT.begin(), TT.end(), TTEntry{});
    memset(history, 0, sizeof(history));
    memset(counterMoves, 0, sizeof(counterMoves));
    for (int d = 0; d < 64; d++)
        for (int m = 0; m < 64; m++)
            lmrTable[d][m] = (d && m) ? int(0.75 + std::log(d) * std::log(m) / 2.25) : 0;
}

void search_wait() {
    if (searchThread.joinable()) searchThread.join();
}

void search_stop() {
    stopFlag = true;
    search_wait();
}

void search_start(const Position& pos, const std::vector<Key>& history, const SearchLimits& lim) {
    search_stop();
    stopFlag = false;
    limits = lim;
    keyStack = history;
    EngineColor = pos.stm;
    startTime = std::chrono::steady_clock::now();
    compute_time(pos.stm);
    searchThread = std::thread(iterate, pos);
}

void bench(int depth) {
    const char* fens[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "r1bq1rk1/pp2bppp/2n1pn2/3p4/2PP4/2N1PN2/PP1B1PPP/R2QKB1R w KQ - 0 8",
        "r1b2rk1/2q1bppp/p2p1n2/np2p3/3PP3/5N1P/PPBN1PP1/R1BQR1K1 w - - 0 13",
        "2r3k1/pp3ppp/1qn1p3/3pP3/3P4/P1PQ1N2/5PPP/R4RK1 b - - 0 20",
        "r1bqr1k1/ppp2ppp/2np1n2/2b1p3/2B1P3/2NP1N2/PPP2PPP/R1BQR1K1 w - - 0 8",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1",
        "r1bq1rk1/pppn1ppp/4pn2/3p2B1/1bPP4/2NBPN2/PP3PPP/R2QK2R b KQ - 0 7",
        "2rq1rk1/pb2bppp/1pn1pn2/2pp4/3P4/1PNBPN2/PBQ2PPP/2R2RK1 w - - 0 12",
    };
    silent = true;
    uint64_t total = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (const char* f : fens) {
        Position pos;
        pos.set_fen(f);
        new_game();
        stopFlag = false;
        limits = SearchLimits();
        limits.depth = depth;
        keyStack.clear();
        EngineColor = pos.stm;
        startTime = std::chrono::steady_clock::now();
        softLimit = hardLimit = 0;
        iterate(pos);
        total += nodes;
    }
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    silent = false;
    std::cout << "Bench: " << total << " nodes " << uint64_t(total * 1000.0 / std::max(1.0, ms)) << " nps " << int64_t(ms) << " ms" << std::endl;
}
