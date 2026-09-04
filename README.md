# arrangement_2d

Python bindings (Cython) for the [CGAL](https://www.cgal.org) **2D Arrangements** package
(`Arrangement_on_surface_2`, CGAL 6.1).

An *arrangement* is the planar subdivision induced by a set of curves: vertices, edges and
faces stored in a doubly-connected edge list (DCEL) that you can traverse and edit
(insert / split / merge / remove), query (point location, ray shooting, zone, vertical
decomposition), overlay with other arrangements, and combine through 2D Boolean set
operations. Everything is computed **exactly** — rational and algebraic arithmetic, no
tolerances, no degenerate case that silently goes the wrong way.

```python
import arrangement_2d as a2

arr = a2.Arrangement("segment")
arr.insert([a2.Segment((0, 0), (4, 0)), a2.Segment((4, 0), (4, 4)),
            a2.Segment((4, 4), (0, 4)), a2.Segment((0, 4), (0, 0)),
            a2.Segment((-1, 2), (5, 2))])

print(arr)                      # Arrangement(kind='segment', vertices=8, edges=9, faces=3, curves=5)

face = arr.locate((2, 1))       # -> Face
for he in face.outer_ccb():
    print(he.source.point, "->", he.target.point)

face.data = {"name": "lower half"}
print(a2.regions.face_area(face))                # 8 -- an exact Fraction
```

## Features

* **Seven geometry kinds**, one CGAL traits instantiation each — see the table below.
* **Exact arithmetic everywhere.** Integers, `float` (its exact binary value),
  `Fraction`, `Decimal` and numeric strings go in; `Fraction`, `SqrtExtension(a, b, c)`
  (= `a + b·√c`) or `Algebraic` (approximation + certified, refinable interval) come out.
  `.approx` gives correctly rounded `float`s whenever you want them.
* **Curve history** in every arrangement: which input curve induced which edge,
  which curves an edge originates from, and `remove_curve()`.
* **Python data on every element.** `vertex.data`, `halfedge.data`, `face.data` hold an
  arbitrary object; it survives `copy()` and can be produced by overlay callbacks, and
  reference cycles through it are still garbage-collected.
* **Observers** — a Python subclass notified of every DCEL change (create / modify /
  split / merge / move / remove of vertices, edges, faces and CCBs).
* **Overlay** with the full ten-callback `OverlayTraits` interface, or the `on_vertex` /
  `on_edge` / `on_face` shorthand.
* **Point location** with the naive, simple, walk, landmarks and trapezoid strategies
  (attachable and kept up to date), plus ray shooting, batched location, `zone()` and
  vertical decomposition.
* **2D Boolean set operations** (`PolygonSet`, `|`, `&`, `-`, `^`, `~`) for the four
  kinds CGAL supports them for.
* **Region helpers** (`arrangement_2d.regions`) — merge faces, split a face with a curve,
  connected components, face areas, region extraction by predicate, union outlines.
* **Plot helpers** (`arrangement_2d.plot`) — matplotlib rendering of arrangements,
  polygon sets and curve lists, imported lazily.
* **Safe handles.** A `Vertex` / `Halfedge` / `Face` / `CurveHandle` whose element is
  gone raises `InvalidHandleError` instead of crashing the interpreter, and CGAL's
  preconditions stay compiled in and become Python exceptions.

## Supported geometry kinds

| kind             | curves                                   | topology         | Boolean ops | coordinates |
|------------------|------------------------------------------|------------------|-------------|-------------|
| `segment`        | line segments                            | bounded plane    | yes         | rational |
| `linear`         | segments, rays, lines                    | unbounded plane  | –           | rational |
| `circle_segment` | circular arcs, full circles, segments    | bounded plane    | yes         | `a + b·√c` |
| `polyline`       | polylines                                | bounded plane    | –           | rational |
| `bezier`         | polynomial Bézier curves (any degree)    | bounded plane    | yes         | algebraic |
| `conic`          | conic arcs (ellipses, parabolas, hyperbolas, circles, segments; rational quadratic Bézier) | bounded plane | yes | algebraic |
| `sphere`         | geodesic arcs on the unit sphere         | sphere           | –           | rational directions |

## Installing

Requirements: a C++17 compiler, CGAL 6.1 headers, GMP, MPFR, Boost headers, Python ≥ 3.10,
Cython ≥ 3. On macOS: `brew install cgal gmp mpfr boost`.

```console
$ pip install -e .                 # or: python setup.py build_ext --inplace -j 8
$ pytest -q
```

`numpy` (bulk exports as arrays) and `matplotlib` (`arrangement_2d.plot`) are optional.
See `setup.py` for the environment variables that control library discovery and build
flags.

## Documentation

* **[docs/user_guide.md](docs/user_guide.md)** — concepts, every kind with worked
  examples, history, data, observers, overlay, point location, Boolean operations, the
  region and plot helpers, the exactness and error models, and the CGAL 6.1 limitations
  in user terms.
* **[docs/api_reference.md](docs/api_reference.md)** — every public class, method and
  function, generated from the docstrings by
  `python docs/gen_api_reference.py`.
* **[docs/dev/DESIGN.md](docs/dev/DESIGN.md)** — architecture: the type-erased C++ core,
  the handle/validity model, how Python data lives on DCEL elements.
* **[docs/dev/CGAL_TRAPS_CHECKLIST.md](docs/dev/CGAL_TRAPS_CHECKLIST.md)** — every CGAL
  6.1 trap this binding works around, with file and line.
* **[docs/dev/cgal61_api/](docs/dev/cgal61_api/)** — compile-verified maps of the CGAL 6.1
  API used here.

## A few more examples

Boolean operations on exact circular-arc regions:

```python
left = a2.PolygonSet("circle_segment")
left.insert(a2.Polygon([a2.Circle((0, 0), 2)]))
right = a2.PolygonSet("circle_segment")
right.insert(a2.Polygon([a2.Circle((2, 0), 2)]))

lens = left & right
print(lens.number_of_polygons_with_holes, lens.oriented_side((1, 0)))   # 1 1
```

Overlaying two arrangements and labelling every result face with the pair of input faces
it came from:

```python
other = a2.Arrangement("segment")
other.insert([a2.Segment((2, 2), (6, 2)), a2.Segment((6, 2), (6, 6)),
              a2.Segment((6, 6), (2, 6)), a2.Segment((2, 6), (2, 2))])

result = arr.overlay(other, on_face=lambda fa, fb: (fa.data, fb.data))
```

Merging every face that carries a label, region by region:

```python
from arrangement_2d import regions

for group in regions.extract_regions(arr, lambda f: f.data is not None):
    regions.merge_faces(arr, group)
```

Drawing:

```python
import matplotlib.pyplot as plt

a2.plot.plot_arrangement(arr, tolerance=1e-2, face_colors="data")
plt.show()
```
