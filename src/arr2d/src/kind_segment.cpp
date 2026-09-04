// arr2d — Kind::Segment translation unit.
//
// Geometry: CGAL::Arr_segment_traits_2<Epeck>.  For this traits
//     Curve_2 == X_monotone_curve_2 == CGAL::Arr_segment_2<Epeck>
// (one and the same C++ type — see docs/dev/cgal61_api/traits_segment_linear_polyline.md §1),
// and Point_2 == Epeck::Point_2, whose coordinates are Lazy_exact_nt<mpq_rational>: every point
// of this kind is exactly rational.
//
// This TU contains, in order:
//   1. SegmentTypes::traits()      — the ONE process-wide traits instance of this kind;
//   2. class SegmentOps            — the kind-specific half of KindOps (KindOpsBase<> has the rest);
//   3. namespace arr2d::segment    — the kind-specific free functions declared in ops.hpp;
//   4. template class ArrImpl<SegmentTypes>  — the explicit instantiation of the arrangement;
//   5. a static Registrar          — self-registration in the kind registry.
//
// LINK CONTRACT (important, and the reason for a few detours below): this TU must be linkable
// on its own (together with registry.o / numbers.o / overlay.o and, optionally, bso_segment.o).
// It therefore never calls a free function of another kind's namespace (arr2d::linear::…,
// arr2d::circle_segment::…, arr2d::polyline::…, arr2d::conic::…, arr2d::bezier::…) — those are
// defined in the other kinds' TUs.  Curves of the other Epeck-based kinds are inspected through
// their *raw CGAL types* (header-only, no symbols), and the CORE-based kinds (conic, Bezier) are
// reached through the type-erased KindOps of the registry, which is empty when the kind is not
// linked (-> Error(Unsupported), never a link error).
//
// CGAL 6.1 traps handled here (docs/dev/CGAL_TRAPS_CHECKLIST.md):
//   * "Never use CGAL::to_double(Epeck::FT) or Approximate_2 for user-visible doubles"
//     -> point_approx / approximate / curve_bbox / approximate_coordinate all go through
//        impl/number_conv.hpp (exact -> correctly rounded double / certified interval).
//   * "Approximate_2 curve overload needs a back_insert_iterator and error > 0"
//     -> approximate() validates the tolerance and does not call Approximate_2 at all (see there).
//   * traits_segment_linear_polyline.md gotcha 7 (endpoint accessors return references into the
//     curve) and gotcha 12 (no vertex(i)/min()/max() on Arr_segment_2) -> we copy immediately and
//     use source()/target()/left()/right() only.
//   * traits_segment_linear_polyline.md gotcha 13 (functors hold a reference to the traits and
//     have protected constructors) -> functors are always fetched from the traits at the point of
//     use and never stored (the rule KindOpsBase already follows).
//   * A default-constructed Arr_segment_2 is degenerate and almost every accessor has
//     CGAL_precondition(!is_degenerate()) -> require_valid() below turns that into Error.
#include "arr2d/kinds/segment_types.hpp"

// Other kinds, for convert_point / convert_curve.  Headers only: no symbol of those TUs is
// referenced, so linking this TU alone stays possible (see the LINK CONTRACT above).
#include "arr2d/kinds/circle_segment_types.hpp"
#include "arr2d/kinds/linear_types.hpp"
#include "arr2d/kinds/polyline_types.hpp"

#include "arr2d/bso.hpp"
#include "arr2d/common.hpp"
#include "arr2d/impl/arr_impl.hpp"
#include "arr2d/impl/kind_ops_base.hpp"
#include "arr2d/impl/number_conv.hpp"
#include "arr2d/numbers.hpp"
#include "arr2d/ops.hpp"
#include "arr2d/registry.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace arr2d {

// ===========================================================================
// 1. The process-wide traits object
// ===========================================================================
//
// LIFETIME: one instance per process, created on the first call and destroyed only during static
// destruction at the very end of the process.  It must outlive every Point_2 / Curve_2 /
// X_monotone_curve_2 / Arrangement of this kind, because
//   * every CGAL functor obtained from it stores `const Traits&` (traits_segment_linear_polyline.md
//     gotcha 13), and
//   * every ArrImpl<SegmentTypes> is constructed with &SegmentTypes::traits()
//     (arrangement_core.md gotcha 14: the pointer is shared by every copy of the arrangement).
// A function-local static satisfies that: its storage lives until the process exits.
//
// It is NEVER copied: KindOpsBase copy-constructs an Arr_traits_adaptor_2 from it (a separate
// object) and ArrImpl only ever takes its address.
//
// The CORE MemoryPool trap ("any object holding a CORE::Expr in static storage aborts at exit
// with `! blocks.empty()`", CGAL/CORE/MemoryPool.h:125) does NOT apply to this kind: the segment
// traits is an Epeck traits, it holds no CORE number, and Arr_segment_traits_2<Epeck> is stateless
// (it merely derives from the kernel), so its destructor is trivial.  Bezier/conic must leak their
// traits; segment does not have to.
const SegmentTypes::Traits& SegmentTypes::traits() {
  static const Traits t;
  return t;
}

namespace {

using ST = SegmentTypes;
using Point_2 = ST::Point_2;                  // Epeck::Point_2
using Curve_2 = ST::Curve_2;                  // Arr_segment_2<Epeck>
using Xcv_2 = ST::X_monotone_curve_2;         // ... the same type
using FT = ST::FT;                            // Lazy_exact_nt<mpq_rational>

static_assert(std::is_same<Curve_2, Xcv_2>::value,
              "Arr_segment_traits_2::Curve_2 and ::X_monotone_curve_2 must be the same type");
static_assert(std::is_same<Point_2, LinearTypes::Point_2>::value,
              "the linear kind must share Epeck::Point_2 with the segment kind");
static_assert(std::is_same<Point_2, PolylineTypes::Point_2>::value,
              "the polyline kind must share Epeck::Point_2 with the segment kind");
static_assert(std::is_same<Curve_2, PolylineTypes::Segment_2>::value,
              "a polyline subcurve must be exactly our Arr_segment_2<Epeck>");

/// Canonical text of an exact rational: "3" for an integer, "3/4" otherwise.
std::string rat_str(const Rational& r) {
  std::string num, den;
  rational_to_strings(r, num, den);   // canonical: den > 0, gcd 1
  return (den == "1") ? num : (num + "/" + den);
}

/// "(x, y)" with exact rational coordinates.
std::string point_text(const Point_2& p) {
  return "(" + rat_str(to_rational(p.x())) + ", " + rat_str(to_rational(p.y())) + ")";
}

// ===========================================================================
// 2. SegmentOps
// ===========================================================================
class SegmentOps final : public KindOpsBase<SegmentTypes> {
 public:
  SegmentOps() = default;

  bool has_polygon_set() const override { return true; }   // Gps_segment_traits_2<Epeck>, see bso_segment.cpp

  // ------------------------------------------------------------------ points
  Geom make_point(const Rational& x, const Rational& y) const override {
    return box_point(Point_2(to_epeck_ft(x), to_epeck_ft(y)));
  }

  Geom make_point_3(const Rational&, const Rational&, const Rational&) const override {
    throw_error(ErrorCode::Unsupported,
                "segment: points of this kind are planar (x, y); make_point_3 exists only for the "
                "'sphere' kind, whose points are directions in 3D");
  }

  void point_approx(const Geom& p, double* xyz) const override {
    const Point_2& pt = point(p);
    // NOT CGAL::to_double / Approximate_2: both derive the double from the lazy INTERVAL and are
    // not correctly rounded (number_types_and_errors.md gotcha 2, exact_coordinates_contract.md
    // gotcha 2 — measured: 1/3 -> 0.33333333333333337 instead of 0.33333333333333331).
    xyz[0] = to_double_correctly_rounded(pt.x());
    xyz[1] = to_double_correctly_rounded(pt.y());
  }

  void point_interval(const Geom& p, std::vector<std::pair<double, double>>& out) const override {
    const Point_2& pt = point(p);
    out.clear();
    out.push_back(interval_of(pt.x()));   // certified, tight (exact rational -> floor/ceil double)
    out.push_back(interval_of(pt.y()));
  }

  /// Epeck coordinates are Lazy_exact_nt<mpq_rational>: always exactly rational.
  bool point_is_rational(const Geom& p) const override {
    point(p);   // validate the box (kind + type)
    return true;
  }

  void point_exact_rational(const Geom& p, std::vector<Rational>& out) const override {
    const Point_2& pt = point(p);
    out.clear();
    // .exact() returns a reference INTO the shared lazy DAG node; to_rational() copies it out
    // immediately (exact_coordinates_contract.md gotcha 10).
    out.push_back(to_rational(pt.x()));
    out.push_back(to_rational(pt.y()));
  }

  void point_exact(const Geom& p, std::vector<Geom>& numbers) const override {
    const Point_2& pt = point(p);
    numbers.clear();
    numbers.push_back(box_epeck_ft(pt.x()));   // NumberKind::Rational boxes
    numbers.push_back(box_epeck_ft(pt.y()));
  }

  std::string point_repr(const Geom& p) const override {
    const Point_2& pt = point(p);
    return "Point(" + rat_str(to_rational(pt.x())) + ", " + rat_str(to_rational(pt.y())) + ")";
  }

  /// Any planar kind whose point has rational coordinates converts exactly (all rationals in this
  /// build are the same mpq_rational, so nothing is lost).
  Geom convert_point(const Geom& p) const override {
    require_type(p, GeomType::Point, "point");
    if (p.kind == Kind::Segment) { point(p); return p; }   // identity (validated)

    // Sphere points are directions in 3D — checked before anything else so that the answer never
    // depends on what the box happens to hold.
    if (p.kind == Kind::Sphere)
      throw_error(ErrorCode::KindMismatch,
                  "segment: a 'sphere' point is a direction in 3D and has no planar (x, y) "
                  "representation");

    // Fast, registry-free path: the linear and polyline kinds use the very same Epeck::Point_2.
    if (p.holds<Point_2>()) return box_point(p.as<Point_2>());

    // Circle-segment points carry Sqrt_extension coordinates; they convert iff both are rational
    // (which includes the "extended but perfect-square root" case — traits_circle_segment.md /
    // exact_coordinates_contract.md gotcha 3; box_sqrt_extension() normalises it).
    if (p.holds<CircleSegmentTypes::Point_2>()) {
      const CircleSegmentTypes::Point_2& cp = p.as<CircleSegmentTypes::Point_2>();
      Rational x, y;
      if (!one_root_coord_to_rational(cp.x(), x) || !one_root_coord_to_rational(cp.y(), y))
        throw_error(ErrorCode::NotRepresentable,
                    "segment: the circle-segment point has an irrational (sqrt-extension) "
                    "coordinate and cannot be represented exactly by a rational segment point");
      return make_point(x, y);
    }

    // Everything else (conic, Bezier) goes through the type-erased ops of that kind.
    const KindOps& src = other_ops(p.kind, "point");
    if (src.dimension() != 2)
      throw_error(ErrorCode::KindMismatch,
                  std::string("segment: cannot convert a ") + std::to_string(src.dimension()) +
                      "D point of kind '" + kind_name(p.kind) + "' into a planar segment point");
    if (!src.point_is_rational(p))
      throw_error(ErrorCode::NotRepresentable,
                  std::string("segment: the point of kind '") + kind_name(p.kind) +
                      "' has algebraic coordinates and is not representable as a rational "
                      "segment point");
    std::vector<Rational> xy;
    src.point_exact_rational(p, xy);
    if (xy.size() < 2)
      throw_error(ErrorCode::InvalidArgument,
                  std::string("segment: kind '") + kind_name(p.kind) +
                      "' reported fewer than two exact coordinates for a planar point");
    return make_point(xy[0], xy[1]);
  }

  // ------------------------------------------------------------------ curves
  /// Curve_2 and X_monotone_curve_2 are the same C++ type here, so this is a re-boxing.
  Geom to_curve(const Geom& xc) const override { return box_curve(require_valid(xcurve(xc))); }

  Geom xcurve_source(const Geom& xc) const override {
    // Copy at once: Construct_min_vertex_2 / source() return a reference into the curve
    // (traits_segment_linear_polyline.md gotcha 7).
    return box_point(require_valid(xcurve(xc)).source());
  }
  Geom xcurve_target(const Geom& xc) const override {
    return box_point(require_valid(xcurve(xc)).target());
  }
  /// A segment is bounded on both sides; both endpoints always exist.
  bool xcurve_has_source(const Geom& xc) const override { require_valid(xcurve(xc)); return true; }
  bool xcurve_has_target(const Geom& xc) const override { require_valid(xcurve(xc)); return true; }

  BBox curve_bbox(const Geom& c) const override {
    const Xcv_2& s = require_valid(xcurve(c));
    // Arr_segment_2::bbox() would go through CGAL::to_double of the lazy interval; interval_of()
    // gives a certified enclosure built from the exact rational value instead.
    const std::pair<double, double> sx = interval_of(s.source().x());
    const std::pair<double, double> sy = interval_of(s.source().y());
    const std::pair<double, double> tx = interval_of(s.target().x());
    const std::pair<double, double> ty = interval_of(s.target().y());
    BBox b;
    b.dim = 2;
    b.lo[0] = std::min(sx.first, tx.first);
    b.hi[0] = std::max(sx.second, tx.second);
    b.lo[1] = std::min(sy.first, ty.first);
    b.hi[1] = std::max(sy.second, ty.second);
    return b;
  }

  bool curve_is_bounded(const Geom& c) const override { require_valid(xcurve(c)); return true; }

  /// The polyline approximation of a segment is the segment itself: source, then target.
  ///
  /// CGAL's own Arr_segment_traits_2::Approximate_2 curve overload is deliberately NOT used:
  ///   * it ignores `error` and emits exactly the two endpoints anyway
  ///     (rendering_and_approximation.md gotcha 3), so there is nothing to gain, and
  ///   * it converts the coordinates with CGAL::to_double(Epeck::FT), which is not correctly
  ///     rounded (gotcha 2 of number_types_and_errors.md / exact_coordinates_contract.md), so its
  ///     output would disagree with point_approx() on the very same endpoint.
  /// (Had we called it, the rules from the checklist would apply: validate error > 0 first — a
  /// non-positive error segfaults or hangs the recursive traits — and always pass a
  /// std::back_insert_iterator, never a raw pointer or vector::iterator, because the functors
  /// recurse with the iterator passed by value: rendering_and_approximation.md gotchas 6 and 7.)
  void approximate(const Geom& c, double tolerance, const BBox* clip,
                   std::vector<double>& out) const override {
    const Xcv_2& s = require_valid(xcurve(c));
    (void)checked_tolerance(tolerance);   // validated for a uniform contract across kinds
    (void)clip;                           // segments are bounded: no clipping box is needed
    out.clear();
    out.reserve(4);
    out.push_back(to_double_correctly_rounded(s.source().x()));
    out.push_back(to_double_correctly_rounded(s.source().y()));
    out.push_back(to_double_correctly_rounded(s.target().x()));
    out.push_back(to_double_correctly_rounded(s.target().y()));
  }

  std::string curve_repr(const Geom& c) const override {
    const Xcv_2& s = require_valid(xcurve(c));
    return "Segment(" + point_text(s.source()) + ", " + point_text(s.target()) + ")";
  }

  /// Exact conversions into this kind (see DESIGN.md's conversion matrix):
  ///   segment        -> itself;
  ///   linear         -> only a bounded segment (a ray / line is not representable);
  ///   circle_segment -> only a linear piece whose two endpoints are rational;
  ///   polyline       -> one segment per subcurve (several output curves);
  ///   conic / Bezier -> only an arc that IS the straight chord of its two rational endpoints;
  ///   sphere         -> never (3D directions).
  void convert_curve(const Geom& c, std::vector<Geom>& out) const override {
    out.clear();
    if (c.empty()) throw_error(ErrorCode::InvalidArgument, "segment: empty geometry");
    if (c.type != GeomType::Curve && c.type != GeomType::XCurve)
      throw_error(ErrorCode::InvalidArgument,
                  "segment: convert_curve expects a curve or an x-monotone curve");
    switch (c.kind) {
      case Kind::Segment:
        out.push_back(box_xcurve(require_valid(xcurve(c))));   // identity, normalised to an XCurve box
        return;
      case Kind::Linear: return from_linear(c, out);
      case Kind::CircleSegment: return from_circle_segment(c, out);
      case Kind::Polyline: return from_polyline(c, out);
      case Kind::Conic:
      case Kind::Bezier: return from_algebraic_kind(c, out);
      case Kind::Sphere:
        throw_error(ErrorCode::NotRepresentable,
                    "segment: a geodesic arc on the sphere is not a planar straight segment");
      default:
        throw_error(ErrorCode::InvalidArgument,
                    std::string("segment: unknown source kind ") + std::to_string(int(c.kind)));
    }
  }

  // ---------------------------------------------------- generic-method overrides
  /// KindOpsBase would call Arr_segment_traits_2::Approximate_2(p, i), which is
  /// `CGAL::to_double(p.x())` on a Lazy_exact_nt — NOT correctly rounded (measured above).  Route
  /// it through our own exact conversion so that approximate_coordinate(), point_approx() and
  /// approximate() can never disagree about the same coordinate.
  double approximate_coordinate(const Geom& p, int i) const override {
    if (i < 0 || i >= 2) invalid("coordinate index out of range (planar points have x and y)");
    double xy[2] = {0.0, 0.0};
    point_approx(p, xy);
    return xy[i];
  }

  // ---------------------------------------------------- helpers used by the free functions
  /// Build the x-monotone segment [a, b] with the traits' caching constructor.
  Geom segment_from_points(const Point_2& a, const Point_2& b) const {
    // CGAL precondition of Construct_x_monotone_curve_2 / Arr_segment_2(p, q):
    // "Cannot construct a degenerate segment."  Checked here so that misuse is a clean Error.
    auto eq = traits().equal_2_object();
    if (eq(a, b))
      throw_error(ErrorCode::InvalidArgument,
                  "segment: the two endpoints must be distinct (a degenerate segment is not a "
                  "valid curve)");
    auto ctr = traits().construct_x_monotone_curve_2_object();
    return box_xcurve(ctr(a, b));
  }

  Geom segment_from_rationals(const Rational& x1, const Rational& y1,
                              const Rational& x2, const Rational& y2) const {
    return segment_from_points(Point_2(to_epeck_ft(x1), to_epeck_ft(y1)),
                               Point_2(to_epeck_ft(x2), to_epeck_ft(y2)));
  }

  /// A default-constructed Arr_segment_2 is flagged degenerate and nearly every accessor has
  /// CGAL_precondition(!is_degenerate()).  Reject it up front.
  static const Xcv_2& require_valid(const Xcv_2& s) {
    if (s.is_degenerate())
      throw_error(ErrorCode::InvalidArgument,
                  "segment: the curve is degenerate (an empty / default-constructed segment)");
    return s;
  }

  double checked_tolerance(double tolerance) const {
    // !(t > 0) also rejects NaN.  A non-positive tolerance makes CGAL's recursive Approximate_2
    // implementations recurse forever (rendering_and_approximation.md gotcha 7); we validate it
    // for every kind so that the Python-visible contract is identical everywhere.
    if (!(tolerance > 0.0))
      invalid("approximate: the tolerance must be a positive number");
    return tolerance < 1e-12 ? 1e-12 : tolerance;
  }

 private:
  /// The registered KindOps of another kind, or Error(Unsupported) when that kind's TU is not
  /// linked into this build (registry entry empty).  Never a link-time dependency.
  static const KindOps& other_ops(Kind k, const char* what) {
    if (!kind_available(k))
      throw_error(ErrorCode::Unsupported,
                  std::string("segment: cannot convert a ") + what + " of kind '" + kind_name(k) +
                      "': that kind is not linked into this build");
    return arr2d::ops(k);
  }

  /// A circle-segment coordinate (CGAL::Sqrt_extension over Epeck::FT) as an exact Rational.
  /// Returns false when the value is provably irrational.  box_sqrt_extension() normalises the
  /// "extended but actually rational" cases (a1 == 0, root == 0, perfect-square root).
  template <class CoordNT>
  static bool one_root_coord_to_rational(const CoordNT& v, Rational& out) {
    const Geom n = box_sqrt_extension(v);
    if (!number_is_rational(n)) return false;
    out = number_to_rational(n);
    return true;
  }

  // ---- convert_curve back-ends -------------------------------------------
  void from_linear(const Geom& c, std::vector<Geom>& out) const {
    if (!c.holds<LinearTypes::Curve_2>())
      throw_error(ErrorCode::InvalidArgument,
                  "segment: the geometry is tagged 'linear' but does not hold an "
                  "Arr_linear_object_2");
    const LinearTypes::Curve_2& lo = c.as<LinearTypes::Curve_2>();
    // Arr_linear_object_2::is_segment() == (!degenerate && has_source && has_target).
    // source()/target() have CGAL preconditions (!is_line(), !is_ray()) which is_segment() covers.
    if (!lo.is_segment())
      throw_error(ErrorCode::NotRepresentable,
                  lo.is_ray()
                      ? "segment: a 'linear' ray is unbounded and cannot be converted into a "
                        "bounded segment"
                      : (lo.is_line()
                             ? "segment: a 'linear' line is unbounded and cannot be converted into "
                               "a bounded segment"
                             : "segment: the 'linear' object is degenerate (a single point)"));
    out.push_back(segment_from_points(lo.source(), lo.target()));
  }

  void from_circle_segment(const Geom& c, std::vector<Geom>& out) const {
    using CS = CircleSegmentTypes;
    const CS::Point_2* src = nullptr;
    const CS::Point_2* tgt = nullptr;
    if (c.holds<CS::X_monotone_curve_2>()) {
      const CS::X_monotone_curve_2& x = c.as<CS::X_monotone_curve_2>();
      if (!x.is_linear())
        throw_error(ErrorCode::NotRepresentable,
                    "segment: a circular arc cannot be converted into a straight segment");
      src = &x.source();
      tgt = &x.target();
    } else if (c.holds<CS::Curve_2>()) {
      const CS::Curve_2& g = c.as<CS::Curve_2>();
      // source()/target() are meaningless for a full circle (CGAL precondition).
      if (g.is_full())
        throw_error(ErrorCode::NotRepresentable,
                    "segment: a full circle cannot be converted into a straight segment");
      if (!g.is_linear())
        throw_error(ErrorCode::NotRepresentable,
                    "segment: a circular arc cannot be converted into a straight segment");
      src = &g.source();
      tgt = &g.target();
    } else {
      throw_error(ErrorCode::InvalidArgument,
                  "segment: the geometry is tagged 'circle_segment' but holds neither a "
                  "_Circle_segment_2 nor an _X_monotone_circle_segment_2");
    }
    Rational x1, y1, x2, y2;
    if (!one_root_coord_to_rational(src->x(), x1) || !one_root_coord_to_rational(src->y(), y1) ||
        !one_root_coord_to_rational(tgt->x(), x2) || !one_root_coord_to_rational(tgt->y(), y2))
      throw_error(ErrorCode::NotRepresentable,
                  "segment: the circle-segment curve has an endpoint with an irrational "
                  "(sqrt-extension) coordinate");
    out.push_back(segment_from_rationals(x1, y1, x2, y2));
  }

  void from_polyline(const Geom& c, std::vector<Geom>& out) const {
    using PL = PolylineTypes;
    // A polyline (x-monotone or not) is a chain of Arr_segment_2<Epeck> subcurves — exactly our
    // own Curve_2 (static_assert above), so each one is copied over as a segment.
    if (c.holds<PL::X_monotone_curve_2>()) {
      const PL::X_monotone_curve_2& p = c.as<PL::X_monotone_curve_2>();
      push_subcurves(p.subcurves_begin(), p.subcurves_end(), out);
    } else if (c.holds<PL::Curve_2>()) {
      const PL::Curve_2& p = c.as<PL::Curve_2>();
      push_subcurves(p.subcurves_begin(), p.subcurves_end(), out);
    } else {
      throw_error(ErrorCode::InvalidArgument,
                  "segment: the geometry is tagged 'polyline' but holds neither a Polycurve_2 nor "
                  "an X_monotone_polycurve_2");
    }
    if (out.empty())
      throw_error(ErrorCode::InvalidArgument, "segment: the polyline has no subcurve");
  }

  template <class It>
  void push_subcurves(It first, It last, std::vector<Geom>& out) const {
    for (It it = first; it != last; ++it) out.push_back(box_xcurve(require_valid(*it)));
  }

  /// conic / Bezier -> segment.  Both kinds live in CORE-based TUs whose headers we do not want
  /// here (and whose free functions we may not call — see the LINK CONTRACT), so the whole test
  /// is done through the type-erased KindOps of the source kind:
  ///
  ///   * the arc must have two RATIONAL endpoints, and
  ///   * the arc must BE the straight chord between them.  That is certified exactly: the arc is
  ///     an algebraic curve, and a curve of degree d that is not contained in a line meets that
  ///     line in at most d points (Bezout).  We test the two endpoints plus k_chord_samples
  ///     interior points of the chord with the source traits' exact Compare_y_at_x_2, so any arc
  ///     of degree <= k_chord_samples + 1 that passes all of them IS the chord.  A conic arc has
  ///     degree 2 and is therefore fully certified; a polynomial Bezier arc of degree <= 7 is
  ///     too (CGAL's Bezier traits does not produce higher degrees here in practice — see the
  ///     open issue in the stage-2 notes).
  ///   * a vertical arc is a vertical straight segment already (x(t) is constant on an
  ///     x-monotone piece), and Compare_y_at_x_2 is not usable there, so it is accepted directly.
  ///
  /// This is deliberately NOT `conic::conic_type() == LINE_PAIR_OR_SEGMENT`: that free function
  /// is defined in kind_conic.cpp and calling it would make this TU un-linkable on its own.
  void from_algebraic_kind(const Geom& c, std::vector<Geom>& out) const {
    const KindOps& src = other_ops(c.kind, "curve");

    // A general Curve_2 may be non-x-monotone (a full ellipse, an S-shaped Bezier): split first.
    std::vector<Geom> pieces;
    if (c.type == GeomType::XCurve) pieces.push_back(c);
    else src.make_x_monotone(c, pieces);

    for (const Geom& piece : pieces) {
      if (piece.type == GeomType::Point)
        throw_error(ErrorCode::NotRepresentable,
                    std::string("segment: the curve of kind '") + kind_name(c.kind) +
                        "' contains an isolated point, which is not a segment");
      out.push_back(chord_of(src, piece, c.kind));
    }
    if (out.empty())
      throw_error(ErrorCode::NotRepresentable,
                  std::string("segment: the curve of kind '") + kind_name(c.kind) +
                      "' has no x-monotone piece");
  }

  static constexpr int k_chord_samples = 6;   ///< interior samples; certifies degree <= 7

  Geom chord_of(const KindOps& src, const Geom& piece, Kind k) const {
    const Geom gs = src.xcurve_source(piece);
    const Geom gt = src.xcurve_target(piece);
    if (!src.point_is_rational(gs) || !src.point_is_rational(gt))
      throw_error(ErrorCode::NotRepresentable,
                  std::string("segment: the curve of kind '") + kind_name(k) +
                      "' has an endpoint with algebraic coordinates");
    std::vector<Rational> a, b;
    src.point_exact_rational(gs, a);
    src.point_exact_rational(gt, b);
    if (a.size() < 2 || b.size() < 2)
      throw_error(ErrorCode::InvalidArgument,
                  std::string("segment: kind '") + kind_name(k) +
                      "' reported fewer than two exact endpoint coordinates");
    if (a[0] == b[0] && a[1] == b[1])
      throw_error(ErrorCode::NotRepresentable,
                  std::string("segment: the curve of kind '") + kind_name(k) +
                      "' is degenerate (its two endpoints coincide)");

    const bool vertical = src.xcurve_is_vertical(piece);
    if (a[0] == b[0]) {
      // Same x on both ends: on an x-monotone curve that is only possible for a vertical curve.
      if (!vertical)
        throw_error(ErrorCode::NotRepresentable,
                    std::string("segment: the curve of kind '") + kind_name(k) +
                        "' is not the straight chord between its endpoints");
    } else if (!vertical) {
      for (int i = 1; i <= k_chord_samples; ++i) {
        const Rational lambda(Rational(i) / Rational(k_chord_samples + 1));
        const Rational x = a[0] + lambda * (b[0] - a[0]);
        const Rational y = a[1] + lambda * (b[1] - a[1]);
        const Geom probe = src.convert_point(make_point(x, y));   // rational -> the source kind
        if (src.compare_y_at_x(probe, piece) != 0)
          throw_error(ErrorCode::NotRepresentable,
                      std::string("segment: the curve of kind '") + kind_name(k) +
                          "' is not the straight chord between its endpoints and cannot be "
                          "converted exactly into a segment");
      }
    }
    return segment_from_rationals(a[0], a[1], b[0], b[1]);
  }
};

/// The one KindOps instance of this kind.  Deliberately leaked (never destroyed): the registry
/// keeps the pointer for the whole life of the process, KindOpsBase itself owns a leaked
/// Arr_traits_adaptor_2, and destroying per-kind singletons during static teardown is the trap
/// that aborts the CORE-based kinds (CGAL/CORE/MemoryPool.h:125).  The same shape is used by every
/// kind TU (STAGE1_NOTES.md, kind_ops_base.hpp).
SegmentOps& segment_ops() {
  static SegmentOps* ops = new SegmentOps();
  return *ops;
}

}  // namespace

// ===========================================================================
// 3. arr2d::segment — the kind-specific free functions of ops.hpp
// ===========================================================================
namespace segment {

Geom make(const Geom& p, const Geom& q) {
  const SegmentOps& o = segment_ops();
  // Points of any planar kind with rational coordinates are accepted and converted (ops.hpp).
  const Geom a = o.convert_point(p);
  const Geom b = o.convert_point(q);
  return o.segment_from_points(SegmentOps::point(a), SegmentOps::point(b));
}

Geom make_xy(const Rational& x1, const Rational& y1, const Rational& x2, const Rational& y2) {
  return segment_ops().segment_from_rationals(x1, y1, x2, y2);
}

void endpoints(const Geom& s, Geom& source, Geom& target) {
  const Curve_2& cv = SegmentOps::require_valid(SegmentOps::xcurve(s));
  // source()/target() return references into `cv`; box_point copies them out.
  source = SegmentOps::box_point(cv.source());
  target = SegmentOps::box_point(cv.target());
}

void supporting_line(const Geom& s, Rational& a, Rational& b, Rational& c) {
  const Curve_2& cv = SegmentOps::require_valid(SegmentOps::xcurve(s));
  // _Segment_cached_2::line() has CGAL_precondition(!is_degenerate()) (covered by require_valid)
  // and memoises the line through a mutable member on the first call — reading the same curve
  // object from two threads at once is therefore not safe
  // (traits_segment_linear_polyline.md §2).  The coefficients are exact:
  // a*x + b*y + c = 0 with (a, b, c) as CGAL builds them from source and target, i.e.
  // a = sy - ty, b = tx - sx, c = sx*ty - tx*sy.
  const ST::Kernel::Line_2& line = cv.line();
  a = to_rational(line.a());
  b = to_rational(line.b());
  c = to_rational(line.c());
}

}  // namespace segment
}  // namespace arr2d

// ===========================================================================
// 4. The arrangement of this kind
// ===========================================================================
// Arrangement_with_history_2<Arr_segment_traits_2<Epeck>, Arr_extended_dcel<...>>, instantiated
// once here.  Everything in it is generic (impl/arr_impl.hpp); the per-kind feature matrix lives
// in KindPolicy<SegmentTypes>, which arr_impl.hpp already specialises (segment supports every
// point-location strategy, vertical ray shooting and the free CGAL::is_valid()).
template class arr2d::ArrImpl<arr2d::SegmentTypes>;

// ===========================================================================
// 5. Registration
// ===========================================================================
namespace {
struct Registrar {
  Registrar() {
    arr2d::register_kind(
        arr2d::Kind::Segment,
        arr2d::KindEntry{
            &arr2d::segment_ops(),
            []() -> std::unique_ptr<arr2d::ArrBase> {
              return std::unique_ptr<arr2d::ArrBase>(new arr2d::ArrImpl<arr2d::SegmentTypes>());
            },
            // Boolean set operations: Gps_segment_traits_2<Epeck>, implemented in bso_segment.cpp.
            // This is only a function POINTER here, but the symbol must exist at link time, so a
            // program that links this TU must also link bso_segment.cpp (or, for the standalone
            // kind test, define the stub under -DARR2D_TEST_STUB_BSO; see
            // tests/test_kind_segment.cpp).
            &arr2d::make_polygon_set_segment});
  }
};
Registrar registrar;
}  // namespace
