# CGAL 6.1 — Linear-geometry traits API map

Scope: the *installed* headers at `/opt/homebrew/include/CGAL` (CGAL 6.1, git `b26b07a1242`,
release date 2025-09-29, header-only). Everything below is quoted or transcribed from the
actual header text; compile-verified claims are marked **[verified]** and were checked with

```
/usr/bin/clang++ -std=c++17 -O0 -DCGAL_USE_CORE -DCGAL_USE_GMP -DCGAL_USE_MPFR \
  -I/opt/homebrew/include -L/opt/homebrew/lib -lgmp -lmpfr -o test test.cpp
```

Files covered:

| File | Main entities |
|---|---|
| `CGAL/Arr_segment_traits_2.h` | `Arr_segment_traits_2<Kernel>`, `Arr_segment_2<Kernel>` |
| `CGAL/Arr_non_caching_segment_basic_traits_2.h` | `Arr_non_caching_segment_basic_traits_2<Kernel>` |
| `CGAL/Arr_non_caching_segment_traits_2.h` | `Arr_non_caching_segment_traits_2<Kernel>` |
| `CGAL/Arr_linear_traits_2.h` | `Arr_linear_traits_2<Kernel>`, `Arr_linear_object_2<Kernel>` |
| `CGAL/Arr_polycurve_basic_traits_2.h` | `Arr_polycurve_basic_traits_2<SubcurveTraits>` |
| `CGAL/Arr_polycurve_traits_2.h` | `Arr_polycurve_traits_2<SubcurveTraits>` |
| `CGAL/Arr_polyline_traits_2.h` | `Arr_polyline_traits_2<SegmentTraits>` |
| `CGAL/Arr_geometry_traits/Polycurve_2.h` | `internal::Polycurve_2`, `internal::X_monotone_polycurve_2` |
| `CGAL/Arr_geometry_traits/Polyline_2.h` | `polyline::Polyline_2`, `polyline::X_monotone_polyline_2` (**deprecated header**) |

---

## Gotchas / surprises vs. older CGAL

Read this section before writing any binding code.

1. **`Arr_polyline_traits_2::Curve_2` is no longer `polyline::Polyline_2`.**
   In 6.1 `Arr_polyline_traits_2<Seg>` derives from `Arr_polycurve_traits_2<Seg>` and its
   `Curve_2` / `X_monotone_curve_2` are `CGAL::internal::Polycurve_2<Subcurve_2, Point_2>` and
   `CGAL::internal::X_monotone_polycurve_2<X_monotone_subcurve_2, Point_2>`. The header
   `CGAL/Arr_geometry_traits/Polyline_2.h` (with `polyline::Polyline_2` /
   `polyline::X_monotone_polyline_2` and their `begin_segments()` / `number_of_segments()`
   names) is **deprecated** — it emits `CGAL_DEPRECATED_HEADER` and merely forwards to
   `Polycurve_2.h`. Use `subcurves_begin()` / `number_of_subcurves()`, and do *not* include
   `Polyline_2.h`.

2. **Copy-*assigning* a polycurve/polyline traits object double-frees. [verified]**
   `Arr_polycurve_basic_traits_2` holds `const Subcurve_traits_2* m_subcurve_traits; bool
   m_own_traits;`, has a user-written copy-ctor and a `delete`-ing destructor, but **no**
   `operator=`. The implicit copy-assignment copies the raw pointer *and* `m_own_traits ==
   true`, so both objects delete the same sub-traits. `Traits a, b; a = b;` aborts with a
   malloc error (exit 133). Copy-*construction* is safe (it allocates a fresh, default-
   constructed sub-traits when the source owns one — note that this also **silently discards
   the sub-traits' state**, e.g. a Bezier cache). For a type-erased core: store the traits in a
   `std::unique_ptr`, never copy-assign, and keep the traits alive for the whole lifetime of
   every arrangement, curve-functor and X-monotone curve derived from it.

3. **Two functions of `Arr_linear_traits_2` do not compile at all. [verified]**
   * `construct_opposite_2_object()(xcv)` → `Arr_linear_traits_2.h:644,646` call
     `xcv.get_pt()` / `xcv.get_ps()`, members that do not exist on `Arr_linear_object_2`.
   * `construct_curve_2_object()` → `Arr_linear_traits_2.h:1572` does
     `return Construct_x_monotone_curve_2(*this);` where that functor has no such ctor.
   Both are templates so they only break at instantiation. Do **not** expose them.
   `trim_2_object()` on linear traits *does* work (and always returns a bounded segment).

4. **`Arr_polycurve_traits_2::approximate_2_object()` cannot approximate a polycurve. [verified]**
   `Arr_polycurve_basic_traits_2` simply re-exports the *sub-curve* traits'
   `Approximate_2` (via a `has_approximate_2<>` SFINAE probe; if the sub-traits has none, the
   nested types become `void`). Only `Arr_polyline_traits_2` overrides `Approximate_2` with an
   overload taking the polycurve `X_monotone_curve_2`. So for "polycurves of arcs" you must
   approximate subcurve-by-subcurve yourself. Likewise
   `Arr_polycurve_traits_2::Number_of_points_2` **hides** the base overload and only accepts
   `Curve_2`; calling it with an `X_monotone_curve_2` compiles only when
   `Subcurve_2 == X_monotone_subcurve_2` (i.e. segments/polylines), and fails for conic/Bezier
   subcurves. [verified]

5. **Polycurves of arcs work.** `Arr_polycurve_traits_2` successfully wraps
   `Arr_circle_segment_traits_2`, `Arr_conic_traits_2` and `Arr_Bezier_curve_traits_2`;
   all three build a two-arc `X_monotone_curve_2` and insert into an `Arrangement_2` as a
   *single* edge (`V=2 E=1 F=1`). [verified — see "Polycurves of arcs" below] Caveats:
   `X_monotone_curve_2::bbox()` fails to compile for conic subcurves (the underlying
   `Conic_x_monotone_arc_2::bbox()` is deprecated *and* broken in 6.1); `trim_2_object()`
   requires the sub-traits to have `Trim_2` — the **default** sub-traits of
   `Arr_polycurve_basic_traits_2` (`Arr_non_caching_segment_traits_2<>`) has none, so
   `trim_2_object()` there does not compile. [verified]

6. **`std::variant` / `std::optional` everywhere.** `Make_x_monotone_2` writes
   `std::variant<Point_2, X_monotone_curve_2>` (not `CGAL::Object`); `Intersect_2` writes
   `std::variant<std::pair<Point_2, Multiplicity>, X_monotone_curve_2>`; kernel intersections
   are read with `std::get_if<...>(&*res)` on a `std::optional<std::variant<...>>`.

7. **Endpoint accessors return references into the curve.** `Arr_segment_traits_2` and the
   polycurve traits return `const Point_2&` from `Construct_min_vertex_2` /
   `Construct_max_vertex_2`; `Arr_non_caching_segment_traits_2` (the kernel's functor) returns
   **by value**. [verified] The polycurve functors declare the return type as
   `decltype(std::declval<Subcurve_ctr>().operator()(std::declval<X_monotone_subcurve_2>()))`,
   i.e. it follows the sub-traits. Never bind the result of `min_vertex(tmp_curve)` to a
   reference across statements.

8. **Two `Arr_polyline_traits_2` point-append overloads are broken. [verified]**
   * `Push_back_2::operator()(X_monotone_curve_2&, const Point_2&)` — typo
     `subcurves_traits_2()` (`Arr_polyline_traits_2.h:198`).
   * `Push_front_2::operator()(const X_monotone_curve_2&, Point_2&)` — takes the curve by
     `const&` then calls `push_front` on it (`Arr_polyline_traits_2.h:323`); note also the
     reversed const-ness of the arguments.
   The `Curve_2` (non-x-monotone) point overloads of both work fine.

9. **Polyline stream round-trip is lossy. [verified]** `operator<<` writes
   `number_of_subcurves()` followed by `n+1` points; `operator>>` reads that first number as a
   *point count*. Writing a 2-subcurve polyline and reading it back yields 1 subcurve. Do not
   use `<<`/`>>` for persistence — serialize the point/subcurve list yourself.

10. **Non-`const` `operator()` on some polycurve functors.** `Compare_x_2` and `Compare_xy_2`
    of `Arr_polycurve_basic_traits_2` declare their curve-end overloads
    (`(xs, ce, p)` and `(xs, ce, xs2, ce2)`) **without** `const`; `Arr_linear_traits_2::Trim_2`'s
    `operator()` is also non-`const` and takes its arguments **by value**. Store such functors
    by value in a mutable member, not as `const`.

11. **Side categories decide the topology traits.** `Arr_segment_traits_2` and the
    non-caching segment traits are `Arr_oblivious_side_tag` on all four sides → bounded planar
    topology. `Arr_linear_traits_2` is `Arr_open_side_tag` on all four sides → unbounded planar
    topology (fictitious edges, vertices at infinity; `number_of_vertices_at_infinity()`,
    `number_of_unbounded_faces()`). Polycurve traits *inherit* the four side categories from the
    sub-traits.

12. **No `min()`/`max()`/`vertex(i)`/`operator[]` on `Arr_segment_2`.** The accessors are
    `source()`, `target()`, `left()`, `right()`, `line()`, `is_vertical()`,
    `is_directed_right()`, `is_degenerate()`, `bbox()`, `flip()`, plus an implicit conversion
    `operator Kernel::Segment_2()`. `is_in_x_range()` / `is_in_y_range()` on the curve are
    `CGAL_DEPRECATED` — use the traits' `Is_in_x_range_2` / `Is_in_y_range_2` functors.

13. **Functor construction is private.** Nearly every stateful functor has a `protected`
    constructor taking `const Traits&` with `friend class <Traits>`. You *must* obtain them via
    `traits.xxx_2_object()`. They store a **reference** (`const Traits&`) or, in
    `Arr_non_caching_segment_traits_2`, a **pointer** (`const Traits*`) to the traits — so
    functors must not outlive the traits object.

14. **`Arr_polycurve_basic_traits_2` and `Arr_polycurve_traits_2` have different defaults.**
    `Arr_polycurve_basic_traits_2<SubcurveTraits_2 = Arr_non_caching_segment_traits_2<>>` vs.
    `Arr_polycurve_traits_2<SubcurveTraits_2 = Arr_segment_traits_2<>>` vs.
    `Arr_polyline_traits_2<SegmentTraits_2 = Arr_segment_traits_2<>>`.
    `Arr_segment_traits_2<Kernel_ = Exact_predicates_exact_constructions_kernel>` and
    `Arr_non_caching_segment_traits_2<Kernel_T = Epeck>` have kernel defaults;
    `Arr_linear_traits_2<Kernel_>` and `Arr_non_caching_segment_basic_traits_2<T_Kernel>` do **not**.

15. **`Construct_x_monotone_curve_2` from a point range is disabled for generic polycurves.**
    `Arr_polycurve_basic_traits_2::Construct_x_monotone_curve_2::constructor_impl(..., std::true_type)`
    is `CGAL_error_msg("Cannot construct a polycurve from a range of points!")` (a run-time
    abort, not a compile error). Same for `Arr_polycurve_traits_2::Construct_curve_2`. Only
    `Arr_polyline_traits_2` re-implements the point-range constructors.

---

## 0. Conventions used by all of these traits

* Every traits class exposes `Xxx_2 xxx_2_object() const` returning a functor **by value**.
* Category tags: `Has_left_category`, `Has_merge_category`, `Has_do_intersect_category`,
  and the four `*_side_category` typedefs.
* `Multiplicity` is `unsigned int` for all segment/linear traits (inherited by polycurves).
* `Make_x_monotone_2::operator()` dereference type: `std::variant<Point_2, X_monotone_curve_2>`.
* `Intersect_2::operator()` dereference type:
  `std::variant<std::pair<Point_2, Multiplicity>, X_monotone_curve_2>`.
* Preconditions are `CGAL_precondition` — compiled out unless `CGAL_CHECK_EXPENSIVE` /
  assertions are enabled. Your binding layer must validate inputs itself.

---

## 1. `Arr_segment_traits_2<Kernel_>`

```cpp
template <typename Kernel_ = Exact_predicates_exact_constructions_kernel>
class Arr_segment_traits_2 : public Kernel_ {
  friend class Arr_segment_2<Kernel_>;
```

*It publicly derives from the kernel*, so every kernel type and functor
(`Construct_point_2`, `Construct_segment_2`, `Intersect_2` of the kernel, …) is also visible on
the traits object. That is what makes `Construct_point_2` available to the polycurve wrapper.

### Public typedefs

```cpp
typedef Kernel_                         Kernel;
typedef typename Kernel::FT             FT;
typedef typename Algebraic_structure_traits<FT>::Is_exact   Has_exact_division;

typedef Tag_true                        Has_left_category;
typedef Tag_true                        Has_merge_category;
typedef Tag_false                       Has_do_intersect_category;

typedef Arr_oblivious_side_tag          Left_side_category;
typedef Arr_oblivious_side_tag          Bottom_side_category;
typedef Arr_oblivious_side_tag          Top_side_category;
typedef Arr_oblivious_side_tag          Right_side_category;

typedef typename Kernel::Line_2         Line_2;
typedef CGAL::Segment_assertions<Arr_segment_traits_2<Kernel> >  Segment_assertions;

typedef typename Kernel::Point_2        Point_2;          // line 231
typedef Arr_segment_2<Kernel>           X_monotone_curve_2;
typedef Arr_segment_2<Kernel>           Curve_2;          // same type!
typedef unsigned int                    Multiplicity;

// landmark support
typedef double                                        Approximate_number_type;
typedef CGAL::Cartesian<Approximate_number_type>      Approximate_kernel;
typedef Approximate_kernel::Point_2                   Approximate_point_2;

typedef Construct_x_monotone_curve_2  Construct_curve_2;  // alias
```

Constructor: `Arr_segment_traits_2() {}` (stateless, default-constructible, freely copyable).

Nested `class _Segment_cached_2` is the base of `Arr_segment_2` — documented under §2.

### Functors

All functors below whose declaration shows `m_traits` have a `protected` ctor
`Xxx_2(const Traits& traits)` and `friend class Arr_segment_traits_2<Kernel>`; obtain them with
the corresponding `*_object()`.

| Functor | `*_object()` | Signatures |
|---|---|---|
| `Compare_x_2` | `compare_x_2_object()` | `Comparison_result operator()(const Point_2& p1, const Point_2& p2) const` |
| `Compare_xy_2` | `compare_xy_2_object()` | `Comparison_result operator()(const Point_2& p1, const Point_2& p2) const` |
| `Construct_min_vertex_2` | `construct_min_vertex_2_object()` | `const Point_2& operator()(const X_monotone_curve_2& cv) const` — returns `cv.left()` |
| `Construct_max_vertex_2` | `construct_max_vertex_2_object()` | `const Point_2& operator()(const X_monotone_curve_2& cv) const` — returns `cv.right()` |
| `Is_vertical_2` | `is_vertical_2_object()` | `bool operator()(const X_monotone_curve_2& cv) const` |
| `Compare_y_at_x_2` | `compare_y_at_x_2_object()` | `Comparison_result operator()(const Point_2& p, const X_monotone_curve_2& cv) const` |
| `Compare_y_at_x_left_2` | `compare_y_at_x_left_2_object()` | `Comparison_result operator()(const X_monotone_curve_2& cv1, const X_monotone_curve_2& cv2, const Point_2& p) const` |
| `Compare_y_at_x_right_2` | `compare_y_at_x_right_2_object()` | same shape as above |
| `Equal_2` | `equal_2_object()` | `bool operator()(const X_monotone_curve_2&, const X_monotone_curve_2&) const`, `bool operator()(const Point_2&, const Point_2&) const` |
| `Make_x_monotone_2` | `make_x_monotone_2_object()` | `template <typename OutputIterator> OutputIterator operator()(const Curve_2& cv, OutputIterator oi) const` (stateless functor) |
| `Split_2` | `split_2_object()` | `void operator()(const X_monotone_curve_2& cv, const Point_2& p, X_monotone_curve_2& c1, X_monotone_curve_2& c2) const` |
| `Intersect_2` | `intersect_2_object()` | `template <typename OutputIterator> OutputIterator operator()(const X_monotone_curve_2& cv1, const X_monotone_curve_2& cv2, OutputIterator oi) const` |
| `Are_mergeable_2` | `are_mergeable_2_object()` | `bool operator()(const X_monotone_curve_2& cv1, const X_monotone_curve_2& cv2) const` |
| `Merge_2` | `merge_2_object()` | `void operator()(const X_monotone_curve_2& cv1, const X_monotone_curve_2& cv2, X_monotone_curve_2& c) const` |
| `Approximate_2` | `approximate_2_object()` | 3 overloads, see below |
| `Construct_x_monotone_curve_2` | `construct_x_monotone_curve_2_object()` | 3 overloads, see below |
| `Construct_curve_2` (= same type) | `construct_curve_2_object()` | same 3 overloads |
| `Trim_2` | `trim_2_object()` | `X_monotone_curve_2 operator()(const X_monotone_curve_2& xcv, const Point_2& src, const Point_2& tgt) const` |
| `Compare_endpoints_xy_2` | `compare_endpoints_xy_2_object()` | `Comparison_result operator()(const X_monotone_curve_2& cv) const` (stateless) |
| `Construct_opposite_2` | `construct_opposite_2_object()` | `X_monotone_curve_2 operator()(const X_monotone_curve_2& cv) const` (stateless; returns `cv.flip()`) |
| `Is_in_x_range_2` | `is_in_x_range_2_object()` | `bool operator()(const X_monotone_curve_2& cv, const Point_2& p) const` |
| `Is_in_y_range_2` | `is_in_y_range_2_object()` | `bool operator()(const X_monotone_curve_2& cv, const Point_2& p) const` |

#### Preconditions (verbatim from the doc comments)

* `Compare_y_at_x_2`: “`p` is in the \(x\)-range of `cv`.” (also asserted:
  `CGAL_precondition(m_traits.is_in_x_range_2_object()(cv, p))`).
* `Compare_y_at_x_left_2` / `_right_2`: “the point `p` lies on both curves, and both of them
  must be also be defined (lexicographically) to its left [right].”
* `Split_2`: “`p` lies on cv but is not one of its endpoints.” Implementation: `c1 = cv;
  c1.set_right(p); c2 = cv; c2.set_left(p);` — i.e. **`c1` is the left part and `c2` the right
  part regardless of the curve's direction**, and both keep `cv`'s direction flag.
* `Are_mergeable_2`: “`cv1` and `cv2` share a common endpoint” (the body itself checks
  `equal(cv1.right(), cv2.left()) || equal(cv2.right(), cv1.left())` and returns false otherwise).
* `Merge_2`: “the two curves are mergeable.” The merged curve inherits the direction of
  whichever curve is the left one.
* `Trim_2`: `src != tgt`, and both must lie on `xcv`
  (`compare_y_at_x(src, xcv) == EQUAL`, likewise for `tgt`).

#### `Approximate_2` (3 overloads)

```cpp
Approximate_number_type operator()(const Point_2& p, int i) const;   // pre: i == 0 || i == 1
Approximate_point_2     operator()(const Point_2& p) const;
template <typename OutputIterator>
OutputIterator operator()(const X_monotone_curve_2& xcv, double /*error*/,
                          OutputIterator oi, bool l2r = true) const;
```

The third one (the “polyline approximation” overload used by `CGAL::draw` and the landmark
strategy) writes exactly **two** `Approximate_point_2` — min then max vertex if `l2r`,
reversed otherwise. The `error` argument is ignored.

#### `Construct_x_monotone_curve_2` (3 overloads)

```cpp
typedef typename Kernel::Segment_2  Segment_2;   // nested typedef of the functor

X_monotone_curve_2 operator()(const Point_2& source, const Point_2& target) const;
    // pre: source != target ("Cannot construct a degenerate segment.")
X_monotone_curve_2 operator()(const Segment_2& seg) const;
    // pre: seg is not degenerate
X_monotone_curve_2 operator()(const Line_2& line,
                              const Point_2& source, const Point_2& target) const;
    // pre: source != target and both lie on `line`
```

All three compute `is_directed_right`, `is_vertical`, `is_degenerate` eagerly and pass them to
the 6-argument `Arr_segment_2` constructor — this is the “caching” that gives this traits its
speed advantage over the non-caching one.

#### `Trim_2` — direction semantics (important for bindings)

```cpp
X_monotone_curve_2 operator()(const X_monotone_curve_2& xcv,
                              const Point_2& src, const Point_2& tgt) const
```

The body reorders `src`/`tgt` using `compare_x_2` so the result keeps `xcv`'s left-to-right
orientation. For a **vertical** `xcv`, `compare_x` is always `EQUAL`, so the reordering never
fires and the result is simply `X_monotone_curve_2(src, tgt)` — its direction follows the
argument order, not `xcv`'s. Normalise yourself if you rely on direction.

---

## 2. `Arr_segment_2<Kernel_>` (the curve class)

```cpp
template <typename Kernel_>
class Arr_segment_2 : public Arr_segment_traits_2<Kernel_>::_Segment_cached_2 {   // :1466
  typedef Kernel_                                                  Kernel;   // private
  typedef typename Arr_segment_traits_2<Kernel>::_Segment_cached_2 Base;     // private
  typedef typename Kernel::Segment_2                               Segment_2;// private
  typedef typename Kernel::Point_2                                 Point_2;  // private
  typedef typename Kernel::Line_2                                  Line_2;   // private
public:
```

`Kernel`, `Point_2`, `Segment_2`, `Line_2` are **private** in `Arr_segment_2` itself, but the
same names are **public** in the base `_Segment_cached_2`, so `Arr_segment_2<K>::Point_2` still
resolves (through the base) — do not rely on it; use `Traits::Point_2`.

### Constructors (all public)

```cpp
Arr_segment_2();                                                    // degenerate/empty
Arr_segment_2(const Segment_2& seg);                                // pre: seg not degenerate
Arr_segment_2(const Point_2& source, const Point_2& target);        // pre: source != target
Arr_segment_2(const Line_2& line,
              const Point_2& source, const Point_2& target);        // pre: both on line, s!=t
Arr_segment_2(const Line_2& line,
              const Point_2& source, const Point_2& target,
              bool is_directed_right, bool is_vert, bool is_degen); // "all fields"
```

The default ctor sets `m_is_degen = true` — such a curve is **not** usable; most accessors have
`CGAL_precondition(!m_is_degen)`. Copy ctor / copy assignment are implicit and cheap
(three points/lines + four bools).

### Members declared on `Arr_segment_2`

```cpp
operator Segment_2() const;      // :1515  builds Kernel::Segment_2(source(), target())
Arr_segment_2 flip() const;      // :1519  swaps source/target, flips is_directed_right
Bbox_2 bbox() const;             // :1523  bbox(m_ps) + bbox(m_pt)
```

### Members inherited from `_Segment_cached_2` (all public)

```cpp
// nested typedefs
typedef typename Kernel::Line_2     Line_2;
typedef typename Kernel::Segment_2  Segment_2;
typedef typename Kernel::Point_2    Point_2;

// creation
_Segment_cached_2();
_Segment_cached_2(const Segment_2& seg);                                  // pre: not degenerate
_Segment_cached_2(const Point_2& source, const Point_2& target);          // pre: source != target
_Segment_cached_2(const Line_2& line, const Point_2& source, const Point_2& target);
                                                    // pre: source != target, both lie on `line`
_Segment_cached_2(const Line_2& line, const Point_2& source, const Point_2& target,
                  bool is_directed_right, bool is_vert, bool is_degen);
const _Segment_cached_2& operator=(const Segment_2& seg);                 // pre: not degenerate

// accessors
const Line_2&  line()               const;   // lazily computes+caches the supporting line
const Point_2& source()             const;
const Point_2& target()             const;
bool           is_vertical()        const;   // pre: !is_degenerate() (forces line() first)
bool           is_degenerate()      const;
bool           is_directed_right()  const;
const Point_2& left()               const;   // lexicographically smaller endpoint
const Point_2& right()              const;   // lexicographically larger endpoint

// modifiers
void set_left(const Point_2& p);   // pre: !degenerate, p on the supporting line, p < right()
void set_right(const Point_2& p);  // pre: !degenerate, p on the supporting line, p > left()

// deprecated
CGAL_DEPRECATED bool is_in_x_range(const Point_2& p) const;
CGAL_DEPRECATED bool is_in_y_range(const Point_2& p) const;
```

`line()` is `mutable`-cached: the first call on a segment built from two points constructs the
line and memoises it, so `line()` on a `const` object mutates internal state — **not thread-safe
for concurrent reads of the same curve object**.

### Free operators

```cpp
template <typename Kernel, typename OutputStream>
OutputStream& operator<<(OutputStream& os, const Arr_segment_2<Kernel>& seg);
    // prints static_cast<Kernel::Segment_2>(seg)   →  "0 0 1 1"
template <typename Kernel, typename InputStream>
InputStream& operator>>(InputStream& is, Arr_segment_2<Kernel>& seg);
    // reads a Kernel::Segment_2 then assigns
```

---

## 3. `Arr_non_caching_segment_basic_traits_2<T_Kernel>`

```cpp
template <class T_Kernel>
class Arr_non_caching_segment_basic_traits_2 : public T_Kernel
```

No default template argument. Model of `AosBasicTraits_2` — handles **x-monotone,
non-intersecting** segments only (no `Intersect_2`, no `Make_x_monotone_2`).

```cpp
typedef T_Kernel                              Kernel;
typedef typename Kernel::FT                   FT;
typedef Boolean_tag<Algebraic_structure_traits<FT>::Is_exact::value>  Has_exact_division;
typedef CGAL::Segment_assertions<Arr_non_caching_segment_basic_traits_2<Kernel> >
                                              Segment_assertions;

typedef Tag_true                              Has_left_category;
typedef Tag_false                             Has_do_intersect_category;
typedef Arr_oblivious_side_tag                Left_side_category;   // and Bottom/Top/Right

typedef typename Kernel::Point_2              Point_2;
typedef typename Kernel::Segment_2            X_monotone_curve_2;   // *the kernel segment*
typedef unsigned int                          Multiplicity;

// functors taken straight from the kernel:
typedef typename Kernel::Compare_x_2            Compare_x_2;
typedef typename Kernel::Compare_xy_2           Compare_xy_2;
typedef typename Kernel::Construct_min_vertex_2 Construct_min_vertex_2;  // returns BY VALUE
typedef typename Kernel::Construct_max_vertex_2 Construct_max_vertex_2;  // returns BY VALUE
typedef typename Kernel::Is_vertical_2          Is_vertical_2;
typedef typename Kernel::Compare_y_at_x_2       Compare_y_at_x_2;
typedef typename Kernel::Equal_2                Equal_2;

typedef double                                        Approximate_number_type;
typedef CGAL::Cartesian<Approximate_number_type>      Approximate_kernel;
typedef Approximate_kernel::Point_2                   Approximate_point_2;

typedef typename Kernel::Construct_segment_2    Construct_x_monotone_curve_2;
```

Functors defined here: `Compare_y_at_x_left_2`, `Compare_y_at_x_right_2` (both stateless,
`Comparison_result operator()(const X_monotone_curve_2&, const X_monotone_curve_2&, const Point_2&) const`),
and `Approximate_2` (same 3 overloads as `Arr_segment_traits_2`, stateful with `const Traits&`).

`construct_x_monotone_curve_2_object()` returns `this->construct_segment_2_object()`.

---

## 4. `Arr_non_caching_segment_traits_2<Kernel_T>`

```cpp
template <typename Kernel_T = Exact_predicates_exact_constructions_kernel>
class Arr_non_caching_segment_traits_2 : public Arr_non_caching_segment_basic_traits_2<Kernel_T>
```

Adds `Has_merge_category = Tag_true`, `typedef X_monotone_curve_2 Curve_2;`
(i.e. `Curve_2 == X_monotone_curve_2 == Kernel::Segment_2`), and:

| Functor | `*_object()` | Signature / notes |
|---|---|---|
| `Make_x_monotone_2` | `make_x_monotone_2_object()` | stateless; emits `std::variant<Point_2, X_monotone_curve_2>` |
| `Split_2` | `split_2_object()` | `void operator()(const X_monotone_curve_2&, const Point_2&, X_monotone_curve_2&, X_monotone_curve_2&) const`; stateless (default-constructs a `Base`/`Self` internally) |
| `Intersect_2` | `intersect_2_object()` | stores `const Traits&`; uses `kernel.intersect_2_object()` + `std::get_if` |
| `Are_mergeable_2` | `are_mergeable_2_object()` | stores `const Traits*` (**pointer**, `Are_mergeable_2(this)`) |
| `Merge_2` | `merge_2_object()` | stores `const Traits*` |
| `Construct_opposite_2` | `construct_opposite_2_object()` | `typedef typename Kernel::Construct_opposite_segment_2 Construct_opposite_2;` |
| `Compare_endpoints_xy_2` | `compare_endpoints_xy_2_object()` | stateless; `Comparison_result operator()(const X_monotone_curve_2& cv) const` |
| `Construct_curve_2` | `construct_curve_2_object()` | `typedef typename Kernel::Construct_segment_2 Construct_curve_2;` |

**There is no `Trim_2`, no `Is_in_x_range_2` and no `Is_in_y_range_2`** in either non-caching
class — hence gotcha #5 about `Arr_polycurve_basic_traits_2`'s default sub-traits. [verified]

---

## 5. `Arr_linear_traits_2<Kernel_>`

```cpp
template <typename Kernel_>
class Arr_linear_traits_2 : public Kernel_ {
  friend class Arr_linear_object_2<Kernel_>;
```

**No default kernel argument.**

### Public typedefs

```cpp
typedef Kernel_                         Kernel;
typedef typename Kernel::FT             FT;
typedef typename Algebraic_structure_traits<FT>::Is_exact  Has_exact_division;

typedef Tag_true                        Has_left_category;
typedef Tag_true                        Has_merge_category;
typedef Tag_false                       Has_do_intersect_category;

typedef Arr_open_side_tag               Left_side_category;    // ← UNBOUNDED topology
typedef Arr_open_side_tag               Bottom_side_category;
typedef Arr_open_side_tag               Top_side_category;
typedef Arr_open_side_tag               Right_side_category;

typedef typename Kernel::Line_2         Line_2;
typedef typename Kernel::Ray_2          Ray_2;
typedef typename Kernel::Segment_2      Segment_2;
typedef CGAL::Segment_assertions<Arr_linear_traits_2<Kernel> >  Segment_assertions;

typedef typename Kernel::Point_2        Point_2;
typedef Arr_linear_object_2<Kernel>     X_monotone_curve_2;
typedef Arr_linear_object_2<Kernel>     Curve_2;             // same type
typedef unsigned int                    Multiplicity;

typedef double                          Approximate_number_type;   // no Approximate_point_2!
typedef Construct_x_monotone_curve_2    Construct_curve_2;
```

`Arr_linear_traits_2() {}` — stateless.

Because all four sides are `Arr_open_side_tag`, `Arrangement_2<Arr_linear_traits_2<K>>` uses the
unbounded planar topology: fictitious halfedges, vertices at infinity, several unbounded faces.
[verified] Inserting `y=0` (line) + a vertical ray + a segment gives
`V=3, V_inf=3, E=4, F=3, unbounded_F=3`.

### Functors

| Functor | `*_object()` | Signature / notes |
|---|---|---|
| `Compare_x_2` | `compare_x_2_object()` | `Comparison_result operator()(const Point_2&, const Point_2&) const` (holds `const Traits&`) |
| `Compare_xy_2` | `compare_xy_2_object()` | stateless; `Comparison_result operator()(const Point_2&, const Point_2&) const` |
| `Compare_endpoints_xy_2` | `compare_endpoints_xy_2_object()` | stateless; `Comparison_result operator()(const X_monotone_curve_2& xcv) const` |
| `Trim_2` | `trim_2_object()` | **non-const, by-value args**: `X_monotone_curve_2 operator()(const X_monotone_curve_2 xcv, const Point_2 src, const Point_2 tgt)` — always returns a **segment**, so trimming a ray/line yields a bounded curve. [verified] |
| `Construct_opposite_2` | `construct_opposite_2_object()` | **DOES NOT COMPILE** (uses nonexistent `get_ps()`/`get_pt()`) [verified] |
| `Construct_min_vertex_2` | `construct_min_vertex_2_object()` | stateless; `const Point_2& operator()(const X_monotone_curve_2& cv) const`; **pre: `!cv.is_degenerate() && cv.has_left()`** |
| `Construct_max_vertex_2` | `construct_max_vertex_2_object()` | stateless; **pre: `!cv.is_degenerate() && cv.has_right()`** |
| `Is_vertical_2` | `is_vertical_2_object()` | stateless; `bool operator()(const X_monotone_curve_2& cv) const`; pre: `!cv.is_degenerate()` |
| `Compare_y_at_x_2` | `compare_y_at_x_2_object()` | `Comparison_result operator()(const Point_2& p, const X_monotone_curve_2& cv) const` |
| `Compare_y_at_x_left_2` / `_right_2` | ditto `_object()` | `(cv1, cv2, p)` |
| `Equal_2` | `equal_2_object()` | stateless; point/point and curve/curve overloads |
| `Parameter_space_in_x_2` | `parameter_space_in_x_2_object()` | `Arr_parameter_space operator()(const X_monotone_curve_2& xcv, Arr_curve_end ce) const` (pre: `!xcv.is_degenerate()`), and `Arr_parameter_space operator()(const Point_2 /*p*/) const { return ARR_INTERIOR; }` (note: **by value**) |
| `Parameter_space_in_y_2` | `parameter_space_in_y_2_object()` | same two overloads |
| `Compare_x_on_boundary_2` | `compare_x_on_boundary_2_object()` | `(const Point_2&, const X_monotone_curve_2&, Arr_curve_end) const`; `(const X_monotone_curve_2&, Arr_curve_end, const X_monotone_curve_2&, Arr_curve_end) const`. **pre: the curves are vertical** |
| `Compare_x_near_boundary_2` | `compare_x_near_boundary_2_object()` | `Comparison_result operator()(const X_monotone_curve_2&, const X_monotone_curve_2&, Arr_curve_end) const` — always returns `EQUAL` |
| `Compare_y_near_boundary_2` | `compare_y_near_boundary_2_object()` | `Comparison_result operator()(const X_monotone_curve_2& xcv1, const X_monotone_curve_2& xcv2, Arr_curve_end ce) const`; pre: both `ce` ends on the left (resp. right) boundary |
| `Make_x_monotone_2` | `make_x_monotone_2_object()` | stateless; one `std::variant` per input curve |
| `Split_2` | `split_2_object()` | stateless; `void operator()(const X_monotone_curve_2& cv, const Point_2& p, X_monotone_curve_2& c1, X_monotone_curve_2& c2) const`; pre: `p` interior to `cv` (`!cv.has_left() || left<p`, `!cv.has_right() || right>p`) |
| `Intersect_2` | `intersect_2_object()` | holds `const Traits&`; standard `OutputIterator` form |
| `Are_mergeable_2` | `are_mergeable_2_object()` | stateless; pre: neither curve degenerate |
| `Merge_2` | `merge_2_object()` | holds `const Traits&`; the merged curve may become unbounded (`c.set_right()` with no argument) |
| `Approximate_2` | `approximate_2_object()` | stateless, **only** `Approximate_number_type operator()(const Point_2& p, int i) const` — no point overload, **no curve overload**, and **no `Approximate_point_2` typedef** |
| `Construct_x_monotone_curve_2` | `construct_x_monotone_curve_2_object()` | stateless; **only** `X_monotone_curve_2 operator()(const Point_2& p, const Point_2& q) const` (builds a `Kernel::Segment_2`); pre: `p != q` |
| `Construct_curve_2` | `construct_curve_2_object()` | **DOES NOT COMPILE** (`Arr_linear_traits_2.h:1572`) [verified] — build `Curve_2` objects directly from `Segment_2` / `Ray_2` / `Line_2` instead |

`Compare_y_on_boundary_2`, `Is_on_x_identification_2`, `Is_on_y_identification_2` are **absent**
(not needed for open boundaries).

---

## 6. `Arr_linear_object_2<Kernel_>` (the curve class)

```cpp
template <typename Kernel_>
class Arr_linear_object_2 : public Arr_linear_traits_2<Kernel_>::_Linear_object_cached_2
{
  typedef typename Arr_linear_traits_2<Kernel_>::_Linear_object_cached_2 Base;  // private
public:
  typedef Kernel_                      Kernel;
  typedef typename Kernel::Point_2     Point_2;
  typedef typename Kernel::Segment_2   Segment_2;
  typedef typename Kernel::Ray_2       Ray_2;
  typedef typename Kernel::Line_2      Line_2;
```

### Constructors

```cpp
Arr_linear_object_2();                                        // degenerate (is_degen = true)
Arr_linear_object_2(const Point_2& s, const Point_2& t);      // pre: s != t   → segment
Arr_linear_object_2(const Segment_2& seg);                    // pre: not degenerate
Arr_linear_object_2(const Ray_2& ray);                        // pre: not degenerate
Arr_linear_object_2(const Line_2& line);                      // pre: not degenerate
```

There is **no** implicit conversion operator to `Segment_2`/`Ray_2`/`Line_2` (the header
explains that three cast operators would be ambiguous). Use the named accessors.

### Members declared on `Arr_linear_object_2`

```cpp
bool           is_segment() const;   // !is_degen && has_source && has_target
Segment_2      segment()    const;   // pre: is_segment()
bool           is_ray()     const;   // !is_degen && (has_source != has_target)
Ray_2          ray()        const;   // pre: is_ray()
bool           is_line()    const;   // !is_degen && !has_source && !has_target
Line_2         line()       const;   // pre: is_line()
const Line_2&  supporting_line() const;   // pre: !is_degenerate()
const Point_2& source()     const;   // pre: !is_line()   (a "flipped" ray returns pt)
const Point_2& target()     const;   // pre: !is_line() && !is_ray()
Bbox_2         bbox()       const;   // pre: is_segment()  ← asserts, cannot bbox a ray/line
```

### Members inherited from `_Linear_object_cached_2` (all public)

```cpp
typedef typename Kernel::Line_2 Line_2;  typedef typename Kernel::Ray_2 Ray_2;
typedef typename Kernel::Segment_2 Segment_2;  typedef typename Kernel::Point_2 Point_2;

_Linear_object_cached_2();
_Linear_object_cached_2(const Point_2& source, const Point_2& target);  // pre: distinct
_Linear_object_cached_2(const Segment_2& seg);   // pre: not degenerate
_Linear_object_cached_2(const Ray_2& ray);       // pre: not degenerate
_Linear_object_cached_2(const Line_2& ln);       // pre: not degenerate

Arr_parameter_space left_infinite_in_x()  const;   // ARR_LEFT_BOUNDARY | ARR_INTERIOR
Arr_parameter_space left_infinite_in_y()  const;   // ARR_BOTTOM/TOP_BOUNDARY | ARR_INTERIOR
Arr_parameter_space right_infinite_in_x() const;   // ARR_RIGHT_BOUNDARY | ARR_INTERIOR
Arr_parameter_space right_infinite_in_y() const;

bool           has_left()  const;
const Point_2& left()      const;                  // pre: has_left()
void           set_left(const Point_2& p, bool check_validity = true);
void           set_left();                         // makes the left end infinite
bool           has_right() const;
const Point_2& right()     const;                  // pre: has_right()
void           set_right(const Point_2& p, bool check_validity = true);
void           set_right();                        // makes the right end infinite

const Line_2&  supp_line()        const;           // pre: !is_degenerate()
bool           is_vertical()      const;           // pre: !is_degenerate()
bool           is_degenerate()    const;
bool           is_directed_right() const;
bool           is_in_x_range(const Point_2& p) const;
bool           is_in_y_range(const Point_2& p) const;   // pre: is_vertical()
```

Note that `supporting_line()` (derived) and `supp_line()` (base) are two names for the same
thing; the boundary functors of the traits call `supp_line()`.

### Free operators

```cpp
template <typename Kernel, typename OutputStream>
OutputStream& operator<<(OutputStream& os, const Arr_linear_object_2<Kernel>& lobj);
    // " S <segment>" | " R <ray>" | " L <line>"
template <typename Kernel, typename InputStream>
InputStream& operator>>(InputStream& is, Arr_linear_object_2<Kernel>& lobj);
    // skips until it sees S/s, R/r or L/l then reads the corresponding kernel object
```

---

## 7. `CGAL::internal::Polycurve_2` / `X_monotone_polycurve_2` (`Polycurve_2.h`)

This is the actual curve representation used by **all three** polycurve/polyline traits.
(The closing brace comment says `namespace polycurve` but the namespace really is
`CGAL::internal`.)

```cpp
template <typename SubcurveType_2, typename PointType_2>
class Polycurve_2 {
public:
  typedef SubcurveType_2                            Subcurve_type_2;
  typedef PointType_2                               Point_type_2;
protected:
  typedef typename std::vector<Subcurve_type_2>     Subcurves_container;
  Subcurves_container m_subcurves;                  // contiguous std::vector
public:
  typedef typename Subcurves_container::size_type   Size;
  typedef typename Subcurves_container::size_type   size_type;
```

### Construction

```cpp
Polycurve_2();                                       // empty
Polycurve_2(const Subcurve_type_2& seg);             // single subcurve
template <typename InputIterator>
Polycurve_2(InputIterator begin, InputIterator end); // dispatches on value_type
```

The range ctor dispatches on `std::is_same<value_type, Point_type_2>`:
* subcurve range → `construct_polycurve(begin, end, std::false_type)`:
  `m_subcurves.assign(begin, end);` — **pre: “the end of subcurve n should be the beginning of
  subcurve n+1”** (not checked).
* point range → `CGAL_DEPRECATED construct_polycurve(..., std::true_type)`: builds
  `Subcurve_type_2(*ps, *pt)` for consecutive pairs. **Deprecated — do not use;** use the
  traits' `Construct_curve_2` / `Construct_x_monotone_curve_2`.

### Modifiers

```cpp
inline void push_back(const Subcurve_type_2& seg);   // "risky, prefer the traits functor"
                    // pre: seg's source == last subcurve's target (unchecked)
inline void push_front(const Subcurve_type_2& seg);  // vector insert at begin() → O(n)
CGAL_DEPRECATED void push_back(const Point_type_2& p);  // asserts !m_subcurves.empty()
inline void clear();                                 // m_subcurves.clear()
```

### Queries

```cpp
Bbox_2 bbox() const;   // union of (*this)[i].bbox() over all subcurves
                       // → requires Subcurve_type_2::bbox(); BREAKS for conic subcurves

size_type number_of_subcurves() const;
CGAL_DEPRECATED size_type size() const;               // == number_of_subcurves()
CGAL_DEPRECATED std::size_t points() const;           // 0 or n+1
inline const Subcurve_type_2& operator[](const std::size_t i) const;   // assert i < n
```

### Iteration

```cpp
typedef typename Subcurves_container::iterator                  Subcurve_iterator;
typedef typename Subcurves_container::const_iterator            Subcurve_const_iterator;
typedef std::reverse_iterator<Subcurve_const_iterator>          Subcurve_const_reverse_iterator;

Subcurve_const_iterator          subcurves_begin() const;
Subcurve_const_iterator          subcurves_end()   const;
Subcurve_const_reverse_iterator  subcurves_rbegin() const;
Subcurve_const_reverse_iterator  subcurves_rend()   const;
CGAL_DEPRECATED Subcurve_const_iterator begin_subcurves() const;   // = subcurves_begin()
CGAL_DEPRECATED Subcurve_const_iterator end_subcurves()   const;

class Point_const_iterator;                       // bidirectional, value_type = Point_type_2
typedef std::reverse_iterator<Point_const_iterator>  Point_const_reverse_iterator;
typedef Point_const_iterator          const_iterator;           // backward compatibility
typedef Point_const_reverse_iterator  const_reverse_iterator;

Point_const_iterator          points_begin()  const;   // empty iterator if no subcurves
Point_const_iterator          points_end()    const;
Point_const_reverse_iterator  points_rbegin() const;
Point_const_reverse_iterator  points_rend()   const;
CGAL_DEPRECATED Point_const_iterator begin() const;    // = points_begin()  (also end/rbegin/rend)
```

`Point_const_iterator::operator*` returns `((*m_cvP)[0]).source()` for index 0 and
`((*m_cvP)[i-1]).target()` otherwise — i.e. **it requires `Subcurve_type_2::source()` and
`::target()`**, and it holds a raw `const Polycurve_2*`: the iterator dangles if the curve
is destroyed or reallocated (`push_back`). There are exactly `number_of_subcurves() + 1`
points; there is **no** `points_size()` — use the traits' `Number_of_points_2`.

### `X_monotone_polycurve_2`

```cpp
template <typename SubcurveType_2, typename PointType_2>
class X_monotone_polycurve_2 : public Polycurve_2<SubcurveType_2, PointType_2>
{
public:
  typedef Polycurve_2<Subcurve_type_2, Point_type_2> Base;
  X_monotone_polycurve_2();
  X_monotone_polycurve_2(Subcurve_type_2 seg);                        // by value!
  template <typename InputIterator>
  X_monotone_polycurve_2(InputIterator begin, InputIterator end);     // pre: x-monotone
};
```

Its `construct_x_monotone_polycurve(...)` hooks are empty — **no x-monotonicity validation
happens in the curve class**; the traits' `Construct_x_monotone_curve_2` does it in
`CGAL_precondition` blocks only. Note the doc comment: “An x-monotone polycurve is always
directed from left to right” — that is **only true if you compile with
`CGAL_ALWAYS_LEFT_TO_RIGHT`**; otherwise the direction follows the input, and
`Compare_endpoints_xy_2` tells you which.

---

## 8. `polyline::Polyline_2` / `polyline::X_monotone_polyline_2` (deprecated)

`CGAL/Arr_geometry_traits/Polyline_2.h` starts with

```cpp
#define CGAL_DEPRECATED_HEADER "<CGAL/Arr_geometry_traits/Polyline_2.h>"
#define CGAL_REPLACEMENT_HEADER "<CGAL/Arr_geometry_traits/Polycurve_2.h>"
#include <CGAL/Installation/internal/deprecation_warning.h>
```

so including it emits a deprecation warning. The two classes only add aliases over
`internal::Polycurve_2` / `internal::X_monotone_polycurve_2`:

```cpp
typedef typename Base::Subcurve_type_2                Segment_type_2;
typedef typename Base::size_type                      Segments_size_type;
typedef typename Base::Subcurve_iterator              Segment_iterator;
typedef typename Base::Subcurve_const_iterator        Segment_const_iterator;
typedef typename Base::Subcurve_const_reverse_iterator Segment_const_reverse_iterator;

Segment_const_iterator          begin_segments()  const;   // = subcurves_begin()
Segment_const_iterator          end_segments()    const;
Segment_const_reverse_iterator  rbegin_segments() const;
Segment_const_reverse_iterator  rend_segments()   const;
Segments_size_type              number_of_segments() const;  // = number_of_subcurves()
```

**`Arr_polyline_traits_2` in 6.1 does *not* use these types.** Do not expose them.

---

## 9. `Arr_polycurve_basic_traits_2<SubcurveTraits_2>`

```cpp
template <typename SubcurveTraits_2 = Arr_non_caching_segment_traits_2<> >
class Arr_polycurve_basic_traits_2 {
```

Handles **x-monotone polycurves only** (no `Curve_2`, no `Make_x_monotone_2`, no `Intersect_2`).

### Public typedefs

```cpp
using Subcurve_traits_2 = SubcurveTraits_2;

using Has_left_category         = typename Subcurve_traits_2::Has_left_category;
using Has_do_intersect_category = typename Subcurve_traits_2::Has_do_intersect_category;
using Left_side_category        = typename Subcurve_traits_2::Left_side_category;   // + 3 more
using All_sides_oblivious_category =
  typename Arr_all_sides_oblivious_category<Left_side_category, Bottom_side_category,
                                            Top_side_category, Right_side_category>::result;
using Bottom_or_top_sides_category =
  typename Arr_two_sides_category<Bottom_side_category, Top_side_category>::result;

using Point_2               = typename Subcurve_traits_2::Point_2;
using X_monotone_subcurve_2 = typename Subcurve_traits_2::X_monotone_curve_2;
using Multiplicity          = typename Subcurve_traits_2::Multiplicity;
using X_monotone_segment_2  = X_monotone_subcurve_2;               // backward compatibility

using X_monotone_curve_2 = internal::X_monotone_polycurve_2<X_monotone_subcurve_2, Point_2>;
using Size      = typename X_monotone_curve_2::Size;
using size_type = typename X_monotone_curve_2::size_type;

// landmark block (may become `void` if the sub-traits has no Approximate_2):
using Approximate_number_type = typename has_approximate_2<Subcurve_traits_2>::Approximate_number_type;
using Approximate_2           = typename has_approximate_2<Subcurve_traits_2>::Approximate_2;
using Approximate_point_2     = typename has_approximate_2<Subcurve_traits_2>::Approximate_point_2;
```

### Construction / ownership (read carefully)

```cpp
Arr_polycurve_basic_traits_2();                                    // owns a new Subcurve_traits_2
Arr_polycurve_basic_traits_2(const Subcurve_traits_2* geom_traits); // borrows; does NOT own
Arr_polycurve_basic_traits_2(const Arr_polycurve_basic_traits_2& other);
~Arr_polycurve_basic_traits_2();                    // deletes only if m_own_traits
const Subcurve_traits_2* subcurve_traits_2() const; // never null
```

* Default ctor: `m_subcurve_traits = new Subcurve_traits_2(); m_own_traits = true;`
* Pointer ctor: **the caller must keep the sub-traits alive** for the whole life of this
  object, every functor obtained from it, and every arrangement using it.
* Copy ctor: if `other` owns, a **fresh default-constructed** sub-traits is allocated
  (state of the original is lost); otherwise the pointer is shared.
* **No `operator=`** → the implicit one double-frees. [verified, see gotcha #2]

### Functors

Every functor here has a `protected` ctor `Xxx_2(const Polycurve_basic_traits_2& traits)`
storing `const Polycurve_basic_traits_2& m_poly_traits`, with
`friend class Arr_polycurve_basic_traits_2<Subcurve_traits_2>`.

```cpp
// --- Compare_x_2 ---  compare_x_2_object()
Comparison_result operator()(const Point_2& p1, const Point_2& p2) const;
Comparison_result operator()(const X_monotone_subcurve_2& xs1, Arr_curve_end ce1,
                             const Point_2& p2);                              // NOT const
Comparison_result operator()(const X_monotone_subcurve_2& xs1, Arr_curve_end ce1,
                             const X_monotone_subcurve_2& xs2, Arr_curve_end ce2); // NOT const

// --- Compare_xy_2 ---  compare_xy_2_object()   (identical overload set, same const-ness)

// --- Construct_min_vertex_2 ---  construct_min_vertex_2_object()
using Subcurve_ctr = typename Subcurve_traits_2::Construct_min_vertex_2;
decltype(std::declval<Subcurve_ctr>().operator()(std::declval<X_monotone_subcurve_2>()))
operator()(const X_monotone_curve_2& xcv) const;   // assert: number_of_subcurves() > 0
// picks xcv[0] if that subcurve is left-to-right, else xcv[n-1]

// --- Construct_max_vertex_2 ---  construct_max_vertex_2_object()   (mirror image)

// --- Is_vertical_2 ---  is_vertical_2_object()
bool operator()(const X_monotone_curve_2& cv) const;   // tests only cv[0]

// --- Compare_y_at_x_2 ---  compare_y_at_x_2_object()
Comparison_result operator()(const Point_2& p, const X_monotone_curve_2& xcv) const;
Comparison_result operator()(const X_monotone_subcurve_2& xs1, Arr_curve_end ce1,
                             const X_monotone_subcurve_2& xs2) const;

// --- Compare_y_at_x_left_2 / Compare_y_at_x_right_2 ---
Comparison_result operator()(const X_monotone_curve_2& cv1,
                             const X_monotone_curve_2& cv2, const Point_2& p) const;

// --- Equal_2 ---  equal_2_object()
bool operator()(const Point_2& p1, const Point_2& p2) const;
bool operator()(const X_monotone_curve_2& cv1, const X_monotone_curve_2& cv2) const;

// --- Compare_endpoints_xy_2 ---  compare_endpoints_xy_2_object()
Comparison_result operator()(const X_monotone_curve_2& xcv) const;   // looks at xcv[0] only

// --- Construct_opposite_2 ---  construct_opposite_2_object()
X_monotone_curve_2 operator()(const X_monotone_curve_2& xcv) const;
// pre: xcv contains at least one subcurve; reverses BOTH the subcurve order and each subcurve

// --- Construct_point_2 ---  construct_point_2_object()
template <typename ... Args> Point_2 operator()(Args ... args) const;
// perfect-forwards to subcurve_traits_2()->construct_point_2_object()

// --- Construct_x_monotone_curve_2 ---  construct_x_monotone_curve_2_object()
X_monotone_curve_2 operator()(const X_monotone_subcurve_2& seg) const;   // pre: not degenerate
template <typename ForwardIterator>
X_monotone_curve_2 operator()(ForwardIterator begin, ForwardIterator end) const;
//   subcurve range → OK;  point range → CGAL_error_msg at run time

// --- boundary functors (only meaningful when a side is not oblivious) ---
Arr_parameter_space Parameter_space_in_x_2::operator()(const X_monotone_curve_2&, Arr_curve_end) const;
Arr_parameter_space Parameter_space_in_x_2::operator()(const Point_2 p) const;     // by value
Arr_parameter_space Parameter_space_in_y_2::operator()(const X_monotone_curve_2&, Arr_curve_end) const;
Arr_parameter_space Parameter_space_in_y_2::operator()(const Point_2 p) const;
Comparison_result Compare_x_on_boundary_2::operator()(const Point_2&, const X_monotone_curve_2&, Arr_curve_end) const;
Comparison_result Compare_x_on_boundary_2::operator()(const X_monotone_curve_2&, Arr_curve_end,
                                                      const X_monotone_curve_2&, Arr_curve_end) const;
size_type         Compare_x_near_boundary_2::get_curve_index(const X_monotone_curve_2&, Arr_curve_end) const;
Comparison_result Compare_x_near_boundary_2::operator()(const X_monotone_curve_2&,
                                                        const X_monotone_curve_2&, Arr_curve_end) const;
Comparison_result Compare_y_on_boundary_2::operator()(const Point_2& p1, const Point_2& p2) const;
Comparison_result Compare_y_near_boundary_2::operator()(const X_monotone_curve_2&,
                                                        const X_monotone_curve_2&, Arr_curve_end) const;
bool Is_on_y_identification_2::operator()(const Point_2& p) const;
bool Is_on_y_identification_2::operator()(const X_monotone_curve_2& xcv) const;
bool Is_on_x_identification_2::operator()(const Point_2& p) const;
bool Is_on_x_identification_2::operator()(const X_monotone_curve_2& xcv) const;

// --- utilities (not required by any concept) ---
// Number_of_points_2 ---  number_of_points_2_object()   (stateless)
size_type operator()(const X_monotone_curve_2& cv) const;   // 0 if empty, else n+1

// Push_back_2 ---  push_back_2_object()
void operator()(X_monotone_curve_2& xcv, const X_monotone_subcurve_2& seg) const;
// Push_front_2 ---  push_front_2_object()
void operator()(X_monotone_curve_2& xcv, const X_monotone_subcurve_2& seg) const;

// Trim_2 ---  trim_2_object()
X_monotone_curve_2 operator()(const X_monotone_curve_2& xcv,
                              const Point_2& source, const Point_2& target) const;
```

**`Push_back_2` preconditions** (checked only with assertions on; two implementations,
selected by `All_sides_oblivious_category`):
* `xcv` empty, or both `xcv[0]` and `seg` vertical / both non-vertical
  (“xcv is vertical and seg is not or vice versa!”),
* same orientation: `cmp_endpts(xcv[0]) == cmp_endpts(seg)`
  (“xcv and seg do not have the same orientation!”),
* `seg` not degenerate (“Seg degenerates to a point!”),
* connectivity: if left-to-right, `max_vertex(xcv[n-1]) == min_vertex(seg)`
  (“Seg does not connect to the right!”); if right-to-left, `min_vertex(xcv[n-1]) ==
  max_vertex(seg)` (“Seg does not connect to the left!”).
  In the non-oblivious variant these connectivity checks are skipped for ends that lie on a
  boundary. The body is just `xcv.push_back(seg);`.

**`Trim_2`** requires `Subcurve_traits_2::Trim_2` (fails to compile otherwise, see gotcha #5),
calls `m_poly_traits.locate(xcv, p)` (a binary search, `O(log n)`), and uses `compare_x_2` to
decide whether to swap `source`/`target` — so for **vertical** polycurves the swap logic
degenerates exactly like the segment traits' `Trim_2`.

**Protected helpers** worth knowing (not part of the public API but they explain complexity):
`enum { INVALID_INDEX = 0xffffffff };`, `template <typename Compare> std::size_t
locate_gen(const X_monotone_curve_2& cv, Compare compare) const` — binary search,
`O(log n)`, returns `INVALID_INDEX` when the query is out of the x-range; plus `locate()`
and `locate_side()` used by `Compare_y_at_x_left_2/right_2` and `Trim_2`.

---

## 10. `Arr_polycurve_traits_2<SubcurveTraits_2>`

```cpp
template <typename SubcurveTraits_2 = Arr_segment_traits_2<> >
class Arr_polycurve_traits_2 : public Arr_polycurve_basic_traits_2<SubcurveTraits_2>
```

Adds the *general* (non-x-monotone) curve type plus subdivision/intersection/merging.

### Added typedefs

```cpp
using Has_merge_category = typename Subcurve_traits_2::Has_merge_category;
using Multiplicity       = typename Subcurve_traits_2::Multiplicity;
using Subcurve_2         = typename Subcurve_traits_2::Curve_2;    // NOT X_monotone_curve_2
using Segment_2          = Subcurve_2;                             // backward compatibility
using Curve_2            = internal::Polycurve_2<Subcurve_2, Point_2>;
```

Everything from `Arr_polycurve_basic_traits_2` is re-exported by name
(`Compare_x_2`, …, `Trim_2`).

### Constructors

```cpp
Arr_polycurve_traits_2() : Base() {}                                     // owns sub-traits
Arr_polycurve_traits_2(const Subcurve_traits_2* geom_traits) : Base(geom_traits) {}  // borrows
```

(Same ownership rules and the same missing `operator=`.)

### Functors added / overridden

```cpp
// Number_of_points_2 : public Base::Number_of_points_2    number_of_points_2_object()
size_type operator()(const Curve_2& cv) const;      // HIDES the base X_monotone overload

// Make_x_monotone_2                                       make_x_monotone_2_object()
Make_x_monotone_2(const Polycurve_traits_2& traits);        // public ctor here
template <typename OutputIterator>
OutputIterator operator()(const Curve_2& cv, OutputIterator oi) const;
// dereference type: std::variant<Point_2, X_monotone_curve_2>
// pre: "if cv is not empty then it must be continuous and well-oriented"
// empty input → no output at all

// Push_back_2 : public Base::Push_back_2                   push_back_2_object()
using Base::Push_back_2::operator();                        // keeps the X_monotone overload
void operator()(Curve_2& cv, const Subcurve_2& seg) const;  // cv.push_back(seg), no checks

// Push_front_2 : public Base::Push_front_2                 push_front_2_object()
using Base::Push_front_2::operator();
void operator()(Curve_2& cv, const Subcurve_2& seg) const;  // cv.push_front(seg)

// Split_2                                                  split_2_object()
Split_2(const Polycurve_traits_2& traits);                  // public ctor
void operator()(const X_monotone_curve_2& xcv, const Point_2& p,
                X_monotone_curve_2& xcv1, X_monotone_curve_2& xcv2) const;
// pre: p lies on xcv but is not one of its endpoints

// Intersect_2                                              intersect_2_object()
Intersect_2(const Polycurve_traits_2& traits);              // public ctor
template <typename OutputIterator>
OutputIterator operator()(const X_monotone_curve_2& cv1, const X_monotone_curve_2& cv2,
                          OutputIterator oi) const;
// dereference type: std::variant<std::pair<Point_2, Multiplicity>, X_monotone_curve_2>
// note (verbatim): "If the intersection yields an overlap then it will be oriented
//                   from left-to-right."

// Are_mergeable_2                                          are_mergeable_2_object()
bool operator()(const X_monotone_curve_2& cv1, const X_monotone_curve_2& cv2) const;
// true iff same orientation, share an endpoint, and both vertical or both non-vertical

// Merge_2                                                  merge_2_object()
void operator()(const X_monotone_curve_2& cv1, const X_monotone_curve_2& cv2,
                X_monotone_curve_2& c) const;   // pre: are_mergeable_2(cv1, cv2)
// c.clear() first; tries to merge the two touching *subcurves* with the sub-traits' Merge_2

// Construct_curve_2                                        construct_curve_2_object()
Construct_curve_2(const Polycurve_traits_2& traits);        // public ctor
Curve_2 operator()(const Subcurve_2& seg) const;
template <typename ForwardIterator>
Curve_2 operator()(ForwardIterator begin, ForwardIterator end) const;
//   subcurve range → Curve_2(begin, end); pre: begin != end
//   point range    → CGAL_error_msg("Cannot construct a polycurve from a range of points!")
```

`Make_x_monotone_2`, `Split_2`, `Intersect_2`, `Are_mergeable_2`, `Merge_2` and
`Construct_curve_2` of this class have **public** constructors taking `const Polycurve_traits_2&`
(unlike the basic-traits functors), but they still hold a reference — same lifetime rule.

---

## 11. `Arr_polyline_traits_2<SegmentTraits_2>`

```cpp
template <typename SegmentTraits_2 = Arr_segment_traits_2<> >
class Arr_polyline_traits_2 : public Arr_polycurve_traits_2<SegmentTraits_2>
```

```cpp
typedef SegmentTraits_2                                Segment_traits_2;
typedef typename Segment_traits_2::Curve_2             Segment_2;             // "for completeness"
typedef typename Segment_traits_2::X_monotone_curve_2  X_monotone_segment_2;
// plus every typedef of Arr_polycurve_traits_2, re-exported by name, including
//   Curve_2, X_monotone_curve_2, Subcurve_2, X_monotone_subcurve_2, Number_of_points_2, Trim_2,
//   Split_2, Intersect_2, Are_mergeable_2, Merge_2, all Compare_*/Parameter_space_* functors.

Arr_polyline_traits_2();                                        // owns a Segment_traits_2
Arr_polyline_traits_2(const Segment_traits_2* geom_traits);     // borrows
CGAL_DEPRECATED const Segment_traits_2* segment_traits_2() const;  // = subcurve_traits_2()
```

With the default `Arr_segment_traits_2<K>`, `Subcurve_2 == X_monotone_subcurve_2 ==
Arr_segment_2<K>`, and therefore `Curve_2` and `X_monotone_curve_2` are
`internal::Polycurve_2<Arr_segment_2<K>, K::Point_2>` and
`internal::X_monotone_polycurve_2<Arr_segment_2<K>, K::Point_2>` (the latter derives from the
former — which is why `Number_of_points_2` still accepts an x-monotone polyline here).

### Functors re-defined here

```cpp
// --- Push_back_2 : public Base::Push_back_2 ---   push_back_2_object()
using Base::Push_back_2::operator();     // (Curve_2&, Subcurve_2&) and (X_mono&, X_mono_sub&)
void operator()(Curve_2& cv, const Point_2& p) const;              // WORKS  [verified]
    // pre: cv.number_of_subcurves() > 0; appends [last endpoint, p] preserving orientation
void operator()(X_monotone_curve_2& xcv, const Point_2& p) const;  // BROKEN  [verified]
    // Arr_polyline_traits_2.h:198 calls `subcurves_traits_2()` (typo) → does not compile

// --- Push_front_2 : public Base::Push_front_2 --- push_front_2_object()
using Base::Push_front_2::operator();
void operator()(Curve_2& cv, const Point_2& p) const;              // WORKS  [verified]
void operator()(const X_monotone_curve_2& xcv, Point_2& p) const;  // BROKEN  [verified]
    // takes the curve by const& then calls xcv.push_front(...)  (:323)

// --- Construct_curve_2 : public Base::Construct_curve_2 ---   construct_curve_2_object()
Curve_2 operator()(const Point_2& p, const Point_2& q) const;      // 2-point polyline
Curve_2 operator()(const Subcurve_2& seg) const;
template <typename ForwardIterator> Curve_2 operator()(ForwardIterator begin,
                                                       ForwardIterator end) const;
template <typename Range>           Curve_2 operator()(const Range& range) const;  // NEW
//   subcurve range → forwarded to the base
//   point range    → builds consecutive segments; pre: range size != 1,
//                    "Cannot construct a degenerated segment" for equal consecutive points

// --- Construct_x_monotone_curve_2 : public Base::Construct_x_monotone_curve_2 ---
//     construct_x_monotone_curve_2_object()
X_monotone_curve_2 operator()(const Point_2& p, const Point_2& q) const;   // pre: p != q
X_monotone_curve_2 operator()(const X_monotone_subcurve_2& seg) const;     // pre: not degenerate
template <typename ForwardIterator>
X_monotone_curve_2 operator()(ForwardIterator begin, ForwardIterator end) const;
//   subcurve range → base implementation (x-monotonicity + connectivity preconditions)
//   point range    → pre: >= 2 points, no two consecutive points equal,
//                    compare_x and compare_xy of every consecutive pair must equal that of
//                    the first pair (i.e. the points must be strictly monotone in x, or all
//                    on one vertical line and monotone in y)
//   There is NO `operator()(const Range&)` on this functor (only on Construct_curve_2).

// --- Approximate_2 : public Base::Approximate_2 ---   approximate_2_object()
using Approximate_number_type = typename Base::Approximate_number_type;   // double
using Approximate_point_2     = typename Base::Approximate_point_2;       // Cartesian<double>::Point_2
Approximate_number_type operator()(const Point_2& p, int i) const;
Approximate_point_2     operator()(const Point_2& p) const;
template <typename OutputIterator>
OutputIterator operator()(const X_monotone_curve_2& xcv, double /*error*/,
                          OutputIterator oi, bool l2r = true) const;
//   emits number_of_subcurves()+1 Approximate_point_2, walking points_begin()..points_end()
//   (l2r == true) or points_rbegin()..points_rend() (l2r == false).
//   NB: this is the *stored* order, so for a right-to-left oriented polyline `l2r == true`
//   actually yields right-to-left output. Check Compare_endpoints_xy_2 first.
```

Verified behaviour of the polyline traits (compiled and run):

```
curve subcurves=3   npts(curve)=4      // 4-point range
xcv subcurves=2     npts(xcv)=3
bbox=0 0 2 3
points: (0,0) (1,1) (2,3)
Construct_curve_2(range)  → 3 subcurves
Construct_curve_2(p,q) / Construct_x_monotone_curve_2(p,q) → 1 subcurve
approximate_2_object()(xcv, 0.01, back_inserter) → 3 points
push_back_2_object()(cv, p) → 4 subcurves
insert into Arrangement_2 → V=2 E=1
```

---

## 12. Polycurves of arcs — support matrix (all **[verified]** by compiling *and running*)

What `Arr_polycurve_traits_2<Sub>` needs from `Sub`, and where each requirement comes from:

| Requirement on the sub-traits | Used by |
|---|---|
| `Point_2`, `X_monotone_curve_2`, `Curve_2`, `Multiplicity`, `Has_merge_category`, the 4 side categories, `Has_left_category`, `Has_do_intersect_category` | typedefs |
| `Compare_x_2`, `Compare_xy_2`, `Equal_2`, `Compare_y_at_x_2`, `Compare_y_at_x_left_2`, `Compare_y_at_x_right_2` | basic predicates |
| **`Compare_endpoints_xy_2`** | orientation of every polycurve; used pervasively |
| **`Construct_opposite_2`** | `Construct_opposite_2`, `Make_x_monotone_2`, `CGAL_ALWAYS_LEFT_TO_RIGHT` paths |
| **`Is_vertical_2`** | `Is_vertical_2`, `Push_back/Push_front` preconditions, `Are_mergeable_2` |
| `Construct_min_vertex_2`, `Construct_max_vertex_2` | endpoints, connectivity checks |
| `Make_x_monotone_2`, `Split_2`, `Intersect_2`, `Are_mergeable_2`, `Merge_2` | `Arr_polycurve_traits_2` only |
| **`Trim_2`** | only if you call `trim_2_object()` |
| `Approximate_2` (+`Approximate_number_type`, `Approximate_point_2`) | optional; detected by SFINAE, degrades to `void` |
| `Construct_point_2` | only if you call `construct_point_2_object()` |
| `parameter_space_in_x/y_2` etc. | only when some side category is not oblivious |
| `Subcurve_type_2::bbox()` | only if you call `X_monotone_curve_2::bbox()` |
| `Subcurve_type_2::source()/target()` | only if you use `points_begin()/points_end()` |

| Sub-traits | `Compare_endpoints_xy_2` | `Construct_opposite_2` | `Trim_2` | `Is_vertical_2` | polycurve build + `insert()` | `xcv.bbox()` | polycurve `Approximate_2` |
|---|---|---|---|---|---|---|---|
| `Arr_segment_traits_2` | ✔ | ✔ | ✔ | ✔ | **✔** | ✔ | ✔ (only via `Arr_polyline_traits_2`) |
| `Arr_non_caching_segment_traits_2` | ✔ | ✔ (kernel) | ✘ | ✔ (kernel) | ✔ (but `trim_2_object()` won't compile) | ✔ | ✘ |
| `Arr_circle_segment_traits_2` | ✔ (:798) | ✔ (:819) | ✔ (:838) | ✔ (:187) | **✔** (`V=2 E=1 F=1`) | ✔ | ✘ (sub-traits functor only) |
| `Arr_conic_traits_2` | ✔ (:2800) | ✔ (:2816) | ✔ (:2830) | ✔ (:257) | **✔** (`V=2 E=1 F=1`) | ✘ (deprecated & broken) | ✘ |
| `Arr_Bezier_curve_traits_2` | ✔ (:738) | ✔ (:816) | ✔ (:762) | ✔ (:286) | **✔** (`V=2 E=1 F=1`) | untested | ✘ |

Conclusion: **yes, we can offer “polycurves of arcs”** for circle-segment, conic and Bezier
sub-traits. Practical rules for the binding layer:

1. Build the sub-arcs with the sub-traits, normalise their direction with
   `sub.compare_endpoints_xy_2_object()` / `sub.construct_opposite_2_object()`, then call
   `poly_traits.construct_x_monotone_curve_2_object()(first, last)`.
2. Keep **one** long-lived sub-traits instance and pass its address to
   `Arr_polycurve_traits_2(const Subcurve_traits_2*)`; do not let the polycurve traits own it
   if the sub-traits is stateful (`Arr_Bezier_curve_traits_2` caches!) — the copy ctor would
   silently drop the cache.
3. Do not expose `bbox()` on a conic polycurve; compute the bbox from the subcurves' own
   (non-deprecated) API instead.
4. For rendering, approximate the subcurves individually with
   `sub_traits.approximate_2_object()(subcurve, error, oi, l2r)` and concatenate; only the
   polyline traits gives you a whole-curve `Approximate_2`.
5. `Number_of_points_2` on a polycurve of arcs is only available for `Curve_2`; for
   `X_monotone_curve_2` use `xcv.number_of_subcurves() + 1` directly.

---

## 13. Stream I/O of polycurves (`Arr_geometry_traits/IO/Polycurve_2_iostream.h`)

Included automatically by `Arr_polycurve_basic_traits_2.h`. In `CGAL::internal`:

```cpp
template <typename OutputStream, typename SubcurveType_2, typename PointType_2>
OutputStream& operator<<(OutputStream& os, const Polycurve_2<SubcurveType_2, PointType_2>& xcv);
template <typename InputStream, typename SubcurveType_2, typename PointType_2>
InputStream&  operator>>(InputStream& is,  Polycurve_2<SubcurveType_2, PointType_2>& xcv);
```

Dispatch (via overloads of `write_polycurve` / `read_polycurve`):
* `Polycurve_2<CGAL::Arr_segment_2<K>, P>` and `Polycurve_2<CGAL::Segment_2<K>, P>` use the
  *point* form: `number_of_subcurves()` then the `n+1` **points**;
* everything else uses the *subcurve* form: `number_of_subcurves()` then the `n` **subcurves**.

The point form is **asymmetric**: `write_polyline` prints `number_of_subcurves()` while
`read_polyline` treats that first integer as a point count. Round-trip loses one subcurve.
[verified: written `2 0 0 1 1 2 3`, read back → 1 subcurve.] Use your own serialization.

---

## 14. Quick reference for a type-erased C++ core

```cpp
// segments (bounded, exact)
using SegTraits  = CGAL::Arr_segment_traits_2<CGAL::Exact_predicates_exact_constructions_kernel>;
using SegArr     = CGAL::Arrangement_2<SegTraits>;                  // bounded topology
// linear objects (segments + rays + lines, unbounded)
using LinTraits  = CGAL::Arr_linear_traits_2<CGAL::Exact_predicates_exact_constructions_kernel>;
using LinArr     = CGAL::Arrangement_2<LinTraits>;                  // unbounded topology
// polylines
using PolyTraits = CGAL::Arr_polyline_traits_2<SegTraits>;
using PolyArr    = CGAL::Arrangement_2<PolyTraits>;
// polycurves of arcs
using ArcTraits  = CGAL::Arr_circle_segment_traits_2<CGAL::Exact_predicates_exact_constructions_kernel>;
using PolyArcT   = CGAL::Arr_polycurve_traits_2<ArcTraits>;
```

Lifetime/ownership rules to encode in the wrapper:

* `Arr_segment_traits_2`, `Arr_linear_traits_2`, and the non-caching segment traits are
  **stateless and freely copyable**; the polycurve/polyline traits are **not** (owning raw
  pointer, no `operator=`). Wrap them in `std::unique_ptr` and expose only references.
* Functors returned by `*_object()` hold a reference (or pointer) to the traits; never store
  a functor longer than its traits.
* `Construct_min_vertex_2` / `Construct_max_vertex_2` return `const Point_2&` into the curve
  for `Arr_segment_traits_2`, `Arr_linear_traits_2` and the polycurve traits; copy the point
  before the curve can be destroyed or modified.
* `Polycurve_2::Point_const_iterator` holds a raw pointer to the curve; invalidated by
  `push_back` / `push_front` / `clear` and by curve destruction.
* `Arr_segment_2::line()` mutates a `mutable` cache on first call — do not share a single
  curve object across threads without synchronisation.
* Everything under `CGAL::internal::` (`Polycurve_2`, `X_monotone_polycurve_2`) is
  officially internal; prefer accessing it only through the traits' functors, except for the
  read-only accessors listed in §7 which are the documented polycurve API.
