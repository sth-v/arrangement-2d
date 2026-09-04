#pragma once
#include <CGAL/CORE_algebraic_number_traits.h>
#include <CGAL/Cartesian.h>
// MUST come before <CGAL/Arr_Bezier_curve_traits_2.h>: it defines an explicit specialization of
// _Bezier_cache<CORE_algebraic_number_traits>::get_intersections (CGAL 6.1 pairs the two curves'
// resultant roots assuming a bijection and reads out of bounds when one curve passes through the
// other's self-intersection point), and [temp.expl.spec]/6 requires the specialization to be
// declared before anything can implicitly instantiate the member.  This header is the ONLY place
// in the project that includes the Bezier traits, which is what makes that orderable at all.
#include "arr2d/impl/bezier_cache_fix.hpp"
#include <CGAL/Arr_Bezier_curve_traits_2.h>
#include <CGAL/Arr_extended_dcel.h>
#include <CGAL/Arrangement_with_history_2.h>
#include "arr2d/common.hpp"
#include "arr2d/dcel_data.hpp"

namespace arr2d {
struct BezierTypes {
  static constexpr Kind kind = Kind::Bezier;
  static constexpr bool is_sphere = false;
  static constexpr bool is_unbounded = false;
  using Nt_traits = CGAL::CORE_algebraic_number_traits;
  using Rational = Nt_traits::Rational;                     // == arr2d::Rational (mpq_rational)
  using Algebraic = Nt_traits::Algebraic;                   // CORE::Expr
  using Integer = Nt_traits::Integer;                       // CORE::BigInt
  using Rat_kernel = CGAL::Cartesian<Rational>;
  using Alg_kernel = CGAL::Cartesian<Algebraic>;
  using Traits = CGAL::Arr_Bezier_curve_traits_2<Rat_kernel, Alg_kernel, Nt_traits>;
  using Point_2 = Traits::Point_2;                          // _Bezier_point_2
  using Curve_2 = Traits::Curve_2;                          // _Bezier_curve_2
  using X_monotone_curve_2 = Traits::X_monotone_curve_2;    // _Bezier_x_monotone_2
  using Rat_point_2 = Rat_kernel::Point_2;
  using Dcel = CGAL::Arr_extended_dcel<Traits, VData, HData, FData>;
  using Arrangement = CGAL::Arrangement_with_history_2<Traits, Dcel>;
  /// ONE process-wide traits object (owns the Bezier cache that all curves/points/arrangements of
  /// this kind share). Arrangements are constructed with a pointer to it. Never copied.
  static Traits& traits();
};
}  // namespace arr2d
