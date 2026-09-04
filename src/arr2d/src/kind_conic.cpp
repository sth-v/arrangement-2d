// =============================================================================================
// arr2d — Kind::Conic
//
//   Traits: CGAL::Arr_conic_traits_2<Cartesian<CORE::BigRat>, Cartesian<CORE::Expr>,
//                                    CGAL::CORE_algebraic_number_traits>
//   Point_2            = CGAL::Conic_point_2<Alg_kernel>          (coordinates are CORE::Expr)
//   Curve_2            = CGAL::Conic_arc_2<...>                   (bounded arc, or a full ellipse)
//   X_monotone_curve_2 = CGAL::Conic_x_monotone_arc_2<Curve_2>
//
// This TU provides
//   (1) ConicTypes::traits()                — the ONE process-wide traits instance,
//   (2) class ConicOps : KindOpsBase<ConicTypes>,
//   (3) every free function of namespace arr2d::conic declared in ops.hpp,
//   (4) template class arr2d::ArrImpl<arr2d::ConicTypes>,
//   (5) the static registrar that publishes the kind in the registry.
//
// The CGAL 6.1 traps this file handles (docs/dev/CGAL_TRAPS_CHECKLIST.md + docs/dev/cgal61_api/
// traits_conic.md) are named at each work-around; the index is:
//
//   * CORE MemoryPool  — nothing that owns a CORE::Expr may be destroyed during static teardown
//     (`! blocks.empty()`, CGAL/CORE/MemoryPool.h:125).  ConicTypes::traits() and the ConicOps
//     singleton are heap-allocated and deliberately leaked.               [traits_conic.md 13.11]
//   * Construct_curve_2 silently negates/integerises the coefficients     [traits_conic.md 2]
//     -> conic::coefficients() reports what CGAL STORED, never the input.
//   * Invalid arcs are returned silently, not thrown                      [traits_conic.md 3]
//     -> every construction path ends with an explicit `is_valid()` test.
//   * Hyperbolic supporting conics hit the sin/cos swap in                [traits_conic.md 4, 13]
//     build_hyperbolic_arc_data() -> the exact rational predicate of 13.4 gates every
//     constructor (arr2d::conic::detail::hyperbolic_axis_is_sound).
//   * Conic_arc_2::bbox() is UB for full conics                          [traits_conic.md 13]
//     -> Construct_bbox_2 (+ the exact endpoints, + outward widening) instead.
//   * Approximate_curve_length_2 is unreachable/uncompilable             [traits_conic.md 5]
//     -> KindOpsBase::approximate_length sums the chords of approximate().
//   * Conic_x_monotone_arc_2::merge()/2-point ctor do not compile        [traits_conic.md 6]
//     -> only traits functors are used (KindOpsBase already does this).
//   * Approximate_2 needs a back_insert_iterator and error > 0           [rendering_and_approximation.md 6,7]
//     -> the tolerance is validated/clamped BEFORE the call, output through std::back_inserter.
//   * Parameter_space_in_x/y_2(xcv, ce) is a CGAL_error stub             [traits_conic.md 14]
//     -> KindOpsBase routes those through Arr_traits_adaptor_2 (oblivious sides -> ARR_INTERIOR).
//   * CORE::Expr has no safe rationality test                           [exact_coordinates_contract.md 1]
//     -> point_is_rational() is always false and point_exact_rational() always throws.
// =============================================================================================

#include "arr2d/kinds/conic_types.hpp"

// Foreign kinds we can convert FROM.  Only the geometry *types* are needed (their `traits()`
// functions live in their own TUs and are never called from here), so a conic-only build links.
#include "arr2d/kinds/segment_types.hpp"
#include "arr2d/kinds/linear_types.hpp"
#include "arr2d/kinds/polyline_types.hpp"
#include "arr2d/kinds/circle_segment_types.hpp"
#include "arr2d/kinds/bezier_types.hpp"

#include "arr2d/bso.hpp"
#include "arr2d/impl/arr_impl.hpp"
#include "arr2d/impl/kind_ops_base.hpp"
#include "arr2d/impl/number_conv.hpp"
#include "arr2d/numbers.hpp"
#include "arr2d/ops.hpp"
#include "arr2d/registry.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace arr2d {

// ---------------------------------------------------------------------------------------------
// (1) The process-wide traits object.
//
// LIFETIME: one instance per process, created on first use and NEVER destroyed.  It must outlive
// every Point_2 / Curve_2 / X_monotone_curve_2 / Arrangement of this kind, because
//   * `Arrangement_2(&traits)` stores a bare pointer to it (traits_conic.md 1.2), and
//   * every functor returned by `*_object()` holds `const Traits&`.
// Being a function-local static it satisfies that trivially.  It is heap-allocated and leaked on
// purpose: the traits owns a `mutable Intersection_map` full of `CORE::Expr` values, and freeing a
// CORE object after CORE's own static MemoryPool has been torn down aborts the process with
// `CGAL error: assertion violation! ! blocks.empty()` (CGAL/CORE/MemoryPool.h:125)
// — traits_conic.md 13.11 and the STAGE1 note on kind_ops_base.hpp.
// ---------------------------------------------------------------------------------------------
ConicTypes::Traits& ConicTypes::traits() {
  static ConicTypes::Traits* t = new ConicTypes::Traits();  // intentionally never deleted
  return *t;
}

// =============================================================================================
// File-local helpers
// =============================================================================================
namespace {

using CT = ConicTypes;
using CTraits = CT::Traits;
using CPoint = CT::Point_2;                 // Conic_point_2<Alg_kernel>
using CCurve = CT::Curve_2;                 // Conic_arc_2
using CXcv = CT::X_monotone_curve_2;        // Conic_x_monotone_arc_2
using CRatPoint = CT::Rat_point_2;          // Cartesian<Rational>::Point_2
using CRatSeg = CT::Rat_segment_2;
using CRatCircle = CT::Rat_circle_2;
using CApproxPoint = CTraits::Approximate_point_2;   // Cartesian<double>::Point_2

// Foreign point/curve types (all fully qualified through the Types structs).
using EpeckPoint = SegmentTypes::Point_2;            // == Linear/Polyline Point_2 as well
using SegCurve = SegmentTypes::Curve_2;              // Arr_segment_2<Epeck>
using LinCurve = LinearTypes::Curve_2;               // Arr_linear_object_2<Epeck>
using PolyCurve = PolylineTypes::Curve_2;
using PolyXcv = PolylineTypes::X_monotone_curve_2;
using CsPoint = CircleSegmentTypes::Point_2;         // _One_root_point_2<Epeck::FT, true>
using CsCurve = CircleSegmentTypes::Curve_2;         // _Circle_segment_2
using CsXcv = CircleSegmentTypes::X_monotone_curve_2;
using CsCoordNT = CsPoint::CoordNT;                  // Sqrt_extension<Epeck::FT, ...>
using BzCurve = BezierTypes::Curve_2;                // _Bezier_curve_2
using BzXcv = BezierTypes::X_monotone_curve_2;
using BzPoint = BezierTypes::Point_2;                // _Bezier_point_2

inline CTraits& tr() { return ConicTypes::traits(); }

[[noreturn]] void err(ErrorCode code, const std::string& msg) {
  throw_error(code, "conic: " + msg);
}

// ---- small numeric helpers -------------------------------------------------------------------

inline Algebraic alg(const Rational& r) { return to_core_expr(r); }

inline int sign_of_rat(const Rational& r) { return static_cast<int>(CGAL::sign(r)); }

inline CGAL::Orientation orientation_of_int(int o) {
  if (o > 0) return CGAL::COUNTERCLOCKWISE;
  if (o < 0) return CGAL::CLOCKWISE;
  return CGAL::COLLINEAR;
}

/// Shortest decimal representation of `v` that round-trips (used by the reprs only).
std::string fmt_double(double v) {
  if (std::isnan(v)) return "nan";
  if (std::isinf(v)) return v > 0 ? "inf" : "-inf";
  char buf[64];
  for (int prec = 6; prec <= 17; ++prec) {
    std::snprintf(buf, sizeof(buf), "%.*g", prec, v);
    if (std::strtod(buf, nullptr) == v) return std::string(buf);
  }
  std::snprintf(buf, sizeof(buf), "%.17g", v);
  return std::string(buf);
}

std::string fmt_rational(const Rational& r) {
  std::string num, den;
  rational_to_strings(r, num, den);
  if (den == "1") return num;
  return num + "/" + den;
}

const char* orientation_name(CGAL::Orientation o) {
  if (o == CGAL::COUNTERCLOCKWISE) return "ccw";
  if (o == CGAL::CLOCKWISE) return "cw";
  return "collinear";
}

// ---- exact rational orientation predicate (used before handing points to CGAL) ---------------
int rat_orientation(const Rational& ax, const Rational& ay, const Rational& bx, const Rational& by,
                    const Rational& cx, const Rational& cy) {
  return sign_of_rat((bx - ax) * (cy - ay) - (by - ay) * (cx - ax));
}

// ---- exact 5x5 determinant over Rational (fraction-carrying Gaussian elimination) -------------
Rational det5(Rational m[5][5]) {
  Rational det(1);
  for (int col = 0; col < 5; ++col) {
    int piv = -1;
    for (int row = col; row < 5; ++row) {
      if (CGAL::sign(m[row][col]) != CGAL::ZERO) { piv = row; break; }
    }
    if (piv < 0) return Rational(0);
    if (piv != col) {
      for (int k = 0; k < 5; ++k) std::swap(m[col][k], m[piv][k]);
      det = -det;
    }
    det *= m[col][col];
    for (int row = col + 1; row < 5; ++row) {
      if (CGAL::sign(m[row][col]) == CGAL::ZERO) continue;
      const Rational f = m[row][col] / m[col][col];
      for (int k = col; k < 5; ++k) m[row][k] -= f * m[col][k];
    }
  }
  return det;
}

/// Coefficients (r,s,t,u,v,w) of the (unique, up to a scalar) conic through five rational points,
/// as the null vector of the 5x6 matrix whose rows are (x^2, y^2, xy, x, y, 1).
/// Returns false when the five points do not determine a conic (rank < 5).
bool conic_through_five_points(const Rational px[5], const Rational py[5], Rational out[6]) {
  Rational rows[5][6];
  for (int i = 0; i < 5; ++i) {
    rows[i][0] = px[i] * px[i];
    rows[i][1] = py[i] * py[i];
    rows[i][2] = px[i] * py[i];
    rows[i][3] = px[i];
    rows[i][4] = py[i];
    rows[i][5] = Rational(1);
  }
  bool nonzero = false;
  for (int j = 0; j < 6; ++j) {
    Rational minor[5][5];
    for (int i = 0; i < 5; ++i) {
      int c = 0;
      for (int k = 0; k < 6; ++k) {
        if (k == j) continue;
        minor[i][c++] = rows[i][k];
      }
    }
    Rational d = det5(minor);
    out[j] = ((j % 2) == 0) ? d : -d;
    if (CGAL::sign(out[j]) != CGAL::ZERO) nonzero = true;
  }
  return nonzero;
}

// =============================================================================================
// The CGAL 6.1 hyperbolic-axis bug gate  (traits_conic.md 13.3 / 13.4, CGAL_TRAPS_CHECKLIST)
// =============================================================================================

std::atomic<bool>& allow_hyperbolic_flag() {
  static std::atomic<bool> flag{false};
  return flag;
}

}  // namespace

namespace conic {
namespace detail {

/// Exact rational predicate of traits_conic.md 13.4.
///
///   true  -> the `Extra_data` line CGAL will build in build_hyperbolic_arc_data() really
///            separates the two branches of the hyperbola: Construct_curve_2 is safe.
///   false -> CGAL will store a *chord*: a CGAL assertion in an assertions-on build, and silently
///            wrong point containment / intersections / x-monotone splits otherwise.  Also false
///            for a degenerate hyperbola (N == 0, i.e. a line pair).
///
/// Non-hyperbolic supporting conics (4rs - t^2 >= 0) are always safe.  The answer depends only on
/// the supporting conic — not on the endpoints, the requested orientation, or any positive
/// rescaling / global negation of the coefficients (13.3, step 0).
bool hyperbolic_axis_is_sound(const Rational& r, const Rational& s, const Rational& t,
                              const Rational& u, const Rational& v, const Rational& w) {
  const Rational det = 4 * r * s - t * t;
  if (CGAL::sign(det) != CGAL::NEGATIVE) return true;   // not a hyperbola: not our problem
  const Rational N = det * w - u * u * s - v * v * r + u * v * t;   // == ConicCPA2 z_prime
  const int sN = static_cast<int>(CGAL::sign(N));
  if (sN == 0) return false;                            // degenerate hyperbola = line pair
  const Rational R = sN * r, S = sN * s, T = sN * t;
  if (CGAL::sign(T) == CGAL::ZERO) return true;         // the `sign_t == ZERO` branch is correct
  const Rational P = R + S, A = (R - S) * (R - S), B = T * T, E = B - A;
  const int sP = static_cast<int>(CGAL::sign(P));
  const int sE = static_cast<int>(CGAL::sign(E));
  const int q = (sP * sE <= 0) ? (sP != 0 ? sP : -sE)
                               : sP * static_cast<int>(CGAL::sign(P * P * (A + B) - E * E));
  return q <= 0;
}

/// `N` of 13.3 (== 4*det of the conic's 3x3 matrix).  N == 0 <=> the conic is degenerate
/// (a line pair, a double line, a single point or empty).
Rational conic_discriminant_N(const Rational& r, const Rational& s, const Rational& t,
                              const Rational& u, const Rational& v, const Rational& w) {
  return (4 * r * s - t * t) * w - u * u * s - v * v * r + u * v * t;
}

}  // namespace detail
}  // namespace conic

namespace {

/// Refuse the supporting conics that CGAL 6.1 cannot handle, unless the user opted in.
/// `orient` is the orientation that will be handed to Construct_curve_2: for CGAL::COLLINEAR the
/// traits takes the "line pair segment" branch of Traits::set() and never reaches
/// build_hyperbolic_arc_data(), so no gate is needed there (Arr_conic_traits_2.h:3092-3127).
void gate_supporting_conic(const Rational& r, const Rational& s, const Rational& t,
                           const Rational& u, const Rational& v, const Rational& w,
                           CGAL::Orientation orient) {
  if (orient == CGAL::COLLINEAR) return;
  if (CGAL::sign(4 * r * s - t * t) != CGAL::NEGATIVE) return;   // ellipse / parabola: fine
  const Rational N = conic::detail::conic_discriminant_N(r, s, t, u, v, w);
  if (CGAL::sign(N) == CGAL::ZERO)
    err(ErrorCode::InvalidArgument,
        "the supporting conic is a degenerate hyperbola (a pair of lines); build the two segments "
        "separately, or pass orientation=0 to use CGAL's line-pair segment representation");
  if (!allow_hyperbolic_flag().load() &&
      !conic::detail::hyperbolic_axis_is_sound(r, s, t, u, v, w))
    err(ErrorCode::Unsupported,
        "this hyperbolic conic arc triggers a CGAL 6.1 bug (build_hyperbolic_arc_data); "
        "call conic_allow_hyperbolic(True) to override at your own risk");
}

/// Construct an arc through Construct_curve_2 and enforce the two things CGAL does NOT:
/// the hyperbolic gate (before) and validity (after, traits_conic.md 3).
CCurve build_arc(const Rational& r, const Rational& s, const Rational& t, const Rational& u,
                 const Rational& v, const Rational& w, CGAL::Orientation orient,
                 const CPoint& source, const CPoint& target) {
  // CGAL_precondition of the overload: compare_xy(source, target) != EQUAL.  Checked here so that
  // misuse is Error(InvalidArgument) rather than a CGAL::Precondition_exception.
  if (tr().equal_2_object()(source, target))
    err(ErrorCode::InvalidArgument, "the source and the target of an arc must be distinct");
  gate_supporting_conic(r, s, t, u, v, w, orient);
  CCurve c = tr().construct_curve_2_object()(r, s, t, u, v, w, orient, source, target);
  if (!c.is_valid())
    err(ErrorCode::InvalidArgument,
        "invalid conic arc: the endpoints are not both on the conic "
        "r x^2 + s y^2 + t xy + u x + v y + w = 0, the arc is unbounded, or the conic is degenerate");
  return c;
}

/// Full-conic construction (ellipses only).  Enforces the CGAL precondition 4rs - t^2 > 0 and the
/// two extra conditions CGAL only `CGAL_assertion`s / ignores: the conic must be non-degenerate
/// (N != 0) and must have real points (N*(r+s) < 0 for an ellipse).
CCurve build_full(const Rational& r, const Rational& s, const Rational& t, const Rational& u,
                  const Rational& v, const Rational& w) {
  if (CGAL::sign(4 * r * s - t * t) != CGAL::POSITIVE)
    err(ErrorCode::InvalidArgument,
        "a full conic must be an ellipse or a circle (4rs - t^2 > 0); got 4rs - t^2 = " +
            fmt_rational(4 * r * s - t * t));
  const Rational N = conic::detail::conic_discriminant_N(r, s, t, u, v, w);
  if (CGAL::sign(N) == CGAL::ZERO)
    err(ErrorCode::InvalidArgument, "degenerate conic (it is a single point, not an ellipse)");
  if (CGAL::sign(N * (r + s)) != CGAL::NEGATIVE)
    err(ErrorCode::InvalidArgument,
        "the conic is an imaginary ellipse: it has no real points");
  CCurve c = tr().construct_curve_2_object()(r, s, t, u, v, w);
  if (!c.is_valid()) err(ErrorCode::InvalidArgument, "CGAL rejected the full conic as invalid");
  return c;
}

/// The six coefficients of a full ellipse/circle, with the sign chosen so that CGAL's
/// `set_full(..., comp_orient=true)` computes exactly the requested orientation.
///
/// `ConicCPA2::analyse()` gives `orientation() == NEGATIVE (== CLOCKWISE)` iff `r > 0` for a real
/// ellipse (traits_conic.md 2), so a global negation flips the orientation.  [verified by probe:
/// circle (1,1,0,-2,-4,1) -> orientation -1; the negated set -> +1]
void orient_full_coefficients(int orientation, Rational& r, Rational& s, Rational& t, Rational& u,
                              Rational& v, Rational& w) {
  if (orientation == 0)
    err(ErrorCode::InvalidArgument, "orientation must be +1 (counterclockwise) or -1 (clockwise)");
  // The natural orientation of the (r>0) form is CLOCKWISE.
  const bool natural_is_ccw = (CGAL::sign(r) == CGAL::NEGATIVE);
  const bool want_ccw = orientation > 0;
  if (natural_is_ccw != want_ccw) { r = -r; s = -s; t = -t; u = -u; v = -v; w = -w; }
}

// ---- exact conversions from the foreign kinds ------------------------------------------------

/// Circle-segment coordinate (a0 + a1*sqrt(root), all Epeck::FT) -> an exact CORE::Expr.
Algebraic alg_of_coord(const CsCoordNT& c) {
  const Rational a0 = to_rational(c.a0());
  if (!c.is_extended()) return alg(a0);
  SqrtExt s{a0, to_rational(c.a1()), to_rational(c.root())};
  Rational exact;
  if (sqrt_ext_is_rational(s, exact)) return alg(exact);   // perfect-square root, gotcha #3
  return to_core_expr(s);                                  // a0 + a1*sqrt(root), exactly
}

inline CPoint conic_point_of_rational(const Rational& x, const Rational& y) {
  return CPoint(alg(x), alg(y));
}

/// Exact rational coordinates of a point Geom of any kind, when they exist.
/// Returns false (without throwing) when the point has no provably-rational coordinates.
bool try_rational_coords(const Geom& g, Rational& x, Rational& y) {
  if (g.type != GeomType::Point) return false;
  switch (g.kind) {
    case Kind::Segment:
    case Kind::Linear:
    case Kind::Polyline: {
      const EpeckPoint& p = g.as<EpeckPoint>();
      x = to_rational(p.x());
      y = to_rational(p.y());
      return true;
    }
    case Kind::CircleSegment: {
      const CsPoint& p = g.as<CsPoint>();
      SqrtExt sx{to_rational(p.x().a0()), to_rational(p.x().a1()), to_rational(p.x().root())};
      SqrtExt sy{to_rational(p.y().a0()), to_rational(p.y().a1()), to_rational(p.y().root())};
      if (!p.x().is_extended()) { sx.b = 0; sx.c = 0; }
      if (!p.y().is_extended()) { sy.b = 0; sy.c = 0; }
      return sqrt_ext_is_rational(sx, x) && sqrt_ext_is_rational(sy, y);
    }
    case Kind::Bezier: {
      const BzPoint& p = g.as<BzPoint>();
      if (!p.is_rational()) return false;
      // _Bezier_point_2 converts to its Rat_point_2 exactly when is_rational().
      const BezierTypes::Rat_point_2 rp = static_cast<BezierTypes::Rat_point_2>(p);
      x = rp.x();
      y = rp.y();
      return true;
    }
    case Kind::Conic:
      // A Conic_point_2 stores CORE::Expr coordinates.  CORE offers no *sound* rationality test
      // (exact_coordinates_contract.md gotcha 1: Expr comparison can report a FALSE EQUAL against a
      // rational convergent, breaking transitivity), and Conic_point_2 cannot carry a "I was built
      // from rationals" flag.  We therefore never claim a conic point is rational.
      return false;
    case Kind::Sphere:
    default:
      return false;
  }
}

void require_rational_coord_pair(const Geom& g, const std::string& what, Rational& x,
                                 Rational& y) {
  if (g.type != GeomType::Point) err(ErrorCode::InvalidArgument, what + " must be a point");
  if (g.kind == Kind::Sphere)
    err(ErrorCode::KindMismatch,
        what + ": a sphere point is a 3-D direction and has no (x, y) coordinates");
  if (!try_rational_coords(g, x, y)) {
    if (g.kind == Kind::Conic)
      err(ErrorCode::NotRepresentable,
          what +
              " must have rational coordinates, but a conic point stores algebraic (CORE::Expr) "
              "coordinates that cannot be tested for rationality safely; pass a rational point of "
              "another kind (e.g. a segment-kind point) instead");
    err(ErrorCode::NotRepresentable,
        what + " must have rational coordinates (it has algebraic ones)");
  }
}

/// Convert a point of ANY kind into a Conic Point_2.  Exact for every rational-coordinate kind and
/// for circle-segment sqrt-extension coordinates (a + b*sqrt(c) -> Expr(a) + Expr(b)*sqrt(Expr(c))).
CPoint to_conic_point(const Geom& g) {
  require_type(g, GeomType::Point, "point");
  switch (g.kind) {
    case Kind::Conic:
      return g.as<CPoint>();
    case Kind::Segment:
    case Kind::Linear:
    case Kind::Polyline: {
      const EpeckPoint& p = g.as<EpeckPoint>();
      return conic_point_of_rational(to_rational(p.x()), to_rational(p.y()));
    }
    case Kind::CircleSegment: {
      const CsPoint& p = g.as<CsPoint>();
      return CPoint(alg_of_coord(p.x()), alg_of_coord(p.y()));
    }
    case Kind::Bezier: {
      const BzPoint& p = g.as<BzPoint>();
      if (!p.is_rational())
        err(ErrorCode::NotRepresentable,
            "cannot convert a Bezier point with non-rational coordinates: its exact algebraic "
            "coordinates are only reachable through the bezier kind's own cache "
            "(_Bezier_point_2::make_exact)");
      const BezierTypes::Rat_point_2 rp = static_cast<BezierTypes::Rat_point_2>(p);
      return conic_point_of_rational(rp.x(), rp.y());
    }
    case Kind::Sphere:
      err(ErrorCode::KindMismatch,
          "cannot convert a sphere point (a 3-D direction) into a planar conic point");
    default:
      err(ErrorCode::KindMismatch, "unknown point kind");
  }
}

// ---- segment helpers -------------------------------------------------------------------------

/// A conic arc that is a straight segment.  Uses the rational Rat_segment_2 overload when both
/// endpoints are rational (that yields the real supporting line in (u, v, w)); otherwise the
/// "special segment" overload, which stores algebraic endpoints and all-zero coefficients
/// (traits_conic.md 2.7/2.8).
CCurve build_segment(const Geom& p, const Geom& q) {
  Rational x1, y1, x2, y2;
  const bool p_rat = try_rational_coords(p, x1, y1);
  const bool q_rat = try_rational_coords(q, x2, y2);
  auto ctr = tr().construct_curve_2_object();
  if (p_rat && q_rat) {
    if (CGAL::sign(x1 - x2) == CGAL::ZERO && CGAL::sign(y1 - y2) == CGAL::ZERO)
      err(ErrorCode::InvalidArgument, "the two endpoints of a segment must be distinct");
    CCurve c = ctr(CRatSeg(CRatPoint(x1, y1), CRatPoint(x2, y2)));
    if (!c.is_valid()) err(ErrorCode::InvalidArgument, "CGAL rejected the segment as invalid");
    return c;
  }
  const CPoint a = to_conic_point(p);
  const CPoint b = to_conic_point(q);
  if (tr().equal_2_object()(a, b))
    err(ErrorCode::InvalidArgument, "the two endpoints of a segment must be distinct");
  CCurve c = ctr(a, b);   // special segment: r=s=t=u=v=w=0, orientation COLLINEAR
  if (!c.is_valid()) err(ErrorCode::InvalidArgument, "CGAL rejected the segment as invalid");
  return c;
}

CCurve build_segment_rat(const Rational& x1, const Rational& y1, const Rational& x2,
                         const Rational& y2) {
  if (CGAL::sign(x1 - x2) == CGAL::ZERO && CGAL::sign(y1 - y2) == CGAL::ZERO)
    err(ErrorCode::InvalidArgument, "the two endpoints of a segment must be distinct");
  CCurve c = tr().construct_curve_2_object()(CRatSeg(CRatPoint(x1, y1), CRatPoint(x2, y2)));
  if (!c.is_valid()) err(ErrorCode::InvalidArgument, "CGAL rejected the segment as invalid");
  return c;
}

// ---- rational quadratic Bezier -> conic arc (traits_conic.md 12.1) ---------------------------

/// Implicitise the rational quadratic Bezier with control points P0,P1,P2 and weights w0,w1,w2:
///   F = K*L_B^2 - M*L_A*L_C,  K = w0*w2, M = 4*w1^2,
/// where L_A, L_B, L_C are twice the signed sub-triangle areas.  All coefficients rational.
void rational_bezier_coefficients(const Rational& x0, const Rational& y0, const Rational& x1,
                                  const Rational& y1, const Rational& x2, const Rational& y2,
                                  const Rational& w0, const Rational& w1, const Rational& w2,
                                  Rational out[6]) {
  const Rational a1 = y1 - y2, a2 = x2 - x1, a3 = x1 * y2 - x2 * y1;   // line P1P2
  const Rational b1 = y2 - y0, b2 = x0 - x2, b3 = x2 * y0 - x0 * y2;   // line P0P2
  const Rational c1 = y0 - y1, c2 = x1 - x0, c3 = x0 * y1 - x1 * y0;   // line P0P1
  const Rational K = w0 * w2;
  const Rational M = 4 * w1 * w1;
  out[0] = K * b1 * b1 - M * a1 * c1;
  out[1] = K * b2 * b2 - M * a2 * c2;
  out[2] = 2 * K * b1 * b2 - M * (a1 * c2 + a2 * c1);
  out[3] = 2 * K * b1 * b3 - M * (a1 * c3 + a3 * c1);
  out[4] = 2 * K * b2 * b3 - M * (a2 * c3 + a3 * c2);
  out[5] = K * b3 * b3 - M * a3 * c3;
}

CCurve build_rational_bezier(const Rational& x0, const Rational& y0, const Rational& x1,
                             const Rational& y1, const Rational& x2, const Rational& y2,
                             const Rational& w0, const Rational& w1, const Rational& w2) {
  if (CGAL::sign(w0) != CGAL::POSITIVE || CGAL::sign(w1) != CGAL::POSITIVE ||
      CGAL::sign(w2) != CGAL::POSITIVE)
    err(ErrorCode::InvalidArgument,
        "the three weights of a rational quadratic Bezier must be positive");
  if (CGAL::sign(x0 - x2) == CGAL::ZERO && CGAL::sign(y0 - y2) == CGAL::ZERO)
    err(ErrorCode::InvalidArgument,
        "a rational quadratic Bezier needs distinct endpoints P0 != P2");
  const int turn = rat_orientation(x0, y0, x1, y1, x2, y2);
  if (turn == 0) {
    // Collinear control points: the "conic" degenerates into a line pair; the curve is the
    // straight segment P0 -> P2 (traits_conic.md 12.1, degenerate case).
    return build_segment_rat(x0, y0, x2, y2);
  }
  Rational c[6];
  rational_bezier_coefficients(x0, y0, x1, y1, x2, y2, w0, w1, w2, c);
  const CGAL::Orientation orient = (turn > 0) ? CGAL::COUNTERCLOCKWISE : CGAL::CLOCKWISE;
  return build_arc(c[0], c[1], c[2], c[3], c[4], c[5], orient,
                   conic_point_of_rational(x0, y0), conic_point_of_rational(x2, y2));
}

// ---- circle-segment -> conic -----------------------------------------------------------------

/// Full circle.  NOT the Rat_circle_2 overload of Construct_curve_2: that one takes no orientation
/// and forces CLOCKWISE (`set_full(..., comp_orient=false)`, traits_conic.md 2.9).  The six
/// coefficients of the circle, negated when the caller asks for COUNTERCLOCKWISE, give the
/// requested orientation through `set_full(..., comp_orient=true)`.
CCurve build_circle_full(const Rational& cx, const Rational& cy, const Rational& r2,
                         int orientation) {
  if (CGAL::sign(r2) != CGAL::POSITIVE)
    err(ErrorCode::InvalidArgument, "the squared radius of a circle must be positive");
  Rational r(1), s(1), t(0), u(-2 * cx), v(-2 * cy), w(cx * cx + cy * cy - r2);
  orient_full_coefficients(orientation, r, s, t, u, v, w);
  CCurve c = build_full(r, s, t, u, v, w);
  if (static_cast<int>(c.orientation()) != (orientation > 0 ? 1 : -1))
    err(ErrorCode::Generic, "internal error: CGAL computed an unexpected circle orientation");
  return c;
}

CCurve build_circle_arc(const Rational& cx, const Rational& cy, const Rational& r2, int orientation,
                        const CPoint& source, const CPoint& target) {
  if (CGAL::sign(r2) != CGAL::POSITIVE)
    err(ErrorCode::InvalidArgument, "the squared radius of a circle must be positive");
  // CGAL_precondition of the Rat_circle_2 overload: orient != COLLINEAR (checked here so misuse is
  // Error(InvalidArgument) rather than a CGAL::Precondition_exception).
  if (orientation == 0)
    err(ErrorCode::InvalidArgument,
        "a circular arc needs orientation +1 (counterclockwise) or -1 (clockwise)");
  // CGAL_precondition: source != target.
  if (tr().equal_2_object()(source, target))
    err(ErrorCode::InvalidArgument, "the source and the target of an arc must be distinct");
  // Route (a) of traits_conic.md 12.2: the Rat_circle_2 + orientation overload.  It pre-negates
  // the coefficients itself; the supporting conic is a circle (4rs - t^2 = 4 > 0), so the
  // hyperbolic branch of Traits::set() is never reached and no gate is needed.
  // The squared radius must be rational -- it is, by the signature.
  CCurve c = tr().construct_curve_2_object()(CRatCircle(CRatPoint(cx, cy), r2),
                                             orientation_of_int(orientation), source, target);
  // Endpoints that are not on the circle silently give an INVALID arc (traits_conic.md gotcha 3).
  if (!c.is_valid())
    err(ErrorCode::InvalidArgument,
        "invalid circular arc: the endpoints are not both on the circle "
        "(x - cx)^2 + (y - cy)^2 = squared_radius");
  return c;
}

/// The SqrtExt image of a circle-segment coordinate (a1/root are meaningless when !is_extended()).
SqrtExt sqrt_ext_of_coord(const CsCoordNT& c) {
  if (!c.is_extended()) return SqrtExt{to_rational(c.a0()), Rational(0), Rational(0)};
  return SqrtExt{to_rational(c.a0()), to_rational(c.a1()), to_rational(c.root())};
}

/// A circle-segment LINE segment (either curve type) -> a conic arc: the rational Rat_segment_2
/// route when all four coordinates are provably rational, otherwise the "special segment" overload
/// with exact algebraic endpoints (traits_conic.md 2.7/2.8).
CCurve build_cs_linear(const CsPoint& ps, const CsPoint& pt) {
  Rational x1, y1, x2, y2;
  if (sqrt_ext_is_rational(sqrt_ext_of_coord(ps.x()), x1) &&
      sqrt_ext_is_rational(sqrt_ext_of_coord(ps.y()), y1) &&
      sqrt_ext_is_rational(sqrt_ext_of_coord(pt.x()), x2) &&
      sqrt_ext_is_rational(sqrt_ext_of_coord(pt.y()), y2))
    return build_segment_rat(x1, y1, x2, y2);
  const CPoint a(alg_of_coord(ps.x()), alg_of_coord(ps.y()));
  const CPoint b(alg_of_coord(pt.x()), alg_of_coord(pt.y()));
  if (tr().equal_2_object()(a, b))
    err(ErrorCode::InvalidArgument, "the two endpoints of a segment must be distinct");
  CCurve out = tr().construct_curve_2_object()(a, b);
  if (!out.is_valid()) err(ErrorCode::InvalidArgument, "CGAL rejected the segment as invalid");
  return out;
}

/// A circle-segment circular arc (either curve type) -> a conic arc.  The circle's centre and
/// squared radius are rational by construction; the endpoints may carry sqrt-extension
/// coordinates, which convert exactly to CORE::Expr.  The orientation is preserved.
template <class Circle>
CCurve build_cs_circular(const Circle& circ, int orient, const CsPoint& ps, const CsPoint& pt) {
  return build_circle_arc(to_rational(circ.center().x()), to_rational(circ.center().y()),
                          to_rational(circ.squared_radius()), orient,
                          CPoint(alg_of_coord(ps.x()), alg_of_coord(ps.y())),
                          CPoint(alg_of_coord(pt.x()), alg_of_coord(pt.y())));
}

CCurve build_from_circle_segment_curve(const CsCurve& c) {
  if (c.is_full()) {
    const auto& circ = c.supporting_circle();
    return build_circle_full(to_rational(circ.center().x()), to_rational(circ.center().y()),
                             to_rational(circ.squared_radius()),
                             static_cast<int>(c.orientation()));
  }
  if (c.is_circular())
    return build_cs_circular(c.supporting_circle(), static_cast<int>(c.orientation()), c.source(),
                             c.target());
  return build_cs_linear(c.source(), c.target());
}

/// An x-monotone circle-segment arc is never a full circle, so only the two cases remain.
CCurve build_from_circle_segment_xcurve(const CsXcv& c) {
  if (c.is_circular())
    return build_cs_circular(c.supporting_circle(), static_cast<int>(c.orientation()), c.source(),
                             c.target());
  return build_cs_linear(c.source(), c.target());
}

// ---- x-monotone decomposition & chaining ------------------------------------------------------

void x_monotone_pieces(const CCurve& c, std::vector<CXcv>& out) {
  std::vector<std::variant<CPoint, CXcv>> res;
  tr().make_x_monotone_2_object()(c, std::back_inserter(res));
  for (const auto& item : res) {
    if (const CXcv* x = std::get_if<CXcv>(&item)) out.push_back(*x);
    // Make_x_monotone_2 of this traits never emits isolated points (traits_conic.md 7); if a
    // future CGAL does, the point simply contributes nothing to a polyline approximation.
  }
}

/// Order the x-monotone pieces of one Curve_2 head-to-tail (piece[i].target == piece[i+1].source),
/// so that concatenating their approximations traces the curve continuously.  Verified for a full
/// circle/ellipse: Make_x_monotone_2 gives 2 pieces that chain in both directions (a closed loop).
void chain_pieces(std::vector<CXcv>& pieces) {
  const std::size_t n = pieces.size();
  if (n < 2) return;
  auto eq = tr().equal_2_object();
  std::vector<char> used(n, 0);
  std::vector<CXcv> ordered;
  ordered.reserve(n);
  ordered.push_back(pieces[0]);
  used[0] = 1;
  for (std::size_t k = 1; k < n; ++k) {
    const CPoint& end = ordered.back().target();
    std::size_t found = n;
    for (std::size_t j = 0; j < n; ++j) {
      if (!used[j] && eq(pieces[j].source(), end)) { found = j; break; }
    }
    if (found == n) break;   // not a connected chain: keep CGAL's own order for the rest
    ordered.push_back(pieces[found]);
    used[found] = 1;
  }
  for (std::size_t j = 0; j < n; ++j)
    if (!used[j]) ordered.push_back(pieces[j]);
  pieces.swap(ordered);
}

}  // namespace

// =============================================================================================
// (2) ConicOps
// =============================================================================================
class ConicOps final : public KindOpsBase<ConicTypes> {
 public:
  ConicOps() = default;

  bool has_polygon_set() const override { return true; }

  // ------------------------------------------------------------------ points
  Geom make_point(const Rational& x, const Rational& y) const override {
    return box_point(conic_point_of_rational(x, y));
  }

  Geom make_point_3(const Rational&, const Rational&, const Rational&) const override {
    unsupported("3-D points (the conic kind is planar; only the sphere kind uses (x, y, z))");
  }

  void point_approx(const Geom& p, double* xyz) const override {
    const CPoint& q = point(p);
    // NOT the traits' Approximate_2 (== CGAL::to_double), which is not correctly rounded
    // (CGAL_TRAPS_CHECKLIST "Numbers / coordinates").
    xyz[0] = to_double_correctly_rounded(q.x());
    xyz[1] = to_double_correctly_rounded(q.y());
  }

  void point_interval(const Geom& p, std::vector<std::pair<double, double>>& out) const override {
    const CPoint& q = point(p);
    out.clear();
    out.push_back(interval_of(q.x(), 53));
    out.push_back(interval_of(q.y(), 53));
  }

  /// ALWAYS false.  CORE has no sound rationality test for `Expr`
  /// (number_types_and_errors.md gotcha 4, exact_coordinates_contract.md gotcha 1: an `Expr`
  /// produced by the conic traits can compare EQUAL to a rational convergent that is provably
  /// different).  `Conic_point_2` has no room for a "built from rationals" flag either, so even
  /// the points produced by make_point() are reported as non-rational.  KNOWN LIMITATION.
  bool point_is_rational(const Geom& p) const override {
    point(p);   // kind/type validation only
    return false;
  }

  void point_exact_rational(const Geom& p, std::vector<Rational>&) const override {
    point(p);
    throw_error(ErrorCode::NotRepresentable,
                "conic: the coordinates of a conic point are algebraic (CORE::Expr); CORE offers no "
                "sound rationality test, so they are never exposed as exact rationals (use "
                "point_exact() for the Algebraic values or point_interval() for a certified "
                "enclosure)");
  }

  void point_exact(const Geom& p, std::vector<Geom>& numbers) const override {
    const CPoint& q = point(p);
    numbers.clear();
    numbers.push_back(box_core_expr(q.x()));
    numbers.push_back(box_core_expr(q.y()));
  }

  std::string point_repr(const Geom& p) const override {
    const CPoint& q = point(p);
    return "ConicPoint(~" + fmt_double(to_double_correctly_rounded(q.x())) + ", ~" +
           fmt_double(to_double_correctly_rounded(q.y())) + ")";
  }

  Geom convert_point(const Geom& p) const override {
    if (p.kind == Kind::Conic) { point(p); return p; }
    return box_point(to_conic_point(p));
  }

  /// Overridden so that every double this kind hands out is produced by the same correctly-rounded
  /// conversion as point_approx().  The traits' Approximate_2 is `CGAL::to_double(Expr)`, which is
  /// only an approximation of the exact value (CGAL_TRAPS_CHECKLIST "Numbers / coordinates").
  double approximate_coordinate(const Geom& p, int i) const override {
    if (i < 0 || i >= 2) invalid("coordinate index out of range (conic points are planar)");
    double xyz[3] = {0.0, 0.0, 0.0};
    point_approx(p, xyz);
    return xyz[i];
  }

  // ------------------------------------------------------------------ curves
  Geom to_curve(const Geom& xc) const override {
    const CXcv& x = xcurve(xc);
    auto ctr = tr().construct_curve_2_object();
    CCurve c;
    if (x.is_special_segment()) {
      // All six coefficients are 0 for a special segment; feeding them to the coefficient overload
      // would divide by gcd{0} inside Nt_traits::convert_coefficients.  Use the two-point overload
      // (traits_conic.md 2.7), which is exactly how the arc was built.
      c = ctr(x.source(), x.target());
    } else {
      // NB: for a hyperbolic supporting conic this re-enters build_hyperbolic_arc_data().  Such an
      // arc can only exist if conic::set_allow_hyperbolic(true) was used, in which case a CGAL
      // assertion may escape from here — documented, deliberate.
      c = ctr(to_rational(x.r()), to_rational(x.s()), to_rational(x.t()), to_rational(x.u()),
              to_rational(x.v()), to_rational(x.w()), x.orientation(), x.source(), x.target());
    }
    if (!c.is_valid())
      throw_error(ErrorCode::Unsupported,
                  "conic: could not rebuild a general Curve_2 from this x-monotone arc");
    return box_curve(c);
  }

  Geom xcurve_source(const Geom& xc) const override { return box_point(xcurve(xc).source()); }
  Geom xcurve_target(const Geom& xc) const override { return box_point(xcurve(xc).target()); }
  bool xcurve_has_source(const Geom& xc) const override { xcurve(xc); return true; }
  bool xcurve_has_target(const Geom& xc) const override { xcurve(xc); return true; }

  /// Conic arcs are always bounded: this traits declares oblivious side categories, i.e. a bounded
  /// planar arrangement (traits_conic.md gotcha 14), and no constructor of this TU can produce an
  /// unbounded arc (Traits::set() rejects those as invalid).
  bool curve_is_bounded(const Geom& c) const override {
    require_any_curve(c, Kind::Conic, "curve");
    return true;
  }

  BBox curve_bbox(const Geom& c) const override {
    require_any_curve(c, Kind::Conic, "curve");
    // Never Conic_arc_2::bbox(): it passes a Point_2[2] through an Alg_point_2* parameter and is
    // UB for full conics (traits_conic.md gotcha 13).  Construct_bbox_2 is its correct replacement
    // (it uses the vertical AND horizontal tangency points), but it converts with CGAL::to_double
    // without outward rounding, so the result is not certified — we union it with the exactly
    // rounded endpoints and widen it by 4 ulps on every side.
    auto bb_ftor = tr().construct_bbox_2_object();
    CGAL::Bbox_2 bb = (c.type == GeomType::XCurve) ? bb_ftor(xcurve(c)) : bb_ftor(curve(c));
    double lo[2] = {bb.xmin(), bb.ymin()};
    double hi[2] = {bb.xmax(), bb.ymax()};
    // Belt and braces: union with a polyline approximation of the curve (its vertices are ON the
    // curve, so they can only ever be inside the true box) at a tolerance relative to the box we
    // just got.  This catches a grossly wrong Construct_bbox_2 without an unbounded point count.
    {
      const double extent = std::max(hi[0] - lo[0], hi[1] - lo[1]);
      const double tol = std::max(1e-9, 1e-3 * (std::isfinite(extent) && extent > 0 ? extent : 1.0));
      std::vector<double> pts;
      approximate(c, tol, nullptr, pts);
      for (std::size_t i = 0; i + 1 < pts.size(); i += 2) {
        lo[0] = std::min(lo[0], pts[i]);
        hi[0] = std::max(hi[0], pts[i]);
        lo[1] = std::min(lo[1], pts[i + 1]);
        hi[1] = std::max(hi[1], pts[i + 1]);
      }
    }
    if (c.type == GeomType::XCurve || !curve(c).is_full_conic()) {
      const CCurve* base = nullptr;
      CXcv const* x = nullptr;
      if (c.type == GeomType::XCurve) x = &xcurve(c);
      else base = &curve(c);
      const CPoint& s = x ? x->source() : base->source();
      const CPoint& t = x ? x->target() : base->target();
      const double sx = to_double_correctly_rounded(s.x()), sy = to_double_correctly_rounded(s.y());
      const double tx = to_double_correctly_rounded(t.x()), ty = to_double_correctly_rounded(t.y());
      lo[0] = std::min(lo[0], std::min(sx, tx));
      hi[0] = std::max(hi[0], std::max(sx, tx));
      lo[1] = std::min(lo[1], std::min(sy, ty));
      hi[1] = std::max(hi[1], std::max(sy, ty));
    }
    BBox out;
    out.dim = 2;
    for (int i = 0; i < 2; ++i) {
      double l = lo[i], h = hi[i];
      for (int k = 0; k < 4; ++k) {
        l = std::nextafter(l, -std::numeric_limits<double>::infinity());
        h = std::nextafter(h, std::numeric_limits<double>::infinity());
      }
      out.lo[i] = l;
      out.hi[i] = h;
    }
    return out;
  }

  void approximate(const Geom& c, double tolerance, const BBox* /*clip*/,
                   std::vector<double>& out) const override {
    require_any_curve(c, Kind::Conic, "curve");
    // Validate BEFORE touching Approximate_2: `error <= 0` makes the recursion in add_points()
    // never terminate -> stack overflow / SIGSEGV (rendering_and_approximation.md gotcha 7).
    if (!(tolerance > 0.0) || std::isnan(tolerance))
      invalid("approximate: the tolerance must be a positive number");
    if (tolerance < 1e-12) tolerance = 1e-12;
    // `clip` is ignored: every conic curve of this kind is bounded.
    if (c.type == GeomType::XCurve) {
      approx_xcurve(xcurve(c), tolerance, out, false);
      return;
    }
    const CCurve& cv = curve(c);
    std::vector<CXcv> pieces;
    x_monotone_pieces(cv, pieces);
    if (pieces.empty())
      invalid("approximate: the curve has no x-monotone pieces (it is degenerate)");
    chain_pieces(pieces);
    for (std::size_t i = 0; i < pieces.size(); ++i) approx_xcurve(pieces[i], tolerance, out, i > 0);
  }

  std::string curve_repr(const Geom& c) const override {
    require_any_curve(c, Kind::Conic, "curve");
    std::ostringstream os;
    if (c.type == GeomType::Curve) {
      const CCurve& cv = curve(c);
      if (cv.is_full_conic()) {
        os << "Conic(full " << coeff_list(cv) << ", orientation=" << orientation_name(cv.orientation())
           << ")";
        return os.str();
      }
      os << "ConicArc(" << coeff_list(cv) << ", orientation=" << orientation_name(cv.orientation())
         << ", source=" << pt_str(cv.source()) << ", target=" << pt_str(cv.target()) << ")";
      return os.str();
    }
    const CXcv& x = xcurve(c);
    os << "ConicArc(" << coeff_list(x) << ", orientation=" << orientation_name(x.orientation())
       << ", source=" << pt_str(x.source()) << ", target=" << pt_str(x.target()) << ")";
    return os.str();
  }

  void convert_curve(const Geom& c, std::vector<Geom>& out) const override {
    if (c.kind == Kind::Conic) {
      require_any_curve(c, Kind::Conic, "curve");
      out.push_back(c);
      return;
    }
    if (c.type != GeomType::Curve && c.type != GeomType::XCurve)
      invalid("convert_curve: the argument must be a curve");
    switch (c.kind) {
      case Kind::Segment: {
        const SegCurve& s = c.as<SegCurve>();
        out.push_back(box_curve(build_segment_rat(to_rational(s.source().x()),
                                                  to_rational(s.source().y()),
                                                  to_rational(s.target().x()),
                                                  to_rational(s.target().y()))));
        return;
      }
      case Kind::Linear: {
        const LinCurve& l = c.as<LinCurve>();
        if (!l.is_segment())
          throw_error(ErrorCode::Unsupported,
                      "conic: a linear ray or line is unbounded and has no conic-arc "
                      "representation (only bounded linear segments convert)");
        const auto seg = l.segment();
        out.push_back(box_curve(build_segment_rat(to_rational(seg.source().x()),
                                                  to_rational(seg.source().y()),
                                                  to_rational(seg.target().x()),
                                                  to_rational(seg.target().y()))));
        return;
      }
      case Kind::Polyline: {
        if (c.holds<PolyCurve>()) {
          const PolyCurve& p = c.as<PolyCurve>();
          push_polyline(p.number_of_subcurves(), [&p](std::size_t i) { return p[i]; }, out);
          return;
        }
        const PolyXcv& p = c.as<PolyXcv>();
        push_polyline(p.number_of_subcurves(), [&p](std::size_t i) { return p[i]; }, out);
        return;
      }
      case Kind::CircleSegment: {
        if (c.holds<CsCurve>()) {
          out.push_back(box_curve(build_from_circle_segment_curve(c.as<CsCurve>())));
          return;
        }
        out.push_back(box_curve(build_from_circle_segment_xcurve(c.as<CsXcv>())));
        return;
      }
      case Kind::Bezier: {
        const BzCurve* bc = nullptr;
        if (c.holds<BzCurve>()) {
          bc = &c.as<BzCurve>();
        } else {
          const BzXcv& bx = c.as<BzXcv>();
          bc = &bx.supporting_curve();
          if (bc->number_of_control_points() != 2)
            throw_error(ErrorCode::Unsupported,
                        "conic: an x-monotone piece of a Bezier curve of degree > 1 is a sub-arc "
                        "whose endpoints are algebraic Bezier points; convert its supporting curve "
                        "instead");
        }
        const unsigned n = bc->number_of_control_points();
        if (n == 2) {
          const auto& p0 = bc->control_point(0);
          const auto& p1 = bc->control_point(1);
          out.push_back(box_curve(build_segment_rat(p0.x(), p0.y(), p1.x(), p1.y())));
          return;
        }
        if (n == 3) {
          // A polynomial quadratic Bezier is the rational quadratic Bezier with weights (1,1,1),
          // i.e. a parabolic arc (traits_conic.md 12.1).
          const auto& p0 = bc->control_point(0);
          const auto& p1 = bc->control_point(1);
          const auto& p2 = bc->control_point(2);
          out.push_back(box_curve(build_rational_bezier(p0.x(), p0.y(), p1.x(), p1.y(), p2.x(),
                                                        p2.y(), Rational(1), Rational(1),
                                                        Rational(1))));
          return;
        }
        throw_error(ErrorCode::NotRepresentable,
                    "conic: a Bezier curve of degree " + std::to_string(n - 1) +
                        " is not a conic (only degrees 1 and 2 convert exactly)");
      }
      case Kind::Sphere:
        throw_error(ErrorCode::KindMismatch,
                    "conic: a geodesic arc on the sphere is not a planar curve");
      default:
        throw_error(ErrorCode::Unsupported, "conic: unknown source kind for convert_curve");
    }
  }

 private:
  static std::string pt_str(const CPoint& p) {
    return "(~" + fmt_double(to_double_correctly_rounded(p.x())) + ", ~" +
           fmt_double(to_double_correctly_rounded(p.y())) + ")";
  }

  template <class Arc>
  static std::string coeff_list(const Arc& a) {
    // The coefficients as CGAL STORED them (integerised, and possibly negated to agree with the
    // requested orientation) — traits_conic.md gotcha 2.
    return fmt_rational(to_rational(a.r())) + ", " + fmt_rational(to_rational(a.s())) + ", " +
           fmt_rational(to_rational(a.t())) + ", " + fmt_rational(to_rational(a.u())) + ", " +
           fmt_rational(to_rational(a.v())) + ", " + fmt_rational(to_rational(a.w()));
  }

  template <class Get>
  static void push_polyline(std::size_t n, Get get, std::vector<Geom>& out) {
    if (n == 0) err(ErrorCode::InvalidArgument, "an empty polyline has no conic representation");
    for (std::size_t i = 0; i < n; ++i) {
      const auto& s = get(i);
      out.push_back(box_curve(build_segment_rat(to_rational(s.source().x()),
                                                to_rational(s.source().y()),
                                                to_rational(s.target().x()),
                                                to_rational(s.target().y()))));
    }
  }

  void approx_xcurve(const CXcv& x, double error, std::vector<double>& out, bool skip_first) const {
    // The output iterator MUST be a back_insert_iterator: Approximate_2::add_points recurses with
    // the iterator passed by value and discards the returned one, so a raw pointer / vector
    // iterator silently writes garbage (rendering_and_approximation.md gotcha 6).
    std::vector<CApproxPoint> pts;
    auto ap = tr().approximate_2_object();
    auto cmp_end = tr().compare_endpoints_xy_2_object();
    // `l2r` is about min-vertex -> max-vertex, NOT about the arc's own direction; passing
    // `is_directed_right` reproduces source -> target order, which is what ops.hpp promises.
    const bool l2r = (cmp_end(x) == CGAL::SMALLER);
    ap(x, error, std::back_inserter(pts), l2r);
    if (pts.empty()) return;
    // Approximate_2 is CGAL::to_double(CORE::Expr), i.e. an approximation of an approximation
    // (see approximate_coordinate above).  Both endpoints are always emitted, so replace exactly
    // those two with the correctly-rounded conversion used by point_approx() — otherwise every
    // polyline handed to Python starts and ends on a coordinate that differs from the one
    // Point.approx / Arrangement.vertex_coordinates() report for the very same point (measured on
    // 28 % of random rational-endpoint arcs).  Interior samples are rendering data: left alone.
    const double sx = to_double_correctly_rounded(x.source().x());
    const double sy = to_double_correctly_rounded(x.source().y());
    const double tx = to_double_correctly_rounded(x.target().x());
    const double ty = to_double_correctly_rounded(x.target().y());
    std::size_t i = skip_first ? 1 : 0;
    out.reserve(out.size() + 2 * (pts.size() - i));
    for (; i < pts.size(); ++i) {
      if (i == 0) { out.push_back(sx); out.push_back(sy); }
      else if (i + 1 == pts.size()) { out.push_back(tx); out.push_back(ty); }
      else { out.push_back(pts[i].x()); out.push_back(pts[i].y()); }
    }
  }
};

// =============================================================================================
// (3) namespace arr2d::conic — the kind-specific free functions declared in ops.hpp
// =============================================================================================
namespace conic {

Geom make_full(const Rational& r, const Rational& s, const Rational& t, const Rational& u,
               const Rational& v, const Rational& w) {
  return make_geom(Kind::Conic, GeomType::Curve, build_full(r, s, t, u, v, w));
}

Geom make_arc(const Rational& r, const Rational& s, const Rational& t, const Rational& u,
              const Rational& v, const Rational& w, int orientation, const Geom& source,
              const Geom& target) {
  return make_geom(Kind::Conic, GeomType::Curve,
                   build_arc(r, s, t, u, v, w, orientation_of_int(orientation),
                             to_conic_point(source), to_conic_point(target)));
}

Geom make_arc_with_defining_conics(const Rational coeffs[6], int orientation,
                                   double approx_source_x, double approx_source_y,
                                   const Rational source_conic[6], double approx_target_x,
                                   double approx_target_y, const Rational target_conic[6]) {
  if (coeffs == nullptr || source_conic == nullptr || target_conic == nullptr)
    err(ErrorCode::InvalidArgument, "null coefficient array");
  const CGAL::Orientation orient = orientation_of_int(orientation);
  gate_supporting_conic(coeffs[0], coeffs[1], coeffs[2], coeffs[3], coeffs[4], coeffs[5], orient);
  // The approximate endpoints only *select* among the exact intersection candidates; converting the
  // doubles exactly to rationals (never through Expr(double), which is an approximation) is enough.
  const CPoint app_src(alg(rational_from_double(approx_source_x)),
                       alg(rational_from_double(approx_source_y)));
  const CPoint app_tgt(alg(rational_from_double(approx_target_x)),
                       alg(rational_from_double(approx_target_y)));
  CCurve c = tr().construct_curve_2_object()(
      coeffs[0], coeffs[1], coeffs[2], coeffs[3], coeffs[4], coeffs[5], orient, app_src,
      source_conic[0], source_conic[1], source_conic[2], source_conic[3], source_conic[4],
      source_conic[5], app_tgt, target_conic[0], target_conic[1], target_conic[2], target_conic[3],
      target_conic[4], target_conic[5]);
  if (!c.is_valid())
    err(ErrorCode::InvalidArgument,
        "invalid conic arc: no intersection of the arc's conic with the two defining conics was "
        "found near the given approximate endpoints, or the two chosen endpoints coincide");
  return make_geom(Kind::Conic, GeomType::Curve, std::move(c));
}

Geom make_circle(const Rational& cx, const Rational& cy, const Rational& squared_radius,
                 int orientation) {
  return make_geom(Kind::Conic, GeomType::Curve,
                   build_circle_full(cx, cy, squared_radius, orientation));
}

Geom make_circle_arc(const Rational& cx, const Rational& cy, const Rational& squared_radius,
                     int orientation, const Geom& source, const Geom& target) {
  return make_geom(Kind::Conic, GeomType::Curve,
                   build_circle_arc(cx, cy, squared_radius, orientation, to_conic_point(source),
                                    to_conic_point(target)));
}

Geom make_ellipse(const Rational& cx, const Rational& cy, const Rational& a, const Rational& b,
                  const Rational& dx, const Rational& dy, int orientation) {
  if (CGAL::sign(a) != CGAL::POSITIVE || CGAL::sign(b) != CGAL::POSITIVE)
    err(ErrorCode::InvalidArgument, "the semi-axis lengths of an ellipse must be positive");
  const Rational n = dx * dx + dy * dy;
  if (CGAL::sign(n) == CGAL::ZERO)
    err(ErrorCode::InvalidArgument, "the axis direction of an ellipse must not be (0, 0)");
  // Derivation.  With d = (dx, dy), n = |d|^2, D = X - C, the ellipse with semi-axis a along d and
  // semi-axis b along d^perp = (-dy, dx) is
  //     b^2 (d . D)^2 + a^2 (d^perp . D)^2 = a^2 b^2 n,
  // because d/sqrt(n) and d^perp/sqrt(n) are the orthonormal canonical axes.  Expanding
  //     (d . D)      = dx*Dx + dy*Dy,      (d^perp . D) = -dy*Dx + dx*Dy
  // gives   A Dx^2 + B Dy^2 + C Dx Dy - a^2 b^2 n = 0 with
  //     A = b^2 dx^2 + a^2 dy^2,  B = b^2 dy^2 + a^2 dx^2,  C = 2 dx dy (b^2 - a^2),
  // and substituting Dx = x - cx, Dy = y - cy yields the six coefficients below.
  // Sanity: 4rs - t^2 = 4AB - C^2 = 4 a^2 b^2 n^2 > 0 (an ellipse, as required by set_full).
  const Rational a2 = a * a, b2 = b * b;
  const Rational A = b2 * dx * dx + a2 * dy * dy;
  const Rational B = b2 * dy * dy + a2 * dx * dx;
  const Rational C = 2 * dx * dy * (b2 - a2);
  Rational r = A, s = B, t = C;
  Rational u = -2 * A * cx - C * cy;
  Rational v = -2 * B * cy - C * cx;
  Rational w = A * cx * cx + B * cy * cy + C * cx * cy - a2 * b2 * n;
  orient_full_coefficients(orientation, r, s, t, u, v, w);
  CCurve c = build_full(r, s, t, u, v, w);
  if (static_cast<int>(c.orientation()) != (orientation > 0 ? 1 : -1))
    err(ErrorCode::Generic, "internal error: CGAL computed an unexpected ellipse orientation");
  return make_geom(Kind::Conic, GeomType::Curve, std::move(c));
}

Geom make_segment(const Geom& p, const Geom& q) {
  return make_geom(Kind::Conic, GeomType::Curve, build_segment(p, q));
}

Geom make_from_five_points(const Geom& p1, const Geom& p2, const Geom& p3, const Geom& p4,
                           const Geom& p5) {
  const Geom* gs[5] = {&p1, &p2, &p3, &p4, &p5};
  Rational px[5], py[5];
  for (int i = 0; i < 5; ++i) {
    Rational x, y;
    require_rational_coord_pair(*gs[i], "point " + std::to_string(i + 1), x, y);
    px[i] = x;
    py[i] = y;
  }
  // CGAL 6.1 preconditions of the five-point overload (traits_conic.md 2.5): the orientation of
  // (p1, p2, p5) is used for the arc and (p1, p3, p5), (p1, p4, p5) must agree with it.  Tested
  // here in exact rational arithmetic so that misuse is Error(InvalidArgument) instead of a
  // CGAL::Precondition_exception.
  const int o = rat_orientation(px[0], py[0], px[1], py[1], px[4], py[4]);
  if (o == 0)
    err(ErrorCode::InvalidArgument,
        "the points p1, p2 and p5 are collinear: they do not define a conic arc");
  if (rat_orientation(px[0], py[0], px[2], py[2], px[4], py[4]) != o ||
      rat_orientation(px[0], py[0], px[3], py[3], px[4], py[4]) != o)
    err(ErrorCode::InvalidArgument,
        "p2, p3 and p4 must all lie on the same side of the chord p1-p5 (they must be interior "
        "points of the arc, in this order)");
  // The supporting conic is determined by the five points; compute it exactly ourselves so that the
  // CGAL 6.1 hyperbolic-axis bug can be gated BEFORE Construct_curve_2 is entered (13.7: the
  // five-point overload reaches Traits::set() and therefore build_hyperbolic_arc_data()).
  Rational c[6];
  if (!conic_through_five_points(px, py, c))
    err(ErrorCode::InvalidArgument, "the five points do not determine a conic");
  gate_supporting_conic(c[0], c[1], c[2], c[3], c[4], c[5],
                        o > 0 ? CGAL::COUNTERCLOCKWISE : CGAL::CLOCKWISE);
  CCurve arc = tr().construct_curve_2_object()(CRatPoint(px[0], py[0]), CRatPoint(px[1], py[1]),
                                               CRatPoint(px[2], py[2]), CRatPoint(px[3], py[3]),
                                               CRatPoint(px[4], py[4]));
  if (!arc.is_valid())
    err(ErrorCode::InvalidArgument,
        "invalid conic arc through five points (three of them are collinear, or p2/p3/p4 are not "
        "strictly between p1 and p5 on the conic)");
  return make_geom(Kind::Conic, GeomType::Curve, std::move(arc));
}

Geom make_from_rational_bezier(const Geom& p0, const Geom& p1, const Geom& p2, const Rational& w0,
                               const Rational& w1, const Rational& w2) {
  Rational x0, y0, x1, y1, x2, y2;
  require_rational_coord_pair(p0, "control point P0", x0, y0);
  require_rational_coord_pair(p1, "control point P1", x1, y1);
  require_rational_coord_pair(p2, "control point P2", x2, y2);
  return make_geom(Kind::Conic, GeomType::Curve,
                   build_rational_bezier(x0, y0, x1, y1, x2, y2, w0, w1, w2));
}

Geom make_from_circle_segment(const Geom& circle_segment_curve) {
  const Geom& g = circle_segment_curve;
  require_kind(g, Kind::CircleSegment, "curve");
  if (g.type != GeomType::Curve && g.type != GeomType::XCurve)
    err(ErrorCode::InvalidArgument, "make_from_circle_segment expects a circle-segment curve");
  // Read the raw CGAL object out of the box directly: the circle-segment KindOps may not be linked
  // into this build, so ops(Kind::CircleSegment) must not be used here.
  if (g.holds<CsCurve>())
    return make_geom(Kind::Conic, GeomType::Curve, build_from_circle_segment_curve(g.as<CsCurve>()));
  return make_geom(Kind::Conic, GeomType::Curve, build_from_circle_segment_xcurve(g.as<CsXcv>()));
}

void coefficients(const Geom& c, Rational out[6]) {
  require_any_curve(c, Kind::Conic, "curve");
  if (out == nullptr) err(ErrorCode::InvalidArgument, "null output array");
  if (c.type == GeomType::XCurve) {
    const CXcv& x = c.as<CXcv>();
    out[0] = to_rational(x.r()); out[1] = to_rational(x.s()); out[2] = to_rational(x.t());
    out[3] = to_rational(x.u()); out[4] = to_rational(x.v()); out[5] = to_rational(x.w());
    return;
  }
  const CCurve& cv = c.as<CCurve>();
  out[0] = to_rational(cv.r()); out[1] = to_rational(cv.s()); out[2] = to_rational(cv.t());
  out[3] = to_rational(cv.u()); out[4] = to_rational(cv.v()); out[5] = to_rational(cv.w());
}

int orientation(const Geom& c) {
  require_any_curve(c, Kind::Conic, "curve");
  if (c.type == GeomType::XCurve) return static_cast<int>(c.as<CXcv>().orientation());
  return static_cast<int>(c.as<CCurve>().orientation());
}

bool is_full(const Geom& c) {
  require_any_curve(c, Kind::Conic, "curve");
  // An x-monotone arc is never a full conic (Make_x_monotone_2 splits a full conic into 2 arcs).
  if (c.type == GeomType::XCurve) return false;
  return c.as<CCurve>().is_full_conic();
}

int conic_type(const Geom& c) {
  Rational co[6];
  coefficients(c, co);
  if (orientation(c) == 0) return LINE_PAIR_OR_SEGMENT;   // segment / line-pair segment
  const CGAL::Sign disc = CGAL::sign(4 * co[0] * co[1] - co[2] * co[2]);
  if (CGAL::sign(co[0]) == CGAL::ZERO && CGAL::sign(co[1]) == CGAL::ZERO &&
      CGAL::sign(co[2]) == CGAL::ZERO)
    return LINE_PAIR_OR_SEGMENT;
  if (disc == CGAL::POSITIVE) return ELLIPSE;     // circles included
  if (disc == CGAL::ZERO) return PARABOLA;
  return HYPERBOLA;
}

Geom make_point_algebraic(const Geom& x, const Geom& y) {
  auto to_alg = [](const Geom& n, const char* what) -> Algebraic {
    if (n.type != GeomType::Number)
      err(ErrorCode::InvalidArgument, std::string(what) + " must be a boxed number");
    switch (number_kind(n)) {
      case NumberKind::Rational: return alg(number_to_rational(n));
      case NumberKind::SqrtExt: return to_core_expr(number_to_sqrt_ext(n));
      case NumberKind::Algebraic: return n.as<Algebraic>();
    }
    err(ErrorCode::InvalidArgument, std::string(what) + " has an unknown number kind");
  };
  return make_geom(Kind::Conic, GeomType::Point, CPoint(to_alg(x, "x"), to_alg(y, "y")));
}

void set_allow_hyperbolic(bool allow) { allow_hyperbolic_flag().store(allow); }
bool allow_hyperbolic() { return allow_hyperbolic_flag().load(); }

}  // namespace conic

// =============================================================================================
// (4) Explicit instantiation of the generic arrangement implementation.
//     KindPolicy<ConicTypes> is already specialised in impl/arr_impl.hpp (naive/simple/walk/
//     landmarks/trapezoid point location, no triangulation, ray shooting, free CGAL::is_valid).
// =============================================================================================
template class ArrImpl<ConicTypes>;

}  // namespace arr2d

// =============================================================================================
// (5) Static registrar
// =============================================================================================
namespace {
struct Registrar {
  Registrar() {
    // Leaked on purpose: KindOpsBase owns an Arr_traits_adaptor_2 copy whose intersection cache
    // holds CORE::Expr values.  Destroying it during static teardown aborts the process with
    // `! blocks.empty()` (CGAL/CORE/MemoryPool.h:125) — CGAL_TRAPS_CHECKLIST "Process / build".
    static arr2d::ConicOps* ops = new arr2d::ConicOps();
    arr2d::register_kind(
        arr2d::Kind::Conic,
        arr2d::KindEntry{ops,
                         [] {
                           return std::unique_ptr<arr2d::ArrBase>(
                               new arr2d::ArrImpl<arr2d::ConicTypes>());
                         },
                         &arr2d::make_polygon_set_conic});
  }
} registrar;
}  // namespace
