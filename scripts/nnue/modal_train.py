"""Run scripts/nnue/train.py on a Modal H100.

One-time setup (your Modal account):
  pip install modal
  modal setup                                   # opens the browser to log in

Each training run (from Git Bash, first `export MSYS_NO_PATHCONV=1` or remote paths get mangled):
  modal volume create capablanca-data           # first time only
  modal volume put capablanca-data data/nnue/ /nnue
  modal run scripts/nnue/modal_train.py --epochs 30
  modal volume get capablanca-data capablanca.nnue nets/capablanca.nnue
"""
import modal

app = modal.App("capablanca-nnue")
image = (
    modal.Image.debian_slim(python_version="3.12")
    .pip_install("torch==2.7.1", "numpy==2.2.6")
    .add_local_file("scripts/nnue/train.py", "/root/train.py")
)
volume = modal.Volume.from_name("capablanca-data", create_if_missing=True)


@app.function(gpu="H100", image=image, volumes={"/data": volume}, timeout=8 * 3600, memory=65536)
def train(epochs: int, batch: int, lr: float, lam: float, out: str, data: str, hidden: int):
    import subprocess

    subprocess.run(
        ["python", "/root/train.py", *[f"/data/{d}/*.bin" for d in data.split(",")], "--out", f"/data/{out}",
         "--epochs", str(epochs), "--batch", str(batch), "--lr", str(lr), "--lam", str(lam), "--hidden", str(hidden)],
        check=True,
    )
    volume.commit()


@app.local_entrypoint()
def main(epochs: int = 30, batch: int = 16384, lr: float = 1e-3, lam: float = 1.0, out: str = "capablanca.nnue",
         data: str = "nnue", hidden: int = 256, gpu: str = "H100"):
    # data: comma-separated volume folders; gpu: e.g. L4 (cheaper) or H100
    train.with_options(gpu=gpu).remote(epochs, batch, lr, lam, out, data, hidden)
