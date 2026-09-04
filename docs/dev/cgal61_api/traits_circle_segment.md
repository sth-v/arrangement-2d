# CGAL 6.1 — Circle/segment traits & one-root numbers (installed headers API map)

Source of truth: the headers installed at `/opt/homebrew/include/CGAL` (CGAL 6.1, header-only,
`$URL: .../v6.1/...`). Every signature below is quoted verbatim from those files. Everything marked
**[verified]** was checked by compiling and running a test program with
`clang++ -std=c++17 -DCGAL_USE_CORE -DCGAL_USE_GMP -DCGAL_USE_MPFR -I/opt/homebrew/include -lgmp -lmpfr`.

Files covered:

| File | Contents |
|---|---|
| `CGAL/Arr_circle_segment_traits_2.h` | `Arr_circle_segment_traits_2<Kernel_, bool Filter = true>` |
| `CGAL/Arr_geometry_traits/Circle_segment_2.h` | `_One_root_point_2_rep`, `_One_root_point_2`, `_Circle_segment_2`, `_X_monotone_circle_segment_2` |
| `CGAL/Arr_geometry_traits/One_root_number.h` | `_One_root_number<NT, bool Filter>` — **dead legacy code, not used by anything** |
| `CGAL/Gps_circle_segment_traits_2.h` | `Gps_circle_segment_traits_2<Kernel_, bool Filer_ = true>` (sic) |
| `CGAL/Arr_circular_arc_traits_2.h` | `Arr_circular_arc_traits_2<CircularKernel>` |
| `CGAL/Arr_line_arc_traits_2.h` | `Arr_line_arc_traits_2<CircularKernel>` |
| `CGAL/Arr_circular_line_arc_traits_2.h` | `Arr_circular_line_arc_traits_2<CircularKernel>` + `CGAL::VariantFunctors::*` |

---

## Gotchas / surprises vs. older CGAL

1. **`_One_root_number` is dead code.** `Point_2::CoordNT` is **not** `_One_root_number<NT>` any
   more — it is
   `Sqrt_extension<NT, NT, Tag_true, Boolean_tag<Filter_>>`
   (`Circle_segment_2.h:46`). `grep -rl One_root_number /opt/homebrew/include/CGAL` returns
   **only `One_root_number.h` itself** — nothing includes it. Do **not** bind `_One_root_number`;
   bind `Sqrt_extension`. The old `alpha()/beta()/gamma()/is_rational()` names survive on
   `Sqrt_extension` purely as backward-compat aliases for `a0()/a1()/root()/!is_extended()`.
2. **`_One_root_number` had no `to_double()`/`to_interval()` members and no `operator+` between two
   one-root numbers** (only rational ⊕ one-root). `Sqrt_extension` fixes all of that: it has full
   field arithmetic (`+ - * /` between two extensions, via `boost::ordered_field_operators`),
   `to_interval()`, `sign()`, `compare()`, `operator==`, `operator<`, and `CGAL::to_double`.
   Extensions with *different* roots compare correctly (`ACDE_TAG = Tag_true` ⇒ default
   `in_same_extension = false`).
3. **`Arr_circle_segment_traits_2` has NO `Construct_x_monotone_curve_2` and NO `Construct_curve_2`.**
   Consequences **[verified]**:
   * `CGAL::Arr_landmarks_point_location` **fails to compile** with this traits
     (`Arr_landmarks_point_location.h:329` calls `traits.construct_x_monotone_curve_2_object()(p,q)`)
     — even though the header comment claims `Approximate_2` exists "for the landmarks strategy".
     Use `Arr_trapezoid_ric_point_location` / `Arr_walk_along_line_point_location` /
     `Arr_naive_point_location` instead.
   * `CGAL::convert_polygon(Polygon_2<K>, traits)` (`Boolean_set_operations_2/Polygon_conversions.h:88`)
     does **not** work — it needs `construct_curve_2_object()`. You must convert
     `CGAL::Polygon_2<Kernel>` → `General_polygon_2` by hand (recipe below).
4. **Construct circles by `(center, radius)` when the radius is rational, not by squared radius.**
   `_Circle_segment_2(const Kernel::Point_2& c, const NT& r, Orientation)` sets `m_has_radius=true`
   and then the vertical tangency points come out **rational** (`x0 ± r`) instead of one-root numbers
   (`Circle_segment_2.h:479-489`). That halves the arithmetic cost of everything downstream. The
   `Circle_2` constructor takes a *squared* radius and loses this information.
5. **Traits functors hold references/pointers into the traits object.**
   `Intersect_2` stores `Intersection_map& _inter_map` (a reference to the traits' `mutable` member),
   `Merge_2` stores `const Traits*`, `Approximate_2` and `Trim_2` store `const Traits&`.
   A functor must never outlive the traits object. Also the intersection cache
   (`Arr_circle_segment_traits_2(bool use_intersection_caching = false)`) is **never cleared** —
   there is no `clear_cache()`; it grows for the life of the traits object.
6. `Make_x_monotone_2`'s output dereference type is `std::variant<Point_2, X_monotone_curve_2>`
   (the functor just does `*oi++ = X_monotone_curve_2(...)` / `*oi++ = Point_2(...)`, relying on
   implicit conversion) — **not** `CGAL::Object`. `Intersect_2`'s is
   `std::variant<std::pair<Point_2, Multiplicity>, X_monotone_curve_2>`. **[verified]**
7. `Approximate_2::operator()` on a curve has the *new* CGAL 6 signature
   `(xcv, double error, OutputIterator oi, bool l2r = true)` and emits
   `Approximate_point_2 = CGAL::Cartesian<double>::Point_2` — adaptive subdivision to an
   **absolute** error, not a fixed sample count. The *old* fixed-count API still exists but as a
   member of the curve: `X_monotone_curve_2::approximate(oi, unsigned int n)` emitting
   `std::pair<double,double>`. Two different element types — do not mix them up.
8. The three `Circular_kernel_2`-based traits (`Arr_circular_arc_traits_2`,
   `Arr_line_arc_traits_2`, `Arr_circular_line_arc_traits_2`) are second-class citizens in 6.1:
   `Has_left_category = Has_merge_category = Tag_false`, no `Approximate_2`, no `Trim_2`,
   no `Compare_endpoints_xy_2`, no `Construct_opposite_2`; they carry a `\todo` about still using
   `CGAL::Object` internally; and **`Arrangement_with_history_2` fails to compile with all three**
   **[verified]**. `Arr_circular_line_arc_traits_2::X_monotone_curve_2` is a
   `std::variant<Line_arc_2, Circular_arc_2>` and its `Curve_2` a
   `std::variant<Line_arc_2, Circular_arc_2, Not_X_Monotone>` — awkward to type-erase.
   **Recommendation: do not bind them. `Arr_circle_segment_traits_2` strictly dominates.**
9. `Gps_traits_2::Equal_2::operator()(Polygon_with_holes_2, Polygon_with_holes_2)` contains a real
   bug: `if (pgn1.number_of_holes(), pgn2.number_of_holes()) return false;` (comma operator —
   `Gps_traits_2.h`). Any PWH with ≥1 hole compares unequal to anything. Don't rely on it.
10. `_One_root_point_2` is a reference-counted COW handle (`Handle_for`) — `sizeof == 8`
    **[verified]**, copies are cheap, `identical()` is a pointer compare used as a fast path in
    `Compare_x_2`/`Compare_xy_2`. `_X_monotone_circle_segment_2` (48 B) and `_Circle_segment_2`
    (56 B) are plain value types.
11. `Filter = true` makes `CoordNT` 56 bytes (it embeds a `mutable std::optional<pair<double,double>>`
    interval cache); `Filter = false` makes it 32 bytes **[verified]**. Keep `true`.
12. `operator>>` (istream) exists for `General_polygon_2` but **not** for
    `_X_monotone_circle_segment_2` or `_One_root_point_2` (the point's `operator>>` is commented out
    in the header). So `General_polygon_2::operator>>` will not compile for this traits — I/O is
    output-only.

---

## 1. `CGAL::_One_root_number<NumberType_, bool Filter_ = true>` — LEGACY, DO NOT BIND

`CGAL/Arr_geometry_traits/One_root_number.h`. Kept for source compatibility only; nothing in
CGAL 6.1 includes this header. Documented here so you recognise it and skip it.

Value semantics: `value = m_alpha + m_beta * sqrt(m_gamma)` (verbatim class comment:
*"a number of the form m_alpha + m_beta*sqrt(m_gamma)"*). `m_is_rational` is `true` iff constructed
from the 1-arg/0-arg ctor — it is **a construction flag, not a recomputed property**: constructing
`_One_root_number(a, 0, c)` leaves `m_is_rational == false`.

```cpp
template <class NumberType_, bool Filter_ = true>
class _One_root_number
{
public:
  typedef NumberType_                       NT;
  typedef _One_root_number<NT, Filter_>     Self;

  _One_root_number ();                                          // 0
  _One_root_number (const NT& val);                             // rational
  _One_root_number (const NT& a, const NT& b, const NT& c);     // a + b*sqrt(c); pre: sign(c)==POSITIVE

  Self operator- () const;
  Self operator+ (const NT& val) const;
  Self operator- (const NT& val) const;
  Self operator* (const NT& val) const;
  Self operator/ (const NT& val) const;      // pre: sign(val) != ZERO
  void operator+= (const NT& val);
  void operator-= (const NT& val);
  void operator*= (const NT& val);
  void operator/= (const NT& val);

  NT alpha() const;
  NT beta() const;
  NT gamma() const;
  bool is_rational() const;

  CGAL::Sign _sign () const;                 // "private" by comment only; actually public
};
```

Free functions (namespace `CGAL`):

```cpp
template <class NT, bool FL> std::pair<double,double> to_interval (const _One_root_number<NT,FL>& x);
template <class NT, bool FL> _One_root_number<NT,FL> operator+ (const NT& val, const _One_root_number<NT,FL>& x);
template <class NT, bool FL> _One_root_number<NT,FL> operator- (const NT& val, const _One_root_number<NT,FL>& x);
template <class NT, bool FL> _One_root_number<NT,FL> operator* (const NT& val, const _One_root_number<NT,FL>& x);
template <class NT, bool FL> _One_root_number<NT,FL> operator/ (const NT& val, const _One_root_number<NT,FL>& x);
template <class NT, bool FL> double                 to_double  (const _One_root_number<NT,FL>& x);
template <class NT, bool FL> _One_root_number<NT,FL> square    (const _One_root_number<NT,FL>& x);
template <class NT, bool FL> CGAL::Sign             sign       (const _One_root_number<NT,FL>& x);
template <class NT, bool FL> CGAL::Comparison_result compare (const NT& val, const _One_root_number<NT,FL>& x);
template <class NT, bool FL> CGAL::Comparison_result compare (const _One_root_number<NT,FL>& x, const NT& val);
template <class NT, bool FL> CGAL::Comparison_result compare (const _One_root_number<NT,FL>& x,
                                                              const _One_root_number<NT,FL>& y);
```

There is **no** `operator+`/`-`/`*`/`/` between two `_One_root_number`s, no `operator==`, no
`operator<`, no `Real_embeddable_traits` specialisation. `Filter_` only gates the interval-arithmetic
early-out inside `sign`/`compare`.

---

## 2. The coordinate number type actually in use: `CoordNT = Sqrt_extension<NT, NT, Tag_true, Boolean_tag<Filter>>`

`CGAL/Sqrt_extension/Sqrt_extension_type.h`. This *is* the "one-root number" of CGAL 6.1.
Value = `a0 + a1 * sqrt(root)` when `is_extended()`, else just `a0`.

```cpp
template <class NT_, class ROOT_, class ACDE_TAG_, class FP_TAG>
class Sqrt_extension : public internal::Interval_optional_caching<FP_TAG>,
    boost::ordered_field_operators1<Sqrt_extension<NT_,ROOT_,ACDE_TAG_,FP_TAG>,
    boost::ordered_field_operators2<Sqrt_extension<NT_,ROOT_,ACDE_TAG_,FP_TAG>, NT_,
    boost::ordered_field_operators2<Sqrt_extension<NT_,ROOT_,ACDE_TAG_,FP_TAG>, CGAL_int(NT_)> > >
{
public:
    typedef NT_ NT;  typedef ROOT_ ROOT;  typedef ACDE_TAG_ ACDE_TAG;
    typedef Sqrt_extension<NT,ROOT,ACDE_TAG,FP_TAG> Self;
```

For our instantiation: `NT = ROOT = Kernel::FT`, `ACDE_TAG = Tag_true`,
`FP_TAG = Boolean_tag<Filter>` (i.e. `Tag_true` when `Filter == true`).

### Constructors

```cpp
Sqrt_extension();                                    // 0, not extended
Sqrt_extension(CGAL_int(NT) i);                      // implicit from int (if NT != int)
Sqrt_extension(const NT& i);                         // implicit from the rational NT  <-- the common one
Sqrt_extension(const Sqrt_extension<NT, ROOT_, ACDE_TAG,
               std::integral_constant<bool, !FP_TAG::value>>& other);   // convert filtered <-> unfiltered
Sqrt_extension(const ROOT& root, bool);              // undocumented: 0 + 1*sqrt(root)
template <class NTX> explicit Sqrt_extension(const NTX& i);
template <class NTX, class NTY, class ROOTX>
explicit Sqrt_extension(const NTX& a0, const NTY& a1, const ROOTX& root);   // a0 + a1*sqrt(root)
    // pre: ACDE_TAG::value || !is_zero(root)   -- with ACDE_TAG = Tag_true, root == 0 IS allowed
template <class NTX> explicit Sqrt_extension(const NTX& a, const NTX& b, const NTX& c,
                                             const bool is_smaller, /*SFINAE*/);  // root of a x^2+b x+c
Sqrt_extension& operator=(const Sqrt_extension<...opposite FP_TAG...>&);
Sqrt_extension& operator=(const NT& i);
Sqrt_extension& operator=(CGAL_int(NT) i);
```

> The 3-arg ctor is **`explicit`**. `Point_2(CoordNT(x0), CoordNT(a,b,c))` is fine;
> `Point_2(x_rational, y_rational)` also works because the 1-arg `NT` ctor is implicit.

### Accessors

```cpp
Self conjugate() const;                     // a0 - a1*sqrt(root)
inline const NT&   a0() const;   NT& a0();
inline const NT&   alpha() const; NT& alpha();      // backward-compat alias of a0
inline const NT&   a1() const;   NT& a1();
inline const NT&   beta() const;  NT& beta();       // backward-compat alias of a1
inline const ROOT& root() const;
inline const ROOT& gamma() const;                   // backward-compat alias of root
inline const bool& is_extended() const;
inline bool  is_rational() const;                   // == !is_extended(); pre-asserts NT==ROOT
inline bool  check_if_is_extended();                // recompute flag: false if a1==0 or root==0
static void  check_roots(const Self& a, const Self& b);
inline void  output_maple(std::ostream& os) const;
```

### Numeric operations

```cpp
std::pair<double, double> compute_to_interval() const;   // always recomputes, refreshes cache
std::pair<double, double> to_interval() const;           // uses the cache when FP_TAG == Tag_true
void          simplify();
::CGAL::Sign  sign_() const;                             // exact, unfiltered
::CGAL::Sign  sign()  const;                             // interval-filtered when FP_TAG::value
bool          is_zero() const;
Self          abs() const;

Self& operator += (const Self& p);   Self& operator -= (const Self& p);
Self& operator *= (const Self& p);   Self& operator /= (const Self& p);
Self& operator += (const NT& num);   Self& operator -= (const NT& num);
Self& operator *= (const NT& num);   Self& operator /= (const NT& num);
Self& operator += (CGAL_int(NT) num); /* -=, *=, /= likewise */

CGAL::Comparison_result compare (const CGAL_int(NT)& num) const;
CGAL::Comparison_result compare (const NT& num) const;
CGAL::Comparison_result compare (const Self& y, bool in_same_extension = !ACDE_TAG::value) const;
```

`+ - * /` (binary), `!=`, `<=`, `>`, `>=` come from the Boost operator base classes.
Explicit friends: `operator==(Self,Self)`, `operator<(Self,Self)`, `operator==(Self,NT)`,
`operator==(Self,int)`, `operator<(Self,int)`, `operator>(Self,int)`; plus free
`operator<(Self,NT)` / `operator>(Self,NT)`.

**`compare(Self)` default `in_same_extension = !ACDE_TAG::value = false`** for our instantiation, so
`a0+a1*sqrt(r1)` vs `b0+b1*sqrt(r2)` with `r1 != r2` is handled by the exact
square-and-resolve-signs path. **[verified]** `compare(sqrt(2), 3/2) == SMALLER`.

### `CGAL::Real_embeddable_traits<Sqrt_extension<...>>` (`Sqrt_extension/Real_embeddable_traits.h`)

```cpp
class Sgn        { ::CGAL::Sign        operator()(const Type& x) const; };            // x.sign()
class Compare    { Comparison_result   operator()(const Type& x, const Type& y) const;    // x.compare(y)
                   Comparison_result   operator()(const Type& x, const COEFF& y) const;
                   Comparison_result   operator()(const COEFF& x, const Type& y) const; };
class To_interval{ std::pair<double,double> operator()(const Type& x) const; };       // x.to_interval()
class To_double  { double operator()(const Type& x) const; };
    // == to_double(a0) + to_double(a1)*sqrt(to_double(root)), or to_double(a0) if !is_extended()
```

So `CGAL::to_double(p.x())`, `CGAL::to_interval(p.x())`, `CGAL::sign(v)`, `CGAL::compare(u,v)` all
work on `CoordNT`. **This is the only sanctioned way to get a `double` out of a point.**

### Interval caching (`internal::Interval_optional_caching<Tag>`)

```cpp
template <> class Interval_optional_caching< ::CGAL::Tag_false > { protected:
  void invalidate_interval() {}  bool is_cached() const {return false;}
  std::pair<double,double> cached_value() const;  void update_cached_value(const std::pair<double,double>&) const {} };

template <> class Interval_optional_caching< ::CGAL::Tag_true > { protected:
  typedef std::optional< std::pair<double,double> > Cached_interval;
  mutable Cached_interval interval_;  /* invalidate/is_cached/cached_value/update_cached_value */ };
```

All members are `protected` — invisible to bindings, but they explain the +24 bytes for `Filter=true`.

---

## 3. `CGAL::_One_root_point_2<NumberType_, bool Filter_>` — the traits' `Point_2`

`CGAL/Arr_geometry_traits/Circle_segment_2.h:41-135`.

### `_One_root_point_2_rep<NumberType_, Filter_>` (the rep; internal, `friend` of the handle)

```cpp
public:
  typedef NumberType_                            NT;
  typedef _One_root_point_2_rep<NT, Filter_>     Self;
  typedef Sqrt_extension<NT, NT, Tag_true, Boolean_tag<Filter_> >   CoordNT;   // <-- THE typedef
private:
  CoordNT _x;  CoordNT _y;
public:
  _One_root_point_2_rep();
  _One_root_point_2_rep(const CoordNT& x, const CoordNT& y);
```

### `_One_root_point_2<NumberType_, Filter_> : public Handle_for<_One_root_point_2_rep<...>>`

```cpp
public:
  typedef NumberType_                           NT;
  typedef _One_root_point_2<NT, Filter_>        Self;
  typedef typename Point_rep::CoordNT           CoordNT;

  _One_root_point_2();                                    // (0,0)
  _One_root_point_2(const Self& p);                       // handle copy, O(1)
  _One_root_point_2& operator=(const _One_root_point_2&) = default;
  _One_root_point_2(const CoordNT& x, const CoordNT& y);  // also takes rationals: NT -> CoordNT implicit

  const CoordNT& x() const;                               // returns a REFERENCE into the shared rep
  const CoordNT& y() const;

  bool equals(const Self& p) const;                       // identical() fast path, then CGAL::compare on x,y
  bool operator != (const Self& p) const;                 // !equals
  bool operator == (const Self& p) const;                 // equals

  void set(const NT& x, const NT& y);                     // copy_on_write() then assign
  void set(const CoordNT& x, const CoordNT& y);
```

Free function: `template <typename NT, bool Filter> std::ostream& operator<<(std::ostream&, const _One_root_point_2<NT,Filter>&)`
— prints `to_double(x) ' ' to_double(y)`. **`operator>>` is commented out in the header.**

Inherited from `CGAL::Handle_for` (public part, `Handle_for.h`):

```cpp
typedef T element_type;  typedef std::ptrdiff_t Id_type;
Id_type id() const noexcept;
bool identical(const Handle_for& h) const noexcept;      // pointer equality of the reps
const element_type* Ptr() const noexcept;
bool is_shared() const noexcept;  bool unique() const noexcept;  long use_count() const noexcept;
void swap(Handle_for& h) noexcept;
// copy_on_write(), ptr() are protected
```

**Ownership/lifetime.** `Point_2` is a refcounted COW handle; `sizeof == 8` **[verified]**. `x()`/`y()`
hand out references into the shared rep — they stay valid as long as *any* handle to that rep lives,
but a `set()` on a *unique* handle mutates in place, so never cache a `const CoordNT&` across a
`set()`. For a Cython binding, copy the handle (cheap) rather than the coordinates.

**No ordering.** There is no `operator<`. Use `traits.compare_xy_2_object()` (or
`CGAL::compare(p.x(), q.x())`) if you need a total order.

---

## 4. `CGAL::_Circle_segment_2<Kernel_, bool Filter_>` — the general (non-x-monotone) `Curve_2`

`Circle_segment_2.h:165-598`. Represents a **line segment**, a **circular arc**, or a **full circle**.

```cpp
template <typename Kernel_, bool Filter_>
class _Circle_segment_2 {
public:
  typedef Kernel_                                          Kernel;
  typedef typename Kernel::FT                              NT;
  typedef _One_root_point_2<NT, Filter_>                   Point_2;
  typedef typename Kernel::Circle_2                        Circle_2;
  typedef typename Kernel::Segment_2                       Segment_2;
  typedef typename Kernel::Line_2                          Line_2;
protected:
  typedef typename Point_2::CoordNT                        CoordNT;
  Line_2 m_line;  Circle_2 m_circ;  bool m_is_full;  bool m_has_radius;
  NT m_radius;  Point_2 m_source;  Point_2 m_target;  Orientation m_orient;
```

### All constructors (verbatim)

```cpp
_Circle_segment_2();
    // COLLINEAR, not full, default line/circle/points. Do not use except as a placeholder.

_Circle_segment_2(const Segment_2& seg);
    // line segment from a kernel segment. m_orient = COLLINEAR.

_Circle_segment_2(const typename Kernel::Point_2& ps,
                  const typename Kernel::Point_2& pt);
    // line segment from two RATIONAL endpoints. m_line = Line_2(ps,pt). m_orient = COLLINEAR.
    // (no explicit precondition in the header; Line_2(p,p) is degenerate — ensure ps != pt)

_Circle_segment_2(const Line_2& line,
                  const Point_2& source, const Point_2& target);
    // line segment with ONE-ROOT endpoints on a given supporting line. m_orient = COLLINEAR.
    // \pre Both endpoints lie on the supporting line.
    //   checked as: compare(source.x()*line.a() + line.c(), -source.y()*line.b()) == EQUAL (idem target)

_Circle_segment_2(const Circle_2& circ);
    // FULL circle. m_is_full = true. m_orient = circ.orientation();  asserts m_orient != COLLINEAR.

_Circle_segment_2(const typename Kernel::Point_2& c, const NT& r,
                  Orientation orient = COUNTERCLOCKWISE);
    // FULL circle from center + RADIUS (not squared!). m_circ = Circle_2(c, r*r, orient),
    // m_has_radius = true, m_radius = r.  asserts orient != COLLINEAR.
    // *** PREFER THIS: it makes the vertical tangency points rational. ***

_Circle_segment_2(const Circle_2& circ,
                  const Point_2& source, const Point_2& target);
    // circular ARC on a supporting circle; the CIRCLE's orientation defines the arc's direction.
    // \pre Both endpoints lie on the supporting circle
    //   (compare(square(src.x()-c.x()), sqr_r - square(src.y()-c.y())) == EQUAL, idem target)

_Circle_segment_2(const typename Kernel::Point_2& c,
                  const NT& r, Orientation orient,
                  const Point_2& source, const Point_2& target);
    // circular ARC from center + RADIUS + explicit orientation. m_has_radius = true.
    // asserts orient != COLLINEAR; same endpoint-on-circle preconditions.

_Circle_segment_2(const typename Kernel::Point_2& p1,
                  const typename Kernel::Point_2& p2,
                  const typename Kernel::Point_2& p3);
    // arc through three RATIONAL points: p1 = source, p2 interior, p3 = target.
    // \pre p1 and p3 are not equal  (asserted via compare_xy_2(p1,p3) != EQUAL)
    // If the three points are COLLINEAR it silently builds a SEGMENT p1->p3 instead.
    // Orientation: COUNTERCLOCKWISE iff orientation_2(p1,p2,p3) == LEFT_TURN, else CLOCKWISE.
    // m_has_radius stays false (only the squared radius is computed).
```

There is **no** ctor from `Line_2 + rational endpoints`, none from `(source, target, bulge)`, and
**no `vertex(i)` accessor** (that is `Arr_conic_traits`/polyline vocabulary, not this class).

### Accessors

```cpp
inline Orientation orientation() const;     // COLLINEAR for segments, CLOCKWISE/COUNTERCLOCKWISE else
inline bool is_linear()  const;             // m_orient == COLLINEAR
inline bool is_circular() const;            // m_orient != COLLINEAR
const Line_2&   supporting_line()   const;  // \pre orientation() == COLLINEAR   (returns a reference)
const Circle_2& supporting_circle() const;  // \pre orientation() != COLLINEAR   (returns a reference)
bool is_full() const;
const Point_2& source() const;              // \pre ! is_full()
const Point_2& target() const;              // \pre ! is_full()

unsigned int vertical_tangency_points(Point_2* vpts) const;
    // \pre the curve is circular (m_orient != COLLINEAR)
    // \return 0, 1 or 2; writes into vpts[0..n-1]. CALLER MUST SUPPLY AN ARRAY OF >= 2.
    // For a full circle always returns 2: vpts[0] = leftmost, vpts[1] = rightmost.
```

Private helpers (not bindable, listed for completeness): `_ccw_vertical_tangency_points`, `_quart_index`.

Free: `template <typename Kernel, bool Filter> std::ostream& operator<<(std::ostream&, const _Circle_segment_2<Kernel,Filter>&)`
printing `"segment: src -> trg"` / `"circular arc: <circle> src -> trg"` / `"circular arc: <circle>"`.

**No `bbox()` on `Curve_2`** — only the x-monotone class has one.

### Building the `Kernel::Circle_2` you feed to these ctors (`CGAL/Circle_2.h`)

```cpp
Circle_2(const Point_2& center, const FT& squared_radius, const Orientation& orientation);
Circle_2(const Point_2& center, const FT& squared_radius);          // orientation = COUNTERCLOCKWISE
Circle_2(const Point_2& p, const Point_2& q, const Point_2& r);
Circle_2(const Point_2& p, const Point_2& q, const Orientation&);   // diametral
Circle_2(const Point_2& p, const Point_2& q);
Circle_2(const Point_2& p, const Point_2& q, const FT& bulge);
Circle_2(const Point_2& center, const Orientation&);                // radius 0
```

---

## 5. `CGAL::_X_monotone_circle_segment_2<Kernel_, bool Filter_>` — the traits' `X_monotone_curve_2`

`Circle_segment_2.h:630-2062`. Stores 3 coefficients that mean *either* `(x0, y0, r²)` for a circle
*or* `(a, b, c)` for the line `ax+by+c=0`, plus source/target and a bit-packed `m_info`.

```cpp
public:
  typedef Kernel_                                          Kernel;
  typedef _X_monotone_circle_segment_2<Kernel, Filter_>    Self;
  typedef typename Kernel::FT                              NT;
  typedef _One_root_point_2<NT, Filter_>                   Point_2;
  typedef typename Kernel::Circle_2                        Circle_2;
  typedef typename Kernel::Line_2                          Line_2;
  typedef typename Point_2::CoordNT                        CoordNT;

  typedef std::pair<unsigned int, unsigned int>            Curve_id_pair;
  typedef unsigned int                                     Multiplicity;
  typedef std::pair<Point_2, Multiplicity>                 Intersection_point;
  typedef std::list<Intersection_point>                    Intersection_list;
  struct Less_id_pair { bool operator()(const Curve_id_pair&, const Curve_id_pair&) const; };
  typedef std::map<Curve_id_pair, Intersection_list, Less_id_pair> Intersection_map;
  typedef typename Intersection_map::value_type            Intersection_map_entry;
  typedef typename Intersection_map::iterator              Intersection_map_iterator;
```

`m_info` bit layout (protected enum, useful to know when debugging):
`bit0 IS_DIRECTED_RIGHT_MASK`, `bit1 IS_VERTICAL_SEGMENT_MASK`, `bits2-3 orientation`
(`COUNTERCLOCKWISE_CODE = 4`, `CLOCKWISE_CODE = 8`), `bits ≥4` = curve index (`INDEX_SHIFT_BITS = 4`).

### Constructors

```cpp
_X_monotone_circle_segment_2();                                        // all-zero placeholder

_X_monotone_circle_segment_2(const Line_2& line,
                             const Point_2& source, const Point_2& target,
                             unsigned int index = 0);
    // \pre source and target differ; if source.x()==target.x() then sign(line.b())==0 (vertical)

_X_monotone_circle_segment_2(const typename Kernel::Point_2& source,
                             const typename Kernel::Point_2& target);
    // straight segment directly from two RATIONAL points. \pre source != target.

_X_monotone_circle_segment_2(const Circle_2& circ,
                             const Point_2& source, const Point_2& target,
                             Orientation orient,
                             unsigned int index = 0);
    // \pre compare(source.x(), target.x()) != EQUAL   (an x-monotone arc is never vertical)
    // \pre orient != COLLINEAR
```

`index` is the "curve id" used by the intersection cache; `0` means "no id / do not cache".

### Public member functions

```cpp
inline bool is_linear () const;                 // (m_info & ORIENTATION_MASK) == 0
inline bool is_circular () const;

Line_2   supporting_line()   const;             // \pre is_linear();   returns BY VALUE: Line_2(a(),b(),c())
Circle_2 supporting_circle() const;             // \pre is_circular(); returns BY VALUE:
                                                //   Circle_2(Point_2(x0(),y0()), sqr_r(), orientation())

inline const Point_2& source() const;
inline const Point_2& target() const;
bool is_directed_right() const;                 // m_info & IS_DIRECTED_RIGHT_MASK
bool has_left()  const;                         // always true (bounded traits)
bool has_right() const;                         // always true
inline const Point_2& left()  const;            // source if directed right, else target
inline const Point_2& right() const;

bool is_in_x_range(const Point_2& p) const;
inline bool is_vertical() const;                // vertical LINE SEGMENT (never true for arcs)
inline Orientation orientation() const;         // COLLINEAR / CLOCKWISE / COUNTERCLOCKWISE

Comparison_result point_position(const Point_2& p) const;
    // SMALLER: p below the curve; LARGER: above; EQUAL: on it.
    // \pre (linear case) is_in_x_range(p)

Comparison_result compare_to_right(const Self& cv, const Point_2& p) const;
Comparison_result compare_to_left (const Self& cv, const Point_2& p) const;
    // \pre p lies on both curves and both are defined to the right/left of p

bool has_same_supporting_curve(const Self& cv) const;
    // true if indices match (non-zero), else compares circle center+r^2, or scaled line coefficients
bool equals(const Self& cv) const;
    // same supporting curve AND same endpoint set; opposite segments/arcs compare EQUAL

void split(const Point_2& p, Self& c1, Self& c2) const;
    // c1 gets p as its right end, c2 as its left end (arg order respects is_directed_right()).
    // \pre (enforced by the traits' Split_2) p lies on cv and is not an endpoint.

template <typename OutputIterator>
OutputIterator intersect(const Self& cv, OutputIterator oi,
                         Intersection_map* inter_map = nullptr) const;
    // writes std::pair<Point_2, Multiplicity> (as Intersection_point) and/or a Self for an overlap.
    // For overlapping arcs it writes exactly ONE Self and returns.
    // Multiplicity is 0 (undefined) for shared endpoints of co-supported arcs.
    // Uses/fills the cache only when inter_map != nullptr AND both indices are non-zero.

bool can_merge_with(const Self& cv) const;      // same supporting curve && share an endpoint
void merge(const Self& cv);                     // \pre can_merge_with(cv)
Self construct_opposite() const;                // flips direction bit and orientation bits

Bbox_2 bbox() const;
    // double-precision box; for circular arcs it widens y to the FULL circle extremum on the
    // relevant side (upper arc -> y_max = cy + sqrt(r^2)), so it is SAFE but LOOSE.
    // Computed with plain to_double(), i.e. no directed rounding.

template <class OutputIterator>
void approximate(OutputIterator oi, unsigned int n) const;
    // LEGACY fixed-sample API. Emits std::pair<double,double>.
    // Linear: exactly 2 points (source, target — NOT left/right).
    // Circular: n+1 points, equally spaced in x from left to right, y from the circle equation.
    // NOTE: uses source()/target() for the endpoints but marches x from x_left to x_right,
    // so for a right-to-left arc the first emitted point is the RIGHT one. Caller beware.

Self trim(const Point_2& ps, const Point_2& pt) const;
    // \pre Both ps and pt lie on the arc and conform with the current direction of the arc.
    // Returns a copy with m_source = ps, m_target = pt. No validation.
```

Protected (not bindable, but they tell you what `m_first/m_second/m_third` mean):
`_index()`, `x0()`, `y0()`, `sqr_r()`, `_is_upper()`, `a()`, `b()`, `c()`, plus the
`_line_point_position`, `_circ_point_position`, `_*_compare_to_right/left`, `_lines_intersect`,
`_circ_line_intersect`, `_circs_intersect`, `_is_between_endpoints`,
`_is_strictly_between_endpoints`, `_compute_overlap` helpers.

Free: `operator<<` printing `"(<circle>) [src --> trg]"`. **No `operator>>`.**

**Value semantics.** `sizeof == 48` **[verified]**; plain copyable, no handles apart from the two
`Point_2` members and the kernel `FT`s. `split`/`trim`/`construct_opposite` all return/produce
independent values — no aliasing to worry about.

---

## 6. `CGAL::Arr_circle_segment_traits_2<Kernel_, bool Filter = true>`

`CGAL/Arr_circle_segment_traits_2.h`.

```cpp
template <typename Kernel_, bool Filter = true>
class Arr_circle_segment_traits_2 {
public:
  typedef Kernel_                                        Kernel;
  typedef typename Kernel::FT                            NT;
  typedef typename Kernel::Point_2                       Rational_point_2;
  typedef typename Kernel::Segment_2                     Rational_segment_2;
  typedef typename Kernel::Circle_2                      Rational_circle_2;
  typedef _One_root_point_2<NT, Filter>                  Point_2;
  typedef typename Point_2::CoordNT                      CoordNT;
  typedef _Circle_segment_2<Kernel, Filter>              Curve_2;
  typedef _X_monotone_circle_segment_2<Kernel, Filter>   X_monotone_curve_2;
  typedef unsigned int                                   Multiplicity;
  typedef Arr_circle_segment_traits_2<Kernel, Filter>    Self;

  // Category tags:
  typedef Tag_true                                   Has_left_category;
  typedef Tag_true                                   Has_merge_category;      // <-- yes
  typedef Tag_false                                  Has_do_intersect_category;

  typedef Arr_oblivious_side_tag                     Left_side_category;
  typedef Arr_oblivious_side_tag                     Bottom_side_category;
  typedef Arr_oblivious_side_tag                     Top_side_category;
  typedef Arr_oblivious_side_tag                     Right_side_category;

protected:
  typedef typename X_monotone_curve_2::Intersection_map   Intersection_map;
  mutable Intersection_map inter_map;
  bool m_use_cache;

public:
  Arr_circle_segment_traits_2 (bool use_intersection_caching = false);
  static unsigned int get_index ();     // ++ of a process-wide static std::atomic<unsigned int>
```

`Kernel` must be a **rational (exact-field) kernel**. Verified working:
`CGAL::Cartesian<CGAL::Gmpq>` and `CGAL::Exact_predicates_exact_constructions_kernel` **[verified]**.

### Functors — exact signatures

All functor classes are nested and public; `*_2_object()` members are `const`.

```cpp
class Compare_x_2 {
public:  Comparison_result operator() (const Point_2& p1, const Point_2& p2) const; };
Compare_x_2 compare_x_2_object () const;

class Compare_xy_2 {
public:  Comparison_result operator() (const Point_2& p1, const Point_2& p2) const; };
Compare_xy_2 compare_xy_2_object () const;

class Construct_min_vertex_2 {
public:  const Point_2& operator() (const X_monotone_curve_2& cv) const; };   // cv.left()
Construct_min_vertex_2 construct_min_vertex_2_object () const;

class Construct_max_vertex_2 {
public:  const Point_2& operator() (const X_monotone_curve_2& cv) const; };   // cv.right()
Construct_max_vertex_2 construct_max_vertex_2_object () const;

class Is_vertical_2 {
public:  bool operator() (const X_monotone_curve_2& cv) const; };
Is_vertical_2 is_vertical_2_object () const;

class Compare_y_at_x_2 {
public:  Comparison_result operator() (const Point_2& p, const X_monotone_curve_2& cv) const;
         // \pre p is in the x-range of cv
};
Compare_y_at_x_2 compare_y_at_x_2_object () const;

class Compare_y_at_x_right_2 {
public:  Comparison_result operator() (const X_monotone_curve_2& cv1,
                                       const X_monotone_curve_2& cv2,
                                       const Point_2& p) const;
         // \pre p lies on both curves and both are defined to its right
};
Compare_y_at_x_right_2 compare_y_at_x_right_2_object () const;

class Compare_y_at_x_left_2 {
public:  Comparison_result operator() (const X_monotone_curve_2& cv1,
                                       const X_monotone_curve_2& cv2,
                                       const Point_2& p) const;
         // \pre p lies on both curves and both are defined to its left
};
Compare_y_at_x_left_2 compare_y_at_x_left_2_object () const;

class Equal_2 {
public:  bool operator() (const X_monotone_curve_2& cv1, const X_monotone_curve_2& cv2) const;
         bool operator() (const Point_2& p1, const Point_2& p2) const; };
Equal_2 equal_2_object () const;
```

#### Approximation

```cpp
typedef double                                        Approximate_number_type;
typedef CGAL::Cartesian<Approximate_number_type>      Approximate_kernel;
typedef Approximate_kernel::Point_2                   Approximate_point_2;   // Cartesian<double>::Point_2

class Approximate_2 {
protected:
  using Traits = Arr_circle_segment_traits_2<Kernel, Filter>;
  const Traits& m_traits;
  Approximate_2(const Traits& traits);          // PROTECTED; friend class Arr_circle_segment_traits_2
public:
  Approximate_number_type operator()(const Point_2& p, int i) const;
      // \pre i is either 0 or 1.  Returns to_double(p.x()) / to_double(p.y()).
  Approximate_point_2     operator()(const Point_2& p) const;
  template <typename OutputIterator>
  OutputIterator operator()(const X_monotone_curve_2& xcv, double error,
                            OutputIterator oi, bool l2r = true) const;
      // dereference type: Approximate_point_2
      // linear  -> exactly 2 points (min,max) or (max,min) per l2r
      // circular-> adaptive bisection in the arc parameter until the sagitta error < `error`,
      //            always emitting the two endpoints first/last (in l2r order).
private:
  // approximate_segment, add_points, circular_point, transform_point, approximate_arc
};
Approximate_2 approximate_2_object() const;   // { return Approximate_2(*this); }
```

**[verified]** `approximate_2_object()(xcv, 0.01, back_inserter(v), true)` on a half-circle of radius 2
produced 17 `Approximate_point_2`. `CGAL::draw(arr)` uses exactly this overload with `error = 0.01`
(`draw_arrangement_2.h:206-227`), so drawing works out of the box.

#### Subdivision / intersection / merging

```cpp
class Make_x_monotone_2 {
private: typedef Arr_circle_segment_traits_2<Kernel_, Filter> Self;  bool m_use_cache;
public:
  Make_x_monotone_2(bool use_cache = false);
  template <typename OutputIterator>
  OutputIterator operator()(const Curve_2& cv, OutputIterator oi) const;
      // "Its dereference type is a variant that wraps a Point_2 or an X_monotone_curve_2 objects."
      // => std::variant<Point_2, X_monotone_curve_2>              [verified]
      // \pre sign(supporting_circle().squared_radius()) != NEGATIVE
      // degenerate circle (r == 0) -> emits ONE Point_2 (the center) and nothing else
      // full circle  -> exactly 2 arcs (lower/upper), split at the two vertical tangency points
      // arc          -> 1, 2 or 3 arcs depending on the number of tangency points it contains
      // segment      -> 1 X_monotone_curve_2
};
Make_x_monotone_2 make_x_monotone_2_object() const;   // { return Make_x_monotone_2(m_use_cache); }

class Split_2 {
public:  void operator() (const X_monotone_curve_2& cv, const Point_2& p,
                          X_monotone_curve_2& c1, X_monotone_curve_2& c2) const;
         // c1 = left subcurve (p is its right endpoint), c2 = right subcurve
         // \pre p lies on cv but is not one of its end-points
};
Split_2 split_2_object () const;

class Intersect_2 {
private: Intersection_map& _inter_map;              // REFERENCE into the traits object
public:
  Intersect_2(Intersection_map& map);               // public ctor
  template <typename OutputIterator>
  OutputIterator operator()(const X_monotone_curve_2& cv1,
                            const X_monotone_curve_2& cv2,
                            OutputIterator oi) const;
      // dereference type: std::variant<std::pair<Point_2, Multiplicity>, X_monotone_curve_2>
};
Intersect_2 intersect_2_object() const;             // { return Intersect_2(inter_map); }

class Are_mergeable_2 {
public:  bool operator() (const X_monotone_curve_2& cv1, const X_monotone_curve_2& cv2) const; };
Are_mergeable_2 are_mergeable_2_object () const;

class Merge_2 {
protected: typedef Arr_circle_segment_traits_2<Kernel, Filter> Traits;
           const Traits* m_traits;
           Merge_2(const Traits* traits);           // PROTECTED; friend class Arr_circle_segment_traits_2
public:  void operator() (const X_monotone_curve_2& cv1, const X_monotone_curve_2& cv2,
                          X_monotone_curve_2& c) const;
         // \pre The two curves are mergeable
};
Merge_2 merge_2_object () const;                    // { return Merge_2(this); }

class Compare_endpoints_xy_2 {
public:  Comparison_result operator()(const X_monotone_curve_2& cv) const;
         // SMALLER if cv.is_directed_right(), else LARGER
};
Compare_endpoints_xy_2 compare_endpoints_xy_2_object() const;

class Construct_opposite_2 {
public:  X_monotone_curve_2 operator()(const X_monotone_curve_2& cv) const; };
Construct_opposite_2 construct_opposite_2_object() const;

class Trim_2 {
protected: typedef Arr_circle_segment_traits_2<Kernel, Filter> Traits;
           const Traits& m_traits;
           Trim_2(const Traits& traits);            // PROTECTED; friend class Arr_circle_segment_traits_2
public:  X_monotone_curve_2 operator()(const X_monotone_curve_2& xcv,
                                       const Point_2& src, const Point_2& tgt) const;
         // \pre src != tgt
         // \pre both points must be interior and must lie on cv
         // It normalises the order to match xcv's direction before calling xcv.trim().
};
Trim_2 trim_2_object() const;
```

### NOT present (checked exhaustively)

`Construct_curve_2`, `Construct_x_monotone_curve_2`, `Do_intersect_2`, `Push_back_2`,
`Number_of_points_2`, `Parameter_space_in_x/y_2`, `Compare_x_on_boundary_2` (unnecessary —
all four side categories are `Arr_oblivious_side_tag`), `Is_on_x_identification_2`,
any cache-clearing method.

### Integration matrix (all **[verified]** by compiling)

| Consumer | Works? |
|---|---|
| `CGAL::Arrangement_2<Traits>` | yes |
| `CGAL::Arrangement_with_history_2<Traits>` | **yes** (`Has_merge_category = Tag_true`) |
| `CGAL::Arr_naive/walk/trapezoid_ric_point_location` | yes |
| `CGAL::Arr_landmarks_point_location` | **NO — compile error** (needs `Construct_x_monotone_curve_2`) |
| `CGAL::Arr_polycurve_traits_2<Traits>` | **yes**, both `Curve_2` and `X_monotone_curve_2` levels |
| `CGAL::draw(arr)` | yes (via the `Approximate_2` curve overload) |
| `CGAL::Gps_circle_segment_traits_2` / `General_polygon_set_2` | yes |
| `CGAL::Exact_predicates_exact_constructions_kernel` as `Kernel` | yes |

Minimal smoke test that ran: circle r=2 at origin + segment (-5,0)→(5,0) ⇒ `V=4 E=5 F=3`;
adding a 3-point arc ⇒ `V=6 E=8 F=4`.

### Polycurve wrapping (recipe)

```cpp
using SubTraits  = CGAL::Arr_circle_segment_traits_2<Kernel>;
using PolyTraits = CGAL::Arr_polycurve_traits_2<SubTraits>;
PolyTraits tr;
std::vector<SubTraits::X_monotone_curve_2> segs = {...};      // must chain source->target, x-monotone
PolyTraits::X_monotone_curve_2 xp = tr.construct_x_monotone_curve_2_object()(segs.begin(), segs.end());
std::vector<SubTraits::Curve_2> cs = {...};
PolyTraits::Curve_2            cp = tr.construct_curve_2_object()(cs.begin(), cs.end());
```

`Arr_polycurve_basic_traits_2::Construct_x_monotone_curve_2` needs only
`construct_min_vertex_2_object`, `construct_max_vertex_2_object`, `equal_2_object`,
`compare_endpoints_xy_2_object` and `construct_opposite_2_object` from the subcurve traits —
all present. `Arr_polycurve_traits_2` re-exports
`Has_merge_category = Subcurve_traits_2::Has_merge_category` (so `Tag_true` here),
`Multiplicity = Subcurve_traits_2::Multiplicity`, `Subcurve_2 = Subcurve_traits_2::Curve_2`.
Preconditions on the range: at least one subcurve, no degenerate subcurve, all subcurves in the same
direction, consecutive subcurves share an endpoint.

**[verified]** `construct_x_monotone_curve_2_object()` on 2 chained segments → `number_of_subcurves()==2`,
inserted into `Arrangement_2<PolyTraits>` giving `V=2 E=1`.

### Intersection cache — semantics and hazards

* `Arr_circle_segment_traits_2(true)` turns caching on. Then `Make_x_monotone_2` stamps every emitted
  `X_monotone_curve_2` with `index = get_index()` (a **process-wide** `std::atomic<unsigned int>`,
  starting at 1). Sub-arcs of the same input `Curve_2` share one index.
* `Intersect_2` then keys `inter_map` by the *sorted pair* of indices and stores the full intersection
  list of the two **supporting** curves, filtering per-arc on retrieval.
* Index `0` (the default of both x-monotone ctors) disables caching for that curve and also disables
  the `_index() != 0 && _index() == cv._index()` fast paths in `has_same_supporting_curve`,
  `_lines_compare_to_right` and `_circs_compare_to_right`.
* The map is `mutable` and **never cleared**. Copying the traits copies the map. Sharing one traits
  object between arrangements is safe (indices are globally unique) but the map grows monotonically.
* `unsigned int` wraparound after 2³² curves is a theoretical correctness hazard.

---

## 7. `CGAL::Gps_circle_segment_traits_2<Kernel_, bool Filer_ = true>`

`CGAL/Gps_circle_segment_traits_2.h` — the whole file, verbatim body:

```cpp
template <class Kernel_, bool Filer_ = true>          // NOTE: "Filer_" is a typo in CGAL, harmless
class Gps_circle_segment_traits_2 :
  public Gps_traits_2<Arr_circle_segment_traits_2<Kernel_, Filer_> >
{
public:
  Gps_circle_segment_traits_2(bool use_cache = false) :
    Gps_traits_2<Arr_circle_segment_traits_2<Kernel_, Filer_> >()
  { this->m_use_cache = use_cache; }
};
```

So it is `Gps_traits_2<Arr_circle_segment_traits_2<K,F>>` plus a ctor that pokes the protected
`m_use_cache`. **It inherits everything from `Arr_circle_segment_traits_2` publicly** —
`Curve_2`, `Point_2`, `CoordNT`, all the functors — and adds the general-polygon layer.

### `CGAL::Gps_traits_2<ArrTraits_2, GeneralPolygon_2 = General_polygon_2<ArrTraits_2>>`

```cpp
template <typename ArrTraits_2,
          typename GeneralPolygon_2 = General_polygon_2<ArrTraits_2> >
class Gps_traits_2 : public ArrTraits_2 {
  typedef ArrTraits_2                                   Base;
  typedef Gps_traits_2<ArrTraits_2, GeneralPolygon_2>   Self;
public:
  typedef typename Base::Point_2                        Point_2;
  typedef typename Base::X_monotone_curve_2             X_monotone_curve_2;
  typedef typename Base::Multiplicity                   Multiplicity;

  typedef GeneralPolygon_2                              Polygon_2;
  typedef Polygon_2                                     General_polygon_2;          // back-compat
  typedef CGAL::General_polygon_with_holes_2<Polygon_2> Polygon_with_holes_2;
  typedef Polygon_with_holes_2                          General_polygon_with_holes_2;

  typedef typename Polygon_2::Curve_const_iterator      Curve_const_iterator;
  typedef typename Polygon_with_holes_2::Hole_const_iterator  Hole_const_iterator;

  typedef typename Base::Compare_endpoints_xy_2         Compare_endpoints_xy_2;
  typedef typename Base::Construct_min_vertex_2         Construct_min_vertex_2;
  typedef typename Base::Construct_max_vertex_2         Construct_max_vertex_2;
  typedef Gps_traits_adaptor<Base>                      Traits_adaptor;
```

Note `Curve_2` is **not** re-typedef'd but is inherited from the base, so
`Gps_circle_segment_traits_2<K>::Curve_2` resolves to `_Circle_segment_2<K,F>` **[verified]**.

Functors added by `Gps_traits_2`:

```cpp
class Construct_polygon_2 {
public:  template <class XCurveIterator>
         void operator()(XCurveIterator begin, XCurveIterator end, Polygon_2& pgn) const;  // pgn.init(...)
};
Construct_polygon_2 construct_polygon_2_object() const;

class Construct_curves_2 {
public:  std::pair<Curve_const_iterator, Curve_const_iterator>
         operator()(const Polygon_2& pgn) const; };
Construct_curves_2 construct_curves_2_object() const;

class Construct_outer_boundary {
public:  Polygon_2 operator()(const Polygon_with_holes_2& pol_wh) const; };   // BY VALUE (copies)
Construct_outer_boundary construct_outer_boundary_object() const;

class Construct_holes {
public:  std::pair<Hole_const_iterator, Hole_const_iterator>
         operator()(const Polygon_with_holes_2& pol_wh) const; };
Construct_holes construct_holes_object() const;

class Construct_polygon_with_holes_2 {
public:  Polygon_with_holes_2 operator()(const Polygon_2& pgn_boundary) const;
         template <class HolesInputIterator>
         Polygon_with_holes_2 operator()(const Polygon_2& pgn_boundary,
                                         HolesInputIterator h_begin,
                                         HolesInputIterator h_end) const;
         // "If outer is an empty general polygon, then an unbounded polygon with holes will be
         //  created. The holes must be contained inside the outer boundary, and the polygons
         //  representing the holes must be strictly simple and pairwise disjoint, except perhaps
         //  at the vertices."
};
Construct_polygon_with_holes_2 construct_polygon_with_holes_2_object() const;

class Is_unbounded {
public:  bool operator()(const Polygon_with_holes_2& pol_wh) const; };   // pol_wh.is_unbounded()
Is_unbounded is_unbounded_object() const;

class Equal_2 {
protected: const Base& m_traits;  Equal_2(const Base& traits);   // PROTECTED; friend class Gps_traits_2
public:  bool operator()(const Point_2& p1, const Point_2& p2) const;
         bool operator()(const X_monotone_curve_2& cv1, const X_monotone_curve_2& cv2) const;
         bool operator()(const Polygon_2& pgn1, const Polygon_2& pgn2) const;       // cyclic-rotation aware
         bool operator()(const Polygon_with_holes_2& pgn1, const Polygon_with_holes_2& pgn2) const;
             // *** BUGGY: `if (pgn1.number_of_holes(), pgn2.number_of_holes()) return false;` ***
};
Equal_2 equal_2_object() const;
```

**This `Equal_2` shadows the base `Arr_circle_segment_traits_2::Equal_2`.** For point/curve equality
the behaviour is identical (it just forwards), so the arrangement machinery is unaffected.

### `CGAL::General_polygon_2<Arr_traits>` (`CGAL/General_polygon_2.h`)

```cpp
template <class Arr_traits>
class General_polygon_2 {
public:
  typedef Arr_traits                                 Traits_2;
  typedef typename Traits_2::Point_2                 Point_2;
  typedef typename Traits_2::Curve_2                 Curve_2;
  typedef typename Traits_2::X_monotone_curve_2      X_monotone_curve_2;
  typedef std::list<X_monotone_curve_2>              Containter;          // sic
  typedef typename Containter::iterator              Curve_iterator;
  typedef typename Containter::const_iterator        Curve_const_iterator;
  typedef X_monotone_curve_2                         value_type;
protected:
  std::list<X_monotone_curve_2>    m_xcurves;
public:
  General_polygon_2();
  template <typename CurveIterator> General_polygon_2(CurveIterator begin, CurveIterator end);
  template <class CurveIterator> void init(CurveIterator begin, CurveIterator end);   // clear + insert
  template <class CurveIterator> void insert(CurveIterator begin, CurveIterator end); // append
  bool is_empty() const;
  unsigned int size() const;
  Curve_iterator       curves_begin();        Curve_iterator       curves_end();
  Curve_const_iterator curves_begin() const;  Curve_const_iterator curves_end() const;
  void push_back(const X_monotone_curve_2& cv);
  void clear();
  Curve_iterator erase(Curve_iterator it);
  Orientation orientation() const;            // via Gps_traits_adaptor<Traits_2>::Orientation_2
  void reverse_orientation();                 // list.reverse() + Construct_opposite_2 on each curve
  template <class OutputIterator> void approximate(OutputIterator oi, unsigned int n) const;
      // calls X_monotone_curve_2::approximate(oi, n) on each curve => std::pair<double,double>
  Bbox_2 bbox() const;                        // union of per-curve bbox()
};
```

Free `operator<<` (ASCII/BINARY/PRETTY via `IO::get_mode`) and `operator>>`. The `operator>>`
instantiation would need `istream >> X_monotone_curve_2`, which does not exist for
`_X_monotone_circle_segment_2` — **do not instantiate it**.

`General_polygon_2` performs **no validation**: it is just an ordered list of x-monotone curves.
It is the caller's job to make it closed, simple and counterclockwise.

### `CGAL::General_polygon_with_holes_2<Polygon_>` (`CGAL/General_polygon_with_holes_2.h`)

```cpp
template <typename Polygon_> class General_polygon_with_holes_2 {
public:
  typedef Polygon_                                    Polygon_2;
  typedef Polygon_2                                   General_polygon_2;         // back-compat
  typedef std::deque<Polygon_2>                       Holes_container;
  typedef typename Holes_container::iterator          Hole_iterator;
  typedef typename Holes_container::const_iterator    Hole_const_iterator;
  typedef unsigned int                                Size;

  General_polygon_with_holes_2() = default;
  explicit General_polygon_with_holes_2(const Polygon_2& pgn_boundary);
  explicit General_polygon_with_holes_2(Polygon_2&& pgn_boundary);
  template <typename HolesInputIterator>
  General_polygon_with_holes_2(const Polygon_2& pgn_boundary, HolesInputIterator, HolesInputIterator);
  template <typename HolesInputIterator>
  General_polygon_with_holes_2(Polygon_2&& pgn_boundary, HolesInputIterator, HolesInputIterator);

  Holes_container&       holes();          const Holes_container& holes() const;
  Hole_iterator          holes_begin();    Hole_iterator          holes_end();
  Hole_const_iterator    holes_begin() const;  Hole_const_iterator holes_end() const;
  bool                   is_unbounded() const;         // outer boundary empty
  Polygon_2&             outer_boundary();  const Polygon_2& outer_boundary() const;
  void add_hole(const Polygon_2& pgn_hole);   void add_hole(Polygon_2&& pgn_hole);
  void erase_hole(Hole_iterator hit);
  void clear_outer_boundary();  void clear_holes();  void clear();
  bool has_holes() const;
  Size number_of_holes() const;
  bool is_plane() const;                                // empty boundary AND no holes
  bool is_empty() const;
};
```

### Validation helpers (`CGAL/Boolean_set_operations_2/Gps_polygon_validation.h`)

```cpp
template <typename Traits_2> bool is_closed_polygon (const typename Traits_2::Polygon_2&, const Traits_2&);
template <typename Traits_2> bool is_simple_polygon (const typename Traits_2::Polygon_2&, const Traits_2&);
template <typename Traits_2> bool has_valid_orientation_polygon(const typename Traits_2::Polygon_2&, const Traits_2&);
template <typename Traits_2> bool is_valid_polygon  (const typename Traits_2::Polygon_2&, const Traits_2&);
    // closed && simple && COUNTERCLOCKWISE

template <typename Traits_2> bool is_closed_polygon_with_holes (const typename Traits_2::Polygon_with_holes_2&, const Traits_2&);
template <typename Traits_2> bool is_valid_polygon_with_holes  (const typename Traits_2::Polygon_with_holes_2&, const Traits_2&);
template <typename Traits_2> bool is_valid_unknown_polygon(const typename Traits_2::Polygon_2&, const Traits_2&);
template <typename Traits_2> bool is_valid_unknown_polygon(const typename Traits_2::Polygon_with_holes_2&, const Traits_2&);
```

These emit `CGAL_warning_msg` to stderr on failure and return `false`. **[verified]**
`is_valid_polygon(ccw_rect, tr) == true`; after `reverse_orientation()` it prints
`"The polygon has a wrong orientation."` and returns `false`.

### Recipe: circle → general polygon (there is no library helper)

```cpp
using GT = CGAL::Gps_circle_segment_traits_2<Kernel>;
GT::Polygon_2 circle_polygon(const Kernel::Point_2& c, const Kernel::FT& r2, const GT& tr) {
  GT::Curve_2 cv(Kernel::Circle_2(c, r2));                      // CCW by default
  std::list<std::variant<GT::Point_2, GT::X_monotone_curve_2>> objs;
  tr.make_x_monotone_2_object()(cv, std::back_inserter(objs));  // -> exactly 2 arcs
  GT::Polygon_2 pgn;
  for (auto& o : objs) if (auto* x = std::get_if<GT::X_monotone_curve_2>(&o)) pgn.push_back(*x);
  return pgn;                                                   // size()==2, orientation()==CCW
}
```

**[verified]** two such circles (r²=4 at (0,0) and (2,0)): `join` → 1 PWH with a 4-curve outer
boundary and 0 holes; `CGAL::intersection` → 1 PWH.

### Recipe: `CGAL::Polygon_2<Kernel>` → general polygon (no library helper for this traits)

```cpp
GT::Polygon_2 from_kernel_polygon(const CGAL::Polygon_2<Kernel>& kp) {
  GT::Polygon_2 out;
  for (auto e = kp.edges_begin(); e != kp.edges_end(); ++e)
    out.push_back(GT::X_monotone_curve_2(e->source(), e->target()));  // rational-point xcv ctor
  return out;
}
```

**[verified]** produces `size()==4`, `orientation()==COUNTERCLOCKWISE`, `is_valid_polygon()==true`
for a CCW rectangle. `CGAL::convert_polygon(kp, traits)` is **not** usable here — it requires
`traits.construct_curve_2_object()` and is only meant for `Arr_polyline_traits_2`
(see `Gps_polyline_traits` in `Polygon_conversions.h:31-36`).

### `CGAL::Gps_traits_adaptor<Traits_>` (used internally by `General_polygon_2::orientation()`)

```cpp
template <class Traits_> class Gps_traits_adaptor : public Traits_ {
public:
  class Construct_vertex_2 { public: Point_2 operator()(const X_monotone_curve_2& cv, int i) const; };
  Construct_vertex_2 construct_vertex_2_object() const;
  class Orientation_2 { public:
    template <class CurveInputIteraor>
    Orientation operator()(CurveInputIteraor begin, CurveInputIteraor end) const; };
  Orientation_2 orientation_2_object() const;
};
```

Requires `Compare_xy_2`, `Compare_y_at_x_right_2`, `Compare_endpoints_xy_2`,
`Construct_min_vertex_2`, `Construct_max_vertex_2` from the base — all present.

---

## 8. The three `Circular_kernel_2` traits (brief) — and why not to bind them

All three are in `CGAL::` and take `template <typename CircularKernel>`. Use with e.g.
`CGAL::Exact_circular_kernel_2`. All three share:

```cpp
typedef CircularKernel Kernel;
typedef typename CircularKernel::Circular_arc_point_2  Point;      // legacy name
typedef typename CircularKernel::Circular_arc_point_2  Point_2;
typedef unsigned int                                   Multiplicity;
typedef CGAL::Tag_false  Has_left_category;
typedef CGAL::Tag_false  Has_merge_category;
typedef CGAL::Tag_false  Has_do_intersect_category;
typedef Arr_oblivious_side_tag  Left_side_category, Bottom_side_category,
                                Top_side_category, Right_side_category;
Arr_..._traits_2(const CircularKernel& k = CircularKernel());     // stores `CircularKernel ck;`
```

None of them has `Approximate_2`, `Trim_2`, `Compare_endpoints_xy_2`, `Construct_opposite_2`,
`Are_mergeable_2`, `Merge_2`, `Compare_y_at_x_left_2`, `Construct_curve_2`, or
`Construct_x_monotone_curve_2`.

### `Arr_circular_arc_traits_2<CircularKernel>` (`Arr_circular_arc_traits_2.h`)

```cpp
typedef internal::Non_x_monotonic_Circular_arc_2<CircularKernel>  Curve_2;
typedef typename CircularKernel::Circular_arc_2                   X_monotone_curve_2;
// functors forwarded from the kernel:
Compare_x_2, Compare_xy_2, Compare_y_at_x_2,
Compare_y_at_x_right_2 = CircularKernel::Compare_y_to_right_2,
Construct_max_vertex_2 = CircularKernel::Construct_circular_max_vertex_2,
Construct_min_vertex_2 = CircularKernel::Construct_circular_min_vertex_2,
Equal_2, Split_2, Intersect_2, Is_vertical_2
// locally defined:
class Make_x_monotone_2 { public: template <typename OutputIterator>
  OutputIterator operator()(const Curve_2& arc, OutputIterator oi) const; };
  // internally: calls the kernel functor into std::vector<std::variant<Point_2,X_monotone_curve_2>>
  // and re-emits; dereference type is std::variant<Point_2, X_monotone_curve_2>
```

`internal::Non_x_monotonic_Circular_arc_2 : public CircularKernel::Circular_arc_2` ctors:
`()`, `(Circle_2)`, `(Circle_2, Line_2, bool, Line_2, bool)`, `(Circle_2, Circle_2, bool, Circle_2, bool)`,
`(Point_2 start, Point_2 middle, Point_2 end)`, `(Circle_2, Circular_arc_point_2 begin, Circular_arc_point_2 end)`,
`(Point_2 start, Point_2 end, FT bulge)`, `(Base)`.

### `Arr_line_arc_traits_2<CircularKernel>` (`Arr_line_arc_traits_2.h`)

```cpp
typedef typename CircularKernel::Line_arc_2  Curve_2;
typedef typename CircularKernel::Line_arc_2  X_monotone_curve_2;      // same type!
class Make_x_monotone_2 { public: template <typename OutputIterator>
  OutputIterator operator()(const Curve_2& line, OutputIterator oi) const
  { typedef std::variant<Point_2, X_monotone_curve_2> Make_x_monotone_result;
    *oi++ = Make_x_monotone_result(line); return oi; } };
```

Segments only. Strictly weaker than `Arr_segment_traits_2`; no reason to bind.

### `Arr_circular_line_arc_traits_2<CircularKernel>` (`Arr_circular_line_arc_traits_2.h`)

```cpp
typedef typename CircularKernel::Line_arc_2                Arc1;   // private
typedef typename CircularKernel::Circular_arc_2            Arc2;   // private
typedef internal_Argt_traits::Not_X_Monotone               Not_X_Monotone;   // empty tag struct
typedef std::variant< Arc1, Arc2, Not_X_Monotone >         Curve_2;
typedef std::variant< Arc1, Arc2 >                         X_monotone_curve_2;
typedef typename CircularKernel::Circular_arc_point_2      Circular_arc_point_2;
// functors: Compare_x_2/Compare_xy_2 forwarded from the kernel; the rest are
// CGAL::VariantFunctors::{Construct_min_vertex_2, Construct_max_vertex_2, Is_vertical_2,
//                         Compare_y_at_x_2, Compare_y_to_right_2 (as Compare_y_at_x_right_2),
//                         Equal_2, Make_x_monotone_2, Split_2, Intersect_2}<CircularKernel, Arc1, Arc2>
```

Useful `VariantFunctors` signatures:

```cpp
template <class CK, class Arc1, class Arc2, class OutputIterator>
OutputIterator object_to_object_variant(const std::vector<CGAL::Object>& res1, OutputIterator res2);
    // still bridges from CGAL::Object internally (the header carries a \todo about it)

Compare_y_to_right_2::operator()(const std::variant<Arc1,Arc2>&, const std::variant<Arc1,Arc2>&,
                                 const Circular_arc_point_2&) const -> Comparison_result;
Equal_2::operator()(const Curve_2&, const Curve_2&) const -> bool;   // Curve_2 = variant<Arc1,Arc2>
Make_x_monotone_2::operator()(const std::variant<Arc1,Arc2,Not_X_Monotone>& A,
                              OutputIterator res) const;
Split_2::operator()(const std::variant<Arc1,Arc2>& A, const Circular_arc_point_2& p,
                    std::variant<Arc1,Arc2>& ca1, std::variant<Arc1,Arc2>& ca2) const -> void;
Intersect_2::operator()(const std::variant<Arc1,Arc2>& c1, const std::variant<Arc1,Arc2>& c2,
                        OutputIterator oi) const;
Construct_min_vertex_2::operator()(const std::variant<Arc1,Arc2>& cv) const -> Point_2;   // BY VALUE
Construct_max_vertex_2::operator()(const std::variant<Arc1,Arc2>& cv) const -> Point_2;   // BY VALUE
Do_overlap_2::operator()(const std::variant<Arc1,Arc2>&, const std::variant<Arc1,Arc2>&) const -> bool;
```

### Verdict **[all verified by compiling]**

| | `Arr_circle_segment_traits_2` | `Arr_circular_arc_traits_2` | `Arr_line_arc_traits_2` | `Arr_circular_line_arc_traits_2` |
|---|---|---|---|---|
| segments + arcs in one traits | **yes** | arcs only | segments only | yes (via `std::variant`) |
| `Arrangement_2` | yes | yes | yes | yes |
| `Arrangement_with_history_2` | **yes** | **compile error** | n/a | **compile error** |
| `Has_merge_category` | `Tag_true` | `Tag_false` | `Tag_false` | `Tag_false` |
| `Approximate_2` (drawing, tessellation) | yes | no | no | no |
| `Trim_2`, `Construct_opposite_2`, `Compare_endpoints_xy_2` | yes | no | no | no |
| Boolean set operations (`Gps_*`) | yes | no | no | no |
| `Arr_polycurve_traits_2` wrapping | yes | no (`Compare_endpoints_xy_2` missing) | no | no |
| `Point_2` copy cost | 8 B handle | kernel `Circular_arc_point_2` | idem | idem |
| kernel requirement | any exact-field kernel (`Cartesian<Gmpq>`, `Epeck`) | `Circular_kernel_2` | idem | idem |

**Bind only `Arr_circle_segment_traits_2` (+ `Gps_circle_segment_traits_2`).** The circular-kernel
traits add no capability the circle/segment traits lacks, cost you a second kernel dependency, a
`std::variant` curve type that is hostile to type erasure, and lose history, merging, trimming,
approximation and Boolean operations.

---

## 9. Quick binding checklist

* Type-erase `Point_2` as an 8-byte handle; expose `x()/y()` as
  `(a0: rational, a1: rational, root: rational, is_extended: bool)` **and** a `to_double()` /
  `to_interval()` pair. Do not expose `_One_root_number`.
* Expose `Curve_2` construction through the 8 real constructors; prefer `(center, radius, orient)`
  and `(center, radius, orient, source, target)` for rational radii.
* Expose `Make_x_monotone_2` as returning a tagged union `Point | XCurve`
  (`std::variant<Point_2, X_monotone_curve_2>`).
* Expose `Intersect_2` as returning a tagged union `(Point, multiplicity) | XCurve`
  (`std::variant<std::pair<Point_2,unsigned>, X_monotone_curve_2>`).
* Keep one long-lived `Arr_circle_segment_traits_2` per arrangement, own it on the C++ side, and
  never let a functor object outlive it. Default `use_intersection_caching = false` unless you
  measure a win; if you turn it on, the cache is unbounded.
* For tessellation to Python, use `approximate_2_object()(xcv, error, oi, l2r)` (adaptive,
  `Cartesian<double>::Point_2`) rather than the legacy `xcv.approximate(oi, n)`.
* For point location, wire `Arr_naive_/Arr_walk_along_line_/Arr_trapezoid_ric_point_location`;
  do **not** offer the landmarks strategy.
