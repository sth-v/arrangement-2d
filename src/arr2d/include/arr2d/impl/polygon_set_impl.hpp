// arr2d — PolygonSetImpl<Types, GpsTraits, Policy>: the generic implementation of
// PolygonSetBase on top of CGAL::General_polygon_set_2<GpsTraits>.
//
//   template class arr2d::PolygonSetImpl<arr2d::SegmentTypes,
//                                        CGAL::Gps_segment_traits_2<Epeck>,
//                                        SegmentGpsPolicy>;          // in bso_segment.cpp
//
// One instantiation per Boolean-capable kind (segment, circle_segment, conic, bezier);
// the four `bso_<kind>.cpp` TUs supply the `Policy` and define
// `arr2d::make_polygon_set_<kind>()` (declared in arr2d/bso.hpp).
//
// ---------------------------------------------------------------------------------------
// `Policy` requirements (a plain struct defined by each bso_<kind>.cpp):
//
//   static const GpsTraits& gps_traits();
//       The ONE process-wide GPS traits object of this kind.  It must be heap-allocated and
//       deliberately leaked (`static const GpsTraits* p = new GpsTraits{...}; return *p;`)
//       because CGAL::General_polygon_set_2(const Traits_2&) stores &traits and does NOT copy
//       it (boolean_set_operations.md gotcha 4) — the traits has to outlive every set — and
//       because for the conic / Bezier kinds it holds CORE::Expr-backed caches, which abort
//       the process at exit when destroyed from static storage
//       (`! blocks.empty()`, CGAL/CORE/MemoryPool.h:125 — CGAL_TRAPS_CHECKLIST "Process/build").
//
//   static constexpr bool insert_with_history;
//       true  when a boundary X_monotone_curve_2 can be turned into an equivalent general
//             Curve_2 (KindOps::to_curve), so that `to_arrangement()` can use the
//             with-history aggregate insertion;
//       false when that conversion is lossy — the Bezier kind, whose `to_curve()` returns the
//             *supporting* Bezier curve rather than the sub-arc, so inserting it would add
//             geometry that is not on the polygon boundary.  `to_arrangement()` then falls
//             back to insert_non_intersecting_curve() (no history).
//
// ---------------------------------------------------------------------------------------
// CGAL 6.1 traps handled here (docs/dev/cgal61_api/boolean_set_operations.md §0):
//
//   * gotcha 2  the aggregated (range) do_intersect returns the INVERTED answer — only the
//               binary `do_intersect(const Self&)` is used.
//   * gotcha 3  every binary Boolean op deletes and replaces the internal arrangement: no
//               handle, reference or iterator into `arrangement()` is ever cached, and an
//               operation with `other == this` is special-cased (it would be a
//               use-after-free inside CGAL).
//   * gotcha 4  the traits is stored by pointer, not copied  -> Policy::gps_traits().
//   * gotcha 5  orientation (CCW outer / CW holes) is a hard precondition that CGAL never
//               fixes and that silently disappears under -DNDEBUG: every insert() and
//               *_polygon() validates with CGAL::is_valid_polygon[_with_holes] FIRST and
//               throws Error(InvalidArgument) with a message naming the actual defect.
//   * gotcha 6  Ccb_curve_iterator walks the CCB backwards, so construct_polygon() on a
//               contained face yields a CLOCKWISE polygon.  We never call construct_polygon()
//               ourselves — polygons_with_holes() (Arr_bfs_scanner) already applies the
//               "always take the CCB seen from the non-contained side" rule and hands out
//               CCW outer boundaries and CW holes.
//   * gotcha 10 polygons_with_holes()/number_of_polygons_with_holes()/locate() are `const`
//               but mutate the faces' `visited` bit: they are not reentrant / thread safe.
//   * gotcha 12 Gps_traits_2::Equal_2's polygon overloads are buggy — only the *point*
//               overload is used here (chain closure), never the polygon ones.
//
// The orientation / closure helpers deliberately go through the GPS traits and
// CGAL::Gps_traits_adaptor rather than through arr2d::ops(kind), so that a bso_<kind>.o can
// be linked and used without the corresponding kind_<kind>.o.  Only to_arrangement() needs
// the kind registry (it builds an arr2d::ArrImpl of the same kind).
#pragma once

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#include <CGAL/assertions.h>
#include <CGAL/Boolean_set_operations_2/Gps_polygon_validation.h>
#include <CGAL/Boolean_set_operations_2/Gps_traits_adaptor.h>
#include <CGAL/General_polygon_set_2.h>
#include <CGAL/enum.h>

#include "arr2d/arrangement.hpp"
#include "arr2d/common.hpp"
#include "arr2d/polygon_set.hpp"
#include "arr2d/registry.hpp"

namespace arr2d {

namespace ps_detail {

/// True when `P` is a CGAL::General_polygon_2 (a sequence of x-monotone CURVES) and false when
/// it is a CGAL::Polygon_2 (a sequence of POINTS, used by Gps_segment_traits_2).
template <class P, class = void>
struct is_curve_polygon : std::false_type {};
template <class P>
struct is_curve_polygon<P, std::void_t<decltype(std::declval<const P&>().curves_begin())>>
    : std::true_type {};

}  // namespace ps_detail

// ===========================================================================
// PolygonSetImpl
// ===========================================================================
template <class Types, class GpsTraits, class Policy>
class PolygonSetImpl final : public PolygonSetBase {
 public:
  using Self = PolygonSetImpl<Types, GpsTraits, Policy>;
  using Gps = CGAL::General_polygon_set_2<GpsTraits>;
  using Polygon_2 = typename GpsTraits::Polygon_2;
  using Polygon_with_holes_2 = typename GpsTraits::Polygon_with_holes_2;
  using Xcv = typename GpsTraits::X_monotone_curve_2;
  using Pt = typename GpsTraits::Point_2;
  using Adaptor = CGAL::Gps_traits_adaptor<GpsTraits>;

  /// The GPS traits must describe exactly the same geometry as the kind's arrangement traits,
  /// otherwise the Geom boxes handed in and out would hold the wrong C++ type.
  static_assert(std::is_same<Xcv, typename Types::X_monotone_curve_2>::value,
                "GpsTraits::X_monotone_curve_2 must be Types::X_monotone_curve_2");
  static_assert(std::is_same<Pt, typename Types::Point_2>::value,
                "GpsTraits::Point_2 must be Types::Point_2");

  /// A General_polygon_2 stores curves, a CGAL::Polygon_2 stores points.
  static constexpr bool k_curve_polygon = ps_detail::is_curve_polygon<Polygon_2>::value;

  PolygonSetImpl() : m_gps(Policy::gps_traits()) {}
  ~PolygonSetImpl() override = default;

  PolygonSetImpl(const PolygonSetImpl&) = delete;
  PolygonSetImpl& operator=(const PolygonSetImpl&) = delete;

  Kind kind() const override { return Types::kind; }

  std::unique_ptr<PolygonSetBase> clone() const override {
    auto out = std::unique_ptr<Self>(new Self());
    // NOT the CGAL copy constructor: Gps_on_surface_base_2(const Self&) does
    // `m_traits = new Traits_2(*ps.m_traits); m_traits_owner = true;`, which would give every
    // copy its own traits object (a fresh conic intersection cache, a Bezier traits copy that
    // aliases the process-wide cache, ...).  Assigning the *arrangement* instead keeps the
    // destination's traits pointer (Arrangement_on_surface_2::assign() never touches
    // m_geom_traits) and copies the DCEL including Gps_face_base::assign(), i.e. the
    // `contained` flags that ARE the point set (boolean_set_operations.md §6).
    out->m_gps.arrangement() = m_gps.arrangement();
    return out;
  }

  void clear() override { m_gps.clear(); }

  // ---------------------------------------------------------------- validation helpers
  //
  // None of the three throws for a geometrically bad boundary; they do throw Error for a
  // *box* that is not an x-monotone curve of this kind (genuine API misuse).

  /// CGAL's own notion of closure (Gps_polygon_validation.h `is_closed_polygon`): an empty
  /// chain is closed, a single-curve chain is NOT ("a polygon cannot have just a single
  /// edge"), no curve may be degenerate, and every curve's target must be the next curve's
  /// source (the curves are DIRECTED, so source/target follow the traversal, which is what
  /// Gps_traits_adaptor::Construct_vertex_2(cv, 0/1) returns).
  bool is_closed_chain(const std::vector<Geom>& boundary) const override {
    const std::size_t n = boundary.size();
    if (n == 0) return true;
    if (n == 1) return false;
    auto ctr_v = adaptor().construct_vertex_2_object();
    auto eq = gps().equal_2_object();
    for (std::size_t i = 0; i < n; ++i) {
      const Xcv& cur = xcurve(boundary[i]);
      const Xcv& nxt = xcurve(boundary[(i + 1) % n]);
      if (eq(ctr_v(cur, 0), ctr_v(cur, 1))) return false;   // degenerate curve
      if (!eq(ctr_v(cur, 1), ctr_v(nxt, 0))) return false;  // chain broken
    }
    return true;
  }

  /// +1 counterclockwise, -1 clockwise, 0 when undecidable.
  ///
  /// Implemented for EVERY kind (segment included) with
  /// CGAL::Gps_traits_adaptor<GpsTraits>::Orientation_2, the very functor CGAL's own
  /// `has_valid_orientation_polygon()` uses, so that `orientation(ring) > 0` is exactly the
  /// condition insert() validates against.  Deliberately NOT CGAL::Polygon_2::orientation()
  /// for the segment kind: that one goes through CGAL::orientation_2(), whose
  /// `CGAL_precondition(is_simple_2(...))` is ACTIVE in this project's build flags
  /// (-DNDEBUG -DCGAL_DEBUG) and would make orientation() throw for a self-intersecting
  /// boundary, while this method must not throw.  (Verified in tests/test_bso.cpp that the
  /// two agree for simple polygons.)
  ///
  /// Orientation_2 dereferences its "leftmost" iterator unconditionally, so the two
  /// structural preconditions (a closed chain of >= 2 curves that has a local leftmost
  /// vertex) have to be checked before it runs; and for a CLOSED BUT ZERO-AREA ring (e.g.
  /// (0,0)->(1,0)->(2,0)->(0,0)) the two curves leaving the leftmost vertex overlap, so
  /// CGAL's functor trips `CGAL_assertion(res != EQUAL)`
  /// (Boolean_set_operations_2/Gps_traits_adaptor.h:164/167/179) — it would THROW here (this
  /// build keeps CGAL checks on) and silently return COUNTERCLOCKWISE with them off.
  ///
  /// The leftmost-vertex search below is therefore CGAL's own algorithm, replicated verbatim
  /// with every comparison CGAL merely *asserts* on turned into `return 0` ("undecidable").
  /// For a ring CGAL accepts the two agree by construction.
  int orientation(const std::vector<Geom>& boundary) const override {
    const std::size_t n = boundary.size();
    if (n < 2) return 0;
    if (!is_closed_chain(boundary)) return 0;
    std::vector<Xcv> ring;
    ring.reserve(n);
    for (const Geom& g : boundary) ring.push_back(xcurve(g));

    auto cmp_xy = adaptor().compare_xy_2_object();
    auto cmp_y_at_x_right = adaptor().compare_y_at_x_right_2_object();
    auto ctr_v = adaptor().construct_vertex_2_object();
    auto cmp_endpoints = adaptor().compare_endpoints_xy_2_object();

    const std::size_t none = n;            // CGAL's `end`
    std::size_t from_leftmost = none, into_leftmost = none;
    std::size_t into = n - 1;
    try {
      for (std::size_t from = 0; from < n; ++from) {
        // Only a vertex that is *entered* from the right and *left* towards the right can be
        // the leftmost one.
        if (cmp_endpoints(ring[from]) != CGAL::SMALLER ||
            cmp_endpoints(ring[into]) != CGAL::LARGER) { into = from; continue; }
        if (from_leftmost == none) {
          from_leftmost = from; into_leftmost = into; into = from; continue;
        }
        const CGAL::Comparison_result res_xy =
            cmp_xy(ctr_v(ring[from], 0), ctr_v(ring[from_leftmost], 0));
        if (res_xy == CGAL::LARGER) { into = from; continue; }
        if (res_xy == CGAL::SMALLER) {
          from_leftmost = from; into_leftmost = into; into = from; continue;
        }
        // Two candidate vertices coincide: CGAL asserts the three comparisons are decisive.
        const Pt& v = ctr_v(ring[from_leftmost], 0);
        const CGAL::Comparison_result from_lm_into = cmp_y_at_x_right(ring[from_leftmost], ring[into], v);
        const CGAL::Comparison_result into_lm_from = cmp_y_at_x_right(ring[into_leftmost], ring[from], v);
        const CGAL::Comparison_result into_from = cmp_y_at_x_right(ring[into_leftmost], ring[from_leftmost], v);
        if (from_lm_into == CGAL::EQUAL || into_lm_from == CGAL::EQUAL ||
            into_from == CGAL::EQUAL || from_lm_into == into_lm_from)
          return 0;                        // overlapping curves at the leftmost vertex
        if (into_from == from_lm_into) { from_leftmost = from; into_leftmost = into; }
        into = from;
      }
      if (from_leftmost == none) return 0;      // no local leftmost vertex
      const Pt& v = ctr_v(ring[from_leftmost], 0);
      const CGAL::Comparison_result res = cmp_y_at_x_right(ring[into_leftmost], ring[from_leftmost], v);
      if (res == CGAL::EQUAL) return 0;         // zero-area / self-overlapping ring
      return (res == CGAL::SMALLER) ? -1 : 1;   // SMALLER == CLOCKWISE
    } catch (const CGAL::Failure_exception&) {
      return 0;                                 // a traits precondition on a degenerate ring
    }
  }

  /// CGAL::is_valid_polygon (no holes, bounded) or CGAL::is_valid_polygon_with_holes.
  /// Both print CGAL warnings to stderr for the failing rule; that is accepted (they are the
  /// only diagnostics CGAL offers) and no exception is thrown.
  bool is_valid_polygon(const PolygonGeom& p) const override {
    if (!structurally_ok(p)) return false;
    if (is_plain_polygon(p)) {
      Polygon_2 pgn;
      to_polygon(p.outer, pgn);
      return CGAL::is_valid_polygon<GpsTraits>(pgn, gps());
    }
    const Polygon_with_holes_2 pwh = to_pwh(p);
    return CGAL::is_valid_polygon_with_holes<GpsTraits>(pwh, gps());
  }

  // ---------------------------------------------------------------- construction
  void insert(const PolygonGeom& p) override {
    // CGAL preconditions of Gps_on_surface_base_2::insert():
    //   (a) the polygon is valid (closed, (relatively) simple, CCW outer / CW holes) —
    //       checked here so the caller gets Error(InvalidArgument) with a useful message
    //       instead of a CGAL::Precondition_exception (or, under -DNDEBUG, silent garbage);
    //   (b) the polygon is COMPLETELY DISJOINT from what the set already contains
    //       (_insert() only ever uses non-intersecting insertion).  That one is NOT checked
    //       here: it costs a full Boolean operation.  It stays a documented CGAL
    //       precondition — use join_polygon() when the input may overlap.
    insert_checked(p, "insert");
  }

  void insert_polygons(const std::vector<PolygonGeom>& ps) override {
    if (ps.empty()) return;
    // Same two preconditions as insert(), plus: the polygons must also be pairwise disjoint
    // (CGAL uses insert_non_intersecting_curves + a BFS face initialisation).  Documented,
    // not checked.  Everything goes through the polygon-with-holes validation, exactly like
    // CGAL's own range insert (its elements are Polygon_with_holes_2, so its
    // ValidationPolicy resolves to is_valid_polygon_with_holes).
    if (needs_sweep_precheck()) {
      std::vector<Geom> all;
      for (const PolygonGeom& p : ps) collect_polygon_curves(p, all);
      reject_unsweepable(all);
    }
    std::vector<Polygon_with_holes_2> pwhs;
    pwhs.reserve(ps.size());
    for (const PolygonGeom& p : ps) pwhs.push_back(checked_pwh(p, "insert_polygons"));
    m_gps.insert(pwhs.begin(), pwhs.end());
  }

  // ---------------------------------------------------------------- Boolean operations
  void join(const PolygonSetBase& other) override {
    const Self& o = same_kind(other, "join");
    if (&o == this) return;  // A | A == A; CGAL would delete m_arr while reading it
    reject_unsweepable_with_set(o);
    m_gps.join(o.m_gps);
  }

  void intersection(const PolygonSetBase& other) override {
    const Self& o = same_kind(other, "intersection");
    if (&o == this) return;  // A & A == A
    reject_unsweepable_with_set(o);
    m_gps.intersection(o.m_gps);
  }

  void difference(const PolygonSetBase& other) override {
    const Self& o = same_kind(other, "difference");
    if (&o == this) { m_gps.clear(); return; }  // A - A == empty
    reject_unsweepable_with_set(o);
    m_gps.difference(o.m_gps);
  }

  void symmetric_difference(const PolygonSetBase& other) override {
    const Self& o = same_kind(other, "symmetric_difference");
    if (&o == this) { m_gps.clear(); return; }  // A ^ A == empty
    reject_unsweepable_with_set(o);
    m_gps.symmetric_difference(o.m_gps);
  }

  void complement() override { m_gps.complement(); }

  // The single-polygon overloads of CGAL's Boolean operations build a temporary
  // `Aos_2 second_arr;` — a DEFAULT-constructed arrangement, i.e. one with its own,
  // default-constructed traits object.  For the conic / Bezier kinds that would silently
  // introduce a second Bezier_cache / conic intersection cache.  We therefore always route
  // through a temporary set built with Policy::gps_traits() and use the set-vs-set form,
  // which allocates its result arrangement as `new Aos_2(m_traits)`.
  void join_polygon(const PolygonGeom& p) override {
    Self tmp;
    tmp.insert_checked(p, "join_polygon");
    reject_unsweepable_with_set(tmp);
    m_gps.join(tmp.m_gps);
  }
  void intersection_polygon(const PolygonGeom& p) override {
    Self tmp;
    tmp.insert_checked(p, "intersection_polygon");
    reject_unsweepable_with_set(tmp);
    m_gps.intersection(tmp.m_gps);
  }
  void difference_polygon(const PolygonGeom& p) override {
    Self tmp;
    tmp.insert_checked(p, "difference_polygon");
    reject_unsweepable_with_set(tmp);
    m_gps.difference(tmp.m_gps);
  }
  void symmetric_difference_polygon(const PolygonGeom& p) override {
    Self tmp;
    tmp.insert_checked(p, "symmetric_difference_polygon");
    reject_unsweepable_with_set(tmp);
    m_gps.symmetric_difference(tmp.m_gps);
  }

  // ---------------------------------------------------------------- queries
  std::size_t number_of_polygons_with_holes() const override {
    return static_cast<std::size_t>(m_gps.number_of_polygons_with_holes());
  }
  bool is_empty() const override { return m_gps.is_empty(); }
  bool is_plane() const override { return m_gps.is_plane(); }

  void polygons_with_holes(std::vector<PolygonGeom>& out) const override {
    out.clear();
    std::vector<Polygon_with_holes_2> pwhs;
    m_gps.polygons_with_holes(std::back_inserter(pwhs));
    out.reserve(pwhs.size());
    for (const Polygon_with_holes_2& pwh : pwhs) {
      out.emplace_back();
      from_pwh(pwh, out.back());
    }
  }

  int oriented_side(const Geom& point) const override {
    require_point(point, Types::kind, "query point");
    return static_cast<int>(m_gps.oriented_side(point.template as<Pt>()));
  }

  int oriented_side_of_set(const PolygonSetBase& other) const override {
    const Self& o = same_kind(other, "oriented_side");
    if (&o == this) return is_empty() ? -1 : 1;  // CGAL would still be correct; this is cheaper
    reject_unsweepable_with_set(o);              // oriented_side(set) is a full sweep
    return static_cast<int>(m_gps.oriented_side(o.m_gps));
  }

  bool locate(const Geom& point, PolygonGeom& out) const override {
    require_point(point, Types::kind, "query point");
    Polygon_with_holes_2 pwh;
    if (!m_gps.locate(point.template as<Pt>(), pwh)) return false;
    from_pwh(pwh, out);
    return true;
  }

  /// Only the BINARY form: the range overload of CGAL's do_intersect returns the inverted
  /// answer (boolean_set_operations.md gotcha 2).
  bool do_intersect(const PolygonSetBase& other) const override {
    const Self& o = same_kind(other, "do_intersect");
    if (&o == this) return !is_empty();
    reject_unsweepable_with_set(o);              // do_intersect(set) is a full sweep
    return m_gps.do_intersect(o.m_gps);
  }

  /// Gps_on_surface_base_2::is_valid() is NOT const (it is a full O(E) sweep over the
  /// arrangement that only reads it); const_cast is safe and keeps our interface const.
  bool is_valid() const override { return const_cast<Gps&>(m_gps).is_valid(); }

  // ---------------------------------------------------------------- arrangement bridge
  std::unique_ptr<ArrBase> to_arrangement(std::vector<FH>& contained) const override {
    contained.clear();
    // arr2d::make_arrangement() needs kind_<kind>.cpp to be linked (it throws
    // Error(Unsupported) otherwise); the Boolean part of this class does not.
    std::unique_ptr<ArrBase> arr = arr2d::make_arrangement(Types::kind);

    std::vector<PolygonGeom> pgs;
    polygons_with_holes(pgs);

    std::vector<Geom> curves;
    bool any_unbounded = false;
    for (const PolygonGeom& pg : pgs) {
      if (pg.unbounded || pg.outer.empty()) any_unbounded = true;
      for (const Geom& g : pg.outer) curves.push_back(g);
      for (const std::vector<Geom>& hole : pg.holes)
        for (const Geom& g : hole) curves.push_back(g);
    }

    std::unordered_set<const void*> seen;
    auto add_face = [&](FH f) {
      if (f.p != nullptr && seen.insert(f.p).second) contained.push_back(f);
    };

    // An unbounded polygon-with-holes (what complement() produces, and the whole plane) has
    // an EMPTY outer boundary: its region is the one reaching infinity, i.e. the arrangement's
    // unbounded face.  Its holes ARE inserted below, and the "face to the left of a correctly
    // directed boundary curve" rule below finds the same face again — add_face() dedups.
    if (any_unbounded) add_face(arr->unbounded_face());

    if (curves.empty()) return arr;

    // One representative halfedge per boundary curve, collected DURING insertion but resolved
    // to a face only AFTERWARDS: while curves are still being added, the face on a given side
    // of an already inserted curve keeps being split (an open chain has the unbounded face on
    // both sides).  Halfedge handles survive the remaining insertions because the boundary
    // curves of a valid set never cross — they meet only at endpoints — so no edge is split.
    std::vector<std::pair<std::size_t, HH>> reps;
    reps.reserve(curves.size());

    if constexpr (Policy::insert_with_history) {
      std::vector<CH> chs;
      arr->insert_curves(curves, chs);  // aggregate sweep insertion, recorded in the history
      // ArrBase::insert_curves() returns one curve handle per input curve, in input order
      // (CGAL appends the Curve_halfedges nodes in that order; see arr_impl.hpp), which is what
      // the chs[i] <-> curves[i] pairing below relies on.  `n` guards against a shorter result.
      const std::size_t n = std::min(chs.size(), curves.size());
      std::vector<HH> hes;
      for (std::size_t i = 0; i < n; ++i) {
        arr->induced_edges(chs[i], hes);
        if (!hes.empty()) reps.emplace_back(i, hes.front());
      }
    } else {
      // Bezier: an x-monotone sub-arc has no equivalent general Curve_2, so the with-history
      // path would insert whole supporting curves.  The boundary curves of a valid set never
      // cross (they meet only at endpoints), which is exactly the precondition of
      // insert_non_intersecting_curve(); the price is that the arrangement records no history.
      for (std::size_t i = 0; i < curves.size(); ++i)
        reps.emplace_back(i, arr->insert_non_intersecting_curve(curves[i]));
    }

    // The face that belongs to the set lies to the LEFT of every boundary curve as it is
    // traversed (CCW outer boundaries, CW holes — that is exactly what polygons_with_holes()
    // guarantees).  A halfedge's incident face is the one on its left, so for each boundary
    // curve we pick the halfedge whose direction agrees with the curve's own direction:
    // a curve directed right (Compare_endpoints_xy_2 == SMALLER) is traversed by the
    // ARR_LEFT_TO_RIGHT halfedge.  Doing this for EVERY curve (not just the first one of
    // each polygon) also covers regions that a hole touching the outer boundary splits into
    // several faces.
    auto cmp = gps().compare_endpoints_xy_2_object();
    for (const std::pair<std::size_t, HH>& rep : reps) {
      const int want = (cmp(xcurve(curves[rep.first])) == CGAL::SMALLER) ? ARR_LEFT_TO_RIGHT
                                                                        : ARR_RIGHT_TO_LEFT;
      HH he = rep.second;
      if (arr->he_direction(he) != want) he = arr->he_twin(he);
      add_face(arr->he_face(he));
    }
    return arr;
  }

  std::size_t arrangement_number_of_faces() const override {
    return static_cast<std::size_t>(m_gps.arrangement().number_of_faces());
  }
  std::size_t arrangement_number_of_edges() const override {
    return static_cast<std::size_t>(m_gps.arrangement().number_of_edges());
  }

 private:
  // ---------------------------------------------------------------- small helpers
  static const GpsTraits& gps() { return Policy::gps_traits(); }

  /// Gps_traits_adaptor copy-constructs the GPS traits (which in turn copy-constructs the
  /// arrangement traits: for Bezier that aliases the process-wide cache with m_owner == false,
  /// for conic it shares the kernels through shared_ptr).  Leaked on purpose, exactly like
  /// KindOpsBase's Arr_traits_adaptor_2 — destroying a CORE-backed object during static
  /// teardown aborts with `! blocks.empty()` (CGAL/CORE/MemoryPool.h:125).
  static const Adaptor& adaptor() {
    static const Adaptor* p = new Adaptor(Policy::gps_traits());
    return *p;
  }

  [[noreturn]] static void bad(ErrorCode code, const std::string& msg) {
    throw_error(code, std::string(kind_name(Types::kind)) + " polygon set: " + msg);
  }

  const Self& same_kind(const PolygonSetBase& other, const char* what) const {
    const Self* o = dynamic_cast<const Self*>(&other);
    if (o == nullptr)
      bad(ErrorCode::KindMismatch,
          std::string(what) + " expects a polygon set of kind '" + kind_name(Types::kind) +
              "' but got one of kind '" + kind_name(other.kind()) + "'");
    return *o;
  }

  /// An XCurve box of this kind (a Curve box is accepted only when the kind's Curve_2 and
  /// X_monotone_curve_2 are the same C++ type, i.e. it really holds an X_monotone_curve_2).
  static const Xcv& xcurve(const Geom& g) {
    require_kind(g, Types::kind, "polygon boundary curve");
    if (g.template holds<Xcv>() &&
        (g.type == GeomType::XCurve || g.type == GeomType::Curve))
      return g.template as<Xcv>();
    require_xcurve(g, Types::kind, "polygon boundary curve");  // throws NotXMonotone
    return g.template as<Xcv>();
  }

  static Geom box_xcurve(const Xcv& cv) {
    return make_geom(Types::kind, GeomType::XCurve, cv);
  }

  /// "A bounded polygon without holes" — the case CGAL validates with is_valid_polygon()
  /// (strictly simple) rather than is_valid_polygon_with_holes() (relatively simple).
  static bool is_plain_polygon(const PolygonGeom& p) {
    return !p.unbounded && p.holes.empty() && !p.outer.empty();
  }

  /// Cheap structural screening done before handing anything to CGAL.  Returns false (never
  /// throws) for a polygon CGAL could not even parse.
  ///
  /// An EMPTY outer boundary always means "unbounded" — that is CGAL's own representation
  /// (General_polygon_with_holes_2::is_unbounded() == outer_boundary().is_empty()) — so
  /// `unbounded == false` with an empty outer boundary and at least one hole is accepted and
  /// treated as unbounded; `unbounded == true` with a NON-empty outer boundary is rejected.
  bool structurally_ok(const PolygonGeom& p) const {
    if (p.unbounded) {
      if (!p.outer.empty()) return false;  // an unbounded polygon has an empty outer boundary
    } else if (p.outer.empty() && p.holes.empty()) {
      return false;  // nothing at all
    } else if (!p.outer.empty() && !is_closed_chain(p.outer)) {
      return false;
    }
    for (const std::vector<Geom>& hole : p.holes)
      if (hole.empty() || !is_closed_chain(hole)) return false;
    return true;
  }

  /// A human-readable reason why CGAL rejects `p` (best effort; never throws).
  std::string invalid_reason(const PolygonGeom& p) const {
    if (p.unbounded && !p.outer.empty())
      return "an unbounded polygon must have an empty outer boundary (its holes are the "
             "bounded pieces)";
    if (!p.unbounded && p.outer.empty() && p.holes.empty())
      return "the polygon is empty";
    if (!p.outer.empty() && !is_closed_chain(p.outer))
      return "the outer boundary is not a closed chain of at least two x-monotone curves "
             "(every curve's target must be the next curve's source)";
    for (std::size_t i = 0; i < p.holes.size(); ++i)
      if (p.holes[i].empty() || !is_closed_chain(p.holes[i]))
        return "hole " + std::to_string(i) +
               " is not a closed chain of at least two x-monotone curves";
    if (!p.outer.empty() && orientation(p.outer) == 0)
      return "the outer boundary encloses no area (a degenerate or self-overlapping ring)";
    if (!p.outer.empty() && orientation(p.outer) != 1)
      return "the outer boundary has clockwise orientation; CGAL requires counterclockwise "
             "outer boundaries (and clockwise holes) — reverse it";
    for (std::size_t i = 0; i < p.holes.size(); ++i)
      if (orientation(p.holes[i]) == 0)
        return "hole " + std::to_string(i) +
               " encloses no area (a degenerate or self-overlapping ring)";
    for (std::size_t i = 0; i < p.holes.size(); ++i)
      if (orientation(p.holes[i]) != -1)
        return "hole " + std::to_string(i) +
               " has counterclockwise orientation; CGAL requires clockwise holes (and a "
               "counterclockwise outer boundary) — reverse it";
    if (p.holes.empty())
      return "the boundary is not simple (it self-intersects or touches itself)";
    return "the boundary or one of the holes is not simple, the holes overlap each other, or "
           "a hole is not contained in the outer boundary";
  }

  Polygon_2 checked_polygon(const PolygonGeom& p, const char* what) const {
    Polygon_2 pgn;
    if (structurally_ok(p)) {
      to_polygon(p.outer, pgn);
      if (CGAL::is_valid_polygon<GpsTraits>(pgn, gps())) return pgn;
    }
    bad(ErrorCode::InvalidArgument, std::string(what) + ": invalid polygon — " +
                                        invalid_reason(p));
  }

  Polygon_with_holes_2 checked_pwh(const PolygonGeom& p, const char* what) const {
    if (structurally_ok(p)) {
      Polygon_with_holes_2 pwh = to_pwh(p);
      if (CGAL::is_valid_polygon_with_holes<GpsTraits>(pwh, gps())) return pwh;
    }
    bad(ErrorCode::InvalidArgument, std::string(what) + ": invalid polygon with holes — " +
                                        invalid_reason(p));
  }

  void insert_checked(const PolygonGeom& p, const char* what) {
    reject_unsweepable_with_polygon(p);
    if (is_plain_polygon(p)) {
      Polygon_2 pgn = checked_polygon(p, what);
      m_gps.insert(pgn);
    } else {
      Polygon_with_holes_2 pwh = checked_pwh(p, what);
      m_gps.insert(pwh);
    }
  }

  // ---------------------------------------------------------------- sweep pre-flight
  //
  // Same guard as ArrImpl::reject_unsweepable(), for the OTHER sweep in this library: every
  // Boolean operation runs CGAL's surface sweep over the boundary curves of both operands and
  // therefore calls Intersect_2 on pairs that no method of ours ever sees.  Measured for the
  // BEZIER kind: two DISJOINT polygons — one bounded by an arc of a self-intersecting cubic
  // (the arc itself simple and nowhere near the crossing), the other by a straight curve
  // through that crossing — make `join()` and `intersection()` SIGSEGV in every run, while
  // `insert()` (non-intersecting insertion) is fine.  See KindOps::check_sweepable.
  //
  // The registry lookup is guarded by kind_available(): a bso_<kind>.cpp TU is designed to be
  // linkable without its kind_<kind>.cpp, and in such a build no curve of that kind can exist
  // in the first place (the constructors live in the kind TU).
  bool needs_sweep_precheck() const {
    return kind_available(Types::kind) && ops(Types::kind).needs_sweep_precheck();
  }
  /// Appends this set's boundary curves (the edges of its internal arrangement).
  void collect_curves(std::vector<Geom>& out) const {
    auto& arr = const_cast<Gps&>(m_gps).arrangement();
    for (auto e = arr.edges_begin(); e != arr.edges_end(); ++e) out.push_back(box_xcurve(e->curve()));
  }
  static void collect_polygon_curves(const PolygonGeom& p, std::vector<Geom>& out) {
    out.insert(out.end(), p.outer.begin(), p.outer.end());
    for (const std::vector<Geom>& h : p.holes) out.insert(out.end(), h.begin(), h.end());
  }
  void reject_unsweepable(std::vector<Geom>& all) const {
    collect_curves(all);
    ops(Types::kind).check_sweepable(all);
  }
  void reject_unsweepable_with_polygon(const PolygonGeom& p) const {
    if (!needs_sweep_precheck()) return;
    std::vector<Geom> all;
    collect_polygon_curves(p, all);
    reject_unsweepable(all);
  }
  void reject_unsweepable_with_set(const Self& other) const {
    if (!needs_sweep_precheck()) return;
    std::vector<Geom> all;
    other.collect_curves(all);
    reject_unsweepable(all);
  }

  // ---------------------------------------------------------------- conversions
  /// vector of DIRECTED x-monotone curve boxes -> GpsTraits::Polygon_2.
  void to_polygon(const std::vector<Geom>& ring, Polygon_2& out) const {
    out.clear();
    if constexpr (k_curve_polygon) {
      std::vector<Xcv> cvs;
      cvs.reserve(ring.size());
      for (const Geom& g : ring) cvs.push_back(xcurve(g));
      out.init(cvs.begin(), cvs.end());
    } else {
      // Gps_segment_traits_2::Polygon_2 is a CGAL::Polygon_2 of POINTS.  Its
      // Construct_polygon_2 appends the TARGET of every curve (honouring the curve's own
      // direction), and Construct_curves_2 rebuilds one segment per consecutive vertex pair.
      // Lossless exactly because the chain is closed (checked by the callers).
      auto ctr_v = adaptor().construct_vertex_2_object();
      for (const Geom& g : ring) out.push_back(ctr_v(xcurve(g), 1));
    }
  }

  /// GpsTraits::Polygon_2 -> vector of DIRECTED x-monotone curve boxes.
  void from_polygon(const Polygon_2& pgn, std::vector<Geom>& out) const {
    out.clear();
    if constexpr (k_curve_polygon) {
      for (auto it = pgn.curves_begin(); it != pgn.curves_end(); ++it)
        out.push_back(box_xcurve(*it));
    } else {
      const std::size_t n = static_cast<std::size_t>(pgn.size());
      if (n < 2) return;
      auto eq = gps().equal_2_object();
      for (std::size_t i = 0; i < n; ++i) {
        const Pt& p = pgn.vertex(i);
        const Pt& q = pgn.vertex((i + 1) % n);
        // Arr_segment_2(p, q) has a p != q precondition; CGAL never emits a repeated vertex,
        // so this can only fire on hand-built input and is reported as an argument error.
        if (eq(p, q))
          bad(ErrorCode::InvalidArgument,
              "polygon has two equal consecutive vertices (degenerate edge)");
        out.push_back(box_xcurve(Xcv(p, q)));
      }
    }
  }

  Polygon_with_holes_2 to_pwh(const PolygonGeom& p) const {
    Polygon_2 outer;  // left empty for an unbounded polygon -> Is_unbounded == true
    if (!p.unbounded && !p.outer.empty()) to_polygon(p.outer, outer);
    std::vector<Polygon_2> holes(p.holes.size());
    for (std::size_t i = 0; i < p.holes.size(); ++i) to_polygon(p.holes[i], holes[i]);
    return gps().construct_polygon_with_holes_2_object()(outer, holes.begin(), holes.end());
  }

  void from_pwh(const Polygon_with_holes_2& pwh, PolygonGeom& out) const {
    out.outer.clear();
    out.holes.clear();
    out.unbounded = gps().is_unbounded_object()(pwh);
    if (!out.unbounded) from_polygon(pwh.outer_boundary(), out.outer);
    auto hs = gps().construct_holes_object()(pwh);
    for (auto it = hs.first; it != hs.second; ++it) {
      out.holes.emplace_back();
      from_polygon(*it, out.holes.back());
    }
  }

  Gps m_gps;
};

}  // namespace arr2d
