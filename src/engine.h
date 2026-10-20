// Capablanca - evaluation and search interfaces.
#pragma once
#include "chess.h"
#include <vector>

constexpr int INF = 32001;
constexpr int MATE = 32000;
constexpr int MATE_BOUND = MATE - 256;
constexpr int MAX_PLY = 128;

// Style knobs, all exposed as UCI options.
struct StyleConfig {
    int aggression = 125;   // % weight of OUR attack on the enemy king
    int defense = 100;      // % weight of the enemy attack on our king
    int contempt = 15;      // cp; draws count as this much worse for us
    int sacrificeVision = 1; // 1 = exempt king-attack sacrifices from pruning + extend them
};
extern StyleConfig Style;
extern Color EngineColor;

void init_eval();
int evaluate(const Position& pos);
void eval_trace(const Position& pos);
bool insufficient_material(const Position& pos);

struct SearchLimits {
    int time[2] = {-1, -1};
    int inc[2] = {0, 0};
    int movestogo = 0;
    int movetime = -1;
    int depth = MAX_PLY - 1;
    uint64_t nodes = 0;
    bool infinite = false;
};

void tt_resize(size_t mb);
void new_game();
void search_start(const Position& pos, const std::vector<Key>& history, const SearchLimits& limits);
void search_stop();
void search_wait();
void bench(int depth);
