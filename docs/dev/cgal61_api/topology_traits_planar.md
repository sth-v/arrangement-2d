# CGAL 6.1 — the two **planar** topology-traits classes (`Arr_bounded_planar_topology_traits_2`, `Arr_unb_planar_topology_traits_2`)

Source of truth: the installed headers under `/opt/homebrew/include/CGAL` (CGAL 6.1, header-only,
`$URL: .../cgal/blob/v6.1/...`, `$Id: ... b26b07a1242 $`). Everything below is quoted from / verified against:

* `/opt/homebrew/include/CGAL/Arr_bounded_planar_topology_traits_2.h` (474 lines)
* `/opt/homebrew/include/CGAL/Arr_unb_planar_topology_traits_2.h` (501 lines)
* `/opt/homebrew/include/CGAL/Arr_topology_traits/Arr_planar_topology_traits_base_2.h` (378 lines)
* `/opt/homebrew/include/CGAL/Arr_topology_traits/Arr_bounded_planar_topology_traits_2_impl.h` (78 lines)
* `/opt/homebrew/include/CGAL/Arr_topology_traits/Arr_unb_planar_topology_traits_2_impl.h` (897 lines)
* the ten sweep helpers `Arr_{bounded,unb}_planar_{batched_pl,construction,insertion,overlay,vert_decomp}_helper.h`
  and `Arr_topology_traits/Arr_inc_insertion_zone_visitor.h` (429 lines)
* `/opt/homebrew/include/CGAL/Arrangement_2/Arr_default_planar_topology.h`,
  `/opt/homebrew/include/CGAL/Arrangement_on_surface_2.h`, `/opt/homebrew/include/CGAL/Arrangement_2.h`,
  `/opt/homebrew/include/CGAL/Arr_accessor.h`, `/opt/homebrew/include/CGAL/Arr_dcel_base.h`,
  `/opt/homebrew/include/CGAL/Arr_enums.h`, `/opt/homebrew/include/CGAL/enum.h`,
  `/opt/homebrew/include/CGAL/draw_arrangement_2.h`, `/opt/homebrew/include/CGAL/Arr_linear_traits_2.h`

Runtime/compile facts marked **[verified]** were produced by compiling and running small programs with
`/usr/bin/clang++ -std=c++17 -O0 -DCGAL_USE_CORE -DCGAL_USE_GMP -DCGAL_USE_MPFR -I/opt/homebrew/include -L/opt/homebrew/lib -lgmp -lmpfr`
(kernel: `CGAL::Exact_predicates_exact_constructions_kernel` = `Epeck`; CGAL assertions **on**, i.e. no `-DNDEBUG`).

This file fills the gap left by `traits_geodesic_sphere.md` §6 (which maps only the *spherical* topology
traits). Every non-spherical arrangement in this project — segment, polyline, circle-segment, conic,
Bezier, **linear (lines/rays)** — is instantiated with one of the two classes documented here.

---

## Gotchas / surprises vs. older CGAL (and vs. the spherical topology traits)

1. **You never name these classes; `Arrangement_2` picks one for you from the traits' four side
   categories.** `CGAL::Arrangement_2<GeomTraits, Dcel>` derives from
   `Arrangement_on_surface_2<GeomTraits, typename Default_planar_topology<GeomTraits, Dcel>::Traits>`;
   the dispatch is *all four sides oblivious → bounded*, *anything else → unbounded*. So
   `Arr_segment_traits_2`, `Arr_polyline_traits_2<segment>`, `Arr_circle_segment_traits_2`,
   `Arr_conic_traits_2`, `Arr_Bezier_curve_traits_2` all get
   `Arr_bounded_planar_topology_traits_2`, and `Arr_linear_traits_2`,
   `Arr_rational_function_traits_2`, `Arr_algebraic_segment_traits_2` get
   `Arr_unb_planar_topology_traits_2` **[verified]**. Two arrangements over "the same kind of thing"
   therefore have **different `Topology_traits` types and different member sets** — this is the single
   biggest constraint on a type-erased core (see §10).
2. **The two classes are not interface-compatible.** The bounded one has
   `unbounded_face()`; it has **no** `fictitious_face()`, no `bottom_left_vertex()` / `top_left_vertex()` /
   `bottom_right_vertex()` / `top_right_vertex()`. The unbounded one has `fictitious_face()` and the four
   corner accessors; it has **no** `unbounded_face()`. All three are hard compile errors
   (`no member named 'bottom_left_vertex' in 'CGAL::Arr_bounded_planar_topology_traits_2<...>'`,
   `no member named 'unbounded_face' in 'CGAL::Arr_unb_planar_topology_traits_2<...>'`) **[verified]**.
   Only `initial_face()` and `reference_face()` are common — and they mean *different things* (§7.3).
3. **`arr.fictitious_face()` is not fictitious under the bounded topology** (confirms
   `arrangement_core.md` gotcha 6). For `Arr_segment_traits_2<Epeck>`:
   `unbounded_face() == fictitious_face()` is `true` and `fictitious_face()->is_fictitious()` is `false`
   **[verified]** — because `Arrangement_on_surface_2::fictitious_face()` is literally
   `topology_traits()->initial_face()`, and the bounded traits' `initial_face()` returns `unb_face`.
   Under the unbounded topology the two are genuinely different faces and `is_fictitious()` is `true`
   **[verified]**.
4. **Under the unbounded topology `Arrangement_2::unbounded_face()` is *an arbitrary* unbounded face,
   and it moves.** It is implemented as "take the first inner-CCB halfedge of the fictitious face and
   return the face on the other side". With `Arr_linear_traits_2` and 2 crossing lines it returned the
   *second-quadrant* face while `reference_face()` returned the *first-quadrant* face **[verified]**;
   with 3 lines it moved again. There is no "the" unbounded face — use
   `arr.unbounded_faces_begin() / unbounded_faces_end()` (or `number_of_unbounded_faces()`), never
   `unbounded_face()`, on an unbounded arrangement.
5. **A vertex at infinity may be `ARR_LEFT_BOUNDARY` in x *and* `ARR_TOP_BOUNDARY` in y at the same
   time.** For a line of negative slope, `Arr_linear_traits_2`'s `Parameter_space_in_y_2` reports
   `ARR_TOP_BOUNDARY` at the min end (`left_infinite_in_y()` returns
   `has_pos_slope ? ARR_BOTTOM_BOUNDARY : ARR_TOP_BOUNDARY`). The resulting DCEL vertex has
   `parameter_space_in_x() == ARR_LEFT_BOUNDARY` **and** `parameter_space_in_y() == ARR_TOP_BOUNDARY`
   **[verified]**, and it is placed on the **left** edge of the fictitious rectangle (x wins: the
   construction helper switches on `ps_x` first). Do **not** write `if (ps_x == ARR_INTERIOR) …` /
   `else` logic that assumes exclusivity.
6. **The fictitious rectangle is stored "inside-out": the four fictitious edges form the single *inner*
   CCB (hole) of the fictitious face, and their twins form the *outer* CCB of the real unbounded face.**
   `fict_face->number_of_outer_ccbs() == 0` and `number_of_inner_ccbs() == 1` in every state
   **[verified]**. So `Face::outer_ccb()` (the `Arrangement_2`-compatibility accessor, which has
   `CGAL_precondition(number_of_outer_ccbs() == 1)`) would fire on the fictitious face.
7. **`direction()` on a fictitious halfedge is bookkeeping, not geometry.** The "vertical" left edge of
   the rectangle (from `v_bl` up to `v_tl`) is `ARR_LEFT_TO_RIGHT`; the right edge (`v_tr` down to
   `v_br`) is `ARR_RIGHT_TO_LEFT` **[verified]**. The rule is *lexicographic source→target order*, not
   x-order. (`ARR_LEFT_TO_RIGHT == -1`, `ARR_RIGHT_TO_LEFT == 1` in `Arr_enums.h`.)
8. **The whole fictitious layer is invisible through the ordinary arrangement iterators.**
   `arr.vertices_begin()/end()` filters on `is_concrete_vertex` (so **no** vertices at infinity),
   `arr.halfedges_begin()/end()` filters on `is_valid_halfedge` (so **no** fictitious halfedges),
   `arr.faces_begin()/end()` filters on `is_valid_face` (so **no** fictitious face). Verified counts:
   an unbounded arrangement of one line reports `number_of_vertices()==0`, `number_of_edges()==1`,
   `number_of_faces()==2`, while the DCEL holds 6 vertices / 14 halfedges / 3 faces **[verified]**.
   Fictitious features are reachable *only* by walking a CCB (or via `topology_traits()->dcel()`).
9. **Touching a fictitious feature's geometry aborts.** `Halfedge::curve()` on a fictitious halfedge
   raises `CGAL ERROR: precondition violation! Expression : p_cv != nullptr` at
   `Arr_dcel_base.h:192`; `Vertex::point()` on an open-boundary vertex raises
   `CGAL ERROR: assertion violation! Expression : p_pt != nullptr` at `Arr_dcel_base.h:105`
   **[verified]**. Guard with `he->is_fictitious()` and `v->is_at_open_boundary()` — those are the two
   public predicates on the arrangement wrappers.
10. **`CGAL::draw(arr)` is unusable on an unbounded arrangement.** `draw_arrangement_2.h`'s
    `find_smallest()` calls `curr->source()->point()` and `draw_region_impl1()` calls
    `approx(curr->curve(), …)` with **no** `is_fictitious()` guard anywhere in the file (grep finds zero
    occurrences of `is_fictitious`), so it will hit gotcha 9. You must write your own walk (§8).
11. **`std::variant` / `std::optional`, not `CGAL::Object`.** `place_boundary_vertex` returns
    `std::optional<std::variant<Vertex*, Halfedge*>>`; `locate_curve_end` returns
    `std::variant<Vertex*, Halfedge*, Face*>`. (In CGAL ≤ 5.x these were `CGAL::Object`.) The bounded
    variants return `std::nullopt` / a null `Vertex*` **after calling `CGAL_error()`** — they are
    unreachable stubs, not usable API (gotcha 12).
12. **Half of the "topology-traits methods" on the bounded class are `CGAL_error()` stubs.**
    `place_boundary_vertex`, `locate_around_boundary_vertex`, `locate_curve_end`,
    `split_fictitious_edge`, `erase_redundant_vertex` all call `CGAL_error()` — which *throws/aborts*,
    it is not compiled out in a release build either (`CGAL_error` is unconditional). Never expose them
    from a binding. Same for `locate_around_boundary_vertex` on the *unbounded* class.
13. **`Arr_planar_topology_traits_base_2` has a `virtual` destructor and three pure-virtual
    predicates**, so these topology traits are polymorphic types with a vtable — unlike the DCEL
    records. `Arrangement_on_surface_2` stores the topology traits **by value** (`Topology_traits
    m_topol_traits;`), so its address is stable for the arrangement's lifetime, and `topology_traits()`
    never returns null.
14. **Copy construction and assignment of the topology traits are declared but never defined**
    (`Arr_bounded_planar_topology_traits_2(const Self&); Self& operator=(const Self&);` in a
    `protected:` block, with no body anywhere) — a link error if you ever reach them. Copying goes
    through `void assign(const Self& other)` instead, which deep-copies the DCEL and then re-derives
    the cached pointers via `dcel_updated()`.
15. Header sloppiness worth knowing when you read them: the comment on `Vertex* v_br` says
    "A fictitious vertex at (-oo,+oo)" (copy-paste; it is at (+oo,−oo)), and `is_empty_dcel()`'s comment
    says "contains just two four vertices". `Arr_bounded_planar_topology_traits_2.h` includes
    `<variant>` and `<CGAL/use.h>`; `Arr_unb_planar_topology_traits_2.h` includes neither but still uses
    `std::variant` (it works only because `Arr_planar_topology_traits_base_2.h` pulls it in
    transitively).

---

## 1. Which arrangement gets which topology traits

`/opt/homebrew/include/CGAL/Arrangement_2/Arr_default_planar_topology.h`, verbatim (lines 34–68):

```cpp
namespace internal {

template <typename GeomTraits, typename Dcel, typename Tag>
struct Default_planar_topology_impl {};

template <typename GeomTraits, typename Dcel>
struct Default_planar_topology_impl<GeomTraits, Dcel,
                                    Arr_all_sides_oblivious_tag>
{
  // A topology-traits class that supports only bounded curves:
  typedef Arr_bounded_planar_topology_traits_2<GeomTraits, Dcel>    Traits;

};

template <typename GeomTraits, typename Dcel>
struct Default_planar_topology_impl<GeomTraits, Dcel,
                                    Arr_not_all_sides_oblivious_tag>
{
  // A topology-traits class that supports unbounded curves:
  typedef Arr_unb_planar_topology_traits_2<GeomTraits, Dcel>        Traits;
};

} // namespace internal

template <typename GeomTraits, typename Dcel>
struct Default_planar_topology :
  public internal::Default_planar_topology_impl<
    GeomTraits, Dcel,
    typename Arr_all_sides_oblivious_category<
      typename internal::Arr_complete_left_side_category<GeomTraits>::Category,
      typename internal::Arr_complete_bottom_side_category<GeomTraits>::Category,
      typename internal::Arr_complete_top_side_category<GeomTraits>::Category,
      typename internal::Arr_complete_right_side_category<GeomTraits>::Category>::result
>
{};
```

and `Arrangement_2.h` lines 41–66:

```cpp
template <class GeomTraits_,
          class Dcel_ = Arr_default_dcel<GeomTraits_> >
class Arrangement_2 :
  public Arrangement_on_surface_2
    <GeomTraits_, typename Default_planar_topology<GeomTraits_, Dcel_>::Traits>
{
protected:
  typedef Default_planar_topology<GeomTraits_, Dcel_>     Default_topology;
public:
  typedef Arrangement_on_surface_2<GeomTraits_,
                                   typename Default_topology::Traits>  Base;
  typedef GeomTraits_                                     Geometry_traits_2;
  typedef Dcel_                                           Dcel;
  typedef Arrangement_2<Geometry_traits_2, Dcel>          Self;
  typedef typename Default_topology::Traits               Topology_traits;
```

Note `Arr_default_dcel<Traits>` is now just an alias:
`template <typename Traits> using Arr_default_dcel = Arr_dcel<Traits>;`
(`Arr_default_dcel.h:32`), so error messages name `CGAL::Arr_dcel<...>` **[verified]**.

Side categories declared by the traits classes this project uses (grepped verbatim):

| geometry traits | Left/Bottom/Top/Right side category | selected topology traits |
|---|---|---|
| `Arr_segment_traits_2` (`:68–71`) | all `Arr_oblivious_side_tag` | **bounded** **[verified]** |
| `Arr_non_caching_segment_traits_2` | all oblivious (inherits) | **bounded** **[verified]** |
| `Arr_polyline_traits_2<Arr_segment_traits_2>` | forwarded from subcurve traits (`Arr_polycurve_basic_traits_2.h:53–56`) | **bounded** **[verified]** |
| `Arr_circle_segment_traits_2` (`:61–64`) | all oblivious | **bounded** **[verified]** |
| `Arr_conic_traits_2` (`:83–86`) | all oblivious | bounded |
| `Arr_Bezier_curve_traits_2` (`:77–80`) | all oblivious | bounded |
| `Arr_linear_traits_2` (`:61–64`) | all `Arr_open_side_tag` | **unbounded** **[verified]** |
| `Arr_rational_function_traits_2` (`:92–95`) | all `Arr_open_side_tag` | unbounded |
| `Arr_algebraic_segment_traits_2` (`:90–93`) | forwarded from `CKvA_2` (open) | unbounded |

`Arr_complete_*_side_category` supplies `Arr_oblivious_side_tag` when the geometry traits does not
declare the typedef at all, so an old-style traits class silently lands on the bounded topology.

---

## 2. `CGAL::Arr_planar_topology_traits_base_2<GeomTraits_, Dcel_>` — the common base

File: `CGAL/Arr_topology_traits/Arr_planar_topology_traits_base_2.h`.
This is the base of **both** planar classes (and of nothing else). Not a documented public class, but
its members *are* inherited into the public interface of the two derived classes, so they matter.

### 2.1 Template parameters

```cpp
template <typename GeomTraits_,
          typename Dcel_ = Arr_default_dcel<GeomTraits_> >
class Arr_planar_topology_traits_base_2
```

### 2.2 Public typedefs (verbatim, lines 44–65)

```cpp
  ///! \name The geometry-traits types.
  typedef GeomTraits_                                     Geometry_traits_2;
  typedef typename Geometry_traits_2::Point_2             Point_2;
  typedef typename Geometry_traits_2::X_monotone_curve_2  X_monotone_curve_2;

  ///! \name The DCEL types.
  typedef Dcel_                                           Dcel;
  typedef typename Dcel::Size                             Size;
  typedef typename Dcel::Vertex                           Vertex;
  typedef typename Dcel::Halfedge                         Halfedge;
  typedef typename Dcel::Face                             Face;
  typedef typename Dcel::Outer_ccb                        Outer_ccb;
  typedef typename Dcel::Inner_ccb                        Inner_ccb;
  typedef typename Dcel::Isolated_vertex                  Isolated_vertex;

  typedef Arr_planar_topology_traits_base_2<Geometry_traits_2, Dcel>
                                                          Self;
```

> `Vertex`, `Halfedge`, `Face` here are the **DCEL** records (`CGAL::Arr_vertex<…>` etc.), *not* the
> `Arrangement_on_surface_2::Vertex` wrappers. The wrappers derive from them
> (`class Vertex : public DVertex`), so `&*vh` converts implicitly to `const Vertex*` and you can pass
> arrangement handles straight into these methods (see §7.4).

### 2.3 Protected members / lifetime (lines 67–79)

```cpp
protected:
  typedef Arr_traits_basic_adaptor_2<Geometry_traits_2>   Traits_adaptor_2;

  // Data members:
  Dcel m_dcel;                           // The DCEL.

  const Traits_adaptor_2* m_geom_traits; // The geometry-traits adaptor.
  bool m_own_geom_traits;                // Indicate whether we should
                                         // eventually free the traits object.

  // Copy constructor and assignment operator - not supported.
  Arr_planar_topology_traits_base_2(const Self&);
  Self& operator=(const Self&);
```

**Ownership:** the topology traits *owns the DCEL by value*. Every vertex/halfedge/face pointer you
ever see belongs to `m_dcel` and dies with the arrangement. The geometry traits is owned only when the
default ctor was used (`m_own_geom_traits == true`, allocated with `new Traits_adaptor_2`).

### 2.4 Construction / destruction (verbatim, lines 85–108)

```cpp
  /*! constructs defaults. */
  Arr_planar_topology_traits_base_2() :
    m_own_geom_traits(true)
  { m_geom_traits = new Traits_adaptor_2; }

  /*! constructs with a geometry-traits class. */
  Arr_planar_topology_traits_base_2 (const Geometry_traits_2* geom_traits) :
    m_own_geom_traits(false)
  { m_geom_traits = static_cast<const Traits_adaptor_2*>(geom_traits); }

  /*! assigns the contents of another topology-traits class. */
  void assign(const Self& other);

  /*! destructs. */
  virtual ~Arr_planar_topology_traits_base_2()
```

The destructor calls `m_dcel.delete_all()` and deletes the traits adaptor if owned. `assign` (defined at
lines 249–267) does:

```cpp
  m_dcel.delete_all();
  m_dcel.assign(other.m_dcel);
  if (m_own_geom_traits && (m_geom_traits != nullptr)) { delete m_geom_traits; m_geom_traits = nullptr; }
  if (other.m_own_geom_traits) m_geom_traits = new Traits_adaptor_2;
  else m_geom_traits = other.m_geom_traits;
  m_own_geom_traits = other.m_own_geom_traits;
```

> **Lifetime trap:** if `other` did *not* own its traits, `assign` copies the **raw pointer**. The
> assigned-to arrangement then aliases someone else's traits object. This is exactly what
> `Arrangement_on_surface_2`'s copy-assignment does, so a Cython layer that keeps a
> `Arrangement_2(const Traits*)`-constructed arrangement must keep the traits object alive at least as
> long as every copy of that arrangement.

### 2.5 Public member functions inherited by both derived classes

```cpp
  /*! obtains the DCEL (const version). */
  const Dcel& dcel() const { return m_dcel; }

  /*! obtains the DCEL (non-const version). */
  Dcel& dcel() { return (m_dcel); }

  /*! receives a notification on the creation of a new boundary vertex that
   * corresponds to a given point. */
  void notify_on_boundary_vertex_creation(Vertex*,
                                          const Point_2& ,
                                          Arr_parameter_space /* ps_x */,
                                          Arr_parameter_space /* ps_y */)
  {
    // In the planar-topology traits this function should never be invoked:
    return;
  }

  /*! receives a notification on the creation of a new boundary vertex that
   * corresponds to the given curve end. */
  void notify_on_boundary_vertex_creation(Vertex*,
                                          const X_monotone_curve_2& ,
                                          Arr_curve_end,
                                          Arr_parameter_space /* ps_x */,
                                          Arr_parameter_space /* ps_y */)
  {
    // In the planar-topology traits this function should never be invoked:
    return;
  }

  /*! determines whether the function should decide on swapping the predecssor
   * halfedges that imply two ccb (and whose signs are given here). ... */
  bool
  let_me_decide_the_outer_ccb(std::pair<CGAL::Sign, CGAL::Sign> /* signs1 */,
                              std::pair<CGAL::Sign, CGAL::Sign> /* signs2 */,
                              bool& swap_predecessors) const
  {
    swap_predecessors = false;
    return false;
  }

  /*! given signs of two ccbs that show up when splitting upon insertion of
   * curve into two, determines what happens to the face(s). ... */
  std::pair<bool, bool>
  face_split_after_edge_insertion(std::pair<CGAL::Sign,
                                            CGAL::Sign > /* signs1 */,
                                  std::pair<CGAL::Sign,
                                            CGAL::Sign > /* signs2 */) const
  {
    // In case of a planar topology, connecting two vertices on the same
    // inner CCB closes a new face that becomes a hole in the original face:
    return std::make_pair(true, true);
  }

  /*! determines whether the given point lies in the interior of the given face.
   * \param f The face.
   * \param p The query point.
   * \param v The vertex associated with p (if exists).
   * \param f must not be fictitious, and v must not lie at infinity.
   * \return Whether p is contained in f's interior. */
  bool is_in_face(const Face* f, const Point_2& p, const Vertex* v) const;
```

Notes:

* Both `notify_on_boundary_vertex_creation` overloads are **no-ops** in the planar case (the doc
  comment says "should never be invoked", but they are in fact invoked, from
  `Arr_accessor::create_boundary_vertex(..., notify = true)`; they simply do nothing). Contrast with
  the spherical traits, where the same call registers the vertex in `m_boundary_vertices`.
* `let_me_decide_the_outer_ccb` always returns `false` and sets `swap_predecessors = false` — i.e. the
  planar topology always lets the generic "lexicographically minimal point" rule decide. It is `const`.
* `face_split_after_edge_insertion` always returns `{true, true}`.
* `is_in_face` is the point-in-face ray-shooting used by the walk point-location. Preconditions from
  the body (lines 276–278): `(v == nullptr) || !v->has_null_point()` and
  `(v == nullptr) || equal_2(p, v->point())`. It early-returns `true` when
  `f->is_unbounded() && f->number_of_outer_ccbs() == 0` (the bounded topology's single unbounded face).
  It uses `f->outer_ccbs_begin()` unconditionally otherwise, so **passing the fictitious face is UB**
  (it has zero outer CCBs but is also `is_unbounded()`, so it takes the early `return true` branch —
  i.e. it silently claims every point is inside the fictitious face).

### 2.6 Pure virtuals the derived classes must (and do) implement

```cpp
  /*! This function is used by the "walk" point-location strategy. */
  virtual const Face* initial_face () const = 0;

  virtual Comparison_result compare_x(const Point_2& p, const Vertex* v) const = 0;
  virtual Comparison_result compare_xy(const Point_2& p, const Vertex* v) const = 0;
  virtual Comparison_result compare_y_at_x(const Point_2& p, const Halfedge* he) const = 0;
```

(`compare_y_at_x`'s precondition: "p should lie in the x-range of the given edge".)

---

## 3. `CGAL::Arr_bounded_planar_topology_traits_2<GeometryTraits_2, Dcel_>`

File: `CGAL/Arr_bounded_planar_topology_traits_2.h`.
Doc comment: *"A topology-traits class that encapsulates the embedding of 2D arrangements of bounded
curves on the plane."*

### 3.1 Template parameters and class head (verbatim, lines 50–57)

```cpp
template <typename GeometryTraits_2,
          typename Dcel_ = Arr_default_dcel<GeometryTraits_2> >
class Arr_bounded_planar_topology_traits_2 :
  public Arr_planar_topology_traits_base_2<GeometryTraits_2, Dcel_>
{
public:
  typedef GeometryTraits_2                              Geometry_traits_2;
  typedef Dcel_                                         Dcel;
```

private aliases: `typedef Geometry_traits_2 Gt2;`,
`typedef Arr_planar_topology_traits_base_2<Gt2, Dcel_> Base;`

### 3.2 Public typedefs (verbatim, lines 63–85)

```cpp
  ///! \name The geometry-traits types.
  typedef typename Base::Point_2                        Point_2;
  typedef typename Base::X_monotone_curve_2             X_monotone_curve_2;

  ///! \name The DCEL types.
  typedef typename Base::Size                            Size;
  typedef typename Base::Vertex                          Vertex;
  typedef typename Base::Halfedge                        Halfedge;
  typedef typename Base::Face                            Face;
  typedef typename Base::Outer_ccb                       Outer_ccb;
  typedef typename Base::Inner_ccb                       Inner_ccb;
  typedef typename Base::Isolated_vertex                 Isolated_vertex;

  //! \name Arrangement types
  typedef Arr_bounded_planar_topology_traits_2<Gt2, Dcel> Self;
  typedef Arr_traits_basic_adaptor_2<Gt2>                 Gt_adaptor_2;
```

### 3.3 The four side categories + static asserts (verbatim, lines 87–98)

```cpp
  ///! \name The side tags
  typedef typename Gt_adaptor_2::Left_side_category   Left_side_category;
  typedef typename Gt_adaptor_2::Bottom_side_category Bottom_side_category;
  typedef typename Gt_adaptor_2::Top_side_category    Top_side_category;
  typedef typename Gt_adaptor_2::Right_side_category  Right_side_category;

  static_assert(std::is_same< Left_side_category, Arr_oblivious_side_tag >::value);
  static_assert(std::is_same< Bottom_side_category, Arr_oblivious_side_tag >::value);
  static_assert(std::is_same< Top_side_category, Arr_oblivious_side_tag >::value);
  static_assert(std::is_same< Right_side_category, Arr_oblivious_side_tag >::value);
```

> These are C++17 one-argument `static_assert`s (CGAL 6 dropped the message and dropped
> `boost::is_same`). Note the categories are taken from the **adaptor**, not from the geometry traits
> directly, so a traits class that omits them still compiles here (the adaptor completes them to
> oblivious).

### 3.4 `rebind` (verbatim, lines 100–107)

```cpp
  /*! \struct
   * An auxiliary structure for rebinding the topology traits with a new
   * geometry-traits class and a new DCEL class.
   */
  template <typename T, typename D>
  struct rebind {
    typedef Arr_bounded_planar_topology_traits_2<T, D> other;
  };
```

Used by the surface-sweep machinery and by `Arr_overlay_2` / `Arr_batched_point_location` to build a
topology traits over a *different* geometry traits (e.g. `Arr_overlay_traits_2`) and a different DCEL.

### 3.5 Data members and lifetime (lines 109–115)

```cpp
protected:
  // Data members:
  Face* unb_face;     // The unbounded face.

  // Copy constructor and assignment operator - not supported.
  Arr_bounded_planar_topology_traits_2(const Self&);
  Self& operator=(const Self&);
```

`unb_face` is the **only** cached pointer. It is set by `init_dcel()` and re-derived by
`dcel_updated()`. It stays valid for the whole arrangement lifetime under normal use (the unbounded
face is never deleted).

### 3.6 Construction and `assign`

```cpp
  /*! constructs default. */
  Arr_bounded_planar_topology_traits_2() :
    Base(),
    unb_face(nullptr)
  {}

  /*! constructs from a geometry-traits object. */
  Arr_bounded_planar_topology_traits_2(const Gt2* traits) :
    Base(traits),
    unb_face(nullptr)
  {}

  /*! assigns the contents of another topology-traits class. */
  void assign(const Self& other);
```

`assign` (impl file, lines 30–39) is exactly:

```cpp
  // Assign the base class.
  Base::assign(other);
  // Update the topology-traits properties after the DCEL have been updated.
  dcel_updated();
```

> Note the ctors leave `unb_face == nullptr`; `Arrangement_on_surface_2`'s constructors immediately call
> `m_topol_traits.init_dcel()` (`Arrangement_on_surface_2_impl.h:78` and `:133`), so a live arrangement
> never has a null `unb_face`.

### 3.7 DCEL-accounting methods (verbatim, lines 140–186) — all `const`

```cpp
  /*! determines whether the DCEL reprsenets an empty structure. */
  bool is_empty_dcel() const
  {
    // An empty bounded arrangement has no edges or vertices.
    return (this->m_dcel.size_of_vertices() == 0 &&
            this->m_dcel.size_of_halfedges() == 0);
  }

  /*! checks if the given vertex is concrete (associated with a point). */
  inline bool is_concrete_vertex(const Vertex*) const { return true; }

  /*! obtains the number of concrete vertices. */
  Size number_of_concrete_vertices() const

  /*! checks if the given vertex is valid (not a fictitious one). */
  inline bool is_valid_vertex(const Vertex*) const { return true; }

  /*! obtains the number of valid vertices. */
  Size number_of_valid_vertices() const

  /*! checks if the given halfedge is valid (not a fictitious one). */
  inline bool is_valid_halfedge(const Halfedge*) const { return true; }

  /*! obtains the number of valid halfedges. */
  Size number_of_valid_halfedges() const

  /*! checks if the given face is valid (not a fictitious one). */
  inline bool is_valid_face (const Face*) const { return true; }

  /*! obtains the number of valid faces. */
  Size number_of_valid_faces() const
```

Bodies: `number_of_concrete_vertices()` and `number_of_valid_vertices()` both return
`this->m_dcel.size_of_vertices()`; `number_of_valid_halfedges()` returns
`this->m_dcel.size_of_halfedges()`; `number_of_valid_faces()` returns `this->m_dcel.size_of_faces()`.
All three "is_valid…" predicates **ignore their argument** and return `true` — passing `nullptr` is
harmless. There is **no `is_valid_dcel()`** method on either planar class (grep over all of
`/opt/homebrew/include/CGAL` finds zero hits); the whole-structure check is
`Arrangement_on_surface_2::is_valid() const`.

### 3.8 Sweep-helper alias templates (lines 200–239)

```cpp
private:
  typedef Arrangement_on_surface_2<Gt2, Self>                   Arr;
public:
  template <typename Evt, typename Crv>
  using Construction_helper =
    Arr_bounded_planar_construction_helper<Gt2, Arr, Evt, Crv>;

  template <typename Evt, typename Crv>
  using No_intersection_construction_helper =
    Arr_bounded_planar_construction_helper<Gt2, Arr, Evt, Crv>;

  typedef Arr_insertion_traits_2<Gt2, Arr>                      I_traits;
  template <typename Evt, typename Crv>
  using Insertion_helper =
    Arr_bounded_planar_insertion_helper<I_traits, Arr, Evt, Crv>;

  typedef Arr_basic_insertion_traits_2<Gt2, Arr>                Nxi_traits;
  template <typename Evt, typename Crv>
  using No_intersection_insertion_helper =
    Arr_bounded_planar_insertion_helper<Nxi_traits, Arr, Evt, Crv>;

  typedef Arr_batched_point_location_traits_2<Arr>              Bpl_traits;
  template <typename Evt, typename Crv>
  using Batched_point_location_helper =
    Arr_bounded_planar_batched_pl_helper<Bpl_traits, Arr, Evt, Crv>;

  typedef Arr_batched_point_location_traits_2<Arr>              Vd_traits;
  template <typename Evt, typename Crv>
  using Vertical_decomposition_helper =
    Arr_bounded_planar_vert_decomp_helper<Vd_traits, Arr, Evt, Crv>;

  template <typename Gt, typename Evt, typename Crv,
            typename ArrA, typename ArrB>
  using Overlay_helper =
    Arr_bounded_planar_overlay_helper<Gt, ArrA, ArrB, Arr, Evt, Crv>;
```

> `Arr` is `Arrangement_on_surface_2<Gt2, Self>` — **not** `Arrangement_2<Gt2, Dcel>`. Since
> `Arrangement_2` derives from exactly that instantiation, handles are interchangeable, but the *type
> name* in template errors and in `Zone_insertion_visitor` is the `_on_surface_` one.

### 3.9 Visitor / strategy typedefs (verbatim, lines 245–253)

```cpp
  typedef Arr_inc_insertion_zone_visitor<Arr>
    Zone_insertion_visitor;

  typedef Arr_walk_along_line_point_location<Arr>
    Default_point_location_strategy;

  typedef Arr_walk_along_line_point_location<Arr>
    Default_vertical_ray_shooting_strategy;
```

Identical (character for character) in the unbounded class. So:
`arr.topology_traits()->…` is not how you use them — you use
`typename Arr::Topology_traits::Default_point_location_strategy pl(arr);`. Both default strategies are
the *walk-along-a-line* strategy, which starts from `topology_traits()->initial_face()`.

### 3.10 Topology-traits methods (verbatim, lines 258–419)

```cpp
  /*! initializes an empty DCEL structure. */
  void init_dcel();

  /*! makes the necessary updates after the DCEL structure have been updated. */
  void dcel_updated();

  /*! checks if the given vertex is associated with the given curve end.
   * \pre The curve has a boundary condition in either x or y.
   * \return Whether v represents the given curve end. */
  bool are_equal(const Vertex* v,
                 const X_monotone_curve_2& cv, Arr_curve_end ind,
                 Arr_parameter_space ps_x, Arr_parameter_space ps_y) const

  /*! ... \pre The curve has a boundary condition in either x or y.
   * \return An object that wraps the curve end. */
  std::optional<std::variant<Vertex*, Halfedge*> >
  place_boundary_vertex(Face*,
                        const X_monotone_curve_2&,
                        Arr_curve_end,
                        Arr_parameter_space /* ps_x */,
                        Arr_parameter_space /* ps_y */)

  Halfedge*
  locate_around_boundary_vertex(Vertex*,
                                const X_monotone_curve_2&,
                                Arr_curve_end,
                                Arr_parameter_space /* ps_x */,
                                Arr_parameter_space /* ps_y */) const

  std::variant<Vertex*, Halfedge*, Face*>
  locate_curve_end(const X_monotone_curve_2&,
                   Arr_curve_end,
                   Arr_parameter_space /* ps_x */,
                   Arr_parameter_space /* ps_y */)

  /*! splits a fictitious edge using the given vertex.
   * \pre e is a fictitious halfedge. */
  Halfedge* split_fictitious_edge (Halfedge*, Vertex*)

  /*! determines whether the given face is unbounded.
   * There is only one unbounded face in the arrangement: */
  bool is_unbounded(const Face* f) const { return (f == unb_face); }

  /*! determines whether the given boundary vertex is redundant.
   * There are no redundant vertices. */
  bool is_redundant(const Vertex*) const { return false; }

  /*! erases the given redundant vertex by merging a fictitious edge.
   * \pre v is a redundant vertex. */
  Halfedge* erase_redundant_vertex(Vertex*)

  //! reference_face (const version).
  const Face* reference_face() const { return unbounded_face(); }
  //! reference_face (non-const version).
  Face* reference_face() { return unbounded_face(); }
```

Bodies / behaviour:

| method | body | const? | safe to call? |
|---|---|---|---|
| `init_dcel()` | `m_dcel.delete_all(); unb_face = m_dcel.new_face(); unb_face->set_unbounded(true); unb_face->set_fictitious(false);` | non-const | **destructive** — wipes the arrangement without freeing `Point_2`/`X_monotone_curve_2` objects or notifying observers. Called by the `Arrangement_on_surface_2` ctors and by `clear()` (which frees points/curves itself first). Never call from a binding. |
| `dcel_updated()` | scans `m_dcel.faces_begin()..faces_end()` for the first `fit->is_unbounded()` and caches it; `CGAL_assertion(unb_face != nullptr)` | non-const | idempotent re-derivation; only needed after external DCEL surgery (`Arr_accessor::dcel_updated()`, `Arrangement_2_reader.h:140`). |
| `are_equal(...)` | `CGAL_assertion((ps_x == ARR_INTERIOR) && (ps_y == ARR_INTERIOR));` then compares `v->point()` with `construct_min_vertex_2`/`construct_max_vertex_2` of `cv` | **const** | fine, but the assertion contradicts the doc `\pre` (which demands a boundary condition). In practice the sweep only calls it with interior ends here. |
| `place_boundary_vertex(...)` | `CGAL_error(); return std::nullopt;` | non-const | **never** |
| `locate_around_boundary_vertex(...)` | `CGAL_error(); return nullptr;` | **const** | **never** |
| `locate_curve_end(...)` | `CGAL_error();` then returns `Result(Vertex*(nullptr))` | non-const | **never** |
| `split_fictitious_edge(...)` | `CGAL_error(); return nullptr;` | non-const | **never** |
| `is_unbounded(const Face*)` | pointer compare with `unb_face` | **const** | yes |
| `is_redundant(const Vertex*)` | `return false;` | **const** | yes (useless) |
| `erase_redundant_vertex(Vertex*)` | `CGAL_error(); return nullptr;` | non-const | **never** |

`CGAL_error()` is `CGAL::error(__FILE__, __LINE__)` — it is **not** compiled out in release builds; by
default it prints `CGAL ERROR: ...` and throws/aborts.

### 3.11 Accessors and predicates specialised for this class (verbatim, lines 426–464)

```cpp
  /*! This function is used by the "walk" point-location strategy. */
  const Face* initial_face() const { return (unb_face); }

  /*! obtains the unbounded face (const version). */
  const Face* unbounded_face() const { return (unb_face); }

  /*! obtains the unbounded face (non-const version). */
  Face* unbounded_face() { return (unb_face); }

  virtual Comparison_result compare_x(const Point_2& p, const Vertex* v) const
  { return (this->m_geom_traits->compare_x_2_object()(p, v->point())); }

  virtual Comparison_result compare_xy(const Point_2& p, const Vertex* v) const
  { return (this->m_geom_traits->compare_xy_2_object()(p, v->point())); }

  /*! \pre p should lie in the x-range of the given edge. */
  virtual Comparison_result compare_y_at_x(const Point_2& p,
                                           const Halfedge* he) const
  { return (this->m_geom_traits->compare_y_at_x_2_object()(p, he->curve())); }
```

`initial_face()` is **not** marked `override`/`virtual` in the header but it does override the pure
virtual from the base (the signature matches exactly). `compare_x`/`compare_xy` dereference
`v->point()`, so passing an open-boundary vertex aborts — impossible under this topology.

### 3.12 Members that do **not** exist here

`fictitious_face()`, `bottom_left_vertex()`, `top_left_vertex()`, `bottom_right_vertex()`,
`top_right_vertex()`, `_curve()`, `_is_on_fictitious_edge()`, `n_inf_verts`. All hard compile errors
**[verified]**.

---

## 4. `CGAL::Arr_unb_planar_topology_traits_2<GeometryTraits_2, Dcel_>`

File: `CGAL/Arr_unb_planar_topology_traits_2.h` (+ `…/Arr_unb_planar_topology_traits_2_impl.h`).
Doc comment: *"A topology-traits class that encapsulates the embedding of 2D arrangements of unbounded
curves on the plane."*

### 4.1 Template parameters and class head (verbatim, lines 47–58)

```cpp
template <typename GeometryTraits_2,
          typename Dcel_ = Arr_default_dcel<GeometryTraits_2> >
class Arr_unb_planar_topology_traits_2 :
  public Arr_planar_topology_traits_base_2<GeometryTraits_2, Dcel_>
{
public:
  typedef GeometryTraits_2                              Geometry_traits_2;
  typedef Dcel_                                         Dcel;

private:
  typedef Geometry_traits_2                             Gt2;
  typedef Arr_planar_topology_traits_base_2<Gt2, Dcel_> Base;
```

### 4.2 Public typedefs (verbatim, lines 61–82)

```cpp
  typedef typename Base::Point_2                         Point_2;
  typedef typename Base::X_monotone_curve_2              X_monotone_curve_2;

  typedef typename Base::Size                            Size;
  typedef typename Base::Vertex                          Vertex;
  typedef typename Base::Halfedge                        Halfedge;
  typedef typename Base::Face                            Face;
  typedef typename Base::Outer_ccb                       Outer_ccb;
  typedef typename Base::Inner_ccb                       Inner_ccb;
  typedef typename Base::Isolated_vertex                 Isolated_vertex;

  typedef Arr_unb_planar_topology_traits_2<Gt2, Dcel>   Self;
  typedef Arr_traits_basic_adaptor_2<Gt2>               Gt_adaptor_2;
```

### 4.3 Side categories + static asserts (verbatim, lines 84–99)

```cpp
  typedef typename Gt_adaptor_2::Left_side_category   Left_side_category;
  typedef typename Gt_adaptor_2::Bottom_side_category Bottom_side_category;
  typedef typename Gt_adaptor_2::Top_side_category    Top_side_category;
  typedef typename Gt_adaptor_2::Right_side_category  Right_side_category;

  static_assert(std::is_same< Left_side_category, Arr_oblivious_side_tag >::value ||
                         std::is_same< Left_side_category, Arr_open_side_tag >::value);
  static_assert(std::is_same< Bottom_side_category, Arr_oblivious_side_tag >::value ||
                         std::is_same< Bottom_side_category, Arr_open_side_tag >::value);
  static_assert(std::is_same< Top_side_category, Arr_oblivious_side_tag>::value ||
                         std::is_same< Top_side_category, Arr_open_side_tag >::value);
  static_assert(std::is_same< Right_side_category, Arr_oblivious_side_tag >::value ||
                         std::is_same< Right_side_category, Arr_open_side_tag >::value);
```

Only `Arr_oblivious_side_tag` and `Arr_open_side_tag` are accepted — **`Arr_closed_side_tag`,
`Arr_contracted_side_tag` and `Arr_identified_side_tag` do not compile here** (those go to the
spherical topology). This is what makes this class "the unbounded plane" and not "any surface".

### 4.4 `rebind` (verbatim, lines 101–108)

```cpp
  template <typename T, typename D>
  struct rebind {
    typedef Arr_unb_planar_topology_traits_2<T, D> other;
  };
```

### 4.5 Data members and lifetime (verbatim, lines 110–121)

```cpp
protected:
  // Data members:
  Vertex* v_bl;         // A fictitious vertex at (-oo,-oo).
  Vertex* v_tl;         // A fictitious vertex at (-oo,+oo).
  Vertex* v_br;         // A fictitious vertex at (-oo,+oo).   <-- comment typo; it is (+oo,-oo)
  Vertex* v_tr;         // A fictitious vertex at (+oo,+oo).
  Size n_inf_verts;     // Number of vertices at infinity.
  Face* fict_face;      // The fictitious DCEL face.

  // Copy constructor and assignment operator - not supported.
  Arr_unb_planar_topology_traits_2(const Self&);
  Self& operator=(const Self&);
```

**Handle validity:** `v_bl/v_tl/v_br/v_tr` and `fict_face` are created once by `init_dcel()` and are
**never deleted or replaced** during insertions/removals — only `assign()`/`init_dcel()`/`dcel_updated()`
change them **[verified across 4 successive line insertions: all four corner pointers and `fict_face`
were bit-identical throughout]**. `n_inf_verts` counts *all* vertices with a null point, i.e. the four
corners **plus** every curve end at infinity: it starts at 4 and is incremented by
`split_fictitious_edge` / decremented by `erase_redundant_vertex`.

### 4.6 Construction and `assign` (declarations lines 127–134; definitions in the impl file)

```cpp
  /*! constructs Default. */
  Arr_unb_planar_topology_traits_2();

  /*! constructs with a geometry-traits class. */
  Arr_unb_planar_topology_traits_2(const Gt2* tr);

  /*! assigns the contents of another topology-traits class. */
  void assign(const Self& other);
```

Both ctors just null the six data members (`n_inf_verts(0)`). `assign` is
`Base::assign(other); dcel_updated();`.

### 4.7 DCEL-accounting methods (verbatim, lines 140–190) — all `const`

```cpp
  /*! determines whether the DCEL reprsenets an empty structure. */
  bool is_empty_dcel() const
  {
    // An empty arrangement contains just two four vertices at infinity
    // and eight fictitious halfedges connecting them.
    return (this->m_dcel.size_of_vertices() == 4 &&
            this->m_dcel.size_of_halfedges() == 8);
  }

  /*! checks whether the given vertex is concrete (associated with a point). */
  bool is_concrete_vertex(const Vertex* v) const
  { return (! v->has_null_point()); }

  /*! obtains the number of concrete vertices. */
  Size number_of_concrete_vertices() const
  { return (this->m_dcel.size_of_vertices() - n_inf_verts); }

  /*! checks if the given vertex is valid (not a fictitious one). */
  bool is_valid_vertex(const Vertex* v) const
  {
    return (! v->has_null_point() ||
            ((v != v_bl) && (v != v_tl) && (v != v_br) && (v != v_tr)));
  }

  /*! obtains the number of valid vertices. */
  Size number_of_valid_vertices() const
  { return (this->m_dcel.size_of_vertices() - 4); }

  /*! checks whether the given halfedge is valid (not a fictitious one). */
  bool is_valid_halfedge(const Halfedge* he) const
  { return (! he->has_null_curve()); }

  /*! obtains the number of valid halfedges. */
  Size number_of_valid_halfedges() const
  { return (this->m_dcel.size_of_halfedges() - 2*n_inf_verts); }

  /*! checks whether the given face is valid (not a fictitious one). */
  bool is_valid_face (const Face* f) const { return (! f->is_fictitious()); }

  /*! obtains the number of valid faces. */
  Size number_of_valid_faces() const
  { return (this->m_dcel.size_of_faces() - 1); }
```

Read these carefully — they are the definitions of what `Arrangement_2`'s counters mean:

* `is_valid_vertex` is `true` for a **vertex at infinity that is not a corner** — such a vertex *is*
  counted by `number_of_vertices_at_infinity()` but is **not** in `arr.vertices_begin()..end()`
  (that filter is `is_concrete_vertex`).
* `number_of_valid_halfedges() == size_of_halfedges() - 2*n_inf_verts` encodes "each vertex at
  infinity induces two fictitious halfedges" (the rectangle cycle has exactly `n_inf_verts` halfedges
  and each has a twin) **[verified: 8−2·4=0, 14−2·6=2, 24−2·8=8, 38−2·10=18]**.
* `number_of_valid_faces() == size_of_faces() - 1` — exactly one fictitious face, always.

### 4.8 Sweep-helper alias templates and visitor typedefs (lines 204–256)

Structurally identical to §3.8/§3.9 with `Arr_unb_planar_*` helpers substituted:

```cpp
  template <typename Evt, typename Crv>
  using Construction_helper = Arr_unb_planar_construction_helper<Gt2, Arr, Evt, Crv>;
  template <typename Evt, typename Crv>
  using No_intersection_construction_helper = Arr_unb_planar_construction_helper<Gt2, Arr, Evt, Crv>;
  typedef Arr_insertion_traits_2<Gt2, Arr>                      I_traits;
  template <typename Evt, typename Crv>
  using Insertion_helper = Arr_unb_planar_insertion_helper<I_traits, Arr, Evt, Crv>;
  typedef Arr_basic_insertion_traits_2<Gt2, Arr>                Nxi_traits;
  template <typename Evt, typename Crv>
  using No_intersection_insertion_helper = Arr_unb_planar_insertion_helper<Nxi_traits, Arr, Evt, Crv>;
  typedef Arr_batched_point_location_traits_2<Arr>              Bpl_traits;
  template <typename Evt, typename Crv>
  using Batched_point_location_helper = Arr_unb_planar_batched_pl_helper<Bpl_traits, Arr, Evt, Crv>;
  typedef Arr_batched_point_location_traits_2<Arr>              Vd_traits;
  template <typename Evt, typename Crv>
  using Vertical_decomposition_helper = Arr_unb_planar_vert_decomp_helper<Vd_traits, Arr, Evt, Crv>;
  template <typename Gt, typename Evt, typename Crv, typename ArrA, typename ArrB>
  using Overlay_helper = Arr_unb_planar_overlay_helper<Gt, ArrA, ArrB, Arr, Evt, Crv>;

  typedef Arr_inc_insertion_zone_visitor<Arr>          Zone_insertion_visitor;
  typedef Arr_walk_along_line_point_location<Arr>      Default_point_location_strategy;
  typedef Arr_walk_along_line_point_location<Arr>      Default_vertical_ray_shooting_strategy;
```

### 4.9 Topology-traits methods (verbatim declarations, lines 262–391)

```cpp
  /*! initializes an empty DCEL structure. */
  void init_dcel();

  /*! makes the necessary updates after the DCEL structure have been updated. */
  void dcel_updated();

  /*! checks if the given vertex is associated with the given curve end.
   * \pre The curve has a boundary condition in either x or y. */
  bool are_equal(const Vertex* v,
                 const X_monotone_curve_2& cv, Arr_curve_end ind,
                 Arr_parameter_space ps_x, Arr_parameter_space ps_y) const;

  /*! ... \pre The curve has a boundary condition in either x or y.
   * \return An object that contains the curve end.
   *         In our case this object always wraps a fictitious edge. */
  std::optional<std::variant<Vertex*, Halfedge*> >
  place_boundary_vertex(Face* f,
                        const X_monotone_curve_2& cv,
                        Arr_curve_end ind,
                        Arr_parameter_space ps_x,
                        Arr_parameter_space ps_y);

  Halfedge*
  locate_around_boundary_vertex(Vertex* /* v */,
                                const X_monotone_curve_2& /* cv */,
                                Arr_curve_end /* ind */,
                                Arr_parameter_space /* ps_x */,
                                Arr_parameter_space /* ps_y */) const
  {
    CGAL_error();
    return (nullptr);
  }

  /*! locates a DCEL feature that contains the given unbounded curve end.
   * \pre The curve end is unbounded in either x or y.
   * \return An object that contains the curve end.
   *         In our case this object may either wrap an unbounded face,
   *         or an edge with an end-vertex at infinity (in case of an overlap). */
  std::variant<Vertex*, Halfedge*, Face*>
  locate_curve_end(const X_monotone_curve_2& cv,
                   Arr_curve_end ind,
                   Arr_parameter_space ps_x,
                   Arr_parameter_space ps_y);

  /*! splits a fictitious edge using the given vertex.
   * \pre e is a fictitious halfedge.
   * \return A halfedge whose direction is the same as e's and whose target is
   *         the split vertex v. */
  Halfedge* split_fictitious_edge(Halfedge* e, Vertex* v);

  /*! determines whether the given face is unbounded. */
  bool is_unbounded(const Face* f) const;

  /*! determines whether the given boundary vertex is redundant. */
  bool is_redundant(const Vertex* v) const;

  /*! erases the given redundant vertex by merging a fictitious edge.
   * The function does not free the vertex v itself.
   * \pre v is a redundant vertex.
   * \return One of the pair of halfedges that form the merged edge. */
  Halfedge* erase_redundant_vertex(Vertex* v);

  //! reference_face (const version).
  const Face* reference_face() const
  {
    CGAL_assertion(v_tr->halfedge()->direction() == ARR_LEFT_TO_RIGHT);
    return v_tr->halfedge()->outer_ccb()->face();
  }

  //! reference_face (non-const version).
  Face* reference_face()
  {
    CGAL_assertion(v_tr->halfedge()->direction() == ARR_LEFT_TO_RIGHT);
    return v_tr->halfedge()->outer_ccb()->face();
  }
```

What each one actually does (from the impl file):

* **`init_dcel()`** — see §5 for the full verbatim construction and the ASCII diagram.
* **`dcel_updated()`** (impl lines 76–133) — full rescan: iterates `m_dcel.vertices_begin()..end()`,
  counts every vertex with `has_null_point()` into `n_inf_verts`, and identifies a corner by
  *degree 2* (`first_he = vit->halfedge(); next_he = first_he->next()->opposite();
  if (next_he->next()->opposite() == first_he)`) combined with its `(parameter_space_in_x,
  parameter_space_in_y)` pair; then scans the faces for the unique `fit->is_fictitious()`.
  `CGAL_assertion(v_bl && v_tl && v_br && v_tr)` and `CGAL_assertion(fict_face != nullptr)`.
  **O(V+F)**, non-const.
* **`are_equal(...) const`** — returns `false` immediately if
  `ps_x != v->parameter_space_in_x() || ps_y != v->parameter_space_in_y()`. Otherwise fetches the curve
  inducing `v` via the protected `_curve(v, v_ind)`; if there is none it just compares the two
  parameter-space pairs; else compares with `compare_y_curve_ends_2` (when `ps_x != ARR_INTERIOR`) or
  `compare_x_curve_ends_2` (when `ps_y != ARR_INTERIOR`) and returns `res == EQUAL`.
* **`place_boundary_vertex(f, cv, ind, ps_x, ps_y)`** — walks `*(f->outer_ccbs_begin())` around `f`'s
  CCB, and for every halfedge with `has_null_curve()` tests `_is_on_fictitious_edge(...)`; returns
  `Result(curr)` (i.e. `std::optional<std::variant<...>>` holding a **`Halfedge*`**) for the first hit,
  asserting `! eq_source && ! eq_target`. If the loop completes: `CGAL_error(); return std::nullopt;`.
  **It never returns a `Vertex*`** despite the variant type, and it does **not** modify the DCEL.
* **`locate_curve_end(cv, ind, ps_x, ps_y)`** — walks `*(fict_face->inner_ccbs_begin())` (the rectangle
  cycle). On a hit: if `eq_source`, returns `Halfedge*` = `curr->opposite()->next()`; if `eq_target`,
  returns `Halfedge*` = `curr->opposite()->prev()` (both asserted non-fictitious — these are the overlap
  cases); otherwise returns `Face*` = `curr->opposite()->outer_ccb()->face()` with
  `CGAL_assertion(uf->is_unbounded() && ! uf->is_fictitious())`. Falls through to `CGAL_error()`.
  Non-const, but does not modify the DCEL.
* **`split_fictitious_edge(e, v)`** — **mutating**. Precondition
  `CGAL_precondition(v->parameter_space_in_x() != ARR_INTERIOR || v->parameter_space_in_y() != ARR_INTERIOR)`.
  Increments `n_inf_verts`, allocates one new edge pair, splices it in:

  ```
  //            he1      he3
  //         -------> ------->
  //       (.)      (.)v     (.)
  //         <------- <-------
  //            he2      he4
  ```

  `he3->set_direction(he1->direction())` — the new halfedge inherits `e`'s direction. Returns `he1`
  (i.e. **`e` itself**, now retargeted at `v`), *not* the new halfedge — read the doc comment carefully:
  "a halfedge whose direction is the same as e's and whose target is the split vertex v".
  Asserts `! he1->is_on_inner_ccb()`, `he1->outer_ccb()->face()->is_unbounded()`,
  `he2->is_on_inner_ccb()`, `he2->inner_ccb()->face() == fict_face` — i.e. **`e` must be the halfedge on
  the *unbounded-face* side, not the fictitious-face side**.
* **`is_unbounded(const Face* f) const`** — walks `*(f->outer_ccbs_begin())` and returns `true` as soon
  as it meets a halfedge with `has_null_curve()`. **O(|CCB|)**, and it dereferences `outer_ccbs_begin()`
  unconditionally → **passing the fictitious face (0 outer CCBs) is undefined behaviour**.
* **`is_redundant(const Vertex* v) const`** — `CGAL_precondition(v != v_bl && v != v_tl && v != v_br && v != v_tr)`;
  returns `true` iff `v` has degree 2 with both incident halfedges fictitious.
* **`erase_redundant_vertex(Vertex* v)`** — **mutating**. `CGAL_precondition(is_redundant(v))`. Merges
  the two fictitious edges back into one, fixes up `Inner_ccb`/`Outer_ccb` representatives, decrements
  `n_inf_verts`, `delete_edge(he3)`, returns `he1`. Does **not** free `v` ("the `Arrangement_on_surface_2`
  class will do it").
* **`reference_face()`** — the unbounded face incident to the top-right corner. Verified to be a
  *different* face from `Arrangement_2::unbounded_face()` in general (gotcha 4).

### 4.10 Accessors specialised for this class (verbatim, lines 398–429)

```cpp
  /*! This function is used by the "walk" point-location strategy. */
  const Face* initial_face() const { return fict_face; }

  /*! obtains the fictitious face (const version). */
  const Face* fictitious_face() const { return fict_face; }

  /*! obtains the fictitious face (non-const version). */
  Face* fictitious_face() { return fict_face; }

  /*! obtains the bottom-left fictitious vertex (const version). */
  const Vertex* bottom_left_vertex() const { return (v_bl); }

  /*! obtains the bottom-left fictitious vertex (non-const version). */
  Vertex* bottom_left_vertex() { return (v_bl); }

  /*! obtains the top-left fictitious vertex (const version). */
  const Vertex* top_left_vertex() const { return (v_tl); }

  /*! obtains the top-left fictitious vertex (non-const version). */
  Vertex* top_left_vertex() { return (v_tl); }

  /*! obtains the bottom-right fictitious vertex (const version). */
  const Vertex* bottom_right_vertex() const { return (v_br); }

  /*! obtains the bottom-right fictitious vertex (non-const version). */
  Vertex* bottom_right_vertex() { return (v_br); }

  /*! obtains the top-right fictitious vertex (const version). */
  const Vertex* top_right_vertex() const { return (v_tr); }

  /*! obtains the top-right fictitious vertex (non-const version). */
  Vertex* top_right_vertex() { return (v_tr); }
```

> **`initial_face()` here is the FICTITIOUS face**, which is why
> `Arrangement_on_surface_2::fictitious_face()` (= `initial_face()`) is genuinely fictitious under this
> topology and merely the unbounded face under the bounded one (gotcha 3).

Wrapping a corner in an arrangement handle is legal and is exactly what the sweep helpers do:
`Vertex_const_handle v_tl = Vertex_const_handle(m_top_traits->top_left_vertex());`
(`Arr_unb_planar_batched_pl_helper.h`, `Arr_unb_planar_vert_decomp_helper.h`).

### 4.11 Predicates (verbatim declarations, lines 440–459; definitions in the impl)

```cpp
  virtual Comparison_result compare_x(const Point_2& p, const Vertex* v) const;
  virtual Comparison_result compare_xy(const Point_2& p, const Vertex* v) const;
  /*! \pre p should lie in the x-range of the given edge. */
  virtual Comparison_result compare_y_at_x(const Point_2& p, const Halfedge* he) const;
```

* `compare_x`: `ARR_LEFT_BOUNDARY → LARGER`, `ARR_RIGHT_BOUNDARY → SMALLER`; if `ps_y != ARR_INTERIOR`
  it uses `compare_x_point_curve_end_2(p, *v_cv, v_ind)` on the curve inducing `v`; else
  `compare_x_2(p, v->point())`.
* `compare_xy`: same, and on `EQUAL` at a `ps_y != ARR_INTERIOR` vertex returns
  `(ps_y == ARR_BOTTOM_BOUNDARY) ? LARGER : SMALLER`.
* `compare_y_at_x`: if the halfedge has a curve, `compare_y_at_x_2(p, he->curve())`; otherwise it is a
  *horizontal* fictitious edge and the answer is `LARGER` for the bottom edge, `SMALLER` for the top.
  It asserts that the edge is **not** one of the two vertical (x = ±∞) fictitious edges.

### 4.12 Protected auxiliaries (verbatim, lines 467–491) — useful to understand, not to bind

```cpp
  /*! obtains the curve associated with a boundary vertex.
   * \param ind Output: ARR_MIN_END if the vertex is induced by the minimal end;
   *                    ARR_MAX_END if it is induced by the curve's maximal end.
   * \pre v is a valid (not fictitious) boundary.
   * \return The curve that induces v, or nullptr if v has no incident curves yet. */
  const X_monotone_curve_2* _curve(const Vertex* v, Arr_curve_end& ind) const;

  /*! checks whether the given infinite curve end lies on the given fictitious
   * halfedge. ...
   * \param eq_source Output: Whether the curve coincides with he's source.
   * \param eq_target Output: Whether the curve coincides with he's target.
   * \return Whether the curve end lies on the fictitious halfedge. */
  bool _is_on_fictitious_edge(const X_monotone_curve_2& cv, Arr_curve_end ind,
                               Arr_parameter_space ps_x,
                               Arr_parameter_space ps_y,
                               const Halfedge* he,
                               bool& eq_source, bool& eq_target);
```

`_curve` is the canonical **"how do I get the geometry of a vertex at infinity"** routine, and a binding
should reimplement it (it is protected):

```cpp
  const Halfedge* he = v->halfedge();
  while (he->has_null_curve()) {
    he = he->next()->opposite();
    if (he == v->halfedge()) return (nullptr);   // no incident curve yet
  }
  // he is directed toward v: L2R => v is the MAX end of the curve, else the MIN end.
  ind = (he->direction() == ARR_LEFT_TO_RIGHT) ? ARR_MAX_END : ARR_MIN_END;
  return &(he->curve());
```

Note `_is_on_fictitious_edge` is **non-const** (an oversight — it only reads), which is why
`place_boundary_vertex` and `locate_curve_end` are non-const too.

---

## 5. The initial DCEL of an empty **unbounded** arrangement **[verified]**

`init_dcel()` verbatim (impl lines 137–241), including its own diagram:

```cpp
  // Clear the current DCEL.
  this->m_dcel.delete_all();

  // Create the fictitious unbounded face.
  fict_face = this->m_dcel.new_face();
  fict_face->set_unbounded (true);
  fict_face->set_fictitious (true);

  // Create the four fictitious vertices corresponding to corners of the bounding rectangle.
  v_bl = this->m_dcel.new_vertex();  v_bl->set_boundary (ARR_LEFT_BOUNDARY,  ARR_BOTTOM_BOUNDARY);
  v_tl = this->m_dcel.new_vertex();  v_tl->set_boundary (ARR_LEFT_BOUNDARY,  ARR_TOP_BOUNDARY);
  v_br = this->m_dcel.new_vertex();  v_br->set_boundary (ARR_RIGHT_BOUNDARY, ARR_BOTTOM_BOUNDARY);
  v_tr = this->m_dcel.new_vertex();  v_tr->set_boundary (ARR_RIGHT_BOUNDARY, ARR_TOP_BOUNDARY);

  //                            he2
  //             v_tl (.) ----------------> (.) v_tr
  //                   ^ <------------------
  //                   ||                   ^|
  //  fict_face    he1 ||        in_f       ||
  //                   ||                   || he3
  //                   |V                   ||
  //                     ------------------> V
  //             v_bl (.) <---------------- (.) v_br
  //                             he4

  Halfedge *he1 = new_edge(); Halfedge *he1_t = he1->opposite();   // and he2..he4 likewise
  Outer_ccb *oc = new_outer_ccb();  Inner_ccb *ic = new_inner_ccb();  Face *in_f = new_face();

  he1->set_curve(nullptr); he2->set_curve(nullptr); he3->set_curve(nullptr); he4->set_curve(nullptr);

  he1->set_next (he2);        he1_t->set_next (he4_t);
  he2->set_next (he3);        he4_t->set_next (he3_t);
  he3->set_next (he4);        he3_t->set_next (he2_t);
  he4->set_next (he1);        he2_t->set_next (he1_t);

  he1->set_vertex (v_tl);     he1_t->set_vertex (v_bl);
  he2->set_vertex (v_tr);     he2_t->set_vertex (v_tl);
  he3->set_vertex (v_br);     he3_t->set_vertex (v_tr);
  he4->set_vertex (v_bl);     he4_t->set_vertex (v_br);

  oc->set_face (in_f);        ic->set_face (fict_face);

  he1->set_inner_ccb (ic);       he1_t->set_outer_ccb (oc);   // ... same for he2..he4
  v_bl->set_halfedge (he1_t);  v_tl->set_halfedge (he2_t);
  v_tr->set_halfedge (he3_t);  v_br->set_halfedge (he4_t);

  he1->set_direction (ARR_LEFT_TO_RIGHT);
  he2->set_direction (ARR_LEFT_TO_RIGHT);
  he3->set_direction (ARR_RIGHT_TO_LEFT);
  he4->set_direction (ARR_RIGHT_TO_LEFT);

  fict_face->add_inner_ccb (ic, he1);
  in_f->add_outer_ccb (oc, he1_t);
  in_f->set_unbounded (true);
  n_inf_verts = 4;
```

### 5.1 Verified initial state (`Arrangement_2<Arr_linear_traits_2<Epeck>> arr;`)

```
DCEL: V=4  HE=8  F=2  OCCB=1  ICCB=1
arr:  is_empty=1  number_of_vertices=0  vertices_at_infinity=0  edges=0  halfedges=0
      faces=1  unbounded_faces=1  isolated_vertices=0
tt:   concrete_v=0  valid_v=0  valid_he=0  valid_f=1  is_empty_dcel=1
fict_face  is_fictitious=1  is_unbounded=1  #outer_ccbs=0  #inner_ccbs=1
unbounded_face()  is_fictitious=0   unbounded_face() == fictitious_face() -> FALSE
tt->fictitious_face() == tt->initial_face()   (same pointer)
tt->reference_face() == the single real unbounded face
arr.is_valid() = 1
```

So the header's `is_empty_dcel()` claim (4 vertices, 8 halfedges) is **confirmed**. Also confirmed:
`arr.is_empty()` is `true` even though the DCEL is not.

### 5.2 The fictitious rectangle, walked **[verified]**

`fict_face`'s single **inner** CCB (the hole), in `next()` order:

| # | fictitious | direction | source (psx, psy) | target (psx, psy) |
|---|---|---|---|---|
| 0 | yes | `ARR_LEFT_TO_RIGHT` | `v_bl` (LEFT, BOTTOM) | `v_tl` (LEFT, TOP) |
| 1 | yes | `ARR_LEFT_TO_RIGHT` | `v_tl` (LEFT, TOP) | `v_tr` (RIGHT, TOP) |
| 2 | yes | `ARR_RIGHT_TO_LEFT` | `v_tr` (RIGHT, TOP) | `v_br` (RIGHT, BOTTOM) |
| 3 | yes | `ARR_RIGHT_TO_LEFT` | `v_br` (RIGHT, BOTTOM) | `v_bl` (LEFT, BOTTOM) |

The real unbounded face's **outer** CCB is the twin cycle, traversed the other way:

| # | fictitious | direction | source | target |
|---|---|---|---|---|
| 0 | yes | `ARR_RIGHT_TO_LEFT` | `v_tl` | `v_bl` |
| 1 | yes | `ARR_LEFT_TO_RIGHT` | `v_bl` | `v_br` |
| 2 | yes | `ARR_LEFT_TO_RIGHT` | `v_br` | `v_tr` |
| 3 | yes | `ARR_RIGHT_TO_LEFT` | `v_tr` | `v_tl` |

Every corner vertex has `is_at_open_boundary() == true` and `degree() == 2`. Every fictitious halfedge's
twin is also fictitious, and exactly one of the two lies on `fict_face` **[verified]**:

```
fict he direction=ARR_RIGHT_TO_LEFT  twin fict=1  twin face fict=1
fict he direction=ARR_LEFT_TO_RIGHT  twin fict=1  twin face fict=1
```

The construction helper (`Arr_unb_planar_construction_helper::before_sweep()`) restates the same layout
with the *unbounded-face-side* halfedges it caches:

```
//              m_th
//  v_tl (.)<----------(.) v_tr
//        |             ^
//   m_lh |             | m_rh
//        v             |
//  v_bl (.)---------->(.) v_br
//              m_bh
```
with asserted directions `m_lh = R2L`, `m_bh = L2R`, `m_rh = L2R`, `m_th = R2L`, and
`m_?h->face() != fict_face` for all four.

### 5.3 How the rectangle grows

Each **curve end at infinity** encountered by the sweep causes
`Arr_accessor::create_boundary_vertex(xc, ind, ps_x, ps_y, false)` followed by
`Arr_accessor::split_fictitious_edge(m_?h, v_at_inf)` → `topology_traits()->split_fictitious_edge()`.
Which of the four sides is split is decided in `Arr_unb_planar_construction_helper::before_handle_event`
by `switch (ps_x)` **first** (`ARR_LEFT_BOUNDARY → m_lh`, `ARR_RIGHT_BOUNDARY → m_rh`), then
`switch (ps_y)` (`ARR_BOTTOM_BOUNDARY → m_bh`, `ARR_TOP_BOUNDARY → m_th`). Hence gotcha 5: a
LEFT+TOP curve end lands on the **left** edge.

Net effect per curve end at infinity: `+1` DCEL vertex, `+2` DCEL halfedges, `n_inf_verts += 1`, and the
rectangle cycle grows by one halfedge. Measured **[verified]**, inserting full `Line_2`s into
`Arr_linear_traits_2<Epeck>`:

| state | DCEL V | DCEL HE | DCEL F | `n_inf_verts` (= rectangle-cycle length) | `arr` V / Vinf / E / F / UF |
|---|---|---|---|---|---|
| empty | 4 | 8 | 2 | 4 | 0 / 0 / 0 / 1 / 1 |
| + `y=0` | 6 | 14 | 3 | 6 | 0 / 2 / 1 / 2 / 2 |
| + `x=0` | 9 | 24 | 5 | 8 | 1 / 4 / 4 / 4 / 4 |
| + `x+y=1` | 13 | 38 | 8 | 10 | 3 / 6 / 9 / 7 / 6 |

(`n_inf_verts` is not directly exposed; it equals `size_of_halfedges()/2 - number_of_edges()` and equals
the measured length of `fict_face`'s inner CCB: 4, 6, 8, 10 **[verified]**.)

The rectangle cycle after 2 lines (`y=0`, `x=0`), verbatim from the probe:

```
v_left(LEFT,INTERIOR) --L2R--> v_tl(LEFT,TOP) --L2R--> v_top(INTERIOR,TOP) --L2R--> v_tr(RIGHT,TOP)
   --R2L--> v_right(RIGHT,INTERIOR) --R2L--> v_br(RIGHT,BOTTOM) --R2L--> v_bottom(INTERIOR,BOTTOM)
   --R2L--> v_bl(LEFT,BOTTOM) --L2R--> v_left
```

### 5.4 Fictitious halfedges on an **unbounded face's outer CCB** **[verified]**

Because each fictitious halfedge of the rectangle has its twin on exactly one unbounded face's outer
CCB, **the fictitious halfedges are partitioned among the unbounded faces** and their total is always
`n_inf_verts`.

| after | # unbounded faces | outer-CCB length (real + fictitious), per face | Σ fictitious |
|---|---|---|---|
| 0 lines | 1 | 4 = 0 real + **4 fict** | 4 |
| 1 line (`y=0`) | 2 | 4 = 1 real + **3 fict** (both faces) | 6 |
| 2 lines (`y=0`,`x=0`) | 4 | 4 = 2 real + **2 fict** (all four quadrants) | 8 |
| 3 lines (+`x+y=1`) | 6 unbounded + 1 bounded triangle | 5=3+**2**, 4=2+**2**, 4=3+**1**, 4=2+**2**, 5=3+**2**, 3=2+**1**; triangle 3=3+**0** | 10 |

So an unbounded face can have as few as **one** fictitious halfedge on its boundary, and the "number of
fictitious halfedges" is *not* a constant you can rely on. What **is** invariant:

* a face is unbounded ⟺ its outer CCB contains ≥ 1 fictitious halfedge (that is literally
  `is_unbounded()`'s implementation);
* the bounded faces have **zero** fictitious halfedges and `f->is_unbounded() == false`;
* `arr.is_valid()` was `true` in every state **[verified]**.

---

## 6. The initial DCEL of an empty **bounded** arrangement **[verified]**

`init_dcel()` verbatim (impl lines 44–55):

```cpp
  // Clear the current DCEL.
  this->m_dcel.delete_all();

  // Create the unbounded face.
  unb_face = this->m_dcel.new_face();

  unb_face->set_unbounded(true);
  unb_face->set_fictitious(false);
```

Measured for `Arrangement_2<Arr_segment_traits_2<Epeck>> arr;`:

```
EMPTY:
DCEL: V=0  HE=0  F=1  OCCB=0  ICCB=0
arr:  is_empty=1  V=0  Vinf=0  E=0  HE=0  F=1  UF=1
tt:   concrete_v=0  valid_v=0  valid_he=0  valid_f=1  is_empty_dcel=1
fict_face  is_fictitious=0  is_unbounded=1  #outer_ccbs=0  #inner_ccbs=0
unbounded_face() == fictitious_face()   ->  TRUE          <-- arrangement_core.md gotcha 6 CONFIRMED
fictitious_face()->is_fictitious()      ->  FALSE         <--            "                  CONFIRMED
tt->unbounded_face() == tt->initial_face() == tt->reference_face()  (one and the same pointer)
tt->is_unbounded(unb_face)=1   tt->is_valid_face(unb_face)=1
arr.is_valid() = 1
```

After one segment `(0,0)–(1,0)`:
`DCEL V=2 HE=2 F=1 OCCB=0 ICCB=1`; `arr V=2 E=1 F=1 UF=1`; the unbounded face gains an **inner** CCB
(the "antenna") and still has **zero outer CCBs**.

After closing a triangle:
`DCEL V=3 HE=6 F=2 OCCB=1 ICCB=1`; `arr V=3 E=3 F=2 UF=1`; face 0 = unbounded (`#occb=0 #iccb=1`),
face 1 = the triangle (`#occb=1 #iccb=0`, `is_unbounded()==false`, `tt->is_unbounded()==false`).

In every state: **zero** fictitious halfedges reachable through `arr.halfedges_begin()..end()`, **zero**
open-boundary vertices through `arr.vertices_begin()..end()`, and `unbounded_face()->number_of_outer_ccbs() == 0`
**[verified]**.

> Practical consequence for a renderer/binding: under the bounded topology, the distinguished unbounded
> face is **the unique face with no outer CCB**, exactly as on the sphere. `Face::outer_ccb()` would fire
> its `CGAL_precondition(number_of_outer_ccbs() == 1)` on it; iterate
> `outer_ccbs_begin()/outer_ccbs_end()` instead (which is a valid empty range).

---

## 7. Reaching the topology traits from a binding

### 7.1 Exact declarations (`Arrangement_on_surface_2.h`, lines 68, 109, 962–967)

```cpp
  typedef TopTraits_                                      Topology_traits;
  typedef typename Topology_traits::Dcel            Dcel;

  /*! accesses the topology-traits object (non-const version). */
  inline Topology_traits* topology_traits()
  { return (&m_topol_traits); }

  /*! accesses the topology-traits object (const version). */
  inline const Topology_traits* topology_traits() const
  { return (&m_topol_traits); }
```

`m_topol_traits` is a **by-value** protected data member (`Topology_traits m_topol_traits;`), so the
returned pointer is never null and never dangles while the arrangement is alive. There is also
`inline const Traits_adaptor_2* traits_adaptor() const` and
`inline const Geometry_traits_2* geometry_traits() const` next to it.

A separate, unrelated `topology_traits()` exists at line 259 on the internal `_Is_valid_face`-style
filter functor — ignore it.

`Arrangement_2` re-exports the typedef (`typedef typename Default_topology::Traits Topology_traits;`) and
inherits both accessors unchanged.

### 7.2 What the arrangement itself routes through the topology traits

```cpp
  bool is_empty() const           { return (m_topol_traits.is_empty_dcel()); }
  Size number_of_vertices() const { return (m_topol_traits.number_of_concrete_vertices()); }
  Size number_of_halfedges() const{ return (m_topol_traits.number_of_valid_halfedges()); }
  Size number_of_edges() const    { return (m_topol_traits.number_of_valid_halfedges() / 2); }
  Size number_of_faces() const    { return (m_topol_traits.number_of_valid_faces()); }

  Face_const_handle reference_face() const
  { return _const_handle_for(this->topology_traits()->reference_face()); }
  Face_handle reference_face()
  { return _handle_for(this->topology_traits()->reference_face()); }

  Face_handle fictitious_face()
  { return Face_handle(const_cast<DFace*>(this->topology_traits()->initial_face())); }
  Face_const_handle fictitious_face() const
  { return DFace_const_iter(this->topology_traits()->initial_face()); }
```

and (`Arrangement_2.h`, lines 168–213), only on `Arrangement_2`:

```cpp
  Size number_of_vertices_at_infinity() const
  {
    // The vertices at infinity are valid, but not concrete:
    return (this->topology_traits()->number_of_valid_vertices() -
            this->topology_traits()->number_of_concrete_vertices());
  }

  Face_handle unbounded_face ()
  {
    typename Base::DFace *un_face =
      const_cast<typename Base::DFace*>(this->topology_traits()->initial_face());
    if (! un_face->is_fictitious()) return (Face_handle (un_face));
    typename Base::DHalfedge  *p_he = *(un_face->inner_ccbs_begin());
    typename Base::DHalfedge  *p_opp = p_he->opposite();
    typename Base::DOuter_ccb *p_oc = p_opp->outer_ccb();
    return (Face_handle (p_oc->face()));
  }
```

(and the `const` twin). This is the source of gotcha 4.

### 7.3 `initial_face()` / `reference_face()` mean different things

| | bounded traits | unbounded traits |
|---|---|---|
| `initial_face()` | the unbounded face (`unb_face`) | **the fictitious face** (`fict_face`) |
| `reference_face()` | the unbounded face | the unbounded face incident to `v_tr` |
| `arr.fictitious_face()` | == `arr.unbounded_face()`, `is_fictitious()==false` | the real fictitious face, `is_fictitious()==true` |
| `arr.unbounded_face()` | *the* unbounded face | an arbitrary, drifting unbounded face |

### 7.4 Safe-to-call-from-a-read-only-binding matrix

`arr.topology_traits()` on a `const Arrangement_2&` gives `const Topology_traits*`, which mechanically
restricts you to the `const` members. Passing arrangement handles in is fine:
`&*face_const_handle` is a `const Arrangement::Face*` and upcasts implicitly to `const Dcel::Face*`
**[verified: `tt->is_unbounded(&*fh)` compiles and runs]**.

| method | const? | read-only safe? | note |
|---|---|---|---|
| `dcel() const` | yes | ✔ | gives `size_of_vertices/halfedges/faces/outer_ccbs/inner_ccbs` incl. fictitious |
| `is_empty_dcel()` | yes | ✔ | |
| `is_concrete_vertex`, `is_valid_vertex`, `is_valid_halfedge`, `is_valid_face` | yes | ✔ | bounded versions ignore the argument |
| `number_of_{concrete_vertices,valid_vertices,valid_halfedges,valid_faces}` | yes | ✔ | O(1) |
| `is_unbounded(const Face*)` | yes | ✔ **except** on the fictitious face (unbounded traits: 0 outer CCBs → UB) | prefer `f->is_unbounded()` on the DCEL face, which is a stored bit |
| `is_redundant(const Vertex*)` | yes | ✔ | unbounded: `CGAL_precondition` the vertex is not a corner |
| `are_equal(...)` | yes | ✔ (pure query) | bounded version asserts both parameter spaces are `ARR_INTERIOR` |
| `initial_face() const`, `reference_face() const` | yes | ✔ | see §7.3 |
| `unbounded_face() const` (bounded only) | yes | ✔ | |
| `fictitious_face() const`, `bottom_left_vertex() const`, `top_left_vertex() const`, `bottom_right_vertex() const`, `top_right_vertex() const` (unbounded only) | yes | ✔ | |
| `compare_x`, `compare_xy`, `compare_y_at_x` | yes (virtual) | ✔ | `compare_y_at_x` `\pre` p in the edge's x-range |
| `let_me_decide_the_outer_ccb`, `face_split_after_edge_insertion` | yes | ✔ (constant answers) | |
| `is_in_face(f, p, v)` | yes | ⚠ | O(|CCB|) ray shooting; do not pass the fictitious face |
| `locate_around_boundary_vertex` | yes | ✘ | `CGAL_error()` in **both** classes |
| `assign`, `init_dcel`, `dcel_updated` | no | ✘ | `init_dcel` is destructive; `dcel_updated` only after manual DCEL surgery |
| `place_boundary_vertex`, `locate_curve_end` | no | ✘ | bounded: `CGAL_error()`. unbounded: queries only, but non-const and `CGAL_error()` on miss |
| `split_fictitious_edge`, `erase_redundant_vertex` | no | ✘ | mutate the DCEL without touching observers/point/curve storage |
| non-const `dcel()`, `unbounded_face()`, `fictitious_face()`, the four corner accessors | no | ✘ from a const binding | identical values to the const versions |

**Rule of thumb for the Cython layer:** expose exactly
`is_empty_dcel`, the four `number_of_*`, `is_unbounded`, and (unbounded only) the four corner accessors
+ `fictitious_face`. Everything else is either a constant, a `CGAL_error()` stub, or DCEL surgery.

---

## 8. Rendering an unbounded arrangement (the part CGAL does not do for you)

### 8.1 The problem

`CGAL::draw(arr)` / `CGAL::add_to_graphics_scene(arr, scene)` iterate
`m_aos.unbounded_faces_begin()..unbounded_faces_end()` and then, per CCB, call `find_smallest()` →
`curr->source()->point()` and `draw_region_impl1()` → `approx(curr->curve(), …)`. There is **no**
`is_fictitious()` check anywhere in `draw_arrangement_2.h`, so both calls hit gotcha 9. You must walk
the CCBs yourself.

### 8.2 The walk

For a face `f` (bounded or unbounded), for each CCB in
`f->outer_ccbs_begin()..outer_ccbs_end()` and `f->inner_ccbs_begin()..inner_ccbs_end()`:

1. **Skip** every halfedge with `he->is_fictitious() == true`
   (`Arrangement_on_surface_2::Halfedge::is_fictitious() const { return Base::has_null_curve(); }`).
   Each maximal run of fictitious halfedges is a **gap at infinity**; each maximal run of real
   halfedges is one **drawable chain**.
2. Rotate the circulator so you start *just after* a fictitious halfedge, otherwise the run that wraps
   around the circulator's start point is split in two.
3. A CCB may contain **more than one** gap: the strip between two parallel lines has **2 drawable
   chains** on a single outer CCB **[verified]** (`y=0` and `y=1`, the middle face reports
   `2 drawable chain(s)`). Do not assume one chain per face.
4. For a filled polygon, close each gap by walking your viewport rectangle from the chain's last point
   to the next chain's first point. Outer CCBs are counter-clockwise, inner CCBs (holes) clockwise, so
   walk the viewport border in the matching sense.

### 8.3 What the endpoints report

For a **real** halfedge `he` incident to infinity:

* `he->source()->is_at_open_boundary()` / `he->target()->is_at_open_boundary()` — `true` exactly when
  that end is at infinity. `Vertex::point()` on such a vertex aborts (gotcha 9).
* `v->parameter_space_in_x()` / `v->parameter_space_in_y()` — `Arr_parameter_space` =
  `Box_parameter_space_2`, so the numeric values are
  `ARR_LEFT_BOUNDARY=0, ARR_RIGHT_BOUNDARY=1, ARR_BOTTOM_BOUNDARY=2, ARR_TOP_BOUNDARY=3, ARR_INTERIOR=4`.
  They tell you **which side of the rectangle** the end sits on; remember gotcha 5 (both can be
  non-interior). Measured for `Arr_linear_traits_2<Epeck>` **[verified]**:

  | curve | end | `parameter_space_in_x` | `parameter_space_in_y` |
  |---|---|---|---|
  | `y = 0` (horizontal line) | min / max | LEFT / RIGHT | INTERIOR / INTERIOR |
  | `x = 0` (vertical line) | min / max | INTERIOR / INTERIOR | BOTTOM / TOP |
  | `x + y = 1` (negative slope) | min / max | LEFT / RIGHT | **TOP / BOTTOM** |
  | a segment | min / max | INTERIOR | INTERIOR |

* Traits-independent way to ask the same question about the *curve* rather than the vertex — go through
  the **adaptor**, which synthesises the functors for oblivious traits:
  `arr.traits_adaptor()->parameter_space_in_x_2_object()(cv, CGAL::ARR_MIN_END)`. Verified to compile
  and return `ARR_INTERIOR (4)` for `Arr_segment_traits_2` and the table above for
  `Arr_linear_traits_2` **[verified]**. (`Arr_linear_traits_2` also exposes the functors directly;
  `Arr_segment_traits_2` does **not** — only the adaptor has them.)
* `he->direction()` (`ARR_LEFT_TO_RIGHT == -1`, `ARR_RIGHT_TO_LEFT == 1`) tells you whether the CCB
  traversal runs along the curve's own min→max order or against it.

### 8.4 Getting the ray/line geometry (`Arr_linear_traits_2`)

`X_monotone_curve_2 = Arr_linear_object_2<Kernel>` (derives from `_Linear_object_cached_2`).
The accessors you need, verbatim:

```cpp
  bool is_segment() const;   // ! is_degen && has_source && has_target
  bool is_ray() const;       // ! is_degen && (has_source != has_target)
  bool is_line() const;      // ! is_degen && ! has_source && ! has_target
  Segment_2 segment() const; // \pre is_segment()
  Ray_2     ray() const;     // \pre is_ray()   -- already oriented away from the finite endpoint
  Line_2    line() const;    // \pre is_line()
  const Line_2& supporting_line() const;   // \pre ! is_degenerate()   <-- always available
  const Point_2& source() const;           // \pre ! is_line()
  const Point_2& target() const;           // \pre ! is_line() && ! is_ray()
  Bbox_2 bbox() const;                     // \pre is_segment()        <-- NOT usable on rays/lines

  // from the cached base:
  bool has_left() const;  const Point_2& left() const;   // \pre has_left()
  bool has_right() const; const Point_2& right() const;  // \pre has_right()
  Arr_parameter_space left_infinite_in_x()  const;  Arr_parameter_space left_infinite_in_y()  const;
  Arr_parameter_space right_infinite_in_x() const;  Arr_parameter_space right_infinite_in_y() const;
  bool is_vertical() const;  bool is_degenerate() const;  bool is_directed_right() const;
```

The clean recipe is **`supporting_line()` + `has_left()/left()` + `has_right()/right()`** — it is the
only combination with no precondition you have to branch on, and it covers segment, ray and line
uniformly.

### 8.5 Worked, verified recipe

```cpp
// Clip the supporting line of a curve to a viewport box, then trim by whichever
// endpoints are finite, then orient along the halfedge's traversal direction.
static bool drawable(Arr::Halfedge_const_handle he, const Box& box, P& from, P& to) {
  const Xcv& cv = he->curve();                 // caller has checked ! he->is_fictitious()
  P lo, hi;
  auto r = CGAL::intersection(cv.supporting_line(), box);   // std::optional<std::variant<P,S>>
  if (!r) return false;
  const S* s = std::get_if<S>(&*r);
  if (!s) return false;                        // line only touches a corner
  lo = s->source(); hi = s->target();
  if (lo.x() > hi.x() || (lo.x() == hi.x() && lo.y() > hi.y())) std::swap(lo, hi);
  P pl = cv.has_left()  ? cv.left()  : lo;
  P pr = cv.has_right() ? cv.right() : hi;
  if (he->direction() == CGAL::ARR_LEFT_TO_RIGHT) { from = pl; to = pr; }
  else                                            { from = pr; to = pl; }
  return true;
}

// Split one CCB into drawable chains.
auto circ = *ccb_it, s = circ;
bool any_fict = false;
do { if (circ->is_fictitious()) { any_fict = true; break; } } while (++circ != s);
if (any_fict) { s = circ; ++s; circ = s; }     // start just after a fictitious halfedge
std::vector<std::vector<P>> chains;  std::vector<P> cur;
auto c = circ; s = circ;
do {
  if (c->is_fictitious()) { if (!cur.empty()) { chains.push_back(cur); cur.clear(); } }
  else {
    P a, b;
    if (drawable(c, box, a, b)) {
      if (cur.empty() || cur.back() != a) cur.push_back(a);
      cur.push_back(b);
    }
  }
} while (++c != s);
if (!cur.empty()) chains.push_back(cur);
```

Verified output for the 3-line arrangement (`y=0`, `x=0`, `x+y=1`) with a `[-10,10]²` viewport
**[verified]**:

```
face 0 [unbounded] : 1 chain  (10.000,-9.000) (1.000,0.000) (0.000,0.000) (0.000,-10.000)
face 1 [bounded]   : 1 chain  (1.000,0.000) (0.000,1.000) (0.000,0.000) (1.000,0.000)   <-- closed
face 2 [unbounded] : 1 chain  (0.000,-10.000) (0.000,0.000) (-10.000,0.000)
face 3 [unbounded] : 1 chain  (-10.000,0.000) (0.000,0.000) (0.000,1.000) (-9.000,10.000)
face 4 [unbounded] : 1 chain  (-9.000,10.000) (0.000,1.000) (0.000,10.000)
face 5 [unbounded] : 1 chain  (0.000,10.000) (0.000,1.000) (1.000,0.000) (10.000,0.000)
face 6 [unbounded] : 1 chain  (10.000,0.000) (1.000,0.000) (10.000,-9.000)
```

and for two parallel lines (`y=0`, `y=1`, `y=2`) **[verified]**:

```
face 0 [unbounded] : 1 chain   (10,0) (-10,0)
face 1 [unbounded] : 2 chains  [(-10,0) (10,0)]  and  [(10,1) (-10,1)]
face 2 [unbounded] : 2 chains  [(-10,1) (10,1)]  and  [(10,2) (-10,2)]
face 3 [unbounded] : 1 chain   (-10,2) (10,2)
```

### 8.6 Extra pitfalls for the renderer

* **Curved traits.** For anything other than lines/segments, use
  `traits->approximate_2_object()(cv, error, out_it, l2r)` (the 4-argument, direction-aware overload
  used by `draw_arrangement_2.h`). All bounded-topology traits have finite endpoints, so no clipping is
  needed for them at all; the clipping problem is specific to `Arr_linear_traits_2` /
  `Arr_rational_function_traits_2` / `Arr_algebraic_segment_traits_2`.
* **Antennas.** A halfedge with `he->face() == he->twin()->face()` is an antenna; CGAL's own drawer
  skips them (`while (curr->face() == curr->twin()->face()) curr = curr->twin()->next();`). Under the
  bounded topology a lone segment produces exactly this (§6).
* **Holes.** Iterate `inner_ccbs_begin()/inner_ccbs_end()` too; the fictitious face is the *only* face
  whose hole is made of fictitious halfedges.
* **Isolated vertices.** `f->isolated_vertices_begin()/end()`; they are always concrete under both
  planar topologies.
* Do **not** use `Arr_linear_object_2::bbox()` on a ray/line — it has `CGAL_precondition(is_segment())`.

---

## 9. The six sweep-helper families (what they are for; you rarely bind them)

Each topology traits exposes them through the alias templates in §3.8 / §4.8; the surface-sweep
visitors instantiate them. They matter to a binding only if you write a custom sweep visitor. All of
them get at the topology traits through `arr->topology_traits()` in their constructor.

| helper | bounded version | unbounded version |
|---|---|---|
| `Arr_*_planar_batched_pl_helper<Gt, Arr, Evt, Crv>` | caches `Face_const_handle m_unb_face = top_traits->unbounded_face()` in `before_sweep()`; `after_handle_event` is a no-op; `top_face()` returns it | caches `Halfedge_const_handle m_top_fict` starting from `top_left_vertex()->incident_halfedges()` (advancing with `->next()->twin()` if it is L2R); `after_handle_event` advances `m_top_fict = m_top_fict->twin()->next()->twin()` on every `ps_y == ARR_TOP_BOUNDARY` open event; `top_face()` returns `m_top_fict->face()` |
| `Arr_*_planar_construction_helper<Gt, Arr, Evt, Crv>` | `before_sweep()` caches `m_unb_face`; `swap_predecessors()` always `false`; `add_subcurve`, `add_subcurve_in_top_face`, `set_halfedge_indices_map` are no-ops; has a `rebind<OtherGt, OtherArr, OtherEvt, OtherSubcurve>` struct | caches the four side halfedges `m_lh/m_bh/m_rh/m_th` and an `Arr_accessor<Arrangement_2> m_arr_access`; `before_handle_event` creates the vertex at infinity and calls `split_fictitious_edge`; `swap_predecessors(event)` returns `ps_x==ARR_INTERIOR && ps_y==ARR_TOP_BOUNDARY`; `top_face()` returns `m_th->face()`; also has `rebind` |
| `Arr_*_planar_insertion_helper` | trivial subclass of the construction helper, only a ctor | overrides `before_sweep()` (re-derives `m_lh/m_bh/m_rh/m_th` from an *already populated* rectangle) and `before_handle_event()` (if the curve is already in the arrangement, just advance the cached side halfedge) |
| `Arr_*_planar_overlay_helper<Gt, ArrRed, ArrBlue, Arr, Evt, Crv>` | caches the red/blue unbounded faces; `red_top_face()/blue_top_face()` | caches red/blue top fictitious halfedges and top-left vertices, advancing them per event colour (`Gt2::RED`, `Gt2::BLUE`, `Gt2::RB_OVERLAP`) |
| `Arr_*_planar_vert_decomp_helper` | `top_object()` and `bottom_object()` both return `Vert_type(m_unb_face)` | tracks `m_top_fict` **and** `m_bottom_fict`; `top_object()/bottom_object()` return those halfedges. `Vert_type = std::optional<std::variant<Vertex_const_handle, Halfedge_const_handle, Face_const_handle>>` |
| `Arr_inc_insertion_zone_visitor<Arr>` (= `Zone_insertion_visitor`, shared) | `typedef std::pair<Halfedge_handle, bool> Result;` `void init(Arrangement_2* arr);` `Result found_subcurve(const X_monotone_curve_2& cv, Face_handle face, Vertex_handle left_v, Halfedge_handle left_he, Vertex_handle right_v, Halfedge_handle right_he);` `Result found_overlap(const X_monotone_curve_2& cv, Halfedge_handle he, Vertex_handle left_v, Vertex_handle right_v);` | identical |

The vertical-decomposition helper's `Cell_type`/`Vert_type` is the CGAL 6 `std::variant`/`std::optional`
pair — the same shape as `Arrangement_on_surface_2`'s `decompose()` output.

`Arr_accessor` is the sanctioned bridge if you ever *do* need the fictitious layer from outside:

```cpp
  Vertex_handle create_boundary_vertex(const Point_2& pt, Arr_parameter_space ps_x,
                                       Arr_parameter_space ps_y, bool notify = true);
  Vertex_handle create_boundary_vertex(const X_monotone_curve_2& cv, Arr_curve_end ind,
                                       Arr_parameter_space ps_x, Arr_parameter_space ps_y,
                                       bool notify = true);
  // \pre One of ps_x or ps_y does not equal ARR_INTERIOR.

  Halfedge_handle split_fictitious_edge(Halfedge_handle e, Vertex_handle v);
  // CGAL_precondition(e->is_fictitious());

  void dcel_updated() { p_arr->topology_traits()->dcel_updated(); }
```

`CGAL/IO/Arrangement_2_reader.h:140` calls `m_arr_access.dcel_updated()` after deserialising — **a
binding that loads an arrangement from a stream must do the same**, or `unb_face` / the four corner
pointers / `fict_face` / `n_inf_verts` stay stale.

---

## 10. Checklist for the type-erased C++ core + Cython layer

1. **Do not put the topology traits in the erased interface.** The two classes have disjoint member
   sets (gotcha 2). Erase at the level of *questions*, not of the traits object:

   ```cpp
   struct ArrView {                       // implemented once per (GeomTraits, TopTraits) pair
     virtual bool  has_unbounded_topology() const = 0;   // std::is_same_v<TT, Arr_unb_planar_...>
     virtual std::size_t num_vertices()  const = 0;      // arr.number_of_vertices()
     virtual std::size_t num_vertices_at_infinity() const = 0;
     virtual std::size_t num_edges()     const = 0;
     virtual std::size_t num_faces()     const = 0;
     virtual std::size_t num_unbounded_faces() const = 0;
     virtual bool  face_is_unbounded(FaceId) const = 0;  // f->is_unbounded()  (stored bit, O(1))
     virtual void  face_chains(FaceId, const Box&, ChainSink&) const = 0;  // §8
   };
   ```
2. **Branch on the topology with `if constexpr`, not with a runtime flag**, inside the concrete
   implementation:
   `if constexpr (std::is_same_v<typename Arr::Topology_traits, CGAL::Arr_unb_planar_topology_traits_2<Gt, Dcel>>) { … }`.
   The corner accessors / `fictitious_face()` only exist in that branch.
3. **Never expose `init_dcel`, `assign`, `split_fictitious_edge`, `erase_redundant_vertex`,
   `place_boundary_vertex`, `locate_curve_end`, `locate_around_boundary_vertex`.** Half of them are
   `CGAL_error()` stubs, the other half are un-observed DCEL surgery.
4. **Guard every geometry access** with `he->is_fictitious()` and `v->is_at_open_boundary()` before
   `he->curve()` / `v->point()`; both abort otherwise (gotcha 9). In a Cython layer that means the
   `Halfedge`/`Vertex` wrappers must expose those two predicates *first*, and `point()`/`curve()` must
   either raise a Python exception or return `None` when the guard fails.
5. **Do not expose `unbounded_face()` from `Arrangement_2`** — it is meaningless under the unbounded
   topology (gotcha 4). Expose `unbounded_faces()` (the filtered iterator range) and, if you need a
   canonical one, `reference_face()`.
6. **`fictitious_face()` is a trap name.** Expose it as `topology_root_face()` or hide it, and expose
   `face.is_fictitious()` so callers can tell (gotcha 3).
7. **Counters:** `number_of_vertices()` excludes vertices at infinity; use
   `Arrangement_2::number_of_vertices_at_infinity()` (bounded topology always returns 0) for the rest.
   `topology_traits()->dcel().size_of_*()` gives the raw, fictitious-inclusive counts if you need them
   for diagnostics.
8. **Rendering** (`lines/rays` requirement): implement §8.5 once, parameterised on a viewport box; keep
   the "chains per CCB" shape (a `list[list[Point]]` per CCB), never a single closed ring, because a
   CCB can have several gaps **[verified]**. Emit the viewport-border closing segments in a separate
   pass if you want filled polygons.
9. **Serialization:** call `Arr_accessor<Arr>(arr).dcel_updated()` after any read that bypasses
   `Arrangement_on_surface_2`'s own machinery.
10. **Lifetime:** the topology traits lives *inside* the arrangement; the DCEL lives inside the topology
    traits. Every `Vertex*/Halfedge*/Face*` and every handle is invalidated by `arr.clear()`,
    `operator=`, and destruction — and by nothing else in normal use. The four corner vertices and the
    fictitious face specifically survive every insertion/removal **[verified]**.
