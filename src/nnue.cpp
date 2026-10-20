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

namespace {

constexpr int QA = 255, QB = 64, SCALE = 400;

int H = 0;
std::vector<int16_t> W0, B0, W1;
int32_t B1 = 0;

inline int feature(Color persp, int pc, int sq) {
    int side = color_of(pc) == persp ? 0 : 384;
    return side + type_of(pc) * 64 + (persp == WHITE ? sq : sq ^ 56);
}

} // namespace

bool nnue_load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    char magic[8];
    int32_t h = 0;
    in.read(magic, 8);
    in.read(reinterpret_cast<char*>(&h), 4);
    if (!in || std::memcmp(magic, "CAPANNUE", 8) != 0 || h <= 0 || h > 4096) return false;
    std::vector<int16_t> w0(size_t(768) * h), b0(h), w1(2 * size_t(h));
    int32_t b1 = 0;
    in.read(reinterpret_cast<char*>(w0.data()), std::streamsize(w0.size() * 2));
    in.read(reinterpret_cast<char*>(b0.data()), std::streamsize(b0.size() * 2));
    in.read(reinterpret_cast<char*>(w1.data()), std::streamsize(w1.size() * 2));
    in.read(reinterpret_cast<char*>(&b1), 4);
    if (!in) return false;
    H = h;
    W0 = std::move(w0);
    B0 = std::move(b0);
    W1 = std::move(w1);
    B1 = b1;
    return true;
}

bool nnue_ready() { return H > 0; }

// Score in centipawns from the side to move's point of view.
int nnue_evaluate(const Position& pos) {
    // Full refresh of both accumulators. Simple and exact; incremental updates can come later.
    alignas(64) int16_t acc[2][4096];
    for (int p = 0; p < 2; p++) std::memcpy(acc[p], B0.data(), size_t(H) * 2);
    for (Bitboard b = pos.all; b; b &= b - 1) {
        int sq = lsb(b), pc = pos.board[sq];
        for (int p = 0; p < 2; p++) {
            const int16_t* w = &W0[size_t(feature(Color(p), pc, sq)) * H];
            int16_t* a = acc[p];
            for (int i = 0; i < H; i++) a[i] += w[i];
        }
    }
    const int16_t* us = acc[pos.stm];
    const int16_t* them = acc[~pos.stm];
    int64_t sum = 0;
    for (int i = 0; i < H; i++) {
        int32_t v = std::clamp<int32_t>(us[i], 0, QA);
        sum += int64_t(v * W1[i]) * v;
    }
    for (int i = 0; i < H; i++) {
        int32_t v = std::clamp<int32_t>(them[i], 0, QA);
        sum += int64_t(v * W1[H + i]) * v;
    }
    int64_t out = sum / QA + B1; // = QA * QB * (network output)
    return int(out * SCALE / (int64_t(QA) * QB));
}
