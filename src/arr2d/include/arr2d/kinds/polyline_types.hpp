#pragma once
#include <CGAL/Exact_predicates_exact_constructions_kernel.h>
#include <CGAL/Arr_segment_traits_2.h>
#include <CGAL/Arr_polyline_traits_2.h>
#include <CGAL/Arr_extended_dcel.h>
#include <CGAL/Arrangement_with_history_2.h>
#include "arr2d/common.hpp"
#include "arr2d/dcel_data.hpp"

namespace arr2d {
struct PolylineTypes {
  static constexpr Kind kind = Kind::Polyline;
  static constexpr bool is_sphere = false;
  static constexpr bool is_unbounded = false;
  using Kernel = CGAL::Exact_predicates_exact_constructions_kernel;
  using FT = Kernel::FT;
  using SegmentTraits = CGAL::Arr_segment_traits_2<Kernel>;
  using Traits = CGAL::Arr_polyline_traits_2<SegmentTraits>;
  using Point_2 = Traits::Point_2;                          // Kernel::Point_2
  using Curve_2 = Traits::Curve_2;                          // polyline (general)
  using X_monotone_curve_2 = Traits::X_monotone_curve_2;    // x-monotone polyline
  using Segment_2 = SegmentTraits::X_monotone_curve_2;      // Arr_segment_2<Kernel> (sub-curve type)
  using Dcel = CGAL::Arr_extended_dcel<Traits, VData, HData, FData>;
  using Arrangement = CGAL::Arrangement_with_history_2<Traits, Dcel>;
  static const Traits& traits();
};
}  // namespace arr2d
