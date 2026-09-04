// arr2d — Boolean set operations for Kind::Conic.
//
// GPS traits: CGAL::Gps_traits_2<ConicTypes::Traits>.  Note that Gps_traits_2<T> is a
// DIFFERENT type from T: it derives from T and adds Polygon_2 = General_polygon_2<T>,
// Polygon_with_holes_2, Construct_polygon_2, Construct_curves_2, Is_unbounded, Equal_2 ...
// Its Polygon_2 is a sequence of DIRECTED x-monotone conic arcs, which is exactly what
// PolygonGeom carries.
//
// Gps_traits_2 declares no constructor and no data member, so it is a C++17 aggregate with
// one public base; `GpsTraits{ConicTypes::traits()}` therefore COPY-CONSTRUCTS the base from
// the process-wide conic traits (verified by a compile+run probe).  Per traits_conic.md §1.2
// that copy shares the rational/algebraic kernels through shared_ptr and starts with its own
// copy of the intersection cache — correct, only slightly less cached than the arrangements'.
//
// The instance is heap-allocated and never freed: it holds CORE::Expr-backed state, and
// destroying such an object from static storage aborts the process at exit with
// `CGAL error: assertion violation! ! blocks.empty()` (CGAL/CORE/MemoryPool.h:125).
//
// LINK NOTE: this TU references ConicTypes::traits(), which kind_conic.cpp defines, so
// bso_conic.o needs kind_conic.o (that is also where the process-wide conic traits, and hence
// the shared caches, live).
#include <memory>

#include <CGAL/Gps_traits_2.h>

#include "arr2d/bso.hpp"
#include "arr2d/impl/polygon_set_impl.hpp"
#include "arr2d/kinds/conic_types.hpp"

namespace arr2d {
namespace {

using ConicGpsTraits = CGAL::Gps_traits_2<ConicTypes::Traits>;

struct ConicGpsPolicy {
  static const ConicGpsTraits& gps_traits() {
    // Aggregate initialisation of the Gps_traits_2 base from the process-wide conic traits.
    // Leaked on purpose (CORE memory pool + General_polygon_set_2 stores the pointer).
    static const ConicGpsTraits* p = new ConicGpsTraits{ConicTypes::traits()};
    return *p;
  }
  /// Conic_x_monotone_arc_2 derives from Conic_arc_2, so KindOps::to_curve() yields the very
  /// same arc as a general Curve_2 and the with-history insertion in to_arrangement() is exact.
  static constexpr bool insert_with_history = true;
};

using ConicPolygonSet = PolygonSetImpl<ConicTypes, ConicGpsTraits, ConicGpsPolicy>;

}  // namespace

template class PolygonSetImpl<ConicTypes, ConicGpsTraits, ConicGpsPolicy>;

std::unique_ptr<PolygonSetBase> make_polygon_set_conic() {
  return std::unique_ptr<PolygonSetBase>(new ConicPolygonSet());
}

}  // namespace arr2d
