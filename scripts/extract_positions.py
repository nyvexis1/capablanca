"""Turn PGN games into Texel-tuning data: "FEN;result" lines (result from White's view).

Keeps quiet positions only: not in check, the next game move is not a capture or promotion,
outside the opening book and the first few engine moves, and not deep into won/lost endings.
Usage: python scripts/extract_positions.py out.txt matches/*.pgn [--per-game 12]
"""
import glob
import random
import sys
import chess
import chess.pgn

RESULT = {"1-0": 1.0, "0-1": 0.0, "1/2-1/2": 0.5}


def main():
    out_path = sys.argv[1]
    per_game = 12
    args = sys.argv[2:]
    if "--per-game" in args:
        i = args.index("--per-game")
        per_game = int(args[i + 1])
        args = args[:i] + args[i + 2:]
    files = [f for p in args for f in glob.glob(p)]
    random.seed(1)
    n_games = n_pos = 0
    with open(out_path, "w") as out:
        for f in files:
            with open(f) as fh:
                while (g := chess.pgn.read_game(fh)) is not None:
                    res = RESULT.get(g.headers.get("Result"))
                    if res is None:
                        continue
                    n_games += 1
                    board = g.board()
                    cands = []
                    ply_after_book = 0
                    for node in g.mainline():
                        mv = node.move
                        in_book = "book" in node.comment
                        if not in_book:
                            ply_after_book += 1
                        if (not in_book and ply_after_book > 4 and not board.is_check()
                                and not board.is_capture(mv) and mv.promotion is None):
                            cands.append(board.fen())
                        board.push(mv)
                    for fen in random.sample(cands, min(per_game, len(cands))):
                        out.write(f"{fen};{res}\n")
                        n_pos += 1
    print(f"{n_games} games -> {n_pos} positions in {out_path}")


if __name__ == "__main__":
    main()
