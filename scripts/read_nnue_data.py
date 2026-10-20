"""Decode Capablanca NNUE training records (see src/datagen.cpp) and print a summary.

Usage: python scripts/read_nnue_data.py data/nnue/*.bin [--show 5]
"""
import glob
import struct
import sys
import chess

REC = struct.Struct("<Q16sBBHhBB")
PIECES = "PNBRQK"


def decode(raw):
    occ, pcs, stm, half, full, score, result, _ = REC.unpack(raw)
    board = chess.Board(None)
    i = 0
    for sq in range(64):
        if occ >> sq & 1:
            nib = (pcs[i // 2] >> (4 * (i % 2))) & 15
            sym = PIECES[nib & 7]
            board.set_piece_at(sq, chess.Piece.from_symbol(sym.lower() if nib & 8 else sym))
            i += 1
    board.turn = chess.WHITE if stm == 0 else chess.BLACK
    board.halfmove_clock, board.fullmove_number = half, full
    return board, score, result


def main():
    args = sys.argv[1:]
    show = 0
    if "--show" in args:
        i = args.index("--show"); show = int(args[i + 1]); args = args[:i] + args[i + 2:]
    files = [f for p in args for f in glob.glob(p)]
    n = bad = 0
    res = [0, 0, 0]
    agree = disagree = 0
    for f in files:
        data = open(f, "rb").read()
        for off in range(0, len(data) - len(data) % 32, 32):
            board, score, result = decode(data[off:off + 32])
            n += 1
            res[result] += 1
            if not board.is_valid():
                bad += 1
            if abs(score) > 200 and result != 1:
                if (score > 0) == (result == 2): agree += 1
                else: disagree += 1
            if n <= show:
                print(board.fen(), score, ["0-1", "1/2", "1-0"][result])
    print(f"{n} positions, {bad} invalid boards, results b/d/w {res}, "
          f"score sign agrees with decisive result {agree}/{agree + disagree}")


if __name__ == "__main__":
    main()
