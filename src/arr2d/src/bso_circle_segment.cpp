// arr2d — Boolean set operations for Kind::CircleSegment.
//
// GPS traits: CGAL::Gps_circle_segment_traits_2<Epeck>, which is
// Gps_traits_2<Arr_circle_segment_traits_2<Epeck, true>> plus a constructor taking
// `bool use_cache` (boolean_set_operations.md §12).  Its Polygon_2 is a
// General_polygon_2<Arr_circle_segment_traits_2<Epeck,true>>, i.e. a sequence of DIRECTED
// x-monotone circle-segment curves — exactly what PolygonGeom carries, so no point/curve
// round trip is needed.  A full circle is a two-curve polygon (CGAL rejects a one-curve
// polygon: "A polygon cannot have just a single edge").
//
// Gps_circle_segment_traits_2 cannot be built from an existing Arr_circle_segment_traits_2
// (its only constructor is `explicit-ish` Gps_circle_segment_traits_2(bool use_cache = false)),
// so this instance is independent of CircleSegmentTypes::traits().  That is harmless: the only
// state of Arr_circle_segment_traits_2 is `m_use_cache` plus a mutable memo table of x-monotone
// intersections; it carries no geometry and no CORE value.  Consequently this TU does not
// reference CircleSegmentTypes::traits() and links without kind_circle_segment.o (only
// PolygonSetImpl::to_arrangement() needs that TU, through arr2d::make_arrangement()).
#include <memory>

#include <CGAL/Gps_circle_segment_traits_2.h>

#include "arr2d/bso.hpp"
#include "arr2d/impl/polygon_set_impl.hpp"
#include "arr2d/kinds/circle_segment_types.hpp"

namespace arr2d {
namespace {

using CircleSegmentGpsTraits = CGAL::Gps_circle_segment_traits_2<CircleSegmentTypes::Kernel>;

struct CircleSegmentGpsPolicy {
  /// Deliberately leaked (General_polygon_set_2 keeps a raw pointer to it —
  /// boolean_set_operations.md gotcha 4).  `use_cache = false` is CGAL's own default and
  /// matches the plain Arr_circle_segment_traits_2 default used by the kind TU.
  static const CircleSegmentGpsTraits& gps_traits() {
    static const CircleSegmentGpsTraits* p = new CircleSegmentGpsTraits(false);
    return *p;
  }
  /// KindOps::to_curve() maps an _X_monotone_circle_segment_2 back to the equivalent
  /// _Circle_segment_2 arc, so the with-history insertion in to_arrangement() reproduces the
  /// very same edges.
  static constexpr bool insert_with_history = true;
};

using CircleSegmentPolygonSet =
    PolygonSetImpl<CircleSegmentTypes, CircleSegmentGpsTraits, CircleSegmentGpsPolicy>;

}  // namespace

template class PolygonSetImpl<CircleSegmentTypes, CircleSegmentGpsTraits, CircleSegmentGpsPolicy>;

std::unique_ptr<PolygonSetBase> make_polygon_set_circle_segment() {
  return std::unique_ptr<PolygonSetBase>(new CircleSegmentPolygonSet());
}

}  // namespace arr2d
