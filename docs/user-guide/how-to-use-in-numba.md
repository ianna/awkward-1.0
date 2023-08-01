---
jupytext:
  text_representation:
    extension: .md
    format_name: myst
    format_version: 0.13
    jupytext_version: 1.10.3
kernelspec:
  display_name: Python 3
  language: python
  name: python3
---

Using arrays in Numba
=====================

```{code-cell} ipython3
import awkward as ak
import numba
import numpy as np

ak.numba.register_and_check()
```

Passing an array to a function
------------------------------

Awkward Arrays can be passed to a JIT-ed Numba functions:

```{code-cell} ipython3
layout = ak.contents.NumpyArray(
  np.array([0.0, 1.1, 2.2, 3.3]),
  parameters={"some": "stuff", "other": [1, 2, "three"]},
)

@numba.njit
def f(out, obj):
  out[0] = len(obj)
  out[1] = obj[1]
  out[2] = obj[3]

  out = np.zeros(3, dtype=np.float64)
  f(out, ak.Array(layout))
  assert out.tolist() == [4.0, 1.1, 3.3]
```
