# CGAL 6.1 — Curve approximation for rendering, and rendering unbounded arrangements

Source of truth: the headers installed at `/opt/homebrew/include/CGAL` (CGAL 6.1, header-only,
`$URL: .../v6.1/...`). Every signature below is quoted verbatim from those files, with the file
and line number. Everything marked **[verified]** was checked by compiling **and running** a test
program with

```
/usr/bin/clang++ -std=c++17 -O0 -DCGAL_USE_CORE -DCGAL_USE_GMP -DCGAL_USE_MPFR \
  -I/opt/homebrew/include -L/opt/homebrew/lib -lgmp -lmpfr -o test test.cpp
```

This file fills gap #7 of the CGAL 6.1 API-map set: it pins down the *semantics* of the
`Approximate_2` contract (the per-traits signatures are already mapped in
`traits_segment_linear_polyline.md` §1/§5/§11, `traits_circle_segment.md` §6, `traits_conic.md` §8,
`traits_geodesic_sphere.md` §4.8 and the matrix in `traits_adapters_and_misc.md` §11), supplies
drop-in replacements for the traits that have none, and covers rendering of **unbounded**
(ray / line / fictitious) edges and faces, which no other file in the set touches.

Files covered:

| File | What is used from it here |
|---|---|
| `CGAL/Arr_segment_traits_2.h` | `Approximate_2` (885–941) |
| `CGAL/Arr_non_caching_segment_basic_traits_2.h` | `Approximate_2` (225–281) |
| `CGAL/Arr_polyline_traits_2.h` | `Approximate_2` (598–650) |
| `CGAL/Arr_linear_traits_2.h` | `Approximate_2` (1520–1539), `_Linear_object_cached_2`, `Arr_linear_object_2` (1579–1721) |
| `CGAL/Arr_circle_segment_traits_2.h` | `Approximate_2` + the bisection recursion (378–549) |
| `CGAL/Arr_conic_traits_2.h` | `Approximate_2` (1648–2060), `Approximate_curve_length_2` (1530–1646), traits-level canonical-form helpers (4055/4124/4202) |
| `CGAL/Arr_geodesic_arc_on_sphere_traits_2.h` | `Approximate_2` (2858–2989) |
| `CGAL/Arr_polycurve_basic_traits_2.h` | `has_approximate_2` detector (1110–1144) |
| `CGAL/Arr_geometry_traits/Bezier_curve_2.h` | `Curve_2::sample()` (444–487), control points (395–421) |
| `CGAL/Arr_geometry_traits/Bezier_x_monotone_2.h` | `parameter_range()` (636–651), `left()/right()` |
| `CGAL/Arr_geometry_traits/Bezier_point_2.h` | `approximate()` (670–688), `is_exact()` (636) |
| `CGAL/Arr_geometry_traits/de_Casteljau_2.h` | `bisect_control_polygon_2`, `point_on_Bezier_curve_2` |
| `CGAL/draw_arrangement_2.h` | the whole `Draw_arr_tool` dispatch |
| `CGAL/Arr_dcel_base.h` | `has_null_point`/`point`/`parameter_space_in_*`, `has_null_curve`/`curve`, face flags |
| `CGAL/Arrangement_on_surface_2.h` | `is_at_open_boundary`, `is_fictitious`, iterator filters, unbounded/fictitious face access |
| `CGAL/Arr_unb_planar_topology_traits_2.h` | `is_concrete_vertex` / `is_valid_*` (145–190) |

---

## Gotchas / surprises vs. older CGAL

1. **`CGAL::draw()` / `add_to_graphics_scene()` no longer SFINAE-degrades — it is a hard compile
   error for any traits without a full `Approximate_2`.** The two `draw_*_impl2` / `draw_*_impl1`
   SFINAE probe pairs in `draw_arrangement_2.h` (lines 128–148, 261–278, 448–468) are all inside
   `#if 0` blocks. The live `#else` branches call `traits.approximate_2_object()` and
   `typename Gt::Approximate_point_2` **unconditionally**. `draw_exact_curve` /
   `draw_exact_region` (230–245) are dead code in 6.1.
   **[verified]** `add_to_graphics_scene` on `Arrangement_2<Arr_Bezier_curve_traits_2<…>>`:
   `error: no member named 'approximate_2_object' in 'CGAL::Arr_Bezier_curve_traits_2<…>'` at
   `draw_arrangement_2.h:152`, `:285`, `:473`.
   **[verified]** on `Arrangement_2<Arr_linear_traits_2<Epeck>>`:
   `error: no type named 'Approximate_point_2' in 'CGAL::Arr_linear_traits_2<CGAL::Epeck>'` at
   `:211` and `:430`, plus `error: no matching function for call to object of type
   'Approximate_2'` at `:285`.
   ⇒ This corrects `traits_bezier.md` §0 gotcha 8 ("silently degrades to straight chords") and
   `traits_adapters_and_misc.md` §8 ("falls back to `draw_exact_curve` … when the traits has no
   approximation"). There is no fallback.
2. **Consequence: CGAL cannot draw *any* unbounded arrangement.** The only traits that produce
   unbounded arrangements are `Arr_linear_traits_2` (no `Approximate_point_2`, no curve overload),
   `Arr_rational_function_traits_2` (no `Approximate_point_2`, no curve overload, and its
   `operator()` is **non-`const`**) and `Arr_algebraic_segment_traits_2` (no `Approximate_2` at
   all). All three fail to compile against `draw_arrangement_2.h`. Unbounded rendering is 100 %
   your own code. §5 gives it.
3. **`error` is an ABSOLUTE distance in world units, never relative.** For circle-segment and
   conic it is a per-chord perpendicular deviation bound; for the sphere it is a chordal sagitta
   on the *unit* sphere. It is **ignored entirely** by the segment, non-caching-segment and
   polyline traits (the parameter is literally spelled `double /* error */`).
   **[verified]** measured Hausdorff distances are always strictly below the requested `error`
   (table in §2.5).
4. **Both endpoints are always emitted.** Every whole-curve overload writes the min-vertex and the
   max-vertex approximations itself. Concatenating the polylines of a CCB therefore **duplicates
   every shared vertex**. CGAL's own `draw_approximate_region` does not de-duplicate either.
5. **`l2r == false` reverses the whole sequence**, first and last swapped included
   (**[verified]** for segment, polyline, circle-segment, conic, sphere and the Bezier
   replacement of §3.1). It is *not* "emit the same order but flag it".
6. **The output iterator must have reference/append semantics — a raw pointer or a
   `vector::iterator` silently produces garbage.** `Arr_circle_segment_traits_2::Approximate_2::
   add_points` (445) and `Arr_conic_traits_2::Approximate_2::add_points` (1861) recurse with the
   iterator **passed by value and the returned iterator discarded**.
   **[verified]** on a half-circle at `error = 0.1` (9 points with `std::back_inserter`): writing
   into `Approximate_point_2 buf[64]` returned `buf + 2` and left
   `buf = {(-5,0), (5,0), (3.53553,-3.53553), (4.6194,-1.91342), …untouched…}` — endpoints in the
   wrong slots, interior points clobbered. **Always use `std::back_insert_iterator`.**
7. **`error <= 0` is catastrophic, not a no-op.**
   * circle-segment / conic: `if (e < error) return oi;` never fires ⇒ unbounded recursion.
     **[verified]** `error = -1.0` on a half-circle ⇒ **SIGSEGV (exit 139)**, stack overflow.
   * sphere: `dtheta = 2*acos(1 - error/1) = 0` ⇒ `num_segs = ceil(theta/0) = +inf` ⇒
     `for (int i = 1; i < num_segs; ++i)` never terminates. **[verified]** `error = 0.0` hangs
     allocating forever. Clamp `error` to a positive floor in the binding layer.
   * sphere with `error > 2`: `acos(1-error)` is a domain error ⇒ `dtheta = NaN` ⇒
     `num_segs = NaN` ⇒ the loop body is skipped and you get exactly the 2 endpoints.
     **[verified]** `error = 2.5` ⇒ `n = 2`. No crash, no warning.
8. **The `Approximate_2` functors of segment / non-caching-segment / polyline / circle-segment /
   conic store `const Traits& m_traits` and have a `protected` constructor.** You cannot
   default-construct them; you must go through `traits.approximate_2_object()`, and the functor
   **must not outlive the traits object**. `Arr_linear_traits_2::Approximate_2` and
   `Arr_geodesic_arc_on_sphere_traits_2::Approximate_2` are stateless and public.
9. **`Arr_polycurve_traits_2<Sub>::Approximate_2` *is* `Sub::Approximate_2` — an alias, not a
   polycurve-aware functor.** `Arr_polycurve_basic_traits_2` (1128–1144) simply forwards the
   subcurve traits' nested types. **[verified]**
   `static_assert(std::is_same<Poly::Approximate_2, Sub::Approximate_2>::value)` passes, and
   calling `poly_traits.approximate_2_object()(polycurve_xcv, 0.01, oi)` is a **hard compile
   error** ("no known conversion from `X_monotone_polycurve_2<…>` to
   `const _X_monotone_circle_segment_2<…>`"), *not* UB.
   This corrects `traits_adapters_and_misc.md` line 1943 ("UB if ever called"):
   `Approximate_2 approximate_2_object_impl(std::true_type) const { }` is reached only when
   `Approximate_2` deduces to `void`, in which case the function's return type is `void` and the
   body is legal; every *use site* is then a compile error ("incomplete type `void`"). Nothing is
   ever undefined at run time.
   The **point** overloads do work through the polycurve traits (`Poly::Point_2 == Sub::Point_2`).
10. **`Arr_non_caching_segment_basic_traits_2` *does* have a full `Approximate_2` in 6.1** —
    `Approximate_number_type`, `Approximate_kernel`, `Approximate_point_2`, the `(p,i)` overload
    (251), the `(p)` overload (258) **and** the whole-curve overload (264). It is byte-for-byte the
    same implementation as `Arr_segment_traits_2`'s. No replacement is needed. (Any note claiming
    "no point overload, no curve overload" is wrong for this install.)
11. **`Arr_Bezier_curve_traits_2` has no `Approximate_2` of any kind** and
    `Curve_2::sample()` is a *uniform* sampler with no error control. §3.1 gives a measured,
    error-bounded de-Casteljau replacement.
12. **Bezier endpoint accuracy is bounded by the *bounding boxes*, not by your `error`.**
    `_Bezier_x_monotone_2::parameter_range()` (636–651) returns the **midpoints of the t-bound
    intervals** of the two endpoints' originators, and `_Bezier_point_2::approximate()` (670–688)
    returns `((bbox.min + bbox.max)/2)` whenever `!is_exact()`.
    **[verified]** on the cubic `(0,0),(6,4),(-2,6),(4,10)`: the two interior x-monotone split
    points report `is_exact() == false`, `parameter_range()` gives the dyadic values
    `0.3125` / `0.625`, and the endpoint from `left().approximate()` is `(1.75, 6.15625)` while the
    curve evaluated at `t = 0.625` is `(1.67969, 6.13281)` — a **0.075 discrepancy**, 75× the
    requested `error = 0.001`. Snap consistently (§3.1) or you get visible cracks.
13. **`Vertex::point()` on a vertex at infinity and `Halfedge::curve()` on a fictitious halfedge
    are assertion failures, and become silent null-pointer dereferences in a release build.**
    **[verified]** they throw `CGAL::Assertion_exception` from `Arr_dcel_base.h:112`
    (`CGAL_assertion(p_pt != nullptr)`) and `Arr_dcel_base.h:199`
    (`CGAL_precondition(p_cv != nullptr)`). `-DNDEBUG` ⇒ `CGAL_NDEBUG` ⇒ `CGAL_NO_ASSERTIONS` +
    `CGAL_NO_PRECONDITIONS` (`assertions.h:26–45`) ⇒ both checks vanish and you dereference
    `nullptr`. **Always guard with `is_at_open_boundary()` / `is_fictitious()`.**
14. **`arr.vertices_begin()`, `arr.halfedges_begin()`, `arr.edges_begin()` and `arr.faces_begin()`
    never show you the entities you must handle when rendering an unbounded arrangement.**
    They are `I_Filtered_iterator`s: `_Is_concrete_vertex` (vertices with a real point only),
    `_Is_valid_halfedge` (`! has_null_curve()`), `_Is_valid_face` (`! is_fictitious()`).
    **[verified]** on a 3-line arrangement: `arr.number_of_vertices() == 3`,
    `arr.number_of_vertices_at_infinity() == 6`, iterating `halfedges_begin()..end()` reports
    `fictitious = 0`, yet walking the face CCBs visits **10 fictitious halfedges** and
    **16 open-boundary vertices**. The fictitious face is likewise invisible to `faces_begin()`
    but reachable via `arr.fictitious_face()`.
15. **A vertex at infinity and a *fictitious corner* vertex can carry the same
    `(parameter_space_in_x, parameter_space_in_y)` pair.** **[verified]** the 3-line arrangement
    has two distinct vertices with `(ARR_RIGHT_BOUNDARY, ARR_BOTTOM_BOUNDARY)`: the end of the
    line `x+y=4` (`degree() == 3`) and the fictitious bottom-right corner (`degree() == 2`).
    The reliable test for "this is one of the four fictitious corners" is *all incident halfedges
    are fictitious*, not the parameter-space pair.
16. **`draw_arrangement_2.h` hard-codes `double error(0.01)`** in three places (212, 431, 173)
    with a `// TODO? (this->pixel_ratio())` comment. It is a world-unit constant and does not
    follow zoom. Your renderer must derive `error` from the pixel size (§6.4).
17. **The sphere's two approximation overloads disagree about normalisation.** The curve overload
    normalises everything it emits (`|d| == 1` exactly, **[verified]** `|dir| ∈ [1,1]` for
    `error ∈ {1, 0.1, 0.01}`; `[0.99999999999999989, 1]` at `error = 0.001`); the **point**
    overload does not — it just `to_double()`s the exact direction.
    **[verified]** `approximate_2_object()(ctr_p(3,4,12))` returns `(3,4,12)`, `|·| = 13`.
    `draw_arrangement_2.h` re-normalises by hand at 178–182 / 303–308 for exactly this reason.
    The curve overload also **loses the `Location_type`**: every emitted point, endpoints
    included, is stamped `NO_BOUNDARY_LOC` (2955–2962).
18. **Interior points emitted by `Approximate_2` are *not* on the exact curve.** Only the two
    endpoints are. **[verified]** with `Arr_circle_segment_traits_2::Compare_y_at_x_2` on the
    lower half of `x²+y²=25`, `error = 0.05`, 17 points: exactly **2** emitted vertices compare
    `EQUAL` (the endpoints), 5 compare `SMALLER`, 10 compare `LARGER` — the interior points
    straddle the arc at the `double`-rounding level (~1e-16 relative), independently of `error`.
    All 16 chord midpoints compare `LARGER`, i.e. the polyline is strictly inside the disc, as the
    convexity argument requires.
19. **The conic `error` really is a Hausdorff bound, and the "parametric midpoint" rule the code
    uses is exact for all three families.** `add_points` (1861) evaluates the deviation at
    `tm = (t1+t2)/2` for ellipses (`a cos t, b sin t`) and hyperbolas (`a cosh t, b sinh t`), and
    `add_parabolic_points` (1971) at `tm = dx/dy` for parabolas (`a t², 2 a t`). In each case that
    is precisely the point whose tangent is parallel to the chord, i.e. the true maximum-deviation
    point (for the ellipse because the max-distance-to-a-chord point is affine-invariant).
    The canonical→world transform is a rotation + translation, so the distance measured in the
    canonical frame equals the world distance.
20. **The circle-segment / conic subdivision is pure bisection**, so the point count jumps in
    powers of two and the achieved accuracy is typically 1.3–7× better than requested.
    **[verified]** half-circle of radius 5: `error = 1 → 5 pts`, `0.1 → 9`, `0.01 → 33`,
    `0.001 → 129` (i.e. `2^k + 1`), with measured deviations `0.38 / 0.096 / 0.0060 / 0.00038`.
21. **`Arr_conic_traits_2` also ships `Approximate_curve_length_2` (1530–1646) whose
    `hyperbola_length()` is `CGAL_error_msg("Not implemented yet!")`** (1731–1735). Do not wire
    arc length into the renderer for hyperbolic arcs.
22. **`Arr_rational_function_traits_2::Approximate_2::operator()(const Point_2&, int)` is
    non-`const`**, `Approximate_number_type` is the algebraic kernel's `Bound` (a rational type,
    **not** `double`), `approx_x` returns `p.x().lower()` — the *lower* end of the current
    isolating interval, not a refined midpoint — and `approx_y` evaluates the rational function
    there. Accuracy is whatever the isolator happens to have refined to. There is no
    `Approximate_point_2` and no curve overload.
23. **`Arr_curve_data_traits_2` / `Arr_consolidated_curve_data_traits_2` are transparent for
    approximation.** They derive publicly from the base traits and their `X_monotone_curve_2`
    derives publicly from the base's, so the inherited functor binds to it by reference.
    **[verified]** approximating a data-decorated circle arc through the data traits gives the
    same 9 points as the bare circle-segment traits.
24. **CGAL's own sphere drawing produces no filled faces.** `draw_region_impl1`'s
    `Arr_geodesic_arc_on_sphere_traits_2` overload (155–198) emits only `add_segment` calls; the
    `m_gs.add_point_in_face(*prev)` line is commented out (196). Only the generic
    `draw_approximate_region` (206–228) fills faces — and it omits the *last* point of every edge
    (the loop stops one short), relying on the next edge to re-emit it.

---

## 0. Conventions

* `AP` denotes the traits' `Approximate_point_2`; `ANT` its `Approximate_number_type`.
* "min vertex" / "max vertex" mean `construct_min_vertex_2_object()(xcv)` /
  `construct_max_vertex_2_object()(xcv)`, i.e. the lexicographically smaller / larger endpoint.
* "l2r order" = min vertex first, max vertex last.
* All measured numbers below come from programs in
  `…/scratchpad/apimap_render_{approx,conic,sphere,sphere0,linear,bezier,polycurve,draw,misc}/`.

---

## 1. The `Approximate_2` contract, verbatim, per traits

### 1.1 `Arr_segment_traits_2<Kernel_>` — `Arr_segment_traits_2.h:885–941`

```cpp
  typedef double                                        Approximate_number_type;
  typedef CGAL::Cartesian<Approximate_number_type>      Approximate_kernel;
  typedef Approximate_kernel::Point_2                   Approximate_point_2;

  class Approximate_2 {
  protected:
    using Traits = Arr_segment_traits_2<Kernel>;
    const Traits& m_traits;                                   // 894
    Approximate_2(const Traits& traits) : m_traits(traits) {}  // 899
    friend class Arr_segment_traits_2<Kernel>;
  public:
    Approximate_number_type operator()(const Point_2& p, int i) const;        // 911, \pre i∈{0,1}
    Approximate_point_2     operator()(const Point_2& p) const;               // 918
    template <typename OutputIterator>
    OutputIterator operator()(const X_monotone_curve_2& xcv, double /* error */,
                              OutputIterator oi, bool l2r = true) const;      // 924
  };
  Approximate_2 approximate_2_object() const { return Approximate_2(*this); } // 941
```

Body of the curve overload (924–937), verbatim:

```cpp
      auto min_vertex = m_traits.construct_min_vertex_2_object();
      auto max_vertex = m_traits.construct_max_vertex_2_object();
      const auto& src = (l2r) ? min_vertex(xcv) : max_vertex(xcv);
      const auto& trg = (l2r) ? max_vertex(xcv) : min_vertex(xcv);
      auto xs = CGAL::to_double(src.x());
      auto ys = CGAL::to_double(src.y());
      auto xt = CGAL::to_double(trg.x());
      auto yt = CGAL::to_double(trg.y());
      *oi++ = Approximate_point_2(xs, ys);
      *oi++ = Approximate_point_2(xt, yt);
      return oi;
```

⇒ exactly **2** points, `error` ignored. **[verified]** `n = 2` for `error ∈ {1, 0.1, 0.01, 0.001}`
on the segment `(0,0)–(10,3)`; `l2r = false` ⇒ `first = (10,3)`, `last = (0,0)`.

### 1.2 `Arr_non_caching_segment_basic_traits_2<T_Kernel>` — `:225–281`

Identical shape and identical body: `Approximate_number_type`/`Approximate_kernel`/
`Approximate_point_2` (225–227), `class Approximate_2` (229), protected
`Approximate_2(const Traits& traits)` (239), `operator() (const Point_2& p, int i) const` (251),
`Approximate_point_2 operator()(const Point_2& p) const` (258),
`OutputIterator operator()(const X_monotone_curve_2& xcv, double /* error */, OutputIterator oi,
bool l2r = true) const` (264), accessor at 281.
**Nothing is missing here.** `Arr_non_caching_segment_traits_2` inherits all of it.

### 1.3 `Arr_polyline_traits_2<SegmentTraits_2>` — `:598–650`

```cpp
  using Approximate_number_type = typename Base::Approximate_number_type;   // 598
  using Approximate_point_2     = typename Base::Approximate_point_2;       // 599

  class Approximate_2 : public Base::Approximate_2 {                        // 601
  protected:
    using Traits = Arr_polyline_traits_2<Segment_traits_2>;
    const Traits& m_traits;
    Approximate_2(const Traits& traits)                                     // 611
      : Base::Approximate_2(*(traits.subcurve_traits_2())), m_traits(traits) {}
    friend class Arr_polyline_traits_2<Segment_traits_2>;
  public:
    Approximate_number_type operator()(const Point_2& p, int i) const;      // 619
    Approximate_point_2     operator()(const Point_2& p) const;             // 622
    template <typename OutputIterator>
    OutputIterator operator()(const X_monotone_curve_2& xcv, double /* error */,
                              OutputIterator oi, bool l2r = true) const;    // 627
  };
```

Body (628–645): iterate `xcv.points_begin()..points_end()` when `l2r`, else
`points_rbegin()..points_rend()`; emit `Approximate_point_2(to_double(p.x()), to_double(p.y()))`
for each. ⇒ **exactly the polyline's own vertices**, `error` ignored, `n = segments + 1`.
**[verified]** a 4-segment polyline gives `n = 5` at `error = 1` and at `error = 0.001`;
`l2r = false` reverses.

Note the subtlety: this is the only polycurve family with a whole-curve `Approximate_2`, and it
exists *because* `Arr_polyline_traits_2` re-declares it — the generic
`Arr_polycurve_basic_traits_2` does not (§1.8).

### 1.4 `Arr_linear_traits_2<Kernel_>` — `:1520–1539` — **point coordinate only**

```cpp
  typedef double                          Approximate_number_type;   // 1520

  class Approximate_2 {                                              // 1522  (public, stateless)
  public:
    /*! \pre `i` is either 0 or 1. */
    Approximate_number_type operator()(const Point_2& p, int i) const  // 1531
    {
      CGAL_precondition((i == 0) || (i == 1));
      return (i == 0) ? CGAL::to_double(p.x()) : CGAL::to_double(p.y());
    }
  };
  Approximate_2 approximate_2_object() const { return Approximate_2(); }  // 1539
```

**No `Approximate_kernel`, no `Approximate_point_2`, no `(p)` overload, no curve overload.**
This is the traits you actually need for unbounded arrangements, so §3.3 + §5 replace it wholesale.

### 1.5 `Arr_circle_segment_traits_2<Kernel_, bool Filter>` — `:378–549`

```cpp
  typedef double                                        Approximate_number_type;  // 378
  typedef CGAL::Cartesian<Approximate_number_type>      Approximate_kernel;       // 379
  typedef Approximate_kernel::Point_2                   Approximate_point_2;      // 380

  class Approximate_2 {                                                           // 382
  protected:
    using Traits = Arr_circle_segment_traits_2<Kernel, Filter>;
    const Traits& m_traits;
    Approximate_2(const Traits& traits) : m_traits(traits) {}                      // 392
    friend class Arr_circle_segment_traits_2<Kernel, Filter>;
  public:
    Approximate_number_type operator()(const Point_2& p, int i) const;             // 404
    Approximate_point_2     operator()(const Point_2& p) const;                    // 411
    template <typename OutputIterator>
    OutputIterator operator()(const X_monotone_curve_2& xcv, double error,
                              OutputIterator oi, bool l2r = true) const {          // 417
      if (xcv.is_linear()) return approximate_segment(xcv, oi, l2r);
      return approximate_arc(xcv, error, oi, l2r);;
    }
  private:
    template <typename OutputIterator>
    OutputIterator approximate_segment(const X_monotone_curve_2& xcv,
                                       OutputIterator oi, bool l2r = true) const;  // 427
    template <typename OutputIterator, typename Op, typename Transform>
    OutputIterator add_points(double x1, double y1, double t1,
                              double x2, double y2, double t2,
                              double error, OutputIterator oi,
                              Op op, Transform transform) const;                   // 445
    void circular_point(double r, double t, double& x, double& y) const;           // 475
    void transform_point(double xc, double yc, double cx, double cy,
                         double& x, double& y) const;                              // 484
    template <typename OutputIterator>
    OutputIterator approximate_arc(const X_monotone_curve_2& xcv, double error,
                                   OutputIterator oi, bool l2r = true) const;      // 493
  };
  Approximate_2 approximate_2_object() const { return Approximate_2(*this); }      // 549
```

`add_points` verbatim (445–468) — this is the whole error model, shared verbatim with the conic
traits:

```cpp
      auto tm = (t1 + t2)*0.5;

      // Compute the canocal point where the error is maximal.
      double xm, ym;
      op(tm, xm, ym);

      auto dx = x2 - x1;
      auto dy = y2 - y1;

      // Compute the error; abort if it is below the threshold
      auto l = std::sqrt(dx*dx + dy*dy);
      auto e = std::abs((xm*dy - ym*dx + x2*y1 - x1*y2) / l);
      if (e < error) return oi;

      double x, y;
      transform(xm, ym, x, y);
      add_points(x1, y1, t1, xm, ym, tm, error, oi, op, transform);   // return VALUE DISCARDED
      *oi++ = Approximate_point_2(x, y);
      add_points(xm, ym, tm, x2, y2, t2, error, oi, op, transform);   // return VALUE DISCARDED
      return oi;
```

`e` is the perpendicular distance from the arc's parametric midpoint to the chord, computed in the
frame centred on the circle centre — a rigid motion away from world coordinates, so `e` is a
**world-unit distance**. `approximate_arc` (493–541) emits the min vertex, recurses, emits the max
vertex.

### 1.6 `Arr_conic_traits_2<RatKernel, AlgKernel, NtTraits>` — `:1526–2060`

```cpp
  typedef double                                        Approximate_number_type;  // 1526
  typedef CGAL::Cartesian<Approximate_number_type>      Approximate_kernel;       // 1527
  typedef Approximate_kernel::Point_2                   Approximate_point_2;      // 1528

  class Approximate_2 {                                                           // 1648
  protected:
    const Traits& m_traits;
    Approximate_2(const Traits& traits) : m_traits(traits) {}                      // 1658
  public:
    Approximate_number_type operator()(const Point_2& p, int i) const;             // 1670
    Approximate_point_2     operator()(const Point_2& p) const;                    // 1679
    template <typename OutputIterator>
    OutputIterator operator()(const X_monotone_curve_2& xcv, double error,
                              OutputIterator oi, bool l2r = true) const {          // 1685
      if (xcv.orientation() == COLLINEAR)
        return approximate_segment(xcv, oi, l2r);
      CGAL::Sign sign_conic = CGAL::sign(4*xcv.r()*xcv.s() - xcv.t()*xcv.t());
      if (sign_conic == POSITIVE)  return approximate_ellipse(xcv, error, oi, l2r);
      if (sign_conic == NEGATIVE)  return approximate_hyperbola(xcv, error, oi, l2r);
      return approximate_parabola(xcv, error, oi, l2r);
    }
  private:
    OutputIterator approximate_segment(…, OutputIterator oi, bool l2r) const;      // 1701
    void transform_point(double xc, double yc, double cost, double sint,
                         double cx, double cy, double& x, double& y) const;        // 1721
    OutputIterator approximate_ellipse (…, double error, OutputIterator oi, bool l2r = true) const; // 1798
    OutputIterator add_points(double x1,double y1,double t1,
                              double x2,double y2,double t2,
                              double error, OutputIterator oi, Op op, Transform transform) const;   // 1861
    void elliptic_point  (double a, double b, double t, double& x, double& y) const;  // 1891
    OutputIterator approximate_parabola(…, double error, OutputIterator oi, bool l2r = true) const; // 1902
    OutputIterator add_parabolic_points(…) const;                                     // 1971
    void parabolic_point (double a, double t, double& x, double& y) const;            // 2001
    OutputIterator approximate_hyperbola(…, double error, OutputIterator oi, bool l2r = true) const;// 2009
    void hyperbolic_point(double a, double b, double t, double& x, double& y) const;  // 2052
  };
  Approximate_2 approximate_2_object() const { return Approximate_2(*this); }      // 2060
```

The header's own words on the meaning of `error` (1793–1795, doc comment of
`approximate_ellipse`):

> `@param error the error bound of the generated approximation. This is the Hausdorff distance
> between the arc and the polyline, which approximates the arc.`

`add_points` is byte-identical to the circle-segment one (§1.5). `add_parabolic_points` differs
only in `auto tm = (dy == 0) ? 0 : dx / dy;` (1973–1975) — the parabola's tangent-parallel-to-chord
parameter.

**Traits-level canonical-form helpers** (public, non-functor; useful if you want to draw the conic
yourself, e.g. as an SVG arc):

```cpp
  void approximate_parabola (const X_monotone_curve_2& xcv,
                             double& r_m, double& t_m, double& s_m,
                             double& u_m, double& v_m, double& w_m,
                             double& cost, double& sint,
                             double& xs_t, double& ys_t,
                             double& xt_t, double& yt_t,
                             double& a, double& ts, double& tt,
                             double& cx, double& cy,
                             bool l2r = true) const;              // 4055
  void approximate_ellipse  (const X_monotone_curve_2& xcv,
                             double& r_m, double& t_m, double& s_m,
                             double& u_m, double& v_m, double& w_m,
                             double& cost, double& sint,
                             double& xs_t, double& ys_t, double& ts,
                             double& xt_t, double& yt_t, double& tt,
                             double& a, double& b, double& cx, double& cy,
                             bool l2r = true) const;              // 4124
  void approximate_hyperbola(const X_monotone_curve_2& xcv,
                             double& r_m, double& t_m, double& s_m,
                             double& u_m, double& v_m, double& w_m,
                             double& cost, double& sint,
                             double& xs_t, double& ys_t, double& ts,
                             double& xt_t, double& yt_t, double& tt,
                             double& a, double& b, double& cx, double& cy,
                             bool l2r = true) const;              // 4202
```

(`a`,`b` = semi-axes; `cx`,`cy` = centre; `cost`,`sint` = the de-rotation angle; `ts`,`tt` = the
endpoint parameters. `xs_t`,`ys_t`,`xt_t`,`yt_t` are the endpoints in the canonical frame.)

**Arc length** — `Approximate_curve_length_2` (1530–1646):

```cpp
    Approximate_number_type operator()(const X_monotone_curve_2& xcv) const;  // 1549
```
with `double hyperbola_length(const X_monotone_curve_2&) { CGAL_error_msg("Not implemented yet!"); … }`
(1731–1735). Accessor: `approximate_curve_length_2_object()`.

### 1.7 `Arr_geodesic_arc_on_sphere_traits_2<Kernel_, atan_x, atan_y>` — `:2858–2989`

```cpp
  using Approximate_number_type       = double;                                    // 2858
  using Approximate_kernel            = CGAL::Cartesian<Approximate_number_type>;  // 2859
  using Approximate_point_2           = Arr_extended_direction_3<Approximate_kernel>; // 2860
  using Approximate_kernel_vector_3   = Approximate_kernel::Vector_3;              // 2861
  using Approximate_kernel_direction_3= Approximate_kernel::Direction_3;           // 2862

  class Approximate_2 {                                            // 2864  (public, stateless)
  public:
    /*! \pre `i` is either 0 or 1.   [the code actually accepts i ∈ {0,1,2}] */
    Approximate_number_type operator()(const Point_2& p, int i) const {            // 2873
      CGAL_precondition((i == 0) || (i == 1) || (i == 2));
      return (i == 0) ? CGAL::to_double(p.dx()) :
        ((i == 1) ? CGAL::to_double(p.dy()) : CGAL::to_double(p.dz()));
    }
    Approximate_point_2 operator()(const Point_2& p) const;                        // 2881
    template <typename OutputIterator>
    OutputIterator operator()(const X_monotone_curve_2& xcv,
                              Approximate_number_type error,
                              OutputIterator oi, bool l2r = true) const;           // 2891
  };
  Approximate_2 approximate_2_object() const { return Approximate_2(); }           // 2989
```

The heart of the curve overload (2948–2962), verbatim:

```cpp
      // compute the number of divisions given the requested error
      const Approximate_number_type radius = 1.0; // radius is always 1
      Approximate_number_type dtheta = 2.0 * std::acos(1 - error / radius);
      auto num_segs = std::ceil(theta / dtheta);
      dtheta = theta / num_segs;

      // generate the points approximating the curve
      const auto loc = Approximate_point_2::NO_BOUNDARY_LOC;
      *oi++ = approximate_point_2(vs, loc); // source vector
      for (int i = 1; i < num_segs; ++i) {
        const Approximate_number_type angle = i * dtheta;
        auto p = std::cos(angle) * axis_x + std::sin(angle) * axis_y;
        *oi++ = approximate_point_2(p, loc);
      }
      *oi++ = approximate_point_2(vt, loc); // target vector
```

with, earlier (2903–2914), the direction handling:

```cpp
      if (xcv.is_directed_right() == l2r) {
        as = (*this)(s); at = (*this)(t);
        vn = Approximate_kernel_vector_3( dx,  dy,  dz);
      }
      else {
        as = (*this)(t); at = (*this)(s);
        vn = Approximate_kernel_vector_3(-dx, -dy, -dz);
      }
```

and `if (xcv.is_full()) theta = 2.0 * CGAL_PI;` (2938).

**Meaning of `error` on the sphere:** `dtheta = 2·acos(1 − error)` inverts
`sagitta = 1 − cos(dθ/2)` on the **unit** sphere. So `error` is the maximum 3-D Euclidean distance
between a chord and the great-circle arc it subtends, in the *unit-sphere* embedding — i.e. an
absolute length in a world whose radius is 1, **not** an angle and **not** a screen tolerance.
If you render on a sphere of radius `R`, pass `error_unit = error_world / R`.
Uniform subdivision (not adaptive): `n = ceil(theta/dtheta) + 1` points.

Emitted directions are unit length; see gotcha 17.

### 1.8 `Arr_polycurve_basic_traits_2<SubcurveTraits_2>` / `Arr_polycurve_traits_2` — `:1110–1144`

```cpp
  template <typename... Ts> using void_t = void;

  template <typename T, typename = void>
  struct has_approximate_2 {              // Generic implementation
    using Approximate_number_type = void;
    using Approximate_point_2     = void;
    using Approximate_2           = void;
  };

  template <typename T>
  struct has_approximate_2<T, void_t<typename T::Approximate_2>> {
    using Approximate_number_type = typename T::Approximate_number_type;
    using Approximate_2           = typename T::Approximate_2;
    using Approximate_point_2     = typename T::Approximate_point_2;
  };

  using Approximate_number_type =
    typename has_approximate_2<Subcurve_traits_2>::Approximate_number_type;
  using Approximate_2 =
    typename has_approximate_2<Subcurve_traits_2>::Approximate_2;
  using Approximate_point_2 =
    typename has_approximate_2<Subcurve_traits_2>::Approximate_point_2;

  Approximate_2 approximate_2_object_impl(std::false_type) const
  { return subcurve_traits_2()->approximate_2_object(); }

  Approximate_2 approximate_2_object_impl(std::true_type) const { }

  Approximate_2 approximate_2_object() const {
    using Is_void = typename std::is_same<void, Approximate_2>::type;
    return approximate_2_object_impl(Is_void());
  }
```

`Arr_polycurve_traits_2` adds nothing. See gotcha 9 for what this means in practice.

### 1.9 `Arr_curve_data_traits_2` / `Arr_consolidated_curve_data_traits_2`

`class Arr_curve_data_traits_2 : public Traits_` (`Arr_curve_data_traits_2.h:55`) — inherits
`Approximate_number_type`, `Approximate_kernel`, `Approximate_point_2`, `Approximate_2` and
`approximate_2_object()` unchanged. Its `X_monotone_curve_2` derives publicly from the base's, so
`base_approx(data_xcv, error, oi, l2r)` binds by reference. **[verified]** (gotcha 23).

### 1.10 / 1.11 The rest

| Traits | Whole-curve `Approximate_2` | Point `(p)` | Coordinate `(p,i)` | `Approximate_point_2` |
|---|---|---|---|---|
| `Arr_rational_function_traits_2` | — | — | Y (1275, **non-`const`**, returns `Bound`) | — |
| `Arr_algebraic_segment_traits_2` | — | — | — | — |
| `Arr_Bezier_curve_traits_2` | — | — | — | — |
| `Arr_circular_arc_traits_2` / `Arr_line_arc_traits_2` / `Arr_circular_line_arc_traits_2` | — | — | — | — |

---

## 2. Semantics of `error` and of the emitted sequence — **[verified]**

### 2.1 Per-traits summary table

| Traits | `error` used? | units | subdivision | points emitted | `l2r=false` |
|---|---|---|---|---|---|
| `Arr_segment_traits_2` | **no** | — | — | 2 (min, max) | full reverse |
| `Arr_non_caching_segment_basic_traits_2` | **no** | — | — | 2 | full reverse |
| `Arr_polyline_traits_2` | **no** | — | — | `#segments + 1` (exact vertices) | full reverse |
| `Arr_circle_segment_traits_2` (linear) | no | — | — | 2 | full reverse |
| `Arr_circle_segment_traits_2` (arc) | **yes** | absolute world units | adaptive bisection on the arc parameter; stop when chord sagitta `< error` | `2^k + 1` | full reverse |
| `Arr_conic_traits_2` (COLLINEAR) | no | — | — | 2 | full reverse |
| `Arr_conic_traits_2` (ellipse/hyperbola) | **yes** | absolute world units (Hausdorff) | adaptive bisection at `tm=(t1+t2)/2` | `2^k + 1` | full reverse |
| `Arr_conic_traits_2` (parabola) | **yes** | absolute world units (Hausdorff) | adaptive split at `tm = dx/dy` | irregular | full reverse |
| `Arr_geodesic_arc_on_sphere_traits_2` | **yes** | absolute, **unit-sphere** chordal sagitta | **uniform** `ceil(theta / 2·acos(1−error))` | `num_segs + 1` | full reverse (normal negated) |
| `Arr_polycurve_traits_2<Sub>` | n/a | n/a | n/a | **does not compile** | n/a |
| `Arr_linear_traits_2` | n/a | n/a | n/a | **no curve overload** | n/a |
| `Arr_Bezier_curve_traits_2` | n/a | n/a | n/a | **no `Approximate_2`** | n/a |

### 2.2 Both endpoints, always

Every implementation writes `*oi++ = src` before the recursion / loop and `*oi++ = trg` after it
(`Arr_segment_traits_2.h:934–935`, `Arr_circle_segment_traits_2.h:535`/`543`,
`Arr_conic_traits_2.h:1825`/`1833`, `1935`/`1943`, `2036`/`2044`,
`Arr_geodesic_arc_on_sphere_traits_2.h:2955`/`2961`). The polyline traits emits every polyline
vertex including both ends.

⇒ To build a CCB polyline, drop the first point of every edge after the first
(or the last point of every edge except the last).

### 2.3 `l2r`

`l2r = true` means "min vertex first". `l2r = false` swaps `src`/`trg` **and** therefore reverses
the interior points too, because the recursion / the parameter sweep is driven from `src` to `trg`.
**[verified]** in every family; the sphere additionally negates the great-circle normal so that the
interior points sweep the other way (2913).

`draw_arrangement_2.h` passes `l2r = (curr->direction() == ARR_LEFT_TO_RIGHT)` when drawing a face
(213) and the **default `true`** when drawing an isolated ("antenna") edge (432).

### 2.4 Measured counts vs. `error` — **[verified]**

Half-circle of radius 5, `(-5,0) → (5,0)`, `Arr_circle_segment_traits_2<Epeck_with_sqrt>`:

| `error` | points | max chord length | measured max deviation |
|---|---|---|---|
| 1 | 5 | 3.827 | 3.806e-01 |
| 0.1 | 9 | 1.951 | 9.607e-02 |
| 0.01 | 33 | 0.4907 | 6.023e-03 |
| 0.001 | 129 | 0.1227 | 3.765e-04 |

Ellipse `x² + 4y² − 16 = 0` (a=4, b=2), lower x-monotone half, `Arr_conic_traits_2`:

| `error` | points | measured Hausdorff |
|---|---|---|
| 1 | 3 | 7.410e-01 |
| 0.1 | 9 | 7.281e-02 |
| 0.01 | 29 | 9.950e-03 |
| 0.001 | 81 | 9.860e-04 |

Parabola `y = x²` from `(-2,4)` to `(2,4)`:

| `error` | points | measured Hausdorff |
|---|---|---|
| 1 | 3 | 4.472e-01 |
| 0.1 | 7 | 7.906e-02 |
| 0.01 | 21 | 9.761e-03 |
| 0.001 | 63 | 9.760e-04 |

Hyperbola `x² − y² = 1`, lower half of the right branch, `(1,0) → (1.25,−0.75)`:
`error = 1 → 2 pts`, `0.1 → 2`, `0.01 → 5`, `0.001 → 9` (the whole arc's sagitta is only 0.054).

Quarter great circle `(1,0,0) → (0,1,0)`, `Arr_geodesic_arc_on_sphere_traits_2<Epeck>`:

| `error` | points | measured max chord sagitta | closed form |
|---|---|---|---|
| 1 | 2 | 2.928932e-01 | `1 − cos(π/4)` ✓ |
| 0.1 | 3 | 7.612047e-02 | `1 − cos(π/8·2)` ✓ |
| 0.01 | 7 | 8.555139e-03 | ✓ |
| 0.001 | 19 | 9.517784e-04 | ✓ |
| 2 | 2 | — | `acos(−1)` ⇒ `dtheta = 2π` |
| 2.5 | 2 | — | `acos(−1.5)` ⇒ **NaN** |
| 0 | ∞ (hangs) | — | `dtheta = 0` ⇒ `num_segs = inf` |

Scaling rule of thumb for the adaptive families: chord count `∝ sqrt(curvature_radius / error)`,
rounded up to a power of two.

### 2.5 Is the polyline actually within `error`? — **yes** **[verified]**

Numerically (dense sampling of the exact parametrisation, nearest-distance to the emitted
polyline): every measurement in §2.4 is strictly below the requested `error`, typically by a
factor 1.3–7 (the bisection overshoot). This is what the algorithm guarantees: it only stops
subdividing a chord whose true maximum deviation (the tangent-parallel point, gotcha 19) is
`< error`.

Exactly, with `Compare_y_at_x_2` (`Arr_circle_segment_traits_2`, lower half of `x²+y²=25`):

| `error` | pts | chord midpoints vs. arc | emitted vertices vs. arc |
|---|---|---|---|
| 0.5 | 5 | 4 `LARGER`, 0 `SMALLER`, 0 `EQUAL` | 3 `SMALLER`, 0 `LARGER`, **2 `EQUAL`** |
| 0.05 | 17 | 16 `LARGER`, 0 `SMALLER`, 0 `EQUAL` | 5 `SMALLER`, 10 `LARGER`, **2 `EQUAL`** |

Reading: every chord midpoint is on the *inside* of the arc (as convexity demands, so the polyline
never crosses the curve), and only the **two endpoints** are exactly on it. See gotcha 18.

### 2.6 Degenerate `error`

Clamp in the binding layer:

```cpp
inline double sane_error(double e, double diag /* world diagonal of the viewport */) {
  if (!(e > 0) || !std::isfinite(e)) e = 1e-3 * diag;   // 0, negative, NaN, inf
  return std::max(e, 1e-9 * diag);                      // avoid 2^40 chords
}
// on the sphere, additionally: e = std::min(e, 1.999);
```

### 2.7 Output iterator requirement

Use `std::back_insert_iterator` (or any iterator whose `operator++` on a *copy* still appends to
the same container). See gotcha 6 for the measured failure with a raw pointer.

---

## 3. Traits with no whole-curve `Approximate_2` — drop-in replacements

### 3.1 `Arr_Bezier_curve_traits_2` — verified error-bounded replacement

`Curve_2::sample()` (`Bezier_curve_2.h:444–487`) is what `traits_bezier.md` §10.2 points at:

```cpp
  /*! samples a portion of the curve (for drawing purposes, etc.).
   * \param t_start The t-value to start with.
   * \param t_end The t-value to end at.
   * \param n_samples The required number of samples.
   * \param oi Output: An output iterator for the samples. The value-type of
   *                   this iterator must be std::pair<double, double>.
   * \return A past-the-end iterator for the samples.
   */
  template <class OutputIterator>
  OutputIterator sample (const double& t_start, const double& t_end,
                         unsigned int n_samples, OutputIterator oi) const;
```

It converts the control points to `Simple_cartesian<double>`, clamps
`const unsigned int n = (n_samples >= 2) ? n_samples : 2;` (**[verified]** `n_samples = 1` yields
2 points) and evaluates `point_on_Bezier_curve_2` at `n` uniformly spaced parameters. **Uniform,
no error control, no knowledge of the x-monotone sub-range.** Turn it into this:

```cpp
// ---------------------------------------------------------------------------
// Error-bounded approximation of a Bezier X_monotone_curve_2.
// Guarantee: every emitted chord is within `error` of the corresponding sub-arc
// (flatness / convex-hull bound), and the emitted sequence obeys the same
// contract as CGAL's Approximate_2: both endpoints, min-vertex first when l2r.
// [verified] measured Hausdorff < error for error in {1, 0.1, 0.01, 0.001}.
// ---------------------------------------------------------------------------
struct P2 { double x, y; };

static void split_at(const std::vector<P2>& c, double u,
                     std::vector<P2>& L, std::vector<P2>& R) {   // de Casteljau at u
  std::vector<P2> v = c;
  const size_t n = v.size();
  L.clear(); R.assign(n, P2{0,0});
  L.push_back(v[0]);
  R[n-1] = v[n-1];
  for (size_t last = n-1; last > 0; --last) {
    for (size_t i = 0; i < last; ++i)
      v[i] = P2{ v[i].x + u*(v[i+1].x - v[i].x), v[i].y + u*(v[i+1].y - v[i].y) };
    L.push_back(v[0]);
    R[last-1] = v[last-1];
  }
}
static std::vector<P2> restrict_to(const std::vector<P2>& c, double a, double b) {
  std::vector<P2> L, R, L2, R2;
  split_at(c, a, L, R);                          // R spans [a,1]
  const double u = (b - a) / (1.0 - a);
  if (!(u > 0 && u < 1)) return R;
  split_at(R, u, L2, R2);                        // L2 spans [a,b]
  return L2;
}
static double flatness(const std::vector<P2>& c) {   // max |control pt -> chord line|
  const P2& p0 = c.front(); const P2& pn = c.back();
  const double dx = pn.x - p0.x, dy = pn.y - p0.y, L = std::hypot(dx, dy);
  double m = 0;
  for (size_t i = 1; i + 1 < c.size(); ++i) {
    const double d = (L < 1e-15)
      ? std::hypot(c[i].x - p0.x, c[i].y - p0.y)
      : std::abs((c[i].x - p0.x)*dy - (c[i].y - p0.y)*dx) / L;
    m = std::max(m, d);
  }
  return m;
}
static void emit(const std::vector<P2>& c, double error, int depth, std::vector<P2>& out) {
  if (depth >= 32 || flatness(c) < error) { out.push_back(c.back()); return; }
  std::vector<P2> L, R;
  split_at(c, 0.5, L, R);
  emit(L, error, depth+1, out);
  emit(R, error, depth+1, out);
}

template <typename Xcv, typename OutputIterator>
OutputIterator approximate_bezier(const Xcv& xcv, double error,
                                  OutputIterator oi, bool l2r = true) {
  const auto& B = xcv.supporting_curve();                       // const Curve_2&
  std::vector<P2> ctrl;
  for (auto it = B.control_points_begin(); it != B.control_points_end(); ++it)
    ctrl.push_back(P2{ CGAL::to_double(it->x()), CGAL::to_double(it->y()) });

  const auto pr = xcv.parameter_range();                        // std::pair<double,double>
  const bool src_is_left = xcv.is_directed_right();
  double ta = src_is_left ? pr.first  : pr.second;              // t of the LEFT endpoint
  double tb = src_is_left ? pr.second : pr.first;               // t of the RIGHT endpoint
  if (!l2r) std::swap(ta, tb);
  bool rev = false;
  if (ta > tb) { std::swap(ta, tb); rev = true; }

  std::vector<P2> sub = restrict_to(ctrl, ta, tb);
  std::vector<P2> out;
  out.push_back(sub.front());
  emit(sub, error, 0, out);
  if (rev) std::reverse(out.begin(), out.end());
  for (const auto& p : out) *oi++ = p;
  return oi;
}
```

**Guarantee it gives.** A Bézier segment lies in the convex hull of its control points
(`de_Casteljau_2.h` header comment: "the control polygons converge to the curve"). `flatness()`
bounds the distance from every control point of a sub-arc to the chord's supporting *line*; the
sub-arc's endpoints are the chord's endpoints; hence every point of the sub-arc is within
`error` of the chord line, and (for a flat piece) within `error` of the chord *segment*. This is
the standard flatness criterion and it is one-sided-conservative.

**Measured** — cubic with control points `(0,0),(6,4),(-2,6),(4,10)`, 3 x-monotone pieces:

| piece | `dir_right` | `parameter_range()` | `left().is_exact()` / `right()` | `error=1` | `0.1` | `0.01` | `0.001` |
|---|---|---|---|---|---|---|---|
| 0 | 1 | (0, 0.3125) | true / **false** | 2 pts, H=5.15e-1 | 5, 3.28e-2 | 15, 7.19e-3 | 33, 5.19e-4 |
| 1 | 0 | (0.3125, 0.625) | **false / false** | 2, 9.02e-2 | 3, 5.73e-2 | 8, 5.42e-3 | 23, 6.43e-4 |
| 2 | 1 | (0.625, 1) | **false** / true | 3, 1.81e-1 | 5, 4.74e-2 | 16, 6.92e-3 | 36, 7.45e-4 |

(`H` = measured one-sided Hausdorff distance to the true sub-arc. Every value is below the
requested `error`.) `l2r = false` reverses (**[verified]**, piece 0: `first=(2.37793,3.28613)`,
`last=(0,0)`).

**Endpoint policy — pick one and apply it everywhere** (gotcha 12):

* *Curve-consistent* (what the routine above does): endpoints are `B(ta)`, `B(tb)` from the same
  double evaluation as the interior. Adjacent x-monotone pieces of the **same** supporting curve
  join exactly (**[verified]**: piece 0's last point `(2.37793, 3.28613)` == piece 1's first).
  Different curves meeting at an arrangement vertex will *not* join.
* *Vertex-consistent* (recommended for a DCEL renderer): compute the polyline as above, then
  overwrite `out.front()` / `out.back()` with the halfedge's own vertices,
  `he->source()->point().approximate()` / `he->target()->point().approximate()`. Everything meets
  at one rendered point; the price is a first/last chord that can be off by the bbox half-diagonal
  of an inexact `_Bezier_point_2`.
* If you need both: call `p.make_exact(cache)` (`Bezier_point_2.h:725`) on the vertex points once,
  after which `approximate()` uses the exact algebraic coordinates. Expensive; do it lazily and
  cache — and remember the traits' `Bezier_cache` must outlive everything (see
  `traits_bezier.md` §0 gotcha 7).

### 3.2 `Arr_polycurve_traits_2<arc sub-traits>` — approximate the subcurves

`poly_traits.approximate_2_object()` returns the **subcurve** functor and does not accept a
polycurve (gotcha 9, **[verified]**). This is the correct replacement — it also confirms note 4 of
`traits_segment_linear_polyline.md` §12:

```cpp
// Approximate a polycurve X_monotone_curve_2 by approximating each subcurve.
// Contract-compatible with CGAL's Approximate_2 (both endpoints, l2r order, no
// duplicated interior joints).
template <typename PolyTraits, typename OutputIterator>
OutputIterator approximate_polycurve(const PolyTraits& poly,
                                     const typename PolyTraits::X_monotone_curve_2& xcv,
                                     double error, OutputIterator oi, bool l2r = true) {
  using AP  = typename PolyTraits::Approximate_point_2;     // == Sub::Approximate_point_2
  const auto* sub = poly.subcurve_traits_2();
  auto approx = sub->approximate_2_object();                // the SUBCURVE functor
  const std::size_t n = xcv.number_of_subcurves();
  std::vector<AP> buf;
  bool first = true;
  for (std::size_t k = 0; k < n; ++k) {
    const std::size_t i = l2r ? k : (n - 1 - k);
    buf.clear();
    approx(xcv[i], error, std::back_inserter(buf), l2r);    // xcv[i] is the i-th subcurve
    for (std::size_t j = (first ? 0 : 1); j < buf.size(); ++j) *oi++ = buf[j];
    first = false;
  }
  return oi;
}
```

Two things to be careful about, both from `traits_segment_linear_polyline.md` §7/§9:

* `xcv[i]` / `xcv.subcurves_begin()` enumerate the subcurves in the polycurve's *stored* order,
  which is left-to-right when the polycurve is directed right. If
  `poly.compare_endpoints_xy_2_object()(xcv) == LARGER` the stored order is right-to-left, so
  invert the `l2r` mapping above. (For polycurves built through
  `poly.construct_x_monotone_curve_2_object()` from left-to-right subcurves this does not arise.)
* The `l2r` flag must be forwarded to the subcurve call as well, otherwise you reverse the
  subcurve *order* but not the points inside each subcurve.

For `Arr_polycurve_traits_2<Arr_segment_traits_2<K>>` prefer `Arr_polyline_traits_2` — it has a
real whole-curve `Approximate_2` (§1.3).

### 3.3 `Arr_linear_traits_2` — there is nothing to salvage

No `Approximate_point_2`, no `(p)` overload, no curve overload, and half the curves have no
endpoints at all. Rendering a linear arrangement is a *clipping* problem, not an approximation
problem. §5.5 gives the routine.

### 3.4 `Arr_non_caching_segment_basic_traits_2` — nothing needed

It has the complete functor (§1.2, gotcha 10).

---

## 4. What `CGAL/draw_arrangement_2.h` actually does — **[verified]**

### 4.1 The SFINAE probe that is switched off

`draw_arrangement_2.h:127–153`, verbatim (note the `#if 0`):

```cpp
  /// Compile time dispatching
#if 0
    template <typename T, typename I = void>
    void draw_region_impl2(Halfedge_const_handle curr, T const&, long)
    { draw_exact_region(curr); }

    template <typename T, typename I>
    auto draw_region_impl2(Halfedge_const_handle curr, T const& approx, int) ->
      decltype(approx.template operator()<I>(X_monotone_curve{}, double{}, I{},
                                             bool{}), void())
    { draw_approximate_region(curr, approx); }

    template <typename T>
    void draw_region_impl1(Halfedge_const_handle curr, T const&, long)
    { draw_exact_region(curr); }

    template <typename T>
    auto draw_region_impl1(Halfedge_const_handle curr, T const& traits, int) ->
      decltype(traits.approximate_2_object(), void()) {
      using Approximate = typename Gt::Approximate_2;
      draw_region_impl2<Approximate, int>(curr, traits.approximate_2_object(), 0);
    }
#else
    template <typename T>
    void draw_region_impl1(Halfedge_const_handle curr, T const& traits, int)
    { draw_approximate_region(curr, traits.approximate_2_object()); }
#endif
```

The same `#if 0 … #else` pattern is repeated for points (261–289) and for isolated curves
(446–474). **The `long`-overload fallbacks — the only path to `draw_exact_region` /
`draw_exact_curve` — are never compiled.** So:

* `traits.approximate_2_object()` must exist ⇒ else `no member named 'approximate_2_object'`.
* `typename Gt::Approximate_point_2` must exist (used at 211 and 430 to declare the buffer).
* `Approximate_2` must have the `(p)` overload (285) and the
  `(xcv, double, OutputIterator, bool)` overload (214, 432).

### 4.2 Measured outcomes

| Arrangement | `add_to_graphics_scene(arr, gs)` |
|---|---|
| `Arr_segment_traits_2<Epeck>` (unit square) | **compiles and runs**; `gs.bounding_box() = 0 0 0 4 4 0` **[verified]** |
| `Arr_circle_segment_traits_2` | compiles (documented in `traits_circle_segment.md`:722) |
| `Arr_Bezier_curve_traits_2` | **compile error** ×3: `no member named 'approximate_2_object'` at `:152`, `:285`, `:473` **[verified]** |
| `Arr_linear_traits_2<Epeck>` | **compile error** ×3: `no type named 'Approximate_point_2'` at `:211`, `:430`; `no matching function for call to object of type 'Approximate_2'` at `:285` **[verified]** |
| `Arr_rational_function_traits_2` | compile error (no `Approximate_point_2`, `operator()` non-`const`) |
| `Arr_algebraic_segment_traits_2` | compile error (no `Approximate_2`) |

### 4.3 What it does when it *does* compile

`draw_approximate_region` (206–228), verbatim:

```cpp
    template <typename Approximate>
    void draw_approximate_region(Halfedge_const_handle curr,
                                 const Approximate& approx)
    {
      std::vector<typename Gt::Approximate_point_2> polyline;
      double error(0.01); // TODO? (this->pixel_ratio());
      bool l2r = curr->direction() == ARR_LEFT_TO_RIGHT;
      approx(curr->curve(), error, std::back_inserter(polyline), l2r);
      if (polyline.empty()) return;
      auto it = polyline.begin();
      auto prev = it++;
      for (; it != polyline.end(); prev = it++) {
        if(m_gso.draw_edge(m_aos, curr))
        {
          if(m_gso.colored_edge(m_aos, curr))
          { m_gs.add_segment(*prev, *it, m_gso.edge_color(m_aos, curr)); }
          else
          { m_gs.add_segment(*prev, *it); }
        }
        m_gs.add_point_in_face(*prev);
      }
    }
```

and `draw_approximate_curve` (425–444) for antenna edges, which passes only three arguments
(`approx(curve, error, std::back_inserter(polyline));`) — i.e. **always `l2r = true`**.

`draw_region` (90–125) picks a canonical starting halfedge with `find_smallest` (338–378), then

```cpp
      do {
        // Skip halfedges that are "antenas":
        while (curr->face() == curr->twin()->face()) curr = curr->twin()->next();
        draw_region_impl1(curr, *traits, 0);
        curr = curr->next();
      } while (curr != ext);
```

`add_faces` (247–253) for generic traits:

```cpp
      for (auto it=m_aos.unbounded_faces_begin(); it!=m_aos.unbounded_faces_end(); ++it)
      { add_face(it); }
```

and `add_face` / `add_ccb` (61–87) flood-fill through `curr->twin()->face()`.

**Neither `draw_region` nor `find_smallest` checks `is_fictitious()` or `is_at_open_boundary()`.**
`find_smallest` calls `curr->source()->point()` (359) and `curr->curve()` (372); `draw_region_impl1`
calls `curr->curve()` (214). On an unbounded arrangement all three would assert (or, under
`-DNDEBUG`, dereference `nullptr`). This is unreachable today only because no unbounded-capable
traits satisfies §4.1 — the code has no defence of its own.

### 4.4 Sphere specialisations

`draw_region_impl1<Kernel_, AtanX, AtanY>` (155–198) and `draw_curve_impl1` (476–510) hand-normalise
each emitted direction (`l = sqrt(x²+y²+z²)`; `Approx_point_3(x/l, y/l, z/l)`) before
`m_gs.add_segment`, and `add_faces` (256–258) does `add_face(m_aos.faces_begin())` only. The
`m_gs.add_point_in_face(*prev)` line is commented out (196) ⇒ no filled faces (gotcha 24).

### 4.5 Reproducing the (non-)degradation

The `#if 0` bodies show exactly what the intended probe was; if you want the degradation the docs
describe, you must implement it yourself. This is the C++17 form (the header's own comment at
93–107 says C++20 `requires` would be the elegant way):

```cpp
template <class...> using void_t_ = void;

template <class Gt, class = void> struct has_approx_pt : std::false_type {};
template <class Gt> struct has_approx_pt<Gt, void_t_<typename Gt::Approximate_point_2>>
  : std::true_type {};

template <class Gt, class Xcv, class OI, class = void>
struct has_curve_approx : std::false_type {};
template <class Gt, class Xcv, class OI>
struct has_curve_approx<Gt, Xcv, OI, void_t_<
    decltype(std::declval<const typename Gt::Approximate_2&>()(
               std::declval<const Xcv&>(), double{}, std::declval<OI>(), bool{}))>>
  : std::true_type {};

template <class Gt, class Xcv, class OI>
inline constexpr bool can_approximate_curve_v =
  has_approx_pt<Gt>::value && has_curve_approx<Gt, Xcv, OI>::value;
```

Then `if constexpr (can_approximate_curve_v<Gt, Xcv, OI>) { approx(...); } else { chord(min,max); }`
gives back the straight-chord fallback. Use this in the type-erased core's traits-probing layer
(§6.3) — never rely on CGAL doing it.

---

## 5. Unbounded and fictitious edges

### 5.1 The DCEL predicates, verbatim

`Arr_dcel_base.h` — `Arr_vertex_base<Point>`:

```cpp
  /*! checks if the point pointer is nullptr. */
  bool has_null_point() const { return (p_pt == nullptr); }          // 100

  /*! obtains the point (const version). */
  const Point& point() const                                        // 102
  {
    CGAL_assertion(p_pt != nullptr);                                //  ← 112 in the built header
    return (*p_pt);
  }

  /*! obtains the boundary type in x. */
  Arr_parameter_space parameter_space_in_x() const                  // 120
  { return (Arr_parameter_space(pss[0])); }

  /*! obtains the boundary type in y. */
  Arr_parameter_space parameter_space_in_y() const                  // 124
  { return (Arr_parameter_space(pss[1])); }
```

`Arr_halfedge_base<X_monotone_curve_>`:

```cpp
  /*! checks if the curve pointer is nullptr. */
  bool has_null_curve() const { return (p_cv == nullptr); }          // 186

  /*! obtains the x-monotone curve (const version). */
  const X_monotone_curve& curve() const                              // 189
  {
    CGAL_precondition(p_cv != nullptr);                              //  ← 199
    return (*p_cv);
  }
```

`Arr_face_base`:

```cpp
  enum { IS_UNBOUNDED = 1, IS_FICTITIOUS = 2 };
  bool is_unbounded()  const { return ((flags & IS_UNBOUNDED)  != 0); }   // 257
  bool is_fictitious() const { return ((flags & IS_FICTITIOUS) != 0); }   // 264
```

`Arrangement_on_surface_2.h` — the *public* wrappers:

```cpp
  class Vertex : public DVertex {
  public:
    /*! Check whether the vertex lies on an open boundary. */
    bool is_at_open_boundary() const { return (Base::has_null_point()); }   // 582
    Size degree() const;                                                    // 585
    Halfedge_around_vertex_const_circulator incident_halfedges() const;     // 617  \pre !is_isolated()
    Face_const_handle face() const;                                         // 636  \pre is_isolated()
  private:
    bool has_null_point() const;          // ← deliberately made private
    void set_boundary(Arr_parameter_space, Arr_parameter_space);
  };

  class Halfedge : public DHalfedge {
  public:
    /*! checks whether the halfedge is fictitious. */
    bool is_fictitious() const { return (Base::has_null_curve()); }         // 667
    Vertex_const_handle   source() const;                                   // 675
    Vertex_const_handle   target() const;                                   // 683
    Face_const_handle     face()   const;                                   // 695
    Halfedge_const_handle twin()   const;                                   // 707
    Halfedge_const_handle prev()   const;                                   // 715
    Halfedge_const_handle next()   const;                                   // 723
    Ccb_halfedge_const_circulator ccb() const;                              // 731
  private:
    bool has_null_curve() const;          // ← private
  };
```

`Vertex::parameter_space_in_x()/_y()` and `Face::is_unbounded()/is_fictitious()` are **not**
blocked by the wrappers — they are inherited and public. `Halfedge::direction()` comes from
`Arr_halfedge_base` and returns `CGAL::Arr_halfedge_direction`
(`ARR_LEFT_TO_RIGHT` / `ARR_RIGHT_TO_LEFT`).

### 5.2 The iterator filters — what you never see (gotcha 14)

`Arrangement_on_surface_2.h:156–264`:

```cpp
  class _Is_concrete_vertex { bool operator()(const DVertex& v) const
    { return m_topol_traits->is_concrete_vertex(&v); } };        // 156
  class _Is_valid_vertex    { …is_valid_vertex(&v);   };         // 179
  class _Is_valid_halfedge  { …is_valid_halfedge(&he);};         // 202
  class _Is_valid_face      { …is_valid_face(&f);     };         // 225
  class _Is_unbounded_face  { return (m_topol_traits->is_valid_face(&f) &&
                                      m_topol_traits->is_unbounded(&f)); };  // 248
```

with (`Arr_unb_planar_topology_traits_2.h:150–190`):

```cpp
  bool is_concrete_vertex(const Vertex* v) const { return (! v->has_null_point()); }   // 150
  Size number_of_concrete_vertices() const
  { return (this->m_dcel.size_of_vertices() - n_inf_verts); }                          // 156
  bool is_valid_vertex(const Vertex* v) const
  { return (! v->has_null_point() ||
            ((v != v_bl) && (v != v_tl) && (v != v_br) && (v != v_tr))); }             // 160
  Size number_of_valid_vertices() const
  { return (this->m_dcel.size_of_vertices() - 4); }                                    // 169
  bool is_valid_halfedge(const Halfedge* he) const { return (! he->has_null_curve()); }// 173
  Size number_of_valid_halfedges() const
  { return (this->m_dcel.size_of_halfedges() - 2*n_inf_verts); }                       // 180
  bool is_valid_face (const Face* f) const { return (! f->is_fictitious()); }          // 184
  Size number_of_valid_faces() const
  { return (this->m_dcel.size_of_faces() - 1); }                                       // 189
```

So `Vertex_iterator` = concrete vertices only; `Halfedge_iterator`/`Edge_iterator` = non-fictitious
halfedges only; `Face_iterator` = non-fictitious faces only.

Reaching what they hide:

```cpp
  Size number_of_vertices_at_infinity() const;                 // Arrangement_2.h:167
      // = number_of_valid_vertices() - number_of_concrete_vertices()
  Unbounded_face_iterator       unbounded_faces_begin();       // AOS2:1242
  Unbounded_face_iterator       unbounded_faces_end();         // AOS2:1249
  Unbounded_face_const_iterator unbounded_faces_begin() const; // AOS2:1256
  Unbounded_face_const_iterator unbounded_faces_end()   const; // AOS2:1264
  Size number_of_unbounded_faces() const;                      // AOS2:1004 (counts the range, O(F))
  Face_handle       fictitious_face();                         // AOS2:1271
  Face_const_handle fictitious_face() const;                   // AOS2:1281
  Face_handle       unbounded_face();                          // Arrangement_2.h:175 (one of them)
  Face_const_handle unbounded_face() const;                    // Arrangement_2.h:196
```

Fictitious halfedges and at-infinity vertices are reachable **only** through CCB circulators and
`Halfedge::source()/target()/twin()/next()/prev()`.

### 5.3 `Arr_linear_object_2<Kernel_>` — the accessors, verbatim

`Arr_linear_traits_2.h:1579–1721`. `X_monotone_curve_2 == Curve_2 == Arr_linear_object_2<Kernel>`
(`:500–501`).

```cpp
template <typename Kernel_>
class Arr_linear_object_2 :
    public Arr_linear_traits_2<Kernel_>::_Linear_object_cached_2
{
public:
  typedef Kernel_                                           Kernel;
  typedef typename Kernel::Point_2                          Point_2;
  typedef typename Kernel::Segment_2                        Segment_2;
  typedef typename Kernel::Ray_2                            Ray_2;
  typedef typename Kernel::Line_2                           Line_2;

  Arr_linear_object_2();                                    // 1597
  Arr_linear_object_2(const Point_2& s, const Point_2& t);  // 1604  \pre s != t
  Arr_linear_object_2(const Segment_2& seg);                // 1610  \pre not degenerate
  Arr_linear_object_2(const Ray_2& ray);                    // 1616  \pre not degenerate
  Arr_linear_object_2(const Line_2& line);                  // 1622  \pre not degenerate

  /*! checks whether the object is actually a segment. */
  bool is_segment() const                                                     // 1626
  { return (! this->is_degen && this->has_source && this->has_target); }

  /*! casts to a segment.  \pre The linear object is really a segment. */
  Segment_2 segment() const;                                                  // 1632

  /*! checks whether the object is actually a ray. */
  bool is_ray() const                                                         // 1643
  { return (! this->is_degen && (this->has_source != this->has_target)); }

  /*! casts to a ray.  \pre The linear object is really a ray. */
  Ray_2 ray() const                                                           // 1649
  {
    CGAL_precondition(is_ray());
    Kernel kernel;
    Ray_2 ray = (this->has_source) ?
      kernel.construct_ray_2_object()(this->ps, this->l) :
      kernel.construct_ray_2_object()
        (this->pt, kernel.construct_opposite_line_2_object()(this->l));
    return ray;
  }

  /*! checks whether the object is actually a line. */
  bool is_line() const                                                        // 1663
  { return (! this->is_degen && ! this->has_source && ! this->has_target); }

  /*! casts to a line.  \pre The linear object is really a line. */
  Line_2 line() const                                                         // 1669
  { CGAL_precondition(is_line()); return (this->l); }

  /*! obtains the supporting line.  \pre The object is not a point. */
  const Line_2& supporting_line() const                                       // 1678
  { CGAL_precondition(! this->is_degen); return (this->l); }

  /*! obtains the source point.  \pre The object is a point, a segment or a ray. */
  const Point_2& source() const;                                              // 1687

  /*! obtains the target point.  \pre The object is a point or a segment. */
  const Point_2& target() const;                                              // 1699

  /*! creates a bounding box for the linear object.  \pre is_segment() */
  Bbox_2 bbox() const;                                                        // 1707
};
```

Inherited from `Arr_linear_traits_2<Kernel>::_Linear_object_cached_2` (public):

```cpp
    Arr_parameter_space left_infinite_in_x()  const;   // 227  ARR_LEFT_BOUNDARY | ARR_INTERIOR
    Arr_parameter_space left_infinite_in_y()  const;   // 242  ARR_BOTTOM/TOP_BOUNDARY | ARR_INTERIOR
    bool has_left() const { return (is_right ? has_source : has_target); }        // 259
    const Point_2& left() const;                       // 266  \pre has_left()
    void set_left(const Point_2& p, bool check_validity = true);                  // 275
    void set_left();                                   // 294  (make it unbounded)
    Arr_parameter_space right_infinite_in_x() const;   // 306
    Arr_parameter_space right_infinite_in_y() const;   // 320
    bool has_right() const { return (is_right ? has_target : has_source); }       // 341
    const Point_2& right() const;                      // 348  \pre has_right()
    void set_right(const Point_2& p, bool check_validity = true);                 // 357
    void set_right();                                  // 376
    const Line_2& supp_line() const;                   // 386  \pre ! is_degenerate()
    bool is_vertical() const;                          // 396  \pre ! is_degenerate()
    bool is_degenerate() const { return (is_degen); }  // 404
    bool is_directed_right() const { return (is_right); }                          // 408
    bool is_in_x_range(const Point_2& p) const;        // 415
```

**The renderer should use `has_left()/left()/has_right()/right()/supporting_line()`, not
`is_segment()/is_ray()/is_line()`** — the four-way `has_left × has_right` split is the same
information without the casting preconditions, and the `Ray_2` returned by `ray()` is
*constructed* (a fresh kernel object, extra exact arithmetic), while `supporting_line()` returns a
`const&` into the curve.

`is_directed_right()` is a property of the *curve*, independent of `Halfedge::direction()`.

Category tags that make all this happen (`Arr_linear_traits_2.h:59–63`):

```cpp
  typedef Arr_open_side_tag               Left_side_category;
  typedef Arr_open_side_tag               Bottom_side_category;
  typedef Arr_open_side_tag               Top_side_category;
  typedef Arr_open_side_tag               Right_side_category;
```

⇒ `Arrangement_2<Arr_linear_traits_2<K>>` selects `Arr_unb_planar_topology_traits_2`.

### 5.4 Measured anatomy of a 3-line arrangement — **[verified]**

`Arrangement_2<Arr_linear_traits_2<Epeck>>`, lines `y=0`, `x=0`, `x+y=4`:

```
V=3  V@inf=6  E=9  F=7  unbounded F=6
```

* `arr.vertices_begin()..end()` yields **3** vertices, all with `is_at_open_boundary() == false`
  and `parameter_space_in_x() == parameter_space_in_y() == ARR_INTERIOR (4)`.
* `arr.halfedges_begin()..end()` reports `real = 18, fictitious = 0`.
* Walking every face's outer CCB visits **10 fictitious halfedges** and **16 open-boundary
  vertices**.
* `arr.fictitious_face()->is_fictitious() == 1`, `number_of_inner_ccbs() == 1` (it holds everything
  else in a single hole). It is **not** produced by `faces_begin()`.
* `arr.unbounded_face()->is_unbounded() == 1`; `unbounded_faces_begin()..end()` counts **6**.
* Open-boundary vertices seen (`ps_x`, `ps_y`, `degree()`):
  `(INTERIOR, BOTTOM, 3)`, `(RIGHT, BOTTOM, 2)`, `(RIGHT, BOTTOM, 3)`, `(LEFT, INTERIOR, 3)`,
  `(LEFT, BOTTOM, 2)`, `(LEFT, TOP, 3)`, `(INTERIOR, TOP, 3)`, `(LEFT, TOP, 2)`,
  `(RIGHT, INTERIOR, 3)` … — note the two distinct `(RIGHT, BOTTOM)` vertices (gotcha 15).
* The 9 edges: 3 segments (`has_left && has_right`), 6 rays (exactly one of them). No `is_line()`
  edge survives insertion — a full line is always split at its first crossing.
* `v->point()` on an open-boundary vertex ⇒ `CGAL ERROR: assertion violation! Expr: p_pt != nullptr`
  (`Arr_dcel_base.h:112`).
* `he->curve()` on a fictitious halfedge ⇒
  `CGAL ERROR: precondition violation! Expr: p_cv != nullptr` (`Arr_dcel_base.h:199`).

### 5.5 Clipping a linear object to the viewport — **[verified]**

```cpp
struct Box { double xmin, ymin, xmax, ymax; };
struct P2  { double x, y; };
static const double INF = std::numeric_limits<double>::infinity();

// Clip one Arr_linear_object_2 (segment / ray / line) to `b`.
// Returns the clipped chord in LEFT-TO-RIGHT (lexicographic) order; false if empty.
template <class Xcv, class K>
bool clip_linear(const Xcv& cv, const Box& b, P2& out_a, P2& out_c) {
  const typename K::Line_2& l = cv.supporting_line();          // const&, no construction
  typename K::Point_2 p0 = l.projection(typename K::Point_2(0, 0));
  double px = CGAL::to_double(p0.x()), py = CGAL::to_double(p0.y());

  typename K::Vector_2 v = l.to_vector();
  double dx = CGAL::to_double(v.x()), dy = CGAL::to_double(v.y());
  if (dx < 0 || (dx == 0 && dy < 0)) { dx = -dx; dy = -dy; }   // lexicographically increasing
  const double n = std::hypot(dx, dy);  dx /= n;  dy /= n;

  auto param = [&](const typename K::Point_2& q) {
    return (CGAL::to_double(q.x()) - px) * dx + (CGAL::to_double(q.y()) - py) * dy;
  };
  double t0 = cv.has_left()  ? param(cv.left())  : -INF;       // NEVER touch left()  without has_left()
  double t1 = cv.has_right() ? param(cv.right()) :  INF;       // NEVER touch right() without has_right()

  auto clip = [&](double p, double q) {                        // Liang-Barsky:  p*t <= q
    if (p == 0) return q >= 0;
    const double r = q / p;
    if (p < 0) { if (r > t1) return false; if (r > t0) t0 = r; }
    else       { if (r < t0) return false; if (r < t1) t1 = r; }
    return true;
  };
  if (!clip(-dx, px - b.xmin)) return false;
  if (!clip( dx, b.xmax - px)) return false;
  if (!clip(-dy, py - b.ymin)) return false;
  if (!clip( dy, b.ymax - py)) return false;
  if (t0 > t1) return false;

  out_a = { px + t0 * dx, py + t0 * dy };
  out_c = { px + t1 * dx, py + t1 * dy };
  return true;
}
```

Notes:
* `Line_2::projection(p)` (`Line_2.h:139`) is always available and needs no preconditions; it is the
  cheap way to get *a* point on the supporting line of a line/ray/segment alike.
* `Line_2::to_vector()` (`Line_2.h:127`) returns `(b, -a)`; its sign is not normalised, hence the
  explicit lexicographic flip.
* If you want exactness up to the clip, do the Liang–Barsky in the kernel's `FT` and only
  `to_double` the two survivors. The version above is `double` throughout and is what the measured
  test used.
* To orient the chord along the *halfedge* rather than the curve:
  `if (he->direction() == CGAL::ARR_RIGHT_TO_LEFT) std::swap(a, c);`

### 5.6 Rendering a FACE of an unbounded arrangement — **[verified]**

The rule: **skip fictitious halfedges entirely** (they carry no curve), clip the real ones, and
close each resulting gap by walking the viewport rectangle counter-clockwise from where the
boundary left it to where it comes back. An outer CCB is CCW, so CCW is always the right way round;
inner CCBs (holes) are bounded and never produce gaps.

```cpp
// perimeter parameter of a point on the box border, CCW from (xmin,ymin); -1 if not on it
static double perim(const P2& p, const Box& b) {
  const double W = b.xmax - b.xmin, H = b.ymax - b.ymin, e = 1e-9 * (W + H);
  if (std::abs(p.y - b.ymin) <= e && p.x <  b.xmax - e) return p.x - b.xmin;
  if (std::abs(p.x - b.xmax) <= e && p.y <  b.ymax - e) return W + (p.y - b.ymin);
  if (std::abs(p.y - b.ymax) <= e && p.x >  b.xmin + e) return W + H + (b.xmax - p.x);
  if (std::abs(p.x - b.xmin) <= e)                      return 2*W + H + (b.ymax - p.y);
  if (std::abs(p.y - b.ymin) <= e) return p.x - b.xmin;                 // corner fallbacks
  if (std::abs(p.x - b.xmax) <= e) return W + (p.y - b.ymin);
  if (std::abs(p.y - b.ymax) <= e) return W + H + (b.xmax - p.x);
  return -1;
}
static P2 corner_at(double s, const Box& b) {
  const double W = b.xmax - b.xmin, H = b.ymax - b.ymin;
  if (s == 0)   return { b.xmin, b.ymin };
  if (s == W)   return { b.xmax, b.ymin };
  if (s == W+H) return { b.xmax, b.ymax };
  return { b.xmin, b.ymax };
}
static void walk_border(double s_from, double s_to, const Box& b, std::vector<P2>& out) {
  const double W = b.xmax - b.xmin, H = b.ymax - b.ymin, P = 2*(W+H);
  const double cs[4] = { 0, W, W+H, 2*W+H };
  const double d = std::fmod(s_to - s_from + P, P);
  std::vector<std::pair<double,int>> hits;
  for (int k = 0; k < 4; ++k) {
    const double dk = std::fmod(cs[k] - s_from + P, P);
    if (dk > 1e-9 && dk < d - 1e-9) hits.push_back({dk, k});
  }
  std::sort(hits.begin(), hits.end());
  for (const auto& h : hits) out.push_back(corner_at(cs[h.second], b));
}

template <class Arr, class K>
std::vector<P2> render_ccb(typename Arr::Ccb_halfedge_const_circulator circ, const Box& b) {
  std::vector<P2> poly;
  auto push = [&](const P2& p) {
    if (poly.empty() || std::abs(poly.back().x - p.x) > 1e-12
                     || std::abs(poly.back().y - p.y) > 1e-12) poly.push_back(p);
  };
  auto cur = circ;
  do {
    if (cur->is_fictitious()) continue;                 // ← curve() would assert here
    P2 a, c;
    if (!clip_linear<typename Arr::X_monotone_curve_2, K>(cur->curve(), b, a, c)) continue;
    P2 s = a, t = c;
    if (cur->direction() == CGAL::ARR_RIGHT_TO_LEFT) std::swap(s, t);
    if (!poly.empty()) {                                // close the gap along the border
      const double s1 = perim(poly.back(), b), s2 = perim(s, b);
      if (s1 >= 0 && s2 >= 0 &&
          (std::abs(poly.back().x - s.x) > 1e-9 || std::abs(poly.back().y - s.y) > 1e-9))
        walk_border(s1, s2, b, poly);
    }
    push(s); push(t);
  } while (++cur != circ);

  if (poly.size() >= 2) {                               // wrap-around closure
    const double s1 = perim(poly.back(), b), s2 = perim(poly.front(), b);
    if (s1 >= 0 && s2 >= 0 &&
        (std::abs(poly.back().x - poly.front().x) > 1e-9 ||
         std::abs(poly.back().y - poly.front().y) > 1e-9))
      walk_border(s1, s2, b, poly);
  }
  return poly;
}

// driver
for (auto f = arr.faces_begin(); f != arr.faces_end(); ++f) {     // fictitious face already filtered
  std::vector<P2> outline;
  if (f->has_outer_ccb()) outline = render_ccb<Arr,K>(f->outer_ccb(), box);
  for (auto h = f->inner_ccbs_begin(); h != f->inner_ccbs_end(); ++h) { /* holes: same call */ }
  for (auto v = f->isolated_vertices_begin(); v != f->isolated_vertices_end(); ++v) { /* points */ }
}
```

**Measured output** for the 3-line arrangement above with `box = {-6,-6, 10,10}`:

| face | unbounded | CCB real / fictitious | clipped pts | area | outline |
|---|---|---|---|---|---|
| 0 | 1 | 3 / 2 | 4 | 42 | `(10,-6)(4,0)(0,0)(0,-6)` |
| 1 | **0** | 3 / 0 | 4 | 8 | `(4,0)(0,4)(0,0)(4,0)` — the triangle |
| 2 | 1 | 2 / 2 | 4 | 36 | `(0,-6)(0,0)(-6,0)(-6,-6)` |
| 3 | 1 | 3 / 1 | 5 | 42 | `(0,4)(-6,10)(-6,0)(0,0)(0,4)` |
| 4 | 1 | 2 / 2 | 3 | 18 | `(-6,10)(0,4)(0,10)` |
| 5 | 1 | 3 / 2 | 6 | 92 | `(0,4)(4,0)(10,0)(10,10)(0,10)(0,4)` |
| 6 | 1 | 2 / 1 | 4 | 18 | `(4,0)(10,-6)(10,0)(4,0)` |

`Σ area = 256.0000` == `box area = 256.0000`, and every signed area is **positive** ⇒ all outer CCBs
came out CCW, no overlaps, no gaps. **[verified]**

### 5.7 Caveats of the routine above

1. **A face with no real edge inside the viewport renders empty.** That happens for the single
   unbounded face of an empty arrangement (its CCB is 4 fictitious halfedges), and for any
   unbounded face whose real edges are all off-screen. Fix: if the outline comes back empty and
   `f->is_unbounded()`, locate the box centre (`Arr_naive_point_location` /
   `Arr_trapezoid_ric_point_location`) and, if it lands in `f`, emit the whole box; otherwise emit
   nothing. Same fallback if a *bounded* face strictly contains the box.
2. **The gap-closing assumes an outer CCB.** For an inner CCB (a hole) the orientation is CW; a
   hole in an unbounded arrangement is always bounded, so it never has a gap and `walk_border` is
   never reached. Assert that, don't rely on it silently.
3. **Two separate "at infinity" excursions on one CCB** (possible with many parallel lines) are
   handled correctly: each gap is closed independently in CCB order because `walk_border` always
   goes CCW by the *shortest* CCW route and the excursions are disjoint arcs of the perimeter.
4. **`perim()` uses a relative epsilon.** Points must genuinely land on the border after clipping;
   if you clip in `FT` and round afterwards, snap the clipped coordinates to the box.
5. `walk_border`'s corner insertion is `O(4)` per gap; the whole routine is `O(|CCB|)` per face.

---

## 6. Renderer contract for the type-erased core

### 6.1 The one interface that covers all seven curve kinds

```cpp
// ---------------------------------------------------------------------------
// arr2::Tessellator — the single approximation entry point of the C++ core.
// Every geometry backend (segment, polyline, linear, circle-segment, conic,
// Bezier, sphere, polycurve) implements exactly this.
// ---------------------------------------------------------------------------
namespace arr2 {

struct Vec2 { double x, y; };
struct Vec3 { double x, y, z; };                 // sphere backend only

enum class Kind { Segment, Polyline, Linear, CircleSegment, Conic, Bezier, GeodesicSphere };

struct Viewport {                                // world-space clip box + resolution
  double xmin, ymin, xmax, ymax;
  double px_per_world;                           // 0 ⇒ "not a screen", use `error` as given
  double diag() const { return std::hypot(xmax - xmin, ymax - ymin); }
};

// The result of tessellating ONE halfedge.
struct Chain {
  std::vector<Vec2> pts;      // >= 2 when `ok`; world coordinates, in halfedge order
  bool ok        = false;     // false  ⇒ nothing to draw (fictitious, or clipped away)
  bool enters    = false;     // true   ⇒ pts.front() lies on the viewport border (came from ∞)
  bool exits     = false;     // true   ⇒ pts.back()  lies on the viewport border (goes to ∞)
};

class Tessellator {
public:
  virtual ~Tessellator() = default;

  // ---- required ---------------------------------------------------------
  virtual Kind kind() const noexcept = 0;

  /// True iff the backend can honour `error` (false for segment/polyline/linear:
  /// their output is exact and `error` is meaningless).
  virtual bool is_error_bounded() const noexcept = 0;

  /// True iff the backend can produce curves with no finite endpoint
  /// (only the Linear backend; drives the viewport-closing logic).
  virtual bool has_unbounded_curves() const noexcept = 0;

  /// Tessellate the x-monotone curve of `he`, in the halfedge's own direction,
  /// clipped to `vp`. MUST return ok == false for a fictitious halfedge.
  /// Postconditions when ok:
  ///   * pts.size() >= 2
  ///   * pts.front() is the halfedge's source side, pts.back() its target side
  ///   * every point of the true curve inside `vp` is within
  ///     effective_error(vp) of the chain (see is_error_bounded())
  virtual Chain edge(HalfedgeId he, const Viewport& vp, double error) const = 0;

  /// Approximation of a vertex. For an at-infinity vertex: returns false.
  virtual bool vertex(VertexId v, Vec2& out) const = 0;

  // ---- optional (sphere) ------------------------------------------------
  /// Non-null only when kind() == GeodesicSphere; then `edge()` returns the
  /// gnomonic/2-D projection and this returns the unit directions.
  virtual bool edge_3d(HalfedgeId, const Viewport&, double, std::vector<Vec3>&) const
  { return false; }
};

} // namespace arr2
```

### 6.2 Per-backend implementation of `edge()`

| `Kind` | body of `edge()` | `is_error_bounded()` | `has_unbounded_curves()` |
|---|---|---|---|
| `Segment` | `approx(he->curve(), 0.0, back_inserter(buf), l2r)` then clip the 1 chord to `vp` | `false` | `false` |
| `Polyline` | same call; clip the chain to `vp` (Sutherland–Hodgman on the open chain) | `false` | `false` |
| `Linear` | `if (he->is_fictitious()) return {};` then **§5.5** `clip_linear`; set `enters`/`exits` from `!cv.has_left()`/`!cv.has_right()` **or** from the clip having bitten | `false` (exact) | **`true`** |
| `CircleSegment` | `approx(he->curve(), e, back_inserter(buf), l2r)`, then clip the chain | `true` | `false` |
| `Conic` | idem | `true` | `false` |
| `Bezier` | **§3.1** `approximate_bezier(he->curve(), e, back_inserter(buf), l2r)`, then clip | `true` | `false` |
| `GeodesicSphere` | `approx(he->curve(), e_unit, back_inserter(buf), l2r)` → already unit 3-D directions; project for `edge()`, hand through for `edge_3d()` | `true` | `false` (but the topology has no unbounded face) |
| polycurve wrapper | **§3.2** `approximate_polycurve(...)` then delegate | = sub-traits | `false` |

`l2r` is always `he->direction() == CGAL::ARR_LEFT_TO_RIGHT`.

Every backend must:

1. `if (he->is_fictitious()) return Chain{};` — **before** touching `he->curve()` (gotcha 13).
   Only the `Linear` backend can ever see one, but make it unconditional; it costs one bit test and
   protects you if you later add an unbounded traits.
2. Use `std::back_insert_iterator` for CGAL's functor (gotcha 6).
3. Pass `sane_error(...)` (§2.6), never a raw user value (gotcha 7).
4. Drop `pts.front()` when concatenating into a CCB chain (gotcha 4).

### 6.3 Face outlines

```cpp
/// Outline of one CCB, viewport-clipped and closed. Returns an empty polygon
/// when the CCB contributes nothing (see §5.7 caveat 1).
std::vector<Vec2> ccb_outline(CcbId ccb, const Tessellator& T,
                              const Viewport& vp, double error);
```

Implementation = §5.6 with `T.edge()` in place of `clip_linear`, and the `walk_border` gap closing
enabled only when `T.has_unbounded_curves()` **or** any returned `Chain` has `enters`/`exits` set
(a bounded curve can still leave the viewport, and the same border walk closes that gap correctly).
For a bounded traits with a viewport that contains the whole arrangement, no gap ever appears and
the routine degenerates to plain concatenation.

Handle `f->is_fictitious()` defensively even though `faces_begin()` filters it, because
`he->twin()->face()` on a fictitious halfedge *does* hand you the fictitious face.

### 6.4 Choosing `error`

```cpp
// half a pixel in world units, floored so we never trip gotcha 7
inline double error_for(const Viewport& vp) {
  const double e = (vp.px_per_world > 0) ? (0.5 / vp.px_per_world) : (1e-3 * vp.diag());
  return std::max(e, 1e-9 * vp.diag());
}
// sphere backend, rendering on a sphere of radius R:
inline double sphere_error_for(const Viewport& vp, double R) {
  return std::min(error_for(vp) / R, 1.999);
}
```

This is exactly the `// TODO? (this->pixel_ratio())` that `draw_arrangement_2.h` never did
(gotcha 16). Expect the point counts of §2.4 to be the cost model: chords ≈
`sqrt(curvature_radius · px_per_world)`.

### 6.5 Cython-boundary shape

Return one flat `double[]` per face/edge batch plus an offsets array; the tessellator already
produces `std::vector<Vec2>`, so the buffer is `2 * pts.size()` doubles with no per-point Python
object. Mirror `Chain::ok/enters/exits` as three bit flags in a parallel `uint8[]` so the Python
layer can distinguish "clipped away" from "runs to infinity" without re-deriving it.

---

## 7. Verification index

| Claim | Program |
|---|---|
| segment / polyline / circle-segment counts, `l2r`, endpoints | `scratchpad/apimap_render_*/…` (`apimap_approx/test.cpp`) |
| conic ellipse / parabola / hyperbola counts + Hausdorff; raw-pointer OI failure | `apimap_render_conic/test.cpp` |
| sphere counts, unit length, `error ≥ 2`, point-overload non-normalisation | `apimap_render_sphere/test.cpp` |
| sphere `error = 0` hangs | `apimap_render_sphere0/test.cpp` |
| polycurve `Approximate_2` identity + compile failure | `apimap_render_polycurve/{a,b}.cpp` |
| `draw_arrangement_2.h` compile matrix | `apimap_render_draw/{lin,seg}.cpp`, `apimap_render_misc/bezdraw.cpp` |
| curve-data traits pass-through | `apimap_render_misc/cdata.cpp` |
| Bezier replacement + measurements | `apimap_render_bezier/test.cpp` |
| unbounded arrangement anatomy, clipping, face outlines, area check | `apimap_render_linear/test.cpp` |
| `Compare_y_at_x_2` exact cross-check, `error < 0` segfault | `apimap_render_exact/test.cpp` |

---

## 8. Corrections to the rest of the API-map set

| File | Existing statement | Correction |
|---|---|---|
| `traits_bezier.md` §0 gotcha 8 | "`CGAL::draw()` SFINAEs on `approximate_2_object()` and silently degrades to straight chords" | The SFINAE is `#if 0`'d out; it is a **hard compile error** (3 errors) **[verified]** |
| `traits_adapters_and_misc.md` §8 | "falls back to `draw_exact_curve` / `draw_exact_region` … when the traits has no approximation" | `draw_exact_curve` / `draw_exact_region` are **dead code**; the fallback overloads are inside `#if 0` |
| `traits_adapters_and_misc.md` line 1943 | "`approximate_2_object_impl(std::true_type) const { }` — UB if ever called" | Not UB: the return type deduces to `void`, the empty body is legal, and every *use site* is a compile error |
| `traits_segment_linear_polyline.md` §12 row `Approximate_2 … degrades to void` | correct, but incomplete | For a subcurve traits that *does* have `Approximate_2`, the polycurve traits exposes the **subcurve** functor, which cannot consume a polycurve — also a compile error **[verified]** |
| (gap description) `Arr_non_caching_segment_basic_traits_2` "no point overload, no curve overload" | — | Wrong for this install: it has all three overloads (`:251`, `:258`, `:264`) **[verified]** |
