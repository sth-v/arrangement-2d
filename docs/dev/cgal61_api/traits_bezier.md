# CGAL 6.1 — `Arr_Bezier_curve_traits_2` and the Bezier geometry traits (installed headers)

Source of truth: the headers installed at `/opt/homebrew/include/CGAL` (CGAL 6.1, header-only, CORE
built in, GMP/MPFR from Homebrew). Every signature below is quoted verbatim from:

| File | Contents |
|---|---|
| `CGAL/Arr_Bezier_curve_traits_2.h` | `CGAL::Arr_Bezier_curve_traits_2` (842 lines) |
| `CGAL/Arr_geometry_traits/Bezier_curve_2.h` | `CGAL::_Bezier_curve_2`, `_Bezier_curve_2_rep` (924) |
| `CGAL/Arr_geometry_traits/Bezier_x_monotone_2.h` | `CGAL::_Bezier_x_monotone_2` (2590) |
| `CGAL/Arr_geometry_traits/Bezier_point_2.h` | `CGAL::_Bezier_point_2`, `Originator` (1706) |
| `CGAL/Arr_geometry_traits/Bezier_cache.h` | `CGAL::_Bezier_cache` (874) |
| `CGAL/Arr_geometry_traits/Bezier_bounding_rational_traits.h` | `CGAL::Bezier_bounding_rational_traits`, `_Bez_point_bound`, `_Bez_point_bbox` (1510) |
| `CGAL/Arr_geometry_traits/de_Casteljau_2.h` | free functions (224) |
| `CGAL/CORE_algebraic_number_traits.h` | `CGAL::CORE_algebraic_number_traits` (619) |

All behavioural claims marked **[measured]** were verified by compiling and running test programs in
`…/scratchpad/apimap_bezier/{test.cpp,test2.cpp}` with
`clang++ -std=c++17 -O0 -DCGAL_USE_CORE -DCGAL_USE_GMP -DCGAL_USE_MPFR -I/opt/homebrew/include -lgmp -lmpfr`.

---

## 0. Gotchas / surprises vs. older CGAL (read this first)

1. **`Make_x_monotone_2` now writes `std::variant<Point_2, X_monotone_curve_2>`, not `CGAL::Object`.**
   The type is spelled *inside* the functor body as
   `typedef std::variant<Point_2, X_monotone_curve_2> Make_x_monotone_result;` — the traits class does
   **not** export it, so you must declare the variant yourself in your own code.
   Likewise `Intersect_2` writes into an iterator whose value type must accept both
   `std::pair<Point_2, Multiplicity>` and `X_monotone_curve_2` (use
   `std::variant<std::pair<Point_2, unsigned int>, X_monotone_curve_2>`).

2. **The traits object owns a heap `Bezier_cache` + `Intersection_map`, and copying the traits shares
   them without ownership.** `Arr_Bezier_curve_traits_2(const Self&)` and `operator=` set
   `m_owner = false` and alias `tr.p_cache` / `tr.p_inter_map`. Only the *originally
   default-constructed* instance deletes them. **If the original dies while a copy is alive, every
   copy has a dangling cache** → use-after-free. Keep exactly one long-lived traits instance and hand
   the arrangement a *pointer* to it (`Arrangement_2 arr(&traits)`).

3. **There is no public accessor for the traits' cache.** `p_cache` is `private` and every functor
   stores its own private copy of the pointer. Any direct call to
   `Point_2::make_exact(cache)`, `X_monotone_curve_2::point_position(p, cache)`,
   `Point_2::compare_xy(q, cache)`, … requires *you* to construct your **own**
   `Traits_2::Bezier_cache` object. That is correct (the cache is keyed by curve id + polynomials and
   is purely memoising), it just duplicates work. `_Bezier_cache` is **non-copyable**
   (private copy ctor/assignment) — hold it by value in your core object or by `std::unique_ptr`.

4. **Arrangement vertices are frequently NOT exact.** **[measured]** In a 2-curve arrangement, 2 of 9
   vertices had `is_exact() == false` (unrefined vertical-tangency points). `Point_2::x()` / `y()`
   have `CGAL_precondition(_rep().is_exact())` and dereference a null `Algebraic*` when preconditions
   are compiled out (`-DCGAL_NDEBUG` / `NDEBUG`) → **silent UB in release builds**. Always guard:
   `if (!p.is_exact()) p.make_exact(my_cache);` before `x()`/`y()`.
   `approximate()` is always safe (falls back to the bbox midpoint).

5. **`X_monotone_curve_2::parameter_range()` is an *approximation of an approximation* and can be
   badly wrong before the endpoints are made exact.** **[measured]** For the middle arc of an S-shaped
   cubic it returned `[0.276393, 0.625]` while the true range is `[0.276393, 0.723607]` — a 14 % error
   on `t_max`. It just averages `point_bound().t_min`/`t_max` of the endpoints' originators. Calling
   `make_exact(cache)` on `source()` and `target()` first makes it exact-to-double.

6. **The exact algebraic parameter range is reachable, but only indirectly.** `_t_range(cache)` is
   `private`. The public route is:
   `cv.source().get_originator(cv.supporting_curve(), cv.xid())->parameter()` — after ensuring
   `has_parameter()` (i.e. after `make_exact(cache)`). See §10.1.

7. **`Curve_2::id()` is `reinterpret_cast<size_t>(rep pointer)`.** IDs are *reused* after a curve rep
   is freed, and the `Bezier_cache` / `Intersection_map` are keyed by those IDs and are **never
   invalidated**. If you drop the last handle to a `Curve_2` and then build a new one while the same
   cache is alive, the new curve can inherit stale vertical tangencies / intersections. **Keep every
   `Curve_2` you ever inserted alive for as long as the traits/cache lives** (an owning
   `std::vector<Curve_2>` in your core object). The cache also grows monotonically — there is no
   `clear()`.

8. **No `Approximate_2`, no `Construct_x_monotone_curve_2`, no `Construct_curve_2`.** The Bezier
   traits is not an `ArrangementApproximateTraits_2`. `CGAL::draw()` SFINAEs on
   `approximate_2_object()` and silently degrades to straight chords. Rendering must go through
   `Curve_2::sample()` (§10.2) or `Point_2::approximate()`.

9. **Comparing two `Point_2`s mutates them (through `const`).** `compare_x` / `compare_xy` /
   `equals` `const_cast` the reps, refine or exactify them, and on `EQUAL` `compare_xy` **merges the
   originator lists and rebinds `p2` to `p1`'s representation** (`p2 = p1;`). Not re-entrant, not
   thread-safe, and it changes `originators_begin()..end()` under you. `Handle_for`'s refcount is
   `std::atomic_uint`, but the lazily-filled rep contents are not synchronised.

10. **Copying an `Arrangement_2` does *not* copy the traits.** `assign()` does
    `m_geom_traits = (arr.m_own_traits) ? new Traits_adaptor_2 : arr.m_geom_traits;` — a **fresh
    default-constructed** traits (hence a fresh empty cache) when the source owned its traits. Cached
    work is lost; still correct, just slow.

11. **`Multiplicity` from `Intersect_2` is `0` or `1` only** and is not a real multiplicity:
    `0` = "unknown / rational / endpoint", `1` = "simple, approximated". **[measured]** three
    transversal intersections all reported `mult = 0`.

12. Minor but binding-relevant: `Construct_opposite_2::operator()` is **non-`const`** (needs a
    non-const functor object). `Merge_2` and `Trim_2` have **private constructors**
    (`friend class Arr_Bezier_curve_traits_2`) — obtain them only via `merge_2_object()` /
    `trim_2_object()`. All `Bezier_bounding_rational_traits` members are non-`const`.

13. `Point_2::get_bbox` parameter order is `(min_x, min_y, max_x, max_y)` — *not* the CGAL
    `Bbox_2` `(xmin, xmax, ymin, ymax)` convention.

---

## 1. Instantiation recipe

```cpp
#include <CGAL/Cartesian.h>
#include <CGAL/CORE_algebraic_number_traits.h>
#include <CGAL/Arr_Bezier_curve_traits_2.h>
#include <CGAL/Arrangement_2.h>

typedef CGAL::CORE_algebraic_number_traits            Nt_traits;
typedef Nt_traits::Rational                           NT_rat;      // CORE::BigRat
typedef Nt_traits::Algebraic                          NT_alg;      // CORE::Expr
typedef CGAL::Cartesian<NT_rat>                       Rat_kernel;
typedef CGAL::Cartesian<NT_alg>                       Alg_kernel;
typedef CGAL::Arr_Bezier_curve_traits_2<Rat_kernel, Alg_kernel, Nt_traits> Traits_2;
typedef CGAL::Arrangement_2<Traits_2>                 Arrangement_2;
```

**[measured]** object sizes: `sizeof(Traits_2) == 24`, `sizeof(Curve_2) == 8`,
`sizeof(X_monotone_curve_2) == 40`, `sizeof(Point_2) == 8`. `Curve_2` and `Point_2` are
`Handle_for<…>` (one pointer); `X_monotone_curve_2` is a plain value type (curve handle + xid + two
point handles + 3 bools).

---

## 2. `CGAL::Arr_Bezier_curve_traits_2`

```cpp
template <class RatKernel_, class AlgKernel_, class NtTraits_,
          class BoundingTraits_ = Bezier_bounding_rational_traits<RatKernel_> >
class Arr_Bezier_curve_traits_2
```

### 2.1 Public typedefs (verbatim)

```cpp
typedef RatKernel_                             Rat_kernel;
typedef AlgKernel_                             Alg_kernel;
typedef NtTraits_                              Nt_traits;
typedef BoundingTraits_                        Bounding_traits;
typedef Arr_Bezier_curve_traits_2<Rat_kernel, Alg_kernel, Nt_traits, Bounding_traits> Self;

typedef typename Nt_traits::Integer            Integer;     // CORE::BigInt
typedef typename Rat_kernel::FT                Rational;    // CORE::BigRat
typedef typename Alg_kernel::FT                Algebraic;   // CORE::Expr

typedef typename Rat_kernel::Point_2           Rat_point_2;
typedef typename Alg_kernel::Point_2           Alg_point_2;

// Category tags:
typedef Tag_true                               Has_left_category;
typedef Tag_true                               Has_merge_category;
typedef Tag_false                              Has_do_intersect_category;

typedef Arr_oblivious_side_tag                 Left_side_category;
typedef Arr_oblivious_side_tag                 Bottom_side_category;
typedef Arr_oblivious_side_tag                 Top_side_category;
typedef Arr_oblivious_side_tag                 Right_side_category;

typedef _Bezier_curve_2<Rat_kernel, Alg_kernel, Nt_traits, Bounding_traits>      Curve_2;
typedef _Bezier_x_monotone_2<Rat_kernel, Alg_kernel, Nt_traits, Bounding_traits> X_monotone_curve_2;
typedef _Bezier_point_2<Rat_kernel, Alg_kernel, Nt_traits, Bounding_traits>      Point_2;

typedef typename X_monotone_curve_2::Multiplicity    Multiplicity;   // == unsigned int

// Type definition for the vertical-tangnecy and intersection point cache.
typedef _Bezier_cache<Nt_traits>                     Bezier_cache;
```

`Rational` must be the same type as `Nt_traits::Rational`, `Algebraic` the same as
`Nt_traits::Algebraic` (documented precondition in the class comment). All four side categories being
`Arr_oblivious_side_tag` means the traits works only with the **bounded** planar topology; no
unbounded-curve support.

### 2.2 Private state (matters for lifetime)

```cpp
typedef typename X_monotone_curve_2::Intersection_map   Intersection_map;   // private typedef

mutable Bezier_cache * p_cache;         // vertical tangencies + exact intersections
mutable Intersection_map * p_inter_map; // curve-pair -> approximated intersection points
bool m_owner;                           // does this instance own the two structures
```

### 2.3 Construction / copy / destruction (verbatim)

```cpp
Arr_Bezier_curve_traits_2 ()                       // p_cache = new Bezier_cache;
                                                   // p_inter_map = new Intersection_map;
                                                   // m_owner = true;
Arr_Bezier_curve_traits_2 (const Self& tr) :
  p_cache (tr.p_cache), p_inter_map (tr.p_inter_map), m_owner (false) {}
Self& operator= (const Self& tr);                  // aliases, sets m_owner = false
~Arr_Bezier_curve_traits_2 ();                     // deletes only if m_owner
```

**Cache lifetime rules for a type-erased core:**
* Store one `Arr_Bezier_curve_traits_2` **by value** in the owning core object; never let a copy
  outlive it. Pass `&traits` to `Arrangement_2`'s `Arrangement_2(const Geometry_traits_2*)` ctor
  (`m_own_traits = false`, so the arrangement will *not* delete it — you must keep it alive).
* `Curve_2` / `Point_2` / `X_monotone_curve_2` objects **do not hold a reference to the cache**; they
  only receive it as a function argument. Destroying the traits therefore never dangles a curve —
  it only dangles *copies of the traits*.
* Because the cache is keyed by `Curve_2::id()` (a raw pointer value), keep every inserted `Curve_2`
  alive as long as the traits lives (gotcha 7).
* Operations that **need a cache**: `Compare_x_2`, `Compare_xy_2`, `Compare_y_at_x_2`,
  `Compare_y_at_x_left_2`, `Compare_y_at_x_right_2`, `Equal_2`, `Make_x_monotone_2`, `Intersect_2`
  (also needs the intersection map). Operations that **do not**: `Construct_min_vertex_2`,
  `Construct_max_vertex_2`, `Is_vertical_2`, `Split_2`, `Are_mergeable_2`, `Merge_2`,
  `Compare_endpoints_xy_2`, `Construct_opposite_2`, and `Trim_2` except for its
  (precondition-only) use of `Compare_y_at_x_2`/`Equal_2`.

### 2.4 Functors — complete list

| Functor | `…_object()` | Needs cache | Signature(s) |
|---|---|---|---|
| `Compare_x_2` | `compare_x_2_object() const` | yes | `Comparison_result operator() (const Point_2& p1, const Point_2& p2) const` |
| `Compare_xy_2` | `compare_xy_2_object() const` | yes | `Comparison_result operator() (const Point_2& p1, const Point_2& p2) const` |
| `Construct_min_vertex_2` | `construct_min_vertex_2_object() const` | no | `const Point_2& operator() (const X_monotone_curve_2 & cv) const` → `cv.left()` |
| `Construct_max_vertex_2` | `construct_max_vertex_2_object() const` | no | `const Point_2& operator() (const X_monotone_curve_2 & cv) const` → `cv.right()` |
| `Is_vertical_2` | `is_vertical_2_object() const` | no | `bool operator() (const X_monotone_curve_2& cv) const` |
| `Compare_y_at_x_2` | `compare_y_at_x_2_object() const` | yes | `Comparison_result operator() (const Point_2& p, const X_monotone_curve_2& cv) const` |
| `Compare_y_at_x_left_2` | `compare_y_at_x_left_2_object() const` | yes | `Comparison_result operator() (const X_monotone_curve_2& cv1, const X_monotone_curve_2& cv2, const Point_2& p) const` |
| `Compare_y_at_x_right_2` | `compare_y_at_x_right_2_object() const` | yes | same 3-arg shape |
| `Equal_2` | `equal_2_object() const` | yes | `bool operator() (const X_monotone_curve_2& cv1, const X_monotone_curve_2& cv2) const` and `bool operator() (const Point_2& p1, const Point_2& p2) const` |
| `Make_x_monotone_2` | `make_x_monotone_2_object() const` | yes | `template <typename OutputIterator> OutputIterator operator() (const Curve_2& B, OutputIterator oi) const` |
| `Split_2` | `split_2_object() const` | no | `void operator() (const X_monotone_curve_2& cv, const Point_2 & p, X_monotone_curve_2& c1, X_monotone_curve_2& c2) const` |
| `Intersect_2` | `intersect_2_object() const` | yes (+ map) | `template<class OutputIterator> OutputIterator operator() (const X_monotone_curve_2& cv1, const X_monotone_curve_2& cv2, OutputIterator oi) const` |
| `Are_mergeable_2` | `are_mergeable_2_object() const` | no | `bool operator() (const X_monotone_curve_2& cv1, const X_monotone_curve_2& cv2) const` |
| `Merge_2` | `merge_2_object() const` | no | `void operator() (const X_monotone_curve_2& cv1, const X_monotone_curve_2& cv2, X_monotone_curve_2& c) const` |
| `Compare_endpoints_xy_2` | `compare_endpoints_xy_2_object() const` | no | `Comparison_result operator() (const X_monotone_curve_2& cv) const` |
| `Trim_2` | `trim_2_object() const` | no | `X_monotone_curve_2 operator()(const X_monotone_curve_2& xcv, const Point_2& src, const Point_2& tgt) const` |
| `Construct_opposite_2` | `construct_opposite_2_object() const` | no | `X_monotone_curve_2 operator() (const X_monotone_curve_2& cv)` — **non-const** |

**Absent** (verified by grep over the whole header): `Approximate_2`, `Approximate_number_type`,
`Approximate_point_2`, `approximate_2_object`, `Construct_curve_2`, `Construct_x_monotone_curve_2`,
`Parameter_space_in_x_2` / `_y_2`, `Compare_x_near_boundary_2`, `Is_on_x_identification_2`,
`Do_intersect_2` (`Has_do_intersect_category == Tag_false`), and any `bbox`-returning functor.

Preconditions from the doc comments:
* `Compare_y_at_x_2`: *"`p` is in the x-range of `cv`."*
* `Compare_y_at_x_left_2` / `_right_2`: *"The point `p` lies on both curves, and both of them must be
  also be defined (lexicographically) to its left [right]."*
* `Split_2`: *"`p` lies on `cv` but is not one of its end-points."*
* `Merge_2`: *"The two curves are mergeable."* — enforced by
  `CGAL_precondition(m_traits->are_mergeable_2_object()(cv2, cv1));`
* `Trim_2`: *"`src != tgt`; both points must be interior and must lie on `cv`."* The implementation
  additionally **normalises the direction**: it swaps `src`/`tgt` so the result keeps `xcv`'s
  orientation.

### 2.5 `Make_x_monotone_2` — exact behaviour

Two paths, both producing **only `X_monotone_curve_2` alternatives** for a well-formed Bezier curve
(no isolated `Point_2` is ever emitted; **[measured]** an S-shaped cubic with 2 vertical tangencies
produced `3` variants, all `X_monotone_curve_2`, `0` points):

1. **Filtered path.** `Bounding_traits::compute_vertical_tangency_points(cpts, oi)` bounds the
   vertical-tangency points. If *every* bound has `can_refine == true`, each becomes a `Point_2`:
   * `bound.type == RATIONAL_PT` → `pt = Point_2(B, t0)` with `t0 = bound.t_min` (a `Rational`);
   * otherwise → `pt.add_originator(Point_2::Originator(B, bound))` (an **inexact** point, `xid == 0`)
     and then `pt.set_bbox(bbox)`.
2. **Exact fallback.** If any bound could not be refined, it computes
   `p_cache->get_vertical_tangencies(B.id(), B.x_polynomial(), B.x_norm())` and builds
   `Point_2(B, *it)` from each `Algebraic` root of `X'(t) = 0` in `(0,1)`.

In both paths the subcurves are built as
```cpp
unsigned int xid = 1;
Point_2 p0(B, xid, Rational(0));          // rational start point, xid-tagged
for each vertical tangency point v:
    *oi++ = X_monotone_curve_2(B, xid, p0, v, *p_cache);  xid++;  p0 = v;
Point_2 p1(B, xid, Rational(1));
*oi++ = X_monotone_curve_2(B, xid, p0, p1, *p_cache);
```
So: **`k` vertical tangency points ⇒ `k+1` x-monotone subcurves with `xid = 1 … k+1`**, and every
vertical-tangency point becomes an arrangement vertex (shared handle between consecutive subcurves).
The `xid` is *per Bezier curve*, starts at `1`, and `xid == 0` is the sentinel meaning "originator
not bound to a specific x-monotone subcurve".

Self-intersections of the base curve are *not* resolved here — they appear later via `Intersect_2`
on two subcurves of the same curve (guarded by `Curve_2::has_no_self_intersections()`).

### 2.6 `Intersect_2` — output details

`_Bezier_x_monotone_2::intersect` writes, in this order:
1. `Intersection_point(left(), 0)` if the two subcurves share the same supporting `Curve_2` and
   `left()` is handle-identical to `cv.left()` or `cv.right()`;
2. either a single `X_monotone_curve_2` (the overlap) **or** the interior intersection points sorted
   xy-lexicographically as `std::pair<Point_2, Multiplicity>`;
3. `Intersection_point(right(), 0)` under the mirror condition of (1).

`typedef std::pair<Point_2, Multiplicity> Intersection_point;` is a **private** typedef of
`_Bezier_x_monotone_2` — declare your own `std::pair<Point_2, unsigned int>` in the output variant.

**[measured]** two crossing curves (cubic + quadratic) produced 3 intersection points, all
`is_exact() == true`, `n_originators == 2`, `mult == 0`.

---

## 3. `CGAL::_Bezier_curve_2` (`Traits_2::Curve_2`)

```cpp
template <class RatKernel_, class AlgKernel_, class NtTraits_, class BoundingTraits_>
class _Bezier_curve_2 :
  public Handle_for<_Bezier_curve_2_rep<RatKernel_, AlgKernel_, NtTraits_, BoundingTraits_> >
```

Reference-counted handle (`Handle_for`, `std::atomic_uint` refcount). Copies are shallow and share
the same rep — hence the same `id()`.

### 3.1 Public typedefs

```cpp
typedef RatKernel_        Rat_kernel;      typedef AlgKernel_       Alg_kernel;
typedef NtTraits_         Nt_traits;       typedef BoundingTraits_  Bounding_traits;
typedef _Bezier_curve_2<Rat_kernel, Alg_kernel, Nt_traits, Bounding_traits>  Self;

typedef typename Bcv_rep::Rat_point_2           Rat_point_2;    // Rat_kernel::Point_2
typedef typename Bcv_rep::Alg_point_2           Alg_point_2;    // Alg_kernel::Point_2
typedef typename Bcv_rep::Integer               Integer;
typedef typename Bcv_rep::Rational              Rational;
typedef typename Bcv_rep::Algebraic             Algebraic;
typedef typename Bcv_rep::Polynomial            Polynomial;     // Nt_traits::Polynomial
typedef typename Control_pt_vec::const_iterator Control_point_iterator;
```
The underlying container is `std::deque<Rat_point_2>` (private typedef `Control_point_vec`), chosen
because the code needs `push_front`. `Control_point_iterator` is therefore a deque const_iterator
(random access, but **not** a raw pointer).

Inherited from `Handle_for` and usable: `bool identical(const Handle_for&) const noexcept`,
`bool is_shared() const noexcept`, `bool unique() const noexcept`, `void swap(Handle_for&)`,
`typedef std::ptrdiff_t Id_type`.

### 3.2 Constructors / assignment

```cpp
_Bezier_curve_2 ();                                  // default: 0 control points
_Bezier_curve_2 (const Self& bc);                    // shallow, shares rep
template <class InputIterator>
_Bezier_curve_2 (InputIterator pts_begin, InputIterator pts_end);
Self& operator= (const Self& bc);
```
Precondition on the range ctor (doc comment): *"The value-type of the input iterator must be
`Rat_kernel::Point_2`. It is forbidden to specify two identical consecutive control points."*
The rep additionally asserts `CGAL_precondition_msg (pts_size > 1, "There must be at least 2 control
points.")`. **The identical-consecutive-points check is commented out in 6.1** (`//SL: According to
the fact that all operations are based on polynomials duplicated control points can be allowed.`) —
duplicates are in practice tolerated, but the degree-`n` polynomial construction still asserts
`n > 0`.

Construction eagerly computes the double `Bbox_2` and calls
`Bounding_traits::may_have_self_intersections(ctrl_pts)` once (stored in `_no_self_inter`).
The `X(t)`, `Y(t)` polynomials are **lazily** built on first access (`_construct_polynomials()`,
mutable members, no locking → not thread-safe).

### 3.3 Members (all `const`, verbatim)

```cpp
size_t id () const;                                  // reinterpret_cast<size_t>(this->ptr())
const Polynomial& x_polynomial () const;             // X(t) numerator (integer coeffs)
const Integer&    x_norm () const;                   // X(t) = x_polynomial(t) / x_norm
const Polynomial& y_polynomial () const;
const Integer&    y_norm () const;
unsigned int number_of_control_points () const;
const Rat_point_2& control_point (unsigned int i) const;      // \pre i < number_of_control_points()
Control_point_iterator control_points_begin () const;
Control_point_iterator control_points_end () const;
bool is_same (const Self& bc) const;                 // handle identity (this->identical(bc))
Rat_point_2 operator() (const Rational& t) const;    // \pre 0 <= t <= 1
Alg_point_2 operator() (const Algebraic& t) const;   // \pre 0 <= t <= 1
template <class OutputIterator>
OutputIterator sample (const double& t_start, const double& t_end,
                       unsigned int n_samples, OutputIterator oi) const;
template <class OutputIterator>
OutputIterator get_t_at_x (const Rational& x0, OutputIterator oi) const;
template <class OutputIterator>
OutputIterator get_t_at_y (const Rational& y0, OutputIterator oi) const;
bool has_same_support (const Self& bc) const;
const Bbox_2& bbox () const;
bool has_no_self_intersections () const;
```

Plus the free stream operators
`std::ostream& operator<< (std::ostream&, const _Bezier_curve_2&)` (writes `n  p0  p1 …`) and
`std::istream& operator>> (std::istream&, _Bezier_curve_2&)`.

Details:

* **`operator()(const Rational& t)`** — `CGAL_precondition(sign(t) != NEGATIVE)` and
  `CGAL_precondition(compare(t, Rational(1)) != LARGER)`. Shortcuts `t == 0` → first control point,
  `t == 1` → last control point. If the polynomials have not been built yet it uses de Casteljau
  (`point_on_Bezier_curve_2`), otherwise `evaluate_at(x_polynomial, t) / Rational(x_norm, 1)`.
  Returns a **`Rat_point_2` by value** — exact rational coordinates.
* **`operator()(const Algebraic& t)`** — same 0/1 shortcuts (converting the control point through
  `nt_traits.convert`), else
  `x = evaluate_at(x_polynomial(), t) / nt_traits.convert(x_norm())`. Returns `Alg_point_2`
  (`CORE::Expr` coordinates). This is the way to evaluate at an algebraic parameter.
* **`sample(t_start, t_end, n_samples, oi)`** — *pure `double`* path: it converts control points with
  `CGAL::to_double`, then evaluates `point_on_Bezier_curve_2` on a `Simple_cartesian<double>` kernel.
  It emits exactly `n = max(n_samples, 2)` values, `*oi = std::make_pair(p.x(), p.y())`, at
  `t_start + k*(t_end-t_start)/(n-1)`, `k = 0…n-1`, **inclusive of both ends**.
  **The output value type must be `std::pair<double,double>`.** Note the first two parameters are
  `const double&`, so an exact algebraic range must be converted with `CGAL::to_double` first.
* **`get_t_at_x(x0, oi)` / `get_t_at_y(y0, oi)`** — output value type is `Algebraic`. Doc comment:
  *"the function does not return only values between 0 and 1, so the output t-values may belong to
  the imaginary continuation of the curve."* Implemented via `Nt_traits::compute_polynomial_roots` on
  `P(t)·denom(x0) − numer(x0)·norm`. Returns immediately (empty) if `degree(poly) <= 0`.
  **[measured]** `get_t_at_x(1)` on a cubic returned 3 roots `{0.112702, 0.5, 0.887298}`.
* **`has_same_support(bc)`** — samples `deg1*deg2 + 1` points of `*this` and checks each lies on `bc`
  via `get_t_at_x` + `operator()(Algebraic)`. Expensive; used by `equals()` for overlap detection.
* **`bbox()`** — a `CGAL::Bbox_2` of the **control polygon** (double), computed once at construction;
  a conservative superset of the curve (convex hull property).
* **`has_no_self_intersections()`** — conservative: `true` ⇒ definitely none; `false` ⇒ maybe.

There is **no** `degree()` accessor (use `number_of_control_points() - 1`), no `split()`, no
`reverse()`, no `control_point` mutation — `Curve_2` is immutable after construction.

---

## 4. `CGAL::_Bezier_point_2` (`Traits_2::Point_2`)

```cpp
template <class RatKernel_, class AlgKernel_, class NtTraits_, class BoundingTraits_>
class _Bezier_point_2 :
  public Handle_for<_Bezier_point_2_rep<RatKernel_, AlgKernel_, NtTraits_, BoundingTraits_> >
```

Class comment: *"Representation of a point on a Bezier curve. The point has algebraic coefficients,
with an additional list of originator. An originator is a pair of the form `<B(t), t0>`, meaning that
this point is obtained by computing `B(t0)` on the curve `B(t)`."*

Rep state: `Algebraic* p_alg_x, *p_alg_y; Rational* p_rat_x, *p_rat_y; std::list<Originator> _origs;
Bez_point_bbox _bbox;` — all four coordinate pointers may be null.

### 4.1 Public typedefs

```cpp
typedef RatKernel_ Rat_kernel;  typedef AlgKernel_ Alg_kernel;
typedef NtTraits_  Nt_traits;   typedef BoundingTraits_ Bounding_traits;
typedef _Bezier_point_2<…> Self;

typedef typename Bpt_rep::Rat_point_2           Rat_point_2;
typedef typename Bpt_rep::Alg_point_2           Alg_point_2;
typedef typename Bpt_rep::Rational              Rational;
typedef typename Bpt_rep::Algebraic             Algebraic;
typedef typename Bpt_rep::Curve_2               Curve_2;
typedef typename Bpt_rep::Originator            Originator;
typedef typename Bpt_rep::Bezier_cache          Bezier_cache;

typedef typename Bpt_rep::Orig_const_iter       Originator_iterator;  // std::list<Originator>::const_iterator
typedef typename Bpt_rep::Bez_point_bound       Bez_point_bound;
typedef typename Bpt_rep::Bez_point_bbox        Bez_point_bbox;
```

### 4.2 Constructors (verbatim)

```cpp
_Bezier_point_2 ();                                                    // empty, not exact
_Bezier_point_2 (const Self& bpt);                                     // shallow
_Bezier_point_2 (const Algebraic& x, const Algebraic& y, bool dummy);  // "only for private use"
_Bezier_point_2 (const Rational& x, const Rational& y);
_Bezier_point_2 (const Curve_2& B, const Rational& t0);                // \pre 0 <= t0 <= 1
_Bezier_point_2 (const Curve_2& B, unsigned int xid, const Rational& t0);   // \pre 0 <= t0 <= 1
_Bezier_point_2 (const Curve_2& B, const Algebraic& t0);               // \pre 0 <= t0 <= 1
_Bezier_point_2 (const Curve_2& B, unsigned int xid, const Algebraic& t0); // \pre 0 <= t0 <= 1
Self& operator= (const Self& pt);
```

Semantics of each:
* `(x, y, bool)` — the third argument is an unused tag to disambiguate from the `Rational` overload
  (`Algebraic` and `Rational` may otherwise be convertible). Sets `p_alg_x/y`, leaves `p_rat_x/y`
  null (`is_rational() == false`), bbox = `double_interval(x) × double_interval(y)`. **No
  originators** — such a point is not attached to any curve.
* `(Rational x, Rational y)` — sets both rational and algebraic coordinates,
  `bbox = [x,x] × [y,y]`. No originators.
* `(B, t0 : Rational)` and `(B, xid, t0)` — pushes an `Originator(B[, xid], nt_traits.convert(t0))`
  whose bound `type` is forced to `RATIONAL_PT`, then evaluates `p = B(t0)` (exact rational) and sets
  `p_rat_x/y`, `p_alg_x/y`, `bbox = [x,x] × [y,y]`. Result: `is_exact() && is_rational()`.
* `(B, t0 : Algebraic)` and `(B, xid, t0)` — pushes `Originator(B[, xid], t0)` (whose
  `set_parameter` also sets `bound.t_min/t_max = double_interval(t0)`), evaluates `p = B(t0)` as
  `Alg_point_2`, sets only the algebraic coordinates. Result: `is_exact()`, `!is_rational()`.
  **[measured]** `Point_2 q(B, cv.xid(), t_mid); cv.point_position(q, cache) == EQUAL` — this is the
  supported way to construct a point **on** a given x-monotone subcurve at parameter `t`.

### 4.3 Members (verbatim)

```cpp
bool is_same (const Self& pt) const;                 // handle identity
bool is_exact () const;                              // p_alg_x && p_alg_y
bool is_rational () const;                           // p_rat_x && p_rat_y
const Algebraic& x () const;                         // \pre _rep().is_exact()
const Algebraic& y () const;                         // \pre _rep().is_exact()
std::pair<double, double> approximate () const;      // always safe
operator Rat_point_2 () const;                       // \pre _rep().is_rational()

bool refine () const;                                // returns whether refinement was possible
void fit_to_bbox () const;
void make_exact (Bezier_cache& cache) const;

Comparison_result compare_x  (const Self& pt, Bezier_cache& cache) const;
Comparison_result compare_xy (const Self& pt, Bezier_cache& cache) const;
bool equals (const Self& pt, Bezier_cache& cache) const;   // == (compare_xy(...) == EQUAL)

Comparison_result vertical_position (const typename Bounding_traits::Control_points& cp,
                                     const typename Bounding_traits::NT& t_min,
                                     const typename Bounding_traits::NT& t_max) const;

Originator_iterator get_originator (const Curve_2& B) const;
Originator_iterator get_originator (const Curve_2& B, unsigned int xid) const;
Originator_iterator originators_begin () const;
Originator_iterator originators_end () const;
void add_originator (const Originator& o) const;
void update_originator_xid (const Originator& o, unsigned int xid) const;
void merge_originators (const Self& pt) const;

void set_bbox (const Bez_point_bbox& bbox);          // NON-const
void get_bbox (typename Bounding_traits::NT& min_x, typename Bounding_traits::NT& min_y,
               typename Bounding_traits::NT& max_x, typename Bounding_traits::NT& max_y) const;
```
Plus `std::ostream& operator<< (std::ostream&, const _Bezier_point_2&)` which prints
`to_double(x) to_double(y)` when exact and `~mid_x ~mid_y` otherwise.

Notes that matter for bindings:

* Every "mutating" member above except `set_bbox` is declared `const` and internally
  `const_cast`s the rep. **`Point_2` is logically mutable through a const reference.**
* `approximate()` returns `to_double(x), to_double(y)` when exact, else the **bbox centre**
  `to_double((min_x+max_x)/2)`. Never throws, never needs a cache. Use it for rendering.
* `operator Rat_point_2()` is the only way to get *exact rationals*. It is an implicit conversion
  operator — in C++ you normally write `Rat_point_2 rp = (Rat_point_2) p;` (a functional/ C-style
  cast; `static_cast` also works). **[measured]** on `Point_2 p0(B, Rational(1,3))` it yields the
  exact rational `(38/27, 1)`.
* `make_exact(cache)` **never fills in `p_rat_x/p_rat_y`** — after it, a genuinely algebraic point is
  `is_exact() == true` and `is_rational() == false`, so `operator Rat_point_2()` still fails.
  **[measured]** exactly this pattern for a vertical-tangency vertex.
  Internals: with 1 originator it asserts the bound type is `VERTICAL_TANGENCY_PT` and picks the
  cached vertical tangency whose `t` lies in `[bound.t_min, bound.t_max]`; with 2 originators it
  asserts `INTERSECTION_PT` and uses `cache.get_intersections(...)`, matching `(s,t)` against both
  bounds (with a swap allowed for a self-intersection where both originators are the same curve).
  On no match: `CGAL_error()`.
* `refine()` bisects the bounding control polygon via
  `Bounding_traits::refine_vertical_tangency_point` / `refine_intersection_point`. Returns `false`
  when no further refinement is possible **or** the point is already exact. When refinement produces
  a `RATIONAL_PT` bound the point becomes fully exact *and rational*.
* `fit_to_bbox()` repeatedly `_refine()`s until every originator's control-polygon bbox is contained
  in the point's bbox. Used internally before intersection filtering.
* `get_originator(B, xid)` matches an originator on `B` whose `xid()` is **either `xid` or `0`**
  (`0` = "not bound to a specific x-monotone subcurve"). **[measured]** vertical-tangency points
  created by the filtered `Make_x_monotone_2` path have `xid() == 0`.
* `get_originator` returns `originators_end()` when not found — always test.
* `compare_xy` mutates: see gotcha 9.
* `get_bbox` outputs `Bounding_traits::NT` = `Rat_kernel::FT` = `Rational` (exact `CORE::BigRat`).
  For a rational point the box is degenerate and equals the exact coordinates.

### 4.4 `Point_2::Originator`

Nested public class (`typedef typename Bpt_rep::Originator Originator;`). Members (verbatim):

```cpp
Originator (const Curve_2& c, const Algebraic& t);
Originator (const Curve_2& c, unsigned int xid, const Algebraic& t);
Originator (const Curve_2& c, const Bez_point_bound& bpb);
Originator (const Curve_2& c, unsigned int xid, const Bez_point_bound& bpb);
Originator (const Originator& other);                 // deep-copies the lazy Algebraic* p_t
~Originator();
Originator& operator= (const Originator& other);

const Curve_2& curve () const;
unsigned int xid () const;
const Bez_point_bound& point_bound () const;
void update_point_bound (const Bez_point_bound& bpb);
bool has_parameter () const;                          // p_t != nullptr
const Algebraic& parameter () const;                  // \pre the parameter value is available
void set_parameter (const Algebraic& t);              // \pre not yet set
void set_xid (unsigned int xid);                      // \pre xid() == 0 and xid > 0
```

`set_parameter(t)` also refreshes `_bpb.t_min/_bpb.t_max` from `nt_traits.double_interval(t)`
(converted to `Rational` via `Bounding_traits::NT`), which is why `parameter_range()` becomes
accurate after `make_exact`.

**`parameter()` is the only public route to the exact algebraic `t` of a point on a curve.**

---

## 5. `CGAL::_Bezier_x_monotone_2` (`Traits_2::X_monotone_curve_2`)

```cpp
template <class Rat_kernel_, class Alg_kernel_, class Nt_traits_, class Bounding_traits_>
class _Bezier_x_monotone_2
```
Plain value type (not a handle). Members: `Curve_2 _curve; unsigned int _xid; Point_2 _ps, _pt;
bool _dir_right, _inc_to_right, _is_vert;` — copying is cheap (two atomic refcount bumps).

### 5.1 Public typedefs

```cpp
typedef Rat_kernel_ Rat_kernel;  typedef Alg_kernel_ Alg_kernel;
typedef Nt_traits_  Nt_traits;   typedef Bounding_traits_ Bounding_traits;
typedef _Bezier_curve_2<…>       Curve_2;
typedef _Bezier_point_2<…>       Point_2;
typedef _Bezier_x_monotone_2<…>  Self;
typedef unsigned int             Multiplicity;
typedef _Bezier_cache<Nt_traits> Bezier_cache;

// declared public further down:
typedef std::map<Curve_pair, Intersection_list, Less_curve_pair>  Intersection_map;
typedef typename Intersection_map::value_type                     Intersection_map_entry;
typedef typename Intersection_map::iterator                       Intersection_map_iterator;
```
`Curve_pair` (= `std::pair<Curve_id, Curve_id>` = `std::pair<size_t,size_t>`),
`Intersection_list` (= `std::list<Point_2>`) and `Intersection_point`
(= `std::pair<Point_2, Multiplicity>`) are **private**; only `Intersection_map` and its two companion
typedefs are public (they are what `Arr_Bezier_curve_traits_2` uses for `p_inter_map`).

### 5.2 Constructors

```cpp
_Bezier_x_monotone_2();                        // _xid = 0, _dir_right = false, _is_vert = false
_Bezier_x_monotone_2(const Curve_2& B, unsigned int xid,
                     const Point_2& ps, const Point_2& pt,
                     Bezier_cache& cache);
```
Doc preconditions: *"`B` should be an originator of both `ps` and `pt`"*, *"`xid` is a non-zero serial
number"* (`CGAL_precondition(xid > 0)`), and, from the code, `t_src != t_trg`
(`CGAL_precondition(t_res != EQUAL)`).

`xid` meaning (doc comment): *"The serial number of the x-monotone subcurve with respect to the
parameter range of the Bezier curve. For example, if `B` is split to two x-monotone subcurves at
`t'`, the subcurve defined over `[0, t']` has a serial number 1, and the other, defined over
`[t', 1]` has a serial number 2."*

The ctor calls `_ps.compare_x(_pt, cache)` (so it needs — and possibly refines/exactifies — the
endpoints), sets `_is_vert` when they compare `EQUAL` (and then orders by `y()`, which requires
exact endpoints), and computes `_inc_to_right` by comparing the endpoints' originator parameter
bounds (falling back to exact `parameter()` when the bounds overlap).

### 5.3 Members (verbatim)

```cpp
const Curve_2& supporting_curve() const;      // inline: return _curve;
unsigned int   xid() const;
const Point_2& source() const;                // _ps
const Point_2& target() const;                // _pt
const Point_2& left() const;                  // _dir_right ? _ps : _pt
const Point_2& right() const;                 // _dir_right ? _pt : _ps
bool is_vertical() const;
bool is_directed_right() const;

std::pair<double, double> parameter_range() const;

Comparison_result point_position(const Point_2& p, Bezier_cache& cache) const;
Comparison_result compare_to_right(const Self& cv, const Point_2& p, Bezier_cache& cache) const;
Comparison_result compare_to_left (const Self& cv, const Point_2& p, Bezier_cache& cache) const;
bool equals(const Self& cv, Bezier_cache& cache) const;

template <typename OutputIterator>
OutputIterator intersect(const Self& cv, Intersection_map& inter_map,
                         Bezier_cache& cache, OutputIterator oi) const;

void split(const Point_2& p, Self& c1, Self& c2) const;
bool can_merge_with(const Self& cv) const;
Self merge(const Self& cv) const;
Self flip() const;
Self trim(const Point_2& src, const Point_2& tgt) const;
```
Plus `std::ostream& operator<<` printing `<curve> [xid] | <source> --> <target>`.

Preconditions from the doc comments:
* `point_position`: *"`p` is in the x-range of the arc."*
* `compare_to_right` / `compare_to_left`: *"`p` is incident to both subcurves, and both are defined
  to its right [left]."* Return `EQUAL` only in the (not-expected) overlap case.
* `split`: *"`p` lies in the interior of the subcurve (not one of its endpoints."* Enforced as
  `CGAL_precondition(p.get_originator(_curve, _xid) != p.originators_end() || p.is_rational());`
* `merge`: *"The two arcs are mergeable."* — asserts `_curve.is_same(cv._curve)` and `_xid == cv._xid`.

Behavioural notes:

* **`parameter_range()`** returns
  `((to_double(s_org->point_bound().t_min) + to_double(s_org->point_bound().t_max))/2,
    (same for target))` where `s_org = _ps.get_originator(_curve, _xid)`. It is the **source→target**
  order (so `first > second` for a left-directed subcurve). See gotcha 5 for accuracy;
  §10.1 for the exact route.
* **`split`** does *not* verify that `p` is geometrically on the curve; it just copies `*this` twice
  and rebinds one endpoint each, respecting `_dir_right`. For a **vertical** subcurve and a rational
  `p` it first solves `Y(t) = p.y()` over `[0,1]` (`compute_polynomial_roots(poly_y, 0, 1, …)`,
  asserting exactly one root) and attaches `Originator(_curve, _xid, t)` to `p`. Consequence: the two
  halves keep the **same `xid`** and the **same supporting curve handle**.
* **`can_merge_with`** requires the *same handle* (`_curve.is_same`), the *same `xid`*, and a
  handle-identical shared endpoint (`right().is_same(cv.left()) || left().is_same(cv.right())`).
  Overlapping-but-distinct curves are deliberately not mergeable.
* **`flip()`** swaps `_ps`/`_pt` and negates `_dir_right`; **it does not touch `_inc_to_right`**
  (source comment: *"we just swap the source and target of the original subcurve and do not touch the
  supporting Beizer curve"*). Harmless for the arrangement, but do not assume `flip()` twice is a
  no-op at the bit level.
* **`trim(src, tgt)`** is a *raw* rebind of the two endpoints with **no checks at all** — all
  validation lives in `Trim_2::operator()` (which also normalises the direction). Prefer the functor.
* **`equals`** may call `Curve_2::has_same_support` (expensive) and, on success,
  `cache.mark_as_overlapping(id1, id2)`.
* **There is no `bbox()`** on `X_monotone_curve_2` (the only `bbox` in the file is the private
  `Subcurve::bbox` helper). Build one from `left()/right()` bboxes plus `supporting_curve().bbox()`,
  or from `sample()` output.
* **Private, but worth knowing they exist** (they explain the cost model and are the only exact
  routes internally): `_t_range(Bezier_cache&) const → std::pair<Algebraic,Algebraic>`,
  `_is_in_range(...)` (3 overloads), `_get_y(const Rational& x0, Bezier_cache&) const → Algebraic`,
  `_compare_slopes`, `_compare_to_side`, `_clip_control_polygon`,
  `_approximate_intersection_points`, `_intersect`, `_exact_vertical_position`.
  `_t_range` simply does `make_exact` on both endpoints and returns their originators'
  `parameter()`s — reimplementable publicly (§10.1).

---

## 6. `CGAL::_Bezier_cache` (`Traits_2::Bezier_cache`)

```cpp
template <class Nt_traits_> class _Bezier_cache
```

### 6.1 Public typedefs

```cpp
typedef Nt_traits_                      Nt_traits;
typedef typename Nt_traits::Integer     Integer;
typedef typename Nt_traits::Polynomial  Polynomial;
typedef typename Nt_traits::Algebraic   Algebraic;
typedef _Bezier_cache<Nt_traits>        Self;

typedef size_t                                        Curve_id;
typedef std::list<Algebraic>                          Vertical_tangency_list;
typedef typename Vertical_tangency_list::const_iterator Vertical_tangency_iter;

struct Intersection_point {
  Algebraic s;   // parameter for the first curve
  Algebraic t;   // parameter for the second curve
  Algebraic x;   // x-coordinate
  Algebraic y;   // y-coordinate
  Intersection_point (const Algebraic& _s, const Algebraic& _t,
                      const Algebraic& _x, const Algebraic& _y);
};

typedef std::pair<Curve_id, Curve_id>                 Curve_pair;
typedef std::pair<Algebraic, Algebraic>               Parameter_pair;
typedef std::list<Intersection_point>                 Intersection_list;
typedef typename Intersection_list::const_iterator    Intersection_iter;
```

### 6.2 Members

```cpp
_Bezier_cache ();                       // the ONLY ctor
// private, undefined:  _Bezier_cache (const Self&);  Self& operator= (const Self&);

const Vertical_tangency_list&
get_vertical_tangencies (const Curve_id& id,
                         const Polynomial& polyX, const Integer& normX);

const Intersection_list&
get_intersections (const Curve_id& id1,
                   const Polynomial& polyX_1, const Integer& normX_1,
                   const Polynomial& polyY_1, const Integer& normY_1,
                   const Curve_id& id2,
                   const Polynomial& polyX_2, const Integer& normX_2,
                   const Polynomial& polyY_2, const Integer& normY_2,
                   bool& do_ovlp);

void mark_as_overlapping (const Curve_id& id1, const Curve_id& id2);
```

* `get_vertical_tangencies` doc: *"\return A list of parameters `0 < t_1 < … < t_k < 1` such that
  `X'(t_i) = 0`."* (The `normX` argument is ignored in the body.) Returned reference points into the
  cache's own `std::map` → **valid until the cache is destroyed**, stable across further inserts
  (`std::map`/`std::list` node stability).
* `get_intersections` doc precondition: ***"`id1 < id2` (swap their order if this is not the
  case)."*** All callers respect this by comparing `Curve_2::id()`. Output `do_ovlp` is `true` when
  the two curves overlap (then the list is empty).
* **Non-copyable, non-assignable.** Hold by value or `std::unique_ptr`.
* No `clear()`, no size query, no eviction — the cache grows without bound over a session.

---

## 7. `CGAL::Bezier_bounding_rational_traits` and its structs

```cpp
template <typename _Kernel> class Bezier_bounding_rational_traits
```
Default `BoundingTraits_` of `Arr_Bezier_curve_traits_2` is
`Bezier_bounding_rational_traits<RatKernel_>`, so **`Bounding_traits::NT == Rat_kernel::FT ==
Rational`** and all bounds/boxes below carry **exact `CORE::BigRat`** values.

### 7.1 `_Bez_point_bound<Kernel_>`

```cpp
enum Type { RATIONAL_PT, VERTICAL_TANGENCY_PT, INTERSECTION_PT, UNDEFINED };

typedef Kernel_                  Kernel;
typedef typename Kernel::FT      NT;
typedef typename Kernel::Point_2 Point_2;
typedef std::deque<Point_2>      Control_points;

Type            type;
Control_points  ctrl;         // control points whose convex hull contains the point
NT              t_min;        // minimal t value
NT              t_max;        // maximal t value
bool            can_refine;

_Bez_point_bound();                                    // type = UNDEFINED, can_refine = false
_Bez_point_bound (Type _type, const Control_points& _ctrl,
                  const NT& _t_min, const NT& _t_max, bool _can_refine);
```

### 7.2 `_Bez_point_bbox<Kernel_>`

```cpp
typedef typename Kernel::FT NT;   typedef _Bez_point_bbox<Kernel> Self;
NT min_x, max_x, min_y, max_y;

_Bez_point_bbox();                                     // all zero
_Bez_point_bbox (const NT& _min_x, const NT& _max_x, const NT& _min_y, const NT& _max_y);
Self operator+ (const Self& other) const;              // union
void operator+= (const Self& other);
bool overlaps   (const Self& other) const;
bool overlaps_x (const Self& other) const;
```
**Note the ctor argument order is `(min_x, max_x, min_y, max_y)`** — different from
`Point_2::get_bbox`'s `(min_x, min_y, max_x, max_y)` out-parameters.

### 7.3 `Bezier_bounding_rational_traits` public API

```cpp
typedef _Kernel                                   Kernel;
typedef typename Kernel::FT                       NT;
typedef _Bez_point_bound<Kernel>                  Bez_point_bound;
typedef _Bez_point_bbox<Kernel>                   Bez_point_bbox;
typedef typename Bez_point_bound::Control_points  Control_points;   // std::deque<Kernel::Point_2>

struct Vertical_tangency_point { Bez_point_bound bound; Bez_point_bbox bbox;
  Vertical_tangency_point ();
  Vertical_tangency_point (const Bez_point_bound& _bound, const Bez_point_bbox& _bbox); };

struct Intersection_point { Bez_point_bound bound1, bound2; Bez_point_bbox bbox;
  Intersection_point();
  Intersection_point (const Bez_point_bound& _bound1, const Bez_point_bound& _bound2,
                      const Bez_point_bbox& _bbox); };

Bezier_bounding_rational_traits (double bound = 0.00000000000000011);

bool may_have_self_intersections (const Control_points& cp);
template <class OutputIterator>
OutputIterator compute_intersection_points (const Control_points& cp1,
                                            const Control_points& cp2, OutputIterator oi);
bool refine_intersection_point (const Intersection_point& in_pt, Intersection_point& ref_pt);
template <class OutputIterator>
OutputIterator compute_vertical_tangency_points (const Control_points& cp, OutputIterator oi);
bool refine_vertical_tangency_point (const Vertical_tangency_point& in_pt,
                                     Vertical_tangency_point& ref_pt);
bool can_refine (const Control_points& , const NT& t_min, const NT& t_max);
Comparison_result compare_slopes_at_intersection_point (const Bez_point_bound& bound1,
                                                        const Bez_point_bound& bound2);
void construct_bbox (const Control_points& cp, Bez_point_bbox& bez_bbox);   // \pre cp non-empty
```

* **Every member is non-`const`** (it mutates `m_active_nodes` / holds kernel functors) — keep a
  mutable instance.
* Ctor comment: *"Accuracy bound. We cannot determine the order of two values whose absolute
  difference is smaller than this bound."* Default `1.1e-16`; `can_refine` is literally
  `compare(t_max - t_min, m_accuracy_bound) != SMALLER`. Raising it makes the filtered path bail
  out to the exact path earlier.
* `compute_vertical_tangency_points` output value type is `Vertical_tangency_point`, **sorted by
  `bound.t_min` ascending**.
* `compute_intersection_points` output value type is `Intersection_point`.
* `may_have_self_intersections` returns `false` (⇒ certainly none) when the control polygon is
  convex, or when its angular span is < 180°.

---

## 8. `CGAL::CORE_algebraic_number_traits`

Non-template class, all members `const`, stateless (default-constructible; the code often just does
`Nt_traits nt_traits;` locally).

```cpp
typedef CORE::BigInt              Integer;
typedef CORE::BigRat              Rational;
typedef CORE::Polynomial<Integer> Polynomial;
typedef CORE::Expr                Algebraic;
```

```cpp
Integer numerator   (const Rational& q) const;
Integer denominator (const Rational& q) const;
Algebraic convert (const Integer& z) const;
Algebraic convert (const Rational& q) const;
Rational rational_in_interval (const Algebraic& x1, const Algebraic& x2) const;  // \pre x1 != x2
std::pair<double, double> double_interval (const Algebraic& x) const;
template <class InputIterator, class OutputIterator>
OutputIterator convert_coefficients (InputIterator q_begin, InputIterator q_end,
                                     OutputIterator zoi) const;
Algebraic sqrt (const Algebraic& x) const;                                       // \pre x >= 0
template <class NT, class OutputIterator>
OutputIterator solve_quadratic_equation (const NT& a, const NT& b, const NT& c,
                                         OutputIterator oi) const;
Polynomial construct_polynomial (const Integer *coeffs, unsigned int degree) const;
bool construct_polynomial (const Rational *coeffs, unsigned int degree,
                           Polynomial& poly, Integer& poly_denom) const;
bool construct_polynomials (const Rational *p_coeffs, unsigned int p_degree,
                            const Rational *q_coeffs, unsigned int q_degree,
                            Polynomial& p_poly, Polynomial& q_poly) const;
int      degree (const Polynomial& poly) const;                   // poly.getTrueDegree()
Integer  get_coefficient (const Polynomial& poly, unsigned int i) const;   // 0 when i > degree
template <class NT> NT evaluate_at (const Polynomial& poly, NT& x) const;
Polynomial derive (const Polynomial& poly) const;
Polynomial scale  (const Polynomial& poly, const Integer& a) const;
Polynomial divide (const Polynomial& polyA, const Polynomial& polyB, Polynomial& rem) const;
template <class OutputIterator>
OutputIterator compute_polynomial_roots (const Polynomial& poly, OutputIterator oi) const;
template <class OutputIterator>
OutputIterator compute_polynomial_roots (const Polynomial& poly, double x_min, double x_max,
                                         OutputIterator oi) const;
```

* **There is no `solve()` member.** The solvers are `solve_quadratic_equation` and the two
  `compute_polynomial_roots` overloads. Both root finders require the output value type to be
  `Algebraic` and return roots **sorted ascending**; degree ≤ 2 is special-cased through
  `solve_quadratic_equation`, degree ≥ 3 uses `CORE::Sturm<Integer>` + `rootOf`.
  `degree <= 0` ⇒ nothing is written.
* `evaluate_at`'s second parameter is `NT&` (non-const). Template deduction makes `NT` deduce to
  `const Rational` / `const Algebraic` when you pass a const lvalue, so `nt_traits.evaluate_at(poly,
  t)` compiles for `const Rational& t` but **not for an rvalue temporary** — bind it to a named
  variable first.
* `double_interval(x)` calls `CORE::Expr::doubleInterval(lo, hi)` and returns `{lo, hi}` — the
  cheapest safe way to get a guaranteed enclosure of an algebraic coordinate.
* `rational_in_interval(x1, x2)` finds an exact `Rational` strictly between two algebraics by
  repeated doubling — this is how the traits gets rational sample points between exact parameters.
* `convert_coefficients` doc: `a(i) = n(i)·lcm{d(1..k)} / (d(i)·gcd{n(1..k)})`; value type of `zoi`
  must be `Integer`.

---

## 9. `CGAL/Arr_geometry_traits/de_Casteljau_2.h` — free functions

```cpp
template <class InputIterator, class OutputIterLeft, class OutputIterRight>
void bisect_control_polygon_2(InputIterator ctrl_pts_begin, InputIterator ctrl_pts_end,
                              OutputIterLeft left_ctrl_pts, OutputIterRight right_ctrl_pts);

template <class InputIterator>
typename InputIterator::value_type
point_on_Bezier_curve_2(InputIterator ctrl_pts_begin, InputIterator ctrl_pts_end,
                        const typename Kernel_traits<typename InputIterator::value_type>::Kernel::FT& t0);

template <class InputIterator, class OutputIterLeft, class OutputIterRight>
typename InputIterator::value_type
de_Casteljau_2(InputIterator ctrl_pts_begin, InputIterator ctrl_pts_end,
               const typename Kernel_traits<typename InputIterator::value_type>::Kernel::FT& t0,
               OutputIterLeft left_ctrl_pts, OutputIterRight right_ctrl_pts);
```
* All require `InputIterator::value_type` to be a nested typedef → **raw pointers do not work**;
  pass container iterators.
* Usage note from the header: *"Typically you should call this function as follows:
  `bisect_control_polygon_2(ctrl_pts.begin(), ctrl_pts.end(), std::back_inserter(left_pts),
  std::front_inserter(right_pts));`"* — the right polygon is emitted **in reverse**, hence
  `front_inserter`.
* `CGAL_precondition(n_pts != 0)`. `t0`'s type is the kernel's `FT` (so `Rational` for a rational
  control polygon, `double` for a `Simple_cartesian<double>` one).
* These are useful directly for **rendering an arbitrary rational sub-span** of a curve without going
  through `sample()`'s double-only path.

---

## 10. Recipes for the C++ core / Cython layer

### 10.1 Exact algebraic parameter range of an x-monotone subcurve (the public `_t_range`)

```cpp
// cache: your own long-lived Traits_2::Bezier_cache
std::pair<NT_alg, NT_alg> exact_t_range(const Xcv& cv, Bezier_cache& cache) {
  const Point_2& s = cv.source();
  const Point_2& t = cv.target();
  auto so = s.get_originator(cv.supporting_curve(), cv.xid());
  auto to = t.get_originator(cv.supporting_curve(), cv.xid());
  CGAL_assertion(so != s.originators_end() && to != t.originators_end());
  if (!so->has_parameter()) s.make_exact(cache);   // fills the originator's p_t
  if (!to->has_parameter()) t.make_exact(cache);
  return { so->parameter(), to->parameter() };     // Algebraic, source-then-target order
}
```
Iterators stay valid across `make_exact` (`std::list`), and `make_exact` writes the parameter into
the *existing* originator. **[measured]** for the S-cubic this yields exactly
`{0, 0.276393}`, `{0.276393, 0.723607}`, `{0.723607, 1}` for `xid` 1, 2, 3.

### 10.2 Sampling an arrangement **edge** for rendering

```cpp
std::vector<std::pair<double,double>> sample_edge(const Xcv& cv, unsigned n, Bezier_cache& cache) {
  // 1. make both endpoints exact so parameter_range() is trustworthy
  if (!cv.source().is_exact()) cv.source().make_exact(cache);
  if (!cv.target().is_exact()) cv.target().make_exact(cache);
  std::pair<double,double> tr = cv.parameter_range();          // (t_src, t_trg), may be decreasing
  std::vector<std::pair<double,double>> out;
  cv.supporting_curve().sample(tr.first, tr.second, n, std::back_inserter(out));
  return out;   // exactly max(n,2) points, endpoints inclusive, in source->target order
}
```
* `sample()` accepts `t_start > t_end` (the step is negative) — the output then runs
  source→target, matching the halfedge direction. For a *left-to-right* polyline use
  `cv.is_directed_right()` or reverse the vector.
* The endpoints of the returned polyline are `to_double`-evaluations of the **approximated**
  control polygon at the (approximate) `t`, so they will not be bit-identical to
  `cv.left().approximate()`. For pixel-exact joins, overwrite the first/last sample with
  `cv.source().approximate()` / `cv.target().approximate()`.
* For a *higher-fidelity* span, use the exact algebraic range from §10.1 and
  `CGAL::to_double` on it — same call, better `t` bounds.
* A fully exact alternative (slower): pick rational `t_i` via
  `nt_traits.rational_in_interval(t_lo, t_hi)` and evaluate `B(t_i)` (`Rat_point_2`, exact) — or use
  `de_Casteljau_2` on the rational control polygon to clip the sub-span.

### 10.3 Safe extraction of coordinates from a `Point_2`

```cpp
// (a) always-safe double
std::pair<double,double> xy = p.approximate();

// (b) exact rational, when available
if (p.is_rational()) { Rat_point_2 rp = (Rat_point_2) p; /* rp.x(), rp.y() exact BigRat */ }

// (c) exact algebraic (CORE::Expr)
if (!p.is_exact()) p.make_exact(cache);       // MUST come first, incl. in release builds
const NT_alg& X = p.x();  const NT_alg& Y = p.y();

// (d) guaranteed double enclosure of an algebraic coordinate
Nt_traits nt;  std::pair<double,double> xlo_hi = nt.double_interval(X);

// (e) exact rational bounding box (always available, no cache needed)
NT_rat bx0, by0, bx1, by1;  p.get_bbox(bx0, by0, bx1, by1);   // (min_x, min_y, max_x, max_y)
```
`make_exact` does **not** make an algebraic point rational (gotcha in §4.3). To get an exact rational
*near* an algebraic point, use `nt.rational_in_interval(X - eps, X + eps)` or the bbox corners.

### 10.4 Constructing a point ON a curve at a parameter

```cpp
Point_2 p_rat(B, NT_rat(1,3));            // exact + rational, originator xid = 0
Point_2 p_rat_x(B, xid, NT_rat(1,3));     // same, tagged with the x-monotone subcurve id
Point_2 p_alg(B, xid, t_algebraic);       // exact, not rational, originator carries t
```
Always prefer the **`xid`-carrying** overloads when the point is meant to lie on a specific
x-monotone subcurve (e.g. before `Split_2`): `X_monotone_curve_2`'s ctor and `split` look the
originator up with `get_originator(curve, xid)`.
Precondition on all four: `0 <= t0 <= 1`.
**[measured]** `cv.point_position(Point_2(B, cv.xid(), t_mid), cache) == EQUAL`.

### 10.5 Evaluating at an algebraic parameter

```cpp
Alg_point_2 q = B(t_alg);                  // Curve_2::operator()(const Algebraic&)
double qx = CGAL::to_double(q.x());
```
Under the hood: `evaluate_at(x_polynomial(), t) / convert(x_norm())`, with `t == 0` / `t == 1`
shortcutting to the first/last control point. This forces construction of the lazy polynomials.

### 10.6 Cache management pattern for a type-erased core

```cpp
struct BezierCore {
  Traits_2                traits;      // the ONE owner; never copy it
  Traits_2::Bezier_cache  cache;       // your own cache for direct calls (non-copyable)
  std::vector<Curve_2>    curves;      // keep every curve alive: ids are rep pointers
  Arrangement_2           arr{&traits};// arrangement does NOT own the traits
  BezierCore(const BezierCore&) = delete;             // traits copy would alias p_cache
  BezierCore& operator=(const BezierCore&) = delete;
};
```
Destruction order inside the struct is reverse-declaration, i.e. `arr` dies before `curves`, before
`cache`, before `traits` — which is the order you want. If you reorder members, make sure `traits`
outlives `arr`.

### 10.7 The `std::variant` shapes you must declare yourself

```cpp
using MakeXMonotoneResult = std::variant<Traits_2::Point_2, Traits_2::X_monotone_curve_2>;
using IntersectionPoint   = std::pair<Traits_2::Point_2, Traits_2::Multiplicity>; // Multiplicity = unsigned int
using IntersectResult     = std::variant<IntersectionPoint, Traits_2::X_monotone_curve_2>;
```

---

## 11. Measured reference numbers (for sanity-checking a port)

Curve `B` = cubic with rational control points `(0,0) (4,1) (-2,2) (2,3)`;
curve `B2` = quadratic `(0,3) (2,-1) (3,4)`.

```
n_ctrl=4  has_no_self_intersections=1  bbox = -2 0 4 3
make_x_monotone(B) -> 3 variants, all X_monotone_curve_2, 0 Point_2
  xid=1  vertical=0  directed_right=1  exact t = [0,        0.276393]
  xid=2  vertical=0  directed_right=0  exact t = [0.276393, 0.723607]
  xid=3  vertical=0  directed_right=1  exact t = [0.723607, 1       ]
  parameter_range() BEFORE make_exact: [0,0.3125] [0.276393,0.625] [0.723607,1]
B.get_t_at_x(1) -> 3 roots: 0.112702, 0.5, 0.887298
Intersect_2 over all subcurve pairs of B and B2 -> 3 points, all exact, 2 originators, mult=0
Arrangement_2 with {B, B2}: V=9  E=10  F=3
  of the 9 vertices, 2 had is_exact()==false and 4 had is_rational()==true
Point_2 p(B, Rational(1,3)) : is_rational()==true, exact bbox = [38/27,38/27] x [1,1]
```
