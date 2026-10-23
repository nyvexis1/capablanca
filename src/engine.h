// Capablanca - evaluation and search interfaces.
#pragma once
#include "chess.h"
#include <string>
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
    int sacrificeVision = 63; // bitmask of SV_* features below
};
// Sacrifice-vision feature bits (each one tested separately)
enum : int {
    SV_ORDER = 1,     // order king-zone moves/sacrifices earlier
    SV_PRUNE = 2,     // exempt king strikes/sacrifices from pruning
    SV_LMR = 4,       // reduce king strikes/sacrifices less
    SV_EXT = 8,       // extend material-losing king strikes
    SV_CHECKEXT = 16, // extend every check, even losing ones
    SV_VERIFY = 32,   // probe sacrifices at half depth: sound ones are extended, refuted ones lose the bonuses
};
extern StyleConfig Style;
extern Color EngineColor;

void init_eval();
int evaluate(const Position& pos);
void eval_trace(const Position& pos);
bool insufficient_material(const Position& pos);

// Texel tuning support
struct TuneParam {
    const char* name;
    int* values;
    int count;
    bool isS; // pairs of (mg, eg)
};
std::vector<TuneParam> tune_params();
void tune(const std::string& dataFile, int threads, int passes);
int qsearch_score(const Position& pos);

// NNUE (src/nnue.cpp). evaluate() uses it when a net is loaded and UseNNUE is set.
bool nnue_load(const std::string& path);
bool nnue_load_embedded();
bool nnue_ready();
int nnue_evaluate(const Position& pos);
int nnue_evaluate_full(const Position& pos);
extern bool UseNNUE;

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
void set_threads(int n); // Lazy SMP search threads (UCI option Threads)
void search_start(const Position& pos, const std::vector<Key>& history, const SearchLimits& limits);
void search_stop();
void search_wait();
int search_sync(const Position& pos, const std::vector<Key>& history, const SearchLimits& limits, Move& best);
void datagen(const std::string& outFile, uint64_t seed, uint64_t nodes, int aggression = 100);
void bench(int depth);
// Thread-safe line output (search thread and UCI thread share stdout)
void sync_out(const std::string& line);
