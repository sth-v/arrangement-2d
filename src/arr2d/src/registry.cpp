// arr2d — kind registry implementation + small shared helpers from common.hpp.
#include "arr2d/registry.hpp"

#include <CGAL/version.h>
#include <CGAL/Exact_rational.h>

#include <array>
#include <cstring>
#include <mutex>
#include <sstream>
#include <typeinfo>

namespace arr2d {

namespace {
std::array<KindEntry, int(Kind::NumKinds)>& table() {
  static std::array<KindEntry, int(Kind::NumKinds)> t{};
  return t;
}
PyObjectHook g_incref = nullptr;
PyObjectHook g_decref = nullptr;
}  // namespace

// ---- common.hpp ----
int kind_from_name(const std::string& name) {
  for (int i = 0; i < int(Kind::NumKinds); ++i)
    if (name == kind_name(Kind(i))) return i;
  // friendly aliases
  if (name == "segments" || name == "seg") return int(Kind::Segment);
  if (name == "lines" || name == "line" || name == "rays") return int(Kind::Linear);
  if (name == "circle" || name == "circles" || name == "circular" || name == "circular_arc" || name == "arc" || name == "arcs" || name == "circle-segment")
    return int(Kind::CircleSegment);
  if (name == "polylines") return int(Kind::Polyline);
  if (name == "beziers" || name == "bézier") return int(Kind::Bezier);
  if (name == "conics") return int(Kind::Conic);
  if (name == "spherical" || name == "geodesic") return int(Kind::Sphere);
  return -1;
}

const char* point_location_name(int strategy) {
  switch (strategy) {
    case PL_DEFAULT: return "default";
    case PL_NAIVE: return "naive";
    case PL_SIMPLE: return "simple";
    case PL_WALK: return "walk";
    case PL_LANDMARKS: return "landmarks";
    case PL_TRAPEZOID: return "trapezoid";
    case PL_TRIANGULATION: return "triangulation";
    default: return "?";
  }
}

int point_location_from_name(const std::string& name) {
  if (name == "default" || name.empty()) return PL_DEFAULT;
  for (int i = 0; i < PL_NUM_STRATEGIES; ++i)
    if (name == point_location_name(i)) return i;
  if (name == "walk_along_line" || name == "walk-along-line") return PL_WALK;
  if (name == "trapezoidal" || name == "ric" || name == "trapezoid_ric" || name == "trapezoidal_map") return PL_TRAPEZOID;
  if (name == "landmark") return PL_LANDMARKS;
  return -2;
}

void set_pyobject_hooks(PyObjectHook incref, PyObjectHook decref) {
  g_incref = incref;
  g_decref = decref;
}
void pyobject_incref(void* obj) { if (obj && g_incref) g_incref(obj); }
void pyobject_decref(void* obj) { if (obj && g_decref) g_decref(obj); }

// ---- registry ----
void register_kind(Kind k, const KindEntry& entry) { table()[int(k)] = entry; }

void init_all_kinds() {
  // Kinds self-register through static registrars in their TUs (see registry.hpp).
}

bool kind_available(Kind k) {
  int i = int(k);
  return i >= 0 && i < int(Kind::NumKinds) && table()[i].ops != nullptr;
}

const KindOps& ops(Kind k) {
  if (!kind_available(k)) throw_error(ErrorCode::Unsupported, std::string("kind not available: ") + kind_name(k));
  return *table()[int(k)].ops;
}

std::unique_ptr<ArrBase> make_arrangement(Kind k) {
  if (!kind_available(k)) throw_error(ErrorCode::Unsupported, std::string("kind not available: ") + kind_name(k));
  return table()[int(k)].make_arrangement();
}

bool kind_has_polygon_set(Kind k) { return kind_available(k) && table()[int(k)].make_polygon_set != nullptr; }

std::unique_ptr<PolygonSetBase> make_polygon_set(Kind k) {
  if (!kind_has_polygon_set(k))
    throw_error(ErrorCode::Unsupported, std::string("Boolean set operations are not available for kind '") + kind_name(k) + "'");
  return table()[int(k)].make_polygon_set();
}

std::string cgal_version() { return CGAL_STR(CGAL_VERSION); }

std::string build_info() {
  std::ostringstream os;
  os << "CGAL " << CGAL_STR(CGAL_VERSION) << "; Exact_rational=" << typeid(CGAL::Exact_rational).name();
#ifdef CGAL_USE_CORE
  os << "; CORE";
#endif
#ifdef CGAL_NDEBUG
  os << "; CGAL_NDEBUG (assertions off)";
#else
  os << "; CGAL assertions on";
#endif
  return os.str();
}

}  // namespace arr2d
