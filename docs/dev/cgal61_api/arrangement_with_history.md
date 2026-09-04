# CGAL 6.1 — Arrangements with history (installed headers, verbatim API map)

Scope: `/opt/homebrew/include/CGAL` (CGAL 6.1, header-only, `$Id: ... b26b07a1242 $`).

Files covered:

| File | Contents |
| --- | --- |
| `CGAL/Arrangement_with_history_2.h` | `Arrangement_with_history_2<GeomTraits_, Dcel_>` (planar convenience wrapper) |
| `CGAL/Arrangement_on_surface_with_history_2.h` | `Arrangement_on_surface_with_history_2<GeomTraits_, TopTraits_>` + all free functions (`insert`, `remove_curve`, `overlay`) |
| `CGAL/Arrangement_2/Arr_on_surface_with_history_2_impl.h` | out-of-line member definitions (ctors, `assign`, `clear`, `split_edge`, `merge_edge`, `are_mergeable`) |
| `CGAL/Arrangement_2/Arr_with_history_accessor.h` | `Arr_with_history_accessor<ArrWithHistory_>` |
| `CGAL/Arr_consolidated_curve_data_traits_2.h` | `Arr_consolidated_curve_data_traits_2<Traits_, Data_>` |
| `CGAL/Arr_curve_data_traits_2.h` | `Arr_curve_data_traits_2<Traits_, XMonotoneCurveData_, Merge_, CurveData_, Convert_>` |
| `CGAL/Arr_geometry_traits/Consolidated_curve_data_aux.h` | `_Unique_list<Data_>`, `_Consolidate_unique_lists<Data>` |
| `CGAL/Arr_geometry_traits/Curve_data_aux.h` | `_Curve_data_ex<BaseCurveType, Data>`, `_Default_merge_func`, `_Default_convert_func` |

Supporting headers read for the same claims: `CGAL/In_place_list.h`, `CGAL/Arr_dcel.h`, `CGAL/Arr_default_dcel.h`, `CGAL/Arr_extended_dcel.h`, `CGAL/Arr_dcel_base.h`, `CGAL/Arrangement_on_surface_2.h`, `CGAL/Arrangement_2/Arrangement_on_surface_2_global.h`, `CGAL/Arrangement_2/Arrangement_2_iterators.h`, `CGAL/Arrangement_2/Arr_default_planar_topology.h`, `CGAL/Aos_observer.h`, `CGAL/Arr_default_overlay_traits.h`.

Everything marked **[verified]** was checked by compiling and running a test program against these headers with
`clang++ -std=c++17 -O0 -DCGAL_USE_CORE -DCGAL_USE_GMP -DCGAL_USE_MPFR -I/opt/homebrew/include -lgmp -lmpfr`.

---

## Gotchas / surprises vs. older CGAL (read this first)

1. **`Arrangement_with_history_2` is *not* an `Arrangement_2`.** It derives from
   `Arrangement_on_surface_with_history_2<GT, TopTraits>`, which derives from
   `Arrangement_on_surface_2<Arr_consolidated_curve_data_traits_2<GT, GT::Curve_2*>, rebound-TopTraits>`.
   The underlying arrangement's geometry traits is **the data-extended traits**, not your `GT`.
   Consequence: **`he->curve()` returns `_Curve_data_ex<GT::X_monotone_curve_2, _Unique_list<GT::Curve_2*>>&`, not `GT::X_monotone_curve_2&`** — even though
   `Arrangement_with_history_2::X_monotone_curve_2` is typedef'd to the *plain* `GT::X_monotone_curve_2`. The two names disagree. **[verified]**
   The extended type is publicly reachable as `Arr::Base_arrangement_2::X_monotone_curve_2`; it derives publicly from `GT::X_monotone_curve_2`, so `const GT::X_monotone_curve_2& x = he->curve();` binds fine (slicing on copy).
2. **`Arr_default_dcel` is now an alias template**, verbatim from `Arr_default_dcel.h:32`:
   `template <typename Traits> using Arr_default_dcel = Arr_dcel<Traits>;`
   There is no `Arr_default_dcel` *class* to specialize, forward-declare, or pass as a template-template argument. `Arr_dcel`, `Arr_extended_dcel`, `Arr_face_extended_dcel` all provide the required `template <class T> struct rebind { using other = …; }` and all work as the `Dcel_` argument of `Arrangement_with_history_2`. **[verified]**
3. **`split_edge` and `merge_edge` from `Arrangement_on_surface_2` are name-hidden.** The with-history class declares `split_edge(Halfedge_handle, const Point_2&)` and `merge_edge(Halfedge_handle, Halfedge_handle)`, which hide the base's 3-argument versions. `arr.split_edge(e, cv1, cv2)` fails to compile ("too many arguments to function call, expected 2, have 3"); you must qualify: `arr.Arr::Base_arrangement_2::split_edge(e, cv1, cv2)`. **[verified]** The public typedef `Traits_adaptor_2` is likewise redefined (to `Arr_traits_adaptor_2<Data_traits_2>`, the *full* adaptor) and hides the base's `Arr_traits_basic_adaptor_2<Data_traits_2>`.
4. **`Arr_with_history_accessor<Arrangement_with_history_2<…>>` does not compile.** The friend declaration is `friend class Arr_with_history_accessor<Self>;` where `Self` is `Arrangement_on_surface_with_history_2<GT, TopTraits>` — the *base*, not `Arrangement_with_history_2`. Instantiating the accessor with the derived class gives "`_insert_curve` is a protected member". **[verified]** Use the free functions, or spell the base:
   `Arr_with_history_accessor<Arrangement_on_surface_with_history_2<Arr::Geometry_traits_2, Arr::Base_topology_traits>>`. **[verified]**
5. **Curve identity is a raw pointer, and it is stable.** `Curve_handle` = `Curve_iterator` = `In_place_list<Curve_halfedges,false>::iterator`, i.e. literally a wrapper around a `Curve_halfedges*` that CGAL heap-allocates once per curve and never moves. `&*ch` gives the pointer; `Curve_handle(ptr)` (implicit, non-explicit ctor `In_place_list_iterator(T* x)`) reconstructs the handle; round-trip compares equal. Handles support `==`, `!=`, `<`, `<=`, `>`, `>=` (pointer order) so they are directly usable as `std::map`/`std::set` keys. **[verified]** *But* `assign`/`operator=`/copy-construction/`overlay` **deep-copy** the curve nodes, so every pointer and handle changes. **[verified]**
6. **The data-extended traits forces `Has_merge_category = Tag_true`** (`Arr_curve_data_traits_2.h:69`, `Arr_consolidated_curve_data_traits_2.h:76`) *regardless of the base traits*. With a base traits whose `Has_merge_category` is `Tag_false` (`Arr_circular_arc_traits_2`, `Arr_line_arc_traits_2`, `Arr_circular_line_arc_traits_2`) this compiles but `CGAL::remove_edge(arr, e)` — which unconditionally calls `are_mergeable_2_object()` on any surviving degree-2 end vertex — hits `CGAL_error_msg("Are mergeable is not supported.")` at **runtime**.
7. **Two edges are mergeable only if their *originating-curve sets are equal*.** `Arr_curve_data_traits_2::Are_mergeable_2` requires base-mergeability **and** `cv1.data() == cv2.data()` (`_Unique_list::operator==` = set equality). Collinear, geometrically mergeable edges coming from two different input curves report `are_mergeable == false`. **[verified]** `Merge_2` additionally has `CGAL_precondition(cv1.data() == cv2.data())`.
8. **Overlapping curves consolidate**: an edge covered by *k* input curves carries all *k* `Curve_halfedges*` in its data list, and `number_of_originating_curves(e) == k`. **[verified]** Removing such a curve does **not** delete the edge; it only erases that one pointer from the edge's data list.
9. **Inherited base modification functions silently produce history-less features.** `insert_in_face_interior(cv, f)`, `insert_at_vertices`, `modify_edge(e, cv)` etc. take the *data-extended* x-monotone curve; a plain `GT::X_monotone_curve_2` implicitly converts (via the non-explicit `_Curve_data_ex(const BaseCurveType&)`) to one with an **empty** data list. The resulting edge has `number_of_originating_curves == 0` and belongs to no `Curve_halfedges` set. **[verified]** Conversely `arr.remove_edge(e)` / `CGAL::remove_edge(arr, e)` unregister the edge from its curves' sets but **leave the `Curve_halfedges` node in the curve list** — you get a curve with `number_of_induced_edges == 0` and `number_of_curves()` unchanged. **[verified]**
10. **Induced edges are iterated in memory-address order**, not geometric order: the set comparator is `Less_halfedge_handle { return &(*h1) < &(*h2); }`. The stored handle is *one of the twins*, whichever the observer saw first — you cannot assume orientation. Ordering is not reproducible across runs; sort yourself if you need determinism.
11. **`std::variant`/`std::optional` throughout the data traits.** `Arr_curve_data_traits_2::Make_x_monotone_2` emits `std::variant<Point_2, X_monotone_curve_2>`; `Intersect_2` emits `std::variant<std::pair<Point_2, Multiplicity>, X_monotone_curve_2>` (note: **not** wrapped in `std::optional`, and `CGAL::Object` is gone).
12. **No `insert_point` in the with-history API.** Points go in through the *base* free function `CGAL::insert_point(arr, p[, pl])`, which contributes nothing to the history. **[verified]** There is likewise no with-history `zone`, `do_intersect`, or `insert_non_intersecting_curve`; those base overloads bind to the with-history object through derived-to-base deduction and bypass history bookkeeping except for the observer's edge registration.
13. **`Originating_curve_iterator` and `Induced_edge_iterator` leak non-const access out of a `const` arrangement.** `originating_curves_begin(Halfedge_const_handle) const` returns an iterator whose `operator*` is `Curve_2&` (non-const); `induced_edges_begin(Curve_const_handle) const` yields `Halfedge_handle` (non-const). Do not rely on constness for safety in bindings.
14. **The overlay result gets *duplicated* curves.** `res` receives fresh `Curve_halfedges` copies of **all** curves of `arr1` followed by **all** curves of `arr2` (even those inducing no edge in the result), with correctly rebuilt induced-edge sets. Input handles do **not** identify output curves; only positional order (arr1's, then arr2's, each in their own list order) does. **[verified]**

---

## 1. `CGAL::Arrangement_with_history_2` — `CGAL/Arrangement_with_history_2.h`

### 1.1 Declaration

```cpp
template <class GeomTraits_,
          class Dcel_ = Arr_default_dcel<GeomTraits_> >
class Arrangement_with_history_2 :
  public Arrangement_on_surface_with_history_2
    <GeomTraits_,
     typename Default_planar_topology<GeomTraits_, Dcel_>::Traits>
{
private:
  typedef Default_planar_topology<GeomTraits_, Dcel_>     Default_topology;
  typedef Arrangement_on_surface_with_history_2<GeomTraits_,
             typename Default_topology::Traits>           Base;   // PRIVATE
```

`Default_planar_topology<GeomTraits, Dcel>::Traits` (`Arrangement_2/Arr_default_planar_topology.h`) selects
`Arr_bounded_planar_topology_traits_2<GeomTraits, Dcel>` when all four side categories are oblivious, else
`Arr_unb_planar_topology_traits_2<GeomTraits, Dcel>`.

`Base` is **private**; to name it from outside use the inherited public typedef `Base_topology_traits`:

```cpp
using ArrBaseWH = CGAL::Arrangement_on_surface_with_history_2<
                    Arr::Geometry_traits_2, Arr::Base_topology_traits>;
static_assert(std::is_base_of<ArrBaseWH, Arr>::value);   // [verified]
```

### 1.2 Public typedefs (all verbatim)

```cpp
typedef GeomTraits_                                     Geometry_traits_2;
typedef Dcel_                                           Dcel;

typedef typename Base::Point_2                          Point_2;              // == GeomTraits_::Point_2
typedef typename Base::X_monotone_curve_2               X_monotone_curve_2;   // == GeomTraits_::X_monotone_curve_2  (PLAIN — see gotcha 1)
typedef typename Base::Curve_2                          Curve_2;              // == GeomTraits_::Curve_2

typedef typename Base::Topology_traits                  Topology_traits;      // the REBOUND (data) topology traits

typedef typename Base::Vertex                   Vertex;
typedef typename Base::Halfedge                 Halfedge;
typedef typename Base::Face                     Face;
typedef typename Base::Size                     Size;                          // std::size_t

typedef typename Base::Vertex_iterator          Vertex_iterator;
typedef typename Base::Vertex_const_iterator    Vertex_const_iterator;
typedef typename Base::Halfedge_iterator        Halfedge_iterator;
typedef typename Base::Halfedge_const_iterator  Halfedge_const_iterator;
typedef typename Base::Edge_iterator            Edge_iterator;
typedef typename Base::Edge_const_iterator      Edge_const_iterator;
typedef typename Base::Face_iterator            Face_iterator;
typedef typename Base::Face_const_iterator      Face_const_iterator;

typedef typename Base::Halfedge_around_vertex_circulator        Halfedge_around_vertex_circulator;
typedef typename Base::Halfedge_around_vertex_const_circulator  Halfedge_around_vertex_const_circulator;
typedef typename Base::Ccb_halfedge_circulator                  Ccb_halfedge_circulator;
typedef typename Base::Ccb_halfedge_const_circulator            Ccb_halfedge_const_circulator;
typedef typename Base::Outer_ccb_iterator                       Outer_ccb_iterator;
typedef typename Base::Outer_ccb_const_iterator                 Outer_ccb_const_iterator;
typedef typename Base::Inner_ccb_iterator                       Inner_ccb_iterator;
typedef typename Base::Inner_ccb_const_iterator                 Inner_ccb_const_iterator;
typedef typename Base::Isolated_vertex_iterator                 Isolated_vertex_iterator;
typedef typename Base::Isolated_vertex_const_iterator           Isolated_vertex_const_iterator;

typedef typename Base::Vertex_handle             Vertex_handle;
typedef typename Base::Vertex_const_handle       Vertex_const_handle;
typedef typename Base::Halfedge_handle           Halfedge_handle;
typedef typename Base::Halfedge_const_handle     Halfedge_const_handle;
typedef typename Base::Face_handle               Face_handle;
typedef typename Base::Face_const_handle         Face_const_handle;

typedef typename Base::Curve_iterator             Curve_iterator;
typedef typename Base::Curve_const_iterator       Curve_const_iterator;
typedef typename Base::Curve_handle               Curve_handle;
typedef typename Base::Curve_const_handle         Curve_const_handle;
typedef typename Base::Originating_curve_iterator Originating_curve_iterator;
typedef typename Base::Induced_edge_iterator      Induced_edge_iterator;

// backward compatibility:
typedef Geometry_traits_2                        Traits_2;
typedef typename Base::Inner_ccb_iterator        Hole_iterator;
typedef typename Base::Inner_ccb_const_iterator  Hole_const_iterator;
```

Also inherited and public (from `Arrangement_on_surface_with_history_2`): `Base_topology_traits`, `Base_arrangement_2`,
`Traits_adaptor_2`, `Curve_halfedges`, and (from `Arrangement_on_surface_2`) `Observer`, `Base_aos`, `Dcel` (the *rebound* DCEL — careful, `Arrangement_with_history_2::Dcel` re-typedefs it to your un-rebound `Dcel_`).

`friend class Arr_accessor<Self>;` is declared here, but `Arr_accessor<Arrangement_with_history_2<…>>` is not usable in practice (it needs `Arrangement_on_surface_2`'s protected members, which that friendship does not grant).

### 1.3 Members

```cpp
Arrangement_with_history_2();                                // default
Arrangement_with_history_2(const Base& base);                // copy from the on-surface-with-history base
Arrangement_with_history_2(const Traits_2* tr);              // does NOT take ownership of tr

Self& operator=(const Base& base);                           // -> Base::assign(base)
void  assign(const Base& base);                              // -> Base::assign(base)

const Traits_2* traits() const;                              // == geometry_traits()

Size number_of_vertices_at_infinity() const;                 // valid - concrete vertices
Size number_of_unbounded_faces() const;                      // O(F) scan of faces_begin()..faces_end()

Face_handle       unbounded_face();
Face_const_handle unbounded_face() const;
```

The implicitly-declared copy constructor and copy-assignment operator work correctly (they route to
`Arrangement_on_surface_with_history_2`'s copy ctor / `operator=`, both of which call `assign`). **[verified]**
Note `operator=(const Base&)` does **not** suppress the implicit `operator=(const Self&)`, and the implicit one wins
for `Arr b = a;` by exact match.

Ownership/lifetime: the `const Traits_2*` constructor stores a **non-owning** pointer (it is `static_cast` to
`const Data_traits_2*` inside `Arr_on_surface_with_history_2_impl.h`; the traits object must outlive the arrangement,
and the cast is only valid because `Data_traits_2` derives from `GeomTraits_` — CGAL relies on it being an
empty-ish, stateless-cast-compatible traits; prefer the default constructor in bindings).

---

## 2. `CGAL::Arrangement_on_surface_with_history_2` — `CGAL/Arrangement_on_surface_with_history_2.h`

### 2.1 Declaration and the whole rebind chain

```cpp
template <class GeomTraits_, class TopTraits_>
class Arrangement_on_surface_with_history_2 :
  public Arrangement_on_surface_2
  <Arr_consolidated_curve_data_traits_2
     <GeomTraits_, typename GeomTraits_::Curve_2 *>,
   typename TopTraits_::template rebind
     <Arr_consolidated_curve_data_traits_2
       <GeomTraits_, typename GeomTraits_::Curve_2 *>,
      typename TopTraits_::Dcel::template rebind
      <Arr_consolidated_curve_data_traits_2
       <GeomTraits_, typename GeomTraits_::Curve_2 *> >::other>::other>
```

Internal typedefs that define the whole scheme:

```cpp
public:
  typedef GeomTraits_  Geometry_traits_2;
  typedef TopTraits_   Base_topology_traits;
  typedef typename Geometry_traits_2::Point_2             Point_2;
  typedef typename Geometry_traits_2::Curve_2             Curve_2;
  typedef typename Geometry_traits_2::X_monotone_curve_2  X_monotone_curve_2;

protected:
  friend class Arr_accessor<Self>;
  friend class Arr_with_history_accessor<Self>;              // Self == THIS class, not the derived one

  typedef Arr_consolidated_curve_data_traits_2<Geometry_traits_2, Curve_2*> Data_traits_2;
  typedef typename Data_traits_2::Curve_2            Data_curve_2;     // _Curve_data_ex<GT::Curve_2, _Unique_list<GT::Curve_2*>>
  typedef typename Data_traits_2::X_monotone_curve_2 Data_x_curve_2;   // _Curve_data_ex<GT::X_monotone_curve_2, _Unique_list<GT::Curve_2*>>
  typedef typename Data_traits_2::Data_iterator      Data_iterator;    // std::list<GT::Curve_2*>::const_iterator

  typedef typename Base_topology_traits::Dcel                       Base_dcel;
  typedef typename Base_dcel::template rebind<Data_traits_2>        Dcel_rebind;
  typedef typename Dcel_rebind::other                               Data_dcel;
  typedef typename Base_topology_traits::template
                   rebind<Data_traits_2, Data_dcel>                 Top_traits_rebind;
  typedef typename Top_traits_rebind::other                         Data_top_traits;
  typedef Arrangement_on_surface_2<Data_traits_2, Data_top_traits>  Base_arr_2;

public:
  typedef Arr_traits_adaptor_2<Data_traits_2>          Traits_adaptor_2;
  typedef Data_top_traits                              Topology_traits;
  typedef Base_arr_2                                   Base_arrangement_2;   // <-- your public door to the data types
  typedef typename Base_arr_2::Size                    Size;
  // … all Vertex/Halfedge/Face/iterator/circulator/handle typedefs forwarded from Base_arr_2 …
```

#### What the `Dcel` template parameter must provide

For `Dcel_` (as passed to `Arrangement_with_history_2`) or `TopTraits_::Dcel`:

* `template <class T> struct rebind { using other = <Dcel instantiated on T>; };` where `T` is a geometry traits
  supplying `Point_2` and `X_monotone_curve_2`. This is the *only* extra requirement compared to a plain
  `Arrangement_2`; the DCEL is re-instantiated on `Arr_consolidated_curve_data_traits_2<GT, GT::Curve_2*>`, so the
  halfedge base is re-instantiated on the *extended* x-monotone curve type.
* The vertex base must provide `template <class Point_> struct rebind`, and the halfedge base
  `template <class Xcv> struct rebind` (this is what `Arr_dcel::rebind` itself calls).
* Everything else is the ordinary `ArrDcel` concept.

**Does `Arr_extended_dcel` satisfy it? Yes.** `Arr_extended_dcel<Traits_, VertexData, HalfedgeData, FaceData, VertexBase, HalfedgeBase, FaceBase>` has

```cpp
template <typename T>
struct rebind {
private:
  using Pnt = typename T::Point_2;
  using Xcv = typename T::X_monotone_curve_2;
  using Rebind_vertex   = typename VertexBase::template rebind<Pnt>;
  using Vertex_other    = typename Rebind_vertex::other;
  using Rebind_halfedge = typename HalfedgeBase::template rebind<Xcv>;
  using Halfedge_other  = typename Rebind_halfedge::other;
public:
  using other = Arr_extended_dcel<T, Vertex_data, Halfedge_data, Face_data,
                                  Vertex_other, Halfedge_other, Face_base>;
};
```

so the auxiliary data types survive the rebind. `Arr_face_extended_dcel<Traits_, FaceData, …>` and
`Arr_dcel<Traits, V, H, F>` are equally fine. All three verified to compile and to keep `set_data`/`data()` working
on vertices, halfedges and faces of an `Arrangement_with_history_2`. **[verified]**

#### What the topology traits must provide

```cpp
template <class T, class D> struct rebind { typedef <TopTraits on T,D> other; };
typedef Dcel_ Dcel;
```
Both `Arr_bounded_planar_topology_traits_2` and `Arr_unb_planar_topology_traits_2` have exactly that
(`rebind { typedef Arr_*_planar_topology_traits_2<T, D> other; }`). Unbounded traits (e.g. `Arr_linear_traits_2`)
work end-to-end, including `number_of_vertices_at_infinity()` and `number_of_unbounded_faces()`. **[verified]**

### 2.2 `Curve_halfedges` — the history node

```cpp
struct Less_halfedge_handle {                                 // protected
  bool operator()(Halfedge_handle h1, Halfedge_handle h2) const
  { return (&(*h1) < &(*h2)); }
};

class Curve_halfedges : public Curve_2,                       // PUBLIC nested class
                        public In_place_list_base<Curve_halfedges> {
  friend class Curve_halfedges_observer;
  friend class Arrangement_on_surface_with_history_2<Gt, Btt>;
  friend class Arr_with_history_accessor<Aos_wh>;
private:
  using Halfedges_set = std::set<Halfedge_handle, Less_halfedge_handle>;
  Halfedges_set m_halfedges;
public:
  Curve_halfedges() {}
  Curve_halfedges(const Curve_2& curve) : Curve_2(curve) {}
  using iterator       = typename Halfedges_set::iterator;
  using const_iterator = typename Halfedges_set::const_iterator;
private:                                                       // <-- private: friends only
  Size           size() const;
  const_iterator begin() const;   iterator begin();
  const_iterator end()   const;   iterator end();
  iterator       _insert(Halfedge_handle he);
  void           erase(iterator pos);
  void           _erase(Halfedge_handle he);   // erases he, else he->twin(); asserts one was found
  void           clear();
};
```

Key facts for bindings:

* `Curve_halfedges` **IS-A** `Curve_2` (public, first base, offset 0). To read the original geometry:
  `const Curve_2& cv = *ch;` or `static_cast<const Curve_2&>(*ch)`.
* It is also `In_place_list_base<Curve_halfedges>`, i.e. it carries the list links **inside itself**
  (`T* next_link; T* prev_link;`, both public). It is **not** a node of a `std::list`.
* `size()/begin()/end()` are **private** — use the arrangement's `number_of_induced_edges` /
  `induced_edges_begin` / `induced_edges_end` instead. `ch->size()` from outside will not compile.
* Storage: `typedef CGAL_ALLOCATOR(Curve_halfedges) Curves_alloc;` (i.e. `std::allocator<Curve_halfedges>`),
  each node individually `allocate(1)` + `construct` in `_insert_curve`, `push_back`ed into
  `In_place_list<Curve_halfedges, false> m_curves` (`managed == false`, so the list never frees the node).
  **The node address never changes for the life of the curve.** (`sizeof == 72` for `Arr_segment_traits_2<Epeck>`.) **[verified]**

### 2.3 `Curve_halfedges_observer`

```cpp
class Curve_halfedges_observer : public Base_arr_2::Observer {   // == Aos_observer<Base_arr_2>
  using Base_aos = typename Base_arr_2::Base_aos;
  using Vertex_handle      = typename Base_aos::Vertex_handle;
  using Halfedge_handle    = typename Base_aos::Halfedge_handle;
  using X_monotone_curve_2 = typename Base_aos::X_monotone_curve_2;   // the DATA-extended curve

  virtual void after_create_edge(Halfedge_handle e) override;
  virtual void before_modify_edge(Halfedge_handle e, const X_monotone_curve_2&) override;
  virtual void after_modify_edge(Halfedge_handle e) override;
  virtual void before_split_edge(Halfedge_handle e, Vertex_handle,
                                 const X_monotone_curve_2& c1, const X_monotone_curve_2& c2) override;
  virtual void after_split_edge(Halfedge_handle e1, Halfedge_handle e2) override;
  virtual void before_merge_edge(Halfedge_handle e1, Halfedge_handle e2,
                                 const X_monotone_curve_2& c) override;
  virtual void after_merge_edge(Halfedge_handle e) override;
  virtual void before_remove_edge(Halfedge_handle e) override;
private:
  void _register_edge(Halfedge_handle e);     // for each p in e->curve().data(): ((Curve_halfedges*)p)->_insert(e)
  void _unregister_edge(Halfedge_handle e);   // …->_erase(e)
};
```

Data members of the arrangement:

```cpp
Curves_alloc              m_curves_alloc;
Curve_halfedges_list      m_curves;      // In_place_list<Curve_halfedges, false>
Curve_halfedges_observer  m_observer;    // attached in every ctor, detached/reattached around _overlay
```

This is *the* mechanism keeping history consistent: **any** edge change routed through the base arrangement's
observer notifications (including base `remove_edge`, `modify_edge`, the sweep, `CGAL::remove_edge`'s automatic
merges) updates the per-curve edge sets. **[verified]** — a user-supplied `Arr::Observer` attached on top sees the
same notifications and coexists with `m_observer`.

### 2.4 Curve container typedefs

```cpp
typedef typename Curve_halfedges_list::iterator        Curve_iterator;        // In_place_list_iterator<Curve_halfedges, std::allocator<Curve_halfedges>>
typedef typename Curve_halfedges_list::const_iterator  Curve_const_iterator;
typedef Curve_iterator                                 Curve_handle;
typedef Curve_const_iterator                           Curve_const_handle;
typedef typename Curve_halfedges::const_iterator       Induced_edge_iterator; // std::set<Halfedge_handle,Less_halfedge_handle>::const_iterator
```

`In_place_list_iterator<T, Alloc>` (from `CGAL/In_place_list.h`) public interface:

```cpp
typedef T value_type;  typedef T* pointer;  typedef T& reference;
typedef std::size_t size_type;  typedef std::ptrdiff_t difference_type;
typedef std::bidirectional_iterator_tag iterator_category;

In_place_list_iterator();                 // node == 0
In_place_list_iterator(T* x);             // NON-explicit  <-- pointer -> handle
bool operator==(const Self&) const;  bool operator!=(const Self&) const;
bool operator==(std::nullptr_t) const;  bool operator!=(std::nullptr_t) const;
bool operator< (const Self&) const;  bool operator<=(const Self&) const;
bool operator> (const Self&) const;  bool operator>=(const Self&) const;
T&    operator*()  const;  T* operator->() const;
Self& operator++();  Self operator++(int);  Self& operator--();  Self operator--(int);
```

`In_place_list_const_iterator<T,Alloc>` additionally has `In_place_list_const_iterator(Iterator i)`
(implicit `Curve_handle` → `Curve_const_handle`), `const T* node`, and `remove_const()` returning the mutable
iterator. Both have `CGAL::internal::hash_value(iterator)` overloads.

**Stable identity recipe for bindings:**

```cpp
using CH = Arr::Curve_halfedges;
CH* key = &(*ch);                 // or ch.operator->()
Arr::Curve_handle back(key);      // implicit; back == ch          [verified]
```

`curves_end()` is the list's sentinel node — allocated once in the `In_place_list` constructor and preserved even by
`In_place_list::destroy()` — so the end handle is stable too. Never dereference it.

### 2.5 `Originating_curve_iterator`

```cpp
class Originating_curve_iterator :
  public I_Dereference_iterator<Data_iterator, Curve_2,
                                typename Data_iterator::difference_type,
                                typename Data_iterator::iterator_category>
{
public:
  Originating_curve_iterator() {}
  Originating_curve_iterator(Data_iterator iter);
  operator Curve_iterator () const;         // static_cast<Curve_halfedges*>(this->ptr())
  operator Curve_const_iterator () const;
};
```

`I_Dereference_iterator` (`CGAL/Arrangement_2/Arrangement_2_iterators.h`) gives:

```cpp
typedef Value_ value_type;  typedef value_type& reference;  typedef value_type* pointer;
Iterator current_iterator() const;
pointer  ptr() const;              // static_cast<value_type*>(*iter)
reference operator*() const;       // Curve_2&      (non-const, even from a const arrangement)
pointer   operator->() const;      // Curve_2*
Self& operator++();  Self operator++(int);  Self& operator--();  Self operator--(int);
bool operator==(const Self&) const;  bool operator!=(const Self&) const;
```

So `*oc` yields `Curve_2&` (really the `Curve_2` sub-object of a `Curve_halfedges`), and
`Arr::Curve_handle h = oc;` / `Arr::Curve_const_handle ch = oc;` both work (one user-defined conversion each,
no ambiguity). **[verified]**

### 2.6 Constructors / assignment / destruction (definitions in `Arr_on_surface_with_history_2_impl.h`)

```cpp
Arrangement_on_surface_with_history_2();                            // Base_arr_2(); m_observer.attach(*this)
Arrangement_on_surface_with_history_2(const Self& arr);             // Base_arr_2(); assign(arr); m_observer.attach(*this)
Arrangement_on_surface_with_history_2(const Geometry_traits_2* tr); // Base_arr_2(static_cast<const Data_traits_2*>(tr)); attach

Self& operator=(const Self& arr);                                   // self-assignment safe
void  assign(const Self& arr);

virtual ~Arrangement_on_surface_with_history_2();                   // calls clear()
virtual void clear();                                               // frees every Curve_halfedges, then Base_arr_2::clear()
```

`assign(arr)`: `clear()` → `Base_arr_2::assign(arr)` → allocate a **duplicate** `Curve_halfedges` for every source
curve → walk `edges_begin()..edges_end()` remapping each `e->curve().data()` pointer through the map and
`dup_c->_insert(e)`. **All curve pointers/handles from the source are meaningless in the copy.** **[verified]**

`clear()` deallocates every `Curve_halfedges` (invalidating every `Curve_handle` and every raw pointer) and then
clears the DCEL. **[verified]**

### 2.7 Traits / topology accessors

```cpp
inline const Geometry_traits_2* geometry_traits() const;   // returns the PLAIN traits pointer (upcast from
                                                           // const Arr_traits_basic_adaptor_2<Data_traits_2>*)
inline       Topology_traits*   topology_traits();
inline const Topology_traits*   topology_traits() const;   // the REBOUND (data) topology traits
```

Note that `Arrangement_on_surface_2::geometry_traits()` (hidden here) returns `const Data_traits_2*`;
reach it via `static_cast<const Arr::Base_arrangement_2&>(arr).geometry_traits()` when you need the data traits
functors (e.g. to build `Data_x_curve_2` for a qualified `Base_arrangement_2::split_edge`). **[verified]**

### 2.8 Curve traversal

```cpp
Size                 number_of_curves() const { return (m_curves.size()); }   // O(1) (In_place_list keeps `length`)
Curve_iterator       curves_begin();
Curve_iterator       curves_end();
Curve_const_iterator curves_begin() const;
Curve_const_iterator curves_end() const;
```

Curves appear in **insertion order** (`push_back`), including the aggregated range insertion and the two blocks
appended by `_overlay`. **[verified]**

### 2.9 Origin curves of an edge

```cpp
Size number_of_originating_curves(Halfedge_const_handle e) const
{ return (e->curve().data().size()); }

Originating_curve_iterator originating_curves_begin(Halfedge_const_handle e) const
{ return Originating_curve_iterator(e->curve().data().begin()); }

Originating_curve_iterator originating_curves_end(Halfedge_const_handle e) const
{ return Originating_curve_iterator(e->curve().data().end()); }
```

All three are `const` members taking `Halfedge_const_handle`. Complexity: `size()` is `std::list::size()` (O(1) in
C++11 and later).

### 2.10 Edges induced by a curve

```cpp
Size                 number_of_induced_edges(Curve_const_handle c) const { return c->size(); }
Induced_edge_iterator induced_edges_begin(Curve_const_handle c) const    { return (c->begin()); }
Induced_edge_iterator induced_edges_end  (Curve_const_handle c) const    { return (c->end()); }
```

Value type is `Halfedge_handle` (non-const), ordered by halfedge address (see gotcha 10). These are `std::set`
iterators: **any** arrangement modification that creates/removes/splits/merges/modifies an edge invalidates the
iterator that points at an erased element (other elements' iterators stay valid, per `std::set` rules). CGAL's own
`_remove_curve` increments before removing for exactly this reason.

### 2.11 Edge manipulation (the with-history overrides)

```cpp
/*! splits a given edge into two at the given split point.
 * \pre p lies in the interior of the curve associated with e.
 * \return A handle for the halfedge whose source is the source of the
 *         original halfedge e, and whose target is the split point. */
Halfedge_handle split_edge(Halfedge_handle e, const Point_2& p);

/*! merges two edges to form a single edge.
 * \pre e1 and e2 must have a common end-vertex of degree 2 and must be mergeable. */
Halfedge_handle merge_edge(Halfedge_handle e1, Halfedge_handle e2);

/*! \return true iff e1 and e2 are mergeable. */
bool are_mergeable(Halfedge_const_handle e1, Halfedge_const_handle e2) const;
```

Implementations:

* `split_edge`: `m_geom_traits->split_2_object()(e->curve(), p, cv1, cv2)` on the **data traits**, so both halves keep
  the full originating-curve list; then `Base_arr_2::split_edge(e, cv1, cv2)` (order swapped when
  `e->direction() != ARR_LEFT_TO_RIGHT`). History: the curve's induced-edge count goes 1 → 2. **[verified]**
* `merge_edge`: `CGAL_precondition_msg(are_mergeable(e1, e2), "Edges are not mergeable.")`, then
  `m_geom_traits->merge_2_object()(e1->curve(), e2->curve(), cv)` and `Base_arr_2::merge_edge(e1, e2, cv)`.
  History: induced-edge count goes 2 → 1. **[verified]**
* `are_mergeable`: `false` if either halfedge `is_fictitious()`; requires a shared end vertex; requires that vertex's
  `degree() == 2`; finally delegates to the data traits' `Are_mergeable_2`, which requires base-mergeability **and**
  equal originating-curve sets.

### 2.12 Protected curve insertion / removal (called only through the accessor / free functions)

```cpp
template <class PointLocation>
Curve_handle _insert_curve(const Curve_2& cv, const PointLocation& pl);   // protected
Curve_handle _insert_curve(const Curve_2& cv);                           // protected
template <class InputIterator>
void         _insert_curves(InputIterator begin, InputIterator end);     // protected, returns void
Size         _remove_curve(Curve_handle ch);                             // protected, returns #removed edges
```

* `_insert_curve` allocates the `Curve_halfedges` from `cv`, `m_curves.push_back(*p_cv)` **before** inserting,
  builds `Data_curve_2 data_curve(cv, p_cv)` and calls `CGAL::insert(base_arr, data_curve[, pl])`; returns
  `--m_curves.end()`.
* `_insert_curves` allocates and appends one node per input curve (input order), then does a single aggregated
  `CGAL::insert(base_arr, data_curves.begin(), data_curves.end())` (sweep). **It returns nothing** — to recover
  handles, remember `number_of_curves()` before the call and walk forward from `curves_begin()`, or hold
  `--curves_end()` beforehand and `++` it afterwards.
* `_remove_curve(ch)`: for each induced halfedge `he` — if `he->curve().data().size() == 1` (asserting the sole
  pointer is `&*ch`) then `Base_arr_2::remove_edge(he)` and count it; otherwise just
  `he->curve().data().erase(p_cv)` and keep the edge. Finally erases the node from `m_curves`, destroys and
  deallocates it. Returns the number of *removed edges*. After this, **`ch` and its raw pointer dangle.** **[verified]**
  Note it calls `Base_arr_2::remove_edge(he)` (defaults `remove_source = remove_target = true`), **not** the free
  `CGAL::remove_edge`, so it does **not** merge surviving neighbours.

### 2.13 `_overlay` (public despite the underscore)

```cpp
template <class TopTraits1, class TopTraits2, class OverlayTraits>
void _overlay(const Arrangement_on_surface_with_history_2<Geometry_traits_2, TopTraits1>& arr1,
              const Arrangement_on_surface_with_history_2<Geometry_traits_2, TopTraits2>& arr2,
              OverlayTraits& overlay_tr);
```

Sequence: `clear()` → `m_observer.detach()` → `CGAL::overlay(base_arr1, base_arr2, base_res, overlay_tr)` (on the
**base** arrangements) → duplicate every curve of `arr1`, then of `arr2`, into `m_curves` and build a
`std::map<const Curve_halfedges*, Curve_halfedges*>` → for every edge of the result, remap `e->curve().data()`
through the map and `dup_c->_insert(e)` → `m_observer.attach(*this)`.

Consequences:

* The `OverlayTraits` you write is applied to the **base** arrangements, whose `Vertex_const_handle`,
  `Halfedge_const_handle`, `Face_const_handle`, `Vertex_handle`, `Halfedge_handle`, `Face_handle` are *the same
  types* as the with-history arrangement's, so `Arr_default_overlay_traits<ArrWH>` and
  `Arr_face_overlay_traits<ArrWH_A, ArrWH_B, ArrWH_R, Fn>` work unchanged. **[verified]**
* Any observer notification issued during the overlay is not seen by `m_observer` — history is rebuilt
  wholesale at the end.
* `res` gets `arr1.number_of_curves() + arr2.number_of_curves()` curves, *always*, even curves that induce no edge
  in the result. **[verified]**
* Inputs are untouched (their own history is intact after the call). **[verified]**

---

## 3. Free functions in `Arrangement_on_surface_with_history_2.h` (verbatim)

```cpp
template <class GeomTraits, class TopTraits, class PointLocation>
typename Arrangement_on_surface_with_history_2<GeomTraits, TopTraits>::Curve_handle
insert(Arrangement_on_surface_with_history_2<GeomTraits,TopTraits>& arr,
       const typename GeomTraits::Curve_2& c,
       const PointLocation& pl);

template <class GeomTraits, class TopTraits>
typename Arrangement_on_surface_with_history_2<GeomTraits, TopTraits>::Curve_handle
insert(Arrangement_on_surface_with_history_2<GeomTraits,TopTraits>& arr,
       const typename GeomTraits::Curve_2& c);

template <class GeomTraits, class TopTraits, class InputIterator>
void insert(Arrangement_on_surface_with_history_2<GeomTraits, TopTraits>& arr,
            InputIterator begin, InputIterator end);          // \pre value_type == Curve_2

template <class GeomTraits, class TopTraits>
typename Arrangement_on_surface_with_history_2<GeomTraits, TopTraits>::Size
remove_curve(Arrangement_on_surface_with_history_2<GeomTraits, TopTraits>& arr,
             typename Arrangement_on_surface_with_history_2<GeomTraits, TopTraits>::Curve_handle ch);

template <class GeomTraits, class TopTraits1, class TopTraits2,
          class ResTopTraits, class OverlayTraits>
void overlay(const Arrangement_on_surface_with_history_2<GeomTraits, TopTraits1>& arr1,
             const Arrangement_on_surface_with_history_2<GeomTraits, TopTraits2>& arr2,
             Arrangement_on_surface_with_history_2<GeomTraits, ResTopTraits>& res,
             OverlayTraits& ovl_traits);

template <class GeomTraits, class TopTraits1, class TopTraits2, class ResTopTraits>
void overlay(const Arrangement_on_surface_with_history_2<GeomTraits, TopTraits1>& arr1,
             const Arrangement_on_surface_with_history_2<GeomTraits, TopTraits2>& arr2,
             Arrangement_on_surface_with_history_2<GeomTraits, ResTopTraits>& res);
             // uses _Arr_default_overlay_traits_base<ArrA, ArrB, ArrRes>
```

* All of them are `CGAL::`-namespaced; ADL finds them from an `Arrangement_with_history_2` argument.
* `insert`/`remove_curve` create an `Arr_with_history_accessor<Arr_with_hist_2>` where `Arr_with_hist_2` is the
  *on-surface* class — the friend — and forward to `_insert_curve` / `_insert_curves` / `_remove_curve`.
* **There is no `insert_point`, `insert_non_intersecting_curve`, `zone`, `do_intersect`, or `remove_edge`
  with-history overload.** Those names resolve to the base overloads in
  `CGAL/Arrangement_2/Arrangement_on_surface_2_global.h` via derived-to-base template deduction.
* **Overload resolution against the base `insert`**: for `Arrangement_with_history_2 arr`, both the with-history
  `insert` and the generic base `template <GT2, Tt, Curve> void insert(Arrangement_on_surface_2<GT2,Tt>&, const Curve&)`
  are viable, but binding to the *nearer* base wins ([over.ics.rank]/4.4.4), so the with-history one is selected and
  `number_of_curves()` grows. **[verified]** with `Arr_segment_traits_2` (where `Curve_2 == X_monotone_curve_2`),
  `Arr_linear_traits_2`, and `Arr_polyline_traits_2` (where `Curve_2 != X_monotone_curve_2`).
* `insert(arr, first, last)` returns `void`; the base range `insert` (`Arrangement_on_surface_2_global.h:505`) also
  returns `void` — but if you accidentally hit the base one, no history is recorded.

---

## 4. `CGAL::Arr_with_history_accessor` — `CGAL/Arrangement_2/Arr_with_history_accessor.h`

```cpp
template <class ArrWithHistory_>
class Arr_with_history_accessor {
public:
  typedef ArrWithHistory_                                    Arrangement_with_history_2;
  typedef Arr_with_history_accessor<Arrangement_with_history_2> Self;
  typedef typename Arrangement_with_history_2::Geometry_traits_2 Geometry_traits_2;
  typedef typename Arrangement_with_history_2::Topology_traits   Topology_traits;
  typedef typename Arrangement_with_history_2::Size              Size;
  typedef typename Arrangement_with_history_2::Point_2           Point_2;
  typedef typename Arrangement_with_history_2::Curve_2           Curve_2;
  typedef typename Arrangement_with_history_2::Curve_handle      Curve_handle;
  typedef typename Arrangement_with_history_2::Halfedge_handle   Halfedge_handle;
private:
  Arrangement_with_history_2* p_arr;
public:
  Arr_with_history_accessor(Arrangement_with_history_2& arr);         // stores a raw, non-owning pointer

  template <class PointLocation>
  Curve_handle insert_curve(const Curve_2& cv, const PointLocation& pl);
  Curve_handle insert_curve(const Curve_2& cv);
  template <class InputIterator>
  void         insert_curves(InputIterator begin, InputIterator end);
  Size         remove_curve(Curve_handle ch);

  Curve_handle new_curve(const Curve_2& cv);                          // allocate + push_back only; NO edges created
  void         connect_curve_edge(Curve_handle ch, Halfedge_handle he);
};
```

* **Must be instantiated with `Arrangement_on_surface_with_history_2<GT, TopTraits>`, not with
  `Arrangement_with_history_2<GT, Dcel>`** (gotcha 4). **[verified]**
* `new_curve` registers a curve with an **empty** induced-edge set (`number_of_induced_edges == 0`) and bumps
  `number_of_curves()`. **[verified]**
* `connect_curve_edge(ch, he)` does `(*ch)._insert(he)` and `he->curve().data().insert(&*ch)` — the manual way to
  attach a hand-built edge (e.g. one made with `insert_at_vertices`) to a curve. It does not check geometry.
* The accessor holds a **non-owning** pointer; it must not outlive the arrangement.

---

## 5. The data-extended traits stack

### 5.1 `_Curve_data_ex` — `CGAL/Arr_geometry_traits/Curve_data_aux.h`

```cpp
template <class BaseCurveType, class Data>
class _Curve_data_ex : public BaseCurveType {
private:
  Data m_data;
public:
  _Curve_data_ex();
  _Curve_data_ex(const BaseCurveType& cv);                 // NON-explicit  <-- plain curve converts, data empty
  _Curve_data_ex(const BaseCurveType& cv, const Data& data);
  const Data& data() const;
  Data&       data();
  void        set_data(const Data& data);
};
```

Also in that header: `template <class TYPE> struct _Default_merge_func { const TYPE& operator()(const TYPE& obj1, const TYPE&); }`
and `template <class FROM, class TO> struct _Default_convert_func { TO operator()(const FROM& obj); }`.

### 5.2 `_Unique_list` / `_Consolidate_unique_lists` — `CGAL/Arr_geometry_traits/Consolidated_curve_data_aux.h`

```cpp
template <class Data_>
class _Unique_list {
public:
  typedef Data_              Data;
  typedef _Unique_list<Data> Self;
  typedef typename std::list<Data>::const_iterator const_iterator;
private:
  std::list<Data> m_list;
public:
  _Unique_list();
  _Unique_list(const Data& data);                 // singleton, NON-explicit
  const_iterator begin() const;
  const_iterator end() const;
  std::size_t    size() const;
  const Data&    front() const;
  const Data&    back()  const;
  bool operator==(const Self& other) const;       // set equality, O(n*m) linear scans
  const_iterator find(const Data& data) const;    // linear scan
  bool insert(const Data& data);                  // false if already present
  bool erase (const Data& data);                  // false if not present
  void clear();
};

template <class Data>
struct _Consolidate_unique_lists {
  _Unique_list<Data> operator()(const _Unique_list<Data>& list1,
                                const _Unique_list<Data>& list2) const;   // union
};
```

There is **no non-const `begin()`/`end()`** — the container exposes only `const_iterator`, but `insert`, `erase` and
`clear` are non-const, and `he->curve().data()` (non-const halfedge) gives a mutable `_Unique_list&`.
This is exactly what `_remove_curve` uses (`he->curve().data().erase(p_cv)`).

### 5.3 `Arr_curve_data_traits_2` — `CGAL/Arr_curve_data_traits_2.h`

```cpp
template <typename Traits_, typename XMonotoneCurveData_,
          typename Merge_   = _Default_merge_func<XMonotoneCurveData_>,
          typename CurveData_ = XMonotoneCurveData_,
          typename Convert_ = _Default_convert_func<CurveData_, XMonotoneCurveData_> >
class Arr_curve_data_traits_2 : public Traits_ {
public:
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
  typedef Tag_true                                   Has_merge_category;          // FORCED
  typedef typename Base_traits_2::Has_do_intersect_category Has_do_intersect_category;

  typedef typename internal::Arr_complete_left_side_category<Base_traits_2>::Category   Left_side_category;
  typedef typename internal::Arr_complete_bottom_side_category<Base_traits_2>::Category Bottom_side_category;
  typedef typename internal::Arr_complete_top_side_category<Base_traits_2>::Category    Top_side_category;
  typedef typename internal::Arr_complete_right_side_category<Base_traits_2>::Category  Right_side_category;

  typedef _Curve_data_ex<Base_curve_2, Curve_data>                          Curve_2;
  typedef _Curve_data_ex<Base_x_monotone_curve_2, X_monotone_curve_data>    X_monotone_curve_2;
  typedef typename Base_traits_2::Multiplicity                              Multiplicity;

  Arr_curve_data_traits_2();
  Arr_curve_data_traits_2(const Base_traits_2& traits);
```

Overridden functors (every other functor is inherited from `Traits_`):

| Functor | Signature | Behaviour |
| --- | --- | --- |
| `Make_x_monotone_2` | `template <OutputIterator> OutputIterator operator()(const Curve_2& cv, OutputIterator oi) const` | value type `std::variant<Point_2, X_monotone_curve_2>`; attaches `Convert()(cv.data())` to each x-monotone piece; isolated points pass through unchanged |
| `Split_2` | `void operator()(const X_monotone_curve_2& cv, const Point_2& p, X_monotone_curve_2& c1, X_monotone_curve_2& c2) const` | `\pre p lies on cv but is not one of its end-points`; **both halves get `cv.data()`** |
| `Intersect_2` | `template <OutputIterator> OutputIterator operator()(const X_monotone_curve_2& cv1, const X_monotone_curve_2& cv2, OutputIterator oi) const` | value type `std::variant<std::pair<Point_2, Multiplicity>, X_monotone_curve_2>`; overlaps get `Merge()(cv1.data(), cv2.data())` |
| `Are_mergeable_2` | `bool operator()(const X_monotone_curve_2& cv1, const X_monotone_curve_2& cv2) const` | base `are_mergeable_2_object()` **and** `cv1.data() == cv2.data()`; `CGAL_error_msg("Are mergeable is not supported.")` if the base traits has no `Are_mergeable_2` |
| `Merge_2` | `void operator()(const X_monotone_curve_2& cv1, const X_monotone_curve_2& cv2, X_monotone_curve_2& c) const` | `CGAL_precondition(cv1.data() == cv2.data())`; result carries `cv1.data()`; the SFINAE guard `BOOST_MPL_HAS_XXX_TRAIT_NAMED_DEF(has_merge_2, Are_mergeable_2, false)` tests for **`Are_mergeable_2`** (not `Merge_2`) |
| `Construct_x_monotone_curve_2` | `X_monotone_curve_2 operator()(const Point_2& p, const Point_2& q) const` | `\pre p != q`; result data is **default-constructed (empty)** |
| `Construct_opposite_2` | `X_monotone_curve_2 operator()(const X_monotone_curve_2& cv) const` | keeps `cv.data()`; `CGAL_error_msg` if the base traits has no `Construct_opposite_2` |

Accessors: `make_x_monotone_2_object()`, `split_2_object()`, `intersect_2_object()`, `are_mergeable_2_object()`,
`merge_2_object()`, `construct_x_monotone_curve_2_object()`, `construct_opposite_2_object()` — all `const`,
all returning by value, each functor holding `const Base_traits_2& m_base` bound to `*this`.

### 5.4 `Arr_consolidated_curve_data_traits_2` — `CGAL/Arr_consolidated_curve_data_traits_2.h`

```cpp
template <class Traits_, class Data_>
class Arr_consolidated_curve_data_traits_2 :
  public Arr_curve_data_traits_2<Traits_,
                                 _Unique_list<Data_>,
                                 _Consolidate_unique_lists<Data_>,
                                 Data_>          // Curve_data == Data_ (a bare pointer here)
{
public:
  typedef Traits_                                     Base_traits_2;
  typedef Data_                                       Data;
  typedef _Unique_list<Data_>                         Data_container;
  typedef typename Data_container::const_iterator     Data_iterator;
  typedef typename Data_container::const_iterator     Data_const_iterator;

  typedef typename Base::Curve_2                      Curve_2;             // _Curve_data_ex<GT::Curve_2, Data_>
  typedef typename Base_traits_2::Curve_2             Base_curve_2;
  typedef typename Base::X_monotone_curve_2           X_monotone_curve_2;  // _Curve_data_ex<GT::X_monotone_curve_2, _Unique_list<Data_>>
  typedef typename Base_traits_2::X_monotone_curve_2  Base_x_monotone_curve_2;
  typedef typename Base_traits_2::Point_2             Point_2;
  typedef typename Base_traits_2::Multiplicity        Multiplicity;

  typedef typename Base_traits_2::Has_left_category   Has_left_category;
  typedef typename Base_traits_2::Has_merge_category  Base_has_merge_category;
  typedef Tag_true                                    Has_merge_category;    // FORCED
  typedef typename Base_traits_2::Has_do_intersect_category Has_do_intersect_category;
  typedef typename Base_traits_2::Left_side_category   Left_side_category;
  typedef typename Base_traits_2::Bottom_side_category Bottom_side_category;
  typedef typename Base_traits_2::Top_side_category    Top_side_category;
  typedef typename Base_traits_2::Right_side_category  Right_side_category;
};
```

Note the asymmetry that makes the arrangement-with-history work: the **input curve** carries a *single*
`Curve_2*` (`Curve_data == Data_`), while the **x-monotone curve** carries a *set* of them
(`X_monotone_curve_data == _Unique_list<Data_>`), and `Convert` is `_Default_convert_func<Data_, _Unique_list<Data_>>`,
whose `operator()` returns `_Unique_list<Data_>(obj)` via the non-explicit singleton constructor.

### 5.5 The concrete types for a with-history arrangement

For `Arr = Arrangement_with_history_2<GT, Dcel>`:

| Concept | Type |
| --- | --- |
| Underlying arrangement | `Arr::Base_arrangement_2` = `Arrangement_on_surface_2<Arr_consolidated_curve_data_traits_2<GT, GT::Curve_2*>, rebound-TopTraits>` |
| Underlying geometry traits | `Arr::Base_arrangement_2::Geometry_traits_2` = `Arr_consolidated_curve_data_traits_2<GT, GT::Curve_2*>` |
| Underlying `Curve_2` (`Data_curve_2`) | `_Curve_data_ex<GT::Curve_2, GT::Curve_2*>` |
| Underlying `X_monotone_curve_2` (`Data_x_curve_2`) | `_Curve_data_ex<GT::X_monotone_curve_2, _Unique_list<GT::Curve_2*>>` = `Arr::Base_arrangement_2::X_monotone_curve_2` |
| `he->curve()` (non-const) | `Data_x_curve_2&` **[verified]** |
| `he->curve()` (const) | `const Data_x_curve_2&` **[verified]** |
| `he->curve().data()` | `_Unique_list<GT::Curve_2*>&` (or `const&`) |
| Element of that list | `GT::Curve_2*` that really points to a `Arr::Curve_halfedges`; recover with `static_cast<Arr::Curve_halfedges*>(p)` **[verified]** |
| Underlying DCEL | `Dcel::rebind<Data_traits_2>::other` — e.g. `Arr_dcel<Data_traits_2, Arr_vertex_base<GT::Point_2>, Arr_halfedge_base<Data_x_curve_2>, Arr_face_base>` |
| Observer base | `Arr::Observer` = `Aos_observer<Arr::Base_arrangement_2>`, whose `X_monotone_curve_2` is `Data_x_curve_2` |

---

## 6. Inherited `Arrangement_on_surface_2` API and its effect on history

All of the following are inherited **unchanged** and take/return the **data-extended** x-monotone curve
(`Arr::Base_arrangement_2::X_monotone_curve_2`). A plain `GT::X_monotone_curve_2` converts implicitly with an
**empty** data list.

| Member (verbatim from `Arrangement_on_surface_2.h`) | Effect on history |
| --- | --- |
| `Vertex_handle insert_in_face_interior(const Point_2& p, Face_handle f);` | none (vertices carry no history) |
| `Halfedge_handle insert_in_face_interior(const X_monotone_curve_2& cv, Face_handle f);` | `after_create_edge` registers the edge in each curve named in `cv.data()`. With an implicitly converted plain curve: `number_of_originating_curves == 0`, no curve list entry. **[verified]** |
| `Halfedge_handle insert_from_left_vertex(const X_monotone_curve_2& cv, Vertex_handle v, Face_handle f = Face_handle());` / `(cv, Halfedge_handle prev)` | same |
| `Halfedge_handle insert_from_right_vertex(const X_monotone_curve_2& cv, Vertex_handle v, Face_handle f = Face_handle());` / `(cv, Halfedge_handle prev)` | same |
| `Halfedge_handle insert_at_vertices(const X_monotone_curve_2& cv, Vertex_handle v1, Vertex_handle v2, Face_handle f = Face_handle());` / `(cv, Halfedge_handle prev1, Vertex_handle v2)` / `(cv, Halfedge_handle prev1, Halfedge_handle prev2)` | same |
| `Vertex_handle modify_vertex(Vertex_handle v, const Point_2& p);` `\pre p geometrically equivalent` | none |
| `Face_handle remove_isolated_vertex(Vertex_handle v);` `\pre v isolated` | none |
| `Halfedge_handle modify_edge(Halfedge_handle e, const X_monotone_curve_2& cv);` `\pre cv geometrically equivalent to e's curve` | `before_modify_edge` unregisters from the **old** data set, `after_modify_edge` registers into the **new** one. Passing a plain curve **orphans** the edge (`originating == 0`) while the curve node stays in the list with `induced == 0`. **[verified]** |
| `Halfedge_handle split_edge(Halfedge_handle e, const X_monotone_curve_2& cv1, const X_monotone_curve_2& cv2);` | **hidden**; reach as `arr.Arr::Base_arrangement_2::split_edge(...)`. History follows whatever data the two curves carry. **[verified]** |
| `Halfedge_handle merge_edge(Halfedge_handle e1, Halfedge_handle e2, const X_monotone_curve_2& cv);` | **hidden**; same remark |
| `Face_handle remove_edge(Halfedge_handle e, bool remove_source = true, bool remove_target = true);` | `before_remove_edge` unregisters the edge from every originating curve. **The curve nodes stay**: `number_of_curves()` unchanged, that curve's `number_of_induced_edges()` can drop to 0. **[verified]** |
| `virtual void clear();` | overridden — frees all `Curve_halfedges` too |
| `bool is_valid() const;` and the free `bool is_valid(const Arrangement_on_surface_2<…>&)` | work; do **not** validate the history structures |
| `number_of_vertices / number_of_isolated_vertices / number_of_halfedges / number_of_edges / number_of_faces / number_of_unbounded_faces`, all `vertices_/halfedges_/edges_/faces_begin/end`, `non_const_handle(...)`, `unbounded_face()` | unchanged |

Free functions from `CGAL/Arrangement_2/Arrangement_on_surface_2_global.h` that bind to a with-history object by
derived-to-base deduction (all deduce `GeometryTraits_2 = Data_traits_2`):

* `insert_point(arr, p)` / `insert_point(arr, p, pl)` → `Vertex_handle`. Splits an existing edge through
  `Arr_accessor::split_edge`, so the split halves keep their originating curves; the point itself has no history.
  **[verified]**
* `remove_vertex(arr, v)` → `bool`. Uses the traits adaptor's `are_mergeable_2` / `merge_2` on the data curves —
  same equal-data-sets restriction as `merge_edge`.
* `remove_edge(arr, e)` → `Face_handle`. Removes the edge, then for each surviving end vertex of degree 2 calls
  `traits->are_mergeable_2_object()(e1->curve(), e2->curve())` and merges if true. With history this means:
  neighbours coming from the **same** input curve get merged (induced count drops), neighbours from **different**
  curves are left alone. **[verified]**
* `insert_non_intersecting_curve(arr, cv[, pl])`, `insert_non_intersecting_curves(arr, first, last)`,
  `zone(...)`, `do_intersect(...)`, the deprecated `insert_x_monotone_curve*` — all take/produce data-extended
  curves; if you feed them plain curves, the created edges have no originating curve.
* `insert_curve(arr, ...)`, `insert_curves(arr, ...)` in that header are **base** helpers, unrelated to
  `Arr_with_history_accessor::insert_curve`.

Point location classes (`Arr_walk_along_line_point_location<Arr>`, `Arr_naive_point_location<Arr>`,
`Arr_landmarks_point_location<Arr>`, `Arr_trapezoid_ric_point_location<Arr>`) instantiate directly on the
with-history arrangement type and are accepted by `CGAL::insert(arr, cv, pl)`. **[verified]** with
`Arr_walk_along_line_point_location`.

---

## 7. Traits requirements imposed by the with-history classes

Hard (compile-time) requirements on `GeomTraits_`, over and above the `ArrangementTraits_2` concept, because
`Arr_curve_data_traits_2` / `Arr_consolidated_curve_data_traits_2` name them unconditionally:

* `Point_2`, `Curve_2`, `X_monotone_curve_2` (`Curve_2` is mandatory — an `ArrangementXMonotoneTraits_2` that has
  no `Curve_2` cannot be used with history)
* `Multiplicity`
* `Has_left_category`
* `Has_merge_category`  ← named directly, no SFINAE
* `Has_do_intersect_category`
* Side categories are completed via `internal::Arr_complete_*_side_category`, so they may be absent.
* `Curve_2` and `X_monotone_curve_2` must be **inheritable class types** (they become base classes of
  `_Curve_data_ex`) — no `typedef`s to scalars, references or final classes.
* `Curve_2` must be copy-constructible (each `Curve_halfedges` stores a copy) and default-constructible only if you
  use the default `_Curve_data_ex()` path.
* `Make_x_monotone_2`, `Split_2`, `Intersect_2` are required (the data traits wraps them).

Soft (runtime) requirements:

* `Are_mergeable_2` and `Merge_2` are needed as soon as `are_mergeable`, `merge_edge`, `CGAL::remove_edge` or
  `CGAL::remove_vertex` runs, because `Has_merge_category` is forced to `Tag_true`. A base traits with
  `Has_merge_category = Tag_false` compiles but aborts via `CGAL_error_msg` (see gotcha 6).
* `Construct_opposite_2` only if something calls `construct_opposite_2_object()`.

Verified working base traits: `Arr_segment_traits_2<Epeck>`, `Arr_linear_traits_2<Epeck>`,
`Arr_polyline_traits_2<Arr_segment_traits_2<Epeck>>`. **[verified]**
Declared `Has_merge_category = Tag_true` (safe): `Arr_segment_traits_2`, `Arr_non_caching_segment_traits_2`,
`Arr_linear_traits_2`, `Arr_circle_segment_traits_2`, `Arr_conic_traits_2`, `Arr_Bezier_curve_traits_2`,
`Arr_rational_function_traits_2`, `Arr_geodesic_arc_on_sphere_traits_2`.
Declared `Tag_false` (unsafe, see gotcha 6): `Arr_circular_arc_traits_2`, `Arr_line_arc_traits_2`,
`Arr_circular_line_arc_traits_2`. Inherited from the base: `Arr_polycurve_traits_2`, `Arr_polyline_traits_2`,
`Arr_algebraic_segment_traits_2`, `Arr_counting_traits_2`, `Arr_tracing_traits_2`.

---

## 8. Recipes for a type-erased C++ core + Cython bindings

```cpp
using Arr   = CGAL::Arrangement_with_history_2<GT, Dcel>;
using BArr  = Arr::Base_arrangement_2;                 // the data-extended arrangement
using DXcv  = BArr::X_monotone_curve_2;                // _Curve_data_ex<GT::X_monotone_curve_2, _Unique_list<GT::Curve_2*>>
using CNode = Arr::Curve_halfedges;                    // the history node; IS-A GT::Curve_2
using ArrBaseWH = CGAL::Arrangement_on_surface_with_history_2<
                    Arr::Geometry_traits_2, Arr::Base_topology_traits>;   // for the accessor
```

* **Opaque curve id** → `CNode*` (`&*ch`). Rehydrate with `Arr::Curve_handle(ptr)`. Invalidated by
  `remove_curve`, `clear`, `assign`, `operator=`, copy-construction, `_overlay`, and destruction. Never by
  edge-level edits.
* **Opaque halfedge id** → `&*he` (`Arr::Halfedge*`); `Arr::Halfedge_handle` and `Arr::Halfedge_const_handle`
  both construct back from that pointer via `I_Filtered_iterator`'s non-explicit `template <typename T> I_Filtered_iterator(T* p)`
  (`Arrangement_2_iterators.h:292`), and the round-trip compares equal. **[verified]**
  Note `Less_halfedge_handle` already uses that address, so it is the canonical key.
  (`In_place_list<T, managed, Alloc = CGAL_ALLOCATOR(T)>`, so `Curve_iterator` is
  `In_place_list_iterator<Curve_halfedges, std::allocator<Curve_halfedges>>`.)
* **Reading a curve's geometry**: `const GT::Curve_2& cv = *ch;` (slice from `Curve_halfedges`).
* **Reading an edge's x-monotone geometry as the plain traits type**:
  `const GT::X_monotone_curve_2& x = he->curve();` (base-class binding; copying gives a plain curve).
* **Batch insert with handles**: record `n = arr.number_of_curves()` (or `it = --arr.curves_end()` when non-empty),
  call `CGAL::insert(arr, first, last)`, then walk from `curves_begin()` skipping `n` (curves are appended in input
  order). The aggregated path is a single sweep — much faster than a loop of single inserts.
* **Do not expose `Curve_iterator`/`Induced_edge_iterator` objects across the FFI boundary.** Materialize into
  `std::vector<CNode*>` / `std::vector<Arr::Halfedge*>` on the C++ side; the underlying `std::set` iterators are
  invalidated by the very next edit, and the induced-edge order is address order.
* **Removal semantics to expose explicitly**: `remove_curve(arr, ch)` returns the number of *deleted edges*, not the
  number of induced edges — edges shared with other curves survive with a shortened originating list.
* **Overlay**: after `CGAL::overlay(a1, a2, res)`, map input curve *indices* (position in the list) to output
  indices as `res` index `i` for `i < a1.number_of_curves()` from `a1`, and `i - a1.number_of_curves()` from `a2`.
  Pointer/handle identity does not carry over.
* **Assertions**: `merge_edge` uses `CGAL_precondition_msg`, and `Curve_halfedges::_insert/_erase` use
  `CGAL_assertion`. Compile the binding with `CGAL_NDEBUG`/`NDEBUG` only after you have validated preconditions in
  your own wrapper, since these abort the process rather than throw a Python-catchable exception (unless you install
  a CGAL error handler that throws).
