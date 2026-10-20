"""Cross-check training features against the engine: the dequantised net evaluated with the
trainer's feature code must match `capablanca eval` on the same positions (within rounding).

Usage: python scripts/nnue/check.py <net.nnue> <data.bin> [engine.exe] [n]
"""
import subprocess
import sys
import numpy as np
import torch

sys.path.insert(0, "scripts/nnue")
sys.path.insert(0, "scripts")
import train  # noqa: E402
from read_nnue_data import decode  # noqa: E402


def load_net(path):
    raw = open(path, "rb").read()
    assert raw[:8] == b"CAPANNUE"
    h = int(np.frombuffer(raw, np.int32, 1, 8)[0])
    off = 12
    w0 = np.frombuffer(raw, np.int16, 768 * h, off).reshape(768, h); off += 768 * h * 2
    b0 = np.frombuffer(raw, np.int16, h, off); off += h * 2
    w1 = np.frombuffer(raw, np.int16, 2 * h, off); off += 2 * h * 2
    b1 = np.frombuffer(raw, np.int32, 1, off)[0]
    net = train.Net()
    with torch.no_grad():
        net.ft.weight.copy_(torch.tensor(w0.T / train.QA, dtype=torch.float32))
        net.ft.bias.copy_(torch.tensor(b0 / train.QA, dtype=torch.float32))
        net.out.weight.copy_(torch.tensor(w1 / train.QB, dtype=torch.float32).unsqueeze(0))
        net.out.bias.fill_(float(b1) / (train.QA * train.QB))
    return net


def main():
    netfile, datafile = sys.argv[1], sys.argv[2]
    exe = sys.argv[3] if len(sys.argv) > 3 else "./capablanca.exe"
    n = int(sys.argv[4]) if len(sys.argv) > 4 else 20
    data = np.fromfile(datafile, dtype=train.REC)[:n * 50:50]
    net = load_net(netfile)
    us, them = train.features(data, "cpu")
    with torch.no_grad():
        py = (net(us, them) * train.SCALE).numpy()
    cmds = [f"setoption name EvalFile value {netfile}"]
    for rec in data:
        board, _, _ = decode(rec.tobytes())
        cmds += [f"position fen {board.fen()}", "eval"]
    out = subprocess.run([exe], input="\n".join(cmds + ["quit"]) + "\n", capture_output=True, text=True).stdout
    eng = [int(l.split()[-1]) for l in out.splitlines() if l.startswith("nnue (side to move)")]
    diffs = [abs(a - b) for a, b in zip(py, eng)]
    for a, b in list(zip(py, eng))[:8]:
        print(f"python {a:8.1f}   engine {b:6d}")
    print(f"{len(eng)} positions, max |diff| {max(diffs):.1f} cp, mean {np.mean(diffs):.2f} cp")


if __name__ == "__main__":
    main()
