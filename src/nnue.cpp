// Capablanca - NNUE evaluation: (768 -> H) x 2 perspectives -> 1, SCReLU.
//
// Network file (written by scripts/nnue/train.py):
//   "CAPANNUE", i32 H, i16 w0[768][H], i16 b0[H], i16 w1[2H], i32 b1
// Quantisation: feature transformer x QA, output weights x QB, output bias x QA*QB.
// Inputs from a perspective p: (piece is p's ? 0 : 384) + type * 64 + square (flipped if p is black).
#include "engine.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>

namespace Nnue {
const int16_t* W0 = nullptr;
const int16_t* B0 = nullptr;
uint32_t NetId = 0;
} // namespace Nnue

namespace {

constexpr int QA = 255, QB = 64, SCALE = 400;

std::vector<int16_t> W0, B0, W1;
int32_t B1 = 0;

// Rebuild both accumulators from scratch.
void refresh(const Position& pos, int16_t (&acc)[2][NNUE_H]) {
    for (int p = 0; p < 2; p++) std::memcpy(acc[p], B0.data(), sizeof(acc[p]));
    for (Bitboard b = pos.all; b; b &= b - 1) {
        int sq = lsb(b), pc = pos.board[sq];
        for (int p = 0; p < 2; p++) {
            const int16_t* w = &W0[size_t(Nnue::feature(Color(p), pc, sq)) * NNUE_H];
            for (int i = 0; i < NNUE_H; i++) acc[p][i] = int16_t(acc[p][i] + w[i]);
        }
    }
}

// Sum of screlu(acc) * w over one perspective. The trainer clips output weights to +-1.98, so
// |w| <= 127 and v * w fits in 16 bits: the loop vectorises as 16-bit multiplies + pmaddwd.
int32_t screlu_dot(const int16_t* acc, const int16_t* w) {
    int32_t sum = 0;
    for (int i = 0; i < NNUE_H; i++) {
        int16_t v = int16_t(std::clamp<int>(acc[i], 0, QA));
        int16_t vw = int16_t(v * w[i]);
        sum += int32_t(vw) * v;
    }
    return sum;
}

int output(const int16_t* us, const int16_t* them) {
    int64_t sum = int64_t(screlu_dot(us, W1.data())) + screlu_dot(them, W1.data() + NNUE_H);
    int64_t out = sum / QA + B1; // = QA * QB * (network output)
    return int(out * SCALE / (int64_t(QA) * QB));
}

} // namespace

bool nnue_load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    char magic[8];
    int32_t h = 0;
    in.read(magic, 8);
    in.read(reinterpret_cast<char*>(&h), 4);
    if (!in || std::memcmp(magic, "CAPANNUE", 8) != 0 || h != NNUE_H) return false;
    std::vector<int16_t> w0(size_t(768) * h), b0(h), w1(2 * size_t(h));
    int32_t b1 = 0;
    in.read(reinterpret_cast<char*>(w0.data()), std::streamsize(w0.size() * 2));
    in.read(reinterpret_cast<char*>(b0.data()), std::streamsize(b0.size() * 2));
    in.read(reinterpret_cast<char*>(w1.data()), std::streamsize(w1.size() * 2));
    in.read(reinterpret_cast<char*>(&b1), 4);
    if (!in) return false;
    W0 = std::move(w0);
    B0 = std::move(b0);
    W1 = std::move(w1);
    B1 = b1;
    Nnue::W0 = W0.data();
    Nnue::B0 = B0.data();
    Nnue::NetId++; // existing accumulators are now stale and get rebuilt on first use
    return true;
}

bool nnue_ready() { return Nnue::W0 != nullptr; }

// Score in centipawns from the side to move's point of view, using the incrementally
// maintained accumulators (rebuilt first if stale).
int nnue_evaluate(const Position& pos) {
    if (pos.accNet != Nnue::NetId) {
        refresh(pos, pos.acc);
        pos.accNet = Nnue::NetId;
    }
    return output(pos.acc[pos.stm], pos.acc[~pos.stm]);
}

// Same score computed from scratch (for verifying the incremental updates).
int nnue_evaluate_full(const Position& pos) {
    alignas(32) int16_t acc[2][NNUE_H];
    refresh(pos, acc);
    return output(acc[pos.stm], acc[~pos.stm]);
}
