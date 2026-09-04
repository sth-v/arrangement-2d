// arr2d — Kind::Sphere: geodesic arcs on the unit sphere.
//
//   Traits     CGAL::Arr_geodesic_arc_on_sphere_traits_2<Epeck>   (atan_x = -1, atan_y = 0:
//              the identification meridian is the half plane  y == 0 && x < 0)
//   Point_2    CGAL::Arr_extended_direction_3<Epeck>  — an UNNORMALISED direction (dx, dy, dz)
//              plus a Location_type (interior / south pole / identification / north pole)
//   Curve_2    Arr_geodesic_arc_on_sphere_3          (general geodesic arc, may be full)
//   Xcv        Arr_x_monotone_geodesic_arc_on_sphere_3
//   Topology   CGAL::Arr_spherical_topology_traits_2 — no fictitious cells, no unbounded face;
//              the "outer" face is the SPHERICAL face = the unique face with zero outer CCBs,
//              and it always contains the north pole (traits_geodesic_sphere.md §6.4).
//
// This TU contains
//   1. SphereTypes::traits()                       — the one process-wide traits instance,
//   2. class SphereOps : KindOpsBase<SphereTypes>  — every kind-specific KindOps virtual,
//   3. namespace arr2d::sphere                     — the free functions declared in ops.hpp,
//   4. template class ArrImpl<SphereTypes>         — the explicit instantiation,
//   5. a static registrar.
//
// CGAL 6.1 traps handled here (docs/dev/CGAL_TRAPS_CHECKLIST.md "Sphere kind" +
// docs/dev/cgal61_api/traits_geodesic_sphere.md §0 gotchas 1/2/6 and §10.0, and
// rendering_and_approximation.md gotchas 3/6/7/17).  Each work-around carries the gotcha
// number at the call site.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <CGAL/Interval_nt.h>

#include "arr2d/kinds/sphere_types.hpp"

#include "arr2d/impl/kind_ops_base.hpp"
#include "arr2d/impl/number_conv.hpp"
#include "arr2d/impl/arr_impl.hpp"
#include "arr2d/numbers.hpp"
#include "arr2d/ops.hpp"
#include "arr2d/registry.hpp"

namespace arr2d {

// ===========================================================================
// 1. The process-wide traits object
// ===========================================================================
//
// Lifetime: a function-local static, i.e. constructed on the first call and destroyed only
// during static destruction of this TU, AFTER main() has returned.  It therefore outlives
// every Point_2 / Curve_2 / X_monotone_curve_2 / ArrImpl<SphereTypes> that a program can
// still be using, which is exactly the requirement of
//   * Arrangement_on_surface_2(const Geometry_traits_2*)  — borrows, never owns
//     (traits_geodesic_sphere.md §6.2), and
//   * every traits functor, which stores `const Traits& m_traits` (§4.4).
// It is NEVER copied: the arrangements hold a pointer to it (ArrImpl's ctor passes
// &Types::traits()), and KindOpsBase's Arr_traits_adaptor_2 copy is a separate, leaked object.
//
// Unlike the Bezier / conic traits this one is safe in static storage: it derives from Epeck
// and holds no CORE::Expr, so the CORE MemoryPool teardown abort ("! blocks.empty()",
// CGAL/CORE/MemoryPool.h:125) cannot happen here.  (Its own function-local statics
// identification_xy() / identification_normal() hold Epeck directions only.)
const SphereTypes::Traits& SphereTypes::traits() {
  static const Traits t;
  return t;
}

namespace {

// ---------------------------------------------------------------------------
// Local shorthands
// ---------------------------------------------------------------------------
using Types = SphereTypes;
using Traits = Types::Traits;
using Kernel = Types::Kernel;
using FT = Types::FT;
using Point_2 = Types::Point_2;
using Curve_2 = Types::Curve_2;
using Xcv = Types::X_monotone_curve_2;
using Direction_3 = Kernel::Direction_3;
using Approx_point = Traits::Approximate_point_2;

inline const Traits& tr() { return Types::traits(); }

/// The exact (unnormalised) rational components of a sphere point / of a direction.
inline void dir_components(const Direction_3& d, Rational& x, Rational& y, Rational& z) {
  // Direction_3::dx()/dy()/dz() return `decltype(auto)`; with Epeck that is a (possibly
  // temporary) Lazy_exact_nt.  to_rational() goes through exact() — never CGAL::to_double,
  // which is not correctly rounded (number_types_and_errors.md gotcha #2).
  const FT fx = d.dx(), fy = d.dy(), fz = d.dz();
  x = to_rational(fx);
  y = to_rational(fy);
  z = to_rational(fz);
}

inline const Direction_3& as_direction(const Point_2& p) {
  // Point_2 (Arr_extended_direction_3) publicly derives from Kernel::Direction_3.
  return static_cast<const Direction_3&>(p);
}

[[noreturn]] inline void bad(const std::string& msg) {
  throw_error(ErrorCode::InvalidArgument, "sphere: " + msg);
}

/// A rational as "n" or "n/d" (canonical: d > 0, gcd 1).
std::string rat_str(const Rational& r) {
  std::string num, den;
  rational_to_strings(r, num, den);
  return (den == "1") ? num : (num + "/" + den);
}

std::string dir_str(const Direction_3& d) {
  Rational x, y, z;
  dir_components(d, x, y, z);
  return "(" + rat_str(x) + ", " + rat_str(y) + ", " + rat_str(z) + ")";
}

// ---------------------------------------------------------------------------
// Unboxing helpers
// ---------------------------------------------------------------------------
inline const Point_2& in_point(const Geom& g) {
  require_point(g, Kind::Sphere);
  return g.as<Point_2>();
}

/// XCurve box -> X_monotone_curve_2.  A Curve box is refused (Curve_2 and X_monotone_curve_2
/// are different C++ types for this kind).
inline const Xcv& in_xcurve(const Geom& g) {
  require_kind(g, Kind::Sphere, "curve");
  if (g.type == GeomType::Curve)
    throw_error(ErrorCode::NotXMonotone,
                "sphere: a general curve was given where an x-monotone curve is required "
                "(use to_x_monotone())");
  require_type(g, GeomType::XCurve, "curve");
  return g.as<Xcv>();
}

inline const Curve_2& in_curve(const Geom& g) {
  require_kind(g, Kind::Sphere, "curve");
  if (g.type == GeomType::XCurve)
    bad("an x-monotone curve was given where a general curve is required "
        "(convert it with to_curve() first)");
  require_type(g, GeomType::Curve, "curve");
  return g.as<Curve_2>();
}

/// Curve OR XCurve box -> the x-monotone base sub-object.  Curve_2 derives publicly from
/// X_monotone_curve_2 (traits_geodesic_sphere.md §3), so the flags source()/target()/normal()/
/// is_full()/is_vertical()/is_degenerate()/is_meridian() are readable through either box.
inline const Xcv& in_arc(const Geom& g) {
  require_any_curve(g, Kind::Sphere, "curve");
  if (g.holds<Xcv>()) return g.as<Xcv>();
  return static_cast<const Xcv&>(g.as<Curve_2>());
}

inline Geom box_point(const Point_2& p) { return make_geom(Kind::Sphere, GeomType::Point, p); }
inline Geom box_curve(const Curve_2& c) { return make_geom(Kind::Sphere, GeomType::Curve, c); }
inline Geom box_xcurve(const Xcv& c) { return make_geom(Kind::Sphere, GeomType::XCurve, c); }

// ---------------------------------------------------------------------------
// Construction helpers (every point goes through Construct_point_2 — the raw
// Arr_extended_direction_3(dir, location) ctor trusts the caller and silently corrupts the
// arrangement with a wrong Location_type; traits_geodesic_sphere.md §1 + CGAL_TRAPS "Sphere kind")
// ---------------------------------------------------------------------------
Point_2 make_sphere_point(const Rational& x, const Rational& y, const Rational& z) {
  if (rational_sign(x) == 0 && rational_sign(y) == 0 && rational_sign(z) == 0)
    bad("(0, 0, 0) is not a direction; a sphere point needs a non-zero (x, y, z)");
  // gotcha 6: Construct_point_2::operator() is NON-const -> the functor must live in a
  // non-const local (never `const auto`, never cached).
  auto ctr = tr().construct_point_2_object();
  return ctr(to_epeck_ft(x), to_epeck_ft(y), to_epeck_ft(z));
}

Point_2 point_from_direction(const Direction_3& d) {
  auto ctr = tr().construct_point_2_object();   // gotcha 6: non-const functor
  return ctr(d);
}

inline bool dir_is_zero(const Direction_3& d) {
  return sign_of(FT(d.dx())) == 0 && sign_of(FT(d.dy())) == 0 && sign_of(FT(d.dz())) == 0;
}

/// Exact 3-D dot product of two directions.
inline FT dot(const Direction_3& a, const Direction_3& b) {
  return CGAL::scalar_product(a.vector(), b.vector());
}

enum class PairRelation { Distinct, Equal, Antipodal };

PairRelation relate(const Point_2& p, const Point_2& q) {
  const Kernel& k = tr();   // the traits derives from the kernel (§4.2)
  auto eq3 = k.equal_3_object();
  const Direction_3& dp = as_direction(p);
  const Direction_3& dq = as_direction(q);
  if (eq3(dp, dq)) return PairRelation::Equal;
  auto opp = k.construct_opposite_direction_3_object();
  if (eq3(dp, opp(dq))) return PairRelation::Antipodal;
  return PairRelation::Distinct;
}

/// Preconditions of Construct_curve_2(p, q) / Construct_x_monotone_curve_2(p, q):
/// "the source and target cannot be equal" and "cannot be the opposite of each other"
/// (traits_geodesic_sphere.md §4.4).  CGAL only CGAL_precondition()s the antipodal case and
/// would compute a ZERO normal from the cross product, so both are checked here.
void require_minor_arc_endpoints(const Point_2& p, const Point_2& q) {
  switch (relate(p, q)) {
    case PairRelation::Equal:
      bad("the two endpoints of a geodesic arc must be distinct directions");
    case PairRelation::Antipodal:
      bad("the two endpoints are antipodal: infinitely many great circles pass through them; "
          "use make_arc_with_normal(p, q, normal) to pick one");
    case PairRelation::Distinct:
      return;
  }
}

// ---------------------------------------------------------------------------
// Approximation
// ---------------------------------------------------------------------------

/// Validate the user tolerance BEFORE it can reach CGAL's Approximate_2
/// (rendering_and_approximation.md gotcha 7): on the sphere
///   error <= 0 -> dtheta = 2*acos(1) = 0 -> num_segs = +inf -> the loop never terminates
///                 (verified: hangs allocating forever),
///   error >  2 -> acos(1 - error) is a domain error -> dtheta = NaN -> only the 2 endpoints.
double sane_tolerance(double tolerance) {
  if (!(tolerance > 0.0))
    bad("approximate(): the tolerance must be > 0 (CGAL's Approximate_2 loops forever at 0)");
  if (!(tolerance <= 2.0))
    bad("approximate(): on the unit sphere the tolerance is a chordal sagitta and must be <= 2 "
        "(CGAL computes acos(1 - tolerance) and returns NaN above 2)");
  if (tolerance < 1e-12) tolerance = 1e-12;   // ~2.2e6 chords for a full circle: the floor
  return tolerance;
}

inline void push_unit(std::vector<double>& out, const Approx_point& p) {
  out.push_back(p.dx());
  out.push_back(p.dy());
  out.push_back(p.dz());
}

/// Normalised double coordinates of an exact sphere point.  CGAL's Approximate_2 *point*
/// overload does NOT normalise (rendering_and_approximation.md gotcha 17: it returns (3,4,12)
/// for the direction (3,4,12)), so the normalisation is done here — and everything this TU
/// exposes as "approximate coordinates" is unit length.
void unit_approx(const Point_2& p, double* xyz) {
  const Direction_3& d = as_direction(p);
  const FT fx = d.dx(), fy = d.dy(), fz = d.dz();
  double x = to_double_correctly_rounded(fx);
  double y = to_double_correctly_rounded(fy);
  double z = to_double_correctly_rounded(fz);
  double n = std::sqrt(x * x + y * y + z * z);
  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(n) || n <= 0.0) {
    // The exact components over/underflow the double range (e.g. 10^400 or 10^-400).
    // Rescale EXACTLY by the largest |component| first; the scaled direction is the same
    // direction and every component then lies in [-1, 1].
    Rational rx = to_rational(fx), ry = to_rational(fy), rz = to_rational(fz);
    Rational ax = rational_sign(rx) < 0 ? -rx : rx;
    Rational ay = rational_sign(ry) < 0 ? -ry : ry;
    Rational az = rational_sign(rz) < 0 ? -rz : rz;
    Rational m = ax;
    if (rational_compare(ay, m) > 0) m = ay;
    if (rational_compare(az, m) > 0) m = az;
    if (rational_sign(m) == 0) { xyz[0] = xyz[1] = xyz[2] = 0.0; return; }   // unreachable
    x = to_double_correctly_rounded(Rational(rx / m));
    y = to_double_correctly_rounded(Rational(ry / m));
    z = to_double_correctly_rounded(Rational(rz / m));
    n = std::sqrt(x * x + y * y + z * z);
    if (!(n > 0.0)) { xyz[0] = xyz[1] = xyz[2] = 0.0; return; }
  }
  xyz[0] = x / n;
  xyz[1] = y / n;
  xyz[2] = z / n;
}

/// Approximate ONE x-monotone arc, following it from source to target as stored.
///
/// CGAL's Approximate_2 curve overload emits from source to target iff
/// `xcv.is_directed_right() == l2r` (traits_geodesic_sphere.md §4.8), so `l2r` must be set to
/// the arc's own direction flag to obtain the "as stored" order that ops.hpp promises.
/// `std::back_insert_iterator` is mandatory: the traits recurse/advance the iterator by value
/// (rendering_and_approximation.md gotcha 6).
void approx_xcurve(const Xcv& xc, double tolerance, std::vector<double>& out) {
  if (xc.is_degenerate()) {   // a degenerate arc is a single direction
    double xyz[3];
    unit_approx(xc.right(), xyz);
    out.push_back(xyz[0]);
    out.push_back(xyz[1]);
    out.push_back(xyz[2]);
    return;
  }
  std::vector<Approx_point> pts;
  auto approx = tr().approximate_2_object();
  approx(xc, tolerance, std::back_inserter(pts), xc.is_directed_right());
  out.reserve(out.size() + 3 * pts.size());
  for (const Approx_point& p : pts) push_unit(out, p);
}

/// True when the last point already written to `out` equals `xyz` (used to drop the duplicated
/// joint between consecutive x-monotone pieces: every whole-curve Approximate_2 emits both of
/// its endpoints — rendering_and_approximation.md gotcha 4).
bool same_last(const std::vector<double>& out, const double* xyz) {
  if (out.size() < 3) return false;
  const std::size_t n = out.size();
  for (int i = 0; i < 3; ++i)
    if (std::abs(out[n - 3 + i] - xyz[i]) > 1e-12) return false;
  return true;
}

}  // namespace

// ===========================================================================
// 2. SphereOps
// ===========================================================================
class SphereOps final : public KindOpsBase<SphereTypes> {
 public:
  // ---------------------------------------------------------------- points
  Geom make_point(const Rational& /*x*/, const Rational& /*y*/) const override {
    throw_error(ErrorCode::Unsupported,
                "sphere: points are 3-D directions on the unit sphere; use make_point_3(x, y, z) "
                "(the planar 2-coordinate constructor does not apply to this kind)");
  }

  Geom make_point_3(const Rational& x, const Rational& y, const Rational& z) const override {
    return box_point(make_sphere_point(x, y, z));
  }

  /// Unit-length doubles (see unit_approx: CGAL's point Approximate_2 is not normalised).
  void point_approx(const Geom& p, double* xyz) const override { unit_approx(in_point(p), xyz); }

  /// Certified intervals of the NORMALISED coordinates.
  ///
  /// The stored coordinates are exact rationals but the normalisation divides by
  /// sqrt(x^2+y^2+z^2), which is irrational in general — there is no exact rational answer.
  /// The enclosure is therefore computed with interval arithmetic (CGAL::Interval_nt<true>,
  /// i.e. the rounding-mode-protected flavour) from certified enclosures of the exact
  /// components.  The components are first rescaled EXACTLY by the largest |component| so that
  /// the squared norm lies in [1, 3] and the division can never hit an interval containing 0.
  void point_interval(const Geom& p, std::vector<std::pair<double, double>>& out) const override {
    const Point_2& pt = in_point(p);
    Rational rx, ry, rz;
    dir_components(as_direction(pt), rx, ry, rz);

    Rational ax = rational_sign(rx) < 0 ? -rx : rx;
    Rational ay = rational_sign(ry) < 0 ? -ry : ry;
    Rational az = rational_sign(rz) < 0 ? -rz : rz;
    Rational m = ax;
    if (rational_compare(ay, m) > 0) m = ay;
    if (rational_compare(az, m) > 0) m = az;

    out.clear();
    out.reserve(3);
    if (rational_sign(m) == 0) {   // unreachable: a direction is never (0,0,0)
      for (int i = 0; i < 3; ++i) out.emplace_back(-1.0, 1.0);
      return;
    }
    const Rational sx(rx / m), sy(ry / m), sz(rz / m);

    using I = CGAL::Interval_nt<true>;   // Protected == true: sets/restores the rounding mode
    const std::pair<double, double> bx = interval_of(sx);
    const std::pair<double, double> by = interval_of(sy);
    const std::pair<double, double> bz = interval_of(sz);
    I ix(bx.first, bx.second), iy(by.first, by.second), iz(bz.first, bz.second);
    I n2 = ix * ix + iy * iy + iz * iz;   // subset of [1, 3]
    I n = CGAL::sqrt(n2);
    const I comp[3] = {ix / n, iy / n, iz / n};
    for (int i = 0; i < 3; ++i) {
      double lo = comp[i].inf(), hi = comp[i].sup();
      if (!(lo >= -1.0)) lo = -1.0;
      if (!(hi <= 1.0)) hi = 1.0;
      out.emplace_back(lo, hi);
    }
  }

  /// Always true: a sphere point stores three EXACT RATIONAL direction components.  Note that
  /// the *normalised* coordinates (what point_approx / point_interval report) are irrational in
  /// general — point_exact_rational returns the unnormalised components, not the unit vector.
  bool point_is_rational(const Geom& /*p*/) const override { return true; }

  void point_exact_rational(const Geom& p, std::vector<Rational>& out) const override {
    Rational x, y, z;
    dir_components(as_direction(in_point(p)), x, y, z);
    out.clear();
    out.reserve(3);
    out.push_back(x);
    out.push_back(y);
    out.push_back(z);
  }

  void point_exact(const Geom& p, std::vector<Geom>& numbers) const override {
    std::vector<Rational> r;
    point_exact_rational(p, r);
    numbers.clear();
    numbers.reserve(3);
    for (const Rational& v : r) numbers.push_back(box_rational(v));
  }

  std::string point_repr(const Geom& p) const override {
    Rational x, y, z;
    dir_components(as_direction(in_point(p)), x, y, z);
    return "SpherePoint(" + rat_str(x) + ", " + rat_str(y) + ", " + rat_str(z) + ")";
  }

  /// A sphere point IS a 3-D direction; a planar point of any other kind has no z and no
  /// meaningful lift onto the sphere, so only the identity conversion exists.
  Geom convert_point(const Geom& p) const override {
    if (p.kind == Kind::Sphere) {
      require_point(p, Kind::Sphere);
      return p;
    }
    require_type(p, GeomType::Point, "point");
    throw_error(ErrorCode::KindMismatch,
                std::string("sphere: a planar point cannot become a sphere point (kind '") +
                    kind_name(p.kind) +
                    "' is 2-D, the sphere kind's points are 3-D directions); build one with "
                    "make_point_3(x, y, z)");
  }

  /// Consistency override (see point_approx): CGAL's Approximate_2(point, i) returns the RAW
  /// unnormalised component (rendering_and_approximation.md gotcha 17), which would disagree
  /// with point_approx(), with ArrBase::vertex_coordinates() and with ArrBase::bbox(), all of
  /// which go through point_approx.  This kind reports the normalised coordinate everywhere.
  double approximate_coordinate(const Geom& p, int i) const override {
    if (i < 0 || i > 2) bad("coordinate index out of range (0, 1 or 2)");
    double xyz[3];
    unit_approx(in_point(p), xyz);
    return xyz[i];
  }

  // ---------------------------------------------------------------- curves
  /// The general Curve_2 that carries the same geodesic as `xc`.  A Curve box is already a
  /// Curve_2 and is returned unchanged (the "identity" case of ops.hpp).
  Geom to_curve(const Geom& xc) const override {
    if (xc.kind == Kind::Sphere && xc.type == GeomType::Curve && xc.holds<Curve_2>()) return xc;
    const Xcv& c = in_xcurve(xc);
    auto ctr = tr().construct_curve_2_object();   // gotcha 6: non-const operator()
    if (c.is_full()) {
      // Cannot normally occur: full x-monotone arcs are disabled in CGAL 6.1
      // (CGAL_FULL_X_MONOTONE_GEODESIC_ARC_ON_SPHERE_IS_SUPPORTED is commented out).
      return box_curve(ctr(c.normal()));
    }
    if (c.is_degenerate())
      throw_error(ErrorCode::Unsupported,
                  "sphere: a degenerate arc (a single direction) has no Curve_2 representation");
    // Never use Arr_geodesic_arc_on_sphere_3(const Direction_3&) — it does not compile
    // (gotcha 2) — and never the (src, trg) overload here, which would silently pick the MINOR
    // arc: the (src, trg, normal) overload is the only one that preserves the supporting plane.
    return box_curve(ctr(c.source(), c.target(), c.normal()));
  }

  Geom xcurve_source(const Geom& xc) const override { return box_point(in_xcurve(xc).source()); }
  Geom xcurve_target(const Geom& xc) const override { return box_point(in_xcurve(xc).target()); }
  /// Every geodesic arc is bounded and both of its ends are real directions.
  bool xcurve_has_source(const Geom& xc) const override { in_xcurve(xc); return true; }
  bool xcurve_has_target(const Geom& xc) const override { in_xcurve(xc); return true; }

  /// 3-D axis-aligned box (BBox::dim == 3) of the polyline approximation, in unit-sphere
  /// coordinates.  Arr_x_monotone_geodesic_arc_on_sphere_3::bbox() is `#if 0`-ed out in CGAL 6.1
  /// (traits_geodesic_sphere.md §2), so there is nothing exact to call.
  BBox curve_bbox(const Geom& c) const override {
    std::vector<double> pts;
    approximate(c, kBboxTolerance, nullptr, pts);
    BBox b;
    b.dim = 3;
    if (pts.size() < 3) return b;
    for (int i = 0; i < 3; ++i) b.lo[i] = b.hi[i] = pts[std::size_t(i)];
    for (std::size_t i = 3; i + 3 <= pts.size(); i += 3)
      for (int k = 0; k < 3; ++k) {
        b.lo[k] = std::min(b.lo[k], pts[i + std::size_t(k)]);
        b.hi[k] = std::max(b.hi[k], pts[i + std::size_t(k)]);
      }
    for (int k = 0; k < 3; ++k) {   // the approximation is unit length up to rounding
      if (b.lo[k] < -1.0) b.lo[k] = -1.0;
      if (b.hi[k] > 1.0) b.hi[k] = 1.0;
    }
    return b;
  }

  /// Everything on a sphere is bounded (Arr_spherical_topology_traits_2::is_unbounded() is
  /// hard-coded false).
  bool curve_is_bounded(const Geom& c) const override { in_arc(c); return true; }

  /// Polyline approximation, 3 doubles (a UNIT direction) per point, from source to target as
  /// stored.  `clip` is ignored: no sphere curve is unbounded.
  ///
  /// A general Curve_2 is split with Make_x_monotone_2 first, because CGAL's Approximate_2
  /// takes an X_monotone_curve_2 and would divide by zero on a full circle (its source/target
  /// are default-constructed, i.e. both the north pole, so axis_y = normal x source = 0).
  /// Piece boundaries are emitted once, not twice (gotcha 4).
  void approximate(const Geom& c, double tolerance, const BBox* /*clip*/,
                   std::vector<double>& out) const override {
    const double tol = sane_tolerance(tolerance);   // BEFORE any call into CGAL (gotcha 7)
    out.clear();
    if (c.type == GeomType::XCurve) {
      approx_xcurve(in_xcurve(c), tol, out);
      return;
    }
    const Curve_2& cv = in_curve(c);
    std::vector<Make_x_monotone_result> pieces;
    auto mx = tr().make_x_monotone_2_object();
    mx(cv, std::back_inserter(pieces));
    for (const auto& item : pieces) {
      if (const Point_2* p = std::get_if<Point_2>(&item)) {   // degenerate curve
        double xyz[3];
        unit_approx(*p, xyz);
        if (same_last(out, xyz)) continue;
        out.push_back(xyz[0]);
        out.push_back(xyz[1]);
        out.push_back(xyz[2]);
        continue;
      }
      std::vector<double> part;
      approx_xcurve(*std::get_if<Xcv>(&item), tol, part);
      if (part.size() >= 3 && same_last(out, part.data()))
        out.insert(out.end(), part.begin() + 3, part.end());
      else
        out.insert(out.end(), part.begin(), part.end());
    }
  }

  std::string curve_repr(const Geom& c) const override {
    const Xcv& a = in_arc(c);
    if (a.is_full()) return "GreatCircle(normal=" + dir_str(a.normal()) + ")";
    return "GeodesicArc(source=" + dir_str(as_direction(a.source())) +
           ", target=" + dir_str(as_direction(a.target())) +
           ", normal=" + dir_str(a.normal()) + ")";
  }

  /// No other kind's curves live on a sphere.
  void convert_curve(const Geom& c, std::vector<Geom>& out) const override {
    if (c.kind == Kind::Sphere) {
      require_any_curve(c, Kind::Sphere, "curve");
      out.clear();
      out.push_back(c);
      return;
    }
    throw_error(ErrorCode::Unsupported,
                std::string("sphere: a curve of kind '") + kind_name(c.kind) +
                    "' cannot be converted to a geodesic arc (planar curves live in the plane, "
                    "sphere curves live on the unit sphere; there is no exact lift)");
  }

  bool has_polygon_set() const override { return false; }

  /// Validation override: the generic KindOpsBase version calls
  /// Construct_x_monotone_curve_2(p, q) directly.  For the sphere that functor builds the minor
  /// arc and classifies it WITHOUT checking x-monotonicity, so a minor arc that crosses the
  /// identification meridian would come back as a silently wrong "x-monotone" curve.  Route
  /// through arr2d::sphere::make_x_monotone_arc(), which asks Construct_curve_2 first.
  Geom construct_x_monotone_curve(const Geom& p, const Geom& q) const override {
    return sphere::make_x_monotone_arc(p, q);
  }

 private:
  /// Tolerance used by curve_bbox(): 1e-6 gives ~2200 chords for a full great circle and a
  /// box error below ~1.5e-3 of the true extent.
  static constexpr double kBboxTolerance = 1e-6;
};

// ===========================================================================
// 3. namespace arr2d::sphere — the free functions declared in ops.hpp
// ===========================================================================
namespace sphere {

Geom make_point(const Rational& x, const Rational& y, const Rational& z) {
  return box_point(make_sphere_point(x, y, z));
}

void point_xyz(const Geom& p, Rational& x, Rational& y, Rational& z) {
  // The STORED, unnormalised components (CGAL never normalises a direction — §1).
  dir_components(as_direction(in_point(p)), x, y, z);
}

int point_location(const Geom& p) {
  switch (in_point(p).location()) {
    case Point_2::NO_BOUNDARY_LOC: return NO_BOUNDARY;
    case Point_2::MIN_BOUNDARY_LOC: return MIN_BOUNDARY;   // south pole
    case Point_2::MID_BOUNDARY_LOC: return MID_BOUNDARY;   // identification meridian
    case Point_2::MAX_BOUNDARY_LOC: return MAX_BOUNDARY;   // north pole
  }
  return NO_BOUNDARY;
}

Geom make_arc(const Geom& p, const Geom& q) {
  const Point_2& a = in_point(p);
  const Point_2& b = in_point(q);
  require_minor_arc_endpoints(a, b);
  auto ctr = tr().construct_curve_2_object();   // gotcha 6: NON-const operator()
  return box_curve(ctr(a, b));
}

Geom make_arc_with_normal(const Geom& p, const Geom& q, const Geom& normal) {
  const Point_2& a = in_point(p);
  const Point_2& b = in_point(q);
  const Point_2& n = in_point(normal);
  const Direction_3& dn = as_direction(n);
  if (dir_is_zero(dn)) bad("the normal must be a non-zero direction");
  if (relate(a, b) == PairRelation::Equal)
    bad("the two endpoints of a geodesic arc must be distinct directions "
        "(use make_full_circle(normal) for a whole great circle)");
  // CGAL_precondition(has_on(normal, source)) / (normal, target) of Construct_curve_2's
  // (src, trg, normal) overload: both endpoints must lie in the plane through the origin whose
  // normal is `normal`.  Checked here so that misuse is Error(InvalidArgument).
  if (sign_of(FT(dot(dn, as_direction(a)))) != 0)
    bad("the source direction does not lie on the great circle with the given normal");
  if (sign_of(FT(dot(dn, as_direction(b)))) != 0)
    bad("the target direction does not lie on the great circle with the given normal");
  auto ctr = tr().construct_curve_2_object();   // gotcha 6
  return box_curve(ctr(a, b, dn));
}

Geom make_full_circle(const Geom& normal) {
  const Direction_3& dn = as_direction(in_point(normal));
  if (dir_is_zero(dn)) bad("the normal must be a non-zero direction");
  // Arr_geodesic_arc_on_sphere_3(const Direction_3&) does NOT COMPILE (gotcha 2:
  // `this->normal(normal)` calls the 0-argument accessor).  Construct_curve_2's (normal)
  // overload is the working route: it sets is_full = true, is_x_monotone = false and leaves
  // source/target default-constructed, which Make_x_monotone_2 recomputes.
  auto ctr = tr().construct_curve_2_object();
  return box_curve(ctr(dn));
}

Geom make_x_monotone_arc(const Geom& p, const Geom& q) {
  const Point_2& a = in_point(p);
  const Point_2& b = in_point(q);
  require_minor_arc_endpoints(a, b);
  // Construct_x_monotone_curve_2(p, q) has an *unchecked* precondition that the minor arc is
  // x-monotone: it fills in is_vertical / is_directed_right but never tests whether the arc
  // crosses the identification meridian, which would produce a silently invalid curve.
  // Construct_curve_2(p, q) computes exactly that bit, so ask it first.
  auto ccv = tr().construct_curve_2_object();   // gotcha 6
  const Curve_2 c = ccv(a, b);
  if (!c.is_x_monotone())
    bad("the minor arc between these two directions is not x-monotone (it crosses the "
        "identification meridian y == 0, x < 0, or contains a pole); use make_arc(p, q) and "
        "make_x_monotone()");
  auto ctr = tr().construct_x_monotone_curve_2_object();
  return box_xcurve(ctr(a, b));
}

bool is_full(const Geom& c) { return in_arc(c).is_full(); }
bool is_vertical(const Geom& c) { return in_arc(c).is_vertical(); }
bool is_meridian(const Geom& c) { return in_arc(c).is_meridian(); }
bool is_degenerate(const Geom& c) { return in_arc(c).is_degenerate(); }

Geom normal(const Geom& c) {
  // Boxed as a Sphere point so that the caller can read its exact rational components with the
  // ordinary point API.  Built through Construct_point_2 so that its Location_type is right.
  return box_point(point_from_direction(in_arc(c).normal()));
}

}  // namespace sphere

// ===========================================================================
// 4. Explicit instantiation of the generic arrangement implementation
// ===========================================================================
//
// KindPolicy<SphereTypes> is already specialised in impl/arr_impl.hpp (naive + landmarks point
// location only; no simple/walk/trapezoid/triangulation, no vertical ray shooting, member
// is_valid() instead of the free CGAL::is_valid) — a kind TU adds nothing here.
template class ArrImpl<SphereTypes>;

// ===========================================================================
// 5. Static registrar
// ===========================================================================
namespace {
struct Registrar {
  Registrar() {
    // Leaked on purpose (STAGE1_NOTES convention "leak the KindOps singleton"): the registry
    // keeps a raw pointer to it and must stay usable for the whole life of the process,
    // including during static destruction.  KindOpsBase already leaks its traits adaptor.
    static SphereOps* ops = new SphereOps();
    register_kind(Kind::Sphere,
                  KindEntry{ops,
                            [] { return std::unique_ptr<ArrBase>(new ArrImpl<SphereTypes>()); },
                            // The sphere has no Boolean set operations: CGAL's
                            // General_polygon_set_2 needs a planar Gps traits.  Reported by
                            // KindOps::has_polygon_set() == false and by
                            // registry kind_has_polygon_set(Kind::Sphere) == false.
                            nullptr});
  }
} registrar;
}  // namespace

}  // namespace arr2d
