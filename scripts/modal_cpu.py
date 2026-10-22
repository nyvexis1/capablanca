"""Self-play tests, Stockfish matches and NNUE datagen on rented Modal CPU cores.

Engines are built on the rented machine (-march=native) from git refs of this repository, so
tests compare committed versions; "WORKTREE" means the current files instead.

From Git Bash (first `export MSYS_NO_PATHCONV=1 PYTHONIOENCODING=utf-8`):
  modal run scripts/modal_cpu.py::selfplay --a WORKTREE --b b5a0812 --games 3000 --cores 32
  modal run scripts/modal_cpu.py::match --ref b5a0812 --elo 3190 --games 400 --cores 32
  modal run scripts/modal_cpu.py::datagen --ref b5a0812 --minutes 60 --cores 32
  modal run scripts/modal_cpu.py::speed --ref WORKTREE --cores 8        # cheap benchmark
Cost: Modal bills physical cores, about $0.047 per core-hour (October 2026): 32 cores ~ $1.50/h.
"""
import io
import os
import subprocess
import tarfile
import time

import modal

app = modal.App("capablanca-cpu")
image = (
    modal.Image.debian_slim(python_version="3.12")
    .apt_install("g++", "make", "git", "wget")
    .run_commands(
        # same fastchess and Stockfish versions as tools/ on the Windows machine
        "git clone https://github.com/Disservin/fastchess /opt/fastchess && cd /opt/fastchess"
        " && git checkout f618e34 && make -j8 && cp fastchess /usr/local/bin/",
        "git clone --depth 1 --branch sf_18 https://github.com/official-stockfish/Stockfish /opt/sf"
        " && cd /opt/sf/src && make -j8 build ARCH=x86-64-avx2 && cp stockfish /usr/local/bin/",
    )
    .add_local_file("tools/books/8moves_v3.pgn", "/opt/book.pgn")
)
volume = modal.Volume.from_name("capablanca-data", create_if_missing=True)
FILES = ["src", "nets/default.nnue"]


def source_tar(ref: str) -> bytes:
    """The engine sources at a git ref (or the working tree) as a tar archive."""
    if ref == "WORKTREE":
        buf = io.BytesIO()
        with tarfile.open(fileobj=buf, mode="w") as tar:
            for f in FILES:
                tar.add(f)
        return buf.getvalue()
    return subprocess.run(["git", "archive", "--format=tar", ref, *FILES], check=True, capture_output=True).stdout


def build(tar: bytes, where: str) -> str:
    os.makedirs(where, exist_ok=True)
    with tarfile.open(fileobj=io.BytesIO(tar)) as t:
        t.extractall(where, filter="data")
    nnue = os.path.join(where, "src", "nnue.cpp")
    with open(nnue) as f:  # versions before the Linux port used a Windows-only section name
        code = f.read()
    with open(nnue, "w") as f:
        f.write(code.replace('".section .rdata,\\"dr\\"\\n"', '".section .rodata\\n"'))
    exe = os.path.join(where, "capablanca")
    srcs = sorted(os.path.join("src", s) for s in os.listdir(os.path.join(where, "src")) if s.endswith(".cpp"))
    subprocess.run(["g++", '-DNNUE_EMBED="nets/default.nnue"', "-O3", "-march=native", "-flto=auto", "-std=c++20",
                    "-pthread", "-DNDEBUG", *srcs, "-o", exe], cwd=where, check=True)
    print(subprocess.run([exe, "bench"], capture_output=True, text=True).stdout.strip().splitlines()[-1], flush=True)
    return exe


def fastchess(args: list[str]) -> str:
    """Run fastchess, echo progress lines, return the final summary."""
    p = subprocess.Popen(["fastchess", *args], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    summary = []
    for line in p.stdout:
        if line.startswith(("Elo:", "Games:", "LLR:", "SPRT", "Ptnml")) or "loses on time" in line:
            print(line.rstrip(), flush=True)
            summary = (summary + [line.rstrip()])[-5:]
    p.wait()
    return "\n".join(summary)


def book(games: int, conc: int) -> list[str]:
    return ["-openings", "file=/opt/book.pgn", "format=pgn", "order=random", "-rounds", str(games // 2),
            "-games", "2", "-repeat", "-concurrency", str(conc), "-recover", "-ratinginterval", "200"]


@app.function(image=image, timeout=8 * 3600)
def run_selfplay(a: bytes, b: bytes, games: int, tc: str, elo0: float, elo1: float, conc: int) -> str:
    ea, eb = build(a, "/w/a"), build(b, "/w/b")
    return fastchess(["-engine", f"cmd={ea}", "name=A", "-engine", f"cmd={eb}", "name=B",
                      "-each", f"tc={tc}", "option.Hash=16", *book(games, conc),
                      "-sprt", f"elo0={elo0}", f"elo1={elo1}", "alpha=0.05", "beta=0.05"])


@app.function(image=image, timeout=8 * 3600)
def run_match(src: bytes, elo: int, games: int, tc: str, conc: int) -> str:
    e = build(src, "/w/a")
    return fastchess(["-engine", f"cmd={e}", "name=Capablanca",
                      "-engine", "cmd=stockfish", f"name=SF{elo}", "option.UCI_LimitStrength=true", f"option.UCI_Elo={elo}",
                      "option.Move Overhead=100", "-each", f"tc={tc}", "option.Hash=16", "option.Threads=1",
                      *book(games, conc)])


@app.function(image=image, timeout=8 * 3600, volumes={"/data": volume})
def run_datagen(src: bytes, minutes: float, nodes: int, workers: int, outdir: str) -> str:
    e = build(src, "/w/a")
    os.makedirs(f"/data/{outdir}", exist_ok=True)
    base = int(time.time())
    procs = [subprocess.Popen(["timeout", f"{int(minutes * 60)}s", e, "datagen", f"/data/{outdir}/m{base}_{i}.bin",
                               str(base * 1000 + i), str(nodes)], stdout=subprocess.DEVNULL) for i in range(workers)]
    for p in procs:
        p.wait()
    volume.commit()
    total = sum(os.path.getsize(f"/data/{outdir}/m{base}_{i}.bin") // 32 for i in range(workers))
    return f"{total} positions from {workers} workers in {minutes} min -> {total / minutes * 60 / 1e6:.2f}M/h"


@app.function(image=image, timeout=3600)
def run_speed(src: bytes, seconds: int, workers: int) -> str:
    """Datagen speed without saving anything: how many positions per core-hour."""
    e = build(src, "/w/a")
    procs = [subprocess.Popen(["timeout", f"{seconds}s", e, "datagen", f"/tmp/s{i}.bin", str(i + 1), "5000"],
                              stdout=subprocess.DEVNULL) for i in range(workers)]
    for p in procs:
        p.wait()
    total = sum(os.path.getsize(f"/tmp/s{i}.bin") // 32 for i in range(workers))
    return (f"{total} positions in {seconds}s on {workers} workers -> {total / seconds * 3600 / 1e6:.2f}M/h total, "
            f"{total / seconds * 3600 / workers / 1e6:.3f}M/h per core")


@app.local_entrypoint()
def selfplay(a: str = "WORKTREE", b: str = "HEAD", games: int = 3000, tc: str = "10+0.1",
             elo0: float = 0, elo1: float = 10, cores: int = 32):
    print(run_selfplay.with_options(cpu=cores).remote(source_tar(a), source_tar(b), games, tc, elo0, elo1, cores))


@app.local_entrypoint()
def match(ref: str = "HEAD", elo: int = 3190, games: int = 400, tc: str = "10+0.1", cores: int = 32):
    print(run_match.with_options(cpu=cores).remote(source_tar(ref), elo, games, tc, cores))


@app.local_entrypoint()
def datagen(ref: str = "HEAD", minutes: float = 60, nodes: int = 5000, cores: int = 32, outdir: str = "nnue"):
    print(run_datagen.with_options(cpu=cores).remote(source_tar(ref), minutes, nodes, cores, outdir))


@app.local_entrypoint()
def speed(ref: str = "WORKTREE", seconds: int = 300, cores: int = 8):
    print(run_speed.with_options(cpu=cores).remote(source_tar(ref), seconds, cores))
