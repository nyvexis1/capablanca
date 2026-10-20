"""Measure how sacrificial/attacking Capablanca's games are.

Usage: python scripts/style_stats.py matches/*.pgn [--engine Capablanca] [--show N]

Per game, from Capablanca's side:
  sacrifice  - a Capablanca move after which its material, measured 4 plies later
               and still 6 plies later, is >= 2 pawns lower than before the move.
  down-material win - a win in which Capablanca was >= 2 pawns behind for 4+ plies.
  mate win   - a win that ended in checkmate.
"""
import argparse
import glob
import chess
import chess.pgn

VAL = {chess.PAWN: 1, chess.KNIGHT: 3, chess.BISHOP: 3, chess.ROOK: 5, chess.QUEEN: 9, chess.KING: 0}


def material(board, color):
    s = 0
    for pt, v in VAL.items():
        s += v * (len(board.pieces(pt, color)) - len(board.pieces(pt, not color)))
    return s


def analyse(game, engine):
    white = game.headers.get("White", "")
    color = chess.WHITE if white == engine else chess.BLACK
    board = game.board()
    mats, movers, sans = [], [], []
    for move in game.mainline_moves():
        mats.append(material(board, color))
        movers.append(board.turn)
        sans.append(board.san(move))
        board.push(move)
    mats.append(material(board, color))
    n = len(mats) - 1
    sacs = []
    for i in range(n):
        if movers[i] != color:
            continue
        before = mats[i]
        if i + 6 <= n and mats[i + 4] <= before - 2 and mats[i + 6] <= before - 2:
            sacs.append((i, sans[i], before - mats[i + 4]))
    # dedupe sacrifices in the same sequence (keep first within 6 plies)
    deduped = []
    for s in sacs:
        if not deduped or s[0] - deduped[-1][0] > 6:
            deduped.append(s)
    run = best_run = 0
    deepest = 0
    for m in mats:
        run = run + 1 if m <= -2 else 0
        best_run = max(best_run, run)
        deepest = min(deepest, m)
    result = game.headers.get("Result", "*")
    won = (result == "1-0" and color == chess.WHITE) or (result == "0-1" and color == chess.BLACK)
    lost = (result == "0-1" and color == chess.WHITE) or (result == "1-0" and color == chess.BLACK)
    return dict(won=won, lost=lost, sacs=deduped, down_run=best_run, deepest=deepest,
                mate=board.is_checkmate(), plies=n, color=color)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pgns", nargs="+")
    ap.add_argument("--engine", default="Capablanca")
    ap.add_argument("--show", type=int, default=3)
    args = ap.parse_args()
    files = [f for p in args.pgns for f in glob.glob(p)]
    games = []
    for f in files:
        with open(f) as fh:
            while (g := chess.pgn.read_game(fh)) is not None:
                if args.engine in (g.headers.get("White"), g.headers.get("Black")):
                    games.append((f, g, analyse(g, args.engine)))
    if not games:
        print("no games")
        return
    W = sum(a["won"] for _, _, a in games)
    L = sum(a["lost"] for _, _, a in games)
    D = len(games) - W - L
    sac_games = [a for _, _, a in games if a["sacs"]]
    sac_wins = [a for a in sac_games if a["won"]]
    sac_losses = [a for a in sac_games if a["lost"]]
    down_wins = [a for _, _, a in games if a["won"] and a["down_run"] >= 4]
    mates = [a for _, _, a in games if a["won"] and a["mate"]]
    total_sacs = sum(len(a["sacs"]) for _, _, a in games)
    print(f"games {len(games)}  W {W}  L {L}  D {D}")
    print(f"sacrifices: {total_sacs} total, in {len(sac_games)} games "
          f"({100 * len(sac_games) / len(games):.0f}%) -> won {len(sac_wins)}, lost {len(sac_losses)}")
    print(f"wins while >=2 pawns down for 4+ plies: {len(down_wins)} ({100 * len(down_wins) / max(1, W):.0f}% of wins)")
    print(f"wins by checkmate: {len(mates)} ({100 * len(mates) / max(1, W):.0f}% of wins)")
    # showcase: wins with the deepest sustained deficit
    showcase = sorted([x for x in games if x[2]["won"] and x[2]["sacs"]],
                      key=lambda x: (x[2]["deepest"], -x[2]["down_run"]))[: args.show]
    for f, g, a in showcase:
        side = "White" if a["color"] == chess.WHITE else "Black"
        sac_str = ", ".join(f"{s[0] // 2 + 1}{'.' if s[0] % 2 == 0 else '...'}{s[1]} (-{s[2]})" for s in a["sacs"])
        print(f"\n--- {g.headers.get('White')} vs {g.headers.get('Black')} {g.headers.get('Result')} "
              f"({side}, deepest deficit {a['deepest']}, {'mate' if a['mate'] else 'conversion'})")
        print(f"    opening FEN: {g.headers.get('FEN', 'startpos')}")
        print(f"    sacrifices: {sac_str}")
        print("    " + str(g.mainline_moves()))


if __name__ == "__main__":
    main()
