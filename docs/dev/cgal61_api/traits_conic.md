# CGAL 6.1 — `Arr_conic_traits_2` (conic traits) — signature-level API map

Source of truth (read verbatim for this document, CGAL 6.1, `/opt/homebrew/include/CGAL/version.h` →
`CGAL_VERSION 6.1`, `CGAL_VERSION_NR 1060101000`):

| file | lines |
|---|---|
| `/opt/homebrew/include/CGAL/Arr_conic_traits_2.h` | 4292 |
| `/opt/homebrew/include/CGAL/Arr_geometry_traits/Conic_arc_2.h` | 1726 |
| `/opt/homebrew/include/CGAL/Arr_geometry_traits/Conic_x_monotone_arc_2.h` | 1381 |
| `/opt/homebrew/include/CGAL/Arr_geometry_traits/Conic_point_2.h` | 117 |
| `/opt/homebrew/include/CGAL/Arr_geometry_traits/Conic_intersections_2.h` | 218 |
| `/opt/homebrew/include/CGAL/Cartesian/ConicCPA2.h` (orientation convention) | — |
| `/opt/homebrew/include/CGAL/CORE_algebraic_number_traits.h` (`Nt_traits`) | — |

Everything below was compiled and run against the installed headers
(`clang++ -std=c++17 -DCGAL_USE_CORE -DCGAL_USE_GMP -DCGAL_USE_MPFR -I/opt/homebrew/include -lgmp -lmpfr`).
Facts marked **[verified]** were checked by executing a program.

---

## Gotchas / surprises vs. older CGAL

1. **The whole geometric-construction API moved out of `Conic_arc_2` into the traits.**
   Every `Conic_arc_2` constructor except the default and the copy constructor is
   `CGAL_DEPRECATED`, and *all* of the geometry helpers (`bbox()`, `vertical_tangency_points`,
   `_set`, `_set_full`, `_is_between_endpoints`, …) are `CGAL_DEPRECATED` too. Curves must be
   built through `traits.construct_curve_2_object()` / `construct_x_monotone_curve_2_object()`,
   which are the only things that get you the traits' shared kernels. The traits is now
   *stateful* (`std::shared_ptr<Rat_kernel/Alg_kernel/Nt_traits>` + a `mutable` intersection
   cache), so a type-erased core must own one traits instance and hand out functors from it.

2. **`Construct_curve_2` silently negates your `(r,s,t,u,v,w)`.** The stored integer
   coefficients are the input coefficients scaled to integers (`Nt_traits::convert_coefficients`,
   i.e. `n(i)·lcm{d} / (d(i)·gcd{n})`) and then **multiplied by −1 whenever the natural
   `Rat_kernel::Conic_2::orientation()` of the input differs from the `Orientation` you asked for.**
   For a non-empty ellipse `ConicCPA2::analyse()` gives `orientation() == NEGATIVE` iff `r > 0`, so:
   *ellipse + `COUNTERCLOCKWISE` ⇒ stored `r() < 0`; ellipse + `CLOCKWISE` ⇒ stored `r() > 0`.*
   **[verified]** input `(1,4,0,0,0,−4)` + `COUNTERCLOCKWISE` → `r,s,t,u,v,w = −1,−4,0,0,0,4`.
   Never assume `arc.r()…arc.w()` equal what you passed in; if you need round-tripping, store
   your own coefficients alongside the handle.

3. **Invalid arcs are returned silently, not thrown/asserted.** "Endpoints lie on the conic" is
   *not* a `CGAL_precondition`; `Traits::set()` calls `reset_flags()` and you get a `Curve_2`
   with `is_valid() == false`. Only "source ≠ target" (and, for the full-conic overload,
   `4rs − t² > 0`) are real preconditions. **A binding must check `cv.is_valid()` after every
   `Construct_curve_2` call** and refuse to pass invalid curves on — `make_x_monotone_2` happily
   consumes an invalid curve and produces garbage. **[verified]**

4. **Hyperbolic supporting conics are buggy in 6.1 — but the failure is exactly decidable.**
   `Arr_conic_traits_2::build_hyperbolic_arc_data()` (line 3316) assigns the two half-angle
   formulae the wrong way round (`sin_phi = sqrt((1+cos2φ)/2)`, `cos_phi = ±sqrt((1−cos2φ)/2)`,
   contradicting its own comment and its own `t == 0` branch). The swap is the reflection
   `φ ↦ π/2 − φ`, so CGAL stores the principal axis **of the conic with `x` and `y` interchanged**;
   the line still passes through the correct centre, so it is either still a separating line or a
   *chord* that cuts each branch once. Chord ⇒ debug builds die on
   `CGAL_assertion(side != ZERO)` (line 3400) or
   `CGAL_assertion(side == cv.sign_of_extra_data(target…))` (line 3403); `NDEBUG` builds either
   mark the arc invalid or keep it and **silently mis-answer point containment, drop or invent
   intersections, and corrupt the arrangement while `arr.is_valid()` still returns `true`**.
   Which one happens is not luck — it is a rational sign test on `(r,s,t,u,v,w)` alone
   (§13.3/13.4): with `N = (4rs−t²)w − su² − rv² + tuv`, `(R,S,T) = sign(N)·(r,s,t)`,
   `P = R+S`, `E = T² − (R−S)²`, the stored line is a chord **iff `T ≠ 0` and
   `P·√((R−S)²+T²) > E`**; geometrically iff `dist(2θ mod π, 0) < γ` (θ = transverse-axis angle,
   γ = asymptote half-angle), which hits a fraction `2γ/π` of rotations — exactly 1/2 for every
   rectangular hyperbola, which is what "2 of 4 random Bézier hyperbolas" was measuring.
   **[verified]** 82 systematic cases (rotated `x·y=k`, rotated `x²/a²−y²/b²=1`, rational
   quadratic Béziers with `w1² > w0·w2`, plus translated/negated/rescaled variants): 49 SAFE,
   33 UNSAFE, **0 disagreements** between the predicate and an exact measurement of the stored
   line, in both a debug and an `-O2 -DNDEBUG` build.
   *Practical rule: run the predicate first; if it says UNSAFE, either build the arc yourself with
   the correct axis (§13.8(g) — works with all assertions on) or rotate the whole scene by a
   rational rotation that makes it SAFE (§13.8(e)). §13 has the full diagnosis, the reference
   implementation, and both workarounds verified end-to-end.*

5. **`Approximate_curve_length_2` is unreachable and would not compile.** The class exists
   (line 1530) but there is **no `approximate_curve_length_2_object()` anywhere in the CGAL 6.1
   headers** **[verified: compile error "no member named …"; `grep -r approximate_curve_length`
   over `/opt/homebrew/include/CGAL/` returns nothing]**, and its constructor is `protected`
   with only `Arr_conic_traits_2` as friend. Even if reachable, `operator() const` calls the
   non-`const` privates `segment_length/parabola_length/ellipse_length/hyperbola_length`, so it
   cannot instantiate. (`segment_length` also has an arithmetic bug: it computes `dx,dy` and then
   returns `sqrt(x2*x2 + y2*y2)`.) Do not expose curve length; compute it yourself from
   `Approximate_2`'s polyline, or from `traits.approximate_ellipse(...)` + Boost `ellint_2`.

6. **Two public `Conic_x_monotone_arc_2` members do not compile if instantiated.** **[verified]**
   * `Conic_x_monotone_arc_2(const Point_2& source, const Point_2& target)` (deprecated, line 124)
     references `this->extra_data` (the member is `m_extra_data`; `extra_data` is a function) →
     *"reference to non-static member function must be called"*. Use
     `traits.construct_x_monotone_curve_2_object()(source, target)` instead.
   * `void merge(const Self& xcv2) const` (line 1037) calls non-const `set_source/set_target`
     through a `const` alias → *"'this' argument … has type 'const …' but function is not marked const"*.
     Use `traits.merge_2_object()`.

7. **`Rat_kernel` must be `CGAL::Cartesian<Rational>`.** `Traits::set()`, `set_full()` and the
   five-point constructor instantiate `typename Rat_kernel::Conic_2`, which is only typedef'd in
   `CGAL/Cartesian/Cartesian_base.h` (`ConicCPA2<Point_2, Data_accessor_2>`). `Simple_cartesian`
   has no `Conic_2` and will not compile.

8. **CORE number types are Boost.Multiprecision now.** With
   `CGAL::CORE_algebraic_number_traits`: `Integer = CORE::BigInt = boost::multiprecision::mpz_int`,
   `Rational = CORE::BigRat = boost::multiprecision::mpq_rational`, `Algebraic = CORE::Expr`,
   `Polynomial = CORE::Polynomial<Integer>`. Your Cython layer marshals GMP mpz/mpq, not CORE's
   old hand-rolled classes.

9. **`std::variant` output, not `CGAL::Object`.** `Make_x_monotone_2` writes
   `std::variant<Point_2, X_monotone_curve_2>`; `Intersect_2` writes
   `std::variant<std::pair<Point_2, Multiplicity>, X_monotone_curve_2>` where
   `Multiplicity = std::size_t`. The functors are templated on `OutputIterator` and simply do
   `*oi++ = <the underlying type>`, so the value type only has to be *constructible from* both.

10. **`Point_2` is mutated during const predicates.** `Traits::contains_point()` does
    `const_cast<Point_2&>(p).set_generating_conic(xcv.id())`, appending to a `std::list<Conic_id>`
    inside the point. Predicate evaluation is therefore **not thread-safe** on shared points, and
    `Point_2` is not a trivially-copyable value. The traits' `mutable Intersection_map m_inter_map`
    is likewise shared, unbounded, and unsynchronised — one traits object per worker thread.

11. **Conic ids come from a process-global counter.** `static size_t Arr_conic_traits_2::get_index()`
    uses a function-local `static std::atomic<size_t>`, so ids are unique across *all* traits
    instances with the same template arguments. Each `Make_x_monotone_2::operator()` call burns one
    id, so x-monotone pieces of the *same* `Curve_2` obtained from two different calls have
    different ids and fall back to the coefficient comparison in `has_same_supporting_conic`.

12. **No named ellipse/parabola/hyperbola constructors.** `Construct_curve_2` has exactly 10
    overloads (listed below); there is nothing like `construct_ellipse(...)`. Rotated ellipses and
    rational Béziers must be reduced to `(r,s,t,u,v,w) + Orientation + source + target` by hand
    (formulas in the last section).

13. `Conic_arc_2::bbox()` (deprecated) is **undefined behaviour for full conics**: it passes a
    `Point_2 tan_ps[2]` array through an `Alg_point_2*` parameter and indexes it with the base
    class's stride (`Point_2` adds a `std::list`, so the sizes differ). Use
    `traits.construct_bbox_2_object()`, whose implementation uses `Alg_point_2 tan_ps[2]` correctly.

14. Side categories are all `Arr_oblivious_side_tag` ⇒ this traits gives a **bounded planar**
    arrangement. `Parameter_space_in_x_2/Parameter_space_in_y_2` exist but their
    `(X_monotone_curve_2, Arr_curve_end)` overloads are `CGAL_error_msg("Not implemented yet!")`;
    the bounded topology traits never calls them.

---

## 1. `CGAL::Arr_conic_traits_2<RatKernel, AlgKernel, NtTraits>`

```cpp
template <typename RatKernel, typename AlgKernel, typename NtTraits>
class Arr_conic_traits_2;
```

No defaults for any template parameter. Canonical instantiation:

```cpp
typedef CGAL::CORE_algebraic_number_traits            Nt_traits;
typedef Nt_traits::Rational                           Rational;   // mpq_rational
typedef Nt_traits::Algebraic                          Algebraic;  // CORE::Expr
typedef CGAL::Cartesian<Rational>                     Rat_kernel; // MUST have Conic_2
typedef CGAL::Cartesian<Algebraic>                    Alg_kernel;
typedef CGAL::Arr_conic_traits_2<Rat_kernel, Alg_kernel, Nt_traits> Traits;
```

Requirements documented in the header comment: `Rat_kernel::FT` integral or rational;
`Alg_kernel::FT` holds algebraic numbers of degree ≤ 4 (preferably `CORE::Expr`);
`Nt_traits` performs conversions between integer/rational/algebraic.

### 1.1 Public typedefs (verbatim, lines 62–92)

```cpp
typedef RatKernel                       Rat_kernel;
typedef AlgKernel                       Alg_kernel;
typedef NtTraits                        Nt_traits;

typedef typename Rat_kernel::FT         Rational;
typedef typename Rat_kernel::Point_2    Rat_point_2;
typedef typename Rat_kernel::Segment_2  Rat_segment_2;
typedef typename Rat_kernel::Line_2     Rat_line_2;      // declared, never used by any functor
typedef typename Rat_kernel::Circle_2   Rat_circle_2;

typedef typename Alg_kernel::FT         Algebraic;
typedef typename Alg_kernel::Point_2    Alg_point_2;

typedef typename Nt_traits::Integer     Integer;

// Category tags:
typedef Tag_true                        Has_left_category;
typedef Tag_true                        Has_merge_category;
typedef Tag_false                       Has_do_intersect_category;

typedef Arr_oblivious_side_tag          Left_side_category;
typedef Arr_oblivious_side_tag          Bottom_side_category;
typedef Arr_oblivious_side_tag          Top_side_category;
typedef Arr_oblivious_side_tag          Right_side_category;

// Traits objects:
typedef Conic_arc_2<Rat_kernel, Alg_kernel, Nt_traits>  Curve_2;
typedef Conic_x_monotone_arc_2<Curve_2>                 X_monotone_curve_2;
typedef Conic_point_2<Alg_kernel>                       Point_2;
typedef size_t                                          Multiplicity;
```

Approximation typedefs (lines 1526–1528):

```cpp
typedef double                                        Approximate_number_type;
typedef CGAL::Cartesian<Approximate_number_type>      Approximate_kernel;
typedef Approximate_kernel::Point_2                   Approximate_point_2;
```

Private, but they define the shapes you must produce for the output iterators (lines 96–118):

```cpp
using Conic_id   = typename Point_2::Conic_id;
using Conic_pair = std::pair<Conic_id, Conic_id>;
typedef std::pair<Point_2, Multiplicity>          Intersection_point;
typedef std::list<Intersection_point>             Intersection_list;
typedef std::map<Conic_pair, Intersection_list, Less_conic_pair> Intersection_map;
typedef std::shared_ptr<Rat_kernel>               Shared_rat_kernel;
typedef std::shared_ptr<Alg_kernel>               Shared_alg_kernel;
typedef std::shared_ptr<Nt_traits>                Shared_nt_traits;
```

### 1.2 Data members, construction, copy semantics

```cpp
const Shared_rat_kernel m_rat_kernel;
const Shared_alg_kernel m_alg_kernel;
const Shared_nt_traits  m_nt_traits;
mutable Intersection_map m_inter_map;   // conic-pair -> cached intersection points
```

```cpp
Arr_conic_traits_2();                                       // makes fresh shared kernels
Arr_conic_traits_2(Shared_rat_kernel rat_kernel,
                   Shared_alg_kernel alg_kernel,
                   Shared_nt_traits  nt_traits);
Shared_rat_kernel rat_kernel() const;                       // returns the shared_ptr by value
Shared_alg_kernel alg_kernel() const;
Shared_nt_traits  nt_traits()  const;
static size_t get_index();                                  // ++ a function-local atomic<size_t>
```

**Copy semantics [verified]:** `std::is_copy_constructible<Traits>::value == true` (the shared_ptrs
are copied — the copy *shares* the kernels but gets its own empty-then-diverging cache copy),
`std::is_copy_assignable<Traits>::value == false` (const members), default constructible.
`sizeof(Traits) == 72` on arm64 macOS.

**Yes, it holds a cache.** `m_inter_map` maps ordered pairs of `Conic_id` to the full list of
intersection points of the two *supporting conics*, and it is populated by `Intersect_2` and never
cleared. It grows with the number of distinct supporting-conic pairs actually intersected. For a
long-lived type-erased core: (a) one traits per arrangement/worker, (b) drop and rebuild the traits
if you need to reclaim the memory, (c) never share a traits across threads.

**Lifetime:** `CGAL::Arrangement_2<Traits>` stores a *pointer* to the traits when you construct it
as `Arrangement_2 arr(&traits)`; the traits must outlive the arrangement. Functor objects
(`Compare_x_2`, `Intersect_2`, …) hold `const Traits& m_traits`; they must not outlive the traits
either. All functor constructors are `protected`/private with
`friend class Arr_conic_traits_2<…>` — obtain them **only** through the `*_object()` accessors.

### 1.3 Functor accessor list (complete)

```cpp
Compare_x_2                  compare_x_2_object()                  const;
Compare_xy_2                 compare_xy_2_object()                 const;
Construct_min_vertex_2       construct_min_vertex_2_object()       const;
Construct_max_vertex_2       construct_max_vertex_2_object()       const;
Is_vertical_2                is_vertical_2_object()                const;
Compare_y_at_x_2             compare_y_at_x_2_object()             const;
Compare_y_at_x_left_2        compare_y_at_x_left_2_object()        const;
Compare_y_at_x_right_2       compare_y_at_x_right_2_object()       const;
Equal_2                      equal_2_object()                      const;
Parameter_space_in_x_2       parameter_space_in_x_2_object()       const;
Parameter_space_in_y_2       parameter_space_in_y_2_object()       const;
Make_x_monotone_2            make_x_monotone_2_object()            const;
Split_2                      split_2_object()                      const;
Intersect_2                  intersect_2_object()                  const;
Are_mergeable_2              are_mergeable_2_object()              const;
Merge_2                      merge_2_object()                      const;
Approximate_2                approximate_2_object()                const;
Construct_x_monotone_curve_2 construct_x_monotone_curve_2_object() const;
Construct_curve_2            construct_curve_2_object()            const;
Compare_endpoints_xy_2       compare_endpoints_xy_2_object()       const;
Construct_opposite_2         construct_opposite_2_object()         const;
Trim_2                       trim_2_object()                       const;
Construct_bbox_2             construct_bbox_2_object()             const;
// NOTE: there is NO approximate_curve_length_2_object().
```

---

## 2. `Construct_curve_2` — every `operator()` overload

Obtained by `auto ctr = traits.construct_curve_2_object();`. All overloads are `const`, return
`Curve_2` **by value**, and never throw (they mark the arc invalid instead — see gotcha 3).

### 2.1 Empty curve
```cpp
Curve_2 operator()() const;
```
Returns a default `Conic_arc_2`: all coefficients 0, `m_orient = COLLINEAR`, `m_info = 0`
⇒ `is_valid() == false`. **[verified]**

### 2.2 Full conic from 6 coefficients — *ellipses only*
```cpp
Curve_2 operator()(const Rational& r, const Rational& s, const Rational& t,
                   const Rational& u, const Rational& v, const Rational& w) const;
```
Doc comment: *"constructs a conic arc which is the full conic: `C: r*x^2 + s*y^2 + t*xy + u*x + v*y + w = 0`.
`\pre` The conic C must be an ellipse (so `4rs - t^2 > 0`)."*
Implementation: `CGAL_precondition(CGAL::sign(4*r*s - t*t) == POSITIVE);` then
`m_traits.set_full(arc, rat_coeffs, /*comp_orient=*/true)` — i.e. the **orientation is computed**
from `Rat_kernel::Conic_2::orientation()` and the coefficients are *not* negated.
Result has `is_full_conic() == true`; `source()`/`target()` must **not** be called
(their `CGAL_precondition(! is_full_conic())` fires). `set_full` additionally
`CGAL_assertion(is_ellipse)` and, if that fails, `reset_flags()`.
**[verified]** `(1,4,0,0,0,−4)` → `is_valid()==1, is_full_conic()==1`;
a rotated ellipse → `orientation() == −1` (`CLOCKWISE`), and `make_x_monotone_2` yields **2** pieces.

### 2.3 Conic arc from 6 coefficients + orientation + endpoints  ← *the workhorse*
```cpp
Curve_2 operator()(const Rational& r, const Rational& s, const Rational& t,
                   const Rational& u, const Rational& v, const Rational& w,
                   Orientation orient,
                   const Point_2& source, const Point_2& target) const;
```
Doc comment: *"`\param orient` The orientation of the arc (clockwise or counterclockwise).
`\pre` The source and the target must be on the conic boundary and must not be the same."*
Only `compare_xy_2(source, target) != EQUAL` is an actual `CGAL_precondition`. Then
`arc.set_orientation(orient); arc.set_endpoints(source, target); m_traits.set(arc, rat_coeffs);`
`Traits::set` (line 3049):
* converts to integers via `Nt_traits::convert_coefficients`;
* **negates all six** iff `arc.orientation() != Rat_kernel::Conic_2(r,s,t,u,v,w).orientation()`;
* if either endpoint fails `r x² + s y² + t x y + u x + v y + w == 0` ⇒ `reset_flags()`, **invalid**;
* if `orient == COLLINEAR` on a degree-2 conic, verifies the chord midpoint also satisfies the
  equation (line-pair segment) and stores the supporting line in `Extra_data` with `side = ZERO`;
* if `4rs − t² < 0` calls `build_hyperbolic_arc_data()` (see gotcha 4);
* if `4rs − t² ≤ 0`, verifies the arc is finite by asking for `points_at_x`/`points_at_y` of the
  chord midpoint, else `reset_flags()`;
* finally `set_flag(IS_VALID); reset_flag(IS_FULL_CONIC);`.

`orient` may legally be `COLLINEAR` (line-pair segment); for a genuine conic use
`COUNTERCLOCKWISE` / `CLOCKWISE`.

### 2.4 Circular arc from three rational points
```cpp
Curve_2 operator()(const Rat_point_2& p1, const Rat_point_2& p2,
                   const Rat_point_2& p3) const;
```
Doc: *"`p1` The arc source. `p2` A point in the interior of the arc. `p3` The arc target.
`\pre` The three points must not be collinear."* Collinearity is **not** asserted — it produces an
invalid arc (`D == 0 ⇒ reset_flags()`). Supporting circle stored as
`(D², D², 0, −2·D·Nx, −2·D·Ny, Nx²+Ny² − …)`; orientation = `COUNTERCLOCKWISE` iff
`orientation(source, p2, target) == LEFT_TURN`. **[verified]** `(−1,0),(0,1),(1,0)` → valid.

### 2.5 Conic through five rational points
```cpp
Curve_2 operator()(const Rat_point_2& p1, const Rat_point_2& p2,
                   const Rat_point_2& p3, const Rat_point_2& p4,
                   const Rat_point_2& p5) const;
```
Doc: *"`p1` The source point of the given arc. `p2,p3,p4` Points lying on the conic arc, between
p1 and p5. `p5` The target point. `\pre` No three points are collinear."*
Uses `Rat_kernel::Conic_2::set(p1..p5)` for the coefficients. Any collinear triple ⇒
`reset_flags()` (invalid, not asserted). Orientation from `orientation(p1,p2,p5)`; there **are**
real `CGAL_precondition`s that `orientation(p1,p3,p5)` and `orientation(p1,p4,p5)` agree with it.
Finally each of `p2,p3,p4` must be `is_strictly_between_endpoints`, else invalid.
**[verified]** 5 points on `x²+4y²−4=0` (`(2,0),(8/5,3/5),(6/5,4/5),(0,1),(−6/5,4/5)`) → valid,
orientation `COUNTERCLOCKWISE`, stored coefficients `(−1,−4,0,0,0,4)`.

### 2.6 Coefficients + orientation + approximate endpoints defined by two auxiliary conics
```cpp
Curve_2 operator()(const Rational& r,   const Rational& s,   const Rational& t,
                   const Rational& u,   const Rational& v,   const Rational& w,
                   Orientation orient,
                   const Point_2& app_source,
                   const Rational& r_1, const Rational& s_1,
                   const Rational& t_1, const Rational& u_1,
                   const Rational& v_1, const Rational& w_1,
                   const Point_2& app_target,
                   const Rational& r_2, const Rational& s_2,
                   const Rational& t_2, const Rational& u_2,
                   const Rational& v_2, const Rational& w_2) const;
```
Doc: *"The source and the target are specified by the intersection of the conic with `C_1` and `C_2`.
The user must also specify the source and the target with approximated coordinates. The actual
intersection points that best fits the source (or the target) will be selected."*
Mechanics: `compute_resultant_roots` (from `Conic_intersections_2.h`) is called twice per auxiliary
conic — once for x, once for the y-swapped system — giving up to 4 candidate x's and 4 y's; every
`(x_i, y_j)` pair is exactly re-substituted into **both** conics, and the exact root nearest (in
`to_double` Euclidean distance) to `app_source` / `app_target` wins. If no candidate is found for
either endpoint, or if the two chosen endpoints compare `EQUAL`, ⇒ `reset_flags()` (invalid).
Degrees are inferred: `deg = 1` iff `r == s == t == 0` after integerization.
**[verified]** base `x²+4y²−4=0`, `C_1: x−2=0` (`u_1=1, w_1=−2`), `C_2: y−1=0` (`v_2=1, w_2=−1`),
approximations `(2,0)` and `(0,1)` → valid arc with exact endpoints `(2,0)` and `(0,1)`.
This is the overload to use when the endpoints are algebraic (e.g. intersections of two circles)
and you only have floating-point estimates.

### 2.7 Segment between two algebraic points ("special segment")
```cpp
Curve_2 operator()(const Point_2& source, const Point_2& target) const;
```
Doc: *"`\pre` `source` and `target` must not be the same."* (real `CGAL_precondition`).
Produces `r=s=t=u=v=w=0`, `orientation() == COLLINEAR`, `IS_VALID` set, and the supporting line
stored in `Extra_data` via `update_extra_data()`: `a = y2−y1, b = x1−x2, c = x2·y1 − x1·y2,
side = ZERO`. This is the only way to feed *algebraic* endpoints (e.g. conic∩conic points) into a
segment; used by the landmarks point-location strategy. **[verified]**

### 2.8 Segment from a rational segment
```cpp
Curve_2 operator()(const Rat_segment_2& seg) const;
```
`CGAL_precondition(compare_xy(source, target) != EQUAL)`. Coefficients `r=s=t=0`; for a vertical
segment `(u,v,w) = (1, 0, −x1)`, otherwise `(u,v,w) = (y2−y1, x1−x2, x2·y1 − x1·y2)`.
Orientation `COLLINEAR`. **[verified]**

### 2.9 Full circle
```cpp
Curve_2 operator()(const Rat_circle_2& circ) const;
```
Doc: *"`circ` The circle with rational center and rational squared radius."*
Coefficients `(1, 1, 0, −2x₀, −2y₀, x₀²+y₀²−R²)`, orientation forced to `CLOCKWISE`
(the header notes this equation "describes a curve with a negative (clockwise) orientation"),
`set_full(..., /*comp_orient=*/false)`. `is_full_conic() == true`. **[verified]**

### 2.10 Circular arc
```cpp
Curve_2 operator()(const Rat_circle_2& circ, Orientation orient,
                   const Point_2& source, const Point_2& target) const;
```
Doc: *"`\pre` The source and the target must be on the conic boundary and must not be the same."*
Real preconditions: `compare_xy(source,target) != EQUAL` **and** `orient != COLLINEAR`.
For `COUNTERCLOCKWISE` the coefficients are pre-negated to `(−1,−1,0, 2x₀, 2y₀, R²−x₀²−y₀²)`;
for `CLOCKWISE` they are `(1,1,0,−2x₀,−2y₀, x₀²+y₀²−R²)`. Endpoints not on the circle ⇒ invalid
(silently). **[verified]**

---

## 3. `Construct_x_monotone_curve_2` — every overload

`auto ctr_xcv = traits.construct_x_monotone_curve_2_object();` — all `const`, return by value.

```cpp
X_monotone_curve_2 operator()(const Curve_2& cv) const;
```
*"`\pre` cv is x-monotone."* `CGAL_precondition(cv.is_valid() && is_x_monotone(cv))` where
`is_x_monotone` = "no vertical tangency points". The resulting arc gets an **invalid**
`Conic_id` (`m_id` default-constructed), so `has_same_supporting_conic` will fall back to
coefficient comparison and `Intersect_2` will **not** use the intersection cache for it.

```cpp
X_monotone_curve_2 operator()(const Curve_2& cv, const Conic_id& id) const;
```
`CGAL_precondition(xcv.is_valid() && id.is_valid())`. `Conic_id` is
`Traits::Point_2::Conic_id`; get a fresh one with `Conic_id(Arr_conic_traits_2::get_index())`.

```cpp
X_monotone_curve_2 operator()(const Curve_2& cv,
                              const Point_2& source, const Point_2& target,
                              const Conic_id& id) const;
```
Sub-arc of `cv` between `source` and `target` (both must lie on `cv`, in the arc's own direction).
This is what `Make_x_monotone_2` uses internally.

```cpp
X_monotone_curve_2 operator()(const Point_2& source, const Point_2& target) const;
```
*"`\pre` `source` and `target` must not be the same."* Builds a **special segment**:
`COLLINEAR`, `IS_VALID`, `DEGREE_1`, `IS_SPECIAL_SEGMENT`, `update_extra_data()`,
`IS_DIRECTED_RIGHT` iff `compare_xy(source,target) == SMALLER`, `IS_VERTICAL_SEGMENT` iff
`extra_data()->b == 0`. **[verified]** `is_special_segment()==1`, `degree_mask()==degree_1_mask()`.

```cpp
X_monotone_curve_2 operator()(const Algebraic& a, const Algebraic& b, const Algebraic& c,
                              const Point_2& source, const Point_2& target) const;
```
*"`a, b, c` The coefficients of the supporting line (`ax + by + c = 0`)."*
`CGAL_precondition(compare_xy != EQUAL)` and `CGAL_precondition(sign(a·x+b·y+c) == ZERO)` for both
endpoints. Stores the line via `set_extra_data(a, b, c, ZERO)`.

All resulting `X_monotone_curve_2` objects go through `Traits::set_x_monotone(xcv)`, which
computes and caches `alg_r()…alg_w()`, tags the points with the conic id, and sets
`IS_DIRECTED_RIGHT`, `IS_VERTICAL_SEGMENT`, `DEGREE_1|DEGREE_2`, `IS_SPECIAL_SEGMENT`,
`PLUS_SQRT_DISC_ROOT`, `FACING_UP|FACING_DOWN`.

---

## 4. `CGAL::Conic_arc_2<RatKernel, AlgKernel, NtTraits>` (`Traits::Curve_2`)

```cpp
template <typename RatKernel, typename AlgKernel, typename NtTraits>
class Conic_arc_2;
```
No defaults. Represents "a bounded segment that lies on a conic curve, the loci of all points
satisfying `r*x^2 + s*y^2 + t*xy + u*x + v*y + w = 0`".

### 4.1 Public typedefs
```cpp
typedef RatKernel                                        Rat_kernel;
typedef AlgKernel                                        Alg_kernel;
typedef NtTraits                                         Nt_traits;
typedef Conic_arc_2<Rat_kernel, Alg_kernel, Nt_traits>   Self;
typedef typename Rat_kernel::FT                          Rational;
typedef typename Rat_kernel::Point_2                     Rat_point_2;
typedef typename Rat_kernel::Segment_2                   Rat_segment_2;
typedef typename Rat_kernel::Circle_2                    Rat_circle_2;
typedef typename Nt_traits::Integer                      Integer;
typedef typename Alg_kernel::FT                          Algebraic;
typedef typename Alg_kernel::Point_2                     Alg_point_2;
typedef Conic_point_2<Alg_kernel>                        Point_2;
```

### 4.2 `Extra_data` (public nested struct)
```cpp
struct Extra_data {
  Algebraic a;
  Algebraic b;
  Algebraic c;
  Sign      side;
};
```
Doc: *"For arcs whose base is a hyperbola we store the axis (`a*x + b*y + c = 0`) which separates
the two branches of the hyperbola. We also store the side (`NEGATIVE` or `POSITIVE`) that the arc
occupies. In case of line segments connecting two algebraic endpoints, we use this structure to
store the coefficients of the line supporting this segment. In this case we set the side field to
be `ZERO`."*

### 4.3 Flags
```cpp
enum { IS_VALID = 0, IS_FULL_CONIC, LAST_INFO };
```
Manipulated by `protected` templates `flag_mask/reset_flags/set_flag/reset_flag/flip_flag/test_flag`
(the traits and `Conic_x_monotone_arc_2` are friends; `Conic_x_monotone_arc_2` re-exports them with
`using`, so they *are* reachable on an `X_monotone_curve_2`).

### 4.4 Protected data
```cpp
Integer m_r, m_s, m_t, m_u, m_v, m_w;
Orientation m_orient;
int         m_info;
Point_2     m_source, m_target;
Extra_data* m_extra_data;   // owned raw pointer, may be nullptr
```

### 4.5 Public, non-deprecated API
```cpp
Conic_arc_2();                                  // invalid, COLLINEAR, all coeffs 0
Conic_arc_2(const Self& arc);                   // deep-copies *m_extra_data
virtual ~Conic_arc_2();                         // deletes m_extra_data
const Self& operator=(const Self& arc);         // deep-copies m_extra_data; self-assign safe

bool is_valid() const;                          // test_flag(IS_VALID)
bool is_full_conic() const;                     // test_flag(IS_FULL_CONIC)

const Integer& r() const;  const Integer& s() const;  const Integer& t() const;
const Integer& u() const;  const Integer& v() const;  const Integer& w() const;

const Point_2& source() const;    // \pre ! is_full_conic()
Point_2&       source();          // \pre ! is_full_conic()
const Point_2& target() const;    // \pre ! is_full_conic()
Point_2&       target();          // \pre ! is_full_conic()

Orientation orientation() const;
const Extra_data* extra_data() const;

// setters (public, but intended for the traits / friends)
void set_source(const Point_2& ps);
void set_target(const Point_2& pt);
void set_coefficients(Integer r, Integer s, Integer t,
                      Integer u, Integer v, Integer w);
void set_orientation(Orientation orient);
void set_endpoints(const Point_2& source, const Point_2& target);
void set_extra_data(Extra_data* extra_data);                       // takes ownership
void set_extra_data(const Algebraic& a, const Algebraic& b,
                    const Algebraic& c, Sign side);                // news an Extra_data
void update_extra_data();                                          // line through source/target

Sign sign_of_extra_data(const Algebraic& px, const Algebraic& py) const;
```

**Ownership / lifetime:** `m_extra_data` is a raw owning pointer. `set_extra_data(Extra_data*)`
and the 4-argument `set_extra_data(...)` **leak** any previously-held pointer (they do not delete
it) — only `operator=` and the destructor free it. `update_extra_data()` likewise leaks. Do not
call these repeatedly on the same arc from binding code.

`virtual ~Conic_arc_2()` ⇒ the class has a vtable; `X_monotone_curve_2` derives from it publicly, so
`Curve_2*` → `X_monotone_curve_2*` is a real `dynamic_cast`-able relationship, but **never** store an
`X_monotone_curve_2` through a `Curve_2` by value (slicing loses the algebraic coefficients and the id).

`Curve_2` has no `is_x_monotone()`. Use `traits.vertical_tangency_points(cv, ps) == 0`
(the private helper in `Construct_x_monotone_curve_2` does exactly that) or just call
`Make_x_monotone_2` and count the pieces.

**Free function**
```cpp
template <typename Rat_kernel, typename Alg_kernel, typename Nt_traits>
std::ostream& operator<<(std::ostream& os,
                         const Conic_arc_2<Rat_kernel, Alg_kernel, Nt_traits>& arc);
```
Prints `{r*x^2 + s*y^2 + t*xy + u*x + v*y + w}` (as `double`s) then either `" - Full curve"` or
`" : (sx,sy) --cw--> (tx,ty)"` / `--ccw-->` / `--l-->`. Handy for debugging bindings.
**[verified]** `{-1*x^2 + -4*y^2 + 0*xy + 0*x + 0*y + 4} : (2,0) --ccw--> (0,1)`.

### 4.6 Deprecated members (all `CGAL_DEPRECATED`; listed so you recognise them)
Constructors: `(6 Rationals)`, `(6 Rationals, Orientation, Point_2 src, Point_2 tgt)`,
`(Point_2, Point_2)`, `(Rat_segment_2)`, `(Rat_circle_2)`,
`(Rat_circle_2, Orientation, Point_2, Point_2)`, `(Rat_point_2 ×3)`, `(Rat_point_2 ×5)`,
`(6 Rationals, Orientation, Point_2 app_src, 6 Rationals, Point_2 app_tgt, 6 Rationals)`.
Methods: `Bbox_2 bbox() const` (UB for full conics, gotcha 13), and the protected
`vertical_tangency_points`, `horizontal_tangency_points`, `_is_strictly_between_endpoints`,
`_conic_vertical_tangency_points`, `_conic_horizontal_tangency_points`, `_set_full`,
`_is_on_supporting_conic`, `_build_hyperbolic_arc_data`, `_is_between_endpoints`,
`_conic_get_y_coordinates`, `_points_at_x`, `_solve_quadratic_equation`,
`_conic_get_x_coordinates`, `_points_at_y`, `_set`.
**[verified]** the deprecated `(6 Rationals)` and `(Point_2, Point_2)` constructors still compile
and work; prefer the traits.

---

## 5. `CGAL::Conic_x_monotone_arc_2<ConicArc>` (`Traits::X_monotone_curve_2`)

```cpp
template <typename ConicArc>
class Conic_x_monotone_arc_2 : public ConicArc;
```

### 5.1 Public typedefs and re-exports
```cpp
typedef ConicArc                              Conic_arc_2;
typedef Conic_x_monotone_arc_2<Conic_arc_2>   Self;
typedef typename Conic_arc_2::Alg_kernel      Alg_kernel;
typedef typename Conic_arc_2::Algebraic       Algebraic;
typedef typename Conic_arc_2::Alg_point_2     Alg_point_2;
typedef typename Conic_arc_2::Point_2         Point_2;
typedef typename Point_2::Conic_id            Conic_id;

using Conic_arc_2::sign_of_extra_data;
using Conic_arc_2::_is_between_endpoints;      // deprecated base member
using Conic_arc_2::IS_VALID;
using Conic_arc_2::IS_FULL_CONIC;
using Conic_arc_2::flag_mask;   using Conic_arc_2::reset_flags;
using Conic_arc_2::set_flag;    using Conic_arc_2::reset_flag;
using Conic_arc_2::flip_flag;   using Conic_arc_2::test_flag;
```

### 5.2 Flag enum (public)
```cpp
enum {
  IS_VERTICAL_SEGMENT = Conic_arc_2::LAST_INFO,   // == 2
  IS_DIRECTED_RIGHT,                              // 3
  DEGREE_1,                                       // 4
  DEGREE_2,                                       // 5
  PLUS_SQRT_DISC_ROOT,                            // 6
  FACING_UP,                                      // 7
  FACING_DOWN,                                    // 8
  IS_SPECIAL_SEGMENT,                             // 9
  DEGREE_MASK = (0x1 << DEGREE_1) | (0x1 << DEGREE_2),
  FACING_MASK = (0x1 << FACING_UP) | (0x1 << FACING_DOWN)
};
```
`PLUS_SQRT_DISC_ROOT` selects which root of `s·y² + (t·x+v)·y + (r·x²+u·x+w) = 0` the arc uses;
this is the *only* thing distinguishing the upper and lower halves of the same supporting conic.

### 5.3 Private state
```cpp
Algebraic m_alg_r, m_alg_s, m_alg_t, m_alg_u, m_alg_v, m_alg_w;  // Integer coeffs as Algebraic
Conic_id  m_id;                                                  // supporting conic id
```

### 5.4 Public API
```cpp
Conic_x_monotone_arc_2();                              // default: Base() + default (invalid) id
Conic_x_monotone_arc_2(const Self& arc);               // copy
const Self& operator=(const Self& arc);                // \pre arc.is_valid()

size_t facing_mask() const;                            // m_info & FACING_MASK
size_t degree_mask() const;                            // m_info & DEGREE_MASK
static constexpr size_t degree_1_mask();               // flag_mask(DEGREE_1)
static constexpr size_t degree_2_mask();               // flag_mask(DEGREE_2)

const Integer& r() const; const Integer& s() const; const Integer& t() const;
const Integer& u() const; const Integer& v() const; const Integer& w() const;

Algebraic alg_r() const; Algebraic alg_s() const; Algebraic alg_t() const;   // by value
Algebraic alg_u() const; Algebraic alg_v() const; Algebraic alg_w() const;

const Point_2& left()  const;    // source() if IS_DIRECTED_RIGHT else target()
const Point_2& right() const;    // target() if IS_DIRECTED_RIGHT else source()

bool is_directed_right()  const; // test_flag(IS_DIRECTED_RIGHT)
bool is_vertical()        const; // test_flag(IS_VERTICAL_SEGMENT)
bool is_upper()           const; // test_flag(FACING_UP)     -- "facing up"
bool is_lower()           const; // test_flag(FACING_DOWN)   -- "facing down"
bool is_special_segment() const; // "arc is a special segment connecting two algebraic
                                 //  endpoints (and has no underlying integer conic coefficients)"

Conic_id id() const;             // by value

void set_alg_coefficients(const Algebraic& alg_r, const Algebraic& alg_s,
                          const Algebraic& alg_t, const Algebraic& alg_u,
                          const Algebraic& alg_v, const Algebraic& alg_w);
void set_generating_conic(const Conic_id& id);   // tags BOTH endpoints

template <typename OutputIterator>
OutputIterator polyline_approximation(size_t n, OutputIterator oi) const;

Self flip() const;

void derive_by_x_at(const Alg_point_2& p, const unsigned int& i,
                    Algebraic& slope_numer, Algebraic& slope_denom) const;
void derive_by_y_at(const Alg_point_2& p, const int& i,
                    Algebraic& slope_numer, Algebraic& slope_denom) const;
```
plus everything inherited from `Conic_arc_2` (`is_valid`, `is_full_conic`, `source`, `target`,
`orientation`, `extra_data`, the setters, `sign_of_extra_data`).

* **`polyline_approximation(n, oi)`** — doc: *"`n` The maximal number of sample points. `oi` An
  output iterator, whose value-type is `pair<double,double>` … In case the arc is a line segment,
  there are 2 output points, otherwise the arc is approximated by the polyline defined by
  `(p_0, …, p_n)`, where `p_0` and `p_n` are the left and right endpoints."*
  `CGAL_precondition(n != 0)`. Samples uniformly in *x* — no error control. Prefer `Approximate_2`.
* **`flip()`** — doc: *"An arc with swapped source and target and a reverse orientation."*
  Swaps `m_source`/`m_target`, toggles `CLOCKWISE ↔ COUNTERCLOCKWISE` (leaves `COLLINEAR`), flips
  `IS_DIRECTED_RIGHT`. `left()`/`right()` are unchanged as *points*. Used by `Construct_opposite_2`.
* **`derive_by_x_at` / `derive_by_y_at`** — doc: *"`i` The order of the derivatives (either 1, 2 or
  3)"*, `\todo Allow higher order derivatives`. Returns the slope as numerator/denominator; a
  vertical tangent is signalled by `slope_denom == 0`. Falls into `CGAL_error()` for unsupported `i`.

**There is no "conic type" accessor.** Compute it yourself:
`orientation() == COLLINEAR` ⇒ segment / line-pair segment; else
`CGAL::sign(4*xcv.r()*xcv.s() - xcv.t()*xcv.t())`: `POSITIVE` ⇒ ellipse, `ZERO` ⇒ parabola,
`NEGATIVE` ⇒ hyperbola. (This is exactly what `Approximate_2::operator()` does.)

**`bbox()`**: `CGAL_DEPRECATED Bbox_2 bbox() const { return Base::bbox(); }` — use
`traits.construct_bbox_2_object()(xcv)` instead.

There is **no public split/trim/merge on the arc** in 6.1; the corresponding members
(`contains_point`, `point_at_x`, `trim`, `compare_to_left`, `compare_to_right`, `equals`,
`can_merge_with`, `merge`) are all `CGAL_DEPRECATED` and `merge` does not even compile. Use the
traits functors `Split_2`, `Trim_2`, `Are_mergeable_2`, `Merge_2`, `Equal_2`,
`Compare_y_at_x_left_2`, `Compare_y_at_x_right_2`.

The two useful private constructors — `Conic_x_monotone_arc_2(const Base&)` and
`Conic_x_monotone_arc_2(const Base&, const Conic_id&)` — are reachable only through
`Construct_x_monotone_curve_2` (`friend class Arr_conic_traits_2`).

**Free function**
```cpp
template <typename Conic_arc_2>
std::ostream& operator<<(std::ostream& os, const Conic_x_monotone_arc_2<Conic_arc_2>& xcv);
```

---

## 6. `CGAL::Conic_point_2<AlgKernel>` (`Traits::Point_2`)

```cpp
template <typename AlgKernel>
class Conic_point_2 : public AlgKernel::Point_2;
```
*"A class that stores additional information with the point's coordinates, namely the conic IDs of
the generating curves."*

```cpp
typedef AlgKernel                     Alg_kernel;
typedef typename Alg_kernel::Point_2  Base;
typedef Conic_point_2<Alg_kernel>     Self;
typedef typename Alg_kernel::FT       Algebraic;
```

### 6.1 Nested `Conic_id`
```cpp
class Conic_id {
  size_t index;                                    // private; 0 == invalid
public:
  Conic_id() : index(0) {}                         // invalid
  Conic_id(size_t ind);                            // \pre ind != 0
  bool is_valid() const;                           // index != 0
  bool operator==(const Conic_id& id) const;
  bool operator!=(const Conic_id& id) const;
  bool operator< (const Conic_id& id) const;
  bool operator> (const Conic_id& id) const;
};
```
There is **no accessor for `index`** — a binding cannot expose the numeric id, only compare ids.
Obtain a fresh valid id with `Conic_id(Arr_conic_traits_2<…>::get_index())`.

### 6.2 Constructors and members
```cpp
Conic_point_2();                                                     // default Base
Conic_point_2(const Base& p);                                        // implicit from Alg_point_2
Conic_point_2(const Algebraic& hx, const Algebraic& hy, const Algebraic& hz);  // homogeneous
Conic_point_2(const Algebraic& x, const Algebraic& y);               // Cartesian

void set_generating_conic(const Conic_id& id);        // push_back if id.is_valid()
bool is_generating_conic(const Conic_id& id) const;   // linear scan of std::list<Conic_id>
```
Private state: `std::list<Conic_id> conic_ids;`.

`x()` / `y()` come from `AlgKernel::Point_2`; with `CGAL::Cartesian<CORE::Expr>` they return
`const Algebraic&`. Convert with `CGAL::to_double(p.x())` or the traits'
`Approximate_2::operator()(p, i)`. **[verified]**

**Lifetime / ownership:** value type, no dynamic ownership of its own beyond the `std::list`;
copies are deep. But note the `const_cast` mutation in `Traits::contains_point` (gotcha 10): a
`Point_2` you handed to a predicate may have gained a `Conic_id`. Do not cache `Point_2` across
threads. Also: `Point_2` is *larger* than `Alg_point_2` — never store an array of `Point_2` and
pass it through an `Alg_point_2*` parameter.

---

## 7. Predicate / operation functors — exact signatures

All functor `operator()`s are `const`. Functors that need state hold `const Traits& m_traits`
(non-owning reference; must not outlive the traits). Stateless ones: `Construct_min_vertex_2`,
`Construct_max_vertex_2`, `Is_vertical_2`, `Parameter_space_in_x_2`, `Parameter_space_in_y_2`,
`Compare_endpoints_xy_2`, `Construct_opposite_2`.

```cpp
// Compare_x_2
Comparison_result operator()(const Point_2& p1, const Point_2& p2) const;

// Compare_xy_2  ("by x, then by y")
Comparison_result operator()(const Point_2& p1, const Point_2& p2) const;

// Construct_min_vertex_2 / Construct_max_vertex_2  -- return a REFERENCE into the arc
const Point_2& operator()(const X_monotone_curve_2& xcv) const;   // xcv.left() / xcv.right()

// Is_vertical_2
bool operator()(const X_monotone_curve_2& cv) const;

// Compare_y_at_x_2   \pre p is in the x-range of xcv
Comparison_result operator()(const Point_2& p, const X_monotone_curve_2& xcv) const;

// Compare_y_at_x_left_2
//   \pre p lies on both curves, and both are defined (lexicographically) to its left
Comparison_result operator()(const X_monotone_curve_2& xcv1,
                             const X_monotone_curve_2& xcv2,
                             const Point_2& p) const;

// Compare_y_at_x_right_2   (same, to the right)
Comparison_result operator()(const X_monotone_curve_2& xcv1,
                             const X_monotone_curve_2& xcv2,
                             const Point_2& p) const;

// Equal_2   (two overloads)
bool operator()(const X_monotone_curve_2& xcv1, const X_monotone_curve_2& xcv2) const;
bool operator()(const Point_2& p1, const Point_2& p2) const;

// Parameter_space_in_x_2 / Parameter_space_in_y_2
Arr_parameter_space operator()(const X_monotone_curve_2&, Arr_curve_end) const;  // CGAL_error!
Arr_parameter_space operator()(const Point_2) const;                             // ARR_INTERIOR
```

```cpp
// Make_x_monotone_2
template <typename OutputIterator>
OutputIterator operator()(const Curve_2& cv, OutputIterator oi) const;
```
Doc: *"`oi` … Its dereference type is a variant that wraps a `Point_2` or an `X_monotone_curve_2`."*
⇒ use `std::variant<Point_2, X_monotone_curve_2>` (it only ever writes `X_monotone_curve_2` for
conic arcs — degenerate isolated points are not produced by this traits). Allocates **one fresh
global conic id per call** (`Traits::get_index()`), assigned to every piece. Splits at the 0, 1 or 2
vertical tangency points; a full conic yields exactly 2 pieces. **[verified]** full rotated
ellipse → 2 pieces; quarter-ellipse arc → 1 piece.

```cpp
// Split_2   \pre p lies on xcv but is not one of its end-points
void operator()(const X_monotone_curve_2& xcv, const Point_2& p,
                X_monotone_curve_2& xcv1, X_monotone_curve_2& xcv2) const;
```
*"`xcv1` Output: The left resulting sub-arc (`p` is its right endpoint). `xcv2` Output: The right
resulting sub-arc (`p` is its left endpoint)."* Both outputs keep the same `Conic_id`, algebraic
coefficients and flags; only the endpoints change (and `p` gets tagged with the id). **[verified]**

```cpp
// Intersect_2
template <typename OutputIterator>
OutputIterator operator()(const X_monotone_curve_2& xcv1,
                          const X_monotone_curve_2& xcv2,
                          OutputIterator oi) const;
```
Writes either an `X_monotone_curve_2` (overlap; at most one) or a
`std::pair<Point_2, Multiplicity>` ⇒ use
`std::variant<std::pair<Point_2, std::size_t>, X_monotone_curve_2>`.
Notes from the implementation:
* If the two arcs share a supporting conic and do **not** overlap, shared endpoints are reported
  with **multiplicity 0** ("in this case we do not define the multiplicity").
* Otherwise the supporting-conic intersection list is looked up in / inserted into the traits'
  `m_inter_map`, keyed by the ordered `(Conic_id, Conic_id)` pair. If **either** id is invalid the
  cache is bypassed and results are recomputed every call — one more reason to always build
  x-monotone arcs through `Make_x_monotone_2` (which assigns valid ids).
* Multiplicity is computed by comparing first/second derivatives → 1, 2 or 3.
**[verified]** quarter ellipse × segment `y=x` → 1 point `(0.894…, 0.894…)`, multiplicity 1.

```cpp
// Are_mergeable_2
bool operator()(const X_monotone_curve_2& xcv1, const X_monotone_curve_2& xcv2) const;
// "true if the two curves are mergeable; that is, they are supported by the same curve
//  and share a common endpoint"

// Merge_2   \pre The two arcs are mergeable
void operator()(const X_monotone_curve_2& xcv1, const X_monotone_curve_2& xcv2,
                X_monotone_curve_2& xcv) const;
```
`Merge_2` does `xcv = xcv1;` then extends. **[verified]** split-then-merge round-trips
`left().x() = −1.2`, `right().x() = 2`.

```cpp
// Compare_endpoints_xy_2   (stateless)
Comparison_result operator()(const X_monotone_curve_2& cv) const
{ return (cv.is_directed_right()) ? SMALLER : LARGER; }

// Construct_opposite_2     (stateless)
X_monotone_curve_2 operator()(const X_monotone_curve_2& cv) const { return cv.flip(); }
```
**[verified]** `compare_endpoints_xy_2(x) == LARGER (1)` and `== SMALLER (−1)` for its opposite.

```cpp
// Trim_2
//   \pre src != tgt
//   \pre both points must be interior and must lie on cv
X_monotone_curve_2 operator()(const X_monotone_curve_2& xcv,
                              const Point_2& src, const Point_2& tgt) const;
```
Real `CGAL_precondition`s: `compare_y_at_x_2(src, xcv) == EQUAL`, same for `tgt`,
`! equal_2(src, tgt)`. The arguments are **reordered automatically** to match `xcv`'s direction, so
you may pass them in either order. Returns a copy of `xcv` with new endpoints (same id, same
coefficients). **[verified]**

```cpp
// Construct_bbox_2   (two overloads)
Bbox_2 operator()(const X_monotone_curve_2& xcv) const;
Bbox_2 operator()(const Curve_2& cv) const;
```
`CGAL_precondition(xcv.is_valid())`. For a full conic it uses the 2 vertical + 2 horizontal
tangency points; otherwise it starts from source/target and extends by the tangency points that
lie inside the arc. All arithmetic through `CGAL::to_double` ⇒ **the box is not certified to
contain the arc** (no outward rounding). Widen it yourself before using it as a filter.
**[verified]** quarter ellipse → `0 0 2 1`.

---

## 8. `Approximate_2` — all overloads

```cpp
// 1) point coordinate.  \pre i is either 0 or 1
Approximate_number_type operator()(const Point_2& p, int i) const;
//    i == 0 -> CGAL::to_double(p.x());  i == 1 -> CGAL::to_double(p.y())

// 2) point
Approximate_point_2 operator()(const Point_2& p) const;
//    == Approximate_point_2(operator()(p,0), operator()(p,1))

// 3) polyline approximation of an x-monotone arc
template <typename OutputIterator>
OutputIterator operator()(const X_monotone_curve_2& xcv, double error,
                          OutputIterator oi, bool l2r = true) const;
```

Overload 3 semantics:
* **Value type written is `Approximate_point_2` (= `CGAL::Cartesian<double>::Point_2`)**, not
  `std::pair<double,double>` (that is `polyline_approximation`, a different function).
* **`l2r`** (*left to right*): `true` ⇒ the polyline starts at `construct_min_vertex_2(xcv)`
  (the arc's `left()`) and ends at `construct_max_vertex_2(xcv)`; `false` ⇒ reversed.
  It has nothing to do with the arc's own `source`/`target` direction — use
  `Compare_endpoints_xy_2` if you need to match the arc's orientation
  (`l2r = xcv.is_directed_right()` reproduces source→target order). **[verified]**
* **`error`** is documented as *"the error bound of the generated approximation. This is the
  Hausdorff distance between the arc and the polyline"*. The recursion in `add_points` measures the
  perpendicular distance from the canonical mid-parameter point `t = (t1+t2)/2` to the chord and
  stops when it is `< error`, so it is a real (isometry-invariant) geometric bound, not a
  parameter-space one.
* Dispatch (verbatim):
  ```cpp
  if (xcv.orientation() == COLLINEAR) return approximate_segment(xcv, oi, l2r);
  CGAL::Sign sign_conic = CGAL::sign(4*xcv.r()*xcv.s() - xcv.t()*xcv.t());
  if (sign_conic == POSITIVE) return approximate_ellipse(xcv, error, oi, l2r);
  if (sign_conic == NEGATIVE) return approximate_hyperbola(xcv, error, oi, l2r);
  return approximate_parabola(xcv, error, oi, l2r);
  ```
  A segment yields **exactly 2** points and ignores `error`. Conic arcs always emit both endpoints
  plus the adaptively-subdivided interior points, so the count is ≥ 2.
* Parametrisations used internally: ellipse `(a·cos t, b·sin t)`, hyperbola `(a·cosh t, b·sinh t)`,
  parabola with its own `tm` rule; then `transform_point(xc,yc,cost,sint,cx,cy,x,y)` =
  `x = xc·cost − yc·sint + cx`, `y = xc·sint + yc·cost + cy`.
* **[verified]** quarter-ellipse with `error = 0.01` → 9 points, first `(0,1)` last `(2,0)` for
  `l2r=true`; first `(2,0)` for `l2r=false`.

`Approximate_curve_length_2` — see gotcha 5. Not usable.

---

## 9. Public traits-level helper methods (not functors, but public and useful)

These live directly on `Arr_conic_traits_2` after `construct_bbox_2_object()` (lines 3049–4292) and
are all `public` and `const`. They are the non-deprecated replacements for the deprecated
`Conic_arc_2` internals and are exactly what a type-erased C++ core wants.

```cpp
void   set(Curve_2& cv, const Rational* rat_coeffs) const;       // rat_coeffs[6]: x^2,y^2,xy,x,y,1
void   set_full(Curve_2& cv, const Rational* rat_coeffs, const bool& comp_orient) const;
void   set_x_monotone(X_monotone_curve_2& xcv) const;

bool   is_on_supporting_conic(Curve_2& cv, const Point_2& p) const;   // NOTE: non-const Curve_2&
bool   is_between_endpoints(const Curve_2& cv, const Point_2& p) const;         // \pre !is_full_conic
bool   is_strictly_between_endpoints(const Curve_2& cv, const Point_2& p) const;
bool   contains_point(const X_monotone_curve_2& xcv, const Point_2& p) const;   // mutates p's ids!
bool   has_same_supporting_conic(const X_monotone_curve_2& xcv1,
                                 const X_monotone_curve_2& xcv2) const;

void   build_hyperbolic_arc_data(Curve_2& cv) const;              // buggy, see gotcha 4

int    conic_get_x_coordinates(const Curve_2& cv, const Algebraic& y, Algebraic* xs) const; // xs[2]
int    conic_get_y_coordinates(const Curve_2& cv, const Algebraic& x, Algebraic* ys) const; // ys[2]
int    solve_quadratic_equation(const Algebraic& A, const Algebraic& B, const Algebraic& C,
                                Algebraic& x_minus, Algebraic& x_plus) const;

Point_2 point_at_x(const X_monotone_curve_2& xcv, const Point_2& p) const;
//   \pre The arc is not vertical and p is in the x-range of the arc.

int    points_at_x(const Curve_2& cv, const Point_2& p, Alg_point_2* ps) const;  // ps[2]
int    points_at_y(const Curve_2& cv, const Point_2& p, Alg_point_2* ps) const;  // ps[2]

int    conic_vertical_tangency_points(const Curve_2& cv, Alg_point_2* ps) const;    // ps[2]
size_t conic_horizontal_tangency_points(const Curve_2& cv, Alg_point_2* ps) const;  // ps[2]
size_t vertical_tangency_points(const Curve_2& cv, Alg_point_2* vpts) const;        // vpts[2]
int    horizontal_tangency_points(const Curve_2& cv, Alg_point_2* hpts) const;      // hpts[2]
```
Note the inconsistent return types (`int` vs `size_t`) — that is verbatim from the header.
All the `*_tangency_points` take a **caller-allocated array of 2** `Alg_point_2` (documented
`\pre The vector should be allocated at the size of 2`), and *"return only the points that are
contained in the arc interior"* (the `conic_*` variants return the tangency points of the whole
supporting conic). `vertical_tangency_points` / `horizontal_tangency_points` return 0 immediately
for `orientation() == COLLINEAR`.

Double-precision canonicalisation helpers (used by `Approximate_2` and available for drawing):
```cpp
void inverse_conic(const X_monotone_curve_2& xcv, double cost, double sint,
                   double& r_m, double& s_m, double& t_m,
                   double& u_m, double& v_m, double& w_m) const;
void canonical_conic(const X_monotone_curve_2& xcv,
                     double& r_m, double& s_m, double& t_m,
                     double& u_m, double& v_m, double& w_m,
                     double& cost, double& sint) const;
void inverse_transform_point(double x, double y, double cost, double sint,
                             double cx, double cy, double& xc, double& yc) const;

void approximate_parabola(const X_monotone_curve_2& xcv,
                          double& r_m, double& t_m, double& s_m,
                          double& u_m, double& v_m, double& w_m,
                          double& cost, double& sint,
                          double& xs_t, double& ys_t,
                          double& xt_t, double& yt_t,
                          double& a, double& ts, double& tt,
                          double& cx, double& cy,
                          bool l2r = true) const;

void approximate_ellipse(const X_monotone_curve_2& xcv,
                         double& r_m, double& t_m, double& s_m,
                         double& u_m, double& v_m, double& w_m,
                         double& cost, double& sint,
                         double& xs_t, double& ys_t, double& ts,
                         double& xt_t, double& yt_t, double& tt,
                         double& a, double& b, double& cx, double& cy,
                         bool l2r = true) const;

void approximate_hyperbola(const X_monotone_curve_2& xcv,
                           double& r_m, double& t_m, double& s_m,
                           double& u_m, double& v_m, double& w_m,
                           double& cost, double& sint,
                           double& xs_t, double& ys_t, double& ts,
                           double& xt_t, double& yt_t, double& tt,
                           double& a, double& b, double& cx, double& cy,
                           bool l2r = true) const;
```
Note the argument order `r_m, t_m, s_m` (t before s) in the three `approximate_*` methods but
`r_m, s_m, t_m` in `inverse_conic`/`canonical_conic` — verbatim from the header, and an easy
mistake to make. `approximate_ellipse` yields the semi-axes `a`, `b`, the centre `(cx,cy)`, the
rotation `(cost,sint)` and the endpoint parameters `ts`, `tt` — that is everything you need to draw
an SVG/Qt elliptical arc or to compute an exact-ish arc length with `boost::math::ellint_2`.
`approximate_hyperbola` flips the sign of `a` when `xs_t < 0` (branch selection).

---

## 10. Free functions in `Conic_intersections_2.h`

Two overloads, both in namespace `CGAL`, both templated on `Nt_traits`. Used by the
"approximate endpoints defined by auxiliary conics" constructor.

```cpp
template <typename Nt_traits>
int compute_resultant_roots(const Nt_traits& nt_traits,
                            const typename Nt_traits::Integer& r1, ... /* s1,t1,u1,v1,w1 */,
                            const int& deg1,
                            const typename Nt_traits::Integer& r2, ... /* s2,t2,u2,v2,w2 */,
                            const int& deg2,
                            typename Nt_traits::Algebraic* xs);
// \pre xs must be a vector of size 4.  \return The number of distinct roots found (sorted ascending)

template <typename Nt_traits>
int compute_resultant_roots(const Nt_traits& nt_traits,
                            const typename Nt_traits::Algebraic& r, ... /* s,t,u,v,w */,
                            const int& deg1,
                            const typename Nt_traits::Algebraic& A,
                            const typename Nt_traits::Algebraic& B,
                            const typename Nt_traits::Algebraic& C,
                            typename Nt_traits::Algebraic* xs);
// C2 is the line A*x + B*y + C = 0.  \pre xs must be a vector of size 4.
```
`deg1`/`deg2` must be 1 (r=s=t=0) or 2. The first overload builds the degree-3 or degree-4
resultant and calls `Nt_traits::compute_polynomial_roots`; the second reduces to a quadratic.
To get both coordinates you call it twice, the second time with `(s, r, t, v, u, w)` — i.e. the
x/y-swapped system — and then pair-and-filter the candidates, exactly as `Construct_curve_2` does.

---

## 11. `Nt_traits` surface you actually need (`CGAL::CORE_algebraic_number_traits`)

```cpp
typedef CORE::BigInt              Integer;    // boost::multiprecision::mpz_int
typedef CORE::BigRat              Rational;   // boost::multiprecision::mpq_rational
typedef CORE::Polynomial<Integer> Polynomial;
typedef CORE::Expr                Algebraic;

Algebraic convert(const Integer& z) const;
Algebraic convert(const Rational& q) const;
Algebraic sqrt(const Algebraic& x) const;                       // \pre x >= 0

template <class InputIterator, class OutputIterator>
OutputIterator convert_coefficients(InputIterator q_begin, InputIterator q_end,
                                    OutputIterator zoi) const;
//   a(i) = n(i) * lcm{d(1..k)} / ( d(i) * gcd{n(1..k)} )

template <class NT, class OutputIterator>
OutputIterator solve_quadratic_equation(const NT& a, const NT& b, const NT& c,
                                        OutputIterator oi) const;
Polynomial construct_polynomial(const Integer* coeffs, unsigned int degree) const;
template <class OutputIterator>
OutputIterator compute_polynomial_roots(const Polynomial& poly, OutputIterator oi) const;
```
The `convert_coefficients` scaling explains the integer coefficients you observe:
**[verified]** rotated-ellipse `r = 73/25` → stored `r() = −73` (×25 for the lcm of denominators,
then negated for `COUNTERCLOCKWISE`).

---

## 12. Mathematical notes — feeding exact curves to `Construct_curve_2`

All three recipes end at the same call:

```cpp
Curve_2 arc = traits.construct_curve_2_object()(r, s, t, u, v, w,
                                                orient, source, target);
if (! arc.is_valid()) { /* reject */ }
```
with `r…w` of type `Rational` and `source`/`target` of type `Traits::Point_2` (algebraic).
Remember: the traits may negate `r…w`, and it will *never* tell you the endpoints were off-curve —
it just returns an invalid arc.

### 12.1 Rational quadratic Bézier → `Conic_arc_2` (exact)

Input: control points `P0, P1, P2 ∈ ℚ²`, weights `w0, w1, w2 ∈ ℚ`, all `> 0`.

```
B(τ) = [ w0(1−τ)²P0 + 2w1 τ(1−τ)P1 + w2 τ²P2 ] / [ w0(1−τ)² + 2w1 τ(1−τ) + w2 τ² ],  τ ∈ [0,1]
```

**Derivation.** The barycentric coordinates of `B(τ)` w.r.t. the triangle `(P0,P1,P2)` are
proportional to `(λ0, λ1, λ2) = (w0(1−τ)², 2w1τ(1−τ), w2τ²)`. Then
`λ1² = 4w1²τ²(1−τ)²` and `λ0λ2 = w0w2 τ²(1−τ)²`, so `w0 w2 · λ1² − 4 w1² · λ0 λ2 = 0`
identically. The equation is homogeneous of degree 2 in `(λ0,λ1,λ2)`, so we may replace the
normalised barycentrics by the *unnormalised* affine forms (twice the signed sub-triangle areas),
which are affine in `(x,y)`:

```
L_A(x,y) = 2·area(P, P1, P2) = a1·x + a2·y + a3,  a1 = y1−y2, a2 = x2−x1, a3 = x1y2 − x2y1
L_B(x,y) = 2·area(P0, P, P2) = b1·x + b2·y + b3,  b1 = y2−y0, b2 = x0−x2, b3 = x2y0 − x0y2
L_C(x,y) = 2·area(P0, P1, P) = c1·x + c2·y + c3,  c1 = y0−y1, c2 = x1−x0, c3 = x0y1 − x1y0
```
(`L_A` vanishes on line `P1P2`, `L_B` on `P0P2`, `L_C` on `P0P1`, and
`L_A + L_B + L_C ≡ 2·area(P0,P1,P2)`.) The implicit conic is

```
F(x,y) = K · L_B(x,y)²  −  M · L_A(x,y) · L_C(x,y) = 0,     K = w0·w2,   M = 4·w1²
```

Expanding gives the six rational coefficients directly:

```
r = K·b1²        − M·a1·c1
s = K·b2²        − M·a2·c2
t = 2K·b1·b2     − M·(a1·c2 + a2·c1)
u = 2K·b1·b3     − M·(a1·c3 + a3·c1)
v = 2K·b2·b3     − M·(a2·c3 + a3·c2)
w = K·b3²        − M·a3·c3
```

**Conic type** (all rational sign tests, no square roots):

| condition | supporting conic | `4rs − t²` |
|---|---|---|
| `w1² < w0·w2` | ellipse | `> 0` |
| `w1² = w0·w2` | parabola | `= 0` |
| `w1² > w0·w2` | hyperbola | `< 0` — **avoid, see gotcha 4** |

**[verified]** for `P0=(0,0), P1=(1,2), P2=(3,1)`:
`(1,1,1)` → `sign(4rs−t²)=0`, `(1,1/2,1)` → `+1`, `(2,1,3)` → `+1`, `(1,2,1)` → `−1`,
matching `compare(w1², w0·w2)` exactly. Sample points `B(1/4), B(1/2), B(3/4)` satisfy
`F(x,y) == 0` exactly in ℚ.

**Endpoints.** `B(0) = P0`, `B(1) = P2`, both rational and exactly on `F = 0` (`L_B` and `L_C`
vanish at `P0`; `L_A` and `L_B` vanish at `P2`). Lift them with `Nt_traits::convert`:

```cpp
Point_2 source(nt->convert(P0.x()), nt->convert(P0.y()));
Point_2 target(nt->convert(P2.x()), nt->convert(P2.y()));
```

**Orientation, robustly.** The arc bulges toward `P1`, and the shoulder
`S = B(1/2) = (w0·P0 + 2w1·P1 + w2·P2)/(w0+2w1+w2)` satisfies
`cross(S−P0, P2−P0) = (2w1/Σ)·cross(P1−P0, P2−P0)` with `2w1/Σ > 0`, so it has the same sign as
`orientation(P0,P1,P2)`. CGAL's `is_strictly_between_endpoints` uses
`orientation(source, p, target) == LEFT_TURN` for `COUNTERCLOCKWISE`. Therefore:

```cpp
Orientation orient = (CGAL::orientation(P0, P1, P2) == CGAL::LEFT_TURN)
                     ? CGAL::COUNTERCLOCKWISE : CGAL::CLOCKWISE;   // exact rational predicate
```
Degenerate case: if `orientation(P0,P1,P2) == COLLINEAR` the Bézier is a straight segment — emit
`ctr(Rat_segment_2(P0,P2))` instead (`F` would be a degenerate line pair).

**Practical notes.**
* Since `F` is homogeneous, you may divide `(r,…,w)` by any common rational factor to keep the
  integers CGAL derives small; `convert_coefficients` already divides by `gcd{numerators}` and
  multiplies by `lcm{denominators}`, so pre-reducing only helps marginally.
* **[verified] end-to-end**: for `(1,1,1)` (parabola) the constructed arc is valid, produces 1
  x-monotone piece, `compare_y_at_x_2(shoulder, piece) == EQUAL`, and inserts into an
  `Arrangement_2` giving `v=2, e=1`. Same for `(1,1/2,1)` and `(2,1,3)` (ellipses).
* Non-uniform rational Béziers whose weights are not all positive are not curve segments of this
  form; reject them.
* Rational **cubic** Béziers are not conics — use `Arr_Bezier_curve_traits_2`, or subdivide+fit.

### 12.2 Circular arc → conic arc

Input: centre `(x0, y0) ∈ ℚ²`, squared radius `R² ∈ ℚ`, endpoints on the circle, desired
traversal direction.

Two equivalent routes:

```cpp
// (a) via the circle overload — coefficients chosen for you
Curve_2 arc = ctr(Rat_circle_2(Rat_point_2(x0,y0), R2), orient, source, target);

// (b) explicit coefficients
//   r = 1, s = 1, t = 0, u = −2·x0, v = −2·y0, w = x0² + y0² − R²
Curve_2 arc = ctr(Rational(1), Rational(1), Rational(0),
                  -2*x0, -2*y0, x0*x0 + y0*y0 - R2, orient, source, target);
```
`(a)` pre-negates to `(−1,−1,0, 2x0, 2y0, R²−x0²−y0²)` when `orient == COUNTERCLOCKWISE`; `(b)`
lets `Traits::set` do the negation. Both end with the same stored coefficients. Preconditions:
`source != target` and (for `(a)`) `orient != COLLINEAR`; endpoints off the circle silently
produce `is_valid() == false`.

**Getting exactly-on-circle endpoints.**
* If `R ∈ ℚ` (not just `R²`), every rational point of the circle is
  `(x0 + R·(1−m²)/(1+m²), y0 + R·2m/(1+m²))` for `m ∈ ℚ ∪ {∞}` — use this to snap input angles
  to nearby rational points.
* If only `R² ∈ ℚ`, build algebraic endpoints:
  `Algebraic Rr = nt->sqrt(nt->convert(R2));` then
  `Point_2(nt->convert(x0) + Rr*cosθ_alg, nt->convert(y0) + Rr*sinθ_alg)` where `cosθ_alg`,
  `sinθ_alg` are themselves exact algebraic numbers (e.g. `(1−m²)/(1+m²)`, `2m/(1+m²)`).
  `Point_2` coordinates are `CORE::Expr`, so this is exact.
* If the endpoint is the intersection of the circle with another conic/line, use the
  §2.6 "approximate endpoints + two auxiliary conics" overload instead of computing it yourself.

**Full circle:** `ctr(Rat_circle_2(...))` → `is_full_conic() == true`, `orientation() == CLOCKWISE`,
no source/target. Must be fed through `Make_x_monotone_2` (2 pieces) before use.

**Orientation:** `COUNTERCLOCKWISE` = mathematically positive traversal from `source` to `target`.
Since the natural `Conic_2::orientation()` of `x²+y²+… ` (i.e. `r > 0`) is `NEGATIVE = CLOCKWISE`,
requesting `COUNTERCLOCKWISE` flips the stored coefficients to `r() = −1`.

### 12.3 Ellipse arc from centre / semi-axes / rotation

Input: centre `(cx, cy)`, squared semi-axes `a² , b²`, rotation angle θ given as a rational
`(c, s) = (cos θ, sin θ)` with `c² + s² = 1` (use `c = (1−m²)/(1+m²), s = 2m/(1+m²)`, `m ∈ ℚ`).
Only `a²`, `b²`, `c`, `s`, `cx`, `cy` need to be rational.

Canonical coordinates `X = c·dx + s·dy`, `Y = −s·dx + c·dy` with `dx = x−cx`, `dy = y−cy`;
the ellipse is `b²X² + a²Y² − a²b² = 0`. Expanding:

```
A = b²·c² + a²·s²
B = b²·s² + a²·c²
C = 2·c·s·(b² − a²)

r = A
s = B                                  (careful: the conic's "s", not sin θ)
t = C
u = −2·A·cx − C·cy
v = −2·B·cy − C·cx
w =  A·cx² + B·cy² + C·cx·cy − a²·b²
```

Sanity identity: `4rs − t² = 4AB − C² = 4a²b²(c²+s²)² = 4·a²·b² > 0`.
**[verified]** `cx=1, cy=2, a²=4, b²=1, c=3/5, s=4/5` → `4rs − t² = 16 = 4·a²·b²`.

**Rational points on the rotated ellipse** (needed for exact endpoints, requires `a, b ∈ ℚ`):
for any `m ∈ ℚ`, with `(Cm, Sm) = ((1−m²)/(1+m²), 2m/(1+m²))`,
```
x = cx + c·(a·Cm) − s·(b·Sm)
y = cy + s·(a·Cm) + c·(b·Sm)
```
**[verified]** `F(x,y) == 0` exactly in ℚ for `m = 0` and `m = 1`.
If `a` or `b` is irrational, build the endpoints as `Algebraic` via `nt->sqrt(...)`, exactly as in
§12.2.

**Orientation.** As with the circle: for the coefficients above `r = A > 0`, so the natural
`Conic_2::orientation()` is `CLOCKWISE`; ask for `COUNTERCLOCKWISE` to traverse
`source → target` in the mathematically positive sense and expect `arc.r() < 0` afterwards.
**[verified]** `r = 73/25 → arc.r() == −73` with `COUNTERCLOCKWISE`; arc valid, 1 x-monotone piece.
The same coefficients passed to the 6-argument overload give a **full** rotated ellipse
(`is_full_conic() == 1`, `orientation() == CLOCKWISE`, 2 x-monotone pieces). **[verified]**

**Choosing between the two arcs.** The pair `(source, target, orient)` determines the arc
uniquely: `COUNTERCLOCKWISE` takes the CCW sweep from `source` to `target`, `CLOCKWISE` the other
one. If your input is an angle interval `[α0, α1]` in the canonical frame, map it to
`source = P(α0)`, `target = P(α1)` and use `COUNTERCLOCKWISE` when `α1 > α0` (mod 2π), splitting
into ≤ 2 arcs if the sweep exceeds π (not required by CGAL, but keeps `Make_x_monotone_2` output
to 1–2 pieces and keeps `Bbox` tight).

### 12.4 Checklist for a binding layer

1. Build `r…w` as `Rational`; reduce by a common factor if convenient.
2. Compute `sign(4rs − t²)`. `POSITIVE` (ellipse) and `ZERO` (parabola) are fine. If `NEGATIVE`
   (hyperbola) run the exact rational predicate of §13.4:
   `N = (4rs−t²)w − su² − rv² + tuv`; if `N == 0` the conic is a degenerate line pair — reject.
   Otherwise `(R,S,T) = sign(N)·(r,s,t)`; the arc is safe to hand to `Construct_curve_2` iff
   `T == 0` **or** `sign(P·√((R−S)²+T²) − E) ≤ 0` with `P = R+S`, `E = T² − (R−S)²`
   (decided by rational arithmetic alone — code in §13.4). If it is *not* safe, do **not** call
   `Construct_curve_2`: build the `Curve_2` yourself with the correct branch-separating axis
   (§13.8(g)), rotate the whole input by a rational rotation that makes it safe (§13.8(e)), or fall
   back to `Arr_algebraic_segment_traits_2` (§13.10). Also verify yourself that source and target
   lie on the *same branch* — CGAL only asserts it, too late (§13.9).
3. Determine `orient` with an exact rational orientation predicate on `(source, interior, target)`.
4. Lift the endpoints to `Point_2` with `nt_traits->convert` (rational) or exact `Algebraic`
   expressions; or use the §2.6 auxiliary-conic overload.
5. Call `construct_curve_2_object()(r,s,t,u,v,w,orient,source,target)`.
6. **Check `is_valid()`.** Then `make_x_monotone_2_object()` into
   `std::vector<std::variant<Point_2, X_monotone_curve_2>>`.
7. Keep your own `(r,s,t,u,v,w)` if you need to hand them back to the caller — CGAL's may be negated.

---

## 13. The hyperbolic `Extra_data` bug — exact diagnosis, a decidable rational predicate, and workarounds

*(This section supersedes the statistical description in gotcha 4. Everything below was verified by
compiling and running programs against the installed headers, in **both** a debug build
(`-O0`, CGAL assertions **on**) and an `-O2 -DNDEBUG` build (assertions **off**). Test sources live
outside the repo; the families and counts are reproduced here so they can be re-derived.)*

### 13.1 What the `Extra_data` line is supposed to be

`Conic_arc_2::Extra_data` (`Conic_arc_2.h:75`) stores one line plus a side:

```cpp
struct Extra_data { Algebraic a; Algebraic b; Algebraic c; Sign side; };
```

Header comment (`Conic_arc_2.h:69–74`), verbatim:

> *"For arcs whose base is a hyperbola we store the axis (a\*x + b\*y + c = 0) which separates the
> two bracnes of the hyperbola. We also store the side (NEGATIVE or POSITIVE) that the arc occupies.
> In case of line segments connecting two algebraic endpoints, we use this structure two store the
> coefficients of the line supporting this segment. In this case we set the side field to be ZERO."*

So for a hyperbola the invariant that the rest of the traits relies on is:

> **`a·x + b·y + c = 0` must be a line that the hyperbola does not meet**, i.e. one branch lies
> strictly in `{a·x+b·y+c > 0}` and the other strictly in `{a·x+b·y+c < 0}`; `side` is the sign of
> the branch the arc lives on.

Any line through the centre that misses both branches (the conjugate axis, an asymptote, or
anything "between" the asymptotes on the empty side) satisfies the invariant. The *conjugate axis*
is the canonical choice, and it is what the code comment claims to build.

### 13.2 What `build_hyperbolic_arc_data()` actually computes

`Arr_conic_traits_2.h:3316`. Notation: the arc's **stored** integer coefficients
`r,s,t,u,v,w` (already sign-normalised by `set()`, see gotcha 2), and
`or_fact = (orientation()==CLOCKWISE) ? −1 : +1`, applied to `r,s,t,u,v` (**not** to `w`).
Write `R,S,T,U,V = or_fact·(r,s,t,u,v)`.

```cpp
const Algebraic cos_2phi = (r - s) / m_nt_traits->sqrt((r-s)*(r-s) + t*t);
...
//  sin(phi)^2 = 0.5 * (1 - cos(2*phi))
//  cos(phi)^2 = 0.5 * (1 + cos(2*phi))
Sign sign_t = CGAL::sign(t);
if (sign_t == ZERO) {
  if (CGAL::sign(cos_2phi) == POSITIVE) { sin_phi = zero; cos_phi = one;  }   // phi = 0
  else                                  { sin_phi = one;  cos_phi = zero; }   // phi = PI/2
}
else if (sign_t == POSITIVE) {
  sin_phi =   m_nt_traits->sqrt((one + cos_2phi) / two);      // <-- this is |cos phi|
  cos_phi =   m_nt_traits->sqrt((one - cos_2phi) / two);      // <-- this is |sin phi|
}
else {
  sin_phi =   m_nt_traits->sqrt((one + cos_2phi) / two);      // <-- |cos phi|
  cos_phi = - m_nt_traits->sqrt((one - cos_2phi) / two);      // <-- -|sin phi|
}
```

**The two `sign_t != 0` branches assign the half-angle formulae the wrong way round** — they
contradict the comment three lines above them, and they contradict the `sign_t == ZERO` branch
(which *is* correct: `t=0, r>s ⇒ φ=0 ⇒ (cos,sin)=(1,0)`).

The effect is not "the other half-angle branch"; swapping `sin↔cos` is the reflection
`φ ↦ π/2 − φ`, i.e. **the code computes the principal axis of the conic with `x` and `y`
interchanged** and then uses it as if it belonged to the original conic. (The centre `(x0,y0)` is
computed correctly and is *not* mirrored, so the result is not a coordinate-frame artefact — it is
a genuine line through the correct centre pointing in a wrong direction.)

The stored line is then
`a·x + b·y + c = 0` with `(a,b) = (cos_phi, sin_phi)` and `c = −(a·x0 + b·y0)`,
`x0 = (T·V − 2S·U)/det`, `y0 = (T·U − 2R·V)/det`, `det = 4RS − T² < 0`
— always a line **through the centre**, therefore always centrally symmetric, therefore always
either a separating line or a line that cuts **each** branch exactly once ("a chord").

**[verified] direct numerical fingerprint of the mirror.** For `x·y = 1` rotated by
`θ = 81.87°` (`m = 1/3`, `(c,s) = (4/5,3/5)`, coefficients `(−12,12,7,0,0,−25)`), the true
transverse-axis angle is `81.87°` and the line CGAL stores has normal
`(a,b) = (0.9899…, 0.14142…)`, i.e. angle `8.13° = 90° − 81.87°`. Exactly `φ ↦ π/2 − φ`.

The same buggy code is duplicated verbatim in the deprecated
`Conic_arc_2::_build_hyperbolic_arc_data()` (`Conic_arc_2.h:1099`, called from
`Conic_arc_2::_set()` at `:1428`).

### 13.3 Exactly when the computed line is a chord — a rational predicate

Let the *input* rational coefficients be `(r,s,t,u,v,w)` for `r x² + s y² + t xy + u x + v y + w = 0`.

**Step 0 — the sign normalisation is orientation-independent.**
`set()` negates all six coefficients iff `Rat_kernel::Conic_2::orientation() != orient`, and
`build_hyperbolic_arc_data` then multiplies by `or_fact = ±1` according to `orient`.
For a hyperbola `ConicCPA2::analyse()` (`ConicCPA2.h:181–191`) gives

```cpp
FT z_prime = d*w() - u()*u()*s() - v()*v()*r() + u()*v()*t();   // d = 4sr - t^2
o = (CGAL::Orientation)(CGAL_NTS sign (z_prime));
```

`z_prime` is odd (degree 3) in the coefficients, so the two negations compose to
**`(R,S,T,U,V,W) = sign(N)·(r,s,t,u,v,w)` with `N = z_prime`, independent of the requested
orientation and of any positive rescaling.** With that normalisation `N > 0` always.
**[verified]** the same arc built as `(src→tgt, CCW)` and `(tgt→src, CW)` stores negated
coefficients `(−12,12,7,0,0,−25)` vs `(12,−12,−7,0,0,25)` but the **identical** `Extra_data` line
`(0.98995, 0.14142, 0)` and the identical `side = +1`.

**Step 1 — the geometry.** In centred coordinates the hyperbola is `Q(X) = k` with
`Q(X,Y) = R X² + T XY + S Y²` and `k = −(W + (U x0 + V y0)/2) > 0` (`sign(k) = sign(N) = +1`).
A line through the centre with direction `d` meets the curve iff `Q(d)·k > 0`, i.e. iff `Q(d) > 0`.
The direction of the line CGAL builds is `d_code = (−b, a) = (−|cos φ|, sign(T)·|sin φ|)`, and a
short computation gives

```
Q(d_code) = ( P·D + (R−S)² − T² ) / (2D) = ( P·D − E ) / (2D),
      P = R + S,   E = T² − (R−S)²,   D = sqrt((R−S)² + T²) > 0
```

(the *correct* conjugate-axis direction gives `Q = λ₋ = (P−D)/2 < 0`, always safe).

> **PREDICATE.** With `(R,S,T) = sign(N)·(r,s,t)`:
> * `T == 0` ⇒ **SAFE** (the `sign_t == ZERO` branch of the code is correct);
> * otherwise **SAFE ⟺ `P·D − E ≤ 0`**, **UNSAFE (chord) ⟺ `P·D − E > 0`**.
>
> `sign(P·D − E)` is decidable with rational arithmetic only:
> ```
> if sign(P)*sign(E) <= 0:  q = (P != 0) ? sign(P) : -sign(E)
> else:                     q = sign(P) * sign( P*P*((R-S)^2 + T^2) - E*E )
> ```
> (`P·D − E = 0` means the line *is* an asymptote — still separating, hence SAFE.)

**Equivalent geometric form.** Let `θ` be the angle of the transverse axis and `γ` the asymptote
half-angle (`tan γ = b/a`). Then

```
UNSAFE  ⟺  dist( 2θ mod π , 0 ) < γ
```

and for `x²/a² − y²/b² = 1` rotated by `θ`:  **UNSAFE ⟺ `cos 4θ > (a²−b²)/(a²+b²)`**.
Consequences worth internalising:

* the fraction of random rotations that trip the bug is `2γ/π` — **exactly 1/2 for every
  rectangular hyperbola** (`γ = 45°`), tending to 1 for "flat" hyperbolas (`b ≫ a`) and to 0 for
  "pointy" ones (`b ≪ a`). This is what "2 of 4 random Bézier hyperbolas" was measuring.
* the predicate depends **only on the supporting conic** — not on the endpoints, the arc's
  orientation, the sign or scale of the coefficients, or any translation (`u,v,w` enter only
  through `sign(N)`).
* `T == 0` is a genuine discontinuity: `x²/4 − y² = 1` is SAFE, and the same hyperbola rotated by
  `1.15°` is UNSAFE. Nothing about the failure is "numerical".

### 13.4 Reference implementation

```cpp
// returns true  -> CGAL's Extra_data line will really separate the branches (safe to call
//                  Construct_curve_2 on this supporting conic)
// returns false -> CGAL will store a chord: assertion failure in debug, silent wrong answers
//                  under NDEBUG.  (Also returns false for a degenerate conic, N == 0.)
template <class Rational>
bool cgal61_hyperbolic_axis_is_sound(const Rational& r, const Rational& s, const Rational& t,
                                     const Rational& u, const Rational& v, const Rational& w)
{
  const Rational det = 4*r*s - t*t;
  if (CGAL::sign(det) != CGAL::NEGATIVE) return true;        // not a hyperbola: not our problem
  const Rational N = det*w - u*u*s - v*v*r + u*v*t;          // == ConicCPA2 z_prime
  const int sN = CGAL::sign(N);
  if (sN == 0) return false;                                 // degenerate hyperbola = line pair
  const Rational R = sN*r, S = sN*s, T = sN*t;
  if (CGAL::sign(T) == CGAL::ZERO) return true;              // correct special branch
  const Rational P = R+S, A = (R-S)*(R-S), B = T*T, E = B-A;
  const int sP = CGAL::sign(P), sE = CGAL::sign(E);
  const int q = (sP*sE <= 0) ? (sP != 0 ? sP : -sE)
                             : sP * CGAL::sign(P*P*(A+B) - E*E);
  return q <= 0;
}
```

Pure integer/rational arithmetic, no square roots, no `Algebraic`, degree ≤ 6 in the input
coefficients — cheap enough to call on every curve in a Cython binding before touching CGAL.

### 13.5 **[verified]** systematic sweep — 82 cases, 0 disagreements

Three systematic families (not random), each also built as a *narrow* arc (does not straddle the
chord) and a *wide* arc (straddles it):

| family | instances |
|---|---|
| `x·y = 1` rotated by `θ = 2·atan(m)`, `m ∈ {0,1/8,1/5,1/4,1/3,2/5,1/2,3/5,2/3,3/4,1}` | 22 |
| `x²/a² − y²/b² = 1`, `(a,b) ∈ {(2,1),(1,2),(3,1)}`, rotated by `m ∈ {0,1/16,1/8,1/4,1/3,1/2,2/3,1}` | 48 |
| rational quadratic Béziers with `w₁² > w₀w₂` (§12.1), 8 different control/weight sets | 8 |
| translated `(3,−2)`, globally negated, scaled ×7, opposite branch (same conic) | 4 |

All endpoints/branch samples are exact rationals verified to satisfy the conic equation in ℚ.
For every case the stored `Extra_data` line was classified **exactly** (evaluating
`sign(a·x + b·y + c)` as `CORE::Expr` at 26–41 rational points spread along the arc's branch:
"separating" if all signs agree and none is 0, "chord" otherwise).

```
                                        predicate    measured line     debug build      -O2 -DNDEBUG
 49 cases                               SAFE         separating (49)   builds, correct  builds, correct
 33 cases                               UNSAFE       chord      (33)   16 assert        3 invalid,
                                                                       17 build         13 build (10 wrong)
 --------------------------------------------------------------------------------------------------
 predicate / measurement disagreements: 0 / 82        (both builds)
```

* The 16 debug aborts: 15 × `CGAL_assertion(side == cv.sign_of_extra_data(target.x(), target.y()))`
  (`Arr_conic_traits_2.h:3403`), 1 × `CGAL_assertion(side != ZERO)` (`:3400`, the source happened to
  lie exactly on the chord — the Bézier `P₀=(0,0), P₁=(1,2), P₂=(3,1), w=(1,2,1)`).
* **CGAL 6.1 cannot be told to survive these.** `CGAL::set_error_behaviour(CGAL::CONTINUE)` is
  ignored: `assertion_fail()` (`assertions_impl.h:161–174`) falls through `case CONTINUE:` into
  `throw Assertion_exception` ("*The CONTINUE case should not be used anymore*"), and the
  header-only default is already `THROW_EXCEPTION`, not `ABORT`. **[verified]** To get past the
  assertion you must compile with `-DNDEBUG`, `-DCGAL_NDEBUG`, `-DCGAL_NO_ASSERTIONS`, or build
  with `-DCGAL_ENABLE_DISABLE_ASSERTIONS_AT_RUNTIME` and call
  `CGAL::set_use_assertions(false)` around the call (thread-local; it gates `CGAL_assertion` **and**
  `CGAL_precondition`, `assertions.h:116/187`). **[verified]** the latter works and is scoped.
* Precise `-DNDEBUG` outcomes for the 16 straddling arcs: **3** come back `is_valid() == false`
  (the parabola/hyperbola finiteness check at `Arr_conic_traits_2.h:3129–3136` uses the bogus line
  and rejects both `points_at_x`/`points_at_y` candidates); **13** come back `is_valid() == true`
  and wrong. Of those 13, **10** silently **lose** the intersection with a segment that provably
  crosses the arc, **4** of those 10 additionally make `Compare_y_at_x_2(p, xcv)` return `SMALLER`
  for a point `p` that lies exactly on the arc (the `PLUS_SQRT_DISC_ROOT` flag was chosen from the
  wrong branch — the x-monotone arc now evaluates the *other* nappe), and the remaining 3 pass both
  of those probes while still storing a chord.
* The 17 UNSAFE arcs that *do* build (in either build mode) are self-consistent — every point of the
  arc is still reported as being on it, `Make_x_monotone_2`, `Split_2`, `Trim_2`,
  `Arrangement_2::is_valid()` and the vertex/edge counts all look right. **They are still wrong**;
  see 13.6.

### 13.6 What actually goes wrong for arcs that *do* build **[verified]**

A chord through the centre cuts each branch exactly once. If the arc does not contain that
crossing point, both endpoints land on one side, no assertion fires, and every point *of the arc*
still passes the test. The damage is on the **other branch**: the part of it beyond the chord
crossing has the same sign as the arc, so `is_strictly_between_endpoints()` accepts it, and only
the subsequent `orientation(source, p, target)` turn test stands between you and a wrong answer.

Concrete, reproducible case — `x·y = 1` rotated by `(c,s) = (4/5,3/5)`
(coefficients `(−12/25, 12/25, 7/25, 0, 0, −1)`), arc from `(0.2,1.4)` to `(1.3,1.6)`
(parameters `m = 1 … 2`), predicate says UNSAFE, arc builds in **both** build modes:

| query | truth | CGAL 6.1 | after the 13.8(f) patch |
|---|---|---|---|
| `contains_point(xcv, (0.297…, −1.563…))` (other branch) | `false` | **`true`** | `false` |
| `Compare_y_at_x_2` at 133 other-branch points | never `EQUAL` | **2 wrongly `EQUAL`** | 0 |
| `Intersect_2(arc, segment through that point)` | 0 points | **1 point** | 0 points |
| `Arrangement_2` of that arc + that segment (`V/E/F`) | `4/2/1` | **`5/4/1`** | `4/2/1` |
| `Intersect_2(arc, circle r²=1/100 around that point)` | 0 points | **1 point** | 0 |
| `Arrangement_2` of arc + that circle (`V/E/F`) | `4/3/2` | **`5/5/2`** | `4/3/2` |
| `Intersect_2(arc, circle r²=1/100 around a point of the arc)` | 2 points | 2 points ✓ | 2 ✓ |
| `Arrangement_2` of arc + that circle (`V/E/F`) | `6/7/3` | `6/7/3` ✓ | ✓ |

**`arr.is_valid()` returns `true` for the corrupted arrangements.** The arrangement's own
validity check cannot see the problem: it is a *geometric* lie told by the traits, and the DCEL is
internally consistent. A binding must not rely on `is_valid()` to catch this.
For the wide/straddling variant of the same conic under `-DNDEBUG`, 36 of 133 other-branch points
are reported as lying on the arc.

Where the axis is consulted (so you can see how far the damage reaches):

| site | file:line | effect of a wrong axis |
|---|---|---|
| `is_strictly_between_endpoints` | `Arr_conic_traits_2.h:3248–3268` | the primitive that is wrong |
| `is_between_endpoints` | `:3232` | ditto |
| `contains_point(xcv,p)` | `:3774–3793` | wrong point-on-arc answers |
| `Compare_y_at_x_2::operator()` | `:307` (`contains_point`) | wrong `EQUAL` / non-`EQUAL` |
| `set_x_monotone` root choice | `:3680, :3688` | **wrong `PLUS_SQRT_DISC_ROOT`** ⇒ the x-monotone arc evaluates the *other* nappe |
| `Intersect_2` result filter | `:1394–1396` | intersections invented or dropped |
| overlap detection | `:1075–1104` | wrong overlaps |
| finiteness check in `set()` | `:3129` via `:3578/:3602` | arc silently marked invalid |
| vertical/horizontal tangency filters | `:3887, :3963` | wrong `Construct_bbox_2`, wrong x-monotone splits |
| 5-point constructor's interior checks | `:2446–2448` | valid input rejected |

### 13.7 Which construction paths reach the bug (call graph)

`build_hyperbolic_arc_data()` is called from exactly one place, `Traits::set()`
(`Arr_conic_traits_2.h:3124–3127`), guarded by `sign(4rs − t²) == NEGATIVE`. `Traits::set()` is
reached from **every** `Construct_curve_2` overload that produces an arc:
`:2280` (6 coefficients + orientation + endpoints), `:2361` (3 rational points),
`:2438` (5 rational points), `:2613` (**the §2.6 "approximate endpoints + two auxiliary conics"
overload**), `:2688` (`Rat_circle_2` + endpoints), `:2787` (`Rat_segment_2`). The only two calls
that avoid it are `set_full()` (`:2255`, `:2723`) — and `set_full` is ellipses/circles only.

Therefore:

* **(b) `Construct_x_monotone_curve_2` does not help.** All four of its curve-bearing overloads take
  an already-built `Curve_2` (`:2081, :2092, :2105`) and only run `set_x_monotone`; the two
  remaining overloads build *special segments* (straight lines), not conics.
* **(c) the §2.6 auxiliary-conic overload does not help.** **[verified]**: it asserts at `:3403`
  in a debug build on an UNSAFE hyperbola, exactly like the plain overload, and produces the same
  wrong axis under `-DNDEBUG`.
* **(d) subdividing the arc does not help** — subdivision does not change the supporting conic.
  In particular splitting a rational quadratic Bézier at `τ = 1/2` yields two rational quadratic
  Béziers **on the same hyperbola**. Only a change of *supporting conic* (approximating by
  elliptic/parabolic pieces, i.e. giving up exactness) removes the hyperbola.
* **(a) restricting the arc to one branch / one half-plane does not fix it.** Every arc in the
  sweep already lies on a single branch, and the 17 arcs that build already lie entirely on one side
  of the chord — that is *why* they build, and they are still wrong (13.6).

### 13.8 Workarounds that do work

Ordered by how much of CGAL they leave untouched.

**(e) Refuse, and rotate the whole scene.** Because the predicate is invariant under everything
except rotation, and the bad set of angles has measure `2γ/π < 1`, a global rational rotation
`(c,s) = ((1−m²)/(1+m²), 2m/(1+m²))` applied to **all** curves of the arrangement can always make a
given hyperbola SAFE; the arrangement is rigid-motion equivariant, so you rotate the results back.
**[verified]** the straddling case of 13.6 is UNSAFE at `m = 0` and SAFE at `m = 1/2`
(`θ += 53.13°`); in the rotated frame the arc builds, 0 of 133 other-branch points are accepted,
and the arrangement counts are correct. Drawback: with many hyperbolas you must find one `m` that
satisfies all of them (search a few dozen small `m`; each hyperbola forbids a set of measure
`2γᵢ/π`), and every input/output coordinate becomes a rotated rational.

**(f) Patch `Extra_data` after construction.** `Conic_arc_2::set_extra_data(a,b,c,side)` is
**public** (`Conic_arc_2.h:1645`, in the `public:` block at `:1601` labelled "*only friends have the
privilege to use*"), and the x-monotone arcs deep-copy the base's `Extra_data`, so replacing the
line on the `Curve_2` **before** calling `Make_x_monotone_2` fixes everything downstream.

```cpp
// exact conjugate axis of the arc's supporting hyperbola (what CGAL meant to compute)
void true_axis(const Curve_2& cv, Algebraic& a, Algebraic& b, Algebraic& c) {
  const int of = (cv.orientation()==CGAL::CLOCKWISE) ? -1 : 1;
  Algebraic R=nt.convert(Integer(of*cv.r())), S=nt.convert(Integer(of*cv.s())),
            T=nt.convert(Integer(of*cv.t())), U=nt.convert(Integer(of*cv.u())),
            V=nt.convert(Integer(of*cv.v()));
  Algebraic D = nt.sqrt((R-S)*(R-S)+T*T), c2 = (R-S)/D, one(1), two(2), cosp, sinp;
  if (CGAL::sign(T)==CGAL::ZERO) {
    if (CGAL::sign(c2)==CGAL::POSITIVE) { cosp=one; sinp=Algebraic(0); }
    else                                { cosp=Algebraic(0); sinp=one; }
  } else {                                 // <-- the two lines CGAL gets backwards
    cosp = nt.sqrt((one+c2)/two);  if (CGAL::sign(T)==CGAL::NEGATIVE) cosp = -cosp;
    sinp = nt.sqrt((one-c2)/two);
  }
  Algebraic det=4*R*S-T*T, x0=(T*V-two*S*U)/det, y0=(T*U-two*R*V)/det;
  a=cosp; b=sinp; c=-(cosp*x0+sinp*y0);
}
bool patch_axis(Curve_2& cv) {                       // returns false if the endpoints are not
  Algebraic a,b,c; true_axis(cv,a,b,c);              // on one branch (genuinely bad input)
  CGAL::Sign ss = CGAL::sign(a*cv.source().x()+b*cv.source().y()+c);
  CGAL::Sign st = CGAL::sign(a*cv.target().x()+b*cv.target().y()+c);
  if (ss == CGAL::ZERO || ss != st) return false;
  delete const_cast<Curve_2::Extra_data*>(cv.extra_data());   // set_extra_data LEAKS otherwise
  cv.set_extra_data(a,b,c,ss);
  return true;
}
```

**[verified]** on every UNSAFE case that builds: false positives drop from 2/133 (narrow) and
36/133 (wide, `-DNDEBUG`) to **0/133**, `Intersect_2` and the arrangement counts become correct
(13.6, last column). Caveats: (i) `set_extra_data` `new`s without freeing the old block —
delete it yourself, as above; (ii) it only helps if the `Curve_2` survived `set()`, so you still
need assertions disabled (13.5) **and** you must re-check `is_valid()` — 3/16 straddling arcs come
back invalid and cannot be revived (`set_flag` is `protected`).

**(g) Bypass `Traits::set()` entirely — the recommended fix.** All the setters needed to build a
`Curve_2` by hand are public (`set_coefficients`, `set_orientation`, `set_endpoints`,
`set_extra_data`); only `set_flag`/`reset_flag` are `protected` (`Conic_arc_2.h:1577–1600`) and a
one-line derived class re-exposes them. This never enters `build_hyperbolic_arc_data`, so it works
in a plain debug build with all assertions on.

```cpp
struct Arc_hack : public Curve_2 { using Curve_2::set_flag; using Curve_2::reset_flag; };

bool build_hyperbolic_arc(const Rational& r,const Rational& s,const Rational& t,
                          const Rational& u,const Rational& v,const Rational& w,
                          CGAL::Orientation orient,
                          const Point_2& src,const Point_2& tgt, Curve_2& out)
{
  // 1. your own validity checks: endpoints on the conic, src != tgt, 4rs-t^2 < 0, N != 0,
  //    and (for a hyperbola) that both endpoints are on the same branch.
  Rational rc[6] = {r,s,t,u,v,w};  Integer ic[6];
  nt.convert_coefficients(rc, rc+6, ic);                  // same integerisation CGAL uses
  const int oi = (orient==CGAL::CLOCKWISE) ? -1 : 1;      // = ConicCPA2 orientation wanted
  const int f  = (oi == sign_of_N) ? 1 : -1;              // reproduce set()'s negation exactly
  Arc_hack h;
  h.set_coefficients(f*ic[0],f*ic[1],f*ic[2],f*ic[3],f*ic[4],f*ic[5]);
  h.set_orientation(orient);
  h.set_endpoints(src,tgt);
  Algebraic a,b,c; true_axis(h,a,b,c);                    // as in (f)
  CGAL::Sign ss = CGAL::sign(a*src.x()+b*src.y()+c);
  if (ss==CGAL::ZERO || ss!=CGAL::sign(a*tgt.x()+b*tgt.y()+c)) return false;
  h.set_extra_data(a,b,c,ss);
  h.set_flag(Curve_2::IS_VALID);                          // IS_VALID = 0 -> mask 0x1
  h.reset_flag(Curve_2::IS_FULL_CONIC);
  out = h;                                                // slices; the copy ctor deep-copies
  return true;                                            // Extra_data and copies m_info
}
```

**[verified] end-to-end, in a debug build with assertions on**, on the narrow *and* the straddling
arcs of 13.6 and on a SAFE control case:
* the hand-built integer coefficients are **bit-identical** to the ones `Traits::set()` stores
  (checked against CGAL's own output on every case where CGAL could build the arc);
* 0/133 false positives, 0/15 missed on-arc points, `Make_x_monotone_2` gives the same piece count,
  `Compare_y_at_x_2` is `EQUAL` on the arc, `Split_2`/`Trim_2` succeed,
  `Arrangement_2` counts and `is_valid()` are correct;
* the straddling arc that *aborts* in `Construct_curve_2` builds and behaves correctly here.

`set()` does five other things you must reproduce yourself if you take this route
(`Arr_conic_traits_2.h:3049–3143`): integerisation, the orientation-driven negation, "both
endpoints satisfy the conic equation" (⇒ invalid), the `COLLINEAR` line-pair midpoint test, and the
finiteness test. For a hyperbolic arc whose endpoints you know are on one branch, the finiteness
test is automatically satisfied.

### 13.9 Recommended recipe for the binding

1. Compute `sign(4rs − t²)`. `> 0` ellipse, `= 0` parabola → nothing to do, use
   `Construct_curve_2` as documented in §2/§12.
2. `< 0` (hyperbola): compute `N = (4rs−t²)w − su² − rv² + tuv`.
   `N == 0` ⇒ degenerate line pair — reject, or pass `orient = COLLINEAR` and hand CGAL two
   segments instead.
3. Check that source and target are on the same branch **yourself** (rational test: both endpoints
   on the same side of the exact conjugate axis; or, equivalently, that the segment from source to
   target does not cross the "empty" region). CGAL's only check is the assertion that fires too
   late.
4. Run `cgal61_hyperbolic_axis_is_sound()` (13.4).
   * **true** → `Construct_curve_2(r,s,t,u,v,w,orient,src,tgt)` is safe; still check `is_valid()`.
   * **false** → use 13.8(g) (hand-built arc, no assertions disabled anywhere), or 13.8(e) if you
     prefer to keep every curve going through CGAL's own constructor.
5. Never trust `arr.is_valid()` to catch an axis problem (13.6).

If you want a belt-and-braces runtime check after construction, evaluate
`sign_of_extra_data` at one interior point of the arc and at its central reflection
`2·centre − point` (which is on the other branch): a correct axis always gives opposite non-zero
signs, and the point *on the arc* must match `extra_data()->side`.

### 13.10 Is `Arr_algebraic_segment_traits_2` a usable fallback here? **Yes.** **[verified]**

```cpp
#include <CGAL/Arithmetic_kernel.h>
#include <CGAL/Arr_algebraic_segment_traits_2.h>
typedef CGAL::Arithmetic_kernel                       AK;      // no CORE needed
typedef AK::Integer                                   Integer; // = boost::multiprecision::mpz_int
typedef CGAL::Arr_algebraic_segment_traits_2<Integer> Traits;  // one template parameter
typedef CGAL::Arrangement_2<Traits>                   Arrangement_2;
```

* **Instantiates and links out of the box in this installation.** `AK::Integer` resolves to
  `boost::multiprecision::number<gmp_int>` (the GMP arithmetic kernel — CORE is not involved).
  Internally: `Algebraic_kernel_d_1<Coefficient>` → `Algebraic_curve_kernel_2` →
  `Curved_kernel_via_analysis_2` (`Arr_algebraic_segment_traits_2.h:50–55`).
  Compile ≈ 4 s at `-O0`; the test programs run in ~20 ms.
* Curves are integer bivariate polynomials, so a conic is
  `ctr(r*x*x + s*y*y + t*x*y + u*x + v*y + w)` after clearing denominators — **no orientation
  argument, no endpoints, no sign convention, and no `Extra_data`**: the branch structure is
  recovered by curve analysis, so *the bug does not exist here*.
* **[verified]** full hyperbola `x·y − 1` + line `y − x − 1` → arrangement with the two irrational
  intersection points `(−1.618…, −0.618…)`, `(0.618…, 1.618…)`, `is_valid() == true`.
* **[verified]** bounded arcs work too, via
  `construct_x_monotone_segment_2_object()(curve, p, q, out)` with
  `construct_point_2_object()(Bound, Bound)`: the *rotated* hyperbola
  `−12x² + 12y² + 7xy − 25` (the exact conic that breaks the conic traits) between
  `(1/5,7/5)` and `(13/10,8/5)` gives **1** x-monotone piece, and inserting it together with a
  segment crossing only the other branch gives `V/E/F = 4/2/1, is_valid() == true` — the truth.
* Costs of switching: a different `Point_2`/`X_monotone_curve_2`/`Curve_2` type family
  (`Curve_analysis_2`, `Arc_2`, algebraic points), unbounded curves are supported so the traits'
  side categories are *not* oblivious (`Arrangement_2` grows fictitious vertices/edges and
  `number_of_faces()` counts differ from the conic traits), functors are served from a **process-wide
  singleton** `CKvA_2::instance()` (`:200, :542, :611`) rather than from your traits object — a real
  hazard for a multi-threaded or multi-tenant binding — and endpoints must lie *exactly* on the
  curve or you hit an internal `CGAL_assertion(! end)` (`:377`) rather than an error return.
  **[verified]** that assertion is exactly what an off-curve endpoint produces.

Practical reading: keep `Arr_conic_traits_2` for ellipses/parabolas/circles/segments, and treat
`Arr_algebraic_segment_traits_2` as the exact fallback for hyperbolic input if you would rather not
maintain the 13.8(g) bypass. Do not try to mix the two in one arrangement.

### 13.11 Two side findings from these experiments **[verified]**

* **Wrong orientation on a hyperbola ⇒ invalid arc, not the complementary arc.** For
  `x·y − 1 = 0` with `source = (1,1)`, `target = (2,1/2)`, `COUNTERCLOCKWISE` gives a valid arc and
  `CLOCKWISE` gives `is_valid() == false` — the complementary traversal is unbounded, so the
  finiteness check rejects it. (For an ellipse both orientations are valid and give the two
  complementary arcs.) Do not "try the other orientation" as an error-recovery strategy without
  re-checking `is_valid()`.
* **Never let a `Point_2`/`Curve_2` (anything holding a `CORE::Expr`) outlive `main()`.** A
  namespace-scope `static Traits::Curve_2` reproducibly aborts at process exit with
  `CGAL error: assertion violation! Expression : ! blocks.empty()` in
  `CGAL/CORE/MemoryPool.h:125` (the CORE memory pool is destroyed before the objects that use it);
  under `-DNDEBUG` it instead prints a long list of leaked `CORE::…Rep` type names. Minimal repro:
  one global `Curve_2` assigned inside `main`. For a Python extension this means the traits and all
  curve/point objects must be owned by objects that are destroyed before interpreter shutdown, or
  deliberately leaked.
