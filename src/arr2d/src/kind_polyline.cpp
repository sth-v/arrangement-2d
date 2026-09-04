// arr2d — Kind::Polyline: Arr_polyline_traits_2<Arr_segment_traits_2<Epeck>>.
//
// This TU provides, for the polyline kind:
//   1. PolylineTypes::traits()          — the one process-wide traits instance;
//   2. class PolylineOps                — the kind-specific half of KindOps
//                                         (KindOpsBase<PolylineTypes> supplies the generic half);
//   3. namespace arr2d::polyline        — the kind-specific constructors/accessors of ops.hpp;
//   4. template class ArrImpl<PolylineTypes>  — the explicit instantiation of the generic
//                                         arrangement implementation for this kind;
//   5. a static registrar that publishes the kind in the registry (no Boolean set operations:
//      bso.hpp declares no make_polygon_set_polyline, so KindEntry::make_polygon_set is null).
//
// CGAL 6.1 traps handled here (docs/dev/CGAL_TRAPS_CHECKLIST.md, "Polyline kind" +
// docs/dev/cgal61_api/traits_segment_linear_polyline.md):
//
//   * gotcha 2 — copy-ASSIGNING an Arr_polycurve_basic_traits_2 double-frees its sub-traits
//     (raw owning pointer + no operator=).  traits() therefore returns a reference to a single
//     heap instance that is never copied and never assigned.  (KindOpsBase's Arr_traits_adaptor_2
//     copy-CONSTRUCTS the traits, which is safe: the copy allocates a fresh, stateless
//     Arr_segment_traits_2 sub-traits of its own.)
//   * gotcha 1 — Curve_2 / X_monotone_curve_2 are CGAL::internal::Polycurve_2 /
//     X_monotone_polycurve_2; the deprecated polyline::Polyline_2 names (begin_segments(),
//     number_of_segments()) are NOT used and Polyline_2.h is never included.
//   * gotcha 8 — Arr_polyline_traits_2's Push_back_2/Push_front_2 *point* overloads for
//     X_monotone_curve_2 do not compile.  Nothing here appends points to a curve; every curve is
//     built in one shot through Construct_curve_2 / Construct_x_monotone_curve_2.
//   * gotcha 9 — the polyline stream operators are lossy (operator<< writes
//     number_of_subcurves(), operator>> reads it as a point count).  curve_repr() below prints
//     the exact point list itself; the stream operators are never used.
//   * gotcha 4 / rendering_and_approximation.md gotcha 3 — Arr_polyline_traits_2::Approximate_2
//     *does* accept a whole x-monotone polyline, but it IGNORES `error` and converts the
//     coordinates with CGAL::to_double(Epeck::FT), which is interval-derived and not correctly
//     rounded (number_types_and_errors.md gotcha 2).  approximate() below walks the very same
//     point sequence but converts with to_double_correctly_rounded() — same points, same order,
//     correctly rounded (see the comment at approximate()).
//   * traits_segment_linear_polyline.md §7 — Polycurve_2::Point_const_iterator holds a raw
//     pointer into the curve and Construct_min/max_vertex_2 return a `const Point_2&` INTO the
//     curve; every point that leaves this file is copied into its own Geom box first.
//
// The CORE memory-pool trap (CGAL/CORE/MemoryPool.h:125) does not apply to this kind — no
// CORE::Expr is involved anywhere (Epeck::FT is Lazy_exact_nt<mpq_rational>) — but the traits and
// the KindOps singleton are heap-allocated and leaked anyway, so that they outlive every
// arrangement, curve and point of this kind regardless of static-destruction order across TUs
// (see the comments at PolylineTypes::traits() and polyline_ops()).

#include "arr2d/kinds/polyline_types.hpp"

// Only for the exact Segment / Linear -> Polyline curve conversions in convert_curve(): we need
// the C++ types Arr_segment_2<Epeck> (== PolylineTypes::Segment_2, already available) and
// Arr_linear_object_2<Epeck>.  Including the header does NOT create a link dependency on
// kind_linear.cpp: LinearTypes::traits() is only declared, and this TU never calls it.
#include "arr2d/kinds/linear_types.hpp"

#include "arr2d/impl/kind_ops_base.hpp"
#include "arr2d/impl/number_conv.hpp"
#include "arr2d/impl/arr_impl.hpp"
#include "arr2d/ops.hpp"
#include "arr2d/registry.hpp"

#include <cmath>
#include <cstddef>
#include <iterator>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace arr2d {

// ===========================================================================
// 1. The process-wide traits instance
// ===========================================================================
//
// Lifetime contract: the object returned here must outlive every Curve_2 / X_monotone_curve_2 /
// Point_2 / Arrangement of this kind, because
//   * every arrangement of this kind is constructed with `&PolylineTypes::traits()`
//     (ArrImpl<Types>::ArrImpl) and keeps that pointer for its whole life, and
//   * every functor obtained from the traits stores `const Traits&` and must not outlive it
//     (traits_segment_linear_polyline.md gotcha 13).
// A function-local static object would be destroyed during static teardown in an order that is
// unspecified relative to statics in other TUs (and relative to the Python interpreter's own
// teardown), so the single instance is heap-allocated and DELIBERATELY LEAKED.  That also makes
// the "never copy-assign a polycurve traits" rule (gotcha 2) trivially safe: there is exactly one
// object, it is never copied and never assigned; only KindOpsBase's Arr_traits_adaptor_2
// copy-CONSTRUCTS from it (safe — the copy allocates its own fresh, stateless
// Arr_segment_traits_2 sub-traits).
const PolylineTypes::Traits& PolylineTypes::traits() {
  static Traits* t = new Traits();   // intentionally never deleted; see above
  return *t;
}

namespace {

// ---------------------------------------------------------------------------
// Local type aliases
// ---------------------------------------------------------------------------
using Types = PolylineTypes;
using Point_2 = Types::Point_2;                    // Epeck::Point_2
using Curve_2 = Types::Curve_2;                    // internal::Polycurve_2<Arr_segment_2, Point_2>
using XCurve_2 = Types::X_monotone_curve_2;        // internal::X_monotone_polycurve_2<...>
using Segment_2 = Types::Segment_2;                // Arr_segment_2<Epeck> (the subcurve type)
using FT = Types::FT;                              // Lazy_exact_nt<mpq_rational>
using Linear_2 = LinearTypes::Curve_2;             // Arr_linear_object_2<Epeck>

// X_monotone_polycurve_2<S,P> derives from Polycurve_2<S,P>, and for the polyline traits the
// subcurve type of both is Arr_segment_2<Epeck>; so an x-monotone polyline IS-A general polyline
// and the read-only chain API (number_of_subcurves / operator[] / points_begin / bbox) can be
// reached through the base for either box type.  Asserted so that a future CGAL change is a
// compile error rather than silent misbehaviour.
static_assert(std::is_base_of<Curve_2, XCurve_2>::value,
              "arr2d polyline: X_monotone_curve_2 is expected to derive from Curve_2 "
              "(CGAL::internal::X_monotone_polycurve_2 : public Polycurve_2)");
static_assert(std::is_same<Segment_2, Types::SegmentTraits::Curve_2>::value,
              "arr2d polyline: the polyline sub-curve type must be the segment kind's Curve_2 "
              "(Arr_segment_2<Epeck>), so that polyline::subcurve() can be boxed as Kind::Segment");

// ---------------------------------------------------------------------------
// Small text helpers (exact, never lossy — numbers.cpp uses the same shape)
// ---------------------------------------------------------------------------
std::string rat_text(const Rational& r) {
  const Integer n = numerator(r), d = denominator(r);
  if (d == 1) return n.str();
  return n.str() + "/" + d.str();
}

std::string point_text(const Point_2& p) {
  const FT x = p.x(), y = p.y();
  return "(" + rat_text(to_rational(x)) + ", " + rat_text(to_rational(y)) + ")";
}

// ---------------------------------------------------------------------------
// Box helpers
// ---------------------------------------------------------------------------

/// The read-only chain view of either box type (Curve or XCurve) of this kind.
/// An x-monotone polyline is returned through its Polycurve_2 base subobject (no copy).
const Curve_2& chain(const Geom& g) {
  require_any_curve(g, Kind::Polyline, "polyline");
  if (g.holds<Curve_2>()) return g.as<Curve_2>();
  if (g.holds<XCurve_2>()) return static_cast<const Curve_2&>(g.as<XCurve_2>());
  return g.as<Curve_2>();   // wrong C++ type in the box: as<>() produces the precise diagnostic
}

/// A Kind::Segment box holding one sub-segment (Arr_segment_2<Epeck>, which is exactly
/// SegmentTypes::Curve_2 == SegmentTypes::X_monotone_curve_2 — a segment is always x-monotone,
/// hence GeomType::XCurve).
Geom box_segment(const Segment_2& s) { return make_geom(Kind::Segment, GeomType::XCurve, s); }

/// A Kind::Segment point box (ops.hpp documents polyline::point() as returning one).
/// The C++ type is the same Epeck::Point_2 that the polyline kind uses.
Geom box_segment_point(const Point_2& p) { return make_geom(Kind::Segment, GeomType::Point, p); }

[[noreturn]] void poly_error(ErrorCode c, const std::string& msg) {
  throw_error(c, "polyline: " + msg);
}

/// Re-throw a nested Error with positional context, without repeating the "polyline: " prefix.
[[noreturn]] void poly_rethrow(const Error& e, const std::string& context) {
  const std::string what = e.what();
  const std::string tail = (what.rfind("polyline: ", 0) == 0) ? what.substr(10) : what;
  throw_error(e.code, "polyline: " + context + ": " + tail);
}

// ===========================================================================
// 2. PolylineOps
// ===========================================================================
class PolylineOps final : public KindOpsBase<PolylineTypes> {
 public:
  // ------------------------------------------------------------------ points
  Geom make_point(const Rational& x, const Rational& y) const override {
    return box_point(Point_2(to_epeck_ft(x), to_epeck_ft(y)));
  }

  Geom make_point_3(const Rational&, const Rational&, const Rational&) const override {
    poly_error(ErrorCode::Unsupported,
               "points of this kind are planar (x, y); make_point_3() exists only for the "
               "sphere kind, whose points are 3-D directions");
  }

  /// Correctly rounded doubles.  NOT CGAL::to_double(Epeck::FT) / Approximate_2, which derive the
  /// result from the lazy interval and are off by an ulp (number_types_and_errors.md gotcha 2:
  /// to_double(FT(1)/FT(3)) == 0.33333333333333337 instead of 0.33333333333333331).
  void point_approx(const Geom& p, double* xyz) const override {
    const Point_2& q = point(p);
    const FT x = q.x(), y = q.y();
    xyz[0] = to_double_correctly_rounded(x);
    xyz[1] = to_double_correctly_rounded(y);
  }

  void point_interval(const Geom& p, std::vector<std::pair<double, double>>& out) const override {
    const Point_2& q = point(p);
    const FT x = q.x(), y = q.y();
    out.clear();
    out.push_back(interval_of(x));
    out.push_back(interval_of(y));
  }

  /// Epeck points are Cartesian over Lazy_exact_nt<mpq_rational>: every coordinate is rational.
  bool point_is_rational(const Geom& p) const override {
    point(p);   // kind / type validation only
    return true;
  }

  void point_exact_rational(const Geom& p, std::vector<Rational>& out) const override {
    const Point_2& q = point(p);
    const FT x = q.x(), y = q.y();
    out.clear();
    // to_rational(EpeckFT) goes through .exact(); the reference it returns lives in the shared
    // lazy DAG node, so number_conv.hpp copies it out immediately.
    out.push_back(to_rational(x));
    out.push_back(to_rational(y));
  }

  void point_exact(const Geom& p, std::vector<Geom>& numbers) const override {
    const Point_2& q = point(p);
    const FT x = q.x(), y = q.y();
    numbers.clear();
    numbers.push_back(box_epeck_ft(x));   // NumberKind::Rational boxes
    numbers.push_back(box_epeck_ft(y));
  }

  std::string point_repr(const Geom& p) const override {
    return "Point" + point_text(point(p));
  }

  Geom convert_point(const Geom& p) const override {
    require_type(p, GeomType::Point, "point");
    if (p.kind == Kind::Polyline) return p;
    // The segment, linear and polyline kinds all use Epeck::Point_2.  Re-boxing the very same
    // C++ object is exact and — unlike the registry route below — works even when the source
    // kind's TU is not linked into the binary.
    if (p.holds<Point_2>()) return box_point(p.as<Point_2>());
    // Any other kind: go through exact rationals (circle-segment sqrt coordinates that happen to
    // be rational are accepted; Bezier/conic algebraic ones are not — NotRepresentable).
    const KindOps& src = arr2d::ops(p.kind);
    if (src.dimension() != 2)
      poly_error(ErrorCode::KindMismatch,
                 std::string("cannot convert a ") + std::to_string(src.dimension()) +
                     "-D point of kind '" + kind_name(p.kind) + "' into a planar polyline point");
    if (!src.point_is_rational(p))
      poly_error(ErrorCode::NotRepresentable,
                 std::string("the point of kind '") + kind_name(p.kind) +
                     "' has no exact rational coordinates and cannot become a polyline point");
    std::vector<Rational> xy;
    src.point_exact_rational(p, xy);
    if (xy.size() != 2)
      poly_error(ErrorCode::KindMismatch, "expected 2 exact coordinates for a planar point");
    return make_point(xy[0], xy[1]);
  }

  // ------------------------------------------------------------------ curves
  /// XCurve -> Curve.  X_monotone_polycurve_2 derives from Polycurve_2, so slicing would work,
  /// but the traits functor is the documented route and keeps the subcurve vector intact.
  Geom to_curve(const Geom& xc) const override {
    if (xc.type == GeomType::Curve && xc.holds<Curve_2>()) return xc;   // already a general curve
    const XCurve_2& x = xcurve(xc);
    if (x.number_of_subcurves() == 0)
      poly_error(ErrorCode::InvalidArgument, "an empty polyline has no general-curve form");
    // Construct_curve_2's range overload dispatches on the iterator's value_type: subcurves here
    // (Arr_segment_2), so it takes the "range of subcurves" branch — no point re-derivation.
    auto ctr = traits().construct_curve_2_object();
    return box_curve(ctr(x.subcurves_begin(), x.subcurves_end()));
  }

  /// Source = source of the first subcurve.  Polycurve_2 stores its subcurves chained
  /// (subcurve i's target == subcurve i+1's source) and an x-monotone polyline has a uniform
  /// direction, so this is the curve's own source for both orientations.  The point is copied
  /// into the box (Construct_min/max_vertex_2 and Arr_segment_2::source() return references INTO
  /// the curve — traits_segment_linear_polyline.md gotcha 7).
  Geom xcurve_source(const Geom& xc) const override {
    const XCurve_2& x = xcurve(xc);
    if (x.number_of_subcurves() == 0)
      poly_error(ErrorCode::InvalidArgument, "an empty polyline has no source point");
    return box_point(x[0].source());
  }

  Geom xcurve_target(const Geom& xc) const override {
    const XCurve_2& x = xcurve(xc);
    const std::size_t n = x.number_of_subcurves();
    if (n == 0) poly_error(ErrorCode::InvalidArgument, "an empty polyline has no target point");
    return box_point(x[n - 1].target());
  }

  /// Polylines are bounded: both ends always exist (an empty polyline has neither).
  bool xcurve_has_source(const Geom& xc) const override { return xcurve(xc).number_of_subcurves() > 0; }
  bool xcurve_has_target(const Geom& xc) const override { return xcurve(xc).number_of_subcurves() > 0; }

  /// Polycurve_2::bbox() unions the subcurves' Arr_segment_2::bbox(), i.e. the kernel's
  /// Construct_bbox_2 of the two endpoints, which for the lazy Epeck kernel is an outward-rounded
  /// double enclosure of the exact point — safe as an approximate bbox.  (The conic sub-traits
  /// would break here, traits_segment_linear_polyline.md gotcha 5, but our subcurves are
  /// segments.)
  BBox curve_bbox(const Geom& c) const override {
    const Curve_2& p = chain(c);
    if (p.number_of_subcurves() == 0)
      poly_error(ErrorCode::InvalidArgument, "an empty polyline has no bounding box");
    const CGAL::Bbox_2 b = p.bbox();
    BBox out;
    out.dim = 2;
    out.lo[0] = b.xmin();
    out.lo[1] = b.ymin();
    out.lo[2] = 0.0;
    out.hi[0] = b.xmax();
    out.hi[1] = b.ymax();
    out.hi[2] = 0.0;
    return out;
  }

  /// Every polyline is a finite chain of bounded segments.
  bool curve_is_bounded(const Geom& c) const override {
    chain(c);   // kind / type validation only
    return true;
  }

  /// The exact vertex sequence, from source to target as stored.
  ///
  /// Why not traits().approximate_2_object()(xcv, error, oi):  that functor emits exactly the
  /// same points in exactly the same order (Arr_polyline_traits_2.h:626-646 walks
  /// points_begin()..points_end()), it IGNORES `error`
  /// (rendering_and_approximation.md gotcha 3 — for a polyline the approximation IS the curve,
  /// so no subdivision is possible or needed), and it converts with CGAL::to_double(Epeck::FT),
  /// which is not correctly rounded (number_types_and_errors.md gotcha 2).  Walking the points
  /// ourselves gives the identical sequence with correctly rounded coordinates, and it also works
  /// for a *general* (non-x-monotone) polyline, which that functor rejects at compile time.
  /// The tolerance is still validated exactly like every other kind so that Python sees one
  /// behaviour (rendering_and_approximation.md gotcha 7: `error <= 0` makes the subdividing
  /// traits recurse forever / segfault).
  void approximate(const Geom& c, double tolerance, const BBox* clip,
                   std::vector<double>& out) const override {
    const Curve_2& p = chain(c);
    if (!(tolerance > 0.0))   // also rejects NaN
      poly_error(ErrorCode::InvalidArgument,
                 "approximate(): the tolerance must be a positive number");
    // Uniform clamp of denormal-ish tolerances (a no-op here: the polyline traits ignores it).
    const double error = (tolerance < 1e-12) ? 1e-12 : tolerance;
    (void)error;
    // `clip` only matters for unbounded curves (Linear rays/lines); a polyline is always bounded.
    (void)clip;
    const std::size_t n = p.number_of_subcurves();
    out.clear();
    if (n == 0) return;
    out.reserve(2 * (n + 1));
    // Point_const_iterator yields subcurve[0].source() then subcurve[i-1].target(): the chain's
    // vertices in stored order, i.e. from the curve's source to its target.  It holds a raw
    // pointer into `p`, which outlives the loop.
    for (auto it = p.points_begin(); it != p.points_end(); ++it) {
      const FT x = (*it).x(), y = (*it).y();
      out.push_back(to_double_correctly_rounded(x));
      out.push_back(to_double_correctly_rounded(y));
    }
  }

  std::string curve_repr(const Geom& c) const override {
    const Curve_2& p = chain(c);
    std::string s = "Polyline([";
    bool first = true;
    for (auto it = p.points_begin(); it != p.points_end(); ++it) {
      if (!first) s += ", ";
      first = false;
      s += point_text(*it);
    }
    s += "])";
    return s;
  }

  /// Exact conversions into the polyline kind.  Output boxes are always x-monotone
  /// (GeomType::XCurve) except for the identity case, because every curve we can convert
  /// exactly is a straight segment (or an x-monotone piece of one), and an x-monotone box is
  /// accepted everywhere a general curve box is (ArrImpl routes it through to_curve()).
  void convert_curve(const Geom& c, std::vector<Geom>& out) const override {
    if (c.type != GeomType::Curve && c.type != GeomType::XCurve)
      poly_error(ErrorCode::InvalidArgument, "convert_curve() needs a curve, not a point/number");

    if (c.kind == Kind::Polyline) {   // identity (the Python layer short-circuits this)
      chain(c);
      out.push_back(c);
      return;
    }

    // --- Segment kind: Arr_segment_2<Epeck> is our own subcurve type -> a 1-subcurve polyline.
    if (c.holds<Segment_2>()) {
      const Segment_2& s = c.as<Segment_2>();
      if (s.is_degenerate())
        poly_error(ErrorCode::InvalidArgument, "cannot convert a degenerate segment");
      out.push_back(box_xcurve(traits().construct_x_monotone_curve_2_object()(s)));
      return;
    }

    // --- Linear kind: only a bounded segment is representable (a ray/line is not).
    if (c.holds<Linear_2>()) {
      const Linear_2& l = c.as<Linear_2>();
      if (l.is_degenerate())
        poly_error(ErrorCode::InvalidArgument, "cannot convert a degenerate linear object");
      if (!l.is_segment())
        poly_error(ErrorCode::NotRepresentable,
                   "a linear ray/line is unbounded; only a bounded linear segment can become a "
                   "polyline (clip it first)");
      // Arr_linear_object_2::source()/target() have the precondition !is_line() && !is_ray(),
      // satisfied by is_segment().
      out.push_back(box_xcurve(traits().construct_x_monotone_curve_2_object()(l.source(), l.target())));
      return;
    }

    // --- Any other kind: exact only if the curve provably IS a straight segment with rational
    //     endpoints.  We prove it with the source kind's own traits: build the straight
    //     x-monotone curve between the two endpoints there (Construct_x_monotone_curve_2) and
    //     require its Equal_2 to accept it as the same curve.  No approximation is involved, and
    //     nothing is assumed about the source geometry.
    const KindOps& src = arr2d::ops(c.kind);   // Error(Unsupported) if that kind is not linked
    if (src.dimension() != 2)
      poly_error(ErrorCode::KindMismatch,
                 std::string("cannot convert a curve of the 3-D kind '") + kind_name(c.kind) +
                     "' into a planar polyline");
    std::vector<Geom> pieces;
    src.make_x_monotone(c, pieces);
    if (pieces.empty())
      poly_error(ErrorCode::InvalidArgument,
                 std::string("the curve of kind '") + kind_name(c.kind) + "' is empty");
    for (std::size_t i = 0; i < pieces.size(); ++i) {
      const Geom& piece = pieces[i];
      if (piece.type == GeomType::Point)
        poly_error(ErrorCode::NotRepresentable,
                   std::string("the curve of kind '") + kind_name(c.kind) +
                       "' contains an isolated point, which is not a polyline");
      if (!src.xcurve_has_source(piece) || !src.xcurve_has_target(piece))
        poly_error(ErrorCode::NotRepresentable,
                   std::string("the curve of kind '") + kind_name(c.kind) +
                       "' is unbounded and cannot become a (bounded) polyline");
      const Geom ps = src.xcurve_source(piece);
      const Geom pt = src.xcurve_target(piece);
      if (!src.point_is_rational(ps) || !src.point_is_rational(pt))
        poly_error(ErrorCode::NotRepresentable,
                   std::string("piece ") + std::to_string(i) + " of the curve of kind '" +
                       kind_name(c.kind) + "' has non-rational endpoints");
      // Straightness proof, in the SOURCE kind's own exact geometry.
      Geom straight;
      try {
        straight = src.construct_x_monotone_curve(ps, pt);
      } catch (const Error&) {
        poly_error(ErrorCode::NotRepresentable,
                   std::string("kind '") + kind_name(c.kind) +
                       "' cannot build a straight curve between two points, so its curves cannot "
                       "be proven equal to a polyline; convert through the segment kind instead");
      }
      if (!src.curve_equal(piece, straight))
        poly_error(ErrorCode::NotRepresentable,
                   std::string("piece ") + std::to_string(i) + " of the curve of kind '" +
                       kind_name(c.kind) + "' is not a straight segment; a polyline can only "
                       "represent straight chains (approximate() it instead)");
      std::vector<Rational> a, b;
      src.point_exact_rational(ps, a);
      src.point_exact_rational(pt, b);
      if (a.size() != 2 || b.size() != 2)
        poly_error(ErrorCode::KindMismatch, "expected 2 exact coordinates per planar endpoint");
      const Point_2 p0(to_epeck_ft(a[0]), to_epeck_ft(a[1]));
      const Point_2 p1(to_epeck_ft(b[0]), to_epeck_ft(b[1]));
      out.push_back(box_xcurve(traits().construct_x_monotone_curve_2_object()(p0, p1)));
    }
  }

  /// CGAL has no Gps traits for polylines (boolean_set_operations.md): Boolean set operations go
  /// through the segment kind, into which every polyline converts exactly.
  bool has_polygon_set() const override { return false; }
};

// ---------------------------------------------------------------------------
// The per-kind KindOps singleton.
//
// KindOpsBase owns a deliberately leaked Arr_traits_adaptor_2 and is non-copyable; the singleton
// itself is leaked as well so that it (and the functors/adaptor it holds, which keep raw
// back-pointers into the traits) can never be destroyed in an unspecified order relative to the
// traits object or to any live arrangement.  STAGE1_NOTES.md records this as the required kind-TU
// convention.
// ---------------------------------------------------------------------------
const PolylineOps& polyline_ops() {
  static PolylineOps* p = new PolylineOps();   // intentionally never deleted
  return *p;
}

// ---------------------------------------------------------------------------
// Helpers shared by the free functions of namespace arr2d::polyline
// ---------------------------------------------------------------------------

/// A Point_2 out of any Geom point box (converted into this kind when necessary).
/// `what` / `index` only shape the error message.
Point_2 point_arg(const Geom& g, const char* what, std::size_t index) {
  try {
    const Geom p = polyline_ops().convert_point(g);
    return p.as<Point_2>();
  } catch (const Error& e) {
    poly_rethrow(e, std::string(what) + " " + std::to_string(index));
  }
}

/// A sub-segment out of a Geom curve box.  Accepts anything holding an Arr_segment_2<Epeck>
/// (that is the segment kind's Curve_2/X_monotone_curve_2 *and* our own subcurve type, so
/// polyline::subcurve() results round-trip); anything else is converted exactly through
/// PolylineOps::convert_curve and must come out as a single one-subcurve polyline.
Segment_2 segment_arg(const Geom& g, std::size_t index) {
  if (g.type != GeomType::Curve && g.type != GeomType::XCurve)
    poly_error(ErrorCode::InvalidArgument,
               "element " + std::to_string(index) + " is not a curve");
  if (g.holds<Segment_2>()) {
    const Segment_2& s = g.as<Segment_2>();
    if (s.is_degenerate())
      poly_error(ErrorCode::InvalidArgument,
                 "segment " + std::to_string(index) + " is degenerate (source == target)");
    return s;
  }
  std::vector<Geom> conv;
  try {
    polyline_ops().convert_curve(g, conv);
  } catch (const Error& e) {
    poly_rethrow(e, "segment " + std::to_string(index));
  }
  if (conv.size() != 1)
    poly_error(ErrorCode::InvalidArgument,
               "curve " + std::to_string(index) + " of kind '" + kind_name(g.kind) +
                   "' converts to " + std::to_string(conv.size()) +
                   " polylines; a single straight segment is required");
  const Curve_2& p = chain(conv[0]);
  if (p.number_of_subcurves() != 1)
    poly_error(ErrorCode::InvalidArgument,
               "curve " + std::to_string(index) + " of kind '" + kind_name(g.kind) +
                   "' is not a single straight segment");
  return p[0];
}

/// Consecutive points must differ: CGAL's Construct_curve_2 / Construct_x_monotone_curve_2 only
/// check this with CGAL_precondition_msg("Cannot construct a degenerated segment"), so we test it
/// ourselves and name the offending index.
void require_distinct_consecutive(const std::vector<Point_2>& pts) {
  auto eq = PolylineTypes::traits().equal_2_object();
  for (std::size_t i = 0; i + 1 < pts.size(); ++i)
    if (eq(pts[i], pts[i + 1]))
      poly_error(ErrorCode::InvalidArgument,
                 "points " + std::to_string(i) + " and " + std::to_string(i + 1) +
                     " are equal " + point_text(pts[i]) +
                     "; consecutive polyline points must be distinct");
}

std::vector<Point_2> points_arg(const std::vector<Geom>& points) {
  if (points.size() < 2)
    poly_error(ErrorCode::InvalidArgument,
               "at least 2 points are required (got " + std::to_string(points.size()) + ")");
  std::vector<Point_2> pts;
  pts.reserve(points.size());
  for (std::size_t i = 0; i < points.size(); ++i) pts.push_back(point_arg(points[i], "point", i));
  require_distinct_consecutive(pts);
  return pts;
}

}  // namespace

// ===========================================================================
// 3. namespace arr2d::polyline — the kind-specific free functions of ops.hpp
// ===========================================================================
namespace polyline {

Geom make(const std::vector<Geom>& points) {
  const std::vector<Point_2> pts = points_arg(points);
  // Construct_curve_2's point-range overload builds one Arr_segment_2 per consecutive pair, in
  // input order, so the resulting Curve_2 is oriented as given.  Its two CGAL preconditions
  // ("range must not contain a single point", "Cannot construct a degenerated segment") are both
  // pre-empted by points_arg().
  auto ctr = PolylineTypes::traits().construct_curve_2_object();
  return make_geom(Kind::Polyline, GeomType::Curve, ctr(pts.begin(), pts.end()));
}

Geom make_from_segments(const std::vector<Geom>& segments) {
  if (segments.empty())
    poly_error(ErrorCode::InvalidArgument, "at least 1 segment is required (got 0)");
  std::vector<Segment_2> segs;
  segs.reserve(segments.size());
  for (std::size_t i = 0; i < segments.size(); ++i) segs.push_back(segment_arg(segments[i], i));
  // CGAL's precondition "the end of subcurve n should be the beginning of subcurve n+1" is NOT
  // checked anywhere (Polycurve_2's range ctor just does m_subcurves.assign()), so an unchecked
  // gap would silently produce a broken curve that corrupts an arrangement later.  Check it
  // exactly, with the traits' Equal_2 on the sub-points.
  auto eq = PolylineTypes::traits().equal_2_object();
  for (std::size_t i = 0; i + 1 < segs.size(); ++i)
    if (!eq(segs[i].target(), segs[i + 1].source()))
      poly_error(ErrorCode::InvalidArgument,
                 "segment " + std::to_string(i) + " ends at " + point_text(segs[i].target()) +
                     " but segment " + std::to_string(i + 1) + " starts at " +
                     point_text(segs[i + 1].source()) + "; the segments must chain end to end");
  auto ctr = PolylineTypes::traits().construct_curve_2_object();
  return make_geom(Kind::Polyline, GeomType::Curve, ctr(segs.begin(), segs.end()));
}

Geom make_x_monotone(const std::vector<Geom>& points) {
  const std::vector<Point_2> pts = points_arg(points);
  // Remaining CGAL precondition (Arr_polyline_traits_2.h:571-576, enabled in this build):
  // every consecutive pair must have the same Compare_x_2 and Compare_xy_2 result as the first
  // pair, i.e. the chain is strictly x-monotone, or vertical and monotone in y.  Violating it
  // raises CGAL::Precondition_exception, which the Cython layer turns into PreconditionError —
  // the behaviour ops.hpp documents for this function.
  auto ctr = PolylineTypes::traits().construct_x_monotone_curve_2_object();
  return make_geom(Kind::Polyline, GeomType::XCurve, ctr(pts.begin(), pts.end()));
}

std::size_t number_of_subcurves(const Geom& c) { return chain(c).number_of_subcurves(); }

Geom subcurve(const Geom& c, std::size_t i) {
  const Curve_2& p = chain(c);
  const std::size_t n = p.number_of_subcurves();
  if (i >= n)
    poly_error(ErrorCode::InvalidArgument,
               "subcurve index " + std::to_string(i) + " is out of range (the polyline has " +
                   std::to_string(n) + " subcurves)");
  return box_segment(p[i]);
}

/// number_of_subcurves() + 1 for a non-empty polyline, 0 for an empty one — computed directly
/// rather than through Number_of_points_2, whose Arr_polycurve_traits_2 override HIDES the base
/// X_monotone overload (traits_segment_linear_polyline.md gotcha 4).
std::size_t number_of_points(const Geom& c) {
  const std::size_t n = chain(c).number_of_subcurves();
  return n == 0 ? 0 : n + 1;
}

/// The chain's i-th vertex: subcurve 0's source, then each subcurve's target.  This is exactly
/// what Polycurve_2::Point_const_iterator does, and it follows the STORED direction of the
/// subcurves, so for a right-to-left x-monotone polyline point(0) is the rightmost vertex (the
/// curve's source) — consistent with xcurve_source()/xcurve_target().
Geom point(const Geom& c, std::size_t i) {
  const Curve_2& p = chain(c);
  const std::size_t n = p.number_of_subcurves();
  const std::size_t npts = (n == 0) ? 0 : n + 1;
  if (i >= npts)
    poly_error(ErrorCode::InvalidArgument,
               "point index " + std::to_string(i) + " is out of range (the polyline has " +
                   std::to_string(npts) + " points)");
  return box_segment_point(i == 0 ? p[0].source() : p[i - 1].target());
}

}  // namespace polyline

// ===========================================================================
// 4. The arrangement implementation for this kind
// ===========================================================================
// KindPolicy<PolylineTypes> is already specialised in impl/arr_impl.hpp (naive / simple / walk /
// landmarks / trapezoid point location, no triangulation — Arr_polyline_traits_2 has no `Kernel`
// typedef; vertical ray shooting via simple/walk/trapezoid; the free CGAL::is_valid() works), so
// this TU only has to instantiate the template.
template class arr2d::ArrImpl<arr2d::PolylineTypes>;

// ===========================================================================
// 5. Registration
// ===========================================================================
namespace {
struct Registrar {
  Registrar() {
    register_kind(Kind::Polyline,
                  KindEntry{&polyline_ops(),
                            [] { return std::unique_ptr<ArrBase>(new ArrImpl<PolylineTypes>()); },
                            // No Boolean set operations: CGAL ships no Gps traits for polylines
                            // and bso.hpp declares no make_polygon_set_polyline.
                            nullptr});
  }
};
Registrar registrar;
}  // namespace

}  // namespace arr2d
