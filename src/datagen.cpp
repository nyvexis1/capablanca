// Capablanca - self-play data generation for NNUE training.
//
// Plays games from randomised openings with a fixed node budget per move and records quiet
// positions with the search score and the final game result.
//
// Output: 32-byte records, appended per finished game (so stopping early keeps finished games):
//   u64 occupancy            bit i set = square i (a1=0) occupied
//   u8  pieces[16]           one nibble per occupied square in ascending square order,
//                            low nibble first: piece type (0 pawn .. 5 king) | 8 if black
//   u8  stm                  0 white, 1 black
//   u8  halfmove clock
//   u16 fullmove number
//   i16 score                search score, white's point of view, centipawns
//   u8  result               0 black won, 1 draw, 2 white won
//   u8  unused
#include "engine.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {

#pragma pack(push, 1)
struct Record {
    uint64_t occ;
    uint8_t pieces[16];
    uint8_t stm;
    uint8_t halfmove;
    uint16_t fullmove;
    int16_t score;
    uint8_t result;
    uint8_t unused;
};
#pragma pack(pop)
static_assert(sizeof(Record) == 32);

Record pack(const Position& pos, int whiteScore) {
    Record r{};
    r.occ = pos.all;
    int i = 0;
    for (Bitboard b = pos.all; b; b &= b - 1) {
        int sq = lsb(b);
        int pc = pos.board[sq];
        uint8_t nib = uint8_t(type_of(pc) | (color_of(pc) == BLACK ? 8 : 0));
        r.pieces[i / 2] |= i % 2 ? uint8_t(nib << 4) : nib;
        i++;
    }
    r.stm = uint8_t(pos.stm);
    r.halfmove = uint8_t(std::min(pos.halfmove, 255));
    r.fullmove = uint16_t(pos.fullmove);
    r.score = int16_t(std::clamp(whiteScore, -32000, 32000));
    return r;
}

bool is_repetition(const Position& pos, const std::vector<Key>& history) {
    for (int i = int(history.size()) - 2; i >= 0 && i >= int(history.size()) - pos.halfmove; i -= 2)
        if (history[i] == pos.key) return true;
    return false;
}

} // namespace

void datagen(const std::string& outFile, uint64_t seed, uint64_t nodes) {
#ifdef _WIN32
    // Lowest priority: everything else on the machine (video, browser, ...) goes first.
    SetPriorityClass(GetCurrentProcess(), IDLE_PRIORITY_CLASS);
#endif
    // Neutral evaluation for training data: no style asymmetry or contempt.
    Style.aggression = Style.defense = 100;
    Style.contempt = 0;
    tt_resize(16);

    std::mt19937_64 rng(seed);
    std::ofstream out(outFile, std::ios::binary | std::ios::app);
    uint64_t games = 0, positions = 0;
    int counts[3] = {0, 0, 0};

    while (true) {
        new_game();
        Position pos;
        pos.set_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        std::vector<Key> history;

        // Random opening: 8 or 9 uniformly random legal plies
        int randomPlies = 8 + int(rng() % 2);
        bool ok = true;
        for (int i = 0; i < randomPlies && ok; i++) {
            MoveList legal;
            generate_legal(pos, legal);
            if (!legal.size) ok = false;
            else {
                history.push_back(pos.key);
                pos.make(legal.moves[rng() % legal.size]);
            }
        }
        if (!ok) continue;

        SearchLimits lim;
        lim.nodes = nodes;
        Move best;
        // Skip openings that are already lopsided
        {
            SearchLimits quick;
            quick.depth = 8;
            if (std::abs(search_sync(pos, history, quick, best)) > 400) continue;
        }

        std::vector<Record> recs;
        int result = -1; // 0 black, 1 draw, 2 white
        int winPlies = 0, drawPlies = 0;
        for (int ply = 0; result < 0; ply++) {
            MoveList legal;
            generate_legal(pos, legal);
            if (!legal.size) { result = pos.in_check() ? (pos.stm == WHITE ? 0 : 2) : 1; break; }
            if (pos.halfmove >= 100 || is_repetition(pos, history) || insufficient_material(pos)) { result = 1; break; }

            int score = search_sync(pos, history, lim, best);
            if (best == MOVE_NONE) { result = 1; break; }
            int white = pos.stm == WHITE ? score : -score;

            // Adjudication: decisive once the score stays huge; drawn once it stays flat late on
            winPlies = std::abs(score) >= 2000 ? winPlies + 1 : 0;
            if (winPlies >= 6) { result = white > 0 ? 2 : 0; break; }
            drawPlies = (ply >= 80 && std::abs(score) <= 10) ? drawPlies + 1 : 0;
            if (drawPlies >= 12 || ply >= 400) { result = 1; break; }

            if (!pos.in_check() && !is_tactical(best) && std::abs(score) < MATE_BOUND)
                recs.push_back(pack(pos, white));

            history.push_back(pos.key);
            pos.make(best);
            if (pos.halfmove == 0) history.clear();
        }

        for (Record& r : recs) r.result = uint8_t(result);
        out.write(reinterpret_cast<const char*>(recs.data()), std::streamsize(recs.size() * sizeof(Record)));
        out.flush();
        games++;
        positions += recs.size();
        counts[result]++;
        if (games % 50 == 0)
            std::cout << "games " << games << "  positions " << positions << "  white/draw/black " << counts[2] << "/"
                      << counts[1] << "/" << counts[0] << std::endl;
    }
}
