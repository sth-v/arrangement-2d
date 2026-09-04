# arrangement_2d — Design

Python bindings (Cython) for the CGAL 6.1 *2D Arrangements* package
(`Arrangement_on_surface_2`), including arrangements with history, extended
DCEL user data, observers, overlay, point location, zone, vertical
decomposition and 2D Boolean set operations, for these geometry kinds:

| Kind id | Name             | CGAL traits                                                                 | Topology            | Point type                         |
|--------:|------------------|-----------------------------------------------------------------------------|---------------------|------------------------------------|
| 0       | `segment`        | `Arr_segment_traits_2<Epeck>`                                               | bounded planar      | `Epeck::Point_2` (lazy rational)   |
| 1       | `linear`         | `Arr_linear_traits_2<Epeck>` (segments, rays, lines)                        | unbounded planar    | `Epeck::Point_2`                   |
| 2       | `circle_segment` | `Arr_circle_segment_traits_2<Epeck>` (circular arcs + line segments)        | bounded planar      | `_One_root_point_2` (a+b*sqrt(c))  |
| 3       | `polyline`       | `Arr_polyline_traits_2<Arr_segment_traits_2<Epeck>>`                        | bounded planar      | `Epeck::Point_2`                   |
| 4       | `bezier`         | `Arr_Bezier_curve_traits_2<Cartesian<BigRat>, Cartesian<Expr>, CORE_nt>`    | bounded planar      | `_Bezier_point_2` (algebraic)      |
| 5       | `conic`          | `Arr_conic_traits_2<Cartesian<BigRat>, Cartesian<Expr>, CORE_nt>`           | bounded planar      | `Conic_point_2` (algebraic)        |
| 6       | `sphere`         | `Arr_geodesic_arc_on_sphere_traits_2<Epeck>` + `Arr_spherical_topology_traits_2` | sphere         | `Arr_extended_direction_3`         |

Every kind is wrapped as `Arrangement_with_history_2<Traits, Arr_extended_dcel<Traits, VData, HData, FData>>`
(planar) or `Arrangement_on_surface_with_history_2<Traits, Arr_spherical_topology_traits_2<Traits, Dcel>>` (sphere).
"With history" gives us, for free and with full CGAL support: the list of input curves,
the mapping input curve -> induced edges, edge -> originating curves, and `remove_curve`.

Rational Bézier curves: CGAL's Bézier traits only handle *polynomial* Bézier curves
(any degree, rational control points). Rational *quadratic* Bézier curves (3 control
points + weights) are exactly conic arcs and are supported exactly through the `conic`
kind (`ConicArc.from_rational_bezier`). Higher-degree rational Bézier curves are not
representable exactly without the algebraic-curve traits (out of scope for now); they can be
approximated (`BezierCurve.approximate_rational(...)` -> polynomial Bézier / polyline) with
an explicit tolerance.

## 1. Architecture

```
arrangement_2d/                Python package
  __init__.py                  public API re-exports, Kind enum, exceptions, helpers
  _core.pyx / _core.pxd        Cython binding of the type-erased C++ core (ONE module)
  regions.py                   high-level helpers (regions, merging faces, outlines)
  plot.py                      optional matplotlib helpers
src/arr2d/                     C++ core (no Python.h; talks to Python via function-pointer hooks)
  include/arr2d/
    common.hpp                 Kind, Rational, handles, Geom box, PyRef, errors, hooks
    ops.hpp                    KindOps + TraitsOps (type-erased per-kind geometry ops)
    arrangement.hpp            ArrBase (type-erased arrangement), Located, events
    polygon_set.hpp            PolygonSetBase (type-erased Boolean set operations)
    registry.hpp               kind registry: ops(kind), make_arrangement(kind)...
    numbers.hpp                rational/algebraic conversions helpers (templates)
    kinds/<kind>_types.hpp     concrete CGAL typedefs per kind
    impl/arr_impl.hpp          template<Kind, Types> ArrImpl : ArrBase (generic)
    impl/traits_ops_impl.hpp   template<Types> TraitsOpsImpl : TraitsOps (generic, SFINAE for optional functors)
    impl/polygon_set_impl.hpp  template<Types> PolygonSetImpl : PolygonSetBase (generic)
  src/
    numbers.cpp, registry.cpp
    kind_segment.cpp, kind_linear.cpp, kind_circle_segment.cpp, kind_polyline.cpp,
    kind_bezier.cpp, kind_conic.cpp, kind_sphere.cpp      (one TU per kind: KindOps impl + template instantiations)
    bso_segment.cpp, bso_circle_segment.cpp, bso_conic.cpp, bso_bezier.cpp (Boolean ops per kind)
setup.py / pyproject.toml      setuptools + Cython build, CGAL/GMP/MPFR/Boost discovery, parallel compile
tests/                         pytest
docs/                          user docs + dev docs (this file, CGAL 6.1 API maps)
```

### Why type erasure (instead of one generated Cython module per traits)?

* Cython sees only *one* set of opaque C++ types (`Geom`, `ArrBase`, handles). All CGAL
  templates are instantiated in C++ TUs (one per kind) which compile in parallel.
* One Python `Arrangement` class with a `kind`; mixing kinds is a runtime `KindMismatchError`.
* Adding a kind = one C++ TU + a few Python geometry constructors. No Cython regeneration.

### Handles and validity

Python `Vertex`/`Halfedge`/`Face`/`CurveHandle` objects store `(arrangement, raw pointer, id)`.
Every DCEL element carries a unique 64-bit `id` in its extended-DCEL data, assigned by an
internal `Arr_observer` (per-element creation notifications + a rescan in
`after_global_change`, `after_attach`, and after clone). The impl keeps `unordered_set`s of
live element pointers. A Python handle is valid iff `ptr ∈ live && ptr->data().id == id`.
Every access checks this and raises `InvalidHandleError` instead of crashing. Curve handles
(history) are tracked the same way (ids in an `unordered_map<const void*, uint64>`).

### Python object data on DCEL elements

`VData/HData/FData = struct { uint64_t id; PyRef data; }`. `PyRef` is a small RAII holder
of an opaque `void*` with incref/decref performed through global function-pointer hooks
installed by the Cython module at import (`arr2d::set_pyobject_hooks`). The core therefore
compiles without Python. Copying an arrangement copies (increfs) the data; overlay callbacks
may set data on result elements.

A reference owned by C++ is invisible to Python's cycle collector, so storing the *user's* object
in the `PyRef` leaked every cycle that ran through element data: `face.data = {"arr": arr}` kept
the whole arrangement alive for the life of the process and did not even appear in `gc.garbage`
(the collector subtracts only the references its `tp_traverse` walk finds, and the C++ one always
tipped the balance -- mirroring the object in a Python dict does not help either, because the
mirror adds a reference *and* a visit).

What the DCEL stores is therefore an opaque, unique **key** object (a bare `object()`), and the
Cython `Arrangement` keeps `_data_refs: dict[key -> user object]`. The key is a GC root (its
refcount exceeds the visits by exactly the C++ reference) but it references nothing, so it keeps
nothing alive; the user object hangs off `_data_refs`, which the generated `tp_traverse` does
visit, so a `.data` cycle is collected normally and the key then dies by refcounting when the DCEL
is destroyed. `copy()` / `assign()` copy the KEYS (the `PyRef` copy increfs them) and the Cython
layer re-maps them to the source's values; `clear()` empties the map; removed elements leave a
stale entry behind, so the map is rebuilt from the live elements whenever it grows past twice the
element count.

### Numbers

* `arr2d::Rational` = `CGAL::Exact_rational` = `boost::multiprecision::mpq_rational` in this
  build; equal to `CORE::BigRat` and to the exact type behind `Epeck::FT`, so conversions
  between kinds are lossless.
* Python -> Rational: `int` (int64 fast path, else decimal string), `float` (exact via
  `Rational(double)`), `fractions.Fraction`/`decimal.Decimal`/`str` (numerator/denominator strings).
* Rational -> Python: `fractions.Fraction(int(num_str), int(den_str))`.
* Algebraic coordinates (`CORE::Expr` for Bézier/conic, `_One_root_number` for circle segments)
  are exposed as `Algebraic` (double approximation + certified interval + `refine(bits)`)
  or `SqrtExtension(a, b, c)` (= a + b*sqrt(c), all `Fraction`).
* Points/curves expose `approx` (floats) always and `exact()` when representable.

### Errors

CGAL assertion/precondition failures throw `CGAL::Failure_exception` subclasses; Cython
translates them: `PreconditionError` (subclass of `CGALError` and `ValueError`),
`CGALAssertionError`, `PostconditionError`, `CGALWarning`; plus our own
`InvalidHandleError`, `KindMismatchError` (TypeError), `NotXMonotoneError` (ValueError),
`NotRepresentableError` (ValueError; e.g. algebraic point converted to a rational kind).
CGAL preconditions stay **enabled** (we compile with `-DNDEBUG -DCGAL_DEBUG`). Known CGAL 6.1 traps and the code-level responses are collected in `docs/dev/CGAL_TRAPS_CHECKLIST.md`.

## 2. C++ core interface (summary; the headers are the spec)

`common.hpp`

```cpp
namespace arr2d {
enum class Kind : int { Segment=0, Linear=1, CircleSegment=2, Polyline=3, Bezier=4, Conic=5, Sphere=6, NumKinds=7 };
using Rational = CGAL::Exact_rational;               // mpq_rational
struct PyRef { void* obj = nullptr; ... };            // incref/decref via hooks
void set_pyobject_hooks(void(*incref)(void*), void(*decref)(void*));
enum class GeomType : int { Point=0, Curve=1, XCurve=2, Number=3 };
struct Geom { Kind kind; GeomType type; std::shared_ptr<const void> ptr; template<class T> const T& as() const; };
template<class T> Geom make_geom(Kind, GeomType, T value);
struct VH { void* p=nullptr; uint64_t id=0; }; struct HH{...}; struct FH{...}; struct CH{...};
struct Located { int type = -1; /* 0 vertex, 1 halfedge, 2 face, -1 none */ void* p=nullptr; uint64_t id=0; };
struct Error : std::runtime_error { ErrorCode code; };  // thrown by the core for API misuse (kind mismatch, invalid handle, not x-monotone, not representable, unsupported)
}
```

`ops.hpp` — `KindOps` (pure virtual, one implementation per kind): construction of points
from rationals, approximate/exact/interval extraction, `make_x_monotone`, x-monotone
accessors (`source/target/left/right/min/max`, `is_vertical`, `is_directed_right`), bbox,
`approximate(xcurve, tolerance, bbox) -> polyline`, `repr`, `convert(geom, target kind)`,
`equal`, `compare_xy`, and the full `TraitsOps` functor set (`compare_x`, `compare_xy`,
`compare_y_at_x`, `compare_y_at_x_left/right`, `equal`, `make_x_monotone`, `split`,
`intersect`, `are_mergeable`, `merge`, `compare_endpoints_xy`, `construct_opposite`, `trim`,
`is_in_x_range`, `parameter_space_in_x/y`, `construct_x_monotone_curve(p,q)` when available).
Kind-specific constructors/accessors (segment endpoints, line coefficients, circle
center/radius, polyline vertices, Bézier control points, conic coefficients, sphere directions)
are *non-virtual free functions* declared in `ops.hpp` (`namespace arr2d::segment`, `::linear`, ...)
and implemented in the kind TU.

`arrangement.hpp` — `ArrBase` (pure virtual): sizes, iteration (fills vectors of handles),
vertex/halfedge/face accessors, data access, all modification functions of
`Arrangement_2`/`Arrangement_with_history_2` and the global insertion/removal functions,
history queries, point location (`locate`, `ray_shoot_up/down`, `batched_locate`, `attach/detach`
strategy objects), `zone`, `do_intersect`, `decompose`, observers (event dispatch through a
function pointer), overlay (`arr2d::overlay(a, b, r, hooks)`), `clone`, bulk export
(`vertex_coords`, `edge_vertex_indices`, `face_ccbs`), `face_polygon(FH)` (directed x-monotone
curves of outer ccb + holes), `he_directed_curve(HH)`.

`polygon_set.hpp` — `PolygonSetBase`: insert/join/intersection/difference/symmetric_difference/
complement/polygons_with_holes/oriented_side/locate/is_valid_polygon/orientation/
`to_arrangement(ArrBase&, contained faces)`; free functions `polygon_set_join(a, b)` etc.

`registry.hpp` — `const KindOps& ops(Kind)`, `std::unique_ptr<ArrBase> make_arrangement(Kind)`,
`std::unique_ptr<PolygonSetBase> make_polygon_set(Kind)`, `bool kind_supports_polygon_set(Kind)`,
`const char* kind_name(Kind)`, `Kind kind_from_name(...)`, `bool kind_is_unbounded(Kind)`.

## 3. Python API (public)

```python
import arrangement_2d as a2

# ---- kinds & errors
a2.Kind.SEGMENT / LINEAR / CIRCLE_SEGMENT / POLYLINE / BEZIER / CONIC / SPHERE   (IntEnum; strings accepted everywhere)
a2.CGALError, PreconditionError, CGALAssertionError, InvalidHandleError, KindMismatchError, NotXMonotoneError, NotRepresentableError

# ---- numbers
a2.Algebraic          .approx -> float, .interval(bits=53) -> (lo, hi), .refine(bits), float(), .is_rational, .exact() -> Fraction|None
a2.SqrtExtension      .a .b .c (Fraction), value a + b*sqrt(c); float(), .is_rational, .exact()

# ---- geometry (all immutable; all have .kind, .approx, __repr__, __eq__ where exact)
a2.Point(x, y)                      # planar (kind SEGMENT by default; rational coords)
a2.Point(x, y, z)                   # sphere (kind SPHERE; a direction, normalized lazily)
   .x .y (.z) -> float; .xy/.xyz -> tuple of floats; .exact() -> tuple of Fraction | SqrtExtension | Algebraic
   .is_rational; .to_kind(kind); .interval() -> ((lo,hi),(lo,hi)); .compare_xy(other) -> -1/0/1
a2.Segment(p, q)                                            kind SEGMENT
a2.LinearCurve.line(p, q) / .line_from_coefficients(a,b,c) / .ray(p, q) / .segment(p, q)   kind LINEAR
   aliases: a2.Line(p, q), a2.Ray(p, q)
   .is_line .is_ray .is_segment .source .target .has_source .has_target .supporting_line -> (a,b,c) Fractions
a2.CircleSegment.circle(center, radius=None, squared_radius=None, orientation=CCW)   full circle   kind CIRCLE_SEGMENT
   .arc(center, radius|squared_radius, source, target, orientation=CCW)            source/target must be on the circle (rational-coord points on a circle with rational r^2)
   .arc_from_three_points(p, q, r)
   .arc_from_circle(circle, source, target)    # general one-root endpoints allowed
   .segment(p, q)
   aliases: a2.Circle(...), a2.CircularArc(...)
   .is_full .is_linear .is_circular .orientation .center .squared_radius .radius(float) .source .target
a2.Polyline(points)  / Polyline.from_segments(segs) / Polyline.from_x_monotone_points(points)   kind POLYLINE
   .points -> [Point], .segments -> [Segment], len(), __getitem__
a2.BezierCurve(control_points)                                                            kind BEZIER (polynomial)
   .control_points -> [Point], .degree, .evaluate(t) -> Point (rational t exact; float t approx)
   .parameter_range (x-monotone only) -> (float, float), .supporting_curve, .xid, .has_self_intersections
   .sample(t0, t1, n) -> [(x,y)]; .from_rational(points, weights) -> ConicArc (degree 2) | error
a2.ConicArc.from_coefficients(r,s,t,u,v,w, orientation=None, source=None, target=None)     kind CONIC
   .circle(center, radius|squared_radius, orientation=CCW, source=None, target=None)
   .ellipse(center, rx, ry, rotation=0(rational angle? no: give major/minor axis vectors), ...)
   .from_points(p1..p5)  .segment(p, q)  .from_rational_bezier(p0, p1, p2, w0, w1, w2)
   .coefficients -> 6 Fractions, .orientation, .is_full, .source, .target, .conic_type ("ellipse"/"parabola"/"hyperbola"/"segment"/...), .approximate_length()
a2.GeodesicArc.from_points(p, q) / .from_points_and_normal(p, q, normal) / .great_circle(normal) / .x_monotone_arc(p, q)   kind SPHERE
   .source .target .normal -> Point(kind=SPHERE) .is_full .is_vertical .is_meridian
Common curve API: .kind, .is_x_monotone, .make_x_monotone() -> [curve|Point], .source/.target/.left/.right/.min/.max (x-monotone),
   .is_vertical, .is_directed_right, .bbox() -> (xmin,ymin,xmax,ymax) floats, .approximate(tolerance=1e-3, bbox=None) -> [(x,y)] (or (x,y,z)),
   .opposite(), .split(point) -> (c1, c2), .trim(p, q), .merge(other), .can_merge(other), .intersect(other) -> [Point|curve, multiplicity]
   .compare_y_at_x(point) -> -1/0/1, .is_in_x_range(point), .to_kind(kind), .exact() (kind-specific tuple), __eq__

# ---- polygons (Python-level containers of directed x-monotone curves; validated by CGAL on use)
a2.Polygon(points_or_curves, kind=None)     # points -> segments; curves must chain; .curves, .points (if linear), .orientation(), .reverse(), .is_simple(), .area() (segment kind), .to_kind(kind)
a2.PolygonWithHoles(outer, holes=())        # .outer, .holes, .is_unbounded

# ---- arrangement
arr = a2.Arrangement(kind="segment")        # kind name or Kind or another geometry object's kind
arr.kind, arr.is_unbounded_kind, arr.traits -> Traits (functor access: compare_xy(p,q), compare_y_at_x(p, c), intersect(c1,c2), make_x_monotone(c), split, merge, trim, ...)
len(arr) == number_of_edges
arr.number_of_vertices/halfedges/edges/faces/unbounded_faces/isolated_vertices/vertices_at_infinity/curves  (properties)
arr.is_empty, arr.is_valid(), arr.clear(), arr.copy() (deep, incl. data & history), __copy__/__deepcopy__
arr.vertices() / halfedges() / edges() (one halfedge per edge) / faces() / unbounded_faces() / bounded_faces() / curves() -> lists (snapshots)
arr.unbounded_face  (planar bounded kinds: the single unbounded face) ; arr.fictitious_face (linear kind)
# insertion (with history unless noted)
arr.insert(curve_or_point_or_iterable) -> CurveHandle | Vertex | list        # any curve convertible to arr.kind; iterable -> aggregate sweep insertion
arr.insert_curves(curves) -> [CurveHandle]                                    # aggregate (sweep)
arr.insert_point(point) -> Vertex
arr.insert_non_intersecting(xcurve) -> Halfedge ; arr.insert_non_intersecting_curves(xcurves)   # no history
arr.insert_in_face_interior(xcurve, face) -> Halfedge ; arr.insert_point_in_face_interior(point, face) -> Vertex
arr.insert_from_left_vertex(xcurve, v) / insert_from_right_vertex(xcurve, v) / insert_at_vertices(xcurve, v1, v2) -> Halfedge
# modification
arr.modify_vertex(v, point) -> Vertex ; arr.modify_edge(he, xcurve) -> Halfedge
arr.split_edge(he, point) -> Halfedge ; arr.split_edge(he, c1, c2) -> Halfedge   (history-aware)
arr.merge_edge(he1, he2) -> Halfedge ; arr.merge_edge(he1, he2, xcurve)            (history-aware)
arr.remove_edge(he, remove_source=True, remove_target=True) -> Face
arr.remove_vertex(v) -> bool ; arr.remove_isolated_vertex(v) -> Face
arr.remove_curve(curve_handle) -> int (number of removed edges)
# queries
arr.locate(point, strategy=None) -> Vertex | Halfedge | Face
arr.ray_shoot_up(point, strategy=None) / ray_shoot_down -> Vertex | Halfedge | Face | None
arr.batched_locate(points) -> [result, ...] aligned with the input points
arr.attach_point_location(strategy) / detach_point_location(strategy) ; strategies: "naive","simple","walk","landmarks","trapezoid","triangulation"
#   not every kind offers every strategy -- ask arr.supports_point_location(name); "triangulation" is never
#   offered, "landmarks" is not offered for circle_segment/bezier (no Construct_x_monotone_curve_2) nor for
#   sphere (CGAL joins query point and landmark with an arc, which its own precondition forbids for an
#   antipodal pair), "trapezoid" not for linear/sphere.  supports_point_location is an ARRANGEMENT
#   question, not only a kind question: "landmarks" on the linear kind is offered exactly while the
#   arrangement holds no unbounded edge (CGAL reads the null point of a vertex at infinity otherwise);
#   inserting a ray/line turns it off (an attached structure is then no longer used), removing it back on
arr.zone(curve) -> [Vertex | Halfedge | Face] ; arr.do_intersect(curve) -> bool
arr.decompose() -> [(Vertex, below, above)]   (vertical decomposition; below/above are Vertex|Halfedge|Face|None)
arr.overlay(other, on_vertex=None, on_edge=None, on_face=None, ...) -> Arrangement   (callbacks per OverlayTraits event; see Overlay below)
arr.add_observer(obs) / remove_observer(obs)    obs: a2.Observer subclass overriding any before_*/after_* method
arr.bbox() -> (xmin,ymin,xmax,ymax) of vertex approximations
# bulk export (numpy if available, else lists)
arr.vertex_coordinates() -> (n,2|3) floats ; arr.edge_vertex_indices() -> (m,2) ints ; arr.face_boundaries() -> per face lists of vertex indices per ccb
arr.approximate_edges(tolerance) -> list of (k,2) arrays
arr.to_dict()/from_dict()  (exact input curves + data?) — v2

# ---- handles (compare/hash by identity of the underlying element; .is_valid; .id; .arrangement)
Vertex:   .point, .degree, .is_isolated, .face (isolated only), .incident_halfedges() -> [Halfedge] (into the vertex), .is_at_open_boundary,
          .parameter_space_in_x/y, .data (get/set any Python object), .incident_faces()
Halfedge: .source, .target, .twin, .next, .prev, .face, .curve (as stored), .directed_curve (oriented source->target), .direction ("left_to_right"/"right_to_left"),
          .is_fictitious, .is_on_inner_ccb, .ccb() -> [Halfedge] around its ccb, .data, .originating_curves() -> [CurveHandle], .edge_id (min of he/twin ids)
          a FICTITIOUS halfedge carries no curve at all: .curve, .directed_curve, .originating_curves() and
          .number_of_originating_curves all raise UnsupportedError for it
          .face raises UnsupportedError inside an Observer's before_split_face(f, e), for e and e.twin
Face:     .is_unbounded, .is_fictitious, .has_outer_ccb, .outer_ccb() -> [Halfedge] (planar; error if none), .outer_ccbs() -> [[Halfedge]] (sphere may have >1),
          .inner_ccbs() / .holes() -> [[Halfedge]], .isolated_vertices() -> [Vertex], .number_of_outer_ccbs/inner_ccbs/isolated_vertices, .data,
          .polygon(tolerance=None) -> PolygonWithHoles (exact curves) , .boundary_points(tolerance) -> approximated outer polyline + holes, .adjacent_faces(), .edges()
CurveHandle: .curve, .induced_edges() -> [Halfedge], .number_of_induced_edges, .is_valid, .data? (no)

# ---- Boolean set operations
ps = a2.PolygonSet(kind="segment")          # kinds: segment, circle_segment, conic, bezier
ps.insert(polygon | polygon_with_holes | iterable)
ps.join(other|polygon) / intersection / difference / symmetric_difference / complement  (in place; return self)
a2.join(a, b) ... free functions returning new sets; operators | & - ^ ~
ps.polygons_with_holes() -> [PolygonWithHoles] ; ps.number_of_polygons_with_holes ; ps.is_empty ; ps.is_plane
ps.oriented_side(point) -> -1/0/1 ; ps.locate(point) -> PolygonWithHoles|None ; ps.do_intersect(other)
ps.to_arrangement() -> (Arrangement, [contained faces]) ; a2.PolygonSet.from_faces(faces) ; ps.copy()
a2.orientation(polygon) ; a2.is_valid_polygon(polygon)

# ---- high level (regions.py; lazily imported as a2.regions)
a2.regions.bounded_faces(arr)                                  -> [Face]
a2.regions.face_containing(arr, point, *, strategy=None, on_boundary="none"|"any"|"raise") -> Face | None
a2.regions.face_area(face)                                     -> Fraction (rational kinds) | float
a2.regions.faces_polygons(arr, tolerance, *, faces=None, include_unbounded=False) -> [FaceBoundary(face, outer, holes)]
a2.regions.shared_edges(faces)                                 -> [Halfedge]   (edges between two distinct faces of the group)
a2.regions.merge_faces(arr, faces, *, remove_vertices=True)    -> [Face]       (removes those edges, in place)
a2.regions.split_face(arr, face, curve)                        -> [Face]       (zone-checked: the curve must stay in the face)
a2.regions.connected_components(arr, *, include_vertices_at_infinity=False) -> [Component(vertices, edges)]
a2.regions.number_of_connected_components(arr)                 -> int          (the C of V - E + F = 1 + C)
a2.regions.extract_regions(arr, predicate=None, *, faces=None) -> [[Face]]     (selected faces grouped by adjacency)
a2.regions.union_outline(arr, faces=None)                      -> PolygonSet   (exact; BSO kinds only)
a2.regions.supports_boolean_ops(kind)                          -> bool

# ---- plotting (plot.py; lazily imported as a2.plot; matplotlib imported inside the calls)
a2.plot.plot_arrangement(arr, ax=None, *, tolerance, bbox, show_vertices, show_edges, faces,
                         face_colors=None|"index"|"data"|colour|callable|mapping|sequence,
                         face_alpha, edge_color, linewidth, vertex_color, vertex_size,
                         projection, aspect, autoscale) -> Axes
a2.plot.plot_polygon_set(polygons, ax=None, *, tolerance, bbox, face_color, face_alpha, edge_color,
                         linewidth, aspect, autoscale) -> Axes
a2.plot.plot_curves(curves, ax=None, *, tolerance, bbox, color, linewidth, projection, aspect, autoscale) -> Axes
a2.plot.lonlat(x, y, z) -> (lon, lat)      a2.plot.has_matplotlib() -> bool
```

### Overlay callbacks

`arr.overlay(other, callbacks)` where `callbacks` is an `a2.OverlayCallbacks` subclass (or a dict of
callables) with methods named after the `OverlayTraits` concept:
`vertex_vertex(v1, v2, v)`, `vertex_edge(v1, e2, v)`, `vertex_face(v1, f2, v)`, `edge_vertex(e1, v2, v)`,
`face_vertex(f1, v2, v)`, `edge_edge_vertex(e1, e2, v)`, `edge_edge(e1, e2, e)`, `edge_face(e1, f2, e)`,
`face_edge(f1, e2, e)`, `face_face(f1, f2, f)`. Each receives handles of A, B and the result R
(the result arrangement is created first so its handles are real). Default: no data copied.
Convenience: `on_face=lambda fa, fb: value` etc. set `.data` of the result element.

### Observers

`a2.Observer` has a no-op method for every `Arr_observer` notification (`before_assign`,
`after_assign`, `before_clear`, `after_clear`, `before_global_change`, `after_global_change`,
`before_attach`, `after_attach`, `before_detach`, `after_detach`, `before_create_vertex(point)`,
`after_create_vertex(v)`, `before_create_boundary_vertex`, `after_create_boundary_vertex`,
`before_create_edge(xcurve, v1, v2)`, `after_create_edge(he)`, `before_modify_vertex`,
`after_modify_vertex`, `before_modify_edge`, `after_modify_edge`, `before_split_edge(he, point, c1, c2)`,
`after_split_edge(he1, he2)`, `before_split_fictitious_edge`, `after_split_fictitious_edge`,
`before_split_face(f, he)`, `after_split_face(f, new_f, is_hole)`, `before_split_outer_ccb`,
`after_split_outer_ccb`, `before_split_inner_ccb`, `after_split_inner_ccb`, `before_add_outer_ccb`,
`after_add_outer_ccb`, `before_add_inner_ccb`, `after_add_inner_ccb`, `before_add_isolated_vertex`,
`after_add_isolated_vertex`, `before_merge_edge`, `after_merge_edge`, `before_merge_fictitious_edge`,
`after_merge_fictitious_edge`, `before_merge_face`, `after_merge_face`, `before_merge_outer_ccb`,
`after_merge_outer_ccb`, `before_merge_inner_ccb`, `after_merge_inner_ccb`, `before_move_outer_ccb`,
`after_move_outer_ccb`, `before_move_inner_ccb`, `after_move_inner_ccb`, `before_move_isolated_vertex`,
`after_move_isolated_vertex`, `before_remove_vertex`, `after_remove_vertex`, `before_remove_edge`,
`after_remove_edge`, `before_remove_outer_ccb`, `after_remove_outer_ccb`, `before_remove_inner_ccb`,
`after_remove_inner_ccb`). Exact list/signatures follow `docs/dev/cgal61_api/global_functions_overlay_observer.md`.

## 4. Build

`setup.py` (setuptools + Cython 3), single extension `arrangement_2d._core` built from
`_core.pyx` + all `src/arr2d/src/*.cpp`, C++17, `-O2`, `-DCGAL_USE_CORE -DCGAL_USE_GMP -DCGAL_USE_MPFR`,
`-DNDEBUG -DCGAL_DEBUG` (C assert off, CGAL preconditions ON — see docs/dev/cgal61_api/build_and_abi_contract.md), `-fvisibility=hidden`, link `gmp`, `mpfr`.
Include/lib discovery order: env `CGAL_DIR`/`CGAL_INCLUDE_DIR`/`GMP_DIR`..., `CONDA_PREFIX`,
`brew --prefix` (macOS), `/usr/local`, `/usr`, `pkg-config gmp mpfr`. Parallel object compilation
(`build_ext.parallel = cpu_count`). `pip install -e .` and `python setup.py build_ext --inplace -j16` both work.

## 5. Testing

pytest, per kind: construction, counts (V/E/F) on canonical inputs, history, modification
functions, point location strategies, zone/decompose/batched, overlay callbacks, observers,
handle invalidation, data round-trips (incl. copy), exact/approx extraction, Boolean ops,
regions helpers, error translation, and a stress test with random segments checking
`is_valid` and Euler characteristic (V - E + F = 2 for planar bounded incl. unbounded face
and connected arrangements; generalised: V - E + F = 1 + C, C = number of connected components).
