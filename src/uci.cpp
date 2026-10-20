// Capablanca - UCI protocol front end.
#include "engine.h"
#include <iostream>
#include <sstream>

namespace {

const char* StartFen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

Position pos;
std::vector<Key> history; // keys of positions before the current one

void set_position(std::istringstream& is) {
    std::string token, fen;
    is >> token;
    if (token == "startpos") {
        fen = StartFen;
        is >> token; // "moves" (optional)
    } else if (token == "fen") {
        while (is >> token && token != "moves") fen += token + " ";
    } else return;
    pos.set_fen(fen);
    history.clear();
    while (is >> token) {
        Move m = parse_uci_move(pos, token);
        if (m == MOVE_NONE) break;
        history.push_back(pos.key);
        pos.make(m);
        if (pos.halfmove == 0) history.clear();
    }
}

void go(std::istringstream& is) {
    SearchLimits lim;
    std::string token;
    while (is >> token) {
        if (token == "wtime") is >> lim.time[WHITE];
        else if (token == "btime") is >> lim.time[BLACK];
        else if (token == "winc") is >> lim.inc[WHITE];
        else if (token == "binc") is >> lim.inc[BLACK];
        else if (token == "movestogo") is >> lim.movestogo;
        else if (token == "movetime") is >> lim.movetime;
        else if (token == "depth") is >> lim.depth;
        else if (token == "nodes") is >> lim.nodes;
        else if (token == "infinite") lim.infinite = true;
        else if (token == "perft") {
            int d = 1;
            is >> d;
            perft_divide(pos, d);
            return;
        }
    }
    lim.depth = std::min(lim.depth, MAX_PLY - 1);
    search_start(pos, history, lim);
}

void set_option(std::istringstream& is) {
    std::string token, name, value;
    is >> token; // name
    while (is >> token && token != "value") name += (name.empty() ? "" : " ") + token;
    is >> value;
    for (auto& ch : name) ch = char(tolower(ch));
    if (name == "hash") tt_resize(std::max(1, std::stoi(value)));
    else if (name == "aggression") Style.aggression = std::stoi(value);
    else if (name == "defense") Style.defense = std::stoi(value);
    else if (name == "contempt") Style.contempt = std::stoi(value);
    else if (name == "sacrificevision") Style.sacrificeVision = std::stoi(value);
}

} // namespace

int main(int argc, char** argv) {
    std::ios::sync_with_stdio(false);
    init_bitboards();
    Zobrist::init();
    init_eval();
    tt_resize(64);
    new_game();
    pos.set_fen(StartFen);

    if (argc > 1 && std::string(argv[1]) == "tune") {
        // capablanca tune <data> [threads] [passes]
        tune(argv[2], argc > 3 ? std::stoi(argv[3]) : 8, argc > 4 ? std::stoi(argv[4]) : 10);
        return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "bench") {
        bench(argc > 2 ? std::stoi(argv[2]) : 11);
        return 0;
    }

    std::string line;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream is(line);
        std::string cmd;
        is >> cmd;
        if (cmd == "uci") {
            std::cout << "id name Capablanca 0.1\n"
                      << "id author curko & Claude\n"
                      << "option name Hash type spin default 64 min 1 max 4096\n"
                      << "option name Threads type spin default 1 min 1 max 1\n"
                      << "option name Aggression type spin default " << Style.aggression << " min 0 max 300\n"
                      << "option name Defense type spin default " << Style.defense << " min 0 max 300\n"
                      << "option name Contempt type spin default " << Style.contempt << " min -100 max 100\n"
                      << "option name SacrificeVision type spin default " << Style.sacrificeVision << " min 0 max 31\n"
                      << "uciok" << std::endl;
        } else if (cmd == "isready") sync_out("readyok");
        else if (cmd == "ucinewgame") { search_stop(); new_game(); }
        else if (cmd == "setoption") { search_stop(); set_option(is); }
        else if (cmd == "position") { search_stop(); set_position(is); }
        else if (cmd == "go") go(is);
        else if (cmd == "stop") search_stop();
        else if (cmd == "quit") break;
        else if (cmd == "d") std::cout << pos.fen() << std::endl;
        else if (cmd == "eval") eval_trace(pos);
        else if (cmd == "bench") { int d = 11; is >> d; bench(d); }
    }
    search_stop();
    return 0;
}
