// ===========================================================================
// arr2d — Kind::CircleSegment: CGAL::Arr_circle_segment_traits_2<Epeck> (circular arcs
// + line segments, bounded planar topology).
//
// Contents (see docs/dev/workflows/stage2_kinds_and_bso.js, TASK kind_circle_segment):
//   (1) CircleSegmentTypes::traits()          — the process-wide traits instance
//   (2) class CircleSegmentOps                — the kind-specific half of KindOps
//   (3) namespace arr2d::circle_segment       — the free constructors/accessors of ops.hpp
//   (4) template class arr2d::ArrImpl<CircleSegmentTypes>;
//   (5) the static registrar
//
// CGAL 6.1 references (docs/dev/cgal61_api/traits_circle_segment.md unless noted):
//   * gotcha 1  — the coordinate type is CGAL::Sqrt_extension<FT, FT, Tag_true,
//                 Boolean_tag<true>> (a0()/a1()/root()/is_extended()), NOT _One_root_number.
//                 Every exact/approximate conversion here goes through impl/number_conv.hpp.
//   * gotcha 3  — no Construct_x_monotone_curve_2 and no Construct_curve_2:
//                 KindOpsBase::construct_x_monotone_curve reports Unsupported and
//                 KindPolicy<CircleSegmentTypes>::supports_landmarks is false (verified below).
//   * gotcha 4  — the (center, RADIUS, orientation) constructors keep the vertical tangency
//                 points RATIONAL; make_full_circle_r / make_arc_r use them.
//   * gotcha 5  — traits functors hold references into the traits object; never cache them
//                 (KindOpsBase already fetches every functor at the point of use). The
//                 intersection cache is never cleared, so the traits is built with
//                 use_intersection_caching == false.
//   * gotcha 7  — Approximate_2's curve overload is (xcv, error, oi, l2r) and emits
//                 CGAL::Cartesian<double>::Point_2; the legacy xcv.approximate(oi, n) is a
//                 different API and is NOT used here.
//   * rendering_and_approximation.md gotchas 6 & 7 — the output iterator MUST be a
//                 std::back_insert_iterator (add_points() recurses with the iterator passed by
//                 value) and `error <= 0` recurses forever (SIGSEGV). approximate() validates
//                 and clamps the tolerance before calling CGAL.
//   * number_types_and_errors.md gotcha 2 / CGAL_TRAPS_CHECKLIST "Numbers / coordinates":
//                 CGAL::to_double(Epeck::FT) and Approximate_2 are NOT correctly rounded.
//                 point_approx / point_interval / approximate_coordinate use
//                 to_double_correctly_rounded / interval_of from impl/number_conv.hpp.
//   * exact_coordinates_contract.md gotcha 3 / CGAL_TRAPS_CHECKLIST: is_extended() == true does
//                 NOT imply irrational (a perfect-square radicand is never simplified), so
//                 rationality is decided with sqrt_ext_is_rational(), not with is_extended().
//   * CGAL_TRAPS_CHECKLIST "Circle-segment kind" — both items are handled here.
//
// The CORE memory-pool trap (CGAL/CORE/MemoryPool.h:125) does NOT apply to this kind: no
// CORE::Expr is ever constructed (the coordinates are Sqrt_extension over Epeck::FT), so the
// process-wide traits object may be a plain function-local static.
// ===========================================================================

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <limits>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "arr2d/kinds/circle_segment_types.hpp"
// Other kinds' *_types.hpp are header-only: including them lets convert_point/convert_curve
// unbox foreign geometry WITHOUT creating a link dependency on those kinds' TUs (a kind test
// links its own TU only).  Segment / Linear / Polyline all use Epeck, so their Point_2 is one
// and the same C++ type.
#include "arr2d/kinds/segment_types.hpp"
#include "arr2d/kinds/linear_types.hpp"
#include "arr2d/kinds/polyline_types.hpp"

#include "arr2d/impl/kind_ops_base.hpp"
#include "arr2d/impl/number_conv.hpp"
#include "arr2d/impl/arr_impl.hpp"

#include "arr2d/bso.hpp"
#include "arr2d/ops.hpp"
#include "arr2d/registry.hpp"

namespace arr2d {

// ---------------------------------------------------------------------------
// (1) The process-wide traits instance.
//
// Lifetime: a function-local static, constructed on the first call and destroyed only during
// static destruction at process exit.  It therefore outlives every Point_2 / Curve_2 /
// X_monotone_curve_2 / ArrImpl of this kind that the process creates through this library
// (all of those are created after the first call and destroyed before exit), which is exactly
// what CGAL requires: an Arrangement_2 keeps a raw pointer to its traits
// (arrangement_core.md gotcha 14) and every traits functor keeps a reference into the traits
// (traits_circle_segment.md gotcha 5).  The object is NEVER copied — KindOpsBase's
// Arr_traits_adaptor_2 is the single (deliberately leaked) copy, and copy-CONSTRUCTION of this
// traits only duplicates the intersection cache, which is safe.
//
// use_intersection_caching == false on purpose (traits_circle_segment.md §6 "Intersection
// cache"): the map is `mutable`, has no clear() and grows for the life of the process.  With
// caching off Make_x_monotone_2 stamps every curve with index 0, which merely disables a few
// same-supporting-curve fast paths; no result changes.
// ---------------------------------------------------------------------------
const CircleSegmentTypes::Traits& CircleSegmentTypes::traits() {
  static const Traits t(/*use_intersection_caching=*/false);
  return t;
}

namespace {

// ---------------------------------------------------------------------------
// Local type aliases
// ---------------------------------------------------------------------------
using Types = CircleSegmentTypes;
using Kernel = Types::Kernel;
using FT = Types::FT;                               // Epeck::FT (Lazy_exact_nt<mpq_rational>)
using CsTraits = Types::Traits;
using CsPoint = Types::Point_2;                     // _One_root_point_2<FT, true>
using CsCurve = Types::Curve_2;                     // _Circle_segment_2<Epeck, true>
using CsXCurve = Types::X_monotone_curve_2;         // _X_monotone_circle_segment_2<Epeck, true>
using CoordNT = CsTraits::CoordNT;                  // Sqrt_extension<FT, FT, Tag_true, Tag_true>
using Circle_2 = Kernel::Circle_2;
using Line_2 = Kernel::Line_2;
using RatPoint = Kernel::Point_2;                   // Epeck::Point_2 (rational coordinates)

using MxResult = std::variant<CsPoint, CsXCurve>;   // Make_x_monotone_2's dereference type

// ---------------------------------------------------------------------------
// Boxing helpers
// ---------------------------------------------------------------------------
inline Geom box_pt(const CsPoint& p) { return make_geom(Kind::CircleSegment, GeomType::Point, p); }
inline Geom box_cv(const CsCurve& c) { return make_geom(Kind::CircleSegment, GeomType::Curve, c); }

[[noreturn]] void bad(ErrorCode code, const std::string& msg) {
  throw_error(code, "circle_segment: " + msg);
}

// ---------------------------------------------------------------------------
// Coordinate conversions (CoordNT <-> arr2d numbers)
//
// traits_circle_segment.md gotcha 1: CoordNT is a CGAL::Sqrt_extension, value
// a0 + a1*sqrt(root) when is_extended(), else just a0.
// exact_coordinates_contract.md gotcha 3: is_extended() == true may still be a rational value
// (a perfect-square radicand is never simplified), hence sqrt_ext_is_rational() below.
// ---------------------------------------------------------------------------
SqrtExt coord_to_sqrt_ext(const CoordNT& v) {
  if (!v.is_extended()) return SqrtExt{to_rational(v.a0()), Rational(0), Rational(0)};
  Rational root = to_rational(v.root());
  if (sign_of(root) < 0)  // impossible for a circle-segment coordinate (root == squared radius)
    bad(ErrorCode::Generic, "a point coordinate carries a negative radicand");
  return SqrtExt{to_rational(v.a0()), to_rational(v.a1()), root};
}

/// True (and `out` set) iff the coordinate's exact VALUE is rational.
bool coord_is_rational(const CoordNT& v, Rational& out) {
  if (!v.is_extended()) { out = to_rational(v.a0()); return true; }
  return sqrt_ext_is_rational(coord_to_sqrt_ext(v), out);
}

/// Correctly rounded double.  NOT CGAL::to_double / Approximate_2
/// (number_types_and_errors.md gotcha 2).
double coord_to_double(const CoordNT& v) {
  if (!v.is_extended()) return to_double_correctly_rounded(v.a0());
  return to_double_correctly_rounded(coord_to_sqrt_ext(v));
}

/// Certified enclosure of the exact value.
std::pair<double, double> coord_interval(const CoordNT& v) {
  if (!v.is_extended()) return interval_of(v.a0());
  return interval_of(coord_to_sqrt_ext(v));
}

/// SqrtExt -> CoordNT, normalising a provably rational value to the un-extended
/// representation (cheaper arithmetic and exact CGAL fast paths).  Requires c >= 0.
CoordNT sqrt_ext_to_coord(const SqrtExt& s) {
  if (sign_of(s.c) < 0) bad(ErrorCode::InvalidArgument, "sqrt-extension coordinate needs c >= 0");
  Rational value;
  if (sqrt_ext_is_rational(s, value)) return CoordNT(to_epeck_ft(value));
  return CoordNT(to_epeck_ft(s.a), to_epeck_ft(s.b), to_epeck_ft(s.c));
}

// ---------------------------------------------------------------------------
// Textual helpers (exact representations)
// ---------------------------------------------------------------------------
std::string rat_str(const Rational& r) {
  std::string num, den;
  rational_to_strings(r, num, den);
  return den == "1" ? num : num + "/" + den;
}

std::string coord_str(const CoordNT& v) {
  Rational q;
  if (coord_is_rational(v, q)) return rat_str(q);
  SqrtExt s = coord_to_sqrt_ext(v);
  if (sign_of(s.b) < 0) {
    const Rational nb = -s.b;   // named: Rational is an expression-template type
    return rat_str(s.a) + " - " + rat_str(nb) + "*sqrt(" + rat_str(s.c) + ")";
  }
  return rat_str(s.a) + " + " + rat_str(s.b) + "*sqrt(" + rat_str(s.c) + ")";
}

std::string point_str(const CsPoint& p) {
  return "(" + coord_str(p.x()) + ", " + coord_str(p.y()) + ")";
}

std::string rat_point_str(const RatPoint& p) {
  return "(" + rat_str(to_rational(p.x())) + ", " + rat_str(to_rational(p.y())) + ")";
}

const char* orient_str(CGAL::Orientation o) {
  if (o == CGAL::COUNTERCLOCKWISE) return "ccw";
  if (o == CGAL::CLOCKWISE) return "cw";
  return "collinear";
}

// ---------------------------------------------------------------------------
// Argument checking helpers
// ---------------------------------------------------------------------------
CGAL::Orientation orientation_arg(int orient) {
  if (orient == 1) return CGAL::COUNTERCLOCKWISE;
  if (orient == -1) return CGAL::CLOCKWISE;
  bad(ErrorCode::InvalidArgument,
      "orientation must be +1 (counterclockwise) or -1 (clockwise), got " + std::to_string(orient));
}

/// A CircleSegment Point_2 out of a point Geom of ANY planar kind (rational coordinates for the
/// foreign kinds; sqrt coordinates are preserved for our own).  This is the body of
/// CircleSegmentOps::convert_point, factored out so the free constructors can use it too.
CsPoint to_cs_point(const Geom& g) {
  require_type(g, GeomType::Point, "point");
  if (g.kind == Kind::CircleSegment) return g.as<CsPoint>();
  // Segment / Linear / Polyline all use Epeck, so their Point_2 is literally this type.  Reading
  // it directly avoids a link dependency on those kinds' TUs.
  if (g.holds<RatPoint>()) {
    const RatPoint& p = g.as<RatPoint>();
    return CsPoint(CoordNT(p.x()), CoordNT(p.y()));
  }
  // Everything else (bezier / conic / sphere) goes through that kind's KindOps, which must be
  // linked and registered.
  if (!kind_available(g.kind))
    bad(ErrorCode::Unsupported, std::string("cannot convert a point of kind '") + kind_name(g.kind) +
                                    "': that kind is not linked into this build");
  const KindOps& src = arr2d::ops(g.kind);
  if (src.dimension() != 2)
    bad(ErrorCode::KindMismatch, std::string("cannot convert a ") + std::to_string(src.dimension()) +
                                     "-dimensional point of kind '" + kind_name(g.kind) + "'");
  std::vector<Rational> xy;
  src.point_exact_rational(g, xy);   // throws NotRepresentable for algebraic coordinates
  if (xy.size() != 2)
    bad(ErrorCode::InvalidArgument, "the source point did not yield two rational coordinates");
  return CsPoint(CoordNT(to_epeck_ft(xy[0])), CoordNT(to_epeck_ft(xy[1])));
}

/// The rational coordinates of a point Geom (any kind); NotRepresentable if not rational.
void to_rational_xy(const Geom& g, Rational& x, Rational& y, const char* what) {
  CsPoint p = to_cs_point(g);
  if (!coord_is_rational(p.x(), x) || !coord_is_rational(p.y(), y))
    bad(ErrorCode::NotRepresentable,
        std::string(what) + " requires a point with rational coordinates, but got " + point_str(p));
}

RatPoint to_rat_point(const Geom& g, const char* what) {
  Rational x, y;
  to_rational_xy(g, x, y, what);
  return RatPoint(to_epeck_ft(x), to_epeck_ft(y));
}

/// CGAL's own on-circle precondition, evaluated exactly and reported as arr2d::Error instead of
/// a CGAL assertion (Circle_segment_2.h:288-297).  Note that the two coordinates may live in
/// DIFFERENT sqrt extensions; CGAL::compare handles that exactly (ACDE_TAG == Tag_true =>
/// in_same_extension defaults to false), whereas ADDING them would trip Sqrt_extension's
/// check_roots precondition — which is why the comparison is written in this shape.
void require_on_circle(const CsPoint& p, const RatPoint& center, const FT& sqr_r,
                       const char* which) {
  const CoordNT dx = p.x() - center.x();
  const CoordNT dy = p.y() - center.y();
  if (CGAL::compare(CGAL::square(dx), sqr_r - CGAL::square(dy)) != CGAL::EQUAL)
    bad(ErrorCode::InvalidArgument,
        std::string("the ") + which + " point " + point_str(p) + " does not lie on the circle");
}

/// CGAL's on-line precondition (Circle_segment_2.h, the (Line_2, source, target) ctor).
void require_on_line(const CsPoint& p, const Line_2& line, const char* which) {
  if (CGAL::compare(p.x() * line.a() + line.c(), -(p.y() * line.b())) != CGAL::EQUAL)
    bad(ErrorCode::InvalidArgument,
        std::string("the ") + which + " point " + point_str(p) + " does not lie on the line");
}

/// tolerance validation shared by approximate():
/// rendering_and_approximation.md gotcha 7 — `error <= 0` makes Approximate_2's recursion never
/// terminate (verified SIGSEGV).  NaN is rejected by the `!(t > 0)` test.
double checked_tolerance(double t) {
  if (!(t > 0.0))
    bad(ErrorCode::InvalidArgument, "approximate(): the tolerance must be a positive number");
  return t < 1e-12 ? 1e-12 : t;
}

// ---------------------------------------------------------------------------
// Geometry helpers
// ---------------------------------------------------------------------------

/// The x-monotone pieces of a Curve_2, in order along the curve (source -> target).  CGAL emits
/// them in traversal order and each piece keeps the parent's direction
/// (Arr_circle_segment_traits_2.h, Make_x_monotone_2).  `isolated` receives the single point a
/// degenerate (zero-radius) circle produces instead of any arc.
void x_monotone_pieces(const CsCurve& cv, std::vector<CsXCurve>& pieces, bool& isolated,
                       CsPoint& isolated_point) {
  pieces.clear();
  isolated = false;
  std::vector<MxResult> res;
  auto mx = Types::traits().make_x_monotone_2_object();
  mx(cv, std::back_inserter(res));
  for (const MxResult& item : res) {
    if (const CsPoint* p = std::get_if<CsPoint>(&item)) {
      isolated = true;
      isolated_point = *p;
      pieces.clear();
      return;
    }
    pieces.push_back(*std::get_if<CsXCurve>(&item));
  }
}

/// Tight bounding box of one x-monotone curve.
void xcurve_box(const CsXCurve& c, double& xlo, double& ylo, double& xhi, double& yhi) {
  const std::pair<double, double> ixl = coord_interval(c.left().x());
  const std::pair<double, double> ixr = coord_interval(c.right().x());
  xlo = ixl.first;
  xhi = ixr.second;

  const std::pair<double, double> iys = coord_interval(c.source().y());
  const std::pair<double, double> iyt = coord_interval(c.target().y());
  ylo = std::min(iys.first, iyt.first);
  yhi = std::max(iys.second, iyt.second);

  if (!c.is_circular()) return;

  // An x-monotone arc lies entirely in the upper or the lower half of its supporting circle, so
  // its only interior y-extremum is the circle's top (resp. bottom) point at x == cx.  CGAL's own
  // _X_monotone_circle_segment_2::bbox() always widens y to that extremum (traits map §5, "SAFE
  // but LOOSE") and uses undirected to_double(); this computes the tight box exactly instead.
  const Circle_2 circ = c.supporting_circle();
  const CsPoint center(CoordNT(circ.center().x()), CoordNT(circ.center().y()));
  if (!c.is_in_x_range(center)) return;   // y is monotone along the arc: endpoints suffice
  const Rational cy = to_rational(circ.center().y());
  const Rational r2 = to_rational(circ.squared_radius());
  // point_position(p): SMALLER = p below the curve, LARGER = above (traits map §5).
  const CGAL::Comparison_result pos = c.point_position(center);
  if (pos == CGAL::SMALLER) {          // the centre is below => upper arc => top of the circle
    yhi = std::max(yhi, interval_of(SqrtExt{cy, Rational(1), r2}).second);
  } else if (pos == CGAL::LARGER) {    // lower arc => bottom of the circle
    ylo = std::min(ylo, interval_of(SqrtExt{cy, Rational(-1), r2}).first);
  }
}

/// The general Curve_2 that carries the same geometry as an x-monotone curve.
CsCurve xcurve_to_curve(const CsXCurve& x) {
  if (x.is_linear()) {
    // (Line_2, source, target): the endpoints may carry sqrt coordinates.  Preconditions
    // (endpoints on the line) hold by construction.
    return CsCurve(x.supporting_line(), x.source(), x.target());
  }
  // (Circle_2, source, target): the CIRCLE's orientation defines the arc's orientation, and
  // supporting_circle() returns Circle_2(center, sqr_r, orientation()) — so the arc's
  // orientation and its source->target direction are both preserved.
  // NB: the rebuilt Curve_2 has m_has_radius == false (there is no public accessor for the flag
  // and no ctor that takes circle + radius + endpoints), so a later re-subdivision computes the
  // vertical tangency points as sqrt extensions even when the radius happens to be rational.
  return CsCurve(x.supporting_circle(), x.source(), x.target());
}

}  // namespace

// ===========================================================================
// (2) CircleSegmentOps
// ===========================================================================
class CircleSegmentOps final : public KindOpsBase<CircleSegmentTypes> {
 public:
  bool has_polygon_set() const override { return true; }   // Gps_circle_segment_traits_2

  // ---------------------------------------------------------------- points
  Geom make_point(const Rational& x, const Rational& y) const override {
    return box_pt(CsPoint(CoordNT(to_epeck_ft(x)), CoordNT(to_epeck_ft(y))));
  }

  Geom make_point_3(const Rational&, const Rational&, const Rational&) const override {
    throw_error(ErrorCode::Unsupported,
                "circle_segment: points are planar (x, y); make_point_3 exists for the sphere kind only");
  }

  void point_approx(const Geom& p, double* xyz) const override {
    const CsPoint& pt = point(p);
    xyz[0] = coord_to_double(pt.x());
    xyz[1] = coord_to_double(pt.y());
  }

  void point_interval(const Geom& p, std::vector<std::pair<double, double>>& out) const override {
    const CsPoint& pt = point(p);
    out.clear();
    out.push_back(coord_interval(pt.x()));
    out.push_back(coord_interval(pt.y()));
  }

  bool point_is_rational(const Geom& p) const override {
    const CsPoint& pt = point(p);
    Rational x, y;
    // NOT `!is_extended()`: exact_coordinates_contract.md gotcha 3 (a perfect-square radicand
    // stays flagged as extended although the value is rational).
    return coord_is_rational(pt.x(), x) && coord_is_rational(pt.y(), y);
  }

  void point_exact_rational(const Geom& p, std::vector<Rational>& out) const override {
    const CsPoint& pt = point(p);
    Rational x, y;
    if (!coord_is_rational(pt.x(), x) || !coord_is_rational(pt.y(), y))
      throw_error(ErrorCode::NotRepresentable,
                  "circle_segment: the point " + point_str(pt) +
                      " has an irrational (sqrt-extension) coordinate; use point_exact() instead");
    out.clear();
    out.push_back(x);
    out.push_back(y);
  }

  void point_exact(const Geom& p, std::vector<Geom>& numbers) const override {
    const CsPoint& pt = point(p);
    numbers.clear();
    // box_sqrt_extension normalises a rational value (b == 0, root == 0 or a perfect square)
    // into a Rational box.
    numbers.push_back(box_sqrt_extension(pt.x()));
    numbers.push_back(box_sqrt_extension(pt.y()));
  }

  std::string point_repr(const Geom& p) const override { return point_str(point(p)); }

  Geom convert_point(const Geom& p) const override {
    if (p.kind == Kind::CircleSegment) { require_type(p, GeomType::Point, "point"); return p; }
    return box_pt(to_cs_point(p));
  }

  /// Override of the generic implementation: KindOpsBase would call the traits' Approximate_2
  /// point overload, which is `CGAL::to_double(Epeck::FT)` and is NOT correctly rounded
  /// (number_types_and_errors.md gotcha 2, CGAL_TRAPS_CHECKLIST "Numbers / coordinates").
  /// Routing through point_approx() keeps every double this kind hands out consistent.
  double approximate_coordinate(const Geom& p, int i) const override {
    if (i < 0 || i > 1) invalid("coordinate index out of range (planar points have x and y)");
    double xyz[3] = {0.0, 0.0, 0.0};
    point_approx(p, xyz);
    return xyz[i];
  }

  // ---------------------------------------------------------------- curves
  Geom to_curve(const Geom& xc) const override { return box_cv(xcurve_to_curve(xcurve(xc))); }

  Geom xcurve_source(const Geom& xc) const override { return box_pt(xcurve(xc).source()); }
  Geom xcurve_target(const Geom& xc) const override { return box_pt(xcurve(xc).target()); }
  /// Bounded traits: has_left()/has_right() are hard-coded true, so both ends always exist.
  bool xcurve_has_source(const Geom& xc) const override { xcurve(xc); return true; }
  bool xcurve_has_target(const Geom& xc) const override { xcurve(xc); return true; }

  bool curve_is_bounded(const Geom& c) const override {
    require_any_curve(c, Kind::CircleSegment, "curve");
    return true;   // circles, arcs and segments are all bounded
  }

  BBox curve_bbox(const Geom& c) const override {
    require_any_curve(c, Kind::CircleSegment, "curve");
    BBox b;
    b.dim = 2;
    b.lo[2] = b.hi[2] = 0.0;

    if (c.holds<CsXCurve>()) {
      xcurve_box(c.as<CsXCurve>(), b.lo[0], b.lo[1], b.hi[0], b.hi[1]);
      return b;
    }

    const CsCurve& cv = curve(c);
    if (cv.is_full()) {
      const Circle_2& circ = cv.supporting_circle();
      const Rational cx = to_rational(circ.center().x());
      const Rational cy = to_rational(circ.center().y());
      const Rational r2 = to_rational(circ.squared_radius());
      b.lo[0] = interval_of(SqrtExt{cx, Rational(-1), r2}).first;
      b.hi[0] = interval_of(SqrtExt{cx, Rational(1), r2}).second;
      b.lo[1] = interval_of(SqrtExt{cy, Rational(-1), r2}).first;
      b.hi[1] = interval_of(SqrtExt{cy, Rational(1), r2}).second;
      return b;
    }

    // A general arc may span a vertical tangency point: take the union over its x-monotone
    // pieces (each of which gets the tight box computed by xcurve_box).
    std::vector<CsXCurve> pieces;
    bool isolated = false;
    CsPoint ipt;
    x_monotone_pieces(cv, pieces, isolated, ipt);
    if (isolated) {
      const std::pair<double, double> ix = coord_interval(ipt.x());
      const std::pair<double, double> iy = coord_interval(ipt.y());
      b.lo[0] = ix.first; b.hi[0] = ix.second;
      b.lo[1] = iy.first; b.hi[1] = iy.second;
      return b;
    }
    if (pieces.empty()) invalid("curve_bbox: the curve has no x-monotone pieces");
    bool first = true;
    for (const CsXCurve& piece : pieces) {
      double xlo, ylo, xhi, yhi;
      xcurve_box(piece, xlo, ylo, xhi, yhi);
      if (first) {
        b.lo[0] = xlo; b.lo[1] = ylo; b.hi[0] = xhi; b.hi[1] = yhi;
        first = false;
      } else {
        b.lo[0] = std::min(b.lo[0], xlo);
        b.lo[1] = std::min(b.lo[1], ylo);
        b.hi[0] = std::max(b.hi[0], xhi);
        b.hi[1] = std::max(b.hi[1], yhi);
      }
    }
    return b;
  }

  /// Polyline approximation, source -> target.
  ///
  /// * `tolerance` is CGAL's ABSOLUTE `error`: the maximal perpendicular deviation of a chord
  ///   from the true arc (rendering_and_approximation.md gotcha 3).  It is validated/clamped
  ///   before CGAL sees it (gotcha 7: `error <= 0` recurses until SIGSEGV).
  /// * The output iterator is a std::back_insert_iterator, as gotcha 6 requires.
  /// * `clip` is ignored: every curve of this kind is bounded.
  /// * A general Curve_2 is subdivided first and the pieces are concatenated in traversal order,
  ///   dropping the duplicated junction points (gotcha 4: every piece emits both its endpoints).
  ///   A FULL CIRCLE therefore comes out as a closed ring: it starts and ends at the leftmost
  ///   point and runs in the circle's own orientation (CGAL emits the leftmost->rightmost piece
  ///   first).  A degenerate (zero-radius) circle yields its centre, once.
  void approximate(const Geom& c, double tolerance, const BBox* /*clip*/,
                   std::vector<double>& out) const override {
    require_any_curve(c, Kind::CircleSegment, "curve");
    const double error = checked_tolerance(tolerance);
    out.clear();

    std::vector<CsXCurve> pieces;
    if (c.holds<CsXCurve>()) {
      pieces.push_back(c.as<CsXCurve>());
    } else {
      bool isolated = false;
      CsPoint ipt;
      x_monotone_pieces(curve(c), pieces, isolated, ipt);
      if (isolated) {
        out.push_back(coord_to_double(ipt.x()));
        out.push_back(coord_to_double(ipt.y()));
        return;
      }
    }

    auto approx = traits().approximate_2_object();
    for (const CsXCurve& piece : pieces) {
      std::vector<CsTraits::Approximate_point_2> pts;
      // l2r == is_directed_right() turns CGAL's left->right / right->left choice into the
      // curve's own source -> target order (traits map §6, Approximate_2).
      approx(piece, error, std::back_inserter(pts), piece.is_directed_right());
      if (pts.empty()) continue;
      // CGAL's Approximate_2 converts with CGAL::to_double(Epeck::FT) on the lazy INTERVAL, which
      // is not correctly rounded (the rule this file opens with), so its endpoints disagreed with
      // point_approx() / Point.approx / Arrangement.vertex_coordinates() for the very same
      // point — measured on 27 % of random rational-endpoint arcs, which breaks any renderer
      // that stitches approximate_edges() to vertex_coordinates() by float identity.  Both
      // endpoints are always emitted (rendering_and_approximation.md gotcha 4), so overwrite
      // exactly those two with this kind's own correctly-rounded conversion; the interior samples
      // are pure rendering data and stay as CGAL produced them.
      const double sx = coord_to_double(piece.source().x());
      const double sy = coord_to_double(piece.source().y());
      const double tx = coord_to_double(piece.target().x());
      const double ty = coord_to_double(piece.target().y());
      for (std::size_t i = 0; i < pts.size(); ++i) {
        double x = pts[i].x();
        double y = pts[i].y();
        if (i == 0) { x = sx; y = sy; }
        else if (i + 1 == pts.size()) { x = tx; y = ty; }
        if (i == 0 && out.size() >= 2 && out[out.size() - 2] == x && out[out.size() - 1] == y)
          continue;   // shared junction point already emitted by the previous piece
        out.push_back(x);
        out.push_back(y);
      }
    }
  }

  std::string curve_repr(const Geom& c) const override {
    require_any_curve(c, Kind::CircleSegment, "curve");
    if (c.holds<CsXCurve>()) {
      const CsXCurve& x = c.as<CsXCurve>();
      if (x.is_linear())
        return "Segment(" + point_str(x.source()) + ", " + point_str(x.target()) + ")";
      const Circle_2 circ = x.supporting_circle();
      return std::string("CircularArc(center=") + rat_point_str(circ.center()) +
             ", squared_radius=" + rat_str(to_rational(circ.squared_radius())) +
             ", orientation=" + orient_str(x.orientation()) +
             ", source=" + point_str(x.source()) + ", target=" + point_str(x.target()) + ")";
    }
    const CsCurve& cv = curve(c);
    if (cv.is_linear())
      return "Segment(" + point_str(cv.source()) + ", " + point_str(cv.target()) + ")";
    const Circle_2& circ = cv.supporting_circle();
    const std::string head = std::string("center=") + rat_point_str(circ.center()) +
                             ", squared_radius=" + rat_str(to_rational(circ.squared_radius())) +
                             ", orientation=" + orient_str(cv.orientation());
    if (cv.is_full()) return "Circle(" + head + ")";
    return "CircularArc(" + head + ", source=" + point_str(cv.source()) +
           ", target=" + point_str(cv.target()) + ")";
  }

  /// Exact conversions into this kind.  Foreign geometry is unboxed through the raw CGAL types
  /// (their *_types.hpp are header-only), so this works whether or not the other kind's TU is
  /// linked.  Bezier / conic / sphere curves have no exact circle-segment image.
  void convert_curve(const Geom& c, std::vector<Geom>& out) const override {
    if (c.type != GeomType::Curve && c.type != GeomType::XCurve)
      invalid("convert_curve: the geometry is not a curve");
    out.clear();

    if (c.kind == Kind::CircleSegment) { out.push_back(c); return; }

    if (c.holds<SegmentTypes::Curve_2>()) {   // Arr_segment_2<Epeck>
      const SegmentTypes::Curve_2& s = c.as<SegmentTypes::Curve_2>();
      out.push_back(box_cv(CsCurve(s.source(), s.target())));
      return;
    }

    if (c.holds<LinearTypes::Curve_2>()) {    // Arr_linear_object_2<Epeck>
      const LinearTypes::Curve_2& l = c.as<LinearTypes::Curve_2>();
      if (!l.is_segment())
        throw_error(ErrorCode::NotRepresentable,
                    "circle_segment: only bounded linear objects convert to this kind; a "
                    "line or a ray is unbounded and has no circle-segment image");
      out.push_back(box_cv(CsCurve(l.source(), l.target())));
      return;
    }

    if (c.holds<PolylineTypes::Curve_2>()) {
      const PolylineTypes::Curve_2& p = c.as<PolylineTypes::Curve_2>();
      for (auto it = p.subcurves_begin(); it != p.subcurves_end(); ++it)
        out.push_back(box_cv(CsCurve(it->source(), it->target())));
      if (out.empty()) invalid("convert_curve: the polyline has no subcurves");
      return;
    }
    if (c.holds<PolylineTypes::X_monotone_curve_2>()) {
      const PolylineTypes::X_monotone_curve_2& p = c.as<PolylineTypes::X_monotone_curve_2>();
      for (auto it = p.subcurves_begin(); it != p.subcurves_end(); ++it)
        out.push_back(box_cv(CsCurve(it->source(), it->target())));
      if (out.empty()) invalid("convert_curve: the polyline has no subcurves");
      return;
    }

    throw_error(ErrorCode::NotRepresentable,
                std::string("circle_segment: curves of kind '") + kind_name(c.kind) +
                    "' have no exact circle-segment representation (only segment, bounded "
                    "linear objects and polylines convert)");
  }
};

// ===========================================================================
// (3) namespace arr2d::circle_segment — the free constructors/accessors of ops.hpp
// ===========================================================================
namespace circle_segment {

Geom make_point_sqrt(const SqrtExt& x, const SqrtExt& y) {
  return box_pt(CsPoint(sqrt_ext_to_coord(x), sqrt_ext_to_coord(y)));
}

void point_sqrt(const Geom& p, SqrtExt& x, SqrtExt& y) {
  require_point(p, Kind::CircleSegment);
  const CsPoint& pt = p.as<CsPoint>();
  x = coord_to_sqrt_ext(pt.x());
  y = coord_to_sqrt_ext(pt.y());
}

Geom make_full_circle(const Rational& cx, const Rational& cy, const Rational& squared_radius,
                      int orient) {
  const CGAL::Orientation o = orientation_arg(orient);
  if (sign_of(squared_radius) < 0)
    bad(ErrorCode::InvalidArgument, "the squared radius must be >= 0");
  // A zero squared radius is a legal degenerate circle: Make_x_monotone_2 turns it into a single
  // isolated point (Arr_circle_segment_traits_2.h, "Check the case of a degenerate circle").
  const Circle_2 circ(RatPoint(to_epeck_ft(cx), to_epeck_ft(cy)), to_epeck_ft(squared_radius), o);
  return box_cv(CsCurve(circ));
}

Geom make_full_circle_r(const Rational& cx, const Rational& cy, const Rational& radius,
                        int orient) {
  const CGAL::Orientation o = orientation_arg(orient);
  if (sign_of(radius) < 0)
    bad(ErrorCode::InvalidArgument,
        "the radius must be >= 0 (a negative radius mirrors CGAL's vertical tangency points)");
  // traits_circle_segment.md gotcha 4: this ctor records the RADIUS, so the vertical tangency
  // points stay rational (x0 +- r) instead of becoming sqrt extensions.
  return box_cv(CsCurve(RatPoint(to_epeck_ft(cx), to_epeck_ft(cy)), to_epeck_ft(radius), o));
}

namespace {
void check_arc_endpoints(const CsPoint& s, const CsPoint& t, const RatPoint& center,
                         const FT& sqr_r) {
  if (s.equals(t))
    bad(ErrorCode::InvalidArgument,
        "an arc needs two distinct endpoints (use make_full_circle for a whole circle)");
  require_on_circle(s, center, sqr_r, "source");
  require_on_circle(t, center, sqr_r, "target");
}
}  // namespace

Geom make_arc(const Rational& cx, const Rational& cy, const Rational& squared_radius, int orient,
              const Geom& source, const Geom& target) {
  const CGAL::Orientation o = orientation_arg(orient);
  if (sign_of(squared_radius) <= 0)
    bad(ErrorCode::InvalidArgument, "an arc needs a strictly positive squared radius");
  const RatPoint center(to_epeck_ft(cx), to_epeck_ft(cy));
  const FT sqr_r = to_epeck_ft(squared_radius);
  const CsPoint s = to_cs_point(source);
  const CsPoint t = to_cs_point(target);
  check_arc_endpoints(s, t, center, sqr_r);
  // (Circle_2, source, target): the circle's orientation is the arc's orientation.
  return box_cv(CsCurve(Circle_2(center, sqr_r, o), s, t));
}

Geom make_arc_r(const Rational& cx, const Rational& cy, const Rational& radius, int orient,
                const Geom& source, const Geom& target) {
  const CGAL::Orientation o = orientation_arg(orient);
  if (sign_of(radius) <= 0)
    bad(ErrorCode::InvalidArgument, "an arc needs a strictly positive radius");
  const RatPoint center(to_epeck_ft(cx), to_epeck_ft(cy));
  const FT r = to_epeck_ft(radius);
  const CsPoint s = to_cs_point(source);
  const CsPoint t = to_cs_point(target);
  check_arc_endpoints(s, t, center, r * r);
  // (center, radius, orientation, source, target): keeps m_has_radius == true (gotcha 4).
  return box_cv(CsCurve(center, r, o, s, t));
}

/// CGAL's `_Circle_segment_2` keeps `m_has_radius` / `m_radius` PROTECTED and offers no public
/// accessor (verified in Circle_segment_2.h:182-184 — the class has orientation(), is_linear(),
/// is_circular(), supporting_line(), supporting_circle(), is_full(), source(), target() and
/// vertical_tangency_points(), and nothing else).  Rather than degrade this query to a constant
/// `false`, it is answered from the geometry itself: the radius is reported as rational exactly
/// when sqrt(squared_radius) is rational, computed exactly by number_conv's rational_exact_sqrt.
/// That is a superset of "was built with the radius ctor" (a circle built from squared_radius 4
/// answers true with radius 2) and is what the Python `.radius` accessor needs.
bool has_rational_radius(const Geom& c) {
  require_any_curve(c, Kind::CircleSegment, "curve");
  Rational r2;
  if (c.holds<CsXCurve>()) {
    const CsXCurve& x = c.as<CsXCurve>();
    if (x.is_linear()) return false;
    r2 = to_rational(x.supporting_circle().squared_radius());
  } else {
    const CsCurve& cv = c.as<CsCurve>();
    if (cv.is_linear()) return false;
    r2 = to_rational(cv.supporting_circle().squared_radius());
  }
  Rational r;
  return rational_exact_sqrt(r2, r);
}

Rational radius(const Geom& c) {
  Rational r2;
  require_any_curve(c, Kind::CircleSegment, "curve");
  if (c.holds<CsXCurve>()) {
    const CsXCurve& x = c.as<CsXCurve>();
    if (x.is_linear()) bad(ErrorCode::InvalidArgument, "radius(): the curve is a line segment");
    r2 = to_rational(x.supporting_circle().squared_radius());
  } else {
    const CsCurve& cv = c.as<CsCurve>();
    if (cv.is_linear()) bad(ErrorCode::InvalidArgument, "radius(): the curve is a line segment");
    r2 = to_rational(cv.supporting_circle().squared_radius());
  }
  Rational r;
  if (!rational_exact_sqrt(r2, r))
    throw_error(ErrorCode::NotRepresentable,
                "circle_segment: the radius is irrational (squared radius " + rat_str(r2) +
                    "); use squared_radius() instead");
  return r;
}

Geom make_arc_three_points(const Geom& p, const Geom& q, const Geom& r) {
  const RatPoint p1 = to_rat_point(p, "make_arc_three_points");
  const RatPoint p2 = to_rat_point(q, "make_arc_three_points");
  const RatPoint p3 = to_rat_point(r, "make_arc_three_points");
  // CGAL precondition (Circle_segment_2.h): p1 and p3 are not equal.
  if (p1 == p3)
    bad(ErrorCode::InvalidArgument,
        "make_arc_three_points: the source and the target must be distinct");
  // Documented CGAL behaviour: three COLLINEAR points (including a repeated point) silently
  // build the SEGMENT p1 -> p3 instead of an arc.
  return box_cv(CsCurve(p1, p2, p3));
}

Geom make_segment(const Geom& p, const Geom& q) {
  const RatPoint a = to_rat_point(p, "make_segment");
  const RatPoint b = to_rat_point(q, "make_segment");
  // CGAL builds Line_2(a, b), which is degenerate for a == b.
  if (a == b) bad(ErrorCode::InvalidArgument, "make_segment: the two endpoints must be distinct");
  return box_cv(CsCurve(a, b));
}

Geom make_segment_on_line(const Rational& a, const Rational& b, const Rational& c,
                          const Geom& source, const Geom& target) {
  if (sign_of(a) == 0 && sign_of(b) == 0)
    bad(ErrorCode::InvalidArgument, "make_segment_on_line: a and b must not both be zero");
  const Line_2 line(to_epeck_ft(a), to_epeck_ft(b), to_epeck_ft(c));
  const CsPoint s = to_cs_point(source);
  const CsPoint t = to_cs_point(target);
  if (s.equals(t))
    bad(ErrorCode::InvalidArgument, "make_segment_on_line: the two endpoints must be distinct");
  require_on_line(s, line, "source");
  require_on_line(t, line, "target");
  return box_cv(CsCurve(line, s, t));
}

bool is_full(const Geom& c) {
  require_any_curve(c, Kind::CircleSegment, "curve");
  if (c.holds<CsXCurve>()) return false;   // an x-monotone piece is never a whole circle
  return c.as<CsCurve>().is_full();
}

bool is_linear(const Geom& c) {
  require_any_curve(c, Kind::CircleSegment, "curve");
  if (c.holds<CsXCurve>()) return c.as<CsXCurve>().is_linear();
  return c.as<CsCurve>().is_linear();
}

bool is_circular(const Geom& c) {
  require_any_curve(c, Kind::CircleSegment, "curve");
  if (c.holds<CsXCurve>()) return c.as<CsXCurve>().is_circular();
  return c.as<CsCurve>().is_circular();
}

int orientation(const Geom& c) {
  require_any_curve(c, Kind::CircleSegment, "curve");
  // CGAL::CLOCKWISE == -1, COLLINEAR == 0, COUNTERCLOCKWISE == +1 — the arr2d convention.
  if (c.holds<CsXCurve>()) return static_cast<int>(c.as<CsXCurve>().orientation());
  return static_cast<int>(c.as<CsCurve>().orientation());
}

void center(const Geom& c, Rational& cx, Rational& cy) {
  require_any_curve(c, Kind::CircleSegment, "curve");
  RatPoint p;
  if (c.holds<CsXCurve>()) {
    const CsXCurve& x = c.as<CsXCurve>();
    if (!x.is_circular()) bad(ErrorCode::InvalidArgument, "center(): the curve is a line segment");
    p = x.supporting_circle().center();
  } else {
    const CsCurve& cv = c.as<CsCurve>();
    if (!cv.is_circular()) bad(ErrorCode::InvalidArgument, "center(): the curve is a line segment");
    p = cv.supporting_circle().center();
  }
  cx = to_rational(p.x());
  cy = to_rational(p.y());
}

Rational squared_radius(const Geom& c) {
  require_any_curve(c, Kind::CircleSegment, "curve");
  if (c.holds<CsXCurve>()) {
    const CsXCurve& x = c.as<CsXCurve>();
    if (!x.is_circular())
      bad(ErrorCode::InvalidArgument, "squared_radius(): the curve is a line segment");
    return to_rational(x.supporting_circle().squared_radius());
  }
  const CsCurve& cv = c.as<CsCurve>();
  if (!cv.is_circular())
    bad(ErrorCode::InvalidArgument, "squared_radius(): the curve is a line segment");
  return to_rational(cv.supporting_circle().squared_radius());
}

void supporting_line(const Geom& c, Rational& a, Rational& b, Rational& c_) {
  require_any_curve(c, Kind::CircleSegment, "curve");
  Line_2 line;
  if (c.holds<CsXCurve>()) {
    const CsXCurve& x = c.as<CsXCurve>();
    if (!x.is_linear())
      bad(ErrorCode::InvalidArgument, "supporting_line(): the curve is a circular arc");
    // NB: the x-monotone class rebuilds Line_2(a(), b(), c()) from its own stored coefficients,
    // which are the input line's coefficients up to a positive scale factor.
    line = x.supporting_line();
  } else {
    const CsCurve& cv = c.as<CsCurve>();
    if (!cv.is_linear())
      bad(ErrorCode::InvalidArgument, "supporting_line(): the curve is a circular arc");
    line = cv.supporting_line();
  }
  a = to_rational(line.a());
  b = to_rational(line.b());
  c_ = to_rational(line.c());
}

}  // namespace circle_segment

}  // namespace arr2d

// ===========================================================================
// (4) Explicit instantiation of the generic arrangement implementation.
//     KindPolicy<CircleSegmentTypes> is already specialised in impl/arr_impl.hpp
//     (naive/simple/walk/trapezoid + ray shooting + the free CGAL::is_valid; landmarks is off
//     because the traits has no Construct_x_monotone_curve_2 — traits_circle_segment.md gotcha 3
//     — and triangulation is off because the traits exports no `Kernel` and the edges are not
//     straight), so nothing has to be added here.
// ===========================================================================
template class arr2d::ArrImpl<arr2d::CircleSegmentTypes>;

// ===========================================================================
// (5) Static registrar — the kind self-registers at load time so that any subset of kind TUs
//     can be linked (registry.hpp).
// ===========================================================================
namespace {
struct Registrar {
  Registrar() {
    // The KindOps singleton is heap-allocated and deliberately leaked: KindOpsBase owns a leaked
    // Arr_traits_adaptor_2 and must never be destroyed (STAGE1_NOTES.md, kind_ops_base
    // conventions).  Exactly one instance exists per process.
    static arr2d::CircleSegmentOps* ops = new arr2d::CircleSegmentOps();
    arr2d::register_kind(
        arr2d::Kind::CircleSegment,
        arr2d::KindEntry{ops,
                         [] { return std::unique_ptr<arr2d::ArrBase>(
                                  new arr2d::ArrImpl<arr2d::CircleSegmentTypes>()); },
                         &arr2d::make_polygon_set_circle_segment});
  }
} registrar;
}  // namespace
