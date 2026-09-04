// arr2d — Boolean set operations for Kind::Segment.
//
// GPS traits: CGAL::Gps_segment_traits_2<Epeck>.  Its Polygon_2 is a plain
// CGAL::Polygon_2<Epeck> of POINTS (not a General_polygon_2 of curves), so PolygonSetImpl
// converts a boundary of directed Arr_segment_2 curves to the sequence of their targets and
// back to one segment per consecutive vertex pair (boolean_set_operations.md §12).  That is
// lossless exactly because the chain is closed, which every entry point checks first.
//
// This TU is self-contained: it does NOT reference SegmentTypes::traits(), because
// Arr_segment_traits_2 is stateless and Gps_segment_traits_2 has to be default-constructed
// anyway.  Only PolygonSetImpl::to_arrangement() needs kind_segment.cpp (through
// arr2d::make_arrangement(Kind::Segment)).
#include <memory>

#include <CGAL/Gps_segment_traits_2.h>

#include "arr2d/bso.hpp"
#include "arr2d/impl/polygon_set_impl.hpp"
#include "arr2d/kinds/segment_types.hpp"

namespace arr2d {
namespace {

/// CGAL::Gps_segment_traits_2<K, Container, ArrSegmentTraits>; the defaults
/// (std::vector<K::Point_2>, Arr_segment_traits_2<K>) are exactly SegmentTypes'.
using SegmentGpsTraits = CGAL::Gps_segment_traits_2<SegmentTypes::Kernel>;

struct SegmentGpsPolicy {
  /// Deliberately leaked: General_polygon_set_2 stores the traits BY POINTER
  /// (boolean_set_operations.md gotcha 4), so it must outlive every set ever created.
  /// Arr_segment_traits_2 holds no CORE value, but the same shape is used for all four kinds.
  static const SegmentGpsTraits& gps_traits() {
    static const SegmentGpsTraits* p = new SegmentGpsTraits();
    return *p;
  }
  /// Curve_2 == X_monotone_curve_2 for this kind, so to_arrangement() can insert the boundary
  /// with history (CGAL::insert on the with-history arrangement).
  static constexpr bool insert_with_history = true;
};

using SegmentPolygonSet = PolygonSetImpl<SegmentTypes, SegmentGpsTraits, SegmentGpsPolicy>;

}  // namespace

// Force every member (not only the virtuals reachable through the vtable) to be instantiated.
template class PolygonSetImpl<SegmentTypes, SegmentGpsTraits, SegmentGpsPolicy>;

std::unique_ptr<PolygonSetBase> make_polygon_set_segment() {
  return std::unique_ptr<PolygonSetBase>(new SegmentPolygonSet());
}

}  // namespace arr2d
