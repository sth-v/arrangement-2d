# CGAL 6.1 — Exception propagation, DCEL state after a throw, and user-callback safety

Target: a type-erased C++ core + Cython bindings for the CGAL 2D Arrangement package, in which
**observers, overlay `create_*` callbacks, output iterators and (potentially) traits functors are
implemented in Python**, so a Python exception becomes a C++ exception thrown from deep inside
CGAL.

Companion to `number_types_and_errors.md` §8 (which documents the exception *hierarchy* and the
handler API). This file documents what happens to the *live data structures* when such an
exception escapes.

**Installation documented here** (everything below was read out of the installed headers and/or
verified by compiling and running):

* Headers: `/opt/homebrew/include/CGAL` — CGAL **6.1** (`CGAL_VERSION_NR 1060101000`,
  release date 20250929, git `b26b07a1242`), header-only.
* CORE: `/opt/homebrew/include/CGAL/CORE` + `/opt/homebrew/include/CGAL/CORE_*.h`.
* GMP/MPFR: Homebrew, `/opt/homebrew`.
* Compile line used for every verification in this document:

```
/usr/bin/clang++ -std=c++17 -O0 -DCGAL_USE_CORE -DCGAL_USE_GMP -DCGAL_USE_MPFR \
  -I/opt/homebrew/include -L/opt/homebrew/lib -lgmp -lmpfr -o test test.cpp
```

Two variants were also built where explicitly noted: the same line plus `-DNDEBUG` (CGAL checks
off), and the same line plus `-fno-exceptions`.

**Notation.**
* **[verified]** = printed by a compiled-and-run probe program built with the line above.
* **[header]** = read from the installed header text; the file:line is given.
* Quoted code is verbatim from the headers.
* “Leak” numbers are net live `operator new` blocks, measured by replacing the global
  `operator new`/`operator delete` with counting versions and subtracting the value of the same
  program on the non-throwing (control) path. A control run measures **2** blocks (CGAL statics),
  so “net 2” means *no leak*.

---

## Gotchas / surprises vs. older CGAL and vs. naive expectations

1. **A user callback that throws does NOT abort the DCEL surgery in progress — it interrupts it
   half-way, and `arr.is_valid()` will often still say `YES`.** `is_valid()` is a *local*
   topological audit; it does not detect an inner CCB that was created but never registered in its
   face. **[verified]** Throwing from `after_create_edge` during `insert_in_face_interior(cv, f)`
   leaves the new edge in the DCEL (`number_of_edges()` counts it) but *not* reachable from the
   face’s hole iterators — `f->number_of_holes()` stays 1 instead of 2, 4 halfedges reachable
   instead of 6 — and `is_valid()` returns `YES`. **Never use `is_valid()` as your
   “did the exception corrupt it?” test.**

2. **`overlay()` temporarily overwrites `Vertex::inc()` in BOTH `const` input arrangements, and
   the restore is not exception-safe.** `Indexed_sweep_accessor::before_init()` squats the
   `p_inc` field (which normally holds the incident-halfedge pointer / isolated-vertex pointer +
   LSB flag) with a plain integer index; `after_init()` restores it. There is **no RAII guard and
   no try/catch** (`No_intersection_surface_sweep_2.h:333-345`). **[verified]** An exception
   thrown between them (from the geometry traits, from `std::bad_alloc`, from a Python-driven
   interrupt) leaves both const inputs with `inc()` = `0,1,2,3` / `4,5,6,7`; `degree()` returns 0,
   `is_isolated()` returns the LSB of the index, and `is_valid()` **segfaults**. This is a silent,
   permanent, unrecoverable corruption of two arrangements the caller believes are `const`.
   The window is narrow (sweep *initialisation* only) but it is real.

3. **Every sweep-based algorithm leaks on unwinding.** `_complete_sweep()` — the only place that
   destroys the `Subcurve` array — is skipped, and
   `~No_intersection_surface_sweep_2` frees only `m_traits` and `m_queue`
   (`Surface_sweep_2/No_intersection_surface_sweep_2_impl.h:76-84`). **[verified]** net leaked
   blocks: aggregate `CGAL::insert(range)` **+25/26**, `overlay` **+17/+21**, `decompose`
   **+17**, batched `locate` **+9**, `Gps::intersection` **+31**. The zone algorithm and
   `Make_x_monotone_2` do **not** leak.

4. **`CGAL_error()` can never be “continued through”, and `set_error_behaviour(CGAL::CONTINUE)`
   still throws.** For *errors* the `switch` in `assertion_fail`/`precondition_fail`/
   `postcondition_fail` falls through `CONTINUE` into `throw`
   (`assertions_impl.h:157-224`). **[verified]** With a no-op handler installed and
   `set_error_behaviour(CGAL::CONTINUE)`, `CGAL::precondition_fail(...)` still threw
   `CGAL::Precondition_exception`. So the `return nullptr;` lines that follow `CGAL_error()`
   in the topology traits are dead code **as long as the checks are compiled in**. Only `ABORT`,
   `EXIT`, `EXIT_WITH_SUCCESS`, or a handler that `longjmp`s/`exit`s changes that.

5. **The real “continues with a null” hazard is `NDEBUG`, not the handler.** `CGAL_error()` /
   `CGAL_error_msg()` are defined unconditionally (`assertions.h:343-344`) and are never disabled,
   but `CGAL_precondition_msg(false, …); return Halfedge_handle();` in
   `Arrangement_on_surface_2::merge_edge` **is** disabled by `NDEBUG`. **[verified]** built with
   `-DNDEBUG`, `merge_edge` on two non-adjacent edges returns a **singular
   `Halfedge_handle()`** with no diagnostic, and dereferencing it segfaults; and
   `remove_isolated_vertex()` on a degree-2 vertex crashes with SIGBUS. **Ship with
   `-O3 -DNDEBUG -DCGAL_DEBUG`, never plain `-DNDEBUG`.**

6. **`insert_at_vertices(cv, v1, v2)` deletes DCEL records *before* it validates its arguments.**
   `Arrangement_on_surface_2_impl.h:1031-1032` erases and frees `v1`’s `Isolated_vertex` record,
   and only at `:1050` does the assertion “The two isolated vertices must be located inside the
   same face” fire. **[verified]** After catching it, `number_of_isolated_vertices()` dropped 2→1
   while `v1->is_isolated()` still returns `true` — i.e. `v1->isolated_vertex()` is a **dangling
   pointer** — and `is_valid()` still returns `YES`. Calling `v1->face()` afterwards is a
   use-after-free.

7. **`before_*` / `after_*` observer notifications are not exception-paired.** **[verified]**
   `insert_non_intersecting_curve` calls `notify_before_global_change()` and, when the insertion
   throws, `after_global_change()` is never delivered. Same for every
   `_notify_before_X` / `_notify_after_X` pair. A Python observer must tolerate an unbalanced
   sequence forever after.

8. **An observer that throws from `before_detach()`/`after_detach()` calls `std::terminate`.**
   `~Arrangement_on_surface_2` detaches every still-attached observer
   (`Arrangement_on_surface_2_impl.h:232-242`), and destructors are implicitly `noexcept`.
   **[verified]** `libc++abi: terminating due to uncaught exception of type Boom`. This is the
   only `std::terminate` path in the package — there is **no `noexcept` anywhere** in the sweep,
   arrangement, DCEL or observer headers **[verified by grep]**.

9. **`-fno-exceptions` does not compile CGAL 6.1 at all.** **[verified]** hard errors in
   `Uncertain.h:114`, `exceptions.h:182`, `assertions_impl.h:173/195/217/240`,
   `Interval_nt.h:1570`, `Object.h:169` (“cannot use 'throw' with exceptions disabled”). The
   question “what does `-fno-exceptions` do to the sweep” has no answer: you cannot build it.

10. **Overlay `create_*` callbacks are the *safe* callbacks.** They run during `_sweep()` /
    `after_sweep()`, i.e. strictly **after** `accessor.after_init()` has restored `inc()`.
    **[verified]** throwing from `create_face`, `create_vertex` and `create_edge` left both input
    arrangements byte-identical and `is_valid()`; only the *result* arrangement was left
    half-built (and `is_valid()` said `YES` about the half-built result), plus the sweep leak.

11. **`Gps_on_surface_base_2` binary ops have a strong guarantee on the *value* and no guarantee
    on the *representation*.** Every one of `_intersection/_join/_difference/
    _symmetric_difference(const Aos_2&)` is
    `res_arr = new Aos_2(...); overlay(*m_arr, arr, *res_arr, func); delete m_arr; m_arr = res_arr;`
    (`Gps_on_surface_base_2.h:1481-1492, 1542-1553, 1618-1629, 1685-1696`). If `overlay` throws,
    `m_arr` is untouched so the set still reports its old value **[verified]** — but `res_arr` is
    leaked (raw `new`, no RAII, no catch) **[verified +31 blocks]**, and if the throw landed in the
    sweep *init* then `m_arr`’s own `inc()` fields are the squatted indices and
    `Polygon_set_2::is_valid()` **segfaults** **[verified]**.

12. **`CGAL_assertion(is_valid())` fires *after* `m_arr` has already been replaced** in the same
    Gps functions. With `THROW_EXCEPTION` this throws an `Assertion_exception` from a state in
    which the old arrangement is already `delete`d — there is nothing to roll back to. **[header]**

13. **`insert_non_intersecting_curve` only checks the two *endpoints*.** Its two preconditions
    (`Arrangement_2/Arrangement_on_surface_2_global.h:684, 692, 711, 722`) verify that neither end
    lies *on* an edge. Nothing checks that the curve’s interior does not cross the arrangement.
    Likewise `insert_in_face_interior(cv, f)` has **no precondition at all** — **[verified]**
    inserting a curve at (10,10)-(12,12) into the *bounded* face of a 4×4 square succeeded
    silently and `is_valid()` returned `YES`. Argument validation for these is **your** job.

---

## 1. The failure pipeline, exactly

### 1.1 The macros that reach it — `assertions.h`

```cpp
[[noreturn]] CGAL_EXPORT void assertion_fail      ( const char*, const char*, int, const char* = "") ;
[[noreturn]] CGAL_EXPORT void precondition_fail   ( const char*, const char*, int, const char* = "") ;
[[noreturn]] CGAL_EXPORT void postcondition_fail  ( const char*, const char*, int, const char* = "") ;
             CGAL_EXPORT void warning_fail        ( const char*, const char*, int, const char* = "");
```
(`assertions.h:78-84`)

```cpp
// CGAL error
#define CGAL_error_msg(MSG) ::CGAL::assertion_fail( "", __FILE__, __LINE__, MSG )
#define CGAL_error()        ::CGAL::assertion_fail( "", __FILE__, __LINE__ )
```
(`assertions.h:342-344`) — **defined outside every `#ifdef`, so `NDEBUG` does not remove them.**

### 1.2 Dispatch — `assertions_impl.h:179-197`

```cpp
CGAL_INLINE_FUNCTION
void
precondition_fail( const char* expr,
                   const char* file,
                   int         line,
                   const char* msg)
{
    get_static_error_handler()("precondition", expr, file, line, msg);
    switch (get_static_error_behaviour()) {
    case ABORT:
        std::abort();
    case EXIT:
        std::exit(1);  // EXIT_FAILURE
    case EXIT_WITH_SUCCESS:
        std::exit(0);  // EXIT_SUCCESS
    case CONTINUE:
    case THROW_EXCEPTION:
    default:
        throw Precondition_exception("CGAL", expr, file, line, msg);
    }
}
```
`assertion_fail` (`:157`) and `postcondition_fail` (`:203`) are identical modulo the exception
type. `warning_fail` (`:226`) is the only one where `CONTINUE` really continues.

**Consequences for a binding’s error handler.** **[verified]**

| you do | what happens |
|---|---|
| install a no-op `Failure_function` | the `cerr` block is suppressed; the exception is **still thrown** |
| `set_error_behaviour(CGAL::CONTINUE)` | still throws (`CONTINUE` falls through to `throw` for errors) |
| `set_error_behaviour(CGAL::ABORT)` | `std::abort()` — kills the Python interpreter |
| your handler itself throws | your exception propagates *instead of* the CGAL one; the `switch` is never reached (**[header]**, `assertions_impl.h:186`) |
| your handler returns normally | the `switch` runs; execution never continues past the macro |

**Rule for the binding**: *the error handler is a formatting/telemetry hook only. It must return
normally, must not throw (an exception from it during unwinding = `std::terminate`), and must
never be used for control flow.* The behaviour must stay `THROW_EXCEPTION`.

```cpp
// arrangement2d core init — call once, from module import
static void a2d_error_handler(const char* what, const char* expr,
                             const char* file, int line, const char* msg) noexcept
{ /* optionally stash into a thread-local for a richer Python message; NEVER throw */ }

void arrangement2d_init_error_handling() {
    CGAL::set_error_behaviour(CGAL::THROW_EXCEPTION);   // default, be explicit
    CGAL::set_warning_behaviour(CGAL::CONTINUE);        // default
    CGAL::set_error_handler(a2d_error_handler);
    CGAL::set_warning_handler(a2d_error_handler);
}
```

---

## 2. Do preconditions fire after the DCEL has been modified?

All probes: `Arrangement_2<Arr_segment_traits_2<Epeck>>`, a 4-segment square
(V=4, E=4, F=2), silent handler, `THROW_EXCEPTION`. Each row catches the
`CGAL::Failure_exception` and then re-reads the counters and `is_valid()`. **[verified]**

| public operation, deliberately violated precondition | throws at | DCEL before the throw | counters after | `is_valid()` |
|---|---|---|---|---|
| `insert_at_vertices(cv, v1, v2)` — two **isolated** vertices in **different faces** | `…impl.h:1050` `Assertion_exception` “The two isolated vertices must be located inside the same face.” | **YES — `v1`’s `Isolated_vertex` record already erased + freed at `:1031-1032`** | `Viso` 2 → **1**, V/E/F unchanged | `YES` (**does not detect it**) |
| `split_edge(e, cv1, cv2)` — `cv1`/`cv2` do not match `e`’s ends | `…impl.h:1585` `_are_equal(source, cv2, ARR_MAX_END)` | no | unchanged | `YES` |
| `merge_edge(e1, e2, cv)` — non-adjacent edges | `…impl.h:1648` “The input edges do not share a common vertex.” | no | unchanged | `YES` |
| `remove_isolated_vertex(v)` — `v` has degree 2 | `…impl.h:1482` `v->is_isolated()` | no | unchanged | `YES` |
| `modify_vertex(v, p)` — `p != v->point()` | `…impl.h:1465` “The new point is different from the current one.” | no | unchanged | `YES` |
| `insert_from_left_vertex(cv, v)` — `v` is not `cv`’s left end | `…impl.h:448` “The input vertex should be the left curve end.” | no | unchanged | `YES` |
| `remove_edge(e, …)` | `…impl.h:1760` (fictitious check) only; `_remove_edge` (`:4267`) has **no** preconditions **[header]** | no | — | — |
| `insert_in_face_interior(cv, f)` — `cv` geometrically outside `f` | **no precondition exists** | — | V 4→6, E 4→5, the hole is attached to the wrong face | `YES` |

### 2.1 The one unsafe mutator, verbatim

`Arrangement_2/Arrangement_on_surface_2_impl.h:1015-1060`:

```cpp
  // Check whether one of the vertices has no incident halfedges.
  if (v1->degree() == 0) {
    // Get the face containing the isolated vertex v1.
    DVertex* p_v1 = _vertex(v1);
    DIso_vertex* iv1 = nullptr;
    DFace* f1 = nullptr;

    if (p_v1->is_isolated()) {
      // Obtain the containing face from the isolated vertex record.
      iv1 = p_v1->isolated_vertex();
      f1 = iv1->face();

      // Remove the isolated vertex v1, as it will not be isolated any more.
      f1->erase_isolated_vertex(iv1);          // <-- :1031  MUTATION
      _dcel().delete_isolated_vertex(iv1);     // <-- :1032  FREE
    }

    // Check whether the other vertex also has no incident halfedges.
    if (v2->degree() == 0) {
      ...
        CGAL_assertion_msg
          ((f1 == nullptr) || (f1 == f2),
           "The two isolated vertices must be located inside the same face.");   // <-- :1048-1050
      ...
      else if (f1 == nullptr)
        // In this case the containing face must be given by the user.
        CGAL_precondition(f != Face_handle());                                   // <-- :1058
```

`Arr_vertex::is_isolated()` reads the **LSB of `p_inc`** (`Arr_dcel_base.h:299-303`), and
`isolated_vertex()` returns `_clean_pointer(p_inc)` (`:336-339`). Freeing `iv1` does not clear
`p_inc`, hence the dangling pointer that `is_valid()` never notices.

**Binding rule.** For `insert_at_vertices`, validate *in your own C++ core before calling CGAL*
that (a) each vertex matches the corresponding curve end and (b) if both vertices are isolated
they live in the same face (`v1->face() == v2->face()`). After any `Failure_exception` out of
`insert_at_vertices`, treat the arrangement as **destroyed**, not merely unchanged.

---

## 3. `CGAL_error()` followed by a null return — the complete reachable list

Enumerated by scanning every `CGAL_error(` / `CGAL_error_msg(` site in `Arr*.h`,
`Arrangement*.h`, `Gps*.h`, `Arrangement_2/`, `Surface_sweep_2/`,
`Boolean_set_operations_2/` for a `return` within 4 lines. **[header]**

| file:line | function | value returned after the error | reachable from |
|---|---|---|---|
| `Arr_unb_planar_topology_traits_2.h:320` | `locate_around_boundary_vertex` | `nullptr` | `_locate_around_vertex` → `insert_at_vertices`, `Arr_accessor::locate_around_vertex` |
| `Arr_bounded_planar_topology_traits_2.h:317` | `place_boundary_vertex` | `std::nullopt` | `_place_and_set_curve_end` (only if a traits reports a non-`ARR_INTERIOR` parameter space under bounded topology) |
| `Arr_bounded_planar_topology_traits_2.h:339` | `locate_around_boundary_vertex` | `nullptr` | same |
| `Arr_bounded_planar_topology_traits_2.h:359` | `locate_curve_end` | `Result(Vertex*(nullptr))` | `Arr_accessor::locate_curve_end`, `insert_non_intersecting_curve` |
| `Arr_bounded_planar_topology_traits_2.h:374` | `split_fictitious_edge` | `nullptr` | `_place_and_set_curve_end` |
| `Arr_bounded_planar_topology_traits_2.h:401` | `erase_redundant_vertex` | `nullptr` | `_remove_edge` |
| `Arr_spherical_topology_traits_2.h:556` | `locate_around_boundary_vertex`-family | `nullptr` | geodesic arrangements |
| `Arr_accessor.h:126` | `locate_curve_end` | `Pl_result::make_result(Vertex_const_handle())` (**singular handle**) | public accessor API |
| `Arrangement_2/Arrangement_on_surface_2_impl.h:1852` | `_halfedge_distance` | `0` | `_insert_at_vertices`, `_remove_edge` |
| `Arrangement_2/Arrangement_on_surface_2_impl.h:1886, 1892` | `_compare_induced_path_length` | `EQUAL` | `_insert_at_vertices` |
| `Arrangement_2/Arrangement_zone_2_impl.h:378` | `_zone_look_around_vertex`-family | `false` | `CGAL::zone`, `CGAL::insert` |
| `Arr_landmarks_point_location.h:361` | landmark walk | `false` | `Arr_landmarks_point_location::locate` |
| `Arr_point_location_result.h:58` | `default_result()` | `Type()` (**singular**) | point-location plumbing |
| `Arr_segment_traits_2.h:682` | `Are_mergeable_2` helper | `false` | segment traits |
| `Arr_conic_traits_2.h:782, 822` | `Parameter_space_in_x/y_2` | `ARR_INTERIOR` | **conic traits, directly** — `CGAL_error_msg("Not implemented yet!")` |
| `Arr_conic_traits_2.h:1642` | approximate length | `0.0` | conic traits |
| `Arr_curve_data_traits_2.h:252, 286, 429` | `Equal_2`, `Are_mergeable_2`, `Construct_opposite_2` | `false` / `false` / `X_monotone_curve_2()` | curve-data traits (`CGAL_error_msg`) |
| `Arr_geodesic_arc_on_sphere_traits_2.h:3049` | `operator>>` | `is` | geodesic traits IO |
| `Arr_enums.h:54, 86, 128` | `operator<<` for the enums | `os` | any printing of a corrupted enum |
| `Arr_tracing_traits_2.h:683` | tracing wrapper | `false` | debug traits |

### 3.1 The practical rule

Because `precondition_fail`/`assertion_fail` end in `throw` for **every** behaviour except
`ABORT`/`EXIT`/`EXIT_WITH_SUCCESS`, and because `CGAL_error()`/`CGAL_error_msg()` cannot be
compiled out, **none of the `return nullptr;` lines above is reachable in a correctly-configured
build.** They exist to silence `-Wreturn-type`.

They become reachable only if the binding does one of the following — so **do not**:

* set `ABORT` / `EXIT` / `EXIT_WITH_SUCCESS` (they never return either, but they take the whole
  interpreter with them);
* install a handler that `longjmp`s (undefined behaviour across C++ frames anyway);
* compile with `NDEBUG` **without** `CGAL_DEBUG`, which strips the `CGAL_precondition`/
  `CGAL_assertion` guards that normally stop execution *earlier* — this is what actually produces
  singular handles and null dereferences, as measured in §3.2.

> **Rule: a custom error handler must always return normally and must never throw; the *throw* is
> CGAL’s, not yours. Never disable the checks.**

### 3.2 What `NDEBUG` really costs — **[verified]**

Same source, two binaries.

| | checks on (default) | `-DNDEBUG` (checks off) |
|---|---|---|
| `CGAL_PRECONDITIONS_ENABLED` / `CGAL_ASSERTIONS_ENABLED` | `1 / 1` | `0 / 0` |
| `merge_edge(e1, e2, cv)` on non-adjacent edges | throws `Precondition_exception`, handler called once | **no throw**, returns `h == Halfedge_handle()` (singular); `h->direction()` → SIGSEGV |
| `remove_isolated_vertex(v)` on a degree-2 vertex | throws `Precondition_exception`, arrangement unchanged and valid | **SIGBUS inside the call** |
| `set_error_behaviour(CONTINUE)` + no-op handler + `precondition_fail(...)` | throws | throws (the entry point is a real function, not a macro) |

Ship: `-O3 -DNDEBUG -DCGAL_DEBUG` (C asserts off, CGAL checks on). `CGAL_DEBUG` `#undef`s
`CGAL_NDEBUG` (`assertions.h:33-38`), so `-UCGAL_NDEBUG` alone does **not** work.

---

## 4. Observers

### 4.1 The notification loops have no exception handling — **[header]**

`Arrangement_on_surface_2.h:2378-2393`, representative of all 60-odd pairs:

```cpp
  void _notify_before_create_edge(const X_monotone_curve_2& c,
                                  Vertex_handle v1, Vertex_handle v2)
  {
    Observers_iterator iter;
    Observers_iterator end = m_observers.end();
    for (iter = m_observers.begin(); iter != end; ++iter)
      (*iter)->before_create_edge(c, v1, v2);
  }

  void _notify_after_create_edge(Halfedge_handle e)
  {
    Observers_rev_iterator iter;
    Observers_rev_iterator end = m_observers.rend();
    for (iter = m_observers.rbegin(); iter != end; ++iter)
      (*iter)->after_create_edge(e);
  }
```

* Container: `typedef std::list<Observer*> Observers_container;`
  (`Arrangement_on_surface_2.h:890`), member `m_observers` (`:900`).
* `before_*` iterates **forward** (registration order); `after_*` iterates **reverse**
  (last-registered first). A throw from observer *k* therefore means: for `before_*`, observers
  1..k-1 were notified and k..N were not; for `after_*`, the *later-registered* ones were
  notified and the earlier ones were not.
* `_register_observer` is `m_observers.push_back(this)` (`:2254`); `_unregister_observer`
  (`:2260-2275`) is a linear search + `erase`. Neither can throw meaningfully.
* Every hook in `Aos_observer` is `virtual void … {}` — **none is `noexcept`**
  (`Aos_observer.h:138-460`). `CGAL::Arr_observer<Arr>` is now just
  `template <typename Arrangement_> using Arr_observer = typename Arrangement_::Observer;`
  (`Arr_observer.h:27-29`), i.e. an alias for `Aos_observer<Arr>`.

### 4.2 Attach / detach — `Aos_observer.h:89-131`

```cpp
  void attach(Arrangement_2& arr)
  {
    if (p_arr == &arr) return;
    CGAL_precondition (p_arr == nullptr);
    if (p_arr != nullptr) return;
    before_attach(arr);            // user code — a throw here leaves the observer UNattached (clean)
    p_arr = &arr;
    p_arr->_register_observer(this);
    after_attach();                // user code — a throw here leaves it attached but the caller sees an error
  }

  void detach()
  {
    if (p_arr == nullptr) return;
    before_detach ();
    p_arr->_unregister_observer(this);
    p_arr = nullptr;
    after_detach();
  }
```

`~Aos_observer()` is `if (p_arr != nullptr) p_arr->_unregister_observer(this);`
(`Aos_observer.h:71-77`), and `~Arrangement_on_surface_2` walks its observer list calling
`(*iter)->detach()` (`Arrangement_2/Arrangement_on_surface_2_impl.h:232-242`).

> **[verified] Throwing from `before_detach()` or `after_detach()` while the arrangement is being
> destroyed calls `std::terminate`** (`libc++abi: terminating due to uncaught exception`).
> A Cython observer’s detach hooks **must be `noexcept`**.

### 4.3 Where the `after_*` hook sits relative to the DCEL surgery

`Arrangement_2/Arrangement_on_surface_2_impl.h:2313-2355` (`_insert_in_face_interior`):

```cpp
  _notify_before_create_edge(cv, Vertex_handle(v1), Vertex_handle(v2));
  DHalfedge* he1 = _dcel().new_edge();
  DHalfedge* he2 = he1->opposite();
  DInner_ccb* ic = _dcel().new_inner_ccb();
  X_monotone_curve_2* dup_cv = _new_curve(cv);
  ic->set_face(f);
  ...
  _notify_after_create_edge(hh);                 // <-- user code runs HERE
  _notify_before_add_inner_ccb(Face_handle(f), hh);
  f->add_inner_ccb(ic, he2);                     // <-- the hole is only registered HERE
  _notify_after_add_inner_ccb(hh->ccb());
```

By contrast, in `_insert_from_vertex` (`:2436`) `_notify_after_create_edge` is the **last**
statement, so a throw there leaves that primitive consistent.

`_insert_at_vertices` fires `_notify_after_split_face` at `:3122`, but the caller
(`insert_at_vertices`, `:1443`, and the five other call sites at `:419, 559, 656, 792, 891`)
performs `_relocate_in_new_face(new_he)` **after** the internal routine returns. A throw from
`after_split_face` therefore skips relocation of holes and isolated vertices into the newly
created face. **[header]** — my probe geometry did not force a relocation, so this one is
header-read only, not measured.

### 4.4 Verified matrix — observer throws

Square (V=4,E=4,F=2). Each cell is the post-catch state. **[verified]**

| hook that throws | operation | counters after | `is_valid()` | destructor | leak |
|---|---|---|---|---|---|
| — (control) | `insert_in_face_interior(cv, unb)` | V=6 E=5 F=2 | `YES` | OK | none |
| `after_create_vertex` | `insert_in_face_interior(cv, unb)` | V=5 E=4 | **`is_valid()` SEGFAULTS** | OK | none |
| `before_create_edge` | `insert_in_face_interior(cv, unb)` | V=6 E=4 | **`is_valid()` SEGFAULTS** | OK | none |
| `after_create_edge` | `insert_in_face_interior(cv, unb)` | V=6 E=5 F=2 | `YES` **(wrong)** — hole not registered: `unb->number_of_holes()` 1 instead of 2, 4 reachable halfedges instead of 6 | OK | none |
| `after_add_inner_ccb` | `insert_in_face_interior(cv, unb)` | V=6 E=5 F=2 | `YES` (genuinely consistent) | OK | none |
| `after_create_edge` | `CGAL::insert(arr, cv)` (single curve → zone) | V=6 E=6 | `YES` | OK | none |
| `after_split_face` | `CGAL::insert(arr, cv)` (single curve → zone) | V=7 E=8 F=3 | `YES` | OK | none |
| `after_create_edge` | `insert_non_intersecting_curve` | V=6 E=5 | `YES` | OK | none |
| `after_remove_edge` | `remove_edge` | V=4 E=3 F=1 | `YES` | OK | none |
| `after_create_edge` (2nd/1st edge) | **aggregate** `CGAL::insert(arr, first, last)` | V=8 E=7 / V=6 E=5, half of the 4 input segments inserted | `YES` | OK | **+26 / +25 blocks** |

Two conclusions:

* **`is_valid()` segfaults after a throw from `before_create_edge` or `after_create_vertex`**
  (a vertex exists with `p_inc == nullptr` and not marked isolated). Your C++ core must never call
  `is_valid()` on the recovery path.
* **The arrangement is always destructible and never leaks** after an observer throw during a
  *single-primitive* operation. It leaks only when the throw escapes a **sweep** (§6).

---

## 5. `overlay()` and `Indexed_sweep_accessor` — the highest-risk case

### 5.1 The squat, verbatim — `Arr_overlay_2.h:42-120`

```cpp
template <typename Arr1, typename Arr2, typename Curve>
class Indexed_sweep_accessor
{
  const Arr1& arr1;
  const Arr2& arr2;
  mutable std::vector<void*> backup_inc;
public:
  Indexed_sweep_accessor (const Arr1& arr1, const Arr2& arr2)
    : arr1(arr1), arr2(arr2) { }

  std::size_t nb_vertices() const
  { return arr1.number_of_vertices() + arr2.number_of_vertices(); }

  std::size_t min_end_index (const Curve& c) const;   // reinterpret_cast<std::size_t>(…->inc())
  std::size_t max_end_index (const Curve& c) const;
  const Curve& curve (const Curve& c) const { return c; }

  // Initializes indices by squatting Vertex::inc();
  void before_init() const
  {
    std::size_t idx = 0;
    backup_inc.resize (nb_vertices());
    for (typename Arr1::Vertex_const_iterator vit = arr1.vertices_begin();
         vit != arr1.vertices_end(); ++vit, ++idx)
    {
      CGAL_assertion (idx < backup_inc.size());
      backup_inc[idx] = vit->inc();
      vit->set_inc (reinterpret_cast<void*>(idx));
    }
    for (typename Arr2::Vertex_const_iterator vit = arr2.vertices_begin();
         vit != arr2.vertices_end(); ++vit, ++idx)
    { … same … }
  }

  // Restores state of arrangements before index squatting
  void after_init() const
  { … writes backup_inc[idx] back into every vertex … }
};
```

`set_inc` is a **`const` member that `const_cast`s**:

```cpp
  void* inc() const { return p_inc; }
  void set_inc(void * inc) const
  { const_cast<Arr_vertex_base&>(*this).p_inc = inc; }
```
(`Arr_dcel_base.h:95-97`) — and `p_inc` is the *same* field that stores
`halfedge()` / `isolated_vertex()` with the LSB used as the “is isolated” flag
(`Arr_dcel_base.h:299-345`).

### 5.2 The unprotected window — `No_intersection_surface_sweep_2.h:333-345`

```cpp
  void indexed_sweep (const EdgeRange& edges,
                      const Accessor& accessor)
  {
    m_visitor->before_sweep();
    accessor.before_init();
    _init_indexed_sweep(edges, accessor);      // <-- ANY throw here loses the restore
    accessor.after_init();
    _sweep();
    _complete_sweep();
    m_visitor->after_sweep();
  }
```

(and the 4-argument overload at `:371-384`, used when either input has isolated vertices).
`_init_indexed_sweep` → `_init_structures()` + `_init_indexed_curves(edges, accessor)`
(`:492-500` / `:455-478`), which constructs `Subcurve`s, calls
`accessor.min_end_index/max_end_index` (each containing a `CGAL_assertion`), and calls
`_init_curve_end`, which uses the traits’ `Compare_xy_2`, `Parameter_space_in_x/y_2` and the
event-queue comparator, and allocates events.

There is **no `try`/`catch` and no RAII guard** anywhere in that path.

### 5.3 Verified behaviour

Two 4×4 squares, `overlay(a1, a2, res, ovl)`. **[verified]**

| what throws | `a1` / `a2` `inc()` after the catch | `a1.is_valid()` | `res` | leak |
|---|---|---|---|---|
| — (control) | real pointers | `YES` | V=10 E=12 F=4, valid | none |
| overlay traits `create_face` | real pointers (**restored**) | `YES` | V=7 E=7 F=2, `is_valid()==YES` but a *partial* overlay | **+21** |
| overlay traits `create_vertex` | real pointers (**restored**) | `YES` | V=2 E=1 F=1 | **+17** |
| overlay traits `create_edge` | real pointers (**restored**) | `YES` | V=2 E=1 F=1 | **+17** |
| geometry-traits `Compare_xy_2` during `_init_indexed_curves` (probe throws on the 1st/2nd/3rd/4th/7th/17th comparison — all land in init) | `a1` = `0 1 2 3`, `a2` = `4 5 6 7` — **still squatted** | **SEGFAULT** | empty | (crash before measuring) |

Additional readings in the squatted state: `a1.vertices_begin()->is_isolated()` returned `0`
(LSB of index 0) and `->degree()` returned `0` (a null “incident halfedge”). For an odd index the
LSB is 1, so `is_isolated()` returns `true` and `isolated_vertex()` returns a near-null pointer.

### 5.4 What this means for the binding

* Overlay **`create_*` callbacks may propagate.** They are called from `_sweep()` /
  `after_sweep()` (`Arr_overlay_ss_visitor.h:572, 785, 814, 850, 886, 1038, 1052, 1080, 1088,
  1098`), always after `after_init()`. The inputs survive. The result must be `clear()`ed.
* **Nothing that can throw may be reachable from a geometry-traits functor during `overlay`.**
  In particular: do not put a Python callback in a traits class; do not poll
  `PyErr_CheckSignals()` from a traits functor; do not use a lazy number type whose refinement can
  throw. If you must support interruption, check the flag in the *overlay traits* callbacks
  (safe), never in the traits.
* If you cannot rule it out, wrap `overlay` so that on **any** exception you mark **both input
  arrangement wrappers poisoned** at the Python level and refuse all further use of them, because
  you cannot tell from outside whether the throw happened before or after `after_init()`, and
  probing (e.g. calling `is_valid()`) will crash the process.
* A cheap detector, if you want one: after catching, read
  `reinterpret_cast<std::size_t>(v->inc())` for the first vertex of each input; a value smaller
  than `nb_vertices()` means the arrangement is squatted. This is a heuristic (a real pointer is
  never that small on macOS/Linux) but it is enough to raise a hard Python error instead of
  crashing.

---

## 6. Every sweep-based algorithm leaks when an exception escapes

### 6.1 Why — **[header]**

`Surface_sweep_2/No_intersection_surface_sweep_2_impl.h:188-215`:

```cpp
template <typename Vis>
void No_intersection_surface_sweep_2<Vis>::_init_structures()
{
  ...
    m_subCurves = m_subCurveAlloc.allocate(m_num_of_subCurves);      // :196
  ...
}

template <typename Vis>
void No_intersection_surface_sweep_2<Vis>::_complete_sweep()          // :203
{
  CGAL_assertion(m_queue->empty());
  CGAL_assertion((m_statusLine.size() == 0));

  // Free all subcurve objects.
  for (unsigned int i = 0; i < m_num_of_subCurves; ++i){
    std::allocator_traits<Subcurve_alloc>::destroy(m_subCurveAlloc, m_subCurves + i);
  }

  if (m_num_of_subCurves > 0)
    m_subCurveAlloc.deallocate(m_subCurves, m_num_of_subCurves);
}
```

and the destructor (`:76-84`):

```cpp
template <typename Vis>
No_intersection_surface_sweep_2<Vis>::~No_intersection_surface_sweep_2()
{
  // Free the traits-class object, if we own it.
  if (m_traitsOwner) delete m_traits;

  // Free the event queue.
  delete m_queue;
}
```

`_complete_sweep()` is reached only on the success path (`:250, 277, 311, 341, 382`). The
destructor never frees `m_subCurves`, and never runs the `Subcurve` destructors (each `Subcurve`
owns further heap state). Events are safe: `m_allocated_events` is a
`Compact_container<Event>` **by value** (`No_intersection_surface_sweep_2.h:176, 196`), so its
own destructor releases them.

`Arr_construction_ss_visitor::after_sweep()` (`Arr_construction_ss_visitor.h:299-302`) calls
`m_arr->clean_inner_ccbs_after_sweep()` (`Arrangement_on_surface_2.h:1503`), which collapses the
inner-CCB redirection chains. That cleanup is also skipped on the throwing path, so the
constructed arrangement keeps invalid/redirected `Inner_ccb` records — `inner_ccb()` still
resolves through the redirect, which is why `is_valid()` does not complain.

### 6.2 Measured leaks — **[verified]** (net blocks above the control run)

| algorithm | what threw | net leaked blocks |
|---|---|---|
| aggregate `CGAL::insert(arr, first, last)` | observer `after_create_edge` | **+25 / +26** |
| `CGAL::overlay(a1, a2, res, ovl)` | overlay `create_face` / `create_vertex` / `create_edge` | **+21 / +17 / +17** |
| `CGAL::decompose(arr, oi)` | output-iterator `operator=` | **+17** |
| `CGAL::locate(arr, first, last, oi)` (batched) | output-iterator `operator=` | **+9** |
| `Polygon_set_2::intersection(other)` | traits during the sweep proper | **+31** |
| `CGAL::zone(arr, cv, oi)` | output-iterator `operator=` | **none** |
| `Traits::Make_x_monotone_2` | output-iterator `operator=` | **none** |
| single-curve `CGAL::insert` / `insert_non_intersecting_curve` / `remove_edge` | observer | **none** |

These are bounded per-call leaks (tens of blocks per failed sweep), not unbounded ones — but in a
Python loop that retries a failing operation they accumulate. Document them; there is no way to
plug them from outside without patching CGAL.

---

## 7. User output iterators

Probe: a hand-written output iterator whose templated `operator=` throws after *N* writes.

```cpp
struct Throwing_oi {
  int* cnt; int limit;
  typedef std::output_iterator_tag iterator_category;
  typedef void value_type; typedef void difference_type;
  typedef void pointer;    typedef void reference;
  Throwing_oi& operator*()     { return *this; }
  Throwing_oi& operator++()    { return *this; }
  Throwing_oi& operator++(int) { return *this; }
  template <class T> Throwing_oi& operator=(const T&)
  { ++(*cnt); if (limit >= 0 && *cnt > limit) throw Boom("output iterator"); return *this; }
};
```

**[verified]**

| call | signature (verbatim) | exception escapes? | arrangement afterwards | leak |
|---|---|---|---|---|
| `CGAL::zone(arr, cv, oi)` | `template <typename GeomTraits, typename TopTraits, typename OutputIterator> OutputIterator zone(Arrangement_on_surface_2<GeomTraits, TopTraits>& arr, const typename GeomTraits::X_monotone_curve_2& c, OutputIterator oi);` (`Arrangement_on_surface_2.h:2957-2963`, impl `Arrangement_2/Arrangement_on_surface_2_global.h:1472`) | yes, cleanly (2 of 5 items delivered) | **unchanged**, `is_valid()==YES` | none |
| `CGAL::decompose(arr, oi)` | `template <typename GeometryTraits_2, typename TopologyTraits, typename OutputIterator> OutputIterator decompose(const Arrangement_on_surface_2<GeometryTraits_2, TopologyTraits>& arr, OutputIterator oi);` (`Arr_vertical_decomposition_2.h:44-48`) | yes (3 of 8) | **unchanged**, `is_valid()==YES` | **+17** |
| `CGAL::locate(arr, pts_begin, pts_end, oi)` | `template <typename GeometryTraits_2, typename TopologyTraits, typename PointsIterator, typename OutputIterator> OutputIterator locate(const Arrangement_on_surface_2<GeometryTraits_2, TopologyTraits>& arr, PointsIterator points_begin, PointsIterator points_end, OutputIterator oi);` (`Arr_batched_point_location.h:48-53`) | yes (3 of 4) | **unchanged**, `is_valid()==YES` | **+9** |
| `traits.make_x_monotone_2_object()(cv, oi)` | `template <typename OutputIterator> OutputIterator operator()(const Curve_2& cv, OutputIterator oi) const` | yes (1 of 1) | n/a | none |

`decompose` and batched `locate` are `const` on the arrangement and drive their own sweeps, so
they cannot corrupt the arrangement — they only leak the sweep’s subcurve array. `zone` mutates
`arr` in the `insert` path but the plain `CGAL::zone` overload used here reports only; the
arrangement was byte-identical afterwards.

> **Output iterators are the safest place to let a Python exception through.** Nothing is
> corrupted; at worst a bounded amount of memory is leaked.

---

## 8. `Gps_on_surface_base_2` binary operations

### 8.1 The pattern, verbatim — `Boolean_set_operations_2/Gps_on_surface_base_2.h`

```cpp
  void _intersection(const Aos_2& arr)                       // :1481
  {
    Aos_2* res_arr = new Aos_2(m_traits);
    Gps_intersection_functor<Aos_2> func;
    overlay(*m_arr, arr, *res_arr, func);
    delete m_arr; // delete the previous arrangement

    m_arr = res_arr;
    remove_redundant_edges();
    //fix_curves_direction(); // not needed for intersection
    CGAL_assertion(is_valid());
  }
```

`_join` (`:1542`), `_difference` (`:1618`) and `_symmetric_difference` (`:1685`) are
character-for-character the same shape (`_difference`/`_symmetric_difference` additionally call
`fix_curves_direction()`). The three-argument forms
(`_intersection(const Aos_2& arr1, const Aos_2& arr2, Aos_2& res)`, `:1494`, etc.) write into a
caller-supplied `res` and call `_remove_redundant_edges(&res)`.

The `Polygon_`-taking wrappers first build a temporary `Aos_2 second_arr;` with `_insert(pgn, …)`
and then call the `Aos_2` form (`:1504-1522`). The `Self`-taking wrappers short-circuit on
`is_empty()` / `is_plane()`; note `_intersection(const Self& other)` contains
`*(this->m_arr) = *(other.m_arr);` — an arrangement `operator=`, which itself runs
`_notify_before_assign` / `clear()` / `_notify_after_assign`
(`Arrangement_2/Arrangement_on_surface_2_impl.h:145-206`).

### 8.2 Failure windows

| where the throw lands | `m_arr` | set’s *value* | `res_arr` | `is_valid()` |
|---|---|---|---|---|
| inside `overlay`, during sweep **init** | old pointer, **`inc()` squatted** | reads back as the old value **[verified]** | **leaked** | **SEGFAULT [verified]** |
| inside `overlay`, during `_sweep()` / `create_*` | old pointer, intact | old value, fully usable **[verified]** | **leaked** (+31 blocks **[verified]**) | `1` **[verified]** |
| inside `remove_redundant_edges()` / `fix_curves_direction()` | **new** arrangement, old one already `delete`d | partially normalised, unspecified | — | unspecified |
| `CGAL_assertion(is_valid())` fails | **new** arrangement, old one already `delete`d | invalid | — | `false` — and it throws `Assertion_exception` |

**[verified] measurements** (two overlapping 10×10 squares, `a.intersection(b)`, a
`Gps_segment_traits_2`-derived traits whose `Compare_xy_2` throws on the *k*-th call after
arming):

```
k=1,3,8  (init)   caught; a.number_of_polygons_with_holes()==1 (old value); a.is_valid() -> SIGSEGV
k=30     (sweep)  caught; a.number_of_polygons_with_holes()==1; a.is_valid()==1; 31 blocks leaked
k=-1,80  (control) no exception; no leak
```

### 8.3 Binding rule for Boolean set operations

* On **any** exception out of `join`/`intersection`/`difference`/`symmetric_difference`, mark the
  receiving `Polygon_set_2` wrapper **poisoned**: do not call `is_valid()`, `polygons_with_holes()`
  or any further Boolean op on it, and raise a hard Python error telling the user to rebuild the
  set. The value *looks* correct in the common case, but you cannot distinguish “usable” from
  “squatted” without the `inc()` heuristic of §5.4.
* Prefer the **three-argument** forms (`gps.intersection(gps1, gps2)`), which write into `*this`
  and take `gps1`/`gps2` as `const` inputs to `overlay` — the failure then damages `gps1`/`gps2`
  in the init window instead of the receiver, but at least the receiver’s old value is never
  deleted.
* Never let a Python callback run inside a Gps geometry traits.

---

## 9. `std::terminate`, `noexcept`, `-fno-exceptions`

* **`noexcept` count in `Surface_sweep_2/*.h`, `No_intersection_surface_sweep_2.h`,
  `Surface_sweep_2.h`, `Arrangement_on_surface_2.h`, `Arrangement_2/*.h`, `Arr_dcel_base.h`,
  `Aos_observer.h`: zero.** **[verified by grep]** No sweep or arrangement function is `noexcept`,
  so no exception crossing them triggers `std::terminate` by that route.
* **The only `std::terminate` route is a destructor.** Destructors are implicitly `noexcept` in
  C++11 and later. **[verified]** an observer whose `before_detach()`/`after_detach()` throws
  terminates the process when the arrangement is destroyed, because
  `~Arrangement_on_surface_2` detaches its observers.
* CGAL is aware of the destructor problem for its own checks:
  `CGAL_destructor_assertion(EX)` is suppressed when `std::uncaught_exceptions() > 0`, and
  `CGAL_destructor_assertion_catch(CODE)` is `try{ CODE } catch(...) { if(std::uncaught_exceptions() <= 0) throw; }`
  (`assertions.h:113-121`). It provides no such protection for *your* callbacks.
* **`-fno-exceptions` cannot compile CGAL 6.1.** **[verified]**
  ```
  /opt/homebrew/include/CGAL/Uncertain.h:114:5: error: cannot use 'throw' with exceptions disabled
  /opt/homebrew/include/CGAL/exceptions.h:182:45: error: cannot use 'throw' with exceptions disabled
  /opt/homebrew/include/CGAL/assertions_impl.h:173:9: error: cannot use 'throw' with exceptions disabled
  /opt/homebrew/include/CGAL/assertions_impl.h:195:9: error: cannot use 'throw' with exceptions disabled
  /opt/homebrew/include/CGAL/assertions_impl.h:217:9: error: cannot use 'throw' with exceptions disabled
  /opt/homebrew/include/CGAL/assertions_impl.h:240:9: error: cannot use 'throw' with exceptions disabled
  /opt/homebrew/include/CGAL/Interval_nt.h:1570:18: error: cannot use 'throw' with exceptions disabled
  /opt/homebrew/include/CGAL/Object.h:169:5: error: cannot use 'throw' with exceptions disabled
  ```
  There is no `CGAL_NO_EXCEPTIONS` / `__EXCEPTIONS` guard anywhere in the installed tree
  **[verified by grep]**. Every TU that includes CGAL must be built with exceptions enabled.

---

## 10. Recommended policy for the C++ core

### 10.1 Classification of every user-supplied callback

| callback | may it propagate a C++ exception? | why |
|---|---|---|
| output iterator `operator=` for `zone`, `decompose`, batched `locate`, `Make_x_monotone_2`, `Intersect_2` sinks | **YES** (with a documented per-call leak for the sweep-based ones) | nothing is corrupted; §7 |
| overlay traits `create_vertex` / `create_edge` / `create_face` | **YES**, but the caller must `clear()` the result arrangement and accept a leak | inputs are already restored; §5.3 |
| observer `after_*` hooks called from a *single-primitive* mutator (`insert_*`, `split_edge`, `remove_edge`, `merge_edge`) | **NO — buffer** | leaves a structurally broken arrangement that `is_valid()` cannot detect; §4.4 |
| observer `before_*` hooks | **NO — buffer** | worse: `is_valid()` segfaults; §4.4 |
| observer hooks called from inside a sweep (aggregate `insert`, `overlay`, construction visitor) | **NO — buffer** | corrupt arrangement **and** leak; §6 |
| observer `before_attach` / `after_attach` | may propagate (state is clean either way) | §4.2 |
| **observer `before_detach` / `after_detach`** | **NEVER — must be `noexcept`** | `std::terminate` from `~Arrangement_2`; §4.2 |
| any geometry-traits / topology-traits functor (`Compare_xy_2`, `Intersect_2`, `Parameter_space_*`, `Equal_2`, …) | **NEVER — must be `noexcept`** | corrupts `const` inputs of `overlay` beyond repair; §5 |
| CGAL error/warning handler | **NEVER — must be `noexcept`** | replaces the CGAL exception; terminates if unwinding; §1.2 |

### 10.2 The buffering pattern for a Cython observer

Every observer hook is `noexcept`, catches everything, records the first Python error in a
thread-local slot, and returns. The *entry point* that started the operation re-raises it.

```cpp
// a2d_error_slot.hpp
struct A2d_pending {
    bool             set = false;
    std::string      kind;     // "observer.after_create_edge", …
    std::string      what;     // Python repr, already formatted by the Cython layer
};
A2d_pending& a2d_pending();    // thread_local

// the C++ side of a Cython observer
class Py_observer final : public CGAL::Arr_observer<Arr> {
public:
  using CGAL::Arr_observer<Arr>::Arr_observer;

  void after_create_edge(Halfedge_handle e) noexcept override {
      if (a2d_pending().set) return;            // already failed: stay out of the way
      try { call_python_after_create_edge(e); } // Cython `except *` -> C++ exception
      catch (const std::exception& ex) {
          a2d_pending() = {true, "observer.after_create_edge", ex.what()};
      } catch (...) {
          a2d_pending() = {true, "observer.after_create_edge", "unknown C++ exception"};
      }
  }
  // …one such wrapper per hook…

  // MANDATORY: these two must never throw, they run from ~Arrangement_2
  void before_detach() noexcept override { swallow([&]{ call_python_before_detach(); }); }
  void after_detach()  noexcept override { swallow([&]{ call_python_after_detach();  }); }
};
```

and at the boundary:

```cpp
template <class F>
auto a2d_guard(F&& f) -> decltype(f()) {
    a2d_pending() = {};                 // clear
    try {
        auto r = f();
        if (a2d_pending().set) throw A2d_callback_error(a2d_pending());  // deferred re-raise
        return r;
    }
    catch (const CGAL::Failure_exception& e) { throw A2d_cgal_error(e); } // library(), expression(),
                                                                         // filename(), line_number(),
                                                                         // message()
    catch (const A2d_callback_error&)        { throw; }
    catch (...)                              { throw; }
}
```

Notes on the deferred re-raise:
* Because the hook returned normally, CGAL **completes** the operation and the arrangement stays
  consistent. You lose “the operation did not happen”, but you keep “the arrangement is usable”.
  That is the right trade: §4.4 shows the alternative is an undetectably broken DCEL.
* If the Python observer wants to *veto* an operation, it must do so in a `before_*` hook by
  setting a flag that your own pre-validation checks *before* calling into CGAL — not by throwing.
* Reset the slot at every entry point, and check it once at the exit; do not check it inside the
  hooks other than as the cheap “already failed” short-circuit.

### 10.3 Poisoning

Maintain a `poisoned` flag on the Python wrapper of every arrangement / polygon set. Set it when:

* any exception escapes `overlay()` — poison **both** inputs and the result;
* any exception escapes a `Gps` binary operation — poison the receiver (and the operands for the
  three-argument forms);
* a `Failure_exception` escapes `insert_at_vertices` (see §2.1);
* a `Failure_exception` or callback error escapes an aggregate `insert` / construction sweep —
  poison the target arrangement.

A poisoned object must refuse every subsequent operation with a clear Python exception, and must
**not** call `is_valid()`, `number_of_*`, `vertices_begin()` or its destructor’s traversals on the
CGAL object if the `inc()` heuristic says it is squatted (in that case, deliberately leak the
arrangement rather than destroy it — destruction walks `p_inc` and will crash).

### 10.4 Build flags

```
-O3 -DNDEBUG -DCGAL_DEBUG -DCGAL_USE_CORE -DCGAL_USE_GMP -DCGAL_USE_MPFR
```
* `-DNDEBUG` turns off C `assert`; `-DCGAL_DEBUG` `#undef`s `CGAL_NDEBUG` so all four
  `CGAL_NO_{ASSERTIONS,PRECONDITIONS,POSTCONDITIONS,WARNINGS}` stay undefined (`assertions.h:24-45`).
* Never define `CGAL_CHECK_EXPENSIVE` or `CGAL_CHECK_EXACTNESS` in a shipped build.
* Exceptions must be enabled (§9).
* Optionally `-DCGAL_ENABLE_DISABLE_ASSERTIONS_AT_RUNTIME` to expose
  `CGAL::set_use_assertions(bool)` as a “strict mode” switch to Python (`assertions.h:45-66`);
  it is thread-local.

---

## 11. Quick reference

| question | answer |
|---|---|
| Does a `CGAL_precondition` ever fire after the DCEL was already modified? | **Yes — `insert_at_vertices(cv, v1, v2)` only.** `split_edge`, `remove_edge`, `merge_edge`, `modify_vertex`, `modify_edge`, `remove_isolated_vertex`, `insert_from_left/right_vertex` all validate first. |
| Is `is_valid()` a reliable post-exception check? | **No.** It returns `YES` on a dangling isolated-vertex pointer, on an unregistered inner CCB, and on a geometrically wrong insertion; and it **segfaults** on a half-created vertex or a squatted `inc()`. |
| Can a custom error handler make execution continue past `CGAL_error()`? | **No.** `assertion_fail`/`precondition_fail` always `throw` unless the behaviour is `ABORT`/`EXIT`. |
| What actually produces the “null pointer / singular handle” failure mode? | `NDEBUG` without `CGAL_DEBUG`, which strips the guards. Verified with `merge_edge` and `remove_isolated_vertex`. |
| Does an observer throw leak memory? | Not for single-primitive operations; **yes** for anything sweep-based (+25/26 blocks measured for aggregate `insert`). |
| Does an overlay `create_*` throw corrupt the inputs? | **No** — `after_init()` has already restored `inc()`. Only the result arrangement is half-built, plus a sweep leak. |
| Does `Indexed_sweep_accessor` restore `inc()` while unwinding? | **No.** A throw during sweep *initialisation* leaves both `const` inputs permanently squatted (verified: `inc()` = 0,1,2,3 / 4,5,6,7; `is_valid()` segfaults). |
| State of a `Polygon_set_2` if `overlay` throws mid-op? | Value = old value (the `delete m_arr` has not run); `res_arr` leaked; and if the throw was in the init window, the set’s internal arrangement is squatted and unusable. |
| `-fno-exceptions`? | Does not compile CGAL 6.1 at all. |
| Any `noexcept` in the sweep that could `std::terminate`? | None. The only `std::terminate` route is throwing from a destructor — in practice, an observer’s `before_detach`/`after_detach` during `~Arrangement_2` (verified). |
| Which callbacks must be `noexcept`? | Every traits functor, the CGAL error/warning handler, `before_detach`/`after_detach`, and (by policy, to keep the DCEL usable) every observer hook. |
| Which callbacks may propagate? | Output iterators (`zone`, `decompose`, batched `locate`, `Make_x_monotone_2`) and overlay `create_*` — with a bounded leak and a mandatory `clear()` of the result. |
