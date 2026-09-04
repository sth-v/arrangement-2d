# CGAL 6.1 — DCEL, extended DCEL, `Arr_accessor`, index maps, BGL graph traits

Source of truth: the **installed** headers at `/opt/homebrew/include/CGAL` (CGAL 6.1, git `b26b07a1242`,
release date 20250929, header-only). Every signature below is quoted verbatim from those headers.
Empirical claims (marked **[verified]**) were checked by compiling and running small programs with

```
/usr/bin/clang++ -std=c++17 -O0 -DCGAL_USE_CORE -DCGAL_USE_GMP -DCGAL_USE_MPFR \
  -I/opt/homebrew/include -L/opt/homebrew/lib -lgmp -lmpfr -o test test.cpp
```

Files covered: `Arr_dcel_base.h`, `Arr_dcel.h`, `Arr_default_dcel.h`, `Arr_extended_dcel.h`,
`Arr_accessor.h`, `Arr_face_index_map.h`, `Arr_vertex_index_map.h`, `Arr_face_map.h`,
`Arr_vertex_map.h`, `graph_traits_Arrangement_2.h`, `graph_traits_dual_arrangement_2.h`,
`Arrangement_2/graph_traits_dual.h`, `Arr_has.h`, plus the parts of `Arrangement_on_surface_2.h`,
`Arrangement_2.h`, `Arrangement_2/Arrangement_2_iterators.h`, `Aos_observer.h`, `Arr_observer.h`,
`In_place_list.h` and `IO/Arr_text_formatter.h` that the above depend on.

---

## Gotchas / surprises vs. older CGAL

1. **`Arr_default_dcel` is now an alias template, not a class.**
   `template <typename Traits> using Arr_default_dcel = Arr_dcel<Traits>;`
   The real class is `CGAL::Arr_dcel<Traits, V, H, F>` (new header `Arr_dcel.h`). Anything that did
   `typename Arr_default_dcel<T>::rebind<...>` still works (the alias resolves to `Arr_dcel`), but you
   cannot forward-declare `Arr_default_dcel` and you cannot partially specialise it.

2. **`Arr_accessor::is_inside_new_face` no longer exists.** It is now
   `bool defines_outer_ccb_of_new_face(Halfedge_handle prev1, Halfedge_handle prev2, const X_monotone_curve_2& cv) const`.
   Similarly `insert_at_vertices_ex` / `insert_from_vertex_ex` / `insert_in_face_interior_ex` now **require
   an explicit `Arr_halfedge_direction cv_dir` argument**, and `insert_at_vertices_ex` gained two extra
   out/in parameters (`bool& swapped_predecessors`, `bool allow_swap_of_predecessors = true`).
   `insert_at_vertices_ex`'s second halfedge is `he_away` — a halfedge whose **source** is the second
   vertex (i.e. `some_predecessor->next()`), *not* a predecessor pointing at it. **[verified]** Passing
   the old "predecessor pointing at v" triggers `CGAL error: assertion violation! local_mins2.size() > 0`.

3. **`Halfedge::inner_ccb()` mutates the DCEL even through a `const` handle.** `Arr_inner_ccb` implements
   union-find path compression (`is_valid()/next()/set_next()`), and the `const` overload does
   `const_cast<...>(this)->set_inner_ccb(valid)`. Since `Arrangement::Halfedge::face()` calls it, **`he->face()`
   on a logically read-only arrangement writes to memory**. Never call it concurrently from more than one
   thread; if you release the Python GIL for "read-only" traversals, you still need exclusive access.

4. **Extended-DCEL data of scalar type is left UNINITIALISED.** `Arr_extended_vertex/halfedge/face` declare
   a bare `Data m_data;` and `Arr_vertex/Arr_halfedge/Arr_face` have *user-provided* default ctors, so
   value-initialisation degenerates to default-initialisation. **[verified]** placement-newing onto a
   poisoned buffer yields `data() == -1414812757` (0xABABABAB). It often *looks* like 0 because fresh heap
   pages are zero. Always `set_data()` on every feature you create, and give `Data` a defaulted member
   initialiser or a user-provided default ctor if you can.

5. **Handles are freely constructible from raw DCEL pointers — publicly.** `Vertex_handle`/`Halfedge_handle`/
   `Face_handle` are `I_Filtered_iterator<...>`, which has `template <typename T> I_Filtered_iterator(T* p)`
   and `template <typename P> I_Filtered_iterator& operator=(const P* p)` in its **public** section.
   So `Arr::Vertex_handle(raw_vertex_ptr)` compiles outside `Arr_accessor`. **[verified]** round-trips
   compare equal to the original handle. *But* such a handle has `iend == nt` and a **default-constructed
   (null) filter**, so it is only valid for `operator*`, `operator->`, `==`, `<`; **never `++` it** — it will
   walk the raw DCEL list unfiltered and never reach `..._end()`.

Additional, still important:

6. DCEL records live in `CGAL::In_place_list` nodes, one allocation per record, never relocated →
   **raw `Vertex*`/`Halfedge*`/`Face*` addresses are stable** for the record's lifetime, across `insert`,
   `split_edge`, `merge_edge`, face splits, etc. **[verified]**. They are invalidated only by the deletion of
   that record (`remove_edge`, `remove_isolated_vertex`, face merge), by `clear()`, by `assign()`/`operator=`
   (which does `delete_all()` first), and by arrangement destruction. Copying an arrangement produces
   **new** addresses.
7. `Arr_face_index_map` / `Arr_vertex_index_map` now derive from `typename Arrangement_::Observer`
   (`= Aos_observer<Base_aos>`), not from `Arr_observer<Arr>`. `CGAL::Arr_observer<Arr>` still exists in
   `Arr_observer.h` but is only `using Arr_observer = typename Arrangement_::Observer;`.
   They also require `Arrangement_::Base_aos` to exist.
8. The BGL free functions (`num_vertices`, `num_edges`, `vertices`, `edges`, `source`, `target`,
   `out_edges`, `degree`, …) for arrangements live in **`namespace CGAL`**, not `namespace boost`.
   `boost::num_vertices(arr)` does **not** compile; call them unqualified and rely on ADL. **[verified]**
9. `Arr_face_map.h` and `Arr_vertex_map.h` are deprecated shims that `#error`-warn and include
   `Arr_face_index_map.h` / `Arr_vertex_index_map.h`.
10. `Arr_point_location_result<Arr>::Type` (returned by `Arr_accessor::locate_curve_end`) is
    `std::variant<Vertex_const_handle, Halfedge_const_handle, Face_const_handle>` — no `CGAL::Object`,
    no `boost::variant`. Internally `topology_traits()->locate_curve_end` returns
    `std::variant<DFace*, DHalfedge*, DVertex*>` and is unpacked with `std::get_if`.
11. `Arr_dcel_base` is **non-copyable**: the copy ctor and `operator=` are declared `private` and undefined.
    `assign()` is the only way to duplicate a DCEL — and it only copies *pointers* to points/curves
    (see §5.4), so it must be followed by the arrangement-level fix-up.
12. `Arr_dcel_base<V,H,F,Allocator>` defines `typedef Arr_dcel_base<V,H,F> Self;` — the allocator parameter
    is dropped. With a non-default allocator, `assign(const Self&)` names a *different* type. Stick to the
    default `CGAL_ALLOCATOR(int)`.
13. `Arr_face_base` still exposes the legacy "hole" spelling: `Hole`, `Hole_iterator`, `number_of_holes()`,
    `holes_begin()/holes_end()` as aliases of the inner-CCB API.

---

## 1. Header map

| Header | Provides |
|---|---|
| `CGAL/Arr_dcel_base.h` | `_clean_pointer/_set_lsb/_is_lsb_set`, `Arr_vertex_base<Point>`, `Arr_halfedge_base<Xcv>`, `Arr_face_base`, `Arr_vertex<V,H,F>`, `Arr_halfedge<V,H,F>`, `Arr_face<V,H,F>`, `Arr_outer_ccb`, `Arr_inner_ccb`, `Arr_isolated_vertex`, `Arr_dcel_base<V,H,F,Allocator>` |
| `CGAL/Arr_dcel.h` | `Arr_dcel<Traits,V,H,F>` (adds only the `rebind` template + ctor/dtor) |
| `CGAL/Arr_default_dcel.h` | `template <typename Traits> using Arr_default_dcel = Arr_dcel<Traits>;` |
| `CGAL/Arr_extended_dcel.h` | `Arr_extended_vertex`, `Arr_extended_halfedge`, `Arr_extended_face`, `Arr_face_extended_dcel`, `Arr_extended_dcel` |
| `CGAL/Arr_accessor.h` | `Arr_accessor<Arrangement>` |
| `CGAL/Arr_face_index_map.h`, `CGAL/Arr_vertex_index_map.h` | observer-backed `readable_property_map`s |
| `CGAL/Arr_face_map.h`, `CGAL/Arr_vertex_map.h` | **deprecated** forwarding headers |
| `CGAL/graph_traits_Arrangement_2.h` | `boost::graph_traits<Arrangement_on_surface_2<...>>`, `boost::graph_traits<Arrangement_2<...>>` + free functions in `namespace CGAL` |
| `CGAL/Arrangement_2/graph_traits_dual.h` | `CGAL::Dual_arrangement_on_surface<Arr>`, `CGAL::Graph_traits_dual_arr_on_surface_impl<Arr>`, the `CGAL_DUAL_ARRANGEMENT_2_*` macros |
| `CGAL/graph_traits_dual_arrangement_2.h` | `CGAL::Dual<Arrangement_2<...>>`, `boost::graph_traits<CGAL::Dual<Arrangement_2<...>>>`, instantiates the macros |
| `CGAL/Arr_has.h` | 24 SFINAE detectors `CGAL::has_xxx_2<T>` for nested traits functor types |
| `CGAL/IO/Arr_text_formatter.h` | `Arr_text_formatter`, `Arr_face_extended_text_formatter`, `Arr_extended_dcel_text_formatter` |

---

## 2. Pointer bit-tricks (`Arr_dcel_base.h`, free inline functions)

```cpp
inline void* _clean_pointer(const void* p);   // p with LSB cleared
inline void* _set_lsb(const void* p);         // p with LSB set
inline bool  _is_lsb_set(const void* p);      // (p & 1) != 0
```
`_clean_pointer` `static_assert`s `sizeof(void*) == sizeof(std::size_t)`.
Three DCEL fields squat on the LSB of a pointer:

* `Arr_vertex_base::p_inc` — LSB set ⇒ the vertex is **isolated** and `p_inc` points to an `Arr_isolated_vertex`.
* `Arr_halfedge_base::p_v` — LSB set ⇒ direction is `ARR_LEFT_TO_RIGHT`.
* `Arr_halfedge_base::p_comp` — LSB set ⇒ the halfedge lies on an **inner** CCB (`p_comp` is an `Arr_inner_ccb*`);
  clear ⇒ outer CCB (`Arr_outer_ccb*`).

Consequence for bindings: **never** hand a DCEL record pointer to something that expects natural alignment
tricks of its own, and never store your own tag bits in these pointers.

---

## 3. Record base classes

### 3.1 `template <typename Point_> class Arr_vertex_base`

```cpp
typedef Point_ Point;
template<typename PNT> struct rebind { typedef Arr_vertex_base<PNT> other; };
```
Protected data: `void* p_inc; Point* p_pt; char pss[2];` (sizeof = 32 with the vptr on LP64).

```cpp
Arr_vertex_base();                       // p_inc=nullptr, p_pt=nullptr, pss = {ARR_INTERIOR, ARR_INTERIOR}
virtual ~Arr_vertex_base();

void* inc() const;                       // raw p_inc (LSB included)
void  set_inc(void* inc) const;          // NOTE: const, const_casts *this

bool has_null_point() const;             // p_pt == nullptr
const Point& point() const;              // CGAL_assertion(p_pt != nullptr)
Point&       point();                    // CGAL_assertion(p_pt != nullptr)
void set_point(Point* p);                // may be nullptr; stores the POINTER, no copy

Arr_parameter_space parameter_space_in_x() const;   // Arr_parameter_space(pss[0])
Arr_parameter_space parameter_space_in_y() const;   // Arr_parameter_space(pss[1])
void set_boundary(Arr_parameter_space ps_x, Arr_parameter_space ps_y);

virtual void assign(const Arr_vertex_base<Point>& v);  // copies p_pt (POINTER!) and pss[0..1]
```
**Ownership:** the vertex does *not* own its `Point`. `Arrangement_on_surface_2` allocates points from
`Points_alloc` (`_new_point` / `_delete_point`) and stores the pointer here. `assign` is a shallow
pointer copy — see §5.4.

### 3.2 `template <typename X_monotone_curve_> class Arr_halfedge_base`

```cpp
typedef X_monotone_curve_ X_monotone_curve;
template<typename XCV> struct rebind { typedef Arr_halfedge_base<XCV> other; };
```
Protected data: `void* p_opp, *p_prev, *p_next, *p_v, *p_comp; X_monotone_curve* p_cv;`

```cpp
Arr_halfedge_base();                     // all six pointers nullptr
virtual ~Arr_halfedge_base();

bool has_null_curve() const;             // p_cv == nullptr  → "fictitious" halfedge
const X_monotone_curve& curve() const;   // CGAL_precondition(p_cv != nullptr)
X_monotone_curve&       curve();         // CGAL_precondition(p_cv != nullptr)
void set_curve(X_monotone_curve* c);     // *** also sets the OPPOSITE halfedge's curve ***

virtual void assign(const Arr_halfedge_base<X_monotone_curve>& he);  // copies p_cv (POINTER!) only
```
**Precondition on `set_curve`:** it unconditionally does
`reinterpret_cast<Arr_halfedge_base*>(p_opp)->p_cv = c;` — `p_opp` must already be non-null, i.e. the
halfedge must have been created through `Arr_dcel_base::new_edge()` (which pairs the two halves first).

### 3.3 `class Arr_face_base`

```cpp
typedef std::list<void*>                      Outer_ccbs_container;
typedef Outer_ccbs_container::iterator        Outer_ccb_iterator;
typedef Outer_ccbs_container::const_iterator  Outer_ccb_const_iterator;
typedef std::list<void*>                      Inner_ccbs_container;
typedef Inner_ccbs_container::iterator        Inner_ccb_iterator;
typedef Inner_ccbs_container::const_iterator  Inner_ccb_const_iterator;
typedef std::list<void*>                      Isolated_vertices_container;
typedef Isolated_vertices_container::iterator Isolated_vertex_iterator;
typedef Isolated_vertices_container::const_iterator Isolated_vertex_const_iterator;
```
Protected: `enum { IS_UNBOUNDED = 1, IS_FICTITIOUS = 2 }; int flags;` plus the three `std::list<void*>`s
(`outer_ccbs`, `inner_ccbs`, `iso_verts`).

```cpp
Arr_face_base();                         // flags = 0
virtual ~Arr_face_base();

bool is_unbounded() const;               // flags & IS_UNBOUNDED
void set_unbounded(bool unbounded);
bool is_fictitious() const;              // flags & IS_FICTITIOUS
void set_fictitious(bool fictitious);

virtual void assign(const Arr_face_base& f);   // copies `flags` ONLY (not the CCB lists)
```
The three `std::list<void*>` members are `std::list` nodes: **inserting/erasing CCBs does not invalidate
other CCB iterators**, which is why `Arr_outer_ccb`/`Arr_inner_ccb`/`Arr_isolated_vertex` can cache one.

---

## 4. Concrete DCEL records

All three derive from `In_place_list_base<Self>` (adds `Self* next_link; Self* prev_link;`, 16 bytes on
LP64). One `operator new` per record via the DCEL's allocators; **records are never moved**.

Measured on `Arr_segment_traits_2<Epeck>` with the default DCEL: `sizeof(Vertex)=48`,
`sizeof(Halfedge)=80`, `sizeof(Face)=128` **[verified]**.

### 4.1 `template <class V, class H, class F> class Arr_vertex : public V, public In_place_list_base<Arr_vertex<V,H,F>>`

```cpp
typedef V                           Base;
typedef Arr_vertex<V,H,F>           Vertex;
typedef Arr_halfedge<V,H,F>         Halfedge;
typedef Arr_isolated_vertex<V,H,F>  Isolated_vertex;

Arr_vertex();                                     // user-provided, empty body (see gotcha 4)

bool is_isolated() const;                         // _is_lsb_set(p_inc)

const Halfedge* halfedge() const;                 // \pre ! is_isolated()
Halfedge*       halfedge();                       // \pre ! is_isolated()
void            set_halfedge(Halfedge* he);       // clears the LSB (marks non-isolated)

const Isolated_vertex* isolated_vertex() const;   // \pre is_isolated()
Isolated_vertex*       isolated_vertex();         // \pre is_isolated()
void set_isolated_vertex(Isolated_vertex* iv);    // sets the LSB
```
Note there is **no** `Arr_vertex::assign` — the inherited (virtual) `V::assign` is used.

### 4.2 `template <class V, class H, class F> class Arr_halfedge : public H, public In_place_list_base<...>`

```cpp
typedef H                     Base;
typedef Arr_vertex<V,H,F>     Vertex;
typedef Arr_halfedge<V,H,F>   Halfedge;
typedef Arr_face<V,H,F>       Face;
typedef Arr_outer_ccb<V,H,F>  Outer_ccb;
typedef Arr_inner_ccb<V,H,F>  Inner_ccb;

Arr_halfedge();

const Halfedge* opposite() const;
Halfedge*       opposite();
void            set_opposite(Halfedge* he);

Arr_halfedge_direction direction() const;         // LSB(p_v) ? ARR_LEFT_TO_RIGHT : ARR_RIGHT_TO_LEFT
void set_direction(Arr_halfedge_direction dir);   // sets BOTH this and opposite (opposite must be set)

const Halfedge* prev() const;
Halfedge*       prev();
void            set_prev(Halfedge* he);           // also sets he->p_next = this
const Halfedge* next() const;
Halfedge*       next();
void            set_next(Halfedge* he);           // also sets he->p_prev = this

const Vertex* vertex() const;                     // TARGET vertex; LSB masked off
Vertex*       vertex();
void          set_vertex(Vertex* v);              // preserves the direction LSB

bool is_on_outer_ccb() const;                     // !_is_lsb_set(p_comp)
const Outer_ccb* outer_ccb() const;               // \pre ! is_on_inner_ccb()
Outer_ccb*       outer_ccb();                     // \pre ! is_on_inner_ccb()
void set_outer_ccb(Outer_ccb* oc);                // clears the LSB

bool is_on_inner_ccb() const;                     // _is_lsb_set(p_comp)
const Inner_ccb* inner_ccb() const;               // \pre is_on_inner_ccb(); MUTATES (path compression)
Inner_ccb*       inner_ccb();                     // \pre is_on_inner_ccb(); MUTATES (path compression)
Inner_ccb*       inner_ccb_no_redirect();         // \pre is_on_inner_ccb(); raw, no compression
void set_inner_ccb(const Inner_ccb* ic);          // sets the LSB
```
`inner_ccb()` walks `out->next()` until it finds a valid `Arr_inner_ccb`, then rewrites both the
intermediate record and `this` (`const_cast` in the const overload). See gotcha 3.
`inner_ccb_no_redirect()` is the read-only escape hatch — use it if you must inspect from a reader thread.

### 4.3 `template <class V, class H, class F> class Arr_face : public F, public In_place_list_base<...>`

```cpp
typedef F                          Base;
typedef Arr_vertex<V,H,F>          Vertex;
typedef Arr_halfedge<V,H,F>        Halfedge;
typedef Arr_face<V,H,F>            Face;
typedef Arr_outer_ccb<V,H,F>       Outer_ccb;
typedef Arr_inner_ccb<V,H,F>       Inner_ccb;
typedef Arr_isolated_vertex<V,H,F> Isolated_vertex;
typedef Inner_ccb                  Hole;                 // backward compat

Arr_face();
```
Outer CCBs (`Iterator_project` over `std::list<void*>::iterator` with `Cast_function_object<void*, Halfedge*>`;
**dereferencing yields `Halfedge*`, a pointer, not a reference**):
```cpp
typedef Iterator_project<typename F::Outer_ccb_iterator, _Ccb_to_halfedge_cast>       Outer_ccb_iterator;
typedef Iterator_project<typename F::Outer_ccb_const_iterator, _Const_ccb_to_halfedge_cast>
                                                                                     Outer_ccb_const_iterator;
std::size_t number_of_outer_ccbs() const;
Outer_ccb_iterator       outer_ccbs_begin();
Outer_ccb_iterator       outer_ccbs_end();
Outer_ccb_const_iterator outer_ccbs_begin() const;
Outer_ccb_const_iterator outer_ccbs_end()   const;
void add_outer_ccb(Outer_ccb* oc, Halfedge* h);   // pushes h, stores the list iterator in oc
void erase_outer_ccb(Outer_ccb* oc);              // erases via oc->iterator().current_iterator()
```
Inner CCBs (identical shape):
```cpp
typedef Iterator_project<typename F::Inner_ccb_iterator, _Ccb_to_halfedge_cast>       Inner_ccb_iterator;
typedef Iterator_project<typename F::Inner_ccb_const_iterator, _Const_ccb_to_halfedge_cast>
                                                                                     Inner_ccb_const_iterator;
typedef Inner_ccb_iterator       Hole_iterator;
typedef Inner_ccb_const_iterator Hole_const_iterator;
std::size_t number_of_inner_ccbs() const;
Inner_ccb_iterator       inner_ccbs_begin();
Inner_ccb_iterator       inner_ccbs_end();
Inner_ccb_const_iterator inner_ccbs_begin() const;
Inner_ccb_const_iterator inner_ccbs_end()   const;
void add_inner_ccb(Inner_ccb* ic, Halfedge* h);
void erase_inner_ccb(Inner_ccb* ic);
Inner_ccb_iterator splice_inner_ccbs(Arr_face& other);   // moves ALL inner CCBs from `other`; re-points
                                                         // each moved ccb's iterator + face; returns an
                                                         // iterator to the first moved element
// backward compatibility:
std::size_t number_of_holes() const;
Hole_iterator       holes_begin();  Hole_iterator       holes_end();
Hole_const_iterator holes_begin() const; Hole_const_iterator holes_end() const;
```
Isolated vertices (`I_Dereference_iterator`, **dereferences to `Vertex&`**):
```cpp
typedef I_Dereference_iterator<typename F::Isolated_vertex_iterator, Vertex,
                               typename F::Isolated_vertex_iterator::difference_type,
                               typename F::Isolated_vertex_iterator::iterator_category>
                                                                        Isolated_vertex_iterator;
typedef I_Dereference_const_iterator<typename F::Isolated_vertex_const_iterator,
                                     typename F::Isolated_vertex_iterator, Vertex,
                                     ...>                               Isolated_vertex_const_iterator;
std::size_t number_of_isolated_vertices() const;
Isolated_vertex_iterator       isolated_vertices_begin();
Isolated_vertex_iterator       isolated_vertices_end();
Isolated_vertex_const_iterator isolated_vertices_begin() const;
Isolated_vertex_const_iterator isolated_vertices_end()   const;
void add_isolated_vertex(Isolated_vertex* iv, Vertex* v);
void erase_isolated_vertex(Isolated_vertex* iv);
Isolated_vertex_iterator splice_isolated_vertices(Arr_face& other);
```

### 4.4 `Arr_outer_ccb<V,H,F>` (`: In_place_list_base<Arr_outer_ccb<V,H,F>>`)

```cpp
typedef Arr_outer_ccb<V,H,F>              Self;
typedef Arr_halfedge<V,H,F>               Halfedge;
typedef Arr_face<V,H,F>                   Face;
typedef typename Face::Outer_ccb_iterator Outer_ccb_iterator;

Arr_outer_ccb();                            // p_f=nullptr, iter_is_not_singular=false
Arr_outer_ccb(const Arr_outer_ccb& other);  // copies iter only if other.iter_is_not_singular

const Halfedge* halfedge() const;           // *iter
Halfedge*       halfedge();
void            set_halfedge(Halfedge* he); // *iter = he
const Face*     face() const;
Face*           face();
void            set_face(Face* f);
Outer_ccb_iterator iterator() const;        // CGAL_assertion(iter_is_not_singular)
Outer_ccb_iterator iterator();
void set_iterator(Outer_ccb_iterator it);
```

### 4.5 `Arr_inner_ccb<V,H,F>` (`: In_place_list_base<...>`) — union-find

```cpp
typedef Arr_inner_ccb<V,H,F>              Self;
typedef Arr_halfedge<V,H,F>               Halfedge;
typedef Arr_face<V,H,F>                   Face;
typedef typename Face::Inner_ccb_iterator Inner_ccb_iterator;
// private: union { Face* f; Arr_inner_ccb* icc; } f_or_icc; Inner_ccb_iterator iter;
//          enum { ITER_IS_SINGULAR, ITER_IS_NOT_SINGULAR, INVALID } status;

Arr_inner_ccb();                            // status = ITER_IS_SINGULAR, f_or_icc.f = nullptr
Arr_inner_ccb(const Arr_inner_ccb& other);

const Halfedge* halfedge() const;           // CGAL_assertion(is_valid())
Halfedge*       halfedge();                 // CGAL_assertion(is_valid())
void            set_halfedge(Halfedge* he); // CGAL_assertion(is_valid())
const Face*     face() const;               // CGAL_assertion(status != INVALID)
Face*           face();                     // CGAL_assertion(status != INVALID)
void            set_face(Face* f);          // CGAL_assertion(status != INVALID)
Inner_ccb_iterator iterator() const;        // CGAL_assertion(status == ITER_IS_NOT_SINGULAR)
Inner_ccb_iterator iterator();
void set_iterator(Inner_ccb_iterator it);   // CGAL_assertion(is_valid()); status := ITER_IS_NOT_SINGULAR

bool  is_valid() const;                     // status != INVALID
Arr_inner_ccb* next() const;                // CGAL_assertion(status == INVALID)
void  set_next(Arr_inner_ccb* next);        // status := INVALID; f_or_icc.icc = next
```
`set_next` is used only in **sweep mode** (`Arrangement_on_surface_2::m_sweep_mode`, set during
global sweep-based insertion). Outside sweep mode the redundant `Arr_inner_ccb` is deleted eagerly.
Consequence: during/after a global sweep, `dcel.size_of_inner_ccbs()` can exceed
`Σ face->number_of_inner_ccbs()`.

### 4.6 `Arr_isolated_vertex<V,H,F>` (`: In_place_list_base<...>`)

```cpp
typedef Arr_isolated_vertex<V,H,F>              Self;
typedef Arr_face<V,H,F>                         Face;
typedef typename Face::Isolated_vertex_iterator Isolated_vertex_iterator;

Arr_isolated_vertex();
Arr_isolated_vertex(const Arr_isolated_vertex& other);
const Face* face() const;
Face*       face();
void        set_face(Face* f);
Isolated_vertex_iterator iterator() const;   // CGAL_assertion(iter_is_not_singular)
Isolated_vertex_iterator iterator();
void set_iterator(Isolated_vertex_iterator iv);
```

---

## 5. `Arr_dcel_base` — the container

```cpp
template <class V, class H, class F, class Allocator = CGAL_ALLOCATOR(int)>
class Arr_dcel_base;
```

### 5.1 Public typedefs

```cpp
typedef Arr_dcel_base<V,H,F>        Self;              // NB: Allocator dropped (gotcha 12)
typedef Arr_vertex<V,H,F>           Vertex;
typedef Arr_halfedge<V,H,F>         Halfedge;
typedef Arr_face<V,H,F>             Face;
typedef Arr_outer_ccb<V,H,F>        Outer_ccb;
typedef Arr_inner_ccb<V,H,F>        Inner_ccb;
typedef Arr_isolated_vertex<V,H,F>  Isolated_vertex;
typedef Inner_ccb                   Hole;

typedef typename Halfedge_list::size_type       Size;        // == size_type
typedef typename Halfedge_list::size_type       size_type;
typedef typename Halfedge_list::difference_type difference_type;   // == Difference
typedef typename Halfedge_list::difference_type Difference;
typedef std::bidirectional_iterator_tag         iterator_category;

typedef typename Vertex_list::iterator          Vertex_iterator;
typedef typename Halfedge_list::iterator        Halfedge_iterator;
typedef typename Face_list::iterator            Face_iterator;
typedef CGAL::N_step_adaptor_derived<Halfedge_iterator, 2> Edge_iterator;   // every 2nd halfedge
typedef typename Inner_ccb_list::iterator       Inner_ccb_iterator;

typedef typename Vertex_list::const_iterator    Vertex_const_iterator;
typedef typename Halfedge_list::const_iterator  Halfedge_const_iterator;
typedef typename Face_list::const_iterator      Face_const_iterator;
typedef CGAL::N_step_adaptor_derived<Halfedge_const_iterator, 2> Edge_const_iterator;
```

### 5.2 Storage (protected)

```cpp
typedef In_place_list<Vertex, false>          Vertex_list;      // `false` ⇒ the list does NOT own/destroy
typedef In_place_list<Halfedge, false>        Halfedge_list;
typedef In_place_list<Face, false>            Face_list;
typedef In_place_list<Outer_ccb, false>       Outer_ccb_list;
typedef In_place_list<Inner_ccb, false>       Inner_ccb_list;
typedef In_place_list<Isolated_vertex, false> Iso_vert_list;

typedef std::allocator_traits<Allocator> Allocator_traits;
typedef typename Allocator_traits::template rebind_alloc<Vertex>          Vertex_allocator;
typedef typename Allocator_traits::template rebind_alloc<Halfedge>        Halfedge_allocator;
typedef typename Allocator_traits::template rebind_alloc<Face>            Face_allocator;
typedef typename Allocator_traits::template rebind_alloc<Outer_ccb>       Outer_ccb_allocator;
typedef typename Allocator_traits::template rebind_alloc<Inner_ccb>       Inner_ccb_allocator;
typedef typename Allocator_traits::template rebind_alloc<Isolated_vertex> Iso_vert_allocator;

Vertex_list vertices; Halfedge_list halfedges; Face_list faces;
Outer_ccb_list out_ccbs; Inner_ccb_list in_ccbs; Iso_vert_list iso_verts;
Vertex_allocator vertex_alloc;  Halfedge_allocator halfedge_alloc;  Face_allocator face_alloc;
Outer_ccb_allocator out_ccb_alloc; Inner_ccb_allocator in_ccb_alloc; Iso_vert_allocator iso_vert_alloc;
```
`In_place_list<T,false>` stores the intrusive `prev_link/next_link` **inside** each record; records are
individually `allocate(1)`d and `deallocate(…,1)`d. Nothing is ever relocated → address stability.

Copy is disabled:
```cpp
private:
  Arr_dcel_base(const Self&);        // declared, not defined
  Self& operator=(const Self&);      // declared, not defined
```

### 5.3 Public member functions

```cpp
Arr_dcel_base();
~Arr_dcel_base();                    // calls delete_all()

// sizes
Size size_of_vertices()           const;   // vertices.size()
Size size_of_halfedges()          const;   // 2 * number of edges
Size size_of_faces()              const;
Size size_of_outer_ccbs()         const;
Size size_of_inner_ccbs()         const;
Size size_of_isolated_vertices()  const;

// mutable iteration
Vertex_iterator   vertices_begin();   Vertex_iterator   vertices_end();
Halfedge_iterator halfedges_begin();  Halfedge_iterator halfedges_end();
Face_iterator     faces_begin();      Face_iterator     faces_end();
Edge_iterator     edges_begin();      Edge_iterator     edges_end();     // 2-step over halfedges
Inner_ccb_iterator inner_ccbs_begin(); Inner_ccb_iterator inner_ccbs_end();
Iterator_range<Prevent_deref<Vertex_iterator> >   vertex_handles();
Iterator_range<Prevent_deref<Halfedge_iterator> > halfedge_handles();
Iterator_range<Prevent_deref<Face_iterator> >     face_handles();
Iterator_range<Prevent_deref<Edge_iterator> >     edge_handles();

// const iteration (same names, const overloads)
Vertex_const_iterator   vertices_begin()  const;  Vertex_const_iterator   vertices_end()  const;
Halfedge_const_iterator halfedges_begin() const;  Halfedge_const_iterator halfedges_end() const;
Face_const_iterator     faces_begin()     const;  Face_const_iterator     faces_end()     const;
Edge_const_iterator     edges_begin()     const;  Edge_const_iterator     edges_end()     const;
Iterator_range<Prevent_deref<Vertex_const_iterator> >   vertex_handles()   const;
Iterator_range<Prevent_deref<Halfedge_const_iterator> > halfedge_handles() const;
Iterator_range<Prevent_deref<Face_const_iterator> >     face_handles()     const;
Iterator_range<Prevent_deref<Edge_const_iterator> >     edge_handles()     const;

// creation — each returns a raw, stable pointer to a fresh record appended to its list
Vertex*          new_vertex();
Halfedge*        new_edge();               // creates TWO halfedges, pairs them, returns the first
Face*            new_face();
Outer_ccb*       new_outer_ccb();
Inner_ccb*       new_inner_ccb();
Isolated_vertex* new_isolated_vertex();

// deletion — erase from the list, destroy, deallocate
void delete_vertex(Vertex* v);
void delete_edge(Halfedge* h);             // deletes h AND h->opposite()
void delete_face(Face* f);
void delete_outer_ccb(Outer_ccb* oc);
void delete_inner_ccb(Inner_ccb* ic);
void delete_isolated_vertex(Isolated_vertex* iv);
void delete_all();                         // all six lists

void assign(const Self& dcel);             // see 5.4

protected:
Halfedge* _new_halfedge();                 // single, unpaired halfedge
void      _delete_halfedge(Halfedge* h);   // single halfedge
```
`new_edge()` returns `h1` with `h1->set_opposite(h2); h2->set_opposite(h1);` and nothing else set —
no vertex, no CCB, no curve. Deleting a record **does not** delete the `Point`/`X_monotone_curve_2` it
points at; that is the arrangement's job (`_delete_point` / `_delete_curve`).

### 5.4 `void assign(const Self& dcel)` — exactly what it copies

1. `delete_all()` on `*this`.
2. Creates a duplicate of every vertex / halfedge / face and calls the **virtual** `assign` on it:
   `dup_v->assign(*vit)`, `dup_h->assign(*hit)`, `dup_f->assign(*fit)`.
   ⇒ **extended data IS copied** (`Arr_extended_*::assign` chains to the base then copies `m_data`).
   ⇒ **`Point*` and `X_monotone_curve_2*` are copied as raw pointers — shared with the source DCEL.**
3. Creates a duplicate of every `Outer_ccb`, `Inner_ccb`, `Isolated_vertex` (no `assign`; fields are rebuilt).
4. Rewires everything through `std::map<const X*, X*>` translation tables: vertex→halfedge or
   →isolated-vertex record; halfedge→vertex/opposite/prev/next/direction and outer-or-inner CCB;
   face→`set_unbounded`, `set_fictitious`, and the three per-face lists.

**Therefore `Arr_dcel_base::assign` alone leaves the new DCEL aliasing the old geometry.** The public path is
`Arrangement_on_surface_2::assign(const Self&)` (`Arrangement_2/Arrangement_on_surface_2_impl.h:156`), which:
`clear()` → `_notify_before_assign(arr)` → `m_topol_traits.assign(arr.m_topol_traits)` (this is what calls
`m_dcel.assign(other.m_dcel)`) → walks `_dcel().vertices_begin()..end()` and replaces every non-null
`point()` with `_new_point(p_v->point())`, walks `_dcel().edges_begin()..end()` and replaces every non-null
`curve()` with `_new_curve(p_e->curve())` → fixes up the traits ownership → `_notify_after_assign()`.
`operator=` forwards to `assign` after a self-assignment guard.

**[verified]** `Arr arr2(arr);` and `arr3.assign(arr)` both preserve `Arr_extended_dcel` vertex/halfedge/face
data (sum of `int` vertex data identical, `std::string` face data identical), and the record addresses in the
copy differ from the original.

---

## 6. `Arr_dcel` and `Arr_default_dcel`

```cpp
template <typename Traits,
          typename V = Arr_vertex_base<typename Traits::Point_2>,
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
  Arr_dcel();
  virtual ~Arr_dcel();
};

template <typename Traits> using Arr_default_dcel = Arr_dcel<Traits>;   // Arr_default_dcel.h
```
`rebind` is what `Arrangement_with_history_2`, the topology traits and `Arr_overlay_2` use to re-instantiate
the DCEL over a different traits class. **A custom `V`/`H` must supply its own `template <typename> struct rebind`.**
Note `F` is *not* rebound.

---

## 7. Extended DCELs (`Arr_extended_dcel.h`)

### 7.1 The three record mixins

```cpp
template <typename VertexBase, typename VertexData>
class Arr_extended_vertex : public VertexBase {
public:
  typedef VertexData Data;
  const Data& data() const;                 // { return m_data; }
  Data&       data();
  void        set_data(const Data& data);   // { m_data = data; }
  virtual void assign(const Vertex_base& v);// VertexBase::assign(v); then m_data = static_cast<const Self&>(v).m_data
  template <typename Point_> struct rebind {
    using other = Arr_extended_vertex<typename VertexBase::template rebind<Point_>::other, VertexData>;
  };
private:
  Data m_data;                              // NOT initialised (gotcha 4)
};

template <typename HalfedgeBase, typename HalfedgeData>
class Arr_extended_halfedge : public HalfedgeBase {   // identical shape
public:
  typedef HalfedgeData Data;
  const Data& data() const;  Data& data();  void set_data(const Data& data);
  virtual void assign(const Halfedge_base& he);
  template <typename XMonotoneCurve> struct rebind { using other = Arr_extended_halfedge<...>; };
};

template <typename FaceBase, typename FaceData>
class Arr_extended_face : public FaceBase {           // NO rebind member
public:
  typedef FaceData Data;
  const Data& data() const;  Data& data();  void set_data(const Data& data);
  virtual void assign(const Face_base& f);
};
```
`assign` does an **unchecked `static_cast<const Self&>`** on its argument. Only ever `assign` between DCELs
of the same instantiation (which is all `Arr_dcel_base::assign` ever does).

### 7.2 `Arr_face_extended_dcel`

```cpp
template <typename Traits_, typename FaceData,
          typename VertexBase   = Arr_vertex_base<typename Traits_::Point_2>,
          typename HalfedgeBase = Arr_halfedge_base<typename Traits_::X_monotone_curve_2>,
          typename FaceBase     = Arr_face_base>
class Arr_face_extended_dcel :
  public Arr_dcel_base<VertexBase, HalfedgeBase, Arr_extended_face<FaceBase, FaceData>>
{
public:
  using Face_base = FaceBase;
  using Face_data = FaceData;
  template <typename T> class rebind {          // note: `class`, members below are public
  public:
    using other = Arr_face_extended_dcel<T, Face_data,
                                         typename VertexBase::template rebind<typename T::Point_2>::other,
                                         typename HalfedgeBase::template rebind<typename T::X_monotone_curve_2>::other,
                                         Face_base>;
  };
  Arr_face_extended_dcel();
  virtual ~Arr_face_extended_dcel();
};
```

### 7.3 `Arr_extended_dcel`

```cpp
template <typename Traits_,
          typename VertexData, typename HalfedgeData, typename FaceData,
          typename VertexBase   = Arr_vertex_base<typename Traits_::Point_2>,
          typename HalfedgeBase = Arr_halfedge_base<typename Traits_::X_monotone_curve_2>,
          typename FaceBase     = Arr_face_base>
class Arr_extended_dcel :
  public Arr_dcel_base<Arr_extended_vertex<VertexBase, VertexData>,
                       Arr_extended_halfedge<HalfedgeBase, HalfedgeData>,
                       Arr_extended_face<FaceBase, FaceData>>
{
public:
  using Vertex_data = VertexData;  using Halfedge_data = HalfedgeData;  using Face_data = FaceData;
  using Vertex_base = VertexBase;  using Halfedge_base = HalfedgeBase;  using Face_base = FaceBase;
  template <typename T> struct rebind { using other = Arr_extended_dcel<T, Vertex_data, Halfedge_data,
                                                                        Face_data, Vertex_other,
                                                                        Halfedge_other, Face_base>; };
  Arr_extended_dcel();
  virtual ~Arr_extended_dcel();
};
```
Note the parameter order in the task description (`<Traits, VertexData, HalfedgeData, FaceData, V_base, H_base, F_base>`)
is exactly right.

### 7.4 Requirements on the `Data` types — what the headers actually need

| Operation | Requirement on `Data` |
|---|---|
| record construction (`new_vertex`/`new_edge`/`new_face`) | **default constructible** (called via `std::allocator_traits::construct(a, p)` ⇒ `Data()` as a subobject default-init; scalars end up indeterminate) |
| `set_data(const Data&)` | **copy assignable** |
| `assign()` (arrangement copy/assign) | **copy assignable** |
| `Arr_extended_dcel_text_formatter::write_*_data` | `operator<<(std::ostream&, const Data&)` |
| `Arr_extended_dcel_text_formatter::read_*_data` | **default constructible** + `operator>>(std::istream&, Data&)` |

`Data` does **not** need to be copy-constructible for `assign` (only assignable), but the formatter's read
path does default-construct a local. In practice: make it default-constructible, copy-constructible,
copy-assignable, and stream-able. Do **not** put a raw owning pointer / `unique_ptr` in it — `assign`
would double-own.

### 7.5 Data lifecycle you must handle yourself

* Data is **not** propagated on topological change. `after_split_face` creates a *fresh* `Face` with
  default (indeterminate) data; `_new_halfedge()` for a split edge likewise. **[verified]** after inserting
  new curves into an `Arr_extended_dcel<..., std::string>` arrangement whose original faces had data,
  the newly created faces read back as `""`.
  ⇒ If you want inheritance-on-split, attach an observer overriding `after_split_face`,
  `after_split_edge`, `before_merge_face`, `after_create_vertex`, …
* Data **is** preserved by copy-construction, `operator=`, `assign()` and by the text formatters.
* `Arr_overlay_2` needs an `Arr_*_overlay_traits` object to compute the data of overlay features;
  the DCEL alone never merges data.

### 7.6 I/O formatters (`CGAL/IO/Arr_text_formatter.h`)

```cpp
template <class Arrangement_> class Arr_text_formatter {
  typedef Arrangement_ Arrangement_2;  typedef typename Arrangement_2::Size Size;
  typedef typename Arrangement_2::Dcel Dcel;
  typedef typename Arrangement_2::X_monotone_curve_2 X_monotone_curve_2;
  typedef typename Arrangement_2::Point_2 Point_2;
  typedef typename Arrangement_2::Vertex_handle       Vertex_handle;        // + Halfedge_handle, Face_handle
  typedef typename Arrangement_2::Vertex_const_handle Vertex_const_handle;  // + Halfedge/Face const handles
  Arr_text_formatter();
  Arr_text_formatter(std::ostream& os);
  Arr_text_formatter(std::istream& is);
  // hook points (all virtual, all no-ops in the base):
  virtual void write_point(const Point_2& p);                  // out() << p
  virtual void write_vertex_data(Vertex_const_handle);
  virtual void write_x_monotone_curve(const X_monotone_curve_2& cv);
  virtual void write_halfedge_data(Halfedge_const_handle);
  virtual void write_face_data(Face_const_handle);
  virtual void read_point(Point_2& p); virtual void read_vertex_data(Vertex_handle);
  virtual void read_x_monotone_curve(X_monotone_curve_2& cv);
  virtual void read_halfedge_data(Halfedge_handle);
  virtual void read_face_data(Face_handle);
  // plus non-virtual layout helpers: write_(vertex|edge|face)_begin/end, write_vertex_index(int),
  // write_halfedge_index(int), write_outer_ccbs_begin/end, write_inner_ccbs_begin/end,
  // write_ccb_halfedges_begin/end, and the matching read_* / _skip_until_EOL / _write_comment.
};

template <class Arrangement_>
class Arr_face_extended_text_formatter : public Arr_text_formatter<Arrangement_> {
  Arr_face_extended_text_formatter();
  Arr_face_extended_text_formatter(std::ostream& os);
  Arr_face_extended_text_formatter(std::istream& is);
  virtual void write_face_data(Face_const_handle f);   // out() << f->data() << '\n'
  virtual void read_face_data(Face_handle f);          // in() >> f->data(); _skip_until_EOL()
};

template <class Arrangement_>
class Arr_extended_dcel_text_formatter : public Arr_text_formatter<Arrangement_> {
  Arr_extended_dcel_text_formatter();
  Arr_extended_dcel_text_formatter(std::ostream& os);
  Arr_extended_dcel_text_formatter(std::istream& is);
  virtual void write_vertex_data(Vertex_const_handle v);     // out() << '\n' << v->data()
  virtual void read_vertex_data(Vertex_handle v);            // Data_type deduced via decltype(...data())
  virtual void write_halfedge_data(Halfedge_const_handle he);
  virtual void read_halfedge_data(Halfedge_handle he);
  virtual void write_face_data(Face_const_handle f);
  virtual void read_face_data(Face_handle f);
};
```
The `read_*_data` overrides deduce the data type as
`std::remove_reference_t<decltype(std::declval<Arrangement_2::Vertex>().data())>` and call `set_data(data)`.
Use with `CGAL::IO::write_arrangement(arr, os, formatter)` / `read_arrangement(...)` from
`CGAL/IO/Arr_iostream.h` (or `os << arr` / `is >> arr` for the default formatter).
`Arr_face_extended_text_formatter` writes only the **face** data — vertex/halfedge data of an
`Arr_extended_dcel` would be silently dropped; use `Arr_extended_dcel_text_formatter` there.

---

## 8. Handles ⇄ pointers, and the arrangement's "view" record classes

### 8.1 Which type is actually allocated

`Arrangement_on_surface_2<GeomTraits, TopTraits>` (`Arrangement_on_surface_2.h`) declares:

```cpp
typedef typename Dcel::Vertex          DVertex;      // == Arr_vertex<V,H,F>
typedef typename Dcel::Halfedge        DHalfedge;
typedef typename Dcel::Face            DFace;
typedef typename Dcel::Outer_ccb       DOuter_ccb;
typedef typename Dcel::Inner_ccb       DInner_ccb;
typedef typename Dcel::Isolated_vertex DIso_vertex;
class Vertex   : public DVertex   { ... };
class Halfedge : public DHalfedge { ... };
class Face     : public DFace     { ... };
```
The DCEL allocates `DVertex`/`DHalfedge`/`DFace`. `Arrangement::Vertex`/`Halfedge`/`Face` add **no data
members**; the iterators reach them with `static_cast<Vertex*>(&(*dcel_iter))`. So:

* `sizeof(Arrangement::Vertex) == sizeof(Dcel::Vertex)` **[verified]**, and
  `(void*)(Arrangement::Vertex*) == (void*)(Dcel::Vertex*)` for the same record.
* You can freely reinterpret between `Dcel::Vertex*` and `Arrangement::Vertex*`.

### 8.2 Handle types

```cpp
typedef Vertex_iterator       Vertex_handle;         // = I_Filtered_iterator<DVertex_iter, _Is_concrete_vertex,
typedef Halfedge_iterator     Halfedge_handle;       //                       Vertex, DDifference, DIterator_category>
typedef Face_iterator         Face_handle;
typedef Vertex_const_iterator   Vertex_const_handle; // = I_Filtered_const_iterator<...>
typedef Halfedge_const_iterator  Halfedge_const_handle;
typedef Face_const_iterator      Face_const_handle;
```
Filters: vertices → `_Is_concrete_vertex` (skips vertices at infinity), halfedges → `_Is_valid_halfedge`
(skips fictitious), faces → `_Is_valid_face` (skips the fictitious face). Each filter's default ctor sets
`m_topol_traits = nullptr` and `operator()` then **returns `true`** (except `_Is_unbounded_face`, which
dereferences unconditionally and would crash if default-constructed).

Relevant `I_Filtered_iterator` members (`Arrangement_2/Arrangement_2_iterators.h:254`):
```cpp
I_Filtered_iterator();
I_Filtered_iterator(Iterator it);                        // nt = it, iend = nt
template <typename T> I_Filtered_iterator(T* p);         // nt = pointer(p), iend = nt   ← PUBLIC
I_Filtered_iterator(Iterator it, Iterator end);
I_Filtered_iterator(Iterator it, Iterator end, Filter f);
template <typename P> I_Filtered_iterator& operator=(const P* p);   // ← PUBLIC
Iterator current_iterator() const;   Iterator past_the_end() const;   Filter filter() const;
pointer  ptr() const;                                     // static_cast<Value_*>(&*nt)
bool operator==(const Self&) const;  bool operator!=(const Self&) const;
bool operator<(const Self& it) const;                     // &(**this) < &*it   ← pointer order
reference operator*() const;  pointer operator->() const;
Self& operator++();  Self operator++(int);  Self& operator--(); Self operator--(int);
```
`I_Filtered_const_iterator` adds `I_Filtered_const_iterator(mutable_iterator it)` (implicit
non-const → const conversion) and `typedef ... mutable_iterator`.

### 8.3 Round-tripping raw pointers ⇄ handles (the binding-critical part)

* **Protected API** (only `Arr_accessor<Self>` and `Aos_observer<Self>` are friends):
  ```cpp
  inline DVertex*         _vertex(Vertex_handle vh) const;         // &(*vh)
  inline const DVertex*   _vertex(Vertex_const_handle vh) const;
  inline DHalfedge*       _halfedge(Halfedge_handle hh) const;
  inline const DHalfedge* _halfedge(Halfedge_const_handle hh) const;
  inline DFace*           _face(Face_handle fh) const;
  inline const DFace*     _face(Face_const_handle fh) const;
  Vertex_handle       _handle_for(DVertex* v);
  Vertex_const_handle _const_handle_for(const DVertex* v) const;
  Halfedge_handle       _handle_for(DHalfedge* he);
  Halfedge_const_handle _const_handle_for(const DHalfedge* he) const;
  Face_handle       _handle_for(DFace* f);
  Face_const_handle _const_handle_for(const DFace* f) const;
  Dcel&       _dcel();
  const Dcel& _dcel() const;
  ```
  All of them are trivial (`&(*vh)` / `Vertex_handle(v)`).
* **Public equivalents you can use from your own C++ core, no accessor needed:**
  ```cpp
  Arr::Vertex*  raw = &*vh;                    // handle → pointer   [verified]
  Arr::Vertex_handle vh2(raw);                 // pointer → handle   [verified], compares == vh
  Arr::Vertex_const_handle cvh(const_raw);     //                    [verified]
  // same for Halfedge / Face
  ```
  `&*vh` is the sanctioned "handle → stable identity" operation. Use `(const void*)&*h` as the Python
  handle key; use `Handle(ptr)` to come back.
* **Do not `++` a pointer-constructed handle.** It has `iend == nt` and a null filter, so `operator++`
  steps into the raw DCEL list (including fictitious records) and never compares equal to `..._end()`.
  Also do not `--` one.
* Handles are cheap value types (three words: `nt`, `iend`, `filt`) and are **not** invalidated by
  arrangement modification, exactly like the raw pointer they wrap. Copy them by value.
* `operator<` compares record addresses, so handles are usable as `std::map` keys. Hashing works through
  `CGAL::Unique_hash_map<Handle, T>` (this is what the index maps do).

### 8.4 Address stability — measured **[verified]**

| Operation | Effect on existing record addresses |
|---|---|
| `insert(arr, cv)`, `insert_in_face_interior`, `insert_from_*_vertex`, `insert_at_vertices` | all preserved |
| `split_edge(h, cv1, cv2)` | `h` and `h->twin()` **survive** and `h` becomes the first half (`&*result == &*h`); one new halfedge pair + one new vertex are added |
| `merge_edge(h1, h2, cv)` | the surviving pair keeps its address (`&*merged == &*h1` in the tested case); the other pair and the middle vertex are destroyed |
| face split (a new curve closes a cycle) | the pre-existing `Face` record survives; a brand-new `Face` is allocated |
| `remove_edge`, `remove_isolated_vertex`, face merge | the removed records' addresses become **dangling** |
| `clear()`, `assign()`, `operator=`, destructor | **everything** is deallocated |
| copy construction | the copy has entirely different addresses |

Binding implication: raw pointers are safe Python handles **only** if you invalidate them on record
deletion. Attach an observer overriding `before_remove_edge`, `before_remove_vertex`, `before_merge_face`,
`before_clear`, `before_assign` and mark your Python-side handles dead there. (`Arr_vertex_index_map` uses
exactly this pattern for vertices; there is no shipped equivalent for halfedges.)

### 8.5 What the arrangement's record classes expose (and hide)

`Arrangement::Vertex` — public additions:
```cpp
bool is_at_open_boundary() const;                             // == Base::has_null_point()
Size degree() const;                                          // 0 if isolated; walks next()->opposite()
Halfedge_around_vertex_circulator       incident_halfedges();       // \pre ! is_isolated()
Halfedge_around_vertex_const_circulator incident_halfedges() const; // \pre ! is_isolated()
Face_handle       face();                                     // \pre is_isolated()
Face_const_handle face() const;                               // \pre is_isolated()
```
still inherited & public: `point()`, `is_isolated()`, `parameter_space_in_x()`, `parameter_space_in_y()`,
`inc()`, `set_inc()`, and (extended DCEL) `data()` / `set_data()`.
made **private** (blocked): `has_null_point`, `set_point`, `set_boundary`, `halfedge`, `set_halfedge`,
`isolated_vertex`, `set_isolated_vertex`.

`Arrangement::Halfedge` — public additions:
```cpp
bool is_fictitious() const;                                   // == Base::has_null_curve()
Vertex_handle source(); Vertex_const_handle source() const;   // opposite()->vertex()
Vertex_handle target(); Vertex_const_handle target() const;
Face_handle   face();   Face_const_handle   face() const;     // *** calls inner_ccb() → mutates, gotcha 3
Halfedge_handle twin(); Halfedge_const_handle twin() const;
Halfedge_handle prev(); Halfedge_const_handle prev() const;
Halfedge_handle next(); Halfedge_const_handle next() const;
Ccb_halfedge_circulator       ccb();
Ccb_halfedge_const_circulator ccb() const;
```
still inherited & public: `curve()`, `direction()`, `is_on_outer_ccb()`, `is_on_inner_ccb()`,
`inner_ccb_no_redirect()`, and (extended DCEL) `data()` / `set_data()`.
blocked: `has_null_curve`, `set_curve`, `opposite`, `set_opposite`, `set_direction`, `set_prev`, `set_next`,
`vertex`, `set_vertex`, `outer_ccb`, `set_outer_ccb`, `inner_ccb`, `set_inner_ccb`.

`Arrangement::Face` — public additions (all re-typed to hand out circulators / handle-iterators):
```cpp
Outer_ccb_iterator       outer_ccbs_begin();  Outer_ccb_iterator       outer_ccbs_end();
Outer_ccb_const_iterator outer_ccbs_begin() const; Outer_ccb_const_iterator outer_ccbs_end() const;
Inner_ccb_iterator       inner_ccbs_begin();  Inner_ccb_iterator       inner_ccbs_end();
Inner_ccb_const_iterator inner_ccbs_begin() const; Inner_ccb_const_iterator inner_ccbs_end() const;
Isolated_vertex_iterator       isolated_vertices_begin(); Isolated_vertex_iterator       isolated_vertices_end();
Isolated_vertex_const_iterator isolated_vertices_begin() const; Isolated_vertex_const_iterator isolated_vertices_end() const;
bool has_outer_ccb() const;                                    // number_of_outer_ccbs() > 0
Ccb_halfedge_circulator       outer_ccb();                     // \pre number_of_outer_ccbs() == 1
Ccb_halfedge_const_circulator outer_ccb() const;               // \pre number_of_outer_ccbs() == 1
Size number_of_holes() const;  Inner_ccb_iterator holes_begin(); holes_end(); (+const)
```
Here `Outer_ccb_iterator = Iterator_transform<DOuter_ccb_iter, _Halfedge_to_ccb_circulator>` — dereferencing
yields a **`Ccb_halfedge_circulator`**, unlike the DCEL-level iterator which yields a `Halfedge*`.
still inherited & public: `is_unbounded()`, `is_fictitious()`, `number_of_outer_ccbs()`,
`number_of_inner_ccbs()`, `number_of_isolated_vertices()`, `splice_inner_ccbs()`,
`splice_isolated_vertices()`, and (extended DCEL) `data()` / `set_data()`.
blocked: `set_unbounded`, `set_fictitious`, `add_outer_ccb`, `erase_outer_ccb`, `add_inner_ccb`,
`erase_inner_ccb`, `add_isolated_vertex`, `erase_isolated_vertex`.

---

## 9. `Arr_accessor<Arrangement_>` — the expert / low-level API

```cpp
template <typename Arrangement_> class Arr_accessor;
```
Header: `CGAL/Arr_accessor.h`. Both `Arrangement_on_surface_2` and `Arrangement_2` declare
`friend class Arr_accessor<Self>;`, so instantiate it on the concrete arrangement type you use
(`Arr_accessor<CGAL::Arrangement_2<Traits, Dcel>>` works — **[verified]**).

### 9.1 Typedefs

```cpp
typedef Arrangement_                                   Arrangement_2;
typedef Arr_accessor<Arrangement_2>                    Self;
typedef typename Arrangement_2::Size                   Size;
typedef typename Arrangement_2::Point_2                Point_2;
typedef typename Arrangement_2::X_monotone_curve_2     X_monotone_curve_2;
typedef typename Arrangement_2::Vertex_handle          Vertex_handle;
typedef typename Arrangement_2::Vertex_const_handle    Vertex_const_handle;
typedef typename Arrangement_2::Halfedge_handle        Halfedge_handle;
typedef typename Arrangement_2::Halfedge_const_handle  Halfedge_const_handle;
typedef typename Arrangement_2::Face_handle            Face_handle;
typedef typename Arrangement_2::Face_const_handle      Face_const_handle;
typedef typename Arrangement_2::Ccb_halfedge_circulator Ccb_halfedge_circulator;
// (private) DVertex, DHalfedge, DFace, DOuter_ccb, DInner_ccb, DIso_vertex,
//           Pl_result = Arr_point_location_result<Arrangement_2>, Pl_result_type = Pl_result::Type
// BGL section:
typedef typename Arrangement_2::_Is_valid_vertex       Is_valid_vertex;
typedef typename Arrangement_2::_Valid_vertex_iterator Valid_vertex_iterator;
// reader/writer section:
typedef typename Arrangement_2::Dcel                   Dcel;
typedef typename Arrangement_2::DVertex_const_iter     Dcel_vertex_iterator;
typedef typename Arrangement_2::DEdge_const_iter       Dcel_edge_iterator;
typedef typename Arrangement_2::DFace_const_iter       Dcel_face_iterator;
typedef typename Arrangement_2::DOuter_ccb_const_iter  Dcel_outer_ccb_iterator;
typedef typename Arrangement_2::DInner_ccb_const_iter  Dcel_inner_ccb_iterator;
typedef typename Arrangement_2::DIso_vertex_const_iter Dcel_iso_vertex_iterator;
typedef DVertex     Dcel_vertex;      typedef DHalfedge  Dcel_halfedge;
typedef DFace       Dcel_face;        typedef DOuter_ccb Dcel_outer_ccb;
typedef DInner_ccb  Dcel_inner_ccb;   typedef DIso_vertex Dcel_isolated_vertex;
```

### 9.2 Construction and notifications

```cpp
Arr_accessor(Arrangement_2& arr);                    // stores Arrangement_2* p_arr; NON-owning
Arrangement_2&       arrangement();
const Arrangement_2& arrangement() const;

void notify_before_global_change();                  // p_arr->_notify_before_global_change()
void notify_after_global_change();                   // p_arr->_notify_after_global_change()
```
`notify_before_global_change()` fires `before_global_change()` on every attached observer **and** puts the
arrangement into "global change" mode (observers such as `Arr_face_index_map` use it to suspend per-op
bookkeeping). Every low-level editing sequence should be bracketed by the pair. The accessor is a
lightweight non-owning wrapper — construct it on the stack; it must not outlive the arrangement.

### 9.3 Local predicates / location

```cpp
Pl_result_type locate_curve_end(const X_monotone_curve_2& cv, Arr_curve_end ind,
                                Arr_parameter_space ps_x, Arr_parameter_space ps_y) const;
//  \pre (ps_x != ARR_INTERIOR) || (ps_y != ARR_INTERIOR)
//  returns std::variant<Vertex_const_handle, Halfedge_const_handle, Face_const_handle>
//  (Face in the general case, Halfedge on overlap, Vertex when it coincides)

Halfedge_handle locate_around_vertex(Vertex_handle vh, const X_monotone_curve_2& cv) const;
//  \pre vh is one of cv's endpoints.
//  returns the halfedge whose TARGET is vh, such that cv goes between it and its successor
//  around vh in CLOCKWISE order. Internally picks ARR_MIN_END/ARR_MAX_END with is_closed_2 + equal_2.

Halfedge_handle locate_around_boundary_vertex(Vertex_handle vh, const X_monotone_curve_2& cv,
                                              Arr_curve_end ind,
                                              Arr_parameter_space ps_x, Arr_parameter_space ps_y) const;
//  \pre (ps_x != ARR_INTERIOR) || (ps_y != ARR_INTERIOR)

int halfedge_distance(Halfedge_const_handle e1, Halfedge_const_handle e2) const;
//  number of boundary halfedges from e1 to e2 along their common CCB; 0 if e1 == e2; -1 if different CCBs.

bool defines_outer_ccb_of_new_face(Halfedge_handle prev1, Halfedge_handle prev2,
                                   const X_monotone_curve_2& cv) const;
//  \pre prev1 and prev2 are on the same connected component and connecting them by cv creates a new face.
//  true  ⇒ prev1 lies in the interior of the new face (prev2 does not), false ⇒ the other way round.
//  (this replaces the old `is_inside_new_face`)

bool are_equal(Vertex_const_handle v, const X_monotone_curve_2& cv, Arr_curve_end ind,
               Arr_parameter_space ps_x, Arr_parameter_space ps_y) const;

bool is_on_outer_boundary(Halfedge_const_handle he) const;   // ! he->is_on_inner_ccb()
bool is_on_inner_boundary(Halfedge_const_handle he) const;   //   he->is_on_inner_ccb()

bool are_on_same_inner_component(Halfedge_handle e1, Halfedge_handle e2);   // non-const!
bool are_on_same_outer_component(Halfedge_handle e1, Halfedge_handle e2);   // non-const!
```

### 9.4 Vertex creation

```cpp
Vertex_handle create_vertex(const Point_2& p);
//  allocates a DCEL vertex + a copy of p; the vertex is NOT yet attached to any face
//  (it is not registered as an isolated vertex — call insert_isolated_vertex or one of the
//   insert_*_ex functions afterwards).

Vertex_handle create_boundary_vertex(const Point_2& pt,
                                     Arr_parameter_space ps_x, Arr_parameter_space ps_y,
                                     bool notify = true);
//  \pre ps_x != ARR_INTERIOR || ps_y != ARR_INTERIOR
//  if notify: topology_traits()->notify_on_boundary_vertex_creation(v, pt, ps_x, ps_y)

Vertex_handle create_boundary_vertex(const X_monotone_curve_2& cv, Arr_curve_end ind,
                                     Arr_parameter_space ps_x, Arr_parameter_space ps_y,
                                     bool notify = true);
//  \pre ps_x != ARR_INTERIOR || ps_y != ARR_INTERIOR

std::pair<Vertex_handle, Halfedge_handle>
place_and_set_curve_end(Face_handle f, const X_monotone_curve_2& cv, Arr_curve_end ind,
                        Arr_parameter_space ps_x, Arr_parameter_space ps_y);
//  locates/creates the vertex for a boundary curve end inside f; .second is a default-constructed
//  Halfedge_handle when there is no predecessor halfedge.
```

### 9.5 Topological insertion (the "_ex" family)

```cpp
Halfedge_handle insert_in_face_interior_ex(Face_handle f, const X_monotone_curve_2& cv,
                                           Arr_halfedge_direction cv_dir,
                                           Vertex_handle v1, Vertex_handle v2);
//  v1, v2 must be FREE vertices (fresh from create_vertex, or currently isolated).
//  If either is isolated its DIso_vertex record is erased and deleted first.
//  Creates a new inner CCB (hole) of f. Returns the halfedge directed v1 → v2.
//  cv_dir is the direction of cv as traversed from v1 to v2.

Halfedge_handle insert_from_vertex_ex(Halfedge_handle he_to, const X_monotone_curve_2& cv,
                                      Arr_halfedge_direction cv_dir, Vertex_handle v);
//  he_to: the predecessor halfedge, i.e. a halfedge whose TARGET is the existing endpoint; the new
//         edge becomes he_to's successor around that vertex.
//  v: the free (new or isolated) vertex for the other endpoint. Returns the halfedge whose TARGET is v.

Halfedge_handle insert_at_vertices_ex(Halfedge_handle he_to, const X_monotone_curve_2& cv,
                                      Arr_halfedge_direction cv_dir, Halfedge_handle he_away,
                                      bool& new_face, bool& swapped_predecessors,
                                      bool allow_swap_of_predecessors = true);
//  he_to:   halfedge POINTING AT the first vertex.
//  he_away: halfedge POINTING AWAY FROM the second vertex (i.e. `pred2->next()`).   *** see gotcha 2 ***
//  new_face (out): whether a new face was created.
//  swapped_predecessors (out): whether the implementation swapped the roles of the two predecessors
//                              (pass allow_swap_of_predecessors = false to forbid it).
//  Returns a halfedge for the inserted curve directed from he_to's target to he_away's source;
//  if a new face was created it is that halfedge's incident face.

void insert_isolated_vertex(Face_handle f, Vertex_handle v);
//  registers v as an isolated vertex of f (creates the DIso_vertex record).
```
Worked, compiling example **[verified]** — building a triangle from scratch:
```cpp
CGAL::Arr_accessor<Arr> acc(arr);
acc.notify_before_global_change();
auto v1 = acc.create_vertex(Point_2(0,0)), v2 = acc.create_vertex(Point_2(4,0)),
     v3 = acc.create_vertex(Point_2(2,3));
auto e1 = acc.insert_in_face_interior_ex(arr.unbounded_face(), Seg(P(0,0),P(4,0)),
                                         CGAL::ARR_LEFT_TO_RIGHT, v1, v2);
auto e2 = acc.insert_from_vertex_ex(e1, Seg(P(4,0),P(2,3)), CGAL::ARR_RIGHT_TO_LEFT, v3);
bool new_face = false, swapped = false;
auto e3 = acc.insert_at_vertices_ex(e2, Seg(P(2,3),P(0,0)), CGAL::ARR_RIGHT_TO_LEFT,
                                    /* he_away = */ e1, new_face, swapped);
if (new_face) acc.relocate_in_new_face(e3);
acc.notify_after_global_change();
// arr.is_valid() == true, V=3 E=3 F=2
```

### 9.6 Relocation after a face split

```cpp
void relocate_in_new_face(Halfedge_handle new_he);
//  \pre new_he is what insert_at_vertices_ex returned AND it reported new_face == true;
//       the new face lies to new_he's LEFT and the old face to its right.
//  Moves the inner CCBs *and* isolated vertices that now belong to the new face.

void relocate_isolated_vertices_in_new_face(Halfedge_handle new_he);   // isolated vertices only
void relocate_holes_in_new_face(Halfedge_handle new_he);               // inner CCBs only
```
Not calling `relocate_in_new_face` after a face-splitting `insert_at_vertices_ex` leaves holes/isolated
vertices attached to the wrong face — `arr.is_valid()` will fail.

### 9.7 Moving components between faces

```cpp
void move_outer_ccb(Face_handle from_face, Face_handle to_face, Ccb_halfedge_circulator ccb);
void move_inner_ccb(Face_handle from_face, Face_handle to_face, Ccb_halfedge_circulator ccb);
void move_isolated_vertex(Face_handle from_face, Face_handle to_face, Vertex_handle v);
void remove_isolated_vertex_ex(Vertex_handle v);          // \pre v->is_isolated()
```
Note the `Ccb_halfedge_circulator` (not `Halfedge_handle`) parameter — pass `he->ccb()`.

### 9.8 Modification / split / removal

```cpp
Vertex_handle   modify_vertex_ex(Vertex_handle v, const Point_2& p);
//  replaces the point; the new point MAY be geometrically different (no validity check). Returns v.

Halfedge_handle modify_edge_ex(Halfedge_handle e, const X_monotone_curve_2& cv);
//  replaces the curve; may be geometrically different. Returns e.

Halfedge_handle split_edge_ex(Halfedge_handle e, const Point_2& p,
                              const X_monotone_curve_2& cv1, const X_monotone_curve_2& cv2);
//  cv1: e->source() → p ; cv2: p → e->target(). Returns the first half (source == e->source(),
//  target == the new split vertex).  [verified] the returned handle has the SAME address as e.

Halfedge_handle split_edge_ex(Halfedge_handle e, Vertex_handle v,
                              const X_monotone_curve_2& cv1, const X_monotone_curve_2& cv2);
//  same, splitting at an existing vertex v.

Halfedge_handle split_fictitious_edge(Halfedge_handle e, Vertex_handle v);
//  \pre e->is_fictitious()

Face_handle remove_edge_ex(Halfedge_handle e, bool remove_source = true, bool remove_target = true);
//  \pre if the removal creates a new hole, e must point at that hole.
//  removes the twin pair; optionally removes the endpoints if they become isolated.
//  Returns the remaining face.
```

### 9.9 BGL support

```cpp
Valid_vertex_iterator valid_vertices_begin();   // all vertices incl. non-fictitious vertices at infinity
Valid_vertex_iterator valid_vertices_end();
Size number_of_valid_vertices() const;          // topology_traits()->number_of_valid_vertices()
```
`Valid_vertex_iterator` converts implicitly to `Vertex_handle` / `Vertex_const_handle`.

### 9.10 Reader/writer support (raw DCEL surgery)

```cpp
const Dcel& dcel() const;                       // p_arr->_dcel()
void clear_all();                               // p_arr->clear(); p_arr->_dcel().delete_all();

Dcel_vertex*   set_vertex_boundary(const Vertex_handle v,
                                   Arr_parameter_space ps_x, Arr_parameter_space ps_y);
Dcel_vertex*   new_vertex(const Point_2* p, Arr_parameter_space ps_x, Arr_parameter_space ps_y);
//  p == nullptr ⇒ vertex at an open boundary; \pre p_arr->is_open(ps_x, ps_y).
//  Otherwise allocates a copy of *p from the arrangement's point allocator.
Dcel_halfedge* new_edge(const X_monotone_curve_2* cv);   // cv == nullptr ⇒ fictitious edge
Dcel_face*            new_face();
Dcel_outer_ccb*       new_outer_ccb();
Dcel_inner_ccb*       new_inner_ccb();
Dcel_isolated_vertex* new_isolated_vertex();

template <typename VertexRange> void delete_vertices(const VertexRange& range);
//  CGAL_assertion(! (*it)->has_null_point()); frees the point, then the DCEL vertex.
template <typename EdgeRange>   void delete_edges(const EdgeRange& range);
//  CGAL_assertion(! (*it)->has_null_curve()); frees the curve, then the halfedge pair.
template <typename FaceRange>      void delete_faces(const FaceRange& range);
template <typename CcbRange>       void delete_outer_ccbs(const CcbRange& range);
template <typename CcbRange>       void delete_inner_ccbs(const CcbRange& range);
//  These ranges hold RAW Dcel_* POINTERS (they call p_arr->_dcel().delete_xxx(*it)).

void dcel_updated();                            // topology_traits()->dcel_updated()
```
`dcel_updated()` must be called after any batch of raw DCEL surgery so the topology traits can recompute
its cached state (the fictitious face, vertex counters, etc.).

---

## 10. `Arr_face_index_map` / `Arr_vertex_index_map`

Both are `boost` **readable** property maps kept up to date by the observer mechanism.

```cpp
template <typename Arrangement_>
class Arr_face_index_map : public Arrangement_::Observer {
public:
  using Arrangement_2   = Arrangement_;
  using Base_aos        = typename Arrangement_2::Base_aos;
  using Halfedge_handle = typename Base_aos::Halfedge_handle;
  using Face_handle     = typename Base_aos::Face_handle;
  using category   = boost::readable_property_map_tag;
  using value_type = unsigned int;
  using reference  = value_type;
  using key_type   = Face_handle;

  Arr_face_index_map();                          // NOT attached; operator[] is meaningless
  Arr_face_index_map(const Base_aos& arr);       // attaches (const_cast) and builds the map
  Arr_face_index_map(const Self& other);         // attaches to other's arrangement and REBUILDS
  Self& operator=(const Self& other);            // detach(); attach(other's arrangement) → rebuild

  unsigned int operator[](Face_handle f) const;  // \pre f is a valid face of the arrangement
  Face_handle  face(const int i) const;          // \pre (unsigned)i < number of faces

  virtual void after_assign() override;          // rebuild
  virtual void after_clear()  override;          // rebuild
  virtual void after_attach() override;          // rebuild
  virtual void after_detach() override;          // n_faces = 0; index_map.clear()
  virtual void after_split_face(Face_handle f, Face_handle new_f, bool is_hole) override;
  virtual void before_merge_face(Face_handle f1, Face_handle f2, Halfedge_handle e) override;
};

template <typename Arrangement>
unsigned int get(const CGAL::Arr_face_index_map<Arrangement>& index_map,
                 typename Arrangement::Face_handle f);        // in namespace CGAL
```

```cpp
template <typename Arrangement_>
class Arr_vertex_index_map : public Arrangement_::Observer {
public:
  using Arrangement_2 = Arrangement_;   using Base_aos = typename Arrangement_2::Base_aos;
  using Vertex_handle = typename Base_aos::Vertex_handle;
  using category   = boost::readable_property_map_tag;
  using value_type = unsigned int;   using reference = value_type;   using key_type = Vertex_handle;

  Arr_vertex_index_map();
  Arr_vertex_index_map(const Base_aos& arr);
  Arr_vertex_index_map(const Self& other);
  Self& operator=(const Self& other);

  unsigned int  operator[](Vertex_handle v) const;   // \pre v is a valid vertex
  Vertex_handle vertex(const int i) const;           // \pre i < number of vertices

  virtual void after_assign() override;  virtual void after_clear() override;
  virtual void after_attach() override;  virtual void after_detach() override;
  virtual void after_create_vertex(Vertex_handle v) override;
  virtual void after_create_boundary_vertex(Vertex_handle v) override;
  virtual void before_remove_vertex(Vertex_handle v) override;
};

template <typename Arrangement>
unsigned int get(const CGAL::Arr_vertex_index_map<Arrangement>& index_map,
                 typename Arrangement::Vertex_handle v);      // in namespace CGAL
```

Behaviour and lifetime:
* Indices are `0 .. n-1` and **not stable**: on removal the last-indexed element is swapped into the freed
  slot (`before_merge_face` / `before_remove_vertex`). Never persist an index across a modification.
* Backing store: `CGAL::Unique_hash_map<Handle, unsigned int>` + `std::vector<Handle>` reverse map with a
  `MIN_REV_MAP_SIZE = 32` floor and a grow-×2 / shrink-÷2 policy.
* The map holds a raw `Arrangement_2*` (from `Aos_observer`) and the arrangement holds a raw
  `Observer*` in `std::list<Observer*> m_observers`. **Neither owns the other**; the observer's destructor
  unregisters itself. Destroy index maps before the arrangement, or explicitly `detach()`.
* `Aos_observer` (the base) is **non-copyable** (`private` copy ctor / `operator=`); the index maps work
  around it with their own ctor/assignment that re-attach and rebuild.
* `attach(Arrangement_2&)` has `\pre` "the observer is not already attached" (`CGAL_precondition(p_arr == nullptr)`).
* `Arrangement_::Observer` is `Aos_observer<Base_aos>`; `Base_aos` is the `Arrangement_on_surface_2`
  instantiation. For `Arrangement_2<Traits, Dcel>` all the handle types are the same objects, so mixing is fine.

`Arr_face_map.h` / `Arr_vertex_map.h` are deprecated forwarding headers (they `#define CGAL_DEPRECATED_HEADER`
and pull in the `*_index_map.h` header). Do not include them.

---

## 11. BGL primal graph traits (`graph_traits_Arrangement_2.h`)

Graph model: **vertices = valid arrangement vertices** (including non-fictitious vertices at infinity),
**edges = halfedges** (so the graph is *directed* and each arrangement edge shows up twice), parallel edges
allowed, fictitious halfedges skipped.

```cpp
namespace boost {
template <class GeomTraits, class TopTraits>
class graph_traits<CGAL::Arrangement_on_surface_2<GeomTraits, TopTraits> > {
public:
  typedef GeomTraits Geometry_traits_2;   typedef TopTraits Topology_traits;
  typedef CGAL::Arrangement_on_surface_2<Geometry_traits_2, Topology_traits> Arrangement_on_surface_2;

  typedef typename Arrangement_on_surface_2::Vertex_handle    vertex_descriptor;
  typedef boost::directed_tag                                 directed_category;
  typedef boost::allow_parallel_edge_tag                      edge_parallel_category;
  typedef Arr_traversal_category                              traversal_category;   // bidirectional +
                                                              // vertex_list + edge_list + adjacency
  typedef typename Arrangement_on_surface_2::Halfedge_handle  edge_descriptor;
  typedef Halfedge_around_vertex_iterator                     out_edge_iterator;    // private nested class
  typedef typename Arrangement_on_surface_2::Size             degree_size_type;
  typedef Halfedge_around_vertex_iterator                     in_edge_iterator;
  typedef boost::counting_iterator<Vertex_iterator>           vertex_iterator;      // Vertex_iterator ==
                                                              // Arr_accessor::Valid_vertex_iterator
  typedef typename Arrangement_on_surface_2::Size             vertices_size_type;
  typedef boost::counting_iterator<Halfedge_iterator>         edge_iterator;
  typedef typename Arrangement_on_surface_2::Size             edges_size_type;
  typedef CGAL::Vertex_around_target_iterator<Arrangement_on_surface_2> adjacency_iterator;

  graph_traits(const Arrangement_on_surface_2& arr);      // const_casts internally
  static vertex_descriptor null_vertex();                 // vertex_descriptor()
  vertices_size_type number_of_vertices();
  vertex_iterator vertices_begin();   vertex_iterator vertices_end();
  edge_iterator   edges_begin();      edge_iterator   edges_end();    // ALL halfedges
  degree_size_type degree(vertex_descriptor v);           // # non-fictitious incident halfedges; 0 if isolated
  out_edge_iterator out_edges_begin(vertex_descriptor v); out_edge_iterator out_edges_end(vertex_descriptor v);
  in_edge_iterator  in_edges_begin(vertex_descriptor v);  in_edge_iterator  in_edges_end(vertex_descriptor v);
};

template <class Traits_, class Dcel_>
class graph_traits<CGAL::Arrangement_2<Traits_, Dcel_> > :
  public graph_traits<CGAL::Arrangement_on_surface_2<
    typename CGAL::Arrangement_2<Traits_, Dcel_>::Geometry_traits_2,
    typename CGAL::Arrangement_2<Traits_, Dcel_>::Topology_traits> >
{ public: graph_traits(const CGAL::Arrangement_2<Traits_2, Dcel>& arr); };
} // namespace boost
```

Free functions, all in **`namespace CGAL`**, all templated on `<class GeomTraits, class TopTraits>` and
taking `const CGAL::Arrangement_on_surface_2<GeomTraits, TopTraits>&`:

```cpp
degree_size_type  out_degree(vertex_descriptor v, const Arrangement_on_surface_2& arr);
std::pair<out_edge_iterator,out_edge_iterator> out_edges(vertex_descriptor v, const A& arr);
Iterator_range<adjacency_iterator> adjacent_vertices(vertex_descriptor v, const A& arr);  // == vertices_around_target
vertex_descriptor source(edge_descriptor e, const A&);       // e->source()
vertex_descriptor target(edge_descriptor e, const A&);       // e->target()
degree_size_type  in_degree(vertex_descriptor v, const A& arr);
std::pair<in_edge_iterator,in_edge_iterator> in_edges(vertex_descriptor v, const A& arr);
degree_size_type  degree(vertex_descriptor v, const A& arr);          // == 2 * gt.degree(v)
vertices_size_type num_vertices(const A& arr);
std::pair<vertex_iterator,vertex_iterator> vertices(const A& arr);
edges_size_type   num_edges(const A& arr);                            // == arr.number_of_halfedges()
std::pair<edge_iterator,edge_iterator> edges(const A& arr);
```
**[verified]** on a 3×3 grid arrangement (V=9, E=12, F=5): `num_vertices(arr) == 9`,
`num_edges(arr) == 24`, `out_degree(v, arr) == 2`, `degree(v, arr) == 4`, and iterating
`vertices(arr)` yields 9 descriptors. Remember to call them unqualified (ADL) — `boost::num_vertices` fails.

`out_edge_iterator == in_edge_iterator == Halfedge_around_vertex_iterator` (a private nested class of the
graph traits): forward iterator over `Halfedge_around_vertex_circulator`, `value_type == reference ==
Halfedge_handle`, `difference_type == int`, skipping fictitious halfedges; the *out* variant yields
`circ->twin()`, the *in* variant yields `circ`. It is bounded by an integer counter (`_counter`/`_cend`
set from `v->degree()`), so an isolated vertex gives an empty default-constructed range.

No `edge(u,v,g)`, no `add_edge`/`remove_edge`, no built-in `vertex_index` map (use `Arr_vertex_index_map`).

---

## 12. BGL dual graph traits (`Arrangement_2/graph_traits_dual.h`, `graph_traits_dual_arrangement_2.h`)

Graph model: **vertices = valid faces**, **edges = halfedges** (an arc from `e->face()` to `e->twin()->face()`).
Directed, parallel edges allowed. Fictitious faces and fictitious halfedges are skipped.

```cpp
namespace CGAL {
template <typename Arrangement_> class Dual_arrangement_on_surface {
public:
  typedef Arrangement_                             Arrangement;
  typedef typename Arrangement::Geometry_traits_2  Geometry_traits_2;
  typedef typename Arrangement::Topology_traits    Topology_traits;
  typedef typename Arrangement::Size               Size;
  typedef typename Arrangement::Face_handle        Vertex_handle;   // dual vertex  = primal face
  typedef typename Arrangement::Halfedge_handle    Edge_handle;     // dual edge    = primal halfedge
  typedef typename Arrangement::Face_iterator      Vertex_iterator;
  typedef typename Arrangement::Halfedge_iterator  Edge_iterator;
  typedef Face_neighbor_iterator                   Incident_edge_iterator;   // protected nested class

  Dual_arrangement_on_surface();                       // p_arr = nullptr
  Dual_arrangement_on_surface(const Arrangement& arr); // const_cast into `mutable Arrangement* p_arr`
  const Arrangement* arrangement() const;   Arrangement* arrangement();
  Size number_of_vertices() const;                     // p_arr->number_of_faces()
  Vertex_iterator vertices_begin() const;              // p_arr->faces_begin()   (both const!)
  Vertex_iterator vertices_end()   const;
  Size number_of_edges() const;                        // p_arr->number_of_halfedges()
  Edge_iterator edges_begin() const;   Edge_iterator edges_end() const;
  Size degree(Vertex_handle v) const;                  // counts Incident_edge_iterator steps
  Incident_edge_iterator out_edges_begin(Vertex_handle v) const;  // (v, true,  true)
  Incident_edge_iterator out_edges_end  (Vertex_handle v) const;  // (v, true,  false)
  Incident_edge_iterator in_edges_begin (Vertex_handle v) const;  // (v, false, true)
  Incident_edge_iterator in_edges_end   (Vertex_handle v) const;  // (v, false, false)
};

template <typename Arrangement_> class Graph_traits_dual_arr_on_surface_impl {
public:
  typedef Arrangement_ Arrangement;   typedef Dual_arrangement_on_surface<Arrangement> Dual_arr_2;
  typedef typename Dual_arr_2::Vertex_handle       vertex_descriptor;     // Face_handle
  typedef boost::directed_tag                      directed_category;
  typedef boost::allow_parallel_edge_tag           edge_parallel_category;
  typedef Dual_arr_traversal_category              traversal_category;    // bidirectional + vertex_list + edge_list
  typedef typename Dual_arr_2::Edge_handle         edge_descriptor;       // Halfedge_handle
  typedef Incident_edge_iterator                   out_edge_iterator;
  typedef typename Dual_arr_2::Size                degree_size_type;
  typedef Incident_edge_iterator                   in_edge_iterator;
  typedef boost::counting_iterator<Vertex_iterator> vertex_iterator;
  typedef typename Dual_arr_2::Size                vertices_size_type;
  typedef boost::counting_iterator<Edge_iterator>  edge_iterator;
  typedef typename Dual_arr_2::Size                edges_size_type;
  typedef void                                     adjacency_iterator;   // ← NOT provided
};
} // namespace CGAL
```
`Face_neighbor_iterator` (forward iterator; `value_type == reference == Edge_handle`,
`difference_type == int`) walks **all outer CCBs then all inner CCBs** of the face, skipping halfedges whose
relevant side is fictitious. **`\pre ! face->is_fictitious()`** in its constructor. `_out == true` yields
`_ccb_curr`; `_out == false` yields `_ccb_curr->twin()`.

`graph_traits_dual_arrangement_2.h` specialises for `Arrangement_2`:
```cpp
namespace CGAL {
template <typename GeomTraits_2, typename Dcel>
class Dual<Arrangement_2<GeomTraits_2, Dcel> > :
  public Dual_arrangement_on_surface<Arrangement_2<GeomTraits_2, Dcel> > {
public:
  typedef Arrangement_2<GeomTraits_2, Dcel>        Arrangement;
  typedef typename Arrangement::Geometry_traits_2  Geometry_traits_2;
  typedef typename Arrangement::Topology_traits    Topology_traits;
  Dual();
  Dual(const Arrangement& arr);
};
}
namespace boost {
template <typename GeomTraits_2, typename Dcel>
class graph_traits<CGAL::Dual<CGAL::Arrangement_2<GeomTraits_2, Dcel> > > :
  public CGAL::Graph_traits_dual_arr_on_surface_impl<CGAL::Arrangement_2<GeomTraits_2, Dcel> > {};
}
```
plus the eleven free functions generated by the macros (again in **`namespace CGAL`**):
`out_degree`, `out_edges`, `source` (`e->face()`), `target` (`e->twin()->face()`), `in_degree`, `in_edges`,
`degree` (`2 * darr.degree(v)`), `num_vertices` (`= number of primal faces`), `vertices`,
`num_edges` (`= number of primal halfedges`), `edges`.
The macros are named `CGAL_DUAL_ARRANGEMENT_2_{OUT_DEGREE,OUT_EDGES,SOURCE,TARGET,IN_DEGREE,IN_EDGES,
DEGREE,NUM_VERTICES,VERTICES,NUM_EDGES,EDGES}(T)` and take the arrangement *template name* — reuse them if
you ever wrap `Arrangement_with_history_2`.

### BFS over faces — working recipe **[verified]**

```cpp
#include <CGAL/graph_traits_dual_arrangement_2.h>
#include <CGAL/Arr_face_index_map.h>
#include <boost/graph/breadth_first_search.hpp>

typedef CGAL::Dual<Arr>                Dual_arr;
typedef CGAL::Arr_face_index_map<Arr>  Face_index_map;

template <typename IndexMap>
struct Distance_recorder : public boost::default_bfs_visitor {
  Distance_recorder(const IndexMap* m, std::vector<unsigned>& d) : m_m(m), m_d(&d) {}
  template <typename Edge, typename Graph>
  void tree_edge(Edge e, const Graph& g) const {                 // note: unqualified source/target
    (*m_d)[(*m_m)[target(e, g)]] = (*m_d)[(*m_m)[source(e, g)]] + 1;
  }
  const IndexMap* m_m; std::vector<unsigned>* m_d;
};

Dual_arr dual(arr);
Face_index_map index_map(arr);
std::vector<unsigned> dist(arr.number_of_faces(), 0);
Distance_recorder<Face_index_map> vis(&index_map, dist);
boost::breadth_first_search(dual, arr.unbounded_face(),
                            boost::vertex_index_map(index_map).visitor(vis));
```
On a 2×2 grid inside a square this yields `unbounded → 0`, the four cells → `1`;
`num_vertices(dual) == 5`, `num_edges(dual) == 24`.

---

## 13. `Arr_has.h`

24 `std::void_t`-based detectors in `namespace CGAL`, all of the shape

```cpp
template <typename, typename = std::void_t<>> struct has_xxx_2 : std::false_type {};
template <typename T> struct has_xxx_2<T, std::void_t<typename T::Xxx_2>> : std::true_type {};
```

Detected nested types (note the naming irregularity in the middle of the list):

`has_compare_x_2` (`Compare_x_2`), `has_compare_xy_2`, `has_construct_min_vertex_2`,
`has_construct_max_vertex_2`, `has_is_vertical_2`, `has_compare_y_at_x_2`,
**`has_equal_2` → detects the nested type `Equal`, not `Equal_2`**, `has_compare_y_at_x_left_2`,
`has_compare_y_at_x_right_2`, `has_make_x_monotone_2`, `has_split_2`, `has_intersect_2`,
`has_are_mergeable_2`, `has_merge_2`, `has_construct_opposite_2`, `has_construct_point_2`,
`has_compare_endpoints_xy_2`, `has_approximate_2`, `has_parameter_space_in_x_2`,
`has_is_on_x_identification_2`, `has_compare_y_on_boundary_2`, `has_compare_y_near_boundary_2`,
`has_parameter_space_in_y_2`, `has_is_on_y_identification_2`, `has_compare_x_on_boundary_2`,
`has_compare_x_near_boundary_2`.

They only check for the **nested type**, never for the `xxx_2_object()` factory. Handy for a type-erased
core that must decide at compile time whether to expose `split`, `merge`, `approximate`, or the
boundary-side predicates for a given traits class.

---

## 14. Cheat sheet for the type-erased core

* **Python handle representation:** store `(void* record, uint64 arrangement_generation)`.
  Get it with `(void*)&*handle`; rebuild the handle with `Arr::Vertex_handle((Arr::Vertex*)ptr)`
  (public, verified). Bump the generation on `clear` / `assign` / `operator=` / destruction, and
  invalidate individual pointers from an observer's `before_remove_vertex` / `before_remove_edge` /
  `before_merge_face`.
* **Never `++` a pointer-built handle.** For iteration always go through
  `arr.vertices_begin()/end()`, `halfedges_begin()/end()`, `edges_begin()/end()`, `faces_begin()/end()`,
  or the `*_handles()` `Iterator_range<Prevent_deref<...>>` ranges.
* **Treat `he->face()` as a mutating call** (inner-CCB path compression). Guard reads with the same lock
  as writes, or use `he->is_on_inner_ccb() ? he->inner_ccb_no_redirect()->face() : he->outer_ccb()->face()`
  from the DCEL level if you need a truly read-only path.
* **Extended DCEL:** always `set_data()` after creating a feature; hook an observer for split/merge
  propagation; use `Arr_extended_dcel_text_formatter` (not `Arr_face_extended_text_formatter`) when
  vertex/halfedge data must survive I/O.
* **Expert edits:** wrap every sequence in `notify_before_global_change()` … `notify_after_global_change()`,
  call `relocate_in_new_face()` whenever `insert_at_vertices_ex` reports `new_face == true`, and finish with
  `assert(arr.is_valid())` in debug builds. Remember `he_away` points **away from** the second vertex.
* **Graph algorithms:** call the BGL free functions unqualified; combine `CGAL::Dual<Arr>` with
  `CGAL::Arr_face_index_map<Arr>` for face-level BFS/Dijkstra, and `CGAL::Arr_vertex_index_map<Arr>`
  for the primal graph.

---

## 15. Extended DCEL × Boolean set operations — reconciling `Arr_extended_dcel` with `Gps_default_dcel`

This section closes the gap between §7 (extended DCELs) and `boolean_set_operations.md` (§6, §20):
the project needs **both** per-element user data **and** `Polygon_set_2` Booleans, and the two were
documented independently. They are **not** mutually exclusive — the `Dcel_` template parameter of
`General_polygon_set_2` / `Polygon_set_2` accepts an `Arr_extended_dcel` **provided you feed it the
Gps record bases** — but user data does **not** survive a binary Boolean operation unless you drive
the overlay yourself. Everything below was compiled and run; sources in
`scratchpad/apimap_gpsdcel/` (`t1_plain`, `t2_ext`, `t3_obs`, `t4_req`, `t5_hist`, `t6_perf`,
`t7_relabel`, `t8_cast`, `t9_asan`, `t10_hook`, `t11_skip`, `t12_pipeline`, `t13_assign`,
`t14_norebind`, `t15_dual`, `t16_ident`, `t17_arr2only`).

### 15.1 What a `Dcel_` passed to `General_polygon_set_2` / `Polygon_set_2` must provide

The two template heads, verbatim:

```cpp
// CGAL/General_polygon_set_2.h
template <class Traits_, class Dcel_ = Gps_default_dcel<Traits_> >
class General_polygon_set_2 : public General_polygon_set_on_surface_2
  <Traits_, typename Default_planar_topology<Traits_, Dcel_>::Traits>
{
public:
  typedef Traits_                                         Traits_2;
  typedef Dcel_                                           Dcel;
  typedef General_polygon_set_on_surface_2 <Traits_2,
    typename Default_planar_topology<Traits_2, Dcel >::Traits>   Base;
  typedef CGAL::Arrangement_2<Traits_2, Dcel>             Arrangement_2;
  ...
};

// CGAL/Polygon_set_2.h
template <class Kernel,
          typename Containter = std::vector<typename Kernel::Point_2>,
          class Dcel_ =
            Gps_default_dcel<Gps_segment_traits_2<Kernel, Containter> > >
class Polygon_set_2 :
  public General_polygon_set_2<Gps_segment_traits_2<Kernel, Containter>, Dcel_>
```

The requirements are **nowhere documented**; they are whatever `Gps_on_surface_base_2` and its
helpers dereference. Exhaustive list, with the call sites that force each one:

**(R1) `Dcel_` must derive from `Arr_dcel_base<V, H, F>`** (directly or through
`Arr_dcel` / `Arr_extended_dcel` / `Arr_face_extended_dcel`). `Default_planar_topology<Traits_, Dcel_>`
instantiates `Arr_bounded_planar_topology_traits_2<GeomTraits, Dcel>` /
`Arr_unb_planar_topology_traits_2<...>`, which need `Dcel::Vertex`, `Dcel::Halfedge`, `Dcel::Face`,
`Dcel::Outer_ccb`, `Dcel::Inner_ccb`, `Dcel::Isolated_vertex`, the iterators and `Dcel::Size` — all
of them supplied by `Arr_dcel_base` (§5.1). A bare struct will not do.

**(R2) the face record `F` must supply the whole `Gps_face_base` interface:**

| member (exact signature) | forced by |
|---|---|
| `bool contained() const` | `Gps_on_surface_base_2.h:467,473` (`is_empty`/`is_plane`), `:490` (`oriented_side`), `:571,577,936,1009,1344`, `_impl.h:100,173,196,230,239,652,660,664,675,679,750`, `Gps_agg_op.h:120,127`, `Gps_agg_op_surface_sweep_2.h:229`, `Gps_polygon_validation.h:63`, `connect_holes.h:45,147`, `Polygon_vertical_decomposition_2.h:89,210` |
| `void set_contained(bool b)` | `Gps_on_surface_base_2.h:1011,1315,1576,1606`, `_impl.h:305,395,431,484,532,566`, `Gps_bfs_base_visitor.h:72,80`, `Polygon_vertical_decomposition_2.h:89` |
| `bool visited() const` | `Gps_bfs_scanner.h:52,86`, `_impl.h:96,154,192,220,253,723,748`, `connect_holes.h:415` |
| `void set_visited(bool b) const` | **`const`** — `Gps_bfs_scanner.h:55,89`, `Gps_on_surface_base_2.h:1421` (`_reset_faces(...) const`), `_impl.h:102,120,134,193,710,724`, `connect_holes.h:350` |
| `std::size_t id() const` | `Gps_on_surface_base_2.h:996,1058,1077,1081,1083,1091,1112,1178,1223` (`_remove_redundant_edges`, union–find over faces) |
| `void set_id(std::size_t i)` | `Gps_on_surface_base_2.h:988,992` |
| `bool id_not_set() const` | `Gps_on_surface_base_2.h:987,991,1050,1054,1062,1208,1218` |
| `void reset_id()` | `Gps_on_surface_base_2.h:1287` |
| `Arr_face_base::Outer_ccbs_container& _outer_ccbs()` | `Gps_on_surface_base_2.h:1186,1190` (raw CCB surgery in `_remove_redundant_edges`) |
| `Arr_face_base::Inner_ccbs_container& _inner_ccbs()` | `Gps_on_surface_base_2.h:1188,1191` |

**(R3) the halfedge record `H` must supply the `Gps_halfedge_base` interface:**

| member | forced by |
|---|---|
| `int flag() const` | `Gps_on_surface_base_2.h:1046,1053,1090,1092,1111,1209,1212,1214,1225` |
| `void set_flag(int i)` | `Gps_on_surface_base_2.h:949,979,980,1040,1041,1235,1260` |

`flag()` must default to something outside `{NOT_VISITED=0? …}` — `Gps_halfedge_base`'s ctor sets
`_flag(-1)`; the algorithm resets every edge to `NOT_VISITED` before use
(`Gps_on_surface_base_2.h:979-980`), so the initial value is irrelevant, but the field must exist and
must be per-halfedge (**not** per-edge: `h` and `h->twin()` are set independently).

**(R4) the vertex record `V` needs nothing beyond `ArrDcel`.** `Gps_default_dcel` passes
`Arr_vertex_base<typename Traits_::Point_2>` unchanged; no Gps code touches a vertex field.

**(R5) `rebind` is NOT required.** `Gps_default_dcel` itself has none:

```cpp
template <class Traits_>
class Gps_default_dcel :
  public Arr_dcel_base<Arr_vertex_base<typename Traits_::Point_2>,
                       Gps_halfedge_base<typename Traits_::X_monotone_curve_2>,
                       Gps_face_base >
{ public: Gps_default_dcel() {} };
```

`Arrangement_2` / `Arrangement_on_surface_2` never rebind the DCEL (only
`Arr_bounded_planar_topology_traits_2::rebind<T,D>` and `Arr_unb_planar_topology_traits_2::rebind<T,D>`
exist, and they take the DCEL as an argument). **[verified]** a hand-rolled
`struct My_dcel : Arr_dcel_base<Arr_vertex_base<Pt>, Gps_halfedge_base<Xcv>, My_face> {}` with no
`rebind` works as `Polygon_set_2`'s third parameter. `rebind` *is* required by
`Arrangement_with_history_2` — see §15.7.

**Compiler proof of each requirement [verified]** (`t1_plain.cpp`, `t4_req.cpp`):

| `Dcel_` | result |
|---|---|
| `Arr_dcel<GpsTr>` (plain default) | ✗ `error: no member named 'contained' in '…::Face'` at `Gps_on_surface_base_2_impl.h:353`, then `set_contained`, `visited`, … |
| `Arr_extended_dcel<GpsTr,int,int,std::string, Arr_vertex_base<Pt>, Arr_halfedge_base<Xcv>, Gps_face_base>` | ✗ `no member named 'flag' in '…::Halfedge'`, `no member named 'set_flag'` |
| `Arr_extended_dcel<GpsTr,int,int,std::string, Arr_vertex_base<Pt>, Gps_halfedge_base<Xcv>, Arr_face_base>` | ✗ 38 errors: `contained`, `set_contained`, `visited`, `set_visited`, `id`, `id_not_set`, `reset_id`, `_outer_ccbs`, `_inner_ccbs` |
| `Arr_extended_dcel<GpsTr,int,int,std::string, Arr_vertex_base<Pt>, Gps_halfedge_base<Xcv>, Gps_face_base>` | ✓ compiles, runs, Booleans correct |
| hand-rolled `Arr_dcel_base<Arr_vertex_base<Pt>, Gps_halfedge_base<Xcv>, MyFace : Gps_face_base>` (no `rebind`) | ✓ |

### 15.2 Composing user data with the Gps bases — it works **[verified]**

Both extended-DCEL templates take the three record bases as trailing parameters, so you simply pass
the Gps ones (quoted from `Arr_extended_dcel.h`):

```cpp
template <typename Traits_,
          typename VertexData, typename HalfedgeData, typename FaceData,
          typename VertexBase = Arr_vertex_base<typename Traits_::Point_2>,
          typename HalfedgeBase =
            Arr_halfedge_base<typename Traits_::X_monotone_curve_2>,
          typename FaceBase = Arr_face_base>
class Arr_extended_dcel :
  public Arr_dcel_base<Arr_extended_vertex<VertexBase, VertexData>,
                       Arr_extended_halfedge<HalfedgeBase, HalfedgeData>,
                       Arr_extended_face<FaceBase, FaceData>> { ... };

template <typename Traits_, typename FaceData,
          typename VertexBase = Arr_vertex_base<typename Traits_::Point_2>,
          typename HalfedgeBase =
            Arr_halfedge_base<typename Traits_::X_monotone_curve_2>,
          typename FaceBase = Arr_face_base>
class Arr_face_extended_dcel :
  public Arr_dcel_base<VertexBase, HalfedgeBase,
                       Arr_extended_face<FaceBase, FaceData>> { ... };
```

The working recipe:

```cpp
typedef CGAL::Exact_predicates_exact_constructions_kernel        K;
typedef CGAL::Gps_segment_traits_2<K, std::vector<K::Point_2> >  GpsTr;
typedef GpsTr::Point_2                                           Pt;
typedef GpsTr::X_monotone_curve_2                                Xcv;

typedef CGAL::Arr_extended_dcel<GpsTr,
          /*VertexData*/ int, /*HalfedgeData*/ int, /*FaceData*/ std::string,
          CGAL::Arr_vertex_base<Pt>,          // vertex base   (plain)
          CGAL::Gps_halfedge_base<Xcv>,       // halfedge base (MUST be the Gps one)
          CGAL::Gps_face_base>                // face base     (MUST be the Gps one)
                                                                 ExtDcel;

typedef CGAL::Polygon_set_2<K, std::vector<K::Point_2>, ExtDcel>  Pset;
typedef Pset::Arrangement_2                                       Arr;   // Arrangement_2<GpsTr, ExtDcel>
```

Observed **[verified]** (`t2_ext.cpp`):

* compiles clean (no errors);
* `sizeof(Arr::Face)` = 144 vs 120 for `Gps_default_dcel`'s face (`std::string` payload);
* `ps.arrangement()` returns a usable `Arrangement_2&` — `number_of_vertices/edges/faces`,
  `faces_begin()`, `set_data()`, `contained()` all work;
* `ps.join(box)` produces the correct union (1 p-w-h, 8-vertex outer boundary, area 28);
* the `Arr_face_extended_dcel` variant with the same two Gps bases also compiles and joins correctly;
* copy-construction / `operator=` of the **polygon set** preserves the face data (they go through
  `new Aos_2(*(ps.m_arr))` ⇒ `Arr_dcel_base::assign` ⇒ `dup_f->assign(*fit)`).

**Nuance: `Arr_extended_face::assign` *hides* rather than *overrides* the Gps virtual. [verified]**
`Gps_face_base` declares `virtual void assign(const Arr_face_base& f)`, while
`Arr_extended_face<FaceBase,FaceData>` declares `virtual void assign(const Face_base& f)` with
`Face_base = Gps_face_base` — a *different* parameter type, hence a new virtual, not an override
(clang: `warning: 'CGAL::Arr_extended_face<CGAL::Gps_face_base, std::string>::assign' hides
overloaded virtual function [-Woverloaded-virtual]`, `Arr_extended_dcel.h:140`). Consequences:

* the statically typed call `dup_f->assign(*fit)` inside `Arr_dcel_base::assign` (§5.4) resolves to
  the extended one ⇒ **data is copied** — verified: `data='DATA'`, `contained=1`;
* a call through an `Arr_face_base*` dispatches to `Gps_face_base::assign` ⇒ **data silently dropped**
  — verified: `data=''`, `contained=1`. CGAL 6.1 never makes that call, but do not add one.
* the same hiding happens for `Arr_extended_halfedge<Gps_halfedge_base<Xcv>, D>`; note additionally
  that **`Gps_halfedge_base` has no `assign` at all**, so `_flag` is never copied by `assign()`.
  Harmless — every Gps algorithm resets the flags before reading them.

### 15.3 `ps.arrangement()`'s unchecked `static_cast` — formally UB, sound in practice **[verified]**

```cpp
const Arrangement_2& arrangement() const
{ return *(static_cast<const Arrangement_2*>(this->m_arr)); }
Arrangement_2& arrangement()
{ return *(static_cast<Arrangement_2*>(this->m_arr)); }
```

`m_arr` is `Aos_2* = Arrangement_on_surface_2<Traits_2, Topology_traits>*` and the object was
allocated as `new Aos_2(m_traits)` — it is a **base** object, never an `Arrangement_2`. Measured on
the extended DCEL above (`t8_cast.cpp`):

```
sizeof(Arrangement_2)=248  sizeof(Base=Aos_2)=248
same address: 1
polymorphic: 1
dynamic typeid of the object actually allocated by Gps: Arrangement_on_surface_2 (BASE!)
dynamic_cast<Arrangement_2*> succeeds: 0
```

So `dynamic_cast` **fails** and `typeid` reports the base — the `static_cast` is a downcast to a type
the object never had. It works because `Arrangement_2` adds **zero data members** and contains **no
`virtual` token at all** (`grep -c virtual Arrangement_2.h` → 0): it is a pure interface layer whose
members only touch `Base` state. **[verified]** through the fake reference:
`unbounded_face()->contained()`, `number_of_unbounded_faces()`, `number_of_vertices_at_infinity()`,
`traits()`, `is_valid()`, `CGAL::insert(a, cv)` and `Arrangement_2 copy(a);` (the
`Arrangement_2(const Base&)` ctor) all behave correctly, and the copy keeps the extended data.

Binding rules that follow:

* never `dynamic_cast` anything obtained from `ps.arrangement()`, and never store it in a
  `std::shared_ptr<Arrangement_2>` with a deleter — the object must be destroyed by
  `~Gps_on_surface_base_2` as an `Aos_2`;
* `typeid`-based type erasure over `ps.arrangement()` will not match `Arrangement_2`;
* everything else (handles, iterators, free functions, observers) is fine.

### 15.4 What happens to user data across a Boolean operation

#### 15.4.1 Which operations replace the arrangement **object** — measured table **[verified]**

BSO gotcha 3 says "every binary Boolean op replaces the underlying arrangement object". That is true
of the *general* path, but the special cases in `_join/_intersection/_difference/_symmetric_difference`
short-circuit and keep it. Measured by comparing `&ps.arrangement()` before/after (`t16_ident.cpp`):

| call | arrangement object |
|---|---|
| `empty.join(Polygon_2)` | **REPLACED** (`Aos_2* arr = new Aos_2(m_traits); _insert(pgn,*arr); delete this->m_arr; this->m_arr = arr;`) |
| `nonempty.join(Polygon_2)` | **REPLACED** |
| `empty.join(Pset)` | preserved (`*(this->m_arr) = *(other.m_arr);`) |
| `nonempty.join(Pset)` | **REPLACED** |
| `nonempty.join(empty Pset)` | preserved (early `return`) |
| `res.join(a, b)` — **3-arg form** | **preserved** (`this->clear(); _join(*a.m_arr, *b.m_arr, *this->m_arr);`) |
| `nonempty.intersection(Pset)` | **REPLACED** |
| `nonempty.intersection(empty Pset)` | preserved (`m_arr->clear()`) |
| `nonempty.difference(Pset)` | **REPLACED** |
| `nonempty.symmetric_difference(Pset)` | **REPLACED** |
| `complement()`, `insert()`, `clear()`, `fix_curves_direction()`, `remove_redundant_edges()` | preserved |
| `operator=` | **REPLACED** (`delete m_arr; … m_arr = new Aos_2(*(ps.m_arr));`) |
| `join(range)` / any aggregated range op | **REPLACED** (`Gps_merge.h:70` `delete (arr_vec[count].first);`, incl. `arr_vec[0] == this->m_arr`) |

⇒ **whether your `Arrangement_2&`, your handles and your observer survive depends on the run-time
contents of the operands.** A binding must assume "replaced" unconditionally.

The 3-arg `void join(const Self& gps1, const Self& gps2)` (and its `intersection` / `difference` /
`symmetric_difference` siblings) is the only binary form that is guaranteed to keep the object —
this is what makes §15.4.4 possible.

#### 15.4.2 There is no overlay-traits hook — the functors are hard-wired

```cpp
void _join(const Aos_2& arr)
{
  Aos_2* res_arr = new Aos_2(m_traits);
  Gps_join_functor<Aos_2> func;                  // <-- local, non-virtual, not a parameter
  overlay(*m_arr, arr, *res_arr, func);
  delete m_arr; // delete the previous arrangement
  m_arr = res_arr;
  remove_redundant_edges();
  //fix_curves_direction(); // not needed for join
  CGAL_assertion(is_valid());
}
```

Identical shape in `_intersection` (`Gps_intersection_functor`), `_difference`
(`Gps_difference_functor`), `_symmetric_difference` (`Gps_sym_diff_functor`), and
`do_intersect`/`oriented_side` (`Gps_do_intersect_functor`). The functor is a **local variable of a
non-virtual private member function**; there is no template parameter, no setter, no virtual to
override. `Gps_on_surface_base_2`'s only policy parameter is

```cpp
template <class Traits_, class TopTraits_,
          class ValidationPolicy =
          Boolean_set_operation_2_internal::NoValidationPolicy>
class Gps_on_surface_base_2
```

and `ValidationPolicy` supplies only `static void is_valid(const Polygon&, const Traits&)`.

What the stock functors do to data: **nothing**. `Gps_base_functor<Arrangement_>` has empty
`create_face`, seven empty `create_vertex` overloads and three empty `create_edge` overloads;
`Gps_join_functor` overrides exactly one method:

```cpp
void create_face (Face_const_handle f1, Face_const_handle f2, Face_handle res_f)
{ if(f1->contained() || f2->contained()) res_f->set_contained(true); }
```

⇒ every result feature is a **freshly allocated record with default-initialised `Data`**
(§ gotcha 4: for scalar `Data` that means *indeterminate*, not 0).

**Measured [verified]** (`t2_ext.cpp`): faces of a 4-vertex box labelled `"face#0[out]"` /
`"face#1[in]"`, every halfedge and vertex given a non-zero `int`; after `ps.join(box(2,2,6,6))` the
result arrangement's faces read back `''` and `''`, and the halfedge/vertex `int` sums are `0`.

The **aggregated** (range) path is even further out of reach: `Gps_agg_op::sweep_arrangements`
re-sweeps the *edges* of the inputs (`Gps_agg_op.h:116-131`, filtered by
`he->face()->contained() == he->twin()->face()->contained()`), builds a brand-new arrangement, and
`Gps_merge.h:70-71` deletes every input arrangement including the set's own. Only geometry and
`contained()` cross that boundary; there is no functor at all.

#### 15.4.3 The escape hatch: run the overlay yourself **[verified]**

`CGAL::overlay` *does* take overlay traits, `remove_redundant_edges()` is **public**, and the 3-arg
Boolean form proves that writing into the set's existing arrangement is legitimate. So you can
reproduce `_join` with your own functor (`t10_hook.cpp`):

```cpp
struct My_join : public CGAL::Gps_join_functor<Arr::Base> {
  typedef CGAL::Gps_join_functor<Arr::Base> B;
  void create_face(B::Face_const_handle f1, B::Face_const_handle f2, B::Face_handle r) {
    B::create_face(f1, f2, r);                        // sets contained()
    r->set_data("{" + f1->data() + "|" + f2->data() + "}");
  }
  void create_edge(B::Halfedge_const_handle h1, B::Halfedge_const_handle h2, B::Halfedge_handle r)
  { r->set_data(h1->data() + h2->data()); }
  void create_edge(B::Halfedge_const_handle h1, B::Face_const_handle, B::Halfedge_handle r)
  { r->set_data(h1->data()); }
  void create_edge(B::Face_const_handle, B::Halfedge_const_handle h2, B::Halfedge_handle r)
  { r->set_data(h2->data()); }
  // the seven create_vertex overloads are inherited as no-ops; override as needed
};

Pset a(...), b(...), out;
out.clear();
My_join func;
CGAL::overlay(a.arrangement(), b.arrangement(), out.arrangement(), func);
out.remove_redundant_edges();      // public; exactly what _join() does next
// out.fix_curves_direction();     // additionally required for difference / symmetric_difference
```

Result **[verified]**: arrangement object preserved, `v/e/f = 8/8/2`, `polygons_with_holes()` yields
the correct union (outer size 8, area 28), `out.is_valid() == 1`, and the merged labels are present.

**But `remove_redundant_edges()` destroys most of the labels.** Printed before/after **[verified]**:

```
BEFORE remove_redundant_edges: f=4 e=12
   pre face contained=0 data='{-|-}'
   pre face contained=1 data='{A|-}'
   pre face contained=1 data='{A|B}'
   pre face contained=1 data='{-|B}'
AFTER:  v/e/f = 8/8/2
   face contained=0 data='{-|-}'
   face contained=1 data='{A|-}'
```

The three contained overlay faces are merged into one; `_remove_redundant_edges` keeps **one
arbitrary face record** (union–find master, `Gps_on_surface_base_2.h:1165-1290`) and deletes the
others together with their data. There is no merge callback.

Two ways out:

1. **Snapshot before the merge.** Between `overlay(...)` and `remove_redundant_edges()`, walk the
   result faces and record `(representative point, data)` in your own container; after the merge,
   re-locate those points. This is the only way to keep a *set-valued* label per merged region.
2. **Skip `remove_redundant_edges()` entirely.** **[verified]** (`t11_skip.cpp`): all four faces and
   all labels survive, `polygons_with_holes()` still returns the correct single polygon
   (outer size 8, area 28) — **but `out.is_valid()` becomes `0`**, and every subsequent Boolean op on
   that set runs `CGAL_assertion(is_valid())`. Use only as a terminal, read-only result.

#### 15.4.4 Summary of the data-survival matrix **[verified]**

| path | vertex data | halfedge data | face data |
|---|---|---|---|
| `Pset` copy ctor / `operator=` / `Arrangement::assign` | ✓ | ✓ | ✓ |
| `insert()` on existing features | ✓ (untouched) | ✓ (untouched) | ✓ (untouched); features *created* by the insert get default data |
| `complement()` | ✓ | ✓ | ✓ (only `contained` flips) |
| stock `join/intersection/difference/symmetric_difference` (2- or 3-arg) | ✗ | ✗ | ✗ |
| stock aggregated range ops | ✗ | ✗ | ✗ |
| DIY `overlay` + your functor, **without** `remove_redundant_edges` | ✓ | ✓ | ✓ |
| DIY `overlay` + your functor, **with** `remove_redundant_edges` | ✓ | ✓ | partial — one record per merged region survives |

### 15.5 Observers across a Boolean op **[verified]**

**The observer type.** `CGAL/Arr_observer.h` is now just
`template <typename Arrangement_> using Arr_observer = typename Arrangement_::Observer;`, and
`Arrangement_on_surface_2` declares `using Observer = Aos_observer<Self>;` with `Self` = the **base**
arrangement. So `CGAL::Arr_observer<Pset::Arrangement_2>` is
`CGAL::Aos_observer<Pset::Arrangement_2::Base>` — **[verified]** `std::is_same<...>::value == 1`.
Writing `CGAL::Aos_observer<Pset::Arrangement_2>` explicitly does *not* work (its `p_arr` would be a
type `_register_observer` does not accept). Always spell it `Arr_observer<Arr>` or
`Aos_observer<Arr::Base>`.

**In-place operations do notify.** **[verified]** attaching to `ps.arrangement()` and calling
`ps.insert(box)` fires `after_create_edge` ×4 and `after_split_face` ×1; `ps.clear()` fires
`after_clear`.

**A replacing Boolean op silently detaches the observer — it does *not* dangle.**
`~Arrangement_on_surface_2` (`Arrangement_on_surface_2_impl.h:232-242`) ends with

```cpp
  // Detach all observers still attached to the arrangement.
  Observers_iterator  iter = m_observers.begin();
  ...
  while (iter != end) { next = iter; ++next; (*iter)->detach(); iter = next; }
```

and `Aos_observer::detach()` sets `p_arr = nullptr`. **[verified]**: before `ps.join(box)`
`obs->arrangement() == 0x105c8a710`; after, `obs->arrangement() == nullptr` while the set's new
arrangement is at `0x105c8c340`. The observer object itself is untouched and safe to `delete`; it
simply stops receiving events, and `detach()` is a no-op afterwards (`if (p_arr == nullptr) return;`).
No events are delivered *during* the replacing op (the work happens in a different object):
`creates seen during join = 0`.

**The crash risk: `before_detach()` runs on a half-destroyed arrangement. [verified — SIGSEGV]**
The destructor deletes every point and every curve *first* (`Arrangement_on_surface_2_impl.h:214-224`)
and detaches observers *last* (`:232-242`). An observer that reads geometry in `before_detach()`
therefore reads freed memory. Test `t9_asan.cpp` prints
`before_detach: nv=4` (the DCEL records are still linked) and then **segfaults** (exit 139) on
`v->point()`. In a binding:

* `before_detach()` / `after_detach()` may only touch your own bookkeeping — **never**
  `->point()`, `->curve()`, or anything that dereferences the traits-owned geometry;
* treat `after_detach()` as "the whole arrangement is gone": drop every cached
  `(void* record, generation)` pair for that arrangement and bump the generation (§14).

**Working recipe if you need an observer to survive Boolean ops:** use the **3-arg** form, which
preserves the object (§15.4.1). **[verified]**: with `Obs obs(r.arrangement()); r.join(a, b);` the
object address is unchanged, `obs.arrangement()` stays non-null, and the observer sees
`after_clear` ×2 and `after_create_edge` ×12 as the overlay builds the result — you can label
features as they are created. (You still cannot tell *which input face* a result face came from
from the observer alone; combine with §15.4.3 or with point location into the still-alive `a` / `b`.)

### 15.6 The round-trip alternative: BSO on a plain set, then rebuild your own arrangement

Pipeline: run the Boolean op on a stock `Polygon_set_2` (default `Gps_default_dcel`), extract
`Polygon_with_holes_2`, convert each boundary edge to an `X_monotone_curve_2`, aggregate-insert into
your own `Arrangement_2` / `Arrangement_with_history_2` over an `Arr_extended_dcel`, then re-attach
data by point location.

**Cost, ~1000-edge inputs, measured [verified]** (`t6_perf.cpp`; two interlocking 1003-vertex
rectilinear combs, union = 1007 boundary segments; Apple Silicon, clang 17, `-O2 -DNDEBUG -DCGAL_NDEBUG`;
`-O0` figures in parentheses):

| step | ms |
|---|---|
| `ps.join(B)` on `Gps_default_dcel` | **3.59** (223.5) |
| same join on the extended DCEL of §15.2 | 3.21 (225.6) — no measurable penalty |
| `polygons_with_holes()` (1007 segs) | 0.027 (0.29) |
| build 1007 `X_monotone_curve_2` | 0.223 (1.27) |
| `CGAL::insert(user_arr, cvs.begin(), cvs.end())` (aggregate sweep) | 0.823 (45.5) |
| **round-trip overhead total** | **1.07 ms ≈ 30 % of the join** (47.0 ms ≈ 21 %) |

**With many faces and real relabelling [verified]** (`t7_relabel.cpp`; a 1000×20 box minus 250 boxes
⇒ 1004 edges / 252 faces, 250 sequential `difference` calls):

| step | ms |
|---|---|
| BSO (250 `difference` ops) | **59.46** |
| `polygons_with_holes()` (1004 segs) | 0.056 |
| rebuild `Arrangement_2<SegTr, Arr_extended_dcel<…>>` (1004 curves, 252 faces) | 0.989 |
| build `Arr_trapezoid_ric_point_location` over the source arrangement | 2.007 |
| locate 251 representative points (all 251 hit data) | 0.103 |
| **round-trip total** | **3.15 ms ≈ 5 % of the BSO** |

**Verdict: yes, this is the recommended design.** The round trip costs 5–30 % of the Boolean
operation itself and scales the same way (one extra sweep). In exchange you get:

* a DCEL you fully control (no `Gps_face_base`/`Gps_halfedge_base` constraints, no `mutable char
  m_info` written through `const` handles, no reserved `visited`/`id`/`flag` fields);
* stable arrangement-object identity, so observers, handle caches and index maps never dangle;
* the ability to use `Arrangement_with_history_2` (§15.7), which BSO can never give you.

Cost you must budget for: the semantics of "which input contributed this face" are **not** in the
extracted polygons. Re-derive them with point location into the (still alive) input arrangements, or
use the DIY overlay of §15.4.3 when you need exact provenance.

Only prefer the in-place extended-DCEL polygon set (§15.2) when the data is (a) written *after* the
last Boolean op, or (b) recomputable from `contained()` and geometry alone.

### 15.7 `Arrangement_with_history_2` + extended DCEL + Boolean set operations

**(a) BSO can never operate on an `Arrangement_with_history_2`.** `Gps_on_surface_base_2` hard-codes

```cpp
  typedef CGAL::Arrangement_on_surface_2<Traits_2, Topology_traits>
                                                       Arrangement_on_surface_2;
private:
  typedef Arrangement_on_surface_2                     Aos_2;
  ...
  Aos_2*        m_arr;
```

There is no template parameter for the arrangement class; `Dcel_` only selects the DCEL.
`Arrangement_with_history_2<GeomTraits, Dcel>` derives from
`Arrangement_on_surface_2<Arr_consolidated_curve_data_traits_2<GeomTraits, Curve_2*>, Data_top_traits>`
— a *different* `Arrangement_on_surface_2` specialisation — so it is not even convertible to `Aos_2*`.
The protected `Gps_on_surface_base_2(Aos_2* arr)` ctor (`:234`) is not re-exposed by
`General_polygon_set_2`, so you cannot inject one either. **⇒ curve history and BSO are only
combinable through the §15.6 round trip.**

**(b) `Gps_default_dcel` cannot be an `Arrangement_with_history_2` DCEL. [verified]**
`Arrangement_on_surface_with_history_2.h:59,91` needs `typename Base_dcel::template rebind<Data_traits_2>`
and `Gps_default_dcel` has none:

```
error: no member named 'rebind' in 'CGAL::Gps_default_dcel<CGAL::Gps_segment_traits_2<…>>'
  at Arrangement_on_surface_with_history_2.h:59 and :91
```

**(c) `Arr_extended_dcel` *does* rebind — and silently drops `Gps_halfedge_base`. [verified]**

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

`Gps_halfedge_base<Xcv>` declares no `rebind`, so `HalfedgeBase::template rebind<Xcv>` resolves to the
one **inherited from `Arr_halfedge_base<Xcv>`**:
`template <typename XCV> struct rebind { typedef Arr_halfedge_base<XCV> other; };`. Measured with
`DataTr = Arr_consolidated_curve_data_traits_2<GpsTr, GpsTr::Curve_2*>`:

```
rebound halfedge base is Arr_halfedge_base (Gps flag DROPPED): 1
rebound halfedge base is Gps_halfedge_base (flag KEPT):        0
rebound face still Gps_face_base:                              1     // FaceBase is passed through untouched
```

⇒ `Arrangement_with_history_2<GpsTr, Arr_extended_dcel<…, Gps_halfedge_base<Xcv>, Gps_face_base>>`
compiles and works (**[verified]**: 4 vertices / 4 edges / 2 faces / 4 curves, face `data()`
round-trips, `contained()` present) — but its halfedges have **no `flag()`**. Harmless for a
with-history arrangement, fatal if you ever try to feed that rebound DCEL to BSO.

**(d) One DCEL for both, if you want it. [verified]** Give `Gps_halfedge_base` the `rebind` it is
missing (`t15_dual.cpp`):

```cpp
template <class XCV>
struct Gps_he_rb : public CGAL::Gps_halfedge_base<XCV> {
  template <class XCV2> struct rebind { typedef Gps_he_rb<XCV2> other; };
};

typedef CGAL::Arr_extended_dcel<GpsTr, int, int, std::string,
          CGAL::Arr_vertex_base<Pt>, Gps_he_rb<Xcv>, CGAL::Gps_face_base>  DualDcel;

typedef CGAL::Polygon_set_2<K, std::vector<K::Point_2>, DualDcel>  Pset;   // ✓ BSO
typedef CGAL::Arrangement_with_history_2<GpsTr, DualDcel>          AwH;    // ✓ history
```

Results: `Pset` join correct (1 p-w-h, outer 8, area 28); `DualDcel::rebind<DataTr>::other`'s halfedge
still derives from `Gps_halfedge_base` (`1`); the `AwH` builds (4/4/2, 4 curves) with working
`data()`, `contained()` and `flag() == -1`. This does **not** make BSO use the history arrangement —
it only means one DCEL type serves both roles, which simplifies a type-erased core.

**(e) The verified pipeline** (`t12_pipeline.cpp`): `Polygon_set_2<K>` difference → `polygons_with_holes()`
→ `CGAL::insert` into `Arrangement_with_history_2<Arr_segment_traits_2<K>,
Arr_extended_dcel<SegTr, int, HData, FData>>` → per-halfedge data from
`induced_edges_begin/end(curve)` → per-face data. Output: 8/8/3 with 8 curves, 8 halfedges tagged
ring 0 and 8 tagged ring 1, three faces labelled `outside`/`region`/`region` (the last with 1 inner
CCB), and `number_of_originating_curves(he) == 1`. Since the with-history rebind of a *plain*
`Arr_extended_dcel` only touches the vertex/halfedge bases and leaves `Vertex_data`/`Halfedge_data`/
`Face_data` and `Face_base` alone, all three data slots survive (consistent with
`arrangement_with_history.md` §2.1).

### 15.8 Recommendation for the type-erased core / Cython binding

1. **Keep the two worlds separate.** Model the Python `PolygonSet` on a stock
   `CGAL::Polygon_set_2<K>` (default `Gps_default_dcel`) — no user data, no observers, no cached
   handles. Model the Python `Arrangement` on `Arrangement_2` / `Arrangement_with_history_2` over
   your own `Arr_extended_dcel`. Bridge them with the §15.6 round trip.
2. **If you must expose `PolygonSet.arrangement()`** with user data, use the extended DCEL of §15.2
   (`Arr_extended_dcel<GpsTr, V, H, F, Arr_vertex_base<Pt>, Gps_halfedge_base<Xcv>, Gps_face_base>`),
   and document that the data is wiped by every Boolean op. Bump the arrangement generation counter
   (§14) on *every* Boolean call — the object-identity table in §15.4.1 shows you cannot predict
   which ones replace it.
3. **Never keep a `const Arrangement_2&`, a handle, an `Arr_face_index_map`, or an observer alive
   across a Boolean call** on a polygon set. If you need one, use the 3-arg `res.op(a, b)` form and
   re-verify `observer.arrangement() != nullptr` afterwards.
4. **If you need provenance** (which input produced which output face), implement the Boolean op
   yourself with `CGAL::overlay` + a `Gps_*_functor` subclass (§15.4.3) and either snapshot labels
   before `remove_redundant_edges()` or accept `is_valid() == false` and treat the result as
   read-only.
5. **Never expose `before_detach()` to Python.** Route observer callbacks that Python can see
   through `after_split_face` / `after_create_edge` / `after_clear` only; make `before_detach` /
   `after_detach` invalidate your handle cache in C++ and return immediately.
