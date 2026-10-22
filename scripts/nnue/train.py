"""Train Capablanca's NNUE: (768 -> 256) x 2 perspectives -> 1, SCReLU.

Reads the 32-byte records written by `capablanca datagen` (see src/datagen.cpp) and writes a
quantised network that src/nnue.cpp loads.

Target: WDL-blended   t = lam * sigmoid(score / SCALE) + (1 - lam) * result
Loss:   mean |sigmoid(net) - t| ^ 2.6 (from the side to move's point of view)

Usage:
  python scripts/nnue/train.py data/nnue/*.bin --out nets/capablanca.nnue [--epochs 30]
"""
import argparse
import glob
import math
import os
import time

import numpy as np
import torch

HIDDEN = 256
SCALE = 400  # centipawns per sigmoid unit, must match src/nnue.cpp
QA, QB = 255, 64  # quantisation, must match src/nnue.cpp

REC = np.dtype([("occ", "<u8"), ("pcs", "u1", 16), ("stm", "u1"), ("half", "u1"),
                ("move", "<u2"), ("score", "<i2"), ("result", "u1"), ("format", "u1")])  # move: fullmove in format 0
assert REC.itemsize == 32


def load(paths, val_frac):
    """Returns (train, val). Each file holds whole games back to back, so the last val_frac of
    every file goes to validation: held-out games, not positions from games seen in training."""
    # count= skips a partial record at the end of a file (a worker stopped mid-write)
    arrays = [np.fromfile(p, dtype=REC, count=os.path.getsize(p) // REC.itemsize) for p in paths]
    arrays = [a for a in arrays if len(a)]
    cuts = [len(a) - max(1, int(len(a) * val_frac)) for a in arrays]
    train = np.concatenate([a[:c] for a, c in zip(arrays, cuts)])
    val = np.concatenate([a[c:] for a, c in zip(arrays, cuts)])
    return train, val


def features(batch, device):
    """Dense 768-wide inputs for the side to move (us) and the other side (them)."""
    n = len(batch)
    occ = torch.from_numpy(batch["occ"].view(np.int64).copy()).to(device)
    pcs = torch.from_numpy(batch["pcs"].copy()).to(device)
    stm = torch.from_numpy(batch["stm"].astype(np.int64)).to(device)

    bits = (occ.unsqueeze(1) >> torch.arange(64, device=device)) & 1          # n x 64
    nib = torch.stack([pcs & 15, pcs >> 4], dim=2).reshape(n, 32).long()      # n x 32 in square order
    idx = (torch.cumsum(bits, dim=1) - 1).clamp(min=0)                        # nibble index per square
    piece = torch.gather(nib, 1, idx)                                         # n x 64
    occupied = bits.bool()
    ptype, pcolor = piece & 7, piece >> 3
    sq = torch.arange(64, device=device).expand(n, 64)

    us_white = (stm == 0).unsqueeze(1)
    # Perspective of the side to move: own pieces first, board flipped for black
    own_us = pcolor == stm.unsqueeze(1)
    sq_us = torch.where(us_white, sq, sq ^ 56)
    f_us = (~own_us).long() * 384 + ptype * 64 + sq_us
    f_them = own_us.long() * 384 + ptype * 64 + (sq_us ^ 56)

    x_us = torch.zeros(n, 768, device=device)
    x_them = torch.zeros(n, 768, device=device)
    rows, cols = occupied.nonzero(as_tuple=True)
    x_us[rows, f_us[rows, cols]] = 1.0
    x_them[rows, f_them[rows, cols]] = 1.0
    return x_us, x_them


class Net(torch.nn.Module):
    def __init__(self):
        super().__init__()
        self.ft = torch.nn.Linear(768, HIDDEN)
        self.out = torch.nn.Linear(2 * HIDDEN, 1)

    def forward(self, us, them):
        a = torch.clamp(self.ft(us), 0, 1) ** 2
        b = torch.clamp(self.ft(them), 0, 1) ** 2
        return self.out(torch.cat([a, b], dim=1)).squeeze(1)


def targets(batch, device, lam):
    score = torch.from_numpy(batch["score"].astype(np.float32)).to(device)
    result = torch.from_numpy(batch["result"].astype(np.float32) / 2).to(device)  # white's view
    stm = torch.from_numpy(batch["stm"].astype(np.float32)).to(device)
    # flip to side to move
    score = torch.where(stm == 0, score, -score)
    result = torch.where(stm == 0, result, 1 - result)
    return lam * torch.sigmoid(score / SCALE) + (1 - lam) * result


def export(net, path):
    """Quantise: feature transformer *QA (int16), output weights *QB (int16), output bias *QA*QB (int32)."""
    w0 = torch.round(net.ft.weight.detach().cpu().T * QA).clamp(-32768, 32767).short().numpy()  # 768 x H
    b0 = torch.round(net.ft.bias.detach().cpu() * QA).clamp(-32768, 32767).short().numpy()
    w1 = torch.round(net.out.weight.detach().cpu().squeeze(0) * QB).clamp(-32768, 32767).short().numpy()
    b1 = np.array([round(float(net.out.bias.detach().cpu()) * QA * QB)], dtype=np.int32)
    with open(path, "wb") as f:
        f.write(b"CAPANNUE")
        f.write(np.array([HIDDEN], dtype=np.int32).tobytes())
        for a in (w0, b0, w1, b1):
            f.write(np.ascontiguousarray(a).tobytes())
    print(f"wrote {path}")


def main():
    global HIDDEN
    ap = argparse.ArgumentParser()
    ap.add_argument("data", nargs="+")
    ap.add_argument("--out", default="nets/capablanca.nnue")
    ap.add_argument("--epochs", type=int, default=30)
    ap.add_argument("--batch", type=int, default=16384)
    ap.add_argument("--lr", type=float, default=1e-3)
    ap.add_argument("--lam", type=float, default=0.75)
    ap.add_argument("--val", type=float, default=0.01, help="fraction held out for validation")
    ap.add_argument("--hidden", type=int, default=HIDDEN, help="feature transformer width (engine: -DNNUE_HIDDEN)")
    args = ap.parse_args()
    HIDDEN = args.hidden

    device = "cuda" if torch.cuda.is_available() else "cpu"
    paths = [f for p in args.data for f in glob.glob(p)]
    train, val = load(paths, args.val)
    rng = np.random.default_rng(1)
    print(f"{len(train)} training / {len(val)} validation positions on {device}")

    net = Net().to(device)
    opt = torch.optim.AdamW(net.parameters(), lr=args.lr, weight_decay=0.0)
    steps = math.ceil(len(train) / args.batch)
    sched = torch.optim.lr_scheduler.CosineAnnealingLR(opt, T_max=args.epochs * steps, eta_min=args.lr * 0.01)

    def batch_loss(b):
        us, them = features(b, device)
        pred = torch.sigmoid(net(us, them))  # net output is eval / SCALE
        return (torch.abs(pred - targets(b, device, args.lam)) ** 2.6).mean()

    for epoch in range(args.epochs):
        t0 = time.time()
        rng.shuffle(train)
        net.train()
        total = 0.0
        for i in range(steps):
            b = train[i * args.batch:(i + 1) * args.batch]
            loss = batch_loss(b)
            opt.zero_grad()
            loss.backward()
            opt.step()
            sched.step()
            # keep weights small enough that the int16 accumulator cannot overflow
            with torch.no_grad():
                net.ft.weight.clamp_(-1.98, 1.98)
                net.out.weight.clamp_(-1.98, 1.98)
            total += loss.item()
        net.eval()
        with torch.no_grad():
            vl = np.mean([batch_loss(val[j:j + args.batch]).item() for j in range(0, len(val), args.batch)])
        print(f"epoch {epoch + 1}/{args.epochs}  train {total / steps:.5f}  val {vl:.5f}  ({time.time() - t0:.0f}s)",
              flush=True)
        export(net, args.out)


if __name__ == "__main__":
    main()
