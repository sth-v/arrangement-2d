// arr2d — Kind::Linear: CGAL::Arr_linear_traits_2<Epeck> (segments, rays and lines).
//
// This translation unit contains, per the kind-TU contract of docs/dev/DESIGN.md §1:
//   1. the definition of LinearTypes::traits()          (one process-wide instance),
//   2. class LinearOps : KindOpsBase<LinearTypes>       (every kind-specific virtual),
//   3. every free function of namespace arr2d::linear   (ops.hpp),
//   4. the explicit instantiation of ArrImpl<LinearTypes>,
//   5. the static registrar that publishes the kind in the registry.
//
// KindPolicy<LinearTypes> is already specialised in impl/arr_impl.hpp (naive / simple / walk
// supported; landmarks supported but gated at RUNTIME by ArrImpl::landmarks_usable() on
// "no unbounded edge in this arrangement"; trapezoid NOT — an attached RIC structure asserts when
// an edge incident to a vertex at infinity is removed; triangulation NOT — it calls point() on
// vertices at infinity, point_location_and_decomposition.md gotcha 9), so this TU adds no policy
// specialisation.
//
// ---------------------------------------------------------------------------------------------
// CGAL 6.1 traps handled here (docs/dev/CGAL_TRAPS_CHECKLIST.md); every workaround is also
// commented at its site:
//
//  * "Two functions of Arr_linear_traits_2 do not compile at all"
//    (traits_segment_linear_polyline.md gotcha 3):
//      - construct_opposite_2_object()(xcv) calls the non-existent get_pt()/get_ps().
//        KindOpsBase black-lists Kind::Linear (has_construct_opposite == false) so the broken
//        body is never instantiated; LinearOps::construct_opposite below re-implements it.
//      - construct_curve_2_object() does not compile either. Nothing in this TU, in
//        KindOpsBase or in ArrImpl ever calls it; Curve_2 objects are built directly from
//        Kernel Segment_2 / Ray_2 / Line_2, which is what the map prescribes.
//
//  * "Never use CGAL::to_double(Epeck::FT) ... not correctly rounded"
//    (number_types_and_errors.md gotcha 2): every double this TU produces
//    (point_approx, point_interval, curve_bbox, approximate) goes through
//    to_double_correctly_rounded / interval_of from impl/number_conv.hpp.
//    THE ONE EXCEPTION is the inherited KindOps::approximate_coordinate(), which ops.hpp
//    defines as "Approximate_2 on a point coordinate" and Python exposes as
//    Traits.approximate_point() — it is deliberately CGAL's own (interval-derived) functor.
//    Use point_approx() for anything user-facing.
//
//  * "Rendering / approximation: error <= 0 is catastrophic ... always use a
//    std::back_insert_iterator": Arr_linear_traits_2 has NO curve overload of Approximate_2 at
//    all (traits map §5 and rendering_and_approximation.md gotcha 2 — CGAL cannot even draw an
//    unbounded arrangement), so approximate() below is 100 % our own code and never calls a CGAL
//    Approximate_2. The tolerance is still validated (> 0, clamped to >= 1e-12) so that the
//    contract is identical across kinds.
//
//  * "Vertices at infinity / fictitious halfedges ... guard point() / curve()": handled by
//    ArrImpl (not this TU); what belongs here is that Construct_min/max_vertex_2 assert on an
//    end at infinity — KindOpsBase guards them with the adaptor's Is_closed_2, and
//    xcurve_source/target below guard them with has_source()/has_target().
//
//  * "Arr_linear_object_2::bbox() has precondition is_segment()" (traits map §6): curve_bbox()
//    below never calls it; it derives an exact rational box from left()/right() and the four
//    left/right_infinite_in_x/y() predicates and only then rounds to double.
//
//  * "Inserting an unbounded curve (line/ray) that OVERLAPS an existing edge with an unbounded
//    left end aborts (cv.has_left() precondition)": that trap lives on the *insertion* path
//    (ArrImpl / CGAL::insert), which this TU does not own. See the interface change request in
//    the report; test_kind_linear.cpp characterises the behaviour.
// ---------------------------------------------------------------------------------------------
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "arr2d/kinds/linear_types.hpp"

// Other kinds' concrete types, needed ONLY by convert_point / convert_curve (typedefs only; the
// arrangements of those kinds are never instantiated here and their traits() is never called).
#include "arr2d/kinds/circle_segment_types.hpp"
#include "arr2d/kinds/polyline_types.hpp"
#include "arr2d/kinds/segment_types.hpp"

#include "arr2d/impl/arr_impl.hpp"
#include "arr2d/impl/kind_ops_base.hpp"
#include "arr2d/impl/number_conv.hpp"
#include "arr2d/numbers.hpp"
#include "arr2d/ops.hpp"
#include "arr2d/registry.hpp"

namespace arr2d {

// ===========================================================================
// 1. The process-wide traits instance
// ===========================================================================
//
// LIFETIME: the returned object must outlive every Point_2 / Curve_2 / Arrangement of this kind,
// because (a) every functor obtained from it stores a reference or pointer back into it
// (traits_segment_linear_polyline.md gotcha 13) and (b) ArrImpl<LinearTypes> constructs its
// CGAL arrangement with &LinearTypes::traits() and CGAL keeps that pointer for the whole life of
// the arrangement and of every copy (arrangement_core.md gotcha 14).
//
// It is a function-local static POINTER to a heap instance that is intentionally never freed.
// The instance is therefore created on first use (thread-safe under C++11 magic statics) and
// destroyed never, so no static-destruction ordering can make it die before an arrangement,
// a curve or the KindOps singleton that reference it. Arr_linear_traits_2<Epeck> is stateless
// (`Arr_linear_traits_2() {}`, no data members — traits map §5), so the leak is one empty object.
// The object is never copied: KindOpsBase copies it once into its own Arr_traits_adaptor_2,
// which is a separate (also leaked) object; this one is only ever handed out by const reference.
const LinearTypes::Traits& LinearTypes::traits() {
  static const Traits* t = new Traits();
  return *t;
}

namespace {

// ---------------------------------------------------------------------------
// Local type aliases
// ---------------------------------------------------------------------------
using LT = LinearTypes;
using Kern = LT::Kernel;
using Point_2 = LT::Point_2;                 // Epeck::Point_2
using Curve_2 = LT::Curve_2;                 // Arr_linear_object_2<Epeck>  (== X_monotone_curve_2)
using Line_2 = Kern::Line_2;
using Ray_2 = Kern::Ray_2;
using Direction_2 = Kern::Direction_2;

constexpr double kInf = std::numeric_limits<double>::infinity();

[[noreturn]] void bad(const std::string& msg) {
  throw_error(ErrorCode::InvalidArgument, "linear: " + msg);
}

// ---------------------------------------------------------------------------
// Small exact helpers
// ---------------------------------------------------------------------------

Point_2 make_pt(const Rational& x, const Rational& y) {
  return Point_2(to_epeck_ft(x), to_epeck_ft(y));
}

std::string rat_str(const Rational& r) {
  std::string num, den;
  rational_to_strings(r, num, den);          // canonical: den > 0, gcd 1
  return den == "1" ? num : (num + "/" + den);
}

std::string pt_str(const Point_2& p) {
  return "(" + rat_str(to_rational(p.x())) + ", " + rat_str(to_rational(p.y())) + ")";
}

/// Exact coefficients of the supporting line: a*x + b*y + c = 0. Precondition: !is_degenerate().
void line_abc(const Curve_2& c, Rational& a, Rational& b, Rational& cc) {
  const Line_2& l = c.supporting_line();     // const& into the curve, no construction (map §5.3)
  a = to_rational(l.a());
  b = to_rational(l.b());
  cc = to_rational(l.c());
}

/// The direction of the supporting line, normalised to be lexicographically INCREASING
/// (dx > 0, or dx == 0 && dy > 0). Line_2::to_vector() is (b, -a) and its sign is not
/// normalised (rendering_and_approximation.md §5.5), hence the explicit flip.
void lex_dir(const Curve_2& c, Rational& dx, Rational& dy) {
  Rational a, b, cc;
  line_abc(c, a, b, cc);
  dx = b;
  dy = -a;
  const int sx = rational_sign(dx);
  if (sx < 0 || (sx == 0 && rational_sign(dy) < 0)) { dx = -dx; dy = -dy; }
}

/// The direction the curve is traversed in, as stored (source -> target).
void stored_dir(const Curve_2& c, Rational& dx, Rational& dy) {
  lex_dir(c, dx, dy);
  if (!c.is_directed_right()) { dx = -dx; dy = -dy; }
}

/// A rational point on the supporting line: the orthogonal projection of the origin,
/// (-a*c/(a^2+b^2), -b*c/(a^2+b^2)). Always defined ((a, b) != (0, 0) for a non-degenerate line).
void ref_point(const Curve_2& c, Rational& px, Rational& py) {
  Rational a, b, cc;
  line_abc(c, a, b, cc);
  const Rational den = a * a + b * b;
  px = -a * cc / den;
  py = -b * cc / den;
}

/// "source" / "target" in the sense of ops.hpp (the curve AS STORED), expressed through the
/// public left()/right() accessors:
///   has_left() == (is_directed_right ? has_source : has_target)   [traits map §6]
/// so the stored source is left() for a right-directed curve and right() otherwise.
/// This detour matters: Arr_linear_object_2::target() has the precondition
/// `!is_line() && !is_ray()`, which FIRES for a "flipped" ray (has_source == false,
/// has_target == true) — such rays are produced by Split_2 on a line.
bool has_stored_source(const Curve_2& c) { return c.is_directed_right() ? c.has_left() : c.has_right(); }
bool has_stored_target(const Curve_2& c) { return c.is_directed_right() ? c.has_right() : c.has_left(); }
const Point_2& stored_source(const Curve_2& c) { return c.is_directed_right() ? c.left() : c.right(); }
const Point_2& stored_target(const Curve_2& c) { return c.is_directed_right() ? c.right() : c.left(); }

// ---------------------------------------------------------------------------
// Exact rational bounding box (with infinite sides)
// ---------------------------------------------------------------------------
struct RatBox {
  bool xlo_inf = false, xhi_inf = false, ylo_inf = false, yhi_inf = false;
  Rational xlo, xhi, ylo, yhi;
};

RatBox exact_bbox(const Curve_2& c) {
  RatBox out;
  Rational px, py;
  ref_point(c, px, py);

  std::vector<Rational> xs, ys;
  // One end of the curve: either a finite point, or a point at infinity whose parameter space
  // tells us which coordinate runs off in which direction. When a coordinate's parameter space
  // is ARR_INTERIOR although the end is at infinity, that coordinate is CONSTANT along the
  // curve (vertical curve -> constant x, horizontal curve -> constant y), and the reference
  // point carries its exact value.
  auto add_end = [&](bool finite, const Point_2* p, CGAL::Arr_parameter_space ps_x,
                     CGAL::Arr_parameter_space ps_y) {
    if (finite) {
      xs.push_back(to_rational(p->x()));
      ys.push_back(to_rational(p->y()));
      return;
    }
    if (ps_x == CGAL::ARR_LEFT_BOUNDARY) out.xlo_inf = true;
    else if (ps_x == CGAL::ARR_RIGHT_BOUNDARY) out.xhi_inf = true;
    else xs.push_back(px);
    if (ps_y == CGAL::ARR_BOTTOM_BOUNDARY) out.ylo_inf = true;
    else if (ps_y == CGAL::ARR_TOP_BOUNDARY) out.yhi_inf = true;
    else ys.push_back(py);
  };
  add_end(c.has_left(), c.has_left() ? &c.left() : nullptr, c.left_infinite_in_x(), c.left_infinite_in_y());
  add_end(c.has_right(), c.has_right() ? &c.right() : nullptr, c.right_infinite_in_x(), c.right_infinite_in_y());

  // xs / ys can only be empty when BOTH ends are infinite in that coordinate, in which case the
  // two flags below are set and the values are never read; the fallback to the reference point
  // is defensive only.
  if (xs.empty()) xs.push_back(px);
  if (ys.empty()) ys.push_back(py);
  out.xlo = *std::min_element(xs.begin(), xs.end());
  out.xhi = *std::max_element(xs.begin(), xs.end());
  out.ylo = *std::min_element(ys.begin(), ys.end());
  out.yhi = *std::max_element(ys.begin(), ys.end());
  return out;
}

// ---------------------------------------------------------------------------
// Conversions from the other kinds (typedefs only; see the includes at the top)
// ---------------------------------------------------------------------------

/// A circle-segment coordinate (CGAL::Sqrt_extension over Epeck::FT) as an exact Rational.
/// Returns false when the value is genuinely irrational. box_sqrt_extension() NORMALISES a
/// rational-valued extension (b == 0, root == 0, or a perfect-square root — see
/// exact_coordinates_contract.md gotcha 3, "is_extended() == true may still be rational")
/// into a Rational box, so this is the exact test, not `!is_extended()`.
template <class CoordNT>
bool one_root_rational(const CoordNT& v, Rational& out) {
  const Geom n = box_sqrt_extension(v);
  if (!number_is_rational(n)) return false;
  out = number_to_rational(n);
  return true;
}

}  // namespace

// ===========================================================================
// 2. LinearOps
// ===========================================================================
class LinearOps final : public KindOpsBase<LinearTypes> {
 public:
  using Base = KindOpsBase<LinearTypes>;

  // ------------------------------------------------------------------ points
  Geom make_point(const Rational& x, const Rational& y) const override {
    return box_point(make_pt(x, y));
  }

  Geom make_point_3(const Rational&, const Rational&, const Rational&) const override {
    throw_error(ErrorCode::Unsupported,
                "linear: points are planar (x, y); make_point_3 exists only for the sphere kind");
  }

  void point_approx(const Geom& p, double* xyz) const override {
    const Point_2& q = point(p);
    // NOT CGAL::to_double / Approximate_2 (number_types_and_errors.md gotcha 2).
    xyz[0] = to_double_correctly_rounded(q.x());
    xyz[1] = to_double_correctly_rounded(q.y());
  }

  void point_interval(const Geom& p, std::vector<std::pair<double, double>>& out) const override {
    const Point_2& q = point(p);
    out.clear();
    out.push_back(interval_of(q.x()));
    out.push_back(interval_of(q.y()));
  }

  /// Epeck points are exact rationals by construction, always.
  bool point_is_rational(const Geom& p) const override { point(p); return true; }

  void point_exact_rational(const Geom& p, std::vector<Rational>& out) const override {
    const Point_2& q = point(p);
    out.clear();
    // to_rational(EpeckFT) goes through .exact() and copies the value out of the shared lazy
    // DAG node (number_conv.hpp; exact_coordinates_contract.md gotcha 10).
    out.push_back(to_rational(q.x()));
    out.push_back(to_rational(q.y()));
  }

  void point_exact(const Geom& p, std::vector<Geom>& numbers) const override {
    const Point_2& q = point(p);
    numbers.clear();
    numbers.push_back(box_epeck_ft(q.x()));
    numbers.push_back(box_epeck_ft(q.y()));
  }

  std::string point_repr(const Geom& p) const override {
    return "Point" + pt_str(point(p));
  }

  Geom convert_point(const Geom& p) const override {
    require_type(p, GeomType::Point, "point");
    if (p.kind == Kind::Linear) return p;
    // Fast path: segment / polyline points ARE Epeck::Point_2, the very same C++ type. This
    // also works when the source kind's TU is not linked into the binary.
    if (p.holds<Point_2>()) return box_point(p.as<Point_2>());
    // Circle-segment points are handled natively for the same reason (and because their
    // coordinates need the sqrt-extension rationality test, not a plain cast).
    if (p.holds<CircleSegmentTypes::Point_2>()) {
      const CircleSegmentTypes::Point_2& q = p.as<CircleSegmentTypes::Point_2>();
      Rational x, y;
      if (!one_root_rational(q.x(), x) || !one_root_rational(q.y(), y))
        throw_error(ErrorCode::NotRepresentable,
                    "linear: the circle-segment point has a square-root coordinate, which the "
                    "'linear' kind (rational points) cannot represent exactly");
      return box_point(make_pt(x, y));
    }
    // Generic path through the source kind's own KindOps (registry).  Throws
    // Error(Unsupported, "kind not available") if that kind is not compiled in.
    const KindOps& src = arr2d::ops(p.kind);
    if (src.dimension() != 2)
      throw_error(ErrorCode::KindMismatch,
                  std::string("linear: cannot convert a ") + std::to_string(src.dimension()) +
                      "D point of kind '" + kind_name(p.kind) + "' into the planar 'linear' kind");
    if (!src.point_is_rational(p))
      throw_error(ErrorCode::NotRepresentable,
                  std::string("linear: the point of kind '") + kind_name(p.kind) +
                      "' has non-rational coordinates and cannot be represented exactly by the "
                      "'linear' kind (its points are rational)");
    std::vector<Rational> xy;
    src.point_exact_rational(p, xy);
    if (xy.size() < 2) bad("convert_point: the source kind reported fewer than 2 coordinates");
    return box_point(make_pt(xy[0], xy[1]));
  }

  // ------------------------------------------------------------------ curves
  /// Curve_2 and X_monotone_curve_2 are the SAME C++ type for this kind, so this is a re-box.
  Geom to_curve(const Geom& xc) const override {
    return make_geom(Kind::Linear, GeomType::Curve, xcurve(xc));
  }

  Geom xcurve_source(const Geom& xc) const override {
    const Curve_2& c = require_curve(xc);
    if (!has_stored_source(c))
      throw_error(ErrorCode::Unsupported,
                  "linear: this curve's source end lies at infinity (a line has no source; a "
                  "'flipped' ray has none either) — check xcurve_has_source() first");
    return box_point(stored_source(c));
  }

  Geom xcurve_target(const Geom& xc) const override {
    const Curve_2& c = require_curve(xc);
    if (!has_stored_target(c))
      throw_error(ErrorCode::Unsupported,
                  "linear: this curve's target end lies at infinity (a ray runs to infinity and a "
                  "line has neither end) — check xcurve_has_target() first");
    return box_point(stored_target(c));
  }

  bool xcurve_has_source(const Geom& xc) const override { return has_stored_source(require_curve(xc)); }
  bool xcurve_has_target(const Geom& xc) const override { return has_stored_target(require_curve(xc)); }

  BBox curve_bbox(const Geom& c) const override {
    const Curve_2& cv = require_curve(c);
    const RatBox rb = exact_bbox(cv);
    BBox b;
    b.dim = 2;
    // Round OUTWARD, as every other bounded kind does (kind_segment.cpp uses interval_of the same
    // way, conic widens by 4 ulps, the polyline kind inherits CGAL's to_interval-based bbox).
    // to_double_correctly_rounded() rounds to NEAREST, so an upper bound whose exact value is not
    // a double came back rounded DOWN and the box then EXCLUDED part of its own curve — which
    // matters because approximate(tolerance, bbox=...) clips unbounded linear curves against it.
    b.lo[0] = rb.xlo_inf ? -kInf : interval_of(rb.xlo).first;
    b.hi[0] = rb.xhi_inf ? kInf : interval_of(rb.xhi).second;
    b.lo[1] = rb.ylo_inf ? -kInf : interval_of(rb.ylo).first;
    b.hi[1] = rb.yhi_inf ? kInf : interval_of(rb.yhi).second;
    return b;
  }

  bool curve_is_bounded(const Geom& c) const override { return require_curve(c).is_segment(); }

  /// Polyline approximation. A linear object is straight, so the answer is always the two ends of
  /// the (clipped) chord and it is EXACT up to the final double rounding — `tolerance` only has to
  /// be a legal value. Unbounded curves need `clip`.
  ///
  /// Output order follows the curve from its source to its target AS STORED (ops.hpp).
  /// A clip box that misses the curve yields an EMPTY polyline (0 points).
  void approximate(const Geom& c, double tolerance, const BBox* clip,
                   std::vector<double>& out) const override {
    const Curve_2& cv = require_curve(c);
    // Tolerance validation BEFORE any approximation work (CGAL_TRAPS_CHECKLIST: `error <= 0`
    // makes the subdividing traits recurse forever / segfault). The linear traits has no curve
    // overload of Approximate_2 at all, so nothing of CGAL's is called here — the check keeps
    // the contract uniform across kinds.
    double eps = tolerance;
    if (!(eps > 0.0)) bad("approximate: tolerance must be a positive number");
    if (eps < 1e-12) eps = 1e-12;   // uniform clamp; a straight chord is exact, so for this
    (void)eps;                      // kind the (validated) tolerance has no further effect.

    out.clear();
    if (cv.is_segment()) {
      // Bounded: the exact endpoints, source first. `clip` is ignored for bounded curves
      // (ops.hpp: it exists "to clip unbounded curves"), which is also what makes
      // KindOpsBase::approximate_length() work with clip == nullptr.
      emit(stored_source(cv), out);
      emit(stored_target(cv), out);
      return;
    }
    if (clip == nullptr)
      bad("clip bbox required for unbounded curves");
    if (clip->dim < 2) bad("approximate: the clip bbox must be 2-dimensional");
    for (int i = 0; i < 2; ++i)
      if (!std::isfinite(clip->lo[i]) || !std::isfinite(clip->hi[i]))
        bad("approximate: the clip bbox must be finite");
    if (clip->lo[0] > clip->hi[0] || clip->lo[1] > clip->hi[1])
      bad("approximate: the clip bbox is empty (lo > hi)");

    // ---- exact rational clipping (Liang-Barsky in Rational, not in double) ----
    const Rational bxlo = rational_from_double(clip->lo[0]);
    const Rational bxhi = rational_from_double(clip->hi[0]);
    const Rational bylo = rational_from_double(clip->lo[1]);
    const Rational byhi = rational_from_double(clip->hi[1]);

    Rational px, py, dx, dy;
    ref_point(cv, px, py);
    lex_dir(cv, dx, dy);      // t increases lexicographically along the curve

    // Parameter of a point of the curve: P(t) = ref + t * d.
    const bool by_x = (rational_sign(dx) != 0);
    auto param = [&](const Point_2& q) -> Rational {
      return by_x ? (to_rational(q.x()) - px) / dx : (to_rational(q.y()) - py) / dy;
    };

    bool has_lo = cv.has_left(), has_hi = cv.has_right();
    Rational tlo, thi;
    if (has_lo) tlo = param(cv.left());
    if (has_hi) thi = param(cv.right());

    // Intersect with one axis slab. Returns false when the curve misses the slab entirely.
    auto slab = [&](const Rational& p0, const Rational& d, const Rational& lo, const Rational& hi) {
      const int sd = rational_sign(d);
      if (sd == 0) return rational_compare(p0, lo) >= 0 && rational_compare(p0, hi) <= 0;
      Rational a = (lo - p0) / d, b = (hi - p0) / d;
      if (sd < 0) std::swap(a, b);
      if (!has_lo || rational_compare(a, tlo) > 0) { tlo = a; has_lo = true; }
      if (!has_hi || rational_compare(b, thi) < 0) { thi = b; has_hi = true; }
      return true;
    };
    if (!slab(px, dx, bxlo, bxhi)) return;   // empty
    if (!slab(py, dy, bylo, byhi)) return;   // empty
    // (dx, dy) != (0, 0) and the box is bounded, so both bounds are finite by now.
    if (!has_lo || !has_hi || rational_compare(tlo, thi) > 0) return;   // empty

    const Rational ax = px + tlo * dx, ay = py + tlo * dy;
    const Rational bx2 = px + thi * dx, by2 = py + thi * dy;
    if (cv.is_directed_right()) { emit(ax, ay, out); emit(bx2, by2, out); }
    else { emit(bx2, by2, out); emit(ax, ay, out); }
  }

  std::string curve_repr(const Geom& c) const override {
    const Curve_2& cv = curve_or_xcurve(c);
    if (cv.is_degenerate()) return "LinearCurve(degenerate)";
    if (cv.is_segment())
      return "Segment(" + pt_str(stored_source(cv)) + ", " + pt_str(stored_target(cv)) + ")";
    Rational dx, dy;
    stored_dir(cv, dx, dy);
    if (cv.is_ray()) {
      // A "flipped" ray (produced by Split_2 on a line) has NO stored source: it comes in from
      // infinity and ENDS at its finite point. Reaching for left()/right() blindly would trip
      // their has_left()/has_right() preconditions, so pick the end that exists.
      if (has_stored_source(cv))
        return "Ray(" + pt_str(stored_source(cv)) + ", direction=(" + rat_str(dx) + ", " +
               rat_str(dy) + "))";
      return "Ray(target=" + pt_str(stored_target(cv)) + ", direction=(" + rat_str(dx) + ", " +
             rat_str(dy) + "))";
    }
    Rational a, b, cc;
    line_abc(cv, a, b, cc);
    return "Line(a=" + rat_str(a) + ", b=" + rat_str(b) + ", c=" + rat_str(cc) + ")";
  }

  /// Exact conversions into the linear kind (see the matrix in DESIGN.md):
  ///   linear         -> identity
  ///   segment        -> one segment
  ///   polyline       -> one segment per subcurve (in order)
  ///   circle_segment -> one segment, only for a LINEAR piece with rational endpoints
  ///   bezier / conic / sphere -> Error(Unsupported)
  void convert_curve(const Geom& c, std::vector<Geom>& out) const override {
    if (c.type != GeomType::Curve && c.type != GeomType::XCurve)
      bad("convert_curve: the geometry is not a curve");
    out.clear();
    switch (c.kind) {
      case Kind::Linear:
        out.push_back(c);
        return;
      case Kind::Segment: {
        const SegmentTypes::Curve_2& s = c.as<SegmentTypes::Curve_2>();
        // Arr_segment_2's source()/target() are the stored (directed) endpoints.
        out.push_back(box_xcurve(Curve_2(s.source(), s.target())));
        return;
      }
      case Kind::Polyline:
        convert_polyline(c, out);
        return;
      case Kind::CircleSegment:
        convert_circle_segment(c, out);
        return;
      default:
        throw_error(ErrorCode::Unsupported,
                    std::string("linear: a curve of kind '") + kind_name(c.kind) +
                        "' is not a straight segment, ray or line and cannot be converted to the "
                        "'linear' kind");
    }
  }

  bool has_polygon_set() const override { return false; }

  // ------------------------------------------------------- generic overrides
  /// MANDATORY override: KindOpsBase black-lists Kind::Linear because
  /// Arr_linear_traits_2::Construct_opposite_2::operator() does not compile — it calls
  /// Arr_linear_object_2::get_pt() / get_ps(), which do not exist
  /// (traits_segment_linear_polyline.md gotcha 3, verified). The black-list (not just this
  /// override) is what keeps the broken body out of the build: virtual members of a class
  /// template are instantiated together with the vtable.
  Geom construct_opposite(const Geom& xc) const override {
    const Curve_2& c = require_curve(xc);
    if (c.is_segment())
      return box_xcurve(Curve_2(stored_target(c), stored_source(c)));   // flipped segment
    if (c.is_line())
      // Line_2::opposite() is the same line traversed the other way.
      return box_xcurve(Curve_2(c.supporting_line().opposite()));
    throw_error(ErrorCode::Unsupported,
                "linear: a ray cannot be reversed (Arr_linear_object_2 always stores a ray "
                "starting at its finite end; the reversed object is not representable)");
  }

 private:
  // ---- unboxing helpers -------------------------------------------------
  /// An x-monotone curve of this kind. Every Arr_linear_object_2 is x-monotone, and Curve_2 and
  /// X_monotone_curve_2 are the same C++ type, so KindOpsBase::xcurve() accepts both box types.
  /// A DEGENERATE linear object (the default-constructed Arr_linear_object_2, which represents a
  /// single point) is rejected here: nearly every accessor of the curve class asserts on it
  /// (supporting_line(), is_vertical(), left(), right(), ...), so it must never reach them.
  static const Curve_2& require_curve(const Geom& g) {
    const Curve_2& c = Base::xcurve(g);
    if (c.is_degenerate())
      bad("the linear object is degenerate (a single point), not a segment, ray or line");
    return c;
  }
  /// Unchecked (degenerate objects allowed): only curve_repr() uses it, so that printing a
  /// degenerate box says so instead of throwing.
  const Curve_2& curve_or_xcurve(const Geom& g) const { return Base::xcurve(g); }

  static void emit(const Point_2& p, std::vector<double>& out) {
    out.push_back(to_double_correctly_rounded(p.x()));
    out.push_back(to_double_correctly_rounded(p.y()));
  }
  static void emit(const Rational& x, const Rational& y, std::vector<double>& out) {
    out.push_back(to_double_correctly_rounded(x));
    out.push_back(to_double_correctly_rounded(y));
  }

  // ---- convert_curve helpers --------------------------------------------
  static void convert_polyline(const Geom& c, std::vector<Geom>& out) {
    // Arr_polyline_traits_2's Curve_2 / X_monotone_curve_2 are internal::Polycurve_2 /
    // internal::X_monotone_polycurve_2 (traits map gotcha 1); both expose subcurves_begin/end
    // and every subcurve is an Arr_segment_2<Epeck>. Never include Polyline_2.h (deprecated).
    if (c.holds<PolylineTypes::X_monotone_curve_2>()) {
      append_subcurves(c.as<PolylineTypes::X_monotone_curve_2>(), out);
      return;
    }
    if (c.holds<PolylineTypes::Curve_2>()) {
      append_subcurves(c.as<PolylineTypes::Curve_2>(), out);
      return;
    }
    bad("convert_curve: the polyline geometry holds an unexpected C++ type");
  }

  template <class Polycurve>
  static void append_subcurves(const Polycurve& pc, std::vector<Geom>& out) {
    if (pc.number_of_subcurves() == 0)
      bad("convert_curve: the polyline is empty");
    for (auto it = pc.subcurves_begin(); it != pc.subcurves_end(); ++it)
      out.push_back(box_xcurve(Curve_2(it->source(), it->target())));
  }

  static void convert_circle_segment(const Geom& c, std::vector<Geom>& out) {
    if (c.holds<CircleSegmentTypes::Curve_2>()) {
      const CircleSegmentTypes::Curve_2& cs = c.as<CircleSegmentTypes::Curve_2>();
      if (!cs.is_linear())
        throw_error(ErrorCode::Unsupported,
                    "linear: only a LINEAR circle-segment curve (a straight piece) can be "
                    "converted to the 'linear' kind; this one is a circular arc or a full circle");
      if (cs.is_full()) bad("convert_curve: a full circle has no endpoints");
      out.push_back(box_xcurve(from_one_root(cs.source(), cs.target())));
      return;
    }
    if (c.holds<CircleSegmentTypes::X_monotone_curve_2>()) {
      const CircleSegmentTypes::X_monotone_curve_2& cs = c.as<CircleSegmentTypes::X_monotone_curve_2>();
      if (!cs.is_linear())
        throw_error(ErrorCode::Unsupported,
                    "linear: only a LINEAR circle-segment curve (a straight piece) can be "
                    "converted to the 'linear' kind; this one is a circular arc");
      out.push_back(box_xcurve(from_one_root(cs.source(), cs.target())));
      return;
    }
    bad("convert_curve: the circle-segment geometry holds an unexpected C++ type");
  }

  /// Two _One_root_point_2 endpoints -> a linear segment; both must be rational.
  template <class OneRootPoint>
  static Curve_2 from_one_root(const OneRootPoint& s, const OneRootPoint& t) {
    Rational sx, sy, tx, ty;
    if (!one_root_rational(s.x(), sx) || !one_root_rational(s.y(), sy) ||
        !one_root_rational(t.x(), tx) || !one_root_rational(t.y(), ty))
      throw_error(ErrorCode::NotRepresentable,
                  "linear: the circle-segment segment has an endpoint with a square-root "
                  "coordinate, which the 'linear' kind (rational points) cannot represent");
    const Point_2 p = make_pt(sx, sy), q = make_pt(tx, ty);
    if (p == q) bad("convert_curve: the circle-segment segment is degenerate (equal endpoints)");
    return Curve_2(p, q);
  }
};

// ===========================================================================
// The per-kind KindOps singleton
// ===========================================================================
namespace {
/// Leaked on purpose: KindOpsBase owns a deliberately leaked Arr_traits_adaptor_2 and is
/// documented as "construct exactly one instance per kind and never destroy it"
/// (STAGE1_NOTES.md, kind_ops_base conventions). It also keeps a pointer into
/// LinearTypes::traits(), which is itself never destroyed.
const LinearOps& linear_ops() {
  static const LinearOps* p = new LinearOps();
  return *p;
}

/// A Linear-kind point from a Geom of any kind (ops.hpp: "points given to planar constructors
/// may be of ANY kind with rational coordinates ... they are converted").
Point_2 in_point(const Geom& g) {
  const Geom p = linear_ops().convert_point(g);
  return p.as<Point_2>();
}
}  // namespace

// ===========================================================================
// 3. Free functions — namespace arr2d::linear (declared in ops.hpp)
// ===========================================================================
namespace linear {

Geom make_segment(const Geom& p, const Geom& q) {
  const Point_2 a = in_point(p), b = in_point(q);
  // CGAL precondition of Arr_linear_object_2(s, t): "Cannot construct a degenerate segment."
  // Checked here so that misuse is Error(InvalidArgument), not a CGAL assertion.
  if (a == b) bad("make_segment: the two endpoints must be distinct");
  return make_geom(Kind::Linear, GeomType::XCurve, Curve_2(a, b));
}

Geom make_ray(const Geom& source, const Geom& towards) {
  const Point_2 s = in_point(source), t = in_point(towards);
  if (s == t) bad("make_ray: the source and the point the ray passes through must be distinct");
  return make_geom(Kind::Linear, GeomType::XCurve, Curve_2(Ray_2(s, t)));
}

Geom make_ray_direction(const Geom& source, const Rational& dx, const Rational& dy) {
  if (rational_sign(dx) == 0 && rational_sign(dy) == 0)
    bad("make_ray_direction: the direction must not be the zero vector");
  const Point_2 s = in_point(source);
  return make_geom(Kind::Linear, GeomType::XCurve,
                   Curve_2(Ray_2(s, Direction_2(to_epeck_ft(dx), to_epeck_ft(dy)))));
}

Geom make_line(const Geom& p, const Geom& q) {
  const Point_2 a = in_point(p), b = in_point(q);
  if (a == b) bad("make_line: the two points must be distinct");
  return make_geom(Kind::Linear, GeomType::XCurve, Curve_2(Line_2(a, b)));
}

Geom make_line_coefficients(const Rational& a, const Rational& b, const Rational& c) {
  if (rational_sign(a) == 0 && rational_sign(b) == 0)
    bad("make_line_coefficients: a and b must not both be zero (a*x + b*y + c = 0)");
  return make_geom(Kind::Linear, GeomType::XCurve,
                   Curve_2(Line_2(to_epeck_ft(a), to_epeck_ft(b), to_epeck_ft(c))));
}

int which(const Geom& c) {
  const Curve_2& cv = KindOpsBase<LinearTypes>::xcurve(c);
  if (cv.is_degenerate()) bad("which: the curve is degenerate (it is a single point)");
  if (cv.is_segment()) return SEGMENT;
  if (cv.is_ray()) return RAY;
  return LINE;
}

void supporting_line(const Geom& c, Rational& a, Rational& b, Rational& c_) {
  const Curve_2& cv = KindOpsBase<LinearTypes>::xcurve(c);
  if (cv.is_degenerate()) bad("supporting_line: the curve is degenerate");
  line_abc(cv, a, b, c_);
}

void direction(const Geom& c, Rational& dx, Rational& dy) {
  const Curve_2& cv = KindOpsBase<LinearTypes>::xcurve(c);
  if (cv.is_degenerate()) bad("direction: the curve is degenerate");
  if (cv.is_segment()) {
    // Documented as target - source for segments (ops.hpp).
    const Point_2& s = stored_source(cv);
    const Point_2& t = stored_target(cv);
    dx = to_rational(t.x()) - to_rational(s.x());
    dy = to_rational(t.y()) - to_rational(s.y());
    return;
  }
  stored_dir(cv, dx, dy);   // rays / lines: the direction of traversal as stored
}

}  // namespace linear

// ===========================================================================
// 4. Explicit instantiation of the generic arrangement implementation
// ===========================================================================
template class ArrImpl<LinearTypes>;

}  // namespace arr2d

// ===========================================================================
// 5. Static registrar
// ===========================================================================
namespace {
struct Registrar {
  Registrar() {
    arr2d::register_kind(
        arr2d::Kind::Linear,
        arr2d::KindEntry{
            &arr2d::linear_ops(),
            [] { return std::unique_ptr<arr2d::ArrBase>(new arr2d::ArrImpl<arr2d::LinearTypes>()); },
            // No Boolean set operations for this kind: CGAL's General_polygon_set_2 needs a
            // bounded, closed traits; arr2d/bso.hpp declares no make_polygon_set_linear.
            nullptr});
  }
};
Registrar registrar;
}  // namespace
