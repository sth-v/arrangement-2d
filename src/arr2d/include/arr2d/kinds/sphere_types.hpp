#pragma once
#include <CGAL/Exact_predicates_exact_constructions_kernel.h>
#include <CGAL/Arr_geodesic_arc_on_sphere_traits_2.h>
#include <CGAL/Arr_spherical_topology_traits_2.h>
#include <CGAL/Arr_extended_dcel.h>
#include <CGAL/Arrangement_on_surface_with_history_2.h>
#include "arr2d/common.hpp"
#include "arr2d/dcel_data.hpp"

namespace arr2d {
struct SphereTypes {
  static constexpr Kind kind = Kind::Sphere;
  static constexpr bool is_sphere = true;
  static constexpr bool is_unbounded = false;
  using Kernel = CGAL::Exact_predicates_exact_constructions_kernel;
  using FT = Kernel::FT;
  using Traits = CGAL::Arr_geodesic_arc_on_sphere_traits_2<Kernel>;   // identification curve at atan_x=-1, atan_y=0 (negative x half-plane)
  using Point_2 = Traits::Point_2;                          // Arr_extended_direction_3<Kernel>
  using Curve_2 = Traits::Curve_2;                          // Arr_geodesic_arc_on_sphere_3<Kernel>
  using X_monotone_curve_2 = Traits::X_monotone_curve_2;    // Arr_x_monotone_geodesic_arc_on_sphere_3<Kernel>
  using Dcel = CGAL::Arr_extended_dcel<Traits, VData, HData, FData>;
  using Topology_traits = CGAL::Arr_spherical_topology_traits_2<Traits, Dcel>;
  using Arrangement = CGAL::Arrangement_on_surface_with_history_2<Traits, Topology_traits>;
  static const Traits& traits();
};
}  // namespace arr2d
