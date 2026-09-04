#pragma once
#include <CGAL/Exact_predicates_exact_constructions_kernel.h>
#include <CGAL/Arr_segment_traits_2.h>
#include <CGAL/Arr_extended_dcel.h>
#include <CGAL/Arrangement_with_history_2.h>
#include "arr2d/common.hpp"
#include "arr2d/dcel_data.hpp"

namespace arr2d {
struct SegmentTypes {
  static constexpr Kind kind = Kind::Segment;
  static constexpr bool is_sphere = false;
  static constexpr bool is_unbounded = false;
  using Kernel = CGAL::Exact_predicates_exact_constructions_kernel;
  using FT = Kernel::FT;                                   // Lazy_exact_nt<mpq_rational>
  using Traits = CGAL::Arr_segment_traits_2<Kernel>;
  using Point_2 = Traits::Point_2;                          // Kernel::Point_2
  using Curve_2 = Traits::Curve_2;                          // Arr_segment_2<Kernel>
  using X_monotone_curve_2 = Traits::X_monotone_curve_2;    // same type as Curve_2
  using Dcel = CGAL::Arr_extended_dcel<Traits, VData, HData, FData>;
  using Arrangement = CGAL::Arrangement_with_history_2<Traits, Dcel>;
  static const Traits& traits();                            // process-wide traits instance (defined in kind_segment.cpp)
};
}  // namespace arr2d
