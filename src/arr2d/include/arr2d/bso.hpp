// arr2d — per-kind Boolean-set-operation factories (defined in bso_<kind>.cpp).
// Kind TUs reference these when filling KindEntry::make_polygon_set.
#pragma once
#include <memory>
#include "arr2d/polygon_set.hpp"

namespace arr2d {
std::unique_ptr<PolygonSetBase> make_polygon_set_segment();
std::unique_ptr<PolygonSetBase> make_polygon_set_circle_segment();
std::unique_ptr<PolygonSetBase> make_polygon_set_conic();
std::unique_ptr<PolygonSetBase> make_polygon_set_bezier();
}  // namespace arr2d
