// arr2d — ArrImpl<Types>: the generic implementation of ArrBase for every geometry kind.
//
//   template class arr2d::ArrImpl<arr2d::SegmentTypes>;   // in kind_segment.cpp
//   ...
//
// Everything here is generic over the seven `<kind>_types.hpp` structs; a kind TU only has to
// explicitly instantiate the template (and register `make_arrangement`).  Per-kind differences
// are expressed through `KindPolicy<Types>` (below) and `if constexpr (Types::is_sphere / ...)`.
//
// Underlying CGAL type: `typename Types::Arrangement`, i.e.
//   Arrangement_with_history_2<Traits, Arr_extended_dcel<Traits, ElementData x3>>   (planar) or
//   Arrangement_on_surface_with_history_2<Traits, Arr_spherical_topology_traits_2<...>> (sphere).
// It is always constructed with a pointer to the process-wide `Types::traits()` object, which is
// never copied and outlives every arrangement (arrangement_core.md gotcha 14: the traits pointer
// is shared by every copy).
//
// Handles.  A VH/HH/FH is (raw DCEL record pointer, unique id).  The id lives in the element's
// extended-DCEL data (arr2d::ElementData::id) and is assigned lazily by track() the first time a
// handle for that element is produced.  Live sets of raw pointers + the stored id make every
// accessor detect stale handles and raise Error(InvalidHandle) instead of crashing.
// Curve (history) handles use the `Curve_halfedges*` node address plus an id kept in a map.
//
// References: docs/dev/DESIGN.md §1-2 and docs/dev/cgal61_api/{arrangement_core,
// arrangement_with_history, global_functions_overlay_observer, point_location_and_decomposition,
// dcel_and_accessor, traits_geodesic_sphere}.md.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iterator>
#include <limits>
#include <list>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

// The *declarations* of the global functions (with their default arguments) live in
// Arrangement_on_surface_2.h — global_functions_overlay_observer.md gotcha 8 says to include
// this header (never Arrangement_2/Arrangement_on_surface_2_global.h alone).
#include <CGAL/Arrangement_on_surface_2.h>
#include <CGAL/Arrangement_on_surface_with_history_2.h>
#include <CGAL/Arrangement_2/Arr_with_history_accessor.h>
#include <CGAL/Arr_overlay_2.h>
#include <CGAL/Arr_point_location_result.h>
#include <CGAL/Arr_batched_point_location.h>
#include <CGAL/Arr_vertical_decomposition_2.h>
#include <CGAL/Arr_naive_point_location.h>
#include <CGAL/Arr_simple_point_location.h>
#include <CGAL/Arr_walk_along_line_point_location.h>
#include <CGAL/Arr_landmarks_point_location.h>
#include <CGAL/Arr_trapezoid_ric_point_location.h>
#include <CGAL/Arr_triangulation_point_location.h>

#include "arr2d/arrangement.hpp"
#include "arr2d/common.hpp"
#include "arr2d/dcel_data.hpp"
#include "arr2d/impl/arr_impl_detect.hpp"
#include "arr2d/ops.hpp"
#include "arr2d/registry.hpp"

namespace arr2d {

// The seven Types structs (kinds/<kind>_types.hpp).  Only *declared* here so that a kind TU has
// to include its own types header only; KindPolicy is specialised on the incomplete types below.
struct SegmentTypes;
struct LinearTypes;
struct CircleSegmentTypes;
struct PolylineTypes;
struct BezierTypes;
struct ConicTypes;
struct SphereTypes;

// ---------------------------------------------------------------------------
// KindPolicy — the per-kind customisation point of ArrImpl
// ---------------------------------------------------------------------------
//
// Every flag was decided by actually compiling (and, where possible, running) the feature for
// that kind against CGAL 6.1; see the matrix in the specialisations below.  A kind TU never
// touches these: ArrImpl reads them through `if constexpr` and `std::conditional_t`.
template <class Types>
struct KindPolicy {
  // ---- point location -----------------------------------------------------
  /// Arr_naive_point_location: needs only Topology_traits::is_in_face — available everywhere.
  static constexpr bool supports_naive = true;
  /// Arr_simple_point_location: needs Topology_traits::{Dcel, initial_face()} (planar only).
  static constexpr bool supports_simple = !Types::is_sphere;
  /// Arr_walk_along_line_point_location: needs Topology_traits::initial_face() (planar only).
  static constexpr bool supports_walk = !Types::is_sphere;
  /// Arr_landmarks_point_location: needs traits Approximate_2 + Construct_x_monotone_curve_2.
  static constexpr bool supports_landmarks = true;
  /// Arr_trapezoid_ric_point_location: planar topologies only.
  ///
  /// CGAL 6.1 CAVEAT (handled by ArrImpl::TrapGuard, not by this flag): while such an object is
  /// ATTACHED, any edge MERGE (Arrangement.merge_edge, or remove_vertex on a degree-2 vertex)
  /// makes its observer raise `ptr()->v == ptr()->cw_he->source()`
  /// (Arr_point_location/Td_active_vertex.h:168) from inside Arrangement_on_surface_2::merge_edge
  /// — measured for segment, polyline, circle_segment, conic and bezier.  Worse, the exception
  /// escapes between Curve_halfedges_observer::before_merge_edge (which already unregistered both
  /// edges) and after_merge_edge, so the CURVE HISTORY is left asymmetric and a later
  /// remove_curve() + copy()/assign()/overlay() dereferences a freed Curve_halfedges node.
  /// ArrImpl therefore detaches and rebuilds the structure around every merging operation, and
  /// repairs the history if any exception escapes a with-history modification.
  static constexpr bool supports_trapezoid = !Types::is_sphere;
  /// Arr_triangulation_point_location: needs Geometry_traits_2::Kernel, bounded, straight edges.
  static constexpr bool supports_triangulation = false;

  // ---- vertical ray shooting (only simple / walk / trapezoid have ray_shoot_*) -------------
  static constexpr bool supports_ray_shooting_simple = supports_simple;
  static constexpr bool supports_ray_shooting_walk = supports_walk;
  static constexpr bool supports_ray_shooting_trapezoid = supports_trapezoid;
  static constexpr bool supports_ray_shooting =
      supports_ray_shooting_simple || supports_ray_shooting_walk || supports_ray_shooting_trapezoid;

  /// The free CGAL::is_valid() ray-shoots with TopTraits::Default_point_location_strategy; on the
  /// sphere that is Arr_naive_point_location, which has no ray_shoot_down() => does not compile.
  static constexpr bool supports_global_is_valid = !Types::is_sphere;
};

// --- Segment: everything works (the reference kind). -------------------------------------
template <>
struct KindPolicy<SegmentTypes> {
  static constexpr bool supports_naive = true;
  static constexpr bool supports_simple = true;
  static constexpr bool supports_walk = true;
  static constexpr bool supports_landmarks = true;
  static constexpr bool supports_trapezoid = true;
  static constexpr bool supports_triangulation = false;  ///< compiles for segments, but CGAL 6.1 returns the WRONG face for faces with holes (CGAL_TRAPS_CHECKLIST) -> not exposed
  static constexpr bool supports_ray_shooting_simple = true;
  static constexpr bool supports_ray_shooting_walk = true;
  static constexpr bool supports_ray_shooting_trapezoid = true;
  static constexpr bool supports_ray_shooting = true;
  static constexpr bool supports_global_is_valid = true;
};

// --- Linear (unbounded planar). ------------------------------------------------------------
// Triangulation PL compiles (Arr_linear_traits_2 has a Kernel) but calls point() on vertices at
// infinity => runtime CGAL assertion (point_location_and_decomposition.md gotcha 9) => disabled.
template <>
struct KindPolicy<LinearTypes> {
  static constexpr bool supports_naive = true;
  static constexpr bool supports_simple = true;
  static constexpr bool supports_walk = true;
  /// Landmarks is offered, but ONLY while the arrangement holds no unbounded edge — see
  /// ArrImpl::landmarks_usable(), which is what supports_point_location(PL_LANDMARKS) reports
  /// for this kind.  CGAL 6.1 breaks in two places as soon as a ray or a line is present:
  /// `_deal_with_curve_contained_in_segment` (Arr_landmarks_pl_impl.h:414) compares
  /// `he->source()->point()` with `he->target()->point()` WITHOUT the is_at_open_boundary()
  /// guard its sibling code at :309/:329 uses -> `CGAL_assertion(p_pt != nullptr)`
  /// (Arr_dcel_base.h:105), plain UB without CGAL_DEBUG; and `_walk_from_face` runs out of
  /// crossable edges on the all-fictitious outer CCB of the unbounded face ->
  /// `CGAL_assertion(new_face != face)` (Arr_landmarks_pl_impl.h:533).  Both were measured.
  /// With no unbounded edge the bug is structurally unreachable: `_intersection_with_ccb` skips
  /// fictitious halfedges, and the only CCBs that MIX fictitious and concrete halfedges are the
  /// ones an unbounded edge creates.  Verified with 4320 randomized queries (40 random
  /// bounded-only arrangements, every vertex, every collinear extension of every edge, the PL
  /// attached to the arrangement while empty and grown incrementally): zero mismatches against
  /// Arr_naive_point_location and zero exceptions.
  static constexpr bool supports_landmarks = true;
  static constexpr bool supports_trapezoid = false;  ///< CGAL 6.1 bug: an attached Arr_trapezoid_ric_point_location crashes in before_remove_edge when the edge touches a vertex at infinity (Td_inactive_vertex ctor); see STAGE2_NOTES (kind_linear)
  static constexpr bool supports_triangulation = false;
  static constexpr bool supports_ray_shooting_simple = true;
  static constexpr bool supports_ray_shooting_walk = true;
  static constexpr bool supports_ray_shooting_trapezoid = false;
  static constexpr bool supports_ray_shooting = true;
  static constexpr bool supports_global_is_valid = true;
};

// --- CircleSegment: no Construct_x_monotone_curve_2 => landmarks does not compile
//     (traits_circle_segment.md gotcha 3).  No Kernel-based straight edges => no triangulation.
template <>
struct KindPolicy<CircleSegmentTypes> {
  static constexpr bool supports_naive = true;
  static constexpr bool supports_simple = true;
  static constexpr bool supports_walk = true;
  static constexpr bool supports_landmarks = false;
  static constexpr bool supports_trapezoid = true;
  static constexpr bool supports_triangulation = false;
  static constexpr bool supports_ray_shooting_simple = true;
  static constexpr bool supports_ray_shooting_walk = true;
  static constexpr bool supports_ray_shooting_trapezoid = true;
  static constexpr bool supports_ray_shooting = true;
  static constexpr bool supports_global_is_valid = true;
};

// --- Polyline. Arr_polyline_traits_2 has Approximate_2 and Construct_x_monotone_curve_2,
//     but no `Kernel` typedef => no triangulation PL.
template <>
struct KindPolicy<PolylineTypes> {
  static constexpr bool supports_naive = true;
  static constexpr bool supports_simple = true;
  static constexpr bool supports_walk = true;
  static constexpr bool supports_landmarks = true;
  static constexpr bool supports_trapezoid = true;
  static constexpr bool supports_triangulation = false;
  static constexpr bool supports_ray_shooting_simple = true;
  static constexpr bool supports_ray_shooting_walk = true;
  static constexpr bool supports_ray_shooting_trapezoid = true;
  static constexpr bool supports_ray_shooting = true;
  static constexpr bool supports_global_is_valid = true;
};

// --- Bezier: no Approximate_2 and no Construct_x_monotone_curve_2 (traits_bezier.md gotcha 8)
//     => landmarks impossible.
template <>
struct KindPolicy<BezierTypes> {
  static constexpr bool supports_naive = true;
  static constexpr bool supports_simple = true;
  static constexpr bool supports_walk = true;
  static constexpr bool supports_landmarks = false;
  static constexpr bool supports_trapezoid = true;
  static constexpr bool supports_triangulation = false;
  static constexpr bool supports_ray_shooting_simple = true;
  static constexpr bool supports_ray_shooting_walk = true;
  static constexpr bool supports_ray_shooting_trapezoid = true;
  static constexpr bool supports_ray_shooting = true;
  static constexpr bool supports_global_is_valid = true;
};

// --- Conic.
template <>
struct KindPolicy<ConicTypes> {
  static constexpr bool supports_naive = true;
  static constexpr bool supports_simple = true;
  static constexpr bool supports_walk = true;
  static constexpr bool supports_landmarks = true;
  static constexpr bool supports_trapezoid = true;
  static constexpr bool supports_triangulation = false;
  static constexpr bool supports_ray_shooting_simple = true;
  static constexpr bool supports_ray_shooting_walk = true;
  static constexpr bool supports_ray_shooting_trapezoid = true;
  static constexpr bool supports_ray_shooting = true;
  static constexpr bool supports_global_is_valid = true;
};

// --- Sphere: Arr_spherical_topology_traits_2 has no initial_face() => simple / walk / trapezoid
//     do not compile; its Default_point_location_strategy is Arr_naive_point_location, which has
//     no ray_shoot_* => the free CGAL::is_valid() does not compile either.
//     No vertical ray shooting at all on the sphere.
template <>
struct KindPolicy<SphereTypes> {
  static constexpr bool supports_naive = true;
  static constexpr bool supports_simple = false;
  static constexpr bool supports_walk = false;
  /// COMPILES, but is unusable: the landmark walk connects the nearest landmark to the query
  /// point with Construct_x_monotone_curve_2(np, p), whose CGAL 6.1 precondition
  /// `! equal_3(opposite(source), target)` (Arr_geodesic_arc_on_sphere_traits_2.h:611) forbids
  /// an ANTIPODAL pair — and the landmark set is chosen by CGAL, so nothing the caller does can
  /// avoid it.  Measured on the two-octant-triangle arrangement: locating the north pole
  /// (0,0,1) raises that precondition while every other strategy answers correctly; without
  /// CGAL_DEBUG the same call builds a garbage arc instead.  CGAL_TRAPS_CHECKLIST "Sphere kind"
  /// ("only Arr_naive_point_location and batched CGAL::locate are safe") is the rule; this flag
  /// used to contradict it.
  static constexpr bool supports_landmarks = false;
  static constexpr bool supports_trapezoid = false;
  static constexpr bool supports_triangulation = false;
  static constexpr bool supports_ray_shooting_simple = false;
  static constexpr bool supports_ray_shooting_walk = false;
  static constexpr bool supports_ray_shooting_trapezoid = false;
  static constexpr bool supports_ray_shooting = false;
  static constexpr bool supports_global_is_valid = false;
};

namespace impl {
/// Placeholder for a point-location strategy a kind does not support (never instantiated).
struct Unsupported_pl {};
}  // namespace impl

// ---------------------------------------------------------------------------
// ArrImpl
// ---------------------------------------------------------------------------
template <class Types>
class ArrImpl final : public ArrBase {
 public:
  using Policy = KindPolicy<Types>;

  using Traits = typename Types::Traits;
  using Point_2 = typename Types::Point_2;
  using Curve_2 = typename Types::Curve_2;
  using X_monotone_curve_2 = typename Types::X_monotone_curve_2;

  using Arr = typename Types::Arrangement;
  /// The *data-extended* arrangement underneath the with-history class
  /// (arrangement_with_history.md gotcha 1): its X_monotone_curve_2 carries the originating
  /// input-curve pointers.
  using Base_arr = typename Arr::Base_arrangement_2;
  using Data_traits = typename Base_arr::Geometry_traits_2;
  using Data_curve_2 = typename Data_traits::Curve_2;
  using DXcv = typename Data_traits::X_monotone_curve_2;

  using Vertex = typename Arr::Vertex;
  using Halfedge = typename Arr::Halfedge;
  using Face = typename Arr::Face;

  using VHandle = typename Arr::Vertex_handle;
  using HHandle = typename Arr::Halfedge_handle;
  using FHandle = typename Arr::Face_handle;
  using VCHandle = typename Arr::Vertex_const_handle;
  using HCHandle = typename Arr::Halfedge_const_handle;
  using FCHandle = typename Arr::Face_const_handle;
  using Ccb_circ = typename Arr::Ccb_halfedge_circulator;

  using Curve_halfedges = typename Arr::Curve_halfedges;
  using Curve_handle = typename Arr::Curve_handle;

  /// Point-location result: std::variant<V,H,F> of CONST handles, alternatives 0/1/2.
  using PlResult = typename CGAL::Arr_point_location_result<Arr>::Type;
  /// zone() writes NON-const handles (global_functions_overlay_observer.md gotcha 3).
  using ZoneElem = std::variant<VHandle, HHandle, FHandle>;

  // ---- point-location strategy objects (unsupported ones collapse to a dummy type) --------
  using Naive_pl = CGAL::Arr_naive_point_location<Arr>;
  using Simple_pl = std::conditional_t<Policy::supports_simple,
                                       CGAL::Arr_simple_point_location<Arr>, impl::Unsupported_pl>;
  using Walk_pl = std::conditional_t<Policy::supports_walk,
                                     CGAL::Arr_walk_along_line_point_location<Arr>, impl::Unsupported_pl>;
  using Landmarks_pl = std::conditional_t<Policy::supports_landmarks,
                                          CGAL::Arr_landmarks_point_location<Arr>, impl::Unsupported_pl>;
  using Trap_pl = std::conditional_t<Policy::supports_trapezoid,
                                     CGAL::Arr_trapezoid_ric_point_location<Arr>, impl::Unsupported_pl>;
  using Tri_pl = std::conditional_t<Policy::supports_triangulation,
                                    CGAL::Arr_triangulation_point_location<Arr>, impl::Unsupported_pl>;
  /// Strategy used for the temporary point-location object of zone()/do_intersect()/locate().
  using Default_pl = std::conditional_t<Policy::supports_walk,
                                        CGAL::Arr_walk_along_line_point_location<Arr>,
                                        CGAL::Arr_naive_point_location<Arr>>;

  // =========================================================================
  ArrImpl() : m_arr(&Types::traits()) {
    // The internal observer is attached for the whole life of the object; it keeps the live sets
    // and the element ids up to date and forwards every notification to the Python observers.
    m_obs.attach(m_arr);
  }

  ~ArrImpl() override {
    // Members are destroyed in reverse declaration order: the point-location strategies first
    // (some of them are observers on m_arr), then the internal observer, then the arrangement.
    m_py_obs.clear();
  }

  ArrImpl(const ArrImpl&) = delete;
  ArrImpl& operator=(const ArrImpl&) = delete;

  /// Direct access for the kind TU / BSO code (not part of ArrBase).
  Arr& arrangement() { return m_arr; }
  const Arr& arrangement() const { return m_arr; }

  // ---- global ------------------------------------------------------------
  Kind kind() const override { return Types::kind; }
  const KindOps& ops() const override {
    if (!m_ops) m_ops = &arr2d::ops(Types::kind);
    return *m_ops;
  }
  bool is_unbounded_kind() const override { return Types::is_unbounded; }

  std::size_t number_of_vertices() const override { return m_arr.number_of_vertices(); }
  std::size_t number_of_isolated_vertices() const override { return m_arr.number_of_isolated_vertices(); }
  std::size_t number_of_vertices_at_infinity() const override {
    if constexpr (impl::has_vertices_at_infinity_v<Arr>) return m_arr.number_of_vertices_at_infinity();
    else return 0;   // sphere: Arrangement_on_surface_(with_history_)2 has no such member
  }
  std::size_t number_of_halfedges() const override { return m_arr.number_of_halfedges(); }
  std::size_t number_of_edges() const override { return m_arr.number_of_edges(); }
  std::size_t number_of_faces() const override { return m_arr.number_of_faces(); }
  std::size_t number_of_unbounded_faces() const override { return m_arr.number_of_unbounded_faces(); }
  std::size_t number_of_curves() const override { return m_arr.number_of_curves(); }
  bool is_empty() const override { return m_arr.is_empty(); }

  bool is_valid() const override {
    sync();
    // CGAL's own is_valid() knows nothing about the CURVE HISTORY, so an exception that escaped
    // a with-history modification (leaving `Curve_halfedges::m_halfedges` and the edges'
    // `curve().data()` sets disagreeing) went unnoticed and only crashed later, inside
    // copy()/assign()/overlay().  Report it here instead — see history_is_consistent().
    if (!history_is_consistent()) return false;
    // The free CGAL::is_valid() adds the sweep + hole-placement checks on top of the member one,
    // but it vertical-ray-shoots with the topology's default strategy, which the sphere lacks.
    if constexpr (Policy::supports_global_is_valid) {
      // NOTE: <CGAL/utils.h> declares `template <class T> bool is_valid(const T&)` which returns
      // true unconditionally.  Passing our *derived* arrangement would select that one (identity
      // match beats the derived-to-base binding of the arrangement overload), so bind the base
      // reference explicitly — then partial ordering picks the arrangement overload.
      const Base_arr& base = m_arr;
      return CGAL::is_valid(base);
    } else {
      return m_arr.is_valid();
    }
  }

  void clear() override {
    reject_reentrant("clear");
    m_arr.clear();   // observer: after_clear -> live sets emptied
    sync();
  }

  std::unique_ptr<ArrBase> clone() const override {
    sync();
    auto copy = std::make_unique<ArrImpl<Types>>();
    // Do NOT copy-construct the CGAL arrangement: its copy constructor default-constructs a
    // fresh Traits_adaptor_2 first (arrangement_core.md §6.6, traits_bezier.md gotcha 10) which
    // would build a second traits object (and, for Bezier, a second curve cache).  Constructing
    // with the process-wide traits pointer + assign() keeps the shared traits and copies the
    // DCEL, the extended data (=> PyRef increfs) and the whole curve history.
    copy->m_arr.assign(m_arr);
    copy->rescan();
    return copy;
  }

  void assign(const ArrBase& other) override {
    reject_reentrant("assign");
    const ArrImpl* o = as_impl(other);
    if (o == this) return;
    m_arr.assign(o->m_arr);
    renumber();   // the ids copied from `other` are meaningless (and may collide) here
    rescan();
  }

  // ---- iteration ---------------------------------------------------------
  void vertices(std::vector<VH>& out) const override {
    sync();
    out.clear();
    out.reserve(m_arr.number_of_vertices());
    for (auto v = arr().vertices_begin(); v != arr().vertices_end(); ++v) out.push_back(track_v(v));
  }
  void halfedges(std::vector<HH>& out) const override {
    sync();
    out.clear();
    out.reserve(m_arr.number_of_halfedges());
    for (auto h = arr().halfedges_begin(); h != arr().halfedges_end(); ++h) out.push_back(track_h(h));
  }
  void edges(std::vector<HH>& out) const override {
    sync();
    out.clear();
    out.reserve(m_arr.number_of_edges());
    for (auto e = arr().edges_begin(); e != arr().edges_end(); ++e) out.push_back(track_h(e));
  }
  void faces(std::vector<FH>& out) const override {
    sync();
    out.clear();
    out.reserve(m_arr.number_of_faces());
    for (auto f = arr().faces_begin(); f != arr().faces_end(); ++f) out.push_back(track_f(f));
  }
  void unbounded_faces(std::vector<FH>& out) const override {
    sync();
    out.clear();
    for (auto f = arr().unbounded_faces_begin(); f != arr().unbounded_faces_end(); ++f)
      out.push_back(track_f(FHandle(f)));
  }
  void curves(std::vector<CH>& out) const override {
    sync();
    out.clear();
    out.reserve(m_arr.number_of_curves());
    for (auto c = arr().curves_begin(); c != arr().curves_end(); ++c) out.push_back(track_c(c));
  }

  FH unbounded_face() const override {
    sync();
    if constexpr (impl::has_unbounded_face_v<Arr>) {
      // Bounded planar: the unique unbounded face.  Linear: an arbitrary unbounded face
      // (arrangement_core.md §7) — use unbounded_faces() to get them all.
      return track_f(arr().unbounded_face());
    } else {
      // Sphere: the spherical / reference face, the unique face with no outer CCB
      // (traits_geodesic_sphere.md gotcha 5).
      return track_f(arr().reference_face());
    }
  }

  FH fictitious_face() const override {
    if constexpr (Types::is_unbounded) {
      sync();
      return track_f(arr().fictitious_face());
    } else {
      // Bounded planar topologies return the (real) unbounded face here, and the member does not
      // even compile for the spherical topology traits — so report it as unsupported.
      throw_error(ErrorCode::Unsupported,
                  std::string("kind '") + kind_name(Types::kind) +
                      "' has no fictitious face (only the unbounded 'linear' kind has one)");
    }
  }

  // ---- handle validity ---------------------------------------------------
  bool vertex_valid(VH v) const override { sync(); return valid_v(v); }
  bool halfedge_valid(HH h) const override { sync(); return valid_h(h); }
  bool face_valid(FH f) const override { sync(); return valid_f(f); }
  bool curve_valid(CH c) const override {
    sync();
    auto it = m_curve_ids.find(c.p);
    return c.p != nullptr && it != m_curve_ids.end() && it->second == c.id;
  }

  // ---- vertex ------------------------------------------------------------
  Geom vertex_point(VH v) const override {
    VHandle h = check_v(v);
    if (h->is_at_open_boundary())
      throw_error(ErrorCode::Unsupported, "vertex lies at an open boundary and has no point");
    return box_point(h->point());
  }
  std::size_t vertex_degree(VH v) const override {
    VHandle h = check_v(v);
    reject_dead_vertex_ring(v, h, "degree");
    return h->degree();   // null-safe (Arrangement_on_surface_2.h:595)
  }
  bool vertex_is_isolated(VH v) const override { return check_v(v)->is_isolated(); }
  FH vertex_face(VH v) const override {
    VHandle h = check_v(v);
    if (!h->is_isolated())
      throw_error(ErrorCode::InvalidArgument, "vertex_face() requires an isolated vertex");
    return track_f(h->face());
  }
  void vertex_incident_halfedges(VH v, std::vector<HH>& out) const override {
    VHandle h = check_v(v);
    out.clear();
    reject_dead_vertex_ring(v, h, "incident_halfedges");
    // CGAL precondition of incident_halfedges(): the vertex must have at least one incident
    // halfedge.  `is_isolated()` alone is NOT enough — a vertex that CGAL has just created (and
    // that an observer callback sees in after_create_vertex / before_create_edge /
    // before_split_edge / after_create_boundary_vertex) is NOT isolated yet and still has a NULL
    // halfedge pointer, and Vertex::incident_halfedges() has no null check while its sibling
    // Vertex::degree() has one.  Circulating it SEGFAULTs, so use degree() as the gate.
    if (h->is_isolated() || h->degree() == 0) return;
    auto circ = h->incident_halfedges();
    auto first = circ;
    do {
      out.push_back(track_h(HHandle(circ)));
      ++circ;
    } while (circ != first);
  }
  bool vertex_is_at_open_boundary(VH v) const override { return check_v(v)->is_at_open_boundary(); }
  int vertex_parameter_space_in_x(VH v) const override { return int(check_v(v)->parameter_space_in_x()); }
  int vertex_parameter_space_in_y(VH v) const override { return int(check_v(v)->parameter_space_in_y()); }
  PyRef& vertex_data(VH v) override { return check_v(v)->data().data; }
  const PyRef& vertex_data(VH v) const override { return check_v(v)->data().data; }

  // ---- halfedge ----------------------------------------------------------
  VH he_source(HH h) const override { return track_v(check_h(h)->source()); }
  VH he_target(HH h) const override { return track_v(check_h(h)->target()); }
  HH he_twin(HH h) const override { return track_h(check_h(h)->twin()); }
  HH he_next(HH h) const override { return track_h(check_h(h)->next()); }
  HH he_prev(HH h) const override { return track_h(check_h(h)->prev()); }
  FH he_face(HH h) const override {
    // Inside before_split_face CGAL has not re-linked the incident face of the halfedge that is
    // about to bound the new face: e->face() and e->twin()->face() are dangling and dereferencing
    // them SEGFAULTs (CGAL_TRAPS_CHECKLIST, "Observer traces").
    if (h.p != nullptr && (h.p == m_no_face_he[0] || h.p == m_no_face_he[1]))
      throw_error(ErrorCode::Unsupported,
                  "halfedge->face() is not available inside before_split_face: CGAL has not yet "
                  "re-linked the incident face of the new edge");
    return track_f(check_h(h)->face());
  }

  Geom he_curve(HH h) const override {
    HHandle e = check_h(h);
    if (e->is_fictitious())
      throw_error(ErrorCode::Unsupported, "fictitious halfedges carry no curve");
    // he->curve() is the DATA-extended curve; bind its plain base and copy (slice).
    const X_monotone_curve_2& base = e->curve();
    return box_xcurve(base);
  }

  Geom he_directed_curve(HH h) const override {
    HHandle e = check_h(h);
    if (e->is_fictitious())
      throw_error(ErrorCode::Unsupported, "fictitious halfedges carry no curve");
    const X_monotone_curve_2& base = e->curve();
    Geom c = box_xcurve(base);
    const bool curve_l2r = (ops().compare_endpoints_xy(c) == -1);
    const bool he_l2r = (e->direction() == CGAL::ARR_LEFT_TO_RIGHT);
    if (curve_l2r == he_l2r) return c;
    // Unbounded curves (Linear rays/lines) cannot always be reversed (a reversed ray is not a
    // ray in Arr_linear_object_2): return the curve as stored and let callers consult
    // he_direction() — documented contract in arrangement.hpp.
    if (!ops().curve_is_bounded(c)) return c;
    return ops().construct_opposite(c);   // Unsupported propagates for other kinds
  }

  int he_direction(HH h) const override {
    // CGAL spells ARR_LEFT_TO_RIGHT = -1; arr2d::HalfedgeDirection spells it 0.
    return check_h(h)->direction() == CGAL::ARR_LEFT_TO_RIGHT ? int(arr2d::ARR_LEFT_TO_RIGHT)
                                                              : int(arr2d::ARR_RIGHT_TO_LEFT);
  }
  bool he_is_fictitious(HH h) const override { return check_h(h)->is_fictitious(); }
  bool he_is_on_inner_ccb(HH h) const override { return check_h(h)->is_on_inner_ccb(); }
  bool he_is_on_outer_ccb(HH h) const override { return check_h(h)->is_on_outer_ccb(); }
  void he_ccb(HH h, std::vector<HH>& out) const override {
    HHandle e = check_h(h);
    out.clear();
    Ccb_circ circ = e->ccb();
    Ccb_circ first = circ;
    do {
      out.push_back(track_h(HHandle(circ)));
      ++circ;
    } while (circ != first);
  }
  PyRef& he_data(HH h) override { return check_h(h)->data().data; }
  const PyRef& he_data(HH h) const override { return check_h(h)->data().data; }

  // ---- face --------------------------------------------------------------
  bool face_is_unbounded(FH f) const override { return check_f(f)->is_unbounded(); }
  bool face_is_fictitious(FH f) const override { return check_f(f)->is_fictitious(); }
  bool face_has_outer_ccb(FH f) const override { return check_f(f)->number_of_outer_ccbs() > 0; }
  std::size_t face_number_of_outer_ccbs(FH f) const override { return check_f(f)->number_of_outer_ccbs(); }
  std::size_t face_number_of_inner_ccbs(FH f) const override { return check_f(f)->number_of_inner_ccbs(); }
  std::size_t face_number_of_isolated_vertices(FH f) const override {
    return check_f(f)->number_of_isolated_vertices();
  }
  HH face_outer_ccb(FH f) const override {
    FHandle fh = check_f(f);
    // Face::outer_ccb() has CGAL_precondition(number_of_outer_ccbs() == 1) and a spherical face
    // may have 0 or several — never call it blindly (traits_geodesic_sphere.md gotcha 5).
    if (fh->number_of_outer_ccbs() == 0)
      throw_error(ErrorCode::InvalidArgument, "face has no outer CCB");
    return track_h(HHandle(*fh->outer_ccbs_begin()));
  }
  void face_outer_ccbs(FH f, std::vector<HH>& out) const override {
    FHandle fh = check_f(f);
    out.clear();
    for (auto o = fh->outer_ccbs_begin(); o != fh->outer_ccbs_end(); ++o)
      out.push_back(track_h(HHandle(*o)));
  }
  void face_inner_ccbs(FH f, std::vector<HH>& out) const override {
    FHandle fh = check_f(f);
    out.clear();
    for (auto o = fh->inner_ccbs_begin(); o != fh->inner_ccbs_end(); ++o)
      out.push_back(track_h(HHandle(*o)));
  }
  void face_isolated_vertices(FH f, std::vector<VH>& out) const override {
    FHandle fh = check_f(f);
    out.clear();
    for (auto v = fh->isolated_vertices_begin(); v != fh->isolated_vertices_end(); ++v)
      out.push_back(track_v(VHandle(v)));
  }
  PyRef& face_data(FH f) override { return check_f(f)->data().data; }
  const PyRef& face_data(FH f) const override { return check_f(f)->data().data; }

  /// Outer boundary + holes as directed x-monotone curves.  A face with several outer CCBs (only
  /// possible on the sphere) reports its FIRST outer CCB in `outer`; the remaining outer CCBs are
  /// appended to `holes` before the inner CCBs — use face_outer_ccbs() when the distinction
  /// matters.  Fictitious halfedges are skipped, so an unbounded face's cycle is "open".
  void face_polygon(FH f, std::vector<Geom>& outer, std::vector<std::vector<Geom>>& holes) const override {
    FHandle fh = check_f(f);
    outer.clear();
    holes.clear();
    bool first_ccb = true;
    for (auto o = fh->outer_ccbs_begin(); o != fh->outer_ccbs_end(); ++o) {
      std::vector<Geom> cycle;
      collect_ccb_curves(*o, cycle);
      if (first_ccb) { outer = std::move(cycle); first_ccb = false; }
      else holes.push_back(std::move(cycle));
    }
    for (auto o = fh->inner_ccbs_begin(); o != fh->inner_ccbs_end(); ++o) {
      std::vector<Geom> cycle;
      collect_ccb_curves(*o, cycle);
      holes.push_back(std::move(cycle));
    }
  }

  // ---- Arrangement_2 modification (specialised, unchecked insertions) ----
  //
  // These map 1:1 to the Arrangement_on_surface_2 members.  They do NO point location and NO
  // intersection test; every CGAL precondition below is checked by CGAL only when assertions are
  // enabled (the project keeps -UNDEBUG, so they raise CGAL::Precondition_exception):
  //   * insert_point_in_face_interior(p, f): p must lie inside f (not checked by CGAL at all).
  //   * insert_in_face_interior(cv, f): cv's interior must be disjoint from the arrangement and
  //     lie inside f; both endpoints are created as new vertices.
  //   * insert_from_left_vertex(cv, v) / insert_from_right_vertex(cv, v): v must be the left
  //     (resp. right) end of cv.  CGAL additionally requires a containing face argument when v
  //     has degree 0 and carries no isolated-vertex record — that overload is not exposed here,
  //     so pass an isolated or an incident vertex.
  //   * insert_at_vertices(cv, v1, v2): v1 and v2 must be cv's endpoints, cv must not intersect
  //     the arrangement, and (when both are isolated) they must lie in the same face.
  //   * modify_vertex(v, p) / modify_edge(h, cv): p (cv) must be geometrically EQUAL to the
  //     current point (curve); v must not lie on an open boundary, h must not be fictitious.
  //   * split_edge(h, cv1, cv2): cv1/cv2 must be the two halves of h's curve sharing the split
  //     point.  merge_edge(h1, h2, cv): h1 and h2 must share a degree-2 vertex not on an open
  //     boundary, and cv must be their union.
  //   * remove_isolated_vertex(v): v must be isolated (checked HERE, not only by CGAL, so that
  //     the check survives an assertions-off build).
  //
  // insert_at_vertices() additionally replicates CGAL's *containment* preconditions BEFORE the
  // call: CGAL frees v1's isolated-vertex record and only afterwards asserts that the two
  // vertices lie in the same face (Arrangement_on_surface_2_impl.h:1026-1050/1081), so letting
  // that assertion fire leaves the DCEL with a dangling DIso_vertex* — see
  // reject_bad_insert_at_vertices().
  // None of them record anything in the curve history (the curve is not an input curve); the two
  // exceptions are split_edge/merge_edge/modify_edge, which carry the ORIGINAL edge's
  // originating-curve set over to the new curve so that the history stays consistent.
  VH insert_point_in_face_interior(const Geom& p, FH f) override {
    reject_reentrant("insert_point_in_face_interior");
    sync();
    RepairOnThrow repair(this);
    FHandle fh = check_f(f);
    return track_v(m_arr.insert_in_face_interior(point_in(p), fh));
  }
  HH insert_in_face_interior(const Geom& xc, FH f) override {
    reject_reentrant("insert_in_face_interior");
    sync();
    reject_unsweepable(xc);
    RepairOnThrow repair(this);
    FHandle fh = check_f(f);
    // No history: the curve is not an input curve, so its data list stays empty.
    return track_h(m_arr.insert_in_face_interior(DXcv(xcurve_in(xc)), fh));
  }
  HH insert_from_left_vertex(const Geom& xc, VH v) override {
    reject_reentrant("insert_from_left_vertex");
    sync();
    reject_unsweepable(xc);
    RepairOnThrow repair(this);
    VHandle vh = check_v(v);
    return track_h(m_arr.insert_from_left_vertex(DXcv(xcurve_in(xc)), vh));
  }
  HH insert_from_right_vertex(const Geom& xc, VH v) override {
    reject_reentrant("insert_from_right_vertex");
    sync();
    reject_unsweepable(xc);
    RepairOnThrow repair(this);
    VHandle vh = check_v(v);
    return track_h(m_arr.insert_from_right_vertex(DXcv(xcurve_in(xc)), vh));
  }
  HH insert_at_vertices(const Geom& xc, VH v1, VH v2) override {
    reject_reentrant("insert_at_vertices");
    sync();
    reject_unsweepable(xc);
    RepairOnThrow repair(this);
    VHandle a = check_v(v1), b = check_v(v2);
    reject_bad_insert_at_vertices(a, b);
    return track_h(m_arr.insert_at_vertices(DXcv(xcurve_in(xc)), a, b));
  }
  VH modify_vertex(VH v, const Geom& p) override {
    reject_reentrant("modify_vertex");
    sync();
    RepairOnThrow repair(this);
    VHandle vh = check_v(v);
    return track_v(m_arr.modify_vertex(vh, point_in(p)));
  }
  FH remove_isolated_vertex(VH v) override {
    reject_reentrant("remove_isolated_vertex");
    sync();
    RepairOnThrow repair(this);
    VHandle vh = check_v(v);
    // The sphere's boundary rule is checked FIRST: it is the more specific restriction and it is
    // what the documented contract of this method names for a pole / identification vertex.
    reject_boundary_vertex_removal(vh, "remove_isolated_vertex");
    // CGAL only has a *precondition* for the isolation, which disappears under ARR2D_NDEBUG=1 and
    // then makes CGAL read the Arr_vertex halfedge/isolated-vertex union through the wrong member
    // and free a halfedge record as if it were an isolated-vertex record.  Check it ourselves.
    if (!vh->is_isolated())
      throw_error(ErrorCode::InvalidArgument,
                  "remove_isolated_vertex() requires an isolated vertex (use remove_vertex())");
    return track_f(m_arr.remove_isolated_vertex(vh));
  }
  HH modify_edge(HH h, const Geom& xc) override {
    reject_reentrant("modify_edge");
    sync();
    RepairOnThrow repair(this);
    HHandle e = check_h(h);
    if (e->is_fictitious()) throw_error(ErrorCode::InvalidArgument, "cannot modify a fictitious edge");
    // `xc` must be geometrically EQUAL to e->curve(), so it carries the same supporting curve.
    const std::uint64_t support = ops().supporting_curve_id(box_xcurve(e->curve()));
    reject_unsweepable_replacement(&xc, 1, &support, 1);
    // Keep the originating-curve set of the edge so that the history stays consistent.
    DXcv cv(xcurve_in(xc), e->curve().data());
    return track_h(m_arr.modify_edge(e, cv));
  }
  HH split_edge(HH h, const Geom& xc1, const Geom& xc2) override {
    reject_reentrant("split_edge");
    sync();
    RepairOnThrow repair(this);
    HHandle e = check_h(h);
    if (e->is_fictitious()) throw_error(ErrorCode::InvalidArgument, "cannot split a fictitious edge");
    // Arrangement_on_surface_2::split_edge is name-hidden by the with-history 2-argument
    // split_edge (arrangement_with_history.md gotcha 3) — qualify it.
    // Both halves inherit the edge's originating curves (they are parts of the same curves), so
    // the history stays consistent, exactly like the with-history split_edge_at_point().
    // `xc1`/`xc2` are the two halves of e->curve(), so they carry its supporting curve and
    // cannot introduce a new dangerous pair; verified in O(1), full pre-flight otherwise.
    const std::uint64_t support = ops().supporting_curve_id(box_xcurve(e->curve()));
    const Geom halves[2] = {xc1, xc2};
    reject_unsweepable_replacement(halves, 2, &support, 1);
    DXcv c1(xcurve_in(xc1), e->curve().data());
    DXcv c2(xcurve_in(xc2), e->curve().data());
    return track_h(m_arr.Base_arr::split_edge(e, c1, c2));
  }
  HH merge_edge(HH h1, HH h2, const Geom& xc) override {
    reject_reentrant("merge_edge");
    sync();
    RepairOnThrow repair(this);
    TrapGuard no_trap(this);   // CGAL 6.1: an attached trapezoid RIC PL breaks on any merge
    HHandle a = check_h(h1), b = check_h(h2);
    if (a->is_fictitious() || b->is_fictitious())
      throw_error(ErrorCode::InvalidArgument, "cannot merge fictitious edges");
    // `xc` is the union of the two edge curves, so it carries the supporting curve of one of
    // them; verified in O(1), full pre-flight otherwise.
    const std::uint64_t support[2] = {ops().supporting_curve_id(box_xcurve(a->curve())),
                                      ops().supporting_curve_id(box_xcurve(b->curve()))};
    reject_unsweepable_replacement(&xc, 1, support, 2);
    DXcv cv(xcurve_in(xc), a->curve().data());
    HHandle m = m_arr.Base_arr::merge_edge(a, b, cv);
    // CGAL returns a default-constructed (unusable) handle when the two edges share no vertex and
    // preconditions are compiled out (arrangement_core.md §6.13) — never dereference that.
    if (m == HHandle())
      throw_error(ErrorCode::InvalidArgument, "merge_edge: the two edges do not share a vertex");
    return track_h(m);
  }
  FH remove_edge(HH h, bool remove_source, bool remove_target) override {
    reject_reentrant("remove_edge");
    sync();
    RepairOnThrow repair(this);
    HHandle e = check_h(h);
    if (e->is_fictitious()) throw_error(ErrorCode::InvalidArgument, "cannot remove a fictitious edge");
    return track_f(m_arr.remove_edge(e, remove_source, remove_target));
  }

  // ---- with-history operations & global insertion functions --------------
  CH insert_curve(const Geom& c) override {
    reject_reentrant("insert");
    sync();
    reject_unbounded_overlap(c, /*aggregate=*/false);
    reject_unsweepable(c);
    reject_identification_curve(&c, 1);
    RepairOnThrow repair(this);
    Geom store;
    const Curve_2& cv = curve_in(c, store);
    Curve_handle ch = CGAL::insert(m_arr, cv);
    sync();
    return track_c(ch);
  }

  void insert_curves(const std::vector<Geom>& cs, std::vector<CH>& out) override {
    reject_reentrant("insert");
    sync();
    out.clear();
    if (cs.empty()) return;
    for (const Geom& g : cs) reject_unbounded_overlap(g, /*aggregate=*/true);
    reject_unsweepable(cs.data(), cs.size());
    reject_identification_curve(cs.data(), cs.size());
    RepairOnThrow repair(this);
    std::vector<Geom> store(cs.size());
    std::vector<Curve_2> curves;
    curves.reserve(cs.size());
    for (std::size_t i = 0; i < cs.size(); ++i) curves.push_back(curve_in(cs[i], store[i]));

    std::unordered_set<const void*> before;
    for (auto it = arr().curves_begin(); it != arr().curves_end(); ++it) before.insert(&*it);

    CGAL::insert(m_arr, curves.begin(), curves.end());   // aggregate (sweep) insertion
    sync();

    // Curves are appended in input order, so a forward walk of the list yields them in order.
    for (auto it = arr().curves_begin(); it != arr().curves_end(); ++it)
      if (before.find(&*it) == before.end()) out.push_back(track_c(it));
  }

  HH insert_non_intersecting_curve(const Geom& xc) override {
    reject_reentrant("insert_non_intersecting_curve");
    sync();
    reject_unbounded_overlap(xc, /*aggregate=*/false);
    reject_unsweepable(xc);
    reject_identification_curve(&xc, 1);
    RepairOnThrow repair(this);
    HHandle he = CGAL::insert_non_intersecting_curve(m_arr, DXcv(xcurve_in(xc)));
    if (he == HHandle())   // precondition violated with assertions compiled out
      throw_error(ErrorCode::InvalidArgument,
                  "insert_non_intersecting_curve: the curve intersects the arrangement");
    sync();
    return track_h(he);
  }

  void insert_non_intersecting_curves(const std::vector<Geom>& xcs) override {
    reject_reentrant("insert_non_intersecting_curves");
    sync();
    if (xcs.empty()) return;
    reject_unsweepable(xcs.data(), xcs.size());
    reject_identification_curve(xcs.data(), xcs.size());
    RepairOnThrow repair(this);
    std::vector<DXcv> curves;
    curves.reserve(xcs.size());
    for (const Geom& g : xcs) curves.push_back(DXcv(xcurve_in(g)));
    CGAL::insert_non_intersecting_curves(m_arr, curves.begin(), curves.end());
    sync();
  }

  VH insert_point(const Geom& p) override {
    reject_reentrant("insert_point");
    sync();
    RepairOnThrow repair(this);
    VHandle v = CGAL::insert_point(m_arr, point_in(p));
    sync();
    return track_v(v);
  }

  bool remove_vertex(VH v) override {
    reject_reentrant("remove_vertex");
    sync();
    RepairOnThrow repair(this);
    TrapGuard no_trap(this);   // removing a degree-2 vertex MERGES its two edges
    VHandle vh = check_v(v);
    reject_boundary_vertex_removal(vh, "remove_vertex");
    bool removed = CGAL::remove_vertex(m_arr, vh);
    sync();
    return removed;
  }

  std::size_t remove_curve(CH c) override {
    reject_reentrant("remove_curve");
    sync();
    RepairOnThrow repair(this);
    Curve_handle ch = check_c(c);
    std::size_t n = CGAL::remove_curve(m_arr, ch);
    m_curve_ids.erase(c.p);
    sync();
    return n;
  }

  HH split_edge_at_point(HH h, const Geom& p) override {
    reject_reentrant("split_edge");
    sync();
    RepairOnThrow repair(this);
    HHandle e = check_h(h);
    if (e->is_fictitious()) throw_error(ErrorCode::InvalidArgument, "cannot split a fictitious edge");
    // CGAL's with-history split_edge(e, p) has NO precondition that `p` lies on e's curve, and
    // neither has the geodesic Split_2 it ends up calling (Arr_geodesic_arc_on_sphere_traits_2.h:
    // 2258-2266 only rejects a degenerate arc and the two endpoints).  A sphere edge would then
    // be split into two arcs that do not lie on the stored great circle and the arrangement
    // becomes invalid, so verify containment ourselves for every kind (the other six traits do
    // have the precondition, and checking it twice only costs one Compare_y_at_x_2).
    const Geom cv = box_xcurve(e->curve());
    bool interior = false;
    try {
      const KindOps& o = ops();
      interior = o.is_in_x_range(cv, p) && o.compare_y_at_x(p, cv) == 0;
      if (interior && o.xcurve_has_source(cv) && o.point_equal(p, o.xcurve_source(cv)))
        interior = false;
      if (interior && o.xcurve_has_target(cv) && o.point_equal(p, o.xcurve_target(cv)))
        interior = false;
    } catch (...) {
      // The predicates themselves have preconditions (the sphere's Compare_x_2 needs
      // `is_no_boundary()`); a point we cannot even test is certainly not a legal split point.
      interior = false;
    }
    if (!interior)
      throw_error(ErrorCode::InvalidArgument,
                  "split_edge: the point does not lie in the interior of the edge's curve");
    return track_h(m_arr.split_edge(e, point_in(p)));   // with-history overload
  }

  HH merge_edge_history(HH h1, HH h2) override {
    reject_reentrant("merge_edge");
    sync();
    RepairOnThrow repair(this);
    TrapGuard no_trap(this);   // CGAL 6.1: an attached trapezoid RIC PL breaks on any merge
    HHandle a = check_h(h1), b = check_h(h2);
    if (a->is_fictitious() || b->is_fictitious())
      throw_error(ErrorCode::InvalidArgument, "cannot merge fictitious edges");
    if (!m_arr.are_mergeable(a, b))
      throw_error(ErrorCode::InvalidArgument,
                  "the two edges are not mergeable (no shared degree-2 vertex, or they come from "
                  "different input curves)");
    return track_h(m_arr.merge_edge(a, b));   // with-history overload
  }

  // ---- history queries ---------------------------------------------------
  Geom curve_geometry(CH c) const override {
    Curve_handle ch = check_c(c);
    const Curve_2& cv = *ch;   // Curve_halfedges IS-A Curve_2
    return box_curve(cv);
  }
  std::size_t number_of_induced_edges(CH c) const override {
    return m_arr.number_of_induced_edges(check_c(c));
  }
  void induced_edges(CH c, std::vector<HH>& out) const override {
    Curve_handle ch = check_c(c);
    out.clear();
    for (auto it = m_arr.induced_edges_begin(ch); it != m_arr.induced_edges_end(ch); ++it)
      out.push_back(track_h(*it));
  }
  std::size_t number_of_originating_curves(HH h) const override {
    HHandle e = check_h(h);
    // A fictitious halfedge has a null curve pointer; Arrangement_with_history_2 dereferences it
    // (CGAL/Arr_dcel_base.h:192, Halfedge::curve()) -> assertion with CGAL_DEBUG, UB without it.
    if (e->is_fictitious())
      throw_error(ErrorCode::Unsupported, "fictitious halfedges carry no originating curves");
    return m_arr.number_of_originating_curves(e);
  }
  void originating_curves(HH h, std::vector<CH>& out) const override {
    HHandle e = check_h(h);
    if (e->is_fictitious())
      throw_error(ErrorCode::Unsupported, "fictitious halfedges carry no originating curves");
    out.clear();
    for (auto it = m_arr.originating_curves_begin(e); it != m_arr.originating_curves_end(e); ++it) {
      Curve_handle ch = it;   // Originating_curve_iterator -> Curve_iterator
      out.push_back(track_c(ch));
    }
  }

  // ---- point location ----------------------------------------------------
  bool supports_point_location(int strategy) const override {
    switch (strategy) {
      case PL_DEFAULT: return true;
      case PL_NAIVE: return Policy::supports_naive;
      case PL_SIMPLE: return Policy::supports_simple;
      case PL_WALK: return Policy::supports_walk;
      case PL_LANDMARKS: return Policy::supports_landmarks && landmarks_usable();
      case PL_TRAPEZOID: return Policy::supports_trapezoid;
      case PL_TRIANGULATION: return Policy::supports_triangulation;
      default: return false;
    }
  }

  void attach_point_location(int strategy) override {
    reject_reentrant("attach_point_location");
    sync();
    if (strategy == PL_DEFAULT)
      throw_error(ErrorCode::InvalidArgument, "cannot attach the 'default' point-location strategy");
    require_pl(strategy);
    switch (strategy) {
      case PL_NAIVE:
        if (!m_pl_naive) m_pl_naive.reset(new Naive_pl(m_arr));
        return;
      case PL_SIMPLE:
        if constexpr (Policy::supports_simple) { if (!m_pl_simple) m_pl_simple.reset(new Simple_pl(m_arr)); }
        return;
      case PL_WALK:
        if constexpr (Policy::supports_walk) { if (!m_pl_walk) m_pl_walk.reset(new Walk_pl(m_arr)); }
        return;
      case PL_LANDMARKS:
        // Owns its default (vertices) generator; the generator is an observer that rebuilds the
        // landmark set on every local change (point_location_and_decomposition.md gotcha 10).
        if constexpr (Policy::supports_landmarks) {
          if (!m_pl_landmarks) m_pl_landmarks.reset(new Landmarks_pl(m_arr));
        }
        return;
      case PL_TRAPEZOID:
        // Observer-backed and incrementally maintained; its ctor const_casts and attaches.
        if constexpr (Policy::supports_trapezoid) {
          if (!m_pl_trap) m_pl_trap.reset(new Trap_pl(m_arr));
        }
        return;
      case PL_TRIANGULATION:
        if constexpr (Policy::supports_triangulation) {
          if (!m_pl_tri) m_pl_tri.reset(new Tri_pl(m_arr));
        }
        return;
      default:
        throw_error(ErrorCode::InvalidArgument, "unknown point-location strategy");
    }
  }

  void detach_point_location(int strategy) override {
    reject_reentrant("detach_point_location");
    switch (strategy) {
      case PL_NAIVE: m_pl_naive.reset(); return;
      case PL_SIMPLE: if constexpr (Policy::supports_simple) m_pl_simple.reset(); return;
      case PL_WALK: if constexpr (Policy::supports_walk) m_pl_walk.reset(); return;
      case PL_LANDMARKS: if constexpr (Policy::supports_landmarks) m_pl_landmarks.reset(); return;
      case PL_TRAPEZOID: if constexpr (Policy::supports_trapezoid) m_pl_trap.reset(); return;
      case PL_TRIANGULATION: if constexpr (Policy::supports_triangulation) m_pl_tri.reset(); return;
      default: return;
    }
  }

  bool has_point_location(int strategy) const override {
    switch (strategy) {
      case PL_NAIVE: return bool(m_pl_naive);
      case PL_SIMPLE: return bool(m_pl_simple);
      case PL_WALK: return bool(m_pl_walk);
      case PL_LANDMARKS: return bool(m_pl_landmarks);
      case PL_TRAPEZOID: return bool(m_pl_trap);
      case PL_TRIANGULATION: return bool(m_pl_tri);
      default: return false;
    }
  }

  Located locate(const Geom& gp, int strategy) const override {
    sync();
    const Point_2& p = point_in(gp);
    if (strategy == PL_DEFAULT) {
      if constexpr (Policy::supports_trapezoid) { if (m_pl_trap) return from_pl(m_pl_trap->locate(p)); }
      if constexpr (Policy::supports_landmarks) {
        // An attached landmarks object stays attached when an unbounded curve is inserted (the
        // insertion itself is safe); only the QUERY is not, so the default strategy falls back
        // to the walk instead of using it.  See landmarks_usable().
        if (m_pl_landmarks && landmarks_usable()) return from_pl(m_pl_landmarks->locate(p));
      }
      Default_pl pl(m_arr);
      return from_pl(pl.locate(p));
    }
    require_pl(strategy);
    switch (strategy) {
      case PL_NAIVE: {
        if (m_pl_naive) return from_pl(m_pl_naive->locate(p));
        Naive_pl pl(m_arr);
        return from_pl(pl.locate(p));
      }
      case PL_SIMPLE:
        if constexpr (Policy::supports_simple) {
          if (m_pl_simple) return from_pl(m_pl_simple->locate(p));
          Simple_pl pl(m_arr);
          return from_pl(pl.locate(p));
        }
        break;
      case PL_WALK:
        if constexpr (Policy::supports_walk) {
          if (m_pl_walk) return from_pl(m_pl_walk->locate(p));
          Walk_pl pl(m_arr);
          return from_pl(pl.locate(p));
        }
        break;
      case PL_LANDMARKS:
        if constexpr (Policy::supports_landmarks) {
          if (m_pl_landmarks) return from_pl(m_pl_landmarks->locate(p));
          Landmarks_pl pl(m_arr);
          return from_pl(pl.locate(p));
        }
        break;
      case PL_TRAPEZOID:
        if constexpr (Policy::supports_trapezoid) {
          if (m_pl_trap) return from_pl(m_pl_trap->locate(p));
          Trap_pl pl(m_arr);
          return from_pl(pl.locate(p));
        }
        break;
      case PL_TRIANGULATION:
        if constexpr (Policy::supports_triangulation) {
          if (m_pl_tri) return from_pl(m_pl_tri->locate(p));
          Tri_pl pl(m_arr);
          return from_pl(pl.locate(p));
        }
        break;
      default: break;
    }
    throw_error(ErrorCode::InvalidArgument, "unknown point-location strategy");
  }

  Located ray_shoot_up(const Geom& p, int strategy) const override { return ray_shoot(p, strategy, true); }
  Located ray_shoot_down(const Geom& p, int strategy) const override { return ray_shoot(p, strategy, false); }

  /// CGAL precondition (sphere kind): the batched sweep compares points with the traits'
  /// Compare_x_2, which requires `p.is_no_boundary()`.  Worse, a query point at the NORTH POLE
  /// makes it dereference a null halfedge (EXC_BAD_ACCESS in
  /// Arr_batched_point_location_traits_2.h:114; STAGE2_NOTES kind_sphere).  Such query points are
  /// therefore answered ONE BY ONE with the naive strategy, which is safe everywhere, and only
  /// the interior ones go through the sweep.  Planar kinds are unrestricted.
  void batched_locate(const std::vector<Geom>& pts, std::vector<Located>& out) const override {
    sync();
    out.assign(pts.size(), Located::none());
    if (pts.empty()) return;

    // Indices of the queries that are handed to the batched sweep (all of them, except the
    // sphere's pole / identification-curve points).
    std::vector<std::size_t> swept;
    swept.reserve(pts.size());
    if constexpr (Types::is_sphere) {
      Naive_pl pl(m_arr);
      for (std::size_t i = 0; i < pts.size(); ++i) {
        const Point_2& p = point_in(pts[i]);
        if (p.is_no_boundary()) swept.push_back(i);
        else out[i] = from_pl(pl.locate(p));
      }
      if (swept.empty()) return;
    } else {
      swept.resize(pts.size());
      std::iota(swept.begin(), swept.end(), std::size_t(0));
    }

    std::vector<Point_2> ps;
    ps.reserve(swept.size());
    for (std::size_t i : swept) ps.push_back(point_in(pts[i]));

    std::vector<std::pair<Point_2, PlResult>> res;
    res.reserve(ps.size());
    CGAL::locate(m_arr, ps.begin(), ps.end(), std::back_inserter(res));

    // CGAL::locate emits its results in increasing xy-lexicographic order of the query points
    // (point_location_and_decomposition.md §10), so re-establish the input order by sorting the
    // input indices with the traits' Compare_xy_2 and merging.  NOTE: the sweep merges EQUAL
    // query points into a single event, so there is one result per *distinct* point, not per
    // query — matching by value (not by position) is required.  A query point for which CGAL
    // emitted nothing keeps Located::none().
    const KindOps& o = ops();
    std::vector<std::size_t> order = swept;
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
      return o.point_compare_xy(pts[a], pts[b]) < 0;
    });
    std::size_t i = 0;
    for (std::size_t k = 0; k < res.size() && i < order.size(); ++k) {
      Geom rp = box_point(res[k].first);
      while (i < order.size() && o.point_compare_xy(pts[order[i]], rp) < 0) ++i;
      Located loc = from_pl(res[k].second);
      while (i < order.size() && o.point_compare_xy(pts[order[i]], rp) == 0) out[order[i++]] = loc;
    }
  }

  void zone(const Geom& c, std::vector<Located>& out) override {
    sync();
    reject_unsweepable(c);
    out.clear();
    std::vector<Geom> pieces;
    split_into_xcurves(c, pieces);
    Default_pl pl(m_arr);
    for (const Geom& piece : pieces) {
      if (piece.type == GeomType::Point) {   // isolated point produced by make_x_monotone
        out.push_back(from_pl(pl.locate(piece.template as<Point_2>())));
        continue;
      }
      std::list<ZoneElem> elems;
      try {
        CGAL::zone(m_arr, DXcv(xcurve_in(piece)), std::back_inserter(elems), pl);
      } catch (const std::bad_variant_access&) {
        throw_zone_overlap_error();
      }
      for (const ZoneElem& e : elems) {
        if (const auto* v = std::get_if<VHandle>(&e)) out.push_back(Located::vertex(track_v(*v)));
        else if (const auto* h = std::get_if<HHandle>(&e)) out.push_back(Located::halfedge(track_h(*h)));
        else if (const auto* f = std::get_if<FHandle>(&e)) out.push_back(Located::face(track_f(*f)));
      }
    }
  }

  bool do_intersect(const Geom& c) override {
    sync();
    reject_unsweepable(c);
    Default_pl pl(m_arr);
    try {
      if (is_xcurve_box(c)) return CGAL::do_intersect(m_arr, DXcv(xcurve_in(c)), pl);
      Geom store;
      const Curve_2& cv = curve_in(c, store);
      return CGAL::do_intersect(m_arr, Data_curve_2(cv, nullptr), pl);
    } catch (const std::bad_variant_access&) {
      throw_zone_overlap_error();
    }
  }

  /// CGAL precondition (sphere kind): same restriction as batched_locate() — the vertical
  /// decomposition sweep calls Compare_x_2 on every vertex, which requires that no vertex lies on
  /// a pole or on the identification curve.  Verified: a spherical triangle inside one octant
  /// decomposes fine, one with a vertex at the north pole raises the CGAL precondition.
  void decompose(std::vector<VerticalDecompositionEntry>& out) const override {
    sync();
    if constexpr (Types::is_sphere) {
      // CGAL raises `p1.is_no_boundary()` (Arr_geodesic_arc_on_sphere_traits_2.h:1038, from
      // Arr_vert_decomp_ss_visitor::after_handle_event) as soon as ANY vertex sits on a pole or
      // on the identification meridian.  Report our own documented error instead (O(V) scan).
      for (auto v = arr().vertices_begin(); v != arr().vertices_end(); ++v)
        if (v->parameter_space_in_x() != CGAL::ARR_INTERIOR ||
            v->parameter_space_in_y() != CGAL::ARR_INTERIOR)
          throw_error(ErrorCode::Unsupported,
                      "sphere: vertical decomposition requires every vertex to be in the interior "
                      "of the parameter space (no pole, no identification vertex)");
    }
    using Cell = std::variant<VCHandle, HCHandle, FCHandle>;
    using VertT = std::optional<Cell>;
    using Entry = std::pair<VCHandle, std::pair<VertT, VertT>>;
    std::list<Entry> res;
    CGAL::decompose(m_arr, std::back_inserter(res));
    out.clear();
    out.reserve(res.size());
    for (const Entry& e : res) {
      VerticalDecompositionEntry ve;
      ve.v = track_v(nc(e.first));
      ve.below = from_optional_cell(e.second.first);
      ve.above = from_optional_cell(e.second.second);
      out.push_back(ve);
    }
  }

  // ---- observers ---------------------------------------------------------
  int add_observer(void* user, ObserverFn fn) override {
    int token = m_next_token++;
    m_py_obs.push_back(PyObserver{token, user, fn});
    return token;
  }
  void remove_observer(int token) override {
    m_py_obs.erase(std::remove_if(m_py_obs.begin(), m_py_obs.end(),
                                  [token](const PyObserver& o) { return o.token == token; }),
                   m_py_obs.end());
  }

  // ---- overlay -----------------------------------------------------------
  void overlay_with(const ArrBase& other, ArrBase& result, void* user, OverlayFn fn) const override {
    reject_reentrant("overlay");
    const ArrImpl* b = as_impl(other);
    ArrImpl* r = as_impl_mut(result);
    b->reject_reentrant("overlay");
    r->reject_reentrant("overlay");
    if (r == this || r == b)
      throw_error(ErrorCode::InvalidArgument, "the overlay result must be a distinct arrangement");
    if (!r->m_arr.is_empty() || r->m_arr.number_of_curves() != 0)
      throw_error(ErrorCode::InvalidArgument, "the overlay result arrangement must be empty");
    sync();
    b->sync();
    // The overlay sweep meets A's curves with B's; each set is internally safe (every insertion
    // path pre-checks), but the union may not be.
    if (ops().needs_sweep_precheck()) {
      std::vector<Geom> other_curves;
      other_curves.reserve(b->m_arr.number_of_edges());
      for (auto eit = b->arr().edges_begin(); eit != b->arr().edges_end(); ++eit)
        other_curves.push_back(box_xcurve(eit->curve()));
      reject_unsweepable(other_curves.data(), other_curves.size());
    }
    OverlayTraits ovl(this, b, r, user, fn);
    CGAL::overlay(m_arr, b->m_arr, r->m_arr, ovl);
    r->rescan();
  }

  // ---- bulk export -------------------------------------------------------
  void vertex_coordinates(std::vector<double>& out) const override {
    sync();
    const int dim = ops().dimension();
    out.clear();
    out.reserve(std::size_t(dim) * m_arr.number_of_vertices());
    double buf[3] = {0, 0, 0};
    for (auto v = arr().vertices_begin(); v != arr().vertices_end(); ++v) {
      ops().point_approx(box_point(v->point()), buf);
      for (int i = 0; i < dim; ++i) out.push_back(buf[i]);
    }
  }

  void edge_vertex_indices(std::vector<std::size_t>& out) const override {
    sync();
    std::unordered_map<const void*, std::size_t> index;
    build_vertex_index(index);
    out.clear();
    out.reserve(2 * m_arr.number_of_edges());
    const std::size_t none = std::numeric_limits<std::size_t>::max();
    for (auto e = arr().edges_begin(); e != arr().edges_end(); ++e) {
      HHandle h(e);
      out.push_back(vertex_index(index, h->source(), none));
      out.push_back(vertex_index(index, h->target(), none));
    }
  }

  void face_boundaries(std::vector<std::vector<std::vector<std::size_t>>>& out) const override {
    sync();
    std::unordered_map<const void*, std::size_t> index;
    build_vertex_index(index);
    out.clear();
    out.reserve(m_arr.number_of_faces());
    for (auto f = arr().faces_begin(); f != arr().faces_end(); ++f) {
      std::vector<std::vector<std::size_t>> cycles;
      for (auto o = f->outer_ccbs_begin(); o != f->outer_ccbs_end(); ++o)
        cycles.push_back(ccb_indices(*o, index));
      for (auto o = f->inner_ccbs_begin(); o != f->inner_ccbs_end(); ++o)
        cycles.push_back(ccb_indices(*o, index));
      out.push_back(std::move(cycles));
    }
  }

  BBox bbox() const override {
    sync();
    BBox b;
    b.dim = ops().dimension();
    bool first = true;
    double buf[3] = {0, 0, 0};
    for (auto v = arr().vertices_begin(); v != arr().vertices_end(); ++v) {
      ops().point_approx(box_point(v->point()), buf);
      for (int i = 0; i < b.dim; ++i) {
        if (first) { b.lo[i] = b.hi[i] = buf[i]; }
        else { b.lo[i] = std::min(b.lo[i], buf[i]); b.hi[i] = std::max(b.hi[i], buf[i]); }
      }
      first = false;
    }
    if (first) for (int i = 0; i < 3; ++i) b.lo[i] = b.hi[i] = 0.0;
    return b;
  }

  // =========================================================================
  //  Internals
  // =========================================================================
 private:
  // ---- boxing / unboxing -------------------------------------------------
  static Geom box_point(const Point_2& p) { return make_geom(Types::kind, GeomType::Point, p); }
  static Geom box_curve(const Curve_2& c) { return make_geom(Types::kind, GeomType::Curve, c); }
  static Geom box_xcurve(const X_monotone_curve_2& c) { return make_geom(Types::kind, GeomType::XCurve, c); }

  static const Point_2& point_in(const Geom& g) {
    require_point(g, Types::kind);
    return g.template as<Point_2>();
  }

  /// True when the box holds an X_monotone_curve_2 usable as such (a Curve box also qualifies for
  /// the kinds where Curve_2 and X_monotone_curve_2 are the same C++ type).
  static bool is_xcurve_box(const Geom& g) {
    return g.kind == Types::kind && g.template holds<X_monotone_curve_2>() &&
           (g.type == GeomType::XCurve || g.type == GeomType::Curve);
  }

  static const X_monotone_curve_2& xcurve_in(const Geom& g) {
    require_kind(g, Types::kind);
    if (g.template holds<X_monotone_curve_2>() &&
        (g.type == GeomType::XCurve || g.type == GeomType::Curve))
      return g.template as<X_monotone_curve_2>();
    require_xcurve(g, Types::kind);          // throws NotXMonotone / InvalidArgument
    return g.template as<X_monotone_curve_2>();
  }

  /// A general Curve_2 out of `g`.  `storage` keeps a converted temporary alive when the caller
  /// passed an x-monotone box of a kind whose Curve_2 differs from X_monotone_curve_2.
  const Curve_2& curve_in(const Geom& g, Geom& storage) const {
    require_kind(g, Types::kind);
    if (g.template holds<Curve_2>() && (g.type == GeomType::Curve || g.type == GeomType::XCurve))
      return g.template as<Curve_2>();
    if (g.type == GeomType::XCurve) {
      storage = ops().to_curve(g);           // Unsupported propagates
      return storage.template as<Curve_2>();
    }
    require_any_curve(g, Types::kind);
    return g.template as<Curve_2>();
  }

  /// Split a general curve into x-monotone pieces (and isolated points); an x-monotone input is
  /// passed through unchanged.
  void split_into_xcurves(const Geom& c, std::vector<Geom>& out) const {
    out.clear();
    if (is_xcurve_box(c)) { out.push_back(c); return; }
    require_any_curve(c, Types::kind);
    ops().make_x_monotone(c, out);
  }

  // ---- CGAL 6.1 guards ---------------------------------------------------

  /// CGAL_TRAPS_CHECKLIST "Arrangement core": inserting an UNBOUNDED curve (a line or a ray) that
  /// OVERLAPS an existing edge aborts inside `Arr_linear_traits_2::Construct_min_vertex_2`
  /// (`CGAL_precondition(cv.has_left())`, Arr_linear_traits_2.h:689) as soon as the overlap has
  /// no left endpoint.  Only the Linear kind has unbounded curves, so this is a no-op everywhere
  /// else and costs nothing for a bounded input curve.
  ///
  /// Two deviations from the checklist wording, both documented here:
  ///  * the candidate edges are found by scanning the arrangement's edges rather than by
  ///    `CGAL::zone()`, because zone() drives exactly the same insertion machinery and trips the
  ///    same precondition on the offending input before it could report anything.  The scan is
  ///    O(E) and only runs for an unbounded input curve;
  ///  * only an overlap that CGAL actually mishandles is refused.  For the INCREMENTAL path
  ///    (`aggregate == false`) that is an overlap without a left endpoint, exactly the
  ///    `cv.has_left()` precondition; an overlap bounded on the left (inserting the ray from
  ///    (2,0) towards (5,0) into an arrangement holding the line y = 0) is legal and CGAL gets
  ///    it right.  The AGGREGATE sweep (`aggregate == true`, used by insert_curves) is stricter:
  ///    it also asserts `! e->is_fictitious()` (Arrangement_on_surface_2_impl.h:1517) when the
  ///    overlap is unbounded on the RIGHT, so both ends are checked there.  An overlap that is
  ///    bounded at both ends (an unbounded curve overlapping a bounded edge) is fine either way.
  void reject_unbounded_overlap(const Geom& c, bool aggregate) {
    if constexpr (!Types::is_unbounded) {
      (void)c;
      (void)aggregate;
    } else {
      if (m_arr.number_of_edges() == 0) return;
      const KindOps& o = ops();
      if (o.curve_is_bounded(c)) return;
      std::vector<Geom> pieces;
      split_into_xcurves(c, pieces);
      std::vector<IntersectionResult> res;
      for (const Geom& piece : pieces) {
        if (piece.type == GeomType::Point) continue;
        for (auto eit = arr().edges_begin(); eit != arr().edges_end(); ++eit) {
          const X_monotone_curve_2& ecv = eit->curve();
          o.intersect(piece, box_xcurve(ecv), res);
          for (const IntersectionResult& r : res) {
            if (r.is_point) continue;
            // `cv.has_left()` (Arr_linear_traits_2.h:689) is false exactly when the overlap's
            // minimal end lies on an open boundary; the aggregate sweep breaks on an open
            // maximal end as well.
            const bool left_open = o.parameter_space_in_x(r.overlap, ARR_MIN_END) != ARR_INTERIOR ||
                                   o.parameter_space_in_y(r.overlap, ARR_MIN_END) != ARR_INTERIOR;
            const bool right_open = o.parameter_space_in_x(r.overlap, ARR_MAX_END) != ARR_INTERIOR ||
                                    o.parameter_space_in_y(r.overlap, ARR_MAX_END) != ARR_INTERIOR;
            if (left_open || (aggregate && right_open))
              throw_error(ErrorCode::Unsupported,
                          "CGAL 6.1 cannot insert an unbounded curve overlapping an existing edge");
          }
        }
      }
    }
  }

  /// Sweep pre-flight (KindOps::check_sweepable): hands CGAL's sweep-line / zone machinery the
  /// complete set of curves it is about to work on — the arrangement's edge curves plus the
  /// `added` ones — so that the kind can refuse a configuration CGAL 6.1 handles by corrupting
  /// memory.  Only the BEZIER kind has such a configuration (a curve through another curve's
  /// SELF-intersection point, see KindOps::check_sweepable and the checklist); every other kind
  /// answers `needs_sweep_precheck() == false` and pays one virtual call.
  ///
  /// Every entry point that lets a NEW curve into the arrangement calls this, which gives the
  /// invariant "an arrangement never holds a dangerous pair of supporting curves"; the sweeps
  /// that take no new curve (decompose, batched_locate) are safe by that invariant, and
  /// overlay_with — which does bring two independently safe sets together — checks the union.
  /// modify_edge / split_edge / merge_edge go through reject_unsweepable_replacement() below,
  /// which reaches the same conclusion in O(1) while their CGAL precondition holds.
  void reject_unsweepable(const Geom* added, std::size_t n_added) const {
    const KindOps& o = ops();
    if (!o.needs_sweep_precheck()) return;
    std::vector<Geom> all;
    all.reserve(m_arr.number_of_edges() + n_added);
    for (auto eit = arr().edges_begin(); eit != arr().edges_end(); ++eit)
      all.push_back(box_xcurve(eit->curve()));
    for (std::size_t i = 0; i < n_added; ++i) all.push_back(added[i]);
    o.check_sweepable(all);
  }
  void reject_unsweepable(const Geom& added) const { reject_unsweepable(&added, 1); }

  /// O(1) form of the pre-flight for modify_edge / split_edge / merge_edge.  CGAL's documented
  /// precondition for those three is that the supplied x-monotone curves are the edge curve
  /// itself, its two halves, or the union of two edges — i.e. that they carry a SUPPORTING curve
  /// the arrangement already holds.  When that is the case no new pair of supporting curves can
  /// appear and there is nothing for check_sweepable() to find, so the O(E) scan is skipped; a
  /// caller that violates the precondition (or a kind that cannot answer
  /// KindOps::supporting_curve_id) falls back to the full pre-flight instead of into undefined
  /// behaviour.  `support` lists the ids of the edge curves being replaced.
  void reject_unsweepable_replacement(const Geom* added, std::size_t n_added,
                                      const std::uint64_t* support, std::size_t n_support) const {
    const KindOps& o = ops();
    if (!o.needs_sweep_precheck()) return;
    for (std::size_t i = 0; i < n_added; ++i) {
      const std::uint64_t id = o.supporting_curve_id(added[i]);
      bool known = false;
      for (std::size_t j = 0; j < n_support && !known; ++j) known = (id != 0 && id == support[j]);
      if (!known) {                       // a supporting curve the arrangement may not hold
        reject_unsweepable(added, n_added);
        return;
      }
    }
  }

  /// CGAL 6.1 bug reached by zone() / do_intersect() on the BEZIER kind: when the query curve
  /// OVERLAPS an existing edge, `Arrangement_zone_2::compute_zone()` sets m_found_overlap and
  /// then does `std::get<X_monotone_curve_2>(*_compute_next_intersection(...))`
  /// (Arrangement_zone_2_impl.h:214) on a variant that holds an intersection POINT, throwing
  /// std::bad_variant_access out of CGAL.  Reported as our documented Unsupported error rather
  /// than as an opaque RuntimeError (measured only for Bezier; the check is generic and free).
  [[noreturn]] static void throw_zone_overlap_error() {
    throw_error(ErrorCode::Unsupported,
                std::string(kind_name(Types::kind)) +
                    ": CGAL 6.1 cannot compute the zone of a curve that overlaps an existing "
                    "edge (std::bad_variant_access in Arrangement_zone_2)");
  }

  /// CGAL_TRAPS_CHECKLIST "Sphere kind" / traits_geodesic_sphere.md gotcha G4: removing a vertex
  /// that lies on a pole or on the identification curve leaves the spherical topology traits'
  /// `north_pole()/south_pole()/m_boundary_vertices` dangling, and the NEXT insertion SIGSEGVs.
  /// Refuse instead.  No-op for every planar kind.
  void reject_boundary_vertex_removal(VHandle v, const char* what) const {
    if constexpr (!Types::is_sphere) {
      (void)v; (void)what;
    } else {
      if (v->parameter_space_in_x() != CGAL::ARR_INTERIOR ||
          v->parameter_space_in_y() != CGAL::ARR_INTERIOR)
        throw_error(ErrorCode::Unsupported,
                    std::string("sphere: ") + what +
                        " is not supported for a vertex on a pole or on the identification curve "
                        "(it would leave the topology traits with dangling pointers)");
    }
  }

  /// CGAL_TRAPS_CHECKLIST "Arrangement core": `insert_at_vertices(cv, v1, v2)` frees v1's
  /// isolated-vertex record (`f1->erase_isolated_vertex(iv1); _dcel().delete_isolated_vertex(iv1)`,
  /// Arrangement_on_surface_2_impl.h:1026-1027) and only AFTERWARDS asserts that the two vertices
  /// lie in the same face (:1050 for two isolated vertices, :1081 for an isolated one joined to an
  /// incident one).  When that assertion throws — this build keeps the CGAL checks on — the vertex
  /// still reports `is_isolated() == true` while its `isolated_vertex()` pointer is dangling, so
  /// `Vertex.face`, `Face.isolated_vertices()`, `remove_isolated_vertex()` and `is_valid()` all
  /// read freed memory (SIGSEGV); with the checks off CGAL silently links an edge across two
  /// different faces.  Both containment conditions are therefore replicated HERE, before CGAL
  /// touches anything.  Nothing is checked when neither vertex is isolated: CGAL deletes no
  /// record on that path and its own preconditions are then harmless.
  void reject_bad_insert_at_vertices(VHandle a, VHandle b) const {
    const bool ia = a->is_isolated(), ib = b->is_isolated();
    if (!ia && !ib) return;
    if (ia && ib) {
      if (a->face() != b->face())
        throw_error(ErrorCode::InvalidArgument,
                    "insert_at_vertices: both vertices are isolated but lie inside different "
                    "faces (CGAL requires the same face)");
      return;
    }
    VHandle iso = ia ? a : b;
    VHandle inc = ia ? b : a;
    if (inc->degree() == 0) return;   // a degree-0, non-isolated vertex: nothing to compare with
    auto circ = inc->incident_halfedges();
    auto first = circ;
    do {
      if (circ->face() == iso->face()) return;
      ++circ;
    } while (circ != first);
    throw_error(ErrorCode::InvalidArgument,
                "insert_at_vertices: the isolated vertex does not lie in a face incident to the "
                "other vertex, so the new curve would cross the arrangement");
  }

  /// Inside `before_remove_vertex` CGAL has ALREADY deleted the halfedges around the vertex —
  /// `_remove_edge` does `_dcel().delete_edge(he1)` before notifying
  /// (Arrangement_on_surface_2_impl.h:4523/4539) and `_merge_edge` re-links `he1` before it
  /// notifies (:1721-1723) — while `is_isolated()` still answers false.  `degree()`,
  /// `incident_halfedges()` and everything routed through them then walk freed / half-relinked
  /// records and SEGFAULT.  A vertex removed through `remove_isolated_vertex()` is NOT affected
  /// (there the notification comes first, :1491), so the guard only fires for a non-isolated one.
  void reject_dead_vertex_ring(VH v, VHandle h, const char* what) const {
    if (v.p != nullptr && v.p == m_no_ring_v && !h->is_isolated())
      throw_error(ErrorCode::Unsupported,
                  std::string("vertex ") + what +
                      " is not available inside before_remove_vertex: CGAL has already deleted "
                      "the halfedges incident to this vertex (its point, id, data and "
                      "is_isolated are still readable)");
  }

  /// Nothing may modify an arrangement while a Python observer or overlay callback is running:
  /// CGAL is then in the middle of a DCEL modification and a re-entrant clear() / assign() /
  /// insert() / remove_curve() SEGFAULTs (measured: the with-history observer inserts a halfedge
  /// into a Curve_halfedges node that clear() has just destroyed) or hangs forever.  `m_in_notify`
  /// already marks that window, so use it as a hard gate.
  void reject_reentrant(const char* what) const {
    if (m_in_notify > 0)
      throw_error(ErrorCode::Unsupported,
                  std::string(what) +
                      ": the arrangement cannot be modified from inside an observer or overlay "
                      "callback (CGAL is in the middle of a modification); callbacks are "
                      "read-only with respect to the arrangements they are given");
  }

  /// CGAL_TRAPS_CHECKLIST "Sphere kind": a curve LYING ON the identification meridian cannot be
  /// inserted once the arrangement crosses that meridian — CGAL raises `f == f2` ("The two
  /// halfedges must share the same incident face", Arrangement_on_surface_2_impl.h:2699) from
  /// inside `_insert_at_vertices`, i.e. in the MIDDLE of the DCEL surgery, leaving the curve
  /// half-inserted; the next insertions then fail and eventually SIGSEGV.  With the checks off it
  /// links two halfedges on different faces instead.  The offending x-monotone piece is exactly
  /// the one whose BOTH ends lie on the left/right boundary of the parameter space (a harmless
  /// wrap-around piece has only one such end).  No-op for every planar kind.
  void reject_identification_curve(const Geom* cs, std::size_t n) const {
    if constexpr (!Types::is_sphere) {
      (void)cs; (void)n;
    } else {
      if (m_arr.is_empty()) return;   // an empty arrangement takes any single curve set
      const KindOps& o = ops();
      bool on_identification = false;
      std::vector<Geom> pieces;
      for (std::size_t i = 0; i < n && !on_identification; ++i) {
        split_into_xcurves(cs[i], pieces);
        for (const Geom& piece : pieces) {
          if (piece.type == GeomType::Point) continue;
          if (o.parameter_space_in_x(piece, ARR_MIN_END) != ARR_INTERIOR &&
              o.parameter_space_in_x(piece, ARR_MAX_END) != ARR_INTERIOR) {
            on_identification = true;
            break;
          }
        }
      }
      if (!on_identification) return;
      // The abort needs the arrangement to already CROSS the identification curve, which is
      // exactly "some vertex does not lie in the interior of the parameter space" (every
      // crossing edge creates such a vertex, and nothing else does).
      for (auto v = arr().vertices_begin(); v != arr().vertices_end(); ++v)
        if (v->parameter_space_in_x() != CGAL::ARR_INTERIOR)
          throw_error(ErrorCode::Unsupported,
                      "sphere: CGAL 6.1 cannot insert a curve lying ON the identification "
                      "meridian into an arrangement that already crosses it — it aborts in the "
                      "middle of the DCEL surgery (f == f2, "
                      "Arrangement_on_surface_2_impl.h:2699) and leaves the arrangement "
                      "half-built; insert the whole curve set with a single insert() call on an "
                      "empty arrangement instead");
    }
  }

  // ---- CGAL 6.1 curve-history repair -------------------------------------
  //
  // `Curve_halfedges_observer` (Arrangement_on_surface_with_history_2.h:246-338) keeps the
  // with-history bookkeeping symmetric by UNregistering an edge in `before_*` and REregistering
  // it in `after_*`.  An exception thrown between the two halves (CGAL 6.1 raises one from an
  // attached trapezoidal-map point location on ANY edge merge, Td_active_vertex.h:168) leaves the
  // two directions of the relation disagreeing: the curve's halfedge set no longer contains the
  // edge although the edge still names the curve.  `remove_curve()` on such a curve then FREES the
  // Curve_halfedges node while halfedges still point at it, and the next copy()/assign()/overlay()
  // dereferences the freed node (SIGSEGV inside Curve_halfedges::Less_halfedge_handle).
  //
  // The repair is deferred to the next sync() so that it never runs while the stack is unwinding
  // (the DCEL may still be mid-surgery there).

  /// True when every edge names exactly the live curves whose halfedge set contains it.
  bool history_is_consistent() const {
    Arr& a = arr();
    std::unordered_map<const void*, std::unordered_set<const void*>> induced;
    for (auto c = a.curves_begin(); c != a.curves_end(); ++c) {
      std::unordered_set<const void*>& s = induced[static_cast<const void*>(&*c)];
      for (auto it = a.induced_edges_begin(c); it != a.induced_edges_end(c); ++it) {
        HHandle h = *it;
        s.insert(static_cast<const void*>(&*h));
        s.insert(static_cast<const void*>(&*h->twin()));
      }
    }
    std::size_t named = 0;
    for (auto e = a.edges_begin(); e != a.edges_end(); ++e) {
      HHandle h(e);
      for (auto di = h->curve().data().begin(); di != h->curve().data().end(); ++di) {
        ++named;
        auto it = induced.find(static_cast<const void*>(*di));
        if (it == induced.end()) return false;                       // dangling curve pointer
        if (it->second.find(static_cast<const void*>(&*h)) == it->second.end()) return false;
      }
    }
    std::size_t total = 0;
    for (const auto& kv : induced) total += kv.second.size() / 2;
    return total == named;
  }

  /// Restore the symmetry described above.  Uses only the public
  /// `Arr_with_history_accessor::connect_curve_edge` (which does `cv._insert(he)` plus
  /// `he->curve().data().insert(&cv)`) and `_Unique_list::erase` for the dangling direction.
  void repair_history() const {
    Arr& a = arr();
    std::unordered_map<const void*, std::unordered_set<const void*>> induced;
    std::unordered_map<const void*, Curve_handle> nodes;
    for (auto c = a.curves_begin(); c != a.curves_end(); ++c) {
      const void* key = static_cast<const void*>(&*c);
      nodes.emplace(key, Curve_handle(c));
      std::unordered_set<const void*>& s = induced[key];
      for (auto it = a.induced_edges_begin(c); it != a.induced_edges_end(c); ++it) {
        HHandle h = *it;
        s.insert(static_cast<const void*>(&*h));
        s.insert(static_cast<const void*>(&*h->twin()));
      }
    }
    // The accessor must be instantiated with the ON-SURFACE with-history class: that is the type
    // `Curve_halfedges` declares as its friend, and for the six planar kinds `Arr` is the DERIVED
    // Arrangement_with_history_2, which is NOT a friend.
    using Aos_wh = CGAL::Arrangement_on_surface_with_history_2<
        typename Arr::Geometry_traits_2, typename Arr::Base_topology_traits>;
    CGAL::Arr_with_history_accessor<Aos_wh> acc(a);
    for (auto e = a.edges_begin(); e != a.edges_end(); ++e) {
      HHandle h(e);
      std::vector<Curve_halfedges*> dangling, lost;
      for (auto di = h->curve().data().begin(); di != h->curve().data().end(); ++di) {
        Curve_halfedges* p = static_cast<Curve_halfedges*>(*di);
        auto it = induced.find(static_cast<const void*>(p));
        if (it == induced.end()) dangling.push_back(p);
        else if (it->second.find(static_cast<const void*>(&*h)) == it->second.end())
          lost.push_back(p);
      }
      for (Curve_halfedges* p : dangling) h->curve().data().erase(p);
      for (Curve_halfedges* p : lost) {
        auto it = nodes.find(static_cast<const void*>(p));
        if (it != nodes.end()) acc.connect_curve_edge(it->second, h);
      }
    }
  }

  /// RAII: when the guarded operation leaves through an exception, schedule the history repair
  /// for the next sync() (running it here, during unwinding, would walk a DCEL that CGAL may
  /// still have half-modified).
  struct RepairOnThrow {
    const ArrImpl* self;
    int depth;
    explicit RepairOnThrow(const ArrImpl* s) : self(s), depth(std::uncaught_exceptions()) {}
    ~RepairOnThrow() {
      if (std::uncaught_exceptions() > depth) {
        self->m_history_dirty = true;
        self->m_dirty = true;
      }
    }
  };

  /// RAII: CGAL 6.1's `Arr_trapezoid_ric_point_location` is an observer whose
  /// `before_merge_edge` handler raises `ptr()->v == ptr()->cw_he->source()`
  /// (Arr_point_location/Td_active_vertex.h:168) on EVERY edge merge — measured for segment,
  /// polyline, circle_segment, conic and bezier.  Because the exception escapes between the
  /// with-history observer's unregister and re-register halves it also corrupts the curve
  /// history (see repair_history()).  Detach the structure around a merging operation and
  /// rebuild it afterwards, which is O(size) but correct; the strategy therefore stays usable
  /// and `attach_point_location("trapezoid")` keeps its documented "stays up to date" contract.
  struct TrapGuard {
    ArrImpl* self;
    bool had = false;
    explicit TrapGuard(ArrImpl* s) : self(s) {
      if constexpr (Policy::supports_trapezoid) {
        if (s->m_pl_trap) { s->m_pl_trap.reset(); had = true; }
      }
    }
    ~TrapGuard() {
      if constexpr (Policy::supports_trapezoid) {
        if (had) { try { self->m_pl_trap.reset(new Trap_pl(self->m_arr)); } catch (...) {} }
      }
    }
  };

  // ---- const access to the arrangement -----------------------------------
  //
  // Every "read-only" traversal in CGAL mutates something (Halfedge::inner_ccb() path-compresses
  // through const, ids are assigned lazily), so ArrImpl keeps the CGAL object non-const
  // internally and hands out non-const handles.
  Arr& arr() const { return const_cast<Arr&>(m_arr); }

  static VHandle nc(VCHandle v) { return VHandle(const_cast<Vertex*>(&*v)); }
  static HHandle nc(HCHandle h) { return HHandle(const_cast<Halfedge*>(&*h)); }
  static FHandle nc(FCHandle f) { return FHandle(const_cast<Face*>(&*f)); }

  // ---- tracking ----------------------------------------------------------
  VH track_v(VHandle v) const {
    ElementData& d = v->data();
    if (d.id == 0) d.id = m_next_id++;
    void* p = static_cast<void*>(&*v);
    m_live_v.insert(p);
    return VH{p, d.id};
  }
  HH track_h(HHandle h) const {
    ElementData& d = h->data();
    if (d.id == 0) d.id = m_next_id++;
    void* p = static_cast<void*>(&*h);
    m_live_h.insert(p);
    return HH{p, d.id};
  }
  FH track_f(FHandle f) const {
    ElementData& d = f->data();
    if (d.id == 0) d.id = m_next_id++;
    void* p = static_cast<void*>(&*f);
    m_live_f.insert(p);
    return FH{p, d.id};
  }
  void track_edge(HHandle h) const { track_h(h); track_h(h->twin()); }
  CH track_c(Curve_handle c) const {
    void* p = static_cast<void*>(&*c);
    auto it = m_curve_ids.find(p);
    if (it == m_curve_ids.end()) it = m_curve_ids.emplace(p, m_next_id++).first;
    return CH{p, it->second};
  }

  void untrack_v(VHandle v) const { m_live_v.erase(static_cast<const void*>(&*v)); }
  void untrack_h(HHandle h) const { m_live_h.erase(static_cast<const void*>(&*h)); }
  void untrack_edge(HHandle h) const { untrack_h(h); untrack_h(h->twin()); }
  void untrack_f(FHandle f) const { m_live_f.erase(static_cast<const void*>(&*f)); }

  // ---- validation --------------------------------------------------------
  bool valid_v(VH v) const {
    if (!v.p || v.id == 0 || m_live_v.find(v.p) == m_live_v.end()) return false;
    return VHandle(static_cast<Vertex*>(v.p))->data().id == v.id;
  }
  bool valid_h(HH h) const {
    if (!h.p || h.id == 0 || m_live_h.find(h.p) == m_live_h.end()) return false;
    return HHandle(static_cast<Halfedge*>(h.p))->data().id == h.id;
  }
  bool valid_f(FH f) const {
    if (!f.p || f.id == 0 || m_live_f.find(f.p) == m_live_f.end()) return false;
    return FHandle(static_cast<Face*>(f.p))->data().id == f.id;
  }

  [[noreturn]] static void bad_handle(const char* what) {
    throw_error(ErrorCode::InvalidHandle,
                std::string(what) + " handle is not valid: the element was deleted or belongs to "
                                    "another arrangement");
  }

  VHandle check_v(VH v) const { sync(); if (!valid_v(v)) bad_handle("vertex"); return VHandle(static_cast<Vertex*>(v.p)); }
  HHandle check_h(HH h) const { sync(); if (!valid_h(h)) bad_handle("halfedge"); return HHandle(static_cast<Halfedge*>(h.p)); }
  FHandle check_f(FH f) const { sync(); if (!valid_f(f)) bad_handle("face"); return FHandle(static_cast<Face*>(f.p)); }
  Curve_handle check_c(CH c) const {
    sync();
    auto it = m_curve_ids.find(c.p);
    if (!c.p || it == m_curve_ids.end() || it->second != c.id) bad_handle("curve");
    return Curve_handle(static_cast<Curve_halfedges*>(c.p));
  }

  // ---- rescan ------------------------------------------------------------
  //
  // Rebuild the live sets from the DCEL, keeping ids that are already assigned.  Needed after
  // after_clear / after_assign / after_attach / after_global_change, because the aggregate sweep
  // insertions build the DCEL without a usable per-element notification stream.  It is deferred
  // (m_dirty) so that a loop of N insertions does not cost O(N * size).
  void sync() const {
    if (m_in_notify != 0) return;
    if (m_history_dirty) {
      m_history_dirty = false;
      try { repair_history(); } catch (...) {}
    }
    if (m_dirty) rescan();
  }

  /// Drop every element id copied from ANOTHER arrangement.  `assign()` copies the whole extended
  /// DCEL, ElementData::id included, and those ids were handed out by the SOURCE arrangement: they
  /// can collide with ids this arrangement issued earlier, and then a stale handle whose record
  /// address happens to be reused answers `is_valid() == true` and silently resolves to a
  /// different element (measured with a randomized assign() fuzz).  Clearing them makes rescan()
  /// issue fresh ones from `m_next_id`, which is NEVER reset, so ids stay unique over the whole
  /// life of this arrangement and `Arrangement.assign`'s documented contract ("every handle into
  /// this arrangement becomes invalid") holds.
  void renumber() const {
    Arr& a = arr();
    for (auto v = a.vertices_begin(); v != a.vertices_end(); ++v) v->data().id = 0;
    for (auto h = a.halfedges_begin(); h != a.halfedges_end(); ++h) h->data().id = 0;
    for (auto f = a.faces_begin(); f != a.faces_end(); ++f) f->data().id = 0;
    if constexpr (Types::is_unbounded) {
      // The fictitious face, its halfedges and the vertices at infinity are invisible to the
      // filtered iterators above; rescan() reaches them through the fictitious face's CCBs.
      FHandle ff = a.fictitious_face();
      ff->data().id = 0;
      clear_ccb_ids(ff);
    }
    m_curve_ids.clear();
  }

  void clear_ccb_ids(FHandle f) const {
    for (auto o = f->outer_ccbs_begin(); o != f->outer_ccbs_end(); ++o) clear_ccb_ids_one(*o);
    for (auto o = f->inner_ccbs_begin(); o != f->inner_ccbs_end(); ++o) clear_ccb_ids_one(*o);
    for (auto v = f->isolated_vertices_begin(); v != f->isolated_vertices_end(); ++v)
      VHandle(v)->data().id = 0;
  }
  void clear_ccb_ids_one(Ccb_circ start) const {
    Ccb_circ c = start;
    do {
      HHandle h(c);
      h->data().id = 0;
      h->twin()->data().id = 0;
      h->target()->data().id = 0;
      ++c;
    } while (c != start);
  }

  void rescan() const {
    m_dirty = false;
    m_live_v.clear();
    m_live_h.clear();
    m_live_f.clear();
    Arr& a = arr();

    // Concrete vertices, concrete halfedges, non-fictitious faces.
    for (auto v = a.vertices_begin(); v != a.vertices_end(); ++v) bump(track_v(v).id);
    for (auto h = a.halfedges_begin(); h != a.halfedges_end(); ++h) bump(track_h(h).id);
    for (auto f = a.faces_begin(); f != a.faces_end(); ++f) {
      bump(track_f(f).id);
      scan_ccbs(FHandle(f));
    }
    if constexpr (Types::is_unbounded) {
      // The fictitious face, its fictitious halfedges and the vertices at infinity are invisible
      // to the filtered iterators; they are reachable through the fictitious face's CCBs
      // (arrangement_core.md gotchas 4-6).
      FHandle ff = a.fictitious_face();
      bump(track_f(ff).id);
      scan_ccbs(ff);
    }

    // Curve (history) nodes: keep the id of every node that is still in the list.
    std::unordered_map<const void*, std::uint64_t> old;
    old.swap(m_curve_ids);
    for (auto c = a.curves_begin(); c != a.curves_end(); ++c) {
      const void* p = static_cast<const void*>(&*c);
      auto it = old.find(p);
      std::uint64_t id = (it != old.end()) ? it->second : m_next_id++;
      bump(id);
      m_curve_ids.emplace(p, id);
    }
  }

  void bump(std::uint64_t id) const { if (id >= m_next_id) m_next_id = id + 1; }

  void scan_ccbs(FHandle f) const {
    for (auto o = f->outer_ccbs_begin(); o != f->outer_ccbs_end(); ++o) scan_ccb(*o);
    for (auto o = f->inner_ccbs_begin(); o != f->inner_ccbs_end(); ++o) scan_ccb(*o);
    for (auto v = f->isolated_vertices_begin(); v != f->isolated_vertices_end(); ++v)
      bump(track_v(VHandle(v)).id);
  }
  void scan_ccb(Ccb_circ start) const {
    Ccb_circ c = start;
    do {
      HHandle h(c);
      bump(track_h(h).id);
      bump(track_h(h->twin()).id);
      bump(track_v(h->target()).id);
      ++c;
    } while (c != start);
  }

  // ---- point location helpers -------------------------------------------
  void require_pl(int strategy) const {
    if (supports_point_location(strategy)) return;
    // Distinguish "this kind never offers the strategy" from "this ARRANGEMENT currently
    // cannot use it" (landmarks on an unbounded-planar arrangement, see landmarks_usable()).
    if constexpr (Policy::supports_landmarks) {
      if (strategy == PL_LANDMARKS)
        throw_error(ErrorCode::Unsupported,
                    "point-location strategy 'landmarks' is not available while the arrangement "
                    "contains an unbounded edge: CGAL 6.1 reads the null point of a vertex at "
                    "infinity (Arr_landmarks_pl_impl.h:414) and walks off the fictitious outer "
                    "ccb (:533).  It is available again once no ray or line is left");
    }
    throw_error(ErrorCode::Unsupported,
                std::string("point-location strategy '") + point_location_name(strategy) +
                    "' is not available for kind '" + kind_name(Types::kind) + "'");
  }

  /// Whether the LANDMARKS strategy may be used on THIS arrangement right now.  Compile-time
  /// true for every bounded kind that offers landmarks at all; for the unbounded-planar kind
  /// (Linear) it additionally requires that no unbounded edge is present, which is exactly
  /// `number_of_vertices_at_infinity() == 0` (every ray/line end creates one such vertex, and
  /// nothing else does).  See KindPolicy<LinearTypes>::supports_landmarks for the two CGAL 6.1
  /// bugs this avoids and for the measurements.  O(1).
  bool landmarks_usable() const {
    if constexpr (!Policy::supports_landmarks) {
      return false;
    } else if constexpr (!impl::has_vertices_at_infinity_v<Arr>) {
      return true;
    } else {
      return m_arr.number_of_vertices_at_infinity() == 0;
    }
  }

  Located from_pl(const PlResult& r) const {
    if (const auto* v = std::get_if<VCHandle>(&r)) return Located::vertex(track_v(nc(*v)));
    if (const auto* h = std::get_if<HCHandle>(&r)) return Located::halfedge(track_h(nc(*h)));
    if (const auto* f = std::get_if<FCHandle>(&r)) return Located::face(track_f(nc(*f)));
    return Located::none();
  }

  template <class OptCell>
  Located from_optional_cell(const OptCell& oc) const {
    if (!oc.has_value()) return Located::none();
    const auto& cell = *oc;
    if (const auto* v = std::get_if<VCHandle>(&cell)) return Located::vertex(track_v(nc(*v)));
    if (const auto* h = std::get_if<HCHandle>(&cell)) return Located::halfedge(track_h(nc(*h)));
    if (const auto* f = std::get_if<FCHandle>(&cell)) return Located::face(track_f(nc(*f)));
    return Located::none();
  }

  Located ray_shoot(const Geom& gp, int strategy, bool up) const {
    sync();
    const Point_2& p = point_in(gp);
    if constexpr (!Policy::supports_ray_shooting) {
      (void)p; (void)strategy; (void)up;
      throw_error(ErrorCode::Unsupported,
                  std::string("kind '") + kind_name(Types::kind) +
                      "': no point-location strategy supports vertical ray shooting");
    } else {
      if (strategy == PL_DEFAULT) {
        if constexpr (Policy::supports_ray_shooting_trapezoid) {
          if (m_pl_trap) return from_pl(up ? m_pl_trap->ray_shoot_up(p) : m_pl_trap->ray_shoot_down(p));
        }
        if constexpr (Policy::supports_ray_shooting_walk) {
          if (m_pl_walk) return from_pl(up ? m_pl_walk->ray_shoot_up(p) : m_pl_walk->ray_shoot_down(p));
          Walk_pl pl(m_arr);
          return from_pl(up ? pl.ray_shoot_up(p) : pl.ray_shoot_down(p));
        } else if constexpr (Policy::supports_ray_shooting_simple) {
          if (m_pl_simple) return from_pl(up ? m_pl_simple->ray_shoot_up(p) : m_pl_simple->ray_shoot_down(p));
          Simple_pl pl(m_arr);
          return from_pl(up ? pl.ray_shoot_up(p) : pl.ray_shoot_down(p));
        } else {
          Trap_pl pl(m_arr);
          return from_pl(up ? pl.ray_shoot_up(p) : pl.ray_shoot_down(p));
        }
      }
      switch (strategy) {
        case PL_SIMPLE:
          if constexpr (Policy::supports_ray_shooting_simple) {
            if (m_pl_simple) return from_pl(up ? m_pl_simple->ray_shoot_up(p) : m_pl_simple->ray_shoot_down(p));
            Simple_pl pl(m_arr);
            return from_pl(up ? pl.ray_shoot_up(p) : pl.ray_shoot_down(p));
          }
          break;
        case PL_WALK:
          if constexpr (Policy::supports_ray_shooting_walk) {
            if (m_pl_walk) return from_pl(up ? m_pl_walk->ray_shoot_up(p) : m_pl_walk->ray_shoot_down(p));
            Walk_pl pl(m_arr);
            return from_pl(up ? pl.ray_shoot_up(p) : pl.ray_shoot_down(p));
          }
          break;
        case PL_TRAPEZOID:
          if constexpr (Policy::supports_ray_shooting_trapezoid) {
            if (m_pl_trap) return from_pl(up ? m_pl_trap->ray_shoot_up(p) : m_pl_trap->ray_shoot_down(p));
            Trap_pl pl(m_arr);
            return from_pl(up ? pl.ray_shoot_up(p) : pl.ray_shoot_down(p));
          }
          break;
        default: break;
      }
      throw_error(ErrorCode::Unsupported,
                  std::string("point-location strategy '") + point_location_name(strategy) +
                      "' does not support vertical ray shooting for kind '" +
                      kind_name(Types::kind) + "' (only simple, walk and trapezoid do)");
    }
  }

  // ---- export helpers ----------------------------------------------------
  void build_vertex_index(std::unordered_map<const void*, std::size_t>& index) const {
    std::size_t i = 0;
    for (auto v = arr().vertices_begin(); v != arr().vertices_end(); ++v)
      index.emplace(static_cast<const void*>(&*v), i++);
  }
  static std::size_t vertex_index(const std::unordered_map<const void*, std::size_t>& index,
                                  VHandle v, std::size_t none) {
    auto it = index.find(static_cast<const void*>(&*v));
    return it == index.end() ? none : it->second;   // SIZE_MAX for a vertex at infinity
  }
  std::vector<std::size_t> ccb_indices(Ccb_circ start,
                                       const std::unordered_map<const void*, std::size_t>& index) const {
    std::vector<std::size_t> cycle;
    Ccb_circ c = start;
    do {
      HHandle h(c);
      auto it = index.find(static_cast<const void*>(&*h->target()));
      if (it != index.end()) cycle.push_back(it->second);   // skip vertices at infinity
      ++c;
    } while (c != start);
    return cycle;
  }
  void collect_ccb_curves(Ccb_circ start, std::vector<Geom>& out) const {
    Ccb_circ c = start;
    do {
      HHandle h(c);
      if (!h->is_fictitious()) out.push_back(he_directed_curve(track_h(h)));
      ++c;
    } while (c != start);
  }

  // ---- casts -------------------------------------------------------------
  static const ArrImpl* as_impl(const ArrBase& o) {
    const ArrImpl* p = dynamic_cast<const ArrImpl*>(&o);
    if (!p)
      throw_error(ErrorCode::KindMismatch,
                  std::string("expected an arrangement of kind '") + kind_name(Types::kind) +
                      "' but got kind '" + kind_name(o.kind()) + "'");
    return p;
  }
  static ArrImpl* as_impl_mut(ArrBase& o) {
    ArrImpl* p = dynamic_cast<ArrImpl*>(&o);
    if (!p)
      throw_error(ErrorCode::KindMismatch,
                  std::string("expected an arrangement of kind '") + kind_name(Types::kind) +
                      "' but got kind '" + kind_name(o.kind()) + "'");
    return p;
  }

  // ---- Python observer dispatch -----------------------------------------
  struct PyObserver {
    int token;
    void* user;
    ObserverFn fn;
  };

  /// RAII: while a Python callback runs, sync() must not rescan — the DCEL can be in an
  /// intermediate state in the middle of a CGAL operation and a CCB walk could then loop.
  struct NotifyGuard {
    const ArrImpl* self;
    explicit NotifyGuard(const ArrImpl* s) : self(s) { ++s->m_in_notify; }
    ~NotifyGuard() { --self->m_in_notify; }
  };

  void notify(const ObsEventData& ev) const {
    if (m_py_obs.empty()) return;
    // The callbacks are void and never throw (Cython records the Python exception); copy the list
    // so that add_observer/remove_observer from inside a callback is safe.
    std::vector<PyObserver> snapshot = m_py_obs;
    NotifyGuard guard(this);
    for (const PyObserver& o : snapshot)
      if (o.fn) o.fn(o.user, ev);
  }

  static ObsEventData mk(ObsEvent e) {
    ObsEventData d;
    d.event = e;
    return d;
  }

  // =========================================================================
  //  Overlay traits (the ten create_* functions of the OverlayTraits concept).
  //  The callbacks are applied to the *base* arrangements, whose handle types are the same as the
  //  with-history ones (arrangement_with_history.md §2.13).  A/B handles are const, R's are not.
  // =========================================================================
  struct OverlayTraits {
    const ArrImpl* A;
    const ArrImpl* B;
    ArrImpl* R;
    void* user;
    OverlayFn fn;

    OverlayTraits(const ArrImpl* a, const ArrImpl* b, ArrImpl* r, void* u, OverlayFn f)
        : A(a), B(b), R(r), user(u), fn(f) {}

    // -- helpers: ids of the input elements (only ElementData::id is written) --
    static std::pair<void*, std::uint64_t> idv(const ArrImpl* i, VCHandle v) {
      VH h = i->track_v(nc(v));
      return {h.p, h.id};
    }
    static std::pair<void*, std::uint64_t> idh(const ArrImpl* i, HCHandle e) {
      HH h = i->track_h(nc(e));
      return {h.p, h.id};
    }
    static std::pair<void*, std::uint64_t> idf(const ArrImpl* i, FCHandle f) {
      FH h = i->track_f(nc(f));
      return {h.p, h.id};
    }

    void emit(OverlayEvent ev, std::pair<void*, std::uint64_t> a, std::pair<void*, std::uint64_t> b,
              std::pair<void*, std::uint64_t> r) {
      if (!fn) return;
      OverlayEventData d;
      d.event = ev;
      d.a = a.first; d.a_id = a.second;
      d.b = b.first; d.b_id = b.second;
      d.r = r.first; d.r_id = r.second;
      // The callback must not touch ANY of the three arrangements: CGAL's overlay sweep is in
      // the middle of building R while reading A's and B's transient marks, and a re-entrant
      // clear()/insert() SEGFAULTs or hangs.  Raise the re-entrancy gate on all three for the
      // duration of the call, so such a callback gets a clean Error(Unsupported).
      NotifyGuard ga(A), gb(B), gr(R);
      fn(user, d);
    }

    void create_vertex(VCHandle v1, VCHandle v2, VHandle v) {
      VH rv = R->track_v(v);
      emit(OverlayEvent::VertexVertex, idv(A, v1), idv(B, v2), {rv.p, rv.id});
    }
    void create_vertex(VCHandle v1, HCHandle e2, VHandle v) {
      VH rv = R->track_v(v);
      emit(OverlayEvent::VertexEdge, idv(A, v1), idh(B, e2), {rv.p, rv.id});
    }
    void create_vertex(VCHandle v1, FCHandle f2, VHandle v) {
      VH rv = R->track_v(v);
      emit(OverlayEvent::VertexFace, idv(A, v1), idf(B, f2), {rv.p, rv.id});
    }
    void create_vertex(HCHandle e1, VCHandle v2, VHandle v) {
      VH rv = R->track_v(v);
      emit(OverlayEvent::EdgeVertex, idh(A, e1), idv(B, v2), {rv.p, rv.id});
    }
    void create_vertex(FCHandle f1, VCHandle v2, VHandle v) {
      VH rv = R->track_v(v);
      emit(OverlayEvent::FaceVertex, idf(A, f1), idv(B, v2), {rv.p, rv.id});
    }
    void create_vertex(HCHandle e1, HCHandle e2, VHandle v) {
      VH rv = R->track_v(v);
      emit(OverlayEvent::EdgeEdgeVertex, idh(A, e1), idh(B, e2), {rv.p, rv.id});
    }
    void create_edge(HCHandle e1, HCHandle e2, HHandle e) {
      HH re = R->track_h(e);
      emit(OverlayEvent::EdgeEdge, idh(A, e1), idh(B, e2), {re.p, re.id});
    }
    void create_edge(HCHandle e1, FCHandle f2, HHandle e) {
      HH re = R->track_h(e);
      emit(OverlayEvent::EdgeFace, idh(A, e1), idf(B, f2), {re.p, re.id});
    }
    void create_edge(FCHandle f1, HCHandle e2, HHandle e) {
      HH re = R->track_h(e);
      emit(OverlayEvent::FaceEdge, idf(A, f1), idh(B, e2), {re.p, re.id});
    }
    void create_face(FCHandle f1, FCHandle f2, FHandle f) {
      FH rf = R->track_f(f);
      emit(OverlayEvent::FaceFace, idf(A, f1), idf(B, f2), {rf.p, rf.id});
    }
  };

  // =========================================================================
  //  Internal observer.  Derived from typename Arr::Observer == Aos_observer<Base_arr>, so its
  //  Point_2 is the plain point type and its X_monotone_curve_2 is the DATA-EXTENDED curve.
  //  Convention: `after_*` handlers update the bookkeeping first and then call the Python
  //  observers; `before_*` handlers that *invalidate* elements call the Python observers first
  //  (so that a callback still sees a usable handle for the element that is about to die) and
  //  erase afterwards.  Either way a callback never sees a handle to an already-dead element.
  // =========================================================================
  class InternalObserver : public Arr::Observer {
   public:
    using Base = typename Arr::Observer;
    using Point = typename Base::Point_2;
    using Xcv = typename Base::X_monotone_curve_2;   // data-extended
    using VH_ = typename Base::Vertex_handle;
    using HH_ = typename Base::Halfedge_handle;
    using FH_ = typename Base::Face_handle;
    using Ccb_ = typename Base::Ccb_halfedge_circulator;

    explicit InternalObserver(ArrImpl* self) : m_self(self) {}

    // ---- global ----------------------------------------------------------
    void before_assign(const Base_arr&) override { m_self->notify(mk(ObsEvent::BeforeAssign)); }
    void after_assign() override {
      m_self->m_dirty = true;
      m_self->notify(mk(ObsEvent::AfterAssign));
    }
    void before_clear() override { m_self->notify(mk(ObsEvent::BeforeClear)); }
    void after_clear() override {
      m_self->m_live_v.clear();
      m_self->m_live_h.clear();
      m_self->m_live_f.clear();
      m_self->m_curve_ids.clear();
      // clear() re-initialises the DCEL: under the unbounded topology that silently re-creates
      // the fictitious face, its 4 corner vertices and 8 fictitious halfedges without any
      // notification, so the fresh skeleton has to be rescanned.
      m_self->m_dirty = true;
      m_self->notify(mk(ObsEvent::AfterClear));
    }
    void before_global_change() override { m_self->notify(mk(ObsEvent::BeforeGlobalChange)); }
    void after_global_change() override {
      m_self->m_dirty = true;
      m_self->notify(mk(ObsEvent::AfterGlobalChange));
    }
    void before_attach(const Base_arr&) override { m_self->notify(mk(ObsEvent::BeforeAttach)); }
    void after_attach() override {
      m_self->m_dirty = true;
      m_self->notify(mk(ObsEvent::AfterAttach));
    }
    void before_detach() override { m_self->notify(mk(ObsEvent::BeforeDetach)); }
    void after_detach() override { m_self->notify(mk(ObsEvent::AfterDetach)); }

    // ---- vertices --------------------------------------------------------
    void before_create_vertex(const Point& p) override {
      ObsEventData d = mk(ObsEvent::BeforeCreateVertex);
      Geom g = box_point(p);
      d.g1 = &g;
      m_self->notify(d);
    }
    void after_create_vertex(VH_ v) override {
      ObsEventData d = mk(ObsEvent::AfterCreateVertex);
      d.v1 = m_self->track_v(v);
      m_self->notify(d);
    }
    void before_create_boundary_vertex(const Point& p, CGAL::Arr_parameter_space ps_x,
                                       CGAL::Arr_parameter_space ps_y) override {
      ObsEventData d = mk(ObsEvent::BeforeCreateBoundaryVertex);
      Geom g = box_point(p);
      d.g1 = &g;
      d.i1 = -1;                 // the point-based overload
      d.i2 = int(ps_x);
      d.i3 = int(ps_y);
      m_self->notify(d);
    }
    void before_create_boundary_vertex(const Xcv& cv, CGAL::Arr_curve_end ind,
                                       CGAL::Arr_parameter_space ps_x,
                                       CGAL::Arr_parameter_space ps_y) override {
      ObsEventData d = mk(ObsEvent::BeforeCreateBoundaryVertex);
      Geom g = box_xcurve(cv);   // slice-copy to the plain x-monotone curve
      d.g1 = &g;
      d.i1 = int(ind);
      d.i2 = int(ps_x);
      d.i3 = int(ps_y);
      m_self->notify(d);
    }
    void after_create_boundary_vertex(VH_ v) override {
      ObsEventData d = mk(ObsEvent::AfterCreateBoundaryVertex);
      d.v1 = m_self->track_v(v);
      m_self->notify(d);
    }
    void before_modify_vertex(VH_ v, const Point& p) override {
      ObsEventData d = mk(ObsEvent::BeforeModifyVertex);
      d.v1 = m_self->track_v(v);
      Geom g = box_point(p);
      d.g1 = &g;
      m_self->notify(d);
    }
    void after_modify_vertex(VH_ v) override {
      ObsEventData d = mk(ObsEvent::AfterModifyVertex);
      d.v1 = m_self->track_v(v);
      m_self->notify(d);
    }

    // ---- edges -----------------------------------------------------------
    void before_create_edge(const Xcv& cv, VH_ v1, VH_ v2) override {
      ObsEventData d = mk(ObsEvent::BeforeCreateEdge);
      Geom g = box_xcurve(cv);
      d.g1 = &g;
      d.v1 = m_self->track_v(v1);
      d.v2 = m_self->track_v(v2);
      m_self->notify(d);
    }
    void after_create_edge(HH_ e) override {
      m_self->track_edge(e);
      ObsEventData d = mk(ObsEvent::AfterCreateEdge);
      d.h1 = m_self->track_h(e);
      m_self->notify(d);
    }
    void before_modify_edge(HH_ e, const Xcv& cv) override {
      ObsEventData d = mk(ObsEvent::BeforeModifyEdge);
      d.h1 = m_self->track_h(e);
      Geom g = box_xcurve(cv);
      d.g1 = &g;
      m_self->notify(d);
    }
    void after_modify_edge(HH_ e) override {
      ObsEventData d = mk(ObsEvent::AfterModifyEdge);
      d.h1 = m_self->track_h(e);
      m_self->notify(d);
    }
    void before_split_edge(HH_ e, VH_ v, const Xcv& c1, const Xcv& c2) override {
      ObsEventData d = mk(ObsEvent::BeforeSplitEdge);
      d.h1 = m_self->track_h(e);
      d.v1 = m_self->track_v(v);
      Geom g1 = box_xcurve(c1), g2 = box_xcurve(c2);
      d.g1 = &g1;
      d.g2 = &g2;
      m_self->notify(d);
    }
    void after_split_edge(HH_ e1, HH_ e2) override {
      m_self->track_edge(e1);
      m_self->track_edge(e2);
      ObsEventData d = mk(ObsEvent::AfterSplitEdge);
      d.h1 = m_self->track_h(e1);
      d.h2 = m_self->track_h(e2);
      m_self->notify(d);
    }
    void before_split_fictitious_edge(HH_ e, VH_ v) override {
      ObsEventData d = mk(ObsEvent::BeforeSplitFictitiousEdge);
      d.h1 = m_self->track_h(e);
      d.v1 = m_self->track_v(v);
      m_self->notify(d);
    }
    void after_split_fictitious_edge(HH_ e1, HH_ e2) override {
      m_self->track_edge(e1);
      m_self->track_edge(e2);
      ObsEventData d = mk(ObsEvent::AfterSplitFictitiousEdge);
      d.h1 = m_self->track_h(e1);
      d.h2 = m_self->track_h(e2);
      m_self->notify(d);
    }
    void before_merge_edge(HH_ e1, HH_ e2, const Xcv& cv) override {
      ObsEventData d = mk(ObsEvent::BeforeMergeEdge);
      d.h1 = m_self->track_h(e1);
      d.h2 = m_self->track_h(e2);
      Geom g = box_xcurve(cv);
      d.g1 = &g;
      m_self->notify(d);
      // e2's pair is deleted by the merge; e1's pair survives (and is re-tracked, keeping its id,
      // in after_merge_edge).
      m_self->untrack_edge(e1);
      m_self->untrack_edge(e2);
    }
    void after_merge_edge(HH_ e) override {
      m_self->track_edge(e);
      ObsEventData d = mk(ObsEvent::AfterMergeEdge);
      d.h1 = m_self->track_h(e);
      m_self->notify(d);
    }
    void before_merge_fictitious_edge(HH_ e1, HH_ e2) override {
      ObsEventData d = mk(ObsEvent::BeforeMergeFictitiousEdge);
      d.h1 = m_self->track_h(e1);
      d.h2 = m_self->track_h(e2);
      m_self->notify(d);
      m_self->untrack_edge(e1);
      m_self->untrack_edge(e2);
    }
    void after_merge_fictitious_edge(HH_ e) override {
      m_self->track_edge(e);
      ObsEventData d = mk(ObsEvent::AfterMergeFictitiousEdge);
      d.h1 = m_self->track_h(e);
      m_self->notify(d);
    }

    // ---- faces & CCBs ----------------------------------------------------
    void before_split_face(FH_ f, HH_ e) override {
      ObsEventData d = mk(ObsEvent::BeforeSplitFace);
      d.f1 = m_self->track_f(f);
      d.h1 = m_self->track_h(e);
      // e->face() / e->twin()->face() are dangling here; block he_face for the pair while the
      // Python callbacks run (CGAL_TRAPS_CHECKLIST, "Observer traces").
      struct NoFaceGuard {
        const ArrImpl* self;
        NoFaceGuard(const ArrImpl* s, const void* a, const void* b) : self(s) {
          s->m_no_face_he[0] = a;
          s->m_no_face_he[1] = b;
        }
        ~NoFaceGuard() { self->m_no_face_he[0] = nullptr; self->m_no_face_he[1] = nullptr; }
      } no_face(m_self, d.h1.p, m_self->track_h(e->twin()).p);
      m_self->notify(d);
    }
    void after_split_face(FH_ f, FH_ new_f, bool is_hole) override {
      ObsEventData d = mk(ObsEvent::AfterSplitFace);
      d.f1 = m_self->track_f(f);
      d.f2 = m_self->track_f(new_f);
      d.flag = is_hole;
      m_self->notify(d);
    }
    void before_split_outer_ccb(FH_ f, Ccb_ h, HH_ e) override {
      ObsEventData d = mk(ObsEvent::BeforeSplitOuterCcb);
      d.f1 = m_self->track_f(f);
      d.h1 = m_self->track_h(HH_(h));
      d.h2 = m_self->track_h(e);
      m_self->notify(d);
    }
    void after_split_outer_ccb(FH_ f, Ccb_ h1, Ccb_ h2) override {
      ObsEventData d = mk(ObsEvent::AfterSplitOuterCcb);
      d.f1 = m_self->track_f(f);
      d.h1 = m_self->track_h(HH_(h1));
      d.h2 = m_self->track_h(HH_(h2));
      m_self->notify(d);
    }
    void before_split_inner_ccb(FH_ f, Ccb_ h, HH_ e) override {
      ObsEventData d = mk(ObsEvent::BeforeSplitInnerCcb);
      d.f1 = m_self->track_f(f);
      d.h1 = m_self->track_h(HH_(h));
      d.h2 = m_self->track_h(e);
      m_self->notify(d);
    }
    void after_split_inner_ccb(FH_ f, Ccb_ h1, Ccb_ h2) override {
      ObsEventData d = mk(ObsEvent::AfterSplitInnerCcb);
      d.f1 = m_self->track_f(f);
      d.h1 = m_self->track_h(HH_(h1));
      d.h2 = m_self->track_h(HH_(h2));
      m_self->notify(d);
    }
    void before_add_outer_ccb(FH_ f, HH_ e) override {
      ObsEventData d = mk(ObsEvent::BeforeAddOuterCcb);
      d.f1 = m_self->track_f(f);
      d.h1 = m_self->track_h(e);
      m_self->notify(d);
    }
    void after_add_outer_ccb(Ccb_ h) override {
      ObsEventData d = mk(ObsEvent::AfterAddOuterCcb);
      d.h1 = m_self->track_h(HH_(h));
      m_self->notify(d);
    }
    void before_add_inner_ccb(FH_ f, HH_ e) override {
      ObsEventData d = mk(ObsEvent::BeforeAddInnerCcb);
      d.f1 = m_self->track_f(f);
      d.h1 = m_self->track_h(e);
      m_self->notify(d);
    }
    void after_add_inner_ccb(Ccb_ h) override {
      ObsEventData d = mk(ObsEvent::AfterAddInnerCcb);
      d.h1 = m_self->track_h(HH_(h));
      m_self->notify(d);
    }
    void before_add_isolated_vertex(FH_ f, VH_ v) override {
      ObsEventData d = mk(ObsEvent::BeforeAddIsolatedVertex);
      d.f1 = m_self->track_f(f);
      d.v1 = m_self->track_v(v);
      m_self->notify(d);
    }
    void after_add_isolated_vertex(VH_ v) override {
      ObsEventData d = mk(ObsEvent::AfterAddIsolatedVertex);
      d.v1 = m_self->track_v(v);
      m_self->notify(d);
    }
    void before_merge_face(FH_ f1, FH_ f2, HH_ e) override {
      ObsEventData d = mk(ObsEvent::BeforeMergeFace);
      d.f1 = m_self->track_f(f1);
      d.f2 = m_self->track_f(f2);
      d.h1 = m_self->track_h(e);
      m_self->notify(d);
      // One of the two face records is deleted and CGAL does not say which; the survivor is
      // re-tracked (keeping its id) in after_merge_face.
      m_self->untrack_f(f1);
      m_self->untrack_f(f2);
    }
    void after_merge_face(FH_ f) override {
      ObsEventData d = mk(ObsEvent::AfterMergeFace);
      d.f1 = m_self->track_f(f);
      m_self->notify(d);
    }
    void before_merge_outer_ccb(FH_ f, Ccb_ h1, Ccb_ h2, HH_ e) override {
      ObsEventData d = mk(ObsEvent::BeforeMergeOuterCcb);
      d.f1 = m_self->track_f(f);
      d.h1 = m_self->track_h(HH_(h1));
      d.h2 = m_self->track_h(HH_(h2));
      d.h3 = m_self->track_h(e);
      m_self->notify(d);
    }
    void after_merge_outer_ccb(FH_ f, Ccb_ h) override {
      ObsEventData d = mk(ObsEvent::AfterMergeOuterCcb);
      d.f1 = m_self->track_f(f);
      d.h1 = m_self->track_h(HH_(h));
      m_self->notify(d);
    }
    void before_merge_inner_ccb(FH_ f, Ccb_ h1, Ccb_ h2, HH_ e) override {
      ObsEventData d = mk(ObsEvent::BeforeMergeInnerCcb);
      d.f1 = m_self->track_f(f);
      d.h1 = m_self->track_h(HH_(h1));
      d.h2 = m_self->track_h(HH_(h2));
      d.h3 = m_self->track_h(e);
      m_self->notify(d);
    }
    void after_merge_inner_ccb(FH_ f, Ccb_ h) override {
      ObsEventData d = mk(ObsEvent::AfterMergeInnerCcb);
      d.f1 = m_self->track_f(f);
      d.h1 = m_self->track_h(HH_(h));
      m_self->notify(d);
    }
    void before_move_outer_ccb(FH_ from_f, FH_ to_f, Ccb_ h) override {
      ObsEventData d = mk(ObsEvent::BeforeMoveOuterCcb);
      d.f1 = m_self->track_f(from_f);
      d.f2 = m_self->track_f(to_f);
      d.h1 = m_self->track_h(HH_(h));
      m_self->notify(d);
    }
    void after_move_outer_ccb(Ccb_ h) override {
      ObsEventData d = mk(ObsEvent::AfterMoveOuterCcb);
      d.h1 = m_self->track_h(HH_(h));
      m_self->notify(d);
    }
    void before_move_inner_ccb(FH_ from_f, FH_ to_f, Ccb_ h) override {
      ObsEventData d = mk(ObsEvent::BeforeMoveInnerCcb);
      d.f1 = m_self->track_f(from_f);
      d.f2 = m_self->track_f(to_f);
      d.h1 = m_self->track_h(HH_(h));
      m_self->notify(d);
    }
    void after_move_inner_ccb(Ccb_ h) override {
      ObsEventData d = mk(ObsEvent::AfterMoveInnerCcb);
      d.h1 = m_self->track_h(HH_(h));
      m_self->notify(d);
    }
    void before_move_isolated_vertex(FH_ from_f, FH_ to_f, VH_ v) override {
      ObsEventData d = mk(ObsEvent::BeforeMoveIsolatedVertex);
      d.f1 = m_self->track_f(from_f);
      d.f2 = m_self->track_f(to_f);
      d.v1 = m_self->track_v(v);
      m_self->notify(d);
    }
    void after_move_isolated_vertex(VH_ v) override {
      ObsEventData d = mk(ObsEvent::AfterMoveIsolatedVertex);
      d.v1 = m_self->track_v(v);
      m_self->notify(d);
    }

    // ---- removals --------------------------------------------------------
    void before_remove_vertex(VH_ v) override {
      ObsEventData d = mk(ObsEvent::BeforeRemoveVertex);
      d.v1 = m_self->track_v(v);
      // CGAL has already deleted / re-linked the halfedges around a NON-isolated vertex here;
      // block the ring accessors for the duration of the Python dispatch (see
      // ArrImpl::reject_dead_vertex_ring).
      struct NoRingGuard {
        const ArrImpl* self;
        NoRingGuard(const ArrImpl* s, const void* p) : self(s) { s->m_no_ring_v = p; }
        ~NoRingGuard() { self->m_no_ring_v = nullptr; }
      } no_ring(m_self, d.v1.p);
      m_self->notify(d);
      m_self->untrack_v(v);
    }
    void after_remove_vertex() override { m_self->notify(mk(ObsEvent::AfterRemoveVertex)); }
    void before_remove_edge(HH_ e) override {
      ObsEventData d = mk(ObsEvent::BeforeRemoveEdge);
      d.h1 = m_self->track_h(e);
      m_self->notify(d);
      m_self->untrack_edge(e);
    }
    void after_remove_edge() override { m_self->notify(mk(ObsEvent::AfterRemoveEdge)); }
    void before_remove_outer_ccb(FH_ f, Ccb_ h) override {
      ObsEventData d = mk(ObsEvent::BeforeRemoveOuterCcb);
      d.f1 = m_self->track_f(f);
      d.h1 = m_self->track_h(HH_(h));
      m_self->notify(d);
    }
    void after_remove_outer_ccb(FH_ f) override {
      ObsEventData d = mk(ObsEvent::AfterRemoveOuterCcb);
      d.f1 = m_self->track_f(f);
      m_self->notify(d);
    }
    void before_remove_inner_ccb(FH_ f, Ccb_ h) override {
      ObsEventData d = mk(ObsEvent::BeforeRemoveInnerCcb);
      d.f1 = m_self->track_f(f);
      d.h1 = m_self->track_h(HH_(h));
      m_self->notify(d);
    }
    void after_remove_inner_ccb(FH_ f) override {
      ObsEventData d = mk(ObsEvent::AfterRemoveInnerCcb);
      d.f1 = m_self->track_f(f);
      m_self->notify(d);
    }

   private:
    ArrImpl* m_self;
  };

  // ---- data members (declaration order == construction order) ------------
  Arr m_arr;                                   ///< constructed with &Types::traits()
  InternalObserver m_obs{this};                ///< attached in the ctor, detached in ~InternalObserver

  // Point-location strategies: declared AFTER m_arr so that they are destroyed FIRST.
  std::unique_ptr<Naive_pl> m_pl_naive;
  std::unique_ptr<Simple_pl> m_pl_simple;
  std::unique_ptr<Walk_pl> m_pl_walk;
  std::unique_ptr<Landmarks_pl> m_pl_landmarks;
  std::unique_ptr<Trap_pl> m_pl_trap;
  std::unique_ptr<Tri_pl> m_pl_tri;

  mutable std::unordered_set<const void*> m_live_v;
  mutable std::unordered_set<const void*> m_live_h;
  mutable std::unordered_set<const void*> m_live_f;
  mutable std::unordered_map<const void*, std::uint64_t> m_curve_ids;
  mutable std::uint64_t m_next_id = 1;
  mutable bool m_dirty = false;
  mutable bool m_history_dirty = false;   ///< an exception escaped a with-history modification
  mutable int m_in_notify = 0;      ///< > 0 while a Python observer / overlay callback is running
  mutable const KindOps* m_ops = nullptr;

  /// The two halfedges whose incident-face pointer is dangling while `before_split_face` is
  /// dispatched to Python (CGAL_TRAPS_CHECKLIST, "Observer traces").  `he_face` refuses them.
  mutable const void* m_no_face_he[2] = {nullptr, nullptr};
  /// The vertex whose halfedge ring CGAL has already destroyed while `before_remove_vertex` is
  /// dispatched to Python.  `vertex_degree` / `vertex_incident_halfedges` refuse it.
  mutable const void* m_no_ring_v = nullptr;

  std::vector<PyObserver> m_py_obs;
  int m_next_token = 1;
};

}  // namespace arr2d
