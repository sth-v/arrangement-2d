#pragma once
#include <CGAL/CORE_algebraic_number_traits.h>
#include <CGAL/Cartesian.h>
#include <CGAL/Arr_conic_traits_2.h>
#include <CGAL/Arr_extended_dcel.h>
#include <CGAL/Arrangement_with_history_2.h>
#include "arr2d/common.hpp"
#include "arr2d/dcel_data.hpp"

namespace arr2d {
struct ConicTypes {
  static constexpr Kind kind = Kind::Conic;
  static constexpr bool is_sphere = false;
  static constexpr bool is_unbounded = false;
  using Nt_traits = CGAL::CORE_algebraic_number_traits;
  using Rational = Nt_traits::Rational;
  using Algebraic = Nt_traits::Algebraic;                   // CORE::Expr
  using Integer = Nt_traits::Integer;
  using Rat_kernel = CGAL::Cartesian<Rational>;
  using Alg_kernel = CGAL::Cartesian<Algebraic>;
  using Traits = CGAL::Arr_conic_traits_2<Rat_kernel, Alg_kernel, Nt_traits>;
  using Point_2 = Traits::Point_2;                          // Conic_point_2<Alg_kernel>
  using Curve_2 = Traits::Curve_2;                          // Conic_arc_2
  using X_monotone_curve_2 = Traits::X_monotone_curve_2;    // Conic_x_monotone_arc_2
  using Rat_point_2 = Rat_kernel::Point_2;
  using Rat_segment_2 = Rat_kernel::Segment_2;
  using Rat_circle_2 = Rat_kernel::Circle_2;
  using Dcel = CGAL::Arr_extended_dcel<Traits, VData, HData, FData>;
  using Arrangement = CGAL::Arrangement_with_history_2<Traits, Dcel>;
  static Traits& traits();                                  // process-wide traits object (may hold caches)
};
}  // namespace arr2d
