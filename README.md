# arrangement_2d

Python bindings (Cython) for the [CGAL](https://www.cgal.org) **2D Arrangements** package
(`Arrangement_on_surface_2`, CGAL ≥ 6.0).

An *arrangement* is the planar subdivision induced by a set of curves: vertices, edges and
faces stored in a doubly-connected edge list (DCEL) that you can traverse and edit
(insert / split / merge / remove), query (point location, ray shooting, zone, vertical
decomposition), overlay with other arrangements, and combine through 2D Boolean set
operations. Everything is computed **exactly** (rational / algebraic arithmetic).

## Supported geometry kinds

| kind             | curves                                   | topology         | Boolean ops |
|------------------|------------------------------------------|------------------|-------------|
| `segment`        | line segments                            | bounded plane    | yes         |
| `linear`         | segments, rays, lines                    | unbounded plane  | –           |
| `circle_segment` | circular arcs, full circles, segments    | bounded plane    | yes         |
| `polyline`       | polylines                                | bounded plane    | –           |
| `bezier`         | polynomial Bézier curves (any degree)    | bounded plane    | yes         |
| `conic`          | conic arcs (ellipses, parabolas, circles, segments; rational quadratic Bézier) | bounded plane | yes |
| `sphere`         | geodesic arcs on the unit sphere         | sphere           | –           |

Every arrangement keeps its **history** (which input curve induced which edge), supports
per-element Python **data**, **observers**, **overlay** with callbacks and several point
location strategies. See `docs/` for the user guide and `docs/dev/DESIGN.md` for the design.

## Quick start

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
```

## Building

Requirements: a C++17 compiler, CGAL ≥ 6.0 headers, GMP, MPFR, Boost headers, Python ≥ 3.10,
Cython ≥ 3. On macOS: `brew install cgal gmp mpfr boost`.

```
pip install -e .          # or: python setup.py build_ext --inplace -j 8
pytest
```

See `setup.py` for the environment variables that control discovery and build flags.
