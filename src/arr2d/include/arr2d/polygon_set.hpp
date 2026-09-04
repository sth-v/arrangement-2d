// arr2d — type-erased 2D Boolean set operations (CGAL General_polygon_set_2).
//
// Supported kinds: Segment (Gps_segment_traits_2<Epeck>), CircleSegment (Gps_circle_segment_traits_2<Epeck>),
// Conic (Gps_traits_2<Arr_conic_traits_2>), Bezier (Gps_traits_2<Arr_Bezier_curve_traits_2>).
//
// Polygons are exchanged as sequences of DIRECTED x-monotone curves (Geom, GeomType::XCurve)
// chained end to end: curve i's target == curve i+1's source, closed. Outer boundaries must be
// counterclockwise and holes clockwise (CGAL precondition) — use orientation() / reverse to fix.
#pragma once

#include <memory>
#include <vector>

#include "arr2d/common.hpp"
#include "arr2d/arrangement.hpp"

namespace arr2d {

struct PolygonGeom {
  std::vector<Geom> outer;                 ///< EMPTY outer == unbounded polygon ("the whole plane minus holes"), matching CGAL's representation
  std::vector<std::vector<Geom>> holes;
  bool unbounded = false;                  ///< informational; must not be true when outer is non-empty
};

class PolygonSetBase {
 public:
  virtual ~PolygonSetBase() = default;
  virtual Kind kind() const = 0;
  virtual std::unique_ptr<PolygonSetBase> clone() const = 0;
  virtual void clear() = 0;

  // ---- validation helpers (do not throw on invalid input) ----
  virtual int orientation(const std::vector<Geom>& boundary) const = 0;      ///< +1 ccw, -1 cw, 0 degenerate/not closed
  virtual bool is_valid_polygon(const PolygonGeom& p) const = 0;              ///< CGAL's is_valid_polygon / is_valid_polygon_with_holes
  virtual bool is_closed_chain(const std::vector<Geom>& boundary) const = 0;  ///< consecutive curves connect and the chain closes (CGAL semantics: an empty chain is vacuously closed, a single-curve chain is not)

  // ---- construction ----
  virtual void insert(const PolygonGeom& p) = 0;                ///< validated first (CGAL is_valid_polygon*): throws Error(InvalidArgument) naming the defect; disjointness from the current content remains a CGAL precondition (use join_polygon for overlapping input)
  virtual void insert_polygons(const std::vector<PolygonGeom>& ps) = 0;   ///< range insertion (polygons may not overlap each other)

  // ---- Boolean operations (in place) ----
  virtual void join(const PolygonSetBase& other) = 0;
  virtual void intersection(const PolygonSetBase& other) = 0;
  virtual void difference(const PolygonSetBase& other) = 0;
  virtual void symmetric_difference(const PolygonSetBase& other) = 0;
  virtual void complement() = 0;
  virtual void join_polygon(const PolygonGeom& p) = 0;         ///< with a single valid polygon (with holes)
  virtual void intersection_polygon(const PolygonGeom& p) = 0;
  virtual void difference_polygon(const PolygonGeom& p) = 0;
  virtual void symmetric_difference_polygon(const PolygonGeom& p) = 0;

  // ---- queries ----
  virtual std::size_t number_of_polygons_with_holes() const = 0;
  virtual bool is_empty() const = 0;
  virtual bool is_plane() const = 0;
  virtual void polygons_with_holes(std::vector<PolygonGeom>& out) const = 0;
  virtual int oriented_side(const Geom& point) const = 0;      ///< +1 inside, 0 on boundary, -1 outside
  virtual int oriented_side_of_set(const PolygonSetBase& other) const = 0;   ///< +1 overlap, 0 touch only, -1 disjoint (CGAL oriented_side(polygon set))
  virtual bool locate(const Geom& point, PolygonGeom& out) const = 0;   ///< true + the polygon containing the point
  virtual bool do_intersect(const PolygonSetBase& other) const = 0;
  virtual bool is_valid() const = 0;

  // ---- arrangement bridge ----
  /// Build a fresh arrangement (our ArrBase of the same kind, with history) containing the
  /// boundaries of all polygons; `contained` receives the faces that belong to the set.
  virtual std::unique_ptr<ArrBase> to_arrangement(std::vector<FH>& contained) const = 0;
  /// Underlying arrangement sizes (for diagnostics).
  virtual std::size_t arrangement_number_of_faces() const = 0;
  virtual std::size_t arrangement_number_of_edges() const = 0;
};

}  // namespace arr2d
