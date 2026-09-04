// arr2d — small detection-idiom helpers used by impl/arr_impl.hpp.
//
// CGAL 6.1 spells some members only on *some* arrangement classes:
//   * `unbounded_face()` and `number_of_vertices_at_infinity()` exist on
//     Arrangement_2 / Arrangement_with_history_2 (planar) but NOT on
//     Arrangement_on_surface_2 / Arrangement_on_surface_with_history_2
//     (used for the sphere kind)  — see docs/dev/cgal61_api/arrangement_core.md §6.9/§7
//     and traits_geodesic_sphere.md §7.3.
//   * `fictitious_face()` IS declared on Arrangement_on_surface_2 for every topology, but its
//     body does not compile for the spherical topology traits (no `initial_face()`), so it can
//     NOT be detected — guard it with `Types::is_unbounded` instead.
#pragma once

#include <type_traits>
#include <utility>

namespace arr2d {
namespace impl {

template <class...>
using void_t = void;

#define ARR2D_DEFINE_DETECTOR(NAME, EXPR)                                                   \
  template <class T, class = void>                                                          \
  struct NAME : std::false_type {};                                                         \
  template <class T>                                                                        \
  struct NAME<T, void_t<decltype(EXPR)>> : std::true_type {};                               \
  template <class T>                                                                        \
  inline constexpr bool NAME##_v = NAME<T>::value

/// Detects `arr.unbounded_face()` (planar arrangement classes only).
ARR2D_DEFINE_DETECTOR(has_unbounded_face, std::declval<T&>().unbounded_face());
/// Detects `arr.number_of_vertices_at_infinity()` (planar arrangement classes only).
ARR2D_DEFINE_DETECTOR(has_vertices_at_infinity, std::declval<const T&>().number_of_vertices_at_infinity());
/// Detects `arr.reference_face()` (present on every Arrangement_on_surface_2).
ARR2D_DEFINE_DETECTOR(has_reference_face, std::declval<T&>().reference_face());

#undef ARR2D_DEFINE_DETECTOR

}  // namespace impl
}  // namespace arr2d
