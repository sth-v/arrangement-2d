// arr2d — C++ self-test for Kind::Bezier (src/arr2d/src/kind_bezier.cpp).
//
// Self-contained: it links registry.o numbers.o overlay.o kind_bezier.o and NOTHING else.
// kind_bezier.cpp's registrar stores &arr2d::make_polygon_set_bezier in its KindEntry, so that
// symbol must exist at link time even though the test never calls it.  Two ways to satisfy it:
//
// (1) WITHOUT the Boolean-set-operations TU (the default for this test) — the test defines a
//     stub factory under ARR2D_TEST_STUB_BSO:
//
//   SC=<scratch dir>;  REPO=/Users/sthv/PycharmProjects/arrangement-2d
//   for f in registry numbers overlay; do
//     /usr/bin/clang++ -std=c++17 -O0 -g -c -DCGAL_USE_CORE -DCGAL_USE_GMP -DCGAL_USE_MPFR \
//       -I/opt/homebrew/include -I$REPO/src/arr2d/include $REPO/src/arr2d/src/$f.cpp -o $SC/$f.o
//   done
//   /usr/bin/clang++ -std=c++17 -O0 -g -c -DCGAL_USE_CORE -DCGAL_USE_GMP -DCGAL_USE_MPFR \
//     -I/opt/homebrew/include -I$REPO/src/arr2d/include $REPO/src/arr2d/src/kind_bezier.cpp \
//     -o $SC/kind_bezier.o
//   /usr/bin/clang++ -std=c++17 -O0 -g -c -DARR2D_TEST_STUB_BSO \
//     -DCGAL_USE_CORE -DCGAL_USE_GMP -DCGAL_USE_MPFR \
//     -I/opt/homebrew/include -I$REPO/src/arr2d/include \
//     $REPO/src/arr2d/tests/test_kind_bezier.cpp -o $SC/test_kind_bezier.o
//   /usr/bin/clang++ -std=c++17 -o $SC/test_kind_bezier \
//     $SC/test_kind_bezier.o $SC/kind_bezier.o $SC/registry.o $SC/numbers.o $SC/overlay.o \
//     -L/opt/homebrew/lib -lgmp -lmpfr
//   $SC/test_kind_bezier            # must print "0 failures" and exit 0
//
// (2) WITH the real Boolean TU (once src/arr2d/src/bso_bezier.cpp exists): compile the test
//     WITHOUT -DARR2D_TEST_STUB_BSO, compile bso_bezier.cpp the same way as kind_bezier.cpp and
//     add $SC/bso_bezier.o to the link line.  Nothing else changes.
//
// Exiting normally (status 0) is part of the test: it proves that no CORE-backed object is
// destroyed during static teardown (`! blocks.empty()`, CGAL/CORE/MemoryPool.h:125).
//
// -----------------------------------------------------------------------------------------
// The reference curve used almost everywhere below is the one measured in
// docs/dev/cgal61_api/traits_bezier.md §11:
//
//     B1 = cubic with rational control points (0,0) (4,1) (-2,2) (2,3)
//
// which gives, in closed form (hand-derived, and matching the map's measured numbers):
//     x(t) = 20t^3 - 30t^2 + 12t          y(t) = 3t
//     X'(t) = 60t^2 - 60t + 12 = 0   <=>  5t^2 - 5t + 1 = 0   <=>  t = (5 -+ sqrt 5)/10
//     t1 = (5-sqrt5)/10 = 0.27639320225002104     t2 = (5+sqrt5)/10 = 0.72360679774997896
//     at a vertical tangency, 5t^2 = 5t-1  =>  x(t) = 2 - 2t, so
//     (x,y)(t1) = (1.4472135954999579, 0.8291796067500631)
//     (x,y)(t2) = (0.5527864045000421, 2.170820393249937)
//     B1(1/3) = (38/27, 1)  exactly
// and
//     B2 = quadratic (0,3) (2,-1) (3,4):  x(t) = 4t - t^2 (X' = 4-2t > 0 on [0,1] => x-monotone)
//                                         y(t) = 9t^2 - 8t + 3
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "arr2d/arrangement.hpp"
#include "arr2d/bso.hpp"
#include "arr2d/common.hpp"
#include "arr2d/impl/number_conv.hpp"
#include "arr2d/numbers.hpp"
#include "arr2d/ops.hpp"
#include "arr2d/polygon_set.hpp"
#include "arr2d/registry.hpp"

// Types of the OTHER kinds, used to box raw CGAL objects for convert_point / convert_curve.
// Including these headers costs nothing at link time: SegmentTypes::traits() & co are never
// called here, so no other kind TU is needed.
#include "arr2d/kinds/linear_types.hpp"
#include "arr2d/kinds/polyline_types.hpp"
#include "arr2d/kinds/segment_types.hpp"

#ifdef ARR2D_TEST_STUB_BSO
// See (1) in the header comment: kind_bezier.cpp's registrar takes the address of this factory.
namespace arr2d {
std::unique_ptr<PolygonSetBase> make_polygon_set_bezier() { return nullptr; }
}  // namespace arr2d
#endif

using namespace arr2d;

// ===========================================================================================
// tiny check harness
// ===========================================================================================
static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    ++g_checks;                                                              \
    if (!(cond)) {                                                           \
      ++g_failures;                                                          \
      std::printf("FAIL line %d: %s\n", __LINE__, #cond);                    \
    }                                                                        \
  } while (0)

#define CHECK_NEAR(a, b, eps)                                                            \
  do {                                                                                   \
    ++g_checks;                                                                          \
    const double va = (a), vb = (b), ve = (eps);                                         \
    if (!(std::fabs(va - vb) <= ve)) {                                                   \
      ++g_failures;                                                                      \
      std::printf("FAIL line %d: |%.17g - %.17g| = %.3g > %.3g   (%s vs %s)\n", __LINE__, \
                  va, vb, std::fabs(va - vb), ve, #a, #b);                                \
    }                                                                                    \
  } while (0)

template <class F>
static bool throws_code(F&& f, ErrorCode code) {
  try {
    f();
  } catch (const Error& e) {
    if (e.code == code) return true;
    std::printf("      (got Error code %d: %s)\n", int(e.code), e.what());
    return false;
  } catch (const std::exception& e) {
    std::printf("      (got std::exception: %s)\n", e.what());
    return false;
  }
  return false;
}

#define CHECK_ERR(code, ...)                                                    \
  do {                                                                          \
    ++g_checks;                                                                 \
    if (!throws_code([&] { __VA_ARGS__; }, code)) {                             \
      ++g_failures;                                                             \
      std::printf("FAIL line %d: expected Error(%d) from: %s\n", __LINE__,      \
                  int(code), #__VA_ARGS__);                                     \
    }                                                                           \
  } while (0)

// ===========================================================================================
// helpers
// ===========================================================================================

// Reference constants (see the file header).
static const double kT1 = 0.27639320225002104;
static const double kT2 = 0.72360679774997896;
static const double kX1 = 1.4472135954999579, kY1 = 0.8291796067500631;
static const double kX2 = 0.5527864045000421, kY2 = 2.170820393249937;

static Rational R(long n, long d = 1) { return Rational(n) / Rational(d); }

static void approx_xy(const Geom& p, double& x, double& y) {
  double buf[3] = {0, 0, 0};
  ops(Kind::Bezier).point_approx(p, buf);
  x = buf[0];
  y = buf[1];
}

/// Distance from (px,py) to the polyline stored as flattened x,y pairs.
static double dist_to_polyline(double px, double py, const std::vector<double>& poly) {
  double best = 1e300;
  for (std::size_t i = 0; i + 3 < poly.size(); i += 2) {
    const double ax = poly[i], ay = poly[i + 1], bx = poly[i + 2], by = poly[i + 3];
    const double dx = bx - ax, dy = by - ay;
    const double l2 = dx * dx + dy * dy;
    double t = (l2 > 0.0) ? ((px - ax) * dx + (py - ay) * dy) / l2 : 0.0;
    t = std::max(0.0, std::min(1.0, t));
    const double qx = ax + t * dx, qy = ay + t * dy;
    best = std::min(best, std::hypot(px - qx, py - qy));
  }
  return best;
}

// Raw CGAL types of the other kinds (for the conversion tests).
using EK = CGAL::Epeck;
using EPoint = EK::Point_2;
using SegCurve = SegmentTypes::Curve_2;              // Arr_segment_2<Epeck>
using LinCurve = LinearTypes::Curve_2;               // Arr_linear_object_2<Epeck>
using PolyCurve = PolylineTypes::Curve_2;            // internal::Polycurve_2<Arr_segment_2, Point>

static EPoint epoint(long x, long y) { return EPoint(EK::FT(x), EK::FT(y)); }

// ===========================================================================================
// 1. points
// ===========================================================================================
static void test_points() {
  std::printf("-- points\n");
  const KindOps& K = ops(Kind::Bezier);
  CHECK(K.kind() == Kind::Bezier);
  CHECK(std::string(K.name()) == "bezier");
  CHECK(K.dimension() == 2);
  CHECK(!K.is_unbounded_kind());
  CHECK(K.has_polygon_set());

  // --- construction from rationals: 3/4, -2 ------------------------------------------------
  const Geom p = K.make_point(R(3, 4), R(-2));
  CHECK(K.point_is_rational(p));
  double x = 0, y = 0;
  approx_xy(p, x, y);
  CHECK_NEAR(x, 0.75, 0.0);      // 3/4 is a double exactly
  CHECK_NEAR(y, -2.0, 0.0);
  CHECK_NEAR(K.approximate_coordinate(p, 0), 0.75, 0.0);
  CHECK_NEAR(K.approximate_coordinate(p, 1), -2.0, 0.0);
  CHECK_ERR(ErrorCode::InvalidArgument, K.approximate_coordinate(p, 2));

  std::vector<Rational> ex;
  K.point_exact_rational(p, ex);
  CHECK(ex.size() == 2 && ex[0] == R(3, 4) && ex[1] == R(-2));

  std::vector<Geom> nums;
  K.point_exact(p, nums);
  CHECK(nums.size() == 2);
  CHECK(number_kind(nums[0]) == NumberKind::Rational);
  CHECK(number_to_rational(nums[0]) == R(3, 4));
  CHECK(number_to_rational(nums[1]) == R(-2));

  std::vector<std::pair<double, double>> iv;
  K.point_interval(p, iv);
  CHECK(iv.size() == 2);
  CHECK(iv[0].first <= 0.75 && 0.75 <= iv[0].second);
  CHECK(iv[1].first <= -2.0 && -2.0 <= iv[1].second);

  CHECK(K.point_repr(p) == "BezierPoint(0.75, -2)");

  // --- comparison ---------------------------------------------------------------------------
  const Geom q = K.make_point(R(3, 4), R(1));
  const Geom p2 = K.make_point(R(3, 4), R(-2));
  CHECK(K.point_compare_x(p, q) == 0);
  CHECK(K.point_compare_xy(p, q) == -1);
  CHECK(K.point_compare_xy(q, p) == 1);
  CHECK(K.point_compare_xy(p, p2) == 0);
  CHECK(K.point_equal(p, p2));
  CHECK(!K.point_equal(p, q));

  // --- convert_point -------------------------------------------------------------------------
  // identity for our own kind
  CHECK(K.point_equal(K.convert_point(p), p));
  // from a raw Epeck point boxed as Kind::Segment (the same C++ type is used by the segment,
  // linear and polyline kinds, so this one box covers all three)
  const Geom seg_pt = make_geom(Kind::Segment, GeomType::Point,
                                EPoint(EK::FT(3) / EK::FT(4), EK::FT(-2)));
  const Geom conv = K.convert_point(seg_pt);
  CHECK(conv.kind == Kind::Bezier && conv.type == GeomType::Point);
  CHECK(K.point_equal(conv, p));
  // ...and boxed as Kind::Polyline / Kind::Linear (same underlying type)
  CHECK(K.point_equal(
      K.convert_point(make_geom(Kind::Polyline, GeomType::Point, epoint(2, 5))),
      K.make_point(R(2), R(5))));
  CHECK(K.point_equal(
      K.convert_point(make_geom(Kind::Linear, GeomType::Point, epoint(-1, 7))),
      K.make_point(R(-1), R(7))));

  // errors
  CHECK_ERR(ErrorCode::Unsupported, K.make_point_3(R(1), R(2), R(3)));
  CHECK_ERR(ErrorCode::InvalidArgument, K.convert_point(make_geom(Kind::Bezier, GeomType::Curve, 1)));
  CHECK_ERR(ErrorCode::KindMismatch, K.point_is_rational(seg_pt));
}

// ===========================================================================================
// 2. curve construction + accessors
// ===========================================================================================
static Geom make_B1() {
  return bezier::make({R(0), R(0), R(4), R(1), R(-2), R(2), R(2), R(3)});
}
static Geom make_B2() {
  return bezier::make({R(0), R(3), R(2), R(-1), R(3), R(4)});
}

static void test_curve_constructors() {
  std::printf("-- curve constructors\n");
  const KindOps& K = ops(Kind::Bezier);

  const Geom B1 = make_B1();
  CHECK(B1.kind == Kind::Bezier && B1.type == GeomType::Curve);
  CHECK(bezier::number_of_control_points(B1) == 4);
  Rational cx, cy;
  bezier::control_point(B1, 0, cx, cy);
  CHECK(cx == R(0) && cy == R(0));
  bezier::control_point(B1, 1, cx, cy);
  CHECK(cx == R(4) && cy == R(1));
  bezier::control_point(B1, 3, cx, cy);
  CHECK(cx == R(2) && cy == R(3));
  CHECK_ERR(ErrorCode::InvalidArgument, bezier::control_point(B1, 4, cx, cy));
  CHECK(bezier::curve_id(B1) != 0);
  CHECK(bezier::has_no_self_intersections(B1));      // map §11: has_no_self_intersections = 1
  CHECK(K.curve_is_bounded(B1));
  CHECK(K.curve_repr(B1) == "BezierCurve([(0, 0), (4, 1), (-2, 2), (2, 3)])");

  // bbox of the control polygon: map §11 says  -2 0 4 3
  const BBox bb = K.curve_bbox(B1);
  CHECK(bb.dim == 2);
  CHECK_NEAR(bb.lo[0], -2.0, 0.0);
  CHECK_NEAR(bb.lo[1], 0.0, 0.0);
  CHECK_NEAR(bb.hi[0], 4.0, 0.0);
  CHECK_NEAR(bb.hi[1], 3.0, 0.0);

  // make_from_points: same curve from Bezier points and from raw Epeck (segment-kind) points
  std::vector<Geom> pts = {K.make_point(R(0), R(0)), K.make_point(R(4), R(1)),
                           K.make_point(R(-2), R(2)), K.make_point(R(2), R(3))};
  const Geom B1b = bezier::make_from_points(pts);
  CHECK(K.curve_repr(B1b) == K.curve_repr(B1));
  CHECK(bezier::curve_id(B1b) != bezier::curve_id(B1));   // distinct representations

  std::vector<Geom> mixed = {make_geom(Kind::Segment, GeomType::Point, epoint(0, 0)),
                             make_geom(Kind::Polyline, GeomType::Point, epoint(1, 1)),
                             K.make_point(R(2), R(0))};
  const Geom B3 = bezier::make_from_points(mixed);
  CHECK(bezier::number_of_control_points(B3) == 3);
  CHECK(K.curve_repr(B3) == "BezierCurve([(0, 0), (1, 1), (2, 0)])");

  // evaluation
  const Geom pt13 = bezier::point_at(B1, R(1, 3));         // B1(1/3) == (38/27, 1) exactly
  CHECK(K.point_is_rational(pt13));
  std::vector<Rational> e13;
  K.point_exact_rational(pt13, e13);
  CHECK(e13[0] == R(38, 27) && e13[1] == R(1));
  double ex = 0, ey = 0;
  bezier::evaluate_approx(B1, 1.0 / 3.0, ex, ey);
  CHECK_NEAR(ex, 38.0 / 27.0, 1e-12);
  CHECK_NEAR(ey, 1.0, 1e-12);
  bezier::evaluate_approx(B1, kT1, ex, ey);
  CHECK_NEAR(ex, kX1, 1e-12);
  CHECK_NEAR(ey, kY1, 1e-12);

  std::vector<double> smp;
  bezier::sample(B1, 0.0, 1.0, 5, smp);
  CHECK(smp.size() == 10);
  CHECK_NEAR(smp[0], 0.0, 1e-15);
  CHECK_NEAR(smp[1], 0.0, 1e-15);
  CHECK_NEAR(smp[8], 2.0, 1e-12);
  CHECK_NEAR(smp[9], 3.0, 1e-12);
  {   // the samples are uniform in t; the middle one is B1(0.5) = (12*0.5-30*0.25+20*0.125, 1.5)
    //                                                          = (6 - 7.5 + 2.5, 1.5) = (1, 1.5)
    CHECK_NEAR(smp[4], 1.0, 1e-12);
    CHECK_NEAR(smp[5], 1.5, 1e-12);
  }

  // originators / exact parameter of a point on the curve
  std::vector<std::pair<std::size_t, double>> origs;
  bezier::point_originators(pt13, origs);
  CHECK(origs.size() == 1);
  CHECK(origs[0].first == bezier::curve_id(B1));
  CHECK_NEAR(origs[0].second, 1.0 / 3.0, 1e-12);
  const Geom tparam = bezier::point_parameter(pt13, bezier::curve_id(B1));
  CHECK(number_kind(tparam) == NumberKind::Algebraic);
  CHECK_NEAR(number_to_double(tparam), 1.0 / 3.0, 1e-12);
  CHECK_ERR(ErrorCode::Unsupported, bezier::point_parameter(pt13, std::size_t(12345)));

  // RETENTION REGISTRY (traits_bezier.md gotcha 7 / CGAL_TRAPS_CHECKLIST "Bezier kind"):
  // Curve_2::id() is the address of the reference-counted rep and BOTH Bezier caches are keyed
  // by it and are never invalidated.  kind_bezier.cpp keeps every Curve_2 it ever built alive, so
  // dropping the last Geom box must NOT let a later curve inherit the same id (which would make
  // it inherit the dead curve's cached vertical tangencies / intersections).
  std::size_t dropped_id = 0;
  {
    const Geom tmp = bezier::make({R(7), R(7), R(8), R(9)});
    dropped_id = bezier::curve_id(tmp);
  }
  {
    const Geom later = bezier::make({R(-7), R(-7), R(-8), R(-9)});
    CHECK(bezier::curve_id(later) != dropped_id);
  }

  // constructor error paths
  CHECK_ERR(ErrorCode::InvalidArgument, bezier::make({R(0), R(0)}));                 // 1 point
  CHECK_ERR(ErrorCode::InvalidArgument, bezier::make({R(0), R(0), R(1)}));           // odd count
  CHECK_ERR(ErrorCode::InvalidArgument, bezier::make({R(1), R(1), R(1), R(1)}));     // degenerate
  CHECK_ERR(ErrorCode::InvalidArgument, bezier::make_from_points({K.make_point(R(0), R(0))}));
  CHECK_ERR(ErrorCode::InvalidArgument, bezier::point_at(B1, R(2)));                 // t outside [0,1]
  CHECK_ERR(ErrorCode::InvalidArgument, bezier::point_at(B1, R(-1, 2)));
  CHECK_ERR(ErrorCode::InvalidArgument, bezier::evaluate_approx(B1, 1.5, ex, ey));
  CHECK_ERR(ErrorCode::InvalidArgument, bezier::sample(B1, 0.0, 1.0, 1, smp));
}

// ===========================================================================================
// 3. make_x_monotone + x-monotone accessors
// ===========================================================================================
static void test_x_monotone() {
  std::printf("-- make_x_monotone / xcurve accessors\n");
  const KindOps& K = ops(Kind::Bezier);
  const Geom B1 = make_B1();
  const Geom B2 = make_B2();

  CHECK(!K.is_x_monotone(B1));
  CHECK_ERR(ErrorCode::NotXMonotone, K.to_x_monotone(B1));

  std::vector<Geom> pieces;
  K.make_x_monotone(B1, pieces);
  CHECK(pieces.size() == 3);                       // map §11: 3 variants, all x-monotone curves
  for (const Geom& g : pieces) CHECK(g.type == GeomType::XCurve);
  CHECK(bezier::xid(pieces[0]) == 1u);
  CHECK(bezier::xid(pieces[1]) == 2u);
  CHECK(bezier::xid(pieces[2]) == 3u);
  for (const Geom& g : pieces) CHECK(bezier::curve_id(g) == bezier::curve_id(B1));

  // exact parameter ranges (traits_bezier.md §10.1 / §11)
  double t0 = 0, t1 = 0;
  bezier::parameter_range(pieces[0], t0, t1);
  CHECK_NEAR(t0, 0.0, 1e-14);
  CHECK_NEAR(t1, kT1, 1e-12);
  bezier::parameter_range(pieces[1], t0, t1);
  CHECK_NEAR(t0, kT1, 1e-12);
  CHECK_NEAR(t1, kT2, 1e-12);
  bezier::parameter_range(pieces[2], t0, t1);
  CHECK_NEAR(t0, kT2, 1e-12);
  CHECK_NEAR(t1, 1.0, 1e-14);

  // directions (map §11: 1, 0, 1)
  CHECK(K.xcurve_is_directed_right(pieces[0]));
  CHECK(!K.xcurve_is_directed_right(pieces[1]));
  CHECK(K.xcurve_is_directed_right(pieces[2]));
  CHECK(K.compare_endpoints_xy(pieces[0]) == -1);
  CHECK(K.compare_endpoints_xy(pieces[1]) == +1);
  CHECK(!K.xcurve_is_vertical(pieces[0]));
  CHECK(K.xcurve_has_source(pieces[0]) && K.xcurve_has_target(pieces[0]));
  CHECK(K.curve_is_bounded(pieces[0]));
  CHECK(K.parameter_space_in_x(pieces[0], ARR_MIN_END) == ARR_INTERIOR);
  CHECK(K.parameter_space_in_y(pieces[0], ARR_MAX_END) == ARR_INTERIOR);

  // endpoints
  double x = 0, y = 0;
  approx_xy(K.xcurve_source(pieces[0]), x, y);
  CHECK_NEAR(x, 0.0, 1e-14);
  CHECK_NEAR(y, 0.0, 1e-14);
  approx_xy(K.xcurve_target(pieces[0]), x, y);
  CHECK_NEAR(x, kX1, 1e-12);
  CHECK_NEAR(y, kY1, 1e-12);
  approx_xy(K.xcurve_source(pieces[1]), x, y);      // directed leftwards: source is the RIGHT end
  CHECK_NEAR(x, kX1, 1e-12);
  approx_xy(K.xcurve_target(pieces[1]), x, y);
  CHECK_NEAR(x, kX2, 1e-12);
  CHECK_NEAR(y, kY2, 1e-12);
  approx_xy(K.xcurve_min_vertex(pieces[1]), x, y);  // lexicographically smaller == target here
  CHECK_NEAR(x, kX2, 1e-12);
  approx_xy(K.xcurve_max_vertex(pieces[1]), x, y);
  CHECK_NEAR(x, kX1, 1e-12);
  approx_xy(K.xcurve_target(pieces[2]), x, y);
  CHECK_NEAR(x, 2.0, 1e-12);
  CHECK_NEAR(y, 3.0, 1e-12);

  // the vertical-tangency endpoints are algebraic, not rational (traits_bezier.md §4.3)
  CHECK(!K.point_is_rational(K.xcurve_target(pieces[0])));
  CHECK(K.point_is_rational(K.xcurve_source(pieces[0])));   // B1(0) = (0,0)
  {
    std::vector<Geom> nums;
    K.point_exact(K.xcurve_target(pieces[0]), nums);        // forces make_exact via our cache
    CHECK(nums.size() == 2);
    CHECK(number_kind(nums[0]) == NumberKind::Algebraic);
    CHECK_NEAR(number_to_double(nums[0]), kX1, 1e-12);
    CHECK_NEAR(number_to_double(nums[1]), kY1, 1e-12);
    std::vector<std::pair<double, double>> iv;
    K.point_interval(K.xcurve_target(pieces[0]), iv);
    CHECK(iv[0].first <= kX1 && kX1 <= iv[0].second);
    CHECK(iv[1].first <= kY1 && kY1 <= iv[1].second);
    CHECK(K.point_repr(K.xcurve_target(pieces[0])).substr(0, 13) == "BezierPoint(~");
    CHECK_ERR(ErrorCode::NotRepresentable,
              { std::vector<Rational> r; K.point_exact_rational(K.xcurve_target(pieces[0]), r); });
  }

  // construct_opposite (Bezier's Construct_opposite_2::operator() is non-const — gotcha 12)
  const Geom opp = K.construct_opposite(pieces[0]);
  CHECK(K.compare_endpoints_xy(opp) == +1);
  CHECK(K.curve_equal(opp, pieces[0]));            // Equal_2 is direction-insensitive
  approx_xy(K.xcurve_source(opp), x, y);
  CHECK_NEAR(x, kX1, 1e-12);

  // to_curve gives the supporting curve back
  const Geom sup = K.to_curve(pieces[1]);
  CHECK(sup.type == GeomType::Curve);
  CHECK(bezier::curve_id(sup) == bezier::curve_id(B1));
  CHECK(bezier::curve_id(bezier::supporting_curve(pieces[1])) == bezier::curve_id(B1));
  CHECK(bezier::number_of_control_points(pieces[1]) == 4);   // via the supporting curve

  // repr of an x-monotone piece carries the parameter range
  const std::string r1 = K.curve_repr(pieces[1]);
  CHECK(r1.find("BezierCurve([(0, 0), (4, 1), (-2, 2), (2, 3)], t=[0.2763932") == 0);
  CHECK(r1.find(", 0.7236067") != std::string::npos);
  CHECK(r1.back() == ')');

  // the quadratic is already x-monotone
  std::vector<Geom> p2;
  K.make_x_monotone(B2, p2);
  CHECK(p2.size() == 1);
  CHECK(K.is_x_monotone(B2));
  const Geom x2 = K.to_x_monotone(B2);
  CHECK(x2.type == GeomType::XCurve);
  CHECK(K.xcurve_is_directed_right(x2));           // x(t) = 4t - t^2 is increasing on [0,1]

  // Construct_x_monotone_curve_2 does not exist for this traits (gotcha 8)
  CHECK_ERR(ErrorCode::Unsupported,
            K.construct_x_monotone_curve(K.make_point(R(0), R(0)), K.make_point(R(1), R(1))));
  // parameter_range / xid / supporting_curve need an x-monotone box
  CHECK_ERR(ErrorCode::NotXMonotone, { double a, b; bezier::parameter_range(B1, a, b); });
  CHECK_ERR(ErrorCode::NotXMonotone, bezier::xid(B1));
}

// ===========================================================================================
// 4. approximate() + curve_bbox of an x-monotone piece
// ===========================================================================================
static void test_approximate() {
  std::printf("-- approximate\n");
  const KindOps& K = ops(Kind::Bezier);
  const Geom B1 = make_B1();
  std::vector<Geom> pieces;
  K.make_x_monotone(B1, pieces);

  const double tol = 1e-3;
  for (std::size_t k = 0; k < pieces.size(); ++k) {
    std::vector<double> poly;
    K.approximate(pieces[k], tol, nullptr, poly);
    CHECK(poly.size() >= 4 && poly.size() % 2 == 0);

    // (a) the polyline starts and ends exactly at the endpoints' own approximations
    double sx = 0, sy = 0, tx = 0, ty = 0;
    approx_xy(K.xcurve_source(pieces[k]), sx, sy);
    approx_xy(K.xcurve_target(pieces[k]), tx, ty);
    CHECK(poly.front() == sx);
    CHECK(poly[1] == sy);
    CHECK(poly[poly.size() - 2] == tx);
    CHECK(poly.back() == ty);

    // (b) every sampled point of the TRUE curve is within `tol` of the polyline
    double t0 = 0, t1 = 0;
    bezier::parameter_range(pieces[k], t0, t1);
    double worst = 0.0;
    for (int i = 0; i <= 20; ++i) {
      const double t = t0 + (t1 - t0) * (double(i) / 20.0);
      double cxx = 0, cyy = 0;
      bezier::evaluate_approx(B1, t, cxx, cyy);
      worst = std::max(worst, dist_to_polyline(cxx, cyy, poly));
    }
    std::printf("   piece %zu: %zu points, worst deviation %.3e (tolerance %.1e)\n", k,
                poly.size() / 2, worst, tol);
    CHECK(worst <= tol * 1.05);

    // (c) the polyline runs source -> target (x-monotone: check the x order)
    const bool l2r = K.xcurve_is_directed_right(pieces[k]);
    CHECK((poly.front() < poly[poly.size() - 2]) == l2r);

    // (d) curve_bbox of the piece contains every sampled point and sits inside the whole
    //     curve's control-polygon box (-2,0)-(4,3)
    const BBox pb = K.curve_bbox(pieces[k]);
    CHECK(pb.lo[0] >= -2.0 - 1e-12 && pb.hi[0] <= 4.0 + 1e-12);
    CHECK(pb.lo[1] >= 0.0 - 1e-12 && pb.hi[1] <= 3.0 + 1e-12);
    for (int i = 0; i <= 20; ++i) {
      const double t = t0 + (t1 - t0) * (double(i) / 20.0);
      double cxx = 0, cyy = 0;
      bezier::evaluate_approx(B1, t, cxx, cyy);
      CHECK(cxx >= pb.lo[0] - 1e-9 && cxx <= pb.hi[0] + 1e-9);
      CHECK(cyy >= pb.lo[1] - 1e-9 && cyy <= pb.hi[1] + 1e-9);
    }
  }

  // piece 0 spans t in [0, t1]: x grows 0 -> 1.4472, y = 3t grows 0 -> 0.8292
  {
    const BBox pb = K.curve_bbox(pieces[0]);
    CHECK(pb.lo[0] <= 0.0 && pb.hi[0] >= kX1);
    CHECK(pb.lo[1] <= 0.0 && pb.hi[1] >= kY1);
    CHECK(pb.hi[1] < 1.5);            // much tighter than the whole curve's [0,3]
  }

  // the whole (non x-monotone) curve: endpoints are B1(0) = (0,0) and B1(1) = (2,3)
  {
    std::vector<double> poly;
    K.approximate(B1, tol, nullptr, poly);
    CHECK_NEAR(poly[0], 0.0, 0.0);
    CHECK_NEAR(poly[1], 0.0, 0.0);
    CHECK_NEAR(poly[poly.size() - 2], 2.0, 1e-15);
    CHECK_NEAR(poly.back(), 3.0, 1e-15);
    double worst = 0.0;
    for (int i = 0; i <= 40; ++i) {
      double cxx = 0, cyy = 0;
      bezier::evaluate_approx(B1, double(i) / 40.0, cxx, cyy);
      worst = std::max(worst, dist_to_polyline(cxx, cyy, poly));
    }
    std::printf("   whole curve: %zu points, worst deviation %.3e\n", poly.size() / 2, worst);
    CHECK(worst <= tol * 1.05);
  }

  // a tighter tolerance produces strictly more points and a smaller deviation
  {
    std::vector<double> coarse, fine;
    K.approximate(pieces[2], 1e-2, nullptr, coarse);
    K.approximate(pieces[2], 1e-5, nullptr, fine);
    CHECK(fine.size() > coarse.size());
  }

  // approximate_length of the whole curve, compared with a dense chord sum
  {
    const double len = K.approximate_length(B1, 1e-5);
    double ref = 0.0, px = 0, py = 0;
    bezier::evaluate_approx(B1, 0.0, px, py);
    for (int i = 1; i <= 20000; ++i) {
      double cxx = 0, cyy = 0;
      bezier::evaluate_approx(B1, double(i) / 20000.0, cxx, cyy);
      ref += std::hypot(cxx - px, cyy - py);
      px = cxx;
      py = cyy;
    }
    std::printf("   approximate_length = %.9f (dense reference %.9f)\n", len, ref);
    CHECK(std::fabs(len - ref) < 1e-3);
  }

  // tolerance validation happens BEFORE any subdivision (rendering gotcha 7)
  std::vector<double> tmp;
  CHECK_ERR(ErrorCode::InvalidArgument, K.approximate(B1, 0.0, nullptr, tmp));
  CHECK_ERR(ErrorCode::InvalidArgument, K.approximate(B1, -1.0, nullptr, tmp));
  CHECK_ERR(ErrorCode::InvalidArgument, K.approximate(B1, std::nan(""), nullptr, tmp));
  // a ridiculously small tolerance is clamped to 1e-12 and still terminates
  K.approximate(pieces[0], 1e-30, nullptr, tmp);
  CHECK(tmp.size() >= 4);
}

// ===========================================================================================
// 5. traits functors that need a Bezier workaround (split) + merge/trim/intersect
// ===========================================================================================
static void test_traits_functors() {
  std::printf("-- traits functors (split workaround, merge, trim, intersect)\n");
  const KindOps& K = ops(Kind::Bezier);
  const Geom B1 = make_B1();
  std::vector<Geom> pieces;
  K.make_x_monotone(B1, pieces);
  const Geom& piece0 = pieces[0];                    // t in [0, 0.2763932...], directed right

  // split at an interior point of the piece (t = 1/10)
  const Geom mid = bezier::point_at(piece0, R(1, 10));
  CHECK(K.is_in_x_range(piece0, mid));
  CHECK(K.compare_y_at_x(mid, piece0) == 0);
  Geom left, right;
  K.split(piece0, mid, left, right);
  double a = 0, b = 0;
  bezier::parameter_range(left, a, b);
  CHECK_NEAR(a, 0.0, 1e-14);
  CHECK_NEAR(b, 0.1, 1e-12);
  bezier::parameter_range(right, a, b);
  CHECK_NEAR(a, 0.1, 1e-12);
  CHECK_NEAR(b, kT1, 1e-12);

  // ... and merge back (Merge_2 has a private ctor: only via merge_2_object)
  CHECK(K.are_mergeable(left, right));
  const Geom merged = K.merge(left, right);
  bezier::parameter_range(merged, a, b);
  CHECK_NEAR(a, 0.0, 1e-14);
  CHECK_NEAR(b, kT1, 1e-12);

  // THE TRAP (exact_coordinates_contract.md gotcha 5 / CGAL_TRAPS_CHECKLIST "Bezier kind"):
  // CGAL's Split_2 silently accepts a rational point that is nowhere near the curve.
  // BezierOps::split() must reject it.
  CHECK_ERR(ErrorCode::InvalidArgument,
            { Geom l, r; K.split(piece0, K.make_point(R(1000), R(1000)), l, r); });
  // a point in the x-range but off the curve
  CHECK_ERR(ErrorCode::InvalidArgument,
            { Geom l, r; K.split(piece0, K.make_point(R(1, 2), R(1000)), l, r); });
  // an endpoint is not an interior point
  CHECK_ERR(ErrorCode::InvalidArgument,
            { Geom l, r; K.split(piece0, K.xcurve_source(piece0), l, r); });

  // trim (private ctor as well: only via trim_2_object)
  const Geom t1p = bezier::point_at(piece0, R(1, 20));
  const Geom t2p = bezier::point_at(piece0, R(3, 20));
  const Geom trimmed = K.trim(piece0, t1p, t2p);
  bezier::parameter_range(trimmed, a, b);
  CHECK_NEAR(a, 0.05, 1e-12);
  CHECK_NEAR(b, 0.15, 1e-12);
  CHECK_ERR(ErrorCode::InvalidArgument, K.trim(piece0, t1p, t1p));

  // intersect: the cubic against the quadratic; the map measured 3 intersection points overall
  const Geom B2 = make_B2();
  std::vector<Geom> q;
  K.make_x_monotone(B2, q);
  std::size_t n_points = 0, n_overlaps = 0;
  for (const Geom& pc : pieces) {
    std::vector<IntersectionResult> res;
    K.intersect(pc, q[0], res);
    for (const IntersectionResult& r : res) {
      if (r.is_point) {
        ++n_points;
        // Bezier reports multiplicity 0 (unknown) or 1 only (traits_bezier.md gotcha 11)
        CHECK(r.multiplicity <= 1);
      } else {
        ++n_overlaps;
      }
    }
  }
  std::printf("   intersect(B1 pieces, B2) -> %zu points, %zu overlaps\n", n_points, n_overlaps);
  CHECK(n_points == 3);
  CHECK(n_overlaps == 0);

  // compare_y_at_x_left at the vertical tangency shared by pieces 0 and 1.  Both pieces are
  // defined to the LEFT of it (that is CGAL's precondition), and since y(t) = 3t on B1, just
  // left of the tangency piece 0 (t < t1) lies BELOW piece 1 (t > t1).
  CHECK(K.compare_y_at_x_left(pieces[0], pieces[1], K.xcurve_target(pieces[0])) == -1);
}

// ===========================================================================================
// 6. convert_curve from other kinds (raw CGAL objects boxed by hand)
// ===========================================================================================
static void test_convert_curve() {
  std::printf("-- convert_curve\n");
  const KindOps& K = ops(Kind::Bezier);

  // --- from Kind::Segment (Arr_segment_2<Epeck>) -------------------------------------------
  const SegCurve seg(epoint(1, 2), epoint(4, 6));
  const Geom gseg = make_geom(Kind::Segment, GeomType::Curve, seg);
  std::vector<Geom> out;
  K.convert_curve(gseg, out);
  CHECK(out.size() == 1);
  CHECK(out[0].kind == Kind::Bezier && out[0].type == GeomType::Curve);
  CHECK(bezier::number_of_control_points(out[0]) == 2);        // degree-1 Bezier
  CHECK(K.curve_repr(out[0]) == "BezierCurve([(1, 2), (4, 6)])");
  // ... and it really is that straight segment: B(1/2) = midpoint (5/2, 4)
  {
    std::vector<Rational> e;
    K.point_exact_rational(bezier::point_at(out[0], R(1, 2)), e);
    CHECK(e[0] == R(5, 2) && e[1] == R(4));
  }
  // the same object boxed as an XCurve (Arr_segment_2 is both Curve_2 and X_monotone_curve_2)
  K.convert_curve(make_geom(Kind::Segment, GeomType::XCurve, seg), out);
  CHECK(out.size() == 1);

  // --- from Kind::Polyline (internal::Polycurve_2) -----------------------------------------
  std::vector<SegCurve> segs = {SegCurve(epoint(0, 0), epoint(1, 1)),
                                SegCurve(epoint(1, 1), epoint(2, 0)),
                                SegCurve(epoint(2, 0), epoint(3, 2))};
  const PolyCurve pl(segs.begin(), segs.end());
  K.convert_curve(make_geom(Kind::Polyline, GeomType::Curve, pl), out);
  CHECK(out.size() == 3);                                       // one Bezier curve per sub-segment
  CHECK(K.curve_repr(out[0]) == "BezierCurve([(0, 0), (1, 1)])");
  CHECK(K.curve_repr(out[1]) == "BezierCurve([(1, 1), (2, 0)])");
  CHECK(K.curve_repr(out[2]) == "BezierCurve([(2, 0), (3, 2)])");

  // --- from Kind::Linear (Arr_linear_object_2) ---------------------------------------------
  const LinCurve lseg(epoint(-1, -1), epoint(5, 3));
  K.convert_curve(make_geom(Kind::Linear, GeomType::Curve, lseg), out);
  CHECK(out.size() == 1);
  CHECK(K.curve_repr(out[0]) == "BezierCurve([(-1, -1), (5, 3)])");
  const LinCurve lray(EK::Ray_2(epoint(0, 0), epoint(1, 1)));
  CHECK_ERR(ErrorCode::Unsupported,
            K.convert_curve(make_geom(Kind::Linear, GeomType::Curve, lray), out));
  const LinCurve lline(EK::Line_2(epoint(0, 0), epoint(1, 1)));
  CHECK_ERR(ErrorCode::Unsupported,
            K.convert_curve(make_geom(Kind::Linear, GeomType::Curve, lline), out));

  // --- identity and refusals ----------------------------------------------------------------
  const Geom B1 = make_B1();
  K.convert_curve(B1, out);
  CHECK(out.size() == 1 && bezier::curve_id(out[0]) == bezier::curve_id(B1));
  CHECK_ERR(ErrorCode::InvalidArgument, K.convert_curve(K.make_point(R(0), R(0)), out));
  // a kind we cannot inspect without its TU (circle_segment is not linked here)
  CHECK_ERR(ErrorCode::Unsupported,
            K.convert_curve(make_geom(Kind::CircleSegment, GeomType::Curve, 42), out));
}

// ===========================================================================================
// 7. full arrangement round trip
// ===========================================================================================
static void test_arrangement() {
  std::printf("-- arrangement round trip\n");
  const KindOps& K = ops(Kind::Bezier);
  const Geom B1 = make_B1();
  const Geom B2 = make_B2();

  std::unique_ptr<ArrBase> arr = make_arrangement(Kind::Bezier);
  CHECK(arr->kind() == Kind::Bezier);
  CHECK(!arr->is_unbounded_kind());
  CHECK(arr->is_empty());
  CHECK(arr->number_of_faces() == 1);

  const CH c1 = arr->insert_curve(B1);
  const CH c2 = arr->insert_curve(B2);
  std::printf("   V=%zu E=%zu H=%zu F=%zu curves=%zu\n", arr->number_of_vertices(),
              arr->number_of_edges(), arr->number_of_halfedges(), arr->number_of_faces(),
              arr->number_of_curves());
  // traits_bezier.md §11 measured V=9 E=10 F=3 for exactly these two curves.
  // Cross-check by hand: B1 has 2 vertical tangencies -> 4 vertices / 3 edges on its own;
  // B2 is x-monotone -> 2 vertices / 1 edge; they cross at 3 points, each adding a vertex and
  // splitting one edge of each curve: V = 4+2+3 = 9, E = (3+3)+(1+3) = 10, and Euler gives
  // F = 2 - V + E = 3 (one unbounded + two bounded faces).
  CHECK(arr->number_of_vertices() == 9);
  CHECK(arr->number_of_edges() == 10);
  CHECK(arr->number_of_halfedges() == 20);
  CHECK(arr->number_of_faces() == 3);
  CHECK(arr->number_of_unbounded_faces() == 1);
  CHECK(arr->number_of_curves() == 2);
  CHECK(arr->number_of_vertices_at_infinity() == 0);
  CHECK(arr->is_valid());
  CHECK_ERR(ErrorCode::Unsupported, arr->fictitious_face());

  // history
  CHECK(arr->number_of_induced_edges(c1) == 6);
  CHECK(arr->number_of_induced_edges(c2) == 4);
  std::vector<HH> induced;
  arr->induced_edges(c1, induced);
  CHECK(induced.size() == 6);
  CHECK(arr->number_of_originating_curves(induced[0]) == 1);
  CHECK(bezier::curve_id(arr->curve_geometry(c1)) == bezier::curve_id(B1));

  // iteration
  std::vector<VH> vs;
  std::vector<HH> hs, es;
  std::vector<FH> fs;
  arr->vertices(vs);
  arr->halfedges(hs);
  arr->edges(es);
  arr->faces(fs);
  CHECK(vs.size() == 9 && hs.size() == 20 && es.size() == 10 && fs.size() == 3);
  std::size_t degree_sum = 0;
  for (const VH& v : vs) {
    CHECK(arr->vertex_valid(v));
    CHECK(!arr->vertex_is_at_open_boundary(v));
    degree_sum += arr->vertex_degree(v);
    const Geom vp = arr->vertex_point(v);
    CHECK(vp.kind == Kind::Bezier && vp.type == GeomType::Point);
  }
  CHECK(degree_sum == 20);                       // sum of degrees == number of halfedges

  // bulk export
  std::vector<double> coords;
  arr->vertex_coordinates(coords);
  CHECK(coords.size() == 18);
  std::vector<std::size_t> eidx;
  arr->edge_vertex_indices(eidx);
  CHECK(eidx.size() == 20);
  const BBox ab = arr->bbox();
  std::printf("   arrangement bbox = (%.6f, %.6f) - (%.6f, %.6f)\n", ab.lo[0], ab.lo[1], ab.hi[0],
              ab.hi[1]);
  CHECK_NEAR(ab.lo[0], 0.0, 1e-9);               // leftmost vertex is B1(0) = (0,0)
  CHECK_NEAR(ab.hi[0], 3.0, 1e-9);               // rightmost is B2(1) = (3,4)
  CHECK_NEAR(ab.lo[1], 0.0, 1e-9);
  CHECK_NEAR(ab.hi[1], 4.0, 1e-9);

  // ---- point location -----------------------------------------------------------------------
  const Geom far = K.make_point(R(100), R(100));
  const Geom origin = K.make_point(R(0), R(0));   // B1(0): a real vertex
  Located ref_far = arr->locate(far, PL_DEFAULT);
  CHECK(ref_far.type == 2);                       // a face
  CHECK(arr->face_is_unbounded(ref_far.as_face()));

  const int strategies[] = {PL_NAIVE, PL_SIMPLE, PL_WALK, PL_LANDMARKS, PL_TRAPEZOID,
                            PL_TRIANGULATION};
  for (int s : strategies) {
    const bool supported = arr->supports_point_location(s);
    // KindPolicy<BezierTypes>: naive/simple/walk/trapezoid yes; landmarks no (the traits has no
    // Approximate_2 and no Construct_x_monotone_curve_2); triangulation no (needs a Kernel).
    const bool expected = (s == PL_NAIVE || s == PL_SIMPLE || s == PL_WALK || s == PL_TRAPEZOID);
    CHECK(supported == expected);
    if (supported) {
      Located l = arr->locate(far, s);
      CHECK(l.type == 2 && l.p == ref_far.p);
      Located v = arr->locate(origin, s);
      CHECK(v.type == 0);
      arr->attach_point_location(s);
      CHECK(arr->has_point_location(s));
      Located l2 = arr->locate(far, s);
      CHECK(l2.type == 2 && l2.p == ref_far.p);
      arr->detach_point_location(s);
      CHECK(!arr->has_point_location(s));
    } else {
      CHECK_ERR(ErrorCode::Unsupported, arr->locate(far, s));
      CHECK_ERR(ErrorCode::Unsupported, arr->attach_point_location(s));
    }
  }
  // vertical ray shooting (simple / walk / trapezoid)
  {
    Located up = arr->ray_shoot_up(K.make_point(R(1), R(-5)), PL_WALK);
    CHECK(up.type == 1 || up.type == 0);          // it hits B1 or B2 somewhere above y = -5
    CHECK_ERR(ErrorCode::Unsupported, arr->ray_shoot_up(far, PL_NAIVE));
  }

  // ---- batched locate ------------------------------------------------------------------------
  {
    std::vector<Geom> qs = {far, origin, far, K.make_point(R(1000), R(0))};
    std::vector<Located> res;
    arr->batched_locate(qs, res);
    CHECK(res.size() == 4);
    CHECK(res[0].type == 2 && res[0].p == ref_far.p);
    CHECK(res[1].type == 0);                      // the vertex at (0,0)
    CHECK(res[2].p == res[0].p);                  // duplicate query point handled by value
    CHECK(res[3].type == 2 && res[3].p == ref_far.p);
  }

  // ---- he_curve / he_directed_curve around a bounded face -------------------------------------
  FH bounded{};
  for (const FH& f : fs) {
    if (arr->face_is_unbounded(f)) continue;
    if (!arr->face_has_outer_ccb(f)) continue;
    bounded = f;
    break;
  }
  CHECK(bounded.p != nullptr);
  {
    std::vector<HH> ccb;
    arr->he_ccb(arr->face_outer_ccb(bounded), ccb);
    CHECK(ccb.size() >= 2);
    std::printf("   bounded face outer ccb has %zu halfedges\n", ccb.size());
    for (std::size_t i = 0; i < ccb.size(); ++i) {
      const HH& h = ccb[i];
      const HH& nx = ccb[(i + 1) % ccb.size()];
      CHECK(!arr->he_is_fictitious(h));
      const Geom cv = arr->he_curve(h);
      const Geom dcv = arr->he_directed_curve(h);
      CHECK(cv.type == GeomType::XCurve && dcv.type == GeomType::XCurve);
      CHECK(K.curve_equal(cv, dcv));              // same geometry, possibly opposite direction
      // the directed curve runs source -> target of the halfedge
      CHECK(K.point_equal(K.xcurve_source(dcv), arr->vertex_point(arr->he_source(h))));
      CHECK(K.point_equal(K.xcurve_target(dcv), arr->vertex_point(arr->he_target(h))));
      // targets chain: this curve's target is the next curve's source
      const Geom dnx = arr->he_directed_curve(nx);
      CHECK(K.point_equal(K.xcurve_target(dcv), K.xcurve_source(dnx)));
      CHECK(arr->he_face(h).p == bounded.p);
    }

    // face_polygon of the same face returns exactly those directed curves
    std::vector<Geom> outer;
    std::vector<std::vector<Geom>> holes;
    arr->face_polygon(bounded, outer, holes);
    CHECK(outer.size() == ccb.size());
    CHECK(holes.empty());
    for (std::size_t i = 0; i < outer.size(); ++i)
      CHECK(K.point_equal(K.xcurve_target(outer[i]),
                          K.xcurve_source(outer[(i + 1) % outer.size()])));
  }

  // the unbounded face has NO outer ccb and exactly one inner ccb (the arrangement is connected)
  {
    const FH uf = arr->unbounded_face();
    CHECK(arr->face_is_unbounded(uf));
    CHECK(!arr->face_has_outer_ccb(uf));
    CHECK(arr->face_number_of_inner_ccbs(uf) == 1);
    std::vector<Geom> outer;
    std::vector<std::vector<Geom>> holes;
    arr->face_polygon(uf, outer, holes);
    CHECK(outer.empty());
    CHECK(holes.size() == 1);
    // The hole cycle walks the outer boundary of the (single) connected component: an edge that
    // separates two real faces is seen once, an edge with the unbounded face on both sides twice.
    // Together with the two bounded faces' cycles it must account for all 20 halfedges.
    std::size_t ccb_total = holes[0].size();
    for (const FH& f : fs) {
      if (arr->face_is_unbounded(f)) continue;
      std::vector<HH> cyc;
      arr->he_ccb(arr->face_outer_ccb(f), cyc);
      ccb_total += cyc.size();
    }
    std::printf("   unbounded face hole cycle: %zu halfedges (all cycles: %zu of 20)\n",
                holes[0].size(), ccb_total);
    CHECK(ccb_total == 20);
    for (std::size_t i = 0; i < holes[0].size(); ++i)
      CHECK(K.point_equal(K.xcurve_target(holes[0][i]),
                          K.xcurve_source(holes[0][(i + 1) % holes[0].size()])));
    CHECK_ERR(ErrorCode::InvalidArgument, arr->face_outer_ccb(uf));
  }

  // ---- zone / do_intersect ---------------------------------------------------------------------
  {
    // a straight (degree-1) Bezier curve crossing the whole arrangement at y = 3/2
    const Geom probe = bezier::make({R(-1), R(3, 2), R(5), R(3, 2)});
    CHECK(arr->do_intersect(probe));
    std::vector<Located> zone;
    arr->zone(probe, zone);
    std::printf("   zone(y = 3/2 segment) -> %zu features\n", zone.size());
    CHECK(zone.size() >= 3);
    CHECK(zone.front().type == 2);                // starts in the unbounded face
    std::size_t nf = 0, ne = 0, nv = 0;
    for (const Located& l : zone) {
      if (l.type == 0) ++nv;
      else if (l.type == 1) ++ne;
      else if (l.type == 2) ++nf;
    }
    CHECK(nf >= 2 && ne >= 1);
    CHECK(nv + ne + nf == zone.size());
    CHECK(arr->number_of_edges() == 10);          // zone() does not modify the arrangement

    const Geom away = bezier::make({R(100), R(100), R(101), R(101)});
    CHECK(!arr->do_intersect(away));
  }

  // ---- decompose --------------------------------------------------------------------------------
  {
    std::vector<VerticalDecompositionEntry> dec;
    arr->decompose(dec);
    std::printf("   decompose -> %zu entries\n", dec.size());
    CHECK(dec.size() == 9);                        // one entry per vertex
    for (const VerticalDecompositionEntry& e : dec) CHECK(arr->vertex_valid(e.v));
    // the leftmost vertex is B1(0) = (0,0); nothing lies below it except the unbounded face
    CHECK(dec.front().below.type == 2 || dec.front().below.type == -1);
  }

  // ---- clone -------------------------------------------------------------------------------------
  {
    std::unique_ptr<ArrBase> copy = arr->clone();
    CHECK(copy->number_of_vertices() == 9);
    CHECK(copy->number_of_edges() == 10);
    CHECK(copy->number_of_faces() == 3);
    CHECK(copy->number_of_curves() == 2);
    CHECK(copy->is_valid());
    // handles of the original are foreign to the copy
    CHECK(!copy->vertex_valid(vs[0]));
    CHECK(arr->vertex_valid(vs[0]));
    // the clone is independent
    copy->clear();
    CHECK(copy->is_empty());
    CHECK(arr->number_of_vertices() == 9);
  }

  // ---- remove_curve -------------------------------------------------------------------------------
  {
    const std::size_t removed = arr->remove_curve(c2);
    std::printf("   remove_curve(B2) removed %zu edges -> V=%zu E=%zu F=%zu curves=%zu\n", removed,
                arr->number_of_vertices(), arr->number_of_edges(), arr->number_of_faces(),
                arr->number_of_curves());
    CHECK(removed == 4);                            // B2 induced 4 edges
    CHECK(arr->number_of_curves() == 1);
    // Arrangement_on_surface_with_history_2::_remove_curve() calls the plain
    // Base_arr_2::remove_edge(he), which drops an endpoint only when it becomes ISOLATED — it
    // never merges the two surviving edges of a degree-2 vertex.  So B2's own two endpoints
    // ((0,3) and (3,4)) disappear and the 3 crossing vertices SURVIVE, each now of degree 2 on
    // B1: V = 9 - 2 = 7, E = 10 - 4 = 6, F = 1 (B1 alone bounds nothing).  Euler: 7-6+1 = 2.
    CHECK(arr->number_of_vertices() == 7);
    CHECK(arr->number_of_edges() == 6);
    CHECK(arr->number_of_faces() == 1);
    CHECK(arr->is_valid());

    // CGAL::remove_vertex() is what merges a redundant degree-2 vertex.  Exactly 3 of the 7
    // vertices qualify: the former crossings, whose two incident edges share B1's supporting
    // curve AND its xid (Bezier's Are_mergeable_2 requires both, plus a handle-identical shared
    // endpoint).  The two vertical-tangency vertices have neighbours with DIFFERENT xids and are
    // therefore not mergeable, and B1's two endpoints have degree 1.
    std::vector<VH> left_vs;
    arr->vertices(left_vs);
    std::size_t merged = 0;
    for (const VH& v : left_vs)
      if (arr->vertex_degree(v) == 2 && arr->remove_vertex(v)) ++merged;
    std::printf("   remove_vertex merged %zu redundant vertices -> V=%zu E=%zu\n", merged,
                arr->number_of_vertices(), arr->number_of_edges());
    CHECK(merged == 3);
    CHECK(arr->number_of_vertices() == 4);          // B1(0), the two tangencies, B1(1)
    CHECK(arr->number_of_edges() == 3);             // the 3 x-monotone pieces of B1
    CHECK(arr->number_of_faces() == 1);
    CHECK(arr->is_valid());
    CHECK(!arr->curve_valid(c2));
    CHECK(arr->curve_valid(c1));
    CHECK_ERR(ErrorCode::InvalidHandle, arr->number_of_induced_edges(c2));
  }

  // ---- kind mismatch --------------------------------------------------------------------------------
  CHECK_ERR(ErrorCode::KindMismatch,
            arr->insert_curve(make_geom(Kind::Segment, GeomType::Curve,
                                        SegCurve(epoint(0, 0), epoint(1, 1)))));
}

// ===========================================================================================
// 7b. the same arrangement built by the AGGREGATE (sweep) insertion path
// ===========================================================================================
static void test_aggregate_insert() {
  std::printf("-- aggregate insertion\n");
  std::unique_ptr<ArrBase> arr = make_arrangement(Kind::Bezier);
  std::vector<Geom> curves = {make_B1(), make_B2()};
  std::vector<CH> handles;
  arr->insert_curves(curves, handles);
  std::printf("   V=%zu E=%zu F=%zu curves=%zu\n", arr->number_of_vertices(),
              arr->number_of_edges(), arr->number_of_faces(), arr->number_of_curves());
  CHECK(handles.size() == 2);
  CHECK(arr->number_of_vertices() == 9);          // same result as the incremental path
  CHECK(arr->number_of_edges() == 10);
  CHECK(arr->number_of_faces() == 3);
  CHECK(arr->number_of_curves() == 2);
  CHECK(arr->is_valid());
  CHECK(arr->number_of_induced_edges(handles[0]) == 6);
  CHECK(arr->number_of_induced_edges(handles[1]) == 4);
  // an isolated point in the unbounded face
  const VH iv = arr->insert_point(ops(Kind::Bezier).make_point(R(50), R(50)));
  CHECK(arr->vertex_is_isolated(iv));
  CHECK(arr->number_of_isolated_vertices() == 1);
  CHECK(arr->is_valid());
}

// ===========================================================================================
// 8. registry wiring
// ===========================================================================================
static void test_registry() {
  std::printf("-- registry\n");
  CHECK(kind_available(Kind::Bezier));
  CHECK(kind_has_polygon_set(Kind::Bezier));
  CHECK(kind_from_name("bezier") == int(Kind::Bezier));
  CHECK(std::string(kind_name(Kind::Bezier)) == "bezier");
  // the other kind TUs are deliberately not linked into this test
  CHECK(!kind_available(Kind::Segment));
  CHECK_ERR(ErrorCode::Unsupported, ops(Kind::Segment));
}

int main() {
  init_all_kinds();
  std::printf("arr2d bezier kind test — %s\n", build_info().c_str());
  test_registry();
  test_points();
  test_curve_constructors();
  test_x_monotone();
  test_approximate();
  test_traits_functors();
  test_convert_curve();
  test_arrangement();
  test_aggregate_insert();
  std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
  // Exiting normally is part of the test (CORE MemoryPool teardown abort).
  return g_failures == 0 ? 0 : 1;
}
