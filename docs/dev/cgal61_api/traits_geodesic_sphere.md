# CGAL 6.1 — Geodesic arcs on the sphere + spherical topology (`Arrangement_on_surface_2`)

Source of truth: the installed headers under `/opt/homebrew/include/CGAL` (CGAL 6.1, header-only,
`$URL: .../cgal/blob/v6.1/...`). Everything below is quoted from / verified against:

* `/opt/homebrew/include/CGAL/Arr_geodesic_arc_on_sphere_traits_2.h` (3789 lines)
* `/opt/homebrew/include/CGAL/Arr_geodesic_arc_on_sphere_partition_traits_2.h` (599 lines)
* `/opt/homebrew/include/CGAL/Arr_spherical_topology_traits_2.h` (640 lines)
* `/opt/homebrew/include/CGAL/Arr_topology_traits/Arr_spherical_topology_traits_2_impl.h` (991 lines)
* `/opt/homebrew/include/CGAL/Arr_extended_dcel.h` (245 lines)
* `/opt/homebrew/include/CGAL/Arrangement_on_surface_2.h` (3013 lines), `Arr_dcel_base.h`, `Arr_enums.h`, `enum.h`
* `/opt/homebrew/include/CGAL/Arrangement_on_surface_with_history_2.h`

Runtime/compile facts marked **[verified]** were produced by compiling and running small programs with
`/usr/bin/clang++ -std=c++17 -O0 -DCGAL_USE_CORE -DCGAL_USE_GMP -DCGAL_USE_MPFR -I/opt/homebrew/include -L/opt/homebrew/lib -lgmp -lmpfr`
(kernel used: `CGAL::Exact_predicates_exact_constructions_kernel`; ~3 s per compile).

---

## Gotchas / surprises vs. older CGAL (and vs. the rest of the Arrangement package)

1. **`Point_2` is a 3D `Direction_3`, unnormalized, and its default value is the north pole.**
   `Point_2 = Arr_extended_direction_3<Kernel>` derives publicly from `Kernel::Direction_3`; the default
   ctor is `Direction_3(0,0,1)` with `MAX_BOUNDARY_LOC` **[verified]**. There is no `x()/y()`; you get
   `dx()/dy()/dz()` and they are *not* normalized — `Intersect_2` returns points such as `(2,2,0)`
   **[verified]**. Any Cython layer must normalize itself (e.g. via `Approximate_2`) before showing
   coordinates to a user. Two points are equal iff the *directions* are equal, so `(1,0,0) == (2,0,0)`.
2. **Several public members of these headers do not compile or are UB. Do not bind them:**
   * `Arr_geodesic_arc_on_sphere_3::Arr_geodesic_arc_on_sphere_3(const Direction_3&)` —
     *hard compile error* (`this->normal(normal)` calls the 0-arg accessor), header line 3680 **[verified]**.
     Use `traits.construct_curve_2_object()(normal)` instead.
   * `Arr_x_monotone_geodesic_arc_on_sphere_3::init()` — *hard compile error* (calls the non-static
     `Traits::orientation` as if static), header lines 3219/3223/3244/3245 **[verified]**.
   * `Arr_geodesic_arc_on_sphere_traits_2::Construct_x_monotone_curve_2::operator()(const Direction_3&)`
     (full great circle as an *x-monotone* curve) — **missing `return`**, i.e. UB
     (`warning: non-void function does not return a value in all control paths`, header line 639) **[verified]**.
     Full x-monotone arcs are disabled anyway (`CGAL_FULL_X_MONOTONE_GEODESIC_ARC_ON_SPHERE_IS_SUPPORTED`
     is commented out at the top of the header); the sibling ctor `Xcv(point, normal)` calls
     `CGAL_error_msg("Full x-monotone arcs are not supported!")` at runtime.
   * `Arr_geodesic_arc_on_sphere_partition_traits_2::is_convex_2_object()` — compile error
     (`Is_convex_2` only has a 1-arg template ctor, so it is not default-constructible) **[verified]**.
   * `Arrangement_on_surface_2::fictitious_face()` — compile error with this topology traits
     (`no member named 'initial_face' in Arr_spherical_topology_traits_2`) **[verified]**. There are no
     fictitious vertices/halfedges/faces at all on the sphere.
3. **`std::variant` / `std::optional` everywhere (no `CGAL::Object`, no boost).**
   `Make_x_monotone_2` writes `std::variant<Point_2, X_monotone_curve_2>`;
   `Intersect_2` writes `std::variant<std::pair<Point_2, Multiplicity>, X_monotone_curve_2>` with
   `Multiplicity = std::size_t`; the topology traits returns
   `std::optional<std::variant<Vertex*, Halfedge*>>` and `std::variant<Vertex*, Halfedge*, Face*>`.
4. **A point on the identification curve is ONE DCEL vertex, not two** (it is registered in the
   topology traits' `m_boundary_vertices` map, keyed by `Point_2`, ordered by `compare_y_on_boundary_2`).
   Its `parameter_space_in_x()` is `ARR_LEFT_BOUNDARY` (== 0) and `parameter_space_in_y()` is
   `ARR_INTERIOR` (== 4) **[verified]**. The two poles are *separate* singleton vertices
   (`north_pole()`, `south_pole()`), and are *not* in the boundary-vertex map; a pole vertex has
   `parameter_space_in_y() == ARR_TOP/BOTTOM_BOUNDARY` while its `parameter_space_in_x()` may be
   either `ARR_INTERIOR` or `ARR_LEFT_BOUNDARY` depending on which curve end created it **[verified]**.
5. **"Unbounded" is meaningless here: `is_unbounded()` is hard-coded `false`, so
   `arr.number_of_unbounded_faces() == 0` always** **[verified]**. The distinguished face is the
   **spherical face** = *the unique face with **zero** outer CCBs*, and it always contains the north pole
   **[verified]**. Use `arr.topology_traits()->spherical_face()` / `arr.reference_face()`, never
   `unbounded_face()` (it does not exist on `Arrangement_on_surface_2`). Every other face has exactly one
   outer CCB in every configuration I tested, but the *spherical face* forces you to use
   `outer_ccbs_begin()/outer_ccbs_end()`: `Face::outer_ccb()` has
   `CGAL_precondition(number_of_outer_ccbs() == 1)` and would fire on it.
6. Bonus: the functors `Construct_point_2::operator()`, `Construct_curve_2::operator()(p,q[,n])`,
   `Compare_endpoints_xy_2::operator()` and `Construct_opposite_2::operator()` are **non-`const`**;
   `auto f = traits.construct_point_2_object();` must not be `const auto`.
7. `Arr_geodesic_arc_on_sphere_partition_traits_2.h` physically contains **two copies** of the same file
   (lines 1–304 and 306–599); the second is dead code because of the include guard. Only the first copy
   is compiled. Also, `disable_warnings.h` is included in the live copy but `enable_warnings.h` only in the
   dead copy, so including that header leaves CGAL's warning suppression on.
8. **Eight more sphere-specific gotchas about the *global algorithms* (overlay, zone, decompose,
   batched point location, removal, observers, with-history) are listed in [§10.0](#100-extra-gotchas-discovered-here-read-these-first),
   with the "what does NOT work" summary in [§10.10](#1010-what-does-not-work-on-the-spherical-topology-short-list).**
   In particular: `CGAL::decompose` aborts on any boundary vertex; incremental `CGAL::insert` of a curve
   *on* the identification meridian aborts (use the aggregate overload); batched `CGAL::locate`
   mis-answers a query at the south pole; `CGAL::remove_vertex` on an isolated boundary vertex leaves a
   dangling pointer and the next insert segfaults; the default point-location strategy here is
   `Arr_naive_point_location`, **not** walk (which does not even compile).

---

## 1. `Arr_extended_direction_3<Kernel>` (the `Point_2`)

Header lines 46–117. Publicly derives from `Kernel::Direction_3` (so all of `Direction_3`'s API —
`dx()`, `dy()`, `dz()`, `vector()`, `operator==`, `operator!=`, `operator-` (opposite) — is available).

```cpp
template <typename Kernel>
class Arr_extended_direction_3 : public Kernel::Direction_3 {
public:
  using FT = typename Kernel::FT;
  using Direction_3 = typename Kernel::Direction_3;

  enum Location_type {
    NO_BOUNDARY_LOC = 0,   // interior of the parameter space
    MIN_BOUNDARY_LOC,      // = 1, south pole  (contraction, bottom)
    MID_BOUNDARY_LOC,      // = 2, on the vertical identification curve
    MAX_BOUNDARY_LOC       // = 3, north pole  (contraction, top)
  };
```

### Public members (exact signatures)

```cpp
  Arr_extended_direction_3();                                              // Direction_3(0,0,1), MAX_BOUNDARY_LOC
  Arr_extended_direction_3(const Direction_3& dir, Location_type location);
  Arr_extended_direction_3(const Arr_extended_direction_3& other);
  Arr_extended_direction_3& operator=(const Arr_extended_direction_3& other);

  void          set_location(Location_type location);
  Location_type location() const;
  Location_type discontinuity_type() const;   // \todo deprecate this one; use location()

  bool is_no_boundary()  const;   // location() == NO_BOUNDARY_LOC
  bool is_min_boundary() const;   // location() == MIN_BOUNDARY_LOC  (south pole)
  bool is_mid_boundary() const;   // location() == MID_BOUNDARY_LOC  (identification)
  bool is_max_boundary() const;   // location() == MAX_BOUNDARY_LOC  (north pole)
```

Notes for a binding:

* **No normalization, ever.** The class stores whatever `Direction_3` it was handed. Equality/ordering is
  direction equality, i.e. positive scaling is irrelevant, but `dx()/dy()/dz()` return the raw numbers.
* **The 2-arg ctor does not compute `location`; it trusts you.** Building points with the raw ctor and a
  wrong `Location_type` silently corrupts the arrangement. Always go through `Construct_point_2`
  (which calls `init(...)` and derives the location from the coordinates and from `atan_x/atan_y`).
* Value type, no ownership issues; copy is cheap-ish (three `FT`s + enum). Stored *by value* inside
  arcs and inside DCEL vertices.
* There is no `operator<<`/`>>` inside the traits (that block is `#if 0`), but free
  `operator<<`/`operator>>` for `Arr_extended_direction_3<Kernel>` and
  `Arr_x_monotone_geodesic_arc_on_sphere_3<Kernel>` exist at the bottom of the header
  (lines 3701–3777). The text format for a point is `dx dy dz location`.

---

## 2. `Arr_x_monotone_geodesic_arc_on_sphere_3<Kernel_>` (the `X_monotone_curve_2`)

Header lines 3064–3470. **Templated on the kernel only — it does not know `atan_x/atan_y`.**

State (protected): `m_source`, `m_target` (both `Arr_extended_direction_3`), `m_normal` (`Direction_3`,
the normal of the plane containing the arc — the arc runs CCW around it from source to target),
and the flags `m_is_vertical`, `m_is_directed_right`, `m_is_full`, `m_is_degenerate`, `m_is_empty`.

### Public typedefs

```cpp
  using Kernel      = Kernel_;
  using Direction_3 = typename Kernel::Direction_3;
  using Plane_3     = typename Kernel::Plane_3;
  using Vector_3    = typename Kernel::Vector_3;
  using Direction_2 = typename Kernel::Direction_2;
```

### Constructors

```cpp
  /*! constructs default; constructs an empty arc */
  Arr_x_monotone_geodesic_arc_on_sphere_3();          // is_empty()==true, all other flags false

  /*! \pre Both endpoints lie on the given plane. */
  Arr_x_monotone_geodesic_arc_on_sphere_3
  (const Arr_extended_direction_3& src,
   const Arr_extended_direction_3& trg,
   const Direction_3& normal,
   bool is_vertical, bool is_directed_right,
   bool is_full = false, bool is_degenerate = false, bool is_empty = false);

  Arr_x_monotone_geodesic_arc_on_sphere_3
  (const Arr_x_monotone_geodesic_arc_on_sphere_3& other);
  Arr_x_monotone_geodesic_arc_on_sphere_3& operator=
  (const Arr_x_monotone_geodesic_arc_on_sphere_3& other);

  /*! constructs a full spherical_arc from a plane
   * \pre the plane is not vertical                     */
  Arr_x_monotone_geodesic_arc_on_sphere_3(const Direction_3& normal);

  /*! constructs a full spherical_arc from a common endpoint and a plane
   * \pre the point lies on the plane
   * \pre the point lies on the open discontinuity arc  */
  Arr_x_monotone_geodesic_arc_on_sphere_3(const Arr_extended_direction_3& point,
                                          const Direction_3& normal);

  /*! constructs a spherical_arc from two endpoints directions contained in a plane.
   * \pre Both endpoints lie on the given plane.        */
  Arr_x_monotone_geodesic_arc_on_sphere_3
  (const Arr_extended_direction_3& source,
   const Arr_extended_direction_3& target,
   const Direction_3& normal);
```

* The `(normal)` full-circle ctor compiles **[verified]** but takes the branch
  `#if (CGAL_IDENTIFICATION_XY == CGAL_X_MINUS_1_Y_0)` — both macros are *undefined*, so in `#if` they
  are both `0` and the condition is **true**: the split point is hard-coded to the `x<0` identification
  (`d = sign(dz)>0 ? (-dz,0,dx) : (dz,0,-dx)`), regardless of `atan_x/atan_y`. Producing a *full*
  x-monotone arc is unsupported downstream anyway; do not use.
* The `(point, normal)` ctor unconditionally executes `CGAL_error_msg("Full x-monotone arcs are not supported!")`
  (the `#if !defined(CGAL_FULL_X_MONOTONE_...)` guard is active) → runtime abort.
* The 3-arg `(source, target, normal)` ctor is the one used by `Construct_x_monotone_curve_2`'s 3-arg
  overload; it has `CGAL_precondition(has_on(source)); CGAL_precondition(has_on(target));` and derives
  `is_vertical`/`is_directed_right` itself, sets `is_full=false`.

### Public member functions

```cpp
  void init();                                 // *** DOES NOT COMPILE — do not bind ***

  void set_source(const Arr_extended_direction_3& p);
  void set_target(const Arr_extended_direction_3& p);
  void set_normal(const Direction_3& normal);
  void set_is_vertical(bool flag);
  void set_is_directed_right(bool flag);
  void set_is_full(bool flag);
  void set_is_degenerate(bool flag);
  void set_is_empty(bool flag);

  const Arr_extended_direction_3& source() const;
  const Arr_extended_direction_3& target() const;
  const Direction_3&              normal() const;
  const Arr_extended_direction_3& left()  const;   // m_is_directed_right ? m_source : m_target
  const Arr_extended_direction_3& right() const;   // m_is_directed_right ? m_target : m_source

  bool is_vertical() const;
  bool is_directed_right() const;
  bool is_full() const;
  bool is_degenerate() const;
  bool is_empty() const;
  bool is_meridian() const;    // left().is_min_boundary() && right().is_max_boundary()

  Arr_x_monotone_geodesic_arc_on_sphere_3 opposite() const;   // swaps source/target, flips is_directed_right
  inline bool has_on(const Direction_3& dir) const;           // normal().vector()*dir.vector() == 0
```

`left()`/`right()` return **references into the arc**; they dangle if the arc dies. `bbox()` is `#if 0`
(not available). There is no `approximate()` member on the arc — approximation lives in the traits'
`Approximate_2`.

Semantics recap: "vertical" == lies on a meridian (a great circle through the poles);
`is_meridian()` == a *full* pole-to-pole vertical arc. `is_directed_right()` == target is
lexicographically larger than source in the (longitude, latitude) parameterization.

---

## 3. `Arr_geodesic_arc_on_sphere_3<Kernel_>` (the `Curve_2`)

Header lines 3487–3695. `class Arr_geodesic_arc_on_sphere_3 : public Arr_x_monotone_geodesic_arc_on_sphere_3<Kernel_>`
— it adds exactly one bit, `m_is_x_monotone`.

```cpp
  using Kernel = Kernel_;
  using Plane_3 = typename Base::Plane_3;
  using Direction_3 = typename Base::Direction_3;
  using Direction_2 = typename Base::Direction_2;

  /*! constructs default; constructs an empty arc */
  Arr_geodesic_arc_on_sphere_3();                       // Base() + m_is_x_monotone = true

  Arr_geodesic_arc_on_sphere_3(const Arr_geodesic_arc_on_sphere_3& other);   // (compiler-generated; only
                                                                            //  declared under DOXYGEN_RUNNING)

  /*! \pre plane contains the origin / src / trg */
  Arr_geodesic_arc_on_sphere_3(const Arr_extended_direction_3& src,
                               const Arr_extended_direction_3& trg,
                               const Direction_3& normal,
                               bool is_x_monotone, bool is_vertical,
                               bool is_directed_right,
                               bool is_full = false,
                               bool is_degenerate = false,
                               bool is_empty = false);

  /*! \pre Both endpoints lie on the given plane. */
  Arr_geodesic_arc_on_sphere_3(const Arr_extended_direction_3& source,
                               const Arr_extended_direction_3& target,
                               const Direction_3& normal);   // computes is_vertical / is_directed_right /
                                                             // is_x_monotone from the geometry

  /*! constructs a full spherical_arc from a normal to a plane. */
  Arr_geodesic_arc_on_sphere_3(const Direction_3& normal);   // *** DOES NOT COMPILE — do not bind ***

  bool is_x_monotone() const;
  void set_is_x_monotone(bool flag);
```

Because `Curve_2` derives from `X_monotone_curve_2`, everything in §2 is also available on a `Curve_2`,
and an `X_monotone_curve_2&` can bind to a `Curve_2` (this is exactly what `Make_x_monotone_2` exploits:
`const X_monotone_curve_2* xc = &c;`).

---

## 4. `Arr_geodesic_arc_on_sphere_traits_2<Kernel_, atan_x, atan_y>`

```cpp
template <typename Kernel_, int atan_x = -1, int atan_y = 0>
class Arr_geodesic_arc_on_sphere_traits_2 : public Kernel_ { ... };
```

### 4.1 Template parameters — what `atan_x`, `atan_y` mean

The sphere is parameterized by (longitude, latitude). The **identification curve** ("line of
discontinuity") is the meridian where longitude wraps around. `(atan_x, atan_y)` is the *2D direction in
the xy-plane* that this meridian projects onto:

```cpp
  /*! obtains the intersection of the identification arc and the xy-plane.
   * By default, it is the vector directed along the negative x-axis (x = -inf). */
  inline static const Direction_2& identification_xy()      { static const Direction_2 d(atan_x, atan_y); return d; }

  /*! obtains the normal of the plane that contains the identification arc.
   * By default, it is the vector directed along the positive y-axis (y = inf). */
  inline static const Direction_3& identification_normal()  { static const Direction_3 d(atan_y, -atan_x, 0); return d; }
```

So the **default `<-1, 0>` puts the identification at longitude 180°**, i.e. the half-plane `y == 0, x < 0`;
longitudes are compared CCW starting from that direction (`compare_x` uses
`counterclockwise_in_between_2_object()(identification_xy(), d1, d2)`). The two poles `(0,0,±1)` are the
*contraction* points.

`using Zero_atan_y = std::integral_constant<bool, atan_y==0>;` selects the fast path: when `atan_y == 0`
(the default) the point-location test is a pure sign test on `dx`/`dy`; otherwise a `Direction_2` equality
against `identification_xy()` is used. Both paths are implemented, so non-default `<atan_x, atan_y>` works,
**but** `Arr_x_monotone_geodesic_arc_on_sphere_3` and `Arr_geodesic_arc_on_sphere_3` are templated on the
kernel only and their `Direction_3`-taking ctors assume `<-1,0>` (see §2). Practically: **stick to the
default `<-1,0>`** unless you have a strong reason.

### 4.2 Public typedefs

```cpp
  using Kernel = Kernel_;

  using Has_left_category         = Tag_true;
  using Has_merge_category        = Tag_true;
  using Has_do_intersect_category = Tag_false;

  using Left_side_category   = Arr_identified_side_tag;   // vertical identification
  using Bottom_side_category = Arr_contracted_side_tag;   // south pole
  using Top_side_category    = Arr_contracted_side_tag;   // north pole
  using Right_side_category  = Arr_identified_side_tag;

  using Zero_atan_y = std::integral_constant<bool, atan_y==0>;

  using Point_2            = Arr_extended_direction_3<Kernel>;
  using X_monotone_curve_2 = Arr_x_monotone_geodesic_arc_on_sphere_3<Kernel>;
  using Curve_2            = Arr_geodesic_arc_on_sphere_3<Kernel>;
  using Multiplicity       = std::size_t;

  using FT          = typename Kernel::FT;
  using Direction_3 = typename Kernel::Direction_3;
  using Vector_3    = typename Kernel::Vector_3;
  using Direction_2 = typename Kernel::Direction_2;
  using Vector_2    = typename Kernel::Vector_2;

  // for the landmarks point-location strategy:
  using Approximate_number_type      = double;
  using Approximate_kernel           = CGAL::Cartesian<Approximate_number_type>;
  using Approximate_point_2          = Arr_extended_direction_3<Approximate_kernel>;
  using Approximate_kernel_vector_3  = Approximate_kernel::Vector_3;
  using Approximate_kernel_direction_3 = Approximate_kernel::Direction_3;

  Arr_geodesic_arc_on_sphere_traits_2() {}      // the only ctor; stateless
```

The traits **derives from the kernel**, so a traits object is also a kernel object: `traits.equal_3_object()`,
`traits.construct_cross_product_vector_3_object()`, … are all available. It is stateless and cheap to copy,
but the arrangement stores a *pointer* to it (see §7 lifetime notes).

### 4.3 Public non-functor helper methods (useful to bind directly)

```cpp
  inline Comparison_result compare_y (const Direction_3& d1, const Direction_3& d2) const;
  inline Comparison_result compare_x (const Direction_2& d1, const Direction_2& d2) const;
  inline Comparison_result compare_x (const Direction_3& d1, const Direction_3& d2) const;  // \pre neither is a pole
  inline Comparison_result compare_xy(const Direction_3& d1, const Direction_3& d2) const;  // \pre neither on the identification
  bool is_in_x_range(const X_monotone_curve_2& xcv, const Point_2& point) const;            // \pre point is not a pole

  void intersection_with_identification(const X_monotone_curve_2& xcv, Direction_3& dp, std::true_type)  const;
  void intersection_with_identification(const X_monotone_curve_2& xcv, Direction_3& dp, std::false_type) const;
  bool overlap_with_identification(const X_monotone_curve_2& xcv, std::true_type)  const;
  bool overlap_with_identification(const X_monotone_curve_2& xcv, std::false_type) const;
```

Pass `Zero_atan_y()` as the tag argument to the last four.

**Protected** (not reachable from a binding unless you derive): `identification_xy()`,
`identification_normal()`, `neg_x_2()`, `neg_y_2()`, `x_sign/y_sign/z_sign(Direction_3)`,
`project_xy/project_yz/project_xz/project_minus_yz/project_minus_xz(const Direction_3&)`,
`oriented_side(normal, dir)`, `orientation(Direction_2, Direction_2)`, `construct_normal_3(d1, d2)`,
`has_on(normal, dir)`, and — importantly — `pos_pole()` / `neg_pole()`.
To obtain the poles from a binding, use `construct_point_2_object()(0,0,1)` and `(0,0,-1)`.

### 4.4 Construction functors

All functors have a protected ctor taking `const Traits&` and are only obtainable via the
`*_object()` accessors; they hold `const Traits& m_traits`, so **a functor must not outlive its traits**.

#### `Construct_point_2` — `Construct_point_2 construct_point_2_object() const`

```cpp
    /*! constructs a point on the sphere from three coordinates, which define
     * a (not necessarily normalized) direction. */
    Point_2 operator()(const FT& x, const FT& y, const FT& z);        // NOT const

    /*! constructs a point on the sphere from a (not necessarily normalized) direction. */
    Point_2 operator()(const Direction_3& other);                     // NOT const
```

There is **no** overload taking a `Point_3` or a `Vector_3` (pass `v.direction()`).
Both overloads call the private `init(p, Zero_atan_y())`, which sets `Location_type`:
with the default identification, `dy()!=0` → `NO_BOUNDARY_LOC`; else `dx()>0` → `NO_BOUNDARY_LOC`,
`dx()<0` → `MID_BOUNDARY_LOC`, `dx()==0` → `dz()<0 ? MIN_BOUNDARY_LOC : MAX_BOUNDARY_LOC`.
(Note the `(0,0,0)` direction is not rejected; the kernel may assert.)

#### `Construct_x_monotone_curve_2` — `..._object() const`

```cpp
    /*! constructs the minor arc from two endpoint directions. The minor arc is
     * the one with the smaller angle among the two geodesic arcs with the given endpoints.
     * \pre the source and target must not coincide.
     * \pre the source and target cannot be antipodal. */
    X_monotone_curve_2 operator()(const Point_2& source, const Point_2& target) const;

    /*! constructs a full spherical_arc from a plane
     * \pre the plane is not vertical            */
    X_monotone_curve_2 operator()(const Direction_3& normal) const;   // *** MISSING return — UB ***

    /*! constructs a spherical_arc from two endpoints directions contained in a plane.
     * \pre Both endpoints lie on the given plane. */
    X_monotone_curve_2 operator()(const Point_2& source, const Point_2& target,
                                  const Direction_3& normal) const;
```

The 2-point overload computes `normal = source × target` and then classifies vertical / directed-right
(a pole endpoint ⇒ vertical). The 3-arg overload just forwards to `X_monotone_curve_2(source,target,normal)`
and is the only way to specify *which* of the two great-circle arcs you mean (e.g. an arc whose two
endpoints are the poles: use `normal = (0,+1,0)` for the arc through `(-1,0,0)` — that one *is* the
identification arc — or `(0,-1,0)` for the one through `(+1,0,0)`) **[verified]**.

#### `Construct_curve_2` — `..._object() const`

```cpp
    /*! \pre the source and target cannot be equal.
     * \pre the source and target cannot be the opoosite of each other. */
    Curve_2 operator()(const Point_2& source, const Point_2& target);                  // NOT const

    /*! \pre plane contain the origin
     * \pre Both endpoints lie on the given plane. */
    Curve_2 operator()(const Point_2& source, const Point_2& target,
                       const Direction_3& normal);                                     // NOT const

    /*! constructs a full spherical_arc from a plane. */
    Curve_2 operator()(const Direction_3& normal) const;
```

The `(normal)` overload is the **correct, working way to build a full great circle**: it sets
`is_full=true`, `is_x_monotone=false`, `is_vertical = (dz==0)`, `is_directed_right = (dz>0)`, and leaves
source/target default-constructed (they are irrelevant for a full curve; `Make_x_monotone_2` recomputes the
split points) **[verified]**.

### 4.5 Basic predicate/comparison functors

| functor | accessor | signatures / notes |
|---|---|---|
| `Compare_x_2` | `compare_x_2_object() const` | `Comparison_result operator()(const Point_2& p1, const Point_2& p2) const;` — `\pre p1/p2 do not lie on the boundary` (i.e. `is_no_boundary()`), compares longitude CCW from the identification |
| `Compare_xy_2` | `compare_xy_2_object() const` | `Comparison_result operator()(const Point_2& p1, const Point_2& p2) const;` — same precondition; longitude then latitude |
| `Construct_min_vertex_2` | `construct_min_vertex_2_object() const` | `const Point_2& operator()(const X_monotone_curve_2& xc) const;` → `xc.left()` (**returns a reference into the curve**) |
| `Construct_max_vertex_2` | `construct_max_vertex_2_object() const` | `const Point_2& operator()(const X_monotone_curve_2& xc) const;` → `xc.right()` |
| `Is_vertical_2` | `is_vertical_2_object() const` | `bool operator()(const X_monotone_curve_2& xc) const;` — `\pre the arc is not degenerate` |
| `Compare_y_at_x_2` | `compare_y_at_x_2_object() const` | `Comparison_result operator()(const Point_2& p, const X_monotone_curve_2& xc) const;` — `\pre p is not a contraction point`, `\pre p is in the x-range of xc` |
| `Compare_y_at_x_left_2` | `compare_y_at_x_left_2_object() const` | `Comparison_result operator()(const X_monotone_curve_2& xc1, const X_monotone_curve_2& xc2, const Point_2& p) const;` — `\pre p == xc1.right() == xc2.right()`, `\pre arcs not degenerate` |
| `Compare_y_at_x_right_2` | `compare_y_at_x_right_2_object() const` | same signature; `\pre p lies on both curves and both are defined to its right`, `\pre arcs not degenerate` |
| `Equal_2` | `equal_2_object() const` | `bool operator()(const X_monotone_curve_2& xc1, const X_monotone_curve_2& xc2) const;` and `bool operator()(const Point_2& p1, const Point_2& p2) const;` |
| `Clockwise_in_between_2` | `clockwise_in_between_2_object() const` | `bool operator()(const Direction_2& d, const Direction_2& d1, const Direction_2& d2) const;` (helper, takes 2D directions) |
| `Compare_endpoints_xy_2` | `compare_endpoints_xy_2_object() const` | `Comparison_result operator()(const X_monotone_curve_2& xc);` **non-const**, `SMALLER` iff `is_directed_right()` |
| `Construct_opposite_2` | `construct_opposite_2_object() const` | `X_monotone_curve_2 operator()(const X_monotone_curve_2& xc);` **non-const**, returns `xc.opposite()` |

`Equal_2` on curves: full circles compare by normal (up to opposite); meridians compare by normal;
otherwise by `left()`/`right()` equality. `Equal_2` on points is plain `Direction_3` equality.

### 4.6 Boundary functors (this is where the sphere shows up)

#### `Parameter_space_in_x_2` — `parameter_space_in_x_2_object() const`

```cpp
    /*! Only called for arcs whose interior lie in the interior of the parameter space, that is,
     * the arc does not coincide with the identification. ...
     * \pre xcv does not coincide with the identification */
    Arr_parameter_space operator()(const X_monotone_curve_2& xcv, Arr_curve_end ce) const;
    /*! \pre p.is_no_boundary() */
    Arr_parameter_space operator()(const Point_2& p) const;      // ALWAYS returns ARR_INTERIOR
```

Curve-end version: `ARR_INTERIOR` if the arc is vertical; else
`ce == ARR_MIN_END ? (xcv.left().is_no_boundary() ? ARR_INTERIOR : ARR_LEFT_BOUNDARY)
: (xcv.right().is_no_boundary() ? ARR_INTERIOR : ARR_RIGHT_BOUNDARY)`.
So: an arc that *starts* on the identification approaches it from the right → `ARR_LEFT_BOUNDARY`;
an arc that *ends* on it approaches from the left → `ARR_RIGHT_BOUNDARY` **[verified]**.

#### `Parameter_space_in_y_2` — `parameter_space_in_y_2_object() const` (stateless functor)

```cpp
    Arr_parameter_space operator()(const X_monotone_curve_2& xcv, Arr_curve_end ce) const;
    Arr_parameter_space operator()(const Point_2& p) const;
```
Curve-end: `ARR_BOTTOM_BOUNDARY` iff `ce==ARR_MIN_END && xcv.left().is_min_boundary()`,
`ARR_TOP_BOUNDARY` iff `ce==ARR_MAX_END && xcv.right().is_max_boundary()`, else `ARR_INTERIOR`.
Point: south pole → `ARR_BOTTOM_BOUNDARY`, north pole → `ARR_TOP_BOUNDARY`, else `ARR_INTERIOR`.
No precondition on the point overload.

`Arr_parameter_space` is `CGAL::Box_parameter_space_2` with
`LEFT_BOUNDARY=0, RIGHT_BOUNDARY=1, BOTTOM_BOUNDARY=2, TOP_BOUNDARY=3, INTERIOR=4, EXTERIOR=5`
(from `CGAL/enum.h`) — handy when marshalling to Python as ints.

#### `Compare_x_on_boundary_2` — `compare_x_on_boundary_2_object() const`

```cpp
    /*! \pre p lies in the interior of the parameter space.
     * \pre The ce end of the arc xcv lies on a pole (implying ce is vertical).
     * \pre xcv does not coincide with the vertical identification curve.     */
    Comparison_result operator()(const Point_2& point, const X_monotone_curve_2& xcv,
                                 Arr_curve_end ce) const;

    /*! \pre xcv1/xcv2 do not coincide with the vertical identification curve.
     * \pre the ce1/ce2 ends lie on a pole (implying vertical).                */
    Comparison_result operator()(const X_monotone_curve_2& xcv1, Arr_curve_end ce1,
                                 const X_monotone_curve_2& xcv2, Arr_curve_end ce2) const;

    /*! \todo This operator should be removed! ... */
    Comparison_result operator()(const Point_2&, const Point_2&) const;   // CGAL_error(); return EQUAL;
```

The third overload **aborts at runtime** (`CGAL_error()`), by design.

#### Others (list only — see the header for the full doc comments)

```cpp
  Compare_x_near_boundary_2 compare_x_near_boundary_2_object() const;
    Comparison_result operator()(const X_monotone_curve_2& xcv1,
                                 const X_monotone_curve_2& xcv2, Arr_curve_end ce) const;   // always EQUAL

  Compare_y_near_boundary_2 compare_y_near_boundary_2_object() const;
    Comparison_result operator()(const X_monotone_curve_2& xcv1,
                                 const X_monotone_curve_2& xcv2, Arr_curve_end ce) const;
    // \pre the ce ends lie on the left or on the right boundary; \pre the curves cannot reach a pole

  Compare_y_on_boundary_2 compare_y_on_boundary_2_object() const;
    Comparison_result operator()(const Point_2& p1, const Point_2& p2) const;
    // \pre p1/p2 lie on the vertical identification arc *including the poles*
    // south pole < anything < north pole; otherwise compare_y. (Only this signature exists —
    // there is deliberately no (ce1,pt2) / (ce1,ce2) form, unlike Compare_x_on_boundary_2.)
```

#### `Is_on_y_identification_2` — `is_on_y_identification_2_object() const`

```cpp
    /*! \return whether p lies on the vertical identification arc (including the poles) */
    bool operator()(const Point_2& p) const { return !p.is_no_boundary(); }

    /*! \return whether xcv coincides with the vertical identification arc */
    bool operator()(const X_monotone_curve_2& xcv) const;
```
The curve version: `false` unless *both* endpoints are on the boundary **and** the arc is vertical; then
`true` if either endpoint `is_mid_boundary()`; else (pole-to-pole) it checks
`overlap_with_identification(xcv, Zero_atan_y())`, i.e. for the default identification:
`x_sign(normal)==0 && ((y_sign(normal)<0 && !is_directed_right()) || (y_sign(normal)>0 && is_directed_right()))`.
**[verified]** a south→north arc with `normal=(0,1,0)` is on the identification; with `normal=(0,-1,0)` it is not.
There is **no** `Is_on_x_identification_2` in this traits (top/bottom are contracted, not identified);
the traits adaptor supplies a dummy one.

### 4.7 Intersection-support functors

#### `Make_x_monotone_2` — `make_x_monotone_2_object() const`

```cpp
    /*! subdivides a given curve into x-monotone subcurves and insert them into a given
     * output iterator. ...
     * \param oi ... Its dereference type is a variant that wraps a \c Point_2 or an
     *           \c X_monotone_curve_2 objects.
     * \return the past-the-end iterator. */
    template <typename OutputIterator>
    OutputIterator operator()(const Curve_2& c, OutputIterator oi) const;
```
Output value type: `std::variant<Point_2, X_monotone_curve_2>`.
**How many pieces you get** (this is the whole point of the identification):

| input `Curve_2` | # of output items |
|---|---|
| `c.is_degenerate()` | 1 — a `Point_2` (`c.right()`) |
| `c.is_x_monotone()` | 1 — the curve itself, re-wrapped as `X_monotone_curve_2` |
| full **vertical** great circle (`is_full && is_vertical`) | **2** meridians, split at both poles |
| full **non-vertical** great circle | **2** arcs, split at the identification point *and* at its antipode (`p1=(-dz,0,dx)`, `p2=(dz,0,-dx)`), because full x-monotone arcs are disabled **[verified]** |
| non-full vertical arc with a pole endpoint | 2 (split at the opposite pole) |
| non-full vertical arc, no pole endpoint, endpoints in the same halfspace | **3** (pole1, pole2) |
| non-full vertical arc, endpoints in different halfspaces | 2 (split at one pole) |
| non-full non-vertical arc crossing the identification | **2**, split at `intersection_with_identification(...)`, the split point carrying `MID_BOUNDARY_LOC` |

**[verified]** `make_x_monotone(full equator, normal=(0,0,1))` → 2 pieces whose shared endpoints have
`location()==MID_BOUNDARY_LOC` (at `(-1,0,0)`) and `NO_BOUNDARY_LOC` (at `(1,0,0)`).

#### `Split_2` — `split_2_object() const`

```cpp
    /*! \pre p lies on xc but is not one of its endpoints.
     * \pre xc is not degenerate */
    void operator()(const X_monotone_curve_2& xc, const Point_2& p,
                    X_monotone_curve_2& xc1, X_monotone_curve_2& xc2) const;
```
`xc1` is the left part (`p` is its right endpoint), `xc2` the right part.

#### `Intersect_2` — `intersect_2_object() const`

```cpp
    /*! finds the intersections of the two given curves and insert them into the given output
     * iterator. As two spherical_arcs may itersect only once, only a single intersection will
     * be contained in the iterator.
     * \pre xc1 and xc2 are not degenerate */
    template<typename OutputIterator>
    OutputIterator operator()(const X_monotone_curve_2& xc1, const X_monotone_curve_2& xc2,
                              OutputIterator oi) const;
```
Output value type: `std::variant<std::pair<Point_2, Multiplicity>, X_monotone_curve_2>` with
`Multiplicity = std::size_t`; a transversal crossing yields multiplicity `1`, an overlap yields an
`X_monotone_curve_2`. Despite the comment, **two arcs on the same great circle can produce two point
results** (both poles), and two arcs on different great circles are tested at both antipodal
cross-product points, so up to 2 items may be written. **[verified]** the returned point is *not*
normalized (e.g. `(2,2,0)`).

#### `Are_mergeable_2` / `Merge_2`

```cpp
  Are_mergeable_2 are_mergeable_2_object() const;
    /*! Two arcs are mergeable if:
     * 1. they are supported by the same plane, and
     * 2. share a common endpoint that is not on the identification arc */
    bool operator()(const X_monotone_curve_2& xc1, const X_monotone_curve_2& xc2) const;

  Merge_2 merge_2_object() const;
    /*! \pre the two curves are mergeable. */
    void operator()(const X_monotone_curve_2& xc1, const X_monotone_curve_2& xc2,
                    X_monotone_curve_2& xc) const;
```
Note: two arcs that share *both* endpoints are **not** mergeable (would make a full x-monotone arc, disabled).

### 4.8 `Approximate_2` — `Approximate_2 approximate_2_object() const` (yes, it exists)

```cpp
    /*! returns an approximation of a point coordinate.
     * \param i the coordinate index (either 0 or 1).
     * \pre `i` is either 0 or 1. */
    Approximate_number_type operator()(const Point_2& p, int i) const;
      // CGAL_precondition((i == 0) || (i == 1) || (i == 2));  <-- actually accepts 2 -> dz()

    /*! obtains an approximation of a point. */
    Approximate_point_2 operator()(const Point_2& p) const;

    /*! obtains an approximation of an x-monotone curve. */
    template <typename OutputIterator>
    OutputIterator operator()(const X_monotone_curve_2& xcv,
                              Approximate_number_type error,
                              OutputIterator oi, bool l2r = true) const;
```

* `Approximate_number_type = double`, `Approximate_point_2 = Arr_extended_direction_3<CGAL::Cartesian<double>>`
  — i.e. approximated points are still 3D directions (with the `Location_type` copied from the exact point
  in the 1-arg overload), **not** 2D points. The doc comment about "x-coordinate / y-coordinate" is stale:
  index `0/1/2` map to `dx/dy/dz`.
* The curve overload emits a polyline of **unit-length** `Approximate_point_2` on the sphere from the arc's
  source to its target (or reversed when `l2r != xcv.is_directed_right()`), with all emitted points tagged
  `NO_BOUNDARY_LOC`. Sampling: `dtheta = 2*acos(1 - error/1.0)`, `num_segs = ceil(theta/dtheta)`, then
  `num_segs+1` points. A full circle uses `theta = 2π`. **[verified]** a 90° arc with `error = 0.01`
  produced 7 points, first `(1,0,0)`, last `(0,1,0)`.
* This is the functor to use for rendering/tessellation in a Python binding; it is also what makes
  `CGAL::Arr_landmarks_point_location<Arr>` compile with this traits **[verified]**.

### 4.9 Functors the traits does NOT define

`Do_intersect_2` (`Has_do_intersect_category = Tag_false`), `Is_on_x_identification_2`,
`Compare_x_point_curve_end_2`, `Compare_x_curve_ends_2`, `Compare_y_curve_ends_2`,
`Construct_vertex_at_curve_end_2`, `Is_closed_2`, `Is_in_x_range_2`, `Compare_y_position_2`,
`Is_between_cw_2`, `Compare_cw_around_point_2`. All of these are synthesized by
`CGAL::Arr_traits_basic_adaptor_2<Gt>` / `Arr_traits_adaptor_2<Gt>`
(`CGAL/Arrangement_2/Arr_traits_adaptor_2.h`), which is what the topology traits and the sweep actually
call (`arr.traits_adaptor()` gives you one). If you want e.g. `compare_x_point_curve_end_2`, go through the
adaptor, not the traits.

---

## 5. `Arr_geodesic_arc_on_sphere_partition_traits_2<T_Kernel, Container_P>`

```cpp
template <class T_Kernel,
          class Container_P = std::vector<typename Arr_geodesic_arc_on_sphere_traits_2<T_Kernel>::Point_2> >
class Arr_geodesic_arc_on_sphere_partition_traits_2
  : public Arr_geodesic_arc_on_sphere_traits_2<T_Kernel>
```

Models `YMonotonePartitionTraits_2` (really `XMonotonePartitionTraits_2` — "PAY ATTENTION TO THE FACT THAT
WE REVERSE THE ROLES OF X AND Y"). Only valid for polygons contained in a hemisphere that do not
intersect a boundary. Note it fixes `atan_x/atan_y` to the defaults.

```cpp
  typedef typename Base::Point_2 Point_2;

  class Polygon_2 : public Container_P {
  public:
    typedef typename Container_P::const_iterator Vertex_const_iterator;
    Vertex_const_iterator vertices_begin() const;
    Vertex_const_iterator vertices_end()   const;
  };

  class Less_xy_2 { bool operator()(const Point_2& p1, const Point_2& p2) const; };  // \pre no boundary
  Less_xy_2 less_xy_2_object() const;
  class Less_yx_2 { bool operator()(const Point_2& p1, const Point_2& p2) const; };  // \pre no boundary
  Less_yx_2 less_yx_2_object() const;

  class Orientation_2 {
    /*! \pre p,q,r do not lie on the boundary; \pre p and q are not antipodal */
    CGAL::Orientation operator()(const Point_2& p, const Point_2& q, const Point_2& r) const;
  };                                            // sign of (p × q) · r
  Orientation_2 orientation_2_object() const;

  class Left_turn_2 { bool operator()(const Point_2& p, const Point_2& q, const Point_2& r) const; };
  Left_turn_2 left_turn_2_object() const;

  typedef typename Base::Compare_x_2 Compare_y_2;                 // roles swapped on purpose
  Compare_y_2 compare_y_2_object() const { return Base::compare_x_2_object(); }

  class Compare_x_2 { Comparison_result operator()(const Point_2& p1, const Point_2& p2) const; };
  Compare_x_2 compare_x_2_object() const;                          // calls m_traits->compare_y(p1,p2)

  struct Is_convex_2 { template <typename T> Is_convex_2(T) {}
                       template<class InputIterator> bool operator()(InputIterator, InputIterator) const; };
  Is_convex_2 is_convex_2_object() const;    // *** DOES NOT COMPILE (see gotcha #2) ***
  struct Is_valid   { ... };                 // `is_valid_object()` is commented out in the header
```

Both `Is_convex_2` and `Is_valid` are stubs that always return `true`.

---

## 6. `Arr_spherical_topology_traits_2<GeometryTraits_2, Dcel_>`

```cpp
template <typename GeometryTraits_2,
          typename Dcel_ = Arr_default_dcel<GeometryTraits_2> >
class Arr_spherical_topology_traits_2 { ... };
```

### 6.1 Public typedefs and the static asserts

```cpp
  typedef GeometryTraits_2  Geometry_traits_2;
  typedef Dcel_             Dcel;
  typedef typename Gt2::Point_2             Point_2;
  typedef typename Gt2::X_monotone_curve_2  X_monotone_curve_2;

  typedef typename Dcel::Size  Size;   typedef typename Dcel::Vertex   Vertex;
  typedef typename Dcel::Halfedge Halfedge;  typedef typename Dcel::Face Face;
  typedef typename Dcel::Outer_ccb Outer_ccb; typedef typename Dcel::Inner_ccb Inner_ccb;
  typedef typename Dcel::Isolated_vertex Isolated_vertex;

  typedef Arr_spherical_topology_traits_2<Gt2, Dcel> Self;
  typedef Arr_traits_basic_adaptor_2<Gt2>            Gt_adaptor_2;

  typedef typename Gt_adaptor_2::Left_side_category   Left_side_category;    // must be oblivious or identified
  typedef typename Gt_adaptor_2::Bottom_side_category Bottom_side_category;  // must be oblivious or contracted
  typedef typename Gt_adaptor_2::Top_side_category    Top_side_category;     // must be oblivious or contracted
  typedef typename Gt_adaptor_2::Right_side_category  Right_side_category;   // must be oblivious or identified

  template <typename T, typename D>
  struct rebind { typedef Arr_spherical_topology_traits_2<T, D> other; };

  typedef Arr_inc_insertion_zone_visitor<Arr>  Zone_insertion_visitor;
  typedef Arr_naive_point_location<Arr>        Default_point_location_strategy;
  typedef Arr_naive_point_location<Arr>        Default_vertical_ray_shooting_strategy;
```
The four `static_assert`s reject any geometry traits whose side categories are not
oblivious/identified (left,right) and oblivious/contracted (bottom,top). **`rebind<T,D>` takes two
parameters** (traits + dcel) — this is what `Arrangement_on_surface_with_history_2` uses.

### 6.2 Construction / lifetime

```cpp
  Arr_spherical_topology_traits_2();                        // allocates its own Gt_adaptor_2 (m_own_geom_traits=true)
  Arr_spherical_topology_traits_2(const Gt2* traits);       // borrows; does NOT take ownership
  ~Arr_spherical_topology_traits_2();                       // m_dcel.delete_all(); deletes the traits iff it owns it
  void assign(const Self& other);
  // copy ctor and operator= are declared private and NOT defined
```
`Arr_spherical_topology_traits_2(const Gt2*)` `static_cast`s the geometry traits pointer to
`const Gt_adaptor_2*` — the adaptor derives from the traits and adds no state, so this is the usual CGAL
trick. **Ownership**: when an `Arrangement_on_surface_2` is built from `const Geometry_traits_2*`, neither
the arrangement nor the topology traits owns that object — the caller must keep the traits alive for the
whole life of the arrangement. This matters a lot for a Cython wrapper: keep the traits in the same
Python object as the arrangement.

### 6.3 Topology-traits methods

```cpp
  const Dcel& dcel() const;         Dcel& dcel();
  bool is_empty_dcel() const;                       // m_dcel.size_of_vertices() == 0
  void init_dcel();                                 // clears, then creates ONE face
  void dcel_updated();                              // recomputes poles/boundary map/spherical face

  bool is_concrete_vertex(const Vertex*) const;     // always true — no fictitious vertices
  Size number_of_concrete_vertices() const;         // = size_of_vertices()
  bool is_valid_vertex(const Vertex*) const;        // always true
  Size number_of_valid_vertices() const;
  bool is_valid_halfedge(const Halfedge*) const;    // always true
  Size number_of_valid_halfedges() const;
  bool is_valid_face(const Face*) const;            // always true
  Size number_of_valid_faces() const;

  const Face* spherical_face() const;   Face* spherical_face();
  const Face* south_face()     const;   Face* south_face();
  const Vertex* south_pole()   const;   Vertex* south_pole();
  const Vertex* north_pole()   const;   Vertex* north_pole();

  Vertex*       discontinuity_vertex(const Point_2& pt);            // nullptr if none
  const Vertex* discontinuity_vertex(const Point_2& pt) const;
  Vertex*       discontinuity_vertex(const X_monotone_curve_2& xc, Arr_curve_end ind);   // TODO: to be removed

  void notify_on_boundary_vertex_creation(Vertex* v, const Point_2& p,
                                          Arr_parameter_space ps_x, Arr_parameter_space ps_y);
  void notify_on_boundary_vertex_creation(Vertex* v, const X_monotone_curve_2& xc, Arr_curve_end ind,
                                          Arr_parameter_space ps_x, Arr_parameter_space ps_y);

  bool let_me_decide_the_outer_ccb(std::pair<CGAL::Sign, CGAL::Sign> signs1,
                                   std::pair<CGAL::Sign, CGAL::Sign> signs2,
                                   bool& swap_predecessors) const;
  std::pair<bool, bool> face_split_after_edge_insertion(std::pair<CGAL::Sign,CGAL::Sign>,
                                                        std::pair<CGAL::Sign,CGAL::Sign>) const;
                                                        // always (true, true)

  bool is_in_face(const Face* f, const Point_2& p, const Vertex* v) const;
  Comparison_result compare_y_at_x(const Point_2& p, const Halfedge* he) const;
  bool are_equal(const Vertex* v, const X_monotone_curve_2& xc, Arr_curve_end ind,
                 Arr_parameter_space ps_x, Arr_parameter_space ps_y) const;

  std::optional<std::variant<Vertex*, Halfedge*> >
  place_boundary_vertex(Face* f, const X_monotone_curve_2& xc, Arr_curve_end ind,
                        Arr_parameter_space ps_x, Arr_parameter_space ps_y);

  Halfedge* locate_around_boundary_vertex(Vertex* v, const X_monotone_curve_2& cv, Arr_curve_end ind,
                                          Arr_parameter_space ps_x, Arr_parameter_space ps_y) const;

  std::variant<Vertex*, Halfedge*, Face*>
  locate_curve_end(const X_monotone_curve_2& xc, Arr_curve_end ce,
                   Arr_parameter_space ps_x, Arr_parameter_space ps_y);

  Halfedge* split_fictitious_edge(Halfedge*, Vertex*);   // CGAL_error(); return nullptr;  (never called)
  bool is_unbounded(const Face*) const { return false; } // "All faces on a sphere are bounded"
  bool is_redundant(const Vertex* v) const;              // v->halfedge() == nullptr
  Halfedge* erase_redundant_vertex(Vertex* v);           // unregisters pole / boundary vertex; returns nullptr
  const Face* reference_face() const;  Face* reference_face();   // == spherical_face()
```
Protected helpers (`_curve`, `_locate_around_vertex_on_discontinuity`, `_locate_around_pole`,
`_face_below_vertex_on_discontinuity`) are internal.

### 6.4 Faces, "unbounded", and the initial state

* `init_dcel()` creates **exactly one face**, `m_spherical_face`, with `set_unbounded(false)` and
  `set_fictitious(false)`, and sets `m_north_pole = m_south_pole = nullptr`.
  So a brand-new arrangement has **V=0, E=0, F=1** **[verified]**.
* `dcel_updated()` re-derives everything after an external DCEL edit: it scans vertices
  (`parameter_space_in_y()==ARR_BOTTOM/TOP_BOUNDARY` → south/north pole; else
  `parameter_space_in_x()!=ARR_INTERIOR` → insert into the boundary-vertex map) and then finds
  **the unique face with `number_of_outer_ccbs()==0`** — that is the spherical face
  (`CGAL_assertion(m_spherical_face != nullptr)`).
* `is_in_face()` short-circuits: *"If the face has no outer ccb's, it contains everything"*, and it makes
  sure the everything-containing face contains the **north pole**. **[verified]** by point location: for a
  small triangle, for a perimetric cycle in the northern hemisphere, and for a band between two perimetric
  cycles, the north pole is always in the face with zero outer CCBs.
* `south_face()` is *not* symmetric to `spherical_face()`: if there are no boundary vertices it returns the
  spherical face, otherwise `_face_below_vertex_on_discontinuity(first boundary vertex)`.

### 6.5 Vertices on the identification curve — one or two?

**One.** `place_boundary_vertex()` looks the curve end up in `m_boundary_vertices`
(`std::map<Point_2, Vertex*, Vertex_key_comparer>`, ordered by `compare_y_on_boundary_2`) and returns the
existing vertex for *both* the `ARR_LEFT_BOUNDARY` end of one arc and the `ARR_RIGHT_BOUNDARY` end of
another. `are_equal()` compares the vertex and the curve end with `compare_y_on_boundary_2 == EQUAL`.

**[verified]** inserting the full equator (`normal=(0,0,1)`) yields **V=2, E=2, F=2**: one vertex at
`(-1,0,0)` (`location()==MID_BOUNDARY_LOC`, `parameter_space_in_x()==ARR_LEFT_BOUNDARY(0)`,
`parameter_space_in_y()==ARR_INTERIOR(4)`, degree 2) and one at `(1,0,0)`
(`NO_BOUNDARY_LOC`, both parameter spaces `ARR_INTERIOR`).

The **poles are separate**: they are stored in `m_north_pole` / `m_south_pole`, not in the map, and are
returned by `place_boundary_vertex` whenever `ps_y` is `ARR_TOP/BOTTOM_BOUNDARY`. A pole vertex has
`parameter_space_in_y() == ARR_TOP_BOUNDARY(3)` or `ARR_BOTTOM_BOUNDARY(2)`; its
`parameter_space_in_x()` is whatever the creating curve end reported — `ARR_INTERIOR(4)` for a triangle
touching the north pole, `ARR_LEFT_BOUNDARY(0)` for a full meridian circle **[verified]**.

### 6.6 Measured face structure **[verified]**

| configuration | V | E | F | per-face (outer, inner) |
|---|---|---|---|---|
| empty | 0 | 0 | 1 | spherical (0, 0) |
| triangle `(1,0,0)-(0,1,0)-(0,0,1)` | 3 | 3 | 2 | spherical (0, 1); triangle (1, 0) |
| full equator `normal=(0,0,1)` | 2 | 2 | 2 | spherical (0, 1); other (1, 0) |
| full meridian circle `normal=(0,1,0)` | 2 | 2 | 2 | spherical (0, 1); other (1, 0) |
| equator + meridian circle | 4 | 6 | 4 | spherical (0, 1); three lunes (1, 0) each |
| perimetric 4-arc cycle at z=+1 | 4 | 4 | 2 | spherical (0, 1) *contains N pole*; other (1, 0) *contains S pole* |
| that cycle + equator | 6 | 6 | 3 | spherical (0, 1); band (1, 1); south cap (1, 0) |
| cycles at z=+1 and z=−1 | 8 | 8 | 3 | spherical (0, 1); band (1, 1); south cap (1, 0) |
| single arc on the identification (S pole→N pole, `normal=(0,1,0)`) | 2 | 1 | 1 | spherical (0, 1) |

So in practice, with this topology traits, **every non-spherical face had exactly one outer CCB**, even for
perimetric cycles (the second boundary of a band is recorded as an *inner* CCB — `let_me_decide_the_outer_ccb`
prefers the non-perimetric loop as the outer CCB and returns `true` as soon as either loop is perimetric).
Still, write your traversal against `outer_ccbs_begin()/outer_ccbs_end()` (§7), because the spherical face
has zero and `Face::outer_ccb()` asserts on that.

---

## 7. What differs in `Arrangement_on_surface_2<GeomTraits, TopTraits>` for the sphere

`template <typename GeomTraits_, typename TopTraits_> class Arrangement_on_surface_2;`
Everything not listed here behaves as in the planar `Arrangement_2` (which, in CGAL 6.x, *is*
an `Arrangement_on_surface_2` with a bounded-planar topology traits).

### 7.1 Construction

```cpp
  Arrangement_on_surface_2();                                   // topology traits owns its own geometry traits
  Arrangement_on_surface_2(const Self& arr);
  Arrangement_on_surface_2(const Geometry_traits_2* geom_traits);  // borrows: NOT owned, must outlive arr
  Self& operator=(const Self& arr);
  void assign(const Self& arr);
  virtual ~Arrangement_on_surface_2();
  virtual void clear();                        // back to V=0, E=0, F=1  [verified]
  void set_sweep_mode(bool mode);

  inline const Traits_adaptor_2*   traits_adaptor()  const;
  inline const Geometry_traits_2*  geometry_traits() const;
  inline Topology_traits*          topology_traits();
  inline const Topology_traits*    topology_traits() const;
```

### 7.2 Faces: multiple outer CCBs, and faces without any

```cpp
  class Face : public DFace {
    Outer_ccb_iterator       outer_ccbs_begin();          Outer_ccb_const_iterator outer_ccbs_begin() const;
    Outer_ccb_iterator       outer_ccbs_end();            Outer_ccb_const_iterator outer_ccbs_end()   const;
    Inner_ccb_iterator       inner_ccbs_begin();          Inner_ccb_const_iterator inner_ccbs_begin() const;
    Inner_ccb_iterator       inner_ccbs_end();            Inner_ccb_const_iterator inner_ccbs_end()   const;
    Isolated_vertex_iterator isolated_vertices_begin();   Isolated_vertex_const_iterator isolated_vertices_begin() const;
    Isolated_vertex_iterator isolated_vertices_end();     Isolated_vertex_const_iterator isolated_vertices_end()   const;

    // kept for Arrangement_2 compatibility:
    bool has_outer_ccb() const;                      // number_of_outer_ccbs() > 0
    Ccb_halfedge_circulator       outer_ccb();       // \pre The face has a single outer CCB.
    Ccb_halfedge_const_circulator outer_ccb() const; // \pre number_of_outer_ccbs() == 1
    Size number_of_holes() const;                    // == number_of_inner_ccbs()
    Inner_ccb_iterator holes_begin();  Inner_ccb_iterator holes_end();   // aliases of inner_ccbs_*
  };
```
Inherited from `Arr_face_base` (`Arr_dcel_base.h`) and public on `Face`:
`std::size_t number_of_outer_ccbs() const`, `std::size_t number_of_inner_ccbs() const`,
`bool is_unbounded() const`, `bool is_fictitious() const`. (`set_unbounded`, `set_fictitious`,
`add_outer_ccb`, `erase_outer_ccb`, `add_inner_ccb`, `erase_inner_ccb`, `add_isolated_vertex`,
`erase_isolated_vertex` are re-declared **private** in `Face` to block them.)

`Outer_ccb_iterator` is `Iterator_transform<DOuter_ccb_iter, _Halfedge_to_ccb_circulator>` — dereferencing it
gives a `Ccb_halfedge_circulator`, so the idiom is:

```cpp
for (auto o = f->outer_ccbs_begin(); o != f->outer_ccbs_end(); ++o) {
  auto circ = *o, c = circ;
  do { /* c->curve(), c->target(), ... */ ++c; } while (c != circ);
}
```
**[verified]** working against the spherical arrangement.

For the sphere: `f->is_unbounded()` is always `false`; `f->is_fictitious()` is always `false`;
`f->has_outer_ccb()` is `false` **exactly for the spherical face**.

### 7.3 Face-level accessors on the arrangement

```cpp
  Size number_of_faces() const;                 // = topology_traits()->number_of_valid_faces()
  Size number_of_unbounded_faces() const;       // always 0 here
  Face_iterator faces_begin(); Face_iterator faces_end();  (+ const versions, + face_handles())
  Unbounded_face_iterator unbounded_faces_begin()/end();   (+ const)   // empty range here
  Face_const_handle reference_face() const;     Face_handle reference_face();   // == spherical face
  Face_handle fictitious_face();                // *** DOES NOT COMPILE for spherical topology ***
```
There is **no** `unbounded_face()` member on `Arrangement_on_surface_2` (that is `Arrangement_2`-only).
Use `reference_face()` or `topology_traits()->spherical_face()` (the latter returns a raw `DFace*`;
convert with `arr.non_const_handle(...)`-style helpers or just compare `&*fh == tt->spherical_face()`).

### 7.4 Vertices: boundary vertices, no fictitious vertices

```cpp
  class Vertex : public DVertex {
    bool is_at_open_boundary() const;      // == has_null_point(); always false on the sphere
    Size degree() const;
    Halfedge_around_vertex_circulator incident_halfedges();          // \pre !is_isolated()
    Face_handle face();                                              // \pre is_isolated()
    // inherited & public: const Point_2& point() const;  bool is_isolated() const;
    //                     Arr_parameter_space parameter_space_in_x() const;
    //                     Arr_parameter_space parameter_space_in_y() const;
  };
```
`has_null_point()`, `set_point()`, `set_boundary()`, `halfedge()`, `set_halfedge()`, `isolated_vertex()`,
`set_isolated_vertex()` are re-declared private in `Vertex`.

* **No fictitious vertices** (`is_concrete_vertex()` is hard-coded `true`;
  `Halfedge::is_fictitious()` == `has_null_curve()`, never true here).
* A boundary vertex is a normal, concrete vertex with a real `point()`; you tell it apart by
  `parameter_space_in_x()/parameter_space_in_y()` or by `v->point().location()`.
* `arr.number_of_vertices()` counts all of them (concrete == all).

### 7.5 Global functions that work unchanged

```cpp
  template <typename GeometryTraits_2, typename TopologyTraits, typename Curve>
  void insert(Arrangement_on_surface_2<GeometryTraits_2, TopologyTraits>& arr, const Curve& c);
  template <... , typename PointLocation, typename ZoneVisitor>
  void insert(..., const Curve& c, const PointLocation& pl, ZoneVisitor& visitor);
  // overloads dispatch on std::is_same<Curve, X_monotone_curve_2>

  template <...>
  typename Arrangement_on_surface_2<...>::Halfedge_handle
  insert_non_intersecting_curve(..., const typename GeometryTraits_2::X_monotone_curve_2& c,
                                const PointLocation& pl);

  template <...>
  typename Arrangement_on_surface_2<...>::Vertex_handle
  insert_point(Arrangement_on_surface_2<...>& arr, const typename GeometryTraits_2::Point_2& p);
```
(declared in `CGAL/Arrangement_2/Arrangement_on_surface_2_global.h`, pulled in by
`CGAL/Arrangement_on_surface_2.h`). The no-`pl` overloads instantiate
`TopologyTraits::Default_point_location_strategy`, i.e. `Arr_naive_point_location<Arr>`.
**[verified]** `CGAL::insert(arr, curve_or_xcurve)` and `CGAL::insert_point(arr, p)` work, including at a
pole (creates/reuses the pole vertex; `topology_traits()->north_pole()` becomes non-null) and on the
identification (registers a discontinuity vertex; `topology_traits()->discontinuity_vertex(p)` finds it).
`arr.is_valid()` returns `true` for all of the above **[verified]**.

`CGAL::Arr_landmarks_point_location<Arr>` compiles with this traits (it uses `Approximate_2`)
**[verified, compile only]**. `Arr_walk_along_line_point_location` / `Arr_trapezoid_ric_point_location`
were not tested and are planar-oriented; prefer `Arr_naive_point_location` or landmarks.

---

## 8. `Arr_extended_dcel.h` — the parts that matter on-surface

Nothing in this header is topology-specific; it composes cleanly with the spherical topology traits
**[verified]**.

```cpp
template <typename VertexBase, typename VertexData>
class Arr_extended_vertex : public VertexBase {
public:
  typedef VertexData Data;
  const Data& data() const;  Data& data();  void set_data(const Data& data);
  virtual void assign(const Vertex_base& v);
  template <typename Point_> struct rebind { using other = Arr_extended_vertex<...>; };
};
// ... Arr_extended_halfedge<HalfedgeBase, HalfedgeData> (rebind on XMonotoneCurve)
// ... Arr_extended_face<FaceBase, FaceData>            (no rebind — faces don't depend on the traits)

template <typename Traits_, typename FaceData,
          typename VertexBase   = Arr_vertex_base<typename Traits_::Point_2>,
          typename HalfedgeBase = Arr_halfedge_base<typename Traits_::X_monotone_curve_2>,
          typename FaceBase     = Arr_face_base>
class Arr_face_extended_dcel
  : public Arr_dcel_base<VertexBase, HalfedgeBase, Arr_extended_face<FaceBase, FaceData>> {
  using Face_base = FaceBase;  using Face_data = FaceData;
  template <typename T> class rebind { public: using other = Arr_face_extended_dcel<T, Face_data, ...>; };
  Arr_face_extended_dcel();  virtual ~Arr_face_extended_dcel();
};

template <typename Traits_, typename VertexData, typename HalfedgeData, typename FaceData,
          typename VertexBase = ..., typename HalfedgeBase = ..., typename FaceBase = Arr_face_base>
class Arr_extended_dcel
  : public Arr_dcel_base<Arr_extended_vertex<VertexBase, VertexData>,
                         Arr_extended_halfedge<HalfedgeBase, HalfedgeData>,
                         Arr_extended_face<FaceBase, FaceData>> {
  using Vertex_data = VertexData; using Halfedge_data = HalfedgeData; using Face_data = FaceData;
  using Vertex_base = VertexBase; using Halfedge_base = HalfedgeBase; using Face_base = FaceBase;
  template <typename T> struct rebind { using other = Arr_extended_dcel<T, ...>; };
  Arr_extended_dcel();  virtual ~Arr_extended_dcel();
};
```

Relevant on-surface points:

* The **single-parameter `rebind<T>`** on the DCEL is what
  `Arr_spherical_topology_traits_2::rebind<T,D>` and `Arrangement_on_surface_with_history_2` need.
* `data()` is reached through the arrangement's `Vertex`/`Halfedge`/`Face` classes (they derive from the
  DCEL types), e.g. `arr.faces_begin()->set_data(7); arr.faces_begin()->data();` **[verified]**.
* **The spherical face gets a `Face_data` too**, and it is the face you will most often want to mark
  ("outside"): remember it is the one with `number_of_outer_ccbs()==0`.
* `Data` is stored by value inside the DCEL record; the DCEL owns it. Handles stay valid across
  unrelated insertions (standard CGAL arrangement guarantee: only the features actually modified by an
  operation are invalidated), and `data()` returns a reference into the DCEL record — do not keep it
  across a `clear()`/`assign()`.

### `Arrangement_on_surface_with_history_2` with the spherical topology

```cpp
template <class GeomTraits_, class TopTraits_>
class Arrangement_on_surface_with_history_2
  : public Arrangement_on_surface_2<
      Arr_consolidated_curve_data_traits_2<GeomTraits_, typename GeomTraits_::Curve_2*>,
      typename TopTraits_::template rebind<
        Arr_consolidated_curve_data_traits_2<GeomTraits_, typename GeomTraits_::Curve_2*>,
        typename TopTraits_::Dcel::template rebind<
          Arr_consolidated_curve_data_traits_2<GeomTraits_, typename GeomTraits_::Curve_2*>>::other>::other>
```
**[verified]** this compiles *and runs* with

```cpp
using ExtDcel  = CGAL::Arr_extended_dcel<Gt, int, int, int>;
using TopolExt = CGAL::Arr_spherical_topology_traits_2<Gt, ExtDcel>;
using ArrHist  = CGAL::Arrangement_on_surface_with_history_2<Gt, TopolExt>;
ArrHist arrh(&traits);
auto ch = CGAL::insert(arrh, traits.construct_curve_2_object()(p1, p2));
arrh.number_of_curves(); arrh.number_of_induced_edges(ch);
```
Note the **second template argument is the topology traits, not the DCEL**, and the DCEL you put inside it
is rebound to the consolidated-curve-data traits. Also note that inside a `..._with_history_2`, the
effective `Point_2`/`X_monotone_curve_2` are the *data-traits* versions, so `Vertex::point()` returns a
`Arr_extended_direction_3<Kernel>` (unchanged — `Arr_consolidated_curve_data_traits_2` only wraps curves),
while `Halfedge::curve()` returns a curve carrying the originating-`Curve_2*` data set.

---

## 9. Minimal working recipe (all of this was compiled and run)

```cpp
#include <CGAL/Exact_predicates_exact_constructions_kernel.h>
#include <CGAL/Arr_geodesic_arc_on_sphere_traits_2.h>
#include <CGAL/Arr_spherical_topology_traits_2.h>
#include <CGAL/Arrangement_on_surface_2.h>

using Kernel = CGAL::Exact_predicates_exact_constructions_kernel;
using Gt     = CGAL::Arr_geodesic_arc_on_sphere_traits_2<Kernel>;   // <Kernel, -1, 0>
using Point  = Gt::Point_2;
using Curve  = Gt::Curve_2;
using Xcv    = Gt::X_monotone_curve_2;
using Topol  = CGAL::Arr_spherical_topology_traits_2<Gt>;           // Dcel = Arr_default_dcel<Gt>
using Arr    = CGAL::Arrangement_on_surface_2<Gt, Topol>;

Gt traits;                                   // must outlive `arr`
auto cp   = traits.construct_point_2_object();          // non-const auto!
auto ccv  = traits.construct_curve_2_object();
auto cxcv = traits.construct_x_monotone_curve_2_object();

Arr arr(&traits);
Point a = cp(1,0,0), b = cp(0,1,0), c = cp(0,0,1);
CGAL::insert(arr, cxcv(a,b));
CGAL::insert(arr, cxcv(b,c));
CGAL::insert(arr, cxcv(c,a));                // V=3 E=3 F=2
CGAL::insert(arr, ccv(Kernel::Direction_3(0,0,1)));   // full great circle (equator)

const auto* tt = arr.topology_traits();
for (auto f = arr.faces_begin(); f != arr.faces_end(); ++f) {
  bool spherical = (&*f == tt->spherical_face());       // <=> f->outer_ccbs_begin() == f->outer_ccbs_end()
  for (auto o = f->outer_ccbs_begin(); o != f->outer_ccbs_end(); ++o) { auto ci = *o, cc = ci;
    do { /* cc->curve(), cc->target()->point() */ ++cc; } while (cc != ci); }
  for (auto h = f->inner_ccbs_begin(); h != f->inner_ccbs_end(); ++h) { /* same */ }
}
```

### Checklist for the type-erased C++ core / Cython layer

1. Expose `Point_2` as `(dx, dy, dz, location)` **plus** a normalized `(x,y,z)` computed via
   `Approximate_2` — never assume the exact coordinates are unit length.
2. Build points **only** with `Construct_point_2`; build curves **only** with `Construct_curve_2` /
   `Construct_x_monotone_curve_2` (never with the raw curve ctors that take a `Direction_3` alone).
3. Model "which face is the outside" as `number_of_outer_ccbs() == 0`, not as `is_unbounded()`.
4. Iterate CCBs with `outer_ccbs_begin/end` + `inner_ccbs_begin/end`, never `outer_ccb()`.
5. Keep the `Gt` traits object owned by the same wrapper object as the arrangement
   (`Arrangement_on_surface_2(const Geometry_traits_2*)` does not take ownership).
6. When exposing `Make_x_monotone_2`, remember one input `Curve_2` can yield 1, 2, or 3 outputs,
   and the outputs are `std::variant<Point_2, X_monotone_curve_2>`.

---

# 10. Global algorithms on the spherical topology: overlay, zone, vertical decomposition, batched point location, removal, observers, with-history

This section closes gap #6. Everything below was **compiled and run** with

```
/usr/bin/clang++ -std=c++17 -O0 -DCGAL_USE_CORE -DCGAL_USE_GMP -DCGAL_USE_MPFR \
    -I/opt/homebrew/include -L/opt/homebrew/lib -lgmp -lmpfr
```

against `Kernel = CGAL::Exact_predicates_exact_constructions_kernel`,
`Gt = CGAL::Arr_geodesic_arc_on_sphere_traits_2<Kernel>` (i.e. `atan_x = -1`, `atan_y = 0`, so the
vertical identification curve is the meridian `dy == 0 && dx < 0`), and
`Topol = CGAL::Arr_spherical_topology_traits_2<Gt, Dcel>`.

Headers read for this section:

* `Arr_topology_traits/Arr_spherical_overlay_helper.h` (290 lines), `..._vert_decomp_helper.h` (168),
  `..._batched_pl_helper.h` (133), `..._construction_helper.h` (279), `..._insertion_helper.h` (271),
  `..._topology_traits_2_impl.h` (991)
* `Arr_overlay_2.h`, `Surface_sweep_2/Arr_overlay_ss_visitor.h`,
  `Surface_sweep_2/Arr_default_overlay_traits_base.h`, `Arr_default_overlay_traits.h`
* `Arr_vertical_decomposition_2.h`, `Surface_sweep_2/Arr_vert_decomp_ss_visitor.h`
* `Arr_batched_point_location.h`, `Surface_sweep_2/Arr_batched_pl_ss_visitor.h`
* `Arrangement_zone_2.h`, `Arrangement_2/Arrangement_zone_2_impl.h`, `Arrangement_2/Arr_compute_zone_visitor.h`
* `Arrangement_2/Arrangement_on_surface_2_global.h`, `Arrangement_2/Arrangement_on_surface_2_impl.h`
* `Arrangement_on_surface_with_history_2.h`, `Aos_observer.h`, `Arr_accessor.h`
* `No_intersection_surface_sweep_2.h` + `Surface_sweep_2/No_intersection_surface_sweep_2_impl.h`

---

## 10.0 Extra gotchas discovered here (read these first)

**G1. `CGAL::insert` of a curve that *lies on* the identification curve is order-dependent and can
abort.** Inserting an x-monotone arc whose left end is *on* the identification meridian into an
arrangement that already has features crossing that meridian fires

```
CGAL error: precondition violation!
Expression : p1.is_no_boundary()
File       : /opt/homebrew/include/CGAL/Arr_geodesic_arc_on_sphere_traits_2.h
Line       : 1096                      // Compare_xy_2::operator()
```
**[verified]** The stack is
`CGAL::insert -> Arrangement_zone_2::compute_zone -> _zone_in_face ->
_leftmost_intersection_with_face_boundary -> _leftmost_intersection -> _is_to_right ->
_is_to_right_impl(p, he, Arr_not_all_sides_oblivious_tag)` (`Arrangement_zone_2_impl.h:790`), which
handles the boundary conditions of `he->curve()` but *not* those of `p` and then calls
`compare_xy_2_object()(p, v_right->point())`. `Compare_xy_2` and `Compare_x_2` both declare
`\pre p1/p2 does not lie on the boundary`.
*Workarounds (both **[verified]**):* insert the on-identification curve **first**, or use the
**aggregate** `CGAL::insert(arr, first, last)` (sweep-based), which is order-independent and never hits this.

**G2. `CGAL::decompose` is unusable on the sphere as soon as *any* boundary vertex exists.**
`Arr_vert_decomp_ss_visitor::after_handle_event` calls
`m_traits->compare_x_2_object()(vh->point(), m_prev_vh->point())` unconditionally, so a single pole
vertex or a single identification vertex aborts with the `Compare_x_2` precondition
(`Arr_geodesic_arc_on_sphere_traits_2.h:1038/1039`) **[verified, minimal repro in §10.4]**.

**G3. Batched point location silently mis-answers a query at the *south* pole** (returns the
spherical face instead of the pole vertex), because `Arr_spherical_batched_pl_helper` only tracks the
*top* face and never the bottom one **[verified: 1 mismatch out of 12 queries vs `Arr_naive_point_location`]**.

**G4. `CGAL::remove_vertex` / `arr.remove_isolated_vertex()` on an *isolated* pole or identification
vertex leaves a dangling pointer in the topology traits and the next insertion segfaults.**
`Arrangement_on_surface_2::remove_isolated_vertex` deletes the DCEL vertex without ever calling
`m_topol_traits.is_redundant()/erase_redundant_vertex()`, so `north_pole()` / `south_pole()` /
`m_boundary_vertices` still point at freed memory **[verified: `V==0` but `north_pole() != nullptr`, and a
subsequent `CGAL::insert` of a north-pole curve crashes with SIGSEGV]**. Removal via
`remove_edge` is fine — that path *does* go through `_remove_vertex_if_redundant`.

**G5. `Tt::Default_point_location_strategy` for the sphere is `Arr_naive_point_location`, not walk.**
`Arr_spherical_topology_traits_2.h:400`:
```cpp
typedef Arr_naive_point_location<Arr> Default_point_location_strategy;
typedef Arr_naive_point_location<Arr> Default_vertical_ray_shooting_strategy;
```
whereas both planar topology traits say `Arr_walk_along_line_point_location`
(`Arr_bounded_planar_topology_traits_2.h:249`, `Arr_unb_planar_topology_traits_2.h:253`).
The doc comments on `zone`/`do_intersect`/`insert` in `Arrangement_on_surface_2_global.h` still say
*"the walk point-location strategy is used as default"* — that comment is **wrong for the sphere**.
This corrects gotcha 14 of `global_functions_overlay_observer.md`, which states the walk strategy as
*the* default: it is only the default for the two planar topology traits.
Worse, **`Arr_walk_along_line_point_location` does not even compile with the spherical topology traits**
(`no member named 'initial_face'`, `no member named 'compare_xy'` in
`Arr_spherical_topology_traits_2`) **[verified]**.

**G6. `zone` / `do_intersect` miss an incidence when the query curve's *maximal* end is exactly the
north-pole vertex.** `zone(arr, xcv, oi)` reports only the containing face and `do_intersect` returns
`false`, while `CGAL::insert(arr, xcv)` of the *same* curve correctly attaches to the pole vertex
(pole degree 2 → 3, `is_valid()` still true) **[verified]**. The symmetric south-pole case (the pole is
the *minimal* end) is reported correctly. The traits is not at fault: `Intersect_2` on two arcs meeting
only at a pole returns 1 result **[verified]**; the omission is in `Arrangement_zone_2`'s right-end
bookkeeping (`Arr_compute_zone_visitor::found_subcurve` only emits `right_v` if the zone algorithm
found one).

**G7. The sphere takes the NON-indexed overlay sweep** (see §10.2) — slower, but it never mutates the
input arrangements.

**G8. Dead code in `Arr_spherical_batched_pl_helper::after_handle_event`:**
`if (event->parameter_space_in_y() == ARR_RIGHT_BOUNDARY)`. `Arr_parameter_space` is
`Box_parameter_space_2` with `LEFT=0, RIGHT=1, BOTTOM=2, TOP=3, INTERIOR=4` (`enum.h:90`), and
`parameter_space_in_y()` can only be `BOTTOM/TOP/INTERIOR`, so that branch is unreachable.

---

## 10.1 The canonical spherical test arrangement used below

```cpp
using Kernel = CGAL::Exact_predicates_exact_constructions_kernel;
using Gt     = CGAL::Arr_geodesic_arc_on_sphere_traits_2<Kernel>;
using Topol  = CGAL::Arr_spherical_topology_traits_2<Gt>;
using Arr    = CGAL::Arrangement_on_surface_2<Gt, Topol>;

Gt traits;
auto cp  = traits.construct_point_2_object();     // non-const auto!
auto ccv = traits.construct_curve_2_object();
Arr arr(&traits);

// NOTE the order: the arc lying ON the identification curve MUST go first (gotcha G1).
CGAL::insert(arr, ccv(cp(-1,0,2), cp(-1,0,-1)));    // on the identification meridian
CGAL::insert(arr, ccv(cp( 2,0,1), cp( 0,2,1)));     // spherical triangle A-B
CGAL::insert(arr, ccv(cp( 0,2,1), cp( 1,1,-2)));    //                    B-C
CGAL::insert(arr, ccv(cp( 1,1,-2),cp( 2,0,1)));     //                    C-A
CGAL::insert(arr, ccv(Kernel::Direction_3(0,0,1))); // full equator, crosses the triangle
CGAL::insert(arr, ccv(cp( 1,0,2), cp(-1,0,2)));     // arc THROUGH the north pole
CGAL::insert(arr, ccv(cp( 0,-1,-2),cp(0,1,-2)));    // arc THROUGH the south pole
```

**[verified]** result: `V=14  E=15  F=4  isolated=0  is_valid()=true`, `north_pole() != nullptr`,
`south_pole() != nullptr`. Faces:

| face | `number_of_outer_ccbs()` | `number_of_inner_ccbs()` | note |
|---|---|---|---|
| 0 | 0 | 1 | `topology_traits()->spherical_face()` |
| 1 | 1 | 0 | |
| 2 | 1 | 0 | |
| 3 | 1 | 1 | `topology_traits()->south_face()` |

Vertices (`loc` = `Point_2::location()`, `psx/psy` = `parameter_space_in_x/y()`, `4 == ARR_INTERIOR`):

```
(-1,0,-1)  loc=2(MID)  psx=0(LEFT) psy=4  deg=1     on identification
(-1,0, 2)  loc=2       psx=0       psy=4  deg=2     on identification
( 2,0, 1)  loc=0       psx=4       psy=4  deg=2
( 0,2, 1)  loc=0       psx=4       psy=4  deg=2
( 1,1,-2)  loc=0       psx=4       psy=4  deg=2
(-1,0, 0)  loc=2       psx=0       psy=4  deg=4     equator x identification
( 1,0, 0)  loc=0       psx=4       psy=4  deg=2
( 5,1, 0)  loc=0       psx=4       psy=4  deg=4     equator x triangle
( 1,5, 0)  loc=0       psx=4       psy=4  deg=4     equator x triangle
( 0,0, 1)  loc=3(MAX)  psx=4       psy=3(TOP)    deg=2   NORTH POLE
( 1,0, 2)  loc=0       psx=4       psy=4  deg=1
( 0,0,-1)  loc=1(MIN)  psx=4       psy=2(BOTTOM) deg=2   SOUTH POLE
( 0,-1,-2) loc=0       psx=4       psy=4  deg=1
( 0, 1,-2) loc=0       psx=4       psy=4  deg=1
```

`south_face()` is **not** the spherical face in general. From
`Arr_spherical_topology_traits_2.h:247-260`:

```cpp
const Face* south_face() const
{
  if (m_boundary_vertices.empty()) return m_spherical_face;
  typename Vertex_map::const_iterator it = m_boundary_vertices.begin();
  return _face_below_vertex_on_discontinuity(it->second);
}
Face* south_face();                       // same, non-const
```
i.e. it is the face below the *lowest* identification vertex, and it degenerates to the spherical face
only when there are no identification vertices at all **[verified: with an interior-only arrangement,
`south_face() == spherical_face()`; with the canonical one they differ]**.

### Aggregate insertion **[verified]**

```cpp
template <typename GeomTraits, typename TopTraits, typename InputIterator>
void insert(Arrangement_on_surface_2<GeomTraits, TopTraits>& arr,
            InputIterator begin, InputIterator end);          // Curve_2 or X_monotone_curve_2
template <typename GeomTraits, typename TopTraits, typename InputIterator>
void insert_non_intersecting_curves(Arrangement_on_surface_2<GeomTraits, TopTraits>& arr,
                                    InputIterator begin, InputIterator end);
```
The same seven curves inserted with `CGAL::insert(arr, cs.begin(), cs.end())` give the identical
`V=14 E=15 F=4 valid` result **in any order**, including with the on-identification curve last
**[verified]** — this is the surface-sweep path
(`Arr_spherical_construction_helper` / `Arr_spherical_insertion_helper`) and it is the robust way to
build a spherical arrangement. `insert_non_intersecting_curves` also works, including a curve ending at
the south pole **[verified]**.

---

## 10.2 `CGAL::overlay` on the sphere

### 10.2.1 Signatures (verbatim, `Arr_overlay_2.h`)

```cpp
template <typename GeometryTraitsA_2, typename GeometryTraitsB_2, typename GeometryTraitsRes_2,
          typename TopologyTraitsA, typename TopologyTraitsB, typename TopologyTraitsRes,
          typename OverlayTraits>
void
overlay(const Arrangement_on_surface_2<GeometryTraitsA_2, TopologyTraitsA>& arr1,
        const Arrangement_on_surface_2<GeometryTraitsB_2, TopologyTraitsB>& arr2,
        Arrangement_on_surface_2<GeometryTraitsRes_2, TopologyTraitsRes>& arr,
        OverlayTraits& ovl_tr);

template <typename GeometryTraitsA_2, typename GeometryTraitsB_2, typename GeometryTraitsRes_2,
          typename TopologyTraitsA, typename TopologyTraitsB, typename TopologyTraitsRes>
void
overlay(const Arrangement_on_surface_2<GeometryTraitsA_2, TopologyTraitsA>& arr1,
        const Arrangement_on_surface_2<GeometryTraitsB_2, TopologyTraitsB>& arr2,
        Arrangement_on_surface_2<GeometryTraitsRes_2, TopologyTraitsRes>& arr);
        // uses _Arr_default_overlay_traits_base<Arr_a, Arr_b, Arr_res>
```

Static requirements enforced inside (`static_assert`):
`std::is_convertible<Agt2::Point_2, Rgt2::Point_2>`, same for `Bgt2`, and the same two for
`X_monotone_curve_2`. Precondition (`CGAL_precondition`):
`((void*)&arr != (void*)&arr1) && ((void*)&arr != (void*)&arr2)`.
`arr.clear()` is called on the result before the sweep — **the result arrangement is wiped**, so any
handles into it are invalidated and its observers get `before_clear`/`after_clear`.

### 10.2.2 The sweep-selection branch — the sphere takes the NON-indexed path **[verified]**

```cpp
  if (total_iso_verts == 0) {
    arr.clear();
    if (std::is_same<typename Agt2::Bottom_side_category,
                     Arr_contracted_side_tag>::value)
      surface_sweep.sweep (xcvs_vec.begin(), xcvs_vec.end());
    else
      surface_sweep.indexed_sweep (xcvs_vec,
                                   Indexed_sweep_accessor
                                   <Arr_a, Arr_b, Ovl_x_monotone_curve_2>
                                   (arr1, arr2));
    xcvs_vec.clear();
    return;
  }
  ... /* identical branch again, with the isolated-vertex point range appended */
```

`Gt::Bottom_side_category` **is** `CGAL::Arr_contracted_side_tag` for the geodesic traits, so the
condition is true and the **plain `sweep()`** is taken. Confirmed by `static_assert`:

```cpp
static_assert(std::is_same<Gt::Bottom_side_category, CGAL::Arr_contracted_side_tag>::value, "");
static_assert(std::is_same<Gt::Top_side_category,    CGAL::Arr_contracted_side_tag>::value, "");
static_assert(std::is_same<Gt::Left_side_category,   CGAL::Arr_identified_side_tag>::value, "");
static_assert(std::is_same<Gt::Right_side_category,  CGAL::Arr_identified_side_tag>::value, "");
```

Note that this is a **runtime `if`, not `if constexpr`**, so the `indexed_sweep` branch and
`Indexed_sweep_accessor` are still *instantiated* for spherical arrangements (they compile — verified
implicitly, since the overlay test builds) but are never executed.

**Consequences of taking the non-indexed path:**

1. *Performance.* `_init_indexed_curves` caches one `Event_queue_iterator` per input vertex index, so
   the 2nd..k-th curve end incident to the same input vertex skips the comparison-based
   `m_queue->find_lower(...)` lookup. The non-indexed `_init_curves` calls `_init_curve_end` → `_push_event`
   → `find_lower` for **every** curve end, i.e. `2·(E_red + E_blue)` multiset lookups driven by the exact
   spherical `Compare_events` predicate instead of roughly `V_red + V_blue`. For EPECK geodesic arcs
   these comparisons are the dominant cost, so spherical overlay is materially slower than planar
   overlay of the same size. There is no way to opt in to the indexed path from user code.
2. *The inputs are never mutated.* `Indexed_sweep_accessor::before_init()` "squats"
   `Vertex::inc()` on the **const** input arrangements
   (`vit->set_inc(reinterpret_cast<void*>(idx))`, restored in `after_init()`). Because the sphere never
   calls `indexed_sweep`, `CGAL::overlay` on spherical arrangements is genuinely read-only w.r.t.
   `arr1`/`arr2`. (In the plane it is not: two concurrent overlays sharing an input would corrupt each
   other.) For a type-erased core this means spherical overlay is safe to run on shared, immutable inputs.

### 10.2.3 `Arr_spherical_overlay_helper` (verbatim skeleton)

```cpp
template <typename GeometryTraits_2, typename ArrangementRed_2, typename ArrangementBlue_2,
          typename Arrangement_, typename Event_, typename Subcurve_>
class Arr_spherical_overlay_helper {
public:
  typedef GeometryTraits_2   Geometry_traits_2;   typedef ArrangementRed_2  Arrangement_red_2;
  typedef ArrangementBlue_2  Arrangement_blue_2;  typedef Arrangement_      Arrangement_2;
  typedef Event_             Event;               typedef Subcurve_         Subcurve;

  typedef typename Gt2::X_monotone_curve_2          X_monotone_curve_2;
  typedef typename Gt2::Point_2                     Point_2;
  typedef typename Event::Subcurve_iterator         Subcurve_iterator;
  typedef typename Event::Subcurve_reverse_iterator Subcurve_reverse_iterator;
  typedef typename Ar2::Topology_traits             Topology_traits_red;
  typedef typename Ar2::Face_const_handle           Face_handle_red;
  typedef typename Ab2::Topology_traits             Topology_traits_blue;
  typedef typename Ab2::Face_const_handle           Face_handle_blue;
  typedef Arr_spherical_construction_helper<Gt2, Arrangement_2, Event, Subcurve> Construction_helper;

  Arr_spherical_overlay_helper(const Ar2* red_arr, const Ab2* blue_arr);
  void before_sweep();                     // m_red_nf = red_top_traits->south_face(); same for blue
  void before_handle_event(Event* event);  // only acts on ARR_TOP_BOUNDARY / ARR_LEFT_BOUNDARY events
  Face_handle_red   red_top_face()  const { return m_red_nf; }
  Face_handle_blue  blue_top_face() const { return m_blue_nf; }
  const Topology_traits_red*  red_topology_traits()  const;
  const Topology_traits_blue* blue_topology_traits() const;
};
```
Note `before_sweep()` seeds with `south_face()`, **not** `spherical_face()` — the `spherical_face()`
version is commented out in the header (`/* RWRW: ... */`). `red_top_face()`/`blue_top_face()` then
move as the sweep passes events on the north pole (`ARR_TOP_BOUNDARY`) and on the identification
curve (`ARR_LEFT_BOUNDARY`).

Reached from the topology traits as (`Arr_spherical_topology_traits_2.h:381-392`):
```cpp
template <typename Gt, typename Evt, typename Crv, typename ArrA, typename ArrB>
struct Overlay_helper : public Arr_spherical_overlay_helper<Gt, ArrA, ArrB, Arr, Evt, Crv> {
  typedef Arr_spherical_overlay_helper<Gt, ArrA, ArrB, Arr, Evt, Crv> Base;
  Overlay_helper(const ArrA* arr_a, const ArrB* arr_b) : Base(arr_a, arr_b) {}
};
```

### 10.2.4 The overlay-traits interface (all ten `create_*`)

`CGAL::_Arr_default_overlay_traits_base<ArrangementA, ArrangementB, ArrangementR>`
(`Surface_sweep_2/Arr_default_overlay_traits_base.h`) — all members are `virtual ... const` and
empty by default; derive from it and override what you need:

```cpp
typedef typename ArrangementA::Vertex_const_handle    Vertex_handle_A;
typedef typename ArrangementA::Halfedge_const_handle  Halfedge_handle_A;
typedef typename ArrangementA::Face_const_handle      Face_handle_A;
typedef typename ArrangementB::Vertex_const_handle    Vertex_handle_B;
typedef typename ArrangementB::Halfedge_const_handle  Halfedge_handle_B;
typedef typename ArrangementB::Face_const_handle      Face_handle_B;
typedef typename ArrangementR::Vertex_handle          Vertex_handle_R;
typedef typename ArrangementR::Halfedge_handle        Halfedge_handle_R;
typedef typename ArrangementR::Face_handle            Face_handle_R;

virtual ~_Arr_default_overlay_traits_base();

virtual void create_vertex(Vertex_handle_A   v1, Vertex_handle_B   v2, Vertex_handle_R   v) const;
virtual void create_vertex(Vertex_handle_A   v1, Halfedge_handle_B e2, Vertex_handle_R   v) const;
virtual void create_vertex(Vertex_handle_A   v1, Face_handle_B     f2, Vertex_handle_R   v) const;
virtual void create_vertex(Halfedge_handle_A e1, Vertex_handle_B   v2, Vertex_handle_R   v) const;
virtual void create_vertex(Face_handle_A     f1, Vertex_handle_B   v2, Vertex_handle_R   v) const;
virtual void create_vertex(Halfedge_handle_A e1, Halfedge_handle_B e2, Vertex_handle_R   v) const;
virtual void create_edge  (Halfedge_handle_A e1, Halfedge_handle_B e2, Halfedge_handle_R e) const;
virtual void create_edge  (Halfedge_handle_A e1, Face_handle_B     f2, Halfedge_handle_R e) const;
virtual void create_edge  (Face_handle_A     f1, Halfedge_handle_B e2, Halfedge_handle_R e) const;
virtual void create_face  (Face_handle_A     f1, Face_handle_B     f2, Face_handle_R     f) const;
```
Convenience subclasses: `CGAL::Arr_default_overlay_traits<Arrangement_>` (all three arrangements the
same type) and
`CGAL::Arr_face_overlay_traits<ArrangementA, ArrangementB, ArrangementR, OverlayFaceData_>`
(overrides only `create_face`, calling `OverlayFaceData_()(f1->data(), f2->data())`).

**Note the constness:** the callbacks are `const` member functions but the *result* handles are
non-const, so mutating counters must be `mutable`/global. The red/blue handles are **const** handles.

### 10.2.5 Verified run: all ten callbacks fire, with three different extended DCELs

```cpp
using DcelR = CGAL::Arr_face_extended_dcel<Gt, int>;
using DcelB = CGAL::Arr_face_extended_dcel<Gt, char>;
using DcelO = CGAL::Arr_extended_dcel<Gt, std::string, std::string, std::pair<int,char> >;
using ArrR  = CGAL::Arrangement_on_surface_2<Gt, CGAL::Arr_spherical_topology_traits_2<Gt, DcelR> >;
using ArrB  = CGAL::Arrangement_on_surface_2<Gt, CGAL::Arr_spherical_topology_traits_2<Gt, DcelB> >;
using ArrO  = CGAL::Arrangement_on_surface_2<Gt, CGAL::Arr_spherical_topology_traits_2<Gt, DcelO> >;
struct Ovl : CGAL::_Arr_default_overlay_traits_base<ArrR, ArrB, ArrO> { /* all ten overridden */ };
```
Red = equator + a spherical triangle + an arc starting on the equator + a far arc (`V=12 E=14 F=5`);
blue = the *same* equator + a crossing triangle + an arc starting on the equator + a far arc
(`V=12 E=15 F=5`). **[verified]** result `V=25 E=34 F=11`, `res.is_valid() == true`, and the callback
counts were

| callback | count |
|---|---|
| `create_vertex(V_A, V_B, V_R)`  | 2 |
| `create_vertex(V_A, H_B, V_R)`  | 4 |
| `create_vertex(V_A, F_B, V_R)`  | 6 |
| `create_vertex(H_A, V_B, V_R)`  | 3 |
| `create_vertex(F_A, V_B, V_R)`  | 7 |
| `create_vertex(H_A, H_B, V_R)`  | 3 |
| `create_edge(H_A, H_B, H_R)`    | 8 |
| `create_edge(H_A, F_B, H_R)`    | 12 |
| `create_edge(F_A, H_B, H_R)`    | 14 |
| `create_face(F_A, F_B, F_R)`    | 11 (= `res.number_of_faces()`) |

So **all ten fire on the sphere**, exactly as in the plane. The parameter-free
`CGAL::overlay(red, blue, res2)` produced the identical `V=25 E=34 F=11 valid` arrangement **[verified]**.

**The isolated-vertex branch works too.** After `CGAL::insert_point` on both inputs
(`iso_red = iso_blue = 1`), the `sweep(xcvs.begin(), xcvs.end(), pts.begin(), pts.end())` branch ran and
gave `V=27 E=34 F=11 isolated=2 valid=true`, with `cv(V,F)` and `cv(F,V)` each incremented by one
**[verified]**.

### 10.2.6 The final `create_face(red_top_face, blue_top_face, top_face)` IS meaningful on the sphere

`Arr_overlay_ss_visitor::after_sweep()` (`Arr_overlay_ss_visitor.h:553-575`) ends with

```cpp
  m_overlay_traits->create_face(m_overlay_helper.red_top_face(),
                                m_overlay_helper.blue_top_face(),
                                this->m_helper.top_face());
```

On the sphere there is no unbounded face, but this call is *not* vacuous: **[verified]** at that moment
`red_top_face()` is red's `spherical_face()`, `blue_top_face()` is blue's `spherical_face()`, and
`this->m_helper.top_face()` (the `Arr_spherical_construction_helper`'s `m_spherical_face`) is the
**result's** `spherical_face()` — the one face with `number_of_outer_ccbs() == 0` and
`number_of_inner_ccbs() == 1`. Instrumented output of the last call:

```
create_face: red=0 blue=a [red-sph] [blue-sph] res occb=0 iccb=1
```
It is the exact analogue of the planar `create_face(unbounded, unbounded, unbounded)` call, and it is
the **only** call that ever produces the result's spherical face — if your overlay traits skips it, the
spherical face's `data()` is left default-constructed. Every one of the other 10 faces got
`occb=1 iccb=0` and a data pair from a "normal" `create_face` call.

`Arr_spherical_construction_helper::top_face()` is:
```cpp
  virtual Face_handle top_face() const { return m_spherical_face; }   // seeded from spherical_face()
```
and `Arr_spherical_insertion_helper::top_face()` overrides it:
```cpp
  const Halfedge_handle invalid_he;
  if (m_spherical_halfedge != invalid_he) return m_spherical_halfedge->face();
  return this->m_spherical_face;                                       // seeded from south_face()
```

---

## 10.3 `CGAL::zone` and `CGAL::do_intersect` on the sphere

### 10.3.1 Signatures (verbatim, `Arrangement_on_surface_2_global.h`)

```cpp
template <typename GeometryTraits_2, typename TopologyTraits,
          typename OutputIterator, typename PointLocation>
OutputIterator
zone(Arrangement_on_surface_2<GeometryTraits_2, TopologyTraits>& arr,
     const typename GeometryTraits_2::X_monotone_curve_2& c,
     OutputIterator oi,
     const PointLocation& pl);

template <typename GeometryTraits_2, typename TopologyTraits, typename OutputIterator>
OutputIterator
zone(Arrangement_on_surface_2<GeometryTraits_2, TopologyTraits>& arr,
     const typename GeometryTraits_2::X_monotone_curve_2& c,
     OutputIterator oi);
// body:  typename Tt::Default_point_location_strategy def_pl(arr);  zone(arr, c, oi, def_pl);

template <typename GeometryTraits_2, typename TopologyTraits, typename Curve, typename PointLocation>
bool do_intersect(Arrangement_on_surface_2<GeometryTraits_2, TopologyTraits>& arr,
                  const Curve& c, const PointLocation& pl);
template <typename GeometryTraits_2, typename TopologyTraits, typename Curve>
bool do_intersect(Arrangement_on_surface_2<GeometryTraits_2, TopologyTraits>& arr, const Curve& c);
```

* `arr` is taken by **non-const reference** even though neither function modifies it (they run
  `Arrangement_zone_2` with a read-only visitor). A binding cannot call them on a `const Arr&`.
* The output value type of `zone` is
  `std::variant<Arr::Vertex_handle, Arr::Halfedge_handle, Arr::Face_handle>` — **non-const** handles
  (`Arr_compute_zone_visitor` uses `Arrangement_2::Vertex_handle` etc.).
* `do_intersect` accepts both `Curve_2` and `X_monotone_curve_2` (dispatched on
  `std::is_same<Curve, X_monotone_curve_2>`); `zone` accepts only `X_monotone_curve_2`.
* **Which PL does the no-`pl` overload pick?** `Tt::Default_point_location_strategy`, i.e.
  `CGAL::Arr_naive_point_location<Arr>` for the sphere — see gotcha **G5**. The header comment above
  the overload ("the walk point-location strategy is used as default") is stale.

### 10.3.2 `Arrangement_zone_2<Arrangement_, ZoneVisitor_>` public API (verbatim)

```cpp
  using Arrangement_2 = Arrangement_;
  using Geometry_traits_2 = typename Arrangement_2::Geometry_traits_2;
  using Topology_traits   = typename Arrangement_2::Topology_traits;
  using Traits_adaptor_2  = Arr_traits_adaptor_2<Geometry_traits_2>;
  using Left_side_category   = typename Traits_adaptor_2::Left_side_category;
  using Bottom_side_category = typename Traits_adaptor_2::Bottom_side_category;
  using Top_side_category    = typename Traits_adaptor_2::Top_side_category;
  using Right_side_category  = typename Traits_adaptor_2::Right_side_category;
  using Visitor = ZoneVisitor_;
  using Vertex_handle   = typename Arrangement_2::Vertex_handle;
  using Halfedge_handle = typename Arrangement_2::Halfedge_handle;
  using Face_handle     = typename Arrangement_2::Face_handle;
  using Visitor_result  = std::pair<Halfedge_handle, bool>;
  using Point_2 = typename Geometry_traits_2::Point_2;
  using X_monotone_curve_2 = typename Geometry_traits_2::X_monotone_curve_2;
  using Multiplicity = typename Geometry_traits_2::Multiplicity;
  using Intersection_point  = std::pair<Point_2, Multiplicity>;
  using Intersection_result = std::variant<Intersection_point, X_monotone_curve_2>;
  using Optional_intersection = std::optional<Intersection_result>;
  using Pl_result = Arr_point_location_result<Arrangement_2>;
  using Pl_result_type = typename Pl_result::Type;   // std::variant<V_const,H_const,F_const>

  Arrangement_zone_2(Arrangement_2& arr, Visitor* visitor);   // CGAL_assertion(visitor != nullptr);
                                                              // calls visitor->init(&arr)
  template <typename PointLocation>
  void init(const X_monotone_curve_2& cv, const PointLocation& pl);
  void init_with_hint(const X_monotone_curve_2& cv, Pl_result_type obj);
  void compute_zone();
```

`init()` only calls `pl.locate(m_left_pt)` when the curve's **minimal** end is in the interior of the
parameter space; otherwise it goes through `m_arr_access.locate_curve_end(m_cv, ARR_MIN_END, bx1, by1)`
(which the spherical topology traits implements, returning
`std::variant<Vertex*, Halfedge*, Face*>`). So for curves that start at a pole or on the identification
the supplied point-location object is **not used at all**.

### 10.3.3 Verified behaviour on the canonical arrangement

| query x-monotone curve | `zone` output | `do_intersect` |
|---|---|---|
| `(3,1,1)→(1,3,1)` entirely inside one face | `F` | `false` |
| `(2,0,1)→(0,2,1)` overlaps an existing triangle edge | `V H V` | `true` |
| `(1,1,2)→(1,1,-2)` crosses the equator + triangle | `V F H F H F` | `true` |
| `(4,1,1)→(1,4,-1)` crosses the triangle | `F H F H F` | `true` |
| `(1,1,3)→(0,0,1)` **ends at the north pole** | `F` ← **wrong, see G6** | `false` |
| `(1,1,-3)→(0,0,-1)` ends at the south pole | `V F` | `true` |
| `(-1,1,1)→(-1,0,1)` ends on the identification | `F` | `false` |
| `(-1,0,3)→(-1,0,1)` **lies on the identification** | **throws** `Compare_xy_2` precondition | — |

**[all verified]**. Passing an explicit `Arr_naive_point_location` or `Arr_landmarks_point_location`
gives the identical output (6 cells for the crossing query) **[verified]**, i.e. the PL strategy only
picks the starting cell.

`do_intersect(arr, Curve_2)` (non-x-monotone) also works: it x-monotone-splits and calls the
x-monotone version per piece **[verified: `true` for the crossing curve]**.

---

## 10.4 `CGAL::decompose` (vertical decomposition) on the sphere

### 10.4.1 Signature (verbatim, `Arr_vertical_decomposition_2.h`)

```cpp
template <typename GeometryTraits_2, typename TopologyTraits, typename OutputIterator>
OutputIterator
decompose(const Arrangement_on_surface_2<GeometryTraits_2, TopologyTraits>& arr,
          OutputIterator oi);
```
`arr` is `const` here (unlike `zone`/`do_intersect`). The output value type is

```cpp
using VCH   = Arr::Vertex_const_handle;
using HCH   = Arr::Halfedge_const_handle;
using FCH   = Arr::Face_const_handle;
using Cell  = std::variant<VCH, HCH, FCH>;
using Vert_type = std::optional<Cell>;                     // an EMPTY optional is possible
using Entry = std::pair<VCH, std::pair<Vert_type, Vert_type> >;   // (v, (below, above))
```
Vertices are emitted **in increasing xy (sweep) order**, not in `arr.vertices_begin()` order, and the
last vertex is flushed in `after_sweep()`.

### 10.4.2 It aborts as soon as there is ANY boundary vertex **[verified]**

Minimal repros (both start from a 3-arc interior-only spherical triangle, `V=3 E=3 F=2`, whose
`decompose` works):

```cpp
CGAL::insert(b, ccv(cp(1,1,3), cp(0,0,1)));      // ONE north-pole vertex   -> V=5 E=4 F=2
CGAL::decompose(b, oi);   // CGAL error: precondition violation!
                          // Expr: p1.is_no_boundary()   ...traits_2.h:1038   (Compare_x_2)

CGAL::insert(b, ccv(cp(-1,1,1), cp(-1,-1,1)));  // ONE identification vertex -> V=6 E=5 F=2
CGAL::decompose(b, oi);   // Expr: p2.is_no_boundary()   ...traits_2.h:1039   (Compare_x_2)
```
The culprit is in `Arr_vert_decomp_ss_visitor::after_handle_event`:
```cpp
  const bool prev_same_x =
    (m_prev_vh != invalid_vh &&
     m_traits->compare_x_2_object()(vh->point(), m_prev_vh->point()) == EQUAL);
```
which is evaluated for every consecutive pair of vertices, with no boundary guard, while
`Arr_geodesic_arc_on_sphere_traits_2::Compare_x_2` declares `\pre p1/p2 does not lie on the boundary`.

**So on the sphere `decompose` is only usable on arrangements whose vertices all satisfy
`parameter_space_in_x() == parameter_space_in_y() == ARR_INTERIOR`** — i.e. nothing touching either pole
and nothing on the identification meridian.

### 10.4.3 What `below`/`above` contain when it *does* work **[verified]**

There is no unbounded face, so the planar bounded/unbounded distinction of
`point_location_and_decomposition.md §11` does not apply. The helper is

```cpp
template <typename GeometryTraits_2, typename Arrangement_, typename Event_, typename Subcurve_>
class Arr_spherical_vert_decomp_helper {
public:
  typedef std::variant<Vertex_const_handle, Halfedge_const_handle, Face_const_handle> Cell_type;
  typedef std::optional<Cell_type>                                                    Vert_type;
  Arr_spherical_vert_decomp_helper(const Arrangement_2* arr);
  void before_sweep();                     // m_valid_north_pole = (top_traits->north_pole() != nullptr)
                                           // m_north_face = spherical_face(); m_south_face = south_face()
  void after_handle_event(Event* event);   // only reacts to ARR_TOP_BOUNDARY / ARR_BOTTOM_BOUNDARY
  Vert_type top_object()    const { return (m_valid_north_pole) ? Vert_type(m_north_pole) : Vert_type(m_north_face); }
  Vert_type bottom_object() const { return (m_valid_south_pole) ? Vert_type(m_south_pole) : Vert_type(m_south_face); }
};
```
Because a live pole vertex is exactly what makes `decompose` abort, in every arrangement where
`decompose` succeeds `m_valid_north_pole` and `m_valid_south_pole` are `false`, so:

* `above` for the topmost vertex is always the **spherical face** (`Face_const_handle`).
* `below` for the bottommost vertex is always `south_face()`, which — with no identification vertices —
  **is** the spherical face.
* `top_object()` / `bottom_object()` are **never the empty optional** on the sphere.
* The empty optional appears only in the planar-shared case of two vertices connected by a *vertical*
  edge (`obj_below = Vert_type(); m_prev_obj_above = Vert_type();`).

Measured output (arrangement: a triangle `(2,0,1),(0,2,1),(1,1,2)`, a chord `(3,1,2)→(1,3,2)`, a far
southern arc `(1,1,-3)→(1,-1,-3)`; `V=7 E=6 F=2`, `south_face()==spherical_face()`):

```
v(1,-1,-3)  below=F(spherical,occb=0)  above=F(spherical,occb=0)
v( 2,0, 1)  below=H(->1,-1,-3)         above=F(spherical,occb=0)
v( 3,1, 2)  below=H(->1,-1,-3)         above=H(->2,0,1)
v( 1,1,-3)  below=F(spherical,occb=0)  above=H(->3,1,2)
v( 1,1, 2)  below=H(->3,1,2)           above=F(spherical,occb=0)
v( 1,3, 2)  below=F(spherical,occb=0)  above=H(->1,1,2)
v( 0,2, 1)  below=F(spherical,occb=0)  above=F(spherical,occb=0)
```
Note the "vertical" direction is latitude in the cylindrical parameter space, so "above the topmost
vertex" wraps to the spherical face (the region containing the north pole) — exactly the role the
unbounded face plays in the plane.

---

## 10.5 `CGAL::locate` (batched point location) on the sphere

### 10.5.1 Signature (verbatim, `Arr_batched_point_location.h`)

```cpp
template <typename GeometryTraits_2, typename TopologyTraits,
          typename PointsIterator, typename OutputIterator>
OutputIterator
locate(const Arrangement_on_surface_2<GeometryTraits_2, TopologyTraits>& arr,
       PointsIterator points_begin, PointsIterator points_end,
       OutputIterator oi);
```
`\pre` from the doc comment: the value type of `PointsIterator` is `Arrangement::Point_2`, and the
value type of `OutputIterator` is `std::pair<Point_2, Result>`. In CGAL 6.1 the actual `Result` is

```cpp
using Result = CGAL::Arr_point_location_result<Arr>::Type;   // std::variant<V_const, H_const, F_const>
```
— **NOT** wrapped in `std::optional` (the doc comment in the header still says
`std::optional<std::variant<...>>`; the visitor writes `std::make_pair(pt, pl_make_result(x))` with a
bare `Type`) **[verified: `std::vector<std::pair<Point, Arr_point_location_result<Arr>::Type>>` compiles
and `std::get_if<Vertex_const_handle>(&r.second)` works]**.

Results come out in **sweep order**, not in the input order — a Cython layer that wants
input-order results must key on the returned `Point_2`.

`arr` is `const`. Isolated vertices of `arr` are handled (they become ACTION points).

### 10.5.2 Verified: correct everywhere except the south pole

Canonical arrangement, 12 queries, compared against `CGAL::Arr_naive_point_location<Arr>::locate`:

```
  query          naive-PL         batched-PL
  (1,1,5)        F(spherical)     F(spherical)
  (0,0,1)        V(0,0,1)         V(0,0,1)          <- north pole: OK
  (0,0,-1)       V(0,0,-1)        F(spherical)      <<< MISMATCH (south pole)
  (-1,0,0)       V(-1,0,0)        V(-1,0,0)         <- identification vertex: OK
  (-1,0,1)       H(->-1,0,0)      H(->-1,0,0)       <- interior of an identification edge: OK
  (1,0,0)        V(1,0,0)         V(1,0,0)
  (5,1,9)        F(spherical)     F(spherical)
  (1,1,0)        H(->5,1,0)       H(->5,1,0)
  (1,1,-3)       F(inner)         F(inner)          <- southern hemisphere face: OK
  (0,-1,-3)      H(->0,0,-1)      H(->0,0,-1)
  (-1,-1,-3)     F(inner)         F(inner)
  (3,1,1)        F(inner)         F(inner)
```
**[verified]** 11/12 identical; the single failure is the south pole.

Root cause, from `Arr_spherical_batched_pl_helper`:

```cpp
  void before_sweep()
  { m_spherical_face = Face_const_handle(m_top_traits->spherical_face()); }

  void after_handle_event(Event* event)
  {
    if (event->parameter_space_in_y() == ARR_TOP_BOUNDARY)  { /* ... update m_spherical_face ... */ }
    // EBEB 2013-12-012 do similar stuff for right boundary
    if (event->parameter_space_in_y() == ARR_RIGHT_BOUNDARY) { /* DEAD CODE, see G8 */ }
  }

  Face_const_handle top_face() const { return m_spherical_face; }
```
There is no `ARR_BOTTOM_BOUNDARY` case, so the "current top face" is still the initial spherical face
when the sweep processes the very first event (the south pole). `Arr_batched_pl_ss_visitor` then falls
through to `above == status_line_end() -> m_helper.top_face()` because the query event at the south
pole was pushed via `_init_point` (parameter space derived from the *point*) and was **not** merged with
the curve-end event for the south-pole vertex (pushed via `_push_event(cv, ARR_MIN_END, ...)`).

**Binding advice:** filter the query points — answer any query whose
`Point_2::location() == MIN_BOUNDARY_LOC` (south pole) with `topology_traits()->south_pole()` yourself,
or fall back to `Arr_naive_point_location` for those.

---

## 10.6 Removal on the sphere

### 10.6.1 Signatures (verbatim)

```cpp
// member, Arrangement_on_surface_2.h:1494
Face_handle remove_edge(Halfedge_handle e, bool remove_source = true, bool remove_target = true);
// member, Arrangement_on_surface_2.h:1444   \pre v is an isolated vertex (it has no incident halfedges)
Face_handle remove_isolated_vertex(Vertex_handle v);

// free, Arrangement_on_surface_2_global.h  (also merges the incident edges when possible)
template <typename GeomTraits, typename TopTraits>
typename Arrangement_on_surface_2<GeomTraits, TopTraits>::Face_handle
remove_edge(Arrangement_on_surface_2<GeomTraits, TopTraits>& arr,
            typename Arrangement_on_surface_2<GeomTraits, TopTraits>::Halfedge_handle e);

template <typename GeomTraits, typename TopTraits>
bool remove_vertex(Arrangement_on_surface_2<GeomTraits, TopTraits>& arr,
                   typename Arrangement_on_surface_2<GeomTraits, TopTraits>::Vertex_handle v);
```

`CGAL::remove_vertex` body (`Arrangement_on_surface_2_global.h:1204+`): if `v->is_isolated()` it calls
`arr.remove_isolated_vertex(v)` and returns `true`; else if `v->degree() == 2` it merges the two
incident curves **iff** `traits->are_mergeable_2_object()(e1->curve(), e2->curve())` and returns
`true`, otherwise returns `false`. It is bracketed by
`Arr_accessor::notify_before_global_change()`/`notify_after_global_change()`.

### 10.6.2 Removing *edges* correctly cleans up the poles and the identification map **[verified]**

Arrangement: an arc through the north pole ending on the identification, plus an arc through the south
pole (`V=6 E=4 F=1`, `north_pole()` and `south_pole()` both set).

```
before                        : V=6 E=4 F=1 valid=Y npole=1 spole=1
arr.remove_edge(e,true,true)  : V=5 E=3 F=1 valid=Y npole=1 spole=1
arr.remove_edge(e,true,true)  : V=3 E=2 F=1 valid=Y npole=0 spole=1   <- pole vertex freed
north_pole() == nullptr                       -> true
discontinuity_vertex((-1,0,2)) == nullptr     -> true
CGAL::remove_edge(arr, e)     : V=2 E=1 F=1 valid=Y npole=0 spole=1
CGAL::remove_edge(arr, e)     : V=0 E=0 F=1 valid=Y npole=0 spole=0
south_pole() == nullptr                       -> true
```
This goes through `Arrangement_on_surface_2::_remove_vertex_if_redundant(DVertex* v, DFace* f)`
(`Arrangement_on_surface_2_impl.h:5203`), whose precondition is
```cpp
  CGAL_precondition((v->parameter_space_in_x() != ARR_INTERIOR) ||
                    (v->parameter_space_in_y() != ARR_INTERIOR));
```
and which, for `v->halfedge() == nullptr`, calls
`m_topol_traits.is_redundant(v)` →
```cpp
bool Arr_spherical_topology_traits_2<GeomTraits, Dcel>::
is_redundant(const Vertex* v) const { return (v->halfedge() == nullptr); }
```
and then
```cpp
Halfedge* Arr_spherical_topology_traits_2<GeomTraits, Dcel>::erase_redundant_vertex(Vertex* v)
{
  const Arr_parameter_space ps_y = v->parameter_space_in_y();
  if (ps_y == ARR_BOTTOM_BOUNDARY) { m_south_pole = nullptr; return nullptr; }
  if (ps_y == ARR_TOP_BOUNDARY)    { m_north_pole = nullptr; return nullptr; }
  CGAL_assertion(ps_x != ARR_INTERIOR);
  m_boundary_vertices.erase(v->point());
  return nullptr;
}
```
(*"The function does not free the vertex v itself"* — the arrangement frees it afterwards.)
Note that it always returns `nullptr`: there are no fictitious halfedges to merge on the sphere, so
`_remove_vertex_if_redundant`'s "merge two fictitious halfedges" path is dead here and
`before/after_merge_fictitious_edge` never fire (§10.7).

### 10.6.3 `remove_vertex` — the isolated-boundary-vertex trap (gotcha **G4**) **[verified]**

```cpp
Arr a(&traits);
auto v1 = CGAL::insert_point(a, cp(0,0,1));    // north pole, ISOLATED vertex
auto v2 = CGAL::insert_point(a, cp(-1,0,1));   // identification, ISOLATED
auto v3 = CGAL::insert_point(a, cp(1,2,3));    // interior, ISOLATED
// V=3 E=0 F=1 isolated=3 valid=Y npole=1 ; discontinuity_vertex((-1,0,1)) != nullptr
CGAL::remove_vertex(a, v1);  // returns true ; north_pole()               STILL non-null  <-- DANGLING
CGAL::remove_vertex(a, v2);  // returns true ; discontinuity_vertex(...)  STILL non-null  <-- DANGLING
CGAL::remove_vertex(a, v3);  // returns true
// V=0 E=0 F=1 valid=Y  but topology_traits()->north_pole() != nullptr
CGAL::insert(a, ccv(cp(1,0,2), cp(-1,0,2)));   // ---> SIGSEGV (exit 139)
```
`Arrangement_on_surface_2::remove_isolated_vertex` (`Arrangement_on_surface_2_impl.h:1480`) does
`p_f->erase_isolated_vertex(iv); _dcel().delete_isolated_vertex(iv); _delete_point(p_v->point());
_dcel().delete_vertex(p_v);` and never consults the topology traits.

**Binding rule:** before calling `remove_vertex` / `remove_isolated_vertex` on a vertex with
`parameter_space_in_x()/_y() != ARR_INTERIOR`, refuse, or repair the topology traits yourself
(`topology_traits()` is non-const, but `m_north_pole` etc. are private — there is no public setter, so
in practice: **never expose removal of an isolated boundary vertex**).

`remove_vertex` on a degree-2 vertex behaves as documented: `false` for two arcs on *different* great
circles (`Are_mergeable_2` is false), `true` (and the two edges are merged into one) for two co-circular
arcs, e.g. `(1,0,0)→(1,1,0)` and `(1,1,0)→(0,1,0)` on the equator **[both verified]**.

---

## 10.7 `Aos_observer` on a spherical arrangement

`Arr::Observer` is `CGAL::Aos_observer<Arr>` (`Arrangement_on_surface_2.h:112`,
`using Observer = Aos_observer<Self>;`). Boundary-relevant callbacks (verbatim, `Aos_observer.h`):

```cpp
  virtual void before_create_vertex(const Point_2& p) {}
  virtual void after_create_vertex(Vertex_handle v) {}

  /*! \param p The point on the surface boundary. */
  virtual void before_create_boundary_vertex(const Point_2& p,
                                             Arr_parameter_space ps_x,
                                             Arr_parameter_space ps_y) {}
  /*! \param cv The curve incident to the surface boundary. \param ind The relevant curve-end. */
  virtual void before_create_boundary_vertex(const X_monotone_curve_2& cv,
                                             Arr_curve_end ind,
                                             Arr_parameter_space ps_x,
                                             Arr_parameter_space ps_y) {}
  virtual void after_create_boundary_vertex(Vertex_handle v) {}

  virtual void before_split_fictitious_edge(Halfedge_handle e, Vertex_handle v) {}
  virtual void after_split_fictitious_edge (Halfedge_handle e1, Halfedge_handle e2) {}
  virtual void before_merge_fictitious_edge(Halfedge_handle e1, Halfedge_handle e2) {}
  virtual void after_merge_fictitious_edge (Halfedge_handle e) {}
```

### Verified firing pattern

Attaching an observer (constructed as `Obs(arr)`, which auto-attaches) and building the canonical
arrangement gave **[verified]**:

| callback | count |
|---|---|
| `before_create_vertex` / `after_create_vertex` | 9 / 9 |
| `before_create_boundary_vertex(const Point_2&, ps_x, ps_y)` | **1** |
| `before_create_boundary_vertex(const X_monotone_curve_2&, ind, ps_x, ps_y)` | **4** |
| `after_create_boundary_vertex` | **5** (= 1 + 4) |
| `before_split_edge` / `after_split_edge` | 3 / 3 |
| `before_split_face` / `after_split_face` | 3 / 3 |
| `before_add_inner_ccb` / `after_add_inner_ccb` | 4 / 4 |
| `before_split_fictitious_edge`, `after_split_fictitious_edge`, `before_merge_fictitious_edge`, `after_merge_fictitious_edge` | **0** |
| `before_add_outer_ccb`, `before_merge_face`, `before_remove_*`, `before_add_isolated_vertex` | 0 (nothing removed here) |

The five boundary vertices, in creation order, with their arguments:

```
bcbv(xcv) ce=ARR_MIN_END psx=0(LEFT) psy=4(INTERIOR)  ->  acbv (-1, 0,-1) loc=2 (identification)
bcbv(xcv) ce=ARR_MAX_END psx=0        psy=4           ->  acbv (-1, 0, 2) loc=2 (identification)
bcbv(point)  (-1,0,0)    psx=0        psy=4           ->  acbv (-1, 0, 0) loc=2 (identification)
bcbv(xcv) ce=ARR_MAX_END psx=4        psy=3(TOP)      ->  acbv ( 0, 0, 1) loc=3 (NORTH POLE)
bcbv(xcv) ce=ARR_MIN_END psx=4        psy=2(BOTTOM)   ->  acbv ( 0, 0,-1) loc=1 (SOUTH POLE)
```

**Both `before_create_boundary_vertex` overloads fire, and they come from different call sites:**

* the `(X_monotone_curve_2, Arr_curve_end, ps_x, ps_y)` overload from
  `Arrangement_on_surface_2::_place_and_set_curve_end` → `_create_boundary_vertex(cv, ind, ps_x, ps_y)`
  (`Arrangement_on_surface_2_impl.h:2244/2262`), and from
  `Arr_accessor::create_boundary_vertex(cv, ind, ps_x, ps_y)` used by
  `Arr_spherical_insertion_helper::before_handle_event_imp`;
* the `(Point_2, ps_x, ps_y)` overload from `Arrangement_on_surface_2::_split_edge(e, p, cv1, cv2)`
  (`...:3378`, when the split point is on the boundary — here (-1,0,0), where the equator crosses the
  identification), from `insert_in_face_interior(p, f)` for a boundary point (`...:297`), and from
  `Arr_spherical_construction_helper::before_handle_event` via
  `Arr_accessor::create_boundary_vertex(event->point(), ps_x, ps_y)`.

**No fictitious-edge callback ever fires** — confirmed. There are no fictitious halfedges on the
sphere (`Arr_spherical_topology_traits_2::is_valid_vertex()` is hard-coded `true`,
`number_of_valid_vertices() == m_dcel.size_of_vertices()`, and `erase_redundant_vertex` always returns
`nullptr` so `_remove_vertex_if_redundant`'s fictitious-merge path is unreachable).

**Also note:** `Arrangement_on_surface_2::_place_and_set_point(DFace*, const Point_2&, ps_x, ps_y)`
calls `m_topol_traits.place_boundary_vertex(f, p, ps_x, ps_y)`, an overload the spherical topology
traits **does not have** (it only declares the `(Face*, const X_monotone_curve_2&, Arr_curve_end, ...)`
one, returning `std::optional<std::variant<Vertex*, Halfedge*>>`). `_place_and_set_point` is dead code
— declared at `Arrangement_on_surface_2.h:2023`, defined at `...impl.h:2185`, **never called** — so this
does not break the build. Do not try to reach it from a binding.

`Arr_accessor` entry points, for reference:
```cpp
  Vertex_handle create_boundary_vertex(const Point_2& pt,
                                       Arr_parameter_space ps_x, Arr_parameter_space ps_y);
  Vertex_handle create_boundary_vertex(const X_monotone_curve_2& cv, Arr_curve_end ind,
                                       Arr_parameter_space ps_x, Arr_parameter_space ps_y);
```

---

## 10.8 `Arrangement_on_surface_with_history_2` on the sphere — completed end-to-end

(§8 sketched the type; this is the full, compiled *and run* story.)

```cpp
using Dcel   = CGAL::Arr_default_dcel<Gt>;
using Topol  = CGAL::Arr_spherical_topology_traits_2<Gt, Dcel>;
using ArrH   = CGAL::Arrangement_on_surface_with_history_2<Gt, Topol>;   // 2nd arg = TOPOLOGY traits

using ExtDcel = CGAL::Arr_extended_dcel<Gt, int, int, int>;
using TopolE  = CGAL::Arr_spherical_topology_traits_2<Gt, ExtDcel>;
using ArrHE   = CGAL::Arrangement_on_surface_with_history_2<Gt, TopolE>;
```

### Public API used (verbatim, `Arrangement_on_surface_with_history_2.h`)

```cpp
  typedef typename Curve_halfedges_list::iterator        Curve_iterator;
  typedef typename Curve_halfedges_list::const_iterator  Curve_const_iterator;
  typedef Curve_iterator                                 Curve_handle;
  typedef Curve_const_iterator                           Curve_const_handle;
  typedef typename Curve_halfedges::const_iterator       Induced_edge_iterator;
  class Originating_curve_iterator;   // convertible to Curve_iterator / Curve_const_iterator

  Arrangement_on_surface_with_history_2();
  Arrangement_on_surface_with_history_2(const Self& arr);
  Arrangement_on_surface_with_history_2(const Geometry_traits_2* tr);
  Self& operator=(const Self& arr);
  void assign(const Self& arr);
  virtual ~Arrangement_on_surface_with_history_2();
  virtual void clear();

  inline const Geometry_traits_2* geometry_traits() const;
  inline       Topology_traits*   topology_traits();
  inline const Topology_traits*   topology_traits() const;

  Size number_of_curves() const;
  Curve_iterator       curves_begin();        Curve_iterator       curves_end();
  Curve_const_iterator curves_begin() const;  Curve_const_iterator curves_end() const;

  Size number_of_originating_curves(Halfedge_const_handle e) const;   // e->curve().data().size()
  Originating_curve_iterator originating_curves_begin(Halfedge_const_handle e) const;
  Originating_curve_iterator originating_curves_end  (Halfedge_const_handle e) const;

  Size number_of_induced_edges(Curve_const_handle c) const;           // c->size()
  Induced_edge_iterator induced_edges_begin(Curve_const_handle c) const;
  Induced_edge_iterator induced_edges_end  (Curve_const_handle c) const;

  Halfedge_handle split_edge(Halfedge_handle e, const Point_2& p);
  Halfedge_handle merge_edge(Halfedge_handle e1, Halfedge_handle e2);
  bool are_mergeable(Halfedge_const_handle e1, Halfedge_const_handle e2) const;
```
Free functions:
```cpp
template <class GeomTraits, class TopTraits, class PointLocation>
typename Arrangement_on_surface_with_history_2<GeomTraits,TopTraits>::Curve_handle
insert(Arrangement_on_surface_with_history_2<GeomTraits,TopTraits>& arr,
       const typename GeomTraits::Curve_2& c, const PointLocation& pl);
template <class GeomTraits, class TopTraits>
typename Arrangement_on_surface_with_history_2<GeomTraits,TopTraits>::Curve_handle
insert(Arrangement_on_surface_with_history_2<GeomTraits,TopTraits>& arr,
       const typename GeomTraits::Curve_2& c);
template <class GeomTraits, class TopTraits, class InputIterator>
void insert(Arrangement_on_surface_with_history_2<GeomTraits,TopTraits>& arr,
            InputIterator begin, InputIterator end);
template <class GeomTraits, class TopTraits>
typename Arrangement_on_surface_with_history_2<GeomTraits,TopTraits>::Size
remove_curve(Arrangement_on_surface_with_history_2<GeomTraits,TopTraits>& arr,
             typename Arrangement_on_surface_with_history_2<GeomTraits,TopTraits>::Curve_handle ch);
template <class GeomTraits, class TopTraits1, class TopTraits2, class ResTopTraits,
          class OverlayTraits>
void overlay(const Arrangement_on_surface_with_history_2<GeomTraits, TopTraits1>& arr1,
             const Arrangement_on_surface_with_history_2<GeomTraits, TopTraits2>& arr2,
             Arrangement_on_surface_with_history_2<GeomTraits, ResTopTraits>& res,
             OverlayTraits& ovl_traits);
template <class GeomTraits, class TopTraits1, class TopTraits2, class ResTopTraits>
void overlay(const ... & arr1, const ... & arr2, ... & res);   // _Arr_default_overlay_traits_base
```
`remove_curve` returns the number of *removed edges* (`Size`); edges shared with another curve are not
removed, only their `data()` entry is erased.

### Verified end-to-end run **[verified]**

Building the canonical seven curves in an `ArrH` (again: the on-identification curve first, gotcha G1):

```
V=14 E=15 F=4 curves=7 valid=1 npole=1 spole=1                    // identical to the plain Arr
number_of_induced_edges:  identification-arc=2  triangle-edge=1  equator=4  north-pole-arc=2  south-pole-arc=2
number_of_originating_curves(e) == 1 for every edge (no two curves overlap here)

CGAL::remove_curve(arr, north_pole_curve) -> 2 edges removed
    V=12 E=13 F=4 curves=6 valid=1   north_pole() == nullptr          <- pole correctly released
CGAL::remove_curve(arr, south_pole_curve) -> 2 edges removed
    south_pole() == nullptr  valid=1
CGAL::remove_curve(arr, equator)          -> 4 edges removed
    V=8 E=7 F=2 valid=1
```
Duplicate-curve bookkeeping **[verified]**: inserting the *same* `Curve_2` twice gives
`E=1, number_of_curves()==2, number_of_originating_curves(first edge)==2, is_valid()==true`.

Extended DCEL + history **[verified]**: `ArrHE a(&traits)`, three inserts, `f->set_data(7)` on every
face, `f->data() == 7`, `is_valid()`.

Overlay of two *with-history* spherical arrangements **[verified]**:
```cpp
ArrHE a(&traits), b(&traits), res(&traits);
/* fill a and b with two crossing spherical triangles */
CGAL::overlay(a, b, res);
// res: V=10 E=14 F=6 curves=6 valid=1 ; number_of_originating_curves == 1 for all 14 edges
```
(`res._overlay(...)` detaches the internal `Curve_halfedges_observer`, runs `CGAL::overlay` on the base
arrangements, then duplicates and re-links the curves of both inputs.)

Reminder from §8: inside a `..._with_history_2` the effective geometry traits is
`Arr_consolidated_curve_data_traits_2<Gt, Gt::Curve_2*>`, so `Halfedge::curve()` returns a
data-carrying curve, while `Vertex::point()` is still `Arr_extended_direction_3<Kernel>`.

---

## 10.9 Point-location strategies on the sphere — the full compatibility table **[verified]**

| strategy | compiles? | runs? |
|---|---|---|
| `CGAL::Arr_naive_point_location<Arr>` | yes | yes — this is `Tt::Default_point_location_strategy` |
| `CGAL::Arr_landmarks_point_location<Arr>` | yes | yes (uses `Approximate_2`); `zone` with it matches naive |
| `CGAL::Arr_walk_along_line_point_location<Arr>` | **NO** | — `error: no member named 'initial_face' in Arr_spherical_topology_traits_2` (`Arr_walk_along_line_pl_impl.h:47`) and `no member named 'compare_xy'` (`...:513`) |
| `CGAL::Arr_trapezoid_ric_point_location<Arr>` | yes | **NO** — throws at construction/`locate`: `precondition violation! are_equal_end_points(Curve_end(cv,ARR_MIN_END), left_cv_end_node)` (`Trapezoidal_decomposition_2.h:1087`) |

`Tt::Default_vertical_ray_shooting_strategy` is also `Arr_naive_point_location<Arr>`, so
`CGAL::remove_vertex`'s internal `def_pl.ray_shoot_down(...)` and any global function that vertical-ray-shoots
uses naive PL on the sphere.

---

## 10.10 What does NOT work on the spherical topology (short list)

1. **`CGAL::decompose` (vertical decomposition)** — aborts (`Compare_x_2` precondition) whenever the
   arrangement has *any* pole vertex or *any* identification vertex. Usable only for arrangements
   entirely in the interior of the parameter space. No workaround.
2. **`CGAL::insert` (incremental / zone-based) of a curve lying ON the identification meridian**, when
   the arrangement already crosses that meridian — aborts (`Compare_xy_2` precondition, via
   `Arrangement_zone_2::_is_to_right_impl`). *Workaround:* aggregate
   `CGAL::insert(arr, first, last)` (order-independent), or insert such curves first.
3. **`CGAL::zone` / `CGAL::do_intersect` of a curve lying ON the identification meridian** — same
   abort, and there is no aggregate alternative.
4. **`CGAL::zone` / `CGAL::do_intersect` miss the incidence at the north pole** when the pole is the
   curve's *maximal* end (returns just the face / `false`). `insert` of the same curve is correct.
5. **`CGAL::locate` (batched PL) at the south pole** — silently returns the spherical face instead of
   the south-pole vertex.
6. **`CGAL::remove_vertex` / `remove_isolated_vertex` on an isolated boundary vertex** — corrupts the
   topology traits (dangling `north_pole()`/`south_pole()`/`m_boundary_vertices`); the next insertion
   segfaults. Never expose it.
7. **`Arr_walk_along_line_point_location`** — does not compile. **`Arr_trapezoid_ric_point_location`**
   — compiles, throws at runtime.
8. **`Arrangement_on_surface_2::fictitious_face()`** — does not compile (already noted in the header
   gotchas); there are no fictitious cells at all, and the four fictitious-edge observer callbacks are
   dead code.
9. `Arrangement_on_surface_2::_place_and_set_point` is dead code and would not compile if it were ever
   called (`place_boundary_vertex(Face*, const Point_2&, ...)` does not exist in the spherical
   topology traits).

Everything else in this section — overlay with a user overlay-traits and three different extended
DCELs (all ten `create_*`, isolated vertices, `is_valid()`), zone/`do_intersect` for ordinary curves,
batched PL away from the south pole, `remove_edge` (member and free), observers including both
`before_create_boundary_vertex` overloads, and the whole
`Arrangement_on_surface_with_history_2` surface (insert / `number_of_induced_edges` /
`number_of_originating_curves` / `remove_curve` / `overlay`) — **works and validates**.

---

## 10.11 Additions to the binding checklist (extends §9)

7. **Build spherical arrangements with the aggregate `CGAL::insert(arr, first, last)`** whenever you
   have more than one curve. It is order-independent and side-steps the identification-curve abort;
   the incremental overload is only safe if no curve *lies on* the identification meridian.
8. **Do not expose `decompose` for spherical arrangements** (or gate it behind "all vertices interior").
9. **Wrap batched `locate`**: post-process any query point with
   `Point_2::location() == MIN_BOUNDARY_LOC` and answer it from `topology_traits()->south_pole()`.
   Also remember results come back in sweep order, keyed by `Point_2`.
10. **Never expose removal of an isolated boundary vertex.** Only `remove_edge` maintains the topology
    traits' pole / identification bookkeeping.
11. **Overlay**: your overlay-traits' final `create_face(f1, f2, f)` call receives the two input
    spherical faces and the *result's* spherical face — this is the only opportunity to set that face's
    data. Detect it with `f->number_of_outer_ccbs() == 0`.
12. The overlay result arrangement is `clear()`ed at the start — invalidate all cached handles into it,
    and expect `before_clear`/`after_clear` on its observers.
13. Point location: expose `Arr_naive_point_location` (the default) and `Arr_landmarks_point_location`
    only. Refuse walk (won't compile) and trapezoid-RIC (throws).
14. `zone`, `do_intersect` and `insert` take the arrangement by **non-const reference**; `decompose`,
    `locate` and `overlay`'s inputs take it by **const reference**. Plan the Cython ownership/constness
    accordingly.
15. All CGAL precondition failures listed above arrive as thrown
    `CGAL::Precondition_exception` (derived from `std::exception`) — a Cython layer can and should
    catch them (`except +`) rather than letting them abort the interpreter. The one exception is the
    isolated-boundary-vertex removal, which is a **segfault**, not an exception — it must be prevented,
    not caught.
