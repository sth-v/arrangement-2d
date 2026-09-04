# CGAL 6.1 — Point location, vertical ray shooting, batched PL, vertical decomposition

Source of truth: the **installed** headers under `/opt/homebrew/include/CGAL` (CGAL 6.1, header-only,
`$Id: … b26b07a1242 $`). Every signature below is quoted verbatim from those files. Claims marked
**[verified]** were confirmed by compiling and running a test program with
`/usr/bin/clang++ -std=c++17 -DCGAL_USE_CORE -DCGAL_USE_GMP -DCGAL_USE_MPFR -I/opt/homebrew/include`.

Headers covered:

| Header | Content |
|---|---|
| `CGAL/Arr_point_location_result.h` | `Arr_point_location_result<Arrangement>` (the variant/optional plumbing) |
| `CGAL/Arr_naive_point_location.h` | `Arr_naive_point_location` |
| `CGAL/Arr_simple_point_location.h` | `Arr_simple_point_location` |
| `CGAL/Arr_walk_along_line_point_location.h` | `Arr_walk_along_line_point_location` |
| `CGAL/Arr_landmarks_point_location.h` | `Arr_landmarks_point_location` |
| `CGAL/Arr_point_location/Arr_lm_generator_base.h` | `Arr_landmarks_generator_base` |
| `CGAL/Arr_point_location/Arr_lm_vertices_generator.h` | `Arr_landmarks_vertices_generator` (default) |
| `CGAL/Arr_point_location/Arr_lm_grid_generator.h` | `Arr_grid_landmarks_generator` |
| `CGAL/Arr_point_location/Arr_lm_random_generator.h` | `Arr_random_landmarks_generator` |
| `CGAL/Arr_point_location/Arr_lm_halton_generator.h` | `Arr_halton_landmarks_generator` |
| `CGAL/Arr_point_location/Arr_lm_middle_edges_generator.h` | `Arr_middle_edges_landmarks_generator` |
| `CGAL/Arr_point_location/Arr_lm_specified_points_generator.h` | `Arr_landmarks_specified_points_generator` |
| `CGAL/Arr_point_location/Arr_lm_nearest_neighbor.h` | `Arr_landmarks_nearest_neighbor`, `NN_Point_2` |
| `CGAL/Arr_trapezoid_ric_point_location.h` | `Arr_trapezoid_ric_point_location` |
| `CGAL/Arr_triangulation_point_location.h` | `Arr_triangulation_point_location` |
| `CGAL/Arr_batched_point_location.h` | free function `CGAL::locate(arr, first, last, oi)` |
| `CGAL/Arr_vertical_decomposition_2.h` | free function `CGAL::decompose(arr, oi)` |

---

## Gotchas / surprises vs. older CGAL

1. **No `CGAL::Object` anywhere.** The universal result type is
   `std::variant<Vertex_const_handle, Halfedge_const_handle, Face_const_handle>`
   (`Arr_point_location_result<Arr>::Type`, aliased as `Result_type` and `result_type` on every
   strategy). Discriminate with `std::get_if<T>(&res)` / `res.index()`
   (`0 = Vertex`, `1 = Halfedge`, `2 = Face`). `CGAL::assign(obj, x)` is gone; the replacement is the
   static `Arr_point_location_result<Arr>::assign<T>(const Type*)` which is literally
   `std::get_if<T>`.

2. **`ray_shoot_up` / `ray_shoot_down` exist on only three of the six strategies.** **[verified]**
   `Arr_simple_point_location`, `Arr_walk_along_line_point_location`, and
   `Arr_trapezoid_ric_point_location` have them.
   `Arr_naive_point_location`, `Arr_landmarks_point_location`, and
   `Arr_triangulation_point_location` **do not** — not even a protected one. In older CGAL,
   `Arr_naive_point_location` did offer vertical ray shooting; **it was removed**. A type-erased
   C++ core must therefore model "supports ray shooting" as a *capability flag*, not assume it.

3. **Ray shooting never returns "nothing"; it returns a `Face_const_handle`.** The doc comments still
   say *"This object is either an empty object or a Halfedge_const_handle or a
   Vertex_const_handle"* — that comment is stale. All three implementations return the **same**
   `std::variant<V,H,F>` as `locate()`, and when the ray escapes they return the *initial / unbounded*
   face (`Arr_simple_point_location_impl.h:365`, `Arr_walk_along_line_pl_impl.h:368/385`,
   `Arr_trapezoid_ric_pl_impl.h:381`). **[verified]** Only the internal
   `Arr_simple_point_location::_base_vertical_ray_shoot` (protected) uses `std::optional`.

4. **Observer-backed strategies take `attach(Base_aos&)` — a NON-const reference — and their
   `Arrangement_2` typedef may not be what you think.** **[verified]**
   `Arr_trapezoid_ric_point_location` and `Arr_triangulation_point_location` derive from
   `Arrangement_::Observer` (= `CGAL::Aos_observer<Arrangement_::Base_aos>`), so `attach` is the
   inherited `void attach(Base_aos& arr)`. `attach(const Arr&)` **does not compile** for them, while it
   is the only form for naive/simple/walk/landmarks.
   Worse: `Arr_trapezoid_ric_point_location<Arr>::Arrangement_2` is **`Arr::Base_aos`**
   (inherited from `Aos_observer`), *not* `Arr` — it never redeclares the name. Generic code that
   writes `typename PL::Arrangement_2` silently gets the base class. `Arr_triangulation_point_location`
   *does* redeclare it, so there it *is* `Arr`.

5. **Four of the six landmark generators do not compile against `CGAL::Arrangement_2<...>`.**
   **[verified]** `Arr_grid_landmarks_generator`, `Arr_random_landmarks_generator`,
   `Arr_halton_landmarks_generator`, and `Arr_middle_edges_landmarks_generator` write
   `Arrangement_2* arr = this->arrangement();` where their own `Arrangement_2` is the derived
   `Arrangement_2<Traits>` but `Aos_observer::arrangement()` returns `Base_aos*`. Instantiate them
   with **`Arr::Base_aos`** instead (`Arr_grid_landmarks_generator<Arr::Base_aos>`) — that compiles
   and runs, and the resulting generator can still be passed to
   `Arr_landmarks_point_location<Arr, Arr_grid_landmarks_generator<Arr::Base_aos>>`.
   Only `Arr_landmarks_vertices_generator` (the default) and
   `Arr_landmarks_specified_points_generator` compile with `Arr` directly.

6. **`CGAL::decompose` output is a 3-level nest with `std::optional` on the inside.** Exactly:
   `std::pair<Vertex_const_handle, std::pair<Vert_type, Vert_type>>` with
   `Vert_type = std::optional<std::variant<Vertex_const_handle, Halfedge_const_handle, Face_const_handle>>`.
   `first` = below, `second` = above. **[verified by compiling and running.]**

7. **In an *unbounded* arrangement, `decompose` reports *fictitious halfedges*, not faces and not
   `nullopt`, for "nothing above/below".** **[verified]** The bounded-planar helper returns
   `Vert_type(unbounded_face)` (a `Face_const_handle`), but the unbounded-planar helper returns
   `Vert_type(m_top_fict)` / `Vert_type(m_bottom_fict)` — `Halfedge_const_handle`s for which
   `he->is_fictitious() == true`. Binding code must test `is_fictitious()` before touching
   `he->curve()`. `std::nullopt` appears only in the *other* case: two vertices with equal
   x that are joined by a vertical edge "see" each other as empty.

8. **`CGAL::locate` (batched) writes `std::pair<Point_2, std::variant<...>>`, not
   `std::pair<Point_2, std::optional<std::variant<...>>>`,** despite what the `\pre` comment in
   `Arr_batched_point_location.h` says. The visitor does
   `*m_out++ = std::make_pair(event->point().base(), pl_make_result(vh));`. Both output-iterator
   value types work in practice (optional is implicitly constructible from the variant) —
   **[both verified]** — but CGAL's own callers (`Arr_lm_generator_base::_create_nn_points_set`) use
   `std::vector<std::pair<Point_2, PL_result_type>>` i.e. the **non-optional** form.

9. **`Arr_triangulation_point_location` hard-crashes on unbounded arrangements.** **[verified]**
   `build_triangulation()` calls `vh->point()` on every edge endpoint; a vertex at infinity has a null
   point and trips `CGAL error: assertion violation! Expression : p_pt != nullptr`
   (`Arr_dcel_base.h:105`). It also needs `Geometry_traits_2::Kernel` and
   `static_cast<Kernel::Point_2>(Point_2)`, and it inserts every edge into the CDT as a *straight
   constraint between its endpoints* — so it is only correct for **bounded arrangements of line
   segments**. (It compiles for `Arr_linear_traits_2`; it just fails at run time.)

10. **`Arr_landmarks_vertices_generator` rebuilds the entire kd-tree on every single local
    change.** Its `_handle_local_change_notification()` (the sqrt-of-n amortisation heuristic) is
    **dead code — never called from anywhere in the installed tree** **[verified by grep]**. The
    inherited `Arr_landmarks_generator_base::after_create_vertex/after_create_edge/…` call
    `clear_landmark_set(); build_landmark_set();` unconditionally. Incremental insertion into a
    landmark-backed arrangement is therefore O(n) tree rebuilds. Use `before_global_change()` /
    `after_global_change()` (i.e. CGAL's aggregate `insert`) or the RIC strategy for incremental work.

11. **`Arr_middle_edges_landmarks_generator` has three no-op "overrides" that override nothing:**
    `after_add_hole`, `after_move_hole`, `after_remove_hole` do not exist in `Aos_observer` (renamed
    to `after_add_inner_ccb` / `after_move_inner_ccb` / `after_remove_inner_ccb` **[verified]**), and
    none of its functions carry `override`. So inner-CCB events still trigger a full rebuild.

12. **`Arr_observer` is now just an alias.** `CGAL/Arr_observer.h` contains only
    `template <typename Arrangement_> using Arr_observer = typename Arrangement_::Observer;`.
    The real base class is `CGAL::Aos_observer<Arrangement>` in `CGAL/Aos_observer.h`.

13. `Arrangement_2` is a thin derived class: `Arrangement_2<GT, Dcel> : public
    Arrangement_on_surface_2<GT, Default_planar_topology<GT,Dcel>::Traits>`, and
    `Base_aos == Self` on the AOS, so `Arr::Base_aos == Arr::Base`. **[verified]** Free functions
    `CGAL::locate` / `CGAL::decompose` are declared on `Arrangement_on_surface_2<Gt2, Tt>` and bind to
    `Arrangement_2` by derived-to-base deduction.

14. `Arr_landmarks_point_location::attach(arr, gen)` asserts `lm_gen == nullptr`. `detach()` does
    **not** clear `lm_gen`, so re-attaching with an explicit generator after any prior attach trips the
    assertion in debug and **leaks the previously owned generator in release** (`own_gen` is
    overwritten with `false`). See §Landmarks / lifetime.

15. **`Arr_triangulation_point_location` silently returns the *unbounded face* for every query point
    inside a face that has a hole (an inner CCB).** **[verified: 6/6 mismatches vs. naive on a square
    with a square hole; no warning printed.]** Non-convex faces are fine, isolated vertices alone are
    fine — only inner CCBs break it. The cause is the triple circulator loop in
    `Arr_point_location/Arr_triangulation_pl_impl.h:155-180`, which never resets `havc1`/`havc2` and
    refuses to *select* the unbounded face while defaulting to it. Do not ship this strategy. §13.5.

16. **The landmarks strategy is unavailable for three of the seven required traits, and it is a
    *strategy* limitation, not a generator limitation.** `Arr_landmarks_point_location::locate` calls
    `traits.construct_x_monotone_curve_2_object()(landmark, query)`, so the traits must offer a
    `(Point_2, Point_2)` overload. `Arr_circle_segment_traits_2` has no
    `Construct_x_monotone_curve_2` at all, `Arr_Bezier_curve_traits_2` has neither that nor
    `Approximate_number_type`/`Approximate_2`, and `Arr_polycurve_traits_2` has the functor but not the
    two-point overload (`Arr_polyline_traits_2` works only because it *adds* one at
    `Arr_polyline_traits_2.h:470`). **[all verified by compile error.]** §13.3.

17. **Only `Arr_naive_point_location` and batched `CGAL::locate` are safe on the sphere.**
    `Arr_simple_point_location` and `Arr_walk_along_line_point_location` do **not compile**
    (`Arr_spherical_topology_traits_2` has no `initial_face`/`compare_x`/`compare_xy`);
    `Arr_trapezoid_ric_point_location` compiles and is correct only if no vertex is at a pole or on the
    identification (otherwise a `Td_traits.h:423` precondition abort, or a bare segfault);
    landmarks compiles but aborts on antipodal or meridian-aligned landmark/query pairs;
    `Arr_triangulation_point_location` does not compile. `CGAL::decompose` works only when no vertex is
    on a pole or the identification. **[all verified]** §13.3, §13.8.

18. **The halfedge you get back is an arbitrary one of the two twins, and it is not stable.**
    naive returned `#8` where walk returned `#9` for the same query
    (**[verified]** `naive == walk->twin()`), and RIC returned a *different twin before and after* a
    `detach()`/`attach()` cycle on the same arrangement. Compare edges as
    `h == g || h == g->twin()`, never as `h == g`. §13.4.

---

## 0. Capability matrix (all **[verified]** by compile-time detection)

> **Superseded / extended by §13.** This table is per-*strategy* only. For the full
> strategy × geometry-traits compile-and-run matrix (which strategy works with which of the
> seven traits, the exact compile errors, the run-time aborts, the wrong answers, ray-shooting
> results, mutation safety, multi-attach and batched-PL/`decompose` coverage) see
> **§13 — Strategy × geometry-traits compatibility matrix**.

| Strategy | `locate` | `ray_shoot_up/down` | Observer (auto-updates) | `attach` signature | Extra traits requirement |
|---|---|---|---|---|---|
| `Arr_naive_point_location` | yes | **no** | no | `attach(const Arrangement_2&)` | `Topology_traits::is_in_face` |
| `Arr_simple_point_location` | yes | **yes** | no | `attach(const Arrangement_2&)` | `Topology_traits::{Dcel, initial_face}` |
| `Arr_walk_along_line_point_location` | yes | **yes** | no | `attach(const Arrangement_2&)` | `Topology_traits::initial_face` |
| `Arr_landmarks_point_location` | yes | **no** | via its Generator (yes) | `attach(const Arrangement_2&, Generator* = nullptr)` | traits needs `Approximate_2`/`Approximate_number_type`; `Construct_x_monotone_curve_2` |
| `Arr_trapezoid_ric_point_location` | yes | **yes** | **yes** (incremental) | inherited `attach(Base_aos&)` — *non-const* | — |
| `Arr_triangulation_point_location` | yes | **no** | **yes** (full rebuild) | inherited `attach(Base_aos&)` — *non-const* | `Geometry_traits_2::Kernel`; bounded segments only |

Cost characteristics: **the headers carry no complexity annotations at all.** The figures below come
from the CGAL 6.1 user manual, not from the installed sources — treat them as documentation, not as
extracted API facts.

| Strategy | Preprocessing | Space | Query |
|---|---|---|---|
| naive | none | none | O(n) (scans all vertices, then all edges, then all faces) |
| simple | none | none | O(n) (one vertical ray shoot by scanning all CCBs) |
| walk-along-line | none | none | O(n) worst case, much better in practice |
| landmarks | O(n log n) (kd-tree over landmarks) | O(n) | very fast in practice; degrades with landmark density |
| trapezoid RIC (`with_guarantees=true`) | O(n log n) expected | O(n) expected | O(log n) expected |
| triangulation | O(n log n) CDT build, **re-done on every change** | O(n) | O(log n)-ish via `Triangulation_hierarchy_2` |

---

## 1. `CGAL::Arr_point_location_result<Arrangement_>`

`CGAL/Arr_point_location_result.h`. A pure type/utility struct — no state, all members `static`.

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
  static
  inline Type make_result(T t) { return Type(t); }

  static
  inline std::optional<Type> empty_optional_result()
  { return std::optional<Type>(); }

  template <typename T>
  static
  inline const T* assign(const Type* obj) { return std::get_if<T>(obj); }

  static
  inline Type default_result(){
    CGAL_error_msg("This functions should have never been called!");
    return Type();
  }
};
```

Notes for bindings:

* **Alternative order is fixed and load-bearing**: `index() == 0` → `Vertex_const_handle`,
  `1` → `Halfedge_const_handle`, `2` → `Face_const_handle`. Safe to switch on `index()`.
* `Type()` default-constructs to a **default-constructed `Vertex_const_handle`** (index 0), which is a
  *singular / invalid* handle. `default_result()` only ever fires through `CGAL_error_msg`, so a
  released build could hand you that singular handle — never dereference a result you obtained from a
  code path that was supposed to be unreachable.
* Handles are DCEL iterators/handles: they stay valid across unrelated modifications but are
  invalidated by removal of the referenced feature (and by `clear()` / `assign()` / destruction of the
  arrangement). Nothing here owns anything.

---

## 2. `CGAL::Arr_naive_point_location<Arrangement_>`

`CGAL/Arr_naive_point_location.h` (impl: `CGAL/Arr_point_location/Arr_naive_point_location_impl.h`).

### Template parameters

```cpp
template <class Arrangement_>
class Arr_naive_point_location
```

No defaults. `Arrangement_` must be an `Arrangement_on_surface_2<GeomTraits, TopTraits>`
instantiation (or a class derived from one, e.g. `Arrangement_2`).

### Public typedefs

```cpp
typedef Arrangement_                                   Arrangement_2;
typedef typename Arrangement_2::Geometry_traits_2      Geometry_traits_2;
typedef typename Arrangement_2::Topology_traits        Topology_traits;

typedef typename Arrangement_2::Vertex_const_handle    Vertex_const_handle;
typedef typename Arrangement_2::Halfedge_const_handle  Halfedge_const_handle;
typedef typename Arrangement_2::Face_const_handle      Face_const_handle;

typedef typename Geometry_traits_2::Point_2            Point_2;
typedef typename Geometry_traits_2::X_monotone_curve_2 X_monotone_curve_2;

typedef Arr_point_location_result<Arrangement_2>       Result;
typedef typename Result::Type                          Result_type;
typedef Result_type                                    result_type;
```

### Public member functions

```cpp
Arr_naive_point_location();                                  // p_arr = geom_traits = top_traits = nullptr
Arr_naive_point_location(const Arrangement_2& arr);          // caches geometry_traits() and topology_traits()

void attach(const Arrangement_2& arr);
void detach();                                               // nulls all three pointers

Result_type locate(const Point_2& p) const;
```

*No `ray_shoot_up` / `ray_shoot_down`.* **[verified]**

### Preconditions / behaviour

* `locate` requires an attached arrangement — there is **no** null check; calling it default-constructed
  dereferences `nullptr`.
* `locate` is a linear scan in three phases: all vertices (`equal_2`), all edges
  (`is_in_x_range_2` + `compare_y_at_x_2 == EQUAL`), then all faces via
  `top_traits->is_in_face(&*fh, p, nullptr)`, keeping the *innermost* containing face. It ends with
  `CGAL_assertion(f_inner != invalid_f)`.
* Not an observer: the object holds only a `const Arrangement_2*` and two traits pointers. It is
  automatically "up to date" because it recomputes everything per query, but the *pointers* dangle if
  the arrangement is destroyed. **You must call `detach()` (or destroy the strategy) before the
  arrangement dies.**
* Works fine for unbounded arrangements. **[verified: returns the unbounded face for a point in the
  unbounded region of a 2-line arrangement.]**

---

## 3. `CGAL::Arr_simple_point_location<Arrangement_>`

`CGAL/Arr_simple_point_location.h` (impl: `.../Arr_simple_point_location_impl.h`).

### Template parameters

```cpp
template <typename Arrangement_>
class Arr_simple_point_location
```

### Public typedefs

Identical set to naive (`Arrangement_2`, `Geometry_traits_2`, `Topology_traits`, the three handles,
`Point_2`, `X_monotone_curve_2`, `Result`, `Result_type`, `result_type`).

### Public member functions (verbatim)

```cpp
Arr_simple_point_location();
Arr_simple_point_location(const Arrangement_2& arr);

void attach(const Arrangement_2& arr);
void detach();

Result_type locate(const Point_2& p) const;

Result_type ray_shoot_up(const Point_2& p) const
{ return (_vertical_ray_shoot(p, true)); }

Result_type ray_shoot_down(const Point_2& p) const
{ return (_vertical_ray_shoot(p, false)); }
```

### Protected (relevant to understanding the return values)

```cpp
typedef typename std::optional<Result_type>          Optional_result_type;
typedef typename Topology_traits::Dcel                 Dcel;
typedef Arr_traits_basic_adaptor_2<Geometry_traits_2>  Traits_adaptor_2;

Optional_result_type _base_vertical_ray_shoot(const Point_2& p, bool shoot_up) const;
Result_type          _vertical_ray_shoot(const Point_2& p, bool shoot_up) const;
Halfedge_const_handle _first_around_vertex(Vertex_const_handle v) const;
```

### Semantics / preconditions

* `_base_vertical_ray_shoot` (**not including isolated vertices**) is the only place that yields
  `std::nullopt`. The public `ray_shoot_*` wrap `_vertical_ray_shoot`, which additionally scans the
  containing face's isolated vertices and, when nothing is found at all, returns
  `Face_const_handle(m_topol_traits->initial_face())`
  (`Arr_simple_point_location_impl.h:365-366`). **So the public ray-shoot result is a 3-way variant and
  the `Face` alternative means "the ray escaped".** **[verified]**
* `locate` first calls `_base_vertical_ray_shoot(p, true)`; if it is empty, it returns
  `Face_const_handle(m_topol_traits->initial_face())`. If it returns a vertex, `locate` walks to
  `_first_around_vertex(v)` and returns that halfedge's `face()`.
* Fictitious halfedges: `_base_vertical_ray_shoot` explicitly returns a fictitious halfedge if that is
  the closest thing (`Arr_simple_point_location_impl.h:237-239`). `locate` converts it to a face; the
  public `ray_shoot_*` can hand you a **fictitious `Halfedge_const_handle`**. Guard with
  `he->is_fictitious()`.
* Requires `Topology_traits::Dcel` and `Topology_traits::initial_face()`.
* Not an observer. Same dangling-pointer caveat as naive.

---

## 4. `CGAL::Arr_walk_along_line_point_location<Arrangement_>`

`CGAL/Arr_walk_along_line_point_location.h` (impl: `.../Arr_walk_along_line_pl_impl.h`).

Header doc: *"answers point-location and vertical ray-shooting queries on a planar arrangement by
walking on a vertical ray emanating from the query point, going from 'infinity' (the unbounded face)
until reaching the point. … The topology traits class **has to support the additional method
`initial_face()`**."*

### Template parameters

```cpp
template <class Arrangement_>
class Arr_walk_along_line_point_location
```

### Public typedefs

Same set as naive/simple, plus (protected) `Ccb_halfedge_const_circulator`,
`Inner_ccb_const_iterator`, `Isolated_vertex_const_iterator`.

### Public member functions (verbatim)

```cpp
Arr_walk_along_line_point_location();
Arr_walk_along_line_point_location(const Arrangement_2& arr);

void attach(const Arrangement_2& arr);
void detach();

result_type locate(const Point_2& p) const;

result_type ray_shoot_up(const Point_2& p) const
{ return (_vertical_ray_shoot(p, true)); }

result_type ray_shoot_down(const Point_2& p) const
{ return (_vertical_ray_shoot(p, false)); }
```

Protected helpers (documented here only because their doc comments state the real return contract):

```cpp
result_type _vertical_ray_shoot(const Point_2& p, bool shoot_up) const;
// "\return An object representing the arrangement feature the ray hits.
//  This object is either a Face_const_handle or a Halfedge_const_handle
//  or a Vertex_const_handle."   <-- this comment is the accurate one

bool _is_in_connected_component(const Point_2& p,
                                Ccb_halfedge_const_circulator circ,
                                bool shoot_up,
                                bool inclusive,
                                Halfedge_const_handle& closest_he,
                                bool& is_on_edge,
                                bool& closest_to_target) const;

Halfedge_const_handle _first_around_vertex(Vertex_const_handle v,
                                           bool shoot_up) const;
```

### Semantics

* `_vertical_ray_shoot` may return `Face_const_handle(top_traits->initial_face())`
  (line 368/370) or `closest_he->face()` when the closest halfedge is fictitious (line 382/385) —
  i.e. the escape case is again a face. **[verified]**
* Not an observer; requires `Topology_traits::initial_face()`. Works with unbounded arrangements.
  **[verified]**

---

## 5. `CGAL::Arr_landmarks_point_location<Arrangement_, Generator_>`

`CGAL/Arr_landmarks_point_location.h` (impl: `.../Arr_landmarks_pl_impl.h`).

### Template parameters (with default)

```cpp
template <typename Arrangement_,
          typename Generator_ = Arr_landmarks_vertices_generator<Arrangement_>>
class Arr_landmarks_point_location
```

### Public typedefs (C++11 `using`, verbatim)

```cpp
using Arrangement_2 = Arrangement_;
using Generator = Generator_;
using Geometry_traits_2 = typename Arrangement_2::Geometry_traits_2;

using Vertex_const_handle   = typename Arrangement_2::Vertex_const_handle;
using Halfedge_const_handle = typename Arrangement_2::Halfedge_const_handle;
using Face_const_handle     = typename Arrangement_2::Face_const_handle;

using Vertex_const_iterator   = typename Arrangement_2::Vertex_const_iterator;
using Halfedge_const_iterator = typename Arrangement_2::Halfedge_const_iterator;

using Halfedge_around_vertex_const_circulator =
  typename Arrangement_2::Halfedge_around_vertex_const_circulator;
using Ccb_halfedge_const_circulator =
  typename Arrangement_2::Ccb_halfedge_const_circulator;
using Outer_ccb_const_iterator = typename Arrangement_2::Outer_ccb_const_iterator;
using Inner_ccb_const_iterator = typename Arrangement_2::Inner_ccb_const_iterator;
using Isolated_vertex_const_iterator =
  typename Arrangement_2::Isolated_vertex_const_iterator;

using Point_2            = typename Arrangement_2::Point_2;
using X_monotone_curve_2 = typename Arrangement_2::X_monotone_curve_2;

using Result      = Arr_point_location_result<Arrangement_2>;
using Result_type = typename Result::Type;
using result_type = Result_type;
```

Note it takes `Point_2` from the **arrangement**, not from `Geometry_traits_2` (same type in practice).

### Public member functions (verbatim)

```cpp
Arr_landmarks_point_location();                       // p_arr=m_traits=lm_gen=nullptr, own_gen=false
Arr_landmarks_point_location(const Arrangement_2& arr);
    // lm_gen(new Generator(arr)), own_gen(true)
Arr_landmarks_point_location(const Arrangement_2& arr, Generator* gen);
    // lm_gen(gen), own_gen(false)

~Arr_landmarks_point_location();                      // deletes lm_gen iff own_gen

void attach(const Arrangement_2& arr, Generator* gen = nullptr);
void detach();

result_type locate(const Point_2& p) const;
```

*No ray shooting.* **[verified]**

### `attach` / `detach` semantics (read carefully — this is the main binding hazard)

```cpp
void attach(const Arrangement_2& arr, Generator* gen = nullptr) {
    p_arr = &arr;
    m_traits = static_cast<const Traits_adaptor_2*>(p_arr->geometry_traits());
    if (gen != nullptr) {
      CGAL_assertion(lm_gen == nullptr);          // <-- fires if a generator already exists
      lm_gen = gen;  own_gen = false;             // <-- previous owned generator is LEAKED in release
    }
    else if (lm_gen != nullptr) {
      Arrangement_2& non_const_arr = const_cast<Arrangement_2&>(*p_arr);
      lm_gen->attach(non_const_arr);              // re-attaches the observer (non-const!)
    }
    else { lm_gen = new Generator(arr); own_gen = true; }
}

void detach() {
    p_arr = nullptr; m_traits = nullptr;
    CGAL_assertion(lm_gen != nullptr);
    if (lm_gen) lm_gen->detach();                 // detaches the observer, but KEEPS the pointer
}
```

* `detach()` **keeps** `lm_gen`. A subsequent `attach(arr)` (no gen) reuses and re-attaches it. A
  subsequent `attach(arr, gen)` violates the precondition.
* `attach` internally `const_cast`s away constness to attach the generator observer — the arrangement
  must not actually be `const`-qualified storage.
* Ownership: the PL object owns the generator only when it created it. When you supply a generator,
  **you** must keep it alive at least as long as the PL object *and* not longer than the arrangement.
  (The arrangement's destructor detaches all still-attached observers
  (`Arrangement_on_surface_2_impl.h:212`), so an outliving generator is safe; the reverse is not.)

### `locate` behaviour / preconditions

* Empty arrangement (`number_of_vertices() == 0`) short-circuits to `faces_begin()` with
  `CGAL_assertion(number_of_faces() == 1)`.
* Otherwise: `lm_gen->closest_landmark(p, obj)`, then `_walk_from_vertex` / `_walk_from_edge` /
  `_walk_from_face` depending on `obj`'s alternative; finally, if the walk lands in a face, the face's
  isolated vertices are checked for coincidence with `p`.
* Unbounded arrangements **are** supported: the walk explicitly guards
  `is_at_open_boundary()` / `is_fictitious()` (`Arr_landmarks_pl_impl.h:89, 290-291, 309, 330, 385,
  567-568, 608, 617, 676, 681`). **[verified: locate on a 2-line unbounded arrangement returns the
  unbounded face.]**
* Assertions: `_walk_from_vertex()` asserts `!vh->is_at_open_boundary()`; `_walk_from_edge()` asserts
  `!eh->is_fictitious()`; the walk asserts each halfedge is crossed at most twice.
* Boundary handling is dispatched on `Arr_two_sides_category<Left_side_category,
  Right_side_category>::result` via the three `equal_x_2(p, q, tag)` overloads
  (`Arr_all_sides_oblivious_tag`, `Arr_has_identified_side_tag`, `Arr_boundary_cond_tag`).
* Traits requirement beyond the basics: `construct_x_monotone_curve_2_object()(p, q)` must exist
  (SFINAE-selected `construct_segment` uses `Compare_endpoints_xy_2` when available, else
  `Compare_xy_2`), and `is_between_cw_2_object()`.

---

## 6. Landmark generators

All live in `CGAL/Arr_point_location/`. All except the nearest-neighbour helper are **observers**
(`public Arrangement_::Observer`) and register themselves in their constructor.

### 6.1 `CGAL::Arr_landmarks_generator_base<Arrangement_, Nearest_neighbor_>` (abstract)

`Arr_lm_generator_base.h`.

```cpp
template <typename Arrangement_,
          typename Nearest_neighbor_ = Arr_landmarks_nearest_neighbor<Arrangement_> >
class Arr_landmarks_generator_base : public Arrangement_::Observer
```

Public typedefs:

```cpp
using Arrangement_2 = Arrangement_;
using Base_aos      = typename Arrangement_2::Base_aos;
using Nearest_neighbor = Nearest_neighbor_;

using Geometry_traits_2 = typename Base_aos::Geometry_traits_2;
using Vertex_const_handle / Halfedge_const_handle / Face_const_handle
    / Vertex_handle / Halfedge_handle / Face_handle
    / Vertex_const_iterator / Ccb_halfedge_circulator   // all from Base_aos
using Point_2            = typename Base_aos::Point_2;
using X_monotone_curve_2 = typename Base_aos::X_monotone_curve_2;

using NN_Point_2    = typename Nearest_neighbor::NN_Point_2;
using NN_Points_set = std::list<NN_Point_2>;
using Points_set    = std::vector<Point_2>;

using PL_result      = Arr_point_location_result<Base_aos>;
using PL_result_type = typename PL_result::Type;
using PL_pair        = std::pair<Point_2, PL_result_type>;
using Pairs_set      = std::vector<PL_pair>;
using Pairs_iterator = typename std::vector<PL_pair>::iterator;
```

Public functions:

```cpp
bool is_empty() const;                                   // nn.is_empty()
Arr_landmarks_generator_base(const Base_aos& arr);       // registers as observer; does NOT build
virtual void build_landmark_set();                       // _create_nn_points_set -> nn.clear(); nn.init(...)
virtual void clear_landmark_set();
virtual Point_2 closest_landmark(const Point_2& p, PL_result_type& obj);
    // CGAL_assertion(updated); returns nn.find_nearest_neighbor(p, obj)
```

Copy ctor and `operator=` are **private and undefined** → generators are non-copyable, non-assignable.

Overridden observer hooks (all `override`): `before_assign(const Base_aos&)`, `after_assign()`,
`before_attach(const Base_aos&)`, `after_attach()`, `before_detach()`, `after_clear()`,
`before_global_change()`, `after_global_change()`, `before_remove_edge(Halfedge_handle)`,
`after_create_vertex`, `after_create_edge`, `after_split_edge`, `after_split_face`,
`after_split_outer_ccb`, `after_split_inner_ccb`, `after_add_outer_ccb`, `after_add_inner_ccb`,
`after_add_isolated_vertex`, `after_merge_edge`, `after_merge_face`, `after_merge_outer_ccb`,
`after_merge_inner_ccb`, `after_move_outer_ccb`, `after_move_inner_ccb`,
`after_move_isolated_vertex`, `after_remove_vertex()`, `after_remove_edge()`,
`after_remove_outer_ccb(Face_handle)`, `after_remove_inner_ccb(Face_handle)`.
**Every local-change hook does `clear_landmark_set(); build_landmark_set();`** (guarded only by
`m_ignore_notifications` / `m_ignore_remove_edge`). This is the O(n)-rebuild-per-edit behaviour.

Pure virtual to be supplied by subclasses:

```cpp
virtual void _create_points_set(Points_set&) = 0;
virtual void _create_nn_points_set(NN_Points_set& nn_points);   // default: _create_points_set + CGAL::locate + shuffle
```

The default `_create_nn_points_set` calls the **batched** `locate(*(this->arrangement()),
points.begin(), points.end(), std::back_inserter(pairs))` into a
`std::vector<std::pair<Point_2, PL_result_type>>`, then `CGAL::cpp98::random_shuffle`s it (batched PL
returns points in sorted xy-order, which would unbalance the kd-tree).

### 6.2 `CGAL::Arr_landmarks_vertices_generator<Arrangement_, Nearest_neighbor_>` — **the default**

`Arr_lm_vertices_generator.h`.

```cpp
template <typename Arrangement_,
          typename Nearest_neighbor_ = Arr_landmarks_nearest_neighbor<Arrangement_> >
class Arr_landmarks_vertices_generator
  : public Arr_landmarks_generator_base<Arrangement_, Nearest_neighbor_>
```

```cpp
Arr_landmarks_vertices_generator(const Arrangement_2& arr);   // calls build_landmark_set()
virtual void _create_points_set(Points_set&);                 // CGAL_error() — must not be called
virtual void build_landmark_set();                            // every vertex -> NN_Point_2(v->point(), make_result(v))
virtual void clear_landmark_set();
protected: void _handle_local_change_notification();          // DEAD CODE: never called [verified]
```

Compiles with `Arrangement_2 = CGAL::Arrangement_2<...>` (it uses `const auto* arr =
this->arrangement();`). **[verified]** Landmarks = all arrangement vertices, each already located as
itself, so no batched PL is needed.

### 6.3 `CGAL::Arr_grid_landmarks_generator<Arrangement_, Nearest_neighbor_>`

`Arr_lm_grid_generator.h`. **Instantiate with `Arr::Base_aos`.** **[verified]**

```cpp
Arr_grid_landmarks_generator(const Arrangement_2& arr);                        // n = number_of_vertices()
Arr_grid_landmarks_generator(const Arrangement_2& arr, unsigned int n_landmarks);
virtual void build_landmark_set();     // grid points + batched CGAL::locate into lm_pairs
virtual void clear_landmark_set();
virtual Point_2 closest_landmark(const Point_2& q, PL_result_type& obj);       // O(1) index arithmetic — no kd-tree
```

Extra public typedef: `typedef typename Geometry_traits_2::Approximate_number_type ANT;`.
Requires `Geometry_traits_2::Approximate_2` (`approximate_2_object()(p, 0|1)`) and
`Point_2(double, double)` construction. Grid is `sqrt_n × sqrt_n` with
`sqrt_n = ceil(sqrt(num_landmarks))`; `CGAL_assertion(sqrt_n > 1)`, i.e. **≥ 2 vertices required**
(1-vertex arrangements are special-cased earlier). `closest_landmark` indexes
`lm_pairs[sqrt_n * i + j]` directly — the fastest generator, but it stores a
`std::vector<std::pair<Point_2, PL_result_type>>` of size `sqrt_n²`.

### 6.4 `CGAL::Arr_random_landmarks_generator<Arrangement_, Nearest_neighbor_>`

`Arr_lm_random_generator.h`. **Instantiate with `Arr::Base_aos`.** **[verified]**

```cpp
Arr_random_landmarks_generator(const Arrangement_2& arr, unsigned int n_landmarks = 0);
    // n_landmarks == 0  =>  n = arr->number_of_vertices()
protected: virtual void _create_points_set(Points_set& points);
```

Points are uniform in the bounding box of `CGAL::to_double(v->point().x()/y())`. Uses a fresh
`CGAL::Random` **per call** (default-seeded, so runs are reproducible only in the sense that
`CGAL::Random`'s default seed is fixed). Requires `Point_2(double, double)` and `point().x()/.y()`.

### 6.5 `CGAL::Arr_halton_landmarks_generator<Arrangement_, Nearest_neighbor_>`

`Arr_lm_halton_generator.h`. **Instantiate with `Arr::Base_aos`.** **[verified]**

```cpp
Arr_halton_landmarks_generator(const Arrangement_2& arr, unsigned int n_landmarks = 0);
protected: virtual void _create_points_set(Points_set& points);
```

Halton sequence with bases 2 and 3 scaled into the vertex bounding box. `n == 0` → number of
vertices; `n == 0` after that → produces nothing; `n == 1` → the single point `(x_max, y_max)`.
Same `Point_2(double,double)` / `to_double` requirements as the random generator.

### 6.6 `CGAL::Arr_middle_edges_landmarks_generator<Arrangement_, Nearest_neighbor_>`

`Arr_lm_middle_edges_generator.h`. **Instantiate with `Arr::Base_aos`.** **[verified]**

Header banner: *"IMPORTANT: THIS ALGORITHM WORKS ONLY FOR SEGMENTS !!!"* — it computes
`Point_2((p1.x()+p2.x())/2, (p1.y()+p2.y())/2)` from the two endpoints, which is only the curve
midpoint for straight segments.

```cpp
Arr_middle_edges_landmarks_generator(const Arrangement_2& arr, int /*lm_num*/ = -1);
    // second parameter is IGNORED

// no-op observer hooks (note: NO `override` keyword on any of them)
virtual void after_create_vertex(Vertex_handle);            // overrides
virtual void after_split_face(Face_handle, Face_handle, bool);   // overrides
virtual void after_add_hole(Ccb_halfedge_circulator);       // *** overrides NOTHING ***
virtual void after_merge_face(Face_handle);                 // overrides
virtual void after_move_hole(Ccb_halfedge_circulator);      // *** overrides NOTHING ***
virtual void after_remove_vertex();                         // overrides
virtual void after_remove_hole(Face_handle);                // *** overrides NOTHING ***

protected:
  virtual void _create_nn_points_set(NN_Points_set& nn_points);  // midpoints, each located as its halfedge
  virtual void _create_points_set(Points_set&);                  // CGAL_error()
```

Special case: a single isolated vertex arrangement yields one landmark located at that vertex.
No batched PL is used — each midpoint is paired with its own halfedge directly.

### 6.7 `CGAL::Arr_landmarks_specified_points_generator<Arrangement_, Nearest_neighbor_>`

`Arr_lm_specified_points_generator.h`. Compiles with `Arr` directly. **[verified]**

```cpp
Arr_landmarks_specified_points_generator(const Arrangement_2& arr, const Points_set points);
Arr_landmarks_specified_points_generator(const Arrangement_2& arr);   // single landmark at Point_2(0,0)

virtual void _create_points_set(Points_set&);          // CGAL_error()
void build_landmark_set();                             // NOTE: not virtual, hides the base version
void clear_landmark_set();                             // NOTE: not virtual, hides the base version
virtual Point_2 closest_landmark(const Point_2& q, PL_result_type& obj);
```

`Points_set` is `std::vector<Point_2>` and is taken **by value**.
**Beware**: `build_landmark_set()` matches each specified point to its batched-PL result with a linear
scan that can run off the end (`for (…; pairs_it != lm_pairs.end() && (*pairs_it).first != (*pt_it);
++pairs_it); if ((*pairs_it).first == (*pt_it))` — the `if` dereferences `end()` when no match is
found). Duplicated/unlocatable points are therefore UB. Also O(#points²).
`build_landmark_set`/`clear_landmark_set` are **not** marked `virtual` here, so calls through the base
class (e.g. from `after_create_edge`) dispatch to the *base* implementations, which call the pure
`_create_points_set` → `CGAL_error()`. **Do not modify an arrangement while a specified-points
generator is attached.**

### 6.8 `CGAL::Arr_landmarks_nearest_neighbor<Arrangement_>`

`Arr_lm_nearest_neighbor.h`. A kd-tree wrapper (`CGAL::Orthogonal_k_neighbor_search` over
`CGAL::Search_traits<Approximate_number_type, NN_Point_2, const Approximate_number_type*,
Construct_coord_iterator>`).

```cpp
template <typename Arrangement_>
class Arr_landmarks_nearest_neighbor {
public:
  typedef Arrangement_                                  Arrangement_2;
  typedef typename Arrangement_2::Vertex_const_handle   Vertex_const_handle;
  typedef typename Arrangement_2::Halfedge_const_handle Halfedge_const_handle;
  typedef typename Arrangement_2::Face_const_handle     Face_const_handle;
  typedef Arr_point_location_result<Arrangement_2>      PL_result;
  typedef typename PL_result::Type                      PL_result_type;
  typedef typename Arrangement_2::Geometry_traits_2     Geometry_traits_2;
  typedef typename Geometry_traits_2::Approximate_number_type Approximate_number_type;
  typedef typename Geometry_traits_2::Point_2           Point_2;

  class NN_Point_2 {
  public:
    Point_2 m_point;
    PL_result_type m_object;
    Approximate_number_type m_vec[2];

    NN_Point_2();
    NN_Point_2(const Point_2& p);
    NN_Point_2(const Point_2& p, const PL_result_type obj);
    const Point_2& point() const;
    const PL_result_type& object() const;
    const Approximate_number_type* begin() const;
    const Approximate_number_type* end() const;
    bool operator==(const NN_Point_2& nnp) const;   // compares approx coords only
    bool operator!=(const NN_Point_2& nnp) const;
  };

  struct Construct_coord_iterator { … };

  bool is_empty() const;

  Arr_landmarks_nearest_neighbor();
  ~Arr_landmarks_nearest_neighbor();                  // clear()

  template <class InputIterator> void init(InputIterator begin, InputIterator end);
      // \pre The search tree is not initialized.  CGAL_precondition_msg(m_tree == nullptr, …)
  void clear();
  Point_2 find_nearest_neighbor(const Point_2& q, PL_result_type& obj) const;
      // \pre The search tree has been initialized and is not empty.
      //      CGAL_precondition_msg(m_tree != nullptr && !m_is_empty, …)
};
```

Copy ctor / assignment are **private and undefined**. `NN_Point_2`'s single-argument constructor
**default-constructs a `Geometry_traits_2` on the stack** to call `approximate_2_object()` — the
traits type must be cheaply default-constructible. Note `operator==` compares only the *approximate*
`double` coordinates.

---

## 7. `CGAL::Arr_trapezoid_ric_point_location<Arrangement_>`

`CGAL/Arr_trapezoid_ric_point_location.h` (impl: `.../Arr_trapezoid_ric_pl_impl.h`).
Backed by `Trapezoidal_decomposition_2<Td_traits<Traits_adaptor_2, Base_aos>>`.

### Template parameters

```cpp
template <typename Arrangement_>
class Arr_trapezoid_ric_point_location : public Arrangement_::Observer
```

### Public typedefs (verbatim, note the unusual names)

```cpp
using Arrangement_on_surface_2 = Arrangement_;             // NOT "Arrangement_2"
using Base_aos = typename Arrangement_on_surface_2::Base_aos;

using Geometry_traits_2 = typename Base_aos::Geometry_traits_2;
using Traits_adaptor_2  = typename Base_aos::Traits_adaptor_2;

using Vertex_handle         = typename Base_aos::Vertex_handle;
using Vertex_const_handle   = typename Base_aos::Vertex_const_handle;
using Halfedge_handle       = typename Base_aos::Halfedge_handle;
using Halfedge_const_handle = typename Base_aos::Halfedge_const_handle;
using Face_const_handle     = typename Base_aos::Face_const_handle;
using Edge_const_iterator   = typename Base_aos::Edge_const_iterator;
using Isolated_vertex_const_iterator =
  typename Base_aos::Isolated_vertex_const_iterator;

using Point_2            = typename Geometry_traits_2::Point_2;
using X_monotone_curve_2 = typename Geometry_traits_2::X_monotone_curve_2;

using Td_traits = CGAL::Td_traits<Traits_adaptor_2, Base_aos>;
using Trapezoidal_decomposition = Trapezoidal_decomposition_2<Td_traits>;

using Td_map_item                = typename Trapezoidal_decomposition::Td_map_item;
using Td_active_vertex           = typename Trapezoidal_decomposition::Td_active_vertex;
using Td_active_fictitious_vertex= typename Trapezoidal_decomposition::Td_active_fictitious_vertex;
using Td_active_edge             = typename Trapezoidal_decomposition::Td_active_edge;
using Td_active_trapezoid        = typename Trapezoidal_decomposition::Td_active_trapezoid;

using Left_side_category   = typename Traits_adaptor_2::Left_side_category;
using Bottom_side_category = typename Traits_adaptor_2::Bottom_side_category;
using Top_side_category    = typename Traits_adaptor_2::Top_side_category;
using Right_side_category  = typename Traits_adaptor_2::Right_side_category;

using result_type = Result_type;   // Result/Result_type themselves are *protected*
```

`Result` and `Result_type` are **protected** here (unlike every other strategy); only `result_type` is
public. And `Arrangement_2` is inherited from `Aos_observer<Base_aos>` and therefore equals
**`Base_aos`**. **[verified by static_assert]**

### Public member functions (verbatim)

```cpp
Arr_trapezoid_ric_point_location
  (bool with_guarantees = true,
   double depth_thrs = CGAL_TD_DEFAULT_DEPTH_THRESHOLD,     // 60
   double size_thrs  = CGAL_TD_DEFAULT_SIZE_THRESHOLD);     // 12

Arr_trapezoid_ric_point_location
  (const Base_aos& arr,
   bool with_guarantees = true,
   double depth_thrs = CGAL_TD_DEFAULT_DEPTH_THRESHOLD,
   double size_thrs  = CGAL_TD_DEFAULT_SIZE_THRESHOLD);

~Arr_trapezoid_ric_point_location();

void with_guarantees(bool with_guarantees);
    // if it flips false -> true, the whole structure is td.clear()ed and rebuilt

unsigned long depth();                       // NON-const; = td.largest_leaf_depth() + 1
unsigned long longest_query_path_length();   // NON-const

result_type locate(const Point_2& p) const;
result_type ray_shoot_up(const Point_2& p) const   { return (_vertical_ray_shoot(p, true)); }
result_type ray_shoot_down(const Point_2& p) const { return (_vertical_ray_shoot(p, false)); }

#ifdef CGAL_TD_DEBUG
void print_dag(std::ostream& out) const;
#endif
```

Inherited from `Aos_observer<Base_aos>` (see §9): `void attach(Base_aos& arr)` — **non-const
reference**, `void detach()`, `const Base_aos* arrangement() const`, `Base_aos* arrangement()`.
`depth()` and `longest_query_path_length()` are **not `const`** — a `const` strategy object cannot
report them. **[verified: `depth()==6`, `longest_query_path_length()==6` on a 3-segment triangle.]**

Thresholds: `CGAL_TD_DEFAULT_DEPTH_THRESHOLD == 60`, `CGAL_TD_DEFAULT_SIZE_THRESHOLD == 12`
(`Trapezoidal_decomposition_2_misc.h:45-46`). `with_guarantees(true)` makes the DAG rebuild itself
whenever `largest_leaf_depth() > depth_threshold * log(#curves + 1)` or the size threshold is
exceeded, which is what buys the expected-O(log n) query / expected-O(n) space bound. With
`with_guarantees(false)` the structure is cheaper to maintain but has no bound.

### Observer hooks it overrides (this is the **only incrementally-maintained** strategy)

```cpp
virtual void before_assign(const Base_aos& arr) override;   // td.clear(); re-init traits
virtual void after_assign() override;                       // _construct_td()
virtual void before_clear() override;                       // td.clear()
virtual void after_clear() override;                        // _construct_td()
virtual void before_attach(const Base_aos& arr) override;   // td.clear(); init_arrangement_and_traits
virtual void after_attach() override;                       // _construct_td()
virtual void before_detach() override;                      // td.clear()
virtual void after_create_edge(Halfedge_handle e) override;              // td.insert(e)
virtual void before_split_edge(Halfedge_handle e, Vertex_handle,
                               const X_monotone_curve_2&,
                               const X_monotone_curve_2&) override;      // td.remove(e)
virtual void after_split_edge(Halfedge_handle e1, Halfedge_handle e2) override;  // td.insert(e1); td.insert(e2)
virtual void before_merge_edge(Halfedge_handle e1, Halfedge_handle e2,
                               const X_monotone_curve_2& cv) override;
virtual void after_merge_edge(Halfedge_handle e) override;
virtual void before_remove_edge(Halfedge_handle e) override;             // td.remove(e)
```

It reacts only to **edge** events — vertex, face and CCB events are ignored, because the trapezoidal
map is derived purely from the edge set. Cost per edge insertion/removal is that of a DAG update, not
a full rebuild. This makes RIC the right default for a mutable arrangement.

### `locate` / `_vertical_ray_shoot` return semantics

```cpp
protected:
  Face_const_handle _get_unbounded_face(const Td_map_item& tr, const Point_2& p,
                                        Arr_all_sides_oblivious_tag) const;
  Face_const_handle _get_unbounded_face(const Td_map_item& tr, const Point_2& p,
                                        Arr_not_all_sides_oblivious_tag) const;
  result_type _vertical_ray_shoot(const Point_2& p, bool shoot_up) const;
  result_type _check_isolated_for_vertical_ray_shoot
    (Halfedge_const_handle halfedge_found, const Point_2& p, bool shoot_up,
     const Td_map_item& tr) const;
  void _construct_td();
```

`_check_isolated_for_vertical_ray_shoot` returns, in priority order: the closest isolated vertex
(`Vertex_const_handle`), else — if no halfedge was found — the unbounded face
(`Face_const_handle`), else the halfedge. So `ray_shoot_*` yields a `Face_const_handle` exactly when
the ray escapes. **[verified: `ray_shoot_up` on a point above two crossing lines returns the unbounded
face; `ray_shoot_down` returns a halfedge.]**

`_vertical_ray_shoot` dispatches on `Trapezoidal_decomposition_2::Locate_type`:
`enum Locate_type { POINT = 0, CURVE, TRAPEZOID, UNBOUNDED_TRAPEZOID = 8 };`.
For `CURVE`/`TRAPEZOID` it orients the returned halfedge so that the query point is on its *left*
side (twin-flip based on `direction()`), i.e. the returned halfedge is the one whose incident face
contains `p`.

Unbounded arrangements are fully supported. **[verified]**

---

## 8. `CGAL::Arr_triangulation_point_location<Arrangement_>`

`CGAL/Arr_triangulation_point_location.h` (impl: `.../Arr_triangulation_pl_functions.h`).

### Template parameters

```cpp
template <typename Arrangement_>
class Arr_triangulation_point_location : public Arrangement_::Observer
```

### Public typedefs (verbatim)

```cpp
using Arrangement_2 = Arrangement_;                    // redeclared -> really is Arrangement_
using Base_aos      = typename Arrangement_2::Base_aos;

using Geometry_traits_2 = typename Base_aos::Geometry_traits_2;
using Kernel            = typename Geometry_traits_2::Kernel;   // <-- HARD REQUIREMENT

using Vertex_const_handle / Halfedge_const_handle / Face_const_handle
    / Vertex_handle / Halfedge_handle / Face_handle              // from Base_aos
using Vertex_const_iterator / Edge_const_iterator / Face_const_iterator
    / Halfedge_const_iterator
    / Halfedge_around_vertex_const_circulator
    / Ccb_halfedge_const_circulator / Ccb_halfedge_circulator
    / Isolated_vertex_const_iterator                             // from Base_aos

using Point_2            = typename Geometry_traits_2::Point_2;
using X_monotone_curve_2 = typename Geometry_traits_2::X_monotone_curve_2;

using Edge_list         = std::list<Halfedge_const_handle>;
using Std_edge_iterator = typename Edge_list::iterator;

// Triangulation stack (all public):
using Vbb  = Triangulation_vertex_base_with_info_2<Vertex_const_handle, Kernel>;
using Vb   = Triangulation_hierarchy_vertex_base_2<Vbb>;
using Fb   = Constrained_triangulation_face_base_2<Kernel>;
using TDS  = Triangulation_data_structure_2<Vb,Fb>;
using Itag = Exact_predicates_tag;
using CDT_t = Constrained_Delaunay_triangulation_2<Kernel, TDS, Itag>;
using CDTH  = Triangulation_hierarchy_2<CDT_t>;
using CDT   = Constrained_triangulation_plus_2<CDTH>;

using CDT_Point / CDT_Edge / CDT_Face_handle / CDT_Vertex_handle
    / CDT_Finite_faces_iterator / CDT_Finite_vertices_iterator
    / CDT_Finite_edges_iterator / CDT_Locate_type

using Result      = Arr_point_location_result<Base_aos>;
using Result_type = typename Result::Type;
using result_type = Result_type;
```

### Public member functions (verbatim)

```cpp
Arr_triangulation_point_location();                    // m_traits = nullptr; NO triangulation
Arr_triangulation_point_location(const Base_aos& arr); // registers observer, then build_triangulation()

result_type locate(const Point_2& p) const;
```

*No ray shooting.* **[verified]** No explicit `attach`/`detach`: use the inherited
`Aos_observer::attach(Base_aos&)` (non-const) / `detach()`. **[verified]**

### Observer hooks it overrides

`before_attach(const Base_aos&)`, `after_attach()`, `before_detach()`, `after_assign()`,
`after_clear()`, `before_global_change()`, `after_global_change()`,
`before_remove_edge(Halfedge_handle)`, `after_create_vertex`, `after_create_edge`,
`after_split_edge`, `after_split_face`, `after_add_outer_ccb`, `after_merge_edge`,
`after_merge_face`, `after_move_outer_ccb`, `after_remove_vertex()`, `after_remove_edge()`,
`after_remove_outer_ccb(Face_handle)`, `after_add_inner_ccb`, `after_move_inner_ccb`,
`after_remove_inner_ccb(Face_handle)`.

**Every one of them does `clear_triangulation(); build_triangulation();`** — a complete CDT rebuild.
Guarded only by `m_ignore_notifications` (set between `before_global_change` and
`after_global_change`) and `m_ignore_remove_edge`. Do not use this strategy with incremental
insertion.

### Protected

```cpp
result_type locate_in_unbounded(const Point_2& p) const;
void clear_triangulation();     // m_cdt.clear()
void build_triangulation();     // every edge -> two CDT vertices + insert_constraint
```

### Preconditions (the important ones)

* **`Geometry_traits_2::Kernel` must exist.** `Arr_segment_traits_2`,
  `Arr_non_caching_segment_traits_2` and `Arr_linear_traits_2` provide it; `Arr_polyline_traits_2`,
  the conic and Bézier traits do **not** → hard compile error.
* **Bounded arrangements only.** `build_triangulation()` calls `vh->point()` on every edge endpoint;
  a vertex at an open boundary has a null point. **[verified: run-time
  `CGAL error: assertion violation! Expression : p_pt != nullptr, Arr_dcel_base.h:105` on an
  `Arr_linear_traits_2` arrangement containing one line.]** An empty unbounded arrangement is fine
  (nothing to triangulate).
* **Straight segments only.** Each edge is inserted as `m_cdt.insert_constraint(cdt_vh1, cdt_vh2)`
  between its two endpoints, ignoring `curve()`. Curved edges produce wrong answers.
* `locate_in_unbounded` has an in-source `//! \todo Here we assume that there is only one unbounded
  face.` — it always returns `this->arrangement()->unbounded_faces_begin()`.
* `build_triangulation()` prints `"WARNING: source point is equal to destination point!!! "` on
  `std::cerr` for degenerate edges, and ends with `CGAL_assertion(m_cdt.is_valid())`.
* The isolated vertices of the arrangement are **not** inserted into the CDT; `locate` checks them by
  linear scan of the resulting face's `isolated_vertices_begin()/end()`, and there are
  `CGAL_assertion(!v0->is_isolated())` etc. on the triangle's three vertices.
* `locate` on a CDT face resolves the arrangement face by circulating `v0`'s incident halfedges and
  looking for a CCB that visits `v1` then `v2`; failure ⇒ the unbounded face.

---

## 9. Attach/detach for the observer-based strategies — `CGAL::Aos_observer<Arrangement_>`

`CGAL/Aos_observer.h`. `Arrangement_2::Observer` is `Aos_observer<Arrangement_2::Base_aos>`;
`CGAL::Arr_observer<Arr>` is now merely `using Arr_observer = typename Arrangement_::Observer;`.

Relevant public API (the part RIC and triangulation PL inherit):

```cpp
Aos_observer();                                 // p_arr = nullptr
Aos_observer(Arrangement_2& arr);               // registers immediately
virtual ~Aos_observer();                        // unregisters if still attached

const Arrangement_2* arrangement() const;
Arrangement_2*       arrangement();

void attach(Arrangement_2& arr);
    // \pre The observer is not already attached to an arrangement.
    //      no-op if p_arr == &arr; CGAL_precondition(p_arr == nullptr);
    //      calls before_attach(arr), registers, calls after_attach()
void detach();
    // no-op if p_arr == nullptr; calls before_detach(), unregisters,
    // p_arr = nullptr, calls after_detach()
```

Copy ctor and `operator=` are **private and undefined** → every observer-based PL strategy (RIC,
triangulation) and every landmark generator is **non-copyable and non-assignable**. A type-erased
wrapper must hold them by pointer/`unique_ptr`, never by value in a copyable struct.

Lifetime: `~Arrangement_on_surface_2()` **detaches every still-attached observer**
(`Arrangement_2/Arrangement_on_surface_2_impl.h:212 ff.`, after freeing points and curves). So
destroying the arrangement before the strategy is safe for observer-based strategies. It is **not**
safe for naive/simple/walk/landmarks, which just hold raw `const Arrangement_2*` and never learn about
the destruction.

---

## 10. Batched point location — `CGAL::locate(arr, first, last, oi)`

`CGAL/Arr_batched_point_location.h`.

```cpp
template <typename GeometryTraits_2, typename TopologyTraits,
          typename PointsIterator, typename OutputIterator>
OutputIterator
locate(const Arrangement_on_surface_2<GeometryTraits_2, TopologyTraits>& arr,
       PointsIterator points_begin, PointsIterator points_end,
       OutputIterator oi)
```

Doc comment verbatim:

```
 * \pre The value-type of PointsIterator is Arrangement::Point_2,
 *      and the value-type of OutputIterator is is pair<Point_2, Result>,
 *      where Result is std::optional<std::variant<Vertex_const_handle,
 *                                      Halfedge_const_handle,
 *                                      Face_const_handle> >.
 *      It represents the arrangement feature containing the point.
```

### What it actually writes

`Surface_sweep_2/Arr_batched_pl_ss_visitor.h` writes, at every query event:

```cpp
*m_out++ = std::make_pair(event->point().base(), pl_make_result(vh /* or he, or face */));
```

with `pl_make_result` = `Arr_point_location_result<Arrangement_2>::make_result`. So the value written
is

```cpp
std::pair<Point_2,
          std::variant<Vertex_const_handle, Halfedge_const_handle, Face_const_handle>>
```

— **no `std::optional`**. Both of these output iterators compile and run **[both verified]**:

```cpp
using Cell = std::variant<Arr::Vertex_const_handle,
                          Arr::Halfedge_const_handle,
                          Arr::Face_const_handle>;

std::list<std::pair<Arr::Point_2, Cell>>                bpl_out;   // canonical (what CGAL itself uses)
std::list<std::pair<Arr::Point_2, std::optional<Cell>>> bpl_out2;  // also works (implicit conversion)

CGAL::locate(arr, qs.begin(), qs.end(), std::back_inserter(bpl_out));
```

Prefer the **non-optional** form: it matches `Arr_landmarks_generator_base::PL_pair` and avoids a
pointless extra indirection in the binding layer.

### Which alternative you get

| Situation | Alternative written |
|---|---|
| query point == an isolated vertex | `Vertex_const_handle` |
| query point == any arrangement vertex (event has left or right curves) | `Vertex_const_handle` |
| no valid edge above the point | `Face_const_handle` = `helper.top_face()` (the unbounded face for bounded topology; `m_top_fict->face()` for unbounded topology) |
| the point lies **on** the subcurve above (`on_above`) | `Halfedge_const_handle` |
| otherwise | `Face_const_handle` = `he->face()` for the halfedge above |

Faces returned are always incident-from-above, so they are genuine `Face_const_handle`s, never
fictitious. **[verified: 2 query points → 2 result pairs.]**

### Iterator requirements

* `PointsIterator`: input iterator, `value_type` convertible to `Arrangement::Point_2`. The range is
  **not** required to be sorted; the sweep sorts internally.
* `OutputIterator`: output iterator accepting `std::pair<Point_2, Result_type>`. The visitor stores
  `Output_iterator& m_out;` — a reference to the **by-value** `oi` parameter of `locate` — and
  `locate` returns that advanced `oi`. `std::back_inserter` works.
* **Results come out sorted in increasing xy-lexicographic order of the query points**, not in input
  order. (This is why `Arr_landmarks_generator_base::_create_nn_points_set` shuffles afterwards.)
  Duplicate query points each get their own entry.

### Traits requirements

`Arr_batched_point_location_traits_2<Arr>` (`Arr_point_location/Arr_batched_point_location_traits_2.h`)
decorates the arrangement's geometry traits and pulls `Multiplicity`, `Construct_min_vertex_2`,
`Construct_max_vertex_2`, `Compare_x_2`, `Compare_xy_2`, `Compare_y_at_x_2`,
`Compare_y_at_x_right_2`, `Equal_2`, `Is_vertical_2`, `Has_do_intersect_category` from it. It sets
`Has_left_category = Tag_false` and `Has_merge_category = Tag_false`. In effect: the geometry traits
must model `ArrangementXMonotoneTraits_2`, not just the basic traits.

The extended point/curve wrappers carry the DCEL handle:
`Ex_point_2::vertex_handle()`, `Ex_point_2::base()`,
`Ex_x_monotone_curve_2::halfedge_handle()`, `Ex_x_monotone_curve_2::base()`.
Each arrangement edge is registered with the halfedge whose `direction() == ARR_RIGHT_TO_LEFT`.

### Cost

One `No_intersection_surface_sweep_2` pass over `#edges + #isolated_vertices + #queries` events:
O((n + m) log (n + m)). Preferable to m separate `locate()` calls once m is comparable to n.

---

## 11. Vertical decomposition — `CGAL::decompose(arr, oi)`

`CGAL/Arr_vertical_decomposition_2.h`.

```cpp
template <typename GeometryTraits_2, typename TopologyTraits,
          typename OutputIterator>
OutputIterator
decompose(const Arrangement_on_surface_2<GeometryTraits_2, TopologyTraits>& arr,
          OutputIterator oi)
```

Doc comment verbatim:

```
 * \param oi An output iterator of the vertices, each paired with a pair of
 *           arrangement features that lie below and above it, respectively.
 *           The vertices are sorted by increasing xy-order.
 *           The OutputIterator dereferences the type \c
 *           pair<Vertex_const_handle, pair<Vert_type, Vert_type> >, where
 *           \c Vert_type is an optional handle to an arrangement feature.
 * \return A past-the-end iterator for the ordered arrangement vertices.
```

### Exact output value type — **verified by compiling and running**

From `Surface_sweep_2/Arr_vert_decomp_ss_visitor.h`:

```cpp
typedef std::variant<Vertex_const_handle, Halfedge_const_handle,
                       Face_const_handle>              Cell_type;
typedef std::optional<Cell_type>                     Vert_type;
typedef std::pair<Vert_type, Vert_type>                Vert_pair;
typedef std::pair<Vertex_const_handle, Vert_pair>      Vert_entry;
```

Spelled out for a binding layer:

```cpp
using Cell      = std::variant<Arr::Vertex_const_handle,
                               Arr::Halfedge_const_handle,
                               Arr::Face_const_handle>;
using VertT     = std::optional<Cell>;
using VertPair  = std::pair<VertT, VertT>;              // .first = BELOW, .second = ABOVE
using VertEntry = std::pair<Arr::Vertex_const_handle, VertPair>;

std::list<VertEntry> out;
CGAL::decompose(arr, std::back_inserter(out));
```

The visitor writes `*(*m_out) = Vert_entry(vh, Vert_pair(below, above)); ++(*m_out);`
(it holds `Output_iterator* m_out` — a *pointer* to the by-value `oi`; `decompose` returns that
advanced `oi`).

### What each alternative means

| Case | `below` / `above` value |
|---|---|
| a concrete subcurve is directly below/above | `Halfedge_const_handle` (never fictitious) — `(*it)->last_curve().halfedge_handle()` |
| the previous/next vertex has the same x and is the closest feature | `Vertex_const_handle` |
| that vertically-aligned neighbour vertex is **connected by a vertical edge** | `std::nullopt` **[verified]** |
| nothing at all, **bounded** planar topology | `Face_const_handle` = the unbounded face (`Arr_bounded_planar_vert_decomp_helper::top_object/bottom_object` return `Vert_type(m_unb_face)`) **[verified]** |
| nothing at all, **unbounded** planar topology | `Halfedge_const_handle` with `is_fictitious() == true` (`Arr_unb_planar_vert_decomp_helper` returns `Vert_type(m_top_fict)` / `Vert_type(m_bottom_fict)`) **[verified]** |

Observed output for a bounded triangle `(0,0)-(4,0)-(0,4)` with a vertical edge `(0,0)-(0,4)`:

```
vd n=3
  v=(0 0) below idx=2 (Face, unbounded)  above=none   <- nullopt: vertical edge to (0,4)
  v=(0 4) below=none                     above idx=2  (Face, unbounded)
  v=(4 0) below idx=2                    above idx=2
```

Observed output for an unbounded arrangement of two crossing lines:

```
vd n=1
  v=(-0 -0) below=H(fictitious) above=H(fictitious)
```

Note the unbounded case reports **one** entry: vertices at infinity are skipped
(`if (! event->is_closed()) return true;`).

### Iterator requirements and ordering

* `OutputIterator` must accept `Vert_entry` as above; `std::back_inserter` on a
  `std::list`/`std::vector` of that type works.
* Entries come out **sorted by increasing xy-order of the vertices**, one entry per non-fictitious
  arrangement vertex (including isolated vertices — they are fed to the sweep as action points).
* The last entry is emitted from `after_sweep()`, so the sequence is complete only after `decompose`
  returns.
* No `decompose` overload for a query point or a sub-range exists — it is all-vertices-or-nothing.
  **[verified: `Arr_vertical_decomposition_2.h` declares exactly one function.]**

### Traits requirements / cost

Same `Arr_batched_point_location_traits_2` decoration and the same
`No_intersection_surface_sweep_2` machinery as batched PL: one sweep, O(n log n).

---

## 12. Notes for a type-erased C++ core + Cython bindings

1. **Model the result once.** A single `enum class PlCell { Vertex, Halfedge, Face }` plus a tagged
   struct holding the three handle types covers `locate`, `ray_shoot_*`, batched PL and both halves
   of `decompose`. The variant's alternative order (`0/1/2`) is stable across all of them.

2. **Two distinct attach shapes.** Naive/simple/walk/landmarks: `attach(const Arr&) / detach()` on the
   strategy itself, arrangement not modified. RIC/triangulation: inherited
   `attach(Arr::Base_aos&) / detach()` which *registers an observer on the arrangement* — needs a
   non-const arrangement and mutates the arrangement's observer list. Expose them behind one virtual
   `attach(Arr&)` in the erasure layer, `const_cast`ing only for the first group.

3. **Capability flags, not a uniform interface.** `has_ray_shoot` is true only for simple, walk and
   RIC. `is_observer` (auto-updating) is true only for RIC, triangulation and the landmark generators.

4. **Non-copyable types.** RIC, triangulation and every landmark generator inherit private
   copy ctor/assignment from `Aos_observer`. `Arr_landmarks_nearest_neighbor` too. Hold them in
   `std::unique_ptr` and never put them in a value-semantics wrapper.

5. **Destruction order.** Observer-based strategies survive the arrangement dying first
   (the AOS destructor detaches them). Non-observer strategies do **not** — they keep a raw
   `const Arrangement_2*`. In a Python binding, either keep a strong reference from the PL wrapper to
   the arrangement wrapper, or call `detach()` in the arrangement's teardown.

6. **The landmark generator lifetime triangle.** PL object → (maybe owns) generator → registered as an
   observer on the arrangement. If you expose "supply your own generator", document that the generator
   must outlive the PL object, and never re-`attach(arr, gen)` after a first attach (assertion /
   leak, §Gotcha 14).

7. **`ray_shoot_*` "miss" is a face, not `None`.** Map "result is a `Face_const_handle`" to Python
   `None` (or to an explicit `RayEscaped(face)`) rather than propagating a face where callers expect a
   hit. Also check `is_fictitious()` on returned halfedges in unbounded arrangements before touching
   `curve()`.

8. **`decompose` needs three unwrap steps** in Cython: `entry.first` (vertex), then
   `entry.second.first` / `entry.second.second` (`std::optional`), then `.value()` /
   `std::get_if<T>` on the variant. Cython cannot see `std::variant`/`std::optional` members
   directly — write tiny `extern "C++"` shims (`int vd_below_kind(const VertEntry&)`,
   `Halfedge_const_handle vd_below_halfedge(const VertEntry&)`, …) in the C++ core instead of trying
   to declare the nested templates in a `.pxd`.

9. **Empty-arrangement edge cases**: `Arr_landmarks_point_location::locate` short-circuits on
   `number_of_vertices() == 0`; `Arr_grid_landmarks_generator::_create_points_set` returns early on
   `arr->is_empty()` and asserts `sqrt_n > 1` otherwise; the triangulation strategy handles an empty
   CDT (`OUTSIDE_AFFINE_HULL` → `locate_in_unbounded`). **[verified: empty unbounded arrangement →
   triangulation `locate` returns index 2 (Face).]**

---

## 13. Strategy × geometry-traits compatibility matrix — **supersedes and extends §0**

§0 above is a per-*strategy* table only (does this class have `locate` / `ray_shoot_*` / an observer
hook). It says nothing about **which strategy works with which geometry traits**, and the few
per-traits facts that exist are scattered over `traits_circle_segment.md`, `traits_geodesic_sphere.md`
and `arrangement_with_history.md`. This section replaces those scattered claims with a full
compile-**and**-run matrix. Everything below is **[verified]** on this machine unless explicitly
marked otherwise.

### 13.1 How the matrix was produced

One translation unit per (strategy, traits) cell — 150+ TUs — each built with

```
/usr/bin/clang++ -std=c++17 -O0 -DCGAL_USE_CORE -DCGAL_USE_GMP -DCGAL_USE_MPFR \
    -I/opt/homebrew/include -L/opt/homebrew/lib -lgmp -lmpfr
```

(no `-DCGAL_NDEBUG`, so **all CGAL preconditions/assertions are live** — this is what a debug build of
the binding will see). Each cell builds the same reference arrangement for its traits, then runs four
probe queries and compares the answer against `Arr_naive_point_location` on the same arrangement:

| probe | meaning |
|---|---|
| `q0` | a point strictly inside a **bounded** face |
| `q1` | a point **on an edge** |
| `q2` | a point **equal to a vertex** |
| `q3` | a point in the **unbounded face** (on the sphere: in the other face) |

plus, where the strategy offers them, `ray_shoot_up(q0)`, `ray_shoot_down(q0)`,
`ray_shoot_up(q3)`, `ray_shoot_down(q3)`.

Reference arrangements: unit square `(0,0)-(4,0)-(4,4)-(0,4)` plus a diagonal (segment-like traits);
square + circle of rational radius 1 about `(2,2)` (circle-segment / conic); square of degree-1
Béziers + one cubic (Bézier); square + full line `y=x` (linear, unbounded); spherical triangle
(geodesic). Result codes:

* **OK** — compiles, runs, and every probe agrees with `Arr_naive_point_location`
  (modulo *twin choice*, see §13.4).
* **CE** — hard compile error; the missing requirement is quoted.
* **RT!** — compiles, then aborts at run time; the CGAL message is quoted.
* **WRONG** — compiles and runs but returns a different (incorrect) feature than naive.

### 13.2 The matrix

Columns (all `Epeck` = `Exact_predicates_exact_constructions_kernel`; CORE = `CGAL::Cartesian<Rational>` /
`CGAL::Cartesian<CORE::Expr>` / `CGAL::CORE_algebraic_number_traits`):

| # | column |
|---|---|
| **A** | `Arr_segment_traits_2<Epeck>` |
| **B** | `Arr_non_caching_segment_traits_2<Epeck>` |
| **C** | `Arr_linear_traits_2<Epeck>` — **unbounded** |
| **D** | `Arr_polyline_traits_2<Arr_segment_traits_2<Epeck>>` |
| **E** | `Arr_polycurve_traits_2<Arr_circle_segment_traits_2<Epeck>>` |
| **F** | `Arr_circle_segment_traits_2<Epeck>` |
| **G** | `Arr_conic_traits_2<…CORE…>` |
| **H** | `Arr_Bezier_curve_traits_2<…CORE…>` |
| **I** | `Arr_geodesic_arc_on_sphere_traits_2<Epeck>` + `Arr_spherical_topology_traits_2` |
| **J** | `Arrangement_with_history_2<Arr_segment_traits_2<Epeck>>` |

| strategy \ traits | A seg | B ncseg | C linear | D polyline | E polycurve⟨cs⟩ | F circ-seg | G conic | H Bézier | I sphere | J with-history |
|---|---|---|---|---|---|---|---|---|---|---|
| `Arr_naive_point_location` | OK | OK | OK | OK | OK | OK | OK | OK | **OK** | OK |
| `Arr_simple_point_location` | OK | OK | OK | OK | OK | OK | OK | OK | **CE** ¹ | OK |
| `Arr_walk_along_line_point_location` | OK | OK | OK | OK | OK | OK | OK | OK | **CE** ¹ | OK |
| `Arr_landmarks…` + `Arr_landmarks_vertices_generator` *(default)* | OK | OK | OK | OK | **CE** ² | **CE** ³ | OK | **CE** ⁴ | **RT!** ⁵ | OK |
| `Arr_landmarks…` + `Arr_grid_landmarks_generator` | OK | OK | OK | OK | **CE** ² | **CE** ³ | OK | **CE** ⁴ | **CE** ⁶ | OK |
| `Arr_landmarks…` + `Arr_random_landmarks_generator` | OK | OK | OK | OK | **CE** ² | **CE** ³ | OK | **CE** ⁴ | **CE** ⁷ | OK |
| `Arr_landmarks…` + `Arr_halton_landmarks_generator` | OK | OK | OK | OK | **CE** ² | **CE** ³ | OK | **CE** ⁴ | **CE** ⁷ | OK |
| `Arr_landmarks…` + `Arr_middle_edges_landmarks_generator` | OK | OK | **RT!** ⁸ | OK | **CE** ² | **CE** ³ | OK | **CE** ⁴ | **CE** ⁷ | OK |
| `Arr_landmarks…` + `Arr_landmarks_specified_points_generator` | OK | OK | OK | OK | **CE** ² | **CE** ³ | OK | **CE** ⁴ | **RT!** ⁵ | OK |
| `Arr_trapezoid_ric_point_location` | OK | OK | OK | OK | OK | OK | OK | OK | **RT!** ⁹ | OK |
| `Arr_triangulation_point_location` | **OK\*/WRONG** ¹⁰ | **OK\*/WRONG** ¹⁰ | **RT!** ¹¹ | **CE** ¹² | **CE** ¹² | **CE** ¹³ | **CE** ¹² | **CE** ¹² | **CE** ¹³ | **OK\*/WRONG** ¹⁰ |
| `CGAL::locate(arr,first,last,oi)` *(batched)* | OK | OK | OK | OK | OK | OK | OK | OK | **OK / WRONG** ¹⁴ | OK |
| `CGAL::decompose(arr,oi)` | OK | OK | OK | OK | OK | OK | OK | OK | **OK / RT!** ¹⁵ | OK |

Landmark generators marked OK for columns A–D, G, J were instantiated on **`Arr::Base_aos`** for
grid / random / halton / middle-edges (§0 Gotcha 5) and on `Arr` for vertices / specified-points.

### 13.3 The footnotes — exact errors and missing requirements

**¹ `Arr_simple_point_location` / `Arr_walk_along_line_point_location` on the sphere — CE.**
Both are hard-wired to *planar* topology traits:

```
Arr_simple_point_location_impl.h:63:62: error: no member named 'initial_face' in
  'CGAL::Arr_spherical_topology_traits_2<CGAL::Arr_geodesic_arc_on_sphere_traits_2<CGAL::Epeck>>'
Arr_simple_point_location_impl.h:139:29: error: no member named 'compare_x'  in '…Arr_spherical_topology_traits_2…'
Arr_simple_point_location_impl.h:216:49: error: no member named 'compare_xy' in '…Arr_spherical_topology_traits_2…'
```
```
Arr_walk_along_line_pl_impl.h:47:41:  error: no member named 'initial_face' in '…Arr_spherical_topology_traits_2…'
Arr_walk_along_line_pl_impl.h:513:23: error: no member named 'compare_xy'   in '…Arr_spherical_topology_traits_2…'
Arr_walk_along_line_pl_impl.h:568:28: error: no member named 'compare_x'    in '…Arr_spherical_topology_traits_2…'
```
Missing requirement: `Topology_traits::{initial_face, compare_x, compare_xy}` — supplied by
`Arr_bounded_planar_topology_traits_2` and `Arr_unb_planar_topology_traits_2`, **not** by
`Arr_spherical_topology_traits_2`. This confirms and sharpens the "were not tested … planar-oriented"
remark at `traits_geodesic_sphere.md:1032`: they do not merely misbehave, **they do not compile.**

**² landmarks on `Arr_polycurve_traits_2<…>` — CE.** `Arr_landmarks_point_location.h:329` and
`Arr_point_location/Arr_landmarks_pl_impl.h:297` call
`m_traits->construct_x_monotone_curve_2_object()(np, p)` with **two `Point_2`s**.
`Arr_polycurve_basic_traits_2::Construct_x_monotone_curve_2` has only
`operator()(const X_monotone_subcurve_2&)` and a `template<ForwardIterator> operator()(begin, end)`,
so the two points are deduced as *iterators*:

```
Arr_polycurve_basic_traits_2.h:1235:66: error: no type named 'value_type' in
  'std::iterator_traits<CGAL::_One_root_point_2<…>>'
Arr_polycurve_basic_traits_2.h:1292:10: error: cannot increment value of type 'CGAL::_One_root_point_2<…>'
Arr_polycurve_basic_traits_2.h:1298:45: error: indirection requires pointer operand
```

**`Arr_polyline_traits_2` escapes this only because it adds its own overload**
`X_monotone_curve_2 operator()(const Point_2& p, const Point_2& q) const`
(`Arr_polyline_traits_2.h:470`) that the generic polycurve traits does not have. **[verified]** So
"landmarks works with polylines" does **not** generalise to `Arr_polycurve_traits_2` with any other
subcurve traits.

**³ landmarks on `Arr_circle_segment_traits_2` — CE.** Confirms `traits_circle_segment.md:39`
verbatim:

```
Arr_landmarks_point_location.h:329:37: error: no member named 'construct_x_monotone_curve_2_object'
  in 'CGAL::Arr_traits_basic_adaptor_2<CGAL::Arr_circle_segment_traits_2<CGAL::Epeck>>'
Arr_point_location/Arr_landmarks_pl_impl.h:297:15: error: (same)
```
`grep -c Construct_x_monotone_curve_2 /opt/homebrew/include/CGAL/Arr_circle_segment_traits_2.h` → **0**.
All six generators fail identically, because the failure is in the *strategy*, not the generator.

**⁴ landmarks on `Arr_Bezier_curve_traits_2` — CE. (This was never stated anywhere; now it is.)**
Two independent missing requirements:

```
Arr_point_location/Arr_lm_nearest_neighbor.h:52:39: error: no type named 'Approximate_number_type'
  in 'CGAL::Arr_Bezier_curve_traits_2<…>'
Arr_landmarks_point_location.h:329:37: error: no member named 'construct_x_monotone_curve_2_object'
  in 'CGAL::Arr_traits_basic_adaptor_2<CGAL::Arr_Bezier_curve_traits_2<…>>'
```
`grep -c "Approximate_number_type\|class Approximate_2\|Construct_x_monotone_curve_2"
 Arr_Bezier_curve_traits_2.h` → **0 for all three**. The landmarks strategy is therefore
**structurally impossible** for Bézier curves; there is no work-around short of writing a traits
decorator that supplies `Approximate_2`, `Approximate_number_type` and a
`Construct_x_monotone_curve_2(Point_2, Point_2)`.

For the record, the three traits members the landmarks machinery needs, per header **[verified by grep]**:

| traits | `Approximate_number_type` | `class Approximate_2` | `Construct_x_monotone_curve_2` | `…(Point_2,Point_2)` overload | `Kernel` |
|---|---|---|---|---|---|
| `Arr_segment_traits_2` | yes | yes | yes | yes | yes |
| `Arr_non_caching_segment_traits_2` | yes (from `…_basic_traits_2`) | yes (base) | yes (base) | yes (base) | yes |
| `Arr_linear_traits_2` | yes | yes | yes | yes | yes |
| `Arr_polyline_traits_2` | yes | yes (`:601`, wraps the subcurve traits') | yes | **yes** (`:470`) | **no** |
| `Arr_polycurve_traits_2` | conditional | conditional — `Arr_polycurve_basic_traits_2.h:1099-1125` forwards the subcurve traits' `Approximate_2` through a `has_approximate_2` detector, else `void` | yes (base) | **no** | **no** |
| `Arr_circle_segment_traits_2` | yes | yes | **no** | no | yes |
| `Arr_conic_traits_2` | yes (`:1526`) | yes (`:1648`) | yes | **yes** (`operator()(const Point_2& source, const Point_2& target)`) | **no** |
| `Arr_Bezier_curve_traits_2` | **no** | **no** | **no** | no | **no** |
| `Arr_geodesic_arc_on_sphere_traits_2` | yes | yes | yes | yes | yes (but `Point_2` is a `Direction_3`) |

This table is the *root cause* of every CE in §13.2.

**⁵ landmarks on the sphere — compiles, but aborts at run time depending on the query.**
`traits_geodesic_sphere.md:1032` says landmarks "compiles with this traits **[verified, compile only]**".
It does compile — and it also **runs correctly on some inputs and aborts on others**, because
`Arr_landmarks_point_location::locate` builds a *single geodesic arc from the landmark to the query
point* and that construction carries preconditions:

```
// antipodal landmark/query:
CGAL error: precondition violation!
Expression : !kernel.equal_3_object()(kernel.construct_opposite_direction_3_object()(source),
                                      (const typename Kernel::Direction_3&)(target))
File       : /opt/homebrew/include/CGAL/Arr_geodesic_arc_on_sphere_traits_2.h
Line       : 611
```
```
// landmark & query on a common meridian plane containing the identification:
CGAL error: assertion violation!
Expression : orient1 == orient2
File       : /opt/homebrew/include/CGAL/Arr_geodesic_arc_on_sphere_traits_2.h
Line       : 730
```

Measured **[verified]**: spherical triangle `(1,0,0)-(0,1,0)-(0,0,1)` → all four probes OK with both
`Arr_landmarks_vertices_generator` and `Arr_landmarks_specified_points_generator`; the *same code* on
the pole-free triangle `(4,1,1)-(1,4,1)-(1,1,4)` with probe `q3 = (-1,-1,-1)` aborts at line 730
(vertices generator) and at line 611 (specified-points generator, because `(-1,-1,-1)` is exactly
antipodal to the landmark `(2,2,2)`). **Do not expose the landmarks strategy for spherical
arrangements in the binding** — it is a latent abort, not a graceful failure.

**⁶ `Arr_grid_landmarks_generator` on the sphere — CE.** It builds landmarks from a bounding box and
constructs `Point_2` from two `double`s:

```
Arr_point_location/Arr_lm_grid_generator.h:187:24: error: no matching constructor for initialization of
  'Point_2' (aka 'Arr_extended_direction_3<CGAL::Epeck>')     // points.push_back(Point_2(x_min, y_min));
Arr_point_location/Arr_lm_grid_generator.h:258:26: error: (same)  // points.push_back(Point_2(px, py));
```
Missing requirement: `Point_2` constructible from `(Approximate_number_type, Approximate_number_type)`,
i.e. a 2-D Cartesian point. A `Direction_3` is not one.

**⁷ random / halton / middle-edges generators on the sphere — CE.** In addition to ⁶ they read
Cartesian coordinates straight off `Point_2`:

```
Arr_point_location/Arr_lm_halton_generator.h:92:40: error: no member named 'x' in 'CGAL::Arr_extended_direction_3<CGAL::Epeck>'
Arr_point_location/Arr_lm_halton_generator.h:93:40: error: no member named 'y' in '…'
Arr_point_location/Arr_lm_random_generator.h:93:40: error: no member named 'x' in '…'
Arr_point_location/Arr_lm_middle_edges_generator.h:132:21: error: no member named 'x' in '…'
```
So of the six generators, **only the vertices generator and the specified-points generator are
traits-agnostic**; the other four require a 2-D Cartesian `Point_2` with `x()`, `y()` and a
two-coordinate constructor. (The grid generator is the least demanding of the four: it obtains the
bounding box through `approximate_2_object()` and only needs the constructor.)

**⁸ `Arr_middle_edges_landmarks_generator` on an unbounded arrangement — RT!** It is the only
landmark generator that iterates **edges** rather than vertices:

```cpp
// Arr_lm_middle_edges_generator.h:127-131
for (eit = arr->edges_begin(); eit != arr->edges_end(); ++eit) {
  hh = eit;
  const Point_2& p1 = hh->source()->point();
  const Point_2& p2 = hh->target()->point();
  Point_2 p((p1.x()+p2.x())/2, (p1.y()+p2.y())/2);
```
`edges_begin()` yields the two unbounded rays of the line `y=x`, whose far endpoint is a vertex at
infinity with a null point:

```
CGAL error: assertion violation!
Expression : p_pt != nullptr
File       : /opt/homebrew/include/CGAL/Arr_dcel_base.h
Line       : 105
```
The other five generators iterate `vertices_begin()`, which is the *filtered* concrete-vertex
iterator, so they are unaffected. **[verified: grid / random / halton / vertices / specified all
return the naive answer on the unbounded arrangement, including for the query in the unbounded face.]**

**⁹ `Arr_trapezoid_ric_point_location` on the sphere — depends on where the vertices are.**
**[verified]**

| spherical arrangement | RIC |
|---|---|
| triangle `(4,1,1)-(1,4,1)-(1,1,4)` (no pole, no identification) | **OK** — all four probes match naive; `ray_shoot_up/down` also work (§13.5) |
| triangle `(1,0,0)-(0,1,0)-(0,0,1)` (**a vertex at the north pole**) | **RT!** `CGAL warning: check violation! Expression : traits->is_in_closure(tr, traits->vtx_to_ce(v))` (`Trapezoidal_decomposition_2_impl.h:57`) followed by `CGAL error: precondition violation! Expression : (m_traits->compare_curve_end_x_2_object()(ce1, Curve_end(cv2, ARR_MIN_END)) != SMALLER) && (… != LARGER)` (`Td_point_location/Td_traits.h:423`) |
| triangle with a vertex **on the identification curve** (`(-1,0,1)`) | **RT!** — plain `Segmentation fault: 11` (no CGAL message at all) |

So RIC is usable on a sphere only for arrangements that touch neither pole nor the identification
curve — a condition a binding cannot easily enforce. Treat RIC as **not supported on the sphere**.

**¹⁰ `Arr_triangulation_point_location`: correct on hole-free bounded segment arrangements, silently WRONG on any face that has a hole.** `OK*` in the matrix means "all four probes of the reference arrangement pass"; add one inner CCB and it breaks. See §13.6 —
this is the single most dangerous finding in this section.

**¹¹ `Arr_triangulation_point_location` on an unbounded arrangement — RT!** Already in §0 Gotcha 9 and
§8; re-confirmed here:
`CGAL error: assertion violation! Expression : p_pt != nullptr, Arr_dcel_base.h:105` — same root cause
as ⁸.

**¹² `Arr_triangulation_point_location` — no `Geometry_traits_2::Kernel`.**

```
Arr_triangulation_point_location.h:47:46: error: no type named 'Kernel' in 'CGAL::Arr_polyline_traits_2<>'
Arr_triangulation_point_location.h:47:46: error: no type named 'Kernel' in 'CGAL::Arr_polycurve_traits_2<CGAL::Arr_circle_segment_traits_2<CGAL::Epeck>>'
Arr_triangulation_point_location.h:47:46: error: no type named 'Kernel' in 'CGAL::Arr_conic_traits_2<…>'
Arr_triangulation_point_location.h:47:46: error: no type named 'Kernel' in 'CGAL::Arr_Bezier_curve_traits_2<…>'
```
followed by a cascade of `type 'int' cannot be used prior to '::'` from `Triangulation_2.h:101`
(`Kernel` defaults to `int` through `Default`). The §8 claim "`Geometry_traits_2::Kernel` must exist"
is now cross-checked against **all** seven traits: **only** `Arr_segment_traits_2`,
`Arr_non_caching_segment_traits_2`, `Arr_linear_traits_2`, `Arr_circle_segment_traits_2` and
`Arr_geodesic_arc_on_sphere_traits_2` define `Kernel` at all.

**¹³ `Arr_triangulation_point_location` — `Point_2` not convertible to `Kernel::Point_2`.**
The two traits that *do* have a `Kernel` but a non-Cartesian `Point_2` fail one step later:

```
Arr_point_location/Arr_triangulation_pl_functions.h:70:18:  error: no matching conversion for static_cast from
  'const Point_2' (aka 'const CGAL::_One_root_point_2<…>')      to 'CDT_Point' (aka 'CGAL::Point_2<CGAL::Epeck>')
Arr_point_location/Arr_triangulation_pl_functions.h:223:24: error: (same, for Arr_extended_direction_3<Epeck>)
```
So the practical rule is stronger than §8's: the triangulation strategy needs
**`Geometry_traits_2::Kernel` *and* `static_cast<Kernel::Point_2>(Point_2)` *and* a bounded
arrangement *and* no face with an inner CCB** — which leaves exactly
`Arr_segment_traits_2`, `Arr_non_caching_segment_traits_2` and
`Arrangement_with_history_2<Arr_segment_traits_2>` on hole-free bounded inputs.

**¹⁴ batched `CGAL::locate` on the sphere.** It compiles and runs for every spherical arrangement
tried, including one with a vertex at the north pole. It is **wrong for one case**: a query point that
*is* a vertex lying **on the identification curve** is reported as the containing **face** instead of
the vertex. Measured, single-point queries, naive as reference **[verified]**:

```
q=(1 2 3 0)   naive HALFEDGE #2   batched HALFEDGE #3     (twins - fine)
q=(2 5 2 0)   naive FACE #0       batched FACE #0
q=(-1 0 1 2)  naive VERTEX #0     batched FACE #0         <<< WRONG   (location tag 2 = on identification)
q=(0 -1 0 0)  naive FACE #0       batched FACE #0
```
(The 4th number printed for a spherical `Point_2` is its `location()` enum.) On the pole triangle and
on the pole-free triangle all four probes match naive exactly, including the vertex probe.

**¹⁵ `CGAL::decompose` on the sphere.** It works — but only for arrangements whose vertices all lie in
the open sphere:

| spherical arrangement | `decompose` |
|---|---|
| pole-free, identification-free triangle | **OK**, 3 entries, `below`/`above` are the outer `Face_const_handle` or a `Halfedge_const_handle` |
| a vertex at the **north pole** | **RT!** |
| a vertex **on the identification** | **RT!** |

```
CGAL error: precondition violation!
Expression : p1.is_no_boundary()
File       : /opt/homebrew/include/CGAL/Arr_geodesic_arc_on_sphere_traits_2.h
Line       : 1038          //  Compare_x_2::operator()(const Point_2& p1, const Point_2& p2)
```
`Arr_topology_traits/Arr_spherical_vert_decomp_helper.h` does exist and is instantiated (that is why
the pole-free case works); the abort comes from the *geometry* traits' `Compare_x_2`, whose
`\pre p1 does not lie on the boundary` cannot hold once a vertex sits on a pole or on the
identification. Note the spherical helper never yields `std::nullopt`:

```cpp
// Arr_spherical_vert_decomp_helper.h:88-98
Vert_type top_object() const
{ return (…north pole is a vertex…) ? Vert_type(m_north_pole) : Vert_type(m_north_face); }
Vert_type bottom_object() const
{ return (…south pole is a vertex…) ? Vert_type(m_south_pole) : Vert_type(m_south_face); }
```
— so on the sphere "nothing above" is a `Face_const_handle` (the spherical face), never a fictitious
halfedge as in the unbounded-planar case (§11) and never `nullopt`.

### 13.4 Ray shooting, and the query point in the unbounded face

`ray_shoot_up` / `ray_shoot_down` exist only on **simple**, **walk** and **RIC** (§0 Gotcha 2). Every
cell where those three compile was exercised with a ray from inside a bounded face *and* a ray from the
unbounded-face probe. Results **[verified]**:

| column | `ray_up(q0)` | `ray_down(q0)` | `ray_up(q3)` (from the unbounded face) | `ray_down(q3)` |
|---|---|---|---|---|
| A, B, D, E, H, J (bounded) | `Halfedge` | `Halfedge` | `Face` = the unbounded face | `Face` = the unbounded face |
| C `Arr_linear_traits_2` (unbounded) | `Halfedge` | `Halfedge` | `Halfedge` (the line `y=x`, **not fictitious**) | `Face` = an unbounded face |
| F circle-segment, G conic | `Halfedge` | **`Vertex`** (the circle's left vertical-tangency vertex at `(1,2)`) | `Face` | `Face` |
| I sphere (RIC, pole-free only) | **`Vertex`** | `Halfedge` | `Face` | `Face` |

Three things to take away:

1. **A ray that escapes returns a `Face_const_handle`, never a fictitious halfedge and never an empty
   result** — confirmed on the unbounded `Arr_linear_traits_2` arrangement too. `is_fictitious()` was
   `false` on every halfedge any strategy ever returned, in every column. (Fictitious halfedges *do*
   come out of `CGAL::decompose` on unbounded planar arrangements — §11 — but not out of `locate` or
   `ray_shoot_*`.)
2. **Ray shooting really can return a `Vertex_const_handle`**, and not only for isolated vertices: the
   circle-segment/conic columns hit the tangency vertex `(1,2)` exactly. A binding must handle all
   three alternatives for `ray_shoot_*`, not just halfedge/face.
3. **On the sphere, "vertical" means "along the meridian".** RIC's `ray_shoot_up((2,2,2))` returned the
   vertex `(1,1,4)` and `ray_shoot_down` the arc through `(5,5,2)` — both at longitude 45°, i.e. the
   same meridian, one at higher and one at lower latitude. Verified correct by hand.

**Twin choice is not stable and must not be compared across strategies.** For the same query on the
same arrangement, `Arr_naive_point_location` returned halfedge `#8` while
`Arr_walk_along_line_point_location` returned `#9`; an explicit check prints

```
twin-check q1: naive==walk? 0   naive==walk->twin()? 1
```
**[verified]** RIC even returns a *different twin before and after a `detach()`/`attach()` cycle*
(`#19` then `#18` on the identical arrangement). Binding-level equality of "the same edge" must
therefore be `h == g || h == g->twin()`, or normalise on `Arr_with_history`-style edge identity /
`h->direction()`.

### 13.5 Faces, holes and isolated vertices — the `Arr_triangulation_point_location` trap

This is new and it is severe. `Arr_triangulation_point_location` **compiles, runs, produces no warning
and returns the unbounded face for every query point inside a face that has an inner CCB (a hole)**.

Reference arrangement: outer square `(0,0)-(10,0)-(10,10)-(0,10)` plus inner square
`(3,3)-(7,3)-(7,7)-(3,7)`; the annulus between them is `FACE #1`, the inner square's interior is
`FACE #2`. **[verified]**

```
  q=(1 1)    naive FACE #1 (bounded)    tri FACE #0 (unbounded)   <<< MISMATCH
  q=(9 9)    naive FACE #1              tri FACE #0               <<< MISMATCH
  q=(5 1)    naive FACE #1              tri FACE #0               <<< MISMATCH
  q=(1 5)    naive FACE #1              tri FACE #0               <<< MISMATCH
  q=(9 5)    naive FACE #1              tri FACE #0               <<< MISMATCH
  q=(5 9)    naive FACE #1              tri FACE #0               <<< MISMATCH
  q=(5 5)    naive FACE #2              tri FACE #2               ok
  q=(20 20)  naive FACE #0              tri FACE #0               ok
  mismatches=6
```

Six out of six annulus points wrong, and the strategy does **not** print its own
`"NOT GOOD - face not found"` diagnostic in this case — the failure is silent.

Scope of the bug, established by three further runs **[verified]**:

* **Non-convex faces are fine.** An L-shaped polygon `(0,0)(10,0)(10,4)(4,4)(4,10)(0,10)` gives
  `mismatches=0` for three interior points and one point in the convex-hull-minus-face region.
* **Isolated vertices alone are fine.** A single square plus an isolated vertex inside it: the
  isolated vertex probe returns `VERTEX #4` correctly (the strategy falls back to a linear scan of
  `face_found->isolated_vertices_begin()/end()`).
* **A hole plus an isolated vertex** loses *both*: the interior probe and the isolated-vertex probe
  both return the unbounded face, because the isolated-vertex scan is performed on the *wrong* face.

Mechanism, from `Arr_point_location/Arr_triangulation_pl_impl.h:120-205`: after `cdt.locate()` returns
a CDT face, the arrangement face is recovered by looking for a face incident to all three corner
vertices with three nested circulator loops —

```cpp
Face_const_handle face_found = this->arrangement()->unbounded_face();   // :42, the fallback
…
do {  Face_const_handle f0 = (*havc0).face();
  do { Face_const_handle f1 = (*havc1).face();
    if (f0 == f1) {
      do { Face_const_handle f2 = (*havc2).face();
           if (f1 == f2) {
             if (face_found != f0) { face_found = f0; found = true; }
             else                    found_unbounded = true;          // unbounded face is REFUSED
           }
      } while ((++havc2 != havc2_done) && !found );
    }
  } while ((++havc1 != havc1_done) && !found );
} while ((++havc0 != havc0_done) && !found );
```

`havc1` and `havc2` are **never reset** between iterations of the enclosing loops, so once a
circulator has been walked once the search silently stops finding anything; and the loop is written so
that the unbounded face can never be *selected*, only defaulted to. With a hole present, the CDT
triangles spanning the gap have corners on two different CCBs and the incomplete search fails.

**Consequence for the project: do not offer `Arr_triangulation_point_location` in the binding at all**,
or gate it behind an explicit "no face has an inner CCB" precondition check
(`for (f : arr.face_handles()) if (f->number_of_inner_ccbs() != 0) reject;`). Every other strategy
(naive, simple, walk, all landmark variants, RIC, batched `locate`) returns the correct annulus face
and the correct isolated vertex on the very same arrangement **[verified]**.

### 13.6 Q1 — do observer-backed strategies stay correct across mutation?

**Yes. All of them. No detach/reattach is ever required.** **[verified]**

Test: build the arrangement, construct `Arr_naive_point_location`,
`Arr_walk_along_line_point_location`, `Arr_trapezoid_ric_point_location`,
`Arr_triangulation_point_location`, `Arr_landmarks_point_location<Arr>` (owning generator) and
`Arr_landmarks_point_location<Arr, Arr_grid_landmarks_generator<Base_aos>>` (borrowed generator) — six
strategies, four of them registered observers — then mutate the arrangement **while everything is
attached** and re-query, comparing against a *freshly constructed* naive strategy.

| column | mutation | result |
|---|---|---|
| A `Arr_segment_traits_2` | `insert` a crossing segment (`V 4→7`, `E 5→10`, `F 3→5`), then `arr.remove_edge(arr.edges_begin())` | every strategy agrees with fresh-naive after **both** mutations |
| C `Arr_linear_traits_2` (unbounded) | same | idem |
| J `Arrangement_with_history_2` | `insert` a crossing segment | idem |
| I sphere (naive + RIC only) | `insert` an arc | idem |

`arr.is_valid()` stayed `true` after every mutation with four observers attached. The only differences
between strategies are twin choices (§13.4).

Why each one is safe:

* **naive / simple / walk** hold a raw `const Arrangement_2*` and recompute from scratch on every
  query — automatically current, nothing to invalidate.
* **RIC** is a true incremental observer (`after_split_edge`, `after_merge_edge`, `after_remove_edge`,
  … — §7).
* **triangulation** rebuilds the whole CDT on every notification.
* **landmarks** is *not itself* an observer; its **generator** is, and
  `Arr_landmarks_generator_base::after_create_vertex/after_create_edge/…` call
  `clear_landmark_set(); build_landmark_set();` — a full O(n) rebuild per elementary change (§0
  Gotcha 10). Correct, but quadratic if you insert curve-by-curve.

**Handles returned before a mutation**: `Vertex_const_handle` / `Face_const_handle` obtained before an
insertion remain valid for features that survive (the DCEL is an `In_place_list`), but a split edge's
halfedge handle and a face handle that got merged away are dangling. That is arrangement semantics,
not a point-location property; see `dcel_and_accessor.md`. Nothing in the PL layer caches handles
across queries, so re-querying after a mutation always returns fresh, valid handles.

### 13.7 Q2 — can two strategies be attached to the same arrangement at once?

**Yes, any number, of any mix.** **[verified]** Eight strategies were attached to one arrangement
simultaneously — two `Arr_trapezoid_ric_point_location` (one with `with_guarantees=true`, one with
`false`), two `Arr_triangulation_point_location`, two landmarks strategies (one owning its generator,
one borrowing an `Arr_grid_landmarks_generator<Base_aos>`), plus naive, walk and simple. That is
**four independent registered observers**. All six queryable strategies returned the same answer,
`arr.is_valid()` stayed `true`, and after mutating the arrangement with all four observers attached
every one of them still agreed with a fresh naive strategy.

`Arrangement_on_surface_2` keeps a `std::list` of observer pointers and notifies each in registration
order; there is no "one observer" restriction anywhere.

The one thing you **cannot** do is attach *the same* observer object to a *second* arrangement without
detaching first:

```cpp
Arr a1, a2;
CGAL::Arr_trapezoid_ric_point_location<Arr> ric(a1);
ric.attach(a2);           // <-- aborts
```
```
CGAL error: precondition violation!
Expression : p_arr == nullptr
File       : /opt/homebrew/include/CGAL/Aos_observer.h
Line       : 98
```
**[verified]** `ric.detach(); ric.attach(a2);` is fine, and a `detach()`/`attach()` round-trip on the
same arrangement also works for both RIC and triangulation (`attach` takes a **non-const**
`Base_aos&`). For the non-observer strategies `detach()` + `attach(const Arr&)` is likewise fine and
`attach` is idempotent-safe because it just overwrites a pointer.

### 13.8 Q3 — batched `CGAL::locate` and `CGAL::decompose` across the traits

**Batched point location works for all ten columns.** **[verified]** — including
`Arr_circle_segment_traits_2` and `Arr_Bezier_curve_traits_2`, i.e. exactly the two traits for which
the landmarks strategy is impossible. That is not an accident:
`Arr_point_location/Arr_batched_point_location_traits_2.h` decorates the geometry traits and pulls
only

```
Multiplicity, Construct_min_vertex_2, Construct_max_vertex_2, Compare_x_2, Compare_xy_2,
Compare_y_at_x_2, Compare_y_at_x_right_2, Equal_2, Is_vertical_2, Has_do_intersect_category
```

(and sets `Has_left_category = Tag_false`, `Has_merge_category = Tag_false`). It needs **no**
`Approximate_2`, **no** `Construct_x_monotone_curve_2`, **no** `Kernel` — it is the *weakest*
traits requirement of anything in this file. In other words: **batched `locate` is the only
"fast" point-location facility that is available for every one of the seven geometry traits.**

`CGAL::decompose` uses the same decoration and the same
`No_intersection_surface_sweep_2` machinery, so it likewise works for all ten columns
(§11 for the output-type details). Concrete smoke results **[verified]**:

| column | batched `locate` (4 probes) | `decompose` |
|---|---|---|
| A/B/D/E/H/J bounded | 4 pairs, all matching naive; results ordered by increasing xy of the query point, **not** input order | 4 entries (`n = #vertices`), `below`/`above` ∈ {unbounded `Face`, `nullopt`} |
| C unbounded | 4 pairs matching naive | fictitious halfedges appear (§11 / Gotcha 7) |
| F/G circle-segment & conic | 4 pairs matching naive | 6 entries, `below`/`above` are real `Halfedge_const_handle`s for the circle's tangency vertices |
| I sphere | OK — **except** a query point that is a vertex on the identification (footnote ¹⁴) | OK only when no vertex is on a pole or on the identification (footnote ¹⁵) |
| J `Arrangement_with_history_2` | **OK** — this upgrades the one-line claim at `arrangement_with_history.md:895` from "point-location classes instantiate" to "batched PL and vertical decomposition also work" | **OK**, 4 entries, identical to column A |

`Arrangement_with_history_2` needs no special handling anywhere: `CGAL::locate` and `CGAL::decompose`
are declared on `Arrangement_on_surface_2<Gt2, Tt>` and bind by derived-to-base deduction (§0
Gotcha 13), so the with-history arrangement is decomposed as the plain arrangement it derives from —
you get the ordinary `Vertex_const_handle`/`Halfedge_const_handle`/`Face_const_handle`, and you can go
from a returned halfedge to its originating curves with `arr.originating_curves_begin(he)` afterwards.

An isolated vertex is reported correctly by batched `locate` (`VERTEX #8, degree=0`) **[verified]** —
the sweep feeds isolated vertices in as action points.

### 13.9 The decision table for the type-erased core

Given the seven required traits, the honest support matrix is:

| strategy | expose for | reject for |
|---|---|---|
| `Arr_naive_point_location` | **everything** — the only strategy that works in all ten columns | — |
| `Arr_simple_point_location` | all planar traits (bounded and unbounded) | sphere (CE) |
| `Arr_walk_along_line_point_location` | all planar traits (bounded and unbounded) | sphere (CE) |
| `Arr_landmarks_point_location` (vertices / specified-points generator) | segment, non-caching segment, linear, polyline, conic, with-history | circle-segment (CE), generic polycurve (CE), Bézier (CE), sphere (latent abort) |
| `Arr_landmarks_point_location` (grid / random / halton / middle-edges) | same as above **minus** middle-edges on unbounded | additionally sphere (CE); middle-edges also aborts on unbounded |
| `Arr_trapezoid_ric_point_location` | **all nine planar columns**, bounded and unbounded, with ray shooting | sphere |
| `Arr_triangulation_point_location` | nothing — see §13.5 | everything (wrong on holes, aborts on unbounded, CE for 6 of 10 traits) |
| `CGAL::locate` (batched) | **everything**, weakest traits requirements of all | sphere query points on the identification |
| `CGAL::decompose` | **everything** | sphere with a vertex on a pole / the identification |

Practical recommendation for the Cython layer: expose **naive** (always), **walk** (default for planar),
**RIC** (default for repeated planar queries, the only fast strategy that also does ray shooting), and
**batched `locate`** (the only fast facility that covers circle-segment and Bézier). Offer **landmarks**
only for the five traits where it compiles, and either drop **triangulation** or guard it with the
inner-CCB check. On the sphere, only **naive** and **batched `locate`** are safe for arbitrary input.
