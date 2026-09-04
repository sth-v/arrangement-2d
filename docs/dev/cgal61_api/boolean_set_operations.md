# CGAL 6.1 — 2D Boolean Set Operations: signature-level API map

Source of truth: the **installed** headers under `/opt/homebrew/include/CGAL` (CGAL 6.1,
`CGAL_VERSION_NR 1060101000`, git `b26b07a1242`, release date 2025-09-29). Every signature below is
quoted verbatim from those headers. Everything marked *(verified)* was confirmed by compiling and
running a test program with
`clang++ -std=c++17 -O0 -DCGAL_USE_CORE -DCGAL_USE_GMP -DCGAL_USE_MPFR -I/opt/homebrew/include -lgmp -lmpfr`.

---

## 0. Gotchas / surprises vs. older CGAL (and vs. the manual)

1. **`Gps_on_surface_base_2` is still `CGAL::Object`-based internally.** Unlike the arrangement
   package (which moved to `std::variant`/`std::optional` in 6.x), `oriented_side(Point_2)`,
   `locate()` and `_insert()` still do `CGAL::Object obj = pl.locate(q); CGAL::assign(f, obj);`.
   This does not leak into the public API, but do not assume "6.x ⇒ variant" here.
   Conversely, `Polygon_conversions.h` **is** variant-based:
   `using Make_x_monotone_result = std::variant<Point, X_monotone_curve>;`. Traits-level
   `make_x_monotone_2_object()` writes `std::variant<Point_2, X_monotone_curve_2>` — passing a
   `std::list<CGAL::Object>` back-inserter no longer compiles *(verified: hard compile error)*.

2. **The aggregated (range) `do_intersect` returns the INVERTED answer.**
   `Gps_on_surface_base_2::do_intersect(InputIterator begin, InputIterator end, unsigned k=5)`
   ends with `return (other.is_empty());` — i.e. `true` when the polygons do **not** intersect.
   The free-function range overloads (`CGAL::do_intersect(begin, end)`) forward to it.
   *(verified: overlapping range → `0`, disjoint range → `1`.)* The **binary**
   `do_intersect(a, b)` and the member `do_intersect(const Polygon_2&)` are correct.
   → In a binding, implement range `do_intersect` yourself; do not forward.

3. **Every binary Boolean op replaces the underlying arrangement object.**
   `_join/_intersection/_difference/_symmetric_difference` do
   `Aos_2* res_arr = new Aos_2(m_traits); overlay(...); delete m_arr; m_arr = res_arr;`
   and the aggregated D&C path (`Base_merge::operator()` in `Gps_merge.h`) also
   `delete (arr_vec[count].first);` — including the set's original arrangement.
   ⇒ **`const Arrangement_2& arr = ps.arrangement();` dangles after any Boolean op**, and every
   `Face_handle`/`Halfedge_handle`/`Vertex_handle` obtained earlier is invalid. Only `insert()`,
   `complement()`, `remove_redundant_edges()`, `clear()`, `fix_curves_direction()` mutate the
   *same* arrangement in place (handles into removed features still die).

4. **A traits object passed to a constructor is stored by pointer, not copied.**
   `Gps_on_surface_base_2(const Traits_2& tr) : m_traits(&tr), m_traits_owner(false), m_arr(new Aos_2(m_traits))`.
   The caller must keep the traits alive for the whole life of the set *and* of the arrangement.
   (Copy-construction and `operator=` do `new Traits_2(*ps.m_traits)` and set `m_traits_owner=true`,
   so copies are self-owning.) The default ctor allocates its own traits.

5. **Orientation is a *precondition*, not something CGAL fixes.** Outer boundary must be
   counterclockwise, holes clockwise, boundaries closed and (relatively) simple. `General_polygon_set_2`
   / `Polygon_set_2` inherit `PreconditionValidationPolicy`, so a clockwise polygon raises
   `CGAL::Precondition_exception` (default failure behaviour throws) *(verified)*. Compile with
   `NDEBUG`/`CGAL_NDEBUG` and **all validation silently disappears** — the set is built anyway and
   `is_valid()` then returns `false` *(verified: `npwh=1 is_valid=0`, no diagnostics)*.
   Bindings should therefore validate/normalize orientation themselves.

6. **`Ccb_curve_iterator` walks the CCB *backwards* (`--_circ`).** Consequently
   `construct_polygon(ccb, pgn, tr)` on a *contained* face's own outer CCB yields a **clockwise**
   polygon. To get the CCW outer boundary of a region you must use the **inner CCB of the adjacent
   non-contained face** (the twin CCB); holes come from the **outer CCB of the non-contained
   "hole" faces". See §9 for the verified table.

7. **`Polygon_set_2::difference(begin, end)` does not compile.** `Polygon_set_2` declares range
   overloads for `join/intersection/symmetric_difference/difference/do_intersect`, but the base
   `Gps_on_surface_base_2` has **no** range `difference`. Instantiating it is a hard error
   *(verified: "no matching member function for call to 'difference'" at `Polygon_set_2.h:165`)*.
   There are also no free-function aggregated `difference` overloads.

8. **The default free-function path silently converts `Polygon_2` to polylines.** The `Tag_true`
   default (`Tag_true = Tag_true()`) routes through
   `Gps_polyline_traits<Pgn>::Traits = Gps_traits_2<Arr_polyline_traits_2<Arr_segment_traits_2<K>>>`,
   converting each `Polygon_2` into a `General_polygon_2<polyline traits>` and back. Pass
   `CGAL::Tag_false()` to use the plain segment traits instead. The *member* API of `Polygon_set_2`
   never does this — it is always segment-based.

9. **`CGAL_assertion(is_valid())` runs after every binary op** (`_join`, `_intersection`,
   `_difference`, `_symmetric_difference`). `is_valid()` is an O(E) sweep over all edges. In a debug
   build every Boolean op therefore pays a full validity pass.

10. **`polygons_with_holes()` / `number_of_polygons_with_holes()` / `locate()` are `const` but mutate
    the arrangement** through `mutable char m_info` (`set_visited`). They are **not reentrant and not
    thread-safe** even for concurrent readers of the same set.

11. **`Arrangement_2` is a *derived* class of `Arrangement_on_surface_2`** (`class Arrangement_2 :
    public Arrangement_on_surface_2<GeomTraits_, typename Default_planar_topology<GeomTraits_,Dcel_>::Traits>`),
    and `General_polygon_set_2::arrangement()` does an unchecked
    `static_cast<const Arrangement_2*>(this->m_arr)` on the base pointer. This is why the DCEL/topology
    combination is fixed by `Default_planar_topology`.

12. **Two real bugs in `Gps_traits_2::Equal_2`** (`Gps_traits_2.h`):
    `if (pgn2.is_empty()) return false;` (two empty polygons compare unequal) and
    `if (pgn1.number_of_holes(), pgn2.number_of_holes()) return false;` — a **comma operator**, so any
    p-w-h whose *second* operand has ≥1 hole compares unequal. Do not expose this as "polygon equality".

13. **`General_polygon_set_on_surface_2` cannot be used on the sphere as-is.** It compiles with
    geodesic-arc traits + `Arr_spherical_topology_traits_2`, but its precondition validation runs a
    *planar* surface sweep and rejects a legitimate spherical triangle
    (*verified*: "The polygon boundary intersects at vertices" → `Precondition_exception`). Use
    `Gps_on_surface_base_2<…>` directly (its default `ValidationPolicy` is `NoValidationPolicy`);
    that works end-to-end *(verified: insert, `number_of_polygons_with_holes()==1`,
    `polygons_with_holes`, `complement`)*. See §20.

14. **`Gps_segment_traits_2` has no polygon-level `Equal_2`, `Gps_traits_2` does.** `Gps_segment_traits_2`
    merely inherits `Arr_segment_traits_2::Equal_2` (points and x-monotone curves only).

15. `Gps_circle_segment_traits_2`'s second template parameter is spelled **`Filer_`** (sic) in the
    header, and its constructor takes `bool use_cache = false`.

16. `Polygon_2::vertices_begin()/vertices_end()` are declared `const` but return the **mutable**
    `typename Container::iterator` (`typedef typename Container::iterator Vertex_iterator;` *and*
    `Vertex_const_iterator`). Don't rely on const-correctness there.

---

## 1. Header map

| Header | Contents |
| --- | --- |
| `CGAL/Boolean_set_operations_2.h` | umbrella: includes only `complement.h`, `difference.h`, `do_intersect.h`, `intersection.h`, `join.h`, `oriented_side.h`, `symmetric_difference.h` |
| `CGAL/General_polygon_set_2.h` | `General_polygon_set_2<Traits, Dcel>` (planar) |
| `CGAL/General_polygon_set_on_surface_2.h` | `General_polygon_set_on_surface_2<Traits, TopTraits>` + `PreconditionValidationPolicy` |
| `CGAL/Boolean_set_operations_2/Gps_on_surface_base_2.h` (+ `…_impl.h`) | `Gps_on_surface_base_2<Traits, TopTraits, ValidationPolicy>`, `NoValidationPolicy` — **all the real API lives here** |
| `CGAL/Polygon_set_2.h` | `Polygon_set_2<Kernel, Container, Dcel>` |
| `CGAL/General_polygon_2.h` | `General_polygon_2<Arr_traits>` |
| `CGAL/General_polygon_with_holes_2.h` | `General_polygon_with_holes_2<Polygon_>` |
| `CGAL/Polygon_2.h`, `CGAL/Polygon_with_holes_2.h` | linear polygon types |
| `CGAL/Gps_traits_2.h` | `Gps_traits_2<ArrTraits_2, GeneralPolygon_2>` |
| `CGAL/Gps_segment_traits_2.h` | `Gps_segment_traits_2<Kernel, Container, ArrSegmentTraits>` |
| `CGAL/Gps_circle_segment_traits_2.h` | `Gps_circle_segment_traits_2<Kernel, Filer_>` |
| `…/Gps_default_dcel.h` | `Gps_halfedge_base`, `Gps_face_base`, `Gps_default_dcel<Traits>` |
| `…/Gps_default_traits.h` | `Gps_default_traits<Polygon>` traits deduction |
| `…/Polygon_conversions.h` | `Gps_polyline_traits`, `convert_polygon*`, `Enable_if_Polygon_2_iterator`, `Iterator_to_gps_traits` |
| `…/Bso_internal_functions.h` | `s_*` / `r_*` / `_*` implementation functions behind the free functions |
| `…/Gps_polygon_validation.h` | `is_valid_polygon`, `is_valid_polygon_with_holes`, `is_valid_unknown_polygon`, … |
| `…/Ccb_curve_iterator.h` | `Ccb_curve_iterator<Arrangement>` |
| `…/Polygon_2_curve_iterator.h` | `Polygon_2_curve_iterator<XCv, Polygon>` (= `Gps_segment_traits_2::Curve_const_iterator`) |
| `…/Gps_traits_adaptor.h` | `Gps_traits_adaptor<Traits>` (`Construct_vertex_2`, `Orientation_2`) |
| `CGAL/connect_holes.h` | `connect_holes()` (**not** included by `Boolean_set_operations_2.h`) |
| internals (skim only) | `Gps_agg_*`, `Gps_bfs_*`, `Gps_merge.h`, `Gps_*_functor.h`, `Gps_polygon_simplifier.h`, `Gps_simplifier_traits.h`, `Gps_traits_decorator.h`, `Gps_insertion_meta_traits.h`, `Curve_with_halfedge.h`, `Point_with_vertex.h`, `Indexed_event.h` |

### Class hierarchy

```
Gps_on_surface_base_2<Traits_, TopTraits_, ValidationPolicy = NoValidationPolicy>
        ▲
General_polygon_set_on_surface_2<Traits_, TopTraits_>          // = base with PreconditionValidationPolicy
        ▲
General_polygon_set_2<Traits_, Dcel_ = Gps_default_dcel<Traits_>>
        │   TopTraits_ := Default_planar_topology<Traits_, Dcel_>::Traits
        ▲
Polygon_set_2<Kernel, Container = std::vector<Kernel::Point_2>,
              Dcel_ = Gps_default_dcel<Gps_segment_traits_2<Kernel, Container>>>
```

---

## 2. `General_polygon_set_2<Traits_, Dcel_>`

`CGAL/General_polygon_set_2.h`

```cpp
template <class Traits_, class Dcel_ = Gps_default_dcel<Traits_> >
class General_polygon_set_2 : public General_polygon_set_on_surface_2
  <Traits_, typename Default_planar_topology<Traits_, Dcel_>::Traits>
```

Public typedefs:

```cpp
typedef Traits_                                         Traits_2;
typedef Dcel_                                           Dcel;
typedef General_polygon_set_on_surface_2<Traits_2,
          typename Default_planar_topology<Traits_2, Dcel>::Traits>  Base;
typedef CGAL::Arrangement_2<Traits_2, Dcel>             Arrangement_2;
typedef typename Base::Polygon_2                        Polygon_2;             // = Traits_2::Polygon_2
typedef typename Base::Polygon_with_holes_2             Polygon_with_holes_2;  // = Traits_2::Polygon_with_holes_2
// inherited: Size (= Arrangement_on_surface_2::Size = Dcel::Size = std::size_t),
//            Arrangement_on_surface_2, Topology_traits
```

Constructors (all forwarding to `Base`):

```cpp
General_polygon_set_2();
General_polygon_set_2(const Traits_2& traits);
explicit General_polygon_set_2(const Polygon_2& pgn);
explicit General_polygon_set_2(const Polygon_with_holes_2& pwh);
explicit General_polygon_set_2(const Polygon_2& pgn, const Traits_2& traits);
explicit General_polygon_set_2(const Polygon_with_holes_2& pwh, const Traits_2& traits);
```

Note: the copy constructor / `operator=` are **not** redeclared here; the implicitly generated ones
call `General_polygon_set_on_surface_2`'s user-provided copy ctor / assignment, which deep-copy the
arrangement and the traits (see §4). Destruction is via the base's `virtual ~…`.

Members declared in this class (they exist only to defeat name hiding — see the header comment):

```cpp
using Base::intersection; using Base::join; using Base::symmetric_difference;
inline void intersection(const Self& ps1, const Self& ps2);
inline void join(const Self& ps1, const Self& ps2);
inline void symmetric_difference(const Self& ps1, const Self& ps2);
```

**Arrangement accessors (this is the reason to prefer `General_polygon_set_2` over the base):**

```cpp
/*! Obtain a const reference to the underlying arrangement */
const Arrangement_2& arrangement() const
{ return *(static_cast<const Arrangement_2*>(this->m_arr)); }

/*! Obtain a reference to the underlying arrangement */
Arrangement_2& arrangement()
{ return *(static_cast<Arrangement_2*>(this->m_arr)); }
```

*Lifetime*: the reference is valid only until the next Boolean operation (§0.3). The non-const
overload lets you break the Gps invariants (contained flags / curve directions); after hand-editing
the arrangement you must restore them yourself, then `remove_redundant_edges()` and check `is_valid()`.

**Can the DCEL be swapped for a user DCEL? Yes** *(verified)*. Requirements (§8): the face type must
derive from `CGAL::Gps_face_base` (supplies `contained()/set_contained()`, `visited()/set_visited()`,
`id()/set_id()/id_not_set()/reset_id()`, and `virtual void assign(const Arr_face_base&)`), and the
halfedge type must derive from `CGAL::Gps_halfedge_base<X_monotone_curve_2>` (supplies
`int flag()` / `void set_flag(int)`, used by `_remove_redundant_edges`). The vertex type is a plain
`Arr_vertex_base<Traits::Point_2>`.

---

## 3. `General_polygon_set_on_surface_2<Traits_, TopTraits_>`

`CGAL/General_polygon_set_on_surface_2.h`

```cpp
namespace Boolean_set_operation_2_internal {
  struct PreconditionValidationPolicy {
    template <class Polygon, class Traits>
    inline static void is_valid(const Polygon& p, const Traits& t)
    { CGAL_precondition(is_valid_unknown_polygon(p, t)); CGAL_USE(p); CGAL_USE(t); }
  };
}

template <class Traits_, class TopTraits_>
class General_polygon_set_on_surface_2 :
  public Gps_on_surface_base_2<Traits_, TopTraits_,
           Boolean_set_operation_2_internal::PreconditionValidationPolicy>
```

Public typedefs: `Polygon_2`, `Polygon_with_holes_2`, `Arrangement_on_surface_2` (all from `Base`).
`Traits_2`, `Self`, `Base` are **protected**.

```cpp
General_polygon_set_on_surface_2();
General_polygon_set_on_surface_2(const Traits_2& traits);
General_polygon_set_on_surface_2(const Self& ps);                       // deep copy
General_polygon_set_on_surface_2& operator=(const Self& ps);            // deep copy
explicit General_polygon_set_on_surface_2(const Polygon_2& pgn);
explicit General_polygon_set_on_surface_2(const Polygon_with_holes_2& pwh);
explicit General_polygon_set_on_surface_2(const Polygon_2& pgn, const Traits_2& traits);
explicit General_polygon_set_on_surface_2(const Polygon_with_holes_2& pwh, const Traits_2& traits);
protected:
  General_polygon_set_on_surface_2(Arrangement_on_surface_2* arr);       // takes ownership
public:
  virtual ~General_polygon_set_on_surface_2();

  void intersection(const Self& gps1, const Self& gps2);
  void join(const Self& gps1, const Self& gps2);
  void symmetric_difference(const Self& gps1, const Self& gps2);
  using Base::intersection; using Base::join; using Base::symmetric_difference;
```

---

## 4. `Gps_on_surface_base_2<Traits_, TopTraits_, ValidationPolicy>` — the full public API

`CGAL/Boolean_set_operations_2/Gps_on_surface_base_2.h` (+ `…_impl.h`)

```cpp
namespace Boolean_set_operation_2_internal {
  struct NoValidationPolicy {
    template <class Polygon, class Traits>
    inline static void is_valid(const Polygon&, const Traits&) {}
  };
}

template <class Traits_, class TopTraits_,
          class ValidationPolicy = Boolean_set_operation_2_internal::NoValidationPolicy>
class Gps_on_surface_base_2
```

### 4.1 Public typedefs

```cpp
typedef Traits_                                      Traits_2;
typedef TopTraits_                                   Topology_traits;
typedef typename Traits_2::Polygon_2                 Polygon_2;
typedef typename Traits_2::Polygon_with_holes_2      Polygon_with_holes_2;
typedef CGAL::Arrangement_on_surface_2<Traits_2, Topology_traits>  Arrangement_on_surface_2;
typedef typename Arrangement_on_surface_2::Size      Size;   // == Dcel::Size == std::size_t
```

Everything else (`Point_2`, `X_monotone_curve_2`, `Face_const_handle`,
`Ccb_halfedge_const_circulator`, `Point_location`, …) is **private**. To name those types from
outside, go through the arrangement type instead
(`General_polygon_set_2<…>::Arrangement_2::Ccb_halfedge_const_circulator`, etc.).
`Point_location` is `typename Topology_traits::Default_point_location_strategy`
(`Arr_walk_along_line_point_location` for the planar topologies, `Arr_naive_point_location` for
`Arr_spherical_topology_traits_2`).

Protected data (relevant to bindings):

```cpp
const Traits_2*                       m_traits;
CGAL::Arr_traits_adaptor_2<Traits_2>  m_traits_adaptor;
bool                                  m_traits_owner;
Aos_2*                                m_arr;     // Aos_2 = Arrangement_on_surface_2
```

### 4.2 Construction / destruction / assignment

```cpp
Gps_on_surface_base_2();                                   // owns a fresh Traits_2
Gps_on_surface_base_2(const Traits_2& tr);                 // stores &tr; DOES NOT COPY (lifetime!)
Gps_on_surface_base_2(const Self& ps);                     // deep copy of traits and arrangement
Gps_on_surface_base_2& operator=(const Self& ps);          // deep copy; self-assignment safe
explicit Gps_on_surface_base_2(const Polygon_2& pgn);
explicit Gps_on_surface_base_2(const Polygon_2& pgn, const Traits_2& tr);
explicit Gps_on_surface_base_2(const Polygon_with_holes_2& pgn_with_holes);
explicit Gps_on_surface_base_2(const Polygon_with_holes_2& pgn_with_holes, const Traits_2& tr);
protected:
  Gps_on_surface_base_2(Aos_2* arr);                       // takes ownership of arr, own traits
public:
  virtual ~Gps_on_surface_base_2();                        // delete m_arr; delete m_traits if owner
```

All polygon-taking constructors run `ValidationPolicy::is_valid(pgn, *m_traits)` **before** inserting.

### 4.3 Insertion

```cpp
// insert a simple polygon
void insert(const Polygon_2& pgn);
// insert a polygon with holes
void insert(const Polygon_with_holes_2& pgn_with_holes);

// insert a range of polygons that can be either simple polygons or polygons with holes
// precondition: the polygons are disjoint and simple
template <typename PolygonIterator>
void insert(PolygonIterator pgn_begin, PolygonIterator pgn_end);

// insert two ranges of : the first one for simple polygons,
// the second one for polygons with holes
// precondition: the first range is disjoint simple polygons
//               the second range is disjoint polygons with holes
template <typename PolygonIterator, typename PolygonWithHolesIterator>
void insert(PolygonIterator pgn_begin, PolygonIterator pgn_end,
            PolygonWithHolesIterator pgn_with_holes_begin,
            PolygonWithHolesIterator pgn_with_holes_end);
```

**Preconditions (hard, not checked):** the inserted polygon must be *completely disjoint* from what
the set already contains (`_insert` asserts the located face is not contained and uses
`insert_in_face_interior` / `insert_from_left_vertex` / `insert_from_right_vertex` /
`Arr_accessor::insert_at_vertices_ex`, i.e. **non-intersecting insertion only**). For overlapping
input use `join()`. The range versions use `insert_non_intersecting_curves(...)` plus a BFS face
initialisation (`Init_faces_visitor`), so the same disjointness precondition applies.
`insert()` mutates the existing arrangement in place (no pointer swap).

### 4.4 Boolean operations — member overload families

| operation | `Polygon_2` | `Polygon_with_holes_2` | `const Self&` | binary `(Self, Self)` | range | 2 ranges |
| --- | --- | --- | --- | --- | --- | --- |
| `join` | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ |
| `intersection` | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ |
| `difference` | ✔ | ✔ | ✔ | ✔ | **✘** | **✘** |
| `symmetric_difference` | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ |
| `complement` | — | — | ✔ (`complement(const Self&)`) | — | — | — |
| `do_intersect` | ✔ `const` | ✔ `const` | ✔ `const` | — | ✔ (**inverted!**) | ✔ (**inverted!**) |

Exact signatures:

```cpp
void intersection(const Polygon_2& pgn);
void intersection(const Polygon_with_holes_2& pgn);
void intersection(const Self& other);
void intersection(const Self& gps1, const Self& gps2);          // this->clear() first

void join(const Polygon_2& pgn);
void join(const Polygon_with_holes_2& pgn);
void join(const Self& other);
void join(const Self& gps1, const Self& gps2);                  // this->clear() first

void difference (const Polygon_2& pgn);
void difference (const Polygon_with_holes_2& pgn);
void difference (const Self& other);
void difference(const Self& gps1, const Self& gps2);            // this->clear() first

void symmetric_difference(const Polygon_2& pgn);
void symmetric_difference(const Polygon_with_holes_2& pgn);
void symmetric_difference(const Self& other);
void symmetric_difference(const Self& gps1, const Self& gps2);  // this->clear() first

void complement();                                              // in place: flips contained() on
                                                                // every face, and replaces every
                                                                // edge curve by construct_opposite_2
void complement(const Self& other);                             // *m_arr = *other.m_arr; complement();

void fix_curves_direction();                                    // re-orients edge curves to match
                                                                // the contained() flags
```

Aggregated (divide-and-conquer, `k` = threshold at which the sweep-based aggregate kicks in;
"5 is the magic number"):

```cpp
template <typename InputIterator>
void join(InputIterator begin, InputIterator end, unsigned int k = 5);
template <typename InputIterator>
inline void join(InputIterator begin, InputIterator end, Polygon_2&, unsigned int k = 5);
template <typename InputIterator>
inline void join(InputIterator begin, InputIterator end, Polygon_with_holes_2&, unsigned int k = 5);
template <typename InputIterator1, typename InputIterator2>
inline void join(InputIterator1 begin1, InputIterator1 end1,
                 InputIterator2 begin2, InputIterator2 end2, unsigned int k = 5);

template <typename InputIterator>
inline void intersection(InputIterator begin, InputIterator end, unsigned int k = 5);
template <typename InputIterator>
inline void intersection(InputIterator begin, InputIterator end, Polygon_2&, unsigned int k);
template <typename InputIterator>
inline void intersection(InputIterator begin, InputIterator end, Polygon_with_holes_2&, unsigned int k);
template <typename InputIterator1, typename InputIterator2>
inline void intersection(InputIterator1 begin1, InputIterator1 end1,
                         InputIterator2 begin2, InputIterator2 end2, unsigned int k = 5);

template <typename InputIterator>
inline void symmetric_difference(InputIterator begin, InputIterator end, unsigned int k = 5);
template <typename InputIterator>
inline void symmetric_difference(InputIterator begin, InputIterator end, Polygon_2&, unsigned int k = 5);
template <typename InputIterator>
inline void symmetric_difference(InputIterator begin, InputIterator end,
                                 Polygon_with_holes_2&, unsigned int k = 5);
template <typename InputIterator1, typename InputIterator2>
inline void symmetric_difference(InputIterator1 begin1, InputIterator1 end1,
                                 InputIterator2 begin2, InputIterator2 end2, unsigned int k = 5);
```

The 3-argument forms with a dummy `Polygon_2&` / `Polygon_with_holes_2&` are tag-dispatch helpers;
the 2-iterator form picks one via
`typename std::iterator_traits<InputIterator>::value_type pgn; this->join(begin, end, pgn, k);`
and then calls `remove_redundant_edges(); _reset_faces();`. **The dummy-arg forms do *not* call
`remove_redundant_edges()`** — do not call them directly.

`do_intersect`:

```cpp
bool do_intersect(const Polygon_2& pgn) const;                 // validates pgn, builds a temp Self
bool do_intersect(const Polygon_with_holes_2& pgn_with_holes) const;
bool do_intersect(const Self& other) const;                    // correct: overlay + Gps_do_intersect_functor

template <typename InputIterator>
bool do_intersect(InputIterator begin, InputIterator end, unsigned int k = 5);          // NOT const, INVERTED
template <typename InputIterator1, typename InputIterator2>
bool do_intersect(InputIterator1 begin1, InputIterator1 end1,
                  InputIterator2 begin2, InputIterator2 end2, unsigned int k = 5);      // NOT const, INVERTED
```

`do_intersect(const Self&)` short-circuits: `if (is_empty() || other.is_empty()) return false;`
`if (is_plane() || other.is_plane()) return true;`.

### 4.5 Queries

```cpp
Size number_of_polygons_with_holes() const;      // O(n) BFS scan; mutates `visited` flags
const Traits_2& traits() const { return *m_traits; }

bool is_empty() const   { return (m_arr->is_empty() && !m_arr->faces_begin()->contained()); }
bool is_plane() const   { return (m_arr->is_empty() &&  m_arr->faces_begin()->contained()); }
void clear()            { m_arr->clear(); }      // keeps the arrangement object, drops its content
                                                 // NOTE: clear() resets the contained flag to the
                                                 // DCEL default (false) => an is_plane() set becomes empty.

Oriented_side oriented_side(const Point_2& q) const;                    // ON_POSITIVE_SIDE inside,
                                                                        // ON_NEGATIVE_SIDE outside,
                                                                        // ON_ORIENTED_BOUNDARY on an
                                                                        // edge or vertex
Oriented_side oriented_side(const Polygon_2& pgn) const;                // validates, then Self other(pgn)
Oriented_side oriented_side(const Polygon_with_holes_2& pgn) const;
Oriented_side oriented_side(const Self& other) const;
```

*(verified)* for a square `[0,4]²`: `oriented_side((3,3)) == 1` (`ON_POSITIVE_SIDE`),
`oriented_side((0,0)) == 0` (`ON_ORIENTED_BOUNDARY`), `oriented_side((9,9)) == -1`.

`oriented_side(const Self&)`: `ON_NEGATIVE_SIDE` if either is empty, `ON_POSITIVE_SIDE` if either is
the whole plane, otherwise overlay + `Gps_do_intersect_functor`:
`found_reg_intersection() → ON_POSITIVE_SIDE`, `found_boundary_intersection() → ON_ORIENTED_BOUNDARY`,
else `ON_NEGATIVE_SIDE`.

⚠ The `Polygon_2` / `Polygon_with_holes_2` overloads build `Self other(pgn);` — **with a
default-constructed traits object**, not `*m_traits`. Broken for stateful traits.

```cpp
// returns the location of the query point
bool locate(const Point_2& q, Polygon_with_holes_2& pgn) const;
```

Returns `false` if `q` lies in a non-contained face (`pgn` untouched); otherwise fills `pgn` with the
polygon-with-holes containing `q` and returns `true`. If `q` is on an edge or a vertex it picks the
adjacent contained face. If the containing region is unbounded, it is emitted via
`scan_contained_ubf` (unbounded outer boundary + holes). *(verified)*

```cpp
const Arrangement_on_surface_2& arrangement() const;   // returns *m_arr
      Arrangement_on_surface_2& arrangement();
```

### 4.6 Validity, simplification, redundant edges

```cpp
bool is_valid();                                       // NOT const
void remove_redundant_edges();                         // this->_remove_redundant_edges(m_arr)
void simplify(const Polygon_2& pgn, Polygon_with_holes_2& res);   // NOT const
```

`is_valid()` (via the protected `_is_valid(Aos_2&)`) checks, for every edge:
1. `CGAL::is_valid(arr)` (the arrangement's own structural validity);
2. `he->face() != he->twin()->face()`;
3. `he->face()->contained() != he->twin()->face()->contained()` (no redundant edges);
4. the stored curve direction agrees with the halfedge direction **iff** the incident face is
   contained: `has_same_dir = (cmp_endpoints(cv) == he_res)` where
   `he_res = (he->direction()==ARR_LEFT_TO_RIGHT) ? SMALLER : LARGER`; invalid when
   `(is_cont && !has_same_dir) || (!is_cont && has_same_dir)`.

This is the invariant a type-erased core must preserve if it edits `arrangement()` directly.

`remove_redundant_edges()` deletes every edge whose two incident faces have the same `contained()`
flag, merging the faces with a union-find and re-wiring outer/inner CCBs. It asserts that there is
exactly one unbounded face (`CGAL_assertion(std::distance(arr->unbounded_faces_begin(),
arr->unbounded_faces_end()) == 1);`) — **this assertion does not hold on the sphere**. Special case:
if every edge is redundant, the arrangement is cleared and the unbounded face's `contained` flag
restored.

`simplify(pgn, res)` builds a private arrangement with `Gps_polygon_simplifier`, removes redundant
edges, resets faces and writes the single resulting p-w-h into `res` (through
`Oneset_iterator<Polygon_with_holes_2>`). Useful to turn a self-intersecting polygon into a valid
p-w-h before inserting it.

### 4.7 Extraction / CCB helpers (public, but with private parameter types)

```cpp
template <typename OutputIterator>
OutputIterator polygons_with_holes(OutputIterator out) const;   // value_type = Polygon_with_holes_2

static void construct_polygon(Ccb_halfedge_const_circulator ccb,
                              Polygon_2& pgn, const Traits_2* tr);

bool is_hole_of_face(Face_const_handle f, Halfedge_const_handle he) const;

Ccb_halfedge_const_circulator get_boundary_of_polygon(Face_const_iterator f) const;
```

`construct_polygon` is `static` and does:

```cpp
typedef CGAL::Ccb_curve_iterator<Arrangement_on_surface_2> Ccb_curve_iterator;
Ccb_curve_iterator begin(ccb, false);
Ccb_curve_iterator end(ccb, true);
tr->construct_polygon_2_object()(begin, end, pgn);
```

`get_boundary_of_polygon(f)` **sets and relies on the `visited` flags** (`CGAL_assertion(!f->visited());
f->set_visited(true);`) and `CGAL_error_msg("Not implemented yet.")` if a face has more than one outer
CCB. Callers must `_reset_faces()` afterwards — `locate()` does exactly that.
`polygons_with_holes()` resets the flags itself at the end of `Arr_bfs_scanner::scan`.

---

## 5. `Polygon_set_2<Kernel, Container, Dcel_>`

`CGAL/Polygon_set_2.h`

```cpp
template <class Kernel,
          typename Containter = std::vector<typename Kernel::Point_2>,
          class Dcel_ = Gps_default_dcel<Gps_segment_traits_2<Kernel, Containter> > >
class Polygon_set_2 :
  public General_polygon_set_2<Gps_segment_traits_2<Kernel, Containter>, Dcel_>
```

(the template parameter really is spelled `Containter`.)

```cpp
typedef typename Base::Traits_2               Traits_2;              // Gps_segment_traits_2<Kernel,Container>
typedef typename Base::Polygon_2              Polygon_2;             // CGAL::Polygon_2<Kernel,Container>
typedef typename Base::Polygon_with_holes_2   Polygon_with_holes_2;  // CGAL::Polygon_with_holes_2<Kernel,Container>
typedef typename Base::Arrangement_2          Arrangement_2;         // CGAL::Arrangement_2<Traits_2, Dcel_>
typedef typename Base::Size                   Size;

Polygon_set_2();
Polygon_set_2(const Base& base);                       // NOT explicit
Polygon_set_2(const Traits_2& tr);
explicit Polygon_set_2(const Polygon_2& pgn);
explicit Polygon_set_2(const Polygon_with_holes_2& pwh);
```

Then thin forwarders (all `inline`, all returning `void` except `do_intersect`):
`intersection`, `join`, `difference`, `symmetric_difference` — each with the overloads
`(const Polygon_2&)`, `(const Polygon_with_holes_2&)`, `(const Self&)`, `(const Self&, const Self&)`,
`(InputIterator, InputIterator)`, `(InputIterator1, InputIterator1, InputIterator2, InputIterator2)`
— and `do_intersect` with `(const Polygon_2&)`, `(const Polygon_with_holes_2&)`, `(const Self&)`,
`(InputIterator, InputIterator)`, `(InputIterator1×2, InputIterator2×2)`.
Note the range forwarders **drop the `k` parameter** (always `k = 5`), and the `difference` range
forwarders do not compile (§0.7). `insert`, `complement`, `oriented_side`, `locate`,
`polygons_with_holes`, `number_of_polygons_with_holes`, `is_empty`, `is_plane`, `is_valid`, `clear`,
`traits`, `arrangement`, `remove_redundant_edges`, `simplify` are inherited unchanged.

*(verified)* `Polygon_set_2<Epeck>::Arrangement_2` is exactly
`CGAL::Arrangement_2<CGAL::Gps_segment_traits_2<Epeck>, CGAL::Gps_default_dcel<CGAL::Gps_segment_traits_2<Epeck>>>`
(`static_assert` passed).

---

## 6. `Gps_default_dcel` / face & halfedge bases

`CGAL/Boolean_set_operations_2/Gps_default_dcel.h`

```cpp
template <class X_monotone_curve_2>
class Gps_halfedge_base : public Arr_halfedge_base<X_monotone_curve_2>
{
  int _flag;                                   // -1 in the default ctor
public:
  typedef Arr_halfedge_base<X_monotone_curve_2> Base;
  Gps_halfedge_base();
  int  flag() const;
  void set_flag(int i);
};

class Gps_face_base : public Arr_face_base
{
protected:
  mutable char m_info;                         // bit 1 = CONTAINED, bit 2 = VISITED
  enum { CONTAINED = 1, VISITED = 2 };
  std::size_t _id;                             // std::size_t(-1) == "not set"
public:
  Gps_face_base();
  virtual void assign(const Arr_face_base& f); // copies m_info (NOT _id)
  bool contained() const;
  void set_contained(bool b);
  bool visited() const;
  void set_visited(bool b) const;              // const! m_info is mutable
  Arr_face_base::Outer_ccbs_container& _outer_ccbs();
  Arr_face_base::Inner_ccbs_container& _inner_ccbs();
  std::size_t id() const;
  bool id_not_set() const;
  void set_id(std::size_t i);
  void reset_id();
};

template <class Traits_>
class Gps_default_dcel :
  public Arr_dcel_base<Arr_vertex_base<typename Traits_::Point_2>,
                       Gps_halfedge_base<typename Traits_::X_monotone_curve_2>,
                       Gps_face_base >
{ public: Gps_default_dcel() {} };
```

**`contained()` is the "inside" flag on faces** — this is the whole point-set representation.
`visited()` and `id()` are scratch fields used by the extraction BFS and by
`_remove_redundant_edges`; do not repurpose them.

Custom DCEL *(verified to compile and run)*:

```cpp
struct My_face : public CGAL::Gps_face_base {
  int tag = 0;
  virtual void assign(const CGAL::Arr_face_base& f) {
    CGAL::Gps_face_base::assign(f);
    tag = static_cast<const My_face&>(f).tag;
  }
};
struct My_dcel : public CGAL::Arr_dcel_base<
    CGAL::Arr_vertex_base<GpsTr::Point_2>,
    CGAL::Gps_halfedge_base<GpsTr::X_monotone_curve_2>,
    My_face> { My_dcel() {} };
typedef CGAL::Polygon_set_2<K, std::vector<K::Point_2>, My_dcel> Pset;
```

Caveat: per-face user data does **not** survive a Boolean operation — the result arrangement is a
fresh object built by `overlay`, and the overlay functors (`Gps_join_functor`, …) only set
`contained`. Only `assign()`-based copies (arrangement copy/assignment) carry it.

> **Cross-reference — extended DCEL + Booleans:** `dcel_and_accessor.md` **§15** reconciles this
> section with `Arr_extended_dcel` and answers the questions this one leaves open: the exact
> requirements the `Dcel_` parameter must satisfy (§15.1, with compiler proofs); that
> `Arr_extended_dcel<Traits, V, H, F, Arr_vertex_base<Point_2>, Gps_halfedge_base<X_monotone_curve_2>,
> Gps_face_base>` (and the `Arr_face_extended_dcel` variant) **do** compile and run as the `Dcel_` of a
> `General_polygon_set_2` (§15.2); hard evidence that `arrangement()`'s `static_cast` is formally UB
> but sound — `dynamic_cast` fails and `typeid` reports the base (§15.3); a **measured table of which
> Boolean calls replace the arrangement object and which do not** (§15.4.1 — the 3-arg
> `res.join(a, b)` form preserves it, `ps.join(other)` does not); the absence of any overlay-traits
> hook and the DIY `CGAL::overlay` + `remove_redundant_edges()` escape hatch that *does* carry data
> (§15.4.3); what happens to an attached `Arr_observer` (silently detached, **not** dangling — but
> `before_detach()` segfaults if it reads geometry, §15.5); the measured cost of the
> polygon → own-arrangement round trip (5–30 % of the Boolean op, §15.6, recommended); and why
> `Arrangement_with_history_2` can never be BSO's arrangement (§15.7).

---

## 7. `General_polygon_2<Arr_traits>`

`CGAL/General_polygon_2.h`

```cpp
template <class Arr_traits>
class General_polygon_2
{
public:
  typedef Arr_traits                                 Traits_2;
  typedef typename Traits_2::Point_2                 Point_2;
  typedef typename Traits_2::Curve_2                 Curve_2;
  typedef typename Traits_2::X_monotone_curve_2      X_monotone_curve_2;
  typedef std::list<X_monotone_curve_2>              Containter;      // sic
  typedef typename Containter::iterator              Curve_iterator;
  typedef typename Containter::const_iterator        Curve_const_iterator;
  typedef X_monotone_curve_2                         value_type;
protected:
  std::list<X_monotone_curve_2>    m_xcurves;
public:
  General_polygon_2() {}
  template <typename CurveIterator> General_polygon_2(CurveIterator begin, CurveIterator end);

  template <class CurveIterator> void init(CurveIterator begin, CurveIterator end);   // clear + insert
  template <class CurveIterator> void insert(CurveIterator begin, CurveIterator end); // append

  bool         is_empty() const;
  unsigned int size() const;                       // static_cast<unsigned int>(m_xcurves.size())

  Curve_iterator       curves_begin();
  Curve_iterator       curves_end();
  Curve_const_iterator curves_begin() const;
  Curve_const_iterator curves_end() const;

  void           push_back(const X_monotone_curve_2& cv);
  void           clear();
  Curve_iterator erase(Curve_iterator it);

  Orientation orientation() const;                 // Gps_traits_adaptor<Traits_2>::Orientation_2
  void        reverse_orientation();               // list::reverse + Construct_opposite_2 on each curve

  template <class OutputIterator> void approximate(OutputIterator oi, unsigned int n) const;
  Bbox_2 bbox() const;
};

template <class Traits> std::istream& operator>>(std::istream&, General_polygon_2<Traits>&);
template <class Traits> std::ostream& operator<<(std::ostream&, const General_polygon_2<Traits>&);
```

Notes for bindings:
* the container is a **`std::list`** — `size()` is O(n) on some standard libraries (O(1) on libc++/libstdc++),
  and iterators are only bidirectional.
* `orientation()` and `reverse_orientation()` **default-construct a `Traits_2`** internally
  (`Gps_traits_adaptor<Traits_2> tr;` / `Traits_2 tr;`) — broken for stateful traits (e.g. conic,
  Bézier or a cached circle-segment traits).
* `approximate(oi, n)` forwards to `X_monotone_curve_2::approximate(oi, n)`; that member only exists
  for some traits (circle-segment does, segment does not) — it is a template, so it only fails if used.
* there is **no** `is_valid()`, `is_simple()` or `is_closed()` member; use the free functions in
  `Gps_polygon_validation.h` (§18).
* the boundary is stored as an *ordered, closed* chain of x-monotone curves; each curve's own
  direction matters (see `Compare_endpoints_xy_2`), the CCW/CW convention lives in those directions.

---

## 8. `General_polygon_with_holes_2<Polygon_>`

`CGAL/General_polygon_with_holes_2.h` — models `GeneralPolygonWithHoles_2`.

```cpp
template <typename Polygon_>
class General_polygon_with_holes_2 {
public:
  typedef Polygon_                                    Polygon_2;
  typedef Polygon_2                                   General_polygon_2;   // backward compat
  typedef std::deque<Polygon_2>                       Holes_container;
  typedef typename Holes_container::iterator          Hole_iterator;
  typedef typename Holes_container::const_iterator    Hole_const_iterator;
  typedef unsigned int                                Size;                // NOT std::size_t

  General_polygon_with_holes_2() = default;
  explicit General_polygon_with_holes_2(const Polygon_2& pgn_boundary);
  explicit General_polygon_with_holes_2(Polygon_2&& pgn_boundary);
  template <typename HolesInputIterator>
  General_polygon_with_holes_2(const Polygon_2& pgn_boundary,
                               HolesInputIterator h_begin, HolesInputIterator h_end);
  template <typename HolesInputIterator>
  General_polygon_with_holes_2(Polygon_2&& pgn_boundary,
                               HolesInputIterator h_begin, HolesInputIterator h_end);

  Holes_container&       holes();
  const Holes_container& holes() const;
  Hole_iterator          holes_begin();
  Hole_iterator          holes_end();
  Hole_const_iterator    holes_begin() const;
  Hole_const_iterator    holes_end() const;

  bool             is_unbounded() const { return m_pgn.is_empty(); }
  Polygon_2&       outer_boundary();
  const Polygon_2& outer_boundary() const;

  void add_hole(const Polygon_2& pgn_hole);
  void add_hole(Polygon_2&& pgn_hole);
  void erase_hole(Hole_iterator hit);
  void clear_outer_boundary();
  void clear_holes();
  bool has_holes() const;
  Size number_of_holes() const;
  void clear();                       // clears boundary and holes
  bool is_plane() const;              // outer boundary empty AND no holes
  bool is_empty() const;              // outer boundary empty AND every hole empty
protected:
  Polygon_2       m_pgn;
  Holes_container m_holes;
};

template <typename Polygon_> std::ostream& operator<<(std::ostream&, const General_polygon_with_holes_2<Polygon_>&);
template <typename Polygon_> std::istream& operator>>(std::istream&, General_polygon_with_holes_2<Polygon_>&);
```

**`is_unbounded()` means "the outer boundary polygon is empty"** — that is how the complement of a
bounded region is represented (an unbounded p-w-h whose holes are the bounded pieces)
*(verified: `complement` of a square → `is_unbounded()==1`, `number_of_holes()==1`, hole orientation
`CLOCKWISE`)*.
Note the confusing pair `is_plane()` (nothing at all → the whole plane) vs `is_empty()` (boundary and
all holes empty) — with no holes they coincide.

---

## 9. `Polygon_2` / `Polygon_with_holes_2` (public API, condensed)

`CGAL/Polygon_2.h`

```cpp
template <class Traits_, class Container_ = std::vector<typename Traits_::Point_2> >
class Polygon_2 {
public:
  typedef Traits_ Traits;  typedef Container_ Container;
  typedef typename Traits_::FT FT;  typedef typename Traits_::Point_2 Point_2;
  typedef typename Traits_::Segment_2 Segment_2;
  typedef typename Container_::difference_type difference_type;
  typedef typename Container_::value_type value_type;
  typedef typename Container_::iterator iterator;   typedef typename Container_::const_iterator const_iterator;
  typedef typename Container::iterator       Vertex_const_iterator;
  typedef typename Container::iterator       Vertex_iterator;
  typedef Polygon_circulator<Container_>     Vertex_const_circulator;
  typedef Vertex_const_circulator            Vertex_circulator;
  typedef Polygon_2_edge_iterator<Traits_,Container_>            Edge_const_iterator;
  typedef Polygon_2_const_edge_circulator<Traits_,Container_>    Edge_const_circulator;
  typedef Polygon_2_edge_iterator<Traits_,Container_,Tag_false>  Vertex_pair_iterator;   // used by Gps
  typedef Container                          Vertices;
  typedef Iterator_range<Edge_const_iterator> Edges;

  Polygon_2() = default;
  Polygon_2(const Traits& p_traits);
  template <class InputIterator> Polygon_2(InputIterator first, InputIterator last,
                                           Traits p_traits = Traits());

  void           set(Vertex_iterator i, const Point_2& q);
  void           set(Polygon_circulator<Container> const& i, const Point_2& q);
  Vertex_iterator insert(Vertex_iterator i, const Point_2& q);
  Vertex_iterator insert(Vertex_circulator i, const Point_2& q);
  template <class InputIterator> void insert(Vertex_iterator i, InputIterator first, InputIterator last);
  template <class InputIterator> void insert(Vertex_circulator i, InputIterator first, InputIterator last);
  void            push_back(const Point_2& x);
  Vertex_iterator erase(Vertex_iterator i);
  Vertex_iterator erase(Vertex_iterator first, Vertex_iterator last);
  void            clear();
  void            reverse_orientation();

  Vertex_iterator      vertices_begin() const;      // const method, mutable iterator (!)
  Vertex_iterator      vertices_end()   const;
  const Vertices&      vertices() const;
  Vertex_circulator    vertices_circulator() const;
  Edge_const_iterator  edges_begin() const;
  Edge_const_iterator  edges_end() const;
  Edges                edges() const;
  Edge_const_circulator edges_circulator() const;
  Vertex_pair_iterator vertex_pairs_begin() const;  // \cond SKIP_IN_MANUAL
  Vertex_pair_iterator vertex_pairs_end() const;

  bool          is_simple() const;
  bool          is_convex() const;
  Orientation   orientation() const;                       // \pre p.is_simple()
  Oriented_side oriented_side(const Point_2& value) const; // \pre p.is_simple()
  Bounded_side  bounded_side(const Point_2& value) const;  // CGAL_precondition(is_simple())
  Bbox_2        bbox() const;
  FT            area() const;                              // signed: >0 for CCW
  Vertex_const_iterator left_vertex() const;  right_vertex(); top_vertex(); bottom_vertex();
  bool is_counterclockwise_oriented() const;  is_clockwise_oriented(); is_collinear_oriented();
  bool has_on_positive_side(const Point_2&) const;  has_on_negative_side(); has_on_boundary();
       has_on_bounded_side(); has_on_unbounded_side();
  const Point_2& vertex(std::size_t i) const;    Point_2& vertex(std::size_t i);
  const Point_2& operator[](std::size_t i) const; Point_2& operator[](std::size_t i);
  Segment_2      edge(std::size_t i) const;
  std::size_t    size() const;
  bool           is_empty() const;
  const Container_& container() const;   Container_& container();
  typename Container_::iterator begin();  end();
  const typename Container_::const_iterator begin() const;  end() const;
  void resize(std::size_t s);   void reserve(std::size_t s);
  bool identical(const Polygon_2<Traits_,Container_>& q) const;
  Traits_ const& traits_member() const;
};
// free: operator==, operator!=, transform(t, p), operator>>, operator<<
```

`CGAL/Polygon_with_holes_2.h`

```cpp
template <class Kernel, class Container_ = std::vector<typename Kernel::Point_2> >
class Polygon_with_holes_2 :
  public General_polygon_with_holes_2<CGAL::Polygon_2<Kernel, Container_> >
{
public:
  typedef Kernel                                     Traits;
  typedef Container_                                 Container;
  typedef CGAL::Polygon_2<Kernel, Container>         Polygon_2;
  typedef General_polygon_with_holes_2<Polygon_2>    Base;
  typedef typename Base::Hole_const_iterator         Hole_const_iterator;
  typedef typename Base::Size                        Size;

  Polygon_with_holes_2() = default;
  Polygon_with_holes_2(const Base& base);                        // NOT explicit
  explicit Polygon_with_holes_2(const Polygon_2& pgn_boundary);
  explicit Polygon_with_holes_2(Polygon_2&& pgn_boundary);
  template <class HolesInputIterator>
  Polygon_with_holes_2(const Polygon_2& pgn_boundary, HolesInputIterator h_begin, HolesInputIterator h_end);
  template <class HolesInputIterator>
  Polygon_with_holes_2(Polygon_2&& pgn_boundary, HolesInputIterator h_begin, HolesInputIterator h_end);

  Bbox_2 bbox() const { return this->outer_boundary().bbox(); }   // ignores holes (fine)
};
// free: transform(t, pwh), operator<<, operator>>
```

Everything else (`outer_boundary`, `holes*`, `add_hole`, `number_of_holes`, `is_unbounded`, …) comes
from `General_polygon_with_holes_2`.

---

## 10. Arrangement face ⇄ `General_polygon_with_holes_2`

### 10.1 Face → polygon(-with-holes)

Two mechanisms:

**(a) The whole set at once** — `polygons_with_holes(OutputIterator)`. Implemented by
`Arr_bfs_scanner<Arrangement, OutputIterator>` in `Gps_on_surface_base_2_impl.h`:

* it iterates over faces with `number_of_outer_ccbs() == 0` (i.e. the unbounded face(s));
* for a **non-contained** unbounded face it calls `scan_ccb(*inner_ccb)` for each of its inner CCBs;
* `scan_ccb` builds the outer boundary via `construct_polygon(ccb, pgn_boundary, m_traits)` and
  collects holes from the **outer CCBs of the non-contained faces** it reaches
  (`all_incident_faces` → `m_pgn_holes`), then emits
  `m_traits->construct_polygon_with_holes_2_object()(pgn_boundary, m_pgn_holes.begin(), m_pgn_holes.end())`;
* for a **contained** unbounded face it calls `scan_contained_ubf(ubf)`, which emits a p-w-h with an
  **empty** outer boundary (`Polygon_2 boundary;`) ⇒ `is_unbounded() == true`.

**(b) One CCB at a time** — `Gps_on_surface_base_2::construct_polygon(ccb, pgn, tr)` +
`CGAL::Ccb_curve_iterator<Arrangement>` (§13).

**Verified orientation table** (square `[0,10]²` with hole `[3,7]²`, `Epeck`,
`construct_polygon(ccb, pgn, traits)` for each CCB):

| face | `contained()` | CCB used | resulting `Polygon_2::orientation()` | meaning |
| --- | --- | --- | --- | --- |
| unbounded outer face | `false` | its **inner** CCB | `COUNTERCLOCKWISE` | ✅ the region's outer boundary |
| the hole face (bounded, inside) | `false` | its **outer** CCB | `CLOCKWISE` | ✅ a hole |
| the region face | `true` | its **outer** CCB | `CLOCKWISE` | ✗ reversed, do not use directly |
| the region face | `true` | its **inner** CCB | `COUNTERCLOCKWISE` | ✗ wrong sense for a hole |

Rule of thumb: **always take the CCB seen from the non-contained side.** For an outer boundary use
the inner CCB of the surrounding non-contained face (equivalently `he->twin()` of the contained
face's outer CCB); for holes use the outer CCB of the enclosed non-contained faces. This is exactly
what `Arr_bfs_scanner` does. The reason is that `Ccb_curve_iterator::operator++` does `--_circ`
(reverse traversal) while `Construct_polygon_2` reads each curve's own direction.

`polygons_with_holes()` on the same input *(verified)* returns outer boundary `COUNTERCLOCKWISE`,
hole `CLOCKWISE` — i.e. exactly the convention required to feed the result back into `insert()`.

### 10.2 Polygon(-with-holes) → arrangement

```cpp
protected: void _insert(const Polygon_2& pgn, Aos_2& arr);         // one closed boundary, disjoint
public:    void _insert(const Polygon_with_holes_2& pgn, Aos_2& arr);  // public "by accident"
protected: template<typename PolygonIter> void _insert(PolygonIter, PolygonIter, Polygon_2&);
protected: template<typename PolygonIter> void _insert(PolygonIter, PolygonIter, Polygon_with_holes_2&);
protected: template <typename OutputIterator> void _construct_curves(const Polygon_2&, OutputIterator);
protected: template <typename OutputIterator> void _construct_curves(const Polygon_with_holes_2&, OutputIterator);
```

`_construct_curves(pwh, oi)` = outer boundary curves (skipped when `is_unbounded_object()(pwh)`) +
all hole curves. `_insert(pwh, arr)` then calls `insert_non_intersecting_curves(arr, …)`, sets
`contained` on every face with `number_of_outer_ccbs()==0` if the p-w-h is unbounded, and runs a
`Gps_bfs_scanner` with `Init_faces_visitor` (`new_f->set_contained(!old_f->contained())`) to paint
the faces, finishing with `_reset_faces(&arr)`.

For the *public* path just use `insert()`; for a "build an arrangement from a p-w-h without a set"
path, `_insert(pwh, arr)` is callable (it is public) but takes the raw
`Arrangement_on_surface_2&`.

---

## 11. `Gps_traits_2<ArrTraits_2, GeneralPolygon_2>`

`CGAL/Gps_traits_2.h`

```cpp
template <typename ArrTraits_2,
          typename GeneralPolygon_2 = General_polygon_2<ArrTraits_2> >
class Gps_traits_2 : public ArrTraits_2
```

Public typedefs:

```cpp
typedef typename Base::Point_2                        Point_2;
typedef typename Base::X_monotone_curve_2             X_monotone_curve_2;
typedef typename Base::Multiplicity                   Multiplicity;
typedef GeneralPolygon_2                              Polygon_2;
typedef Polygon_2                                     General_polygon_2;            // backward compat
typedef CGAL::General_polygon_with_holes_2<Polygon_2> Polygon_with_holes_2;
typedef Polygon_with_holes_2                          General_polygon_with_holes_2; // backward compat
typedef typename Polygon_2::Curve_const_iterator      Curve_const_iterator;
typedef typename Polygon_with_holes_2::Hole_const_iterator Hole_const_iterator;
typedef typename Base::Compare_endpoints_xy_2         Compare_endpoints_xy_2;
typedef typename Base::Construct_min_vertex_2         Construct_min_vertex_2;
typedef typename Base::Construct_max_vertex_2         Construct_max_vertex_2;
typedef Gps_traits_adaptor<Base>                      Traits_adaptor;
```

Functors and their accessors:

```cpp
class Construct_polygon_2 {
public:
  template <class XCurveIterator>
  void operator()(XCurveIterator begin, XCurveIterator end, Polygon_2& pgn) const { pgn.init(begin, end); }
};
Construct_polygon_2 construct_polygon_2_object() const;

class Construct_curves_2 {
public:
  std::pair<Curve_const_iterator, Curve_const_iterator> operator()(const Polygon_2& pgn) const
  { return std::make_pair(pgn.curves_begin(), pgn.curves_end()); }
};
Construct_curves_2 construct_curves_2_object() const;

class Construct_outer_boundary {                      // returns BY VALUE (copies!)
public: Polygon_2 operator()(const Polygon_with_holes_2& pol_wh) const;
};
Construct_outer_boundary construct_outer_boundary_object() const;

class Construct_holes {
public: std::pair<Hole_const_iterator, Hole_const_iterator> operator()(const Polygon_with_holes_2&) const;
};
Construct_holes construct_holes_object() const;

class Construct_polygon_with_holes_2 {
public:
  Polygon_with_holes_2 operator()(const Polygon_2& pgn_boundary) const;
  template <class HolesInputIterator>
  Polygon_with_holes_2 operator()(const Polygon_2& pgn_boundary,
                                  HolesInputIterator h_begin, HolesInputIterator h_end) const;
};
Construct_polygon_with_holes_2 construct_polygon_with_holes_2_object() const;

class Is_unbounded { public: bool operator()(const Polygon_with_holes_2& pol_wh) const; };
Is_unbounded is_unbounded_object() const;

class Equal_2 {                                       // ctor is protected; friend class Gps_traits_2
public:
  bool operator()(const Point_2& p1, const Point_2& p2) const;
  bool operator()(const X_monotone_curve_2& cv1, const X_monotone_curve_2& cv2) const;
  bool operator()(const Polygon_2& pgn1, const Polygon_2& pgn2) const;              // cyclic compare, BUGGY
  bool operator()(const Polygon_with_holes_2& pgn1, const Polygon_with_holes_2& pgn2) const;  // BUGGY
};
Equal_2 equal_2_object() const { return Equal_2(*this); }
```

`Is_valid_2` is **commented out** in 6.1 (the `/*typedef CGAL::Is_valid_2<Self, Traits_adaptor> Is_valid_2; …*/`
block). There is no `is_valid_2_object()`. Use the free validation functions (§18).

Precondition documented on `Construct_polygon_with_holes_2` (verbatim):
> constructs a general polygon with holes using a given general polygon `outer` as the outer boundary
> and a given range of holes. If `outer` is an empty general polygon, then an unbounded polygon with
> holes will be created. The holes must be contained inside the outer boundary, and the polygons
> representing the holes must be strictly simple and pairwise disjoint, except perhaps at the vertices.

### Which arrangement traits qualify?

`Gps_on_surface_base_2` privately requires from `Traits_2`:
`Polygon_2`, `Polygon_with_holes_2`, `Curve_const_iterator`, `Point_2`, `X_monotone_curve_2`,
**`Compare_endpoints_xy_2`**, **`Construct_opposite_2`**, plus `Compare_xy_2` (for
`Less_vertex_handle`), and the whole `ArrangementXMonotoneTraits_2` set through
`Arr_traits_adaptor_2`. In CGAL terms the underlying `ArrTraits_2` must model
**`ArrangementDirectionalXMonotoneTraits_2`**.

| Arrangement traits | `Compare_endpoints_xy_2` | `Construct_opposite_2` | usable with `Gps_traits_2`? |
| --- | --- | --- | --- |
| `Arr_segment_traits_2` | ✔ | ✔ | ✔ (but prefer `Gps_segment_traits_2` for `CGAL::Polygon_2` input) |
| `Arr_non_caching_segment_traits_2` | ✔ | ✔ | ✔ |
| `Arr_polyline_traits_2` / `Arr_polycurve_traits_2` / `…_basic_…` | ✔ | ✔ | ✔ — this is what the `Tag_true` free-function path uses |
| `Arr_circle_segment_traits_2` | ✔ | ✔ | ✔ — wrapped as `Gps_circle_segment_traits_2` *(verified: join of two circles → 1 p-w-h, 4 x-monotone curves)* |
| `Arr_conic_traits_2<RatKernel, AlgKernel, NtTraits>` | ✔ | ✔ | ✔ *(verified: `General_polygon_set_2<Gps_traits_2<Arr_conic_traits_2<…>>>` instantiates and compiles with CORE)* |
| `Arr_Bezier_curve_traits_2<RatK, AlgK, NtTraits, BoundingTraits>` | ✔ | ✔ | ✔ (per header; not run here) |
| `Arr_linear_traits_2` | ✔ | ✔ | ✔ formally, but unbounded curves cannot form a closed polygon boundary — only the bounded (segment) subset is useful |
| `Arr_rational_function_traits_2`, `Arr_algebraic_segment_traits_2` | ✔ | ✔ | ✔ formally; same unboundedness caveat |
| `Arr_geodesic_arc_on_sphere_traits_2` | ✔ | ✔ | ✔ but **only** via `Gps_on_surface_base_2` + `Arr_spherical_topology_traits_2` (§20) |
| `Arr_circular_arc_traits_2` | ✘ | ✘ | ✘ — no directional functors |

---

## 12. `Gps_segment_traits_2` and `Gps_circle_segment_traits_2`

`CGAL/Gps_segment_traits_2.h`

```cpp
template <typename Kernel_,
          typename Container_ = std::vector<typename Kernel_::Point_2>,
          typename ArrSegmentTraits = Arr_segment_traits_2<Kernel_> >
class Gps_segment_traits_2 : public ArrSegmentTraits {
public:
  typedef CGAL::Polygon_2<Kernel_, Container_>            Polygon_2;              // ← a real CGAL::Polygon_2
  typedef Polygon_2                                       General_polygon_2;
  typedef CGAL::Polygon_with_holes_2<Kernel_, Container_>  Polygon_with_holes_2;
  typedef Polygon_with_holes_2                            General_polygon_with_holes_2;
  typedef typename Base::X_monotone_curve_2               X_monotone_curve_2;
  typedef Polygon_2_curve_iterator<X_monotone_curve_2, Polygon_2>  Curve_const_iterator;
  typedef typename Polygon_with_holes_2::Hole_const_iterator       Hole_const_iterator;
  typedef typename Base::Point_2                          Point_2;
```

This is the adapter that makes a *linear* `CGAL::Polygon_2` behave like a general polygon:
no conversion object is created — `Construct_curves_2` returns a **lazy iterator pair** over the
polygon's vertex pairs, synthesising a `Segment_2` on dereference:

```cpp
class Construct_curves_2 {
public:
  std::pair<Curve_const_iterator, Curve_const_iterator> operator()(const Polygon_2& pgn) const {
    Curve_const_iterator c_begin(&pgn, pgn.vertex_pairs_begin());
    Curve_const_iterator c_end(&pgn, pgn.vertex_pairs_end());
    return (std::make_pair(c_begin, c_end));
  }
};
Construct_curves_2 construct_curves_2_object() const;
```

⚠ `Curve_const_iterator` stores `const Polygon* m_pgn` — the pair is only valid while the polygon
lives, and `operator*` returns a **by-value** `X_monotone_curve_2` (so `operator->` goes through the
helper `Polygon_2_curve_ptr<Xcurve>`). Do not keep these iterators past the polygon's lifetime.

The reverse direction (curves → polygon) appends the *target* vertex of each curve:

```cpp
class Construct_polygon_2 {
  const Traits_adaptor* m_traits;                     // Gps_traits_adaptor<Self>
public:
  Construct_polygon_2(const Self* traits);
  template <typename XCurveIterator>
  void operator()(XCurveIterator begin, XCurveIterator end, Polygon_2& pgn) const {
    typename Traits_adaptor::Construct_vertex_2 ctr_v = m_traits->construct_vertex_2_object();
    for (XCurveIterator itr = begin; itr != end; ++itr) pgn.push_back(ctr_v(*itr, 1));
  }
};
Construct_polygon_2 construct_polygon_2_object() const;   // { return Construct_polygon_2(this); }
```

Note `Construct_polygon_2` **appends** to `pgn` (it never clears it) — pass a fresh polygon.

Also provided (identical shapes to `Gps_traits_2`'s): `Construct_outer_boundary`
(`construct_outer_boundary_object()`, returns `Polygon_2` **by value**), `Construct_holes`
(`construct_holes_object()`), `Construct_polygon_with_holes_2`
(`construct_polygon_with_holes_2_object()`), `Is_unbounded` (`is_unbounded_object()`).
**No `Equal_2` for polygons** here (only the inherited point/curve one), and no `Construct_curves_2`
overload for `Polygon_with_holes_2`.

`CGAL/Gps_circle_segment_traits_2.h` (complete):

```cpp
template <class Kernel_, bool Filer_ = true>
class Gps_circle_segment_traits_2 :
  public Gps_traits_2<Arr_circle_segment_traits_2<Kernel_, Filer_> >
{
public:
  Gps_circle_segment_traits_2(bool use_cache = false) :
    Gps_traits_2<Arr_circle_segment_traits_2<Kernel_, Filer_> >()
  { this->m_use_cache = use_cache; }
};
```

So `Gps_circle_segment_traits_2<K>::Polygon_2 == General_polygon_2<Arr_circle_segment_traits_2<K,true>>`
*(verified via `std::is_same`)* and `Polygon_with_holes_2 == General_polygon_with_holes_2<that>`.
A full circle is a *2-curve* general polygon *(verified: `size()==2`, `orientation()==COUNTERCLOCKWISE`)*.

---

## 13. `Ccb_curve_iterator`, `Polygon_2_curve_iterator`, `Gps_traits_adaptor`

```cpp
template <class Arrangement_>
class Ccb_curve_iterator {
public:
  typedef Arrangement_                            Arrangement;
  typedef typename Arrangement::Geometry_traits_2 Traits;
  typedef Ccb_curve_iterator<Arrangement>         Self;
  typedef typename Arrangement::Ccb_halfedge_const_circulator Ccb_halfedge_const_circulator;
  typedef typename Traits::X_monotone_curve_2     value_type;
  typedef std::forward_iterator_tag               iterator_category;
  typedef const value_type&                       reference;
  typedef const value_type*                       pointer;
  typedef std::size_t                             difference_type;

  Ccb_curve_iterator();                                            // past-the-end (_done = true)
  Ccb_curve_iterator(Ccb_halfedge_const_circulator circ, bool done = false);
  reference operator*() const;    // _circ->curve()
  pointer   operator->() const;
  bool operator==(const Self&) const;   bool operator!=(const Self&) const;
  Self& operator++();             // NOTE: implemented as --_circ  (reverse traversal!)
  Self  operator++(int);
};
```

```cpp
template <class X_monotone_curve_2_, class Polygon_>
class Polygon_2_curve_iterator {
  const Polygon_*     m_pgn;
  Edge_const_iterator m_curr_edge;                 // Polygon::Vertex_pair_iterator
public:
  typedef X_monotone_curve_2_ X_monotone_curve_2, value_type;  // (declared separately in the header)
  Polygon_2_curve_iterator();
  Polygon_2_curve_iterator(const Polygon* pgn, Edge_const_iterator ci);
  bool operator==(const Self&) const;  bool operator!=(const Self&) const;
  X_monotone_curve_2 operator*();                              // by value; NOT const
  Polygon_2_curve_ptr<X_monotone_curve_2> operator->();
  Self& operator++();  Self operator++(int);  Self& operator--();  Self operator--(int);
  Self& operator+=(difference_type n);  Self operator+(difference_type n);
  Self& operator-=(difference_type n);  Self operator-(difference_type n);
  difference_type operator-(const Self& a) const;
};
```
⚠ `operator-=` is `{ return (*this) -= n; }` — infinite recursion. Never use it.

```cpp
template <typename Traits_>
class Gps_traits_adaptor : public Traits_ {
public:
  Gps_traits_adaptor();
  Gps_traits_adaptor(const Base& traits);
  class Construct_vertex_2 {              // protected ctor; friend class Gps_traits_adaptor
  public: Point_2 operator()(const X_monotone_curve_2& cv, int i) const;   // i%2: 0 = source, 1 = target,
  };                                                                       // honouring the curve direction
  Construct_vertex_2 construct_vertex_2_object() const;
  class Orientation_2 {                   // protected ctor
  public: template <typename CurveInputIteraor>
          Orientation operator()(CurveInputIteraor begin, CurveInputIteraor end) const;
  };
  Orientation_2 orientation_2_object() const;
};
```

---

## 14. Traits deduction helpers

`CGAL/Boolean_set_operations_2/Gps_default_traits.h`:

```cpp
template <class Polygon> struct Gps_default_traits {};                       // primary: empty

template <class Kernel, class Container>
struct Gps_default_traits<CGAL::Polygon_2<Kernel, Container> > {
  typedef Arr_segment_traits_2<Kernel>                        Arr_traits;
  typedef Gps_segment_traits_2<Kernel, Container, Arr_traits> Traits;
};
template <class Kernel, class Container>
struct Gps_default_traits<CGAL::Polygon_with_holes_2<Kernel, Container> > {   // Arr_traits + Traits
  typedef Polygon_2<Kernel, Container>                    Polygon;
  typedef typename Gps_default_traits<Polygon>::Arr_traits Arr_traits;
  typedef typename Gps_default_traits<Polygon>::Traits     Traits;
};
template <class Arr_traits>
struct Gps_default_traits<CGAL::General_polygon_2<Arr_traits> >
{ typedef Gps_traits_2<Arr_traits> Traits; };                  // NOTE: no Arr_traits member here
template <class Polygon>
struct Gps_default_traits<CGAL::General_polygon_with_holes_2<Polygon> >
{ typedef typename Gps_default_traits<Polygon>::Traits Traits; };
```

`CGAL/Boolean_set_operations_2/Polygon_conversions.h`:

```cpp
template <typename Polygon>
struct Gps_polyline_traits {
  typedef typename Gps_default_traits<Polygon>::Arr_traits  Segment_traits;
  typedef Arr_polyline_traits_2<Segment_traits>             Polyline_traits;
  typedef Gps_traits_2<Polyline_traits>                     Traits;
};

template <typename Polygon> struct General_polygon_of_polygon;                 // Polygon_2 -> General_polygon_2,
                                                                               // Pwh -> General_pwh
template <typename InputIterator> struct Is_Kernel_Polygon_2;                  // ::value
template <typename InputIterator> using Enable_if_Polygon_2_iterator  = std::enable_if_t<…>;
template <typename InputIterator> using Disable_if_Polygon_2_iterator = std::enable_if_t<!…>;

template <typename Kernel, typename Container, typename ArrTraits>
General_polygon_2<ArrTraits> convert_polygon(const Polygon_2<Kernel,Container>&, const ArrTraits&);
template <typename Kernel, typename Container, typename ArrTraits>
General_polygon_with_holes_2<General_polygon_2<ArrTraits> >
convert_polygon(const Polygon_with_holes_2<Kernel,Container>&, const ArrTraits&);

template <typename Kernel, typename Container, typename ArrTraits>
Polygon_2<Kernel,Container> convert_polygon_back(const General_polygon_2<ArrTraits>&);
template <typename Kernel, typename Container, typename ArrTraits>
Polygon_with_holes_2<Kernel,Container>
convert_polygon_back(const General_polygon_with_holes_2<General_polygon_2<ArrTraits> >&);

template <typename InputIterator, typename Traits>
boost::transform_iterator<…> convert_polygon_iterator(InputIterator it, const Traits& traits);

template <typename Kernel, typename Container, typename OutputIterator>
struct Polygon_converter_output_iterator : boost::function_output_iterator<…>
{ operator OutputIterator() const; };
template <typename OutputIterator, typename Kernel, typename Container>
Polygon_converter_output_iterator<Kernel,Container,OutputIterator>
convert_polygon_back(OutputIterator& output, const Polygon_2<Kernel,Container>&);
template <typename OutputIterator, typename Kernel, typename Container>
Polygon_converter_output_iterator<Kernel,Container,OutputIterator>
convert_polygon_back(OutputIterator& output, const Polygon_with_holes_2<Kernel,Container>&);

template <typename InputIterator> struct Iterator_to_gps_traits {
  typedef typename std::iterator_traits<InputIterator>::value_type InputPolygon;
  typedef typename Gps_default_traits<InputPolygon>::Traits        Traits;
};
```

`convert_polygon(Polygon_2, ArrTraits)` builds **one** polyline curve from
`vertices_begin()..vertices_end()` + the first vertex again, then splits it with
`make_x_monotone_2_object()` into `std::variant<Point, X_monotone_curve>` and pushes only the curve
alternative. `convert_polygon_back` walks each polyline's `points_begin()..std::prev(points_end())`.

---

## 15. Free functions — `join`

`CGAL/Boolean_set_operations_2/join.h`. All in namespace `CGAL`. Behind them:
`s_join` (single pair) and `r_join` (ranges) in `Bso_internal_functions.h`.

### Binary, linear polygons — 4 argument-type combinations × 3 dispatch variants

For each of `(Polygon_2, Polygon_2)`, `(Polygon_2, Polygon_with_holes_2)`,
`(Polygon_with_holes_2, Polygon_2)`, `(Polygon_with_holes_2, Polygon_with_holes_2)`:

```cpp
// With traits
template <typename Kernel, typename Container, typename Traits>
inline bool join(const A& pgn1, const B& pgn2,
                 Polygon_with_holes_2<Kernel, Container>& res, Traits& traits);
// With Tag_true  (DEFAULT — converts to polylines internally)
template <typename Kernel, typename Container>
inline bool join(const A& pgn1, const B& pgn2,
                 Polygon_with_holes_2<Kernel, Container>& res, Tag_true = Tag_true());
// With Tag_false (uses Gps_default_traits<…>::Traits, i.e. Gps_segment_traits_2)
template <typename Kernel, typename Container>
inline bool join(const A& pgn1, const B& pgn2,
                 Polygon_with_holes_2<Kernel, Container>& res, Tag_false);
```

Return value: **`true` if the union is a single p-w-h** (then written to `res`); `false` if the two
polygons do not intersect (`res` untouched — "the original pgn1, pgn2 contain the union") or if
either operand is empty. From `s_join`:

```cpp
if (_is_empty(pgn1, traits) || _is_empty(pgn2, traits)) return false;
General_polygon_set_2<Traits> gps(pgn1, traits);
gps.join(pgn2);
if (gps.number_of_polygons_with_holes() == 1) { … return true; }
return false;
```

### Binary, general polygons — 4 combinations × 2 variants (no `Tag_*`)

```cpp
template <typename ArrTraits, typename Traits>
inline bool join(const General_polygon_2<ArrTraits>& pgn1,
                 const General_polygon_2<ArrTraits>& pgn2,
                 General_polygon_with_holes_2<General_polygon_2<ArrTraits> >& res, Traits& traits);
template <typename ArrTraits>
inline bool join(const General_polygon_2<ArrTraits>& pgn1,
                 const General_polygon_2<ArrTraits>& pgn2,
                 General_polygon_with_holes_2<General_polygon_2<ArrTraits> >& res);
// … same shape for (General_polygon_2, General_pwh), (General_pwh, General_polygon_2), and
template <typename Polygon_, typename Traits>
inline bool join(const General_polygon_with_holes_2<Polygon_>& pgn1,
                 const General_polygon_with_holes_2<Polygon_>& pgn2,
                 General_polygon_with_holes_2<Polygon_>& res, Traits& traits);
template <typename Polygon_>
inline bool join(const General_polygon_with_holes_2<Polygon_>& pgn1,
                 const General_polygon_with_holes_2<Polygon_>& pgn2,
                 General_polygon_with_holes_2<Polygon_>& res);
```

### Aggregated

```cpp
template <typename InputIterator, typename OutputIterator, typename Traits>
inline OutputIterator join(InputIterator begin, InputIterator end,
                           OutputIterator oi, Traits& traits, unsigned int k=5);

template <typename InputIterator, typename OutputIterator>
inline OutputIterator join(InputIterator begin, InputIterator end, OutputIterator oi,
                           Tag_true = Tag_true(), unsigned int k=5,
                           Enable_if_Polygon_2_iterator<InputIterator>* = 0);
template <typename InputIterator, typename OutputIterator>
inline OutputIterator join(InputIterator begin, InputIterator end, OutputIterator oi,
                           Tag_false, unsigned int k=5,
                           Enable_if_Polygon_2_iterator<InputIterator>* = 0);
template <typename InputIterator, typename OutputIterator>   // general polygons / pwh
inline OutputIterator join(InputIterator begin, InputIterator end, OutputIterator oi,
                           unsigned int k=5,
                           Disable_if_Polygon_2_iterator<InputIterator>* = 0);

// two ranges: same four shapes, with (begin1,end1,begin2,end2,oi[,Tag|Traits],k)
```

Traits deduction rule for the aggregated forms: `Iterator_to_gps_traits<InputIterator>::Traits`,
i.e. `Gps_default_traits<value_type>::Traits`. `Tag_true` instead routes through
`Gps_polyline_traits<Pgn>::Traits` and converts input/output with
`convert_polygon_iterator` / `convert_polygon_back`.

---

## 16. Free functions — `intersection`, `difference`, `symmetric_difference`, `complement`, `do_intersect`, `oriented_side`

All follow the same 4-combination × (Traits | `Tag_true` | `Tag_false`) pattern for the linear types,
and 4-combination × (Traits | no-traits) for the general types. Only the distinctive bits:

### `intersection` (`intersection.h`) — writes to an `OutputIterator` of `Polygon_with_holes_2`

```cpp
template <typename Kernel, typename Container, typename OutputIterator, typename Traits>
inline OutputIterator intersection(const Polygon_2<Kernel,Container>& pgn1,
                                   const Polygon_2<Kernel,Container>& pgn2,
                                   OutputIterator out, Traits& traits);
template <typename Kernel, typename Container, typename OutputIterator>
inline OutputIterator intersection(…, OutputIterator out, Tag_true = Tag_true());
template <typename Kernel, typename Container, typename OutputIterator>
inline OutputIterator intersection(…, OutputIterator out, Tag_false);
// + General_polygon_2 / General_polygon_with_holes_2 combinations (Traits / no-Traits)
// aggregated:
template <typename InputIterator, typename OutputIterator, typename Traits>
inline OutputIterator intersection(InputIterator begin, InputIterator end,
                                   OutputIterator oi, Traits& traits, unsigned int k=5);
template <typename InputIterator, typename OutputIterator>
inline OutputIterator intersection(InputIterator begin, InputIterator end, OutputIterator oi,
                                   Tag_true = Tag_true(), unsigned int k=5,
                                   Enable_if_Polygon_2_iterator<InputIterator>* = 0);
template <typename InputIterator, typename OutputIterator>
inline OutputIterator intersection(InputIterator begin, InputIterator end, OutputIterator oi,
                                   Tag_false, unsigned int k=5,
                                   Enable_if_Polygon_2_iterator<InputIterator>* = 0);
template <typename InputIterator, typename OutputIterator>
inline OutputIterator intersection(InputIterator begin, InputIterator end, OutputIterator oi,
                                   unsigned int k=5,
                                   std::enable_if_t<CGAL::is_iterator<InputIterator>::value>* = 0,
                                   Disable_if_Polygon_2_iterator<InputIterator>* = 0);
// + the two-range family (begin1,end1,begin2,end2,oi,…)
```

### `difference` (`difference.h`) — **binary only**

```cpp
template <typename Kernel, typename Container, typename OutputIterator, typename Traits>
inline OutputIterator difference(const Polygon_2<Kernel,Container>& pgn1,
                                 const Polygon_2<Kernel,Container>& pgn2,
                                 OutputIterator oi, Traits& traits);
template <typename Kernel, typename Container, typename OutputIterator>
inline OutputIterator difference(…, OutputIterator oi, Tag_true = Tag_true());
template <typename Kernel, typename Container, typename OutputIterator>
inline OutputIterator difference(…, OutputIterator oi, Tag_false);
// + the 3 other linear combinations, + the 4 general combinations (Traits / no-Traits).
// NO aggregated (range) overloads at all.
```

### `symmetric_difference` (`symmetric_difference.h`)

Same shape as `intersection`, including the full aggregated single-range and two-range families.

### `complement` (`complement.h`)

```cpp
// Polygon_2 -> a single Polygon_with_holes_2 (the complement of a bounded simple polygon is one
// unbounded p-w-h with exactly one hole)
template <typename Kernel, typename Container, typename Traits>
void complement(const Polygon_2<Kernel,Container>& pgn,
                Polygon_with_holes_2<Kernel,Container>& res, Traits& traits);
template <typename Kernel, typename Container>
void complement(const Polygon_2<Kernel,Container>& pgn,
                Polygon_with_holes_2<Kernel,Container>& res, Tag_true = Tag_true());
template <typename Kernel, typename Container>
void complement(const Polygon_2<Kernel,Container>& pgn,
                Polygon_with_holes_2<Kernel,Container>& res, Tag_false);

// Polygon_with_holes_2 -> OutputIterator (may produce several components)
template <typename Kernel, typename Container, typename OutputIterator, typename Traits>
OutputIterator complement(const Polygon_with_holes_2<Kernel,Container>& pgn,
                          OutputIterator oi, Traits& traits);
template <typename Kernel, typename Container, typename OutputIterator>
OutputIterator complement(const Polygon_with_holes_2<Kernel,Container>& pgn,
                          OutputIterator oi, Tag_true = Tag_true());
template <typename Kernel, typename Container, typename OutputIterator>
OutputIterator complement(const Polygon_with_holes_2<Kernel,Container>& pgn,
                          OutputIterator oi, Tag_false);

template <typename ArrTraits, typename Traits>
void complement(const General_polygon_2<ArrTraits>& pgn,
                General_polygon_with_holes_2<General_polygon_2<ArrTraits> >& res, Traits& traits);
template <typename ArrTraits>
void complement(const General_polygon_2<ArrTraits>& pgn,
                General_polygon_with_holes_2<General_polygon_2<ArrTraits> >& res);

template <typename Polygon_, typename OutputIterator, typename Traits>
OutputIterator complement(const General_polygon_with_holes_2<Polygon_>& pgn,
                          OutputIterator oi, Traits& traits);
template <typename Polygon_, typename OutputIterator>
OutputIterator complement(General_polygon_with_holes_2<Polygon_>& pgn,   // ⚠ non-const reference!
                          OutputIterator oi);
```

⚠ The last one takes a **non-const** `General_polygon_with_holes_2<Polygon_>&` — an lvalue is
required. (Almost certainly an oversight; the `Traits` overload is const.)

*(verified)* `CGAL::complement(square, pwh)` → `pwh.is_unbounded()==1`, `number_of_holes()==1`,
hole orientation `CLOCKWISE`.

### `do_intersect` (`do_intersect.h`)

Binary: 4 linear combinations × (Traits | `Tag_true` | `Tag_false`) and 4 general combinations ×
(Traits | no-Traits), all `inline bool`.
Aggregated (⚠ **inverted result**, §0.2):

```cpp
template <typename InputIterator, typename Traits>
inline bool do_intersect(InputIterator begin, InputIterator end, Traits& traits, unsigned int k=5,
                         std::enable_if_t<CGAL::is_iterator<InputIterator>::value>* = 0);
template <typename InputIterator>
inline bool do_intersect(InputIterator begin, InputIterator end, Tag_true = Tag_true(), unsigned int k=5,
                         std::enable_if_t<CGAL::is_iterator<InputIterator>::value>* = 0,
                         Enable_if_Polygon_2_iterator<InputIterator>* = 0);
template <typename InputIterator>
inline bool do_intersect(InputIterator begin, InputIterator end, Tag_false, unsigned int k=5, …);
template <typename InputIterator>
inline bool do_intersect(InputIterator begin, InputIterator end, unsigned int k=5,
                         std::enable_if_t<CGAL::is_iterator<InputIterator>::value>* = 0,
                         Disable_if_Polygon_2_iterator<InputIterator>* = 0);
// + the two-range family
```

Note the two-range `Tag_false` overload's body is `return r_do_intersect(begin1, end1, begin2, end2, k);`
— it ignores the tag and takes the polyline path anyway.

### `oriented_side` (`oriented_side.h`)

Polygon vs. polygon (4 linear × 3, 4 general × 2) **and** point vs. polygon:

```cpp
template <typename Kernel, typename Container, typename Traits>
inline Oriented_side oriented_side(const typename Kernel::Point_2& p,
                                   const Polygon_2<Kernel,Container>& pgn, Traits& traits);
template <typename Kernel, typename Container>
inline Oriented_side oriented_side(const typename Kernel::Point_2& p,
                                   const Polygon_2<Kernel,Container>& pgn, Tag_true = Tag_true());
template <typename Kernel, typename Container>
inline Oriented_side oriented_side(const typename Kernel::Point_2& p,
                                   const Polygon_2<Kernel,Container>& pgn, Tag_false);
// same three for Polygon_with_holes_2
template <typename ArrTraits, typename GpsTraits>
inline Oriented_side oriented_side(const typename ArrTraits::Point_2& p,
                                   const General_polygon_2<ArrTraits>& pgn, GpsTraits& traits);
template <typename ArrTraits>
inline Oriented_side oriented_side(const typename ArrTraits::Point_2& p,
                                   const General_polygon_2<ArrTraits>& pgn);
template <typename Polygon_, typename Traits>
inline Oriented_side oriented_side(const typename Polygon_::Point_2& p,
                                   const General_polygon_with_holes_2<Polygon_>& pgn, Traits& traits);
template <typename Polygon_>
inline Oriented_side oriented_side(const typename Polygon_::Point_2& p,
                                   const General_polygon_with_holes_2<Polygon_>& pgn);
```

⚠ The point-vs-polygon overloads take `typename Kernel::Point_2` / `typename ArrTraits::Point_2` /
`typename Polygon_::Point_2` — a **non-deduced context**, so `Kernel`/`ArrTraits`/`Polygon_` are
deduced from the *polygon* argument only. Passing a point of a different but convertible type works;
passing an explicit template argument list does not behave as one might expect.
*(verified)* `CGAL::oriented_side(Point_2(1,1), square) == ON_POSITIVE_SIDE`;
`CGAL::oriented_side(a, b) == ON_POSITIVE_SIDE` for two overlapping squares.

### `connect_holes` (`CGAL/connect_holes.h` — **must be included separately**)

```cpp
/*! Connect the given polygon with holes, turning it into a sequence of points,
 *  where the holes are connected to the outer boundary using zero-width passages.
 * \param pwh The polygon with holes.
 * \param oi Output: An output iterator for the points.
 * \pre The polygons has an outer boundary.
 * \return A past-the-end iterator of the points.
 */
template <class Kernel, class Container, class OutputIterator>
OutputIterator connect_holes(const Polygon_with_holes_2<Kernel, Container>& pwh,
                             OutputIterator oi);
```

`CGAL_precondition(! pwh.is_unbounded());`. Output `value_type` is `Kernel::Point_2`.
Linear polygons only — there is no general-polygon version. Internally builds a
`General_polygon_set_2<Gps_segment_traits_2<Kernel,Container,Arr_segment_traits_2<Kernel>>,
Gps_default_dcel<Arr_segment_traits_2<Kernel>>>` plus a vertical decomposition
(`Arr_vertical_decomposition_2.h`).

---

## 17. `Bso_internal_functions.h` — the layer bindings may want to call directly

Skipping the free-function overload thicket and calling these directly is often simpler and always
unambiguous:

```cpp
template <typename Pgn1, class Pgn2, typename Traits>
inline bool s_do_intersect(const Pgn1&, const Pgn2&, Traits& traits);
template <typename Pgn1, typename Pgn2>                       inline bool s_do_intersect(const Pgn1&, const Pgn2&);
template <typename InputIterator, typename Traits>
inline bool r_do_intersect(InputIterator begin, InputIterator end, Traits& traits, unsigned int k=5);
template <typename InputIterator>  inline bool r_do_intersect(InputIterator, InputIterator, unsigned int k=5);
template <typename InputIterator1, typename InputIterator2, typename Traits>
inline bool r_do_intersect(InputIterator1, InputIterator1, InputIterator2, InputIterator2, Traits&, unsigned int k=5);
template <typename InputIterator1, typename InputIterator2>
inline bool r_do_intersect(InputIterator1, InputIterator1, InputIterator2, InputIterator2, unsigned int k=5);

template <typename Obj, typename Pgn, typename Traits>
inline Oriented_side _oriented_side(const Obj& obj, const Pgn& pgn, Traits& traits);
template <typename Kernel, typename Pgn>
inline Oriented_side _oriented_side(const Point_2<Kernel>& point, const Pgn& pgn);
template <typename Pgn1, typename Pgn2> inline Oriented_side _oriented_side(const Pgn1&, const Pgn2&);

template <typename Pgn1, typename Pgn2, typename OutputIterator, typename Traits>
inline OutputIterator s_intersection(const Pgn1&, const Pgn2&, OutputIterator oi, Traits& traits);
template <typename Kernel, typename Container, typename Pgn1, typename Pgn2, typename OutputIterator>
inline OutputIterator s_intersection(const Pgn1&, const Pgn2&, OutputIterator oi);
template <typename InputIterator, typename OutputIterator, typename Traits>
inline OutputIterator r_intersection(InputIterator, InputIterator, OutputIterator, Traits&, unsigned int k=5);
/* … r_intersection (no traits), two-range variants … */

template <typename Traits> inline bool _is_empty(const typename Traits::Polygon_2& pgn, Traits& traits);
template <typename Traits> inline bool _is_empty(const typename Traits::Polygon_with_holes_2&, Traits&);  // false

template <typename Pgn1, typename Pgn2, typename Traits>
inline bool s_join(const Pgn1&, const Pgn2&, typename Traits::Polygon_with_holes_2& res, Traits& traits);
template <typename Kernel, typename Container, typename Pgn1, typename Pgn2, typename Pwh>
inline bool s_join(const Pgn1&, const Pgn2&, Pwh& pwh);
/* … r_join × 4 … */

template <typename Pgn1, typename Pgn2, typename OutputIterator, typename Traits>
inline OutputIterator _difference(const Pgn1&, const Pgn2&, OutputIterator oi, Traits& traits);
template <typename Kernel, typename Container, typename Pgn1, typename Pgn2, typename OutputIterator>
inline OutputIterator _difference(const Pgn1&, const Pgn2&, OutputIterator oi);

/* s_symmetric_difference / r_symmetric_difference: same shapes as intersection */

template <typename Kernel, typename Container, typename Traits>
void _complement(const Polygon_2<Kernel,Container>& pgn, typename Traits::Polygon_with_holes_2& res, Traits&);
template <typename ArrTraits, typename Traits>
void _complement(const General_polygon_2<ArrTraits>& pgn, typename Traits::Polygon_with_holes_2& res, Traits&);
template <typename Kernel, typename Container, typename OutputIterator, typename Traits>
OutputIterator _complement(const Polygon_with_holes_2<Kernel,Container>& pgn, OutputIterator oi, Traits&);
template <typename Pgn, typename OutputIterator, typename Traits>
OutputIterator _complement(const General_polygon_with_holes_2<Pgn>& pgn, OutputIterator oi, Traits&);
template <typename Kernel, typename Container, typename Pwh>
void _complement(const Polygon_2<Kernel,Container>& pgn, Pwh& pwh);
template <typename Kernel, typename Container, typename OutputIterator>
OutputIterator _complement(const Polygon_with_holes_2<Kernel,Container>& pgn, OutputIterator oi);
```

Note `r_do_intersect` also inherits the inversion (it calls the member), and
`r_do_intersect(begin1,end1,begin2,end2,traits,k)` contains a latent recursion bug
(`if (begin1 == end1) return do_intersect(begin2, end2, traits, k);` — calls the free function, not
`r_do_intersect`).

---

## 18. Validation — `Gps_polygon_validation.h`

All templated on the **Gps traits** (`Traits_2::Polygon_2`, `Traits_2::Polygon_with_holes_2`), all in
namespace `CGAL`, all taking the polygon as a *non-deduced* `typename Traits_2::Polygon_2`.

```cpp
template <typename Traits_2> bool is_closed_polygon(const typename Traits_2::Polygon_2& pgn, const Traits_2& traits);
template <typename Traits_2> bool is_simple_polygon(const typename Traits_2::Polygon_2& pgn, const Traits_2& traits);
template <typename Traits_2> bool has_valid_orientation_polygon(const typename Traits_2::Polygon_2& pgn, const Traits_2& traits);
template <typename Traits_2> bool is_valid_polygon(const typename Traits_2::Polygon_2& pgn, const Traits_2& traits);

template <typename Traits_2> bool is_closed_polygon_with_holes(const typename Traits_2::Polygon_with_holes_2& pgn, const Traits_2& traits);
template <typename Traits_2, typename PointLocation>
bool is_crossover_outer_boundary(const typename Traits_2::Polygon_with_holes_2& pgn, const Traits_2& traits, PointLocation& pl);
template <typename Traits_2>
bool is_crossover_outer_boundary(const typename Traits_2::Polygon_with_holes_2& pgn, const Traits_2& traits);
template <typename Traits_2> bool is_relatively_simple_polygon(const typename Traits_2::Polygon_2& pgn, const Traits_2& traits);
template <typename Traits_2> bool is_relatively_simple_polygon_with_holes(const typename Traits_2::Polygon_with_holes_2& pgn, const Traits_2& traits);
template <typename Traits_2> bool has_valid_orientation_polygon_with_holes(const typename Traits_2::Polygon_with_holes_2& pgn, const Traits_2& traits);
template <typename Traits_2> bool are_holes_and_boundary_pairwise_disjoint(const typename Traits_2::Polygon_with_holes_2& pwh, Traits_2& traits);   // NON-const traits
template <typename Traits_2> bool is_valid_polygon_with_holes(const typename Traits_2::Polygon_with_holes_2& pgn, const Traits_2& traits);

template <typename Traits_2> bool is_valid_unknown_polygon(const typename Traits_2::Polygon_with_holes_2& pgn, const Traits_2& traits);
template <typename Traits_2> bool is_valid_unknown_polygon(const typename Traits_2::Polygon_2& pgn, const Traits_2& traits);
```

Helper classes: `Validation_overlay_traits<Arrangement_2>` (public: `bool getHoleOverlap()`,
`void setHoleOverlap(bool)`) and
`Gps_polygon_validation_visitor<GeometryTraits_2, Allocator_ = CGAL_ALLOCATOR(int)>` with
`enum Error_code { ERROR_NONE, ERROR_EDGE_INTERSECTION, ERROR_EDGE_VERTEX_INTERSECTION,
ERROR_EDGE_OVERLAP, ERROR_VERTEX_INTERSECTION }`, `bool is_valid() const`, `Error_code error_code() const`,
and ctor `Gps_polygon_validation_visitor(bool is_s_simple = true)`.

### The rules, verbatim from the header

A **valid polygon**:
> 1 - Closed or empty polygon
> 2 - Simple (previously known as strictly simple)
> 3 - Counterclockwise oriented

A **valid polygon with holes** (doc comment above `is_valid_polygon_with_holes`):
> … 3 - Has its boundary oriented counterclockwise and the holes oriented clockwise
> 4 - All the segments (boundary and holes) do not cross or intersect in their relative interior
> 5 - The holes are on the interior of the boundary polygon if the boundary is not empty

Details:
* `is_closed_polygon`: empty polygon → `true`; a polygon with exactly one curve → **`false`**
  ("A polygon cannot have just a single edge") — so a circle must be split into ≥ 2 x-monotone arcs;
  each curve's target must equal the next curve's source; no degenerate curve (`source == target`);
  last target == first source.
* `is_simple_polygon`: surface sweep with `Gps_polygon_validation_visitor(true)` — rejects any
  intersection, weak intersection, overlap, or a vertex with `#right + #left != 2`.
* `is_relatively_simple_polygon`: same sweep with `is_s_simple = false` (self-touching at vertices
  allowed) — this is what p-w-h validation uses.
* `has_valid_orientation_polygon`: empty → `true`; otherwise
  `Gps_traits_adaptor::orientation_2_object()(first, last) == COUNTERCLOCKWISE`.
* `has_valid_orientation_polygon_with_holes`: outer boundary `COUNTERCLOCKWISE` (unless empty),
  every hole `CLOCKWISE` (unless empty).
* `are_holes_and_boundary_pairwise_disjoint`: aggregated join of the holes, then difference against
  the outer boundary; hard-codes `CGAL::Gps_default_dcel<Traits_2>` and the **bounded planar**
  topology traits (there is an in-header `// IMPORTATNT! TODO!` about that).
* Failures emit `CGAL_warning_msg(...)` with texts:
  "The polygon boundary self intersects at edges.", "The polygon boundary self (weakly) intersects.",
  "The polygon boundary self overlaps.", "The polygon boundary intersects at vertices.",
  "The polygon's boundary is not closed.", "The polygon is not simple.",
  "The polygon has a wrong orientation.", "The polygon has a crossover.",
  "Holes of the PWH intersect amongst themselves or with outer boundary".

### What actually happens with bad input

* `General_polygon_set_2` / `Polygon_set_2` / `General_polygon_set_on_surface_2` use
  `PreconditionValidationPolicy` ⇒ `CGAL_precondition(is_valid_unknown_polygon(p, t))`.
  With assertions on (the default; `CGAL_NDEBUG` is only defined when `NDEBUG` is and `CGAL_DEBUG` is not),
  a clockwise polygon produces the warning above, then
  `CGAL::Precondition_exception` (`CGAL/exceptions.h`: `Precondition_exception : Failure_exception`,
  with `expression()`, `filename()`, `line_number()`, `message()`) *(verified)*.
  Failure behaviour is configurable via `CGAL::set_error_behaviour(CGAL::Failure_behaviour)` and
  `CGAL::set_warning_behaviour(...)` (`ABORT, EXIT, EXIT_WITH_SUCCESS, CONTINUE, THROW_EXCEPTION`).
* With `-DNDEBUG` (or `-DCGAL_NDEBUG`) the validation is compiled away entirely and the clockwise
  polygon is inserted anyway, producing a set with `is_valid() == false` and garbage semantics
  *(verified)*. **Clockwise input is never auto-corrected.**
* Recommendation for a binding: call `p.orientation()` / `has_valid_orientation_polygon<Traits>()`
  yourself and `reverse_orientation()` before handing anything to CGAL, and either keep assertions on
  or run `is_valid_unknown_polygon` explicitly and turn the `bool` into a Python exception.

---

## 19. Which types go with which set

| Set | `Traits_2` | `Polygon_2` | `Polygon_with_holes_2` |
| --- | --- | --- | --- |
| `Polygon_set_2<K, C>` | `Gps_segment_traits_2<K, C>` | `CGAL::Polygon_2<K, C>` | `CGAL::Polygon_with_holes_2<K, C>` |
| `General_polygon_set_2<Gps_circle_segment_traits_2<K>>` | that | `General_polygon_2<Arr_circle_segment_traits_2<K,true>>` | `General_polygon_with_holes_2<…>` |
| `General_polygon_set_2<Gps_traits_2<ArrT>>` | that | `General_polygon_2<ArrT>` | `General_polygon_with_holes_2<General_polygon_2<ArrT>>` |
| `General_polygon_set_2<Gps_traits_2<ArrT, MyPgn>>` | that | `MyPgn` | `General_polygon_with_holes_2<MyPgn>` |

---

## 20. The sphere: `General_polygon_set_on_surface_2` / `Gps_on_surface_base_2`

Requirements on the topology traits (as used by `Gps_on_surface_base_2`):
`Topology_traits::Default_point_location_strategy` (present on
`Arr_bounded_planar_topology_traits_2`, `Arr_unb_planar_topology_traits_2` →
`Arr_walk_along_line_point_location`; and on `Arr_spherical_topology_traits_2` →
`Arr_naive_point_location`), and a DCEL whose faces/halfedges are `Gps_face_base` / `Gps_halfedge_base`.

```cpp
template <typename GeometryTraits_2, typename Dcel_ = Arr_default_dcel<GeometryTraits_2> >
class Arr_spherical_topology_traits_2;
```

**Working recipe** *(verified: compiles, inserts a spherical triangle, extracts it, complements it)*:

```cpp
typedef CGAL::Exact_predicates_exact_constructions_kernel        K;
typedef CGAL::Arr_geodesic_arc_on_sphere_traits_2<K>             Geo_traits;
typedef CGAL::Gps_traits_2<Geo_traits>                           Gps_traits;   // Polygon_2 = General_polygon_2<Geo_traits>
typedef CGAL::Gps_default_dcel<Gps_traits>                       Dcel;
typedef CGAL::Arr_spherical_topology_traits_2<Gps_traits, Dcel>  Top_traits;
typedef CGAL::Gps_on_surface_base_2<Gps_traits, Top_traits>      Gps_sphere;   // NoValidationPolicy!
```

`Arr_geodesic_arc_on_sphere_traits_2` provides `Compare_endpoints_xy_2` and `Construct_opposite_2`,
so it satisfies the directional requirement.

Caveats, all confirmed or read directly:

* **Do not use `General_polygon_set_on_surface_2<Gps_traits, Top_traits>`**: its
  `PreconditionValidationPolicy` runs `is_simple_polygon` (a planar `Ss2::Surface_sweep_2`), which
  rejects a valid spherical triangle with "The polygon boundary intersects at vertices" and throws
  `Precondition_exception` *(verified)*.
* `remove_redundant_edges()` asserts exactly one unbounded face; on the sphere there are **zero**
  unbounded faces, so that assertion can fire in debug builds. (It was not reached in the small test
  above because no edge was redundant.)
* `is_empty()` / `is_plane()` rely on `m_arr->faces_begin()->contained()` and on the topology traits
  creating a consistent initial face set — the header comments explicitly discuss the multi-face
  empty-arrangement case for exactly this reason.
* `Arr_bfs_scanner` (extraction) keys on `number_of_outer_ccbs() == 0`, which on the sphere means
  "the face that owns the whole sphere minus the inner CCBs" — extraction worked in the test
  (`number_of_polygons_with_holes() == 1`).
* There is no `arrangement()`-returning-`Arrangement_2` convenience on the surface classes; you get
  `Arrangement_on_surface_2&`.

---

## 21. Practical notes for a type-erased C++ core + Cython bindings

**Ownership / lifetime**

* `General_polygon_set_2` owns its arrangement (`m_arr`, `delete`d in the virtual destructor) and,
  unless constructed from an external traits object, its traits.
* A traits object passed by reference to a constructor is **aliased**, not copied. If you expose a
  "set with custom traits" API, keep the traits object alive in the same owning wrapper
  (e.g. a `std::shared_ptr<Traits>` member next to the set), or just use the default ctor.
* `arrangement()` returns a reference into the set. Any of `join/intersection/difference/
  symmetric_difference` (all forms) destroys that arrangement and installs a new one. Never cache the
  reference or any handle across a mutation. Model this in Cython as a short-lived "view" object with
  a generation counter you bump on every mutating call.
* `polygons_with_holes(oi)` copies everything out — the returned `Polygon_with_holes_2` values are
  independent of the set. This is the safe extraction path.
* `General_polygon_2` stores curves in a `std::list`; `Curve_iterator`s survive `push_back`/`erase`
  of other elements. `General_polygon_with_holes_2` stores holes in a `std::deque`;
  `add_hole` invalidates hole iterators but not references.
* `Gps_segment_traits_2::Curve_const_iterator` keeps a raw `const Polygon_2*` — never let such a pair
  outlive the polygon.

**Thread-safety**

* Nothing here is thread-safe. Even `const` members (`polygons_with_holes`,
  `number_of_polygons_with_holes`, `locate`, `get_boundary_of_polygon`) mutate the `mutable`
  `visited` bit on faces. Serialize all access to a given set; do not release the GIL around two
  const calls on the same object.

**Error handling**

* Wrap every entry point in `try { … } catch (const CGAL::Failure_exception& e) { … }` and surface
  `e.expression()`, `e.filename()`, `e.line_number()`, `e.message()`. Consider calling
  `CGAL::set_error_behaviour(CGAL::THROW_EXCEPTION)` and
  `CGAL::set_warning_behaviour(CGAL::CONTINUE)` explicitly at module import so the behaviour does not
  depend on build flags.
* Prefer validating input yourself (`is_valid_unknown_polygon<Traits>`, orientation checks) and
  raising a Python exception, rather than relying on `CGAL_precondition`, which vanishes under
  `NDEBUG`.

**Type erasure**

* The natural erasure boundary is (`Traits_2`, `Dcel`). `Polygon_2` / `Polygon_with_holes_2` /
  `Point_2` / `X_monotone_curve_2` are all reachable as `Traits_2::…`, so a single
  `template <class Traits, class Dcel> struct GpsImpl : IGps` covering
  {segment (`Epeck`), circle-segment, polyline, conic} instantiations gives you the whole matrix.
* Do **not** try to erase over `General_polygon_set_on_surface_2` and `General_polygon_set_2` with a
  common vtable that exposes `arrangement()` — the return types differ
  (`Arrangement_on_surface_2` vs. `Arrangement_2`) and the downcast is only valid on the planar side.
* Skip the free-function overload set entirely in the core; call
  `General_polygon_set_2` members (or the `s_*`/`r_*` helpers in `Bso_internal_functions.h`) so you
  control traits selection, the `k` parameter and the `Tag_true`/`Tag_false` polyline decision
  explicitly — and so you can implement a correct range `do_intersect`.
