#pragma once
#include <CGAL/Exact_predicates_exact_constructions_kernel.h>
#include <CGAL/Arr_circle_segment_traits_2.h>
#include <CGAL/Arr_extended_dcel.h>
#include <CGAL/Arrangement_with_history_2.h>
#include "arr2d/common.hpp"
#include "arr2d/dcel_data.hpp"

namespace arr2d {
struct CircleSegmentTypes {
  static constexpr Kind kind = Kind::CircleSegment;
  static constexpr bool is_sphere = false;
  static constexpr bool is_unbounded = false;
  using Kernel = CGAL::Exact_predicates_exact_constructions_kernel;
  using FT = Kernel::FT;
  using Traits = CGAL::Arr_circle_segment_traits_2<Kernel>;  // Filter = true
  using Point_2 = Traits::Point_2;                            // _One_root_point_2<FT, true>
  using Curve_2 = Traits::Curve_2;                            // _Circle_segment_2<Kernel, true>
  using X_monotone_curve_2 = Traits::X_monotone_curve_2;      // _X_monotone_circle_segment_2<Kernel, true>
  using Dcel = CGAL::Arr_extended_dcel<Traits, VData, HData, FData>;
  using Arrangement = CGAL::Arrangement_with_history_2<Traits, Dcel>;
  static const Traits& traits();
};
}  // namespace arr2d
