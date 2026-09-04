# CGAL 6.1 — Arrangement_on_surface_2: traits adapters, decorators, IO and misc utilities

Source of truth: the **installed** headers at `/opt/homebrew/include/CGAL` (CGAL 6.1,
`CGAL_VERSION_NR 1060101000`, release date 20250929, git `b26b07a1242`). Everything below is
quoted verbatim from those headers or verified by compiling a probe with:

```
/usr/bin/clang++ -std=c++17 -O0 -DCGAL_USE_CORE -DCGAL_USE_GMP -DCGAL_USE_MPFR \
  -I/opt/homebrew/include -L/opt/homebrew/lib -lgmp -lmpfr -o test test.cpp
```

Scope of this document: `Arr_curve_data_traits_2`, `Arr_consolidated_curve_data_traits_2`,
`Arr_counting_traits_2`, `Arr_tracing_traits_2`, `Arr_traits_basic_adaptor_2` /
`Arr_traits_adaptor_2` (+ its dispatching header), `Arr_directional_non_caching_segment_basic_traits_2`,
`Arr_rational_function_traits_2` (+ `Arr_rat_arc/*`), `Arr_algebraic_segment_traits_2`,
`draw_arrangement_2.h`, the text-formatter / reader / writer IO stack, and a verified
**feature matrix** across all shipped 2D arrangement traits.

---

## 0. Gotchas / surprises vs. older CGAL

These are the things that will actually break a port written from CGAL 4.x/5.x memory.

1. **The traits concepts were renamed `Arrangement*Traits_2` → `Aos*Traits_2`.**
   Grepping the installed headers finds `AosBasicTraits_2` (7), `AosTraits_2` (7),
   `AosDirectionalXMonotoneTraits_2` (2), `AosOpenBoundaryTraits_2` (1),
   `AosConstructXMonotoneCurveTraits_2` (1) and exactly **one** stale
   `ArrangementOpenBoundaryTraits_2` mention. The old names still work in prose only —
   there are no such C++ entities either way (concepts are documentation-only), but the
   doc comments you will read in the headers use `Aos*`.

2. **`std::variant` / `std::optional` replaced `CGAL::Object` and `boost::variant` everywhere.**
   - `Make_x_monotone_2::operator()` emits `std::variant<Point_2, X_monotone_curve_2>`.
   - `Intersect_2::operator()` emits
     `std::variant<std::pair<Point_2, Multiplicity>, X_monotone_curve_2>`.
   Both `Arr_curve_data_traits_2` and `Arr_tracing_traits_2` implement this with
   `std::get_if<...>(&item)`. There is no `CGAL::assign` path left.

3. **`Arr_counting_traits_2` silently under-counts `Equal_2`.**
   `CGAL/Arr_has.h:83` reads
   `struct has_equal_2<T, std::void_t<typename T::Equal>> : std::true_type {};`
   — it probes `T::Equal`, not `T::Equal_2`. Consequently `m_exist[EQUAL_2_POINTS_OP]`
   and `m_exist[EQUAL_2_CURVES_OP]` are always `false` and `print()`/`operator<<` skip
   those two rows. **Verified by running:** the `operator<<` dump of
   `Arr_counting_traits_2<Arr_segment_traits_2<Epeck>>` has no `EQUAL_2_*_OP` lines at all.
   `count(EQUAL_2_POINTS_OP)` still returns the real number — only printing is broken.

4. **Streaming an arrangement is all-or-nothing, and it is *not* available for
   circle-segment / conic / Bézier traits — not even for writing.**
   `Arr_text_formatter<Arr>` declares `virtual void read_point(Point_2&)` and
   `virtual void read_x_monotone_curve(X_monotone_curve_2&)`; because they are virtual they
   are instantiated when the class is instantiated, so `os << arr` drags in `is >> Point_2`
   and `is >> X_monotone_curve_2`. **Verified compile failures:**
   `Arr_circle_segment_traits_2` fails at `Arr_text_formatter.h:348 in() >> p;` and
   `Arr_conic_traits_2` fails at `Arr_text_formatter.h:373 in() >> cv;`. Deriving your own
   formatter does not help (the base still instantiates). Segment / non-caching-segment /
   linear / geodesic round-trip fine (verified). Polyline **compiles** but silently corrupts —
   see 4b.

4b. **Polyline arrangement IO silently drops the last subcurve of every polyline
    (off-by-one in `Polycurve_2_iostream.h`).** `write_polyline` does
    `os << xcv.number_of_subcurves();` (comment in the header even says
    *"// export the number of points."*) and then writes **n+1 points**;
    `read_polyline` reads that leading integer as a **point** count and therefore builds
    only **n−1** subcurves, leaving one point in the stream that
    `Arr_text_formatter::read_x_monotone_curve`'s `_skip_until_EOL()` discards.
    **Verified end-to-end**: a 4-point / 3-subcurve polyline serialises to
    `1 0 1 1 3 0 0 1 2 3 1 5 4` and reads back as a **2-subcurve** polyline, while the
    VERTICES section still records the original endpoint `5 4` — i.e. the reconstructed
    arrangement is geometrically inconsistent yet has the same V/E/F counts, so a naive
    round-trip test passes. Do **not** use `operator<<`/`operator>>` for polyline
    arrangements. (Non-segment polycurves take the `write_polycurve`/`read_polycurve`
    branch, which is correct.)

5. **The traits adaptor synthesizes *predicates only*. It does NOT synthesize
   `Approximate_2`, `Construct_bbox_2`, `Trim_2` or `Construct_x_monotone_curve_2.`**
   `grep -n "Approximate\|Construct_bbox\|bbox\|Trim_2\|Construct_x_monotone_curve"
   Arr_traits_adaptor_2.h` returns **zero hits**. Those reach you only through the public
   inheritance `class Arr_traits_basic_adaptor_2 : public ArrangementBasicTraits_`.
   What the adaptor *does* add: `Is_in_x_range_2`, `Compare_y_position_2`, `Is_between_cw_2`,
   `Compare_cw_around_point_2`, `Is_closed_2`, `Construct_vertex_at_curve_end_2`,
   `Compare_y_curve_ends_2`, `Compare_x_point_curve_end_2`, `Compare_x_curve_ends_2`,
   `Do_intersect_2`, and tag-dispatched defaults for `Parameter_space_in_x/y_2`,
   `Is_on_x/y_identification_2`, `Compare_x/y_on_boundary_2`, `Compare_x/y_near_boundary_2`,
   `Compare_y_at_x_left_2` (when `Has_left_category == Tag_false`).

6. **Every adaptor/decorator functor holds a raw non-owning `const Self*` / `const Base*` /
   `const Self&` back-pointer to the traits.** Functor objects returned by `*_2_object()`
   dangle the moment the traits (or the adaptor, or the `Arrangement_2` that owns it) dies.
   In a type-erased core, never cache functors across a traits swap.

7. **`Arr_counting_traits_2` and `Arr_tracing_traits_2` are non-copyable** —
   `Arr_counting_traits_2(const Arr_counting_traits_2&) = delete;` (same for tracing).
   Their forwarding constructor is `template <typename ... Args> Arr_counting_traits_2(Args ... args)`
   (note: takes `Args` **by value**, not by forwarding reference, even though the body calls
   `std::forward`). Use `Arrangement_2(const Traits*)` and keep the traits alive yourself.

8. **`Arr_tracing_traits_2.h` injects a namespace-scope
   `template <typename OutputStream> OutputStream& operator<<(OutputStream&, Comparison_result)`
   into `namespace CGAL`.** Merely including the header changes how `Comparison_result`
   prints program-wide (verified: my probe printed `SMALLER` instead of `-1`) and can create
   ambiguities. Do not include it from a shared header.

9. **`Approximate_2`'s coordinate overload takes `int i` in every concrete traits, but
   `std::size_t i` in both decorators.** Concrete: `Approximate_number_type operator()(const Point_2& p, int i) const`.
   Decorators (`Arr_counting_traits_2`, `Arr_tracing_traits_2`):
   `Approximate_number_type operator()(const Point_2& p, std::size_t i) const`.
   Also both decorators hard-require `Base::Approximate_point_2`, which
   `Arr_linear_traits_2` and `Arr_rational_function_traits_2` **do not define** — so
   `approximate_2_object()` on those combinations fails to compile (lazily, only if called).

10. **`Arr_rational_function_traits_2::Approximate_2::operator()` is *not* const**
    (`Approximate_number_type operator()(const Point_2& p, int i){...}`, header line 1275).
    Wrapping it in `Arr_counting_traits_2` / `Arr_tracing_traits_2` cannot compile, because
    those call `m_object(p, i)` from a `const` member on a by-value member.

11. **`Arr_curve_data_traits_2` overrides only 7 functors.** Anything else you call
    (`Trim_2`, `Construct_curve_2`, `Construct_bbox_2`, …) is the *base* functor, taking and
    returning **base** curve types — silently dropping the data field. Notably
    `trim_2_object()(xcv, p, q)` returns `Base_x_monotone_curve_2`, not the extended type.

12. **`Arr_consolidated_curve_data_traits_2` re-derives the four side categories from the
    *original* traits, not from the completed ones.** It writes
    `typedef Traits_ Base_traits_2;` then
    `typedef typename Base_traits_2::Left_side_category Left_side_category;`
    while its own base `Arr_curve_data_traits_2` uses
    `internal::Arr_complete_left_side_category<Base_traits_2>::Category`. So a traits class
    that omits the side categories works under `Arr_curve_data_traits_2` but **fails to
    compile** under `Arr_consolidated_curve_data_traits_2`.

13. **`Arr_traits_adaptor_2::Compare_xy_2` hides the base one and gained an
    x-monotone-curve overload** (new in 6.x): it can compare two *curves*
    lexicographically, computing intersections internally. `Compare_xy_2` is deliberately
    *not* re-typedef'd from `Base` (`// typedef typename Base::Compare_xy_2 Compare_xy_2;`
    is commented out in the header).

14. **`Arr_conic_traits_2` is the only traits with a real `Construct_bbox_2` functor.**
    Everyone else exposes `Bbox_2 bbox() const` on the curve object instead
    (`Arr_segment_2`, `Arr_linear_object_2` — with `CGAL_precondition(is_segment())` —,
    `_X_monotone_circle_segment_2`, `internal::Polycurve_2`, `_Bezier_curve_2`).
    The geodesic traits' `bbox()` is `#if 0`'d out.

15. **`draw_arrangement_2.h` compiles and links without Qt** (verified). Without
    `CGAL_USE_BASIC_VIEWER`, `CGAL::draw(arr)` prints
    `"Impossible to draw, CGAL_USE_BASIC_VIEWER is not defined."` and returns.
    **`CGAL::add_to_graphics_scene(arr, scene)` still works and still tessellates** — this is
    a Qt-free way to get flattened `double` geometry out of an arrangement.

15b. **`Arr_linear_traits_2::construct_curve_2_object()` does not compile.** The header has

```cpp
class Construct_x_monotone_curve_2 { public: /* only implicit ctors */ };
Construct_x_monotone_curve_2 construct_x_monotone_curve_2_object() const
{ return Construct_x_monotone_curve_2(); }              // fine
typedef Construct_x_monotone_curve_2  Construct_curve_2;
Construct_curve_2 construct_curve_2_object() const
{ return Construct_x_monotone_curve_2(*this); }         // line 1572 — ill-formed
```

**Verified error:** `Arr_linear_traits_2.h:1572:12: error: no matching conversion for
functional-style cast from 'const CGAL::Arr_linear_traits_2<CGAL::Epeck>' to
'Construct_x_monotone_curve_2'`. It only fires when the member is instantiated, i.e. when you
call it. Workaround: call `construct_x_monotone_curve_2_object()` — `Construct_curve_2` is a
typedef for the very same type. (`Arr_segment_traits_2` has the identical shape but its
functor *does* take `const Traits&`, so there it is fine.)

16. **`Arr_conic_traits_2::Multiplicity` is `size_t`; the geodesic traits' is `std::size_t`;
    everyone else uses `unsigned int`.** A type-erased `Multiplicity` must be `std::size_t`
    (widening) or you lose values on conic/geodesic.

---

## 1. `CGAL/Arr_curve_data_traits_2.h`

### 1.1 Class template

```cpp
template <typename Traits_, typename XMonotoneCurveData_,
          typename Merge_ = _Default_merge_func<XMonotoneCurveData_>,
          typename CurveData_ = XMonotoneCurveData_,
          typename Convert_ =
            _Default_convert_func<CurveData_, XMonotoneCurveData_> >
class Arr_curve_data_traits_2 : public Traits_ {
```

Public inheritance from `Traits_` — every functor not listed in §1.3 is the base one.

### 1.2 Public typedefs (verbatim)

```cpp
typedef Traits_                                    Base_traits_2;
typedef XMonotoneCurveData_                        X_monotone_curve_data;
typedef Merge_                                     Merge;
typedef CurveData_                                 Curve_data;
typedef Convert_                                   Convert;

typedef typename Base_traits_2::Curve_2            Base_curve_2;
typedef typename Base_traits_2::X_monotone_curve_2 Base_x_monotone_curve_2;
typedef typename Base_traits_2::Point_2            Point_2;

typedef typename Base_traits_2::Has_left_category  Has_left_category;
typedef typename Base_traits_2::Has_merge_category Base_has_merge_category;
typedef Tag_true                                   Has_merge_category;
typedef typename Base_traits_2::Has_do_intersect_category
                                                   Has_do_intersect_category;

typedef typename internal::Arr_complete_left_side_category<Base_traits_2>::
Category                                           Left_side_category;
typedef typename internal::Arr_complete_bottom_side_category<Base_traits_2>::
Category                                           Bottom_side_category;
typedef typename internal::Arr_complete_top_side_category<Base_traits_2>::
Category                                           Top_side_category;
typedef typename internal::Arr_complete_right_side_category<Base_traits_2>::
Category                                           Right_side_category;

// Representation of a curve with an additional data field:
typedef _Curve_data_ex<Base_curve_2, Curve_data>   Curve_2;

// Representation of an x-monotone curve with an additional data field:
typedef _Curve_data_ex<Base_x_monotone_curve_2, X_monotone_curve_data>
                                                   X_monotone_curve_2;

typedef typename Base_traits_2::Multiplicity       Multiplicity;
```

Note `Has_merge_category` is **forced to `Tag_true`** regardless of the base
(`Base_has_merge_category` preserves the original). `Merge_2` then hard-errors at run time
via `CGAL_error_msg("Merging curves is not supported.")` if the base lacks `Are_mergeable_2`.

### 1.3 Constructors

```cpp
Arr_curve_data_traits_2() {}
Arr_curve_data_traits_2(const Base_traits_2& traits) : Base_traits_2(traits) {}
```

The traits **copies** the base traits by value (unlike the polycurve traits, which stores a
pointer). No aliasing concerns.

### 1.4 Overridden functors

All are constructed as `Functor(const Base_traits_2& base)` and store `const Base_traits_2& m_base`
(a reference into `*this` — safe, but the functor still must not outlive the traits).

| Functor | `*_2_object()` | Signature |
|---|---|---|
| `Make_x_monotone_2` | `make_x_monotone_2_object() const` | `template <typename OutputIterator> OutputIterator operator()(const Curve_2& cv, OutputIterator oi) const` |
| `Split_2` | `split_2_object() const` | `void operator()(const X_monotone_curve_2& cv, const Point_2& p, X_monotone_curve_2& c1, X_monotone_curve_2& c2) const` |
| `Intersect_2` | `intersect_2_object() const` | `template <typename OutputIterator> OutputIterator operator()(const X_monotone_curve_2& cv1, const X_monotone_curve_2& cv2, OutputIterator oi) const` |
| `Are_mergeable_2` | `are_mergeable_2_object() const` | `bool operator()(const X_monotone_curve_2& cv1, const X_monotone_curve_2& cv2) const` |
| `Merge_2` | `merge_2_object() const` | `void operator()(const X_monotone_curve_2& cv1, const X_monotone_curve_2& cv2, X_monotone_curve_2& c) const` |
| `Construct_x_monotone_curve_2` | `construct_x_monotone_curve_2_object() const` | `X_monotone_curve_2 operator()(const Point_2& p, const Point_2& q) const` |
| `Construct_opposite_2` | `construct_opposite_2_object() const` | `X_monotone_curve_2 operator()(const X_monotone_curve_2& cv) const` |

Semantics / preconditions from the doc comments:

- `Make_x_monotone_2`: *"`oi` the output iterator for the result. Its value type is a variant
  that wraps `Point_2` or an `X_monotone_curve_2` objects."* Internally
  `std::variant<Point_2, Base_x_monotone_curve_2>` → `std::variant<Point_2, X_monotone_curve_2>`;
  the data field is produced by `Convert()(cv.data())`; isolated points pass through unchanged.
- `Split_2`: *"\pre p lies on cv but is not one of its end-points."* Both halves get
  `cv.data()` verbatim via `set_data`.
- `Intersect_2`: overlaps get `Merge()(cv1.data(), cv2.data())`; intersection points are
  copied as-is (`std::pair<Point_2, Multiplicity>`).
- `Are_mergeable_2`: SFINAE-detects `traits.are_mergeable_2_object()`; if the base has none,
  `CGAL_error_msg("Are mergeable is not supported.")`. Additionally requires
  `cv1.data() == cv2.data()`; if `Data` has no `operator==`, it hits
  `CGAL_error_msg("Equality operator is not supported.")`.
- `Merge_2`: `CGAL_precondition(cv1.data() == cv2.data());` then attaches `cv1.data()`.
  Detection uses `BOOST_MPL_HAS_XXX_TRAIT_NAMED_DEF(has_merge_2, Are_mergeable_2, false)`
  — i.e. it probes for `Are_mergeable_2`, not `Merge_2`.
- `Construct_x_monotone_curve_2`: *"\pre p and q must not be the same."* The new curve gets a
  **default-constructed** `X_monotone_curve_data()`, not a converted one.
- `Construct_opposite_2`: SFINAE on `Construct_opposite_2` in the base; otherwise
  `CGAL_error_msg("Construct opposite curve is not supported!")` and returns a
  default-constructed curve.

### 1.5 `_Curve_data_ex` (`CGAL/Arr_geometry_traits/Curve_data_aux.h`)

```cpp
template <class BaseCurveType, class Data>
class _Curve_data_ex : public BaseCurveType
{
private:
  Data    m_data;
public:
  _Curve_data_ex ();
  _Curve_data_ex (const BaseCurveType& cv);
  _Curve_data_ex (const BaseCurveType& cv, const Data& data);
  const Data& data () const;
  Data& data ();
  void set_data (const Data& data);
};
```

Public inheritance ⇒ an `X_monotone_curve_2` binds to `const Base_x_monotone_curve_2&`
by slicing-free reference; but **passing it by value to a base-typed parameter slices the
data away**. Relevant when calling inherited functors (§0 gotcha 11).

Helper functors in the same header:

```cpp
template <class TYPE> struct _Default_merge_func
{ const TYPE& operator() (const TYPE& obj1, const TYPE& /* obj2 */) { return (obj1); } };

template <class TYPE_FROM, class TYPE_TO> struct _Default_convert_func
{ TYPE_TO operator() (const TYPE_FROM& obj) { return (obj); } };
```

Both `operator()` are **non-const**; a custom `Merge_`/`Convert_` must be callable on a
temporary (they are invoked as `Merge()(a,b)` / `Convert()(x)`).

---

## 2. `CGAL/Arr_consolidated_curve_data_traits_2.h`

```cpp
template <class Traits_, class Data_>
class Arr_consolidated_curve_data_traits_2 :
  public Arr_curve_data_traits_2<Traits_,
                                 _Unique_list<Data_>,
                                 _Consolidate_unique_lists<Data_>,
                                 Data_>
```

i.e. `X_monotone_curve_data = _Unique_list<Data_>`, `Curve_data = Data_`, the converter is
the default (`Data_` → `_Unique_list<Data_>` via the implicit singleton constructor), and
overlaps union the lists.

Public typedefs (verbatim):

```cpp
typedef Traits_                                     Base_traits_2;
typedef Data_                                       Data;
typedef _Unique_list<Data_>                         Data_container;
typedef typename Data_container::const_iterator     Data_iterator;
typedef typename Data_container::const_iterator     Data_const_iterator;

typedef typename Base::Curve_2                      Curve_2;
typedef typename Base_traits_2::Curve_2             Base_curve_2;
typedef typename Base::X_monotone_curve_2           X_monotone_curve_2;
typedef typename Base_traits_2::X_monotone_curve_2  Base_x_monotone_curve_2;
typedef typename Base_traits_2::Point_2             Point_2;
typedef typename Base_traits_2::Multiplicity        Multiplicity;

typedef typename Base_traits_2::Has_left_category   Has_left_category;
typedef typename Base_traits_2::Has_merge_category  Base_has_merge_category;
typedef Tag_true                                    Has_merge_category;
typedef typename Base_traits_2::Has_do_intersect_category
                                                    Has_do_intersect_category;
// !!! taken from the ORIGINAL traits, NOT completed — see gotcha 12
typedef typename Base_traits_2::Left_side_category   Left_side_category;
typedef typename Base_traits_2::Bottom_side_category Bottom_side_category;
typedef typename Base_traits_2::Top_side_category    Top_side_category;
typedef typename Base_traits_2::Right_side_category  Right_side_category;
```

No constructors are declared → only the implicit ones; the inherited
`Arr_curve_data_traits_2(const Base_traits_2&)` is **not** inherited (no `using`), so you get
default construction only.

### `_Unique_list<Data_>` (`CGAL/Arr_geometry_traits/Consolidated_curve_data_aux.h`)

```cpp
template <class Data_>
class _Unique_list
{
public:
  typedef Data_                Data;
  typedef _Unique_list<Data>   Self;
  typedef typename std::list<Data>::const_iterator  const_iterator;

  _Unique_list ();
  _Unique_list (const Data& data);          // singleton, non-explicit

  const_iterator begin () const;
  const_iterator end () const;
  std::size_t size () const;
  const Data& front () const;
  const Data& back () const;
  bool operator== (const Self& other) const;     // set equality, O(n^2)
  const_iterator find (const Data& data) const;  // linear
  bool insert (const Data& data);   // false if already present
  bool erase (const Data& data);    // false if not found
  void clear ();
};

template <class Data>
struct _Consolidate_unique_lists
{ _Unique_list<Data> operator() (const _Unique_list<Data>& list1,
                                 const _Unique_list<Data>& list2) const; };
```

Requires `Data::operator==`. All lookups are **O(n) linear scans over a `std::list`** — do
not use with thousands of overlapping input curves. Note `_Consolidate_unique_lists::operator()`
*is* const while `_Default_merge_func::operator()` is not.

---

## 3. `CGAL/Arr_counting_traits_2.h`

```cpp
template <typename BaseTraits>
class Arr_counting_traits_2 : public BaseTraits {
```

### 3.1 `Operation_id` (verbatim, order defines the array indices)

```cpp
enum Operation_id {
  COMPARE_X_2_OP = 0, COMPARE_XY_2_OP,
  CONSTRUCT_MIN_VERTEX_2_OP, CONSTRUCT_MAX_VERTEX_2_OP,
  IS_VERTICAL_2_OP, COMPARE_Y_AT_X_2_OP,
  EQUAL_2_POINTS_OP, EQUAL_2_CURVES_OP,
  COMPARE_Y_AT_X_LEFT_2_OP, COMPARE_Y_AT_X_RIGHT_2_OP,
  MAKE_X_MONOTONE_2_OP, SPLIT_2_OP, INTERSECT_2_OP,
  ARE_MERGEABLE_2_OP, MERGE_2_OP,
  CONSTRUCT_2_OPPOSITE_2_OP, COMPARE_ENDPOINTS_XY_2_OP,
  APPROXIMATE_2_COORD_OP, APPROXIMATE_2_POINT_OP, APPROXIMATE_2_CURVE_OP,

  PARAMETER_SPACE_IN_X_2_CURVE_END_OP, PARAMETER_SPACE_IN_X_2_POINT_OP,
  IS_ON_X_IDENTIFICATION_POINT_2_OP, IS_ON_X_IDENTIFICATION_CURVE_2_OP,
  COMPARE_Y_ON_BOUNDARY_2_OP, COMPARE_Y_NEAR_BOUNDARY_2_OP,

  PARAMETER_SPACE_IN_Y_2_CURVE_END_OP, PARAMETER_SPACE_IN_Y_2_POINT_OP,
  IS_ON_Y_IDENTIFICATION_2_POINT_OP, IS_ON_Y_IDENTIFICATION_2_CURVE_OP,
  COMPARE_X_ON_BOUNDARY_2_POINTS_OP, COMPARE_X_ON_BOUNDARY_2_POINT_CURVE_END_OP,
  COMPARE_X_ON_BOUNDARY_2_CURVE_ENDS_OP, COMPARE_X_NEAR_BOUNDARY_2_OP,

  NUMBER_OF_OPERATIONS
};
```

`NUMBER_OF_OPERATIONS == 34`.

### 3.2 Class-level API (verbatim)

```cpp
using Base = BaseTraits;

template <typename ... Args>
Arr_counting_traits_2(Args ... args);        // forwards to Base, clears counters, ++construction counter

Arr_counting_traits_2(const Arr_counting_traits_2&) = delete;

std::size_t count(Operation_id id) const;    // { return m_counters[id]; }

template <typename OutStream>
OutStream& print(OutStream& os, Operation_id id) const;   // no-op if !m_exist[id]

static std::size_t increment(bool doit = true);   // process-wide count of constructed traits
void clear_counters();                            // m_counters = {}
```

Free function:

```cpp
template <typename OutStream, class BaseTraits>
inline OutStream& operator<<(OutStream& os,
                             const Arr_counting_traits_2<BaseTraits>& traits);
```

It prints every existing operation, then `"total # = <sum>"` and
`"# of traits constructed = <increment(false)>"`.

Private state (matters for binding memory/ABI):
`mutable std::array<std::size_t, 34> m_counters;`
`const std::array<std::string, 34> m_names;` (per-instance, ~34 strings!)
`const std::array<bool, 34> m_exist;` initialised from the `has_*<Base>` detectors of
`CGAL/Arr_has.h`. `increment()` uses a function-local `std::atomic<std::size_t>` unless
`CGAL_NO_ATOMIC`.

### 3.3 Inherited typedefs

```cpp
using Has_left_category         = typename Base::Has_left_category;
using Has_merge_category        = typename Base::Has_merge_category;
using Has_do_intersect_category = typename Base::Has_do_intersect_category;
using Left_side_category   = typename internal::Arr_complete_left_side_category<Base>::Category;
using Bottom_side_category = typename internal::Arr_complete_bottom_side_category<Base>::Category;
using Top_side_category    = typename internal::Arr_complete_top_side_category<Base>::Category;
using Right_side_category  = typename internal::Arr_complete_right_side_category<Base>::Category;
using Point_2            = typename Base::Point_2;
using X_monotone_curve_2 = typename Base::X_monotone_curve_2;
using Curve_2            = typename Base::Curve_2;   // ⇒ cannot wrap a *basic* traits
```

(There is **no** `Multiplicity` typedef in the counting traits — the tracing traits has one.
`Multiplicity` still resolves through public inheritance.)

### 3.4 Functors — exact public signatures

Each wrapper stores `typename Base::Xxx m_object;` by value plus one or more
`std::size_t& m_counter`, and is constructed as `Xxx(const Base& base, std::size_t& counter…)`.

```cpp
Comparison_result  Compare_x_2::operator()(const Point_2& p1, const Point_2& p2) const;
Comparison_result  Compare_xy_2::operator()(const Point_2& p1, const Point_2& p2) const;

// return type is decltype(base functor applied to X_monotone_curve_2) — may be a reference
auto Construct_min_vertex_2::operator()(const X_monotone_curve_2& xcv) const;
auto Construct_max_vertex_2::operator()(const X_monotone_curve_2& xcv) const;

bool               Is_vertical_2::operator()(const X_monotone_curve_2& xc) const;
Comparison_result  Compare_y_at_x_2::operator()(const Point_2& p,
                                                const X_monotone_curve_2& xc) const;
bool               Equal_2::operator()(const Point_2& p1, const Point_2& p2) const;
bool               Equal_2::operator()(const X_monotone_curve_2& xc1,
                                       const X_monotone_curve_2& xc2) const;
Comparison_result  Compare_y_at_x_left_2::operator()(const X_monotone_curve_2& xc1,
                                                     const X_monotone_curve_2& xc2,
                                                     const Point_2& p) const;
Comparison_result  Compare_y_at_x_right_2::operator()(const X_monotone_curve_2& xc1,
                                                      const X_monotone_curve_2& xc2,
                                                      const Point_2& p) const;

template <typename OutputIterator>
OutputIterator Make_x_monotone_2::operator()(const Curve_2& cv, OutputIterator oi) const;

void Split_2::operator()(const X_monotone_curve_2& xc, const Point_2& p,
                         X_monotone_curve_2& xc1, X_monotone_curve_2& xc2) const;

template <typename OutputIterator>
OutputIterator Intersect_2::operator()(const X_monotone_curve_2& xc1,
                                       const X_monotone_curve_2& xc2,
                                       OutputIterator oi) const;

bool Are_mergeable_2::operator()(const X_monotone_curve_2& xc1,
                                 const X_monotone_curve_2& xc2) const;
void Merge_2::operator()(const X_monotone_curve_2& xc1,
                         const X_monotone_curve_2& xc2,
                         X_monotone_curve_2& xc) const;

X_monotone_curve_2 Construct_opposite_2::operator()(const X_monotone_curve_2& xc) const;
Comparison_result  Compare_endpoints_xy_2::operator()(const X_monotone_curve_2& xc) const;

// Approximate_2 — note std::size_t (concrete traits use int)
Approximate_number_type Approximate_2::operator()(const Point_2& p, std::size_t i) const;   // \pre i is 0 or 1
Approximate_point_2     Approximate_2::operator()(const Point_2& p) const;
template <typename OutputIterator>
OutputIterator Approximate_2::operator()(const X_monotone_curve_2& xcv, double error,
                                         OutputIterator oi, bool l2r = true) const;

// boundary
Arr_parameter_space Parameter_space_in_x_2::operator()(const X_monotone_curve_2& xc,
                                                       Arr_curve_end ce) const;
Arr_parameter_space Parameter_space_in_x_2::operator()(const Point_2& p) const;
bool Is_on_x_identification_2::operator()(const Point_2& p) const;
bool Is_on_x_identification_2::operator()(const X_monotone_curve_2& xc) const;
Comparison_result Compare_y_on_boundary_2::operator()(const Point_2& p1, const Point_2& p2) const;
Comparison_result Compare_y_near_boundary_2::operator()(const X_monotone_curve_2& xc1,
                                                        const X_monotone_curve_2& xc2,
                                                        Arr_curve_end ce) const;
Arr_parameter_space Parameter_space_in_y_2::operator()(const X_monotone_curve_2& xc,
                                                       Arr_curve_end ce) const;
Arr_parameter_space Parameter_space_in_y_2::operator()(const Point_2& p) const;
bool Is_on_y_identification_2::operator()(const Point_2& p) const;
bool Is_on_y_identification_2::operator()(const X_monotone_curve_2& xc) const;
Comparison_result Compare_x_on_boundary_2::operator()(const Point_2& p1, const Point_2& p2) const;
Comparison_result Compare_x_on_boundary_2::operator()(const Point_2& pt,
                                                      const X_monotone_curve_2& xcv,
                                                      Arr_curve_end ce) const;
Comparison_result Compare_x_on_boundary_2::operator()(const X_monotone_curve_2& xcv1,
                                                      Arr_curve_end ce1,
                                                      const X_monotone_curve_2& xcv2,
                                                      Arr_curve_end ce2) const;
Comparison_result Compare_x_near_boundary_2::operator()(const X_monotone_curve_2& xc1,
                                                        const X_monotone_curve_2& xc2,
                                                        Arr_curve_end ce) const;
```

All `*_2_object()` accessors are `const` and are listed in the header at lines 701–803.

### 3.5 `CGAL/Arr_has.h` — the detector set (useful for your own dispatch)

`namespace CGAL`, pattern
`template <typename T, typename = void> struct has_X : std::false_type {};` +
`template <typename T> struct has_X<T, std::void_t<typename T::X_2>> : std::true_type {};`

Available: `has_compare_x_2`, `has_compare_xy_2`, `has_construct_min_vertex_2`,
`has_construct_max_vertex_2`, `has_is_vertical_2`, `has_compare_y_at_x_2`,
`has_equal_2` **(broken — probes `T::Equal`)**, `has_compare_y_at_x_left_2`,
`has_compare_y_at_x_right_2`, `has_make_x_monotone_2`, `has_split_2`, `has_intersect_2`,
`has_are_mergeable_2`, `has_merge_2`, `has_construct_opposite_2`, `has_construct_point_2`,
`has_compare_endpoints_xy_2`, `has_approximate_2`, `has_parameter_space_in_x_2`,
`has_is_on_x_identification_2`, `has_compare_y_on_boundary_2`, `has_compare_y_near_boundary_2`,
`has_parameter_space_in_y_2`, `has_is_on_y_identification_2`, `has_compare_x_on_boundary_2`,
`has_compare_x_near_boundary_2`.

---

## 4. `CGAL/Arr_tracing_traits_2.h`

```cpp
template <typename BaseTraits>
class Arr_tracing_traits_2 : public BaseTraits {
```

### 4.1 `Operation_id` — **different from the counting traits!**

```cpp
enum Operation_id {
  COMPARE_X_2_OP = 0, COMPARE_XY_2_OP,
  CONSTRUCT_MIN_VERTEX_2_OP, CONSTRUCT_MAX_VERTEX_2_OP,
  IS_VERTICAL_2_OP, COMPARE_Y_AT_X_2_OP,
  EQUAL_POINTS_2_OP, EQUAL_CURVES_2_OP,          // note: *_POINTS_2_OP, not *_2_POINTS_OP
  COMPARE_Y_AT_X_LEFT_2_OP, COMPARE_Y_AT_X_RIGHT_2_OP,
  MAKE_X_MONOTONE_2_OP, SPLIT_2_OP, INTERSECT_2_OP,
  ARE_MERGEABLE_2_OP, MERGE_2_OP,
  CONSTRUCT_2_OPPOSITE_2_OP, COMPARE_ENDPOINTS_XY_2_OP,
  APPROXIMATE_2_OP,                               // ONE id, not three
  PARAMETER_SPACE_IN_X_2_OP, IS_ON_X_IDENTIFICATION_2_OP,
  COMPARE_Y_ON_BOUNDARY_2_OP, COMPARE_Y_NEAR_BOUNDARY_2_OP,
  PARAMETER_SPACE_IN_Y_2_OP, IS_ON_Y_IDENTIFICATION_2_OP,
  COMPARE_X_ON_BOUNDARY_2_OP, COMPARE_X_NEAR_BOUNDARY_2_OP,
  NUMBER_OF_OPERATIONS
};
```

`NUMBER_OF_OPERATIONS == 26`. Do **not** share an id table between the two decorators.

### 4.2 Class-level API

```cpp
using Base = BaseTraits;

template<typename ... Args>
Arr_tracing_traits_2(Args ... args);            // forwards to Base, then enable_all_traces()
Arr_tracing_traits_2(const Arr_tracing_traits_2&) = delete;

void enable_trace(Operation_id id);   // m_flags |= 0x1ull << id
void enable_all_traces();             // m_flags = 0xffffffff
void disable_trace(Operation_id id);  // m_flags &= ~(0x1ull << id)
void disable_all_traces();            // m_flags = 0x0
```

State: `unsigned long long m_flags;` — **not initialised in the default path other than by
`enable_all_traces()` in the ctor**, so tracing is ON by default and everything goes to
`std::cout` (hard-coded, no stream injection point).

Typedefs: same block as the counting traits, **plus**
`using Multiplicity = typename Base::Multiplicity;`.

### 4.3 Functors

Wrappers hold `typename Base::Xxx m_object;` (some hold `const Base& m_base_traits`) and a
`bool m_enabled`. Constructors take `(const Base& base, bool enabled = true)`. The public
`operator()` signatures are identical to §3.4, with these differences:

- `Construct_min_vertex_2` / `Construct_max_vertex_2` publish the return type explicitly:
  ```cpp
  using Subcurve_ctr_minv = typename Base::Construct_min_vertex_2;
  using Return_type = decltype(std::declval<Subcurve_ctr_minv>()
                                 .operator()(std::declval<X_monotone_curve_2>()));
  Return_type operator()(const X_monotone_curve_2& xcv) const;
  ```
- `Are_mergeable_2` has an extra SFINAE helper returning
  `decltype(m_base_traits.are_mergeable_2_object().operator()(xcv1, xcv2))`.
- `Approximate_2::operator()(const Point_2&, std::size_t i)` — same `std::size_t` mismatch.
- `Intersect_2` materialises the results into a `std::list<Intersection_result>` where
  `using Intersection_point = std::pair<Point_2, Multiplicity>;
   using Intersection_result = std::variant<Intersection_point, X_monotone_curve_2>;`
  before forwarding — so the traced version is **not** zero-copy.
- `Make_x_monotone_2` likewise uses
  `using Make_x_monotone_result = std::variant<Point_2, X_monotone_curve_2>;`.

The tracing functors require `operator<<` for `Point_2`, `X_monotone_curve_2`, `Curve_2` and
`Approximate_point_2` — so the tracing decorator **cannot** wrap circle-segment/conic curve
types for every operation (their `Point_2` prints but `X_monotone_curve_2` only has `<<`,
which is fine; `Curve_2` for circle-segment has `<<` too — but the *arrangement* IO still
does not work, see §7).

### 4.4 Namespace pollution (gotcha 8)

```cpp
template <typename OutputStream>
OutputStream& operator<<(OutputStream& os, Comparison_result cr) {
  os << ((cr == SMALLER) ? "SMALLER" : (cr == EQUAL) ? "EQUAL" : "LARGER");
  return os;
}
```
defined at namespace `CGAL` scope at the bottom of the header.

---

## 5. `CGAL/Arrangement_2/Arr_traits_adaptor_2.h`

Two class templates. Both inherit **publicly** from the wrapped traits.

### 5.1 `Arr_traits_basic_adaptor_2`

```cpp
template <typename ArrangementBasicTraits_>
class Arr_traits_basic_adaptor_2 : public ArrangementBasicTraits_ {
public:
  typedef ArrangementBasicTraits_                   Base;
  typedef Arr_traits_basic_adaptor_2<Base>          Self;
  typedef typename Base::X_monotone_curve_2         X_monotone_curve_2;
  typedef typename Base::Point_2                    Point_2;
  typedef typename Base::Multiplicity               Multiplicity;

  typedef typename Base::Has_left_category          Has_left_category;
  typedef typename Base::Has_do_intersect_category  Has_do_intersect_category;

  typedef typename internal::Arr_complete_left_side_category< Base >::Category
                                                    Left_side_category;
  typedef typename internal::Arr_complete_bottom_side_category< Base >::Category
                                                    Bottom_side_category;
  typedef typename internal::Arr_complete_top_side_category< Base >::Category
                                                    Top_side_category;
  typedef typename internal::Arr_complete_right_side_category< Base >::Category
                                                    Right_side_category;
```

`protected:` dispatch machinery (visible to derived adaptors, e.g. the topology traits):

```cpp
typedef typename Arr_all_sides_oblivious_category<Left_side_category,
                                                  Bottom_side_category,
                                                  Top_side_category,
                                                  Right_side_category>::result
  Are_all_sides_oblivious_category;

typedef CGAL::internal::Arr_left_right_implementation_dispatch<
  Left_side_category, Right_side_category > LR;
typedef typename LR::Is_on_y_identification_2_curve_tag      Ioyi_2_curve_tag;
typedef typename LR::Is_on_y_identification_2_point_tag      Ioyi_2_point_tag;
typedef typename LR::Compare_y_on_boundary_2_points_tag      Cmp_y_ob_2_points_tag;
typedef typename LR::Compare_y_near_boundary_2_curve_ends_tag Cmp_y_nb_2_curve_ends_tag;

typedef CGAL::internal::Arr_bottom_top_implementation_dispatch<
  Bottom_side_category, Top_side_category > BT;
typedef typename BT::Is_on_x_identification_2_curve_tag      Ioxi_2_curve_tag;
typedef typename BT::Is_on_x_identification_2_point_tag      Ioxi_2_point_tag;
typedef typename BT::Compare_x_near_boundary_2_curve_ends_tag Cmp_x_nb_2_curve_ends_tag;

typedef typename Arr_two_sides_category<Left_side_category,
                                        Right_side_category>::result
  Left_or_right_sides_category;      // used by parameter_space_in_x
typedef typename Arr_two_sides_category<Bottom_side_category,
                                        Top_side_category>::result
  Bottom_or_top_sides_category;      // used by parameter_space_in_y, compare_x_on_boundary
```

Constructors:

```cpp
Arr_traits_basic_adaptor_2() : Base() {}
Arr_traits_basic_adaptor_2(const Base& traits) : Base(traits) {}
```
(copies the base traits by value).

Inherited functor typedefs (re-exported verbatim):

```cpp
typedef typename Base::Compare_x_2            Compare_x_2;
typedef typename Base::Compare_xy_2           Compare_xy_2;
typedef typename Base::Construct_min_vertex_2 Construct_min_vertex_2;
typedef typename Base::Construct_max_vertex_2 Construct_max_vertex_2;
typedef typename Base::Is_vertical_2          Is_vertical_2;
typedef typename Base::Compare_y_at_x_right_2 Compare_y_at_x_right_2;
typedef typename Base::Equal_2                Equal_2;
```

#### 5.1.1 Synthesized / overridden functors

Every one of these has a **`protected` constructor** taking `const Self*` (or `const Base*`)
plus `friend class Arr_traits_basic_adaptor_2<Base>;` — you can only obtain them from the
`*_2_object()` accessor. All accessors are `const`.

| Functor | accessor | public `operator()` |
|---|---|---|
| `Compare_y_at_x_2` | `compare_y_at_x_2_object()` | `Comparison_result operator()(const Point_2& p, const X_monotone_curve_2& xcv) const` |
| `Compare_y_at_x_left_2` | `compare_y_at_x_left_2_object()` | `Comparison_result operator()(const X_monotone_curve_2& xcv1, const X_monotone_curve_2& xcv2, const Point_2& p) const` |
| `Do_intersect_2` | `do_intersect_2_object()` | `bool operator()(const X_monotone_curve_2& xcv1, const X_monotone_curve_2& xcv2) const` |
| `Parameter_space_in_x_2` | `parameter_space_in_x_2_object()` | `Arr_parameter_space operator()(const X_monotone_curve_2& xcv, Arr_curve_end ind) const`<br>`Arr_parameter_space operator()(const Point_2& p) const` |
| `Parameter_space_in_y_2` | `parameter_space_in_y_2_object()` | `Arr_parameter_space operator()(const X_monotone_curve_2& xcv, Arr_curve_end ind) const`<br>`Arr_parameter_space operator()(const Point_2& p) const` |
| `Is_on_y_identification_2` | `is_on_y_identification_2_object()` | `bool operator()(const Point_2& p) const`<br>`bool operator()(const X_monotone_curve_2& xcv) const` |
| `Is_on_x_identification_2` | `is_on_x_identification_2_object()` | `bool operator()(const Point_2& p) const`<br>`bool operator()(const X_monotone_curve_2& xcv) const` |
| `Compare_y_on_boundary_2` | `compare_y_on_boundary_2_object()` | `Comparison_result operator()(const Point_2& p1, const Point_2& p2) const` |
| `Compare_y_near_boundary_2` | `compare_y_near_boundary_2_object()` | `Comparison_result operator()(const X_monotone_curve_2& xcv1, const X_monotone_curve_2& xcv2, Arr_curve_end ce) const` |
| `Compare_x_on_boundary_2` | `compare_x_on_boundary_2_object()` | `Comparison_result operator()(const Point_2& p1, const Point_2& p2) const`<br>`Comparison_result operator()(const Point_2& pt, const X_monotone_curve_2& xcv, Arr_curve_end ce) const`<br>`Comparison_result operator()(const X_monotone_curve_2& xcv1, Arr_curve_end ce1, const X_monotone_curve_2& xcv2, Arr_curve_end ce2) const` |
| `Compare_x_near_boundary_2` | `compare_x_near_boundary_2_object()` | `Comparison_result operator()(const X_monotone_curve_2& xcv1, const X_monotone_curve_2& xcv2, Arr_curve_end ce) const` |

Preconditions from the doc comments:
- `Compare_y_at_x_2`: *"\pre p is in the x-range of cv."* Returns `SMALLER` if the point is
  below the curve, `LARGER` if above, `EQUAL` if on it.
- `Compare_y_at_x_left_2`: *"\pre The two curves intersect at p, and they are defined to its
  left."* Implemented against `Has_left_category`; if `Tag_false`, it is emulated from other
  predicates.
- `Do_intersect_2`: dispatched on `Has_do_intersect_category`; if `Tag_false`, emulated.
- `Compare_y_near_boundary_2`: *"\pre Both curve ends have a special boundary in x."*
  Returns `EQUAL` (harmlessly) when the sides are oblivious.
- `Compare_y_on_boundary_2`: *"\pre Both points lie on vertical boundaries."*
- `Compare_x_near_boundary_2`: *"\pre Both curve ends have a special boundary in y."*

#### 5.1.2 "Special non-public comparison functors" (still publicly reachable)

These live under a comment `// special non-public comparison functors` but the accessors are
public members of the adaptor:

```cpp
Compare_y_curve_ends_2       compare_y_curve_ends_2_object() const;
Compare_x_point_curve_end_2  compare_x_point_curve_end_2_object() const;
Compare_x_curve_ends_2       compare_x_curve_ends_2_object() const;
Construct_vertex_at_curve_end_2 construct_vertex_at_curve_end_2_object() const;
Is_closed_2                  is_closed_2_object() const;
```

with

```cpp
Comparison_result Compare_y_curve_ends_2::operator()(const X_monotone_curve_2& xcv1,
                                                     const X_monotone_curve_2& xcv2,
                                                     Arr_curve_end ce) const;
  // \pre Both curve ends have a special boundary in y.

Comparison_result Compare_x_point_curve_end_2::operator()(const Point_2& pt,
                                                          const X_monotone_curve_2& xcv,
                                                          Arr_curve_end ce) const;
  // \pre The curve end has a special boundary in y.

Comparison_result Compare_x_curve_ends_2::operator()(const X_monotone_curve_2& xcv1,
                                                     Arr_curve_end ce1,
                                                     const X_monotone_curve_2& xcv2,
                                                     Arr_curve_end ce2) const;
  // \pre Both curve ends have a special boundary in y.

Point_2 Construct_vertex_at_curve_end_2::operator()(const X_monotone_curve_2& xcv,
                                                    Arr_curve_end ce) const;
  // == (ce == ARR_MIN_END) ? construct_min_vertex(xcv) : construct_max_vertex(xcv)

bool Is_closed_2::operator()(const X_monotone_curve_2& xcv, Arr_curve_end ce) const;
  // "\return true is the curve end is bounded, and false otherwise"
  // ARR_INTERIOR ⇒ true; otherwise the corresponding side category decides
  // (Arr_open_side_tag ⇒ false, any other Arr_boundary_side_tag ⇒ true)
```

#### 5.1.3 "Additional auxiliary functors" — the ones you actually want for geometry work

```cpp
class Is_in_x_range_2 {
public:
  /*! checks whether a given point is in the x-range of the given x-monotone curve.
   * \return true if x(xcv_left) <= x(p) <= x(xcv_right), false otherwise. */
  bool operator()(const X_monotone_curve_2& xcv, const Point_2& p) const;

  /*! checks whether the x-ranges of the given x-monotone curves overlap. */
  bool operator()(const X_monotone_curve_2& xcv1,
                  const X_monotone_curve_2& xcv2) const;
};
Is_in_x_range_2 is_in_x_range_2_object() const;

class Compare_y_position_2 {
public:
  /*! \pre The x-ranges of the two curves overlap.
   * \return SMALLER if xcv1 lies below xcv2;
   *         LARGER if xcv1 lies above xcv2;
   *         EQUAL in case the common x-range is a single point. */
  Comparison_result operator()(const X_monotone_curve_2& xcv1,
                               const X_monotone_curve_2& xcv2) const;
};
Compare_y_position_2 compare_y_position_2_object() const;

class Is_between_cw_2 {
public:
  /*! \pre p is an end-point of all three curves.
   * \return true if xcv is between xcv1 and xcv2; false otherwise.
   *         If xcv overlaps xcv1 or xcv2 the result is always false.
   *         If xcv1 and xcv2 overlap, the result is true, unless xcv also overlaps them. */
  bool operator()(const X_monotone_curve_2& cv, bool cv_to_right,
                  const X_monotone_curve_2& cv1, bool cv1_to_right,
                  const X_monotone_curve_2& cv2, bool cv2_to_right,
                  const Point_2& p,
                  bool& cv_equal_cv1, bool& cv_equal_cv2) const;
};
Is_between_cw_2 is_between_cw_2_object() const;

class Compare_cw_around_point_2 {
public:
  /*! \pre The point p is an endpoint of both curves.
   * \param from_top true if we start from 12 o'clock, false if from 6 o'clock.
   * \return SMALLER if we encounter xcv1 before xcv2;
   *         LARGER if we encounter xcv2 before xcv1; EQUAL otherwise. */
  Comparison_result operator()(const X_monotone_curve_2& xcv1,
                               bool xcv1_to_right,
                               const X_monotone_curve_2& xcv2,
                               bool xcv2_to_right,
                               const Point_2& p,
                               bool from_top = true) const;
};
Compare_cw_around_point_2 compare_cw_around_point_2_object() const;
```

`Compare_y_position_2`'s precondition on interior-disjointness is *documented but
deliberately unimplemented* (comment in the header: "it seems that there is no gain in
checking the precondition, and it is left unimplemented"). The `is_in_x_range` precondition
*is* checked under `CGAL_precondition_code`.

`bool …_to_right` means "the curve is directed from left to right, i.e. the common vertex is
its **left** endpoint".

### 5.2 `Arr_traits_adaptor_2`

```cpp
template <typename ArrangementTraits_>
class Arr_traits_adaptor_2 : public Arr_traits_basic_adaptor_2<ArrangementTraits_>
{
public:
  typedef ArrangementTraits_                           Base_traits_2;
  typedef Arr_traits_basic_adaptor_2<Base_traits_2>    Base;
  typedef Arr_traits_adaptor_2<Base_traits_2>          Self;

  typedef typename Base_traits_2::Curve_2              Curve_2;
  typedef typename Base::X_monotone_curve_2            X_monotone_curve_2;
  typedef typename Base::Point_2                       Point_2;
  typedef typename Base::Multiplicity                  Multiplicity;

  typedef typename Base::Has_left_category             Has_left_category;
  typedef typename Base::Has_merge_category            Has_merge_category;
  typedef typename Base::Has_do_intersect_category     Has_do_intersect_category;

  typedef typename Base::Left_side_category            Left_side_category;
  typedef typename Base::Bottom_side_category          Bottom_side_category;
  typedef typename Base::Top_side_category             Top_side_category;
  typedef typename Base::Right_side_category           Right_side_category;

  typedef typename Base::Are_all_sides_oblivious_category
    Are_all_sides_oblivious_category;                  // promoted to public here

  Arr_traits_adaptor_2() : Base() {}
  Arr_traits_adaptor_2(const Base_traits_2& traits) : Base(traits) {}

  typedef typename Base::Compare_x_2            Compare_x_2;
  // typedef typename Base::Compare_xy_2           Compare_xy_2;   // <-- commented out!
  typedef typename Base::Construct_min_vertex_2 Construct_min_vertex_2;
  typedef typename Base::Construct_max_vertex_2 Construct_max_vertex_2;
  typedef typename Base::Is_vertical_2          Is_vertical_2;
  typedef typename Base::Compare_y_at_x_2       Compare_y_at_x_2;
  typedef typename Base::Compare_y_at_x_right_2 Compare_y_at_x_right_2;
  typedef typename Base::Compare_y_at_x_left_2  Compare_y_at_x_left_2;
  typedef typename Base::Equal_2                Equal_2;

  // Note that the basic adaptor does not have to support these functors:
  typedef typename Base_traits_2::Make_x_monotone_2  Make_x_monotone_2;
  typedef typename Base_traits_2::Split_2            Split_2;
  typedef typename Base_traits_2::Intersect_2        Intersect_2;
```

Overridden functors:

```cpp
class Compare_xy_2 {
  typedef std::pair<Point_2, Multiplicity>          Intersection_point;
  typedef std::variant<Intersection_point, X_monotone_curve_2> Intersection_result;
public:
  /*! \pre p1 does not lie on the boundary.  \pre p2 does not lie on the boundary. */
  Comparison_result operator()(const Point_2& p1, const Point_2& p2) const;
  /*! compares two x-monotone curves lexicographically (left-most point,
   *  then the graphs to its right, then the right-most point). */
  Comparison_result operator()(const X_monotone_curve_2& c1,
                               const X_monotone_curve_2& c2) const;
protected:
  const Self& m_self;                          // reference, not pointer
  // + protected overloads taking std::list<Intersection_result>& and a
  //   Arr_all_sides_oblivious_tag / Arr_not_all_sides_oblivious_tag tag
};
Compare_xy_2 compare_xy_2_object() const { return Compare_xy_2(*this); }

class Are_mergeable_2 {
public:
  /*! \return true if the two curves are mergeable - if they are supported
   *  by the same line and share a common endpoint; false otherwise. */
  bool operator()(const X_monotone_curve_2& xcv1,
                  const X_monotone_curve_2& xcv2) const;   // dispatched on Has_merge_category
protected:
  const Base* m_base;
};
Are_mergeable_2 are_mergeable_2_object() const;

class Merge_2 {
public:
  /*! \pre The two curves are mergeable, that is they are supported by the
   *       curve line and share a common endpoint. */
  void operator()(const X_monotone_curve_2& xcv1,
                  const X_monotone_curve_2& xcv2,
                  X_monotone_curve_2& c) const;             // dispatched on Has_merge_category
protected:
  const Base* m_base;
};
Merge_2 merge_2_object() const { return Merge_2(this); }
```

`Compare_xy_2::operator()(c1, c2)` is the two-curve comparison used by 6.x's
`Arrangement_on_surface_2`; note it may *compute intersections* internally, so it is not
cheap and it requires the full (non-basic) traits.

### 5.3 `CGAL/Arrangement_2/Arr_traits_adaptor_2_dispatching.h` (skim)

Two public tag types in `namespace CGAL`:

```cpp
struct Arr_use_dummy_tag {};    //! tag to specify to use a dummy implementation
struct Arr_use_traits_tag {};   //! tag to specify to call the corresponding traits method
```

`namespace CGAL::internal`:

```cpp
template < class ArrSmallerImplementationTag, class ArrLargerImplementationTag >
struct Or_traits {
  typedef ArrSmallerImplementationTag  Arr_smaller_implementation_tag;
  typedef ArrLargerImplementationTag   Arr_larger_implementation_tag;
  typedef /* Arr_use_traits_tag if either side asks for traits, else Arr_use_dummy_tag */ type;
};

template < class ArrLeftSideTag, class ArrRightSideTag >
struct Arr_left_right_implementation_dispatch {
  typedef ArrLeftSideTag   Left_side_category;
  typedef ArrRightSideTag  Right_side_category;
  typedef … Parameter_space_in_x_2_curve_end_tag;
  typedef … Parameter_space_in_x_2_curve_tag;
  typedef … Parameter_space_in_x_2_point_tag;
  typedef … Is_on_y_identification_2_curve_tag;
  typedef … Is_on_y_identification_2_point_tag;
  typedef … Compare_y_on_boundary_2_points_tag;
  typedef … Compare_y_near_boundary_2_curve_ends_tag;
};

template < class ArrBottomSideTag, class ArrTopSideTag >
struct Arr_bottom_top_implementation_dispatch {
  typedef … Parameter_space_in_y_2_curve_end_tag;
  typedef … Parameter_space_in_y_2_curve_tag;
  typedef … Parameter_space_in_y_2_point_tag;
  typedef … Is_on_x_identification_2_curve_tag;
  typedef … Is_on_x_identification_2_point_tag;
  typedef … Compare_x_on_boundary_2_points_tag;
  typedef … Compare_x_on_boundary_2_point_curve_end_tag;
  typedef … Compare_x_on_boundary_2_curve_ends_tag;
  typedef … Compare_x_near_boundary_2_curve_ends_tag;
};
```

Per-side truth table (`Arr_use_traits_tag` = "call the base traits", `Arr_use_dummy_tag` =
"synthesize"):

| signature | oblivious | open | closed | contracted | identified |
|---|---|---|---|---|---|
| `Parameter_space_in_x_2` curve-end | dummy | **traits** | **traits** | **traits** | **traits** |
| `Parameter_space_in_x_2` curve | dummy | dummy | **traits** | dummy | dummy |
| `Parameter_space_in_x_2` point | dummy | dummy | **traits** | **traits** | dummy |
| `Is_on_y_identification_2` curve | dummy | dummy | dummy | dummy | **traits** |
| `Is_on_y_identification_2` point | dummy | dummy | dummy | dummy | **traits** |
| `Compare_y_on_boundary_2` points | dummy | dummy | **traits** | dummy | **traits** |
| `Compare_y_near_boundary_2` curve-ends | dummy | **traits** | **traits** | **traits** | **traits** |

(The bottom/top table is the mirror image, at lines 581–712 of the header.)

Side categories come from `CGAL/Arr_tags.h`:

```cpp
struct Arr_boundary_side_tag {};
struct Arr_oblivious_side_tag     : public virtual Arr_boundary_side_tag {};
struct Arr_not_oblivious_side_tag : public virtual Arr_boundary_side_tag {};
struct Arr_open_side_tag       : public virtual Arr_not_oblivious_side_tag {};
struct Arr_closed_side_tag     : public virtual Arr_not_oblivious_side_tag {};
struct Arr_contracted_side_tag : public virtual Arr_not_oblivious_side_tag {};
struct Arr_identified_side_tag : public virtual Arr_not_oblivious_side_tag {};
```
plus `Arr_all_sides_oblivious_tag` / `Arr_not_all_sides_oblivious_tag` (and
`Arr_has_identified_side_tag`, `Arr_has_contracted_side_tag`, `Arr_has_closed_side_tag`,
`Arr_has_open_side_tag`, `Arr_all_sides_open_tag`, `Arr_all_sides_not_open_tag`,
`Arr_all_sides_not_finite_tag`, …), and the completion helpers
`internal::Arr_complete_{left,bottom,top,right}_side_category<Traits>::Category` which default
to `Arr_oblivious_side_tag` when the traits omits the typedef.

---

## 6. `CGAL/Arr_directional_non_caching_segment_basic_traits_2.h`

```cpp
/*! A model of the following concepts:
 * 1. AosBasicTraits_2,
 * 2. AosDirectionalXMonotoneTraits_2,
 * 4. AosConstructXMonotoneCurveTraits_2, and
 * 3. AosOpenBoundaryTraits_2
 * It handles linear curves. */
template <class Kernel_T>
class Arr_directional_non_caching_segment_basic_traits_2 :
  public Arr_non_caching_segment_basic_traits_2<Kernel_T>
{
public:
  typedef Kernel_T                                         Kernel;
  typedef Arr_non_caching_segment_basic_traits_2<Kernel>   Base;
  typedef typename Base::Segment_assertions                Segment_assertions;
  typedef typename Base::Has_exact_division                Has_exact_division;

  Arr_directional_non_caching_segment_basic_traits_2() : Base() {}
```

Re-exported from the base: `Has_left_category`, `Has_do_intersect_category`, the four side
categories, `Point_2`, `X_monotone_curve_2` (== `Kernel::Segment_2`), `Compare_x_2`,
`Compare_xy_2`, `Construct_min_vertex_2`, `Construct_max_vertex_2`, `Is_vertical_2`,
`Compare_y_at_x_2`, `Equal_2`, `Compare_y_at_x_left_2`, `Compare_y_at_x_right_2`,
`Construct_x_monotone_curve_2`, `Approximate_number_type`.

Introduced here (the "directional" part):

```cpp
typedef typename Kernel::Construct_opposite_segment_2  Construct_opposite_2;
Construct_opposite_2 construct_opposite_2_object() const;

class Compare_endpoints_xy_2 {
protected:
  const Traits& m_traits;
  Compare_endpoints_xy_2(const Traits& traits);   // protected + friend
public:
  /*! \return SMALLER if cv is directed from left to right and LARGER otherwise. */
  Comparison_result operator()(const X_monotone_curve_2& cv) const;
};
Compare_endpoints_xy_2 compare_endpoints_xy_2_object() const;
```

Note: **no `Curve_2`**, no `Make_x_monotone_2`, no `Intersect_2`, no `Split_2` — it is a
*basic* traits, usable with `Arr_traits_basic_adaptor_2`, sweep-line "basic" variants and
the Boolean-set-operations directional machinery, but **not** with `Arrangement_2` insertion
of general curves, `Arr_counting_traits_2` or `Arr_tracing_traits_2` (both require
`Base::Curve_2`).

---

## 7. IO: formatters, reader, writer, stream operators

### 7.1 `CGAL/IO/Arr_iostream.h`

```cpp
namespace CGAL { namespace IO {

template <class GeomTraits, class TopTraits, class Formatter>
std::ostream& write(const Arrangement_on_surface_2<GeomTraits,TopTraits>& arr,
                    std::ostream& os, Formatter& format);

template <class GeomTraits, class TopTraits, class Formatter>
std::istream& read(Arrangement_on_surface_2<GeomTraits,TopTraits>& arr,
                   std::istream& is, Formatter& format);
} // IO

template <class GeomTraits, class TopTraits>
std::ostream& operator<<(std::ostream& os,
                         const Arrangement_on_surface_2<GeomTraits,TopTraits>& arr);

template <class GeomTraits, class TopTraits>
std::istream& operator>>(std::istream& is,
                         Arrangement_on_surface_2<GeomTraits,TopTraits>& arr);

#ifndef CGAL_NO_DEPRECATED_CODE
using IO::read;  using IO::write;
#endif
}
```

`operator<<`/`operator>>` hard-wire `Arr_text_formatter<Arrangement_2>`. `IO::write` calls
`format.set_out(os); Arrangement_2_writer<Arr>(arr)(format);` and `IO::read` calls
`format.set_in(is); Arrangement_2_reader<Arr>(arr)(format);`.
The functions are declared on `Arrangement_on_surface_2`, so they apply to
`Arrangement_2` too (which derives from it in 6.x).

### 7.2 `CGAL/IO/Arr_text_formatter.h` — `Arr_text_formatter<Arrangement_>`

```cpp
template <class Arrangement_>
class Arr_text_formatter
{
public:
  typedef Arrangement_                                   Arrangement_2;
  typedef typename Arrangement_2::Size                   Size;
  typedef typename Arrangement_2::Dcel                   Dcel;
  typedef typename Arrangement_2::X_monotone_curve_2     X_monotone_curve_2;
  typedef typename Arrangement_2::Point_2                Point_2;
  typedef typename Arrangement_2::Vertex_handle          Vertex_handle;
  typedef typename Arrangement_2::Halfedge_handle        Halfedge_handle;
  typedef typename Arrangement_2::Face_handle            Face_handle;
  typedef typename Arrangement_2::Vertex_const_handle    Vertex_const_handle;
  typedef typename Arrangement_2::Halfedge_const_handle  Halfedge_const_handle;
  typedef typename Arrangement_2::Face_const_handle      Face_const_handle;
protected:
  typedef typename Dcel::Vertex   DVertex;
  typedef typename Dcel::Halfedge DHalfedge;
  typedef typename Dcel::Face     DFace;
  std::ostream*  m_out;  IO::Mode m_old_out_mode;
  std::istream*  m_in;   IO::Mode m_old_in_mode;
public:
  Arr_text_formatter();
  Arr_text_formatter(std::ostream& os);
  Arr_text_formatter(std::istream& is);
  virtual ~Arr_text_formatter();

  void set_out(std::ostream& os);
  void set_in(std::istream& is);
  inline std::ostream& out();   // CGAL_assertion(m_out != nullptr)
  inline std::istream& in();    // CGAL_assertion(m_in != nullptr)
```

Write side (all non-virtual unless marked):

```cpp
  void write_arrangement_begin();  void write_arrangement_end();
  void write_size(const char* label, Size size);
  void write_vertices_begin();     void write_vertices_end();
  void write_edges_begin();        void write_edges_end();
  void write_faces_begin();        void write_faces_end();
  void write_vertex_begin();       void write_vertex_end();
  virtual void write_point(const Point_2& p);              // out() << p;
  virtual void write_vertex_data(Vertex_const_handle);     // {}
  void write_edge_begin();         void write_edge_end();
  void write_vertex_index(int idx);
  virtual void write_x_monotone_curve(const X_monotone_curve_2& cv);  // out() << cv;
  virtual void write_halfedge_data(Halfedge_const_handle); // {}
  void write_face_begin();         void write_face_end();
  void write_outer_ccbs_begin();   void write_outer_ccbs_end();
  void write_inner_ccbs_begin();   void write_inner_ccbs_end();
  virtual void write_face_data(Face_const_handle);         // {}
  void write_ccb_halfedges_begin();void write_ccb_halfedges_end();
  void write_halfedge_index(int idx);
  void write_isolated_vertices_begin(); void write_isolated_vertices_end();
```

Read side:

```cpp
  void read_arrangement_begin();   void read_arrangement_end();
  Size read_size(const char* title = nullptr);
  void read_vertices_begin();      void read_vertices_end();
  void read_edges_begin();         void read_edges_end();
  void read_faces_begin();         void read_faces_end();
  void read_vertex_begin();        void read_vertex_end();
  virtual void read_point(Point_2& p);                     // in() >> p; _skip_until_EOL();
  virtual void read_vertex_data(Vertex_handle);            // {}
  void read_edge_begin();          void read_edge_end();
  int  read_vertex_index();
  virtual void read_x_monotone_curve(X_monotone_curve_2& cv); // in() >> cv; _skip_until_EOL();
  virtual void read_halfedge_data(Halfedge_handle);        // {}
  void read_face_begin();          void read_face_end();
  void read_outer_ccbs_begin();    void read_outer_ccbs_end();
  void read_inner_ccbs_begin();    void read_inner_ccbs_end();
  void read_ccb_halfedges_begin(); void read_ccb_halfedges_end();
  void read_isolated_vertices_begin(); void read_isolated_vertices_end();
  virtual void read_face_data(Face_handle);                // {}
protected:
  void _write_comment(const char* str);
  void _skip_until_EOL();
  void _skip_comments();
};
```

**Requirement:** `operator<<(std::ostream&, Point_2)`, `operator<<(std::ostream&, X_monotone_curve_2)`,
`operator>>(std::istream&, Point_2)` and `operator>>(std::istream&, X_monotone_curve_2)` must
all exist — even for a write-only use, because `read_point`/`read_x_monotone_curve` are
`virtual` (gotcha 4).

Two derived formatters in the same header:

```cpp
template <class Arrangement_>
class Arr_face_extended_text_formatter : public Arr_text_formatter<Arrangement_>
{
public:
  Arr_face_extended_text_formatter();
  Arr_face_extended_text_formatter(std::ostream& os);
  Arr_face_extended_text_formatter(std::istream& is);
  virtual void write_face_data(Face_const_handle f);   // this->out() << f->data() << '\n';
  virtual void read_face_data(Face_handle f);          // this->in() >> f->data(); skip EOL
};

template <class Arrangement_>
class Arr_extended_dcel_text_formatter : public Arr_text_formatter<Arrangement_>
{
public:
  Arr_extended_dcel_text_formatter();
  Arr_extended_dcel_text_formatter(std::ostream& os);
  Arr_extended_dcel_text_formatter(std::istream& is);
  virtual void write_vertex_data(Vertex_const_handle v);   // '\n' << v->data()
  virtual void read_vertex_data(Vertex_handle v);          // reads Data_type, v->set_data(data)
  virtual void write_halfedge_data(Halfedge_const_handle he);
  virtual void read_halfedge_data(Halfedge_handle he);
  virtual void write_face_data(Face_const_handle f);
  virtual void read_face_data(Face_handle f);
};
```

`Arr_extended_dcel_text_formatter`'s readers deduce the data type as
`std::remove_reference_t<decltype(std::declval<Vertex>().data())>` and call `set_data()`,
whereas `Arr_face_extended_text_formatter` streams directly into `f->data()` (needs a
mutable `data()`).

### 7.3 `CGAL/IO/Arrangement_2_writer.h`

```cpp
template <class Arrangement_>
class Arrangement_2_writer
{
public:
  typedef Arrangement_                        Arrangement_2;
  typedef Arrangement_2_writer<Arrangement_2> Self;
protected:
  // Size, Dcel, *_const_handle, CGAL::Arr_accessor<Arrangement_2>,
  // Dcel_{vertex,edge,face,outer_ccb,inner_ccb,iso_vertex}_iterator,
  // std::map<const DVertex*,int> m_v_index; std::map<const DHalfedge*,int> m_he_index;
  const Arrangement_2& m_arr;  const Dcel* m_dcel;
  int m_curr_v;  int m_curr_he;
private:
  Arrangement_2_writer(const Self&);            // not supported
  Self& operator= (const Self&);                // not supported
public:
  Arrangement_2_writer(const Arrangement_2& arr);
  virtual ~Arrangement_2_writer();

  template <class Formatter>
  void operator()(Formatter& formatter);
protected:
  template <class Formatter> void _write_vertex(Formatter&, Vertex_const_iterator);
  template <class Formatter> void _write_edge  (Formatter&, Edge_const_iterator);
  template <class Formatter> void _write_face  (Formatter&, Face_const_iterator) const;
  template <class Formatter> void _write_ccb   (Formatter&, const DHalfedge* ccb) const;
};
```

The record order emitted by `operator()` is:
`write_arrangement_begin`, then `write_size("number_of_vertices", dcel->size_of_vertices())`,
`write_size("number_of_edges", dcel->size_of_halfedges()/2)`,
`write_size("number_of_faces", dcel->size_of_faces())`, then vertices, edges, faces.
It uses `CGAL::Arr_accessor` to reach the raw DCEL, so it *does not* rely on the public
handle API and works on `Arrangement_on_surface_2`.

### 7.4 `CGAL/IO/Arrangement_2_reader.h`

```cpp
template <class Arrangement_>
class Arrangement_2_reader
{
public:
  typedef Arrangement_                        Arrangement_2;
  typedef Arrangement_2_reader<Arrangement_2> Self;
protected:
  // Size, Dcel, X_monotone_curve_2, Point_2, *_handle,
  // CGAL::Arr_accessor<Arrangement_2> and Dcel_{vertex,halfedge,face,
  // outer_ccb,inner_ccb,isolated_vertex} raw types
private:
  Arrangement_2_reader(const Self&);
public:
  Arrangement_2_reader(Arrangement_2& arr);
  virtual ~Arrangement_2_reader();
  template <class Formatter> void operator()(Formatter& formatter);
protected:
  template <class Formatter> void _read_vertex(Formatter&);
  template <class Formatter> void _read_edge  (Formatter&);
  template <class Formatter> void _read_face  (Formatter&);
  template <class Formatter> void _read_ccb   (Formatter&, …);
};
```

`operator()` **clears the target arrangement** and rebuilds the DCEL directly through
`Arr_accessor`; no geometric validation is performed, so a corrupt stream yields a corrupt
arrangement (not an exception).

### 7.5 Arrangement-with-history IO

`CGAL/IO/Arr_with_history_iostream.h`:

```cpp
namespace CGAL { namespace IO {
template <class GeomTraits, class TopTraits, class Formatter>
std::ostream& write(const Arrangement_on_surface_with_history_2<GeomTraits,TopTraits>& arr,
                    std::ostream& os, Formatter& format);
template <class GeomTraits, class TopTraits, class Formatter>
std::istream& read(Arrangement_on_surface_with_history_2<GeomTraits,TopTraits>& arr,
                   std::istream& is, Formatter& format);
} // IO

template <class GeomTraits, class TopTraits>
std::ostream& operator<<(std::ostream& os,
   const Arrangement_on_surface_with_history_2<GeomTraits,TopTraits>& arr);
template <class GeomTraits, class TopTraits>
std::istream& operator>>(std::istream& is,
   Arrangement_on_surface_with_history_2<GeomTraits,TopTraits>& arr);
}
```

The default formatter is
`Arr_with_history_text_formatter< Arr_text_formatter<Arr_with_history_2> >`.

`CGAL/IO/Arr_with_history_text_formatter.h`:

```cpp
template <class ArrFormatter_>
class Arr_with_history_text_formatter : public ArrFormatter_
{
public:
  typedef ArrFormatter_                                   Base;
  typedef Arr_with_history_text_formatter<Base>           Self;
  typedef typename Base::Arrangement_2                    Arr_with_history_2;
  typedef typename Arr_with_history_2::Size               Size;
  typedef typename Arr_with_history_2::Dcel               Dcel;
  typedef typename Arr_with_history_2::Curve_2            Curve_2;
  typedef typename Arr_with_history_2::X_monotone_curve_2 X_monotone_curve_2;
  typedef typename Arr_with_history_2::Point_2            Point_2;
  // + Vertex/Halfedge/Face handles and const handles

  void write_curves_begin();       void write_curves_end();
  void write_curve_begin();        void write_curve_end();
  void write_curve(const Curve_2& c);          // out() << c
  void write_induced_edges_begin();void write_induced_edges_end();
  void read_curves_begin();        void read_curves_end();
  void read_curve_begin();         void read_curve_end();
  void read_curve(Curve_2& c);                 // in() >> c
  void read_induced_edges_begin(); void read_induced_edges_end();
protected:
  void __write_comment(const char* str);
  void __skip_until_EOL();
  void __skip_comments();
};
```

⇒ with-history IO additionally requires `operator<<`/`operator>>` for **`Curve_2`**.

`Arr_with_history_2_writer<ArrWithHistory_> : private Arrangement_2_writer<ArrWithHistory_>`
and `Arr_with_history_2_reader<ArrWithHistory_> : private Arrangement_2_reader<ArrWithHistory_>`;
both expose only `template <class Formatter> void operator()(Formatter&)` plus the
`Arr_with_history_2`/`Self` typedefs. The reader uses
`Arr_with_history_accessor<Arr_with_history_2>`.

### 7.6 `CGAL/Arr_geometry_traits/IO/Polycurve_2_iostream.h`

Included by `Arr_polycurve_basic_traits_2.h`. In `namespace CGAL::internal`:

```cpp
template <typename OutputStream, typename SubcurveType_2, typename PointType_2>
void write_polyline (OutputStream& os, const Polycurve_2<SubcurveType_2, PointType_2>& xcv);
template <typename OutputStream, typename Kernel_, typename PointType_2>
void write_polycurve(OutputStream& os,
                     const Polycurve_2<CGAL::Arr_segment_2<Kernel_>, PointType_2>& xcv);
template <typename OutputStream, typename Kernel_, typename PointType_2>
void write_polycurve(OutputStream& os,
                     const Polycurve_2<CGAL::Segment_2<Kernel_>, PointType_2>& xcv);
template <typename OutputStream, typename SubcurveType_2, typename PointType_2>
void write_polycurve(OutputStream& os, const Polycurve_2<SubcurveType_2, PointType_2>& xcv);
template <typename OutputStream, typename SubcurveType_2, typename PointType_2>
OutputStream& operator<<(OutputStream& os, const Polycurve_2<SubcurveType_2, PointType_2>& xcv);

template <typename InputStream, …> void read_polyline (InputStream&, Polycurve_2<…>&);
template <typename InputStream, …> void read_polycurve(InputStream&, Polycurve_2<…>&);
template <typename InputStream, typename SubcurveType_2, typename PointType_2>
InputStream& operator>>(InputStream& is, Polycurve_2<SubcurveType_2, PointType_2>& xcv);
```

Wire format:

- **polyline specialisation** (subcurve is `CGAL::Arr_segment_2<K>` or `CGAL::Segment_2<K>`) —
  **BROKEN, off by one; see gotcha 4b.**
  `write_polyline` emits `xcv.number_of_subcurves()` (= n) followed by **all n+1 points**;
  `read_polyline` reads that leading number and treats it as a *point* count, consuming only
  n points and building n−1 subcurves. The trailing point is then swallowed by
  `_skip_until_EOL()` in `Arr_text_formatter::read_x_monotone_curve`.
- **general polycurve** (any other subcurve type): `<number_of_subcurves> <sc0> <sc1> …`,
  each subcurve streamed by its own `operator<<`/`operator>>`. This path is **correct**.

### 7.7 Which traits actually support arrangement IO — **verified**

| Traits | `<<` on `Point_2` | `>>` on `Point_2` | `<<` on `X_monotone_curve_2` | `>>` on `X_monotone_curve_2` | `<<`/`>>` on `Curve_2` | `os << arr` | `is >> arr` |
|---|---|---|---|---|---|---|---|
| `Arr_segment_traits_2` | yes (kernel) | yes | yes (`Arr_segment_2`, l.1589) | yes (l.1598) | same type | **works** (verified) | **works** (verified) |
| `Arr_non_caching_segment_traits_2` | yes | yes | yes (`Kernel::Segment_2`) | yes | yes | works | works |
| `Arr_linear_traits_2` | yes | yes | yes (`Arr_linear_object_2`, l.1726) | yes (l.1739) | same type | works | works |
| `Arr_polyline_traits_2` / `Arr_polycurve_traits_2` | via subcurve traits | via subcurve traits | `internal::operator<<` on `Polycurve_2` | `internal::operator>>` | yes | compiles, but **LOSSY** for polylines (gotcha 4b) | compiles, **LOSSY** |
| `Arr_circle_segment_traits_2` | `<<` only (`_One_root_point_2`, l.143) | **no** | `<<` only (l.2065) | **no** | `<<` only (l.611) | **fails to compile** (verified) | no |
| `Arr_conic_traits_2` | yes (`Alg_kernel::Point_2`) | yes | `<<` only (`Conic_x_monotone_arc_2`, l.1354) | **no** | `<<` only (`Conic_arc_2`, l.1698) | **fails to compile** (verified) | no |
| `Arr_Bezier_curve_traits_2` | `<<` only (`_Bezier_point_2`, l.961) | **no** | `<<` only (`_Bezier_x_monotone_2`, l.554) | **no** | `<<` **and** `>>` (`_Bezier_curve_2`, l.600/619) | fails to compile | no |
| `Arr_geodesic_arc_on_sphere_traits_2` | yes (`Arr_extended_direction_3`, l.3702/3746) | yes | yes (l.3725) | yes (l.3761) | — | works | works |
| `Arr_algebraic_segment_traits_2` | via CKvA_2 | no | via CKvA_2 | no | no | no | no |
| `Arr_rational_function_traits_2` | `print()`/`operator<<` on `Algebraic_point_2` | no | `print()` on the arc | no | no | no | no |

*(File line numbers refer to the header named in the same cell.)*

---

## 8. `CGAL/draw_arrangement_2.h`

Includes `CGAL/Basic_viewer.h`, `CGAL/Graphics_scene.h`, `CGAL/Graphics_scene_options.h`,
`CGAL/Arrangement_on_surface_2.h`, `CGAL/Arrangement_2.h`,
`CGAL/Arr_geodesic_arc_on_sphere_traits_2.h`.

With `CGAL_ARR_TYPE == Arrangement_on_surface_2<GeometryTraits_2, TopologyTraits>`:

```cpp
template <typename GeometryTraits_2, typename TopologyTraits, class GSOptions>
void add_to_graphics_scene(const CGAL_ARR_TYPE& aos,
                           CGAL::Graphics_scene& graphics_scene,
                           const GSOptions& gso);

template <typename GeometryTraits_2, typename TopologyTraits>
void add_to_graphics_scene(const CGAL_ARR_TYPE& aos,
                           CGAL::Graphics_scene& graphics_scene);
   // default GSOptions: every face colored, color = get_random_color(Random(address of face))

template <typename GeometryTraits_2, typename TopologyTraits, class GSOptions>
void draw(const CGAL_ARR_TYPE& aos, const GSOptions& gso,
          const char* title = "2D Arrangement on Surface Basic Viewer");

template <typename GeometryTraits_2, typename TopologyTraits>
void draw(const CGAL_ARR_TYPE& aos,
          const char* title = "2D Arrangement on Surface Basic Viewer");
```

Internals: `namespace draw_function_for_arrangement_2 { template<typename Arr, typename GSOptions>
class Draw_arr_tool { Draw_arr_tool(Arr& a_aos, CGAL::Graphics_scene& a_gs, const GSOptions& a_gso); … } }`.
It uses the traits' `Approximate_2` for curved edges (`draw_approximate_region`,
`draw_approximate_curve(curve, approx)`) and falls back to `draw_exact_curve` /
`draw_exact_region` (`m_gs.add_segment(...)`) when the traits has no approximation. There is a
dedicated `draw_region_impl1<Kernel_, AtanX, AtanY>` overload for
`Arr_geodesic_arc_on_sphere_traits_2`.

**Qt requirement — measured, not assumed:**
- The header compiles and links with **no Qt** and no extra libraries (verified with the
  command at the top of this document).
- `CGAL::add_to_graphics_scene(arr, scene)` **works without Qt**: it fills a
  `CGAL::Graphics_scene` (a plain vertex/segment/face buffer). Verified.
- `CGAL::draw(arr)` without `-DCGAL_USE_BASIC_VIEWER` prints
  `Impossible to draw, CGAL_USE_BASIC_VIEWER is not defined.` on `std::cerr` and returns
  (`CGAL/Basic_viewer.h`, the non-Qt stub). Verified.
- To get a real window you need `-DCGAL_USE_BASIC_VIEWER` (which implies
  `CGAL_USE_BASIC_VIEWER_QT`) plus Qt6 (`Qt6::Widgets`, `Qt6::OpenGLWidgets`) linked in.

For a Python binding, the useful, Qt-free extraction path is:

```cpp
CGAL::Graphics_scene gs;                      // CGAL/Graphics_scene.h
CGAL::add_to_graphics_scene(arr, gs);
gs.bounding_box();                            // const CGAL::Bbox_3&
gs.get_buffer_for_points();                   // const Buffer_for_vao&
gs.get_buffer_for_segments();
gs.get_buffer_for_faces();
gs.get_array_of_index(int index);             // const std::vector<BufferType>&
// Local_kernel == Exact_predicates_inexact_constructions_kernel; Local_point == its Point_3
```

---

## 9. `CGAL/Arr_rational_function_traits_2.h` + `CGAL/Arr_rat_arc/*`

### 9.1 Algebraic-kernel requirement — **verified to compile and run**

`Arr_rational_function_traits_2.h` includes `<CGAL/Algebraic_kernel_d_1.h>`, which **is
present** in this install (`/opt/homebrew/include/CGAL/Algebraic_kernel_d_1.h`, 22388 bytes):

```cpp
template< class Coefficient,
          class Bound = typename CGAL::Get_arithmetic_kernel<Coefficient>::Arithmetic_kernel::Rational,
          class RepClass = internal::Algebraic_real_rep<Coefficient, Bound>,
          class Isolator = internal::Descartes<
              typename CGAL::Polynomial_type_generator<Coefficient,1>::Type, Bound> >
class Algebraic_kernel_d_1
  : public internal::Algebraic_kernel_d_1_base<
      internal::Algebraic_real_d_1<Coefficient, Bound,
                                   ::CGAL::Handle_policy_no_union, RepClass>,
      Isolator >
{};
```

Verified probe (compiled and ran, output `V=2 E=1 F=1`):

```cpp
#include <CGAL/CORE_BigInt.h>
#include <CGAL/Algebraic_kernel_d_1.h>
#include <CGAL/Arr_rational_function_traits_2.h>
#include <CGAL/Arrangement_2.h>
typedef CORE::BigInt                              Number_type;
typedef CGAL::Algebraic_kernel_d_1<Number_type>   AK1;
typedef CGAL::Arr_rational_function_traits_2<AK1> Traits_2;
typedef CGAL::Arrangement_2<Traits_2>             Arrangement_2;
AK1 ak1;  Traits_2 traits(&ak1);  Arrangement_2 arr(&traits);
auto ctr = traits.construct_x_monotone_curve_2_object();
Traits_2::Polynomial_1 x = CGAL::shift(Traits_2::Polynomial_1(1), 1);
CGAL::insert(arr, ctr(x*x - 4, Alg_real_1(-3), Alg_real_1(3)));
```

No extra link libraries beyond `-lgmp -lmpfr` were needed (`CGAL_USE_CORE` + header-only
CORE). `sizeof(Arr_rational_function_traits_2<AK1>) == 88`.

### 9.2 Public typedefs (verbatim)

```cpp
template <typename AlgebraicKernel_d_1>
class Arr_rational_function_traits_2
{
public:
  typedef AlgebraicKernel_d_1                           Algebraic_kernel_d_1;
  typedef Arr_rational_function_traits_2<Algebraic_kernel_d_1>          Self;
  typedef Arr_rational_arc::Base_rational_arc_ds_1<Algebraic_kernel_d_1>
                                                        Base_rational_arc_ds_1;
  typedef Arr_rational_arc::Base_rational_arc_d_1<Algebraic_kernel_d_1>      Base_curve_2;
  typedef Arr_rational_arc::Continuous_rational_arc_d_1<Algebraic_kernel_d_1> X_monotone_curve_2;
  typedef Arr_rational_arc::Rational_arc_d_1<Algebraic_kernel_d_1>           Curve_2;
  typedef Arr_rational_arc::Algebraic_point_2<Algebraic_kernel_d_1>          Point_2;

  typedef typename Base_rational_arc_ds_1::Algebraic_real_1   Algebraic_real_1;
  typedef typename Base_rational_arc_ds_1::Multiplicity       Multiplicity;
  typedef typename Base_curve_2::Rat_vector                   Rat_vector;
  typedef typename Base_rational_arc_ds_1::Integer            Integer;
  typedef typename Base_rational_arc_ds_1::Rational           Rational;
  typedef typename Base_rational_arc_ds_1::Polynomial_1       Polynomial_1;
  typedef typename Base_rational_arc_ds_1::Coefficient        Coefficient;
  typedef typename Base_rational_arc_ds_1::FT_rat_1           FT_rat_1;
  typedef typename Base_rational_arc_ds_1::Polynomial_traits_1 Polynomial_traits_1;

  typedef typename Algebraic_kernel_d_1::Bound                Bound;
  typedef Bound                                               Approximate_number_type;

  typedef CGAL::Arr_rational_arc::Rational_function<Algebraic_kernel_d_1> Rational_function;
  typedef CGAL::Arr_rational_arc::Cache<Algebraic_kernel_d_1>             Cache;

  typedef Tag_true Has_left_category;
  typedef Tag_true Has_merge_category;
  typedef Tag_true Has_do_intersect_category;
  typedef Tag_true Has_vertical_segment_category;   // unique to this traits

  typedef Arr_open_side_tag          Left_side_category;
  typedef Arr_open_side_tag          Bottom_side_category;
  typedef Arr_open_side_tag          Top_side_category;
  typedef Arr_open_side_tag          Right_side_category;
```

Note `Approximate_number_type` is the algebraic kernel's `Bound` (a rational), **not** `double`,
and there is **no** `Approximate_point_2`.

### 9.3 Construction / ownership — critical for bindings

```cpp
private:
  mutable Cache                   _cache;
  mutable Algebraic_kernel_d_1*   _ak_ptr;
  bool                            delete_ak;
public:
  Algebraic_kernel_d_1* algebraic_kernel_d_1() const;   // returns the raw pointer
  bool delete_ak_internal_flag() const;
  const Cache& cache() const;

  Arr_rational_function_traits_2();                      // OWNS a new Algebraic_kernel_d_1
  Arr_rational_function_traits_2(Algebraic_kernel_d_1* ak_ptr);  // does NOT own it
  Arr_rational_function_traits_2(const Self& other);     // deep-copies AK iff other owned it
  ~Arr_rational_function_traits_2();                     // deletes the AK iff delete_ak

  void cleanup_cache() const;                            // _cache.cleanup()
```

Lifetime rules: the pointer form is **non-owning** — the caller must outlive the traits.
The copy constructor copies the cache with `_cache.initialize(other.cache(), _ak_ptr)`.
The cache is `mutable` and grows monotonically; call `cleanup_cache()` between batches.
Every curve/point object stores a reference into that cache — **curves must not outlive the
traits**.

### 9.4 Functors

```cpp
class Construct_x_monotone_curve_2 {            // protected ctor(const Traits*), friend
public:
  typedef Polynomial_1 argument_type, first_argument_type, second_argument_type;
  typedef X_monotone_curve_2 result_type;

  X_monotone_curve_2 operator()(const Polynomial_1& P) const;
  template <typename InputIterator>
  X_monotone_curve_2 operator()(InputIterator begin, InputIterator end) const;
  X_monotone_curve_2 operator()(const Polynomial_1& P, const Algebraic_real_1& x_s,
                                bool dir_right) const;
  template <typename InputIterator>
  X_monotone_curve_2 operator()(InputIterator begin, InputIterator end,
                                const Algebraic_real_1& x_s, bool dir_right) const;
  X_monotone_curve_2 operator()(const Polynomial_1& P, const Algebraic_real_1& x_s,
                                const Algebraic_real_1& x_t) const;
  template <typename InputIterator>
  X_monotone_curve_2 operator()(InputIterator begin, InputIterator end,
                                const Algebraic_real_1& x_s, const Algebraic_real_1& x_t) const;
  X_monotone_curve_2 operator()(const Polynomial_1& P, const Polynomial_1& Q) const;
  template <typename InputIterator>
  X_monotone_curve_2 operator()(InputIterator begin_numer, InputIterator end_numer,
                                InputIterator begin_denom, InputIterator end_denom) const;
  X_monotone_curve_2 operator()(const Polynomial_1& P, const Polynomial_1& Q,
                                const Algebraic_real_1& x_s, bool dir_right) const;
  template <typename InputIterator>
  X_monotone_curve_2 operator()(InputIterator begin_numer, InputIterator end_numer,
                                InputIterator begin_denom, InputIterator end_denom,
                                const Algebraic_real_1& x_s, bool dir_right) const;
  X_monotone_curve_2 operator()(const Polynomial_1& P, const Polynomial_1& Q,
                                const Algebraic_real_1& x_s, const Algebraic_real_1& x_t) const;
  template <typename InputIterator>
  X_monotone_curve_2 operator()(InputIterator begin_numer, InputIterator end_numer,
                                InputIterator begin_denom, InputIterator end_denom,
                                const Algebraic_real_1& x_s, const Algebraic_real_1& x_t) const;
};
Construct_x_monotone_curve_2 construct_x_monotone_curve_2_object() const;
```

`Construct_curve_2` (`construct_curve_2_object() const`) mirrors the same 12 overloads but
returns `Curve_2` (possibly discontinuous, i.e. with poles).

```cpp
class Construct_point_2 {                        // protected ctor(const Traits*), friend
public:
  Point_2 operator()(const Rational_function& rational_function,
                     const Algebraic_real_1& x_coordinate);            // NOT const
  Point_2 operator()(const Rational& x, const Rational& y);            // NOT const
  Point_2 operator()(const Algebraic_real_1& x, const Rational& y);    // NOT const
};
Construct_point_2 construct_point_2_object() const;
```

⚠ `Construct_point_2::operator()` overloads are **non-const**.

Remaining functors and accessors (all `*_2_object() const`), header lines in brackets:
`Compare_x_2` [500/518], `Compare_xy_2` [524/555], `Construct_min_vertex_2` [561/575],
`Construct_max_vertex_2` [581/595], `Is_vertical_2` [601/616], `Compare_y_at_x_2` [624],
`Compare_y_at_x_left_2` [656/695], `Compare_y_at_x_right_2` [703], `Equal_2` [751/798],
`Make_x_monotone_2` [806/836], `Split_2` [840/862], `Intersect_2` [868/892],
`Are_mergeable_2` [898/915], `Merge_2` [923/957],
`Parameter_space_in_x_2` [/1002], `Parameter_space_in_y_2` [/1045],
`Compare_y_near_boundary_2` [1112/1139], `Compare_x_on_boundary_2` [1146/1185],
`Compare_x_near_boundary_2` [1193/1215], `Compare_endpoints_xy_2` [1218/1237],
`Construct_opposite_2` [1242/1256], `Approximate_2` [1263/1283].

```cpp
class Approximate_2 {
  Approximate_number_type approx_x(const Point_2& p);   // p.x().lower()
  Approximate_number_type approx_y(const Point_2& p);   // numer/denom evaluated at x().lower()
public:
  Approximate_number_type operator()(const Point_2& p, int i);   // NON-const, i in {0,1}
};
Approximate_2 approximate_2_object() const { return Approximate_2(); }
```

There is a commented-out `Construct_vertical_segment` (lines 465–489) — despite
`Has_vertical_segment_category = Tag_true`, no such functor is exposed in 6.1.

### 9.5 `CGAL/Arr_rat_arc/*` — the geometry objects

| Header | Public class(es) |
|---|---|
| `Base_rational_arc_ds_1.h` | `template <typename Algebraic_kernel_> class Base_rational_arc_ds_1` — the typedef hub (`Algebraic_real_1`, `Multiplicity`, `Polynomial_1`, `Coefficient`, `Arithmetic_kernel`, `Rational`, `Integer`, `Algebraic_vector`, `Multiplicity_vector`, `FT_rat_1`, `Solve_1`) |
| `Rational_function.h` | `Rational_function_rep`, `template <class Algebraic_kernel_> class Rational_function` (handle) |
| `Rational_function_pair.h` / `_ordered_pair.h` / `_canonicalized_pair.h` | cached pairwise analyses used by `Intersect_2` |
| `Cache.h` | `template <typename AlgebraicKernel_d_1> class Cache : public Base_rational_arc_ds_1<…>` with `Less_compare_rational_function_key`, `Less_compare_rational_function_pair_key`; `initialize(AK*)`, `initialize(const Cache&, AK*)`, `get_rational_function(...)`, `cleanup()` |
| `Algebraic_point_2.h` | `Algebraic_point_2_rep`, `Algebraic_point_2` |
| `Rational_arc_d_1.h` | `Base_rational_arc_d_1`, `Continuous_rational_arc_d_1` (== `X_monotone_curve_2`), `Rational_arc_d_1` (== `Curve_2`) |
| `Singleton.h` | `template <class T> class Singleton` |

`Algebraic_point_2` public API (a `Handle_with_policy`):

```cpp
template <typename Algebraic_kernel_>
class Algebraic_point_2 : public Handle_with_policy<Algebraic_point_2_rep<Algebraic_kernel_> >
{
public:
  typedef Algebraic_kernel_                       Algebraic_kernel_d_1;
  typedef Algebraic_point_2<Algebraic_kernel_d_1> Self;
  typedef typename Rep::Rational                  Rational;
  typedef typename Rep::Algebraic_real_1          Algebraic_real_1;
  typedef typename Rep::Rational_function         Rational_function;
  typedef typename Rep::Bound                     Bound;
  typedef typename Rep::Cache                     Cache;
  typedef typename Rep::Polynomial_1              Polynomial_1;

  static Self& get_default_instance();
  explicit Algebraic_point_2(const Rational_function& rational_function,
                             const Algebraic_real_1& x_coordinate);
  Algebraic_point_2();                       // == get_default_instance()

  Comparison_result compare_xy_2(const Algebraic_point_2& other, const Cache& cache) const;
  const Polynomial_1& numerator() const;
  const Polynomial_1& denominator() const;
  Algebraic_real_1& x();                     // copy-on-write, NON-const
  const Algebraic_real_1& x() const;
  const Rational_function& rational_function() const;
  Algebraic_real_1 y() const;                // by value
  std::pair<double,double> to_double() const;
  std::pair<Bound,Bound> approximate_absolute_x(int a) const;
  std::pair<Bound,Bound> approximate_absolute_y(int a) const;
  std::pair<Bound,Bound> approximate_relative_x(int r) const;
  std::pair<Bound,Bound> approximate_relative_y(int r) const;
  std::ostream& print(std::ostream& os) const;
};
template <typename Algebraic_kernel_>
std::ostream& operator<<(std::ostream& os, const Algebraic_point_2<Algebraic_kernel_>& p);
```

`to_double()` and `approximate_*` are the practical bridge to Python floats.

`Base_rational_arc_d_1` public API (inherited by both `Continuous_rational_arc_d_1` and
`Rational_arc_d_1`):

```cpp
typedef Algebraic_point_2 Point_2;
typedef std::vector<Rational> Rat_vector;

const Polynomial_1& numerator() const;
const Polynomial_1& denominator() const;
Arr_parameter_space source_parameter_space_in_x() const;
Arr_parameter_space source_parameter_space_in_y() const;
Arr_parameter_space target_boundary_in_x() const;
Arr_parameter_space target_boundary_in_y() const;
const Algebraic_point_2& source() const;
Algebraic_real_1 source_x() const;
const Algebraic_point_2& target() const;
Algebraic_real_1 target_x() const;
Arr_parameter_space left_parameter_space_in_x() const;
Arr_parameter_space left_parameter_space_in_y() const;
Arr_parameter_space right_parameter_space_in_x() const;
Arr_parameter_space right_parameter_space_in_y() const;
const Algebraic_real_1& left_x() const;
const Algebraic_real_1& right_x() const;
const Algebraic_point_2& left() const;
const Algebraic_point_2& right() const;
bool is_valid() const;
bool is_continuous() const;
bool is_directed_right() const;
void set_continuous();
void set_invalid();
Self split_at_pole(const Algebraic_real_1& x0);
bool equals(const Self& arc) const;
bool can_merge_with(const Self& arc) const;
Self flip() const;
std::ostream& print(std::ostream& os) const;
```

`source()`/`target()`/`left()`/`right()` return **references into the arc**; do not keep them
past a modification. There is `print()` but **no** `operator>>` for the arc types.

---

## 10. `CGAL/Arr_algebraic_segment_traits_2.h` — requirements only

```cpp
template <class Coefficient_>
class Arr_algebraic_segment_traits_2 {
public:
  enum Site_of_point { POINT_IN_INTERIOR = 0, MIN_ENDPOINT = -1, MAX_ENDPOINT = 1 };

  typedef Coefficient_ Coefficient;
  typedef CGAL::Algebraic_kernel_d_1<Coefficient>                  Algebraic_kernel_d_1;
  typedef CGAL::Algebraic_curve_kernel_2<Algebraic_kernel_d_1>     Algebraic_kernel_d_2;
  typedef CGAL::Curved_kernel_via_analysis_2<Algebraic_kernel_d_2> CKvA_2;
  typedef CGAL::Arr_algebraic_segment_traits_2<Coefficient>        Self;

  Arr_algebraic_segment_traits_2();
  Arr_algebraic_segment_traits_2(const Self&);      // stateless
  const Self& operator=(const Self& s);

  typedef typename Algebraic_kernel_d_2::Algebraic_real_1  Algebraic_real_1;
  typedef typename Algebraic_kernel_d_2::Bound             Bound;
  typedef typename Algebraic_kernel_d_2::Polynomial_2      Polynomial_2;
  typedef typename Algebraic_kernel_d_2::Curve_analysis_2  Curve_2;
  typedef typename CKvA_2::Arc_2                           X_monotone_curve_2;
  typedef typename CKvA_2::Point_2                         Point_2;
  // Has_*_category, {Left,Bottom,Top,Right}_side_category, Multiplicity: all from CKvA_2
```

**Requirements:** `Algebraic_kernel_d_2` is **not** `CGAL/Algebraic_kernel_d_2.h`'s class — it
is `CGAL::Algebraic_curve_kernel_2<Algebraic_kernel_d_1<Coefficient>>`, and everything routes
through `CKvA_2::instance()` (a *global singleton*, so the traits is stateless but **not**
thread-safe across different coefficient instantiations). `/opt/homebrew/include/CGAL/Algebraic_kernel_d_2.h`
and the whole `CGAL/Algebraic_kernel_d/` directory (`Algebraic_curve_kernel_2.h`,
`Curve_analysis_2.h`, `Curve_pair_analysis_2.h`, `Bitstream_descartes*.h`, `Descartes.h`, …)
are present in this install, so the traits is available — but it is by far the heaviest
compile in the package and is deliberately deferred here.

Functors are pass-throughs `typedef typename CKvA_2::Xxx Xxx;` with
`Xxx xxx_object() const { return CKvA_2::instance().xxx_object(); }` for:
`Compare_x_2`, `Compare_xy_2`, `Compare_endpoints_xy_2`, `Equal_2`, `Parameter_space_in_y_2`,
`Compare_y_near_boundary_2`, `Parameter_space_in_x_2`, `Compare_x_on_boundary_2`,
`Compare_x_near_boundary_2`, `Construct_min_vertex_2`, `Construct_max_vertex_2`,
`Construct_opposite_2`, `Is_vertical_2`, `Compare_y_at_x_2`, `Compare_y_at_x_left_2`,
`Compare_y_at_x_right_2`, `Intersect_2`, `Split_2`, `Are_mergeable_2`, `Merge_2`,
`Make_x_monotone_2`, `Is_on_2`, `Construct_point_2`.

Defined locally: `Construct_x_monotone_segment_2` (line 216),
`Construct_vertical_segment_2` (547, accessor 588), `Construct_curve_2` (594, accessor 610),
`Connect_points_2` (618, two `template <class OutputIterator>` overloads).
**No `Approximate_2`, no `Trim_2`, no `Construct_bbox_2`, no stream operators.**

---

## 11. Feature matrix (verified by grepping each installed header)

`Y` = present, `—` = absent, `(base)` = inherited unchanged from the subcurve/base traits.
Line numbers in parentheses are in the traits' own header unless noted.

| Feature | `Arr_segment_traits_2` | `Arr_linear_traits_2` | `Arr_circle_segment_traits_2` | `Arr_polyline_traits_2` (`Arr_polycurve_traits_2`) | `Arr_Bezier_curve_traits_2` | `Arr_conic_traits_2` | `Arr_geodesic_arc_on_sphere_traits_2` |
|---|---|---|---|---|---|---|---|
| Template params | `<Kernel_ = Epeck>` | `<Kernel_>` | `<Kernel_, bool Filter = true>` | `<SegmentTraits_2 = Arr_segment_traits_2<>>` (`<SubcurveTraits_2 = …>`) | `<RatKernel_, AlgKernel_, NtTraits_, BoundingTraits_ = Bezier_bounding_rational_traits<RatKernel_>>` | `<RatKernel, AlgKernel, NtTraits>` | `<Kernel_, int atan_x = -1, int atan_y = 0>` |
| Derives from kernel | **yes** (`: public Kernel_`) | **yes** | no | no (holds subcurve traits) | no | no | **yes** (`: public Kernel_`) |
| `Multiplicity` | `unsigned int` (234) | `unsigned int` (502) | `unsigned int` (53) | `Subcurve_traits_2::Multiplicity` | `X_monotone_curve_2::Multiplicity` (98) | **`size_t`** (92) | **`std::size_t`** (151) |
| `Has_left_category` | `Tag_true` | `Tag_true` | `Tag_true` | (base) | `Tag_true` | `Tag_true` | `Tag_true` |
| `Has_merge_category` | `Tag_true` | `Tag_true` | `Tag_true` | `Subcurve_traits_2::…`¹ | `Tag_true` | `Tag_true` | `Tag_true` |
| `Has_do_intersect_category` | `Tag_false` | `Tag_false` | `Tag_false` | (base) | `Tag_false` | `Tag_false` | `Tag_false` |
| side categories | all `Arr_oblivious_side_tag` | all `Arr_open_side_tag` | all `Arr_oblivious_side_tag` | (base) | all `Arr_oblivious_side_tag` | all `Arr_oblivious_side_tag` | L/R `Arr_identified_side_tag`, B/T `Arr_contracted_side_tag` |
| `Approximate_number_type` | `double` (885) | `double` (1520) | `double` (378) | (base) | **—** | `double` (1526) | `double` (2858) |
| `Approximate_kernel` / `Approximate_point_2` | `Cartesian<double>` / its `Point_2` | **— / —** | `Cartesian<double>` / its `Point_2` | (base) | — | `Cartesian<double>` / its `Point_2` | `Cartesian<double>` / **`Arr_extended_direction_3<Approximate_kernel>`** |
| `Approximate_2(p, int i)` | Y (911) | Y (1531) | Y (404) | Y (619) | **—** | Y (1670) | Y (2873), `i ∈ {0,1,2}` |
| `Approximate_2(p)` → point | Y (918) | **—** | Y (411) | Y (622) | — | Y (1679) | Y (2881) |
| `Approximate_2(xcv, error, oi, l2r=true)` | Y (924)² | **—** | Y (417) | Y (627)² | — | Y (1685) | Y (2891), `error` is `Approximate_number_type` |
| extra `Approximate_2` overload | — | — | — | — | — | `Approximate_number_type operator()(const X_monotone_curve_2&)` = **arc length** (1549) | — |
| `Construct_x_monotone_curve_2` | Y (944/1034) | Y (1542/1560) | **—** | Y (451/476) | **—** | Y (2063/2212) | Y (568/753) |
| `Construct_curve_2` | Y (`typedef Construct_x_monotone_curve_2 Construct_curve_2;`, accessor 1045) | typedef Y, **accessor 1571 does not compile** ✘ (gotcha 15b) | — | Y (351/367) | — | Y (2216/2793) | Y (757/1008) |
| `Trim_2` | Y (1052/1102) | Y (566/616) | Y (838/885) | Y (via `Arr_polycurve_basic_traits_2`, 2343) | Y (762/811) | Y (2830/2928) | **—** |
| `Construct_opposite_2` | Y (1120/1131) | Y (618/655) | Y (819/833) | Y (accessor via base) | Y (813/830) | Y (2816/2827) | Y (3012/3023) |
| `Compare_endpoints_xy_2` | Y (1104/1117) | Y (550/563) | Y (798/814) | Y (via base) | Y (735/757) | Y (2800/2813) | Y (2996/3009) |
| `Construct_bbox_2` functor | **—** | — | — | — | — | **Y** (2934, accessor 3040) | — |
| `Bbox_2 bbox() const` on the curve | Y (`Arr_segment_2::bbox()`, 1523/1579) | Y (`Arr_linear_object_2::bbox()`, 1707) — **`\pre is_segment()`** | Y (`_X_monotone_circle_segment_2::bbox()`, `Circle_segment_2.h:1130`) | Y (`internal::Polycurve_2::bbox()`, `Polycurve_2.h:188`) | Y (`_Bezier_curve_2::bbox()`, `Bezier_curve_2.h:529`, returns `const Bbox_2&`) | via `Construct_bbox_2` | **—** (`#if 0`'d at 3444) |
| `Is_in_x_range_2` | only via `Arr_traits_basic_adaptor_2` | idem | idem | idem | idem | idem | idem |
| `Are_mergeable_2` / `Merge_2` | Y (788/830) | Y (1421/1456) | Y (733/756) | Y (`Arr_polycurve_traits_2` 1002/1054) | Y (663/688) | Y (1408/1458) | Y (2685/2755) |
| `Parameter_space_in_x/y_2` | — (oblivious) | Y (959/996) | — | (base) | — | Y (766/801) | Y (1508/1573) |
| `operator<<` on `Curve_2` | Y (1589) | Y (1726) | Y (`Circle_segment_2.h:611`) | Y (`Polycurve_2_iostream.h:73`) | Y (`Bezier_curve_2.h:600`) | Y (`Conic_arc_2.h:1698`) | Y (3725, on the x-monotone arc; `Curve_2` shares it) |
| `operator>>` on `Curve_2` | Y (1598) | Y (1739) | **—** | Y (`Polycurve_2_iostream.h:151`) | Y (`Bezier_curve_2.h:619`) | **—** | Y (3761) |
| `operator<<` / `>>` on `Point_2` | kernel: Y / Y | kernel: Y / Y | Y (`Circle_segment_2.h:143`) / **—** | (base) | Y (`Bezier_point_2.h:961`) / **—** | `Alg_kernel::Point_2`: Y / Y | Y (3702) / Y (3746) |
| **`os << arr` compiles** | **Y** ✔ | Y | **NO** ✘ | Y ✔ but **lossy** ✘ | NO | **NO** ✘ | Y |
| **`is >> arr` compiles** | **Y** ✔ | Y | NO | Y ✔ but **lossy** ✘ | NO | NO | Y |

¹ `Arr_polycurve_basic_traits_2` has **no** `Has_merge_category`; only `Arr_polycurve_traits_2`
adds `using Has_merge_category = typename Subcurve_traits_2::Has_merge_category;`.
² `error` is ignored for segments/polylines (the parameter is named `/* error */`).
✔/✘ = verified by compiling.

Also relevant, not in the table:

- `Arr_non_caching_segment_basic_traits_2<Kernel_T>`: `Multiplicity = unsigned int` (85),
  `Approximate_number_type = double`, `Approximate_kernel = Cartesian<double>`,
  `Approximate_point_2 = Approximate_kernel::Point_2` (225–227). No `Curve_2`.
- `Arr_non_caching_segment_traits_2<Kernel_T = Epeck>` adds `Are_mergeable_2` (273),
  `Merge_2` (315), `Construct_opposite_2` (accessor 377), `Compare_endpoints_xy_2` (380/400),
  `Construct_curve_2` (accessor 411). No `Trim_2`, no `Construct_bbox_2`.
- `Arr_polycurve_basic_traits_2` additionally exposes `Number_of_points_2`, `Push_back_2`,
  `Push_front_2`, `Construct_point_2`, `Trim_2` (2343), and derives `Approximate_2` from the
  subcurve traits via a `void_t` detector — **`Approximate_2` becomes `void`** when the
  subcurve traits has none, and `approximate_2_object()` then returns nothing
  (`Approximate_2 approximate_2_object_impl(std::true_type) const { }` — UB if ever called;
  it compiles only because it is never instantiated).

### Concept ⇒ functor checklist (as dispatched by the adaptor)

- **`AosBasicTraits_2`** (`ArrangementBasicTraits_2`): `Point_2`, `X_monotone_curve_2`,
  `Multiplicity`, `Has_left_category`, four side categories, and
  `Compare_x_2(p,p)`, `Compare_xy_2(p,p)`, `Construct_min_vertex_2(xcv)`,
  `Construct_max_vertex_2(xcv)`, `Is_vertical_2(xcv)`, `Compare_y_at_x_2(p,xcv)`,
  `Compare_y_at_x_right_2(xcv,xcv,p)`, `Equal_2(p,p)`, `Equal_2(xcv,xcv)`; optionally
  `Compare_y_at_x_left_2(xcv,xcv,p)` (synthesized if `Has_left_category == Tag_false`).
- **`AosXMonotoneTraits_2`**: adds `Intersect_2(xcv,xcv,oi)` (variant output),
  `Split_2(xcv,p,xcv&,xcv&)`, `Are_mergeable_2(xcv,xcv)`, `Merge_2(xcv,xcv,xcv&)`,
  `Has_merge_category`, optionally `Do_intersect_2(xcv,xcv)` + `Has_do_intersect_category`
  (synthesized otherwise).
- **`AosTraits_2`**: adds `Curve_2` and `Make_x_monotone_2(cv,oi)`.
- **`AosOpenBoundaryTraits_2`** (`ArrangementOpenBoundaryTraits_2`):
  `Parameter_space_in_x_2(xcv,ce)` / `(p)`, `Parameter_space_in_y_2(xcv,ce)` / `(p)`,
  `Compare_x_near_boundary_2(xcv,xcv,ce)`, `Compare_y_near_boundary_2(xcv,xcv,ce)`,
  `Compare_x_on_boundary_2` (3 overloads), `Compare_y_on_boundary_2(p,p)`,
  `Is_on_x_identification_2` / `Is_on_y_identification_2` (each `(p)` and `(xcv)`).
  Which of these the adaptor actually forwards is decided by the tables in §5.3.
- **`AosApproximateTraits_2`** (landmarks / drawing): `Approximate_number_type`,
  `Approximate_point_2`, `Approximate_2` with `(p,int)`, `(p)`, `(xcv,error,oi,l2r)`.
- **`AosConstructXMonotoneCurveTraits_2`**: `Construct_x_monotone_curve_2(p,q)`.
- **`AosDirectionalXMonotoneTraits_2`**: `Compare_endpoints_xy_2(xcv)` (SMALLER ⇒ left-to-right)
  and `Construct_opposite_2(xcv)`.

---

## 12. Practical notes for a type-erased C++ core + Cython bindings

1. **Never store functors.** Fetch `traits.xxx_2_object()` at the point of use. All adaptor
   functors keep raw back-pointers, and the counting/tracing decorators keep `std::size_t&`
   references into the traits' `m_counters` array.
2. **Type-erase `Multiplicity` as `std::size_t`** (conic and geodesic already use it; the
   others use `unsigned int`).
3. **Model the intersection result as a tagged union of
   `(Point, size_t)` and `XMonotoneCurve`** — that is literally
   `std::variant<std::pair<Point_2, Multiplicity>, X_monotone_curve_2>`.
   `Make_x_monotone_2` produces `std::variant<Point_2, X_monotone_curve_2>`.
4. **Do not route serialization through `operator<<`/`operator>>` on the arrangement.** It
   only compiles for segment / non-caching-segment / linear / polyline / geodesic, and for
   polylines it is *silently lossy* (gotcha 4b). Write your own DCEL walker, or reuse
   `Arrangement_2_writer` / `Arrangement_2_reader` with a **hand-written formatter that does
   not derive from `Arr_text_formatter`** (deriving keeps the virtual `read_*` instantiations,
   gotcha 4).
5. **Feature-probe with `CGAL/Arr_has.h`** for optional functors, but implement your own
   `has_equal_2` (the shipped one is broken) and add detectors for `Trim_2`,
   `Construct_bbox_2`, `Construct_curve_2` and `Construct_x_monotone_curve_2`, which the
   header does not provide.
6. **`Arr_curve_data_traits_2` is the cheapest way to attach a Python-side id to a curve**:
   use `XMonotoneCurveData = std::uint64_t` (or a small POD), a custom `Merge_` that combines
   ids, and `CurveData = std::uint64_t`. Remember `Has_merge_category` is forced to `Tag_true`
   and `Are_mergeable_2` requires `Data::operator==`.
7. **For the rational-function traits, keep the `Algebraic_kernel_d_1` and the traits alive for
   the whole lifetime of every curve, point and arrangement** — points and arcs hold cache
   references. `traits.cleanup_cache()` is the only knob for bounding memory growth.
8. **Use `CGAL::add_to_graphics_scene` for headless rendering data** rather than reimplementing
   curve flattening; it works with no Qt (gotcha 15).
