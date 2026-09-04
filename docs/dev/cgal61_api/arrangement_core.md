# CGAL 6.1 — 2D Arrangement core classes: exact API map

Source of truth: the **installed** headers under `/opt/homebrew/include/CGAL`
(CGAL 6.1, `CGAL_VERSION_NR 1060101000`, header-only). Everything below was read
from the header text; facts marked **[verified]** were additionally confirmed by
compiling and running probe programs with

```
/usr/bin/clang++ -std=c++17 -O0 -DCGAL_USE_CORE -DCGAL_USE_GMP -DCGAL_USE_MPFR \
  -I/opt/homebrew/include -L/opt/homebrew/lib -lgmp -lmpfr -o test test.cpp
```

Files covered:

| File | Contents |
|---|---|
| `CGAL/Arrangement_on_surface_2.h` (3013 l.) | `Arrangement_on_surface_2`, nested `Vertex`/`Halfedge`/`Face`, all iterators, free-function declarations |
| `CGAL/Arrangement_2.h` (222 l.) | `Arrangement_2` (thin derived class) |
| `CGAL/Arrangement_2/Arrangement_2_iterators.h` (686 l.) | `I_Dereference_iterator`, `I_Filtered_iterator`, `I_Filtered_const_iterator`, `std::hash`/`boost::hash` specializations |
| `CGAL/Arrangement_2/Arrangement_on_surface_2_impl.h` (5600 l.) | definitions / precondition text / handle-validity semantics |
| `CGAL/Arrangement_2/Arr_default_planar_topology.h` (71 l.) | `Default_planar_topology` |
| `CGAL/Arr_enums.h` (213 l.) | `Arr_curve_end`, `Arr_halfedge_direction`, `Arr_boundary_type`, `Arr_parameter_space` |
| `CGAL/Arr_tags.h` (558 l.) | side-category tags and the meta-functions over them |
| supporting: `CGAL/Arr_dcel.h`, `CGAL/Arr_dcel_base.h`, `CGAL/Arr_default_dcel.h`, `CGAL/HalfedgeDS_iterator.h`, `CGAL/Aos_observer.h`, `CGAL/Arr_accessor.h`, `CGAL/Arr_point_location_result.h`, `CGAL/Arr_bounded_planar_topology_traits_2.h`, `CGAL/Arr_unb_planar_topology_traits_2.h` |

---

## Gotchas / surprises vs. older CGAL

1. **`Arrangement_2` is now a *derived* class of `Arrangement_on_surface_2`.**
   `Arrangement_2<GeomTraits, Dcel>` publicly derives from
   `Arrangement_on_surface_2<GeomTraits, typename Default_planar_topology<GeomTraits, Dcel>::Traits>`.
   Practically *all* of the API (`insert_in_face_interior`, iteration, `clear`,
   `is_valid`, `remove_edge`, …) lives on the base; `Arrangement_2` only adds
   `traits()`, `number_of_vertices_at_infinity()`, `unbounded_face()` and three
   constructors. Free functions are declared for
   `Arrangement_on_surface_2<GeomTraits, TopTraits>&`, so template deduction
   works on the base — an `Arrangement_2` binds fine.

2. **`Arr_observer<Arr>` is gone as a class; it is an alias.**
   `CGAL/Aos_observer.h` defines `template <class Arrangement_> class Aos_observer`.
   `Arrangement_on_surface_2` exposes `using Observer = Aos_observer<Self>;` and
   `using Base_aos = Self;`. `CGAL/Arr_observer.h` now only contains
   `template <typename Arrangement_> using Arr_observer = typename Arrangement_::Observer;`
   — i.e. you must derive from `MyArr::Observer` (or `Aos_observer<MyArr>`), and
   `Arr_observer<MyArr>` still works but is an alias, so you cannot forward-declare it.

3. **Point-location / zone results are `std::variant`, not `CGAL::Object`.**
   `Arr_point_location_result<Arr>::Type ==
   std::variant<Vertex_const_handle, Halfedge_const_handle, Face_const_handle>`
   (`CGAL/Arr_point_location_result.h`, `#include <variant>`;
   `empty_optional_result()` returns `std::optional<Type>`,
   `assign<T>(const Type*)` is `std::get_if<T>`). The `zone()` free function
   documents its output dereference type as “a variant that wraps a
   `Vertex_handle`, a `Halfedge_handle`, or a `Face_handle`”.

4. **A CCB circulator walks *fictitious* halfedges; the halfedge iterator does not.**
   **[verified]** With `Arr_linear_traits_2` and one line inserted:
   `halfedges_begin()…halfedges_end()` yields **2** halfedges, all non-fictitious;
   but every unbounded face has one outer CCB of length **4**, of which **3** are
   fictitious. Any CCB walk in binding code must test `h->is_fictitious()`
   before touching `h->curve()` (which `CGAL_precondition`s on a non-null curve).

5. **Vertices at infinity are invisible to `vertices_begin()/end()` but reachable
   through halfedges.** The vertex iterator is filtered by `_Is_concrete_vertex`
   (`topology_traits()->is_concrete_vertex(&v)` ⇒ `!v.has_null_point()`).
   **[verified]** a single line gives `number_of_vertices() == 0`,
   `number_of_vertices_at_infinity() == 2`, yet `he->source()` returns a valid
   `Vertex_handle` whose `point()` would fire an assertion. Always guard with
   `v->is_at_open_boundary()`.

6. **`fictitious_face()` is *not* fictitious under the bounded topology.**
   `fictitious_face()` returns `topology_traits()->initial_face()`. For
   `Arr_bounded_planar_topology_traits_2` that *is* the (single, real) unbounded
   face. **[verified]** for `Arr_segment_traits_2`: `unbounded_face() == fictitious_face()`
   and `fictitious_face()->is_fictitious() == false`. For
   `Arr_unb_planar_topology_traits_2` it is a genuinely fictitious face
   (`is_fictitious()==true`, `is_unbounded()==true`) that is *excluded* from
   `faces_begin()…faces_end()` and from `number_of_faces()`.

7. **`Dcel` is *not* rebound by `Arrangement_2`.** `Arrangement_2<GT, Dcel_>`
   passes `Dcel_` straight into `Default_planar_topology<GT, Dcel_>::Traits`,
   which stores it as-is. `Dcel::rebind<T>::other` is used only by
   `Arrangement_on_surface_with_history_2` and by `Arr_extended_dcel` /
   `Arr_dcel` internally (to re-parameterise `Arr_vertex_base<Point>` /
   `Arr_halfedge_base<Xcv>` for a different traits class). So if you hand
   `Arrangement_2<T1, Arr_default_dcel<T2>>` a mismatched `T2`, nothing corrects
   it for you.

8. **Handles are 3-word filtered iterators, and are constructible from a raw
   pointer.** `sizeof(Vertex_handle) == sizeof(Halfedge_handle) ==
   sizeof(Face_handle) == 24` **[verified]** (`nt`, `iend`, `filt`).
   `I_Filtered_iterator` has `template <typename T> I_Filtered_iterator(T* p)`,
   so `Vertex_handle(vptr)`, `Halfedge_handle(heptr)`, `Face_handle(fptr)` all
   compile and round-trip **[verified]** — but such a handle has
   `iend == nt` and a **default-constructed (null) filter**, so incrementing it
   will *not* skip fictitious/non-concrete records. Use pointer-built handles
   only as identities, never to iterate.

9. **`Arr_parameter_space` is a typedef, not its own enum.**
   `typedef Box_parameter_space_2 Arr_parameter_space;` with
   `const Arr_parameter_space ARR_LEFT_BOUNDARY = LEFT_BOUNDARY;` etc. Numeric
   values (from `CGAL/enum.h`) are `LEFT=0, RIGHT=1, BOTTOM=2, TOP=3, INTERIOR=4`.
   The `ARR_*` names are **`const` objects, not enumerators** — you cannot use
   them as `case` labels in a `switch` in C++ (`CGAL::LEFT_BOUNDARY` etc. work).

10. **`std::hash` and `operator<` exist for handles.** `Arrangement_2_iterators.h`
    specialises `std::hash` and `boost::hash` for both filtered iterator
    templates (`reinterpret_cast<std::size_t>(&*i) / sizeof(Value_)`), and
    `I_Filtered_iterator::operator<` compares `&**this < &*it`.
    **[verified]** handles work directly as `std::map` and `std::unordered_map`
    keys. Good news for bindings: you get stable identity without extra work.

11. `Arr_face_base::Hole` / `number_of_holes()` / `holes_begin()` / `holes_end()`
    still exist as **backward-compatible aliases** of the inner-CCB API on both
    the DCEL face and the arrangement `Face`; `Arrangement_2` re-exports
    `Hole_iterator` / `Hole_const_iterator`. They are *not* deprecated with an
    attribute — no compiler warning — so both spellings silently coexist.

12. `virtual ~Arrangement_on_surface_2()` and `virtual void clear()` are
    **virtual**; `Arr_dcel` has `virtual ~Arr_dcel()`. The DCEL base records
    (`Arr_vertex_base`, `Arr_halfedge_base`, `Arr_face_base`) each have a
    `virtual ~…()` and a `virtual void assign(...)`, hence the vptr in the
    measured record sizes (`Vertex` 48 B, `Halfedge` 72 B, `Face` 104 B with
    EPECK) **[verified]**.

13. `Arrangement_on_surface_2` has a **`bool m_sweep_mode`** member with
    `void set_sweep_mode(bool)` and `void clean_inner_ccbs_after_sweep()`. If you
    ever turn sweep mode on you *must* call the cleaner, otherwise invalid inner
    CCB records leak and `is_valid()` semantics get murky.

14. `assign()` / copy **shares the geometry traits pointer** when the source does
    not own its traits: `m_geom_traits = (arr.m_own_traits) ? new Traits_adaptor_2
    : arr.m_geom_traits;`. So `Arrangement_2(const Traits_2* tr)` does **not**
    take ownership, and any copy of that arrangement aliases the same traits
    object. The traits must outlive both.

15. A `static_assert(Arr_sane_identified_tagging<...>::value)` fires at class
    instantiation if opposite sides are inconsistently tagged as identified.

---

## 1. `CGAL/Arr_enums.h`

All in `namespace CGAL`.

```cpp
enum Arr_curve_end { ARR_MIN_END, ARR_MAX_END };
```
Selects the lexicographically smaller (`ARR_MIN_END`) or larger (`ARR_MAX_END`)
end of an x-monotone curve. Values `0`, `1`. `operator<<` prints
`"ARR_MIN_END"` / `"ARR_MAX_END"`.

```cpp
enum Arr_halfedge_direction { ARR_LEFT_TO_RIGHT = -1, ARR_RIGHT_TO_LEFT = 1 };
```
Verbatim header comment: *“Indicator whether a halfedge is directed from left to
right (from the xy-lexicographically smaller vertex to the larger one), or from
right to left.”* Note the **negative** value for `ARR_LEFT_TO_RIGHT` — do not
assume `0/1` in a binding. `operator<<` prints the names.

```cpp
enum Arr_boundary_type {
  ARR_OBLIVIOUS = 0, ARR_OPEN, ARR_CLOSED, ARR_CONTRACTION, ARR_IDENTIFICATION
};
```
Runtime counterpart of the compile-time side tags. Rarely needed directly.

```cpp
typedef Box_parameter_space_2 Arr_parameter_space;

const Arr_parameter_space ARR_LEFT_BOUNDARY   = LEFT_BOUNDARY;   // 0
const Arr_parameter_space ARR_RIGHT_BOUNDARY  = RIGHT_BOUNDARY;  // 1
const Arr_parameter_space ARR_BOTTOM_BOUNDARY = BOTTOM_BOUNDARY; // 2
const Arr_parameter_space ARR_TOP_BOUNDARY    = TOP_BOUNDARY;    // 3
const Arr_parameter_space ARR_INTERIOR        = INTERIOR;        // 4
```
`operator<<` is mode-sensitive (`IO::PRETTY` prints names, `IO::BINARY` prints a
“not yet implemented” note to `std::cerr`, otherwise the `int`);
`operator>>(InputStream&, Arr_parameter_space&)` reads an `int`
(`CGAL_precondition(CGAL::IO::is_ascii(is))`).

**Meaning for a vertex.** `v->parameter_space_in_x()` / `parameter_space_in_y()`
say *where on the parameter-space boundary the vertex lies*; both `ARR_INTERIOR`
means an ordinary finite vertex. **[verified]** with `Arr_linear_traits_2`:

| curve | min end (`source` of the L→R halfedge) | max end |
|---|---|---|
| horizontal line | `(LEFT, INTERIOR)` | `(RIGHT, INTERIOR)` |
| vertical line | `(INTERIOR, BOTTOM)` | `(INTERIOR, TOP)` |
| line of slope 1 | `(LEFT, BOTTOM)` | (symmetric) |

so **both** components can be non-interior simultaneously.

---

## 2. `CGAL/Arr_tags.h` — side categories

`namespace CGAL` (top level):

```cpp
struct Arr_boundary_side_tag {};
struct Arr_oblivious_side_tag      : public virtual Arr_boundary_side_tag {};
struct Arr_not_oblivious_side_tag  : public virtual Arr_boundary_side_tag {};
struct Arr_open_side_tag         : public virtual Arr_not_oblivious_side_tag {};
struct Arr_closed_side_tag       : public virtual Arr_not_oblivious_side_tag {};
struct Arr_contracted_side_tag   : public virtual Arr_not_oblivious_side_tag {};
struct Arr_identified_side_tag   : public virtual Arr_not_oblivious_side_tag {};
```

`BOOST_MPL_HAS_XXX_TRAIT_DEF(Left_side_category)` (and Bottom/Top/Right) gives
`CGAL::has_Left_side_category<T>` etc.

`namespace CGAL::internal` (lines 49–213) holds the *completion* helpers used by
`Default_planar_topology`:

```cpp
template <typename Traits_> struct Arr_complete_left_side_category   { typedef … Category; };
template <typename Traits_> struct Arr_complete_bottom_side_category { typedef … Category; };
template <typename Traits_> struct Arr_complete_top_side_category    { typedef … Category; };
template <typename Traits_> struct Arr_complete_right_side_category  { typedef … Category; };
```
Each yields `Traits_::X_side_category` if the traits declares it, otherwise
`Arr_oblivious_side_tag`. (There are matching `Validate_X_side_category<T,bool>`
classes whose only job is to emit the readable diagnostic
`missing__Left_side_category__assuming__Arr_oblivious_side_tag__instead` from the
`Arrangement_on_surface_2` constructors.)

Back in `namespace CGAL` (lines 213+):

```cpp
struct Arr_boundary_cond_tag{};
struct Arr_all_sides_oblivious_tag     : public virtual Arr_boundary_cond_tag{};
struct Arr_not_all_sides_oblivious_tag : public virtual Arr_boundary_cond_tag{};
struct Arr_has_identified_side_tag  : public virtual Arr_not_all_sides_oblivious_tag{};
struct Arr_has_contracted_side_tag  : public virtual Arr_not_all_sides_oblivious_tag{};
struct Arr_has_closed_side_tag      : public virtual Arr_not_all_sides_oblivious_tag{};
struct Arr_has_open_side_tag        : public virtual Arr_not_all_sides_oblivious_tag{};
struct Arr_all_sides_open_tag       : public virtual Arr_not_all_sides_oblivious_tag{};
struct Arr_all_sides_not_open_tag {};
struct Arr_not_all_sides_not_open_tag {};
struct Arr_all_sides_not_finite_tag     : public virtual Arr_not_all_sides_oblivious_tag {};
struct Arr_not_all_sides_not_finite_tag : public virtual Arr_not_all_sides_oblivious_tag {};

typedef std::true_type  Arr_true;
typedef std::false_type Arr_false;
```

Predicates (each has `::Side_cat`, `::Is_same`, `::result`, `::type`, plus a
`_v` variable template used in `Arr_sane_identified_tagging`):
`Arr_is_side_oblivious<C>`, `Arr_is_side_open<C>`, `Arr_is_side_identified<C>`,
`Arr_is_side_contracted<C>`, `Arr_is_side_closed<C>`.

**Key meta-function** (note the parameter order — Left, **Bottom, Top**, Right):

```cpp
template <typename ArrLeftSideCategory, typename ArrBottomSideCategory,
          typename ArrTopSideCategory,  typename ArrRightSideCategory>
struct Arr_all_sides_oblivious_category {
  // ::result == Arr_all_sides_oblivious_tag      if all four are Arr_oblivious_side_tag
  //          == Arr_not_all_sides_oblivious_tag  otherwise
};
```

Others with the same parameter order: `Arr_all_sides_not_open_category`,
`Arr_sides_category` (`::result` and a second nested category),
`Arr_sane_identified_tagging<L,B,T,R>` (`::result`, `static constexpr bool value`
— true iff left/right are both identified or both not, and likewise bottom/top),
`Arr_has_identified_sides<C1,C2>`, `Arr_has_contracted_sides_two<C1,C2>`,
`Arr_has_closed_sides_two<C1,C2>`, `Arr_has_open_sides_two<C1,C2>`, and

```cpp
template <typename ArrSideOneCategory, typename ArrSideTwoCategory>
struct Arr_two_sides_category {
  // ::result ==  Arr_has_identified_side_tag  if either side is identified,
  //          else Arr_has_contracted_side_tag if either is contracted,
  //          else Arr_has_closed_side_tag     if either is closed,
  //          else Arr_has_open_side_tag       if either is open,
  //          else Arr_all_sides_oblivious_tag
};
```

---

## 3. `CGAL/Arrangement_2/Arr_default_planar_topology.h`

```cpp
namespace CGAL::internal {
  template <typename GeomTraits, typename Dcel, typename Tag>
  struct Default_planar_topology_impl {};                 // primary: empty

  template <typename GeomTraits, typename Dcel>
  struct Default_planar_topology_impl<GeomTraits, Dcel, Arr_all_sides_oblivious_tag>
  { typedef Arr_bounded_planar_topology_traits_2<GeomTraits, Dcel> Traits; };

  template <typename GeomTraits, typename Dcel>
  struct Default_planar_topology_impl<GeomTraits, Dcel, Arr_not_all_sides_oblivious_tag>
  { typedef Arr_unb_planar_topology_traits_2<GeomTraits, Dcel> Traits; };
}

namespace CGAL {
  template <typename GeomTraits, typename Dcel>
  struct Default_planar_topology :
    public internal::Default_planar_topology_impl<
      GeomTraits, Dcel,
      typename Arr_all_sides_oblivious_category<
        typename internal::Arr_complete_left_side_category  <GeomTraits>::Category,
        typename internal::Arr_complete_bottom_side_category<GeomTraits>::Category,
        typename internal::Arr_complete_top_side_category   <GeomTraits>::Category,
        typename internal::Arr_complete_right_side_category <GeomTraits>::Category>::result
    > {};
}
```

**Selection rule.** Take the four side categories from the geometry traits
(missing ones default to `Arr_oblivious_side_tag`); if *all four* are
`Arr_oblivious_side_tag` ⇒ `Arr_bounded_planar_topology_traits_2`, otherwise ⇒
`Arr_unb_planar_topology_traits_2`. Note the dispatch tag is exactly
`Arr_all_sides_oblivious_tag` / `Arr_not_all_sides_oblivious_tag`; there is **no**
partial specialisation for anything else, so a traits class whose sides are
identified/contracted (sphere) makes `Default_planar_topology::Traits`
ill-formed — such traits must be used with `Arrangement_on_surface_2` directly.

**[verified]**
* `Arr_segment_traits_2<Epeck>` → `Arr_bounded_planar_topology_traits_2<STraits, Arr_default_dcel<STraits>>`
* `Arr_linear_traits_2<Epeck>` → `Arr_unb_planar_topology_traits_2<LTraits, Arr_default_dcel<LTraits>>`

`Arr_bounded_planar_topology_traits_2` `static_assert`s that all four categories
are `Arr_oblivious_side_tag`.

---

## 4. The DCEL layer (`Arr_dcel.h`, `Arr_dcel_base.h`, `Arr_default_dcel.h`)

### 4.1 `Arr_default_dcel`

```cpp
template <typename Traits> using Arr_default_dcel = Arr_dcel<Traits>;
```
(an **alias template** in CGAL 6.1 — you cannot forward-declare it as a class.)

### 4.2 `Arr_dcel` and `rebind`

```cpp
template <typename Traits,
          typename V = Arr_vertex_base  <typename Traits::Point_2>,
          typename H = Arr_halfedge_base<typename Traits::X_monotone_curve_2>,
          typename F = Arr_face_base>
class Arr_dcel : public Arr_dcel_base<V, H, F> {
public:
  template <typename T>
  struct rebind {
  private:
    using Pnt      = typename T::Point_2;
    using Xcv      = typename T::X_monotone_curve_2;
    using Rebind_v = typename V::template rebind<Pnt>;
    using V_other  = typename Rebind_v::other;
    using Rebind_h = typename H::template rebind<Xcv>;
    using H_other  = typename Rebind_h::other;
  public:
    using other = Arr_dcel<T, V_other, H_other, F>;
  };

  Arr_dcel() {}
  virtual ~Arr_dcel() {}
};
```

So `Dcel::rebind<NewTraits>::other` re-parameterises the vertex base on
`NewTraits::Point_2` and the halfedge base on `NewTraits::X_monotone_curve_2`,
keeping the face base and any user-added data (that is how `Arr_extended_dcel`
survives a rebind). The face base is **not** rebound.

Who calls it: `Arrangement_on_surface_with_history_2` does

```cpp
typename TopTraits_::template rebind<
   Arr_consolidated_curve_data_traits_2<GeomTraits_, typename GeomTraits_::Curve_2*>,
   typename TopTraits_::Dcel::template rebind<
     Arr_consolidated_curve_data_traits_2<GeomTraits_, typename GeomTraits_::Curve_2*> >::other
 >::other
```
and the topology traits themselves expose `template <typename T, typename D>
struct rebind { typedef Arr_bounded_planar_topology_traits_2<T, D> other; };`
(same shape for the unbounded one). **`Arrangement_2` itself never rebinds.**

### 4.3 Record base classes

```cpp
template <typename Point_> class Arr_vertex_base {
public:
  typedef Point_ Point;
  template<typename PNT> struct rebind { typedef Arr_vertex_base<PNT> other; };

  Arr_vertex_base();                                  // p_inc=nullptr, p_pt=nullptr, pss={INTERIOR,INTERIOR}
  virtual ~Arr_vertex_base();

  void* inc() const;
  void  set_inc(void* inc) const;                     // const! writes through const_cast

  bool  has_null_point() const;                       // blocked in Arrangement's Vertex
  const Point& point() const;                         // CGAL_assertion(p_pt != nullptr)
  Point&       point();
  void  set_point(Point* p);                          // blocked

  Arr_parameter_space parameter_space_in_x() const;   // == Arr_parameter_space(pss[0])
  Arr_parameter_space parameter_space_in_y() const;
  void  set_boundary(Arr_parameter_space ps_x, Arr_parameter_space ps_y);  // blocked

  virtual void assign(const Arr_vertex_base<Point>& v);
};
```
Layout: `void* p_inc; Point* p_pt; char pss[2];` — the **LSB of `p_inc` flags
“isolated”**, so never treat it as a pointer directly.

```cpp
template <typename X_monotone_curve_> class Arr_halfedge_base {
public:
  typedef X_monotone_curve_ X_monotone_curve;
  template<typename XCV> struct rebind { typedef Arr_halfedge_base<XCV> other; };

  Arr_halfedge_base();
  virtual ~Arr_halfedge_base();

  bool has_null_curve() const;                        // blocked in Arrangement's Halfedge
  const X_monotone_curve& curve() const;              // CGAL_precondition(p_cv != nullptr)
  X_monotone_curve&       curve();
  void set_curve(X_monotone_curve* c);                // blocked; also sets it on the opposite
  virtual void assign(const Arr_halfedge_base<X_monotone_curve>& he);
};
```
Layout: `void *p_opp, *p_prev, *p_next, *p_v, *p_comp; X_monotone_curve* p_cv;`
— **LSB of `p_v` encodes the direction**, **LSB of `p_comp` encodes
inner-vs-outer CCB**.

```cpp
class Arr_face_base {
public:
  typedef std::list<void*>                       Outer_ccbs_container;
  typedef Outer_ccbs_container::iterator         Outer_ccb_iterator;
  typedef Outer_ccbs_container::const_iterator   Outer_ccb_const_iterator;
  typedef std::list<void*>                       Inner_ccbs_container;
  typedef Inner_ccbs_container::iterator         Inner_ccb_iterator;
  typedef Inner_ccbs_container::const_iterator   Inner_ccb_const_iterator;
  typedef std::list<void*>                       Isolated_vertices_container;
  typedef Isolated_vertices_container::iterator       Isolated_vertex_iterator;
  typedef Isolated_vertices_container::const_iterator Isolated_vertex_const_iterator;

  Arr_face_base();  virtual ~Arr_face_base();
  bool is_unbounded()  const;      void set_unbounded(bool);   // setter blocked
  bool is_fictitious() const;      void set_fictitious(bool);  // setter blocked
  virtual void assign(const Arr_face_base& f);
};
```
Flags are `IS_UNBOUNDED = 1`, `IS_FICTITIOUS = 2` in an `int flags`.

### 4.4 DCEL records

```cpp
template <class V, class H, class F>
class Arr_vertex : public V, public In_place_list_base<Arr_vertex<V,H,F>> {
public:
  typedef V Base;  typedef Arr_vertex<V,H,F> Vertex;
  typedef Arr_halfedge<V,H,F> Halfedge;  typedef Arr_isolated_vertex<V,H,F> Isolated_vertex;

  bool is_isolated() const;                        // LSB of p_inc
  const Halfedge* halfedge() const;  Halfedge* halfedge();      // pre: !is_isolated()  [blocked upstream]
  void set_halfedge(Halfedge*);                                  // blocked
  const Isolated_vertex* isolated_vertex() const;  Isolated_vertex* isolated_vertex(); // pre: is_isolated() [blocked]
  void set_isolated_vertex(Isolated_vertex*);                    // blocked
};
```

```cpp
template <class V, class H, class F>
class Arr_halfedge : public H, public In_place_list_base<Arr_halfedge<V,H,F>> {
public:
  typedef Arr_vertex<V,H,F> Vertex;  typedef Arr_halfedge<V,H,F> Halfedge;
  typedef Arr_face<V,H,F> Face;  typedef Arr_outer_ccb<V,H,F> Outer_ccb;
  typedef Arr_inner_ccb<V,H,F> Inner_ccb;

  const Halfedge* opposite() const;  Halfedge* opposite();       // blocked
  void set_opposite(Halfedge*);                                  // blocked
  Arr_halfedge_direction direction() const;                      // *** public on Arrangement's Halfedge ***
  void set_direction(Arr_halfedge_direction);                    // blocked
  const Halfedge* prev() const;  Halfedge* prev();               // blocked
  void set_prev(Halfedge*);                                      // blocked (also fixes he->p_next)
  const Halfedge* next() const;  Halfedge* next();               // blocked
  void set_next(Halfedge*);                                      // blocked
  const Vertex* vertex() const;  Vertex* vertex();               // blocked (target)
  void set_vertex(Vertex*);                                      // blocked
  bool is_on_outer_ccb() const;                                  // *** public on Arrangement's Halfedge ***
  const Outer_ccb* outer_ccb() const;  Outer_ccb* outer_ccb();   // blocked; pre: !is_on_inner_ccb()
  void set_outer_ccb(Outer_ccb*);                                // blocked
  bool is_on_inner_ccb() const;                                  // *** public on Arrangement's Halfedge ***
  const Inner_ccb* inner_ccb() const;  Inner_ccb* inner_ccb();   // blocked; pre: is_on_inner_ccb();
                                                                 //   NOTE: path-compresses invalid inner CCBs (mutates through const)
  Inner_ccb* inner_ccb_no_redirect();
  void set_inner_ccb(const Inner_ccb*);                          // blocked
};
```

```cpp
template <class V, class H, class F>
class Arr_face : public F, public In_place_list_base<Arr_face<V,H,F>> {
public:
  typedef Arr_outer_ccb<V,H,F> Outer_ccb;  typedef Arr_inner_ccb<V,H,F> Inner_ccb;
  typedef Arr_isolated_vertex<V,H,F> Isolated_vertex;  typedef Inner_ccb Hole;

  typedef Iterator_project<typename F::Outer_ccb_iterator,       _Ccb_to_halfedge_cast> Outer_ccb_iterator;
  typedef Iterator_project<typename F::Outer_ccb_const_iterator, _Ccb_to_halfedge_cast> Outer_ccb_const_iterator;
  typedef Iterator_project<typename F::Inner_ccb_iterator,       _Ccb_to_halfedge_cast> Inner_ccb_iterator;
  typedef Iterator_project<typename F::Inner_ccb_const_iterator, _Ccb_to_halfedge_cast> Inner_ccb_const_iterator;
  typedef Inner_ccb_iterator       Hole_iterator;
  typedef Inner_ccb_const_iterator Hole_const_iterator;
  typedef I_Dereference_iterator<...>       Isolated_vertex_iterator;
  typedef I_Dereference_const_iterator<...> Isolated_vertex_const_iterator;

  std::size_t number_of_outer_ccbs() const;   // *** public on Arrangement's Face ***
  std::size_t number_of_inner_ccbs() const;   // *** public ***
  std::size_t number_of_holes() const;        // == number_of_inner_ccbs()
  std::size_t number_of_isolated_vertices() const;  // *** public ***
  Outer_ccb_iterator outer_ccbs_begin()/end();  (+const)   // shadowed by Arrangement's Face
  Inner_ccb_iterator inner_ccbs_begin()/end();  (+const)   // shadowed
  Hole_iterator      holes_begin()/holes_end(); (+const)   // shadowed
  Isolated_vertex_iterator isolated_vertices_begin()/end(); (+const)  // shadowed
  void add_outer_ccb(Outer_ccb*, Halfedge*);   void erase_outer_ccb(Outer_ccb*);   // blocked
  void add_inner_ccb(Inner_ccb*, Halfedge*);   void erase_inner_ccb(Inner_ccb*);   // blocked
  void add_isolated_vertex(Isolated_vertex*, Vertex*);  void erase_isolated_vertex(Isolated_vertex*); // blocked
  Inner_ccb_iterator       splice_inner_ccbs(Arr_face& other);        // internal
  Isolated_vertex_iterator splice_isolated_vertices(Arr_face& other); // internal
};
```

### 4.5 `Arr_dcel_base<V,H,F,Allocator = CGAL_ALLOCATOR(int)>` — public surface

```cpp
typedef Arr_vertex<V,H,F>          Vertex;
typedef Arr_halfedge<V,H,F>        Halfedge;
typedef Arr_face<V,H,F>            Face;
typedef Arr_outer_ccb<V,H,F>       Outer_ccb;
typedef Arr_inner_ccb<V,H,F>       Inner_ccb;
typedef Arr_isolated_vertex<V,H,F> Isolated_vertex;
typedef Inner_ccb                  Hole;

typedef typename Halfedge_list::size_type       Size;        // == std::size_t
typedef typename Halfedge_list::size_type       size_type;
typedef typename Halfedge_list::difference_type difference_type;
typedef typename Halfedge_list::difference_type Difference;
typedef std::bidirectional_iterator_tag         iterator_category;

typedef typename Vertex_list::iterator            Vertex_iterator;      // In_place_list iterators
typedef typename Halfedge_list::iterator          Halfedge_iterator;
typedef typename Face_list::iterator              Face_iterator;
typedef CGAL::N_step_adaptor_derived<Halfedge_iterator, 2> Edge_iterator;
typedef typename Inner_ccb_list::iterator         Inner_ccb_iterator;
// … plus the four *_const_iterator counterparts

Arr_dcel_base();  ~Arr_dcel_base();      // copy ctor & operator= are private/undefined

Size size_of_vertices() const;  Size size_of_halfedges() const;  Size size_of_faces() const;
Size size_of_outer_ccbs() const;  Size size_of_inner_ccbs() const;  Size size_of_isolated_vertices() const;

Vertex_iterator vertices_begin()/vertices_end();     Iterator_range<Prevent_deref<...>> vertex_handles();
Halfedge_iterator halfedges_begin()/halfedges_end(); Iterator_range<...> halfedge_handles();
Face_iterator faces_begin()/faces_end();             Iterator_range<...> face_handles();
Edge_iterator edges_begin()/edges_end();             Iterator_range<...> edge_handles();
Inner_ccb_iterator inner_ccbs_begin()/inner_ccbs_end();
// (+ all const overloads)
// plus new_vertex/new_edge/new_face/new_outer_ccb/new_inner_ccb/new_isolated_vertex,
//      delete_vertex/delete_edge/delete_face/…/delete_all(), assign(), used internally
```

Because the containers are `In_place_list`s, **a handle stays valid for the whole
lifetime of the record**; adding or removing *other* records never invalidates it.

---

## 5. Handles, iterators, circulators

### 5.1 `I_Filtered_iterator<Iterator_, Filter_, Value_, Diff_, Category_>`
(`Arrangement_2/Arrangement_2_iterators.h`)

```cpp
typedef Iterator_ Iterator;  typedef Filter_ Filter;
typedef Category_ iterator_category;   typedef Value_ value_type;
typedef value_type& reference;  typedef value_type* pointer;  typedef Diff_ difference_type;

I_Filtered_iterator();
I_Filtered_iterator(Iterator it);                     // iend = nt  (single-element view)
template <typename T> I_Filtered_iterator(T* p);      // *** handle from raw pointer *** iend = nt
I_Filtered_iterator(Iterator it, Iterator end);       // advances past filtered-out elements
I_Filtered_iterator(Iterator it, Iterator end, Filter f);
template <typename P> I_Filtered_iterator& operator=(const P* p);

Iterator current_iterator() const;
Iterator past_the_end()     const;
Filter   filter()           const;
pointer  ptr()              const;   // static_cast<pointer>(&(*nt))

bool operator==(const Self&) const;
bool operator!=(const Self&) const;
bool operator< (const Self& it) const;   // &(**this) < (&*it)   ← address ordering
reference operator*()  const;
pointer   operator->() const;
Self& operator++();  Self operator++(int);
Self& operator--();  Self operator--(int);
```
`operator++`/`--` skip elements for which `filt(*nt)` is false, stopping at `iend`.

`I_Filtered_const_iterator<CIterator_, Filter_, MIterator_, Value_, Diff_, Category_>`
adds `typedef I_Filtered_iterator<MIterator_,…> mutable_iterator;` and a
converting constructor from it, so **`Xxx_handle` → `Xxx_const_handle` is implicit**.
Its `reference`/`pointer` are `const value_type&` / `const value_type*`.

`I_Dereference_iterator<Iterator_, Value_, Diff_, Category_>` and
`I_Dereference_const_iterator<CIterator_, MIterator_, Value_, Diff_, Category_>`
(used for isolated vertices) have `current_iterator()`, `ptr()`, `operator*`,
`operator->`, `==`, `!=`, `++`, `--`, and `ptr()` does
`static_cast<value_type*>(*iter)`.

**Hashing.** `namespace std` and `namespace boost` both get
```cpp
template <…> struct hash<CGAL::I_Filtered_iterator<…>>       { std::size_t operator()(const I& i) const; };
template <…> struct hash<CGAL::I_Filtered_const_iterator<…>> { std::size_t operator()(const I& i) const; };
```
implemented as `reinterpret_cast<std::size_t>(&*i) / sizeof(value_type)`.

### 5.2 Circulators (`CGAL/HalfedgeDS_iterator.h`)

Both circulator families **derive from the halfedge iterator/handle type**, so a
circulator converts implicitly to `Halfedge_handle` / `Halfedge_const_handle`
**[verified]**.

```cpp
template <class Node, class It, class Ctg> class _HalfedgeDS_vertex_circ : public It {
  typedef It Base;  typedef Ctg iterator_category;  typedef Node value_type;
  typedef std::ptrdiff_t difference_type;  typedef std::size_t size_type;
  typedef value_type& reference;  typedef value_type* pointer;

  _HalfedgeDS_vertex_circ();               // It(nullptr)
  _HalfedgeDS_vertex_circ(It i);
  pointer ptr() const;
  bool operator==(std::nullptr_t) const;   bool operator!=(std::nullptr_t) const;
  bool operator==(const It&) const;  bool operator==(const Self&) const;  bool operator!=(const Self&) const;
  Self& operator++();   //  nt = (*nt).next()->opposite()      ← clockwise around the target vertex
  Self  operator++(int);
  Self& operator--();   //  nt = (*nt).opposite()->prev()
  Self  operator--(int);
};

template <class Node, class It, class Ctg> class _HalfedgeDS_facet_circ : public It {
  // same shape; ++ is  nt = (*nt).next() ;  -- is  nt = (*nt).prev()
};
```

⇒ `Halfedge_around_vertex_circulator` visits halfedges whose **target** is the
vertex, in **clockwise** order **[verified]**; `Ccb_halfedge_circulator` follows
`next()` around a CCB (counter-clockwise for an outer CCB, clockwise for an
inner one, in the usual DCEL convention).

---

## 6. `CGAL::Arrangement_on_surface_2<GeomTraits_, TopTraits_>`

`#include <CGAL/Arrangement_on_surface_2.h>`. No default template arguments.

### 6.1 Public typedefs

```cpp
typedef GeomTraits_ Geometry_traits_2;
typedef TopTraits_  Topology_traits;
typedef CGAL_ALLOCATOR(int) Allocator;

typedef Arr_traits_basic_adaptor_2<Geometry_traits_2> Traits_adaptor_2;   // derives from Geometry_traits_2
typedef typename Traits_adaptor_2::Left_side_category   Left_side_category;
typedef typename Traits_adaptor_2::Bottom_side_category Bottom_side_category;
typedef typename Traits_adaptor_2::Top_side_category    Top_side_category;
typedef typename Traits_adaptor_2::Right_side_category  Right_side_category;
// static_assert(Arr_sane_identified_tagging<L,B,T,R>::value);

typedef Arrangement_on_surface_2<Geometry_traits_2, Topology_traits> Self;
typedef typename Geometry_traits_2::Point_2            Point_2;
typedef typename Geometry_traits_2::X_monotone_curve_2 X_monotone_curve_2;

typedef typename Arr_all_sides_oblivious_category<L,B,T,R>::result   Are_all_sides_oblivious_category;
typedef typename Arr_has_identified_sides<L,B>::result               Has_identified_sides;
typedef typename Arr_two_sides_category<B,T>::result                 Top_or_bottom_sides_category;

typedef typename Topology_traits::Dcel Dcel;
typedef typename Dcel::Size            Size;          // std::size_t

using Observer = Aos_observer<Self>;
using Base_aos = Self;
```

Protected (but visible to `Arr_accessor`, which is a `friend`; useful to know
when writing a C++ core):
`DVertex`, `DHalfedge`, `DFace`, `DOuter_ccb`, `DInner_ccb`, `DIso_vertex`,
`DDifference`, `DIterator_category`, `DVertex_iter`, `DVertex_const_iter`,
`DHalfedge_iter`, `DHalfedge_const_iter`, `DEdge_iter`, `DEdge_const_iter`,
`DFace_iter`, `DFace_const_iter`, `DOuter_ccb_iter(+const)`,
`DInner_ccb_iter(+const)`, `DIso_vertex_iter(+const)`.

Iterator / handle types (all public):

```cpp
class Vertex;  class Halfedge;  class Face;                 // forward-declared, defined below

typedef I_Filtered_iterator      <DVertex_iter,       _Is_concrete_vertex, Vertex,   DDifference, DIterator_category> Vertex_iterator;
typedef I_Filtered_const_iterator<DVertex_const_iter, _Is_concrete_vertex, DVertex_iter, Vertex, DDifference, DIterator_category> Vertex_const_iterator;
typedef I_Filtered_iterator      <DHalfedge_iter,       _Is_valid_halfedge, Halfedge, DDifference, DIterator_category> Halfedge_iterator;
typedef I_Filtered_const_iterator<DHalfedge_const_iter, _Is_valid_halfedge, DHalfedge_iter, Halfedge, DDifference, DIterator_category> Halfedge_const_iterator;

class Edge_iterator       : public I_Filtered_iterator<DEdge_iter, _Is_valid_halfedge, Halfedge, …> {
  Edge_iterator();  Edge_iterator(DEdge_iter, DEdge_iter, const _Is_valid_halfedge&);  Edge_iterator(const Base&);
  operator Halfedge_iterator() const;  operator Halfedge_const_iterator() const;
};
class Edge_const_iterator : public I_Filtered_const_iterator<DEdge_const_iter, _Is_valid_halfedge, DEdge_iter, Halfedge, …> {
  Edge_const_iterator();  Edge_const_iterator(Edge_iterator);
  Edge_const_iterator(DEdge_const_iter, DEdge_const_iter, const _Is_valid_halfedge&);  Edge_const_iterator(const Base&);
  operator Halfedge_const_iterator() const;
};

typedef I_Filtered_iterator      <DFace_iter,       _Is_valid_face, Face, DDifference, DIterator_category> Face_iterator;
typedef I_Filtered_const_iterator<DFace_const_iter, _Is_valid_face, DFace_iter, Face, DDifference, DIterator_category> Face_const_iterator;

typedef _HalfedgeDS_vertex_circ      <Halfedge, Halfedge_iterator,       Bidirectional_circulator_tag> Halfedge_around_vertex_circulator;
typedef _HalfedgeDS_vertex_const_circ<Halfedge, Halfedge_const_iterator, Bidirectional_circulator_tag> Halfedge_around_vertex_const_circulator;
typedef _HalfedgeDS_facet_circ       <Halfedge, Halfedge_iterator,       Bidirectional_circulator_tag> Ccb_halfedge_circulator;
typedef _HalfedgeDS_facet_const_circ <Halfedge, Halfedge_const_iterator, Bidirectional_circulator_tag> Ccb_halfedge_const_circulator;

class Unbounded_face_iterator       : public I_Filtered_iterator<DFace_iter, _Is_unbounded_face, Face, …> {
  Unbounded_face_iterator();  Unbounded_face_iterator(DFace_iter, DFace_iter, const _Is_unbounded_face&);
  operator Face_iterator() const;  operator Face_const_iterator() const;
};
class Unbounded_face_const_iterator : public I_Filtered_const_iterator<DFace_const_iter, _Is_unbounded_face, DFace_iter, Face, …> {
  Unbounded_face_const_iterator();  Unbounded_face_const_iterator(Unbounded_face_iterator);
  Unbounded_face_const_iterator(DFace_const_iter, DFace_const_iter, const _Is_unbounded_face&);
  Unbounded_face_const_iterator(const Base&);
  operator Face_const_iterator() const;
};

typedef Iterator_transform<DOuter_ccb_iter,       _Halfedge_to_ccb_circulator>       Outer_ccb_iterator;        // *it → Ccb_halfedge_circulator
typedef Iterator_transform<DOuter_ccb_const_iter, _Const_halfedge_to_ccb_circulator> Outer_ccb_const_iterator;  // *it → Ccb_halfedge_const_circulator
typedef Iterator_transform<DInner_ccb_iter,       _Halfedge_to_ccb_circulator>       Inner_ccb_iterator;
typedef Iterator_transform<DInner_ccb_const_iter, _Const_halfedge_to_ccb_circulator> Inner_ccb_const_iterator;

class Isolated_vertex_iterator       : public Iterator_project<DIso_vertex_iter, _Vertex_to_vertex> {
  Isolated_vertex_iterator();  Isolated_vertex_iterator(DIso_vertex_iter);
  operator Vertex_iterator() const;  operator Vertex_const_iterator() const;
};
class Isolated_vertex_const_iterator : public Iterator_project<DIso_vertex_const_iter, _Vertex_to_vertex> {
  Isolated_vertex_const_iterator();  Isolated_vertex_const_iterator(Isolated_vertex_iterator);
  Isolated_vertex_const_iterator(DIso_vertex_const_iter);
  operator Vertex_const_iterator() const;
};

// Handles ARE iterators:
typedef Vertex_iterator         Vertex_handle;
typedef Halfedge_iterator       Halfedge_handle;
typedef Face_iterator           Face_handle;
typedef Vertex_const_iterator   Vertex_const_handle;
typedef Halfedge_const_iterator Halfedge_const_handle;
typedef Face_const_iterator     Face_const_handle;
```

The four filter functors (protected nested classes, each holding a
`const Topology_traits*` and returning `true` when the pointer is null):
`_Is_concrete_vertex` (→ `is_concrete_vertex`), `_Is_valid_vertex`
(→ `is_valid_vertex`), `_Is_valid_halfedge` (→ `is_valid_halfedge`),
`_Is_valid_face` (→ `is_valid_face`), `_Is_unbounded_face`
(→ `is_valid_face && is_unbounded`, plus a public `topology_traits()` accessor).
There is also a protected `_Valid_vertex_iterator` (filtered by
`_Is_valid_vertex`) with conversions to `Vertex_iterator` /
`Vertex_const_iterator`.

### 6.2 `class Vertex : public DVertex`

```cpp
Vertex();

bool is_at_open_boundary() const;              // == DVertex::has_null_point()
Size degree() const;                           // 0 if is_isolated(); else walks he->next()->opposite()

Halfedge_around_vertex_circulator       incident_halfedges();        // \pre ! is_isolated()
Halfedge_around_vertex_const_circulator incident_halfedges() const;  // \pre ! is_isolated()

Face_handle       face();                      // \pre is_isolated()
Face_const_handle face() const;                // \pre is_isolated()
```

Inherited and still **public**: `bool is_isolated() const`,
`const Point& point() const`, `Point& point()`,
`Arr_parameter_space parameter_space_in_x() const`,
`Arr_parameter_space parameter_space_in_y() const`, `void* inc() const`,
`void set_inc(void*) const`.

Made **private (blocked)**: `has_null_point()`, `set_point(Point_2*)`,
`set_boundary(Arr_parameter_space, Arr_parameter_space)`, `halfedge()` (both),
`set_halfedge(DHalfedge*)`, `isolated_vertex()` (both),
`set_isolated_vertex(DIso_vertex*)`.

Notes:
* `point()` asserts `p_pt != nullptr` — **always test `is_at_open_boundary()` first**
  (it is the public spelling of `has_null_point()`).
* `degree()` is **O(deg)**, not cached.
* `degree() == 0` does **not** imply `is_isolated()`: a vertex can have a null
  incident halfedge without an isolated-vertex record during construction; the
  insertion functions test `v->degree() == 0` and then `v->is_isolated()`
  separately. `face()` requires the *record*, i.e. `is_isolated()`.
* A vertex at infinity is not isolated and has a real degree — **[verified]** the
  “left” vertex of a horizontal line under `Arr_linear_traits_2` has
  `degree() == 3` (one real + two fictitious halfedges).

### 6.3 `class Halfedge : public DHalfedge`

```cpp
Halfedge();

bool is_fictitious() const;                    // == DHalfedge::has_null_curve()

Vertex_handle       source();                  //  opposite()->vertex()
Vertex_const_handle source() const;
Vertex_handle       target();                  //  vertex()
Vertex_const_handle target() const;

Face_handle       face();                      //  outer_ccb()->face()  or  inner_ccb()->face()
Face_const_handle face() const;

Halfedge_handle       twin();       Halfedge_const_handle       twin() const;
Halfedge_handle       prev();       Halfedge_const_handle       prev() const;
Halfedge_handle       next();       Halfedge_const_handle       next() const;

Ccb_halfedge_circulator       ccb();
Ccb_halfedge_const_circulator ccb() const;
```

Inherited and still **public**: `Arr_halfedge_direction direction() const`,
`bool is_on_outer_ccb() const`, `bool is_on_inner_ccb() const`,
`const X_monotone_curve& curve() const`, `X_monotone_curve& curve()`,
`Inner_ccb* inner_ccb_no_redirect()`.

Made **private (blocked)**: `has_null_curve()`, `set_curve(X_monotone_curve_2*)`,
`opposite()` (both), `set_opposite`, `set_direction`, `set_prev`, `set_next`,
`vertex()` (both), `set_vertex`, `outer_ccb()` (both), `set_outer_ccb`,
`inner_ccb()` (both), `set_inner_ccb`.

Notes:
* `curve()` `CGAL_precondition`s on a non-null curve. **Guard with `is_fictitious()`.**
* `curve()` returns a **non-const reference** on a non-const `Halfedge`; the
  arrangement’s own `merge_edge` uses `he1->curve() = cv`. Do **not** mutate it
  from binding code — geometric invariants are not re-checked.
* `direction()` is derived from the LSB of the target pointer, so it is exact and
  cheap; `twin()->direction()` is always the opposite value.
* `ccb()` returns a circulator seeded at `this`; combined with the implicit
  circulator→handle conversion, `Halfedge_handle(h->ccb()) == h` **[verified]**.

### 6.4 `class Face : public DFace`

```cpp
Face();

Outer_ccb_iterator       outer_ccbs_begin();          Outer_ccb_const_iterator       outer_ccbs_begin() const;
Outer_ccb_iterator       outer_ccbs_end();            Outer_ccb_const_iterator       outer_ccbs_end()   const;
Inner_ccb_iterator       inner_ccbs_begin();          Inner_ccb_const_iterator       inner_ccbs_begin() const;
Inner_ccb_iterator       inner_ccbs_end();            Inner_ccb_const_iterator       inner_ccbs_end()   const;
Isolated_vertex_iterator isolated_vertices_begin();   Isolated_vertex_const_iterator isolated_vertices_begin() const;
Isolated_vertex_iterator isolated_vertices_end();     Isolated_vertex_const_iterator isolated_vertices_end()   const;

// "kept for Arrangement_2 compatibility":
bool has_outer_ccb() const;                     // number_of_outer_ccbs() > 0
Ccb_halfedge_circulator       outer_ccb();      // \pre number_of_outer_ccbs() == 1
Ccb_halfedge_const_circulator outer_ccb() const;// \pre number_of_outer_ccbs() == 1
Size number_of_holes() const;                   // == number_of_inner_ccbs()
Inner_ccb_iterator       holes_begin();         Inner_ccb_const_iterator       holes_begin() const;
Inner_ccb_iterator       holes_end();           Inner_ccb_const_iterator       holes_end()   const;
```

Inherited and still **public**: `bool is_unbounded() const`,
`bool is_fictitious() const`, `std::size_t number_of_outer_ccbs() const`,
`std::size_t number_of_inner_ccbs() const`,
`std::size_t number_of_isolated_vertices() const`.

Made **private (blocked)**: `set_unbounded(bool)`, `set_fictitious(bool)`,
`add_outer_ccb`, `erase_outer_ccb`, `add_inner_ccb`, `erase_inner_ccb`,
`add_isolated_vertex`, `erase_isolated_vertex`.

Usage: `*outer_ccbs_begin()` yields a `Ccb_halfedge_circulator`
(`Iterator_transform`), likewise for inner CCBs; `*isolated_vertices_begin()`
yields a `Vertex&`, and the iterator converts to `Vertex_handle` **[verified]**.

⚠ **`number_of_holes()` returns `Size` on the arrangement `Face` but
`std::size_t` on the DCEL face; `number_of_outer_ccbs()`/`number_of_inner_ccbs()`
are only available via the DCEL base and return `std::size_t`.** They are the
same type in practice (`Size == std::size_t`) but write the type out explicitly
in binding glue.

### 6.5 Data members (protected)

```cpp
Topology_traits         m_topol_traits;     // BY VALUE — the DCEL lives inside the arrangement
Points_alloc            m_points_alloc;     // CGAL_ALLOCATOR(Point_2)
Curves_alloc            m_curves_alloc;     // CGAL_ALLOCATOR(X_monotone_curve_2)
Observers_container     m_observers;        // std::list<Observer*>
const Traits_adaptor_2* m_geom_traits;
bool                    m_own_traits;
bool                    m_sweep_mode = false;
```
**[verified]** `sizeof(Arrangement_2<Arr_segment_traits_2<Epeck>>) == 248`.
Because `m_topol_traits` (and its `Dcel`) is a by-value member,
**moving/copying the arrangement object itself moves the DCEL container objects**,
but the individual records live in `In_place_list` nodes on the heap.
There is **no move constructor / move assignment** — copies are deep and O(n).

### 6.6 Constructors / assignment / destruction

```cpp
Arrangement_on_surface_2();                                   // allocates its own Traits_adaptor_2 (m_own_traits = true)
Arrangement_on_surface_2(const Self& arr);                    // deep copy, delegates to assign()
Arrangement_on_surface_2(const Geometry_traits_2* geom_traits); // does NOT take ownership

Self& operator=(const Self& arr);                             // self-assign safe; calls assign()
void  assign(const Self& arr);                                // clear(), copy the DCEL, DEEP-COPY every point and curve

virtual ~Arrangement_on_surface_2();                          // frees points/curves, frees traits if owned, detaches all observers
void set_sweep_mode(bool mode);
virtual void clear();                                         // frees all points/curves, delete_all(), init_dcel()
```

Ownership / lifetime facts read from `Arrangement_on_surface_2_impl.h`:
* `assign()` duplicates every `Point_2` and every `X_monotone_curve_2` through
  the member allocators, so a copy owns its geometry. **All handles into the
  source arrangement are meaningless in the copy** — **[verified]**, the raw
  record addresses differ.
* Traits sharing: `m_geom_traits = (arr.m_own_traits) ? new Traits_adaptor_2 : arr.m_geom_traits;`
  and `m_own_traits = arr.m_own_traits`. So copying an arrangement that was built
  with a user-supplied traits pointer produces a copy **aliasing the same traits
  object**.
* The destructor calls `(*iter)->detach()` on every still-attached observer.
  Observers must outlive nothing in particular, but an observer that outlives the
  arrangement gets detached automatically.
* `clear()` is virtual: derived arrangement types (with-history) override it.

### 6.7 Traits access

```cpp
inline const Traits_adaptor_2*  traits_adaptor()  const;   // the internal adaptor
inline const Geometry_traits_2* geometry_traits() const;   // same pointer, upcast (adaptor derives from the traits)
inline       Topology_traits*   topology_traits();
inline const Topology_traits*   topology_traits() const;
```
`Arrangement_2` additionally provides `const Traits_2* traits() const { return this->geometry_traits(); }`.
There is **no non-const `geometry_traits()`**.

### 6.8 Dimensions

```cpp
bool is_empty() const;                        // m_topol_traits.is_empty_dcel()
bool is_valid() const;                        // full structural + geometric check, O(n) with predicate calls
Size number_of_vertices() const;              // topology_traits()->number_of_concrete_vertices()
Size number_of_isolated_vertices() const;     // _dcel().size_of_isolated_vertices()
Size number_of_halfedges() const;             // number_of_valid_halfedges()   (always even)
Size number_of_edges() const;                 // number_of_valid_halfedges() / 2
Size number_of_faces() const;                 // number_of_valid_faces()
Size number_of_unbounded_faces() const;       // *** O(#faces): it literally counts via Unbounded_face_const_iterator ***
```
`Arrangement_2` adds
```cpp
Size number_of_vertices_at_infinity() const;  // number_of_valid_vertices() - number_of_concrete_vertices()
```

Semantics per topology traits:

| | bounded (`Arr_bounded_planar_topology_traits_2`) | unbounded (`Arr_unb_planar_topology_traits_2`) |
|---|---|---|
| `is_empty_dcel()` | `size_of_vertices()==0 && size_of_halfedges()==0` | `size_of_vertices()==4 && size_of_halfedges()==8` |
| `is_concrete_vertex(v)` | `true` | `!v->has_null_point()` |
| `number_of_concrete_vertices()` | `size_of_vertices()` | `size_of_vertices() - n_inf_verts` |
| `is_valid_vertex(v)` | `true` | `!v->has_null_point() \|\| (v not one of the 4 corners)` |
| `number_of_valid_vertices()` | `size_of_vertices()` | `size_of_vertices() - 4` |
| `is_valid_halfedge(he)` | `true` | `!he->has_null_curve()` |
| `number_of_valid_halfedges()` | `size_of_halfedges()` | `size_of_halfedges() - 2*n_inf_verts` |
| `is_valid_face(f)` | `true` | `!f->is_fictitious()` |
| `number_of_valid_faces()` | `size_of_faces()` | `size_of_faces() - 1` |
| `initial_face()` | the unbounded face | the fictitious face |
| `reference_face()` | the unbounded face | `v_tr->halfedge()->outer_ccb()->face()` |
| `is_unbounded(f)` | `f == unb_face` | (out-of-line, `…_impl.h`) |

**[verified]** empty `Arrangement_2<Arr_linear_traits_2<Epeck>>`:
`number_of_faces()==1`, `number_of_vertices()==0`,
`number_of_vertices_at_infinity()==0`, `number_of_unbounded_faces()==1`,
`fictitious_face()->is_fictitious()==true`, `unbounded_face() != fictitious_face()`.
After inserting one line: `V=0 E=1 F=2`, both faces unbounded, `ninf=2`.

### 6.9 Traversal

```cpp
Vertex_iterator       vertices_begin();       Vertex_iterator       vertices_end();
Vertex_const_iterator vertices_begin() const; Vertex_const_iterator vertices_end() const;
Iterator_range<Prevent_deref<Vertex_iterator> >       vertex_handles();
Iterator_range<Prevent_deref<Vertex_const_iterator> > vertex_handles() const;

Halfedge_iterator       halfedges_begin();       Halfedge_iterator       halfedges_end();
Halfedge_const_iterator halfedges_begin() const; Halfedge_const_iterator halfedges_end() const;
Iterator_range<Prevent_deref<Halfedge_iterator> >       halfedge_handles();
Iterator_range<Prevent_deref<Halfedge_const_iterator> > halfedge_handles() const;

Edge_iterator       edges_begin();       Edge_iterator       edges_end();
Edge_const_iterator edges_begin() const; Edge_const_iterator edges_end() const;
Iterator_range<Prevent_deref<Edge_iterator> >       edge_handles();
Iterator_range<Prevent_deref<Edge_const_iterator> > edge_handles() const;

Face_iterator       faces_begin();       Face_iterator       faces_end();
Face_const_iterator faces_begin() const; Face_const_iterator faces_end() const;
Iterator_range<Prevent_deref<Face_iterator> >       face_handles();
Iterator_range<Prevent_deref<Face_const_iterator> > face_handles() const;

Face_const_handle reference_face() const;   // _const_handle_for(topology_traits()->reference_face())
Face_handle       reference_face();         // _handle_for(...)

Unbounded_face_iterator       unbounded_faces_begin();       Unbounded_face_iterator       unbounded_faces_end();
Unbounded_face_const_iterator unbounded_faces_begin() const; Unbounded_face_const_iterator unbounded_faces_end() const;

Face_handle       fictitious_face();        // Face_handle(const_cast<DFace*>(topology_traits()->initial_face()))
Face_const_handle fictitious_face() const;  // DFace_const_iter(topology_traits()->initial_face())
```

The `*_handles()` range accessors wrap the iterator in `Prevent_deref`, so
`for (auto v : arr.vertex_handles())` gives you `Vertex_handle`, not `Vertex&`
**[verified]**.

`reference_face()` doc comment: *“returns a reference face of the arrangement.
All reference faces of arrangements of the same type have a common point.”*
(used by overlay to align the two inputs).

### 6.10 Const-cast helpers

```cpp
Vertex_handle   non_const_handle(Vertex_const_handle   vh);   // DVertex*   p = (DVertex*)&(*vh);   return Vertex_handle(p);
Halfedge_handle non_const_handle(Halfedge_const_handle hh);
Face_handle     non_const_handle(Face_const_handle     fh);
```
All three are **non-const member functions** (you need a mutable arrangement).
They rebuild the handle from the raw record pointer, so — as in gotcha 8 — the
result has a null filter and a degenerate `past_the_end()`: fine as an identity
and for `->`, not for iteration **[verified]** that the round-trip compares equal.

### 6.11 Insertion — exact signatures + preconditions

All of these are **specialised / “unchecked” insertion**: they do *no* point
location and *no* intersection testing. Violating a precondition is UB in
release builds (`CGAL_precondition` compiles out with `NDEBUG` /
`CGAL_NDEBUG`).

```cpp
Vertex_handle insert_in_face_interior(const Point_2& p, Face_handle f);
```
Header doc: *“inserts a point that forms an isolated vertex in the interior of a
given face … \return A handle for the isolated vertex that has been created.”*
Impl notes: computes `parameter_space_in_x/y_2_object()(p)`; if both are
`ARR_INTERIOR` it calls `_create_vertex(p)`, otherwise `_create_boundary_vertex`
plus `notify_on_boundary_vertex_creation`. Then `_insert_isolated_vertex(p_f, v)`.
**There is no check that `p` actually lies inside `f`.** No existing handle is
invalidated; `f` gains one isolated-vertex record.

```cpp
Halfedge_handle insert_in_face_interior(const X_monotone_curve_2& cv, Face_handle f);
```
Doc: *“inserts an x-monotone curve into the arrangement as a new hole (inner
component) inside the given face … \return A handle for one of the halfedges
corresponding to the inserted curve, directed (lexicographically) from left to
right.”* `CGAL_postcondition(new_he->direction() == ARR_LEFT_TO_RIGHT)`.
Both endpoints are **created fresh** (or placed on the boundary via
`_place_and_set_curve_end`). If *both* ends reach the parameter-space boundary a
**new face may be created**, in which case `_relocate_in_new_face(new_he)` moves
inner CCBs and isolated vertices into it — so `Face_handle`s to the split face
stay valid but their contents change.

```cpp
Halfedge_handle insert_from_left_vertex(const X_monotone_curve_2& cv,
                                        Vertex_handle v,
                                        Face_handle f = Face_handle());
```
Doc: *“\param f The face that contains v (in case it has no incident edges).
\pre The left endpoint of cv is incident to the vertex v. \return A handle for
one of the halfedges corresponding to the inserted curve, whose target is the
new vertex.”*
Exact precondition text in the impl:
`"The input vertex should be the left curve end."` — formally
`(!at_obnd1 && equal_2(v->point(), construct_min_vertex_2(cv))) || (at_obnd1 && v->is_at_open_boundary())`
where `at_obnd1 = !is_closed_2(cv, ARR_MIN_END)`.
When `v->degree() == 0` **and** `!v->is_isolated()`, `f` **must** be supplied
(`CGAL_precondition(f != Face_handle())`); when `v` is isolated the containing
face is taken from the record and the isolated-vertex record is destroyed.

```cpp
Halfedge_handle insert_from_left_vertex(const X_monotone_curve_2& cv,
                                        Halfedge_handle prev);
```
Doc: *“\param prev The reference halfedge. We should represent cv as a pair of
edges, one of them should become prev’s successor. \pre The target vertex of
prev is cv’s left endpoint. \return A handle for one of the halfedges …, whose
target is the new vertex that was created.”*
Two impl preconditions:
`"The target of the input halfedge should be the left curve end."` and
`"In the clockwise order of curves around the vertex, cv must succeed the curve of prev."`
(checked as `_locate_around_vertex(_vertex(prev->target()), cv, ARR_MIN_END) == _halfedge(prev)`).

```cpp
Halfedge_handle insert_from_right_vertex(const X_monotone_curve_2& cv,
                                         Vertex_handle v,
                                         Face_handle f = Face_handle());
Halfedge_handle insert_from_right_vertex(const X_monotone_curve_2& cv,
                                         Halfedge_handle prev);
```
Mirror images; precondition text `"The input vertex should be the right curve
end."` / `"The target of the input halfedge should be the right curve end."`,
using `construct_max_vertex_2` and `ARR_MAX_END`.
**[verified]** `insert_from_right_vertex(Seg((2,3),(4,0)), v_at_(4,0))` returns a
halfedge with `source()==(4,0)`, `target()==(2,3)` and
`direction() == ARR_RIGHT_TO_LEFT` — i.e. the returned halfedge points **at the
newly created vertex**, which is *not* necessarily the left-to-right one.

```cpp
Halfedge_handle insert_at_vertices(const X_monotone_curve_2& cv,
                                   Vertex_handle v1, Vertex_handle v2,
                                   Face_handle f = Face_handle());
```
Doc: *“\param f The face that contains v1 and v2 (in case both have no incident
edges). \pre v1 and v2 corresponds to cv’s endpoints. \return A handle for one
of the halfedges corresponding to the inserted curve directed from v1 to v2.”*
Precondition messages in the impl:
`"One of the input vertices should be the left curve end."` and
`"One of the input vertices should be the right curve end."` — the function
figures out which of `v1`, `v2` is the min end itself.
Assertions: `"The two isolated vertices must be located inside the same face."`,
`"The inserted curve should not intersect the existing arrangement."`,
`"The inserted curve cannot be located in the arrangement."` (when
`_locate_around_vertex` returns `nullptr`).
`f` is needed only when *both* vertices have degree 0 and neither carries an
isolated-vertex record.
If both vertices already have incident edges the call forwards to
`insert_at_vertices(cv, Halfedge_handle(prev1), Halfedge_handle(prev2))`.

```cpp
Halfedge_handle insert_at_vertices(const X_monotone_curve_2& cv,
                                   Halfedge_handle prev1, Vertex_handle v2);
Halfedge_handle insert_at_vertices(const X_monotone_curve_2& cv,
                                   Halfedge_handle prev1, Halfedge_handle prev2);
```
Doc for the last one: *“\pre The target vertices of prev1 and prev2 are cv’s
endpoints. \return A handle for one of the halfedges corresponding to the
inserted curve directed from prev1’s target to prev2’s target.”*
Impl: computes `res = SMALLER/LARGER` for `prev1->target()` vs `prev2->target()`,
calls `_insert_at_vertices(p_prev1, cv, res==SMALLER ? ARR_LEFT_TO_RIGHT :
ARR_RIGHT_TO_LEFT, p_prev2->next(), new_face_created, swapped_predecessors)`,
then, if a face was created, `_relocate_in_new_face(new_he)`, and finally
**`if (swapped_predecessors) new_he = new_he->opposite();`** so the documented
direction (prev1’s target → prev2’s target) always holds.

**Face creation.** Closing a cycle splits a face: the *existing* `Face_handle`
survives and a **new** `Face` record is added. Which of the two keeps the old
record is decided internally (`_defines_outer_ccb_of_new_face`), so after a
closing insertion you must re-read `he->face()` and `he->twin()->face()`; do
**not** assume the handle you held still denotes the region you meant.

### 6.12 Vertex manipulation

```cpp
Vertex_handle modify_vertex(Vertex_handle v, const Point_2& p);
```
Doc: *“\pre p is geometrically equivalent to the current point associated with v.
\return A handle for a the modified vertex (same as v).”*
Impl preconditions: `"The modified vertex must not lie on open boundary."`
(`!vh->is_at_open_boundary()`) and
`"The new point is different from the current one."` — note the message is
inverted; the actual test is `equal_2_object()(vh->point(), p)`, i.e. the new
point **must compare equal**. Notifies `before_/after_modify_vertex`. The
handle is unchanged and stays valid.

```cpp
Face_handle remove_isolated_vertex(Vertex_handle v);
```
Doc: *“\pre v is an isolated vertex (it has no incident halfedges). \return A
handle for the face containing v.”* `CGAL_precondition(v->is_isolated())`.
Erases the isolated-vertex record, deletes the point and the DCEL vertex, and
notifies `before_/after_remove_vertex`. **`v` is dangling afterwards.**
**[verified]** the returned face handle equals the containing face handle.

### 6.13 Halfedge manipulation

```cpp
Halfedge_handle modify_edge(Halfedge_handle e, const X_monotone_curve_2& cv);
```
Doc: *“\pre cv is geometrically equivalent to the current curve associated with
e. \return A handle for a the modified halfedge (same as e).”*
Impl preconditions: `"The edge must be a valid one."` (`!e->is_fictitious()`) and
`equal_2_object()(e->curve(), cv)`. Notifies `before_/after_modify_edge`. Both
`e` and `e->twin()` keep pointing to the (replaced) curve.

```cpp
Halfedge_handle split_edge(Halfedge_handle e,
                           const X_monotone_curve_2& cv1,
                           const X_monotone_curve_2& cv2);
```
Doc: *“\pre cv1’s source and cv2’s target equal the endpoints of the curve
currently associated with e (respectively), and cv1’s target equals cv2’s
target, and this is the split point (or vice versa). \return A handle for the
halfedge whose source is the source of the original halfedge e, and whose target
is the split point.”*
Impl precondition `"The edge must be a valid one."` plus the four-case analysis
(source matches `cv1` min/max or `cv2` min/max) with
`CGAL_precondition(equal_2_object()(p, q))` on the shared split point.
The **new vertex is created by the caller path**
(`_split_edge(he1, p, …)` → `_create_vertex(p)` → `_split_edge(he1, v, …)`).

Handle-validity semantics of `_split_edge` (read from the impl):
* **`e` itself is returned and remains valid**; it now carries `cv1` and its
  target is the new split vertex. **[verified]** `split_edge(e,…) == e`.
* `e->twin()` also remains valid and unchanged as an object. **[verified]**
* A **new halfedge pair `he3/he4`** is allocated for the second half;
  `he3->set_direction(he1->direction())`.
* If the old target vertex pointed at `he1`, its `halfedge()` is repointed to `he3`.
* Face records, CCB records, and every other handle are untouched.
* Observers: `before_split_edge(Halfedge_handle(e), Vertex_handle(v), cv1, cv2)`
  then `after_split_edge(Halfedge_handle(he1), Halfedge_handle(he3))`.

```cpp
Halfedge_handle merge_edge(Halfedge_handle e1, Halfedge_handle e2,
                           const X_monotone_curve_2& cv);
```
Doc: *“\return A handle for the merged halfedge.”* Impl preconditions:
* `"The edges must be a valid."` — `!e1->is_fictitious() && !e2->is_fictitious()`
* `"The input edges do not share a common vertex."` (hard `CGAL_precondition_msg(false, …)`
  fallback that also `return Halfedge_handle();` — **a default-constructed,
  unusable handle**; check for it if you compile with preconditions off).
* `"The vertex removed by the merge must not lie on open boundary."` — `!v->has_null_point()`
* `"The degree of the deleted vertex is greater than 2."` —
  `he1->next()->opposite() == he4 && he4->next()->opposite() == he1`
* `"The endpoints of the merged curve must match the end vertices."`

Handle validity:
* the halfedge pair derived from `e1`’s side (`he1/he2`) **survives and is
  returned**; `he1->curve() = cv` (assignment into the existing curve object).
  **[verified]** `merge_edge(sp, sp->next(), cv) == sp`.
* the pair derived from `e2` (`he3/he4`) is **deleted** — any handle to `e2` or
  its twin dangles.
* the shared vertex is deleted (`_delete_point`, `delete_vertex`) — its handle dangles.
* CCB representatives and the surviving vertex’s incident halfedge are repointed
  if they referenced the deleted pair (no observer notification for that).
* Observers: `before_merge_edge(e1, e2, cv)`, `before_/after_remove_vertex`,
  `after_merge_edge(hh)`.

```cpp
Face_handle remove_edge(Halfedge_handle e,
                        bool remove_source = true,
                        bool remove_target = true);
```
Doc: *“\param remove_source Should the source vertex of e be removed if it
becomes isolated (true by default). \param remove_target … \return A handle for
the remaining face.”* Precondition `"The edge must be a valid one."`
(`!e->is_fictitious()`). The whole body is `_remove_edge(_halfedge(e),
remove_source, remove_target)` — the decision whether to operate on `e` or its
twin is made inside.

Semantics of `_remove_edge` (read from the impl):
* **`f1 == f2` (both sides in the same face).**
  * If `he1->next()==he2 && he2->next()==he1` — a “singleton hole”: the inner CCB
    record is erased (`before_/after_remove_inner_ccb`), the edge is deleted, and
    each end vertex is either deleted (when `remove_*` is true) or turned into an
    **isolated vertex of `f1`** (`_insert_isolated_vertex(f1, v)`) when
    `remove_*` is false.
  * If exactly one of `he1->next()==he2` / `he2->next()==he1` — an “antenna”: the
    tip vertex obeys `remove_target`/`remove_source` (whichever is the tip after
    normalisation), the other endpoint keeps its other edges.
  * Otherwise the removal **may create a new inner CCB (hole)** inside `f1`; the
    `remove_*` flags are irrelevant because neither endpoint becomes isolated.
  * Returns `f1`.
* **`f1 != f2`.** The two faces are merged. Roles are swapped if needed so that
  `f1` is the containing face; then `_move_all_inner_ccb(f2, f1)`,
  `_move_all_isolated_vertices(f2, f1)`, `if (f2->is_unbounded()) f1->set_unbounded(true)`,
  **`_dcel().delete_face(f2)`**, `after_merge_face(Face_handle(f1))`.
  **⇒ one of the two incident `Face_handle`s becomes dangling and you cannot
  predict which one from the outside.** Always use the returned handle.
* Boundary end-vertices are additionally passed through
  `_remove_vertex_if_redundant(v, f1)` *after* `after_remove_edge()`.
* Observers, in order: `before_remove_edge(hh)`, possibly CCB/face notifications,
  `after_remove_edge()`, then any `before_/after_remove_vertex`.
  (With `CGAL_NON_SYMETRICAL_OBSERVER_EDGE_REMOVAL_BACKWARD_COMPATIBILITY`
  defined, `after_remove_edge()` fires *after* the vertex removals instead.)

**[verified]** removing one edge of a triangle returned the unbounded face
(`rf == arr.unbounded_face()`), `number_of_faces()` went 2 → 1, and the two
degree-1 endpoints were **kept** (they did not become isolated).

There is **no** `remove_edge` overload other than this one on the class; the
free function `CGAL::remove_edge(arr, e)` (below) is the intersection-aware one.

### 6.14 Sweep-mode maintenance

```cpp
void clean_inner_ccbs_after_sweep();
```
Walks all halfedges, forces the path compression in `DHalfedge::inner_ccb()`, and
then deletes every `Dcel::Inner_ccb` for which `is_valid()` is false. Must be
called after a `set_sweep_mode(true)` phase.

### 6.15 Protected helpers you may want in a C++ core

Available if your core class derives from the arrangement or is befriended;
otherwise use `Arr_accessor` (§9).

```cpp
inline Dcel&       _dcel();
inline const Dcel& _dcel() const;

inline DVertex*         _vertex  (Vertex_handle vh)         const;   // &(*vh)
inline const DVertex*   _vertex  (Vertex_const_handle vh)   const;
inline DHalfedge*       _halfedge(Halfedge_handle hh)       const;
inline const DHalfedge* _halfedge(Halfedge_const_handle hh) const;
inline DFace*           _face    (Face_handle fh)           const;
inline const DFace*     _face    (Face_const_handle fh)     const;

Vertex_handle         _handle_for      (DVertex* v);
Vertex_const_handle   _const_handle_for(const DVertex* v)   const;
Halfedge_handle       _handle_for      (DHalfedge* he);
Halfedge_const_handle _const_handle_for(const DHalfedge* he) const;
Face_handle           _handle_for      (DFace* f);
Face_const_handle     _const_handle_for(const DFace* f)      const;

Point_2* _new_point(const Point_2& pt);   void _delete_point(Point_2& pt);
X_monotone_curve_2* _new_curve(const X_monotone_curve_2& cv);  void _delete_curve(X_monotone_curve_2& cv);
```
plus the boundary-side helpers `is_open(Arr_boundary_side_tag)`,
`is_open(Arr_open_side_tag)`, `is_open(Arr_parameter_space, Arr_parameter_space)`,
`is_contracted(...)`, `is_identified(...)` and the tag-dispatched comparison
helpers `_is_smaller(...)`, `_is_smaller_near_right(...)`.

**Answer to “how do handles relate to raw DCEL pointers?”**
`&*handle` is exactly the `DVertex*` / `DHalfedge*` / `DFace*` (note the
arrangement’s `Vertex`/`Halfedge`/`Face` are `DVertex`/`DHalfedge`/`DFace`
themselves — the nested classes only re-expose/hide members, they add no state,
so `static_cast` between them is a no-op). The reverse direction is
`Vertex_handle(ptr)` / `Halfedge_handle(ptr)` / `Face_handle(ptr)` via the
`template <typename T> I_Filtered_iterator(T* p)` constructor
(**[verified]** with both `Arr::Vertex*` and `Arr::Dcel::Vertex*`), or, inside
the class hierarchy, `_handle_for(ptr)` / `_const_handle_for(ptr)`.
For an *external* binding layer the supported spelling is
`arr.non_const_handle(const_handle)` and the public constructor.

---

## 7. `CGAL::Arrangement_2<GeomTraits_, Dcel_ = Arr_default_dcel<GeomTraits_>>`

```cpp
template <class GeomTraits_, class Dcel_ = Arr_default_dcel<GeomTraits_> >
class Arrangement_2 :
  public Arrangement_on_surface_2<GeomTraits_,
                                  typename Default_planar_topology<GeomTraits_, Dcel_>::Traits>
{
protected:
  typedef Default_planar_topology<GeomTraits_, Dcel_> Default_topology;
public:
  typedef Arrangement_on_surface_2<GeomTraits_, typename Default_topology::Traits> Base;
  typedef GeomTraits_                            Geometry_traits_2;
  typedef Dcel_                                  Dcel;
  typedef Arrangement_2<Geometry_traits_2, Dcel> Self;
  typedef typename Base::Point_2                 Point_2;
  typedef typename Base::X_monotone_curve_2      X_monotone_curve_2;
  typedef typename Default_topology::Traits      Topology_traits;
  // …re-exports of Vertex, Halfedge, Face, Size, all *_iterator / *_const_iterator,
  //  Halfedge_around_vertex_circulator(+const), Ccb_halfedge_circulator(+const),
  //  Outer_ccb_iterator(+const), Inner_ccb_iterator(+const),
  //  Isolated_vertex_iterator(+const), all six handles…

  // backward compatibility:
  typedef Geometry_traits_2                       Traits_2;
  typedef typename Base::Inner_ccb_iterator       Hole_iterator;
  typedef typename Base::Inner_ccb_const_iterator Hole_const_iterator;

private:
  friend class Arr_accessor<Self>;

public:
  Arrangement_2();                              // : Base()
  Arrangement_2(const Base& base);              // : Base(base)
  Arrangement_2(const Traits_2* tr);            // : Base(tr)   — does NOT take ownership

  Self& operator=(const Base& base);            // Base::assign(base); return *this;
  void  assign(const Base& base);               // Base::assign(base);

  const Traits_2* traits() const;               // == this->geometry_traits()
  Size number_of_vertices_at_infinity() const;  // valid − concrete

  Face_handle       unbounded_face();
  Face_const_handle unbounded_face() const;
};
```

**`unbounded_face()` body (verbatim logic).**
```cpp
DFace* un_face = const_cast<DFace*>(this->topology_traits()->initial_face());
if (!un_face->is_fictitious()) return Face_handle(un_face);
DHalfedge*  p_he  = *(un_face->inner_ccbs_begin());
DHalfedge*  p_opp = p_he->opposite();
DOuter_ccb* p_oc  = p_opp->outer_ccb();
return Face_handle(p_oc->face());
```
i.e. under the bounded topology it *is* `initial_face()`; under the unbounded
topology it steps from the fictitious face’s inner CCB into **one of** the
(possibly several) unbounded faces. **With unbounded curves there can be more
than one unbounded face — `unbounded_face()` gives you an arbitrary one.** Use
`unbounded_faces_begin()/end()` when you need all of them.

**Types NOT re-declared in `Arrangement_2`** but inherited and usable:
`Unbounded_face_iterator`, `Unbounded_face_const_iterator`, `Observer`,
`Base_aos`, `Traits_adaptor_2`, all `Left/Bottom/Top/Right_side_category`,
`Are_all_sides_oblivious_category`, `Has_identified_sides`,
`Top_or_bottom_sides_category`, `Allocator`. **[verified]** e.g.
`LArr::Unbounded_face_iterator` compiles.

Note the copy constructor and copy assignment of `Arrangement_2` are the
*implicitly declared* ones (the `const Base&` overloads do not suppress them);
they forward to `Base`’s deep-copying versions.

---

## 8. Free functions declared in `Arrangement_on_surface_2.h`

(Defined in `Arrangement_2/Arrangement_on_surface_2_global.h`. All in
`namespace CGAL`, all take the **base** class so `Arrangement_2` binds.)

```cpp
template <typename GeomTraits, typename TopTraits, typename Curve, typename PointLocation>
void insert(Arrangement_on_surface_2<GeomTraits, TopTraits>& arr,
            const Curve& c, const PointLocation& pl,
            typename PointLocation::Point_2* = 0);          // dummy arg disambiguates from the range overload

template <typename GeomTraits, typename TopTraits, typename Curve>
void insert(Arrangement_on_surface_2<GeomTraits, TopTraits>& arr, const Curve& c);
                                                            // default point location: "walk"

template <typename GeomTraits, typename TopTraits, typename InputIterator>
void insert(Arrangement_on_surface_2<GeomTraits, TopTraits>& arr,
            InputIterator begin, InputIterator end);        // \pre value_type == Curve_2 ; aggregated (sweep)

template <typename GeomTraits, typename TopTraits>
void insert(Arrangement_on_surface_2<GeomTraits, TopTraits>& arr,
            const typename GeomTraits::X_monotone_curve_2& c,
            typename Arr_point_location_result<
              Arrangement_on_surface_2<GeomTraits, TopTraits> >::type obj);   // std::variant hint

template <typename GeomTraits, typename TopTraits, typename PointLocation>
typename Arrangement_on_surface_2<GeomTraits, TopTraits>::Halfedge_handle
insert_non_intersecting_curve(Arrangement_on_surface_2<GeomTraits, TopTraits>& arr,
                              const typename GeomTraits::X_monotone_curve_2& c,
                              const PointLocation& pl);
      // \pre The interior of c does not intersect any existing edge or vertex.
      // \return one of the new halfedges, directed (lexicographically) left→right.

template <typename GeomTraits, typename TopTraits>
typename Arrangement_on_surface_2<GeomTraits, TopTraits>::Halfedge_handle
insert_non_intersecting_curve(Arrangement_on_surface_2<GeomTraits, TopTraits>& arr,
                              const typename GeomTraits::X_monotone_curve_2& c);

template <typename GeomTraits, typename TopTraits, typename InputIterator>
void insert_non_intersecting_curves(Arrangement_on_surface_2<GeomTraits, TopTraits>& arr,
                                    InputIterator begin, InputIterator end);
      // \pre pairwise interior-disjoint, interiors disjoint from the arrangement

template <typename GeomTraits, typename TopTraits>
typename Arrangement_on_surface_2<GeomTraits, TopTraits>::Face_handle
remove_edge(Arrangement_on_surface_2<GeomTraits, TopTraits>& arr,
            typename Arrangement_on_surface_2<GeomTraits, TopTraits>::Halfedge_handle e);
      // "In case it is possible to merge the edges incident to the end-vertices of the
      //  removed edge after its deletion, the function performs these merges as well."

template <typename GeomTraits, typename TopTraits, typename PointLocation>
typename Arrangement_on_surface_2<GeomTraits, TopTraits>::Vertex_handle
insert_point(Arrangement_on_surface_2<GeomTraits, TopTraits>& arr,
             const typename GeomTraits::Point_2& p, const PointLocation& pl);

template <typename GeomTraits, typename TopTraits>
typename Arrangement_on_surface_2<GeomTraits, TopTraits>::Vertex_handle
insert_point(Arrangement_on_surface_2<GeomTraits, TopTraits>& arr,
             const typename GeomTraits::Point_2& p);

template <typename GeomTraits, typename TopTraits>
bool remove_vertex(Arrangement_on_surface_2<GeomTraits, TopTraits>& arr,
                   typename Arrangement_on_surface_2<GeomTraits, TopTraits>::Vertex_handle v);
      // \return Whether the vertex has been removed or not.

template <typename GeomTraits, typename TopTraits>
bool is_valid(const Arrangement_on_surface_2<GeomTraits, TopTraits>& arr);

template <typename GeomTraits, typename TopTraits, typename OutputIterator, typename PointLocation>
OutputIterator zone(Arrangement_on_surface_2<GeomTraits, TopTraits>& arr,
                    const typename GeomTraits::X_monotone_curve_2& c,
                    OutputIterator oi, const PointLocation& pl);
      // dereference type: a variant wrapping Vertex_handle / Halfedge_handle / Face_handle

template <typename GeomTraits, typename TopTraits, typename OutputIterator>
OutputIterator zone(Arrangement_on_surface_2<GeomTraits, TopTraits>& arr,
                    const typename GeomTraits::X_monotone_curve_2& c, OutputIterator oi);

template <typename GeomTraits, typename TopTraits, typename Curve, typename PointLocation>
bool do_intersect(Arrangement_on_surface_2<GeomTraits, TopTraits>& arr,
                  const Curve& c, const PointLocation& pl);

template <typename GeomTraits, typename TopTraits, typename Curve>
bool do_intersect(Arrangement_on_surface_2<GeomTraits, TopTraits>& arr, const Curve& c);
```

Careful with the first `insert`: the trailing `typename PointLocation::Point_2* = 0`
exists only to disambiguate from the two-iterator overload — never pass it.

---

## 9. Adjacent pieces a binding will need

### 9.1 `Aos_observer<Arrangement_>` (`CGAL/Aos_observer.h`)

```cpp
typedef Arrangement_                                   Arrangement_2;
typedef typename Arrangement_2::Point_2                Point_2;
typedef typename Arrangement_2::X_monotone_curve_2     X_monotone_curve_2;
typedef typename Arrangement_2::Vertex_handle          Vertex_handle;
typedef typename Arrangement_2::Halfedge_handle        Halfedge_handle;
typedef typename Arrangement_2::Face_handle            Face_handle;
typedef typename Arrangement_2::Ccb_halfedge_circulator Ccb_halfedge_circulator;

void attach(Arrangement_2& arr);
void detach();
```
Virtual hooks (all no-ops by default): `before_assign(const Arrangement_2&)`,
`after_assign()`, `before_clear()`, `after_clear()`, `before_global_change()`,
`after_global_change()`, `before_attach(const Arrangement_2&)`, `after_attach()`,
`before_detach()`, `after_detach()`, `before_create_vertex(const Point_2&)`,
`after_create_vertex(Vertex_handle)`,
`before_create_boundary_vertex(const Point_2&, …)` and the
`(const X_monotone_curve_2&, …)` overload, `after_create_boundary_vertex(Vertex_handle)`,
`before_create_edge(const X_monotone_curve_2&, …)`, `after_create_edge(Halfedge_handle)`,
`before_modify_vertex(Vertex_handle, …)`, `after_modify_vertex(Vertex_handle)`,
`before_modify_edge(Halfedge_handle, …)`, `after_modify_edge(Halfedge_handle)`,
`before_split_edge(...)`, `after_split_edge(Halfedge_handle, Halfedge_handle)`,
`before_/after_split_fictitious_edge`, `before_/after_split_face`,
`before_/after_split_outer_ccb`, `before_/after_split_inner_ccb`,
`before_/after_add_outer_ccb`, `before_/after_add_inner_ccb`,
`before_/after_add_isolated_vertex`, `before_merge_edge(Halfedge_handle,
Halfedge_handle, const X_monotone_curve_2&)`, `after_merge_edge(Halfedge_handle)`,
`before_/after_merge_fictitious_edge`, `before_merge_face(Face_handle,
Face_handle, …)`, `after_merge_face(Face_handle)`,
`before_/after_merge_outer_ccb`, `before_/after_merge_inner_ccb`,
`before_/after_move_outer_ccb`, `before_/after_move_inner_ccb`,
`before_/after_move_isolated_vertex`, `before_remove_vertex(Vertex_handle)`,
`after_remove_vertex()`, `before_remove_edge(Halfedge_handle)`,
`after_remove_edge()`, `before_remove_outer_ccb(Face_handle,
Ccb_halfedge_circulator)`, `after_remove_outer_ccb(Face_handle)`,
`before_remove_inner_ccb(...)`, `after_remove_inner_ccb(Face_handle)`.

`CGAL/Arr_observer.h` now contains only
`template <typename Arrangement_> using Arr_observer = typename Arrangement_::Observer;`.

### 9.2 `Arr_accessor<Arrangement_>` (`CGAL/Arr_accessor.h`)

`friend` of the arrangement; the sanctioned way to reach protected functionality
from outside. Public typedefs include the six handles, `Ccb_halfedge_circulator`,
and the *internal* `DVertex`, `DHalfedge`, `DFace`, `DOuter_ccb`, `DInner_ccb`,
`DIso_vertex`, plus `Pl_result` / `Pl_result_type`.

```cpp
Arr_accessor(Arrangement_2& arr);
Arrangement_2&       arrangement();
const Arrangement_2& arrangement() const;
void notify_before_global_change();  void notify_after_global_change();

Pl_result_type   locate_curve_end(const X_monotone_curve_2& cv, Arr_curve_end,
                                  Arr_parameter_space ps_x, Arr_parameter_space ps_y) const;
                                  // \pre ps_x != ARR_INTERIOR || ps_y != ARR_INTERIOR
Halfedge_handle  locate_around_vertex(Vertex_handle vh, const X_monotone_curve_2& cv) const;
Halfedge_handle  locate_around_boundary_vertex(Vertex_handle vh, const X_monotone_curve_2& cv,
                                               Arr_curve_end ind,
                                               Arr_parameter_space ps_x, Arr_parameter_space ps_y) const;
int  halfedge_distance(Halfedge_const_handle e1, Halfedge_const_handle e2) const;  // -1 if not on the same CCB
bool defines_outer_ccb_of_new_face(Halfedge_handle prev1, Halfedge_handle prev2, …) const;
bool are_equal(Vertex_const_handle v, const X_monotone_curve_2& cv, Arr_curve_end,
               Arr_parameter_space, Arr_parameter_space) const;
bool is_on_outer_boundary(Halfedge_const_handle he) const;
bool is_on_inner_boundary(Halfedge_const_handle he) const;
Vertex_handle create_vertex(const Point_2& p);
Vertex_handle create_boundary_vertex(const Point_2& pt, Arr_parameter_space, Arr_parameter_space, bool notify = true);
Vertex_handle create_boundary_vertex(const X_monotone_curve_2& cv, Arr_curve_end,
                                     Arr_parameter_space, Arr_parameter_space, bool notify = true);
std::pair<Vertex_handle, Halfedge_handle>
  place_and_set_curve_end(Face_handle f, const X_monotone_curve_2& cv, Arr_curve_end,
                          Arr_parameter_space, Arr_parameter_space);
// … plus insert_at_vertices_ex / insert_from_vertex_ex / insert_in_face_interior_ex,
//     split_edge_ex, remove_edge_ex, modify_vertex_ex, modify_edge_ex, relocate_*, etc.
```
**[verified]** `CGAL::Arr_accessor<Arrangement_2<…>> acc(arr);` compiles for a
plain `Arrangement_2` (`friend class Arr_accessor<Self>` is declared in both
`Arrangement_on_surface_2` and `Arrangement_2`).

### 9.3 `Arr_point_location_result<Arrangement_>` (`CGAL/Arr_point_location_result.h`)

```cpp
typedef typename Arrangement_2::Vertex_const_handle   Vertex_const_handle;
typedef typename Arrangement_2::Halfedge_const_handle Halfedge_const_handle;
typedef typename Arrangement_2::Face_const_handle     Face_const_handle;
typedef std::variant<Vertex_const_handle, Halfedge_const_handle, Face_const_handle> Type;
typedef Type type;

template <typename T> static inline Type make_result(T t);
static inline std::optional<Type> empty_optional_result();
template <typename T> static inline const T* assign(const Type* obj);   // std::get_if<T>
static inline Type default_result();
```
Order of alternatives is **Vertex, Halfedge, Face** — index 0/1/2.

---

## 10. Cheat sheet for the type-erased core / Cython layer

* **Identity.** `&*handle` is a stable `void*` for the lifetime of the record.
  Handles are hashable (`std::hash`) and totally ordered (`operator<` by address),
  so `std::unordered_map<Vertex_handle, py_id>` works out of the box.
  Rebuild a handle from a pointer with `Vertex_handle((Arr::Vertex*)p)` etc.
  (24-byte handles; only the first word is the record pointer, so do not
  memcpy-serialise them).
* **Never expose `curve()` or `point()` unguarded.** Test `is_fictitious()` /
  `is_at_open_boundary()` first; both accessors assert on null.
* **Iteration vs. CCB walking differ.** `halfedges_begin()/end()` and
  `vertices_begin()/end()` are *filtered*; CCB circulators are not.
* **What invalidates what:**
  | operation | invalidated |
  |---|---|
  | `insert_in_face_interior(p, f)` | nothing (adds an isolated vertex to `f`) |
  | `insert_*` that closes a cycle | nothing is deleted, but a **new** face appears and the *contents* of the split face change |
  | `modify_vertex` / `modify_edge` | nothing |
  | `split_edge(e, …)` | nothing; `e` and `e->twin()` survive, a new pair is added |
  | `merge_edge(e1, e2, cv)` | `e2`, `e2->twin()`, and the shared vertex handle |
  | `remove_edge(e, …)` | `e`, `e->twin()`; possibly one of the two incident faces; possibly one/both end vertices |
  | `remove_isolated_vertex(v)` | `v` |
  | `clear()`, `assign()`, `operator=`, destructor | **everything** |
  Nothing else — records live in `In_place_list`s, so unrelated insertions and
  deletions never move them.
* **Deep copy is O(n) and re-allocates every point and curve.** There is no move
  constructor. Plan your Python-side ownership around a single arrangement object
  per handle table, and invalidate the whole table on `clear()`/assignment.
* **Traits lifetime.** If you pass `const Traits_2*` to the constructor you own
  it, and every copy of that arrangement will alias it.
* **Precondition checking is compile-time-conditional.** `CGAL_precondition` is
  disabled under `NDEBUG` / `CGAL_NDEBUG`; a Python-facing layer that lets users
  call `insert_at_vertices` directly should re-validate the endpoints itself
  (or ship a checked build).
