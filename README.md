![Capablanca](branding/banner.png)

# Capablanca

An attacking UCI chess engine written from scratch in C++20, with a neural network that taught itself by playing millions of games against itself. No Stockfish code and no Stockfish-labelled training data.

## Features

- **Search:** alpha-beta with PVS, transposition table, null move pruning, late move reductions, singular extensions, ProbCut, correction history, capture and continuation history, quiescence search
- **Lazy SMP:** multi-threaded search (`Threads` option, 1–256)
- **NNUE evaluation:** 768 → 2×H (SCReLU) → 1, trained only on Capablanca's own self-play (about 280M positions), embedded in the binary
- **Sacrifice Vision:** search and ordering heuristics that look for attacking sacrifices against the enemy king and verify them with a reduced-depth probe
- **Style controls:** `Aggression`, `Defense`, `Contempt` and `SacrificeVision` UCI options

## Editions

| Edition | Network width | Build |
|---|---|---|
| Rook (default) | 256 | `./build.sh` |
| Queen | 512 | `./build.sh capablanca-queen.exe queen` |

The Rook net is faster; the Queen net is bigger and slower.

## Building

The build uses MinGW-w64 GCC on Windows (Git Bash) and should also work with GCC on Linux:

```sh
./build.sh                 # -> capablanca.exe (Rook)
./build.sh out.exe queen   # Queen edition
```

The build uses `-march=native`, so the binary is tuned for the machine that built it. `-mno-avx` works around a MinGW stack-alignment crash.

## UCI options

| Option | Default | Notes |
|---|---|---|
| Hash | 64 | MB |
| Threads | 1 | Lazy SMP |
| Aggression | 125 | weight of our attack on the enemy king, % (0–300) |
| Defense | 100 | weight of the enemy attack on our king, % (0–300) |
| Contempt | 15 | centipawns; draws count as this much worse for us (−100–100) |
| SacrificeVision | 63 | bitmask of sacrifice heuristics |
| UseNNUE | true | false uses the hand-written evaluation |
| EvalFile | capablanca.nnue | load a different network |

Extra commands: `bench [depth]`, `eval`, `d`, `go perft N`, and the command-line modes `capablanca bench` and `capablanca datagen <out> [seed] [nodes] [aggression]`.

## Training

- **Self-play data:** `datagen` writes self-play positions as 32-byte records.
- **Training:** `scripts/nnue/train.py` (PyTorch) trains a network from those records.
- **Cloud runs:** `scripts/modal_cpu.py` and `scripts/nnue/modal_train.py` run data generation and training on [Modal](https://modal.com).

## Games

`games/` has a selection of Capablanca's games, including wins against Stockfish at `UCI_Elo` 2400 and 3190.

## License

GPL-3.0. See [LICENSE](LICENSE).

Written by nyvexis.
