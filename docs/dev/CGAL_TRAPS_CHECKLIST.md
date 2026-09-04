# CGAL 6.1 traps — actionable checklist for arr2d implementers

Distilled from the compile-verified findings in `docs/dev/cgal61_api/` (see the named files for
proof and details). Every item says WHAT TO DO in our code.

## Process / build
- Release flags: `-O2 -DNDEBUG -DCGAL_DEBUG` (C `assert()` off, CGAL checks ON). `-DCGAL_NO_ASSERTIONS`
  alone does not compile; never use `-ffast-math`/`-Ofast`; all TUs must share identical macros
  (`CGAL_HAS_THREADS` changes `sizeof(Lazy_rep)` → silent ABI break). [build_and_abi_contract.md]
- Any object holding a `CORE::Expr` (Bezier/conic points, curves, traits, caches, adaptors) in static
  storage aborts at exit (`! blocks.empty()`, CORE MemoryPool). Heap-allocate and leak all such
  singletons (`static T* p = new T();`). Python objects holding such values are destroyed before
  interpreter teardown of statics — verify a clean `python -c "import arrangement_2d; ..."` exit for
  bezier/conic; if it aborts, keep a leaked global registry alive or register `Py_AtExit` to leak.
- Install a silent `CGAL::set_error_handler` at module init (the default prints to stderr before
  throwing). `CONTINUE` behaviour no longer exists: CGAL always throws. [number_types_and_errors.md]
- Never background a test binary without `timeout` (an orphaned probe ate 14 GB in this project).

## Numbers / coordinates
- Never use `CGAL::to_double(Epeck::FT)` or `Approximate_2` for user-visible doubles: not correctly
  rounded. Use `to_double_correctly_rounded` / `interval_of` from `impl/number_conv.hpp`.
- `Sqrt_extension::is_extended()==true` can still be rational (perfect-square root). Use
  `box_sqrt_extension` (normalises). CORE has no safe rationality test for `Expr`.
- CORE::Expr comparisons can violate transitivity on conic vertex coordinates (false EQUAL against a
  rational convergent). Document; never build logic on `Expr == rational`.

## Arrangement core
- Handles from raw pointers must never be incremented. Vertices at infinity / fictitious halfedges /
  the fictitious face are invisible to the iterators; reach them via CCB walks; guard `point()` /
  `curve()` with `is_at_open_boundary()` / `is_fictitious()` (they assert on null; UB under NDEBUG).
- `unbounded_face()` under the unbounded topology returns an arbitrary, drifting face: use
  `unbounded_faces_begin/end`. A vertex at infinity can be on a left/right AND top/bottom boundary.
- Inserting an unbounded curve (line/ray) that OVERLAPS an existing edge with an unbounded left end
  aborts (`cv.has_left()` precondition). Detect overlaps first (zone + traits intersect) and raise
  `Unsupported` for the Linear kind.
- `insert_at_vertices(cv, v1, v2)` frees v1's isolated-vertex record before validating: check our
  preconditions first (v1/v2 valid, curve endpoints equal the vertex points).
- Observer traces: `before_merge_face(f1,f2,e)` does not name the survivor (erase both, re-track in
  `after_merge_face`); inside `before_split_face(f,e)` `e->face()`/`e->twin()->face()` SEGFAULT —
  block `he_face` for that halfedge (and twin) while dispatching that event to Python; in
  `before_detach` never touch geometry (the destructor freed it); aggregate insert brackets the whole
  range with one global-change pair; overlay fires `before/after_clear` on the result, no global-change.
  `after_split_edge(e1,e2)`: e1 is the original object (keeps data), e2 new; `after_merge_edge` keeps e1.
- Exceptions thrown by CGAL during a sweep (aggregate insert, overlay, decompose, batched locate, BSO)
  leak sweep memory and overlay leaves the const inputs with corrupted `inc()` marks. Callbacks must
  never throw (Cython records exceptions). Document that a `PreconditionError` escaping a sweep may
  leave the arrangement invalid.
- Point location: `locate`/`ray_shoot` return an arbitrary twin; Python `Halfedge.__eq__` must be by
  identity but tests must compare `edge_id`. Triangulation PL silently returns the wrong face for
  faces with holes → do NOT expose (unsupported). Landmarks needs `Construct_x_monotone_curve_2(p,q)`:
  unavailable for circle_segment, bezier, polyline. Multi-attach and mutation while attached are safe.
- zone(): use the 4-arg overload with a PL object (3-arg returns an unadvanced iterator); visitor
  results must be `std::variant` of NON-const handles.

## Sphere kind
- Only `Arr_naive_point_location` and batched `CGAL::locate` are safe (walk/simple do not compile;
  RIC aborts with pole/identification vertices; landmarks aborts on antipodal pairs). Default = naive.
- `decompose` aborts if any vertex is at a pole or on the identification curve → `Unsupported`.
- Incremental insert / zone / do_intersect of a curve lying ON the identification meridian aborts once
  the arrangement crosses it → prefer aggregate insertion; document.
- `remove_vertex` / `remove_isolated_vertex` on an isolated pole or identification vertex leaves
  dangling topology pointers (next insert SIGSEGVs) → refuse (Unsupported) when
  `parameter_space_in_x/y != INTERIOR`.
- `Construct_point_2` is the ONLY safe way to build points (direct ctor with a wrong location
  silently corrupts). Points are unnormalised directions; the spherical face has 0 outer ccbs and
  contains the north pole; batched locate at the south pole is wrong; zone misses the north pole as MAX end.
- Overlay works (non-indexed sweep branch); the final `create_face` gives the spherical faces.

## Bezier kind
- `Point_2::x()/y()` need `make_exact(cache)` first (own leaked `Bezier_cache`). `parameter_range()` is
  unreliable until endpoints are exact. Keep every `Curve_2` rep alive (cache keyed by rep address).
- `Split_2` silently accepts a rational point NOT on the curve → verify `is_in_x_range && compare_y_at_x==0`
  before splitting. Intersecting two x-monotone pieces of the same `Curve_2` with equal `xid` reports
  no overlap. No `Approximate_2`: implement sampling ourselves; endpoint accuracy of `sample` is poor.
- `Construct_opposite_2::operator()` is non-const; `Merge_2`/`Trim_2` from the traits only.

## Conic kind
- `Construct_curve_2` negates/integerises coefficients; invalid arcs are returned silently → check
  `is_valid()`. Only traits functors; `Conic_arc_2::bbox()` is UB for full conics.
- Hyperbolic supporting conics: CGAL swaps sin/cos in `build_hyperbolic_arc_data`. Exact predicate
  (traits_conic.md §13): with `(R,S,T) = sign(N)*(r,s,t)`, `N = (4rs-t^2)w - s u^2 - r v^2 + t u v`,
  the arc is SAFE iff `T == 0` or `sign(P*sqrt((R-S)^2+T^2) - E) <= 0` with `P = R+S`,
  `E = T^2-(R-S)^2` (evaluate exactly, e.g. via `CORE::Expr` or squaring with sign analysis).
  UNSAFE arcs must be refused (`Unsupported`) unless `conic_allow_hyperbolic()`; unsafe arcs that
  build without asserting give WRONG predicates.
- `Approximate_curve_length_2` is unusable; compute length from the approximation.

## Circle-segment kind
- Coordinates are `Sqrt_extension` (`a0/a1/root`), not `_One_root_number`. Prefer the radius ctor
  (rational tangency points). No `Construct_x_monotone_curve_2` → no landmarks, no `convert_polygon`.

## Polyline kind
- Never copy-assign the traits (double free). `Curve_2` is `internal::Polycurve_2`; broken push_back
  overloads; lossy `<<`/`>>`. `Number_of_points_2` hides overloads.

## Rendering / approximation
- `Approximate_2` curve overloads need a `std::back_insert_iterator` and `error > 0` (`<= 0` segfaults
  or hangs; sphere `error > 2` gives NaN) → validate `tolerance` (clamp to `(1e-12, ...]`, sphere `<= 2`).
  `error` is ignored by segment/polyline traits; both endpoints are always emitted.
- Unbounded faces' outer CCBs may contain several fictitious runs → render as list of chains.

## Boolean set operations
- Orientation is a precondition (CCW outer, CW holes), never fixed; aggregated `do_intersect` is
  inverted (binary only); binary ops usually REPLACE the internal arrangement (never cache handles);
  user data cannot survive the stock ops; `construct_polygon` on a contained face gives CW.
- `Polygon_2` free functions silently convert through polyline traits (use the GPS classes directly).
