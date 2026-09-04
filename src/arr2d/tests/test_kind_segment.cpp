// arr2d — C++ unit test for the Kind::Segment translation unit (src/arr2d/src/kind_segment.cpp).
//
// Self-contained (no test framework): every check is a CHECK(...) that counts, prints the failing
// expression and keeps going.  main() returns 0 only when every check passed AND the process exits
// normally — the latter also proves that nothing in this kind aborts during static destruction.
//
// ---------------------------------------------------------------------------------------------
// BUILD & RUN  (paths shortened: SRC = <repo>/src/arr2d, OUT = a scratch directory)
//
// (a) WITHOUT the Boolean-set-operations TU — the standard way to run this test.
//     kind_segment.cpp registers &arr2d::make_polygon_set_segment as its polygon-set factory;
//     that is only a function POINTER, but the symbol must still resolve at link time, so this
//     file provides a stub under -DARR2D_TEST_STUB_BSO:
//
//       for f in registry numbers overlay kind_segment; do \
//         /usr/bin/clang++ -std=c++17 -O0 -g -DCGAL_USE_CORE -DCGAL_USE_GMP -DCGAL_USE_MPFR \
//           -I/opt/homebrew/include -I$SRC/include -c $SRC/src/$f.cpp -o $OUT/$f.o & done; wait
//       /usr/bin/clang++ -std=c++17 -O0 -g -DARR2D_TEST_STUB_BSO -DCGAL_USE_CORE -DCGAL_USE_GMP \
//         -DCGAL_USE_MPFR -I/opt/homebrew/include -I$SRC/include \
//         -c $SRC/tests/test_kind_segment.cpp -o $OUT/test_kind_segment.o
//       /usr/bin/clang++ -std=c++17 -O0 -g -o $OUT/test_kind_segment \
//         $OUT/test_kind_segment.o $OUT/kind_segment.o $OUT/registry.o $OUT/numbers.o \
//         $OUT/overlay.o -L/opt/homebrew/lib -lgmp -lmpfr
//       $OUT/test_kind_segment
//
// (b) WITH the real Boolean-set-operations TU (src/arr2d/src/bso_segment.cpp):
//     compile it as well, drop -DARR2D_TEST_STUB_BSO and add $OUT/bso_segment.o to the link line.
//     Everything else is identical; three extra checks then run make_polygon_set(Kind::Segment)
//     to prove the factory pointer the registrar installed really works end to end.
//     Measured: (a) 405 checks / 0 failures, (b) 408 checks / 0 failures.
// ---------------------------------------------------------------------------------------------
//
// Only the segment kind is linked, so ops(Kind::Conic) & co. are absent: the conversion checks
// exercise the other Epeck-based kinds by boxing their RAW CGAL objects (header-only) and the
// CORE-based kinds through the "kind not linked" error path.  That is exactly the situation
// kind_segment.cpp is written for (see its LINK CONTRACT comment).

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <CGAL/assertions.h>
#include <CGAL/exceptions.h>

#include "arr2d/arrangement.hpp"
#include "arr2d/common.hpp"
#include "arr2d/impl/number_conv.hpp"
#include "arr2d/kinds/circle_segment_types.hpp"
#include "arr2d/kinds/linear_types.hpp"
#include "arr2d/kinds/polyline_types.hpp"
#include "arr2d/kinds/segment_types.hpp"
#include "arr2d/numbers.hpp"
#include "arr2d/ops.hpp"
#include "arr2d/polygon_set.hpp"
#include "arr2d/registry.hpp"

#ifdef ARR2D_TEST_STUB_BSO
// See (a) above: satisfies the factory symbol referenced by kind_segment.cpp's registrar.
namespace arr2d {
std::unique_ptr<PolygonSetBase> make_polygon_set_segment() { return nullptr; }
}  // namespace arr2d
#endif

using namespace arr2d;

// ===========================================================================
// tiny harness
// ===========================================================================
namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char* expr, const char* file, int line, const std::string& note) {
  ++g_checks;
  if (ok) return;
  ++g_failures;
  std::printf("FAIL %s:%d: %s%s%s\n", file, line, expr, note.empty() ? "" : "  -- ",
              note.c_str());
}

#define CHECK(expr) check(!!(expr), #expr, __FILE__, __LINE__, std::string())
#define CHECK_M(expr, note) check(!!(expr), #expr, __FILE__, __LINE__, std::string(note))

/// Runs `f` and reports whether it threw arr2d::Error with the expected code.
template <class F>
bool throws_error(F f, ErrorCode code, std::string* what = nullptr) {
  try {
    f();
  } catch (const Error& e) {
    if (what) *what = e.what();
    return e.code == code;
  } catch (const std::exception& e) {
    if (what) *what = std::string("unexpected std::exception: ") + e.what();
    return false;
  }
  if (what) *what = "no exception was thrown";
  return false;
}

bool close(double a, double b, double eps = 1e-12) { return std::fabs(a - b) <= eps; }

// ---- shorthands -----------------------------------------------------------
const KindOps& O() { return ops(Kind::Segment); }

Geom P(long x, long y) { return O().make_point(Rational(x), Rational(y)); }
Geom Pq(const Rational& x, const Rational& y) { return O().make_point(x, y); }
Geom S(long x1, long y1, long x2, long y2) {
  return segment::make_xy(Rational(x1), Rational(y1), Rational(x2), Rational(y2));
}

std::pair<double, double> xy(const Geom& p) {
  double v[3] = {0, 0, 0};
  O().point_approx(p, v);
  return {v[0], v[1]};
}

bool same_point(const Geom& a, const Geom& b) { return O().point_equal(a, b); }

}  // namespace

// ===========================================================================
// 1. registry / identity
// ===========================================================================
static void test_registry() {
  std::printf("-- registry --\n");
  CHECK(kind_available(Kind::Segment));
  CHECK(!kind_available(Kind::Conic));       // only this kind's TU is linked
  CHECK(!kind_available(Kind::Linear));
  const KindOps& o = O();
  CHECK(o.kind() == Kind::Segment);
  CHECK(std::string(o.name()) == "segment");
  CHECK(o.dimension() == 2);
  CHECK(!o.is_unbounded_kind());
  CHECK(o.has_polygon_set());
  CHECK(kind_has_polygon_set(Kind::Segment));
  CHECK(kind_from_name("segment") == int(Kind::Segment));
  // The traits is a single process-wide object shared by everything of this kind.
  CHECK(&SegmentTypes::traits() == &SegmentTypes::traits());
#ifndef ARR2D_TEST_STUB_BSO
  // Build (b) only: with the real bso_segment.cpp linked, the factory the registrar installed
  // really does produce a polygon set (with the stub it returns nullptr by construction).
  {
    std::unique_ptr<PolygonSetBase> ps = make_polygon_set(Kind::Segment);
    CHECK(ps != nullptr);
    if (ps) {
      CHECK(ps->kind() == Kind::Segment);
      CHECK(ps->is_empty());
    }
  }
#endif
}

// ===========================================================================
// 2. points
// ===========================================================================
static void test_points() {
  std::printf("-- points --\n");
  const KindOps& o = O();

  const Geom p13 = Pq(Rational(1) / Rational(3), Rational(-2));

  // point_approx: correctly rounded, NOT CGAL::to_double(Epeck::FT) (which gives
  // 0.33333333333333337 for 1/3 — measured; see number_types_and_errors.md gotcha 2).
  double v[3] = {0, 0, 0};
  o.point_approx(p13, v);
  CHECK(v[0] == 0.33333333333333331);        // == (double)(1.0/3.0)
  CHECK(v[1] == -2.0);
  CHECK(o.approximate_coordinate(p13, 0) == v[0]);
  CHECK(o.approximate_coordinate(p13, 1) == v[1]);
  CHECK(throws_error([&] { o.approximate_coordinate(p13, 2); }, ErrorCode::InvalidArgument));

  // certified interval around 1/3: lo <= 1/3 <= hi and both are adjacent doubles
  std::vector<std::pair<double, double>> iv;
  o.point_interval(p13, iv);
  CHECK(iv.size() == 2);
  CHECK(iv[0].first <= 0.33333333333333331 && 0.33333333333333331 <= iv[0].second);
  CHECK(iv[0].first < iv[0].second);         // 1/3 is not a double
  CHECK(iv[1].first == -2.0 && iv[1].second == -2.0);   // -2 is exact

  CHECK(o.point_is_rational(p13));
  std::vector<Rational> ex;
  o.point_exact_rational(p13, ex);
  CHECK(ex.size() == 2);
  CHECK(ex[0] == Rational(1) / Rational(3));
  CHECK(ex[1] == Rational(-2));

  std::vector<Geom> nums;
  o.point_exact(p13, nums);
  CHECK(nums.size() == 2);
  CHECK(number_kind(nums[0]) == NumberKind::Rational);
  CHECK(number_repr(nums[0]) == "1/3");
  CHECK(number_repr(nums[1]) == "-2");

  CHECK(o.point_repr(p13) == "Point(1/3, -2)");
  CHECK(o.point_repr(P(0, 0)) == "Point(0, 0)");
  CHECK(o.point_repr(Pq(Rational(-3) / Rational(6), Rational(4) / Rational(2))) == "Point(-1/2, 2)");

  // comparisons
  CHECK(o.point_compare_x(P(1, 5), P(2, 0)) == -1);
  CHECK(o.point_compare_x(P(2, 5), P(2, 0)) == 0);
  CHECK(o.point_compare_xy(P(2, 5), P(2, 0)) == 1);      // same x, larger y
  CHECK(o.point_compare_xy(P(2, 0), P(2, 0)) == 0);
  CHECK(o.point_equal(P(2, 0), Pq(Rational(4) / Rational(2), Rational(0))));
  CHECK(!o.point_equal(P(2, 0), P(2, 1)));

  // make_point_3 is a sphere-only entry point
  CHECK(throws_error([&] { o.make_point_3(Rational(1), Rational(0), Rational(0)); },
                     ErrorCode::Unsupported));

  // ---- convert_point ------------------------------------------------------
  CHECK(same_point(o.convert_point(P(3, 4)), P(3, 4)));   // identity

  // a raw Epeck point boxed as another Epeck-based kind (linear / polyline share Point_2)
  const Geom lin_pt = make_geom(Kind::Linear, GeomType::Point, LinearTypes::Point_2(7, -1));
  CHECK(same_point(o.convert_point(lin_pt), P(7, -1)));
  const Geom poly_pt = make_geom(Kind::Polyline, GeomType::Point, PolylineTypes::Point_2(-5, 2));
  CHECK(same_point(o.convert_point(poly_pt), P(-5, 2)));

  // circle-segment points: rational ones convert, sqrt-extended ones do not
  using CS = CircleSegmentTypes;
  using CoordNT = CS::Point_2::CoordNT;
  const Geom cs_rat =
      make_geom(Kind::CircleSegment, GeomType::Point, CS::Point_2(CoordNT(3), CoordNT(-4)));
  CHECK(same_point(o.convert_point(cs_rat), P(3, -4)));
  // 1 + 3*sqrt(4) == 7 is "extended" but rational (exact_coordinates_contract.md gotcha 3)
  const Geom cs_sq = make_geom(Kind::CircleSegment, GeomType::Point,
                               CS::Point_2(CoordNT(CS::FT(1), CS::FT(3), CS::FT(4)), CoordNT(0)));
  CHECK(same_point(o.convert_point(cs_sq), P(7, 0)));
  const Geom cs_irr = make_geom(Kind::CircleSegment, GeomType::Point,
                                CS::Point_2(CoordNT(CS::FT(1), CS::FT(1), CS::FT(5)), CoordNT(0)));
  CHECK(throws_error([&] { o.convert_point(cs_irr); }, ErrorCode::NotRepresentable));

  // sphere points are 3D directions
  const Geom fake_sphere_pt = make_geom(Kind::Sphere, GeomType::Point, 0.0);
  CHECK(throws_error([&] { o.convert_point(fake_sphere_pt); }, ErrorCode::KindMismatch));

  // a kind whose TU is not linked
  const Geom fake_conic_pt = make_geom(Kind::Conic, GeomType::Point, 0.0);
  std::string msg;
  CHECK(throws_error([&] { o.convert_point(fake_conic_pt); }, ErrorCode::Unsupported, &msg));
  CHECK_M(msg.find("not linked") != std::string::npos, msg);

  // wrong Geom flavour
  CHECK(throws_error([&] { o.point_approx(S(0, 0, 1, 1), v); }, ErrorCode::InvalidArgument));
  CHECK(throws_error([&] { o.point_repr(Geom()); }, ErrorCode::InvalidArgument));
}

// ===========================================================================
// 3. curve construction + accessors
// ===========================================================================
static void test_curves() {
  std::printf("-- curves --\n");
  const KindOps& o = O();

  // ---- the two constructors of namespace arr2d::segment -------------------
  const Geom s1 = segment::make(P(0, 0), P(4, 2));
  const Geom s2 = segment::make_xy(Rational(0), Rational(0), Rational(4), Rational(2));
  CHECK(s1.kind == Kind::Segment);
  CHECK(s1.type == GeomType::XCurve);        // a segment is always x-monotone
  CHECK(o.curve_equal(s1, s2));

  // constructors accept points of any planar kind with rational coordinates
  const Geom from_linear_pts =
      segment::make(make_geom(Kind::Linear, GeomType::Point, LinearTypes::Point_2(0, 0)), P(4, 2));
  CHECK(o.curve_equal(s1, from_linear_pts));

  // degenerate segments are rejected before CGAL's precondition fires
  CHECK(throws_error([&] { segment::make(P(1, 1), P(1, 1)); }, ErrorCode::InvalidArgument));
  CHECK(throws_error(
      [&] { segment::make_xy(Rational(1), Rational(1), Rational(1), Rational(1)); },
      ErrorCode::InvalidArgument));

  // ---- endpoints / supporting line ---------------------------------------
  Geom src, tgt;
  segment::endpoints(s1, src, tgt);
  CHECK(same_point(src, P(0, 0)));
  CHECK(same_point(tgt, P(4, 2)));

  // CGAL builds the line through (0,0) and (4,2) as a*x + b*y + c = 0 with
  // a = sy - ty = -2, b = tx - sx = 4, c = sx*ty - tx*sy = 0.  (Verified against CGAL.)
  Rational la, lb, lc;
  segment::supporting_line(s1, la, lb, lc);
  CHECK(la == Rational(-2));
  CHECK(lb == Rational(4));
  CHECK(lc == Rational(0));
  // the endpoints satisfy the equation
  CHECK(la * Rational(0) + lb * Rational(0) + lc == Rational(0));
  CHECK(la * Rational(4) + lb * Rational(2) + lc == Rational(0));

  // ---- x-monotonicity -----------------------------------------------------
  CHECK(o.is_x_monotone(s1));
  std::vector<Geom> pieces;
  o.make_x_monotone(s1, pieces);
  CHECK(pieces.size() == 1);
  CHECK(pieces[0].type == GeomType::XCurve);
  CHECK(o.curve_equal(pieces[0], s1));
  // ... also for a Curve box (Curve_2 and X_monotone_curve_2 are the same C++ type here)
  const Geom as_curve = o.to_curve(s1);
  CHECK(as_curve.type == GeomType::Curve);
  pieces.clear();
  o.make_x_monotone(as_curve, pieces);
  CHECK(pieces.size() == 1);
  CHECK(o.to_x_monotone(as_curve).type == GeomType::XCurve);
  CHECK(o.is_x_monotone(as_curve));

  // ---- x-monotone accessors ----------------------------------------------
  CHECK(same_point(o.xcurve_source(s1), P(0, 0)));
  CHECK(same_point(o.xcurve_target(s1), P(4, 2)));
  CHECK(o.xcurve_has_source(s1));
  CHECK(o.xcurve_has_target(s1));
  CHECK(same_point(o.xcurve_min_vertex(s1), P(0, 0)));
  CHECK(same_point(o.xcurve_max_vertex(s1), P(4, 2)));
  CHECK(!o.xcurve_is_vertical(s1));
  CHECK(o.xcurve_is_directed_right(s1));
  CHECK(o.compare_endpoints_xy(s1) == -1);
  CHECK(o.curve_is_bounded(s1));
  CHECK(o.parameter_space_in_x(s1, ARR_MIN_END) == ARR_INTERIOR);
  CHECK(o.parameter_space_in_y(s1, ARR_MAX_END) == ARR_INTERIOR);

  const Geom rev = o.construct_opposite(s1);
  CHECK(same_point(o.xcurve_source(rev), P(4, 2)));
  CHECK(same_point(o.xcurve_target(rev), P(0, 0)));
  CHECK(o.compare_endpoints_xy(rev) == 1);
  CHECK(!o.xcurve_is_directed_right(rev));
  CHECK(o.curve_equal(rev, s1));             // Equal_2 is direction-insensitive
  CHECK(same_point(o.xcurve_min_vertex(rev), P(0, 0)));   // min/max stay lexicographic

  const Geom vert = S(1, 5, 1, -3);
  CHECK(o.xcurve_is_vertical(vert));
  CHECK(!o.xcurve_is_directed_right(vert));  // (1,5) is lexicographically larger than (1,-3)
  CHECK(same_point(o.xcurve_min_vertex(vert), P(1, -3)));

  // ---- repr ---------------------------------------------------------------
  CHECK(o.curve_repr(s1) == "Segment((0, 0), (4, 2))");
  CHECK(o.curve_repr(S(0, 0, 4, 0)) == "Segment((0, 0), (4, 0))");
  CHECK(o.curve_repr(segment::make(Pq(Rational(1) / Rational(2), Rational(0)), P(4, 0))) ==
        "Segment((1/2, 0), (4, 0))");

  // ---- bbox ---------------------------------------------------------------
  const BBox b = o.curve_bbox(S(4, 2, 0, -1));
  CHECK(b.dim == 2);
  CHECK(b.lo[0] == 0.0 && b.hi[0] == 4.0);
  CHECK(b.lo[1] == -1.0 && b.hi[1] == 2.0);
  // a non-dyadic coordinate gives a certified (outward-rounded) box
  const BBox b2 = o.curve_bbox(segment::make(Pq(Rational(1) / Rational(3), Rational(0)), P(1, 1)));
  CHECK(b2.lo[0] <= 0.33333333333333331 && b2.hi[0] == 1.0);

  // ---- approximate --------------------------------------------------------
  std::vector<double> poly;
  o.approximate(s1, 1e-3, nullptr, poly);
  CHECK(poly.size() == 4);                                    // exactly the two endpoints
  CHECK(poly[0] == 0.0 && poly[1] == 0.0);
  CHECK(poly[2] == 4.0 && poly[3] == 2.0);
  // ... and it starts/ends exactly at the endpoint approximations (same rounding as point_approx)
  CHECK(poly[0] == xy(o.xcurve_source(s1)).first && poly[1] == xy(o.xcurve_source(s1)).second);
  CHECK(poly[2] == xy(o.xcurve_target(s1)).first && poly[3] == xy(o.xcurve_target(s1)).second);
  // the polyline follows the curve from SOURCE to TARGET as stored (not min -> max)
  std::vector<double> rpoly;
  o.approximate(rev, 1e-3, nullptr, rpoly);
  CHECK(rpoly.size() == 4);
  CHECK(rpoly[0] == 4.0 && rpoly[1] == 2.0 && rpoly[2] == 0.0 && rpoly[3] == 0.0);
  // sample points of the chord lie on the true segment y = x/2 (well within the tolerance)
  for (int k = 0; k <= 4; ++k) {
    const double t = k / 4.0;
    const double x = poly[0] + t * (poly[2] - poly[0]);
    const double y = poly[1] + t * (poly[3] - poly[1]);
    CHECK(close(y, x / 2.0, 1e-15));
  }
  // a non-dyadic endpoint is still reproduced exactly by the correctly rounded conversion
  std::vector<double> poly13;
  o.approximate(segment::make(Pq(Rational(1) / Rational(3), Rational(0)), P(1, 0)), 1e-3, nullptr,
                poly13);
  CHECK(poly13.size() == 4 && poly13[0] == 0.33333333333333331);
  // tolerance validation (rendering_and_approximation.md gotcha 7)
  CHECK(throws_error([&] { std::vector<double> t; o.approximate(s1, 0.0, nullptr, t); },
                     ErrorCode::InvalidArgument));
  CHECK(throws_error([&] { std::vector<double> t; o.approximate(s1, -1.0, nullptr, t); },
                     ErrorCode::InvalidArgument));
  CHECK(throws_error([&] { std::vector<double> t; o.approximate(s1, std::nan(""), nullptr, t); },
                     ErrorCode::InvalidArgument));
  // a tiny tolerance is clamped, not rejected
  { std::vector<double> t; o.approximate(s1, 1e-300, nullptr, t); CHECK(t.size() == 4); }

  // length of the 3-4-5 triangle's hypotenuse
  CHECK(close(o.approximate_length(S(0, 0, 3, 4), 1e-3), 5.0, 1e-12));
}

// ===========================================================================
// 4. traits functors (the generic KindOpsBase layer, on segment geometry)
// ===========================================================================
static void test_traits_ops() {
  std::printf("-- traits ops --\n");
  const KindOps& o = O();

  const Geom diag = S(0, 0, 4, 4);
  const Geom anti = S(0, 4, 4, 0);

  // intersect: the two diagonals of the square [0,4]^2 meet at (2,2), transversally
  std::vector<IntersectionResult> ir;
  o.intersect(diag, anti, ir);
  CHECK(ir.size() == 1);
  CHECK(ir[0].is_point);
  CHECK(same_point(ir[0].point, P(2, 2)));
  CHECK(ir[0].multiplicity == 1);

  // overlapping collinear segments -> an overlap curve (1,1)-(3,3)
  ir.clear();
  o.intersect(S(0, 0, 3, 3), S(1, 1, 4, 4), ir);
  CHECK(ir.size() == 1);
  CHECK(!ir[0].is_point);
  CHECK(o.curve_equal(ir[0].overlap, S(1, 1, 3, 3)));

  // disjoint
  ir.clear();
  o.intersect(S(0, 0, 1, 0), S(0, 1, 1, 1), ir);
  CHECK(ir.empty());

  // split at an interior point
  Geom left, right;
  o.split(diag, P(2, 2), left, right);
  CHECK(o.curve_equal(left, S(0, 0, 2, 2)));
  CHECK(o.curve_equal(right, S(2, 2, 4, 4)));

  // merge back
  CHECK(o.are_mergeable(left, right));
  CHECK(o.curve_equal(o.merge(left, right), diag));
  CHECK(!o.are_mergeable(diag, anti));
  CHECK(throws_error([&] { o.merge(diag, anti); }, ErrorCode::InvalidArgument));

  // trim
  CHECK(o.curve_equal(o.trim(diag, P(1, 1), P(3, 3)), S(1, 1, 3, 3)));
  CHECK(throws_error([&] { o.trim(diag, P(1, 1), P(1, 1)); }, ErrorCode::InvalidArgument));

  // compare_y_at_x / is_in_x_range
  CHECK(o.compare_y_at_x(P(2, 3), diag) == 1);
  CHECK(o.compare_y_at_x(P(2, 2), diag) == 0);
  CHECK(o.compare_y_at_x(P(2, 1), diag) == -1);
  CHECK(o.is_in_x_range(diag, P(2, 100)));
  CHECK(!o.is_in_x_range(diag, P(5, 0)));
  CHECK(throws_error([&] { o.compare_y_at_x(P(5, 0), diag); }, ErrorCode::InvalidArgument));

  // compare_y_at_x_left / _right around the crossing point (2,2)
  CHECK(o.compare_y_at_x_right(diag, anti, P(2, 2)) == 1);   // the ascending diagonal is above
  CHECK(o.compare_y_at_x_left(diag, anti, P(2, 2)) == -1);

  // construct_x_monotone_curve(p, q)
  CHECK(o.curve_equal(o.construct_x_monotone_curve(P(0, 0), P(4, 4)), diag));
  CHECK(throws_error([&] { o.construct_x_monotone_curve(P(0, 0), P(0, 0)); },
                     ErrorCode::InvalidArgument));

  // a general curve where an x-monotone one is required is still fine for this kind
  // (both boxes hold the same C++ type), but a POINT box is not
  CHECK(throws_error([&] { o.xcurve_source(P(0, 0)); }, ErrorCode::InvalidArgument));
}

// ===========================================================================
// 5. convert_curve
// ===========================================================================
static void test_convert_curve() {
  std::printf("-- convert_curve --\n");
  const KindOps& o = O();
  std::vector<Geom> out;

  // ---- segment -> segment (identity, normalised to an XCurve box) ---------
  o.convert_curve(o.to_curve(S(0, 0, 2, 2)), out);
  CHECK(out.size() == 1 && out[0].type == GeomType::XCurve);
  CHECK(o.curve_equal(out[0], S(0, 0, 2, 2)));

  // ---- linear -------------------------------------------------------------
  using LK = LinearTypes::Kernel;
  const LinearTypes::Curve_2 lseg(LK::Segment_2(LinearTypes::Point_2(0, 0),
                                                LinearTypes::Point_2(3, 6)));
  const LinearTypes::Curve_2 lray(LK::Ray_2(LinearTypes::Point_2(0, 0),
                                            LinearTypes::Point_2(1, 1)));
  const LinearTypes::Curve_2 lline(LK::Line_2(LinearTypes::Point_2(0, 0),
                                              LinearTypes::Point_2(1, 1)));
  out.clear();
  o.convert_curve(make_geom(Kind::Linear, GeomType::Curve, lseg), out);
  CHECK(out.size() == 1);
  CHECK(o.curve_equal(out[0], S(0, 0, 3, 6)));
  CHECK(same_point(o.xcurve_source(out[0]), P(0, 0)));
  CHECK(same_point(o.xcurve_target(out[0]), P(3, 6)));
  CHECK(throws_error(
      [&] { std::vector<Geom> t; o.convert_curve(make_geom(Kind::Linear, GeomType::Curve, lray), t); },
      ErrorCode::NotRepresentable));
  CHECK(throws_error(
      [&] { std::vector<Geom> t; o.convert_curve(make_geom(Kind::Linear, GeomType::Curve, lline), t); },
      ErrorCode::NotRepresentable));

  // ---- polyline: one segment per subcurve ---------------------------------
  {
    PolylineTypes::Traits ptraits;     // a local traits: only used to build the test input
    const std::vector<PolylineTypes::Point_2> pts = {PolylineTypes::Point_2(0, 0),
                                                     PolylineTypes::Point_2(1, 1),
                                                     PolylineTypes::Point_2(2, 0),
                                                     PolylineTypes::Point_2(3, 3)};
    const PolylineTypes::Curve_2 pc =
        ptraits.construct_curve_2_object()(pts.begin(), pts.end());
    CHECK(pc.number_of_subcurves() == 3);
    out.clear();
    o.convert_curve(make_geom(Kind::Polyline, GeomType::Curve, pc), out);
    CHECK(out.size() == 3);
    CHECK(o.curve_equal(out[0], S(0, 0, 1, 1)));
    CHECK(o.curve_equal(out[1], S(1, 1, 2, 0)));
    CHECK(o.curve_equal(out[2], S(2, 0, 3, 3)));
    // the chain is preserved: target(i) == source(i+1)
    for (std::size_t i = 0; i + 1 < out.size(); ++i)
      CHECK(same_point(o.xcurve_target(out[i]), o.xcurve_source(out[i + 1])));

    // an x-monotone polyline box works as well
    const std::vector<PolylineTypes::Point_2> mono = {PolylineTypes::Point_2(0, 0),
                                                      PolylineTypes::Point_2(1, 2),
                                                      PolylineTypes::Point_2(4, 1)};
    const PolylineTypes::X_monotone_curve_2 mx =
        ptraits.construct_x_monotone_curve_2_object()(mono.begin(), mono.end());
    out.clear();
    o.convert_curve(make_geom(Kind::Polyline, GeomType::XCurve, mx), out);
    CHECK(out.size() == 2);
    CHECK(o.curve_equal(out[0], S(0, 0, 1, 2)));
    CHECK(o.curve_equal(out[1], S(1, 2, 4, 1)));
  }

  // ---- circle_segment -----------------------------------------------------
  {
    using CS = CircleSegmentTypes;
    using CoordNT = CS::Point_2::CoordNT;
    CS::Traits ctraits;                // local: only used to build the test input

    // (a) a straight circle-segment curve with rational endpoints -> a segment
    const CS::Curve_2 cs_line(CS::Kernel::Point_2(0, 0), CS::Kernel::Point_2(3, 4));
    CHECK(cs_line.is_linear());
    out.clear();
    o.convert_curve(make_geom(Kind::CircleSegment, GeomType::Curve, cs_line), out);
    CHECK(out.size() == 1);
    CHECK(o.curve_equal(out[0], S(0, 0, 3, 4)));

    // ... and the same through its x-monotone piece
    std::vector<std::variant<CS::Point_2, CS::X_monotone_curve_2>> mx;
    ctraits.make_x_monotone_2_object()(cs_line, std::back_inserter(mx));
    CHECK(mx.size() == 1);
    const CS::X_monotone_curve_2& cx = *std::get_if<CS::X_monotone_curve_2>(&mx[0]);
    CHECK(cx.is_linear());
    out.clear();
    o.convert_curve(make_geom(Kind::CircleSegment, GeomType::XCurve, cx), out);
    CHECK(out.size() == 1);
    CHECK(o.curve_equal(out[0], S(0, 0, 3, 4)));

    // (b) a circular arc is not a straight segment
    const CS::Curve_2 circle(CS::Kernel::Point_2(0, 0), CS::FT(2), CGAL::COUNTERCLOCKWISE);
    CHECK(circle.is_full());
    CHECK(throws_error(
        [&] {
          std::vector<Geom> t;
          o.convert_curve(make_geom(Kind::CircleSegment, GeomType::Curve, circle), t);
        },
        ErrorCode::NotRepresentable));
    std::vector<std::variant<CS::Point_2, CS::X_monotone_curve_2>> arcs;
    ctraits.make_x_monotone_2_object()(circle, std::back_inserter(arcs));
    CHECK(arcs.size() == 2);           // a full circle splits into an upper and a lower arc
    const CS::X_monotone_curve_2& arc = *std::get_if<CS::X_monotone_curve_2>(&arcs[0]);
    CHECK(throws_error(
        [&] {
          std::vector<Geom> t;
          o.convert_curve(make_geom(Kind::CircleSegment, GeomType::XCurve, arc), t);
        },
        ErrorCode::NotRepresentable));

    // (c) a straight piece with an irrational endpoint: exact conversion impossible.
    // The segment from (1 + sqrt(5), 0) to (4, 0) lies on the line y = 0.
    const CS::Point_2 irr(CoordNT(CS::FT(1), CS::FT(1), CS::FT(5)), CoordNT(0));
    const CS::Point_2 rat(CoordNT(4), CoordNT(0));
    const CS::Curve_2 cs_irr(CS::Kernel::Line_2(0, 1, 0), irr, rat);   // y = 0
    CHECK(cs_irr.is_linear());
    CHECK(throws_error(
        [&] {
          std::vector<Geom> t;
          o.convert_curve(make_geom(Kind::CircleSegment, GeomType::Curve, cs_irr), t);
        },
        ErrorCode::NotRepresentable));
  }

  // ---- sphere -------------------------------------------------------------
  CHECK(throws_error(
      [&] {
        std::vector<Geom> t;
        o.convert_curve(make_geom(Kind::Sphere, GeomType::XCurve, 0.0), t);
      },
      ErrorCode::NotRepresentable));

  // ---- conic / Bezier: those TUs are not linked here ----------------------
  std::string msg;
  CHECK(throws_error(
      [&] {
        std::vector<Geom> t;
        o.convert_curve(make_geom(Kind::Conic, GeomType::XCurve, 0.0), t);
      },
      ErrorCode::Unsupported, &msg));
  CHECK_M(msg.find("conic") != std::string::npos && msg.find("not linked") != std::string::npos,
          msg);
  CHECK(throws_error(
      [&] {
        std::vector<Geom> t;
        o.convert_curve(make_geom(Kind::Bezier, GeomType::Curve, 0.0), t);
      },
      ErrorCode::Unsupported));

  // ---- bad input ----------------------------------------------------------
  CHECK(throws_error([&] { std::vector<Geom> t; o.convert_curve(P(0, 0), t); },
                     ErrorCode::InvalidArgument));
  CHECK(throws_error([&] { std::vector<Geom> t; o.convert_curve(Geom(), t); },
                     ErrorCode::InvalidArgument));
  // right kind tag, wrong C++ payload
  CHECK(throws_error(
      [&] { std::vector<Geom> t; o.convert_curve(make_geom(Kind::Linear, GeomType::Curve, 0.0), t); },
      ErrorCode::InvalidArgument));
}

// ===========================================================================
// 6. arrangement round trip
// ===========================================================================
//
// The test arrangement (all numbers below are hand-derived):
//
//     (0,4) +-----------+ (4,4)
//           |           |
//   (-1,2) -+-----------+- (5,2)      the horizontal curve runs from (-1,2) to (5,2)
//           |           |
//     (0,0) +-----------+ (4,0)
//
//   vertices  : 4 square corners + (0,2) + (4,2) + (-1,2) + (5,2)          = 8
//   edges     : bottom 1 + right 2 + top 1 + left 2 + horizontal 3          = 9
//   faces     : unbounded + lower rectangle + upper rectangle               = 3
//   Euler     : V - E + F = 8 - 9 + 3 = 2                                    ok
//   curves    : 4 (square) + 1 (horizontal)                                 = 5
static std::unique_ptr<ArrBase> build_arrangement() {
  std::unique_ptr<ArrBase> arr = make_arrangement(Kind::Segment);
  const std::vector<Geom> square = {S(0, 0, 4, 0), S(4, 0, 4, 4), S(4, 4, 0, 4), S(0, 4, 0, 0)};
  std::vector<CH> handles;
  arr->insert_curves(square, handles);          // aggregate (sweep) insertion
  return arr;
}

static void test_arrangement() {
  std::printf("-- arrangement --\n");
  const KindOps& o = O();

  std::unique_ptr<ArrBase> arr = build_arrangement();
  CHECK(arr->kind() == Kind::Segment);
  CHECK(&arr->ops() == &o);
  CHECK(!arr->is_unbounded_kind());
  CHECK(arr->number_of_vertices() == 4);
  CHECK(arr->number_of_edges() == 4);
  CHECK(arr->number_of_faces() == 2);
  CHECK(arr->number_of_curves() == 4);
  CHECK(arr->is_valid());

  const CH cross = arr->insert_curve(S(-1, 2, 5, 2));
  CHECK(arr->curve_valid(cross));
  CHECK(arr->number_of_vertices() == 8);
  CHECK(arr->number_of_edges() == 9);
  CHECK(arr->number_of_halfedges() == 18);
  CHECK(arr->number_of_faces() == 3);
  CHECK(arr->number_of_unbounded_faces() == 1);
  CHECK(arr->number_of_curves() == 5);
  CHECK(arr->number_of_isolated_vertices() == 0);
  CHECK(arr->number_of_vertices_at_infinity() == 0);
  CHECK(!arr->is_empty());
  CHECK(arr->is_valid());
  CHECK(arr->number_of_induced_edges(cross) == 3);

  // ---- iteration ----------------------------------------------------------
  std::vector<VH> vs;
  std::vector<HH> hs, es;
  std::vector<FH> fs, ufs;
  std::vector<CH> cs;
  arr->vertices(vs);
  arr->halfedges(hs);
  arr->edges(es);
  arr->faces(fs);
  arr->unbounded_faces(ufs);
  arr->curves(cs);
  CHECK(vs.size() == 8 && hs.size() == 18 && es.size() == 9 && fs.size() == 3);
  CHECK(ufs.size() == 1 && cs.size() == 5);
  CHECK(arr->face_is_unbounded(ufs[0]));
  CHECK(arr->unbounded_face() == ufs[0]);
  // degree sum over all vertices == number of halfedges
  std::size_t deg = 0;
  for (const VH& v : vs) deg += arr->vertex_degree(v);
  CHECK(deg == 18);
  // every vertex point is one of the eight expected points
  int found = 0;
  const long expected[8][2] = {{0, 0}, {4, 0}, {4, 4}, {0, 4}, {0, 2}, {4, 2}, {-1, 2}, {5, 2}};
  for (const VH& v : vs) {
    const Geom p = arr->vertex_point(v);
    for (const auto& e : expected)
      if (same_point(p, P(e[0], e[1]))) ++found;
  }
  CHECK(found == 8);
  CHECK(arr->curve_geometry(cs[4]).kind == Kind::Segment);

  // ---- bbox / bulk export -------------------------------------------------
  const BBox bb = arr->bbox();
  CHECK(bb.lo[0] == -1.0 && bb.hi[0] == 5.0 && bb.lo[1] == 0.0 && bb.hi[1] == 4.0);
  std::vector<double> coords;
  arr->vertex_coordinates(coords);
  CHECK(coords.size() == 16);
  std::vector<std::size_t> idx;
  arr->edge_vertex_indices(idx);
  CHECK(idx.size() == 18);
  std::vector<std::vector<std::vector<std::size_t>>> fb;
  arr->face_boundaries(fb);
  CHECK(fb.size() == 3);

  // ---- point location, every strategy KindPolicy<SegmentTypes> allows ------
  const Geom p_up(P(2, 3)), p_low(P(2, 1)), p_out(P(10, 10));
  const Located up = arr->locate(p_up, PL_DEFAULT);
  const Located low = arr->locate(p_low, PL_DEFAULT);
  const Located out = arr->locate(p_out, PL_DEFAULT);
  CHECK(up.type == 2 && low.type == 2 && out.type == 2);
  CHECK(!(up.as_face() == low.as_face()));
  CHECK(!arr->face_is_unbounded(up.as_face()));
  CHECK(!arr->face_is_unbounded(low.as_face()));
  CHECK(arr->face_is_unbounded(out.as_face()));

  // every one of the six strategies is supported by this kind and agrees with the default
  // NB on PL_TRIANGULATION: KindPolicy<SegmentTypes> enables it (segment is the only kind with
  // straight edges and a Kernel), but CGAL's triangulation strategy is known to answer with the
  // wrong face for faces WITH HOLES (CGAL_TRAPS_CHECKLIST.md, point location).  The queries below
  // deliberately stay on faces it handles correctly.
  const int strategies[6] = {PL_NAIVE, PL_SIMPLE, PL_WALK, PL_LANDMARKS, PL_TRAPEZOID,
                             PL_TRIANGULATION};
  for (int s : strategies) {
    CHECK_M(arr->supports_point_location(s), point_location_name(s));
    // as a temporary strategy object ...
    CHECK_M(arr->locate(p_up, s).as_face() == up.as_face(), point_location_name(s));
    // ... and attached (trapezoid/landmarks keep themselves up to date through observers)
    arr->attach_point_location(s);
    CHECK_M(arr->has_point_location(s), point_location_name(s));
    CHECK_M(arr->locate(p_low, s).as_face() == low.as_face(), point_location_name(s));
    CHECK_M(arr->locate(p_out, s).as_face() == out.as_face(), point_location_name(s));
    arr->detach_point_location(s);
    CHECK_M(!arr->has_point_location(s), point_location_name(s));
  }
  CHECK(!arr->supports_point_location(PL_NUM_STRATEGIES));
  // an unknown strategy id is reported as "not available for this kind" (ArrImpl::require_pl)
  CHECK(throws_error([&] { arr->locate(p_up, 42); }, ErrorCode::Unsupported));
  CHECK(throws_error([&] { arr->attach_point_location(PL_DEFAULT); }, ErrorCode::InvalidArgument));

  // a vertex and an edge query
  CHECK(arr->locate(P(0, 0)).type == 0);
  CHECK(arr->locate(P(2, 0)).type == 1);
  CHECK(arr->locate(P(0, 2)).type == 0);          // the crossing point on the left edge

  // ---- vertical ray shooting: only simple / walk / trapezoid support it ----
  const Located up_hit = arr->ray_shoot_up(P(2, 1));
  const Located down_hit = arr->ray_shoot_down(P(2, 1));
  CHECK(up_hit.type == 1 && down_hit.type == 1);
  CHECK(o.curve_equal(arr->he_curve(up_hit.as_halfedge()), S(0, 2, 4, 2)));
  CHECK(o.curve_equal(arr->he_curve(down_hit.as_halfedge()), S(0, 0, 4, 0)));
  CHECK(arr->ray_shoot_up(P(2, 5)).type == 2);    // a miss: the unbounded face
  // NB: point location / ray shooting may return EITHER halfedge of the hit edge
  // (point_location_and_decomposition.md: "locate/ray_shoot return an arbitrary twin"), so the
  // strategies are compared up to the twin.
  for (int s : {PL_SIMPLE, PL_WALK, PL_TRAPEZOID}) {
    const HH h = arr->ray_shoot_up(P(2, 1), s).as_halfedge();
    CHECK_M(h == up_hit.as_halfedge() || h == arr->he_twin(up_hit.as_halfedge()),
            point_location_name(s));
  }
  for (int s : {PL_NAIVE, PL_LANDMARKS, PL_TRIANGULATION})
    CHECK_M(throws_error([&] { arr->ray_shoot_up(P(2, 1), s); }, ErrorCode::Unsupported),
            point_location_name(s));

  // ---- batched_locate (results are restored to the input order) -----------
  const std::vector<Geom> queries = {p_up, p_low, p_out, p_up, P(0, 0)};
  std::vector<Located> res;
  arr->batched_locate(queries, res);
  CHECK(res.size() == 5);
  CHECK(res[0].as_face() == up.as_face());
  CHECK(res[1].as_face() == low.as_face());
  CHECK(res[2].as_face() == out.as_face());
  CHECK(res[3].as_face() == up.as_face());        // duplicate query, same answer
  CHECK(res[4].type == 0);                        // a vertex

  // ---- he_curve / he_directed_curve around a face -------------------------
  // The lower rectangle (0,0)-(4,0)-(4,2)-(0,2): 4 halfedges, and following next() the targets
  // chain, with the face always on the LEFT of the directed curve.
  const FH low_face = low.as_face();
  CHECK(arr->face_has_outer_ccb(low_face));
  CHECK(arr->face_number_of_outer_ccbs(low_face) == 1);
  CHECK(arr->face_number_of_inner_ccbs(low_face) == 0);
  std::vector<HH> ccb;
  arr->he_ccb(arr->face_outer_ccb(low_face), ccb);
  CHECK(ccb.size() == 4);
  for (std::size_t i = 0; i < ccb.size(); ++i) {
    const HH h = ccb[i];
    const Geom dc = arr->he_directed_curve(h);
    const Geom sc = arr->he_curve(h);
    CHECK(o.curve_equal(dc, sc));                                        // same geometry
    CHECK(same_point(o.xcurve_source(dc), arr->vertex_point(arr->he_source(h))));
    CHECK(same_point(o.xcurve_target(dc), arr->vertex_point(arr->he_target(h))));
    // the directed curves chain: target(i) == source(i+1) (mod 4)
    const Geom next_dc = arr->he_directed_curve(ccb[(i + 1) % ccb.size()]);
    CHECK(same_point(o.xcurve_target(dc), o.xcurve_source(next_dc)));
    CHECK(arr->he_face(h) == low_face);
    CHECK(arr->he_is_on_outer_ccb(h));
    CHECK(!arr->he_is_fictitious(h));
    CHECK(arr->he_twin(arr->he_twin(h)) == h);
    CHECK(arr->he_next(arr->he_prev(h)) == h);
    // he_direction agrees with the stored curve's orientation
    const bool l2r = (arr->he_direction(h) == ARR_LEFT_TO_RIGHT);
    CHECK(l2r == o.xcurve_is_directed_right(dc));
  }

  // ---- face_polygon -------------------------------------------------------
  std::vector<Geom> outer;
  std::vector<std::vector<Geom>> holes;
  arr->face_polygon(low_face, outer, holes);
  CHECK(outer.size() == 4);
  CHECK(holes.empty());
  for (std::size_t i = 0; i < outer.size(); ++i)
    CHECK(same_point(o.xcurve_target(outer[i]), o.xcurve_source(outer[(i + 1) % outer.size()])));
  // the signed area of the outer boundary is +8 (counterclockwise, 4 x 2 rectangle)
  Rational area2(0);
  for (const Geom& g : outer) {
    std::vector<Rational> a, b;
    o.point_exact_rational(o.xcurve_source(g), a);
    o.point_exact_rational(o.xcurve_target(g), b);
    area2 += a[0] * b[1] - b[0] * a[1];
  }
  CHECK(area2 == Rational(16));                    // 2 * 8
  // the unbounded face has no outer ccb but one hole (the square's outline plus the two spikes)
  std::vector<Geom> uouter;
  std::vector<std::vector<Geom>> uholes;
  arr->face_polygon(out.as_face(), uouter, uholes);
  CHECK(uouter.empty());
  CHECK(uholes.size() == 1);
  CHECK(throws_error([&] { arr->face_outer_ccb(out.as_face()); }, ErrorCode::InvalidArgument));
  CHECK(throws_error([&] { arr->fictitious_face(); }, ErrorCode::Unsupported));

  // ---- zone / do_intersect ------------------------------------------------
  // The vertical segment x = 2 from y = -1 to y = 5 crosses the whole arrangement:
  //   unbounded face, bottom edge, lower face, middle edge, upper face, top edge, unbounded face.
  std::vector<Located> zone;
  arr->zone(S(2, -1, 2, 5), zone);
  int zfaces = 0, zedges = 0, zverts = 0;
  for (const Located& l : zone) {
    if (l.type == 2) ++zfaces;
    else if (l.type == 1) ++zedges;
    else if (l.type == 0) ++zverts;
  }
  CHECK(zone.size() == 7);
  CHECK(zfaces == 4 && zedges == 3 && zverts == 0);
  CHECK(zone.front().type == 2 && zone.front().as_face() == out.as_face());
  CHECK(zone.back().type == 2 && zone.back().as_face() == out.as_face());
  CHECK(arr->number_of_edges() == 9);              // zone() is a query: nothing was inserted
  CHECK(arr->do_intersect(S(2, -1, 2, 5)));
  CHECK(!arr->do_intersect(S(10, 10, 12, 12)));

  // ---- decompose ----------------------------------------------------------
  std::vector<VerticalDecompositionEntry> dec;
  arr->decompose(dec);
  CHECK(dec.size() == 8);                          // one entry per vertex
  // the leftmost vertex is (-1,2); nothing lies below or above it inside the arrangement
  CHECK(same_point(arr->vertex_point(dec[0].v), P(-1, 2)));
  CHECK(dec[0].below.type == 2 && dec[0].below.as_face() == out.as_face());
  CHECK(dec[0].above.type == 2 && dec[0].above.as_face() == out.as_face());

  // ---- clone --------------------------------------------------------------
  std::unique_ptr<ArrBase> copy = arr->clone();
  CHECK(copy->number_of_vertices() == 8);
  CHECK(copy->number_of_edges() == 9);
  CHECK(copy->number_of_faces() == 3);
  CHECK(copy->number_of_curves() == 5);
  CHECK(copy->is_valid());
  CHECK(copy->locate(p_up).type == 2);
  // handles of the original are not valid in the clone (and vice versa)
  CHECK(!copy->vertex_valid(vs[0]));
  CHECK(arr->vertex_valid(vs[0]));

  // ---- history: remove_curve ---------------------------------------------
  // Removing the horizontal input curve deletes its 3 edges.  CGAL does NOT merge the two
  // degree-2 vertices it leaves behind on the left and right square edges, so
  // V = 6, E = 6, F = 2 (arrangement_with_history semantics).
  const std::size_t removed = arr->remove_curve(cross);
  CHECK(removed == 3);
  CHECK(arr->number_of_vertices() == 6);
  CHECK(arr->number_of_edges() == 6);
  CHECK(arr->number_of_faces() == 2);
  CHECK(arr->number_of_curves() == 4);
  CHECK(arr->is_valid());
  CHECK(!arr->curve_valid(cross));                 // the handle went stale
  CHECK(throws_error([&] { arr->number_of_induced_edges(cross); }, ErrorCode::InvalidHandle));

  // ---- clear --------------------------------------------------------------
  arr->clear();
  CHECK(arr->is_empty());
  CHECK(arr->number_of_vertices() == 0);
  CHECK(arr->number_of_faces() == 1);
  CHECK(arr->number_of_curves() == 0);
  CHECK(arr->is_valid());
}

// ===========================================================================
// 7. a few arrangement modification entry points on segment geometry
// ===========================================================================
static void test_modification() {
  std::printf("-- modification --\n");
  const KindOps& o = O();
  std::unique_ptr<ArrBase> arr = make_arrangement(Kind::Segment);

  // isolated vertex in the unbounded face
  const FH uf = arr->unbounded_face();
  const VH iso = arr->insert_point_in_face_interior(P(1, 1), uf);
  CHECK(arr->vertex_is_isolated(iso));
  CHECK(arr->vertex_face(iso) == uf);
  CHECK(arr->number_of_isolated_vertices() == 1);
  arr->remove_isolated_vertex(iso);
  CHECK(arr->number_of_vertices() == 0);

  // a triangle built with the unchecked Arrangement_2 API (no history)
  const HH e1 = arr->insert_in_face_interior(S(0, 0, 4, 0), arr->unbounded_face());
  const VH v0 = arr->he_source(e1);
  const VH v1 = arr->he_target(e1);
  const HH e2 = arr->insert_from_right_vertex(S(4, 0, 2, 3), v1);
  const VH v2 = arr->he_target(e2);
  arr->insert_at_vertices(S(2, 3, 0, 0), v2, v0);
  CHECK(arr->number_of_vertices() == 3);
  CHECK(arr->number_of_edges() == 3);
  CHECK(arr->number_of_faces() == 2);
  CHECK(arr->number_of_curves() == 0);             // none of these records history
  CHECK(arr->is_valid());

  // split / merge an edge, with history bookkeeping
  const HH split_he = arr->split_edge_at_point(e1, P(2, 0));
  CHECK(arr->number_of_edges() == 4);
  CHECK(arr->number_of_vertices() == 4);
  CHECK(same_point(arr->vertex_point(arr->he_target(split_he)), P(2, 0)));
  const HH merged = arr->merge_edge_history(split_he, arr->he_next(split_he));
  CHECK(arr->number_of_edges() == 3);
  CHECK(o.curve_equal(arr->he_curve(merged), S(0, 0, 4, 0)));

  // insert_non_intersecting_curve (no history) and remove_edge
  std::unique_ptr<ArrBase> arr2 = make_arrangement(Kind::Segment);
  const HH nh = arr2->insert_non_intersecting_curve(S(0, 0, 1, 0));
  CHECK(arr2->number_of_edges() == 1);
  CHECK(arr2->number_of_curves() == 0);
  CHECK(arr2->he_direction(nh) == ARR_LEFT_TO_RIGHT);
  arr2->remove_edge(nh, true, true);
  CHECK(arr2->number_of_edges() == 0 && arr2->number_of_vertices() == 0);
  CHECK(!arr2->halfedge_valid(nh));
  CHECK(throws_error([&] { arr2->he_curve(nh); }, ErrorCode::InvalidHandle));

  // insert_point / remove_vertex through the global functions
  const VH pv = arr2->insert_point(P(3, 3));
  CHECK(arr2->number_of_vertices() == 1);
  CHECK(arr2->remove_vertex(pv));
  CHECK(arr2->number_of_vertices() == 0);

  // a kind mismatch is reported, not crashed on
  CHECK(throws_error([&] { arr2->insert_curve(make_geom(Kind::Linear, GeomType::Curve, 0.0)); },
                     ErrorCode::KindMismatch));
}

// ===========================================================================
// 8. convert_curve from an "algebraic" kind (conic / Bezier)
// ===========================================================================
//
// The conic and Bezier TUs are not linked here (and kind_segment.cpp may not call their free
// functions anyway), so that branch of convert_curve is exercised with a MINIMAL stub KindOps
// registered under Kind::Conic.  It models an arc
//
//     y(x) = chord(x) + k * (x - sx) * (x - tx)          on [sx, tx]
//
// which is exactly the chord for k == 0 and a genuine parabola (a degree-2 algebraic curve that
// meets the chord only at the two endpoints) for k != 0.  That is what the exact
// "is the arc its own chord?" certificate in kind_segment.cpp has to tell apart.
//
// This section runs LAST because register_kind() mutates the process-wide registry.
namespace fake_conic {

struct FPoint {
  Rational x, y;
  bool rational = true;
};
struct FArc {
  Rational sx, sy, tx, ty;
  Rational k = Rational(0);      ///< 0 => the straight chord
  bool rational_ends = true;
  bool vertical = false;
  int pieces = 1;                ///< how many x-monotone pieces make_x_monotone() reports
  bool piece_is_point = false;   ///< make_x_monotone() reports an isolated point instead
};

Geom box_pt(const FPoint& p) { return make_geom(Kind::Conic, GeomType::Point, p); }
Geom box_xc(const FArc& a) { return make_geom(Kind::Conic, GeomType::XCurve, a); }
Geom box_cv(const FArc& a) { return make_geom(Kind::Conic, GeomType::Curve, a); }

class Ops final : public KindOps {
 public:
  Kind kind() const override { return Kind::Conic; }
  const char* name() const override { return "conic"; }
  int dimension() const override { return 2; }
  bool is_unbounded_kind() const override { return false; }
  bool has_polygon_set() const override { return false; }

  // -- the handful of methods kind_segment.cpp's from_algebraic_kind() actually calls --
  void make_x_monotone(const Geom& c, std::vector<Geom>& out) const override {
    const FArc& a = c.as<FArc>();
    if (a.piece_is_point) { out.push_back(box_pt(FPoint{a.sx, a.sy, true})); return; }
    for (int i = 0; i < a.pieces; ++i) {
      FArc part = a;
      if (a.pieces == 2) {   // split [sx,tx] in half; both halves stay straight
        const Rational mx = (a.sx + a.tx) / Rational(2);
        const Rational my = (a.sy + a.ty) / Rational(2);
        if (i == 0) { part.tx = mx; part.ty = my; }
        else { part.sx = mx; part.sy = my; }
      }
      part.pieces = 1;
      out.push_back(box_xc(part));
    }
  }
  Geom xcurve_source(const Geom& xc) const override {
    const FArc& a = xc.as<FArc>();
    return box_pt(FPoint{a.sx, a.sy, a.rational_ends});
  }
  Geom xcurve_target(const Geom& xc) const override {
    const FArc& a = xc.as<FArc>();
    return box_pt(FPoint{a.tx, a.ty, a.rational_ends});
  }
  bool point_is_rational(const Geom& p) const override { return p.as<FPoint>().rational; }
  void point_exact_rational(const Geom& p, std::vector<Rational>& out) const override {
    const FPoint& q = p.as<FPoint>();
    if (!q.rational) throw_error(ErrorCode::NotRepresentable, "fake conic: algebraic coordinate");
    out.clear();
    out.push_back(q.x);
    out.push_back(q.y);
  }
  bool xcurve_is_vertical(const Geom& xc) const override { return xc.as<FArc>().vertical; }
  Geom convert_point(const Geom& p) const override {
    if (p.holds<FPoint>()) return p;
    if (p.holds<SegmentTypes::Point_2>()) {
      const SegmentTypes::Point_2& q = p.as<SegmentTypes::Point_2>();
      return box_pt(FPoint{to_rational(q.x()), to_rational(q.y()), true});
    }
    throw_error(ErrorCode::KindMismatch, "fake conic: unsupported point");
  }
  int compare_y_at_x(const Geom& p, const Geom& xc) const override {
    const FPoint& q = p.as<FPoint>();
    const FArc& a = xc.as<FArc>();
    const Rational chord = a.sy + (a.ty - a.sy) * (q.x - a.sx) / (a.tx - a.sx);
    const Rational y = chord + a.k * (q.x - a.sx) * (q.x - a.tx);
    return rational_compare(q.y, y);
  }

  // -- everything else is out of scope for this stub --------------------------------------
  [[noreturn]] static void nope() { throw_error(ErrorCode::Unsupported, "fake conic stub"); }
  Geom make_point(const Rational&, const Rational&) const override { nope(); }
  Geom make_point_3(const Rational&, const Rational&, const Rational&) const override { nope(); }
  void point_approx(const Geom&, double*) const override { nope(); }
  void point_interval(const Geom&, std::vector<std::pair<double, double>>&) const override { nope(); }
  void point_exact(const Geom&, std::vector<Geom>&) const override { nope(); }
  int point_compare_x(const Geom&, const Geom&) const override { nope(); }
  int point_compare_xy(const Geom&, const Geom&) const override { nope(); }
  bool point_equal(const Geom&, const Geom&) const override { nope(); }
  std::string point_repr(const Geom&) const override { nope(); }
  bool is_x_monotone(const Geom&) const override { nope(); }
  Geom to_x_monotone(const Geom&) const override { nope(); }
  Geom to_curve(const Geom&) const override { nope(); }
  bool xcurve_has_source(const Geom&) const override { nope(); }
  bool xcurve_has_target(const Geom&) const override { nope(); }
  Geom xcurve_min_vertex(const Geom&) const override { nope(); }
  Geom xcurve_max_vertex(const Geom&) const override { nope(); }
  bool xcurve_is_directed_right(const Geom&) const override { nope(); }
  int compare_endpoints_xy(const Geom&) const override { nope(); }
  Geom construct_opposite(const Geom&) const override { nope(); }
  BBox curve_bbox(const Geom&) const override { nope(); }
  bool curve_is_bounded(const Geom&) const override { nope(); }
  void approximate(const Geom&, double, const BBox*, std::vector<double>&) const override { nope(); }
  double approximate_length(const Geom&, double) const override { nope(); }
  std::string curve_repr(const Geom&) const override { nope(); }
  bool curve_equal(const Geom&, const Geom&) const override { nope(); }
  void convert_curve(const Geom&, std::vector<Geom>&) const override { nope(); }
  int compare_y_at_x_left(const Geom&, const Geom&, const Geom&) const override { nope(); }
  int compare_y_at_x_right(const Geom&, const Geom&, const Geom&) const override { nope(); }
  bool is_in_x_range(const Geom&, const Geom&) const override { nope(); }
  void split(const Geom&, const Geom&, Geom&, Geom&) const override { nope(); }
  void intersect(const Geom&, const Geom&, std::vector<IntersectionResult>&) const override { nope(); }
  bool are_mergeable(const Geom&, const Geom&) const override { nope(); }
  Geom merge(const Geom&, const Geom&) const override { nope(); }
  Geom trim(const Geom&, const Geom&, const Geom&) const override { nope(); }
  int parameter_space_in_x(const Geom&, int) const override { nope(); }
  int parameter_space_in_y(const Geom&, int) const override { nope(); }
  Geom construct_x_monotone_curve(const Geom&, const Geom&) const override { nope(); }
  double approximate_coordinate(const Geom&, int) const override { nope(); }
};

}  // namespace fake_conic

static void test_convert_from_algebraic_kind() {
  std::printf("-- convert_curve from an 'algebraic' kind (stub conic ops) --\n");
  using namespace fake_conic;
  register_kind(Kind::Conic, KindEntry{new Ops(), nullptr, nullptr});
  CHECK(kind_available(Kind::Conic));

  const KindOps& o = O();
  std::vector<Geom> out;

  // (a) the arc IS the straight chord (k == 0) -> converted exactly
  const FArc straight{Rational(0), Rational(0), Rational(4), Rational(2)};
  out.clear();
  o.convert_curve(box_xc(straight), out);
  CHECK(out.size() == 1);
  CHECK(o.curve_equal(out[0], S(0, 0, 4, 2)));
  CHECK(same_point(o.xcurve_source(out[0]), P(0, 0)));
  CHECK(same_point(o.xcurve_target(out[0]), P(4, 2)));

  // (b) a genuine parabola through the same endpoints -> refused, exactly
  FArc bent = straight;
  bent.k = Rational(1) / Rational(10);
  CHECK(throws_error([&] { std::vector<Geom> t; o.convert_curve(box_xc(bent), t); },
                     ErrorCode::NotRepresentable));
  // even a very flat one (the certificate is exact rational arithmetic, not a tolerance)
  FArc flat = straight;
  flat.k = Rational(1) / Rational(1000000000);
  CHECK(throws_error([&] { std::vector<Geom> t; o.convert_curve(box_xc(flat), t); },
                     ErrorCode::NotRepresentable));

  // (c) algebraic endpoints -> refused before anything else
  FArc irrational = straight;
  irrational.rational_ends = false;
  CHECK(throws_error([&] { std::vector<Geom> t; o.convert_curve(box_xc(irrational), t); },
                     ErrorCode::NotRepresentable));

  // (d) a vertical arc: Compare_y_at_x_2 is unusable there, and an x-monotone vertical curve
  //     is a straight vertical segment already -> accepted through the vertical shortcut
  const FArc vert{Rational(2), Rational(-1), Rational(2), Rational(5), Rational(0), true, true};
  out.clear();
  o.convert_curve(box_xc(vert), out);
  CHECK(out.size() == 1);
  CHECK(o.curve_equal(out[0], S(2, -1, 2, 5)));
  // ... but a non-vertical curve whose endpoints share an x is not a curve we can convert
  FArc bogus = vert;
  bogus.vertical = false;
  CHECK(throws_error([&] { std::vector<Geom> t; o.convert_curve(box_xc(bogus), t); },
                     ErrorCode::NotRepresentable));

  // (e) a general Curve box is subdivided first: two x-monotone pieces -> two segments
  FArc two = straight;
  two.pieces = 2;
  out.clear();
  o.convert_curve(box_cv(two), out);
  CHECK(out.size() == 2);
  CHECK(o.curve_equal(out[0], S(0, 0, 2, 1)));
  CHECK(o.curve_equal(out[1], S(2, 1, 4, 2)));
  CHECK(same_point(o.xcurve_target(out[0]), o.xcurve_source(out[1])));

  // (f) an isolated point among the x-monotone pieces is not a segment
  FArc pointish = straight;
  pointish.piece_is_point = true;
  CHECK(throws_error([&] { std::vector<Geom> t; o.convert_curve(box_cv(pointish), t); },
                     ErrorCode::NotRepresentable));

  // (g) degenerate: both endpoints coincide
  const FArc degen{Rational(1), Rational(1), Rational(1), Rational(1), Rational(0), true, true};
  CHECK(throws_error([&] { std::vector<Geom> t; o.convert_curve(box_xc(degen), t); },
                     ErrorCode::NotRepresentable));

  // convert_point goes down the same registry path
  CHECK(same_point(o.convert_point(box_pt(FPoint{Rational(3), Rational(-7), true})), P(3, -7)));
  CHECK(throws_error(
      [&] { o.convert_point(box_pt(FPoint{Rational(0), Rational(0), false})); },
      ErrorCode::NotRepresentable));
}

// ===========================================================================
int main() {
  // CGAL's default error handler prints the whole "CGAL error: ..." block to stderr before the
  // exception is thrown (number_types_and_errors.md gotcha 5).  Silence it: the exception is still
  // thrown afterwards (assertions_impl.h: the handler runs first, then the switch throws).
  CGAL::set_error_handler([](const char*, const char*, const char*, int, const char*) {});
  CGAL::set_warning_handler([](const char*, const char*, const char*, int, const char*) {});

  test_registry();
  test_points();
  test_curves();
  test_traits_ops();
  test_convert_curve();
  test_arrangement();
  test_modification();
  test_convert_from_algebraic_kind();   // LAST: it registers a stub ops under Kind::Conic

  std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
  // Returning normally (rather than _exit) is part of the test: it proves that nothing this kind
  // keeps in static storage aborts while the process is torn down.
  return g_failures == 0 ? 0 : 1;
}
