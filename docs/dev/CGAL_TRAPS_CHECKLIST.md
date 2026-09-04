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
  rational convergent). Never build logic on `Expr == rational` — measured: for the conic vertex
  X = sqrt(2) and the convergent p/q with `p^2 - 2q^2 = -1`, `Expr::cmp` says EQUAL and `X > p/q`
  says false. FIXED in `numbers.cpp`: `compare(Algebraic, Rational)` / `compare(Algebraic, SqrtExt)`
  (and every mixed branch of `number_compare`) separate the two with the CERTIFIED enclosure
  (`expr_enclosure`, doubling to 2^20 bits) and only fall back to `Expr::cmp` when the enclosure
  cannot separate them, i.e. when they really are equal.
- `to_double_correctly_rounded(SqrtExt)` and `interval_of(SqrtExt)` must NOT use a fixed refinement
  cap: `sqrt_ext_bounds` has an ABSOLUTE error of `|b| / (den(c) * 2^bits)`, so a value with heavy
  cancellation needs far more than 4096 bits (measured: 6.354 returned for a true value of 0.692,
  and an interval 59x wider than the value, straddling 0). Both now size each refinement step from
  the measured width relative to the value, and `interval_of` finally honours its `bits` argument.

## Arrangement core
- Handles from raw pointers must never be incremented. Vertices at infinity / fictitious halfedges /
  the fictitious face are invisible to the iterators; reach them via CCB walks; guard `point()` /
  `curve()` with `is_at_open_boundary()` / `is_fictitious()` (they assert on null; UB under NDEBUG).
  The history queries `number_of_originating_curves(e)` / `originating_curves(e)` read `e->curve()`
  too (`Arr_dcel_base.h:192`) → guard them with `is_fictitious()` as well (`ArrImpl` does).
- `unbounded_face()` under the unbounded topology returns an arbitrary, drifting face: use
  `unbounded_faces_begin/end`. A vertex at infinity can be on a left/right AND top/bottom boundary.
- Inserting an unbounded curve (line/ray) that OVERLAPS an existing edge with an unbounded left end
  aborts (`cv.has_left()` precondition). Detect overlaps first (zone + traits intersect) and raise
  `Unsupported` for the Linear kind.
- `insert_at_vertices(cv, v1, v2)` frees v1's isolated-vertex record before validating: check our
  preconditions first (v1/v2 valid, curve endpoints equal the vertex points). CGAL checks the
  ENDPOINTS before the deletion but the CONTAINING FACE only after it
  (`Arrangement_on_surface_2_impl.h:1026-1050` for two isolated vertices, `:1081` for isolated +
  incident), so the assertion throws with `is_isolated() == true` and a dangling `isolated_vertex()`
  — every later `Vertex.face` / `Face.isolated_vertices()` / `is_valid()` then SIGSEGVs.
  `ArrImpl::reject_bad_insert_at_vertices` replicates both containment rules before the call.
- Nothing may MODIFY an arrangement from inside an observer or overlay callback: CGAL is halfway
  through a DCEL modification and a re-entrant `clear()` / `assign()` / `insert()` / `remove_curve()`
  SEGFAULTs (the with-history observer inserts into a `Curve_halfedges` node `clear()` just
  destroyed) or hangs forever (overlay, on the half-built result). `ArrImpl::reject_reentrant`
  gates every mutator on `m_in_notify`, and `OverlayTraits::emit` raises that flag on A, B and R.
- An exception escaping a with-history modification breaks the curve history: the
  `Curve_halfedges_observer` unregisters the edge in `before_*` and re-registers it in `after_*`
  (`Arrangement_on_surface_with_history_2.h:246-338`), so a throw in between leaves the curve's
  halfedge set without the edge while the edge still names the curve. `remove_curve()` then frees
  the node under live halfedges and the next `copy()/assign()/overlay()` SIGSEGVs in
  `Curve_halfedges::Less_halfedge_handle`. `ArrImpl` schedules `repair_history()` (via
  `RepairOnThrow`) after any mutator that threw, and `is_valid()` checks the symmetry.
- `assign()` copies the SOURCE's `ElementData::id`s, which collide with the ids this arrangement
  handed out earlier: a stale handle then answers `is_valid == True` and resolves to a different
  element. `ArrImpl::assign` calls `renumber()` (clear every copied id; `m_next_id` is never reset)
  before `rescan()`.
- Observer traces: `before_merge_face(f1,f2,e)` does not name the survivor (erase both, re-track in
  `after_merge_face`); inside `before_split_face(f,e)` `e->face()`/`e->twin()->face()` SEGFAULT —
  block `he_face` for that halfedge (and twin) while dispatching that event to Python; in
  `before_detach` never touch geometry (the destructor freed it); aggregate insert brackets the whole
  range with one global-change pair; overlay fires `before/after_clear` on the result, no global-change.
  `after_split_edge(e1,e2)`: e1 is the original object (keeps data), e2 new; `after_merge_edge` keeps e1.
  Two more per-event restrictions, both measured as SIGSEGVs: a vertex handed to
  `after_create_vertex` / `after_create_boundary_vertex` / `before_create_edge` /
  `before_split_edge` is NOT isolated yet and still has a NULL halfedge, and
  `Vertex::incident_halfedges()` has no null check (its sibling `degree()` has one) — gate the
  circulator on `degree() == 0`; and inside `before_remove_vertex` CGAL has already deleted (or
  re-linked) the halfedges around a NON-isolated vertex, so `degree()` / `incident_halfedges()` /
  `incident_faces()` must be refused there (`ArrImpl::m_no_ring_v`, the same pattern as
  `m_no_face_he`). `remove_isolated_vertex` notifies first, so an isolated vertex is unaffected.
- Exceptions thrown by CGAL during a sweep (aggregate insert, overlay, decompose, batched locate, BSO)
  leak sweep memory and overlay leaves the const inputs with corrupted `inc()` marks. Callbacks must
  never throw (Cython records exceptions). Document that a `PreconditionError` escaping a sweep may
  leave the arrangement invalid.
- Point location: `locate`/`ray_shoot` return an arbitrary twin; Python `Halfedge.__eq__` must be by
  identity but tests must compare `edge_id`. Triangulation PL silently returns the wrong face for
  faces with holes → do NOT expose (unsupported). Landmarks needs `Construct_x_monotone_curve_2(p,q)` and `Approximate_2`:
  unavailable for circle_segment and bezier (polyline has both); on the sphere it aborts on antipodal pairs, so only naive is offered there. Multi-attach is safe, and so is mutation while
  attached — EXCEPT that an attached `Arr_trapezoid_ric_point_location` raises
  `ptr()->v == ptr()->cw_he->source()` (`Arr_point_location/Td_active_vertex.h:168`) on EVERY edge
  MERGE (`merge_edge`, and `remove_vertex` on a degree-2 vertex) — measured for segment, polyline,
  circle_segment, conic and bezier; the escaping exception also corrupts the curve history (see
  "Arrangement core"). `ArrImpl::TrapGuard` detaches and rebuilds the structure around every
  merging operation (O(size) per merge, documented on `Arrangement.attach_point_location`).
- **Landmarks breaks as soon as an UNBOUNDED EDGE exists.** `Arr_landmarks_point_location::
  _deal_with_curve_contained_in_segment` (`Arr_point_location/Arr_landmarks_pl_impl.h:414`) compares
  `he->source()->point()` with `he->target()->point()` WITHOUT the `is_at_open_boundary()` guard its
  sibling code at `:309`/`:329` uses (it is also handed the CCB *representative*, not the halfedge
  that was found); and `_walk_from_face` finds no crossable edge on the all-fictitious outer CCB of
  the unbounded face → `CGAL_assertion(new_face != face)` (`:533`). Measured: a query on a line/ray
  raises one of the two; UB without `CGAL_DEBUG`. With NO unbounded edge the bug is structurally
  unreachable (`_intersection_with_ccb` skips fictitious halfedges, and only an unbounded edge makes
  a CCB mix fictitious and concrete halfedges) — verified with 4320 randomized queries against
  naive PL. So `KindPolicy<LinearTypes>::supports_landmarks = true` and `ArrImpl::landmarks_usable()`
  gates it at runtime on `number_of_vertices_at_infinity() == 0` (O(1)); an already attached
  landmarks object survives the insertion of a line, only the QUERY is refused.
- zone(): use the 4-arg overload with a PL object (3-arg returns an unadvanced iterator); visitor
  results must be `std::variant` of NON-const handles.

## Sphere kind
- Only `Arr_naive_point_location` and batched `CGAL::locate` are safe (walk/simple do not compile;
  RIC aborts with pole/identification vertices; landmarks aborts on antipodal pairs). Default = naive.
  Landmarks *compiles* here, so the policy flag has to say no explicitly: the landmark walk joins the
  nearest landmark to the query point with `Construct_x_monotone_curve_2(np, p)`, whose precondition
  `! equal_3(opposite(source), target)` (`:611`) forbids an antipodal pair, and CGAL picks the
  landmark. Measured: locating the north pole in a two-octant-triangle arrangement raises it →
  `KindPolicy<SphereTypes>::supports_landmarks = false`.
- `decompose` aborts if any vertex is at a pole or on the identification curve → `Unsupported`.
- Incremental insert / zone / do_intersect of a curve lying ON the identification meridian aborts once
  the arrangement crosses it (`f == f2`, "The two halfedges must share the same incident face",
  `Arrangement_on_surface_2_impl.h:2699`) — in the MIDDLE of the DCEL surgery, so the curve is left
  half-inserted, every later insert fails and a longer sequence SIGSEGVs. Aggregate insertion does
  NOT help when the arrangement is already non-empty. `ArrImpl::reject_identification_curve` refuses
  it up front (Unsupported): the offending x-monotone piece is exactly the one whose BOTH ends have
  `parameter_space_in_x != ARR_INTERIOR`, and the abort needs an existing vertex with
  `parameter_space_in_x != ARR_INTERIOR`. Insert the whole set in ONE `insert()` on an empty
  arrangement instead.
- The geodesic `Split_2` (`Arr_geodesic_arc_on_sphere_traits_2.h:2258-2266`) has NO "p lies on the
  curve" precondition — it only rejects a degenerate arc and the two endpoints — so it silently
  returns two arcs off the stored great circle and `Arrangement.split_edge(he, p)` corrupts the
  arrangement (`is_valid()` False, the next insert dies in `Multiset.h:2170`). `KindOpsBase::split`
  and `ArrImpl::split_edge_at_point` therefore verify `is_in_x_range` + `compare_y_at_x == EQUAL` +
  "not an endpoint" for EVERY kind before calling CGAL (the error is `Error(InvalidArgument)`, i.e.
  a plain Python `ValueError`, not a CGAL `PreconditionError`).
- `remove_vertex` / `remove_isolated_vertex` on an isolated pole or identification vertex leaves
  dangling topology pointers (next insert SIGSEGVs) → refuse (Unsupported) when
  `parameter_space_in_x/y != INTERIOR`.
- `Construct_point_2` is the ONLY safe way to build points (direct ctor with a wrong location
  silently corrupts). Points are unnormalised directions; the spherical face has 0 outer ccbs and
  contains the north pole; batched locate at the south pole is wrong; zone misses the north pole as MAX end.
- An arc is "the CCW sweep around `normal` from `source` to `target`" — every constructor establishes
  `normal == source x target` and `Compare_y_at_x_2` (`:1200-1205`) reads `normal` and
  `is_directed_right` together. `X_monotone_curve_2::opposite()` (`:3454`, and therefore
  `Construct_opposite_2`, `:3019`) swaps the endpoints and flips `is_directed_right` but KEEPS the
  normal → the arc it returns is the COMPLEMENTARY one: `approximate()` walks the major arc and
  `compare_y_at_x` is inverted. `SphereOps::construct_opposite` rebuilds it with a NEGATED normal.
  `Equal_2`'s meridian branch (`:1478-1481`) then needs fixing too — it compares only the normal, so
  it calls the two disjoint halves of a vertical great circle equal; compare `normal x source`
  (`SphereOps::curve_equal`).
- `Equal_2`'s NON-meridian branch (`:1483-1484`) compares nothing but `left()` and `right()`. That
  identifies the arc only while the endpoints are not ANTIPODAL (through two non-antipodal points
  there is one great circle, and of its two complementary arcs only the minor one can be
  x-monotone). An antipodal pair leaves the whole pencil: measured, `(0,-1,0) -> (0,1,0)` with
  normal `(0,0,1)` and with normal `(1,0,1)` are both x-monotone, non-meridian, share nothing but
  their endpoints (midpoints `(1,0,0)` vs `(1,0,-1)/sqrt 2`) and CGAL calls them EQUAL.
  `SphereOps::curve_equal` additionally compares the normals up to sign (the same graph traversed
  backwards carries the negated normal); for a non-antipodal pair that test is implied by the
  endpoints and changes no answer.
- Points are UNNORMALISED directions while `Equal_2` is projective (equal iff positive multiples):
  any hash of a sphere point must divide by the largest |component| first, or `a == b` and
  `hash(a) == hash(b)` disagree.
- Overlay works (non-indexed sweep branch); the final `create_face` gives the spherical faces.

## Bezier kind
- `Point_2::x()/y()` need `make_exact(cache)` first (own leaked `Bezier_cache`). `parameter_range()` is
  unreliable until endpoints are exact. Keep every `Curve_2` rep alive (cache keyed by rep address).
- `Split_2` silently accepts a rational point NOT on the curve → verify `is_in_x_range && compare_y_at_x==0`
  before splitting. Intersecting two x-monotone pieces of the same `Curve_2` with equal `xid` reports
  no overlap. No `Approximate_2`: implement sampling ourselves; endpoint accuracy of `sample` is poor.
- `Construct_opposite_2::operator()` is non-const; `Merge_2`/`Trim_2` from the traits only.
- **`Curve_2::has_no_self_intersections()` IS NOT CONSERVATIVE.** It is
  `! Bezier_bounding_rational_traits::may_have_self_intersections(ctrl)` (Bezier_curve_2.h:205),
  which answers "simple" when the control polygon is convex (a theorem) **or** when
  `_compute_angular_span()` claims the control-polygon edge directions span < 180 degrees
  (Bezier_bounding_rational_traits.h:291-308, :859). The second test classifies every edge against
  `dir = front -> back` with `orientation(v, ORIGIN, dir)`, which cannot distinguish "same
  direction" from "opposite direction", so an edge ANTIPODAL to `dir` is silently dropped from the
  span. MEASURED: the quartic `(-4,-4)(-3,4)(-3,-6)(1,3)(-4,-2)` has edge directions spanning
  204 degrees and its second edge `(0,-10)` is antipodal to `dir = (0,2)`, so CGAL reports
  `has_no_self_intersections() == true` although the curve crosses itself twice (exactly, at
  ~(-3.6153,-1.6270) and ~(-2.5932,-0.7837)). `_Bezier_x_monotone_2::_exact_intersect` short-circuits
  on the very same flag (Bezier_x_monotone_2.h:2120-2125), so CGAL builds a **silently wrong**
  arrangement for it: V=3 E=2 F=1, both crossings missing. Never use the flag. `kind_bezier.cpp`
  replaces it with a sound certificate — (a) all non-zero control-polygon edge vectors lie in one
  OPEN half-plane (then `B'(t).w > 0` for the separating `w`, so `t -> B(t).w` is strictly
  increasing and `B` is injective; exact O(n^2) rational test), else (b) CGAL's convexity test —
  and otherwise computes the crossings exactly through the cache's `id1 == id2` branch. That
  branch must NOT be called for a curve with a constant X(t) or Y(t) (`CGAL_assertion(degX > 0)` /
  `(degY > 0)`, Bezier_cache.h:664/681) and SIGSEGVs for degree 1 in both (a 0x0 Sylvester matrix
  in `_compute_resultant`); after (a) the only curve that can reach it is a straight axis-parallel
  motion that reverses on itself, which `check_sweepable` refuses. `check_sweepable` ALSO refuses
  any curve whose exact answer disagrees with CGAL's flag (`has_no_self_intersections()` true but
  crossings found): `_exact_intersect` short-circuits on that flag for two pieces of the same
  supporting curve, so the sweep would silently drop those vertices, and a clear `Unsupported` is
  better than a wrong arrangement. Cost: microseconds for a cubic,
  ~3 ms for a quartic, ~85 s for a wiggly degree-8 curve — memoised per supporting curve, and it
  is the same computation CGAL's own sweep performs for any curve whose flag is false.
- **FIXED (was OPEN):** a Bezier curve whose control points are COLLINEAR and whose motion along
  that line REVERSES traces the same segment twice, which no arrangement can represent.
  `has_no_self_intersections()` is true for it (the control polygon is "convex", and
  `CGAL::is_convex_2` cannot tell convex from collinear), so CGAL's sweep never looks for the
  overlap and then breaks in an input-dependent way — measured: `(0,0)(3,3)(1,1)` raises
  `CGAL assertion violation: org != p.originators_end()` (Bezier_x_monotone_2.h:1008),
  `(0,0)(4,4)(-1,-1)(2,2)` **HANGS** (no result in 60 s, i.e. a denial of service from ordinary
  user input).  `kind_bezier.cpp::collinear_motion_is_injective()` now decides it EXACTLY, before
  the convexity certificate: with every control point on the line `P0 + s*d` the curve is
  `B(t) = P0 + lambda(t)*d` for the Bernstein polynomial of `lambda_i = (P_i - P0).d`, and `B` is
  injective iff `lambda'` does not change sign on (0,1).  A coefficient test is only *sufficient*
  (the control scalars 0,1,0,1 give `lambda'(t) = 3(2t-1)^2 >= 0`, a monotone and perfectly legal
  curve whose differences alternate), so the roots of `lambda'` are isolated with the TU's own
  `Nt_traits` (`compute_polynomial_roots`) and the sign is checked on the midpoint of every gap.
  A reversing curve joins `self_isect().undecidable` and `check_sweepable()` refuses it with
  Unsupported; the legal ones are accepted unchanged.  This subsumes the axis-parallel sub-case
  that used to be the only one refused.
- A curve that passes through ANOTHER curve's own SELF-intersection point trips **two
  independent** CGAL 6.1 defects. arr2d repairs the first and refuses the configuration because of
  the second.
  1. **MEMORY CORRUPTION — repaired.** `_Bezier_cache::get_intersections` solves two resultants
     separately (s-roots restricted to [0,1] at `Bezier_cache.h:398`, ALL real t-roots at `:408`)
     and then pairs the two point lists assuming a BIJECTION: it `erase()`s the partner as it goes
     and scans `for (k = n_pts2 - 1; !found && k > 0; k--)` with an **unsigned** `k` (`:458`,
     loop `:488`). A self-intersection point is reached at two parameters, so the lists differ in
     length, and which of the two failure modes you get depends on which curve got the smaller
     `Curve_2::id()` (= its rep address, i.e. on the allocator): `pts2` runs empty, `n_pts2 - 1`
     wraps to 4294967295 and `dist_vec[k]` reads out of bounds of an EMPTY vector → SIGSEGV in
     `CORE::Expr::cmp`; or the loop stops at the first match, erases it, and silently DROPS the
     second pairing → `CGAL_assertion(_is_in_range(t, cache))`, `Bezier_x_monotone_2.h:933`.
     `src/arr2d/include/arr2d/impl/bezier_cache_fix.hpp` replaces that one member with an
     explicit specialization for `Nt_traits == CORE_algebraic_number_traits` that never erases and
     reports EVERY match (a many-to-many matching), keeping CGAL's "all others eliminated ⇒ the
     closest is the partner" shortcut for the bijective case so the common path costs the same.
     It must be included before `<CGAL/Arr_Bezier_curve_traits_2.h>` ([temp.expl.spec]/6) —
     `kinds/bezier_types.hpp` is the only place in the project that includes the Bezier traits,
     which is what makes that orderable. Version-locked to CGAL 6.1.x
     (`ARR2D_BEZIER_CACHE_FIXED`). Measured: the loop cubic (-1,0)(3,10)(-3,10)(1,0) with the
     linear Bezier (-2,3)-(2,3) through its crossing (0,3) SIGSEGVs / asserts without it and gives
     V=7 E=7 F=2 with it, in either id order. NOTE the guard below cannot replace this fix: it has
     to call `compare_y_at_x(self-intersection point, other curve)` itself, which goes through
     `_Bezier_cache::get_intersections` on the very pair it is about to refuse whenever that point
     is not rational.
  2. **THREE ORIGINATORS — not repaired, hence the refusal.** The shared point is reached by three
     x-monotone branches (two of the self-intersecting curve, one of the other), so
     `_Bezier_point_2` accumulates three originators, and CGAL handles at most two:
     `_Bezier_point_2_rep::_refine()` asserts `_origs.size() == 2` (`Bezier_point_2.h:1421`,
     reached from `fit_to_bbox()` inside `_Bezier_x_monotone_2::_clip_control_polygon`) and
     `make_exact()` asserts the same at `:1603`. Whether it is reached depends on whether CGAL's
     approximate isolation happens to suffice: measured, a quadratic (-3,0)(0,6)(3,0) or a cubic
     through the crossing come out correct, while a vertical segment x=0 through it, or a line
     that also cuts the loop elsewhere, hit the assertion. Repairing it means generalising CGAL's
     simultaneous two-curve refinement to k curves — real geometry work in third-party code.
  `BezierOps::intersect` detects the configuration exactly (the self-intersection points come from
  the SOUND `id1 == id2` branch of the same cache) and refuses with `Unsupported`. CGAL's SWEEP and
  ZONE call `Intersect_2` directly, never through `KindOps::intersect` (measured: `insert`,
  `insert_curves`, `zone` and `do_intersect` all fail, in either insertion order), so the same test
  runs as a PRE-FLIGHT over the whole curve set: `KindOps::check_sweepable` /
  `needs_sweep_precheck`, called by `ArrImpl` from every entry point that lets a new curve in (and
  from `overlay_with`, which unites two safe sets). Cheap: one memoised
  `has_no_self_intersections()` per distinct supporting curve unless one really does cross itself.
  `modify_edge` / `split_edge` / `merge_edge` reach the same conclusion in O(1) through
  `KindOps::supporting_curve_id` — their CGAL precondition is that the supplied x-monotone curves
  are pieces of the edge curve they replace, so they add no new supporting curve; a caller that
  violates it gets the full pre-flight instead of undefined behaviour. The BOOLEAN-SET sweep is the
  third door — two DISJOINT Bezier polygons, one bounded by a simple arc of a self-intersecting
  cubic and one by a curve through that cubic's crossing, fail in `join`/`intersection` (not in
  `insert`, which is non-intersecting) — so `PolygonSetImpl` runs the same pre-flight on insertion,
  on every binary operation and on `do_intersect` / `oriented_side(set)`. Refusing at `insert` too
  is deliberate over-refusal: it is what establishes the invariant "a polygon set never holds a
  dangerous pair", and it keeps the contract identical to the arrangement's.

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
- `Gps_traits_adaptor::Orientation_2` asserts `res != EQUAL` (`Gps_traits_adaptor.h:164/167/179`) and
  hits it on a closed but ZERO-AREA ring (the two curves leaving the leftmost vertex overlap), i.e.
  it throws here and returns a wrong COUNTERCLOCKWISE with assertions off. `PolygonSetImpl::
  orientation()` therefore replicates CGAL's leftmost-vertex algorithm with every asserted
  comparison turned into "undecidable" (0).
- `Polygon_2` free functions silently convert through polyline traits (use the GPS classes directly).
