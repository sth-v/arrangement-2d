// arr2d — type-erased C++ core for the arrangement_2d Python package.
//
// This header defines the small vocabulary shared by every part of the core:
// the geometry "kinds", the exact rational type, opaque handles, the type-erased
// geometry box (Geom), the Python-object holder (PyRef) and the error type.
//
// The core never includes Python.h. It talks to Python only through function
// pointer hooks (see set_pyobject_hooks) so that it can be unit-tested from C++.
#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>

#include <CGAL/Exact_rational.h>   // boost::multiprecision::mpq_rational in this build

namespace arr2d {

// ---------------------------------------------------------------------------
// Kinds
// ---------------------------------------------------------------------------

/// Geometry kind = one CGAL traits instantiation (see docs/dev/DESIGN.md).
enum class Kind : int {
  Segment = 0,        ///< Arr_segment_traits_2<Epeck>                       (bounded planar)
  Linear = 1,         ///< Arr_linear_traits_2<Epeck>: segments, rays, lines  (unbounded planar)
  CircleSegment = 2,  ///< Arr_circle_segment_traits_2<Epeck>: arcs + segs    (bounded planar)
  Polyline = 3,       ///< Arr_polyline_traits_2<Arr_segment_traits_2<Epeck>> (bounded planar)
  Bezier = 4,         ///< Arr_Bezier_curve_traits_2<CORE>                    (bounded planar)
  Conic = 5,          ///< Arr_conic_traits_2<CORE>                           (bounded planar)
  Sphere = 6,         ///< Arr_geodesic_arc_on_sphere_traits_2<Epeck>         (sphere)
  NumKinds = 7
};

inline const char* kind_name(Kind k) {
  switch (k) {
    case Kind::Segment: return "segment";
    case Kind::Linear: return "linear";
    case Kind::CircleSegment: return "circle_segment";
    case Kind::Polyline: return "polyline";
    case Kind::Bezier: return "bezier";
    case Kind::Conic: return "conic";
    case Kind::Sphere: return "sphere";
    default: return "?";
  }
}

/// -1 if unknown.
int kind_from_name(const std::string& name);

// ---------------------------------------------------------------------------
// Numbers
// ---------------------------------------------------------------------------

/// The one exact rational type used at the core boundary. In this CGAL build it is
/// boost::multiprecision::mpq_rational, which is also CORE::BigRat and the exact
/// number type behind Epeck::FT, so conversions between kinds are lossless.
using Rational = CGAL::Exact_rational;

// ---------------------------------------------------------------------------
// Errors
// ---------------------------------------------------------------------------

enum class ErrorCode : int {
  Generic = 0,
  KindMismatch = 1,      ///< geometry of kind A given to an object of kind B
  InvalidHandle = 2,     ///< handle refers to a deleted / foreign element
  NotXMonotone = 3,      ///< an x-monotone curve was required
  NotRepresentable = 4,  ///< e.g. algebraic coordinate requested as a rational
  Unsupported = 5,       ///< feature not available for this kind
  InvalidArgument = 6,   ///< bad argument (wrong type of Geom, empty range, ...)
  CallbackFailed = 7,    ///< a Python callback raised (Cython records the exception)
};

/// Thrown by the core for API misuse. CGAL's own failures (CGAL::Failure_exception and
/// subclasses) propagate unchanged and are translated by Cython.
struct Error : std::runtime_error {
  ErrorCode code;
  Error(ErrorCode c, const std::string& msg) : std::runtime_error(msg), code(c) {}
};

[[noreturn]] inline void throw_error(ErrorCode c, const std::string& msg) { throw Error(c, msg); }

// ---------------------------------------------------------------------------
// Python object holder
// ---------------------------------------------------------------------------

/// Function-pointer hooks used to manage the lifetime of Python objects stored in DCEL
/// element data. Installed once by the Cython module at import time.
using PyObjectHook = void (*)(void*);
void set_pyobject_hooks(PyObjectHook incref, PyObjectHook decref);
void pyobject_incref(void* obj);  ///< no-op when obj == nullptr or hooks not installed
void pyobject_decref(void* obj);

/// Owning reference to an opaque Python object (nullptr == Python None / no data).
/// Copying increfs, destruction decrefs (through the hooks).
struct PyRef {
  void* obj = nullptr;

  PyRef() = default;
  explicit PyRef(void* o, bool steal = false) : obj(o) { if (!steal) pyobject_incref(obj); }
  PyRef(const PyRef& o) : obj(o.obj) { pyobject_incref(obj); }
  PyRef(PyRef&& o) noexcept : obj(o.obj) { o.obj = nullptr; }
  PyRef& operator=(const PyRef& o) {
    if (this != &o) { pyobject_incref(o.obj); pyobject_decref(obj); obj = o.obj; }
    return *this;
  }
  PyRef& operator=(PyRef&& o) noexcept {
    if (this != &o) { pyobject_decref(obj); obj = o.obj; o.obj = nullptr; }
    return *this;
  }
  ~PyRef() { pyobject_decref(obj); obj = nullptr; }

  /// Replace the held object (increfs the new one, decrefs the old one).
  void set(void* o) { pyobject_incref(o); pyobject_decref(obj); obj = o; }
  void* get() const { return obj; }
  bool empty() const { return obj == nullptr; }
};

// ---------------------------------------------------------------------------
// Type-erased geometry box
// ---------------------------------------------------------------------------

enum class GeomType : int {
  Point = 0,   ///< Traits::Point_2 of the kind
  Curve = 1,   ///< Traits::Curve_2 (general, possibly not x-monotone)
  XCurve = 2,  ///< Traits::X_monotone_curve_2
  Number = 3,  ///< an exact number (see NumberKind in numbers.hpp)
};

/// Immutable, shared, type-erased holder of one concrete CGAL object.
/// The concrete C++ type is determined by (kind, type); `as<T>()` checks it via typeid.
struct Geom {
  Kind kind = Kind::Segment;
  GeomType type = GeomType::Point;
  std::shared_ptr<const void> ptr;
  const std::type_info* tinfo = nullptr;

  bool empty() const { return !ptr; }

  template <class T>
  const T& as() const {
    if (!ptr) throw_error(ErrorCode::InvalidArgument, "empty geometry");
    if (tinfo && *tinfo != typeid(T))
      throw_error(ErrorCode::KindMismatch,
                  std::string("geometry type mismatch: holds ") + tinfo->name() + ", wanted " + typeid(T).name());
    return *static_cast<const T*>(ptr.get());
  }

  template <class T>
  bool holds() const { return ptr && tinfo && *tinfo == typeid(T); }
};

/// Box a copy of `value`.
template <class T>
Geom make_geom(Kind kind, GeomType type, T value) {
  Geom g;
  g.kind = kind;
  g.type = type;
  g.tinfo = &typeid(T);
  g.ptr = std::shared_ptr<const void>(new T(std::move(value)),
                                      [](const void* p) { delete static_cast<const T*>(p); });
  return g;
}

/// Check helpers (throw Error on mismatch).
inline void require_kind(const Geom& g, Kind k, const char* what = "geometry") {
  if (g.kind != k)
    throw_error(ErrorCode::KindMismatch, std::string(what) + " has kind '" + kind_name(g.kind) +
                                             "' but kind '" + kind_name(k) + "' is required");
}
inline void require_type(const Geom& g, GeomType t, const char* what = "geometry") {
  if (g.type != t) {
    static const char* names[] = {"point", "curve", "x-monotone curve", "number"};
    throw_error(t == GeomType::XCurve && g.type == GeomType::Curve ? ErrorCode::NotXMonotone
                                                                    : ErrorCode::InvalidArgument,
                std::string(what) + " is a " + names[int(g.type)] + " but a " + names[int(t)] + " is required");
  }
}
inline void require_point(const Geom& g, Kind k, const char* what = "point") { require_kind(g, k, what); require_type(g, GeomType::Point, what); }
inline void require_xcurve(const Geom& g, Kind k, const char* what = "curve") { require_kind(g, k, what); require_type(g, GeomType::XCurve, what); }
/// Accept Curve or XCurve.
inline void require_any_curve(const Geom& g, Kind k, const char* what = "curve") {
  require_kind(g, k, what);
  if (g.type != GeomType::Curve && g.type != GeomType::XCurve)
    throw_error(ErrorCode::InvalidArgument, std::string(what) + " must be a curve");
}

// ---------------------------------------------------------------------------
// Opaque handles
// ---------------------------------------------------------------------------
//
// A handle is (raw pointer to the DCEL element, unique id). The id is stored inside the
// element's extended-DCEL data and lets the arrangement detect stale handles (element
// deleted, or memory reused by a new element) — see ArrBase::vertex_valid & co.

struct VH { void* p = nullptr; std::uint64_t id = 0; };   ///< vertex
struct HH { void* p = nullptr; std::uint64_t id = 0; };   ///< halfedge
struct FH { void* p = nullptr; std::uint64_t id = 0; };   ///< face
struct CH { void* p = nullptr; std::uint64_t id = 0; };   ///< input curve (history)

inline bool operator==(const VH& a, const VH& b) { return a.p == b.p && a.id == b.id; }
inline bool operator==(const HH& a, const HH& b) { return a.p == b.p && a.id == b.id; }
inline bool operator==(const FH& a, const FH& b) { return a.p == b.p && a.id == b.id; }
inline bool operator==(const CH& a, const CH& b) { return a.p == b.p && a.id == b.id; }
inline bool operator!=(const VH& a, const VH& b) { return !(a == b); }
inline bool operator!=(const HH& a, const HH& b) { return !(a == b); }
inline bool operator!=(const FH& a, const FH& b) { return !(a == b); }
inline bool operator!=(const CH& a, const CH& b) { return !(a == b); }

/// Result of point location / ray shooting / zone: one of vertex, halfedge, face, or none.
struct Located {
  int type = -1;          ///< 0 = vertex, 1 = halfedge, 2 = face, -1 = none (e.g. ray shoot to infinity)
  void* p = nullptr;
  std::uint64_t id = 0;

  static Located none() { return Located{}; }
  static Located vertex(VH v) { Located l; l.type = 0; l.p = v.p; l.id = v.id; return l; }
  static Located halfedge(HH h) { Located l; l.type = 1; l.p = h.p; l.id = h.id; return l; }
  static Located face(FH f) { Located l; l.type = 2; l.p = f.p; l.id = f.id; return l; }
  VH as_vertex() const { return VH{p, id}; }
  HH as_halfedge() const { return HH{p, id}; }
  FH as_face() const { return FH{p, id}; }
};

// ---------------------------------------------------------------------------
// Enumerations mirrored from CGAL (values match CGAL's enums)
// ---------------------------------------------------------------------------

/// CGAL::Arr_halfedge_direction
enum HalfedgeDirection : int { ARR_LEFT_TO_RIGHT = 0, ARR_RIGHT_TO_LEFT = 1 };
/// CGAL::Arr_parameter_space
enum ParameterSpace : int { ARR_LEFT_BOUNDARY = 0, ARR_RIGHT_BOUNDARY = 1, ARR_BOTTOM_BOUNDARY = 2, ARR_TOP_BOUNDARY = 3, ARR_INTERIOR = 4 };
/// CGAL::Arr_curve_end
enum CurveEnd : int { ARR_MIN_END = 0, ARR_MAX_END = 1 };
/// CGAL::Comparison_result / Sign / Orientation: -1, 0, +1
/// CGAL::Orientation: CLOCKWISE = -1, COLLINEAR = 0, COUNTERCLOCKWISE = 1

/// Point-location strategies (see ArrBase::locate).
enum PointLocationStrategy : int {
  PL_DEFAULT = -1,       ///< an attached strategy if any (trapezoid > landmarks > walk), else walk
  PL_NAIVE = 0,
  PL_SIMPLE = 1,
  PL_WALK = 2,
  PL_LANDMARKS = 3,
  PL_TRAPEZOID = 4,      ///< Arr_trapezoid_ric_point_location (must be attached; observer-updated)
  PL_TRIANGULATION = 5,  ///< Arr_triangulation_point_location (segment-like kinds only)
  PL_NUM_STRATEGIES = 6
};
const char* point_location_name(int strategy);
int point_location_from_name(const std::string& name);  ///< -2 if unknown

}  // namespace arr2d
