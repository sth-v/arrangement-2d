// arr2d — Boolean set operations for Kind::Bezier.
//
// GPS traits: CGAL::Gps_traits_2<BezierTypes::Traits> (a DIFFERENT type from the arrangement
// traits: it derives from it and adds Polygon_2 = General_polygon_2<Traits> and friends).  A
// polygon boundary is a closed chain of DIRECTED _Bezier_x_monotone_2 curves, which is exactly
// what PolygonGeom carries.
//
// Gps_traits_2 is a C++17 aggregate (no declared constructor, no data member, one public
// base), so `GpsTraits{BezierTypes::traits()}` COPY-CONSTRUCTS the base from the process-wide
// Bezier traits.  Per traits_bezier.md gotcha 2 that copy ALIASES the original's Bezier_cache
// and Intersection_map with `m_owner == false`: it never frees them, and the Boolean code
// therefore shares the very cache the arrangements of this kind use.  This is why the
// instance must be constructed from BezierTypes::traits() and not default-constructed.
//
// The instance is heap-allocated and never freed: CORE::Expr-backed objects in static storage
// abort at exit with `! blocks.empty()` (CGAL/CORE/MemoryPool.h:125).
//
// LINK NOTE: this TU references BezierTypes::traits(), defined by kind_bezier.cpp.
#include <memory>

#include <CGAL/Gps_traits_2.h>

#include "arr2d/bso.hpp"
#include "arr2d/impl/polygon_set_impl.hpp"
#include "arr2d/kinds/bezier_types.hpp"

namespace arr2d {
namespace {

using BezierGpsTraits = CGAL::Gps_traits_2<BezierTypes::Traits>;

struct BezierGpsPolicy {
  static const BezierGpsTraits& gps_traits() {
    static const BezierGpsTraits* p = new BezierGpsTraits{BezierTypes::traits()};
    return *p;
  }
  /// FALSE for this kind: a _Bezier_x_monotone_2 is a sub-arc of a supporting _Bezier_curve_2
  /// and there is no Curve_2 that represents just that sub-arc, so KindOps::to_curve() returns
  /// the WHOLE supporting curve.  Inserting that into the arrangement would add geometry that
  /// is not on the polygon boundary, so PolygonSetImpl::to_arrangement() falls back to
  /// insert_non_intersecting_curve() per boundary curve (exact, but without curve history).
  static constexpr bool insert_with_history = false;
};

using BezierPolygonSet = PolygonSetImpl<BezierTypes, BezierGpsTraits, BezierGpsPolicy>;

}  // namespace

template class PolygonSetImpl<BezierTypes, BezierGpsTraits, BezierGpsPolicy>;

std::unique_ptr<PolygonSetBase> make_polygon_set_bezier() {
  return std::unique_ptr<PolygonSetBase>(new BezierPolygonSet());
}

}  // namespace arr2d
