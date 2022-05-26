# %%

import multiprocessing
import sys  # sys.getsizeof()

from IPython.display import display
from IPython.display import HTML
from IPython.display import Markdown
import matplotlib.pyplot as plt
import numpy as np
import scipy.stats as st


# https://ipython.readthedocs.io/en/stable/interactive/magics.html#magic-matplotlib
# To open matplotlib in interactive mode
# %matplotlib qt5
# %matplotlib notebook
# For VSCode Interactive
#! %matplotlib widget
# %matplotlib gui
# For VSCode No Interactive
# %matplotlib inline
# %matplotlib --list

# %%

rng = np.random.default_rng()

plt.figure(figsize=(12, 12))
num_elems = list(range(256))
legends = []
legends.append("Tuple")
plt.plot(num_elems, [sys.getsizeof(tuple(range(n))) for n in num_elems])
legends.append("List")
plt.plot(num_elems, [sys.getsizeof(list(range(n))) for n in num_elems])
legends.append("Set")
plt.plot(num_elems, [sys.getsizeof(set(range(n))) for n in num_elems], "x")
legends.append("Set-Rand")
set_sizes = [sys.getsizeof(set(rng.random(size=n))) for n in num_elems]
plt.plot(num_elems, set_sizes)
legends.append("numpy.array")
plt.plot(
    num_elems, [sys.getsizeof(np.array(range(n))) for n in num_elems], "--"
)
legends.append("numpy.array(dtype=int)")
plt.plot(
    num_elems,
    [sys.getsizeof(np.array(range(n), dtype=int)) for n in num_elems],
    "-.",
)
legends.append("numpy.array(dtype=numpy.float64)")
plt.plot(
    num_elems,
    [sys.getsizeof(np.array(range(n), dtype=np.float64)) for n in num_elems],
    ",",
)
legends.append("numpy.array(dtype=numpy.float32)")
plt.plot(
    num_elems,
    [sys.getsizeof(np.array(range(n), dtype=np.float32)) for n in num_elems],
)
legends.append("numpy.array(dtype=complex)")
plt.plot(
    num_elems,
    [sys.getsizeof(np.array(range(n), dtype=complex)) for n in num_elems],
)
legends.append("numpy.array(dtype=bool)")
plt.plot(
    num_elems,
    [sys.getsizeof(np.array(range(n), dtype=bool)) for n in num_elems],
    "x",
)
legends.append("numpy.array(dtype=numpy.uint8)")
plt.plot(
    num_elems,
    [sys.getsizeof(np.array(range(n), dtype=np.uint8)) for n in num_elems],
)
plt.xscale("log")
plt.yscale("log")
plt.title("Number of Elements vs Memory Usage")
plt.ylabel("sys.getsizeof(container) in Bytes")
plt.xlabel("Number of Elements")
plt.legend(legends)
# plt.savefig("data-container-sizes.svg")
plt.savefig("data-container-sizes.png")
plt.show()
