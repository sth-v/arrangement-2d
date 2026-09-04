# CGAL 6.1 — Arrangement_on_surface_2: global functions, overlay, observers, zone

Source of truth: the **installed** headers under `/opt/homebrew/include/CGAL` (CGAL 6.1,
`CGAL_VERSION_NR 1060101000`, release 2025‑09‑29, git `b26b07a1242`).
Everything below is quoted verbatim from those headers unless marked *(verified by compiling)*.

Headers covered:

| Header | Content |
|---|---|
| `CGAL/Arrangement_on_surface_2.h` (lines 2776–3013) | **canonical declarations** of all global functions (with default args + `\pre` docs) |
| `CGAL/Arrangement_2/Arrangement_on_surface_2_global.h` | definitions of the same |
| `CGAL/Arr_overlay_2.h` | `overlay()` (2 overloads), `Indexed_sweep_accessor` |
| `CGAL/Arr_overlay.h` | **deprecated** shim → `Arr_overlay_2.h` |
| `CGAL/Surface_sweep_2/Arr_default_overlay_traits_base.h` | `_Arr_default_overlay_traits_base` (the 10 `create_*`) |
| `CGAL/Arr_default_overlay_traits.h` | `Arr_default_overlay_traits`, `Arr_face_overlay_traits` |
| `CGAL/Arr_observer.h` | `Arr_observer` = **alias template** |
| `CGAL/Aos_observer.h` | the actual observer base class |
| `CGAL/Arr_point_location_result.h` | `Arr_point_location_result<Arr>` |
| `CGAL/Arrangement_zone_2.h` | `Arrangement_zone_2<Arr, ZoneVisitor>` (class + `init`) |
| `CGAL/Arrangement_2/Arrangement_zone_2_impl.h` | the zone **algorithm**: `compute_zone`, `_zone_in_face`, `_zone_in_overlap`, … (see §3.5) |
| `CGAL/Arrangement_2/Arr_compute_zone_visitor.h` | zone output visitor |
| `CGAL/Arrangement_2/Arr_do_intersect_zone_visitor.h` | `do_intersect` visitor |
| `CGAL/Arr_topology_traits/Arr_inc_insertion_zone_visitor.h` | the incremental-insertion zone visitor (§3.4, §3.5) |
| `CGAL/Arrangement_on_surface_with_history_2.h` | `insert` / `remove_curve` / `overlay` with history |

---

## Gotchas / surprises vs. older CGAL

1. **`Arr_observer<Arr>` is an alias templated on the *base* class.**
   `CGAL/Arr_observer.h` contains only
   `template <typename Arrangement_> using Arr_observer = typename Arrangement_::Observer;`
   and `Arrangement_on_surface_2` declares `using Observer = Aos_observer<Self>;` where
   `Self = Arrangement_on_surface_2<GT,TT>`. `Arrangement_2<Traits,Dcel>` **does not** redefine
   `Observer`, so
   `CGAL::Arr_observer<Arrangement_2<T,D>> == CGAL::Aos_observer<Arrangement_2<T,D>::Base>`
   and **`observer.arrangement()` returns `Arrangement_on_surface_2<…>*`, not `Arrangement_2<…>*`**
   *(verified by `static_assert`)*. For a type-erased core: store your own `Arrangement_2*`
   alongside, or `static_cast` down — the downcast is safe because `Arrangement_2` adds no state.
   `Aos_observer` is **non-copyable** (copy ctor and `operator=` are private & undefined).

2. **`CGAL::Object` is gone everywhere in this area.**
   Point location returns `std::variant<Vertex_const_handle, Halfedge_const_handle, Face_const_handle>`;
   `make_x_monotone_2` yields `std::variant<Point_2, X_monotone_curve_2>`; zone/intersection use
   `std::optional` + `std::variant`. Use `std::get_if<T>(&obj)` / `std::visit`. `CGAL::assign()` is dead.

3. **`zone()`'s output value type must use NON-const handles.**
   The visitor writes `std::variant<Vertex_handle, Halfedge_handle, Face_handle>`. An output
   iterator over `std::variant<Vertex_const_handle, …>` **fails to compile**
   (`no viable overloaded '='` inside `Arr_compute_zone_visitor.h:148`) *(verified)*.
   Contrast with point location, which yields **const** handles. Your binding layer will need
   `arr.non_const_handle(h)` in one direction and nothing in the other.

4. **`zone(arr, c, oi)` (no point-location) returns the *unadvanced* `oi`.**
   Its body is `zone(arr, c, oi, def_pl); return oi;` — `oi` is passed **by value** to the 4-arg
   overload, so the returned iterator is the original. Only the 4-argument overload returns a
   correctly advanced iterator (its visitor holds `OutputIterator&`). Harmless for
   `std::back_insert_iterator`, wrong for raw pointers/array iterators.

5. **`overlay()` must not write into one of its inputs**, and it **clears the result**:
   `CGAL_precondition(((void*)(&arr) != (void*)(&arr1)) && ((void*)(&arr) != (void*)(&arr2)));`
   then `arr.clear();`. The three arrangements may have different geometry traits (only
   `is_convertible` of `Point_2`/`X_monotone_curve_2` to the result types is `static_assert`ed)
   **and different DCELs / topology traits** — this is the supported way to overlay two
   differently-decorated arrangements *(verified with three distinct face-extended DCELs)*.

6. **`OverlayTraits` is a duck-typed concept, not a base class.** A plain `struct` with 10
   **non-virtual, non-`const`** member functions compiles and runs *(verified: all 10 called)*.
   The visitor holds `OverlayTraits*` (non-const), so `const` is not required — but it **is**
   required if you derive from `_Arr_default_overlay_traits_base`, whose members are
   `virtual … const`.

7. **`overlay()` mutates its `const` inputs during the sweep.** `Indexed_sweep_accessor`
   (in `Arr_overlay_2.h`) temporarily squats every input vertex's `inc()` pointer with an index
   (`vit->set_inc(reinterpret_cast<void*>(idx))`) and restores it afterwards. So `overlay` is
   **not** re-entrant/thread-safe with respect to `arr1`/`arr2` despite the `const&`. The indexed
   path is skipped only when `GeometryTraitsA_2::Bottom_side_category` is `Arr_contracted_side_tag`.

8. **`insert(arr, c, pl)` is really a 4-parameter template with a defaulted dummy.**
   The *declaration* in `Arrangement_on_surface_2.h:2795-2798` ends with
   `typename PointLocation::Point_2* = 0);` (the definition at
   `Arrangement_on_surface_2_global.h:198` repeats it **without** the default). Consequences:
   the point-location type **must** expose a nested `Point_2` typedef, otherwise this overload
   SFINAEs away and the call falls through to the `(begin, end)` range overload or the
   insertion-hint overload and fails with a confusing error. The default argument therefore only
   exists if the declaration block in `<CGAL/Arrangement_on_surface_2.h>` has been seen — always
   include `<CGAL/Arrangement_2.h>` / `<CGAL/Arrangement_on_surface_2.h>`, never
   `Arrangement_2/Arrangement_on_surface_2_global.h` alone.
   *(Verified with clang: a 3-argument `insert(arr, xcv, pl)` instantiates the 4-parameter
   `insert<GT, TT, Curve, PointLocation>` template, and a `pl` type lacking `locate` errors inside
   `Arrangement_zone_2::init`.)*

9. **Curve vs. x-monotone dispatch is by exact type identity, not convertibility.**
   Single-curve: `std::is_same<Curve, typename GT::X_monotone_curve_2>` on the **deduced**
   argument type. Range: `std::is_same<std::iterator_traits<InputIterator>::value_type,
   X_monotone_curve_2>`. For `Arr_segment_traits_2`, `Curve_2 == X_monotone_curve_2`
   *(verified)*, so segments take the fast path; for polyline/conic/Bezier traits they differ.
   Passing a merely-convertible type (e.g. `Kernel::Segment_2`) silently selects the
   *non*-x-monotone path and runs `make_x_monotone_2`.

10. **`Arrangement_2` binds to every global function by derived→base template deduction.**
    All globals take `Arrangement_on_surface_2<GT,TT>&`; `Arrangement_2<T,D>` deduces
    `GT = T`, `TT = Default_planar_topology<T,D>::Traits`. All returned handle types are the
    base's typedefs — which are the *same types* `Arrangement_2` re-exports. No conversion needed.

11. **The global `remove_edge` / `remove_vertex` are not the member functions.** The globals
    additionally *merge* the two remaining edges at a degree-2 end-vertex when
    `are_mergeable_2` says so (and then delete the vertex). `Arrangement_2::remove_edge` is
    `Face_handle remove_edge(Halfedge_handle e, bool remove_source = true, bool remove_target = true);`
    and never merges.

12. **`zone()` and `do_intersect()` do not bracket with `before/after_global_change`** (they do not
    modify). Every mutating global does. `insert(arr, general_curve)` issues **one bracket per
    x-monotone piece**, not one per call.

13. Deprecated (still present, marked `CGAL_DEPRECATED`): `insert_x_monotone_curve`,
    `insert_x_monotone_curves`, `insert_curve`, `insert_curves`; header `<CGAL/Arr_overlay.h>`.

14. **Default point-location strategy** for both bounded and unbounded planar topology traits is
    `Arr_walk_along_line_point_location<Arr>` (`Tt::Default_point_location_strategy`), and the
    default zone visitor for insertion is `Arr_inc_insertion_zone_visitor<Arr>`
    (`Tt::Zone_insertion_visitor`).

15. Observers are notified **in registration order** for every `before_*` and in **reverse
    registration order** for every `after_*` (`m_observers` is walked with `begin()..end()` vs.
    `rbegin()..rend()`).

16. **A `ZoneVisitor` that returns a valid `Halfedge_handle` is making a promise the zone checks
    with an assertion.** `Arrangement_zone_2` asserts
    `equal(m_left_pt, inserted_he->target()->point())` and then re-keys its internal intersection
    cache off `inserted_he->next()` / `inserted_he->twin()->prev()`. A read-only visitor must
    always return `std::make_pair(Halfedge_handle(), false)`. Full contract, traced call
    sequences and the traits requirements: **§3.5**.

17. **CGAL 6.1 bug**: `CGAL::insert(arr, cv)` where `cv` overlaps an existing edge *and* has an
    unbounded left end aborts with `precondition violation! Expression : cv.has_left()`
    (`Arr_linear_traits_2.h:689`) — reproducible by inserting the same `Line_2` twice. See §3.5.9.

> **Observed call sequences.** §5.5/§5.6 list the 61 observer virtuals; **§8** adds the
> *traced, verified* notification order for every real operation (insert, aggregated sweep,
> `insert_point`, `remove_edge`, `split_edge`/`merge_edge`, `overlay`, unbounded/fictitious,
> `Polygon_set_2`), a dereferenceability matrix for the handle arguments, and 15 further
> gotchas labelled **T1–T15**.

---

## 1. `Arr_point_location_result<Arrangement_>` — `<CGAL/Arr_point_location_result.h>`

```cpp
template <typename Arrangement_>
struct Arr_point_location_result {
  typedef Arrangement_                                   Arrangement_2;

  typedef typename Arrangement_2::Vertex_const_handle    Vertex_const_handle;
  typedef typename Arrangement_2::Halfedge_const_handle  Halfedge_const_handle;
  typedef typename Arrangement_2::Face_const_handle      Face_const_handle;

  typedef typename std::variant<Vertex_const_handle,
                                  Halfedge_const_handle,
                                  Face_const_handle>     Type;
  typedef Type                                           type;

  template <typename T>
  static inline Type make_result(T t) { return Type(t); }

  static inline std::optional<Type> empty_optional_result()
  { return std::optional<Type>(); }

  template <typename T>
  static inline const T* assign(const Type* obj) { return std::get_if<T>(obj); }

  static inline Type default_result();   // CGAL_error_msg — never call
};
```

* **Alternative order is fixed**: index 0 = `Vertex_const_handle`, 1 = `Halfedge_const_handle`,
  2 = `Face_const_handle`. Cython/type-erasure can switch on `obj.index()`.
* `Type` and `type` are the same; CGAL code uses both spellings interchangeably.
* Handles are **const** handles. To mutate, call `arr.non_const_handle(h)`
  (overloads for all three handle types on `Arrangement_on_surface_2`).
* Visiting: `std::visit(overloaded{...}, obj)` or the CGAL idiom used throughout 6.1:
  ```cpp
  if (const auto* f = std::get_if<Arr::Face_const_handle>(&obj))      { /* inside face */ }
  else if (const auto* e = std::get_if<Arr::Halfedge_const_handle>(&obj)) { /* on edge */ }
  else if (const auto* v = std::get_if<Arr::Vertex_const_handle>(&obj))   { /* on vertex */ }
  ```
* The default-constructed `Type` holds a **default-constructed (invalid) `Vertex_const_handle`** —
  never dereference a result you did not fill.
* Lifetime: the handles are DCEL iterators into `arr`; they stay valid until the referenced
  cell is removed (see §9).

---

## 2. Global insertion / removal / query functions

All declared in `CGAL/Arrangement_on_surface_2.h` (namespace `CGAL`), defined in
`CGAL/Arrangement_2/Arrangement_on_surface_2_global.h`.
Include `<CGAL/Arrangement_2.h>` (or `<CGAL/Arrangement_on_surface_2.h>`) to get the declarations
with their default arguments.

### 2.1 `insert` — single curve or x-monotone curve

**(a) With a point-location object** (this is the one users mean by "insert with pl"):

```cpp
template <typename GeomTraits, typename TopTraits, typename Curve,
          typename PointLocation>
void insert(Arrangement_on_surface_2<GeomTraits, TopTraits>& arr,
            const Curve& c, const PointLocation& pl,
            typename PointLocation::Point_2* = 0);
```
* `Curve` is deduced; the body dispatches on `std::is_same<Curve, GeomTraits::X_monotone_curve_2>`.
* Requires `PointLocation::Point_2` to exist (SFINAE guard that disambiguates against the
  `(begin,end)` range overload).
* Non-x-monotone path: `traits->make_x_monotone_2_object()(c, back_inserter(list<variant<Point_2,
  X_monotone_curve_2>>))`, then per piece either a zone insertion or `insert_point(arr, p, pl)`.
* Brackets each x-monotone piece with `notify_before_global_change()` /
  `notify_after_global_change()`.

**(b) Default point location:**

```cpp
template <typename GeomTraits, typename TopTraits, typename Curve>
void insert(Arrangement_on_surface_2<GeomTraits, TopTraits>& arr, const Curve& c);
```
Creates `typename TopTraits::Default_point_location_strategy def_pl(arr);`
(= `Arr_walk_along_line_point_location<Arr>`) and forwards.

**(c) With an explicit zone visitor** (public but undocumented; needed if you want to observe the
zone while inserting):

```cpp
template <typename GeometryTraits_2, typename TopologyTraits, typename Curve,
          typename PointLocation, typename ZoneVisitor>
void insert(Arrangement_on_surface_2<GeometryTraits_2, TopologyTraits>& arr,
            const Curve& c, const PointLocation& pl, ZoneVisitor &visitor);
```
and the two tag-dispatched leaves (do not call directly):
```cpp
… void insert(Arr&, const GeometryTraits_2::Curve_2& c,            const PointLocation& pl, ZoneVisitor&, std::is_same<int, double>::type);
… void insert(Arr&, const GeometryTraits_2::X_monotone_curve_2& c, const PointLocation& pl, ZoneVisitor&, std::is_same<int, int>::type);
```
(`std::is_same<int,int>::type` is `std::true_type`, `std::is_same<int,double>::type` is
`std::false_type`; the odd spelling is a legacy gcc-3.4 workaround.)

**(d) With a point-location *hint* (an already-computed location of the left endpoint):**

```cpp
template <typename GeomTraits, typename TopTraits>
void insert(Arrangement_on_surface_2<GeomTraits, TopTraits>& arr,
            const typename GeomTraits::X_monotone_curve_2& c,
            typename Arr_point_location_result<
              Arrangement_on_surface_2<GeomTraits, TopTraits> >::type obj);
```
* `obj` is taken **by value** and must describe the location of **`c`'s left (min) endpoint**.
* Only for x-monotone curves. Internally `arr_zone.init_with_hint(c, obj)`.
* *(verified: `pl.locate(min_endpoint)` then `insert(arr, c, hint)` works.)*

### 2.2 `insert` — ranges (aggregated, sweep-based)

```cpp
template <typename GeomTraits, typename TopTraits, typename InputIterator>
void insert(Arrangement_on_surface_2<GeomTraits, TopTraits>& arr,
            InputIterator begin, InputIterator end);
```
* `\pre The value type of the iterators must be Curve_2.` (doc comment; in practice it may also
  be `X_monotone_curve_2`, which selects the faster path).
* Dispatches on `std::is_same<std::iterator_traits<InputIterator>::value_type,
  X_monotone_curve_2>` to one of the two tag leaves
  `insert(arr, begin, end, std::is_same<int,int>::type)` / `…<int,double>::type`.
* Single global-change bracket around the whole aggregate.
* Chooses `insert_empty(...)` when `arr.is_empty()`, else `insert_non_empty(...)`.
* Non-x-monotone value type ⇒ `Ss2::make_x_monotone(begin, end, back_inserter(xcurves),
  back_inserter(iso_points), geom_traits)` first, so isolated points produced by
  `make_x_monotone_2` are inserted too.

Helper entry points (public, used by the above; useful if you already split your input):

```cpp
template <typename GeometryTraits_2, typename TopologyTraits, typename InputIterator>
void insert_empty(Arr& arr, InputIterator begin_xcurves, InputIterator end_xcurves);

template <typename GeometryTraits_2, typename TopologyTraits,
          typename XcInputIterator, typename PInputIterator>
void insert_empty(Arr& arr,
                  XcInputIterator begin_xcurves, XcInputIterator end_xcurves,
                  PInputIterator begin_points,  PInputIterator end_points);

template <typename GeometryTraits_2, typename TopologyTraits,
          typename XcInputIterator, typename PInputIterator>
void insert_non_empty(Arr& arr,
                      XcInputIterator begin_xcurves, XcInputIterator end_xcurves,
                      PInputIterator begin_points,  PInputIterator end_points);
```
`insert_empty` **assumes** the arrangement is empty (it constructs from scratch with
`Arr_construction_ss_visitor`); `insert_non_empty` re-sweeps the existing edges together with the
new ones (`Ss2::prepare_for_sweep` + `Arr_insertion_ss_visitor`). Neither emits the
global-change bracket — the callers do.

### 2.3 `insert_non_intersecting_curve` / `insert_non_intersecting_curves`

```cpp
template <typename GeomTraits, typename TopTraits, typename PointLocation>
typename Arrangement_on_surface_2<GeomTraits, TopTraits>::Halfedge_handle
insert_non_intersecting_curve
(Arrangement_on_surface_2<GeomTraits, TopTraits>& arr,
 const typename GeomTraits::X_monotone_curve_2& c,
 const PointLocation& pl);

template <typename GeomTraits, typename TopTraits>
typename Arrangement_on_surface_2<GeomTraits, TopTraits>::Halfedge_handle
insert_non_intersecting_curve
(Arrangement_on_surface_2<GeomTraits, TopTraits>& arr,
 const typename GeomTraits::X_monotone_curve_2& c);
```
* `\pre The interior of c does not intersect any existing edge or vertex.`
* `\return A handle for one of the new halfedges corresponding to the inserted curve,
  directed (lexicographically) from left to right.` — guaranteed orientation.
* Implementation locates both ends (via `pl.locate(construct_min/max_vertex_2(c))`, or
  `Arr_accessor::locate_curve_end` for ends with boundary conditions) and then calls exactly one of
  `arr.insert_at_vertices`, `arr.insert_from_left_vertex`, `arr.insert_from_right_vertex()->twin()`,
  `arr.insert_in_face_interior`.
* Runtime preconditions actually checked (`CGAL_precondition_msg`, i.e. only with
  `CGAL_NDEBUG` off): *"The curve must not intersect an existing edge."* / *"The curve must not
  overlap an existing edge."* — both ends must **not** locate on a `Halfedge_const_handle`.
  Also `CGAL_assertion_msg((fh1 != nullptr) && (fh2 != nullptr) && ((*fh1) == (*fh2)),
  "The curve intersects the interior of existing edges.")` when neither end hits a vertex.
  **If the precondition is violated with assertions disabled, `new_he` stays default-constructed
  (invalid) and is returned** — a binding must not blindly dereference it.
* Bracketed by `notify_before_global_change()` / `notify_after_global_change()`.

```cpp
template <typename GeomTraits, typename TopTraits, typename InputIterator>
void insert_non_intersecting_curves
(Arrangement_on_surface_2<GeomTraits, TopTraits>& arr,
 InputIterator begin, InputIterator end);
```
* `\pre The value type of the iterators must be X_monotone_curve_2. The curves in the range are
  pairwise interior-disjoint, and their interiors do not intersect any existing edge or vertex.`
* Returns `void` — **no handles are produced**; you must re-find the edges if you need them.
* Uses `No_intersection_surface_sweep_2` with `No_overlap_event_base` / `No_overlap_subcurve`
  (faster, but a violated precondition is *not* detected).
* Helper entry points, same shape as §2.2:
  `non_intersecting_insert_empty(arr, begin_xcurves, end_xcurves)`,
  `non_intersecting_insert_empty(arr, xc_begin, xc_end, p_begin, p_end)`,
  `non_intersecting_insert_non_empty(arr, xc_begin, xc_end, p_begin, p_end)`.

### 2.4 `insert_point`

```cpp
template <typename GeomTraits, typename TopTraits, typename PointLocation>
typename Arrangement_on_surface_2<GeomTraits, TopTraits>::Vertex_handle
insert_point(Arrangement_on_surface_2<GeomTraits, TopTraits>& arr,
             const typename GeomTraits::Point_2& p,
             const PointLocation& pl);

template <typename GeomTraits, typename TopTraits>
typename Arrangement_on_surface_2<GeomTraits, TopTraits>::Vertex_handle
insert_point(Arrangement_on_surface_2<GeomTraits, TopTraits>& arr,
             const typename GeomTraits::Point_2& p);
```
* No precondition — `p` may lie in a face (→ `insert_in_face_interior`, isolated vertex), in the
  interior of an edge (→ `split_2_object()` on the edge's curve + `arr.split_edge`, returns
  `split_he->target()`), or on an existing vertex (→ `arr.modify_vertex(v, p)`).
* Requires the traits to model `ArrangementXMonotoneTraits_2::Split_2` when `p` can land on an edge.
* Bracketed by the global-change notifications.

### 2.5 `remove_edge`

```cpp
template <typename GeomTraits, typename TopTraits>
typename Arrangement_on_surface_2<GeomTraits, TopTraits>::Face_handle
remove_edge(Arrangement_on_surface_2<GeomTraits, TopTraits>& arr,
            typename Arrangement_on_surface_2<GeomTraits,
                                              TopTraits>::Halfedge_handle e);
```
* `\param e The edge to remove (one of the pair of twin halfedges). \return A handle for the
  remaining face.`
* Behaviour beyond `arr.remove_edge(e)`: for each of the two end-vertices that survives
  (was neither at an open boundary nor of degree 1 before removal) and now has degree 2, if
  `traits->are_mergeable_2_object()(e1->curve(), e2->curve())` then
  `traits->merge_2_object()(e1->curve(), e2->curve(), cv); arr.merge_edge(e1, e2, cv);`.
* Requires `ArrangementXMonotoneTraits_2::Are_mergeable_2` and `Merge_2`.
* After the call, `e` and its twin are **dangling**; so are the halfedge handles of any merged
  pair except the merged result. Bracketed by the global-change notifications.

### 2.6 `remove_vertex`

```cpp
template <typename GeomTraits, typename TopTraits>
bool remove_vertex(Arrangement_on_surface_2<GeomTraits, TopTraits>& arr,
                   typename Arrangement_on_surface_2<GeomTraits,
                                                     TopTraits>::Vertex_handle v);
```
* `\return Whether the vertex has been removed or not.`
* Isolated `v` → `arr.remove_isolated_vertex(v)`, returns `true`.
* `v->degree() == 2` and the two incident curves are mergeable → merge + vertex disappears,
  returns `true`.
* Otherwise returns `false` and the arrangement is unchanged.
* Bracketed by the global-change notifications.

### 2.7 `is_valid`

```cpp
template <typename GeomTraits, typename TopTraits>
bool is_valid(const Arrangement_on_surface_2<GeomTraits, TopTraits>& arr);
```
* Takes a **const** arrangement. Cost is a full sweep, so *O(n log n)* at least.
* Steps: (1) `arr.is_valid()` (the member topological/DCEL check); (2) sweep all edge curves with
  `Ss2::Do_interior_intersect_visitor` → all edges must be interior-disjoint
  (`CGAL_warning_msg(..., "Arrangement edges are not disjoint in their interior.")`);
  (3) for every inner CCB (hole) leftmost vertex and every isolated vertex, ray-shoot down with
  `TopTraits::Default_point_location_strategy::ray_shoot_down` and verify the containing face
  (`CGAL_warning_msg(false, "An inner component is located in the wrong face.")`).
* Note that step (3) uses `std::get_if<Halfedge_const_handle>` / `<Vertex_const_handle>` /
  `<Face_const_handle>` on the ray-shooting variant — the ray-shooting result type is the same
  `Arr_point_location_result<Arr>::Type`.

### 2.8 `zone`

```cpp
template <typename GeomTraits, typename TopTraits,
          typename OutputIterator, typename PointLocation>
OutputIterator zone(Arrangement_on_surface_2<GeomTraits, TopTraits>& arr,
                    const typename GeomTraits::X_monotone_curve_2& c,
                    OutputIterator oi,
                    const PointLocation& pl);

template <typename GeomTraits, typename TopTraits, typename OutputIterator>
OutputIterator zone(Arrangement_on_surface_2<GeomTraits, TopTraits>& arr,
                    const typename GeomTraits::X_monotone_curve_2& c,
                    OutputIterator oi);
```
* From the header doc: *"`oi` the output iterator for the resulting zone elements. Its dereference
  type is a variant that wraps a `Vertex_handle`, a `Halfedge_handle`, or a `Face_handle`."*
* **Exact required value type**: `std::variant<typename Arr::Vertex_handle,
  typename Arr::Halfedge_handle, typename Arr::Face_handle>` (non-const, in that alternative
  order). Anything else fails to compile *(verified)*.
* `arr` is taken by **non-const** reference even though `zone` does not modify it (the
  `Arrangement_zone_2` machinery needs non-const handles).
* No observer notifications are issued.
* Output order: for the first subcurve, the feature containing its left endpoint (vertex,
  else halfedge) if any; then, per subcurve, the containing `Face_handle` (or the overlapped
  `Halfedge_handle`), then the feature at its right endpoint if any. No feature is emitted twice
  at a shared endpoint.
* **The 3-argument version returns `oi` unadvanced** (see gotcha 4).
* Typical use:
  ```cpp
  using Zone_elem = std::variant<Arr::Vertex_handle, Arr::Halfedge_handle, Arr::Face_handle>;
  std::list<Zone_elem> elems;
  CGAL::zone(arr, xcv, std::back_inserter(elems));          // default walk PL
  CGAL::zone(arr, xcv, std::back_inserter(elems), pl);      // explicit PL
  ```

### 2.9 `do_intersect`

```cpp
template <typename GeomTraits, typename TopTraits, typename Curve,
          typename PointLocation>
bool do_intersect(Arrangement_on_surface_2<GeomTraits, TopTraits>& arr,
                  const Curve& c, const PointLocation& pl);

template <typename GeomTraits, typename TopTraits, typename Curve>
bool do_intersect(Arrangement_on_surface_2<GeomTraits, TopTraits>& arr,
                  const Curve& c);
```
plus the two tag leaves (do not call directly):
```cpp
bool do_intersect(Arr&, const GeomTraits::X_monotone_curve_2& c, const PointLocation& pl, std::is_same<int, int>::type);
bool do_intersect(Arr&, const GeomTraits::Curve_2& c,            const PointLocation& pl, std::is_same<int, double>::type);
```
* Same `Curve` vs. `X_monotone_curve_2` identity dispatch as `insert`.
* x-monotone leaf: runs a zone with `Arr_do_intersect_zone_visitor<Arr>` (stops at the first hit)
  and returns `visitor.do_intersect()`.
* General-curve leaf: splits with `make_x_monotone_2`, recurses per x-monotone piece, and for each
  isolated point returned by the split, **returns `true` if the point lies in a face interior**
  (`std::get_if<Face_const_handle>(&pl.locate(*iso_p)) != nullptr`) — note the inverted-looking
  logic: an isolated point strictly inside a face counts as an "intersection" here.
* `arr` is non-const. No notifications.

---

## 3. `Arrangement_zone_2<Arrangement_, ZoneVisitor_>` — `<CGAL/Arrangement_zone_2.h>`

```cpp
template <typename Arrangement_, typename ZoneVisitor_>
class Arrangement_zone_2 {
public:
  using Arrangement_2     = Arrangement_;
  using Geometry_traits_2 = typename Arrangement_2::Geometry_traits_2;
  using Topology_traits   = typename Arrangement_2::Topology_traits;

  using Visitor         = ZoneVisitor_;
  using Vertex_handle   = typename Arrangement_2::Vertex_handle;
  using Halfedge_handle = typename Arrangement_2::Halfedge_handle;
  using Face_handle     = typename Arrangement_2::Face_handle;
  using Visitor_result  = std::pair<Halfedge_handle, bool>;
  using Point_2            = typename Geometry_traits_2::Point_2;
  using X_monotone_curve_2 = typename Geometry_traits_2::X_monotone_curve_2;
  using Multiplicity       = typename Geometry_traits_2::Multiplicity;
```
No defaults for either template parameter.

Public members:
```cpp
  Arrangement_zone_2(Arrangement_2& arr, Visitor* visitor);   // calls visitor->init(&arr)

  template <typename PointLocation>
  void init(const X_monotone_curve_2& cv, const PointLocation& pl);

  void init_with_hint(const X_monotone_curve_2& cv, Pl_result_type obj);

  void compute_zone();
```
* `Pl_result_type = typename Arr_point_location_result<Arrangement_2>::Type` (protected typedef,
  but the parameter type is the public variant).
* Ctor asserts `visitor != nullptr` and immediately calls `visitor->init(&arr)`.
* `init()` locates the **left (min) end** of `cv`; for ends with boundary conditions it uses
  `Arr_accessor::locate_curve_end(cv, ARR_MIN_END, bx, by)` instead of `pl.locate`.
* `init_with_hint()` takes the pre-computed location of the left end.
* `compute_zone()` may be called once per `init`/`init_with_hint`; the object is reusable
  (details, and what state carries over: **§3.5.8**).
* Protected intersection types worth knowing (they mirror the 6.1 traits `Intersect_2`):
  ```cpp
  using Intersection_point     = std::pair<Point_2, Multiplicity>;
  using Intersection_result    = std::variant<Intersection_point, X_monotone_curve_2>;
  using Optional_intersection  = std::optional<Intersection_result>;
  ```

### 3.1 The `ZoneVisitor` concept (from the class doc comment)

> The visitor has to support the following functions:
> * `init()`, for initializing the visitor with a given arrangement.
> * `found_subcurve()`, called when a non-intersecting x-monotone curve is computed and located in
>   the arrangement.
> * `found_overlap()`, called when an x-monotone curve overlaps an existing halfedge in the
>   arrangement.
> Both the second and the third functions return `pair<Halfedge_handle, bool>`, where the halfedge
> handle corresponds to the halfedge created or modified by the visitor (if valid), and the Boolean
> value indicates whether we should halt the zone-computation process.

Exact signatures (from `Arr_compute_zone_visitor` / `Arr_do_intersect_zone_visitor` /
`Arr_inc_insertion_zone_visitor`):

```cpp
  typedef std::pair<Halfedge_handle, bool> Result;

  void   init(Arrangement_2* arr);                       // NOTE: pointer, not reference

  Result found_subcurve(const X_monotone_curve_2& cv,
                        Face_handle face,
                        Vertex_handle left_v,  Halfedge_handle left_he,
                        Vertex_handle right_v, Halfedge_handle right_he);

  Result found_overlap(const X_monotone_curve_2& cv,
                       Halfedge_handle he,                // directed left to right
                       Vertex_handle left_v, Vertex_handle right_v);
```
Invalid vertex/halfedge handles are default-constructed handles; compare with `== Vertex_handle()`.

### 3.2 `Arr_compute_zone_visitor<Arrangement_, OutputIterator_>` — `<CGAL/Arrangement_2/Arr_compute_zone_visitor.h>`

```cpp
template <class Arrangement_, class OutputIterator_>
class Arr_compute_zone_visitor {
public:
  typedef OutputIterator_ OutputIterator;
  typedef Arrangement_    Arrangement_2;
  typedef typename Arrangement_2::Vertex_handle   Vertex_handle;
  typedef typename Arrangement_2::Halfedge_handle Halfedge_handle;
  typedef typename Arrangement_2::Face_handle     Face_handle;
  typedef typename Arrangement_2::Point_2             Point_2;
  typedef typename Arrangement_2::X_monotone_curve_2  X_monotone_curve_2;
  typedef std::pair<Halfedge_handle, bool>            Result;

  Arr_compute_zone_visitor(OutputIterator& oi);           // stores OutputIterator&
  void init(Arrangement_2*);                              // resets output_left = true
  Result found_subcurve(const X_monotone_curve_2&, Face_handle face,
                        Vertex_handle left_v, Halfedge_handle left_he,
                        Vertex_handle right_v, Halfedge_handle right_he);
  Result found_overlap(const X_monotone_curve_2&, Halfedge_handle he,
                       Vertex_handle left_v, Vertex_handle right_v);
};
```
* **The constructor stores a reference to your output iterator** — it must outlive the visitor.
* Both handlers always return `Result(invalid_he, false)` (never halt, never modify).
* Emits `Zone_result = std::variant<Vertex_handle, Halfedge_handle, Face_handle>`.

### 3.3 `Arr_do_intersect_zone_visitor<Arrangement_>` — `<CGAL/Arrangement_2/Arr_do_intersect_zone_visitor.h>`

```cpp
template <class Arrangement_>
class Arr_do_intersect_zone_visitor {
public:
  typedef Arrangement_ Arrangement_2;
  typedef typename Arrangement_2::Vertex_handle   Vertex_handle;
  typedef typename Arrangement_2::Halfedge_handle Halfedge_handle;
  typedef typename Arrangement_2::Face_handle     Face_handle;
  typedef typename Arrangement_2::Point_2             Point_2;
  typedef typename Arrangement_2::X_monotone_curve_2  X_monotone_curve_2;
  typedef std::pair<Halfedge_handle, bool>            Result;

  Arr_do_intersect_zone_visitor();                 // m_intersect = false
  void init(Arrangement_2*);                       // m_intersect = false
  Result found_subcurve(const X_monotone_curve_2&, Face_handle,
                        Vertex_handle left_v, Halfedge_handle left_he,
                        Vertex_handle right_v, Halfedge_handle right_he);
  Result found_overlap(const X_monotone_curve_2&, Halfedge_handle,
                       Vertex_handle, Vertex_handle);
  bool do_intersect() const;
};
```
* `found_subcurve` reports "no intersection" only when **all four** of `left_v`, `right_v`,
  `left_he`, `right_he` are invalid; otherwise sets `m_intersect = true` and returns
  `Result(invalid_he, true)` (halt).
* `found_overlap` always sets `m_intersect = true` and halts.

### 3.4 `Arr_inc_insertion_zone_visitor<Arrangement_>` — `<CGAL/Arr_topology_traits/Arr_inc_insertion_zone_visitor.h>`

This is `TopologyTraits::Zone_insertion_visitor`, the default visitor used by `insert`. Same
concept, plus `typedef std::pair<Halfedge_handle,bool> Result;`, default ctor, and
`void init(Arrangement_2* arr)`. It performs the actual DCEL insertion, so it *does* return valid
halfedge handles from `found_subcurve` / `found_overlap`.

### 3.5 Algorithm semantics and the visitor call sequence — `<CGAL/Arrangement_2/Arrangement_zone_2_impl.h>`

Everything in §3.5 comes from `Arrangement_zone_2.h` (472 lines, the class) and
`Arrangement_2/Arrangement_zone_2_impl.h` (1340 lines, every member definition), read in full.
Line numbers below are into `Arrangement_zone_2_impl.h` unless stated otherwise. Claims marked
**[verified]** were produced by compiling and running an instrumented `ZoneVisitor` that logs every
call with its arguments (segment / circle-segment / linear / conic / Bézier / polyline /
geodesic-sphere traits), built with
`/usr/bin/clang++ -std=c++17 -O0 -DCGAL_USE_CORE -DCGAL_USE_GMP -DCGAL_USE_MPFR -I/opt/homebrew/include -L/opt/homebrew/lib -lgmp -lmpfr`.

> **Naming correction to §3.** The intersection cache member is `m_inter_map`, not
> `m_intersect_map` (`Arrangement_zone_2.h:128`).

#### 3.5.0 Gotchas specific to the zone algorithm

1. **A visitor returning a valid `Halfedge_handle` makes a hard promise.** The zone asserts
   `equal(m_left_pt, inserted_he->target()->point())` (line 1153 / 1326): the handle you return
   **must be directed along `cv` left→right**, with `target()` at `cv`'s right endpoint. Returning
   `inserted_he->twin()` aborts with
   `assertion violation! Expression : equal(m_left_pt, inserted_he->target()->point())` **[verified]**.
   With `NDEBUG` it would silently corrupt the walk instead. A **non-modifying** visitor must always
   return `Halfedge_handle()`.
2. **The very last `found_subcurve` call ignores your return value entirely** (line 1008: the
   result is not even stored; `_zone_in_face` just `return true;`). You cannot halt on it, and its
   `.first` is discarded — but the incremental-insertion visitor still needs to insert there.
3. **`found_overlap` gets no `Face_handle` and no `left_he`/`right_he`.** For an overlapped stretch
   the zone reports only the edge; the visitor must find its own predecessors
   (`Arr_accessor::locate_around_vertex`).
4. **`visitor->init()` is called ONCE, by the `Arrangement_zone_2` constructor** (`Arrangement_zone_2.h:191`),
   **not** by `init()`/`init_with_hint()`. Reusing one zone object for several curves — which is
   exactly what `CGAL::insert` does — does **not** reset the visitor **[verified: a call counter in
   the visitor kept increasing across three `init()`+`compute_zone()` rounds]**.
5. **A wrong `init_with_hint` object is never detected.** Handing the unbounded face for a curve
   that lies inside a bounded face produces a complete, plausible, **wrong** zone with no assertion
   **[verified]**.
6. **`compute_zone()` twice without an intervening `init()` silently re-reports the tail** of the
   previous curve (`m_cv` still holds the last remaining piece, `m_obj` still holds the *original*
   left-end location) **[verified]**.
7. **CGAL 6.1 bug, reachable from the public API**: inserting an x-monotone curve that overlaps an
   existing edge *and* whose left end is unbounded aborts — see §3.5.9.

---

#### 3.5.1 Protected state carried between visitor calls (`Arrangement_zone_2.h:121-173`)

Verbatim declarations, with what they mean *at the moment a visitor is called*:

```cpp
  Arrangement_2& m_arr;                 // The associated arrangement.
  const Traits_adaptor_2* m_geom_traits; // Its associated geometry traits.
  Arr_accessor<Arrangement_2> m_arr_access; // An accessor for the arrangement.
  Visitor* m_visitor;                   // The zone visitor.
  Intersect_map m_inter_map;            // Stores all computed intersections.
  const Vertex_handle m_invalid_v;      // An invalid vertex handle.
  const Halfedge_handle m_invalid_he;   // An invalid halfedge handle.
  X_monotone_curve_2 m_cv;              // The current portion of the inserted curve.
  Pl_result_type m_obj;                 // The location of the left endpoint.
  bool m_has_left_pt;                   // Is the left end of the curve bounded.
  bool m_left_on_boundary;              // Is the left point on the boundary.
  Point_2 m_left_pt;                    // Its current left endpoint.
  bool m_has_right_pt;                  // Is the right end of the curve bounded.
  bool m_right_on_boundary;             // Is the right point on the boundary.
  Point_2 m_right_pt;                   // Its right endpoint (if bounded).
  Vertex_handle m_left_v;               // arrangement vertex at m_left_pt (if any)
  Halfedge_handle m_left_he;            // predecessor for cv around m_left_v, or the
                                        // halfedge containing m_left_pt in its interior
  Vertex_handle m_right_v;              // arrangement vertex at m_right_pt (if any)
  Halfedge_handle m_right_he;           // ditto for the right end
  Point_2 m_intersect_p;                // The next intersection point.
  Multiplicity m_ip_multiplicity;       // Its multiplicity (0 in case of an overlap).
  bool m_found_intersect;               // An intersection has been found (or an overlap).
  X_monotone_curve_2 m_overlap_cv;      // The currently discovered overlap.
  bool m_found_overlap;                 // An overlap has been found.
  bool m_found_iso_vert;                // isolated vertex induces the next intersection
  Vertex_handle m_intersect_v;          // The vertex that intersects cv.
  Halfedge_handle m_intersect_he;       // The halfedge that intersects cv (or overlaps it).
  X_monotone_curve_2 m_sub_cv1;         // Auxiliary variable (for curve split).
  X_monotone_curve_2 m_sub_cv2;         // Auxiliary variable (for curve split).
```

`m_cv` is **rewritten in place** as the algorithm advances: after each reported piece,
`m_cv = m_sub_cv2` (lines 1039, 1314) and `m_left_pt` moves to the split point. So `m_cv` is *the
remaining, not-yet-reported portion of the query curve*, and the query curve you passed to `init()`
is copied (`m_cv = cv;`) — the zone does **not** keep a reference to your curve, you may destroy it
after `init()`.

Intersection-cache types (`Arrangement_zone_2.h:107-116`):
```cpp
  using Intersection_point = std::pair<Point_2, Multiplicity>;
  using Intersection_result = std::variant<Intersection_point, X_monotone_curve_2>;
  using Optional_intersection = std::optional<Intersection_result>;
  using Intersect_list = std::list<Intersection_result>;
  using Intersect_map = std::map<const X_monotone_curve_2*, Intersect_list>;
```

---

#### 3.5.2 `init()` — exact behaviour (`Arrangement_zone_2.h:198-243`, inline in the class)

```cpp
  template <typename PointLocation>
  void init(const X_monotone_curve_2& cv, const PointLocation& pl) {
    m_cv = cv;
    auto bx1 = ps_in_x(m_cv, ARR_MIN_END);
    auto by1 = ps_in_y(m_cv, ARR_MIN_END);
    if ((bx1 == ARR_INTERIOR) && (by1 == ARR_INTERIOR)) {
      m_has_left_pt = true;
      m_left_on_boundary = (bx1 != ARR_INTERIOR || by1 != ARR_INTERIOR);   // always false here
      m_left_pt = m_geom_traits->construct_min_vertex_2_object()(m_cv);
      m_obj = pl.locate(m_left_pt);
    }
    else {
      m_has_left_pt = m_geom_traits->is_closed_2_object()(m_cv, ARR_MIN_END);
      m_left_on_boundary = true;
      if (m_has_left_pt) m_left_pt = m_geom_traits->construct_min_vertex_2_object()(m_cv);
      m_obj = m_arr_access.locate_curve_end(m_cv, ARR_MIN_END, bx1, by1);
    }
    // ... right end: m_has_right_pt / m_right_on_boundary / m_right_pt, no location computed
  }
```

* Only the **left (min) end** is located. The right end is only classified.
* For an interior left end the *user's* `PointLocation` is used; for a boundary/unbounded left end
  the point-location object is bypassed entirely and `Arr_accessor::locate_curve_end` (which
  forwards to `TopologyTraits::locate_curve_end`) is used instead
  (`Arr_accessor.h:105-128`, quoted in `dcel_and_accessor.md`).
* `PointLocation` is only required to have `Pl_result_type locate(const Point_2&) const`.

#### 3.5.3 `init_with_hint(cv, obj)` (lines 29-74)

```cpp
template <typename Arrangement, typename ZoneVisitor>
void Arrangement_zone_2<Arrangement, ZoneVisitor>::
init_with_hint(const X_monotone_curve_2& cv, Pl_result_type obj);
```

* `Pl_result_type` = `typename Arr_point_location_result<Arrangement_2>::Type`
  = `std::variant<Vertex_const_handle, Halfedge_const_handle, Face_const_handle>` — **const**
  handles, taken **by value**.
* It classifies both ends exactly like `init()` **except** that `m_has_left_pt` is always
  `is_closed_2(cv, ARR_MIN_END)` and `m_left_on_boundary` is `(ps_x != ARR_INTERIOR || ps_y != ARR_INTERIOR)`.
  It performs **no** location at all; it just stores `m_obj = obj;`.
* **Where a caller gets `obj`:**
  * interior left end → `pl.locate(traits.construct_min_vertex_2_object()(cv))` with any
    point-location strategy (§ point-location map);
  * boundary/unbounded left end →
    `Arr_accessor<Arr>(arr).locate_curve_end(cv, CGAL::ARR_MIN_END, ps_x, ps_y)`.
  * CGAL's own use is the 3-arg `insert(arr, c, obj)` overload
    (`Arrangement_2/Arrangement_on_surface_2_global.h:545-555`).
* **A wrong hint is never validated.** `compute_zone()` simply `std::get_if`s the variant:
  * a wrong **Face** → the whole zone is computed against that face's CCBs; result is silently
    wrong. **[verified]** — hint = unbounded face for the query segment `(2,2)-(8,8)` strictly
    inside a unit-square face produced exactly one `found_subcurve(cv=2 2 8 8, face=F0(unbounded),
    all four handles INVALID)`; the correct answer is `F1(bounded)`.
  * a wrong **Vertex** with `m_has_left_pt == false` fires `CGAL_assertion(m_has_left_pt)` (line 105).
  * a wrong **Halfedge** feeds `_direct_intersecting_edge_to_right`, guarded only by
    `CGAL_exactness_assertion(compare_y_at_x_2(cv_left_pt, query_he->curve()) == EQUAL)` (line 401)
    — inactive unless exactness assertions are enabled.
* Practical rule for a type-erased core: expose `zone_with_hint` only if you locate the hint
  yourself from `min_vertex(cv)`; never let a caller supply it.

---

#### 3.5.4 `compute_zone()` — dispatch and main loop (lines 80-245)

Structure, condensed but with the real branch conditions:

```
compute_zone():
  m_found_intersect = m_found_overlap = m_found_iso_vert = false
  m_left_v = m_left_he = m_right_v = m_right_he = INVALID
  switch (m_obj):
    Vertex_const_handle vh:                                  # lines 102-122
        CGAL_assertion(m_has_left_pt)
        m_left_v = m_arr.non_const_handle(*vh)
        # NOTE: the boundary-vertex branch that would set m_left_he via
        #       m_arr_access.locate_around_boundary_vertex(...) is `#if 0`-ed out in 6.1
    Halfedge_const_handle hh:                                # lines 123-159
        if m_has_left_pt:
            m_left_he = _direct_intersecting_edge_to_right(m_cv, m_left_pt, non_const(*hh))
            if m_found_overlap:                              # the two curves coincide to the right
                m_overlap_cv = get<X_monotone_curve_2>(*_compute_next_intersection(m_intersect_he,false,dummy))
                _remove_next_intersection(m_intersect_he)
                done = _zone_in_overlap()
        else:            # unbounded left end that already lies on an existing edge
            m_intersect_he = non_const(*hh)
            m_overlap_cv  = get<X_monotone_curve_2>(*_compute_next_intersection(m_intersect_he,false,dummy))
            _remove_next_intersection(m_intersect_he)
            done = _zone_in_overlap()
    Face_const_handle fh:                                    # lines 160-172
        done = _zone_in_face(non_const(*fh), /*on_boundary=*/false)
        if (!done && m_found_overlap) done = _zone_in_overlap()

  while (!done):                                             # lines 176-240
    if m_left_he == INVALID:
        if m_left_v != INVALID:
            if !m_left_v->is_isolated(): m_found_overlap = _find_prev_around_vertex(m_left_v, m_left_he)
            else:                        m_found_iso_vert = true
        else:
            CGAL_assertion(m_right_he != INVALID)
            m_left_he = _direct_intersecting_edge_to_right(m_cv, m_left_pt, m_right_he)
        if m_found_overlap:
            m_overlap_cv = get<X_monotone_curve_2>(*_compute_next_intersection(m_intersect_he,false,dummy))
            _remove_next_intersection(m_intersect_he)
            done = _zone_in_overlap(); continue
    if (m_left_v == INVALID || !m_left_v->is_isolated()):
        done = _zone_in_face(m_left_he->face(), /*on_boundary=*/true)
    else:
        done = _zone_in_face(m_left_v->face(),  /*on_boundary=*/false)
    if (!done && m_found_overlap) done = _zone_in_overlap()

  m_inter_map.clear()                                        # line 244 — ALWAYS, incl. on halt
```

`_zone_in_face`'s stated precondition (line 996, verbatim):
```cpp
  CGAL_precondition((! on_boundary &&
                     (((m_left_v == m_invalid_v) &&
                       (m_left_he == m_invalid_he)) ||
                      m_left_v->is_isolated())) ||
                    (on_boundary && (m_left_he != m_invalid_he)));
```

`_leftmost_intersection_with_face_boundary(face, on_boundary)` (lines 895-965) walks **all outer
CCBs, all inner CCBs (holes), and all isolated vertices** of `face`, skipping fictitious halfedges
(`if (he_curr->is_fictitious()) return;`, line 811) and skipping a halfedge whose twin already
produced the current best (`if (m_found_intersect && (m_intersect_he == he_curr->twin())) return;`).
It is **O(size of the face boundary) per reported piece** — a zone through a face with a large CCB
is quadratic. Relevant when sizing a binding's expectations.

---

#### 3.5.5 `found_subcurve` — the exact contract

```cpp
  Result found_subcurve(const X_monotone_curve_2& cv,
                        Face_handle face,
                        Vertex_handle left_v,  Halfedge_handle left_he,
                        Vertex_handle right_v, Halfedge_handle right_he);
  // Result = std::pair<Halfedge_handle, bool>
```

There are exactly **two** call sites.

**(A) Terminal call — the remaining curve lies wholly inside `face`** (lines 1006-1012, verbatim):
```cpp
    // Notify the visitor that the entire curve lies within the given face,
    // such that its right endpoint is not incident to any arrangement feature.
    m_visitor->found_subcurve(m_cv, face, m_left_v, m_left_he,
                              m_invalid_v, m_invalid_he);
    // Indicate that we are done with the zone-computation process.
    return true;
```
* `cv == m_cv` — the entire remaining portion, **not** split.
* `right_v` and `right_he` are **hard-coded invalid**, regardless of what the geometry looks like.
* **The `Result` is discarded**: you cannot halt here (you are already done) and your inserted
  halfedge is not used. **[verified]**

**(B) Split call — the curve hits the face boundary** (lines 1080-1086, verbatim):
```cpp
  Visitor_result visitor_res = m_visitor->found_subcurve(m_sub_cv1, face,
                                                         m_left_v, m_left_he,
                                                         m_right_v, m_right_he);
  // Check if we are done (either we have no remaining curve or if the
  // visitor has indicated we should end the process).
  if (done || visitor_res.second) return true;
```

**`cv` is always a piece of *your* query curve, never an arrangement curve.** It is `m_sub_cv1`,
produced at line 1033 by
`m_geom_traits->split_2_object()(m_cv, m_intersect_p, m_sub_cv1, m_sub_cv2);`
(or `m_sub_cv1 = m_cv` when the leftmost intersection coincides with the query's own right
endpoint, line 1029-1032). Pieces are emitted **strictly left to right and contiguously**: the right
endpoint of piece *k* is the left endpoint of piece *k*+1, because the algorithm then does
`m_left_pt = m_intersect_p; m_cv = m_sub_cv2;`. **[verified in every traced case]**

**`face`** — the face whose *interior* contains the interior of `cv`. It is `m_left_he->face()`
(when the left end sits on a boundary), `m_left_v->face()` (isolated left vertex), or the located
face. For an **even-multiplicity (tangential) crossing the same face is reported on both sides**
**[verified: circle + tangent segment → two `found_subcurve` calls, both `face=F0(unbounded)`]**.

**The four handles.** `INVALID` below means a default-constructed handle; test with
`h == Halfedge_handle()` / `v == Vertex_handle()` (that is exactly what all three stock visitors do,
via `const Halfedge_handle invalid_he;` members).

| `left_v` | `left_he` | meaning | **[verified]** example |
|---|---|---|---|
| valid | valid | The left endpoint **is** an existing vertex, and `left_he` is the **predecessor of `cv` around that vertex**: `left_he->target() == left_v`, `left_he->face() == face`, and `cv` belongs between `left_he` and `left_he->next()` clockwise around `left_v`. Produced by `_find_prev_around_vertex` (line 289) or by the multiplicity rules below. | edge `(0,0)-(10,0)`, query `(0,0)-(5,5)` → `left_v=V(0 0)`, `left_he=HE[(10 0)->(0 0) R2L]` |
| valid | INVALID | The left endpoint is an existing vertex that is **isolated** (`m_left_v->is_isolated()`), or a vertex whose predecessor the zone could not determine (multiplicity 0, or right after an overlap step). **The visitor must locate around the vertex itself** — `Arr_accessor::locate_around_vertex(left_v, cv)`. | isolated vertex `(5,5)`, query `(0,5)-(10,5)` → 2nd piece `left_v=V(5 5)`, `left_he=INVALID` |
| INVALID | valid | The left endpoint lies in the **interior** of `left_he`'s curve. `left_he` is the member of the twin pair whose incident face is `face` (chosen by `_direct_intersecting_edge_to_right`, or as `m_right_he->twin()` / `m_right_he` by the multiplicity rules). The visitor must **split** that edge to create the vertex. | three vertical edges, query `(0,0)-(4,0)` → 2nd piece `left_he=HE[(1 5)->(1 -5) R2L]` |
| INVALID | INVALID | The left endpoint lies in the **interior of `face`** — or on an open/boundary side of the parameter space, in which case the visitor must realise the curve end itself (`Arr_accessor::place_and_set_curve_end`). Nothing in the arguments distinguishes those two: check `parameter_space_in_{x,y}(cv, ARR_MIN_END)` yourself, exactly as `Arr_inc_insertion_zone_visitor::found_subcurve` does (`Arr_inc_insertion_zone_visitor.h:189-217`). | square, query `(2,2)-(8,8)`; **and** full line `y=0` whose left end is at `x=-inf` |

`right_v` / `right_he` mirror this, and are set at lines 1046-1076:
* `right_v = m_intersect_he->source()` or `->target()` when `m_intersect_p` equals that
  end-vertex's point (and the vertex is not at an open boundary) — and then **`right_he` is forced
  invalid**;
* otherwise `right_v = INVALID` and
  `m_right_he = _direct_intersecting_edge_to_left(m_sub_cv1, m_intersect_he);` — documented as
  *"Obtain the halfedge with the correct direction (which should be the predecessor of m_sub_cv1 if
  we split the edge around this vertex)"*;
* if the next feature is an isolated vertex, `right_v = m_intersect_v`, `right_he = INVALID`.
* On the terminal call (A) both are invalid.

So `left_v`/`right_v` valid ⇒ "a vertex already exists here"; `left_he`/`right_he` valid ⇒ "this
edge must be split here"; both invalid ⇒ "nothing here; create it yourself if you need it". That
four-way test is precisely what `Arr_do_intersect_zone_visitor::found_subcurve` uses to decide
"no intersection" (`Arr_do_intersect_zone_visitor.h:88-95`).

**The return value.**

* `.second == true` → line 1086 `return true`, the `while` loop in `compute_zone` ends, the
  intersection map is cleared, `compute_zone()` returns. **Any remaining portion of the query curve
  is silently dropped and no further visitor call is made** — including the terminal call (A).
  **[verified: halting on the first of four pieces produced exactly one call]**
* `.first == Halfedge_handle()` (invalid) → "I did not modify the arrangement". The zone then
  advances with (lines 1187-1207, verbatim):
  ```cpp
    m_left_v = m_right_v;
    if (m_right_he != m_invalid_he) {
      if ((m_ip_multiplicity % 2) == 1) m_left_he = m_right_he->twin();
      else if (m_ip_multiplicity != 0) m_left_he = m_right_he;
      else m_left_he = m_invalid_he;
    }
    else m_left_he = m_invalid_he;
  ```
* `.first` valid → "I inserted an edge for `cv`". The zone then:
  1. if `m_right_v == m_invalid_v`, **re-keys `m_inter_map`**: it finds the two halves of the edge
     it told you to split as `inserted_he->next()` and `inserted_he->twin()->prev()` (choosing
     which is left/right by `direction()`), moves the cached intersection list to the *right* half,
     gives the *left* half an empty list, and erases the original key (lines 1103-1131);
  2. if this was an overlap continuation, re-points `m_intersect_he` at `inserted_he->next()` or
     `inserted_he->twin()->prev()` (lines 1132-1147);
  3. asserts `equal(m_left_pt, inserted_he->target()->point())` (line 1153) — **the returned
     halfedge must be directed left→right along `cv`**;
  4. sets `m_left_v = inserted_he->target();` and picks `m_left_he` (lines 1157-1185, verbatim):
     ```cpp
        if (m_right_he != m_invalid_he) {
          if ((m_ip_multiplicity % 2) == 1) m_left_he = inserted_he->next()->twin();
          else if (m_ip_multiplicity != 0)
            m_left_he = (m_right_he->direction() == ARR_LEFT_TO_RIGHT) ? inserted_he : m_right_he;
          else m_left_he = m_invalid_he;                       // Multiplicity is unknown
        }
        else m_left_he = (m_found_iso_vert) ? inserted_he : m_invalid_he;
     ```
  Because of step 1 you must **not** return a valid handle unless the DCEL really has that shape,
  i.e. unless you actually inserted `cv` and split `right_he` at `max_vertex(cv)`.

**Multiplicity.** `m_ip_multiplicity` is the `Multiplicity` half of the `Intersection_point` the
traits reported. Odd ⇒ the curve crosses to the other side (`->twin()`); even and non-zero ⇒
tangency, stay on the same side; **0 ⇒ "unknown"**, `m_left_he` is set invalid and `compute_zone`
recovers on the next iteration via
`m_left_he = _direct_intersecting_edge_to_right(m_cv, m_left_pt, m_right_he);` (lines 204-206), guarded
by `CGAL_assertion(m_right_he != m_invalid_he)`. Two of the seven project traits do emit 0:
`Arr_circle_segment_traits_2` (`Arr_geometry_traits/Circle_segment_2.h:1011`,
*"in this case we do not define the multiplicity of the intersection points we report"* — two arcs
of the same supporting circle/line sharing an endpoint) and `Arr_Bezier_curve_traits_2`
(`Arr_geometry_traits/Bezier_x_monotone_2.h:325, 357, 2288, 2324, 2414`).

---

#### 3.5.6 `found_overlap` — the exact contract

```cpp
  Result found_overlap(const X_monotone_curve_2& cv,
                       Halfedge_handle he,          // directed from left to right
                       Vertex_handle left_v, Vertex_handle right_v);
```
Single call site, line 1288-1289:
```cpp
  Visitor_result visitor_res =
    m_visitor->found_overlap(m_overlap_cv, m_intersect_he, m_left_v, m_right_v);
```

* **`cv` is the geometric overlap** (`m_overlap_cv`), i.e. the `X_monotone_curve_2` alternative that
  `Intersect_2` returned for the pair `(he->curve(), m_cv)`. It is a sub-curve of **both** the query
  curve and the existing edge's curve.
* **`he` IS guaranteed directed left→right.** `_zone_in_overlap` normalises it before calling you
  (lines 1240-1246, verbatim):
  ```cpp
    if (m_intersect_he->direction() == ARR_LEFT_TO_RIGHT)
      he_right_v = m_intersect_he->target();
    else {
      he_right_v = m_intersect_he->source();
      m_intersect_he = m_intersect_he->twin();
    }
  ```
  **[verified]**: with the edge stored as `HE[(10 0)->(0 0) R2L]`, the visitor received
  `he = HE[(0 0)->(10 0) L2R]`.
* **The overlap CAN be shorter than the edge — at either or both ends.** **[verified]** edge
  `(0,0)-(10,0)`, query `(2,0)-(7,0)` → `found_overlap(cv = 2 0 7 0, he = HE[(0 0)->(10 0) L2R],
  left_v = INVALID, right_v = INVALID)` — a single call, no `found_subcurve`, and the visitor must
  split the edge **twice** (which is exactly what `Arr_inc_insertion_zone_visitor::found_overlap`
  does, `Arr_inc_insertion_zone_visitor.h:358-376`).
* It can **never be longer**: the code comments (line 1248) *"Note that m_overlap_cv cannot extend to
  the right longer than the halfedge it overlaps"* and asserts the parameter spaces agree when
  unbounded.
* `left_v` — valid iff the overlap's left endpoint coincides with an existing vertex.
  `INVALID` ⇒ the overlap starts in `he`'s interior ⇒ split `he->curve()` at `min_vertex(cv)`.
  **Gotcha:** it is also `INVALID` when the overlap's left end is at an **open boundary**, even
  though a vertex at infinity does exist — see §3.5.9.
* `right_v` — set at lines 1247-1281: `he_right_v` if the overlap reaches the right end-vertex of
  `he` (including the "both unbounded" case), otherwise `INVALID` meaning "`he` extends further
  right than the overlap".
* The four combinations exactly match the four branches of
  `Arr_inc_insertion_zone_visitor::found_overlap`: `left_v` invalid + `right_v` invalid + bounded →
  `split_edge` twice; `left_v` invalid otherwise → one `split_edge`; `right_v` invalid → one
  `split_edge`; both valid → `modify_edge(he, cv)` (i.e. the *entire* edge is overlapped and its
  curve is simply replaced).
* Return value: `.second == true` halts **before anything else happens** (line 1296-1300) — before
  `m_inter_map.erase`, before the `Split_2`, before the state advance. `.first` valid ⇒
  `CGAL_assertion(equal(m_left_pt, updated_he->target()->point()))` (line 1326) then
  `m_left_v = updated_he->target();`; `.first` invalid ⇒ `m_left_v = m_right_v;`. **In both cases
  `m_left_he = m_invalid_he;`** (line 1332), so after every overlap the main loop re-derives the
  predecessor with `_find_prev_around_vertex`.
* After a non-halting overlap the zone erases the edge's cache entry
  (`m_inter_map.erase(p_orig_curve);`, line 1303) *"so we will have to recompute intersections with
  it in the future"* — because the visitor may have replaced the edge's curve.

---

#### 3.5.7 Traced call sequences **[verified]**

Instrumented visitor, non-modifying (always returns `(Halfedge_handle(), false)`), unless the case
says "modifying" (delegates to `CGAL::Arr_inc_insertion_zone_visitor`).
`Arr_segment_traits_2<Epeck>` / `Arrangement_2` unless stated.

**(a) Curve entirely inside one face.** Arrangement = unit square `(0,0)-(10,0)-(10,10)-(0,10)`;
query `(2,2)-(8,8)`.
```
[0] found_subcurve(cv = 2 2 8 8)
      face = F1(bounded)
      left_v = INVALID   left_he = INVALID
      right_v = INVALID  right_he = INVALID
```
Exactly one call — the terminal call (A). Note all four handles invalid: this is the signal
`Arr_do_intersect_zone_visitor` uses to answer "no intersection".

**(b) Curve crossing three edges.** Arrangement = three vertical segments `x=1,2,3`, `y∈[-5,5]`;
query `(0,0)-(4,0)`.
```
[0] found_subcurve(cv = 0 0 1 0)   face=F0(unbounded)
      left_v=INVALID  left_he=INVALID
      right_v=INVALID right_he=HE[(1 -5)->(1 5) L2R]
[1] found_subcurve(cv = 1 0 2 0)   face=F0(unbounded)
      left_v=INVALID  left_he=HE[(1 5)->(1 -5) R2L]        <- twin of the previous right_he
      right_v=INVALID right_he=HE[(2 -5)->(2 5) L2R]
[2] found_subcurve(cv = 2 0 3 0)   face=F0(unbounded)
      left_v=INVALID  left_he=HE[(2 5)->(2 -5) R2L]
      right_v=INVALID right_he=HE[(3 -5)->(3 5) L2R]
[3] found_subcurve(cv = 3 0 4 0)   face=F0(unbounded)
      left_v=INVALID  left_he=HE[(3 5)->(3 -5) R2L]
      right_v=INVALID right_he=INVALID                     <- terminal call
```
4 pieces for 3 crossings; `left_he[k] == right_he[k-1]->twin()` because the segment multiplicity is
1 (odd). The arrangement was **not** modified (`E=3` before and after).

**(b′) Same, with the modifying `Arr_inc_insertion_zone_visitor`.** Now `left_v` becomes valid
because the previous call created the vertex:
```
[0] left_v=INVALID left_he=INVALID / right_he=HE[(1 -5)->(1 5) L2R]  -> (HE[(0 0)->(1 0) L2R], 0)
[1] left_v=V(1 0)  left_he=HE[(1 5)->(1 0) R2L] / right_he=HE[(2 -5)->(2 5) L2R]
                                                                      -> (HE[(1 0)->(2 0) L2R], 0)
[2] left_v=V(2 0)  left_he=HE[(2 5)->(2 0) R2L] / right_he=HE[(3 -5)->(3 5) L2R]
                                                                      -> (HE[(2 0)->(3 0) L2R], 0)
[3] left_v=V(3 0)  left_he=HE[(3 5)->(3 0) R2L] / right_he=INVALID     -> (HE[(3 0)->(4 0) L2R], 0)
{V=6→11, E=3→10, arr.is_valid() == true}
```
Every returned halfedge is directed left→right with `target()` at the piece's right endpoint.

**(c) Curve exactly overlapping an existing edge.** Edge `(0,0)-(10,0)`; query identical.
```
[0] found_OVERLAP(cv = 0 0 10 0)
      he = HE[(0 0)->(10 0) L2R]     (the located halfedge was the R2L twin; normalised)
      left_v = V(0 0)   right_v = V(10 0)
```
One call, no `found_subcurve` at all. Both `left_v`/`right_v` valid ⇒ the insertion visitor would
call `modify_edge`.

**(c′) Overlap strictly inside the edge.** Edge `(0,0)-(10,0)`; query `(2,0)-(7,0)`.
```
[0] found_OVERLAP(cv = 2 0 7 0)  he = HE[(0 0)->(10 0) L2R]
      left_v = INVALID   right_v = INVALID
```

**(d) Partial overlap, then continue.** Edge `(0,0)-(10,0)`; query `(2,0)-(20,0)`.
```
[0] found_OVERLAP(cv = 2 0 10 0)  he = HE[(0 0)->(10 0) L2R]
      left_v = INVALID   right_v = V(10 0)
[1] found_subcurve(cv = 10 0 20 0)   face=F0(unbounded)
      left_v = V(10 0)  left_he = HE[(0 0)->(10 0) L2R]     <- predecessor around V(10 0)
      right_v = INVALID right_he = INVALID
```
The overlap is reported first (left-to-right ordering is preserved across the two kinds of call).
With the modifying visitor: `[0] -> (HE[(2 0)->(10 0) L2R], 0)` then
`[1] left_he = HE[(2 0)->(10 0) L2R] -> (HE[(10 0)->(20 0) L2R], 0)`, `E=1→3`, valid.

**(e) Left end coincides with an existing vertex.** Edge `(0,0)-(10,0)`; query `(0,0)-(5,5)`.
```
[0] found_subcurve(cv = 0 0 5 5)   face=F0(unbounded)
      left_v = V(0 0)   left_he = HE[(10 0)->(0 0) R2L]     <- target() == left_v
      right_v = INVALID right_he = INVALID
```
`m_obj` was a `Vertex_const_handle`; `m_left_he` was then produced by `_find_prev_around_vertex`.

**(e′) Right end coincides with an existing vertex** (query `(-5,5)-(0,0)`):
```
[0] found_subcurve(cv = -5 5 0 0)  face=F0(unbounded)
      left_v=INVALID left_he=INVALID
      right_v=V(0 0) right_he=INVALID          <- right_he forced invalid when right_v is valid
```

**(e″) Isolated vertex on the curve.** Edge `(0,0)-(10,0)` plus isolated vertex `(5,5)`;
query `(0,5)-(10,5)`:
```
[0] found_subcurve(cv = 0 5 5 5)  right_v = V(5 5)  right_he = INVALID
[1] found_subcurve(cv = 5 5 10 5) left_v  = V(5 5)  left_he  = INVALID   (isolated ⇒ no predecessor)
```

**(f) Tangency at one point.** `Arr_circle_segment_traits_2<Epeck>`: circle centred `(0,0)`
radius 5 (2 arcs, vertices at `(±5,0)`); query = the horizontal segment `y = -5, x ∈ [-8,8]`,
tangent at `(0,-5)`. `Intersect_2` reports `point (0,-5) multiplicity = 2` **[verified]**.
```
[0] found_subcurve  face=F0(unb)
      left_v=INVALID  left_he=INVALID
      right_v=INVALID right_he=HE[(5,0)->(-5,0) R2L]        <- the lower arc
[1] found_subcurve  face=F0(unb)                            <- SAME face
      left_v=INVALID  left_he=HE[(5,0)->(-5,0) R2L]         <- SAME halfedge, not the twin
      right_v=INVALID right_he=INVALID
```
So a tangency still **splits the reported curve into two pieces** even though nothing is crossed,
and the even multiplicity keeps `m_left_he = m_right_he` (no `->twin()`). A `zone()` caller sees
the same face twice in the output; a custom visitor that counts crossings must look at the
multiplicity itself, not at the number of calls.

**(g) Unbounded curve, left end at infinity.** `Arr_linear_traits_2<Epeck>` (⇒
`Arr_unb_planar_topology_traits_2`, all four sides `Arr_open_side_tag`). Arrangement = the two full
vertical lines `x=1`, `x=3`; query = the full line `y=0`.

`init()` takes the boundary branch: `ps_x(cv, ARR_MIN_END) == ARR_LEFT_BOUNDARY`,
`is_closed_2(cv, ARR_MIN_END) == false` ⇒ `m_has_left_pt = false`, `m_left_on_boundary = true`, and
`m_obj = m_arr_access.locate_curve_end(cv, ARR_MIN_END, ARR_LEFT_BOUNDARY, ARR_INTERIOR)`.
**[verified]** that call returned a `Face_const_handle` (`obj.index() == 2`) — the unbounded face
`F1` — because no arrangement feature yet exists for that curve end. (For a curve end that *does*
already exist, e.g. the min end of the already-present line `x=1`, the same call returns a
`Halfedge_const_handle` — `obj.index() == 1`, `HE[@inf->@inf R2L cv = L -1 0 1]`.)
```
[0] found_subcurve(cv = R 1 0 0 0)          <- a RAY: left-unbounded piece
      face = F1(unbounded)
      left_v = INVALID   left_he = INVALID      <- NOT a vertex-at-infinity, nothing at all
      right_v = INVALID  right_he = HE[@inf->@inf L2R cv = L -1 0 1]
[1] found_subcurve(cv = S 1 0 3 0)   face = F2(unbounded)
      left_he = HE[@inf->@inf R2L cv = L -1 0 1]
      right_he = HE[@inf->@inf L2R cv = L -1 0 3]
[2] found_subcurve(cv = R 3 0 4 0)   face = F0(unbounded)   <- right-unbounded ray, terminal call
      left_he = HE[@inf->@inf R2L cv = L -1 0 3]
      right_v = INVALID  right_he = INVALID
```
Key points for a custom visitor:
* the first piece is a **trimmed unbounded piece** (`Split_2` on a line yields a ray);
* the visitor sees **all four handles invalid** for an unbounded left end — it must detect the
  situation itself from `parameter_space_in_{x,y}(cv, ARR_MIN_END)` and call
  `Arr_accessor::place_and_set_curve_end(face, cv, ARR_MIN_END, ps_x, ps_y)`, exactly as
  `Arr_inc_insertion_zone_visitor.h:189-217` / `237-258` do;
* **fictitious halfedges are never passed to the visitor** — `_leftmost_intersection` skips them
  (line 811), and the `right_he` values above are real halfedges of the vertical lines;
* the same trace results from `init_with_hint(q, acc.locate_curve_end(q, ARR_MIN_END, ARR_LEFT_BOUNDARY, ARR_INTERIOR))`
  **[verified]**;
* with the modifying visitor the run is also fine (`E=2→7`, `arr.is_valid() == true`), and from
  piece 1 on `left_v` is a real finite vertex.

---

#### 3.5.8 Reuse of the zone object, and `m_inter_map` lifetime

* **`compute_zone()` clears `m_inter_map` on every exit path**, line 244, including when the visitor
  halted. Nothing whatsoever survives into the next `compute_zone()`. **[verified]**: three rounds
  of `init()`+`compute_zone()` on the same object, with a repeated curve as the 3rd round, produced
  byte-identical traces for rounds 1 and 3.
* **Reuse is the officially intended pattern.** `CGAL::insert(arr, c, pl)` builds one
  `Arrangement_zone_2` and drives it once per x-monotone piece, **modifying the arrangement in
  between** (`Arrangement_2/Arrangement_on_surface_2_global.h:91, 105, 112`):
  ```cpp
    Arrangement_zone_2<Arr, Zone_visitor> arr_zone(arr, &visitor);
    ...
    for (const auto& x_obj : x_objects) {
      ...
        arr_zone.init(*x_curve, pl);
        arr_access.notify_before_global_change();
        arr_zone.compute_zone();
        arr_access.notify_after_global_change();
  ```
  Note that `visitor.init()` is **not** re-called there — see §3.5.0 gotcha 4.
* **Why "the visitor modifies the arrangement while the zone runs" is safe.** The map is keyed by
  `const X_monotone_curve_2*` = `&he->curve()`, and `Arr_halfedge_base::set_curve` sets `p_cv` on
  **both twins** (`Arr_dcel_base.h:203-213`), so a key identifies an **edge**, not a halfedge. The
  algorithm repairs the map for exactly the two mutations the insertion visitor is *told* to
  perform:
  * **edge split at the reported intersection** — after a `found_subcurve` that returns a valid
    handle with `m_right_v == m_invalid_v`, lines 1103-1131 locate the two halves as
    `inserted_he->next()` and `inserted_he->twin()->prev()`, hand the pending intersection list to
    the **right** half, give the **left** half an empty list, and `erase` the original key;
  * **overlap** — line 1303 `m_inter_map.erase(p_orig_curve);` unconditionally forgets the edge, so
    intersections with whatever curve the visitor put there are recomputed; and lines 1132-1147
    re-point `m_intersect_he` at the surviving right half.
  In addition, `_leftmost_intersection` recomputes from scratch for any curve pointer it has never
  seen. So a visitor that **only** does what it was asked (split `left_he`/`right_he` at the
  reported points, insert `cv`, `modify_edge` a fully-overlapped edge) keeps the map consistent.
* **What is NOT safe.** A visitor that *removes* edges, *merges* edges, or rewrites the curve of an
  edge it was not told about leaves a dangling `const X_monotone_curve_2*` in `m_inter_map`; the
  next `_compute_next_intersection` will `find()` it and hand back stale intersections (or the
  address may be reused by a different curve). Do not delete arrangement features from a zone
  visitor.
* **Do not call `compute_zone()` twice without `init()`.** **[verified]**: the second call reported
  the last piece of the previous curve again (`found_subcurve(cv = 3 0 4 0)`), because `m_cv` holds
  the leftover remainder and `m_obj` still holds the *original* left-end location.
* Lifetime: the zone stores `Arrangement_2& m_arr`, `Visitor* m_visitor`, and a
  `const Traits_adaptor_2*` obtained from `arr.geometry_traits()` **in the constructor**. All three
  must outlive the zone object; the zone owns none of them. `m_cv` is a *copy* of your curve.

---

#### 3.5.9 KNOWN CGAL 6.1 BUG: unbounded overlap aborts **[verified]**

Reachable directly from the public API:
```cpp
CGAL::Arrangement_2<CGAL::Arr_linear_traits_2<Epeck>> arr;
CGAL::insert(arr, X_monotone_curve_2(Line_2(Point(1,0), Point(1,1))));
CGAL::insert(arr, X_monotone_curve_2(Line_2(Point(1,0), Point(1,1))));   // the SAME line again
```
```
CGAL error: precondition violation!
Expression : cv.has_left()
File       : /opt/homebrew/include/CGAL/Arr_linear_traits_2.h
Line       : 689
```
**Mechanism.** `init()` → `locate_curve_end` returns the existing edge's `Halfedge_const_handle`;
`compute_zone` takes the `m_has_left_pt == false` halfedge branch (lines 146-158) and goes straight
to `_zone_in_overlap()`. There `m_left_v` was never set, so the visitor is called as
`found_overlap(cv = <the whole line>, he, left_v = INVALID, right_v = V@inf)`. Then
`Arr_inc_insertion_zone_visitor::found_overlap` (`Arr_inc_insertion_zone_visitor.h:351-356`)
unconditionally does
```cpp
  if (left_v == invalid_v) {
    m_geom_traits->split_2_object()
      (he->curve(), m_geom_traits->construct_min_vertex_2_object()(cv), m_sub_cv1, m_sub_cv2);
```
and `construct_min_vertex_2` on a left-unbounded curve trips its own precondition.

Consequences for a type-erased core:
* bounded overlaps are fine (cases c/c′/d above, including with the insertion visitor);
* **an unbounded curve whose left end coincides with an existing unbounded edge end must be
  pre-filtered** (de-duplicate before `insert`), or the call must be wrapped in
  `try { … } catch (const CGAL::Precondition_exception&) { … }` — note CGAL's default failure
  behaviour on this install *throws* rather than aborting, so a catch does work, but the
  arrangement may be left mid-modification and should be considered unusable;
* a *custom* `found_overlap` can handle it: test
  `parameter_space_in_x(cv, ARR_MIN_END) != ARR_INTERIOR || parameter_space_in_y(cv, ARR_MIN_END) != ARR_INTERIOR`
  before calling `construct_min_vertex_2`.

---

#### 3.5.10 Traits requirements imposed by the zone

Every geometry-traits functor the zone actually invokes, with the call site and where the
implementation comes from. `Traits_adaptor_2 = Arr_traits_adaptor_2<Geometry_traits_2>` — several
of these are **synthesised** by the adaptor from the basic traits, so they are *not* extra
requirements on your traits class.

Always required (any side categories):

| functor | called at | supplied by |
|---|---|---|
| `Parameter_space_in_x_2`, `Parameter_space_in_y_2` | `.h:203-207,230-231`; impl 41-64, 151-155, 732-780 | basic traits; `Arr_traits_basic_adaptor_2` defaults them to `ARR_INTERIOR` for oblivious sides |
| `Is_closed_2` | `.h:221,229`; impl 40, 56, 586, 1225 | **adaptor** (`Arr_traits_adaptor_2.h:1741, 1790`) |
| `Construct_min_vertex_2` / `Construct_max_vertex_2` | `.h:214,224,235`; impl 48, 64, 452, 481, 585, 808, 1227 | basic traits |
| `Compare_xy_2` | `.h:363,387`; impl 508, 540, 567, 584, 751, 790, 806, 922 | basic traits |
| `Compare_y_at_x_2` | impl 401 + 451 (`CGAL_exactness_assertion`), 945 (isolated vertices) | basic traits |
| `Compare_y_at_x_right_2` | `.h:273`; impl 256, 411, 479 | basic traits |
| `Compare_y_position_2` | impl 463 (`_direct_intersecting_edge_to_left`) | **adaptor** (`:2149, 2336`) |
| `Is_vertical_2` | impl 264-265 (`do_overlap_impl`) | basic traits |
| `Equal_2` | impl 516, 552, 994, 1223 (functor fetch), then 1023, 1046, 1051, 1153, 1268, 1296, 1326 | basic traits |
| `Is_in_x_range_2` | impl 807, 851, 923, 929 | **adaptor** (`:1797, 2146`) |
| `Is_between_cw_2` | impl 342-349 (`_find_prev_around_vertex`) | **adaptor** (`:2339, 3221`) |
| `Intersect_2` | impl 650 | X-monotone traits |
| `Split_2` | impl 1033, 1309 | X-monotone traits |
| `Multiplicity` (type) | `.h:96, 160`; impl 1159, 1189 (`% 2`) | X-monotone traits — must be an **integral** type (`%` is applied) |

Additionally required **only when the four side categories are not all `Arr_oblivious_side_tag`**
(dispatched on `Arr_all_sides_oblivious_category` / `Arr_two_sides_category`):

| functor | called at | needed for |
|---|---|---|
| `Compare_y_near_boundary_2` | impl 281 (`do_overlap_impl`) | left-boundary common min end |
| `Compare_x_on_boundary_2`, `Compare_x_near_boundary_2` | impl 294-295 | vertical curves at top/bottom |
| `Compare_x_point_curve_end_2` | impl 742, 781 (`_is_to_left/right_impl`) | boundary curve ends |
| `Is_on_y_identification_2` | impl 517-519 | only for `Arr_has_identified_side_tag` (identified left/right sides) |

Class-scope static assertion (`Arrangement_zone_2.h:70-73`):
`Arr_sane_identified_tagging<Left_side_category, Bottom_side_category, Top_side_category, Right_side_category>::value`.

**Not used at all** (checked by grepping the whole class + impl): `Trim_2`, `Merge_2`,
`Are_mergeable_2`, `Make_x_monotone_2`, `Construct_x_monotone_curve_2`, `Approximate_2`,
`Compare_endpoints_xy_2`, `Construct_opposite_2`. In particular **`Trim_2` is NOT a zone
requirement** — the zone trims with `Split_2` only.

`Intersect_2` call shape (impl 650, verbatim) — note the argument order:
```cpp
  m_geom_traits->intersect_2_object()(he->curve(), m_cv,
                                      std::back_inserter(inter_list));
```
with `inter_list` of type
`std::list<std::variant<std::pair<Point_2, Multiplicity>, X_monotone_curve_2>>`.
The preceding comment states: *"Note that the first curve we intersect is always the subcurve
associated with the given halfedge and the second curve is the one we insert. Even though the order
seems unimportant, we exploit this fact in some of the traits classes in order to optimize
computations."* A custom traits must therefore not assume the reverse order.

**Which of the seven project traits satisfy all of this: all seven.** **[verified]** by compiling
and running `Arrangement_zone_2<Arr, Visitor>::init()` + `compute_zone()` for each:

| traits | side categories | zone instantiates + runs |
|---|---|---|
| `Arr_segment_traits_2<Epeck>` | all `Arr_oblivious_side_tag` (`:68-71`) | yes |
| `Arr_polyline_traits_2<Arr_segment_traits_2<Epeck>>` | inherited oblivious (`:73-76`) | yes |
| `Arr_linear_traits_2<Epeck>` | all `Arr_open_side_tag` (`:61-64`) | yes |
| `Arr_circle_segment_traits_2<Epeck>` | all oblivious (`:61-64`) | yes |
| `Arr_conic_traits_2<Rat_kernel, Alg_kernel, CORE_algebraic_number_traits>` | all oblivious (`:83-86`) | yes |
| `Arr_Bezier_curve_traits_2<Rat_kernel, Alg_kernel, CORE_algebraic_number_traits>` | all oblivious (`:77-80`) | yes |
| `Arr_geodesic_arc_on_sphere_traits_2<Epeck>` + `Arr_spherical_topology_traits_2` | `Arr_identified_side_tag` L/R, `Arr_contracted_side_tag` B/T (`:140-143`) | yes — including a **crossing** query on a non-empty arrangement (2 `found_subcurve` calls), which exercises `is_intersection_valid_impl(…, Arr_has_identified_side_tag)` and `do_overlap_impl(…, Arr_not_all_sides_oblivious_tag)` |

Caveats found while doing this: `Arr_circle_segment_traits_2` and `Arr_Bezier_curve_traits_2` can
report multiplicity `0` (see §3.5.5); `Arr_linear_traits_2` hits the unbounded-overlap bug of
§3.5.9.

---

#### 3.5.11 Checklist for a hand-written `ZoneVisitor` in a type-erased core

```cpp
struct My_zone_visitor {
  using Arrangement_2      = Arr;                       // required nested types
  using Vertex_handle      = Arr::Vertex_handle;        // (all three stock visitors declare these)
  using Halfedge_handle    = Arr::Halfedge_handle;
  using Face_handle        = Arr::Face_handle;
  using Point_2            = Arr::Point_2;
  using X_monotone_curve_2 = Arr::X_monotone_curve_2;
  using Result             = std::pair<Halfedge_handle, bool>;

  void   init(Arrangement_2* arr);                      // POINTER; called once, by the zone ctor
  Result found_subcurve(const X_monotone_curve_2& cv, Face_handle face,
                        Vertex_handle left_v,  Halfedge_handle left_he,
                        Vertex_handle right_v, Halfedge_handle right_he);
  Result found_overlap(const X_monotone_curve_2& cv, Halfedge_handle he,
                       Vertex_handle left_v, Vertex_handle right_v);
};
```
None of the three parameters need be `const`; nothing is virtual; the concept is duck-typed (the
zone stores `Visitor*` and calls through it). Rules:

1. **Read-only visitor** (a `zone` with a user callback, a `do_intersect`, a "what did I touch"
   query): always `return Result(Halfedge_handle(), false);`. Never return a handle you did not
   create — the zone would take the "an edge was inserted" path and re-key `m_inter_map` from a
   DCEL shape that does not exist.
2. **Halting**: `return Result(Halfedge_handle(), true);` stops the walk immediately; the remaining
   portion of the query curve is dropped without notice. The terminal `found_subcurve` (all-invalid
   right handles) cannot halt anything — its result is discarded.
3. **Reset per query yourself.** `init()` is called only by the `Arrangement_zone_2` constructor.
   Either construct a fresh zone per curve (what `zone()`/`do_intersect()` do), or reset your
   visitor manually before each `init()`+`compute_zone()` round.
4. **Do not delete or merge arrangement features** from inside the visitor (§3.5.8).
5. **Handles you receive are valid only for the duration of the call** if your visitor (or the zone,
   via a later split) modifies the arrangement. `Face_handle` in particular may be split by your own
   insertion; re-query rather than caching across calls.
6. **Buffer, don't emit, if you need Python-side callbacks.** Calling back into the interpreter from
   inside `compute_zone()` re-enters CGAL state that is mid-walk; if the callback raises, the zone
   unwinds with `m_inter_map` unswept (it is only cleared at line 244, on normal return). Collect
   into a vector and hand it over after `compute_zone()` returns.
7. The output value type for a zone-style visitor must use **non-const** handles — see gotcha 3 at
   the top of this file.

---

## 4. `overlay` — `<CGAL/Arr_overlay_2.h>`

### 4.1 With overlay traits

```cpp
template <typename GeometryTraitsA_2,
          typename GeometryTraitsB_2,
          typename GeometryTraitsRes_2,
          typename TopologyTraitsA,
          typename TopologyTraitsB,
          typename TopologyTraitsRes,
          typename OverlayTraits>
void
overlay(const Arrangement_on_surface_2<GeometryTraitsA_2, TopologyTraitsA>& arr1,
        const Arrangement_on_surface_2<GeometryTraitsB_2, TopologyTraitsB>& arr2,
        Arrangement_on_surface_2<GeometryTraitsRes_2, TopologyTraitsRes>& arr,
        OverlayTraits& ovl_tr);
```

From the header doc:

> `\tparam OverlayTraits` An overlay-traits class. As arr1, arr2 and res can be templated with
> different geometry-traits class and different DCELs (encapsulated in the various topology-traits
> classes). The geometry-traits of the result arrangement is used to construct the result
> arrangement. This means that all the types (e.g., `Point_2`, `Curve_2` and `X_monotone_2`) of
> both arr1 and arr2 have to be convertible to the types in the result geometry-traits. The
> overlay-traits class defines the various overlay operations of pairs of DCEL features from
> TopologyTraitsA and TopologyTraitsB to the resulting ResDcel.

Compile-time requirements actually enforced:
```cpp
  static_assert(std::is_convertible<Agt2::Point_2,              Rgt2::Point_2>::value);
  static_assert(std::is_convertible<Bgt2::Point_2,              Rgt2::Point_2>::value);
  static_assert(std::is_convertible<Agt2::X_monotone_curve_2,   Rgt2::X_monotone_curve_2>::value);
  static_assert(std::is_convertible<Bgt2::X_monotone_curve_2,   Rgt2::X_monotone_curve_2>::value);
```

Runtime precondition:
```cpp
  // The result arrangement cannot be on of the input arrangements.
  CGAL_precondition(((void*)(&arr) != (void*)(&arr1)) && ((void*)(&arr) != (void*)(&arr2)));
```

Semantics / side effects:
* `arr.clear()` is called before the sweep — the result arrangement is **completely replaced**
  (and its observers see `before_clear` / `after_clear`).
* Input edges are fed as `Arr_overlay_traits_2::X_monotone_curve_2` objects, each carrying the
  originating **red** (= `arr1`) or **blue** (= `arr2`) halfedge, always **normalised to
  `ARR_RIGHT_TO_LEFT`**.
* Isolated vertices of both inputs are fed as extended points; if there are none the point range
  is skipped entirely.
* Sweep selection: `Ss2::Surface_sweep_2<Arr_overlay_ss_visitor<...>>::sweep(...)` if
  `std::is_same<Agt2::Bottom_side_category, Arr_contracted_side_tag>::value`, otherwise
  `indexed_sweep(...)` with `Indexed_sweep_accessor<Arr_a, Arr_b, Ovl_x_monotone_curve_2>`
  (see gotcha 7 — this mutates and restores `Vertex::inc()` of both inputs).
* **red = `arr1` = A**, **blue = `arr2` = B**. This mapping is fixed by
  `Arr_overlay_traits_2<Gt_adaptor_2, Arr_a, Arr_b>` (`Ar2 = Arrangement_red_2 = Arr_a`).

### 4.2 Without overlay traits ("simple" overlay)

```cpp
template <typename GeometryTraitsA_2,
          typename GeometryTraitsB_2,
          typename GeometryTraitsRes_2,
          typename TopologyTraitsA,
          typename TopologyTraitsB,
          typename TopologyTraitsRes>
void
overlay(const Arrangement_on_surface_2<GeometryTraitsA_2, TopologyTraitsA>& arr1,
        const Arrangement_on_surface_2<GeometryTraitsB_2, TopologyTraitsB>& arr2,
        Arrangement_on_surface_2<GeometryTraitsRes_2, TopologyTraitsRes>& arr);
```
Body: instantiates `_Arr_default_overlay_traits_base<Arr_a, Arr_b, Arr_res> ovl_traits;` (all ten
`create_*` are no-ops) and forwards. Use it when the result DCEL carries no data.

### 4.3 The `OverlayTraits` concept — 10 `create_*` functions

Reference implementation, `<CGAL/Surface_sweep_2/Arr_default_overlay_traits_base.h>`:

```cpp
template <class ArrangementA, class ArrangementB, class ArrangementR>
class _Arr_default_overlay_traits_base
{
public:
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
};
```

Key facts for a binding layer:

* **Argument handedness**: the first argument always refers to arrangement **A = arr1 = "red"**,
  the second to **B = arr2 = "blue"**, the third to the **result R**, which is the only
  **non-const** handle. A/B handles are `*_const_handle`; R handles are mutable handles.
* Doc comments, verbatim, one per function:
  1. `create_vertex(v1, v2, v)` — *"Create a vertex v that corresponds to the coinciding vertices v1 and v2."*
  2. `create_vertex(v1, e2, v)` — *"Create a vertex v that matches v1, which lies of the edge e2."*
  3. `create_vertex(v1, f2, v)` — *"Create a vertex v that matches v1, contained in the face f2."*
  4. `create_vertex(e1, v2, v)` — *"Create a vertex v that matches v2, which lies of the edge e1."*
  5. `create_vertex(f1, v2, v)` — *"Create a vertex v that matches v2, contained in the face f1."*
  6. `create_vertex(e1, e2, v)` — *"Create a vertex v that matches the intersection of the edges e1 and e2."*
  7. `create_edge(e1, e2, e)` — *"Create an edge e that matches the overlap between e1 and e2."*
  8. `create_edge(e1, f2, e)` — *"Create an edge e that matches the edge e1, contained in the face f2."*
  9. `create_edge(f1, e2, e)` — *"Create an edge e that matches the edge e2, contained in the face f1."*
  10. `create_face(f1, f2, f)` — *"Create a face f that matches the overlapping region between f1 and f2."*
* The combinations `(Halfedge_A, Face_B)`, `(Face_A, Halfedge_B)` and `(Face_A, Face_B)` are
  **never** dispatched to `create_vertex` — `Arr_overlay_ss_visitor::Create_vertex_visitor` has
  those overloads but they call `CGAL_error()`.
* `create_edge`: the halfedge handed to you is **normalised** —
  `if (new_he->direction() != ARR_RIGHT_TO_LEFT) new_he = new_he->twin();` — so `e` is always
  directed right-to-left, matching `e1`/`e2`.
* `create_face` is additionally called **once at the very end of the sweep** for the remaining
  top/unbounded face:
  `m_overlay_traits->create_face(m_overlay_helper.red_top_face(),
  m_overlay_helper.blue_top_face(), this->m_helper.top_face());`
* Overlay traits are held as `OverlayTraits*` (non-const) inside the visitor, so
  **`const` and `virtual` are *not* required by the concept** — a plain struct with the 10
  non-const, non-virtual members works *(verified: all 10 fire, correct face data produced)*.
  If you *do* derive from `_Arr_default_overlay_traits_base`, you must keep the `const` and
  match the parameter types exactly, otherwise you silently shadow instead of override.
* The traits object is passed by **non-const lvalue reference** to `overlay()` and may keep state
  across calls (e.g. counters, maps) — its lifetime need only cover the `overlay()` call.

### 4.4 `Arr_default_overlay_traits` / `Arr_face_overlay_traits` — `<CGAL/Arr_default_overlay_traits.h>`

```cpp
template <typename Arrangement_>
class Arr_default_overlay_traits :
  public _Arr_default_overlay_traits_base<Arrangement_, Arrangement_, Arrangement_>
{};
```
All three arrangements must be the **same type**; all callbacks are no-ops. Use when you just want
the geometric overlay.

```cpp
template <typename ArrangementA, typename ArrangementB, typename ArrangementR,
          typename OverlayFaceData_>
class Arr_face_overlay_traits :
  public _Arr_default_overlay_traits_base<ArrangementA, ArrangementB, ArrangementR>
{
public:
  typedef typename ArrangementA::Face_const_handle    Face_handle_A;
  typedef typename ArrangementB::Face_const_handle    Face_handle_B;
  typedef typename ArrangementR::Face_handle          Face_handle_R;
  typedef OverlayFaceData_                            Overlay_face_data;

private:
  Overlay_face_data         overlay_face_data;      // private, default-constructed, no accessor

public:
  virtual void create_face(Face_handle_A f1, Face_handle_B f2, Face_handle_R f) const
  {
    f->set_data(overlay_face_data(f1->data(), f2->data()));
    return;
  }
};
```
* Requires face-extended DCELs on all three arrangements: `f1->data()`, `f2->data()`,
  `f->set_data(...)` (e.g. `CGAL::Arr_face_extended_dcel<Traits, T>`).
* `OverlayFaceData_` must be **default-constructible** and callable as
  `ResData operator()(DataA, DataB) const`. It is stored **by value and privately** — you cannot
  inject configuration into it; write your own traits class if you need stateful merging.
* Only `create_face` is overridden; vertices and edges of the result carry default-constructed data.

### 4.5 `Indexed_sweep_accessor` (implementation detail, in `Arr_overlay_2.h`)

```cpp
template <typename Arr1, typename Arr2, typename Curve>
class Indexed_sweep_accessor {
public:
  Indexed_sweep_accessor(const Arr1& arr1, const Arr2& arr2);
  std::size_t  nb_vertices() const;
  std::size_t  min_end_index(const Curve& c) const;
  std::size_t  max_end_index(const Curve& c) const;
  const Curve& curve(const Curve& c) const;
  void before_init() const;   // squats Vertex::inc() with indices
  void after_init()  const;   // restores the saved inc() pointers
};
```
Documented here only because of its thread-safety implication (gotcha 7).

---

## 5. `Aos_observer<Arrangement_>` / `Arr_observer<Arrangement_>`

`<CGAL/Arr_observer.h>` in 6.1 is only:
```cpp
template <typename Arrangement_>
using Arr_observer = typename Arrangement_::Observer;
```
`<CGAL/Aos_observer.h>` holds the real class. `Arrangement_on_surface_2` declares
`using Observer = Aos_observer<Self>;  using Base_aos = Self;` and
`friend class Aos_observer<Self>;`.

### 5.1 Class shape and typedefs

```cpp
template <typename Arrangement_>
class Aos_observer {
public:
  typedef Arrangement_                                     Arrangement_2;
  typedef Aos_observer<Arrangement_2>                      Self;

  typedef typename Arrangement_2::Point_2                  Point_2;
  typedef typename Arrangement_2::X_monotone_curve_2       X_monotone_curve_2;

  typedef typename Arrangement_2::Vertex_handle            Vertex_handle;
  typedef typename Arrangement_2::Halfedge_handle          Halfedge_handle;
  typedef typename Arrangement_2::Face_handle              Face_handle;
  typedef typename Arrangement_2::Ccb_halfedge_circulator  Ccb_halfedge_circulator;

private:
  Arrangement_2* p_arr;
  Aos_observer(const Self&);          // copy ctor NOT supported (declared private, undefined)
  Self& operator=(const Self&);       // assignment NOT supported
```

### 5.2 Construction / destruction / attachment

```cpp
  Aos_observer();                              // p_arr = nullptr, detached
  Aos_observer(Arrangement_2& arr);            // registers immediately: arr._register_observer(this)
  virtual ~Aos_observer();                     // unregisters if attached

  const Arrangement_2* arrangement() const;    // returns p_arr
  Arrangement_2*       arrangement();

  void attach(Arrangement_2& arr);
  void detach();
```
* `attach`: no-op if already attached to the *same* arrangement; otherwise
  `CGAL_precondition(p_arr == nullptr)` — **"The observer is not already attached to an
  arrangement."** Order: `before_attach(arr)` → register → `after_attach()`.
  (With assertions off, attaching an already-attached observer silently returns.)
* `detach`: no-op if not attached; order `before_detach()` → unregister → `p_arr = nullptr` →
  `after_detach()`.
* The observer stores a raw pointer; the **arrangement must outlive the observer** *or* the
  observer must be detached first. The destructor unregisters, so destroying an observer while its
  arrangement is alive is safe. **Destroying the *arrangement* first is also safe**:
  `~Arrangement_on_surface_2()` walks `m_observers` and calls `detach()` on each
  (`Arrangement_on_surface_2_impl.h:232-241`), so `before_detach`/`after_detach` fire and
  `p_arr` becomes `nullptr` — *verified*, see §8.1 T13. Caveat: that loop runs **after** the
  destructor has already deleted every stored `Point_2`/`X_monotone_curve_2`, so `point()`
  and `curve()` dangle inside `before_detach()`.
* Registration/unregistration go through the arrangement's private
  `void _register_observer(Observer*)` / `bool _unregister_observer(Observer*)` (accessible only
  because `Aos_observer<Self>` is a friend). Observers are stored in a `std::list<Observer*>`.
* For a type-erased core: `Arrangement_2` here is the **base** `Arrangement_on_surface_2<GT,TT>`
  (gotcha 1). Passing an `Arrangement_2<T,D>&` to `attach`/ctor works by derived→base conversion,
  but `arrangement()` gives you the base pointer back.

### 5.3 Notification functions — global operations

| Signature | Fires |
|---|---|
| `virtual void before_assign(const Arrangement_2& arr)` | at the start of `Arrangement_on_surface_2::assign(const Self&)`; `arr` is the source being copied |
| `virtual void after_assign()` | at the end of `assign` |
| `virtual void before_clear()` | at the start of `clear()` (also from `overlay()` on the result arrangement) |
| `virtual void after_clear()` | at the end of `clear()` |
| `virtual void before_global_change()` | `Arr_accessor::notify_before_global_change()` — issued by `insert` (once per x-monotone piece), `insert` (range), `insert` with hint, `insert_non_intersecting_curve(s)`, `insert_point`, `remove_edge`, `remove_vertex`. **Not** by `zone`, `do_intersect`, `is_valid`, `overlay` |
| `virtual void after_global_change()` | matching close of the above |

### 5.4 Notification functions — attachment

```cpp
  virtual void before_attach(const Arrangement_2& arr);
  virtual void after_attach();
  virtual void before_detach();
  virtual void after_detach();
```
`before_attach` fires only from `attach()` (**not** from the `Aos_observer(Arrangement_2&)`
constructor, which registers directly). `after_attach` likewise.

### 5.5 Notification functions — local DCEL changes

Every one of these is a no-op `virtual` in the base; override the ones you need.
`Ccb_halfedge_circulator` parameters are passed **by value**.

**Vertex creation**
```cpp
  virtual void before_create_vertex(const Point_2& p);
  virtual void after_create_vertex(Vertex_handle v);
  virtual void before_create_boundary_vertex(const Point_2& p,
                                             Arr_parameter_space ps_x,
                                             Arr_parameter_space ps_y);
  virtual void before_create_boundary_vertex(const X_monotone_curve_2& cv,
                                             Arr_curve_end ind,
                                             Arr_parameter_space ps_x,
                                             Arr_parameter_space ps_y);
  virtual void after_create_boundary_vertex(Vertex_handle v);
```
`before_create_vertex`'s doc: *"The point to be associated with the vertex. This point cannot lie
on the surface boundaries."* Boundary vertices (at infinity / on an identification) get the
`*_boundary_vertex` pair instead; there are **two** `before_` overloads (point-based and
curve-end-based) but a single `after_`.

**Edge creation / modification**
```cpp
  virtual void before_create_edge(const X_monotone_curve_2& c,
                                  Vertex_handle v1, Vertex_handle v2);
  virtual void after_create_edge(Halfedge_handle e);      // one of the twin halfedges

  virtual void before_modify_vertex(Vertex_handle v, const Point_2& p);
  virtual void after_modify_vertex(Vertex_handle v);

  virtual void before_modify_edge(Halfedge_handle e, const X_monotone_curve_2& c);
  virtual void after_modify_edge(Halfedge_handle e);
```
`modify_vertex` fires from `Arrangement_on_surface_2::modify_vertex` (and thus from
`insert_point` when the point coincides with an existing vertex); `modify_edge` from
`modify_edge`.

**Splits**
```cpp
  virtual void before_split_edge(Halfedge_handle e, Vertex_handle v,
                                 const X_monotone_curve_2& c1,
                                 const X_monotone_curve_2& c2);
  virtual void after_split_edge(Halfedge_handle e1, Halfedge_handle e2);

  virtual void before_split_fictitious_edge(Halfedge_handle e, Vertex_handle v);
  virtual void after_split_fictitious_edge(Halfedge_handle e1, Halfedge_handle e2);

  virtual void before_split_face(Face_handle f, Halfedge_handle e);   // e: the new edge that splits f
  virtual void after_split_face(Face_handle f, Face_handle new_f, bool is_hole);

  virtual void before_split_outer_ccb(Face_handle f, Ccb_halfedge_circulator h,
                                      Halfedge_handle e);
  virtual void after_split_outer_ccb(Face_handle f, Ccb_halfedge_circulator h1,
                                     Ccb_halfedge_circulator h2);

  virtual void before_split_inner_ccb(Face_handle f, Ccb_halfedge_circulator h,
                                      Halfedge_handle e);
  virtual void after_split_inner_ccb(Face_handle f, Ccb_halfedge_circulator h1,
                                     Ccb_halfedge_circulator h2);
```
* `after_split_face`'s `is_hole` is documented as *"Whether the new face forms a hole inside f."*
* The `*_fictitious_edge` pair only ever fires in topologies that have fictitious halfedges
  (`Arr_unb_planar_topology_traits_2`); with `Arr_bounded_planar_topology_traits_2` it never fires.
* `before_split_outer_ccb` / `before_split_inner_ccb` docs say *"The new edge whose **removal**
  causes the … CCB to split"* — the wording in the header says "removal" for both.

**Additions of components to faces**
```cpp
  virtual void before_add_outer_ccb(Face_handle f, Halfedge_handle e);
  virtual void after_add_outer_ccb(Ccb_halfedge_circulator h);

  virtual void before_add_inner_ccb(Face_handle f, Halfedge_handle e);
  virtual void after_add_inner_ccb(Ccb_halfedge_circulator h);

  virtual void before_add_isolated_vertex(Face_handle f, Vertex_handle v);
  virtual void after_add_isolated_vertex(Vertex_handle v);
```
Note the asymmetry: the `after_add_*_ccb` callbacks receive **only** the circulator, not the face.

**Merges**
```cpp
  virtual void before_merge_edge(Halfedge_handle e1, Halfedge_handle e2,
                                 const X_monotone_curve_2& c);
  virtual void after_merge_edge(Halfedge_handle e);

  virtual void before_merge_fictitious_edge(Halfedge_handle e1, Halfedge_handle e2);
  virtual void after_merge_fictitious_edge(Halfedge_handle e);

  virtual void before_merge_face(Face_handle f1, Face_handle f2, Halfedge_handle e);
  virtual void after_merge_face(Face_handle f);

  virtual void before_merge_outer_ccb(Face_handle f, Ccb_halfedge_circulator h1,
                                      Ccb_halfedge_circulator h2, Halfedge_handle e);
  virtual void after_merge_outer_ccb(Face_handle f, Ccb_halfedge_circulator h);

  virtual void before_merge_inner_ccb(Face_handle f, Ccb_halfedge_circulator h1,
                                      Ccb_halfedge_circulator h2, Halfedge_handle e);
  virtual void after_merge_inner_ccb(Face_handle f, Ccb_halfedge_circulator h);
```
`before_merge_face`'s `e` is *"The edge whose removal causes the faces to merge"*;
`before_merge_outer_ccb`'s `e` is *"The edge whose insertion or removal causes the CCBs to merge"*;
`before_merge_inner_ccb`'s `e` is *"The edge whose insertion causes the inner CCBs to merge"*.

**Moves between faces**
```cpp
  virtual void before_move_outer_ccb(Face_handle from_f, Face_handle to_f,
                                     Ccb_halfedge_circulator h);
  virtual void after_move_outer_ccb(Ccb_halfedge_circulator h);

  virtual void before_move_inner_ccb(Face_handle from_f, Face_handle to_f,
                                     Ccb_halfedge_circulator h);
  virtual void after_move_inner_ccb(Ccb_halfedge_circulator h);

  virtual void before_move_isolated_vertex(Face_handle from_f, Face_handle to_f,
                                           Vertex_handle v);
  virtual void after_move_isolated_vertex(Vertex_handle v);
```

**Removals**
```cpp
  virtual void before_remove_vertex(Vertex_handle v);
  virtual void after_remove_vertex();                    // no arguments!

  virtual void before_remove_edge(Halfedge_handle e);    // one of the twin halfedges
  virtual void after_remove_edge();                      // no arguments!

  virtual void before_remove_outer_ccb(Face_handle f, Ccb_halfedge_circulator h);
  virtual void after_remove_outer_ccb(Face_handle f);    // the face that used to own it

  virtual void before_remove_inner_ccb(Face_handle f, Ccb_halfedge_circulator h);
  virtual void after_remove_inner_ccb(Face_handle f);
```
`after_remove_vertex()` and `after_remove_edge()` take **no** parameters — capture anything you
need in the matching `before_*`.

### 5.6 The complete list, in header order (61 `virtual void` members + the virtual dtor)

`before_assign`, `after_assign`, `before_clear`, `after_clear`, `before_global_change`,
`after_global_change`, `before_attach`, `after_attach`, `before_detach`, `after_detach`,
`before_create_vertex`, `after_create_vertex`, `before_create_boundary_vertex` (×2 overloads),
`after_create_boundary_vertex`, `before_create_edge`, `after_create_edge`, `before_modify_vertex`,
`after_modify_vertex`, `before_modify_edge`, `after_modify_edge`, `before_split_edge`,
`after_split_edge`, `before_split_fictitious_edge`, `after_split_fictitious_edge`,
`before_split_face`, `after_split_face`, `before_split_outer_ccb`, `after_split_outer_ccb`,
`before_split_inner_ccb`, `after_split_inner_ccb`, `before_add_outer_ccb`, `after_add_outer_ccb`,
`before_add_inner_ccb`, `after_add_inner_ccb`, `before_add_isolated_vertex`,
`after_add_isolated_vertex`, `before_merge_edge`, `after_merge_edge`,
`before_merge_fictitious_edge`, `after_merge_fictitious_edge`, `before_merge_face`,
`after_merge_face`, `before_merge_outer_ccb`, `after_merge_outer_ccb`, `before_merge_inner_ccb`,
`after_merge_inner_ccb`, `before_move_outer_ccb`, `after_move_outer_ccb`, `before_move_inner_ccb`,
`after_move_inner_ccb`, `before_move_isolated_vertex`, `after_move_isolated_vertex`,
`before_remove_vertex`, `after_remove_vertex`, `before_remove_edge`, `after_remove_edge`,
`before_remove_outer_ccb`, `after_remove_outer_ccb`, `before_remove_inner_ccb`,
`after_remove_inner_ccb`.

### 5.7 Minimal working observer (compiled against this install)

```cpp
typedef CGAL::Arrangement_2<Traits> Arr;

struct MyObs : public CGAL::Arr_observer<Arr> {          // == Aos_observer<Arr::Base>
  explicit MyObs(Arr& a) : CGAL::Arr_observer<Arr>(a) {} // Arr& binds to Arr::Base&
  void after_create_edge(Halfedge_handle e) override { /* ... */ }
};
```
*(verified: compiles, fires once per created edge during `insert(arr, begin, end)`.)*

---

## 6. Arrangement-with-history globals — `<CGAL/Arrangement_on_surface_with_history_2.h>`

`Arrangement_with_history_2<GeomTraits, Dcel>` derives from
`Arrangement_on_surface_with_history_2<GeomTraits, Default_planar_topology<...>::Traits>`, which in
turn derives from `Arrangement_on_surface_2<Arr_consolidated_curve_data_traits_2<GeomTraits,
GeomTraits::Curve_2*>, rebound-topology-traits>` (typedef `Base_arr_2` / `Base_arrangement_2`).

```cpp
  typedef typename Curve_halfedges_list::iterator        Curve_iterator;
  typedef typename Curve_halfedges_list::const_iterator  Curve_const_iterator;
  typedef Curve_iterator                                 Curve_handle;
  typedef Curve_const_iterator                           Curve_const_handle;
```

```cpp
template <class GeomTraits, class TopTraits, class PointLocation>
typename Arrangement_on_surface_with_history_2<GeomTraits, TopTraits>::Curve_handle
insert(Arrangement_on_surface_with_history_2<GeomTraits, TopTraits>& arr,
       const typename GeomTraits::Curve_2& c, const PointLocation& pl);

template <class GeomTraits, class TopTraits>
typename Arrangement_on_surface_with_history_2<GeomTraits, TopTraits>::Curve_handle
insert(Arrangement_on_surface_with_history_2<GeomTraits, TopTraits>& arr,
       const typename GeomTraits::Curve_2& c);

template <class GeomTraits, class TopTraits, class InputIterator>
void insert(Arrangement_on_surface_with_history_2<GeomTraits, TopTraits>& arr,
            InputIterator begin, InputIterator end);            // \pre value_type == Curve_2

template <class GeomTraits, class TopTraits>
typename Arrangement_on_surface_with_history_2<GeomTraits, TopTraits>::Size
remove_curve(Arrangement_on_surface_with_history_2<GeomTraits, TopTraits>& arr,
             typename Arrangement_on_surface_with_history_2
               <GeomTraits, TopTraits>::Curve_handle ch);        // returns #removed edges
```
* All four go through `Arr_with_history_accessor<Arr_with_hist_2>` →
  `_insert_curve(cv, pl)` / `_insert_curve(cv)` / `_insert_curves(begin, end)` / `_remove_curve(ch)`,
  which wrap the base-arrangement `CGAL::insert(base_arr, data_curve[, pl])` calls.
* There are **no** with-history overloads of `insert` taking an `X_monotone_curve_2` or a
  point-location hint, and none of `insert_non_intersecting_curve(s)` / `insert_point` /
  `remove_edge` / `remove_vertex` / `zone` / `do_intersect` / `is_valid`: those all resolve to the
  base-class `Arrangement_on_surface_2` templates by derived→base deduction, **bypassing the
  curve-history bookkeeping for removals**. Removing edges that way leaves the `Curve_halfedges`
  sets updated only through the internal `Curve_halfedges_observer`.
* `Curve_handle` is a `std::list` iterator: stable across other insertions/removals, invalidated
  only by `remove_curve` of that same curve (or `clear()`).
* Related traversal members (all `const`, all taking **const** handles):
  ```cpp
  Size number_of_curves() const;
  Curve_iterator       curves_begin();        Curve_iterator       curves_end();
  Curve_const_iterator curves_begin() const;  Curve_const_iterator curves_end() const;

  Size number_of_originating_curves(Halfedge_const_handle e) const;   // e->curve().data().size()
  Originating_curve_iterator originating_curves_begin(Halfedge_const_handle e) const;
  Originating_curve_iterator originating_curves_end  (Halfedge_const_handle e) const;

  typedef typename Curve_halfedges::const_iterator Induced_edge_iterator;
  Size number_of_induced_edges(Curve_const_handle c) const;           // c->size()
  Induced_edge_iterator induced_edges_begin(Curve_const_handle c) const;
  Induced_edge_iterator induced_edges_end  (Curve_const_handle c) const;
  ```
  `Originating_curve_iterator` has `operator Curve_iterator() const`, so it converts to a
  `Curve_handle` you can feed back into `remove_curve`.

**Overlay with history:**
```cpp
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
```
* Unlike the plain `overlay`, **all three must share the same `GeomTraits`** — only the topology
  traits (hence the DCELs) may differ.
* `res._overlay(...)` does: `clear()`; `m_observer.detach()`; `CGAL::overlay(base_arr1, base_arr2,
  base_res, overlay_tr)` on the **base** arrangements; duplicate every input curve into `res` and
  remap the curve-data pointers on every result edge; `m_observer.attach(*this)`.
  Consequently the `OverlayTraits` you pass sees the **base** arrangement handle types
  (`Base_arr_2::…_const_handle`), whose `X_monotone_curve_2` is the *consolidated-data* curve type,
  not `GeomTraits::X_monotone_curve_2`.
* The no-traits overload uses `_Arr_default_overlay_traits_base<ArrA, ArrB, ArrRes>` where
  `ArrA/ArrB/ArrRes` are the **with-history** types (so its handle typedefs come from those).

---

## 7. Supporting facts a binding layer needs

* `Arr_accessor<Arrangement_2>` — `<CGAL/Arr_accessor.h>`:
  ```cpp
  Arr_accessor(Arrangement_2& arr);
  Arrangement_2&       arrangement();
  const Arrangement_2& arrangement() const;
  void notify_before_global_change();
  void notify_after_global_change();
  Pl_result_type locate_curve_end(const X_monotone_curve_2& cv, Arr_curve_end ind,
                                  Arr_parameter_space ps_x, Arr_parameter_space ps_y) const;
  //  \pre The relevant end of cv has boundary conditions in x or in y.
  //  CGAL_precondition((ps_x != ARR_INTERIOR) || (ps_y != ARR_INTERIOR));
  ```
  Use `notify_before/after_global_change()` to bracket your own composite operations so that
  attached observers see one logical change instead of many.

* Topology-traits hooks used by the globals (identical in
  `Arr_bounded_planar_topology_traits_2` and `Arr_unb_planar_topology_traits_2`):
  ```cpp
  typedef Arr_inc_insertion_zone_visitor<Arr>        Zone_insertion_visitor;
  typedef Arr_walk_along_line_point_location<Arr>    Default_point_location_strategy;
  typedef Arr_walk_along_line_point_location<Arr>    Default_vertical_ray_shooting_strategy;
  ```

* Member `remove_edge` (contrast with the global one, §2.5):
  ```cpp
  Face_handle remove_edge(Halfedge_handle e, bool remove_source = true,
                                             bool remove_target = true);
  ```

* Member `bool is_valid() const;` — cheap DCEL/topology check only; the global `is_valid` adds the
  sweep and hole-placement checks.

### Handle / iterator validity summary

* All handles are DCEL list iterators (`In_place_list_iterator`, wrapped in
  `I_Filtered_const_iterator` for the const versions). They are **stable** under insertions and
  under removals of *other* cells.
* `insert(...)` (any overload) may **split** existing edges: a `Halfedge_handle` to a split edge
  stays valid but now denotes only one of the two pieces (`after_split_edge(e1, e2)` tells you
  both). Faces may be split (`after_split_face`) or merged (`after_merge_face`) — a
  `Face_handle` to a merged-away face **dangles**.
* `remove_edge` / `remove_vertex` / `remove_curve` invalidate the handles of everything they
  delete (including the merged-away partner edge and the removed vertex).
* `overlay()` calls `clear()` on the result → **every** handle into the result arrangement is
  invalidated; handles into the inputs stay valid (the inputs are not structurally modified).
* `clear()` / `assign()` invalidate everything in the target arrangement.
* Observers hold a raw `Arrangement_2*`; the arrangement does not own its observers and never
  deletes them. Ownership of the observer object is entirely yours.
* Overlay traits and zone visitors are held by raw pointer/reference for the duration of the call
  only.

---

## 8. Observed notification traces

Everything in this section was produced by **compiling and running** an instrumented observer
against the installed CGAL 6.1 headers (`/opt/homebrew/include/CGAL`), with
`clang++ -std=c++17 -O0 -DCGAL_USE_CORE -DCGAL_USE_GMP -DCGAL_USE_MPFR`, **CGAL assertions
enabled** (no `CGAL_NO_ASSERTIONS`). Every claim marked **[verified]** is an observed program
output, not a reading of the source; claims marked *(source)* were read out of
`CGAL/Arrangement_2/Arrangement_on_surface_2_impl.h` and are quoted with a line number.

§5.5/§5.6 tell you *what* the 61 virtuals are. This section tells you *when* they fire, *in what
order*, and *what is legal to read inside each one* — the information an extended-DCEL
data-inheriting observer actually needs (cf. `dcel_and_accessor.md` §7.5: "data is not propagated
on split/merge — attach an observer overriding `after_split_face`, `after_split_edge`,
`before_merge_face`, …").

### 8.0 The instrumented observer

A `Tracer<Arr> : public CGAL::Arr_observer<Arr>` overriding **all 61 virtuals**. Each override
logs the callback name plus, for every handle argument, a stable id derived from the DCEL
address (`&*h`), the geometry (`v->point()`, `e->curve()`), `e->is_fictitious()`,
`f->is_fictitious()`, `f->is_unbounded()`, incidence (`e->source()/target()/twin()/prev()/next()/
face()`, `f->has_outer_ccb()`, hole and isolated-vertex counts) and the live
`number_of_vertices()/edges()/faces()`.

Because some of these accessors are **not** legal in some callbacks, every risky access is wrapped
in a `SIGSEGV`/`SIGBUS`-guarded probe (`sigsetjmp`/`siglongjmp`) that prints `<CRASH>` instead of
aborting. That is what makes the dereferenceability matrix in §8.15 an empirical result rather
than a guess.

Notation used below:

* `vN` / `hN` / `fN` — a stable id per DCEL address; the **same id means the same DCEL object**,
  so "`h0` appears in both `before_split_edge` and `after_split_edge`" is proof that the original
  halfedge object was reused.
* Indentation shows nesting inside a `before_global_change` … `after_global_change` bracket.
* `[V=… E=… F=…]` are `number_of_vertices()/edges()/faces()` **at that moment**.
  (`number_of_vertices()` excludes vertices at infinity; `number_of_faces()` excludes the
  fictitious face.)

### 8.1 Gotchas discovered by tracing — labelled **T1 … T15**

These are *additional* to the top-of-file list and are cross-referenced by their **T**
labels throughout §8 (the top-of-file list has its own, independent numbering).

1. **T1 — `before_create_edge` brackets `before_split_face`/`after_split_face`, and
    `after_create_edge` fires *last*.** The canonical order for "a new edge closes a cycle" is
    ```
    before_create_edge(c, v1, v2)
      before_split_face(f, e)          <- e is the NEW halfedge, already linked into next/prev
      after_split_face(f, new_f, is_hole)
    after_create_edge(e2)              <- e2 == e->twin()
    ```
    **[verified]** in every single-curve, aggregated-sweep and overlay scenario below. So
    `after_split_face` is **not** nested inside a create-edge "after" — there is no
    `after_create_edge` before it. A data-inheriting observer that wants to seed `new_f`'s data
    from `f` must do it in `after_split_face`; at that point the new edge's own data has *not*
    been set by any of your `after_create_edge` code yet.

2. **T2 — `after_create_edge(e)` receives `he2`, `before_split_face(f, e)` receives `he1 == he2->twin()`**
    *(source: `_notify_before_split_face(fh, Halfedge_handle(he1))` at
    `Arrangement_on_surface_2_impl.h:2923`, `_notify_after_create_edge(Halfedge_handle(he2))` at
    `:3162`)*, and `he2` is the halfedge directed `v1 → v2` in the `before_create_edge(c, v1, v2)`
    call. **[verified]**. Consequence, and this is the single most useful fact for face-data
    inheritance: **inside `after_create_edge(e)`, `e->face()` is the NEW face and
    `e->twin()->face()` is the surviving original face.**

3. **T3 — `after_split_edge(e1, e2)`: `e1` *is* the halfedge object you passed to
    `before_split_edge`** — same DCEL address, same user data, same `source()`; only its `target()`
    is re-pointed at the split vertex and its curve replaced by `c1`. `e2` is a brand-new edge
    pair carrying `c2` and running split-vertex → original target. *(source:
    `he1->set_vertex(v); he1->curve() = cv1; he3->set_curve(dup_cv2);
    _notify_after_split_edge(he1, he3)` at `:3452-3468`; `split_edge` returns `he1`.)*
    **[verified]** — `h0` in `before_split_edge` is `h0` (first arg) in `after_split_edge` in all
    of S2, S2b, S4b, S6b, S8a, S10b. So **halfedge data is automatically "inherited" by `e1`; only
    `e2` needs seeding.** `c1` is always the piece incident to `e->source()`.

4. **T4 — `after_merge_edge(e)` returns the edge pair of the *first* argument of
    `before_merge_edge(e1, e2, c)`; the pair of `e2` is deleted.** *(source: `he1/he2` are taken
    from `_e1`, `he3/he4` from `_e2`, and `_dcel().delete_edge(he3)` runs before
    `_notify_after_merge_edge(hh)` with `hh = he1`, `:1614-1741`.)* **[verified]**. Caveat: the
    handle you get back may be the **twin** of the `e1` you passed (the code normalises the
    orientation), but it is the same *edge*.

5. **T5 — `before_merge_face(f1, f2, e)` does NOT reliably name the survivor.** The survivor is the
    face reported by `after_merge_face`, and in the "merge a face with a face that is a hole inside
    it" path CGAL swaps `f1`/`f2` **after** the `before_` notification
    *(source: the swap at `:5089-5105` sits between `_notify_before_merge_face` at `:4912` and
    `_notify_after_merge_face` at `:5144`)*.
    **[verified]** — t8 scenario A: `before_merge_face(f1 = the bounded face {#in=2},
    f2 = the unbounded face {#in=1})` but `after_merge_face(the unbounded face {#in=3})`.
    Which face is `f1` at the `before_` call is itself a *performance* heuristic, not a topological
    one: *(source `:4302-4312`)* `if (f1->number_of_inner_ccbs() < f2->number_of_inner_ccbs())
    swap_he1_he2 = true;` — CGAL keeps whichever face has more holes.
    **Therefore: capture BOTH faces' data in `before_merge_face` and decide in `after_merge_face`
    which one survived.** Never assume `f1`.

6. **T6 — `before_split_face(f, e)`: `e->face()` *and* `e->twin()->face()` both crash.** **[verified]**
    (`<CRASH>` in every S2b/S3a/S3b/S3c/S4a/S4b/S9/S10a/S10b trace). At that instant the new
    halfedge pair has `p_comp == nullptr` — `Halfedge::face()` dereferences it with no null check
    *(source: `Arrangement_on_surface_2.h`, `Face_handle face() { return (! Base::is_on_inner_ccb())
    ? DFace_iter(Base::outer_ccb()->face()) : … }` — `is_on_inner_ccb()` returns `false` for a null
    component pointer and `outer_ccb()` then returns `nullptr`)*. Everything else on `e` is fine:
    `curve()`, `source()`, `target()`, `twin()`, `prev()`, `next()`, `is_fictitious()`.

7. **T7 — At `after_split_face(f, new_f, is_hole)` the new face is already fully live**: it is linked
    into `faces_begin()..faces_end()`, `number_of_faces()` has already been incremented, and
    `new_f->outer_ccb()` is a complete walkable circulator. **[verified]** by iterating
    `arr.faces_begin()..faces_end()` inside the callback (`{old:LINKED new:LINKED}` in every trace)
    and by walking `new_f`'s outer CCB (3 halfedges for a triangle). `f` keeps its identity and its
    data; `new_f` is `_dcel().new_face()` — **default-constructed, so its user data is whatever your
    DCEL face type default-constructs to.**

8. **T8 — `v->degree()` is safe everywhere** (it null-checks `Base::halfedge()` internally and returns
    0), but **`v->incident_halfedges()` is not**: it only asserts `!is_isolated()`, then builds a
    circulator over a null halfedge — dereferencing it crashes on a freshly created vertex.
    **[verified]**: `<CRASH>` in `after_create_vertex`, in `before_add_isolated_vertex`, and for the
    new vertex argument of `before_split_edge`. Note a brand-new vertex reports
    `is_isolated() == false` even before it is attached to anything.

9. **T9 — The aggregated sweep (`insert(arr, first, last)`) fires the ordinary per-element callbacks,
    and brackets the WHOLE call with exactly one `before_global_change`/`after_global_change`.**
    **[verified]** both into an empty and into a non-empty arrangement. It goes through
    `Arr_construction_ss_visitor` → `Arr_accessor::create_vertex / insert_in_face_interior_ex /
    insert_at_vertices_ex / insert_from_vertex_ex`, all of which notify normally. `set_sweep_mode(true)`
    (set by the visitor, `Arr_construction_ss_visitor.h:292`) only enables a union-find shortcut for
    inner-CCB records *(source `:2784`)* — it does **not** suppress notifications.
    Difference vs. the incremental path: into an **empty** arrangement no `before/after_split_edge`
    fires at all (the sweep already knows the sub-curves and creates them pre-split); into a
    **non-empty** arrangement existing edges *are* split normally.

10. **T10 — `insert(arr, non_x_monotone_curve)` issues ONE global-change bracket per x-monotone piece.**
    **[verified]**: a 3-piece zig-zag polyline produced three complete
    `before_global_change … after_global_change` blocks. (Gotcha 12 of the top list, now confirmed
    empirically.) A one-piece "non-x-monotone" curve that happens to be x-monotone yields one
    bracket — the count is the number of pieces `make_x_monotone_2` returns, not the number of
    input curves.

11. **T11 — `overlay()` issues NO global-change bracket at all** — it opens with
    `before_clear`/`after_clear` on the result and then fires only local DCEL callbacks.
    **[verified]** (§8.11). It also does **not** detach observers from the result: an observer
    attached to `res` before `overlay` is still attached and still fires afterwards. **[verified]**

12. **T12 — For the same face, the overlay traits' `create_face` fires AFTER `after_split_face`.**
    **[verified]** — the invariant per face is
    `before_split_face → after_split_face → after_create_edge → create_edge → create_face`. The
    `create_face` for the **unbounded** result face fires dead last, after every other callback in
    the whole `overlay` call. Same pattern for vertices/edges: the DCEL `after_create_vertex` /
    `after_create_edge` always precede the matching `create_vertex` / `create_edge`.

13. **T13 — `Aos_observer::detach()` IS called from `~Arrangement_on_surface_2()`.** *(source:
    `Arrangement_on_surface_2_impl.h:232-241` — "Detach all observers still attached to the
    arrangement", looping `(*iter)->detach()`.)* **[verified]**: destroying the arrangement first
    emits `before_detach`/`after_detach` and leaves `observer.arrangement() == nullptr`.
    **This corrects §5.2 above** ("Destroying the *arrangement* first leaves a dangling `p_arr`")
    — it does not; `p_arr` is nulled. Caveat: the detach loop runs **after** the destructor has
    already `_delete_point`-ed and `_delete_curve`-d everything, so inside `before_detach()` the
    DCEL topology is still linked but **every `point()`/`curve()` pointer dangles** — never read
    geometry in `before_detach`.

14. **T14 — `Polygon_set_2` binary ops replace the arrangement object, so your observer is silently
    detached.** **[verified]**: attaching to `ps.arrangement()` and calling `ps.join(...)` produced
    exactly `before_detach`, `after_detach` and nothing else; `&ps.arrangement()` changed and the
    observer's `arrangement()` became `nullptr`. *(source:
    `Gps_on_surface_base_2.h:1482-1488` — `Aos_2* res_arr = new Aos_2(m_traits); overlay(*m_arr, arr,
    *res_arr, func); delete m_arr; m_arr = res_arr;` — same shape at `:1547`, `:1623`, `:1690`.)*
    Re-attach after **every** binary op; `insert()` of a single polygon does not replace it.

15. **T15 — `_move_all_inner_ccb` batches its notifications**: all `before_move_inner_ccb` for every
    moved CCB first, then the splice, then all `after_move_inner_ccb` — *not*
    before/after/before/after. **[verified]** *(source: `:1975-1996`, with an in-source comment
    asking for a `before/after_move_all_inner_ccb` pair instead.)* Do not assume `before_x` is
    immediately followed by its own `after_x`.

### 8.2 Which globals bracket with `before/after_global_change` — **[verified]**

| call | bracket? |
|---|---|
| `CGAL::insert(arr, xcv)` / `insert(arr, xcv, pl)` / `insert(arr, xcv, hint)` | 1 per call |
| `CGAL::insert(arr, non_x_monotone_curve)` | **1 per x-monotone piece** |
| `CGAL::insert(arr, first, last)` (aggregated sweep) | **1 for the whole range** |
| `CGAL::insert_non_intersecting_curve(s)` (single and range) | 1 for the whole call |
| `CGAL::insert_point`, `CGAL::remove_edge`, `CGAL::remove_vertex` | 1 per call |
| `CGAL::zone`, `CGAL::do_intersect`, `CGAL::is_valid` | **0 callbacks of any kind** |
| `CGAL::overlay` | **none** (only `before_clear`/`after_clear` then local callbacks) |
| `arr.split_edge`, `arr.merge_edge`, `arr.remove_edge`, `arr.remove_isolated_vertex`, `arr.insert_*` (members) | **none** — members never bracket |
| `arr.clear()` | `before_clear`/`after_clear` only |
| `arr.assign(src)` | `before_clear`, `after_clear`, `before_assign`, `after_assign` (in that order) |

If you compose several member calls into one logical operation, bracket them yourself with
`Arr_accessor<Arr>::notify_before_global_change()` / `notify_after_global_change()` (§7).

### 8.3 Scenario 1 — `CGAL::insert(arr, s)` into an empty arrangement — **[verified]**

`Arr_segment_traits_2<Epeck>`, `s = (0,0)–(10,0)`.

```
 0  before_global_change                                    [V=0 E=0 F=1]
 1    before_create_vertex(p = 0 0)
 2    after_create_vertex(v0 {iso=0, openb=0, p=0 0, deg=0})
 3    before_create_vertex(p = 10 0)
 4    after_create_vertex(v1 {iso=0, openb=0, p=10 0, deg=0})
 5    before_create_edge(c = 0 0 10 0, v0, v1)
 6    after_create_edge(h0 {fict=0, cv=0 0 10 0, tw=h1, src=v0, tgt=v1, face=f0,
                            prev=h1, next=h1})
 7    before_add_inner_ccb(f0 {fict=0, unb=1, outer=0, #in=0, #iso=0}, h0)
 8    after_add_inner_ccb(ccb@h0)
 9  after_global_change                                     [V=2 E=1 F=1]
```

Notes: the unbounded face of the *bounded* planar topology has **no outer CCB**
(`has_outer_ccb() == false`); an isolated segment becomes an **inner CCB (hole)** of it.
At `after_create_edge` the halfedge is already fully linked — `face()`, `prev()`, `next()` all
readable.

*Data-inheriting observer*: nothing to inherit. Seed defaults for `v0`, `v1`, `h0`/`h0->twin()` in
`after_create_vertex` / `after_create_edge`.

### 8.4 Scenario 2 — `CGAL::insert(arr, s2)` crossing one existing edge — **[verified]**

Existing: `(0,0)–(10,0)`. Inserted: `(5,-5)–(5,5)`.

```
 0  before_global_change                                    [V=2 E=1 F=1]
 1    before_create_vertex(p = 5 0)                           <- the intersection point
 2    after_create_vertex(v0)
 3    before_split_edge(h0 {cv=0 0 10 0, src=v1(10,0), tgt=v2(0,0)}, v0,
                        c1 = 5 0 10 0, c2 = 0 0 5 0)
 4    after_split_edge(h0 {cv=5 0 10 0, src=v1, tgt=v0},      <- SAME object as arg of before_
                       h2 {cv=0 0 5 0,  src=v0, tgt=v2})      <- NEW
 5    before_create_vertex(p = 5 -5)
 6    after_create_vertex(v3)
 7    before_create_edge(c = 5 -5 5 0, v0 {deg=2}, v3)
 8    after_create_edge(h4 {src=v0, tgt=v3, face=f0})
 9    before_create_vertex(p = 5 5)
10    after_create_vertex(v4)
11    before_create_edge(c = 5 0 5 5, v0 {deg=3}, v4)
12    after_create_edge(h6 {src=v0, tgt=v4, face=f0})
13  after_global_change                                     [V=5 E=4 F=1]
```

The inserted curve is split at the intersection into **two** separate `create_edge` events;
`before_create_edge`'s `v1` argument shows the running degree of the shared vertex (2, then 3).
No face split — the new curve does not close a cycle.

**Edge split + face split in one insert** (chord that crosses an edge *and* closes a cycle;
triangle `(0,0),(10,0),(5,8)` already present, insert `(0,0)–(7.5,4)` where `(7.5,4)` is the
midpoint of the right edge):

```
 0  before_global_change                                    [V=3 E=3 F=2]
 1    before_create_vertex(p = 7.5 4)
 2    after_create_vertex(v0)
 3    before_split_edge(h0 {cv=10 0 5 8}, v0, c1 = 10 0 7.5 4, c2 = 7.5 4 5 8)
 4    after_split_edge(h0 {cv=10 0 7.5 4}, h4 {cv=7.5 4 5 8})
 5    before_create_edge(c = 0 0 7.5 4, v3(0,0) {deg=2}, v0 {deg=2})
 6      before_split_face(f0 {unb=0, outer=1, #in=0},
                          newedge = h6 {src=v0, tgt=v3, face=<CRASH>, prev=h0, next=h2})
 7      after_split_face(f0, NEW = f1 {unb=0, outer=1}, is_hole = false)
                                                            [V=4 E=5 F=3] {old:LINKED new:LINKED}
 8    after_create_edge(h7 {tw=h6, src=v3, tgt=v0, face=f1})   <- face=f1 == the NEW face
 9  after_global_change                                     [V=4 E=5 F=3]
```

*Data-inheriting observer*:
* `after_split_edge(e1, e2)` — `e1` already carries the old data (same object). Copy
  `e1`'s data (and `e1->twin()`'s) into `e2` and `e2->twin()`. This is the only thing you need to do.
* Face split — see §8.5.

### 8.5 Scenario 3 — closing a cycle, both `is_hole` values — **[verified]**

**(a) `is_hole == true`** — the new cycle becomes a *hole* of the face it was drawn in.
Two edges of a triangle present in the unbounded face; insert the closing edge:

```
 0  before_global_change                                    [V=3 E=2 F=1]
 1    before_create_edge(c = 5 8 0 0, v0(5,8) {deg=1}, v1(0,0) {deg=1})
 2      before_split_face(f0 {unb=1, outer=0, #in=1}, newedge = h0 {face=<CRASH>})
                                                            [V=3 E=3 F=1]
 3      after_split_face(f0 {unb=1, outer=0, #in=1},
                         NEW = f1 {unb=0, outer=1, #in=0}, is_hole = TRUE)
                                                            [V=3 E=3 F=2] {old:LINKED new:LINKED}
 4    after_create_edge(h1 {tw=h0, src=v0, tgt=v1, face=f1})
 5  after_global_change                                     [V=3 E=3 F=2]
```

Same shape when the cycle closes strictly inside an already-bounded face (square with an inner
triangle): `f0 {unb=0, outer=1, #in=1}` → `NEW f1 {unb=0}`, `is_hole = TRUE`.

**(b) `is_hole == false`** — a chord splits an existing CCB into two. Square present; insert the
diagonal `(0,0)–(10,10)`:

```
 0  before_global_change                                    [V=4 E=4 F=2]
 1    before_create_edge(c = 0 0 10 10, v0 {deg=2}, v1 {deg=2})
 2      before_split_face(f0 {unb=0, outer=1, #in=0}, newedge = h0 {face=<CRASH>})
 3      after_split_face(f0, NEW = f1 {unb=0, outer=1}, is_hole = false)
                                                            [V=4 E=5 F=3] {old:LINKED new:LINKED}
 4    after_create_edge(h1 {tw=h0, face=f1})
 5  after_global_change                                     [V=4 E=5 F=3]
```

Semantics *(source `:3096` "In this case the face f is simply split into two (case 3.4)")*:
`is_hole == true` ⟺ the removed/created edge lay on an **inner** CCB of `f` and `new_f` becomes a
hole inside `f`; `is_hole == false` ⟺ `f`'s **outer** CCB was split and the two faces are
side-by-side. In the `is_hole == true` branch CGAL may additionally emit
`before/after_move_outer_ccb` *nested inside* the split *(source `:3082` `_move_outer_ccb(f, new_f,
*oc_to_move)`)* — not reachable with the planar bounded topology, see §8.14.

*Data-inheriting observer — face data:*

| callback | what is readable | what to do |
|---|---|---|
| `before_split_face(f, e)` | **OLD data of `f` is intact.** `e->curve()/source()/target()/twin()/prev()/next()` OK. `e->face()` and `e->twin()->face()` **CRASH**. `number_of_faces()` is still the pre-split count. | stash `f`'s data (and `&*f`) in a member |
| `after_split_face(f, new_f, is_hole)` | both faces linked and walkable, `new_f` **default-constructed**, `f` unchanged | copy the stashed data into `new_f` — this is the *only* place both faces exist and `f` still holds the old value |
| `after_create_edge(e)` | `e->face()` == `new_f`, `e->twin()->face()` == `f` | use this if you need "which side is which" without stashing |

`f->is_unbounded()` can be *rewritten by the split* between the `before_` and `after_` calls
*(source `:3105-3120`)*, so re-read it in `after_split_face` if you key data off it.

### 8.6 Scenario 4 — `CGAL::insert(arr, first, last)`, the aggregated sweep — **[verified]**

Path: `Arrangement_on_surface_2_global.h:432` → `Arr_construction_ss_visitor`
(`CGAL/Surface_sweep_2/Arr_construction_ss_visitor.h`) → `Arr_accessor::create_vertex`,
`insert_in_face_interior_ex`, `insert_from_vertex_ex`, `insert_at_vertices_ex`,
`relocate_in_new_face`.

**(a) into an EMPTY arrangement** — 4 segments (triangle + a chord crossing two of its edges),
38 callbacks, **one** bracket:

```
 0  before_global_change                                    [V=0 E=0 F=1]
 1-8    create_vertex(0,0) / create_vertex(2.5,4) / create_edge / add_inner_ccb   (first component)
 9-12   create_vertex(0,4) / create_edge
13-16   create_vertex(5,8) / create_edge
17-20   create_vertex(7.5,4) / create_edge
21      before_create_edge(c = 7.5 4 5 8, v4, v3)
22        before_split_face(f0 {unb=1, outer=0, #in=1}, newedge = h8 {face=<CRASH>})  [V=5 E=5 F=1]
23        after_split_face(f0, NEW = f1 {unb=0}, is_hole = TRUE)                      [V=5 E=5 F=2]
24      after_create_edge(h9 {face=f1})
25-28   create_vertex(10,0) / create_edge
29      before_create_edge(c = 10 0 7.5 4, v5, v4)
30        before_split_face(f0, newedge = h12 {face=<CRASH>})                         [V=6 E=7 F=2]
31        after_split_face(f0, NEW = f2 {unb=0}, is_hole = TRUE)                      [V=6 E=7 F=3]
32      after_create_edge(h13 {face=f2})
33-36   create_vertex(10,4) / create_edge
37  after_global_change                                     [V=7 E=8 F=3]
```

**No `before/after_split_edge` at all** — the sweep computes the arrangement of the *whole* input
first and creates already-split edges. Every geometric intersection therefore appears as an
ordinary `create_vertex` + several `create_edge`s.

**(b) into a NON-EMPTY arrangement** — same 2 new segments over an existing triangle: identical
callback vocabulary **plus** `before/after_split_edge` for the pre-existing edges that get cut:

```
 0  before_global_change                                    [V=3 E=3 F=2]
 1-4    create_vertex(2.5,4); split_edge(h0 -> h0 + h4)      <- existing edge split
 5-8    create_vertex(0,4);   create_edge
 9-12   create_vertex(7.5,4); split_edge(h2 -> h2 + h9)      <- existing edge split
13      before_create_edge(c = 2.5 4 7.5 4, v4, v0)
14        before_split_face(f0 {unb=0}, newedge = h11)                                [V=6 E=7 F=2]
15        after_split_face(f0, NEW = f2, is_hole = false)                             [V=6 E=7 F=3]
16      after_create_edge(h12 {face=f2})
17-24   create_vertex ×2; create_edge; add_inner_ccb          <- the disjoint 2nd segment
25-28   create_vertex(10,4); create_edge
29  after_global_change                                     [V=9 E=9 F=3]
```

**Answers to the explicit questions:** the sweep path fires the *full* set of local DCEL callbacks
(`create_vertex`, `create_edge`, `split_edge`, `split_face`, `add_inner_ccb`, plus
`move_inner_ccb`/`move_isolated_vertex` via `relocate_in_new_face`); it fires **no** callback the
incremental path does not; and `before_global_change`/`after_global_change` brackets the **entire
call**, not each curve. **[verified]**

### 8.7 Scenario 5 — `CGAL::insert(arr, non_x_monotone_curve)` — **[verified]**

`Arr_polyline_traits_2<Arr_segment_traits_2<Epeck>>`, zig-zag `(0,0) → (5,-5) → (2,-8) → (9,-9)`
(x is not monotone: 0 → 5 → 2 → 9, so `make_x_monotone_2` yields 3 pieces):

```
 0  before_global_change  [V=0 E=0 F=1]      <- piece 1
 1    before_create_vertex(0 0)   / after_create_vertex(v0)
 3    before_create_vertex(5 -5)  / after_create_vertex(v1)
 5    before_create_edge(c = 1 0 0 5 -5, v0, v1) / after_create_edge(h0)
 7    before_add_inner_ccb(f0, h0) / after_add_inner_ccb(ccb@h0)
 9  after_global_change   [V=2 E=1 F=1]
10  before_global_change  [V=2 E=1 F=1]      <- piece 2
11    create_vertex(2 -8); create_edge(c = 1 5 -5 2 -8, v1, v2)
15  after_global_change   [V=3 E=2 F=1]
16  before_global_change  [V=3 E=2 F=1]      <- piece 3
17    create_vertex(9 -9); create_edge(c = 1 2 -8 9 -9, v2, v3)
21  after_global_change   [V=4 E=3 F=1]
```

Three brackets for one `insert` call — the doc's claim confirmed. A curve that turns out to have a
single x-monotone piece gives a single bracket. Note the second and third pieces do **not** emit
`add_inner_ccb`: they attach to the vertex the previous piece created.

### 8.8 Scenario 6 — `CGAL::insert_point(arr, p)` — **[verified]**

**(a) `p` in a face interior** (square present, `p = (5,5)`):

```
0  before_global_change                       [V=4 E=4 F=2]
1    before_create_vertex(p = 5 5)
2    after_create_vertex(v0 {iso=0, openb=0, p=5 5})    <- is_isolated() is still FALSE here
3    before_add_isolated_vertex(f0 {unb=0, outer=1, #in=0, #iso=0}, v0)
4    after_add_isolated_vertex(v0 {iso=1})              <- now is_isolated() == true
5  after_global_change                        [V=5 E=4 F=2]
```

`v->face()` has a `CGAL_precondition(is_isolated())`, so it is only legal from
`after_add_isolated_vertex` onwards. Read the containing face from
`before_add_isolated_vertex`'s `f` argument instead.

**(b) `p` on an edge** (`p = (5,0)`): a plain edge split, **no** isolated-vertex callbacks:

```
0  before_global_change                       [V=4 E=4 F=2]
1    before_create_vertex(p = 5 0) / after_create_vertex(v0)
3    before_split_edge(h0 {cv=0 0 10 0}, v0, c1 = 5 0 10 0, c2 = 0 0 5 0)
4    after_split_edge(h0 {cv=5 0 10 0}, h4 {cv=0 0 5 0})
5  after_global_change                        [V=5 E=5 F=2]
```

**(c) `p` on an existing vertex** (`p = (0,0)`): **no** creation at all — a *modify*:

```
0  before_global_change                       [V=4 E=4 F=2]
1    before_modify_vertex(v0 {p=0 0, deg=2}, p = 0 0)
2    after_modify_vertex(v0)
3  after_global_change                        [V=4 E=4 F=2]
```

So `insert_point` on an existing vertex is *not* a no-op from the observer's point of view: it
routes through `Arrangement_on_surface_2::modify_vertex`, replacing the stored point with an
equal one. A data-inheriting observer must **not** reset vertex data in `after_modify_vertex`.

### 8.9 Scenario 7 — `CGAL::remove_edge` merging two faces **and** two edges — **[verified]**

Square split by a vertical chord `(5,0)–(5,10)`; removing the chord merges the two halves and then
merges the two collinear edge pairs at the now-degree-2 vertices `(5,10)` and `(5,0)`:

```
 0  before_global_change                                              [V=6 E=7 F=3]
 1    before_remove_edge(h0 {cv=5 0 5 10, src=v0(5,10), tgt=v1(5,0), face=f0})
 2      before_merge_face(f1 = f0 {unb=0, #in=0}, f2 = f1 {unb=0, #in=0},
                          e = h0)                                     [V=6 E=7 F=3]
 3      after_merge_face(f0)                                          [V=6 E=7 F=2]   <- F dropped
 4    after_remove_edge()                                             [V=6 E=6 F=2]   <- E dropped
 5    before_merge_edge(h4 {cv=5 10 0 10, tgt=v0}, h2 {cv=10 10 5 10, tgt=v0},
                        c = 10 10 0 10)
 6      before_remove_vertex(v0 {p=5 10, deg=2})
 7      after_remove_vertex()                                         [V=5 E=6 F=2]
 8    after_merge_edge(h4 {cv=10 10 0 10, src=v2, tgt=v3})            <- h4 == first arg's object
 9    before_merge_edge(h10 {cv=5 0 10 0, tgt=v1}, h12 {cv=0 0 5 0, tgt=v1},
                        c = 0 0 10 0)
10      before_remove_vertex(v1 {p=5 0, deg=2})
11      after_remove_vertex()                                         [V=4 E=5 F=2]
12    after_merge_edge(h10 {cv=0 0 10 0})
13  after_global_change                                               [V=4 E=4 F=2]
```

Structure to note: `before_remove_edge` … `after_remove_edge` **encloses** the whole face merge;
the *edge* merges happen **after** `after_remove_edge`, still inside the global bracket; and each
`before/after_remove_vertex` pair is **nested inside** a `before/after_merge_edge` pair.
`after_remove_edge()` and `after_remove_vertex()` take **no arguments** — everything you need
must be captured in the matching `before_`.

*Data-inheriting observer:*
* `before_remove_edge(e)` — last chance to read the dying edge's data (`e` and `e->twin()`).
* `before_merge_face(f1, f2, e)` — read **both** faces' data. Do not assume `f1` survives
  (T5).
* `after_merge_face(f)` — `f` is the survivor; combine the two stashed values and write into `f`.
  `f->is_unbounded()` may have been flipped by the merge *(source `:5020` `if (f2->is_unbounded())
  f1->set_unbounded(true);`)*.
* `before_merge_edge(e1, e2, c)` — `e1`'s edge object survives with its data; merge `e2`'s data
  into it either here or in `after_merge_edge`.

### 8.10 Scenario 8 — member `split_edge` / `merge_edge` / `remove_*` — **[verified]**

**Members never emit `before/after_global_change`.**

```
--- arr.split_edge(e, c1, c2)                        (returns e1, the ORIGINAL halfedge object)
0  before_create_vertex(p = 5 0)
1  after_create_vertex(v0)
2  before_split_edge(h0 {cv=0 0 10 0, src=v1(10,0)}, v0, c1 = 10 0 5 0, c2 = 5 0 0 0)
3  after_split_edge(h0 {cv=10 0 5 0, src=v1, tgt=v0},  h4 {cv=5 0 0 0, src=v0, tgt=v2})
   # returned halfedge's target == (5,0)                     <- i.e. e1

--- arr.merge_edge(e1, e2, c)                        (returns the surviving edge of e1)
0  before_merge_edge(h0 {cv=10 0 5 0}, h3 {cv=5 0 0 0}, c = 10 0 0 0)
1    before_remove_vertex(v1 {p=5 0, deg=2})
2    after_remove_vertex()                                   [V=4 E=5 F=2]
3  after_merge_edge(h0 {cv=10 0 0 0, src=v0, tgt=v2})        <- h0 == e1's object

--- arr.remove_isolated_vertex(v)                    (returns the containing Face_handle)
0  before_remove_vertex(v0 {iso=1, p=5 5})
1  after_remove_vertex()                                     [V=4 E=4 F=2]

--- CGAL::remove_vertex(arr, v) on a degree-2 vertex  (GLOBAL: brackets, and merges)
0  before_global_change                                      [V=5 E=5 F=2]
1    before_merge_edge(h0 {cv=0 0 5 0}, h4 {cv=5 0 10 0}, c = 0 0 10 0)
2      before_remove_vertex(v1 {p=5 0, deg=2})
3      after_remove_vertex()                                 [V=4 E=5 F=2]
4    after_merge_edge(h0 {cv=0 0 10 0})
5  after_global_change                                       [V=4 E=4 F=2]

--- arr.remove_edge(e, true, true)                   (MEMBER: merges the faces, no bracket)
0  before_remove_edge(h0 {cv=0 0 10 0, face=f0})
1    before_merge_face(f1 = f0 {unb=1, outer=0, #in=1}, f2 = f1 {unb=0, outer=1}, e = h0)
2    after_merge_face(f0 {unb=1})                            [V=4 E=4 F=1]
3  after_remove_edge()                                       [V=4 E=3 F=1]
```

`arr.remove_edge(e, remove_source, remove_target)` also emits `before/after_remove_vertex` for each
end that becomes isolated when the corresponding flag is `true`, and
`before/after_remove_inner_ccb` when the removed edge was an isolated antenna (§8.14 D).

### 8.11 Scenario 9 — `CGAL::overlay(a, b, res, ovl)` — **[verified]**

`a` = square `[0,10]²`, `b` = square `[5,15]²`, plain `Arr_default_overlay_traits`. Observer
attached to `res`. `**` marks the overlay-traits `create_*` calls; everything else is the observer
on `res`. 84 callbacks; the interesting parts verbatim:

```
 0  before_clear                     [V=0 E=0 F=1]      <- overlay() calls res.clear() first
 1  after_clear                      [V=0 E=0 F=1]
      (NO before_global_change anywhere in the whole call)
 2  before_create_vertex(p = 0 0)
 3  after_create_vertex(v0)
 4  before_create_vertex(p = 0 10)
 5  after_create_vertex(v1)
 6  before_create_edge(c = 0 10 0 0, v0, v1)
 7  after_create_edge(h0)
 8  before_add_inner_ccb(f0 {unb=1}, h0)
 9  after_add_inner_ccb(ccb@h0)
10  **create_vertex(V_A, F_B -> v0)                     <- traits called AFTER the DCEL callbacks
11  **create_vertex(V_A, F_B -> v1)
12  **create_edge(E_A, F_B -> h1)                       <- h1 == h0->twin()
...
22  **create_vertex(E_A, E_B -> v3)   [intersection]
...
24  before_create_edge(c = 5 10 0 10, v3, v1)
25    before_merge_inner_ccb(f0 {unb=1}, ccb@h2, ccb@h0, e = h4)
26    after_merge_inner_ccb(f0, ccb@h4)
27  after_create_edge(h5)
28  **create_edge(E_A, F_B -> h5)
...
47  before_create_edge(c = 5 5 10 5, v6, v2)
48    before_split_face(f0 {unb=1}, newedge = h12)      [V=7 E=7 F=1]
49    after_split_face(f0, NEW = f1 {unb=0}, is_hole = TRUE)
                                                        [V=7 E=7 F=2] {old:LINKED new:LINKED}
50  after_create_edge(h13)
51  **create_edge(F_A, E_B -> h13)
52  **create_face(F_A, F_B -> f1 {unb=0})               [V=7 E=7 F=2] {linked:LINKED}
...
60    before_split_face(f0, newedge = h16)              [V=8 E=9 F=2]
61    after_split_face(f0, NEW = f2, is_hole = TRUE)    [V=8 E=9 F=3]
62  after_create_edge(h17)
63  **create_edge(E_A, F_B -> h17)
64  **create_face(F_A, F_B -> f2)                       [V=8 E=9 F=3]
...
78    before_split_face(f0, newedge = h22)              [V=10 E=12 F=3]
79    after_split_face(f0, NEW = f3, is_hole = TRUE)    [V=10 E=12 F=4]
80  after_create_edge(h23)
81  **create_edge(F_A, E_B -> h23)
82  **create_face(F_A, F_B -> f3)                       [V=10 E=12 F=4]
83  **create_face(F_A, F_B -> f0 {unb=1})               <- the UNBOUNDED face, dead last
```

Reading of the interleaving, **[verified]**:

* Per vertex: `before_create_vertex` → `after_create_vertex` → `**create_vertex(...)`.
* Per edge: `before_create_edge` → [any CCB/face callbacks] → `after_create_edge` →
  `**create_edge(...)`.
* Per face: `before_split_face` → `after_split_face` → `after_create_edge` → `**create_edge` →
  `**create_face`. **`create_face` for a given face always fires *after* that face's
  `after_split_face`** — so if you use both an observer and overlay traits, the observer's
  face-copy runs first and the traits' `create_face` runs second and wins.
* The result's unbounded face gets its `**create_face` as the very **last** callback of the whole
  `overlay` call.
* The `**create_edge`/`**create_vertex` handle is the *same* DCEL object the preceding
  `after_create_*` reported (`h13` in both at [50]/[51]); for the very first edge the traits got
  `h1` where the observer got `h0` — i.e. it may be either halfedge of the pair.
* Following the `overlay`, the observer on `res` is **still attached** and fires normally
  (verified with a subsequent `CGAL::insert(res, …)` producing a full bracket). `clear()` does not
  detach observers.

*Data-inheriting observer with overlay*: don't. Use the `OverlayTraits`' 10 `create_*` functions —
they are the only place you get the corresponding input handles. An observer on `res` sees the
construction but has no way to know which input cells a result cell came from.

### 8.12 Scenario 10 — unbounded arrangement, fictitious edges — **[verified]**

`Arr_linear_traits_2<Epeck>` (⇒ `Arr_unb_planar_topology_traits_2`). An empty such arrangement
already reports `V=0 E=0 F=1`, `number_of_unbounded_faces() == 1` — the four fictitious halfedge
pairs and the fictitious face are not counted.

**Inserting a line `y = 0`:**

```
 0  before_global_change                                                [V=0 E=0 F=1]
 1    before_create_boundary_vertex[CV](cv = L 0 1 0, end = MIN,
                                        ps_x = 0 (LEFT), ps_y = 4 (INTERIOR))
 2    after_create_boundary_vertex(v0 {openb=1})           <- v0->point() is ILLEGAL
 3    before_split_fictitious_edge(h0 {fict=1, src=v1, tgt=v2, face=f0}, v0)
 4    after_split_fictitious_edge(h0 {fict=1, src=v1, tgt=v0},  h4 {fict=1, src=v0, tgt=v2})
 5    before_create_boundary_vertex[CV](cv = L 0 1 0, end = MAX,
                                        ps_x = 1 (RIGHT), ps_y = 4 (INTERIOR))
 6    after_create_boundary_vertex(v3 {openb=1})
 7    before_split_fictitious_edge(h6 {fict=1}, v3)
 8    after_split_fictitious_edge(h6 {fict=1}, h8 {fict=1})
 9    before_create_edge(c = L 0 1 0, v0 {openb=1, deg=2}, v3 {openb=1, deg=2})
10      before_split_face(f0 {unb=1, outer=1, #in=0}, newedge = h10 {face=<CRASH>})
                                                                        [V=0 E=1 F=1]
11      after_split_face(f0 {unb=1, outer=1}, NEW = f1 {unb=1, outer=1}, is_hole = false)
                                                                        [V=0 E=1 F=2]
12    after_create_edge(h11 {face=f1})
13  after_global_change                                                 [V=0 E=1 F=2]
```

* **Only the curve-end overload** `before_create_boundary_vertex(const X_monotone_curve_2& cv,
  Arr_curve_end, Arr_parameter_space, Arr_parameter_space)` fires here; the point-based overload
  did not fire in any scenario tested.
* `Arr_parameter_space` prints as its underlying `Box_parameter_space_2` value:
  `LEFT=0, RIGHT=1, BOTTOM=2, TOP=3, INTERIOR=4`.
* `after_split_fictitious_edge(e1, e2)` obeys the same rule as the real split: **`e1` is the
  original halfedge object**, `e2` is new.
* Unlike the bounded topology, unbounded faces here **do** have an outer CCB (of fictitious +
  real halfedges), so `is_hole == false` for a line that separates two unbounded faces.
* A **ray** produces one `create_boundary_vertex` + one `split_fictitious_edge` and one ordinary
  `create_vertex` for the finite end; no face split (it is an antenna).

**Removing that line — the fictitious *merges*:**

```
 0  before_global_change                                                [V=0 E=1 F=2]
 1    before_remove_edge(h0 {fict=0, cv = L 0 1 0, src=v0, tgt=v1})
 2      before_merge_face(f1 = f0 {unb=1, outer=1}, f2 = f1 {unb=1, outer=1}, e = h0)
 3      after_merge_face(f0 {unb=1})                                    [V=0 E=1 F=1]
 4    after_remove_edge()                                               [V=0 E=0 F=1]
 5    before_merge_fictitious_edge(h4 {fict=1, tgt=v1}, h7 {fict=1, tgt=v1})
 6    after_merge_fictitious_edge(h4 {fict=1, src=v2, tgt=v3})
 7    before_remove_vertex(v1 {openb=1, deg=2})
 8    after_remove_vertex()                                             [V=0 E=0 F=1]
 9    before_merge_fictitious_edge(h10 {fict=1, tgt=v0}, h2 {fict=1, tgt=v0})
10    after_merge_fictitious_edge(h10 {fict=1})
11    before_remove_vertex(v0 {openb=1, deg=2})
12    after_remove_vertex()                                             [V=0 E=0 F=1]
13  after_global_change                                                 [V=0 E=0 F=1]
```

`before/after_merge_fictitious_edge` fires **after** `after_remove_edge`, once per boundary
vertex being retired, each immediately followed by that vertex's
`before/after_remove_vertex`. The counts do not change across these because vertices at infinity
and fictitious edges are not counted.

**Guards for a data observer in the unbounded topology:** always test `e->is_fictitious()` before
`e->curve()` and `v->is_at_open_boundary()` before `v->point()` — both accessors are unchecked and
will read a null pointer otherwise. `f->is_fictitious()` marks the single fictitious face.

### 8.13 Scenario 11 — `Polygon_set_2::join` — **[verified]**

```
# arrangement() address before join: 0x1014d6210   V=4 E=4 F=2
Tracer attached to ps.arrangement()
ps.join(box(5,5,15,15));
# arrangement() address after join:  0x1014d89f0   (DIFFERENT object!)
# observer->arrangement() = 0                      (nullptr)

trace on the observer, complete:
  0  before_detach
  1  after_detach
# after join: V=8 E=8 F=2
second ps.join(...)  -> 0 callbacks
```

**The observer does not survive a binary Boolean operation.** `Gps_on_surface_base_2` builds the
result in a **fresh** `Aos_2` and `delete`s the old one *(source `Gps_on_surface_base_2.h:1482-1488`
and the identical shape at `:1547`, `:1623`, `:1690`)*; the old arrangement's destructor detaches
every attached observer (T13), so you get `before_detach`/`after_detach` and
`arrangement() == nullptr` — safe, but silent. This is BSO gotcha 3 in
`boolean_set_operations.md`, seen from the observer side.

Practical rules:
* `ps.insert(polygon)` mutates the existing arrangement in place → an attached observer keeps
  working.
* `join`, `intersection`, `difference`, `symmetric_difference`, `complement(other)` and the
  `*_polygons_*` aggregated forms replace it → **re-`attach()` to `ps.arrangement()` after every
  such call.**
* Detect it cheaply: `obs.arrangement() == nullptr` (or `!= &ps.arrangement()`) means you were
  detached.
* Never `detach()` an observer whose `arrangement()` is `nullptr` — that is already a no-op, but do
  not pass it a *different* arrangement expecting the old registration to move.

### 8.14 Which operation fires the rarer callbacks — **[verified]** unless noted

| callback pair | triggered by (verified) |
|---|---|
| `add_inner_ccb` | inserting a curve that forms a new isolated component (an "antenna") in a face — `insert`, `insert_non_intersecting_curve`, aggregated sweep, overlay |
| `add_isolated_vertex` | `insert_point` in a face interior |
| `move_isolated_vertex` | a newly closed cycle encloses an existing isolated vertex; fires **after** `after_create_edge`, inside the same global bracket |
| `move_inner_ccb` | (a) a newly closed cycle encloses a whole existing component — after `after_create_edge`; (b) `remove_edge` merging a face into another — **inside** the `before_merge_face`/`after_merge_face` bracket, batched (T15) |
| `merge_inner_ccb` | inserting an edge that joins two different inner CCBs of the same face (bounded or unbounded); nested inside `before_create_edge`…`after_create_edge` |
| `split_inner_ccb` | removing such a "bridge" edge; nested inside `before_remove_edge`…`after_remove_edge` |
| `remove_inner_ccb` | (a) removing the last edge of an isolated component; (b) inserting an edge that connects an inner CCB to the **outer** CCB of the same face — nested inside `before_create_edge` |
| `split_fictitious_edge` / `merge_fictitious_edge` | only with `Arr_unb_planar_topology_traits_2` (`Arr_linear_traits_2`, or any traits with unbounded curves) — §8.12 |
| `modify_vertex` | `insert_point` on an existing vertex; `arr.modify_vertex` |
| `modify_edge` | `arr.modify_edge` only |
| `assign` | `arr.assign(src)` (preceded by `before_clear`/`after_clear`) |
| `split_outer_ccb` | *(source `:4805`)* removing an edge whose two halfedges lie on the same **outer** CCB and the CCB splits — needs a face with more than one outer CCB. **Not reachable** with the planar bounded topology; not observed in the unbounded-planar tests either |
| `merge_outer_ccb` | *(source `:3135`)* inserting an edge between two **different outer CCBs of the same face** — same caveat |
| `add_outer_ccb` | *(source `:2875`, `:2896`, `:3005`)*; the `:3005` site carries the comment *"This case can only occur in identification topologies"* |
| `move_outer_ccb` | *(source `:3082`)* inside `_split_face`'s `is_hole == true` branch, and `:1929` |
| `remove_outer_ccb` | *(source `:4720`, `:4963`, `:4973`)* inside `_remove_edge` |
| `create_boundary_vertex` (point overload) | not fired by any traits tested; the curve-end overload is what `Arr_linear_traits_2` uses |

Concrete traces for the reachable ones:

```
--- inserting a BRIDGE between two inner ccbs of the same face
0  before_global_change                                    [V=8 E=8 F=3]
1    before_create_edge(c = 4 0 10 0, v0, v1)
2      before_merge_inner_ccb(f0 {unb=1}, ccb@h0, ccb@h1, e = h2)
3      after_merge_inner_ccb(f0, ccb@h2)
4    after_create_edge(h3)
5  after_global_change                                     [V=8 E=9 F=3]

--- removing that bridge again
0  before_remove_edge(h0 {cv = 4 0 10 0})
1    before_split_inner_ccb(f0 {unb=1}, ccb@h1, e = h2)
2    after_split_inner_ccb(f0, ccb@h3, ccb@h4)
3  after_remove_edge()                                     [V=8 E=8 F=3]

--- connecting an inner ccb to the OUTER ccb of the containing (bounded) face
0  before_global_change                                    [V=6 E=5 F=2]
1    before_create_edge(c = 0 0 4 5, v0, v1)
2      before_remove_inner_ccb(f0 {unb=0}, ccb@h0)
3      after_remove_inner_ccb(f0)
4    after_create_edge(h1)
5  after_global_change                                     [V=6 E=6 F=2]

--- removing the only edge of the arrangement (an antenna)
0  before_remove_edge(h0 {cv = 0 0 10 0})
1    before_remove_inner_ccb(f0 {unb=1}, ccb@h0)
2    after_remove_inner_ccb(f0)
3  after_remove_edge()                                     [V=2 E=0 F=1]
4  before_remove_vertex(v0) / after_remove_vertex()        [V=1 E=0 F=1]
6  before_remove_vertex(v1) / after_remove_vertex()        [V=0 E=0 F=1]

--- closing a cycle around an isolated vertex
0  before_global_change                                    [V=5 E=3 F=1]
1    before_create_edge(c = 0 10 0 0, v0, v1)
2      before_split_face(f0 {unb=1}, newedge = h0)         [V=5 E=4 F=1]
3      after_split_face(f0, NEW = f1 {unb=0}, is_hole = TRUE)
4    after_create_edge(h1)
5    before_move_isolated_vertex(from = f0, to = f1, v2 {iso=1, p=5 5})
6    after_move_isolated_vertex(v2)                        <- OUTSIDE the create_edge pair
7  after_global_change                                     [V=5 E=4 F=2]

--- merge_face where the survivor is the SECOND argument (T5)
   (unbounded face has 1 inner ccb = a square; the square has 2 inner ccbs = 2 triangles)
0  before_remove_edge(h0 {cv = 0 0 20 0, face = f0})
1    before_merge_face(f1 = f0 {unb=0, outer=1, #in=2},
                       f2 = f1 {unb=1, outer=0, #in=1}, e = h0)   [V=10 E=10 F=4]
2      before_move_inner_ccb(from = f0, to = f1, ccb@h4)
3      before_move_inner_ccb(from = f0, to = f1, ccb@h5)      <- both BEFOREs first
4      after_move_inner_ccb(ccb@h4)
5      after_move_inner_ccb(ccb@h5)
6    after_merge_face(f1 {unb=1, outer=0, #in=3})              <- the SECOND argument survived
                                                              [V=10 E=10 F=3]
7  after_remove_edge()                                         [V=10 E=9 F=3]
```

### 8.15 Dereferenceability of handle arguments — **[verified] by SIGSEGV probe**

Answering the question directly: **no, not every accessor is legal inside every `before_*`.**
Handles are always *valid iterators* (`&*h` is always a good pointer, and the object exists), but
some of the object's own accessors read pointers that have not been wired up yet.

| callback | argument | safe | **CRASHES** |
|---|---|---|---|
| `before_split_face(f, e)` | `e` | `is_fictitious()`, `curve()`, `source()`, `target()`, `twin()`, `prev()`, `next()` | **`e->face()`**, **`e->twin()->face()`** |
| `before_split_face(f, e)` | `f` | everything (`has_outer_ccb()`, `outer_ccb()` walk, inner-CCB and isolated-vertex iteration, `is_unbounded()`) | — |
| `after_create_vertex(v)`, `before_add_isolated_vertex(f, v)`, `before_split_edge(e, v, …)` (the *new* `v`), `before_create_edge(c, v1, v2)` (a *new* endpoint) | `v` | `point()`, `is_at_open_boundary()`, `is_isolated()` (returns **false** for a not-yet-linked vertex), `degree()` (null-checked, returns 0) | **`v->incident_halfedges()`** (unchecked; asserts only `!is_isolated()`), `v->face()` (`CGAL_precondition(is_isolated())`) |
| `after_create_boundary_vertex(v)` | `v` | `is_at_open_boundary()` (true) | **`v->point()`** — a boundary vertex has a null point |
| any callback with a fictitious halfedge | `e` | `is_fictitious()`, incidence | **`e->curve()`** |
| `before_split_edge(e, v, c1, c2)` | `e` | everything, including `face()` — `e` is fully linked, still carrying the **old** curve | — |
| `after_split_edge(e1, e2)` | both | everything | — |
| `before_create_edge`, `before_merge_edge`, `before_merge_face`, `before_remove_edge`, `before_remove_vertex`, `before_move_*`, `before_add_*`, `before_split_*_ccb`, `before_merge_*_ccb`, `before_remove_*_ccb` | all | everything tried was safe, including full CCB walks and `arrangement()->faces_begin()..end()` | — |
| every `after_*` | all | everything tried was safe | — |

Additional verified facts:

* **Is the new face already in `faces_begin()..faces_end()` at `after_split_face`? YES.**
  Iterating the face list inside the callback finds both `f` and `new_f`, and
  `number_of_faces()` has already been incremented. `new_f->outer_ccb()` is a complete circulator.
  At `before_split_face`, `number_of_faces()` is still the pre-split value and `new_f` does not
  exist yet.
* `arrangement()` (and therefore `number_of_vertices()/edges()/faces()`, `faces_begin()`, …) is
  usable inside every callback tested, including mid-operation ones. The counters are updated
  eagerly, so they are a reliable "did the DCEL change yet?" signal (see the `[V= E= F=]` columns
  in every trace above).
* Circulator arguments (`Ccb_halfedge_circulator`, passed **by value**) were dereferenceable in
  every callback tested, including `after_add_inner_ccb`, `after_merge_inner_ccb`,
  `after_split_inner_ccb`, `after_move_inner_ccb`.
* Observer ordering, **re-verified with two registered observers**: `before_*` runs in
  registration order (OBS1 then OBS2), `after_*` in reverse (OBS2 then OBS1) — for
  `before/after_global_change` and for the local callbacks alike. The
  `Aos_observer(Arrangement_2&)` **constructor does NOT fire** `before_attach`/`after_attach`;
  only an explicit `attach()` does. `detach()` fires `before_detach`/`after_detach`.

### 8.16 Recipe — an extended-DCEL data-inheriting observer

Putting the traces together, this is the minimum correct set of overrides for a face/edge/vertex
data model that must survive incremental editing:

```cpp
template <class Arr>
struct Inheriting_observer : public CGAL::Arr_observer<Arr> {
  using Base = CGAL::Arr_observer<Arr>;
  using typename Base::Vertex_handle; using typename Base::Halfedge_handle;
  using typename Base::Face_handle;
  explicit Inheriting_observer(Arr& a) : Base(a) {}

  // ---- edge split: e1 IS the original object, so only e2 needs seeding -----
  void after_split_edge(Halfedge_handle e1, Halfedge_handle e2) override {
    e2->set_data(e1->data());                     // e1 kept its own data
    e2->twin()->set_data(e1->twin()->data());
  }

  // ---- edge merge: e1's edge survives; fold e2's data in --------------------
  MyEdgeData m_dying_edge, m_dying_edge_tw;
  void before_merge_edge(Halfedge_handle e1, Halfedge_handle e2,
                         const typename Base::X_monotone_curve_2&) override {
    m_dying_edge = e2->data(); m_dying_edge_tw = e2->twin()->data();
    (void)e1;                                     // e1 keeps its data automatically
  }
  void after_merge_edge(Halfedge_handle e) override {
    e->set_data(combine(e->data(), m_dying_edge));
    e->twin()->set_data(combine(e->twin()->data(), m_dying_edge_tw));
  }

  // ---- face split: stash in before_, write in after_ ------------------------
  MyFaceData m_split_src;
  void before_split_face(Face_handle f, Halfedge_handle /*e*/) override {
    m_split_src = f->data();                      // do NOT touch e->face() here
  }
  void after_split_face(Face_handle f, Face_handle new_f, bool /*is_hole*/) override {
    new_f->set_data(m_split_src);                 // f still holds it; new_f is default-built
    (void)f;
  }

  // ---- face merge: capture BOTH, decide in after_ (T5) --------------
  Face_handle m_mf1, m_mf2;  MyFaceData m_d1, m_d2;
  void before_merge_face(Face_handle f1, Face_handle f2, Halfedge_handle) override {
    m_mf1 = f1; m_mf2 = f2; m_d1 = f1->data(); m_d2 = f2->data();
  }
  void after_merge_face(Face_handle f) override {  // f may be EITHER of the two
    f->set_data(combine(m_d1, m_d2));
  }

  // ---- creation: seed defaults --------------------------------------------
  void after_create_vertex(Vertex_handle v) override { v->set_data(MyVertexData{}); }
  void after_create_boundary_vertex(Vertex_handle v) override { v->set_data(MyVertexData{}); }
  void after_create_edge(Halfedge_handle e) override {
    // e->face() is the NEW face after a split; e->twin()->face() the old one
    e->set_data(MyEdgeData{}); e->twin()->set_data(MyEdgeData{});
  }

  // ---- deletions: last chance to read --------------------------------------
  void before_remove_vertex(Vertex_handle v) override { record(v->data()); }
  void before_remove_edge(Halfedge_handle e) override { record(e->data()); }
  // after_remove_vertex() / after_remove_edge() take NO arguments.
};
```

Constraints this respects, all **[verified]** above:

1. `before_split_face` reads `f`'s old data but must not call `e->face()`.
2. `after_split_face` is the only point where both faces exist and `f` still holds the old value.
3. `after_split_edge`'s `e1` needs nothing — it *is* the old edge.
4. `after_merge_face`'s argument may be `f2`, not `f1`.
5. `after_remove_vertex()` / `after_remove_edge()` take no arguments.
6. During `overlay()` none of this runs the way you want — use the `OverlayTraits` instead, and
   remember its `create_face` fires **after** the observer's `after_split_face` and therefore
   overwrites it.
7. After any `Polygon_set_2` binary operation, `attach()` again.
