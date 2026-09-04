// arr2d — type-erased arrangement interface.
//
// ArrBase wraps CGAL::Arrangement_with_history_2<Traits, Arr_extended_dcel<Traits, VData, HData, FData>>
// (planar kinds) or Arrangement_on_surface_with_history_2<Traits, Arr_spherical_topology_traits_2<...>>
// (sphere). One implementation per kind: impl/arr_impl.hpp (template) instantiated in kind_<name>.cpp.
//
// Handles (VH/HH/FH/CH) carry (pointer, id) and are validated on every access:
// a stale handle raises Error(InvalidHandle), never a crash (see DESIGN.md).
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "arr2d/common.hpp"
#include "arr2d/ops.hpp"

namespace arr2d {

// ---------------------------------------------------------------------------
// Observer events (mirror CGAL 6.1 Aos_observer; exact CGAL signatures are listed in
// docs/dev/cgal61_api/global_functions_overlay_observer.md). Argument mapping per event:
//   v1/v2: vertex handles, h1/h2/h3: halfedge handles (a Ccb_halfedge_circulator is passed as
//   the halfedge it points to), f1/f2: face handles, g1/g2: boxed point / x-monotone curve(s)
//   (valid only during the callback), flag: bool argument, i1/i2/i3: enum arguments as ints.
// ---------------------------------------------------------------------------
enum class ObsEvent : int {
  BeforeAssign = 0, AfterAssign,                 // (the source arrangement is not exposed)
  BeforeClear, AfterClear,
  BeforeGlobalChange, AfterGlobalChange,
  BeforeAttach, AfterAttach, BeforeDetach, AfterDetach,
  BeforeCreateVertex,                            // g1 = point
  AfterCreateVertex,                             // v1
  BeforeCreateBoundaryVertex,                    // g1 = point (i1 = -1) or x-monotone curve (i1 = Arr_curve_end), i2 = ps_x, i3 = ps_y
  AfterCreateBoundaryVertex,                     // v1
  BeforeCreateEdge,                              // g1 = x-monotone curve, v1, v2
  AfterCreateEdge,                               // h1
  BeforeModifyVertex,                            // v1, g1 = new point
  AfterModifyVertex,                             // v1
  BeforeModifyEdge,                              // h1, g1 = new x-monotone curve
  AfterModifyEdge,                               // h1
  BeforeSplitEdge,                               // h1, v1 = split vertex, g1 = curve 1, g2 = curve 2
  AfterSplitEdge,                                // h1, h2
  BeforeSplitFictitiousEdge,                     // h1, v1
  AfterSplitFictitiousEdge,                      // h1, h2
  BeforeSplitFace,                               // f1, h1
  AfterSplitFace,                                // f1 = original, f2 = new face, flag = is_hole
  BeforeSplitOuterCcb,                           // f1, h1 (ccb), h2
  AfterSplitOuterCcb,                            // f1, h1, h2
  BeforeSplitInnerCcb,                           // f1, h1 (ccb), h2
  AfterSplitInnerCcb,                            // f1, h1, h2
  BeforeAddOuterCcb,                             // f1, h1
  AfterAddOuterCcb,                              // h1 (ccb)
  BeforeAddInnerCcb,                             // f1, h1
  AfterAddInnerCcb,                              // h1 (ccb)
  BeforeAddIsolatedVertex,                       // f1, v1
  AfterAddIsolatedVertex,                        // v1
  BeforeMergeEdge,                               // h1, h2, g1 = merged curve
  AfterMergeEdge,                                // h1
  BeforeMergeFictitiousEdge,                     // h1, h2
  AfterMergeFictitiousEdge,                      // h1
  BeforeMergeFace,                               // f1, f2, h1
  AfterMergeFace,                                // f1
  BeforeMergeOuterCcb,                           // f1, h1 (ccb1), h2 (ccb2), h3 = the halfedge whose removal merges them
  AfterMergeOuterCcb,                            // f1, h1
  BeforeMergeInnerCcb,                           // f1, h1, h2, h3
  AfterMergeInnerCcb,                            // f1, h1
  BeforeMoveOuterCcb,                            // f1 = from, f2 = to, h1
  AfterMoveOuterCcb,                             // h1
  BeforeMoveInnerCcb,                            // f1 = from, f2 = to, h1
  AfterMoveInnerCcb,                             // h1
  BeforeMoveIsolatedVertex,                      // f1 = from, f2 = to, v1
  AfterMoveIsolatedVertex,                       // v1
  BeforeRemoveVertex,                            // v1
  AfterRemoveVertex,
  BeforeRemoveEdge,                              // h1
  AfterRemoveEdge,
  BeforeRemoveOuterCcb,                          // f1, h1
  AfterRemoveOuterCcb,                           // f1
  BeforeRemoveInnerCcb,                          // f1, h1
  AfterRemoveInnerCcb,                           // f1
  NumEvents
};
const char* obs_event_name(ObsEvent e);   ///< snake_case CGAL name, e.g. "after_split_face"

struct ObsEventData {
  ObsEvent event = ObsEvent::BeforeAssign;
  VH v1, v2;
  HH h1, h2, h3;
  FH f1, f2;
  bool flag = false;
  int i1 = 0, i2 = 0, i3 = 0;
  const Geom* g1 = nullptr;
  const Geom* g2 = nullptr;
};
/// Observer callback. Must not throw; Cython records Python exceptions and re-raises later.
using ObserverFn = void (*)(void* user, const ObsEventData& ev);

// ---------------------------------------------------------------------------
// Overlay events (mirror the OverlayTraits concept's ten create_* functions).
// a/b = handles into the two input arrangements, r = handle into the result.
// ---------------------------------------------------------------------------
enum class OverlayEvent : int {
  VertexVertex = 0,   ///< create_vertex(v1, v2, v)
  VertexEdge = 1,     ///< create_vertex(v1, e2, v)
  VertexFace = 2,     ///< create_vertex(v1, f2, v)
  EdgeVertex = 3,     ///< create_vertex(e1, v2, v)
  FaceVertex = 4,     ///< create_vertex(f1, v2, v)
  EdgeEdgeVertex = 5, ///< create_vertex(e1, e2, v)  (two edges intersecting at a new vertex)
  EdgeEdge = 6,       ///< create_edge(e1, e2, e)   (overlapping edges)
  EdgeFace = 7,       ///< create_edge(e1, f2, e)
  FaceEdge = 8,       ///< create_edge(f1, e2, e)
  FaceFace = 9,       ///< create_face(f1, f2, f)
};
struct OverlayEventData {
  OverlayEvent event = OverlayEvent::FaceFace;
  void* a = nullptr; std::uint64_t a_id = 0;   ///< handle in arrangement A (type per event)
  void* b = nullptr; std::uint64_t b_id = 0;   ///< handle in arrangement B
  void* r = nullptr; std::uint64_t r_id = 0;   ///< handle in the result arrangement
};
using OverlayFn = void (*)(void* user, const OverlayEventData& ev);

/// One entry of the vertical decomposition: for vertex v, the feature immediately below and above.
struct VerticalDecompositionEntry {
  VH v;
  Located below;   ///< type -1 when nothing (ray to infinity)
  Located above;
};

// ---------------------------------------------------------------------------
// ArrBase
// ---------------------------------------------------------------------------
class ArrBase {
 public:
  virtual ~ArrBase() = default;

  virtual Kind kind() const = 0;
  virtual const KindOps& ops() const = 0;
  virtual bool is_unbounded_kind() const = 0;   ///< Linear: fictitious vertices/halfedges/face exist

  // ---- global ------------------------------------------------------------
  virtual std::size_t number_of_vertices() const = 0;             ///< concrete vertices (excludes fictitious ones at infinity)
  virtual std::size_t number_of_isolated_vertices() const = 0;
  virtual std::size_t number_of_vertices_at_infinity() const = 0; ///< 0 for bounded kinds
  virtual std::size_t number_of_halfedges() const = 0;            ///< excludes fictitious halfedges
  virtual std::size_t number_of_edges() const = 0;
  virtual std::size_t number_of_faces() const = 0;                ///< excludes the fictitious face (unbounded kinds)
  virtual std::size_t number_of_unbounded_faces() const = 0;
  virtual std::size_t number_of_curves() const = 0;               ///< history: number of input curves
  virtual bool is_empty() const = 0;
  virtual bool is_valid() const = 0;                              ///< CGAL is_valid (topological + geometric checks)
  virtual void clear() = 0;
  virtual std::unique_ptr<ArrBase> clone() const = 0;             ///< deep copy incl. history and element data (Python refs incref'd); no observers / point-location objects
  virtual void assign(const ArrBase& other) = 0;                  ///< same kind required

  // ---- iteration (snapshots; order = CGAL iteration order) ----------------
  virtual void vertices(std::vector<VH>& out) const = 0;          ///< concrete vertices
  virtual void halfedges(std::vector<HH>& out) const = 0;         ///< concrete halfedges
  virtual void edges(std::vector<HH>& out) const = 0;             ///< one halfedge per edge
  virtual void faces(std::vector<FH>& out) const = 0;             ///< all faces except the fictitious one
  virtual void unbounded_faces(std::vector<FH>& out) const = 0;
  virtual void curves(std::vector<CH>& out) const = 0;            ///< input curves (history)
  /// The "outer" face: bounded planar kinds -> the unique unbounded face; Linear (unbounded
  /// planar) -> an arbitrary unbounded face (use unbounded_faces()); Sphere -> the spherical
  /// (reference) face, i.e. the unique face with no outer ccb, which contains the north pole.
  virtual FH unbounded_face() const = 0;
  virtual FH fictitious_face() const = 0;                         ///< Linear only: the fictitious face outside the bounding rectangle (Unsupported otherwise)

  // ---- handle validity ---------------------------------------------------
  virtual bool vertex_valid(VH v) const = 0;
  virtual bool halfedge_valid(HH h) const = 0;
  virtual bool face_valid(FH f) const = 0;
  virtual bool curve_valid(CH c) const = 0;

  // ---- vertex ------------------------------------------------------------
  virtual Geom vertex_point(VH v) const = 0;                      ///< Unsupported for vertices at infinity (check vertex_is_at_open_boundary)
  virtual std::size_t vertex_degree(VH v) const = 0;
  virtual bool vertex_is_isolated(VH v) const = 0;
  virtual FH vertex_face(VH v) const = 0;                         ///< isolated vertices only (InvalidArgument otherwise)
  virtual void vertex_incident_halfedges(VH v, std::vector<HH>& out) const = 0;   ///< halfedges whose TARGET is v, in CGAL's circular order
  virtual bool vertex_is_at_open_boundary(VH v) const = 0;
  virtual int vertex_parameter_space_in_x(VH v) const = 0;        ///< ParameterSpace
  virtual int vertex_parameter_space_in_y(VH v) const = 0;
  virtual PyRef& vertex_data(VH v) = 0;
  virtual const PyRef& vertex_data(VH v) const = 0;

  // ---- halfedge ----------------------------------------------------------
  virtual VH he_source(HH h) const = 0;
  virtual VH he_target(HH h) const = 0;
  virtual HH he_twin(HH h) const = 0;
  virtual HH he_next(HH h) const = 0;
  virtual HH he_prev(HH h) const = 0;
  virtual FH he_face(HH h) const = 0;                             ///< incident face (to the left)
  virtual Geom he_curve(HH h) const = 0;                          ///< the stored x-monotone curve (shared with the twin); Unsupported for fictitious halfedges
  virtual Geom he_directed_curve(HH h) const = 0;                 ///< the curve oriented from he_source to he_target (Construct_opposite_2 when needed)
  virtual int he_direction(HH h) const = 0;                       ///< HalfedgeDirection
  virtual bool he_is_fictitious(HH h) const = 0;
  virtual bool he_is_on_inner_ccb(HH h) const = 0;
  virtual bool he_is_on_outer_ccb(HH h) const = 0;
  virtual void he_ccb(HH h, std::vector<HH>& out) const = 0;      ///< the whole connected component of the boundary containing h, starting at h, following next()
  virtual PyRef& he_data(HH h) = 0;
  virtual const PyRef& he_data(HH h) const = 0;

  // ---- face --------------------------------------------------------------
  virtual bool face_is_unbounded(FH f) const = 0;
  virtual bool face_is_fictitious(FH f) const = 0;
  virtual bool face_has_outer_ccb(FH f) const = 0;
  virtual std::size_t face_number_of_outer_ccbs(FH f) const = 0;   ///< 0 or 1 for planar; sphere may have more
  virtual std::size_t face_number_of_inner_ccbs(FH f) const = 0;   ///< holes
  virtual std::size_t face_number_of_isolated_vertices(FH f) const = 0;
  virtual HH face_outer_ccb(FH f) const = 0;                       ///< a halfedge of the (first) outer ccb (InvalidArgument if none)
  virtual void face_outer_ccbs(FH f, std::vector<HH>& out) const = 0;   ///< one representative halfedge per outer ccb
  virtual void face_inner_ccbs(FH f, std::vector<HH>& out) const = 0;   ///< one representative halfedge per inner ccb (hole)
  virtual void face_isolated_vertices(FH f, std::vector<VH>& out) const = 0;
  virtual PyRef& face_data(FH f) = 0;
  virtual const PyRef& face_data(FH f) const = 0;
  /// Boundary of a face as directed x-monotone curves: outer ccb (empty if the face has no
  /// outer ccb) followed by every inner ccb. Curves follow the halfedge traversal, i.e. the
  /// face lies to the LEFT of each curve (outer boundary counterclockwise, holes clockwise).
  virtual void face_polygon(FH f, std::vector<Geom>& outer, std::vector<std::vector<Geom>>& holes) const = 0;

  // ---- Arrangement_2 modification (no history bookkeeping) --------------
  virtual VH insert_point_in_face_interior(const Geom& p, FH f) = 0;
  virtual HH insert_in_face_interior(const Geom& xc, FH f) = 0;
  virtual HH insert_from_left_vertex(const Geom& xc, VH v) = 0;
  virtual HH insert_from_right_vertex(const Geom& xc, VH v) = 0;
  virtual HH insert_at_vertices(const Geom& xc, VH v1, VH v2) = 0;
  virtual VH modify_vertex(VH v, const Geom& p) = 0;
  virtual FH remove_isolated_vertex(VH v) = 0;
  virtual HH modify_edge(HH h, const Geom& xc) = 0;
  virtual HH split_edge(HH h, const Geom& xc1, const Geom& xc2) = 0;     ///< Arrangement_2::split_edge; returns the halfedge directed like h whose target is the split vertex
  virtual HH merge_edge(HH h1, HH h2, const Geom& xc) = 0;               ///< Arrangement_2::merge_edge
  virtual FH remove_edge(HH h, bool remove_source, bool remove_target) = 0;   ///< CGAL::remove_edge semantics: returns the face containing the removed edge; optionally removes end vertices that become isolated/redundant

  // ---- with-history operations & global insertion functions --------------
  virtual CH insert_curve(const Geom& c) = 0;                            ///< CGAL::insert(arr, curve): general curve, recorded in history
  virtual void insert_curves(const std::vector<Geom>& cs, std::vector<CH>& out) = 0;   ///< aggregate (sweep-line) insertion, recorded in history
  virtual HH insert_non_intersecting_curve(const Geom& xc) = 0;         ///< no history (Precondition error if it intersects the arrangement interior)
  virtual void insert_non_intersecting_curves(const std::vector<Geom>& xcs) = 0;
  virtual VH insert_point(const Geom& p) = 0;                            ///< CGAL::insert_point
  virtual bool remove_vertex(VH v) = 0;                                  ///< CGAL::remove_vertex: isolated vertices, or degree-2 vertices whose edges can be merged
  virtual std::size_t remove_curve(CH c) = 0;                            ///< remove an input curve and all edges it induces (edges also induced by other curves are kept); returns #removed edges
  virtual HH split_edge_at_point(HH h, const Geom& p) = 0;               ///< with-history split_edge(e, p): keeps history consistent
  virtual HH merge_edge_history(HH h1, HH h2) = 0;                       ///< with-history merge_edge(e1, e2)

  // ---- history queries ---------------------------------------------------
  virtual Geom curve_geometry(CH c) const = 0;
  virtual std::size_t number_of_induced_edges(CH c) const = 0;
  virtual void induced_edges(CH c, std::vector<HH>& out) const = 0;
  virtual std::size_t number_of_originating_curves(HH h) const = 0;
  virtual void originating_curves(HH h, std::vector<CH>& out) const = 0;

  // ---- point location / queries -----------------------------------------
  virtual Located locate(const Geom& p, int strategy = PL_DEFAULT) const = 0;
  /// Vertical ray shooting (CGAL semantics: a miss returns the unbounded face containing the
  /// ray, or a fictitious halfedge in the Linear kind). Only some strategies support it
  /// (simple, walk, trapezoid); PL_DEFAULT picks a supporting one.
  virtual Located ray_shoot_up(const Geom& p, int strategy = PL_DEFAULT) const = 0;
  virtual Located ray_shoot_down(const Geom& p, int strategy = PL_DEFAULT) const = 0;
  virtual void batched_locate(const std::vector<Geom>& pts, std::vector<Located>& out) const = 0;   ///< out[i] corresponds to pts[i] (CGAL returns xy-sorted results; the impl restores input order)
  virtual bool supports_point_location(int strategy) const = 0;
  virtual void attach_point_location(int strategy) = 0;             ///< build & keep a strategy object (observer-updated where CGAL supports it); Unsupported if !supports_point_location
  virtual void detach_point_location(int strategy) = 0;
  virtual bool has_point_location(int strategy) const = 0;
  virtual void zone(const Geom& c, std::vector<Located>& out) = 0;   ///< CGAL::zone of an x-monotone curve (features in order along the curve); a general Curve is subdivided first
  virtual bool do_intersect(const Geom& c) = 0;
  virtual void decompose(std::vector<VerticalDecompositionEntry>& out) const = 0;   ///< CGAL::decompose (vertical decomposition), vertices in xy-lexicographic order

  // ---- observers ---------------------------------------------------------
  virtual int add_observer(void* user, ObserverFn fn) = 0;    ///< returns a token
  virtual void remove_observer(int token) = 0;

  // ---- overlay -----------------------------------------------------------
  /// this (A) overlaid with `other` (B) into `result` (R). R must be an empty arrangement of the
  /// same kind and a distinct object; A and B are not modified (CGAL touches transient marks).
  /// See the free function arr2d::overlay for the callback contract.
  virtual void overlay_with(const ArrBase& other, ArrBase& result, void* user, OverlayFn fn) const = 0;

  // ---- bulk export -------------------------------------------------------
  virtual void vertex_coordinates(std::vector<double>& out) const = 0;   ///< dimension() doubles per concrete vertex, in vertices() order
  virtual void edge_vertex_indices(std::vector<std::size_t>& out) const = 0;   ///< 2 indices (source, target of the representative halfedge) per edge in edges() order, indices into vertices()
  /// Per face (faces() order): a list of cycles (outer ccbs first, then inner ccbs), each a list of
  /// vertex indices along the ccb. Fictitious vertices/halfedges are skipped (cycles may then be open).
  virtual void face_boundaries(std::vector<std::vector<std::vector<std::size_t>>>& out) const = 0;
  virtual BBox bbox() const = 0;                                        ///< of vertex approximations (empty arrangement: zeros)
};

/// Overlay: r must be an EMPTY arrangement of the same kind as a and b. For every created
/// element the OverlayTraits callback is invoked (fn may be null). Result elements' data are
/// default (None); callbacks may set them through r's handles. History: the result records
/// the input curves of both a and b.
void overlay(const ArrBase& a, const ArrBase& b, ArrBase& r, void* user, OverlayFn fn);

}  // namespace arr2d
