# ml/

| Script | Purpose |
|---|---|
| `capture.py` | record one labeled session from the board (see `data/PROTOCOL.md`) |
| `dataset_status.py` | count sessions / independent windows per class and split vs. the lab minimums |

Python 3.12 pipeline: capture, features, baseline, training, export to C.

```sh
uv venv .venv --python 3.12
uv pip install --python .venv/bin/python -r ml/requirements.txt
```

Edit top-level deps in `requirements.in`, then regenerate the pins:
`uv pip compile ml/requirements.in -o ml/requirements.txt --python-version 3.12`

Note: emlearn 0.23 tree models take `int16_t` features, so quantize features before export (M5).
