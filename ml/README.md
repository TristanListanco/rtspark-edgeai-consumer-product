# ml/

Python 3.12 pipeline: capture, features, baseline, training, export to C.

```sh
uv venv .venv --python 3.12
uv pip install --python .venv/bin/python -r ml/requirements.txt
```

Edit top-level deps in `requirements.in`, then regenerate the pins:
`uv pip compile ml/requirements.in -o ml/requirements.txt --python-version 3.12`

Note: emlearn 0.23 tree models take `int16_t` features, so quantize features before export (M5).
