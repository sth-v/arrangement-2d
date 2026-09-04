// arr2d — self-contained C++ test for Kind::Polyline (src/arr2d/src/kind_polyline.cpp).
//
// Every expected number in this file is hand-derived; the derivation is in the comment next to
// the check.  The program must exit with status 0 ("0 failures" and a normal return), which also
// proves that nothing aborts during static destruction.
//
// ---------------------------------------------------------------------------------------------
// BUILD & RUN
//
//   REPO=/Users/sthv/PycharmProjects/arrangement-2d
//   OUT=<scratch dir>
//   for f in registry numbers overlay; do
//     /usr/bin/clang++ -std=c++17 -O0 -g -Wall -Wextra -c -DCGAL_USE_CORE -DCGAL_USE_GMP \
//       -DCGAL_USE_MPFR -I/opt/homebrew/include -I$REPO/src/arr2d/include \
//       $REPO/src/arr2d/src/$f.cpp -o $OUT/$f.o
//   done
//   /usr/bin/clang++ -std=c++17 -O0 -g -Wall -Wextra -c -DCGAL_USE_CORE -DCGAL_USE_GMP \
//     -DCGAL_USE_MPFR -I/opt/homebrew/include -I$REPO/src/arr2d/include \
//     $REPO/src/arr2d/src/kind_polyline.cpp -o $OUT/kind_polyline.o
//   /usr/bin/clang++ -std=c++17 -O0 -g -Wall -Wextra -c -DCGAL_USE_CORE -DCGAL_USE_GMP \
//     -DCGAL_USE_MPFR -I/opt/homebrew/include -I$REPO/src/arr2d/include \
//     $REPO/src/arr2d/tests/test_kind_polyline.cpp -o $OUT/test_kind_polyline.o
//   /usr/bin/clang++ -std=c++17 -g -o $OUT/test_kind_polyline \
//     $OUT/test_kind_polyline.o $OUT/kind_polyline.o $OUT/registry.o $OUT/numbers.o $OUT/overlay.o \
//     -L/opt/homebrew/lib -lgmp -lmpfr
//   $OUT/test_kind_polyline ; echo "exit=$?"
//
// The BSO translation unit is NOT linked.  The polyline kind has no Boolean set operations at
// all — bso.hpp declares no `make_polygon_set_polyline`, and kind_polyline.cpp registers a null
// `KindEntry::make_polygon_set` — so, unlike the segment / circle-segment / conic / Bezier tests,
// this test needs no factory stub.  The `ARR2D_TEST_STUB_BSO` switch is honoured for uniformity
// with those tests, but it defines nothing for this kind; both command lines below build and run
// identically:
//
//   (1) without the stub (the normal case):
//         ... -c $REPO/src/arr2d/tests/test_kind_polyline.cpp -o $OUT/test_kind_polyline.o
//   (2) with the stub switch (accepted, no effect for this kind):
//         ... -DARR2D_TEST_STUB_BSO -c $REPO/src/arr2d/tests/test_kind_polyline.cpp \
//             -o $OUT/test_kind_polyline.o
// ---------------------------------------------------------------------------------------------

#include "arr2d/kinds/polyline_types.hpp"
#include "arr2d/kinds/segment_types.hpp"   // types only: lets the test box raw Arr_segment_2 Geoms
#include "arr2d/kinds/linear_types.hpp"    // types only: lets the test box raw Arr_linear_object_2

#include "arr2d/arrangement.hpp"
#include "arr2d/common.hpp"
#include "arr2d/numbers.hpp"
#include "arr2d/ops.hpp"
#include "arr2d/registry.hpp"

#include <CGAL/assertions.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#ifdef ARR2D_TEST_STUB_BSO
// Deliberately empty: the polyline kind registers a null polygon-set factory, so no
// arr2d::make_polygon_set_polyline symbol is referenced and no stub is needed.
#endif

using namespace arr2d;

// ===========================================================================
// tiny harness
// ===========================================================================
namespace {

int g_checks = 0;
int g_failures = 0;
std::string g_section;

void section(const char* name) {
  g_section = name;
  std::printf("\n--- %s ---\n", name);
}

void check(bool ok, const std::string& what) {
  ++g_checks;
  if (!ok) {
    ++g_failures;
    std::printf("FAIL [%s] %s\n", g_section.c_str(), what.c_str());
  }
}

void check_eq_sz(std::size_t got, std::size_t want, const std::string& what) {
  check(got == want, what + ": got " + std::to_string(got) + ", want " + std::to_string(want));
}

void check_eq_int(int got, int want, const std::string& what) {
  check(got == want, what + ": got " + std::to_string(got) + ", want " + std::to_string(want));
}

void check_close(double got, double want, double tol, const std::string& what) {
  check(std::fabs(got - want) <= tol,
        what + ": got " + std::to_string(got) + ", want " + std::to_string(want));
}

void check_eq_str(const std::string& got, const std::string& want, const std::string& what) {
  check(got == want, what + ": got \"" + got + "\", want \"" + want + "\"");
}

/// Runs `fn`, expecting an arr2d::Error with the given code.
template <class F>
void expect_error(ErrorCode code, F fn, const std::string& what) {
  try {
    fn();
  } catch (const Error& e) {
    check(e.code == code, what + ": wrong code (" + std::to_string(int(e.code)) + " vs " +
                              std::to_string(int(code)) + "), message: " + e.what());
    return;
  } catch (const std::exception& e) {
    check(false, what + ": wrong exception type: " + e.what());
    return;
  }
  check(false, what + ": no exception was thrown");
}

/// Runs `fn`, expecting an arr2d::Error with either of two codes.
template <class F>
void expect_error2(ErrorCode a, ErrorCode b, F fn, const std::string& what) {
  try {
    fn();
  } catch (const Error& e) {
    check(e.code == a || e.code == b,
          what + ": unexpected code " + std::to_string(int(e.code)) + ", message: " + e.what());
    return;
  } catch (const std::exception& e) {
    check(false, what + ": wrong exception type: " + e.what());
    return;
  }
  check(false, what + ": no exception was thrown");
}

/// Runs `fn`, expecting a CGAL precondition/assertion failure.
template <class F>
void expect_cgal_failure(F fn, const std::string& what) {
  try {
    fn();
  } catch (const CGAL::Failure_exception& e) {
    check(true, what);
    std::printf("    (expected CGAL failure: %s)\n", e.what());
    return;
  } catch (const std::exception& e) {
    check(false, what + ": wrong exception type: " + e.what());
    return;
  }
  check(false, what + ": no CGAL failure was raised");
}

// ---------------------------------------------------------------------------
// convenience constructors
// ---------------------------------------------------------------------------
const KindOps& O() { return arr2d::ops(Kind::Polyline); }

Rational R(long n, long d = 1) { return Rational(n) / Rational(d); }

Geom P(long x, long y) { return O().make_point(Rational(x), Rational(y)); }
Geom Pr(const Rational& x, const Rational& y) { return O().make_point(x, y); }

Geom PL(const std::vector<std::pair<long, long>>& pts) {
  std::vector<Geom> gs;
  for (const auto& p : pts) gs.push_back(P(p.first, p.second));
  return polyline::make(gs);
}

Geom PLX(const std::vector<std::pair<long, long>>& pts) {
  std::vector<Geom> gs;
  for (const auto& p : pts) gs.push_back(P(p.first, p.second));
  return polyline::make_x_monotone(gs);
}

/// KindOps methods only accept points of their own kind (ops.hpp), and polyline::point() /
/// polyline::subcurve() deliberately hand out Kind::Segment boxes, so re-kind first.
Geom as_poly_point(const Geom& p) {
  return (p.kind == Kind::Polyline) ? p : O().convert_point(p);
}

/// (x, y) of a point box, as doubles.
std::pair<double, double> xy(const Geom& p) {
  double c[3] = {0, 0, 0};
  O().point_approx(as_poly_point(p), c);
  return {c[0], c[1]};
}

/// exact (x, y) of a point box.
std::pair<Rational, Rational> exact_xy(const Geom& p) {
  std::vector<Rational> v;
  O().point_exact_rational(as_poly_point(p), v);
  return {v[0], v[1]};
}

bool point_is(const Geom& p, long x, long y) {
  const auto e = exact_xy(p);
  return e.first == Rational(x) && e.second == Rational(y);
}

std::string pstr(const Geom& p) { return O().point_repr(as_poly_point(p)); }

// --- raw boxes of OTHER kinds (built here from raw CGAL objects, no other TU needed) ---
using EPoint = PolylineTypes::Point_2;                 // Epeck::Point_2, shared by seg/lin/poly
using ESeg = SegmentTypes::Curve_2;                    // Arr_segment_2<Epeck>
using ELin = LinearTypes::Curve_2;                     // Arr_linear_object_2<Epeck>
using EKernel = PolylineTypes::Kernel;

EPoint ep(long x, long y) {
  return EPoint(PolylineTypes::FT(Rational(x)), PolylineTypes::FT(Rational(y)));
}

Geom segment_geom(long x1, long y1, long x2, long y2, GeomType t = GeomType::XCurve) {
  return make_geom(Kind::Segment, t, ESeg(ep(x1, y1), ep(x2, y2)));
}
Geom linear_segment_geom(long x1, long y1, long x2, long y2) {
  return make_geom(Kind::Linear, GeomType::Curve, ELin(ep(x1, y1), ep(x2, y2)));
}
Geom linear_ray_geom(long x1, long y1, long x2, long y2) {
  return make_geom(Kind::Linear, GeomType::Curve, ELin(EKernel::Ray_2(ep(x1, y1), ep(x2, y2))));
}
Geom linear_line_geom(long x1, long y1, long x2, long y2) {
  return make_geom(Kind::Linear, GeomType::Curve, ELin(EKernel::Line_2(ep(x1, y1), ep(x2, y2))));
}

/// Squared distance from (px, py) to the segment (ax, ay)-(bx, by), in doubles.
double sq_dist_to_segment(double px, double py, double ax, double ay, double bx, double by) {
  const double vx = bx - ax, vy = by - ay;
  const double wx = px - ax, wy = py - ay;
  const double vv = vx * vx + vy * vy;
  double t = (vv > 0.0) ? ((wx * vx + wy * vy) / vv) : 0.0;
  t = std::max(0.0, std::min(1.0, t));
  const double dx = px - (ax + t * vx), dy = py - (ay + t * vy);
  return dx * dx + dy * dy;
}

/// Distance from (px, py) to the polyline given as a flat x,y array.
double dist_to_polyline(const std::vector<double>& poly, double px, double py) {
  double best = 1e300;
  for (std::size_t i = 2; i < poly.size(); i += 2)
    best = std::min(best, sq_dist_to_segment(px, py, poly[i - 2], poly[i - 1], poly[i], poly[i + 1]));
  return std::sqrt(best);
}

/// Silences CGAL's default handler (which prints the whole violation block to stderr before the
/// exception is thrown — number_types_and_errors.md gotcha 5).  The exception still propagates.
void silent_cgal_handler(const char*, const char*, const char*, int, const char*) {}

}  // namespace

// ===========================================================================
// 1. points
// ===========================================================================
static void test_points() {
  section("points");

  const Geom p = Pr(R(1), R(3, 2));   // (1, 3/2)
  auto a = xy(p);
  check_close(a.first, 1.0, 0.0, "point_approx x of (1, 3/2)");
  check_close(a.second, 1.5, 0.0, "point_approx y of (1, 3/2)");

  auto e = exact_xy(p);
  check(e.first == Rational(1) && e.second == R(3, 2), "point_exact_rational of (1, 3/2)");
  check(O().point_is_rational(p), "Epeck points are always rational");

  // 1/3 is not a double: point_approx must be CORRECTLY rounded, i.e. exactly (double)(1.0/3.0)
  // == 0x3FD5555555555555.  CGAL::to_double(Epeck::FT) would give 0x3FD5555555555556
  // (number_types_and_errors.md gotcha 2).
  const Geom third = Pr(R(1, 3), R(0));
  const double gx = xy(third).first;
  check(gx == 1.0 / 3.0, "point_approx(1/3) is the correctly rounded double");
  check(gx != std::nextafter(1.0 / 3.0, 1.0), "point_approx(1/3) is not the next double up");

  // exact boxed numbers
  std::vector<Geom> nums;
  O().point_exact(p, nums);
  check_eq_sz(nums.size(), 2, "point_exact yields 2 numbers");
  check(number_kind(nums[0]) == NumberKind::Rational, "point_exact x is a Rational box");
  check(number_to_rational(nums[1]) == R(3, 2), "point_exact y == 3/2");
  check_eq_str(number_repr(nums[1]), "3/2", "number_repr of the y coordinate");

  // certified intervals
  std::vector<std::pair<double, double>> iv;
  O().point_interval(third, iv);
  check_eq_sz(iv.size(), 2, "point_interval yields 2 intervals");
  check(iv[0].first <= 1.0 / 3.0 && 1.0 / 3.0 <= iv[0].second, "interval of 1/3 encloses it");
  check(iv[0].first <= iv[0].second, "interval of 1/3 is ordered");
  check(iv[1].first == 0.0 && iv[1].second == 0.0, "interval of 0 is degenerate");

  // comparisons
  check_eq_int(O().point_compare_x(p, third), 1, "compare_x((1,3/2),(1/3,0))");
  check_eq_int(O().point_compare_x(third, p), -1, "compare_x((1/3,0),(1,3/2))");
  check_eq_int(O().point_compare_xy(P(1, 0), P(1, 2)), -1, "compare_xy((1,0),(1,2))");
  check_eq_int(O().point_compare_xy(P(1, 2), P(1, 2)), 0, "compare_xy equal points");
  check(O().point_equal(P(2, 3), P(2, 3)), "point_equal on equal points");
  check(!O().point_equal(P(2, 3), P(3, 2)), "point_equal on different points");

  // repr
  check_eq_str(pstr(p), "Point(1, 3/2)", "point_repr");
  check_eq_str(pstr(Pr(R(-7, 4), R(0))), "Point(-7/4, 0)", "point_repr of a negative fraction");

  // convert_point
  const Geom seg_pt = make_geom(Kind::Segment, GeomType::Point, ep(5, 6));
  const Geom conv = O().convert_point(seg_pt);
  check(conv.kind == Kind::Polyline && conv.type == GeomType::Point, "convert_point re-kinds a segment point");
  check(point_is(conv, 5, 6), "convert_point keeps (5,6)");
  check(O().convert_point(p).kind == Kind::Polyline, "convert_point of a polyline point is the identity");
  expect_error(ErrorCode::InvalidArgument, [&] { O().convert_point(PL({{0, 0}, {1, 1}})); },
               "convert_point rejects a curve box");
  expect_error(ErrorCode::InvalidArgument, [&] { O().convert_point(box_rational(R(1))); },
               "convert_point rejects a number box");

  // make_point_3 is sphere-only
  expect_error(ErrorCode::Unsupported, [&] { O().make_point_3(R(1), R(0), R(0)); },
               "make_point_3 is unsupported for polylines");

  // kind bookkeeping
  check(O().kind() == Kind::Polyline, "ops().kind()");
  check_eq_str(O().name(), "polyline", "ops().name()");
  check_eq_int(O().dimension(), 2, "ops().dimension()");
  check(!O().is_unbounded_kind(), "polyline is a bounded kind");
  check(!O().has_polygon_set(), "polyline has no Boolean set operations");
  check(!kind_has_polygon_set(Kind::Polyline), "registry: no polygon set for polyline");
  expect_error(ErrorCode::Unsupported, [&] { (void)make_polygon_set(Kind::Polyline); },
               "make_polygon_set(Polyline) is unsupported");
}

// ===========================================================================
// 2. curve constructors + accessors of namespace arr2d::polyline
// ===========================================================================
static void test_constructors() {
  section("constructors");

  // ---- polyline::make (general Curve_2) ----
  const Geom c = PL({{0, 0}, {2, 2}, {4, 0}});   // "^" shape, 2 subcurves, 3 points
  check(c.kind == Kind::Polyline && c.type == GeomType::Curve, "polyline::make returns a Curve box");
  check_eq_sz(polyline::number_of_subcurves(c), 2, "number_of_subcurves of a 3-point polyline");
  check_eq_sz(polyline::number_of_points(c), 3, "number_of_points of a 3-point polyline");
  check(point_is(polyline::point(c, 0), 0, 0), "point(0)");
  check(point_is(polyline::point(c, 1), 2, 2), "point(1)");
  check(point_is(polyline::point(c, 2), 4, 0), "point(2)");
  check(polyline::point(c, 0).kind == Kind::Segment, "polyline::point returns a Segment-kind point");
  expect_error(ErrorCode::InvalidArgument, [&] { (void)polyline::point(c, 3); },
               "point index out of range");

  // subcurves are Segment-kind boxes holding Arr_segment_2
  const Geom s0 = polyline::subcurve(c, 0);
  check(s0.kind == Kind::Segment && s0.type == GeomType::XCurve, "subcurve is a Segment XCurve box");
  check(s0.holds<ESeg>(), "subcurve holds Arr_segment_2<Epeck>");
  check(s0.as<ESeg>().source() == ep(0, 0) && s0.as<ESeg>().target() == ep(2, 2), "subcurve(0) endpoints");
  check(polyline::subcurve(c, 1).as<ESeg>().target() == ep(4, 0), "subcurve(1) target");
  expect_error(ErrorCode::InvalidArgument, [&] { (void)polyline::subcurve(c, 2); },
               "subcurve index out of range");

  check_eq_str(O().curve_repr(c), "Polyline([(0, 0), (2, 2), (4, 0)])", "curve_repr");

  // ---- errors of polyline::make ----
  expect_error(ErrorCode::InvalidArgument, [&] { (void)PL({{0, 0}}); },
               "make() needs at least 2 points");
  expect_error(ErrorCode::InvalidArgument, [&] { (void)PL({{0, 0}, {0, 0}, {1, 1}}); },
               "make() rejects equal consecutive points");
  expect_error(ErrorCode::InvalidArgument,
               [&] {
                 std::vector<Geom> bad{P(0, 0), box_rational(R(1))};
                 (void)polyline::make(bad);
               },
               "make() rejects a non-point argument");

  // points of any kind with rational coordinates are converted
  {
    std::vector<Geom> mixed{make_geom(Kind::Segment, GeomType::Point, ep(0, 0)), P(1, 1)};
    const Geom m = polyline::make(mixed);
    check_eq_sz(polyline::number_of_subcurves(m), 1, "make() accepts Segment-kind points");
  }

  // ---- polyline::make_from_segments ----
  {
    std::vector<Geom> segs{segment_geom(0, 0, 2, 2), segment_geom(2, 2, 4, 0)};
    const Geom fs = polyline::make_from_segments(segs);
    check_eq_sz(polyline::number_of_subcurves(fs), 2, "make_from_segments: 2 subcurves");
    check_eq_str(O().curve_repr(fs), "Polyline([(0, 0), (2, 2), (4, 0)])",
                 "make_from_segments reproduces the same chain as make()");
  }
  {   // sub-segments taken out of an existing polyline round-trip
    std::vector<Geom> segs{polyline::subcurve(c, 0), polyline::subcurve(c, 1)};
    const Geom fs = polyline::make_from_segments(segs);
    check_eq_str(O().curve_repr(fs), O().curve_repr(c), "make_from_segments(subcurve(0), subcurve(1))");
  }
  {   // a Linear-kind bounded segment is converted exactly
    std::vector<Geom> segs{linear_segment_geom(0, 0, 1, 1), segment_geom(1, 1, 2, 0)};
    const Geom fs = polyline::make_from_segments(segs);
    check_eq_str(O().curve_repr(fs), "Polyline([(0, 0), (1, 1), (2, 0)])",
                 "make_from_segments accepts a Linear segment");
  }
  expect_error(ErrorCode::InvalidArgument, [&] { (void)polyline::make_from_segments({}); },
               "make_from_segments needs at least one segment");
  expect_error(ErrorCode::InvalidArgument,
               [&] {
                 std::vector<Geom> segs{segment_geom(0, 0, 2, 2), segment_geom(3, 3, 4, 0)};
                 (void)polyline::make_from_segments(segs);
               },
               "make_from_segments rejects an unchained pair");
  expect_error(ErrorCode::NotRepresentable,
               [&] {
                 std::vector<Geom> segs{linear_ray_geom(0, 0, 1, 1)};
                 (void)polyline::make_from_segments(segs);
               },
               "make_from_segments rejects a ray");

  // ---- polyline::make_x_monotone ----
  const Geom xc = PLX({{0, 0}, {2, 2}, {4, 0}});
  check(xc.kind == Kind::Polyline && xc.type == GeomType::XCurve, "make_x_monotone returns an XCurve box");
  check_eq_sz(polyline::number_of_subcurves(xc), 2, "x-monotone polyline: 2 subcurves");
  check(O().is_x_monotone(xc), "is_x_monotone(XCurve)");
  check(O().is_x_monotone(c), "the ^ curve is x-monotone (x strictly increases 0 -> 2 -> 4)");
  expect_error(ErrorCode::InvalidArgument, [&] { (void)PLX({{0, 0}}); },
               "make_x_monotone needs at least 2 points");
  // x reverses direction (0 -> 2 -> 1): CGAL_precondition compare_x(*curr,*next) == cmp_x_res
  // fires (Arr_polyline_traits_2.h:574) -> CGAL::Precondition_exception, as ops.hpp documents.
  expect_cgal_failure([&] { (void)PLX({{0, 0}, {2, 2}, {1, 1}}); },
                      "make_x_monotone rejects a non-x-monotone chain (CGAL precondition)");

  // ---- Make_x_monotone_2 piece counts ----
  {
    std::vector<Geom> out;
    O().make_x_monotone(c, out);
    check_eq_sz(out.size(), 1, "make_x_monotone(^ polyline) -> 1 piece");
    check(out[0].type == GeomType::XCurve, "the piece is an XCurve");

    out.clear();
    // (0,0)->(2,2)->(1,3)->(3,1): x goes 0->2 (right), 2->1 (left), 1->3 (right) -> 3 pieces.
    O().make_x_monotone(PL({{0, 0}, {2, 2}, {1, 3}, {3, 1}}), out);
    check_eq_sz(out.size(), 3, "make_x_monotone(3-turn polyline) -> 3 pieces");

    out.clear();
    // A closed square [0,2]^2 traversed CCW: (0,0)->(2,0) [Compare_x SMALLER],
    // (2,0)->(2,2) [EQUAL, vertical], (2,2)->(0,2) [LARGER], (0,2)->(0,0) [EQUAL, vertical].
    // Make_x_monotone_2 starts a new piece whenever that comparison changes, and a vertical
    // subcurve compares EQUAL, so it can never be joined to a non-vertical neighbour:
    // 4 subcurves -> 4 x-monotone pieces.
    O().make_x_monotone(PL({{0, 0}, {2, 0}, {2, 2}, {0, 2}, {0, 0}}), out);
    check_eq_sz(out.size(), 4, "make_x_monotone(closed square polyline) -> 4 pieces");
    check_eq_sz(polyline::number_of_subcurves(out[0]), 1, "each square side is its own piece");
    check(O().xcurve_is_vertical(out[1]), "the second piece is the vertical side x = 2");

    // to_x_monotone refuses a genuinely non-x-monotone curve
    expect_error(ErrorCode::NotXMonotone,
                 [&] { (void)O().to_x_monotone(PL({{0, 0}, {2, 2}, {1, 3}})); },
                 "to_x_monotone of a 2-piece polyline");
    check(O().to_x_monotone(c).type == GeomType::XCurve, "to_x_monotone of an x-monotone curve");
  }

  // ---- box-type discipline (Curve_2 != X_monotone_curve_2 for this kind) ----
  expect_error(ErrorCode::NotXMonotone, [&] { (void)O().xcurve_source(c); },
               "a Curve box is refused where an XCurve is required");
  expect_error(ErrorCode::InvalidArgument, [&] { std::vector<double> o; O().approximate(P(0, 0), 1e-3, nullptr, o); },
               "approximate refuses a point box");
}

// ===========================================================================
// 3. x-monotone accessors, to_curve, opposite
// ===========================================================================
static void test_xcurve() {
  section("x-monotone accessors");

  const Geom xc = PLX({{0, 0}, {2, 2}, {4, 0}});
  check(point_is(O().xcurve_source(xc), 0, 0), "xcurve_source");
  check(point_is(O().xcurve_target(xc), 4, 0), "xcurve_target");
  check(O().xcurve_has_source(xc) && O().xcurve_has_target(xc), "a polyline always has both ends");
  check(point_is(O().xcurve_min_vertex(xc), 0, 0), "xcurve_min_vertex");
  check(point_is(O().xcurve_max_vertex(xc), 4, 0), "xcurve_max_vertex");
  check(!O().xcurve_is_vertical(xc), "the ^ polyline is not vertical");
  check(O().xcurve_is_directed_right(xc), "built left to right");
  check_eq_int(O().compare_endpoints_xy(xc), -1, "compare_endpoints_xy == SMALLER");

  // a vertical x-monotone polyline (all points on x = 1, monotone in y)
  const Geom vert = PLX({{1, 0}, {1, 1}, {1, 3}});
  check(O().xcurve_is_vertical(vert), "vertical polyline is vertical");
  check(point_is(O().xcurve_min_vertex(vert), 1, 0), "vertical min vertex");
  check(point_is(O().xcurve_max_vertex(vert), 1, 3), "vertical max vertex");

  // right-to-left construction: source is the RIGHTMOST point, min/max are unchanged.
  const Geom rl = PLX({{4, 0}, {2, 2}, {0, 0}});
  check(point_is(O().xcurve_source(rl), 4, 0), "right-to-left: source is (4,0)");
  check(point_is(O().xcurve_target(rl), 0, 0), "right-to-left: target is (0,0)");
  check(point_is(O().xcurve_min_vertex(rl), 0, 0), "right-to-left: min vertex is still (0,0)");
  check(!O().xcurve_is_directed_right(rl), "right-to-left is not directed right");
  check_eq_int(O().compare_endpoints_xy(rl), 1, "right-to-left compare_endpoints_xy == LARGER");
  check(point_is(polyline::point(rl, 0), 4, 0), "polyline::point(0) follows the stored direction");

  // Construct_opposite_2 reverses the subcurve order AND each subcurve.
  const Geom opp = O().construct_opposite(xc);
  check(point_is(O().xcurve_source(opp), 4, 0), "opposite source");
  check(point_is(O().xcurve_target(opp), 0, 0), "opposite target");
  check(O().curve_equal(xc, opp), "Equal_2 is direction insensitive");
  check_eq_str(O().curve_repr(opp), "Polyline([(4, 0), (2, 2), (0, 0)])", "repr of the opposite");

  // to_curve: XCurve -> Curve, same chain
  const Geom back = O().to_curve(xc);
  check(back.type == GeomType::Curve, "to_curve yields a Curve box");
  check_eq_str(O().curve_repr(back), O().curve_repr(xc), "to_curve keeps the point chain");
  check_eq_sz(polyline::number_of_subcurves(back), 2, "to_curve keeps the subcurves");
  check(O().to_curve(back).type == GeomType::Curve, "to_curve of a Curve box is the identity");

  // Construct_x_monotone_curve_2(p, q): a 2-point polyline
  const Geom pq = O().construct_x_monotone_curve(P(0, 0), P(3, 1));
  check_eq_sz(polyline::number_of_subcurves(pq), 1, "construct_x_monotone_curve(p,q): 1 subcurve");
  check(point_is(O().xcurve_source(pq), 0, 0), "construct_x_monotone_curve source");
  expect_error(ErrorCode::InvalidArgument, [&] { (void)O().construct_x_monotone_curve(P(1, 1), P(1, 1)); },
               "construct_x_monotone_curve rejects equal points");

  // parameter space of a bounded kind is always ARR_INTERIOR
  check_eq_int(O().parameter_space_in_x(xc, ARR_MIN_END), ARR_INTERIOR, "parameter_space_in_x(min)");
  check_eq_int(O().parameter_space_in_y(xc, ARR_MAX_END), ARR_INTERIOR, "parameter_space_in_y(max)");

  // generic traits functors still work through KindOpsBase
  check_eq_int(O().compare_y_at_x(P(1, 0), xc), -1, "(1,0) is below the ^ polyline (y=1 there)");
  check_eq_int(O().compare_y_at_x(P(1, 1), xc), 0, "(1,1) is on the ^ polyline");
  check(O().is_in_x_range(xc, P(2, 99)), "x = 2 is in the x-range");
  check(!O().is_in_x_range(xc, P(5, 0)), "x = 5 is not in the x-range");
  {
    Geom l, r;
    O().split(xc, P(1, 1), l, r);
    check(point_is(O().xcurve_target(l), 1, 1), "split: left part ends at (1,1)");
    check(point_is(O().xcurve_source(r), 1, 1), "split: right part starts at (1,1)");
    check_eq_sz(polyline::number_of_subcurves(l), 1, "split: left part has 1 subcurve");
    check_eq_sz(polyline::number_of_subcurves(r), 2, "split: right part has 2 subcurves");
    check(O().are_mergeable(l, r), "the two halves are mergeable");
    // Arr_polycurve_traits_2::Merge_2 concatenates the two chains AND asks the sub-traits'
    // Merge_2 to fuse the two touching subcurves; (0,0)-(1,1) and (1,1)-(2,2) are collinear, so
    // they fuse and the original 2-subcurve curve is restored exactly.
    const Geom merged = O().merge(l, r);
    check_eq_str(O().curve_repr(merged), "Polyline([(0, 0), (2, 2), (4, 0)])",
                 "merge restores the original curve (the collinear split subcurves re-fuse)");
    check(O().curve_equal(merged, xc), "the merged curve equals the original");
    expect_error(ErrorCode::InvalidArgument,
                 [&] { (void)O().merge(l, PLX({{10, 10}, {11, 11}})); },
                 "merge rejects non-mergeable curves");
  }
  {   // Trim_2 comes from the sub-traits through the polycurve traits
    const Geom t = O().trim(xc, P(1, 1), P(3, 1));
    check_eq_str(O().curve_repr(t), "Polyline([(1, 1), (2, 2), (3, 1)])", "trim (1,1) .. (3,1)");
  }
  {   // Intersect_2: the ^ and the v cross at (1,1) and (3,1)
    std::vector<IntersectionResult> res;
    O().intersect(xc, PLX({{0, 2}, {2, 0}, {4, 2}}), res);
    check_eq_sz(res.size(), 2, "^ and v intersect twice");
    if (res.size() == 2) {
      check(res[0].is_point && point_is(res[0].point, 1, 1), "first intersection (1,1)");
      check(res[1].is_point && point_is(res[1].point, 3, 1), "second intersection (3,1)");
      check_eq_sz(res[0].multiplicity, 1, "transversal multiplicity 1");
    }
  }
}

// ===========================================================================
// 4. approximate / bbox / length
// ===========================================================================
static void test_approximate() {
  section("approximate / bbox");

  const Geom xc = PLX({{0, 0}, {2, 2}, {4, 0}});
  std::vector<double> out;
  O().approximate(xc, 1e-3, nullptr, out);
  check_eq_sz(out.size(), 6, "approximate emits number_of_subcurves+1 = 3 points");
  check(out[0] == 0.0 && out[1] == 0.0, "approximation starts at the source (0,0)");
  check(out[4] == 4.0 && out[5] == 0.0, "approximation ends at the target (4,0)");
  check(out[2] == 2.0 && out[3] == 2.0, "the bend (2,2) is reproduced exactly");

  // the polyline IS its own exact geometry: every exact point of the curve is at distance 0.
  // Sample the midpoints of both subcurves: (1,1) and (3,1).
  check_close(dist_to_polyline(out, 1.0, 1.0), 0.0, 1e-15, "midpoint of subcurve 0 lies on the approximation");
  check_close(dist_to_polyline(out, 3.0, 1.0), 0.0, 1e-15, "midpoint of subcurve 1 lies on the approximation");
  check_close(dist_to_polyline(out, 0.5, 0.5), 0.0, 1e-15, "a quarter point lies on the approximation");

  // non-dyadic coordinates: the emitted doubles must be the CORRECTLY rounded ones.
  {
    std::vector<Geom> pts{Pr(R(0), R(0)), Pr(R(1, 3), R(7, 5)), Pr(R(2), R(0))};
    const Geom nd = polyline::make_x_monotone(pts);
    std::vector<double> o2;
    O().approximate(nd, 1e-6, nullptr, o2);
    check_eq_sz(o2.size(), 6, "3 points for the non-dyadic polyline");
    check(o2[2] == 1.0 / 3.0, "x = 1/3 is correctly rounded");
    check(o2[3] == 7.0 / 5.0, "y = 7/5 is correctly rounded");

    // cross-check against CGAL's own Approximate_2 (same points, same order; it uses
    // CGAL::to_double, so allow one ulp of difference).
    std::vector<PolylineTypes::Traits::Approximate_point_2> cg;
    auto approx = PolylineTypes::traits().approximate_2_object();
    approx(nd.as<PolylineTypes::X_monotone_curve_2>(), 1e-6, std::back_inserter(cg), true);
    check_eq_sz(cg.size(), 3, "CGAL Approximate_2 emits the same number of points");
    bool same = (cg.size() * 2 == o2.size());
    for (std::size_t i = 0; same && i < cg.size(); ++i)
      same = std::fabs(cg[i].x() - o2[2 * i]) <= 1e-15 && std::fabs(cg[i].y() - o2[2 * i + 1]) <= 1e-15;
    check(same, "our approximation agrees with CGAL's Approximate_2 to within 1e-15");
  }

  // general (non-x-monotone) curves are approximated too — CGAL's functor cannot do this
  {
    const Geom gen = PL({{0, 0}, {2, 2}, {1, 3}});
    std::vector<double> o3;
    O().approximate(gen, 1e-3, nullptr, o3);
    check_eq_sz(o3.size(), 6, "a general polyline is approximated by its own vertices");
    check(o3[4] == 1.0 && o3[5] == 3.0, "general polyline: last vertex (1,3)");
  }

  // tolerance validation (rendering_and_approximation.md gotcha 7)
  expect_error(ErrorCode::InvalidArgument, [&] { std::vector<double> o; O().approximate(xc, 0.0, nullptr, o); },
               "approximate rejects tolerance 0");
  expect_error(ErrorCode::InvalidArgument, [&] { std::vector<double> o; O().approximate(xc, -1.0, nullptr, o); },
               "approximate rejects a negative tolerance");
  expect_error(ErrorCode::InvalidArgument,
               [&] { std::vector<double> o; O().approximate(xc, std::nan(""), nullptr, o); },
               "approximate rejects NaN");

  // length: 2 * sqrt(8) = 5.656854249492380
  check_close(O().approximate_length(xc, 1e-6), 2.0 * std::sqrt(8.0), 1e-12, "approximate_length of ^");

  // bbox
  const BBox b = O().curve_bbox(xc);
  check_eq_int(b.dim, 2, "bbox dimension");
  check_close(b.lo[0], 0.0, 0.0, "bbox xmin");
  check_close(b.lo[1], 0.0, 0.0, "bbox ymin");
  check_close(b.hi[0], 4.0, 0.0, "bbox xmax");
  check_close(b.hi[1], 2.0, 0.0, "bbox ymax");
  const BBox b2 = O().curve_bbox(PL({{-3, -1}, {0, 5}}));
  check(b2.lo[0] <= -3.0 && b2.hi[0] >= 0.0 && b2.lo[1] <= -1.0 && b2.hi[1] >= 5.0,
        "bbox of a general curve encloses its points");
  check(O().curve_is_bounded(xc) && O().curve_is_bounded(PL({{0, 0}, {1, 1}})),
        "every polyline is bounded");

  // REGRESSION (ops.hpp contract "approximate_coordinate agrees with point_approx"):
  // KindOpsBase used to forward to the traits' Approximate_2, which is
  // CGAL::to_double(Epeck::FT) and is NOT correctly rounded — for x = 1/3 it returns
  // 0.33333333333333337 where the nearest double is 0.33333333333333331.  It now delegates to
  // point_approx() for every kind, so the two can never disagree again.
  check_close(O().approximate_coordinate(P(3, 7), 0), 3.0, 0.0, "approximate_coordinate(x)");
  check_close(O().approximate_coordinate(P(3, 7), 1), 7.0, 0.0, "approximate_coordinate(y)");
  {
    const Geom third = Pr(R(1, 3), R(2, 7));
    const double ax = O().approximate_coordinate(third, 0);
    const double ay = O().approximate_coordinate(third, 1);
    const std::pair<double, double> pa = xy(third);
    std::printf("    approximate_coordinate(1/3) = %.17g ; point_approx(1/3) = %.17g\n", ax, pa.first);
    check(pa.first == 1.0 / 3.0, "point_approx(1/3) is the correctly rounded double");
    check(ax == pa.first, "approximate_coordinate(x) == point_approx(x) (correctly rounded)");
    check(ay == pa.second, "approximate_coordinate(y) == point_approx(y) (correctly rounded)");
  }

  // REGRESSION (ops.hpp contract "every output vector is CLEARED first"): make_x_monotone() and
  // intersect() used to APPEND, so a reused vector silently accumulated results.
  {
    const Geom zig = PL({{0, 0}, {2, 2}, {1, 0}});     // 2 x-monotone pieces
    std::vector<Geom> out;
    O().make_x_monotone(zig, out);
    const std::size_t n1 = out.size();
    O().make_x_monotone(zig, out);                     // same vector, second call
    check_eq_sz(out.size(), n1, "make_x_monotone clears its output vector");

    const Geom a = PL({{0, 0}, {4, 4}});
    const Geom b = PL({{0, 4}, {4, 0}});
    std::vector<IntersectionResult> ir;
    O().intersect(O().to_x_monotone(a), O().to_x_monotone(b), ir);
    const std::size_t m1 = ir.size();
    check_eq_sz(m1, 1, "the two diagonals cross once");
    O().intersect(O().to_x_monotone(a), O().to_x_monotone(b), ir);
    check_eq_sz(ir.size(), m1, "intersect clears its output vector");
  }
  expect_error(ErrorCode::InvalidArgument, [&] { (void)O().approximate_coordinate(P(3, 7), 2); },
               "approximate_coordinate rejects index 2 for a planar kind");
}

// ===========================================================================
// 5. convert_curve
// ===========================================================================
static void test_convert_curve() {
  section("convert_curve");

  // --- from the segment kind (Arr_segment_2<Epeck> == our own subcurve type) ---
  {
    std::vector<Geom> out;
    O().convert_curve(segment_geom(1, 2, 5, 4), out);
    check_eq_sz(out.size(), 1, "a segment becomes one polyline");
    check(out[0].kind == Kind::Polyline && out[0].type == GeomType::XCurve,
          "converted segment is an x-monotone polyline box");
    check_eq_sz(polyline::number_of_subcurves(out[0]), 1, "one subcurve");
    check_eq_str(O().curve_repr(out[0]), "Polyline([(1, 2), (5, 4)])", "converted segment repr");
    check(point_is(O().xcurve_source(out[0]), 1, 2), "converted segment keeps its source");
  }
  {   // a Curve box of the segment kind (Curve_2 == X_monotone_curve_2 there)
    std::vector<Geom> out;
    O().convert_curve(segment_geom(5, 4, 1, 2, GeomType::Curve), out);
    check_eq_sz(out.size(), 1, "segment Curve box converts");
    check(point_is(O().xcurve_source(out[0]), 5, 4), "direction is preserved (source (5,4))");
    check(!O().xcurve_is_directed_right(out[0]), "the converted curve is right-to-left");
  }

  // --- from the linear kind ---
  {
    std::vector<Geom> out;
    O().convert_curve(linear_segment_geom(0, 0, 2, 6), out);
    check_eq_sz(out.size(), 1, "a linear segment becomes one polyline");
    check_eq_str(O().curve_repr(out[0]), "Polyline([(0, 0), (2, 6)])", "linear segment repr");
  }
  expect_error(ErrorCode::NotRepresentable,
               [&] { std::vector<Geom> out; O().convert_curve(linear_ray_geom(0, 0, 1, 1), out); },
               "a ray is unbounded and cannot become a polyline");
  expect_error(ErrorCode::NotRepresentable,
               [&] { std::vector<Geom> out; O().convert_curve(linear_line_geom(0, 0, 1, 1), out); },
               "a line is unbounded and cannot become a polyline");

  // --- identity ---
  {
    std::vector<Geom> out;
    const Geom c = PL({{0, 0}, {1, 1}});
    O().convert_curve(c, out);
    check_eq_sz(out.size(), 1, "polyline -> polyline is the identity");
    check(out[0].ptr == c.ptr, "identity conversion shares the boxed object");
  }

  // --- rejected inputs ---
  expect_error(ErrorCode::InvalidArgument,
               [&] { std::vector<Geom> out; O().convert_curve(P(0, 0), out); },
               "convert_curve rejects a point box");
  // Any other kind falls through to the generic (registry-based) route.  In this per-kind test
  // binary no other kind TU is linked, so the registry answers Unsupported ("kind not
  // available"); in a full build the box's payload type would be rejected instead
  // (KindMismatch).  Either outcome is correct here.
  expect_error2(ErrorCode::Unsupported, ErrorCode::KindMismatch,
                [&] {
                  std::vector<Geom> out;
                  O().convert_curve(make_geom(Kind::Bezier, GeomType::Curve, 42), out);
                },
                "convert_curve from an unavailable kind");
}

// ===========================================================================
// 6. arrangement round trip
// ===========================================================================
namespace {

/// Prints and returns the (V, E, F) triple.
void dump(const ArrBase& a, const char* tag) {
  std::printf("    %-22s V=%zu H=%zu E=%zu F=%zu UF=%zu curves=%zu isolated=%zu valid=%d\n", tag,
              a.number_of_vertices(), a.number_of_halfedges(), a.number_of_edges(),
              a.number_of_faces(), a.number_of_unbounded_faces(), a.number_of_curves(),
              a.number_of_isolated_vertices(), int(a.is_valid()));
}

}  // namespace

static void test_arrangement() {
  section("arrangement");

  auto arr = make_arrangement(Kind::Polyline);
  check(arr != nullptr, "make_arrangement(Kind::Polyline)");
  check(arr->kind() == Kind::Polyline, "arrangement kind");
  check(!arr->is_unbounded_kind(), "polyline arrangements use the bounded planar topology");
  check(arr->is_empty(), "a fresh arrangement is empty");
  check_eq_sz(arr->number_of_faces(), 1, "a fresh arrangement has 1 (unbounded) face");
  expect_error(ErrorCode::Unsupported, [&] { (void)arr->fictitious_face(); },
               "bounded kinds have no fictitious face");

  // ---- insert two crossing polylines ----
  //  c1 = (0,0)-(2,2)-(4,0)   ("^")     c2 = (0,2)-(2,0)-(4,2)   ("v")
  //  y=x meets y=2-x at (1,1);  y=4-x meets y=x-2 at (3,1).  No other crossing (the two
  //  half-chains live in disjoint x-ranges [0,2] and [2,4] and disagree at x=2).
  //  => V = 4 endpoints + 2 crossings = 6 ; each curve is cut into 3 edges => E = 6 ;
  //     the two middle edges bound one closed region => F = 1 bounded + 1 unbounded = 2.
  //     Euler: 6 - 6 + 2 = 2. OK
  const Geom c1 = PL({{0, 0}, {2, 2}, {4, 0}});
  const Geom c2 = PL({{0, 2}, {2, 0}, {4, 2}});
  std::vector<CH> handles;
  arr->insert_curves({c1, c2}, handles);
  dump(*arr, "after c1 + c2");
  check_eq_sz(handles.size(), 2, "insert_curves returns 2 curve handles");
  check_eq_sz(arr->number_of_vertices(), 6, "V after two crossing polylines");
  check_eq_sz(arr->number_of_edges(), 6, "E after two crossing polylines");
  check_eq_sz(arr->number_of_halfedges(), 12, "H = 2E");
  check_eq_sz(arr->number_of_faces(), 2, "F after two crossing polylines");
  check_eq_sz(arr->number_of_unbounded_faces(), 1, "one unbounded face");
  check_eq_sz(arr->number_of_curves(), 2, "history holds 2 input curves");
  check(arr->is_valid(), "is_valid after the first two inserts");
  check_eq_sz(arr->number_of_induced_edges(handles[0]), 3, "c1 induces 3 edges");
  check_eq_sz(arr->number_of_induced_edges(handles[1]), 3, "c2 induces 3 edges");
  check_eq_str(O().curve_repr(arr->curve_geometry(handles[0])), O().curve_repr(c1),
               "curve_geometry round-trips c1");

  // ---- iteration ----
  std::vector<VH> vs;
  std::vector<HH> hs, es;
  std::vector<FH> fs;
  std::vector<CH> cs;
  arr->vertices(vs);
  arr->halfedges(hs);
  arr->edges(es);
  arr->faces(fs);
  arr->curves(cs);
  check_eq_sz(vs.size(), 6, "vertices() snapshot size");
  check_eq_sz(hs.size(), 12, "halfedges() snapshot size");
  check_eq_sz(es.size(), 6, "edges() snapshot size");
  check_eq_sz(fs.size(), 2, "faces() snapshot size");
  check_eq_sz(cs.size(), 2, "curves() snapshot size");
  {
    std::size_t deg_sum = 0;
    for (const VH& v : vs) deg_sum += arr->vertex_degree(v);
    check_eq_sz(deg_sum, 12, "sum of vertex degrees == 2E");
    bool found_crossing = false;
    for (const VH& v : vs)
      if (point_is(arr->vertex_point(v), 1, 1)) found_crossing = true;
    check(found_crossing, "(1,1) is an arrangement vertex");
    bool found_bend = false;
    for (const VH& v : vs)
      if (point_is(arr->vertex_point(v), 2, 2)) found_bend = true;
    check(!found_bend, "the polyline bend (2,2) is NOT an arrangement vertex (it is inside an edge)");
  }

  // ---- point location: all supported strategies must agree ----
  const Geom inside = P(2, 1);      // strictly inside the lens between the two middle edges
  const Geom outside = P(10, 10);   // in the unbounded face
  const int strategies[] = {PL_NAIVE, PL_SIMPLE, PL_WALK, PL_LANDMARKS, PL_TRAPEZOID};
  Located ref = arr->locate(inside, PL_NAIVE);
  check_eq_int(ref.type, 2, "locate((2,1)) is a face");
  for (int s : strategies) {
    check(arr->supports_point_location(s),
          std::string("polyline supports point location '") + point_location_name(s) + "'");
    Located l = arr->locate(inside, s);
    check(l.type == 2 && l.p == ref.p,
          std::string("locate((2,1)) with '") + point_location_name(s) + "' agrees with naive");
    arr->attach_point_location(s);
    check(arr->has_point_location(s), std::string("attach '") + point_location_name(s) + "'");
    Located l2 = arr->locate(inside, s);
    check(l2.p == ref.p, std::string("attached '") + point_location_name(s) + "' agrees");
  }
  check(!arr->face_is_unbounded(ref.as_face()), "(2,1) is in a bounded face");
  check(arr->face_is_unbounded(arr->locate(outside).as_face()), "(10,10) is in the unbounded face");
  check_eq_int(arr->locate(P(0, 0)).type, 0, "locate((0,0)) is a vertex");
  check_eq_int(arr->locate(Pr(R(1, 2), R(1, 2))).type, 1, "locate((1/2,1/2)) is a halfedge (on c1)");
  for (int s : strategies) arr->detach_point_location(s);

  // triangulation point location is impossible for this kind (KindPolicy<PolylineTypes>:
  // Arr_polyline_traits_2 has no `Kernel` typedef, so Arr_triangulation_point_location does not
  // even compile) — everything about it must report Unsupported.
  check(!arr->supports_point_location(PL_TRIANGULATION), "triangulation PL is unsupported");
  expect_error(ErrorCode::Unsupported, [&] { arr->attach_point_location(PL_TRIANGULATION); },
               "attach(triangulation) raises Unsupported");
  expect_error(ErrorCode::Unsupported, [&] { (void)arr->locate(inside, PL_TRIANGULATION); },
               "locate(triangulation) raises Unsupported");

  // ---- vertical ray shooting ----
  //  from (5/2, -1) upwards: the first curve above is c2's middle edge, y = x - 2 -> y = 1/2.
  {
    Located up = arr->ray_shoot_up(Pr(R(5, 2), R(-1)));
    check_eq_int(up.type, 1, "ray_shoot_up from (5/2,-1) hits an edge");
    Located up_walk = arr->ray_shoot_up(Pr(R(5, 2), R(-1)), PL_WALK);
    Located up_trap = arr->ray_shoot_up(Pr(R(5, 2), R(-1)), PL_TRAPEZOID);
    check(up_walk.type == 1 && up_trap.type == 1, "walk and trapezoid ray shooting agree in type");
    Located down = arr->ray_shoot_down(Pr(R(2), R(3)));
    check_eq_int(down.type, 1, "ray_shoot_down from (2,3) hits c1's middle edge");
    expect_error(ErrorCode::Unsupported, [&] { (void)arr->ray_shoot_up(P(0, -1), PL_NAIVE); },
                 "naive point location cannot ray shoot");
  }

  // ---- face_polygon / he_curve / he_directed_curve ----
  {
    const FH bounded = ref.as_face();
    std::vector<Geom> outer;
    std::vector<std::vector<Geom>> holes;
    arr->face_polygon(bounded, outer, holes);
    check_eq_sz(outer.size(), 2, "the lens is bounded by 2 directed x-monotone polylines");
    check_eq_sz(holes.size(), 0, "the lens has no holes");
    // the directed curves must chain: target(i) == source(i+1), cyclically
    bool chained = true;
    for (std::size_t i = 0; i < outer.size(); ++i) {
      const Geom t = O().xcurve_target(outer[i]);
      const Geom s = O().xcurve_source(outer[(i + 1) % outer.size()]);
      if (!O().point_equal(t, s)) chained = false;
    }
    check(chained, "face_polygon curves chain target -> source around the face");

    std::vector<HH> ccb;
    arr->he_ccb(arr->face_outer_ccb(bounded), ccb);
    check_eq_sz(ccb.size(), 2, "the lens outer ccb has 2 halfedges");
    bool oriented = true, curves_match = true;
    for (const HH& h : ccb) {
      const Geom dc = arr->he_directed_curve(h);
      const Geom sp = arr->vertex_point(arr->he_source(h));
      const Geom tp = arr->vertex_point(arr->he_target(h));
      if (!O().point_equal(O().xcurve_source(dc), sp)) oriented = false;
      if (!O().point_equal(O().xcurve_target(dc), tp)) oriented = false;
      // he_curve() is the curve AS STORED: same geometry, possibly the opposite direction
      const Geom sc = arr->he_curve(h);
      if (!O().curve_equal(sc, dc)) curves_match = false;
      if (arr->he_face(h).p != bounded.p) oriented = false;
      if (arr->he_target(h).p != arr->he_source(arr->he_next(h)).p) oriented = false;
    }
    check(oriented, "he_directed_curve runs source -> target and the face is on its left");
    check(curves_match, "he_curve and he_directed_curve describe the same curve");
    // the two boundary curves are the middle pieces (1,1)-(2,2)-(3,1) and (1,1)-(2,0)-(3,1)
    check_eq_sz(polyline::number_of_subcurves(outer[0]), 2, "each lens boundary curve has 2 subcurves");
  }

  // ---- zone / do_intersect ----
  {
    const Geom probe = PLX({{-1, 1}, {5, 1}});   // horizontal line y = 1 through both crossings
    std::vector<Located> zone;
    arr->zone(probe, zone);
    dump(*arr, "before zone (unmodified)");
    // unbounded face, vertex (1,1), the lens, vertex (3,1), unbounded face  => 5 features
    check_eq_sz(zone.size(), 5, "zone of y = 1 crosses 5 features");
    if (zone.size() == 5) {
      check(zone[0].type == 2 && zone[4].type == 2, "zone starts and ends in a face");
      check(zone[1].type == 0 && zone[3].type == 0, "zone passes through the two crossing vertices");
      check(zone[2].type == 2, "zone passes through the lens face");
    }
    check_eq_sz(arr->number_of_edges(), 6, "zone() does not modify the arrangement");
    check(arr->do_intersect(probe), "do_intersect(y = 1) is true");
    check(!arr->do_intersect(PLX({{10, 10}, {12, 12}})), "do_intersect of a far curve is false");
    // a general (non-x-monotone) curve is split by ops().make_x_monotone inside zone()
    std::vector<Located> zone2;
    arr->zone(PL({{-1, 1}, {5, 1}, {5, 5}, {-1, 5}}), zone2);
    check(zone2.size() >= 5, "zone of a general 3-turn polyline visits at least 5 features");
  }

  // ---- batched_locate (input order restored, duplicates handled) ----
  {
    std::vector<Geom> qs{inside, P(0, 0), outside, inside, Pr(R(1, 2), R(1, 2))};
    std::vector<Located> res;
    arr->batched_locate(qs, res);
    check_eq_sz(res.size(), 5, "batched_locate returns one result per query");
    check(res[0].type == 2 && res[0].p == ref.p, "batched: (2,1) -> the lens");
    check_eq_int(res[1].type, 0, "batched: (0,0) -> a vertex");
    check(res[2].type == 2 && arr->face_is_unbounded(res[2].as_face()), "batched: (10,10) -> unbounded");
    check(res[3].p == ref.p, "batched: the duplicate query gets the same answer");
    check_eq_int(res[4].type, 1, "batched: (1/2,1/2) -> a halfedge");
  }

  // ---- decompose ----
  {
    std::vector<VerticalDecompositionEntry> dec;
    arr->decompose(dec);
    check_eq_sz(dec.size(), 6, "decompose reports one entry per vertex");
    // the leftmost vertices are (0,0) and (0,2); below (0,0) there is nothing but the unbounded face
    bool ok = true;
    for (const auto& d : dec) ok = ok && arr->vertex_valid(d.v);
    check(ok, "every decompose entry names a live vertex");
  }

  // ---- bulk export ----
  {
    std::vector<double> coords;
    arr->vertex_coordinates(coords);
    check_eq_sz(coords.size(), 12, "vertex_coordinates: 2 doubles per vertex");
    std::vector<std::size_t> idx;
    arr->edge_vertex_indices(idx);
    check_eq_sz(idx.size(), 12, "edge_vertex_indices: 2 per edge");
    std::vector<std::vector<std::vector<std::size_t>>> fb;
    arr->face_boundaries(fb);
    check_eq_sz(fb.size(), 2, "face_boundaries: one entry per face");
    const BBox b = arr->bbox();
    check_close(b.lo[0], 0.0, 0.0, "arrangement bbox xmin");
    check_close(b.hi[0], 4.0, 0.0, "arrangement bbox xmax");
    check_close(b.lo[1], 0.0, 0.0, "arrangement bbox ymin");
    check_close(b.hi[1], 2.0, 0.0, "arrangement bbox ymax");
  }

  // ---- clone ----
  {
    auto cp = arr->clone();
    check_eq_sz(cp->number_of_vertices(), 6, "clone V");
    check_eq_sz(cp->number_of_edges(), 6, "clone E");
    check_eq_sz(cp->number_of_faces(), 2, "clone F");
    check_eq_sz(cp->number_of_curves(), 2, "clone keeps the history");
    check(cp->is_valid(), "clone is valid");
    // handles of the original are foreign to the clone
    check(!cp->vertex_valid(vs[0]), "an original vertex handle is invalid in the clone");
    check(arr->vertex_valid(vs[0]), "the original handle is still valid in the original");
  }

  // ---- a third curve: a 1-subcurve polyline through both crossing vertices ----
  //  c3 = (0,1)-(4,1).  At y = 1 the existing curves are met exactly at the vertices (1,1) and
  //  (3,1), so c3 becomes 3 edges and adds the 2 new endpoints (0,1) and (4,1):
  //     V = 6 + 2 = 8, E = 6 + 3 = 9, and the lens is split in two => F = 3.
  //     Euler: 8 - 9 + 3 = 2. OK
  const Geom c3 = PL({{0, 1}, {4, 1}});
  const CH h3 = arr->insert_curve(c3);
  dump(*arr, "after c3");
  check_eq_sz(arr->number_of_vertices(), 8, "V after c3");
  check_eq_sz(arr->number_of_edges(), 9, "E after c3");
  check_eq_sz(arr->number_of_faces(), 3, "F after c3 (the lens is split)");
  check_eq_sz(arr->number_of_curves(), 3, "3 input curves");
  check(arr->is_valid(), "is_valid after c3");
  check_eq_sz(arr->number_of_induced_edges(h3), 3, "c3 induces 3 edges");
  check(!arr->face_is_unbounded(arr->locate(Pr(R(2), R(3, 2))).as_face()), "(2,3/2) is in the upper half");
  check(!arr->face_is_unbounded(arr->locate(Pr(R(2), R(1, 2))).as_face()), "(2,1/2) is in the lower half");
  check(arr->locate(Pr(R(2), R(3, 2))).p != arr->locate(Pr(R(2), R(1, 2))).p,
        "the two halves of the lens are different faces");
  {
    std::vector<HH> induced;
    arr->induced_edges(h3, induced);
    check_eq_sz(induced.size(), 3, "induced_edges(c3) size");
    std::vector<CH> orig;
    arr->originating_curves(induced[0], orig);
    check_eq_sz(orig.size(), 1, "an edge of c3 has one originating curve");
    check(orig[0].p == h3.p, "the originating curve is c3");
  }

  // ---- remove_curve ----
  const std::size_t removed = arr->remove_curve(h3);
  dump(*arr, "after remove_curve(c3)");
  check_eq_sz(removed, 3, "remove_curve(c3) removes its 3 edges");
  check_eq_sz(arr->number_of_edges(), 6, "E is back to 6");
  check_eq_sz(arr->number_of_faces(), 2, "F is back to 2");
  check_eq_sz(arr->number_of_curves(), 2, "2 input curves remain");
  check(arr->is_valid(), "is_valid after remove_curve");
  check(!arr->curve_valid(h3), "the removed curve handle is stale");
  expect_error(ErrorCode::InvalidHandle, [&] { (void)arr->number_of_induced_edges(h3); },
               "using a stale curve handle raises InvalidHandle");

  // ---- clear ----
  arr->clear();
  check(arr->is_empty(), "clear() empties the arrangement");
  check_eq_sz(arr->number_of_curves(), 0, "clear() drops the history");
  check(!arr->vertex_valid(vs[0]), "clear() invalidates old handles");

  // ---- an XCurve box given to insert_curve: ArrImpl routes it through KindOps::to_curve(),
  //      which for this kind really has to convert (Curve_2 != X_monotone_curve_2). ----
  {
    const CH hx = arr->insert_curve(PLX({{0, 0}, {2, 2}, {4, 0}}));
    check_eq_sz(arr->number_of_curves(), 1, "insert_curve(XCurve box) records history");
    check_eq_sz(arr->number_of_edges(), 1, "an x-monotone polyline is a single edge");
    check_eq_sz(arr->number_of_vertices(), 2, "... with only its two endpoints as vertices");
    check_eq_sz(arr->number_of_faces(), 1, "... and no new face");
    check_eq_sz(arr->number_of_induced_edges(hx), 1, "it induces exactly 1 edge");
    check(arr->is_valid(), "is_valid after inserting an x-monotone box");

    // insert_non_intersecting_curve: a disjoint chain, recorded WITHOUT history
    const HH nh = arr->insert_non_intersecting_curve(PLX({{5, 0}, {6, 1}, {7, 0}}));
    check_eq_sz(arr->number_of_edges(), 2, "insert_non_intersecting_curve adds an edge");
    check_eq_sz(arr->number_of_curves(), 1, "insert_non_intersecting_curve records no history");
    check_eq_sz(arr->number_of_originating_curves(nh), 0, "the new edge has no originating curve");
    {
      const Geom dc = arr->he_directed_curve(nh);
      check(O().point_equal(O().xcurve_source(dc), as_poly_point(arr->vertex_point(arr->he_source(nh)))),
            "he_directed_curve starts at the halfedge's source");
      check(O().point_equal(O().xcurve_target(dc), as_poly_point(arr->vertex_point(arr->he_target(nh)))),
            "he_directed_curve ends at the halfedge's target");
      const Geom tw = arr->he_directed_curve(arr->he_twin(nh));
      check(O().curve_equal(dc, tw), "a halfedge and its twin carry the same curve");
      check(O().compare_endpoints_xy(dc) == -O().compare_endpoints_xy(tw),
            "... with opposite directions");
      check(arr->he_direction(nh) != arr->he_direction(arr->he_twin(nh)),
            "a halfedge and its twin have opposite directions");
    }

    // assign()
    auto other = make_arrangement(Kind::Polyline);
    other->assign(*arr);
    check_eq_sz(other->number_of_edges(), 2, "assign() copies the DCEL");
    check_eq_sz(other->number_of_curves(), 1, "assign() copies the history");
    check(other->is_valid(), "the assigned arrangement is valid");
  }
}

// ===========================================================================
int main() {
  CGAL::set_error_handler(silent_cgal_handler);
  CGAL::set_warning_handler(silent_cgal_handler);

  std::printf("arr2d polyline kind test — %s\n", build_info().c_str());
  init_all_kinds();
  check(kind_available(Kind::Polyline), "the polyline kind registered itself");

  try {
    test_points();
    test_constructors();
    test_xcurve();
    test_approximate();
    test_convert_curve();
    test_arrangement();
  } catch (const std::exception& e) {
    ++g_failures;
    std::printf("FATAL [%s] uncaught exception: %s\n", g_section.c_str(), e.what());
  }

  std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
