"""Find Capablanca moves that Stockfish misjudges at shallow depth but approves at deep depth.

A move is a BRILLIANCY when:
  - at shallow depth, Stockfish thinks it is clearly worse than its own best move
    (loss >= --shallow-loss centipawns), and
  - at deep depth, Stockfish agrees it is (nearly) best (loss <= --deep-loss).
It is a SACRIFICE-BRILLIANCY if it also gives up material (static material drops 4 plies later).

Usage: python scripts/brilliancy.py matches/x.pgn [--shallow 8] [--deep 20] [--limit-games N]
"""
import argparse
import glob
import chess
import chess.engine
import chess.pgn

SF = "tools/stockfish/stockfish/stockfish-windows-x86-64-avx2.exe"
VAL = {chess.PAWN: 1, chess.KNIGHT: 3, chess.BISHOP: 3, chess.ROOK: 5, chess.QUEEN: 9, chess.KING: 0}


def material(board, color):
    return sum(v * (len(board.pieces(pt, color)) - len(board.pieces(pt, not color))) for pt, v in VAL.items())


def cp(score, color):
    return score.pov(color).score(mate_score=10000)


def move_loss(engine, board, move, depth):
    """centipawn loss of `move` versus Stockfish's best at `depth` (from mover's side)."""
    best = engine.analyse(board, chess.engine.Limit(depth=depth))
    best_cp = cp(best["score"], board.turn)
    if best.get("pv") and best["pv"][0] == move:
        return 0, best_cp
    played = engine.analyse(board, chess.engine.Limit(depth=depth), root_moves=[move])
    played_cp = cp(played["score"], board.turn)
    return max(0, best_cp - played_cp), played_cp


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pgns", nargs="+")
    ap.add_argument("--engine", default="Capablanca")
    ap.add_argument("--shallow", type=int, default=8)
    ap.add_argument("--deep", type=int, default=20)
    ap.add_argument("--shallow-loss", type=int, default=120)
    ap.add_argument("--deep-loss", type=int, default=30)
    ap.add_argument("--limit-games", type=int, default=0)
    ap.add_argument("--wins-only", action="store_true")
    args = ap.parse_args()

    engine = chess.engine.SimpleEngine.popen_uci(SF)
    engine.configure({"Threads": 4, "Hash": 256})
    files = [f for p in args.pgns for f in glob.glob(p)]
    n_games = n_moves = 0
    found = []
    for f in files:
        with open(f) as fh:
            while (g := chess.pgn.read_game(fh)) is not None:
                if args.engine not in (g.headers.get("White"), g.headers.get("Black")):
                    continue
                color = chess.WHITE if g.headers.get("White") == args.engine else chess.BLACK
                res = g.headers.get("Result")
                if args.wins_only and res != ("1-0" if color == chess.WHITE else "0-1"):
                    continue
                n_games += 1
                if args.limit_games and n_games > args.limit_games:
                    break
                board = g.board()
                nodes = list(g.mainline())
                moves = [n.move for n in nodes]
                for i, mv in enumerate(moves):
                    # skip opening-book moves (fastchess comments them "book") and opponent moves
                    if board.turn == color and "book" not in nodes[i].comment:
                        n_moves += 1
                        sl, _ = move_loss(engine, board, mv, args.shallow)
                        if sl >= args.shallow_loss:
                            dl, deep_cp = move_loss(engine, board, mv, args.deep)
                            if dl <= args.deep_loss:
                                before = material(board, color)
                                b2 = board.copy()
                                for m2 in moves[i:i + 4]:
                                    b2.push(m2)
                                sac = material(b2, color) <= before - 2
                                found.append((g, i, board.san(mv), sl, dl, deep_cp, sac, board.fen()))
                                print(f"{'SAC-' if sac else ''}BRILLIANCY  {g.headers.get('White')}-{g.headers.get('Black')} "
                                      f"{i // 2 + 1}{'.' if i % 2 == 0 else '...'}{board.san(mv)}  "
                                      f"shallow d{args.shallow} loss {sl}cp -> deep d{args.deep} loss {dl}cp "
                                      f"(eval {deep_cp:+})  fen: {board.fen()}", flush=True)
                    board.push(mv)
    engine.quit()
    sacs = sum(1 for x in found if x[6])
    print(f"\ngames {n_games}, Capablanca moves checked {n_moves}, brilliancies {len(found)} "
          f"({sacs} with sacrifice), rate {1000 * len(found) / max(1, n_moves):.1f} per 1000 moves")


if __name__ == "__main__":
    main()
