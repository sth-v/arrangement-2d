# CGAL 6.1 — the exact / approximate coordinate contract (all seven curve kinds)

**Scope.** One consolidated, compiled-and-run contract for the *only* two operations a Cython
binding really needs per traits class:

* **EXTRACTION** — arrangement `Vertex → Python`: an exact rational `(numerator, denominator)`
  decimal string pair when the coordinate *is* rational, a **certified** rational interval
  `[lo, hi]` of a caller-chosen width when it is not, and a `double` pair.
* **CONSTRUCTION** — `Python → Traits::Point_2`: how to build a point from user-supplied exact
  rationals, and exactly what happens when the desired point cannot be represented.

This file supersedes the scattered coverage in `number_types_and_errors.md` §4,
`traits_circle_segment.md` §2, `traits_conic.md` §6, `traits_bezier.md` §10.3 and
`traits_geodesic_sphere.md` §1. Those documents remain the reference for everything *else* about
their traits; this one is the single source of truth for coordinates.

**Installation.** `/opt/homebrew/include/CGAL`, CGAL **6.1** (`CGAL_VERSION_NR 1060101000`),
header-only; CORE under `CGAL/CORE`; GMP/MPFR from Homebrew.

**Every claim marked [verified] was produced by compiling and running a program with**

```
/usr/bin/clang++ -std=c++17 -O0 -DCGAL_USE_CORE -DCGAL_USE_GMP -DCGAL_USE_MPFR \
  -I/opt/homebrew/include -L/opt/homebrew/lib -lgmp -lmpfr -o test test.cpp
```

with **assertions and preconditions ON** (`NDEBUG` and `CGAL_NO_PRECONDITIONS` both undefined —
[verified] by `#ifdef` probe). Claims marked **[header]** are quoted verbatim from the installed
header with file:line. Every code block in §§2–8 is copied out of a program that compiled and ran;
the two programs are `recipes_a.cpp` (Epeck-based traits) and `recipes_b.cpp` (CORE-based traits).

---

## Gotchas / surprises (read this before writing any binding code)

1. **`CORE::Expr` comparison can return a FALSE `EQUAL`, and the conic traits produces exactly
   such numbers.** [verified, first operation on a fresh arrangement, reproducible]
   For the arrangement of `x²+y²=2` with the segment `y=0`, take the vertex at `x = +√2` and
   `r = 57715667483393580961165483335871838914149456886446877857294614485577266014763425361 /
       40811139858215520877681994491572916756295496376319821447870175759964125455372247881`
   (a convergent of √2, `p² − 2q² = −1`, so `r ≠ √2` — checked in pure rational arithmetic):

   | expression | result | correct? |
   |---|---|---|
   | `X.cmp(Algebraic(r))` | `0` (EQUAL) | **NO** |
   | `(X - Algebraic(r)).sign()` | `0` | **NO** |
   | `X.cmp(sqrt(Expr(2)))` | `0` | yes |
   | `sqrt(Expr(2)).cmp(Algebraic(r))` | `1` | yes |
   | `(X*X - 2).sign()` | `0` | yes |
   | `(r*r - 2)` in `mpq` | `< 0` | yes |

   Transitivity is violated. Independently: the certified 800-bit enclosure `[lo,hi]` of `X`
   satisfies `lo² < 2 < hi²` and `r < lo` (pure `mpq` comparisons), so `X ≠ r` — yet CORE says
   equal. The identical comparison on a *freshly built* `CGAL::sqrt(Expr(2))`, on
   `Nt_traits::solve_quadratic_equation(1,0,-2)` roots, and on those after
   `approx(200|800|2000)` all answer **correctly** [verified] — so the defect is specific to the
   `Expr` nodes the conic traits builds.
   **Consequence: never decide "is this coordinate rational?" by rational reconstruction plus
   `Expr::operator==`.** For conic and Bezier algebraic coordinates the binding may return
   *only* a certified interval. §5.2 gives the safe API shape.

2. **`Approximate_2` is not correctly rounded on any Epeck-based traits.**
   `Arr_segment_traits_2.h:911` is `return (i == 0) ? (CGAL::to_double(p.x())) : …` — that is
   `to_double(Lazy_exact_nt)`, which derives the result from the *interval* approximation.
   [verified] for `x = 1/3`: `Approximate_2(p,0) == to_double(p.x()) == 0.33333333333333337`
   while `to_double(p.x().exact()) == 0.33333333333333331 == (double)(1.0/3.0)`.
   **Always go through `.exact()` for the double you hand to Python.**

3. **A `Sqrt_extension` with `is_extended() == true` may still be a rational number.**
   `check_if_is_extended()` only clears the flag when `a1 == 0` or `root == 0`; a **perfect
   square** root is never simplified. [verified] `CoordNT(1, 3, 4)` (= `1 + 3·√4` = `7`) reports
   `is_extended() == true`. A binding that maps `is_extended()` to "irrational" will hand Python
   an interval where an exact `7/1` was available. Test the root for rational-squareness (§3.1).

4. **Bezier: `Point_2::x()`/`y()` are UB in release on a non-exact point, and arrangement
   vertices routinely *are* non-exact.** `\pre _rep().is_exact()` [header]; the accessor
   dereferences `Algebraic* p_alg_x` which is null. [verified] 4 of 10 vertices in a 3-curve
   arrangement had `is_exact() == false`. Guard with
   `if (!p.is_exact()) p.make_exact(cache);` — and note that `make_exact` **never** makes an
   algebraic point rational ([verified] `is_rational()` stayed `false` for all 4).

5. **Bezier: `Split_2` accepts a rational point that is nowhere near the curve, silently.**
   `Bezier_x_monotone_2.h:1291` is
   `CGAL_precondition(p.get_originator(_curve, _xid) != p.originators_end() || p.is_rational());`
   — the `|| p.is_rational()` short-circuits for *every* free rational point. [verified] feeding
   `Point_2(Rational(1000), Rational(1000))` to `traits.split_2_object()(xcv, p, c1, c2)` throws
   nothing and yields `c1.target()` and `c2.source()` both at `(1000,1000)` — a corrupt DCEL.
   The conic traits, by contrast, **does** catch this
   (`CGAL_precondition(m_traits.contains_point(xcv, p) && …)`, `Arr_conic_traits_2.h:993`,
   [verified] throws).

6. **Geodesic sphere: constructing `Arr_extended_direction_3` directly with the wrong
   `Location_type` is accepted and corrupts the arrangement.** [verified]
   `Point_2(Direction_3(1,0,0), Point_2::MAX_BOUNDARY_LOC)` reports `is_max_boundary() == true`
   (i.e. "this is the north pole") while being the direction `(1,0,0)`; the correct functor gives
   `NO_BOUNDARY_LOC`. **`Construct_point_2` is the only safe route** (§8.2).

7. **Geodesic sphere coordinates are never normalised, and `Approximate_2` does not normalise
   either.** [verified] an intersection vertex came out as the raw triple `(2, 0, 2)` and
   `Approximate_2(p, i)` returned `2, 0, 2`. Only the *curve* overload
   (`operator()(xcv, error, oi)`) normalises, internally, in `double`.

8. **Vertices at infinity (`Arr_linear_traits_2`) have no point at all, and `point()` fires an
   `assertion`, not a precondition.** [verified] `Arr_dcel_base.h:112`,
   `Expression : p_pt != nullptr` → `CGAL::Assertion_exception`. Under `-DNDEBUG` this is a null
   dereference. The good news: `arr.vertices_begin() … vertices_end()` **excludes** them
   ([verified] a single inserted `Line_2` gives `number_of_vertices() == 0` and
   `number_of_vertices_at_infinity() == 2`), so the normal iteration is safe; they are only
   reachable through halfedges. There is **no** `vertices_at_infinity_begin()` on
   `Arrangement_2` [verified compile error].

9. **Extraction mutates the point on three of the seven traits.**
   * conic — `contains_point` does `const_cast<Point_2&>(p).set_generating_conic(xcv.id())`
     (`Arr_conic_traits_2.h:3785` [header]); [verified] `is_generating_conic()` flips `0 → 1`
     through a `const&`.
   * Bezier — `compare_xy` on equal points merges the originator lists **and rebinds the second
     point to the first's rep**; [verified] `p1.is_same(p2)` went `0 → 1` and both originator
     counts went `1 → 2`.
   * circle-segment — `Sqrt_extension::compute_to_interval() const` ends with
     `this->update_cached_value(res);` into a `mutable std::optional` (`Sqrt_extension_type.h:296`,
     `:97-100` [header]). Reached from `to_interval()`, `sign()` and `compare()`. The cache lives
     in the point's **shared** rep, so two threads holding copies of one handle race.
   Only `.a0()/.a1()/.root()/.exact()` (circle-segment, segment family) are free of this.

10. **`Lazy_exact_nt::exact()` *is* thread-safe** — `Lazy.h:351` is
    `std::call_once(once, [this](){this->update_exact();}); return exact_unsafe();`, and
    `CGAL_HAS_THREADS` is `1` in this installation [verified]. It still *mutates* the shared DAG
    node (memoises the exact value) and returns `const ET&` **into that node** — copy out, do not
    hold the reference past the point's lifetime.

11. **`makeFloorExact()` / `makeCeilExact()` are safe on a copy** — `CORE::BigFloat` is
    `RCImpl<BigFloatRep>` and both call `makeCopy()` first (`BigFloat.h:284-304` [header]). The
    `expr_interval` recipe below is therefore sound. But **`CORE::Expr::BigRatValue()` is an
    approximation** (see `number_types_and_errors.md` gotcha 4) — the recipe deliberately goes
    through `BigFloat::BigRatValue()`, which *is* exact because a `BigFloat` is dyadic.

12. **The width you get from `Expr::approx(bits, bits)` is not a function of `bits` alone.**
    [verified] a fresh `√2` gives width `2^-(2·bits+8)` exactly, but four Bezier vertices at
    `bits = 120` gave widths `1.07e-39`, `3.65e-63`, `2.44e-39`, `1.54e-44`. **Never assume;
    loop until the measured width meets the target** (§1.3).

13. **`Arr_linear_traits_2::Approximate_2` has only the `(p, i)` overload** — no
    `operator()(const Point_2&)`, no `Approximate_kernel` / `Approximate_point_2` typedefs
    (`Arr_linear_traits_2.h:1520-1539` [header]), unlike segment / polyline / circle-segment /
    conic / sphere. Generic binding code must not assume the point overload exists.

---

## 0. Master table

`K = CGAL::Exact_predicates_exact_constructions_kernel` (Epeck); `Nt = CGAL::CORE_algebraic_number_traits`.

| # | curve kind | traits | `Traits::Point_2` | coordinate NT | exact accessors | `sizeof(Point_2)` | copy | extraction mutates? |
|---|---|---|---|---|---|---|---|---|
| 1 | segment | `Arr_segment_traits_2<K>` | `CGAL::Point_2<K>` | `K::FT = Lazy_exact_nt<Exact_rational>` | `p.x().exact()`, `p.y().exact()` | **8** | refcounted handle, O(1) | no |
| 2 | linear / ray / line | `Arr_linear_traits_2<K>` | `CGAL::Point_2<K>` | idem | idem | **8** | O(1) | no |
| 3 | polyline | `Arr_polyline_traits_2<Arr_segment_traits_2<K>>` | `CGAL::Point_2<K>` | idem | idem | **8** | O(1) | no |
| 4 | circle-segment | `Arr_circle_segment_traits_2<K, Filter>` | `CGAL::_One_root_point_2<K::FT, Filter>` | `CoordNT = Sqrt_extension<K::FT, K::FT, Tag_true, Boolean_tag<Filter>>` | `p.x().a0()/.a1()/.root()/.is_extended()` then `.exact()` | **8** | COW handle, O(1) | **yes** (interval cache) |
| 5 | conic | `Arr_conic_traits_2<Cartesian<Rational>, Cartesian<Algebraic>, Nt>` | `CGAL::Conic_point_2<Alg_kernel>` | `Algebraic = CORE::Expr` | `p.x()`, `p.y()` → `const Expr&` | **32** | base handle shared + `std::list<Conic_id>` **deep-copied** | **yes** (`contains_point`) |
| 6 | Bezier | `Arr_Bezier_curve_traits_2<…, Nt, …>` | `CGAL::_Bezier_point_2<…>` | `Algebraic = CORE::Expr` (+ optional `Rational`) | `p.x()`, `p.y()` **after `is_exact()`** | **8** | handle, O(1) | **yes** (`compare_xy`, `make_exact`) |
| 7 | geodesic on sphere | `Arr_geodesic_arc_on_sphere_traits_2<K>` | `CGAL::Arr_extended_direction_3<K>` | `K::FT` ×3 (a **direction**, not a point) | `p.dx()/.dy()/.dz()` then `.exact()`; `p.location()` | **16** | `Direction_3` handle (8) + enum, O(1) | no |

All `sizeof` values [verified] on this installation. Companions: `sizeof(K::FT) == 16`,
`sizeof(Exact_rational) == 32`, `sizeof(CORE::Expr) == 8`, `sizeof(CoordNT) == 80` when
`Filter == true` and **56** when `Filter == false` (the 24-byte `std::optional<pair<double,double>>`
interval cache), `sizeof(Alg_kernel::Point_2) == 8`, `sizeof(K::Direction_3) == 8`.

**Caching advice.** Six of the seven `Point_2` types are one-pointer handles: cache the *point*,
never the coordinates. The exception is `Conic_point_2` (32 B, and its `std::list<Conic_id>` is
deep-copied on every copy — [verified] mutating a copy's id list leaves the original untouched);
copy it once into your own struct and work from there, and never store an array of `Conic_point_2`
behind an `Alg_point_2*` (different strides — `traits_conic.md` gotcha 13).

**Which of the three outputs is achievable, per traits:**

| traits | (a) exact `num/den` | (b) certified `[lo,hi]` of chosen width | (c) `double` pair |
|---|---|---|---|
| segment / linear / polyline | **always** | trivially (degenerate) | yes |
| circle-segment | when `a1 == 0`, `root == 0`, or `root` is a rational square | **yes**, width `≤ \|a1\|·2^-bits` | yes |
| conic | **never decidable** (gotcha 1) — only for coordinates *you* constructed | **yes**, loop to any width | yes |
| Bezier | **yes when `is_rational()`** (via `operator Rat_point_2`) | **yes** when `is_exact()`; plus a free rational bbox always | yes (`approximate()`, never throws) |
| sphere | **always** (the raw direction is rational); the *unit* coordinates are algebraic | **yes** for the unit coordinates | yes |

---

## 1. Shared primitives (used by every section below)

### 1.1 Exact rational ⇄ decimal strings

```cpp
using ER = CGAL::Exact_rational;   // == CORE::BigRat == boost mpq_rational
using EI = CGAL::Exact_integer;    // == CORE::BigInt == boost mpz_int

struct ExactRat { std::string num, den; };                 // canonical, den > 0, gcd == 1

static ExactRat rat_to_strings(const ER& q) {
  CGAL::Fraction_traits<ER>::Decompose dec; EI n, d; dec(q, n, d);
  return { n.str(), d.str() };
}
static ER rat_from_strings(const std::string& n, const std::string& d) {
  EI N(n), D(d);                                            // throws std::runtime_error on bad syntax
  if (D.is_zero()) throw std::invalid_argument("zero denominator");
  CGAL::Fraction_traits<ER>::Compose comp; return comp(N, D);   // canonicalises
}
```

**[verified]** round-trips `1/3`, `-7/2`, `22/7`. Reminder from `number_types_and_errors.md`:
`ER("0.125")` **throws** — only `"n"` and `"n/d"` are valid for the string constructor; use
`is >> CGAL::IO::iformat(q)` for decimal text.

Under CORE the same two functions work unchanged because `Nt_traits::Rational` **is**
`CGAL::Exact_rational` and `Nt_traits::Integer` **is** `CGAL::Exact_integer`
(`traits_conic.md` gotcha 8).

### 1.2 Certified rational enclosure of `sqrt(r)` — for circle-segment and for sphere normalisation

```cpp
// [lo,hi] with lo <= sqrt(r) <= hi and hi-lo <= 2^-bits; exact (lo == hi) when sqrt(r) is rational.
// \pre r >= 0
static std::pair<ER,ER> sqrt_interval(const ER& r, unsigned bits) {
  if (CGAL::is_zero(r))     return { ER(0), ER(0) };
  if (CGAL::is_negative(r)) throw std::domain_error("sqrt of negative");
  EI n = numerator(r), d = denominator(r), N = EI(1) << bits;
  EI rn = n * N * N;
  EI t  = rn / d;                                   // floor(r * N^2)
  EI m  = boost::multiprecision::sqrt(t);           // floor(sqrt(t))   [mpz_sqrt, exact]
  if (rn == t * d && m * m == t) return { ER(m, N), ER(m, N) };   // sqrt(r) is rational
  return { ER(m, N), ER(m + 1, N) };                // width == 1/N == 2^-bits
}
```

Correctness: `m ≤ √t < m+1` and `t ≤ r·N² < t+1`, hence `m/N ≤ √r` and
`√r < √(t+1)/N ≤ (m+1)/N`. **[verified]** at `bits = 100`: width `7.89e-31 == 2^-100` for √2,
and `sqrt_interval(4, 100)` returns the degenerate `[2,2]`.

Needs `#include <boost/multiprecision/integer.hpp>` for `boost::multiprecision::sqrt` on `mpz_int`.

### 1.3 Certified rational enclosure of a `CORE::Expr` — for conic and Bezier

```cpp
// [lo,hi] with lo <= e <= hi and hi-lo <= max_width.  Never claims equality (gotcha 1).
static std::pair<Rational,Rational>
expr_interval(const Algebraic& e, const Rational& max_width,
              long start_bits = 64, long max_bits = 1L << 20) {
  for (long b = start_bits; ; b *= 2) {
    e.approx(b, b);                                  // monotone: only ever refines, never coarsens
    CORE::BigFloat bf = e.BigFloatValue();           // carries an error term
    CORE::BigFloat lo = bf; lo.makeFloorExact();     // BigFloat is COW: makeCopy() inside
    CORE::BigFloat hi = bf; hi.makeCeilExact();
    Rational rlo = lo.BigRatValue(), rhi = hi.BigRatValue();   // BigFloat->BigRat IS exact (dyadic)
    if (rhi - rlo <= max_width) return { rlo, rhi };
    if (b > max_bits) throw std::runtime_error("expr_interval: precision limit");
  }
}
```

The loop is mandatory — see gotcha 12. **[verified]** at `max_width = 1e-30`: conic √2 vertex
converges to width `8.41e-45`; Bezier vertices to `1.07e-39 … 3.65e-63`; rational vertices to the
degenerate `[-3,-3]`, `[3,3]`, width `0`.

Relevant declarations [header]:

```cpp
// CORE/Expr.h
const Real & approx(const extLong& relPrec = get_static_defRelPrec(),      // default 60
                    const extLong& absPrec = get_static_defAbsPrec()) const; // default +inf
BigFloat BigFloatValue() const;              // the current approximation, WITH an error term
BigRat   BigRatValue()   const;              // *** APPROXIMATION — do not use ***
// CORE/BigFloat.h:284-304
BigFloat& makeExact();      // makeCopy(); rep->err = 0
BigFloat& makeCeilExact();  // makeCopy(); rep->m += rep->err; rep->err = 0
BigFloat& makeFloorExact(); // makeCopy(); rep->m -= rep->err; rep->err = 0
BigRat    BigRatValue() const;               // EXACT (a BigFloat is a dyadic rational)
bool      isExact() const;                   // err() == 0
```

### 1.4 Doubles

Use the exact value, never the lazy/filtered one (gotcha 2):

```cpp
double d_epeck (const K::FT& x)      { return CGAL::to_double(x.exact()); }         // correctly rounded
double d_expr  (const Algebraic& x)  { return CGAL::to_double(x); }                 // approx(53,1075) then doubleValue()
// circle-segment: CGAL::to_double(CoordNT) == to_double(a0)+to_double(a1)*std::sqrt(to_double(root))
```

`CGAL::to_interval(x)` on `Exact_rational` is a **certified, tight MPFR enclosure**;
on `Lazy_exact_nt` it is only the *current* interval — pass `x.exact()`.
**[verified]** both give `[0.33333333333333331, 0.33333333333333337]` for `1/3` once `.exact()` is
forced. `CGAL::to_interval(CoordNT)` is certified too ([verified] `[1.4142135623730949,
1.4142135623730951]` for √2) — but it writes the mutable cache (gotcha 9).

---

## 2. Segment / linear / polyline — `CGAL::Point_2<Epeck>`

All three traits share one `Point_2` [verified `typeid` identity]:

```cpp
// Arr_segment_traits_2.h:231     typedef typename Kernel::Point_2        Point_2;
// Arr_linear_traits_2.h:499      typedef typename Kernel::Point_2        Point_2;
// Arr_polyline_traits_2.h:84     typedef typename Base::Point_2          Point_2;   // = subtraits'
```

Accessors used ([header], `CGAL/Point_2.h`):

```cpp
decltype(auto) x() const;            // -> FT  BY VALUE for Epeck  [verified: not an lvalue ref]
decltype(auto) y() const;
decltype(auto) cartesian(int i) const;   // CGAL_kernel_precondition(i == 0 || i == 1)
template <typename T1, typename T2> Point_2(T1&& x, T2&& y);   // anything convertible to FT
```

and (`CGAL/Lazy.h:351`) `const ET& CGAL::Lazy_exact_nt<ET>::exact() const;`.

### 2.1 EXTRACTION — [verified]

```cpp
static ExactRat epeck_x(const K::Point_2& p) { return rat_to_strings(p.x().exact()); }
static ExactRat epeck_y(const K::Point_2& p) { return rat_to_strings(p.y().exact()); }
static std::pair<double,double> epeck_xy_double(const K::Point_2& p) {
  return { CGAL::to_double(p.x().exact()), CGAL::to_double(p.y().exact()) };
}
// certified interval: degenerate, the value IS rational
static std::pair<ER,ER> epeck_x_interval(const K::Point_2& p)
{ ER v = p.x().exact(); return { v, v }; }
```

**Every** vertex coordinate of these three traits is an exact rational — there is no irrational
case. [verified] on the intersection of the diagonals of a unit square: `(3/2, 3/2)`.

Case (b) — a certified interval of caller-chosen width — is therefore always the degenerate
`[v, v]`; return that, do not fabricate a wider one.

**Mutation / thread-safety.** `.exact()` memoises into the shared `Lazy` DAG node but is guarded by
`std::call_once` with `CGAL_HAS_THREADS == 1` [verified], so concurrent extraction from two threads
is safe. Nothing else is written. `x()` returns **by value** here; the `const ET&` from `.exact()`
points into the DAG node — copy it out (`ER v = p.x().exact();`) before the point dies.

**Handle lifetime.** `sizeof(K::Point_2) == 8` and a copy shares the rep
([verified] `a.rep().identical(b.rep()) == true`). Cache handles freely; they keep the coordinates
alive.

### 2.2 CONSTRUCTION — [verified]

```cpp
static K::Point_2 epeck_point(const std::string& xn, const std::string& xd,
                              const std::string& yn, const std::string& yd) {
  return K::Point_2(FT(rat_from_strings(xn, xd)), FT(rat_from_strings(yn, yd)));
}
```

**Nothing can fail to be represented.** Any `p/q` with `q ≠ 0` is exactly a `Point_2`. The only
error is a zero denominator or unparsable digits, both raised by `rat_from_strings`.
[verified] `epeck_point("1","3","-7","2")` round-trips to `(1/3, -7/2)` and to
`(0.333333…, -3.5)`.

### 2.3 Linear traits only — vertices at infinity

```cpp
for (auto v = arr.vertices_begin(); v != arr.vertices_end(); ++v)
    /* v->point() is ALWAYS safe here */;
```

**[verified]** `vertices_begin()…vertices_end()` iterates only the finite vertices;
`arr.number_of_vertices_at_infinity()` counts the rest, and there is **no**
`vertices_at_infinity_begin()` on `Arrangement_2`. If you reach one through a halfedge
(`e->source()->is_at_open_boundary()`), calling `point()` throws
`CGAL::Assertion_exception("p_pt != nullptr", Arr_dcel_base.h:112)` [verified] — and silently
dereferences null under `NDEBUG`. Report such a vertex to Python by its
`parameter_space_in_x()` / `parameter_space_in_y()` (`Arr_parameter_space` enum), never by
coordinates.

### 2.4 Polyline note

A polyline that is already x-monotone becomes **one** `X_monotone_curve_2`; its interior joints are
*not* arrangement vertices. [verified] the 4-point polyline `(0,0),(1,2),(3,-1),(5,1)` gives
`number_of_vertices() == 2`. Interior joint coordinates come from
`xcv.points_begin()…points_end()`, each a `CGAL::Point_2<K>` handled exactly as above.

---

## 3. Circle-segment — `CGAL::_One_root_point_2<K::FT, Filter>`

```cpp
// CGAL/Arr_geometry_traits/Circle_segment_2.h:75-135  [header]
public:
  typedef NumberType_                           NT;
  typedef _One_root_point_2<NT, Filter_>        Self;
  typedef typename Point_rep::CoordNT           CoordNT;   // Sqrt_extension<NT,NT,Tag_true,Boolean_tag<Filter_>>

  _One_root_point_2();                                     // (0,0)
  _One_root_point_2(const Self& p);
  _One_root_point_2& operator=(const _One_root_point_2&) = default;
  _One_root_point_2(const CoordNT& x, const CoordNT& y);   // also takes rationals: NT -> CoordNT is implicit

  const CoordNT& x() const { return (this->ptr()->_x); }   // REFERENCE into the shared rep
  const CoordNT& y() const { return (this->ptr()->_y); }
  bool equals(const Self& p) const;
  bool operator != (const Self& p) const;
  bool operator == (const Self& p) const;
  void set(const NT& x, const NT& y);                      // copy_on_write() then assign
  void set(const CoordNT& x, const CoordNT& y);
```

`CoordNT` accessors [header, `Sqrt_extension/Sqrt_extension_type.h`], all returning **lvalue
references** [verified]:

```cpp
inline const NT&   a0()   const;      // and mutable NT& a0();
inline const NT&   a1()   const;
inline const ROOT& root() const;
inline const bool& is_extended() const;
inline bool  is_rational() const;                  // == !is_extended()  (see gotcha 3!)
::CGAL::Sign  sign()  const;                       // interval-filtered -> writes the cache
std::pair<double,double> to_interval() const;      // certified, but writes the cache
```

Value = `a0 + a1·√root` when `is_extended()`, else `a0`. Each of `a0()/a1()/root()` is a `K::FT`,
so `.exact()` gives the `Exact_rational`.

### 3.1 EXTRACTION — [verified]

```cpp
struct OneRoot { bool extended; ExactRat a0, a1, root; };

static OneRoot one_root_exact(const CoordNT& c) {          // the exact algebraic triple
  OneRoot r; r.extended = c.is_extended();
  r.a0 = rat_to_strings(c.a0().exact());
  if (r.extended) { r.a1   = rat_to_strings(c.a1().exact());
                    r.root = rat_to_strings(c.root().exact()); }
  return r;
}

// exact rational value of a0 + a1*sqrt(root), when that value happens to be rational
static bool sqrt_ext_rational(const ER& a0, const ER& a1, const ER& root, ER& out) {
  if (CGAL::is_zero(a1) || CGAL::is_zero(root)) { out = a0; return true; }
  EI n = numerator(root), d = denominator(root);
  EI sn = boost::multiprecision::sqrt(n), sd = boost::multiprecision::sqrt(d);
  if (sn * sn != n || sd * sd != d) return false;          // sqrt(root) is irrational
  out = a0 + a1 * ER(sn, sd); return true;
}
static bool one_root_rational(const CoordNT& c, ER& out) {
  if (!c.is_extended()) { out = c.a0().exact(); return true; }
  return sqrt_ext_rational(c.a0().exact(), c.a1().exact(), c.root().exact(), out);
}

static std::pair<ER,ER> one_root_interval(const CoordNT& c, unsigned bits) {   // certified
  if (!c.is_extended()) { ER v = c.a0().exact(); return { v, v }; }
  ER a0 = c.a0().exact(), a1 = c.a1().exact(), rt = c.root().exact();
  if (CGAL::is_zero(a1) || CGAL::is_zero(rt)) return { a0, a0 };
  auto s = sqrt_interval(rt, bits);
  ER p = a1 * s.first, q = a1 * s.second;
  if (p > q) std::swap(p, q);                              // a1 may be negative
  return { a0 + p, a0 + q };                               // width <= |a1| * 2^-bits
}
static std::pair<double,double> one_root_double(const CTr::Point_2& p)
{ return { CGAL::to_double(p.x()), CGAL::to_double(p.y()) }; }
```

**[verified]** on the arrangement of the circle `x²+y²=2` with the segment `(-3,0)–(3,0)`:

| vertex | `is_extended` | `a0` | `a1` | `root` | interval width at `bits=100` |
|---|---|---|---|---|---|
| `x = -√2` | 1 | `0/1` | `-1/1` | `2/1` | `7.89e-31` |
| `x = +√2` | 1 | `0/1` | `1/1`  | `2/1` | `7.89e-31` |
| `x = -3`  | 0 | `-3/1` | — | — | `0` |
| `x = +3`  | 0 | `3/1`  | — | — | `0` |

and `one_root_rational(CoordNT(1,3,4))` returns `true` with value `7/1` even though
`is_extended() == true` (gotcha 3).

**Return shape for Python:** always return the triple `(a0, a1, root, extended)` *and* the
interval. The triple is the exact value and is what a downstream exact algorithm needs; the
interval is what a plot needs.

**Mutation / thread-safety.** `a0()/a1()/root()/is_extended()` write nothing — extraction through
these is **thread-safe**. `to_double`/`to_interval`/`sign`/`compare` on a `CoordNT` write the
`mutable` interval cache inside the point's **shared** rep (gotcha 9) — either avoid them in
worker threads or precompute intervals from the exact triple with `one_root_interval` (which uses
only `mpq` arithmetic and is pure).

**Lifetime.** `x()`/`y()` hand out `const CoordNT&` into the shared rep. Valid while any handle to
that rep lives; invalidated by `set()` on a *unique* handle. Copy the handle (8 B), never the
80-byte `CoordNT`.

### 3.2 CONSTRUCTION — [verified]

```cpp
using CTr = CGAL::Arr_circle_segment_traits_2<K>;
using CoordNT = CTr::CoordNT;

static CTr::Point_2 cs_point_rational(const ER& x, const ER& y)
{ return CTr::Point_2(CoordNT(FT(x)), CoordNT(FT(y))); }          // 1-arg NT ctor is implicit

static CTr::Point_2 cs_point_one_root(const ER& xa0, const ER& xa1, const ER& xr,
                                      const ER& ya0, const ER& ya1, const ER& yr) {
  return CTr::Point_2(CoordNT(FT(xa0), FT(xa1), FT(xr)),          // 3-arg ctor is EXPLICIT
                      CoordNT(FT(ya0), FT(ya1), FT(yr)));
}
```

The 3-arg `Sqrt_extension` constructor is `explicit`
(`template <class NTX, class NTY, class ROOTX> explicit Sqrt_extension(const NTX& a0, const NTY& a1, const ROOTX& root);`
[header]) with `\pre ACDE_TAG::value || !is_zero(root)` — and `ACDE_TAG == Tag_true` here, so
`root == 0` is **allowed** and simply yields the rational `a0`. A **negative** `root` is not
rejected by the constructor; it produces a nonsense number. Validate `root ≥ 0` in the binding.

**Points that cannot be represented:** anything not of the form `a0 + a1·√root` with rational
`a0, a1, root`. There is no fallback — reject at the binding boundary.

**Are freely-constructed points accepted by the arrangement?** **Yes** [verified]:

| constructed point | `Arr_naive_point_location::locate` | `CGAL::insert_point` |
|---|---|---|
| rational `(0, 1/2)`, inside the disc | `FACE` | isolated vertex, `V 4 → 5` |
| one-root `(1/2, ½√7)`, exactly on `x²+y²=2` | `HALFEDGE` | splits the edge, `degree == 2`, `arr.is_valid() == true` |

Unlike the Bezier traits, this traits reconstructs everything it needs from the coordinates, so a
"free" point carries no hidden state and is safe everywhere.

---

## 4. Conic — `CGAL::Conic_point_2<AlgKernel>`

```cpp
// CGAL/Arr_conic_traits_2.h   [header]
template <typename AlgKernel>
class Conic_point_2 : public AlgKernel::Point_2 {
  typedef typename Alg_kernel::FT       Algebraic;          // == CORE::Expr
  Conic_point_2();
  Conic_point_2(const Base& p);                                                  // implicit
  Conic_point_2(const Algebraic& hx, const Algebraic& hy, const Algebraic& hz);  // homogeneous
  Conic_point_2(const Algebraic& x, const Algebraic& y);                         // Cartesian
  void set_generating_conic(const Conic_id& id);
  bool is_generating_conic(const Conic_id& id) const;
};
```

`x()` / `y()` come from `CGAL::Point_2<CGAL::Cartesian<CORE::Expr>>` and return
`const Algebraic&` — an **lvalue reference** [verified].

### 4.1 EXTRACTION — [verified]

```cpp
// (a) exact rational: NOT AVAILABLE.  See gotcha 1.
// (b) certified interval of a chosen width
auto ix = expr_interval(p.x(), max_width);     // §1.3
auto iy = expr_interval(p.y(), max_width);
// (c) doubles
auto approx = traits.approximate_2_object();
double dx = approx(p, 0), dy = approx(p, 1);   // == CGAL::to_double(p.x()) / p.y()
```

Traits-side signatures [header, `Arr_conic_traits_2.h`]:

```cpp
typedef double                                   Approximate_number_type;
typedef CGAL::Cartesian<Approximate_number_type> Approximate_kernel;
typedef Approximate_kernel::Point_2              Approximate_point_2;
Approximate_number_type operator()(const Point_2& p, int i) const;   // \pre i == 0 || i == 1
Approximate_point_2     operator()(const Point_2& p) const;
template <typename OutputIterator>
OutputIterator operator()(const X_monotone_curve_2& xcv, double error,
                          OutputIterator oi, bool l2r = true) const;
```

**[verified]** on the arrangement of `x²+y²=2` with `y=0`:
vertices at `±√2` reach width `8.41e-45` for `max_width = 1e-30`; the two segment endpoints reach
the degenerate `[-3,-3]`, `[3,3]` (width 0) — which is how a rational conic coordinate reveals
itself *safely*: the interval collapses, no equality test needed.

> **Do not** attempt "certified interval → simplest rational → verify with `==`". That exact
> pipeline is what exposed gotcha 1: on the `+√2` vertex it reported the 83-digit convergent as
> the exact value. If you must hand Python a single rational, label it an **approximation** and
> derive it from `Nt_traits::rational_in_interval(x1, x2)`
> (`CORE_algebraic_number_traits.h:91-120`, `CGAL_precondition(x1 != x2)`) or from the interval
> endpoints — never call it exact.

**Mutation / thread-safety.** Extraction through `x()`, `y()`, `expr_interval` and `Approximate_2`
does not touch the `std::list<Conic_id>`, but it *does* refine the `Expr`'s shared internal
approximation, and any *predicate* you run (`contains_point`, and therefore `Compare_y_at_x_2`,
`Split_2`, `Intersect_2`, `Are_mergeable_2`) `const_cast`s the point and appends a `Conic_id`
[verified]. Combined with the traits' `mutable Intersection_map`, this means:
**one traits object and no shared points per worker thread.**

**Lifetime.** `Conic_point_2` is a value type; the coordinate handles are shared with copies, the
id list is deep-copied [verified]. `const Algebraic&` from `x()` stays valid as long as the point
does.

### 4.2 CONSTRUCTION — [verified]

```cpp
using Nt_traits = CGAL::CORE_algebraic_number_traits;
using Rational = Nt_traits::Rational;  using Algebraic = Nt_traits::Algebraic;

static Algebraic alg_from_rational(const Rational& q) { return Algebraic(q); }   // the EXACT node
static Algebraic alg_from_sqrt_ext(const Rational& a0, const Rational& a1, const Rational& root) {
  if (CGAL::is_negative(root)) throw std::domain_error("negative root");
  return Algebraic(a0) + Algebraic(a1) * CGAL::sqrt(Algebraic(root));            // a0 + a1*sqrt(root)
}
static CTraits::Point_2 conic_point_rational(const Rational& x, const Rational& y)
{ return CTraits::Point_2(alg_from_rational(x), alg_from_rational(y)); }
```

Equivalent, traits-flavoured: `Nt_traits nt; nt.convert(q)` — `Algebraic convert(const Rational& q) const { return (Algebraic (q)); }`
and `Algebraic convert(const Integer& z) const;` [header, `CORE_algebraic_number_traits.h:66-80`].

**Critical**: use `Expr(BigRat)`, never `Expr("0.1")` — the string constructor is a *decimal
approximation* and `Expr("0.1")` is not `1/10` (`number_types_and_errors.md` gotcha 4).

**Points that cannot be represented:** none, for any real algebraic number you can write as an
`Expr` expression; but the *precision* of any subsequent comparison is subject to gotcha 1. There
is no error signal — a bad `Expr` behaves like a good one.

**Are freely-constructed points accepted?** **Yes** [verified]:

| point | `locate` | `insert_point` |
|---|---|---|
| `(0, 1/2)` rational, inside the disc | `FACE` | isolated vertex |
| `(1/2, ½√7)` algebraic, exactly on `x²+y²=2` | `HALFEDGE` | splits the edge, `degree == 2`, `arr.is_valid()` |

and feeding an off-curve point to `Split_2` **throws** a precondition
(`Arr_conic_traits_2.h:993`) [verified] — so this traits is safe against the Bezier-style
corruption of gotcha 5.

---

## 5. Bezier — `CGAL::_Bezier_point_2`

```cpp
// CGAL/Arr_geometry_traits/Bezier_point_2.h   [header]
_Bezier_point_2 ();                                                    // empty, NOT exact
_Bezier_point_2 (const Self& bpt);
_Bezier_point_2 (const Algebraic& x, const Algebraic& y, bool dummy);  // "only for private use"
_Bezier_point_2 (const Rational& x, const Rational& y);                // <-- the free-point ctor
_Bezier_point_2 (const Curve_2& B, const Rational& t0);                // \pre 0 <= t0 <= 1
_Bezier_point_2 (const Curve_2& B, unsigned int xid, const Rational& t0);
_Bezier_point_2 (const Curve_2& B, const Algebraic& t0);
_Bezier_point_2 (const Curve_2& B, unsigned int xid, const Algebraic& t0);

bool is_exact () const;                              // p_alg_x && p_alg_y
bool is_rational () const;                           // p_rat_x && p_rat_y
const Algebraic& x () const;                         // \pre _rep().is_exact()
const Algebraic& y () const;                         // \pre _rep().is_exact()
std::pair<double, double> approximate () const;      // ALWAYS safe, never throws
operator Rat_point_2 () const;                       // \pre _rep().is_rational()
void make_exact (Bezier_cache& cache) const;         // const, mutates through const_cast
void get_bbox (typename Bounding_traits::NT& min_x, typename Bounding_traits::NT& min_y,
               typename Bounding_traits::NT& max_x, typename Bounding_traits::NT& max_y) const;
```

Note the argument order of `get_bbox` — `(min_x, min_y, max_x, max_y)`, *not* the CGAL `Bbox_2`
`(xmin, xmax, ymin, ymax)` convention.

### 5.1 EXTRACTION — [verified]

```cpp
struct BezCoord { bool rational; ExactRat num_den; std::pair<Rational,Rational> iv; };

static BezCoord bezier_x(const BTraits::Point_2& p, BTraits::Bezier_cache& cache,
                         const Rational& max_width) {
  if (!p.is_exact()) p.make_exact(cache);                 // MANDATORY, incl. release builds
  if (p.is_rational()) {
    Rat_kernel::Point_2 rp = (Rat_kernel::Point_2) p;     // \pre is_rational()
    return { true, rat_to_strings(rp.x()), { rp.x(), rp.x() } };
  }
  return { false, {}, expr_interval(p.x(), max_width) };  // \pre is_exact()  — holds after make_exact
}
```

**[verified]** on a 3-curve arrangement (10 vertices): 6 rational (exact `0/1`, `3/1`, `1/1`, …),
4 algebraic (intervals reaching `1.07e-39 … 3.65e-63` at `max_width = 1e-30`); 4 vertices started
with `is_exact() == false` and `make_exact(cache)` fixed all of them without making any of them
rational.

Three further always-available fallbacks:

```cpp
std::pair<double,double> xy = p.approximate();     // never throws, no cache; bbox centre when inexact
NT_rat bx0, by0, bx1, by1; p.get_bbox(bx0, by0, bx1, by1);   // exact RATIONAL enclosure, no cache
Nt_traits nt; std::pair<double,double> di = nt.double_interval(p.x());   // \pre is_exact()
```

**[verified]** `get_bbox` on an algebraic vertex gives a rational box of width `≈2.2e-16`
(one double ulp) — a cheap certified rational enclosure that needs **no cache and no `Expr`
refinement**. Use it as the fast path and `expr_interval` only when the caller asks for a narrower
width. On a rational vertex the box is degenerate and equals the exact coordinates.

**Own your cache.** `Traits_2::Bezier_cache` is not reachable from the traits (private `p_cache`,
no accessor) and is **non-copyable** — hold one by value in your core object next to the traits
(`traits_bezier.md` gotcha 3).

**Mutation / thread-safety.** `make_exact`, `refine`, `fit_to_bbox`, `add_originator`,
`merge_originators`, `compare_x`, `compare_xy` and `equals` are all declared `const` and all
`const_cast` the rep. `compare_xy` on equal points additionally **merges the originator lists and
rebinds `p2` to `p1`'s rep** — [verified] `is_same` went `0 → 1`, originator counts `1 → 2`.
Extraction is therefore **not thread-safe**, not even through a `const Point_2&`, and not even
read-only: `bezier_x` itself writes (via `make_exact`). One traits + one cache + no shared points
per thread.

### 5.2 CONSTRUCTION — [verified]

```cpp
static BTraits::Point_2 bezier_free_point(const Rational& x, const Rational& y)
{ return BTraits::Point_2(x, y); }        // sets p_rat_x/y AND p_alg_x/y, bbox = [x,x]x[y,y]
```

**[verified]** the result has `is_exact() == true`, `is_rational() == true`,
**`originators_begin() == originators_end()`** (zero originators). Acceptance:

| operation | free point off every curve | free point exactly on a curve |
|---|---|---|
| `Arr_naive_point_location::locate` | **OK** → `FACE` | **OK** → `HALFEDGE` |
| `CGAL::insert_point(arr, p, pl)` | **OK** → isolated vertex (`V 10 → 11`) | **OK** → edge split, `degree == 2` (`V 11 → 12`) |
| `traits.split_2_object()(xcv, p, c1, c2)` on a curve it is **not** on | **SILENT CORRUPTION** | n/a |

[verified] on the third row: `Point_2(Rational(1000), Rational(1000))` fed to `Split_2` against an
x-monotone subcurve spanning `x ∈ [0, 0.292…]` produced **no exception**, and
`c1.target().approximate() == c2.source().approximate() == (1000, 1000)`. Cause
(`Bezier_x_monotone_2.h:1291` [header]):

```cpp
CGAL_precondition(p.get_originator(_curve, _xid) != p.originators_end() ||
                  p.is_rational());
```

`is_rational()` alone satisfies it. `split` then just assigns `c1._pt = p; c2._ps = p;`.
Additionally, when `is_vertical()` and the point is rational, `split` runs
`compute_polynomial_roots` and asserts `CGAL_assertion(sols.size() == 1)` — an off-curve point
trips *that* assertion instead, but only on vertical subcurves.

**Rule for the binding: never hand a user-supplied point straight to `Split_2` / `split_edge`.**
Route every user point through `CGAL::insert_point(arr, p, pl)` (which locates first, so it can
only split an edge the point genuinely lies on), or pre-check with
`xcv.point_position(p, cache) == EQUAL`.

Prefer the curve-bound constructors whenever the point is *meant* to lie on a curve — they attach
an originator and everything downstream then works exactly:

```cpp
Point_2 p_rat  (B, NT_rat(1,3));          // exact + rational, originator xid = 0
Point_2 p_rat_x(B, xid, NT_rat(1,3));     // tagged with the x-monotone subcurve id
Point_2 p_alg  (B, xid, t_algebraic);     // exact, not rational
// \pre on all four: 0 <= t0 <= 1
```

**Points that cannot be represented:** an *irrational* point supplied from Python cannot be built
at all through a public constructor — `_Bezier_point_2(const Algebraic&, const Algebraic&, bool)`
is documented "only for private use" and yields a point with no originators and
`is_rational() == false`, which then fails the `Split_2` precondition and cannot be located
reliably. Restrict the Python-facing constructor to rationals, or to `(curve, t)`.

---

## 6. Geodesic arcs on the sphere — `CGAL::Arr_extended_direction_3<K>`

```cpp
// CGAL/Arr_geodesic_arc_on_sphere_traits_2.h:46-117   [header]
template <typename Kernel>
class Arr_extended_direction_3 : public Kernel::Direction_3 {
public:
  using FT = typename Kernel::FT;
  using Direction_3 = typename Kernel::Direction_3;
  enum Location_type {
    NO_BOUNDARY_LOC = 0,   // interior of the parameter space
    MIN_BOUNDARY_LOC,      // = 1, south pole
    MID_BOUNDARY_LOC,      // = 2, on the vertical identification curve
    MAX_BOUNDARY_LOC       // = 3, north pole
  };
  Arr_extended_direction_3();                                          // (0,0,1), MAX_BOUNDARY_LOC
  Arr_extended_direction_3(const Direction_3& dir, Location_type location);   // TRUSTS you
  Arr_extended_direction_3(const Arr_extended_direction_3& other);
  Arr_extended_direction_3& operator=(const Arr_extended_direction_3& other);
  void          set_location(Location_type location);
  Location_type location() const;
  Location_type discontinuity_type() const;
  bool is_no_boundary()  const;   bool is_min_boundary() const;
  bool is_mid_boundary() const;   bool is_max_boundary() const;
};
// inherited, CGAL/Direction_3.h:103-118  [header]
decltype(auto) dx() const;   decltype(auto) dy() const;   decltype(auto) dz() const;
decltype(auto) delta(int i) const;   // CGAL_kernel_precondition( i >= 0 && i <= 2 )
```

`dx()/dy()/dz()` return **by value** for Epeck [verified], each a `K::FT`.

### 6.1 EXTRACTION — [verified]

The stored triple is an exact rational direction; the *sphere point* is that direction normalised,
which is generally irrational. Return **both**.

```cpp
struct Dir3 { ExactRat x, y, z; int location; };

static Dir3 sphere_raw(const STr::Point_2& p) {              // exact, always available
  return { rat_to_strings(p.dx().exact()), rat_to_strings(p.dy().exact()),
           rat_to_strings(p.dz().exact()), static_cast<int>(p.location()) };
}

// certified interval of the NORMALISED i-th coordinate (i in {0,1,2})
static std::pair<ER,ER> sphere_unit_interval(const STr::Point_2& p, int i, unsigned bits) {
  ER x = p.dx().exact(), y = p.dy().exact(), z = p.dz().exact();
  ER c = (i == 0) ? x : (i == 1) ? y : z;
  auto n = sqrt_interval(x*x + y*y + z*z, bits);             // 0 < n.first <= |d| <= n.second
  if (CGAL::is_zero(n.first)) throw std::domain_error("zero direction");
  return CGAL::is_negative(c) ? std::pair<ER,ER>{ c / n.first,  c / n.second }
                              : std::pair<ER,ER>{ c / n.second, c / n.first };
}
```

**[verified]** on a 4-arc arrangement (octant triangle plus one extra great circle):

| vertex (raw) | `location` | `‖d‖² ` | unit `x` interval at `bits = 100` |
|---|---|---|---|
| `(1/1, 0/1, 0/1)` | 0 | `1` | `[1, 1]` — **exact**, `sqrt_interval` detected the square |
| `(0/1, 0/1, 1/1)` | 3 (north pole) | `1` | `[0, 0]` |
| `(2/1, 0/1, 2/1)` | 0 | `8` | width `1.97e-31` |
| `(1/1, 1/1, 1/1)` | 0 | `3` | width `2.63e-31` |

Note the raw `(2,0,2)`: an intersection vertex is **not** unit length and not even reduced.

**What the caller should return.** Return the **raw exact rational triple plus `location`** as the
canonical identity of the vertex — it is exact, it is what `compare_y_on_boundary_2` and the
topology traits key on, and two directions that differ by a positive scale are *the same point*.
Return normalised values only as a *derived, approximate* view: the `double` triple from
`Approximate_2` (which you must normalise yourself) or `sphere_unit_interval` when a certified
figure is wanted. Never present the raw triple as "the coordinates of the point on the unit
sphere".

Traits-side [header, `Arr_geodesic_arc_on_sphere_traits_2.h:2858-2890`]:

```cpp
using Approximate_number_type = double;
using Approximate_kernel      = CGAL::Cartesian<Approximate_number_type>;
using Approximate_point_2     = Arr_extended_direction_3<Approximate_kernel>;
Approximate_number_type operator()(const Point_2& p, int i) const;  // \pre i == 0 || 1 || 2  (three!)
Approximate_point_2     operator()(const Point_2& p) const;         // still UNNORMALISED
template <typename OutputIterator>
OutputIterator operator()(const X_monotone_curve_2& xcv, Approximate_number_type error,
                          OutputIterator oi, bool l2r = true) const;   // this one DOES normalise
```

**[verified]** `Approximate_2(p, 0..2)` on the point `(2,2,0)` returned `2, 2, 0`, and
`Approximate_2(p)` returned the direction `(2,2,0)` with the location copied over.

**Mutation / thread-safety.** Nothing here mutates: `Arr_extended_direction_3` is a plain value
(a `Direction_3` handle plus an enum) and `dx()/dy()/dz()` only read. With `.exact()` being
`call_once`-guarded, **extraction is thread-safe**.

### 6.2 CONSTRUCTION — the only safe route

```cpp
// CGAL/Arr_geodesic_arc_on_sphere_traits_2.h:480-563   [header]
class Construct_point_2 {
protected:
  using Traits = Arr_geodesic_arc_on_sphere_traits_2<Kernel, atan_x, atan_y>;
  const Traits& m_traits;                                    // non-owning
  Construct_point_2(const Traits& traits) : m_traits(traits) {}
  friend class Arr_geodesic_arc_on_sphere_traits_2<Kernel, atan_x, atan_y>;
public:
  Point_2 operator()(const FT& x, const FT& y, const FT& z);        // NOT const
  Point_2 operator()(const Direction_3& other);                     // NOT const
  void init(Point_2& p, std::true_type)  const;                     // atan_y == 0 specialisation
  void init(Point_2& p, std::false_type) const;
};
Construct_point_2 construct_point_2_object() const { return Construct_point_2(*this); }
```

Both `operator()`s are **non-`const`** — write `auto ctp = traits.construct_point_2_object();`,
never `const auto`. They build the `Direction_3` and then call `init(...)`, which derives
`Location_type` from the signs of the coordinates and from the traits' identification direction.

```cpp
STr traits;
auto ctp = traits.construct_point_2_object();                        // non-const!
STr::Point_2 p = ctp(FT(rat_from_strings(xn,xd)),
                     FT(rat_from_strings(yn,yd)),
                     FT(rat_from_strings(zn,zd)));
```

**Preconditions.** None are declared. The one real requirement is implicit: the direction must not
be `(0,0,0)` — `Kernel::Direction_3(0,0,0)` is degenerate and `init` will classify it as a pole
(the `z_sign == ZERO` branch falls through to `MAX_BOUNDARY_LOC`). Reject a zero triple in the
binding. Any non-zero rational triple is representable; scale is irrelevant, so you may
(and should not bother to) reduce it.

**Why the raw constructor is forbidden.** `Arr_extended_direction_3(dir, location)` performs no
check. **[verified]** `Point_2(Direction_3(1,0,0), Point_2::MAX_BOUNDARY_LOC)` yields a point that
reports `is_max_boundary() == true` — the topology traits will then treat the direction `(1,0,0)`
as the north pole, register it in the pole slot instead of the boundary-vertex map, and the DCEL
becomes inconsistent. The correct location for `(1,0,0)` is `NO_BOUNDARY_LOC` [verified via the
functor]. **Expose `Construct_point_2` and nothing else.**

---

## 7. Thread-safety summary

| traits | extraction is thread-safe? | why |
|---|---|---|
| segment / linear / polyline | **yes** | `Lazy::exact()` uses `std::call_once` + atomic rep pointer, `CGAL_HAS_THREADS == 1` [verified] |
| circle-segment | **yes via `a0()/a1()/root()/.exact()`**; **no** via `to_double/to_interval/sign/compare` | those write the `mutable` interval cache in the shared rep [header] |
| conic | **no** | any predicate `const_cast`s the point and appends a `Conic_id` [verified]; traits' `mutable Intersection_map` unsynchronised |
| Bezier | **no** | `make_exact`/`compare_xy` mutate through `const`, `compare_xy` rebinds handles [verified] |
| sphere | **yes** | plain value type; `.exact()` is `call_once`-guarded |

For the three "no" rows the rule is the same: one traits object, one cache, and no `Point_2`
shared across threads. Copying the traits is **not** a fix for Bezier — the copy aliases the
original's cache without owning it (`traits_bezier.md` gotcha 2).

---

## 8. Programs compiled and run for this document

Under `…/scratchpad/apimap_coords/`, all with the compile line at the top of this file:

| file | what it establishes |
|---|---|
| `recipes_a.cpp` | every recipe in §§1–3 and §6, run against live arrangements |
| `recipes_b.cpp` | every recipe in §§4–5, run against live arrangements |
| `t_seg.cpp` | `Point_2` identity across the three linear traits, `sizeof`, handle sharing |
| `t_cs.cpp`, `t_ctor.cpp` | `CoordNT` sizes (`Filter` on/off), accessor reference-ness, free-point `locate` / `insert_point` |
| `t_conic.cpp` … `t_conic5.cpp` | the CORE false-equality (gotcha 1) from four angles, `contains_point` mutation, `Split_2` precondition |
| `t_expreq.cpp`, `t_expr2.cpp`, `t_expr3.cpp`, `t_encl.cpp` | that fresh / refined / polynomial-root `Expr`s compare *correctly*, and the enclosure width law |
| `t_bez.cpp` | non-exact vertices, `make_exact`, free-point acceptance, `Split_2` corruption, `compare_xy` merging |
| `t_lin.cpp`, `t_misc.cpp` | vertices at infinity, `point()` assertion, `Direction_3` accessor kinds |
| `t_sph.cpp` | sphere locations, unnormalised `Approximate_2`, raw-ctor corruption |
| `t_dbl.cpp`, `t_thr.cpp` | `Approximate_2` rounding error, `CGAL_HAS_THREADS` |

Headers read verbatim: `Point_2.h`, `Lazy.h`, `Lazy_exact_nt.h`, `Direction_3.h`,
`Arr_segment_traits_2.h`, `Arr_linear_traits_2.h`, `Arr_polyline_traits_2.h`,
`Arr_circle_segment_traits_2.h`, `Arr_geometry_traits/Circle_segment_2.h`,
`Sqrt_extension/Sqrt_extension_type.h`, `Sqrt_extension/Real_embeddable_traits.h`,
`Arr_conic_traits_2.h`, `Arr_Bezier_curve_traits_2.h`,
`Arr_geometry_traits/Bezier_point_2.h`, `Arr_geometry_traits/Bezier_x_monotone_2.h`,
`Arr_geodesic_arc_on_sphere_traits_2.h`, `CORE_algebraic_number_traits.h`,
`CORE/Expr.h`, `CORE/BigFloat.h`, `Arr_dcel_base.h`, `Arrangement_on_surface_2.h`.
