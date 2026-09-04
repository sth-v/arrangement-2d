// ===========================================================================
// arr2d — C++ test for Kind::CircleSegment (src/arr2d/src/kind_circle_segment.cpp).
//
// Self-contained: it links registry.o numbers.o overlay.o kind_circle_segment.o.  The kind's
// registrar hands the registry &arr2d::make_polygon_set_circle_segment (declared in
// arr2d/bso.hpp), so the symbol has to exist at link time even though the test never calls it.
// Two ways to satisfy that:
//
//   A) WITHOUT the Boolean-set-operations TU  (define the stub below):
//      REPO=/Users/sthv/PycharmProjects/arrangement-2d
//      SCR=/private/tmp/claude-501/-Users-sthv-PycharmProjects-arrangement-2d/caeba100-f0a3-4bc9-8340-691c4b0ddc3d/scratchpad/circle_segment
//      /usr/bin/clang++ -std=c++17 -O0 -g -c -DCGAL_USE_CORE -DCGAL_USE_GMP -DCGAL_USE_MPFR \
//          -I/opt/homebrew/include -I$REPO/src/arr2d/include $REPO/src/arr2d/src/registry.cpp -o $SCR/registry.o
//      ... likewise numbers.cpp, overlay.cpp, kind_circle_segment.cpp ...
//      /usr/bin/clang++ -std=c++17 -O0 -g -c -DARR2D_TEST_STUB_BSO -DCGAL_USE_CORE -DCGAL_USE_GMP \
//          -DCGAL_USE_MPFR -I/opt/homebrew/include -I$REPO/src/arr2d/include \
//          $REPO/src/arr2d/tests/test_kind_circle_segment.cpp -o $SCR/test_kind_circle_segment.o
//      /usr/bin/clang++ -std=c++17 -O0 -g -o $SCR/test_kind_circle_segment \
//          $SCR/test_kind_circle_segment.o $SCR/kind_circle_segment.o $SCR/registry.o \
//          $SCR/numbers.o $SCR/overlay.o -L/opt/homebrew/lib -lgmp -lmpfr
//      $SCR/test_kind_circle_segment            # must print "0 failures" and exit 0
//
//   B) WITH the real Boolean-set-operations TU (src/arr2d/src/bso_circle_segment.cpp): compile
//      the test WITHOUT -DARR2D_TEST_STUB_BSO and add bso_circle_segment.o to the link line.
//      That build additionally checks that make_polygon_set(Kind::CircleSegment) returns a
//      usable, empty polygon set.
//
// Every expected number in this file is hand-derived; the derivation is in the comment next to
// the check.  The program must exit normally (return 0) — that also proves nothing in this kind
// aborts during static destruction.
// ===========================================================================

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "arr2d/arrangement.hpp"
#include "arr2d/common.hpp"
#include "arr2d/numbers.hpp"
#include "arr2d/ops.hpp"
#include "arr2d/polygon_set.hpp"
#include "arr2d/registry.hpp"

// Foreign kinds are boxed directly from their raw CGAL types (header-only) so that convert_point
// / convert_curve can be tested without linking those kinds' TUs.
#include "arr2d/kinds/linear_types.hpp"
#include "arr2d/kinds/polyline_types.hpp"
#include "arr2d/kinds/segment_types.hpp"

#ifdef ARR2D_TEST_STUB_BSO
// Command line A: the BSO TU is not linked.  The registrar only stores the address of this
// function; it is never called by the test.
namespace arr2d {
std::unique_ptr<PolygonSetBase> make_polygon_set_circle_segment() { return nullptr; }
}  // namespace arr2d
#endif

using namespace arr2d;

// ---------------------------------------------------------------------------
// tiny check framework
// ---------------------------------------------------------------------------
static int g_checks = 0;
static int g_failures = 0;
static const char* g_section = "";

#define CHECK(cond)                                                            \
  do {                                                                         \
    ++g_checks;                                                                \
    if (!(cond)) {                                                             \
      ++g_failures;                                                            \
      std::cout << "FAIL [" << g_section << "] line " << __LINE__ << ": "      \
                << #cond << "\n";                                              \
    }                                                                          \
  } while (0)

#define CHECK_EQ(a, b)                                                         \
  do {                                                                         \
    ++g_checks;                                                                \
    auto va_ = (a);                                                            \
    auto vb_ = (b);                                                            \
    if (!(va_ == vb_)) {                                                       \
      ++g_failures;                                                            \
      std::cout << "FAIL [" << g_section << "] line " << __LINE__ << ": "      \
                << #a << " == " << #b << "  (" << va_ << " vs " << vb_ << ")\n"; \
    }                                                                          \
  } while (0)

#define CHECK_STR(a, b)                                                        \
  do {                                                                         \
    ++g_checks;                                                                \
    std::string va_ = (a);                                                     \
    std::string vb_ = (b);                                                     \
    if (va_ != vb_) {                                                          \
      ++g_failures;                                                            \
      std::cout << "FAIL [" << g_section << "] line " << __LINE__ << ": "      \
                << #a << "\n     got: \"" << va_ << "\"\n     want: \"" << vb_ << "\"\n"; \
    }                                                                          \
  } while (0)

static bool close_to(double a, double b, double eps) { return std::fabs(a - b) <= eps; }

#define CHECK_CLOSE(a, b, eps)                                                 \
  do {                                                                         \
    ++g_checks;                                                                \
    double va_ = (a);                                                          \
    double vb_ = (b);                                                          \
    if (!close_to(va_, vb_, (eps))) {                                          \
      ++g_failures;                                                            \
      std::cout << "FAIL [" << g_section << "] line " << __LINE__ << ": "      \
                << #a << " ~= " << #b << "  (" << va_ << " vs " << vb_         \
                << ", |d| = " << std::fabs(va_ - vb_) << ")\n";                \
    }                                                                          \
  } while (0)

template <class F>
static bool throws_error(F fn, ErrorCode expected) {
  try {
    fn();
  } catch (const Error& e) {
    if (e.code == expected) return true;
    std::cout << "     (wrong ErrorCode " << int(e.code) << ", wanted " << int(expected)
              << "; message: " << e.what() << ")\n";
    return false;
  } catch (const std::exception& e) {
    std::cout << "     (non-arr2d exception: " << e.what() << ")\n";
    return false;
  }
  return false;
}

#define CHECK_THROWS(expr, code)                                               \
  do {                                                                         \
    ++g_checks;                                                                \
    if (!throws_error([&] { expr; }, code)) {                                  \
      ++g_failures;                                                            \
      std::cout << "FAIL [" << g_section << "] line " << __LINE__              \
                << ": expected Error(" << #code << ") from " << #expr << "\n"; \
    }                                                                          \
  } while (0)

// ---------------------------------------------------------------------------
// small helpers
// ---------------------------------------------------------------------------
static Rational R(long n, long d = 1) { return Rational(n) / Rational(d); }

static const KindOps& OPS() { return ops(Kind::CircleSegment); }

static Geom P(long x, long y) { return OPS().make_point(R(x), R(y)); }

static void approx_xy(const Geom& p, double& x, double& y) {
  double xyz[3] = {0, 0, 0};
  OPS().point_approx(p, xyz);
  x = xyz[0];
  y = xyz[1];
}

static bool points_equal(const Geom& a, const Geom& b) { return OPS().point_equal(a, b); }

/// distance from the origin of the i-th point of a flattened polyline
static double radius_at(const std::vector<double>& poly, std::size_t i) {
  return std::sqrt(poly[2 * i] * poly[2 * i] + poly[2 * i + 1] * poly[2 * i + 1]);
}

// ===========================================================================
// 1. registry / capabilities
// ===========================================================================
static void test_registry() {
  g_section = "registry";
  CHECK(kind_available(Kind::CircleSegment));
  const KindOps& o = OPS();
  CHECK(o.kind() == Kind::CircleSegment);
  CHECK_STR(o.name(), "circle_segment");
  CHECK_EQ(o.dimension(), 2);
  CHECK(!o.is_unbounded_kind());
  CHECK(o.has_polygon_set());                       // Gps_circle_segment_traits_2 exists
  CHECK(kind_has_polygon_set(Kind::CircleSegment));  // the registrar installed the factory
  CHECK_EQ(kind_from_name("circle_segment"), int(Kind::CircleSegment));
  CHECK_EQ(kind_from_name("arc"), int(Kind::CircleSegment));
#ifndef ARR2D_TEST_STUB_BSO
  // command line B only: the real bso_circle_segment.cpp is linked, so the registered factory
  // must produce a usable, empty polygon set of this kind.
  std::unique_ptr<PolygonSetBase> ps = make_polygon_set(Kind::CircleSegment);
  CHECK(ps != nullptr);
  if (ps) {
    CHECK(ps->kind() == Kind::CircleSegment);
    CHECK(ps->is_empty());
  }
#endif
}

// ===========================================================================
// 2. points
// ===========================================================================
static void test_points() {
  g_section = "points";
  const KindOps& o = OPS();

  // -- rational point ------------------------------------------------------
  Geom p = o.make_point(R(1, 2), R(-3));
  double x, y;
  approx_xy(p, x, y);
  CHECK_CLOSE(x, 0.5, 0.0);            // 1/2 is exactly representable
  CHECK_CLOSE(y, -3.0, 0.0);
  CHECK(o.point_is_rational(p));
  std::vector<Rational> xy;
  o.point_exact_rational(p, xy);
  CHECK_EQ(xy.size(), std::size_t(2));
  CHECK(xy[0] == R(1, 2));
  CHECK(xy[1] == R(-3));
  CHECK_STR(o.point_repr(p), "(1/2, -3)");
  std::vector<std::pair<double, double>> iv;
  o.point_interval(p, iv);
  CHECK_EQ(iv.size(), std::size_t(2));
  CHECK(iv[0].first <= 0.5 && 0.5 <= iv[0].second);
  CHECK(iv[1].first <= -3.0 && -3.0 <= iv[1].second);
  CHECK_CLOSE(o.approximate_coordinate(p, 0), 0.5, 0.0);
  CHECK_CLOSE(o.approximate_coordinate(p, 1), -3.0, 0.0);
  CHECK_THROWS(o.approximate_coordinate(p, 2), ErrorCode::InvalidArgument);

  // a coordinate that is NOT exactly a double: 1/3 must be the correctly rounded double
  // 0x3FD5555555555555 == (double)(1.0/3.0) — NOT CGAL::to_double's 0x...56
  // (number_types_and_errors.md gotcha 2).
  Geom third = o.make_point(R(1, 3), R(0));
  approx_xy(third, x, y);
  CHECK(x == 1.0 / 3.0);
  o.point_interval(third, iv);
  CHECK(rational_from_double(iv[0].first) <= R(1, 3) &&
        R(1, 3) <= rational_from_double(iv[0].second));

  // -- sqrt-extension point: x = 1 + sqrt(2), y = 0 ------------------------
  SqrtExt sx{R(1), R(1), R(2)};
  SqrtExt sy{R(0), R(0), R(0)};
  Geom ps = circle_segment::make_point_sqrt(sx, sy);
  approx_xy(ps, x, y);
  CHECK_CLOSE(x, 1.0 + std::sqrt(2.0), 1e-15);   // 2.414213562373095...
  CHECK_CLOSE(y, 0.0, 0.0);
  CHECK(!o.point_is_rational(ps));
  CHECK_THROWS(o.point_exact_rational(ps, xy), ErrorCode::NotRepresentable);
  std::vector<Geom> nums;
  o.point_exact(ps, nums);
  CHECK_EQ(nums.size(), std::size_t(2));
  CHECK(number_kind(nums[0]) == NumberKind::SqrtExt);
  CHECK(number_kind(nums[1]) == NumberKind::Rational);   // y == 0 normalises to a Rational box
  SqrtExt back = number_to_sqrt_ext(nums[0]);
  CHECK(back.a == R(1) && back.b == R(1) && back.c == R(2));
  CHECK_STR(o.point_repr(ps), "(1 + 1*sqrt(2), 0)");
  o.point_interval(ps, iv);
  CHECK(iv[0].first <= 1.0 + std::sqrt(2.0) && 1.0 + std::sqrt(2.0) <= iv[0].second);

  SqrtExt neg{R(0), R(-1), R(2)};                        // -sqrt(2)
  Geom pneg = circle_segment::make_point_sqrt(neg, sy);
  CHECK_STR(o.point_repr(pneg), "(0 - 1*sqrt(2), 0)");

  // round trip through circle_segment::point_sqrt
  SqrtExt rx, ry;
  circle_segment::point_sqrt(ps, rx, ry);
  CHECK(rx.a == R(1) && rx.b == R(1) && rx.c == R(2));
  CHECK(ry.a == R(0) && ry.b == R(0) && ry.c == R(0));

  // -- perfect-square radicand: 1 + 3*sqrt(4) == 7 is RATIONAL -------------
  // exact_coordinates_contract.md gotcha 3 / CGAL_TRAPS_CHECKLIST: is_extended() would still be
  // true here, so rationality must be decided by an exact square test.
  Geom seven = circle_segment::make_point_sqrt(SqrtExt{R(1), R(3), R(4)}, sy);
  CHECK(o.point_is_rational(seven));
  o.point_exact_rational(seven, xy);
  CHECK(xy[0] == R(7));
  CHECK_STR(o.point_repr(seven), "(7, 0)");
  CHECK(o.point_equal(seven, P(7, 0)));
  CHECK_EQ(o.point_compare_xy(seven, P(7, 0)), 0);

  // negative radicand is rejected
  CHECK_THROWS(circle_segment::make_point_sqrt(SqrtExt{R(0), R(1), R(-2)}, sy),
               ErrorCode::InvalidArgument);

  // -- comparisons ---------------------------------------------------------
  CHECK_EQ(o.point_compare_x(ps, P(2, 0)), 1);      // 1+sqrt(2) = 2.414 > 2
  CHECK_EQ(o.point_compare_xy(ps, P(2, 0)), 1);
  CHECK_EQ(o.point_compare_xy(P(2, 0), ps), -1);
  CHECK_EQ(o.point_compare_xy(P(1, 0), P(1, 5)), -1);   // same x, smaller y
  CHECK(!o.point_equal(ps, P(2, 0)));

  // -- make_point_3 is a sphere-only entry point ---------------------------
  CHECK_THROWS(o.make_point_3(R(1), R(0), R(0)), ErrorCode::Unsupported);

  // -- convert_point -------------------------------------------------------
  // identity for our own kind
  CHECK(points_equal(o.convert_point(P(3, 4)), P(3, 4)));
  // from a Segment-kind point (raw Epeck::Point_2 box; the segment TU is NOT linked)
  Geom seg_pt = make_geom(Kind::Segment, GeomType::Point,
                          SegmentTypes::Point_2(SegmentTypes::FT(3), SegmentTypes::FT(4)));
  Geom conv = o.convert_point(seg_pt);
  CHECK(conv.kind == Kind::CircleSegment);
  CHECK(points_equal(conv, P(3, 4)));
  // a kind that is not linked into this build reports Unsupported (the box deliberately holds a
  // type that is not Epeck::Point_2, so the registry path is taken)
  Geom fake_bezier_pt = make_geom(Kind::Bezier, GeomType::Point, double(1.5));
  CHECK_THROWS(o.convert_point(fake_bezier_pt), ErrorCode::Unsupported);
  // a curve where a point is required
  CHECK_THROWS(o.convert_point(circle_segment::make_segment(P(0, 0), P(1, 1))),
               ErrorCode::InvalidArgument);
}

// ===========================================================================
// 3. every curve constructor of namespace arr2d::circle_segment
// ===========================================================================
static void test_curve_constructors() {
  g_section = "constructors";
  const KindOps& o = OPS();

  // -- full circle from an explicit RATIONAL radius (traits gotcha 4) ------
  Geom circ = circle_segment::make_full_circle_r(R(0), R(0), R(2), +1);
  CHECK(circle_segment::is_full(circ));
  CHECK(circle_segment::is_circular(circ));
  CHECK(!circle_segment::is_linear(circ));
  CHECK_EQ(circle_segment::orientation(circ), 1);
  Rational cx, cy;
  circle_segment::center(circ, cx, cy);
  CHECK(cx == R(0) && cy == R(0));
  CHECK(circle_segment::squared_radius(circ) == R(4));
  CHECK(circle_segment::has_rational_radius(circ));
  CHECK(circle_segment::radius(circ) == R(2));
  CHECK_STR(o.curve_repr(circ), "Circle(center=(0, 0), squared_radius=4, orientation=ccw)");
  CHECK(o.curve_is_bounded(circ));

  // -- full circle from a squared radius that is not a perfect square ------
  Geom circ2 = circle_segment::make_full_circle(R(1), R(-1), R(2), -1);
  CHECK(circle_segment::is_full(circ2));
  CHECK_EQ(circle_segment::orientation(circ2), -1);
  CHECK(circle_segment::squared_radius(circ2) == R(2));
  CHECK(!circle_segment::has_rational_radius(circ2));   // sqrt(2) is irrational
  CHECK_THROWS(circle_segment::radius(circ2), ErrorCode::NotRepresentable);
  CHECK_STR(o.curve_repr(circ2), "Circle(center=(1, -1), squared_radius=2, orientation=cw)");
  // a squared radius that IS a perfect square answers true even without the radius ctor
  CHECK(circle_segment::has_rational_radius(circle_segment::make_full_circle(R(0), R(0), R(9), 1)));
  CHECK(circle_segment::radius(circle_segment::make_full_circle(R(0), R(0), R(9), 1)) == R(3));

  // -- circular arcs -------------------------------------------------------
  // CCW from (2,0) to (-2,0) on x^2 + y^2 = 4: the UPPER half circle.
  Geom arc = circle_segment::make_arc(R(0), R(0), R(4), +1, P(2, 0), P(-2, 0));
  CHECK(!circle_segment::is_full(arc));
  CHECK(circle_segment::is_circular(arc));
  CHECK_EQ(circle_segment::orientation(arc), 1);
  CHECK(circle_segment::squared_radius(arc) == R(4));
  CHECK_STR(o.curve_repr(arc),
            "CircularArc(center=(0, 0), squared_radius=4, orientation=ccw, source=(2, 0), "
            "target=(-2, 0))");
  Geom arc_r = circle_segment::make_arc_r(R(0), R(0), R(2), +1, P(2, 0), P(-2, 0));
  CHECK(circle_segment::has_rational_radius(arc_r));
  CHECK(o.curve_equal(o.to_x_monotone(arc), o.to_x_monotone(arc_r)));

  // arcs with SQRT endpoints: the circle x^2 + y^2 = 4 and the two points (1, +-sqrt(3)).
  Geom p_up = circle_segment::make_point_sqrt(SqrtExt{R(1), R(0), R(0)},
                                              SqrtExt{R(0), R(1), R(3)});
  Geom p_dn = circle_segment::make_point_sqrt(SqrtExt{R(1), R(0), R(0)},
                                              SqrtExt{R(0), R(-1), R(3)});
  Geom arc_sqrt = circle_segment::make_arc(R(0), R(0), R(4), +1, p_dn, p_up);
  CHECK(circle_segment::is_circular(arc_sqrt));
  CHECK_STR(o.curve_repr(arc_sqrt),
            "CircularArc(center=(0, 0), squared_radius=4, orientation=ccw, "
            "source=(1, 0 - 1*sqrt(3)), target=(1, 0 + 1*sqrt(3)))");

  // endpoint-not-on-circle / degenerate-argument checks (we report them, CGAL would assert)
  CHECK_THROWS(circle_segment::make_arc(R(0), R(0), R(4), +1, P(1, 0), P(-2, 0)),
               ErrorCode::InvalidArgument);
  CHECK_THROWS(circle_segment::make_arc(R(0), R(0), R(4), +1, P(2, 0), P(2, 0)),
               ErrorCode::InvalidArgument);
  CHECK_THROWS(circle_segment::make_arc(R(0), R(0), R(0), +1, P(0, 0), P(0, 0)),
               ErrorCode::InvalidArgument);
  CHECK_THROWS(circle_segment::make_full_circle(R(0), R(0), R(4), 0), ErrorCode::InvalidArgument);
  CHECK_THROWS(circle_segment::make_full_circle(R(0), R(0), R(-1), 1), ErrorCode::InvalidArgument);
  CHECK_THROWS(circle_segment::make_full_circle_r(R(0), R(0), R(-2), 1), ErrorCode::InvalidArgument);

  // -- arc through three points -------------------------------------------
  // (2,0), (0,2), (-2,0): circumcircle x^2+y^2=4; orientation_2 gives LEFT_TURN
  // ((0,2)-(2,0)) x ((-2,0)-(0,2)) = (-2,2) x (-2,-2) = 4 + 4 = 8 > 0  => CCW.
  Geom arc3 = circle_segment::make_arc_three_points(P(2, 0), P(0, 2), P(-2, 0));
  CHECK(circle_segment::is_circular(arc3));
  CHECK_EQ(circle_segment::orientation(arc3), 1);
  circle_segment::center(arc3, cx, cy);
  CHECK(cx == R(0) && cy == R(0));
  CHECK(circle_segment::squared_radius(arc3) == R(4));
  // three COLLINEAR points silently build the SEGMENT p1 -> p3 (documented CGAL behaviour)
  Geom arc3_line = circle_segment::make_arc_three_points(P(0, 0), P(1, 1), P(2, 2));
  CHECK(circle_segment::is_linear(arc3_line));
  CHECK_EQ(circle_segment::orientation(arc3_line), 0);
  CHECK_THROWS(circle_segment::make_arc_three_points(P(0, 0), P(1, 1), P(0, 0)),
               ErrorCode::InvalidArgument);
  // sqrt coordinates are not acceptable for the three-point constructor
  CHECK_THROWS(circle_segment::make_arc_three_points(p_up, P(0, 2), P(-2, 0)),
               ErrorCode::NotRepresentable);

  // -- line segments -------------------------------------------------------
  Geom seg = circle_segment::make_segment(P(0, 0), P(4, 0));
  CHECK(circle_segment::is_linear(seg));
  CHECK(!circle_segment::is_full(seg));
  CHECK_EQ(circle_segment::orientation(seg), 0);
  CHECK_STR(o.curve_repr(seg), "Segment((0, 0), (4, 0))");
  Rational la, lb, lc;
  circle_segment::supporting_line(seg, la, lb, lc);
  // CGAL's line_from_pointsC2 special-cases horizontal/vertical lines for robustness:
  // py == qy and qx > px  =>  (a, b, c) = (0, 1, -py) = (0, 1, 0)  [verified against CGAL].
  CHECK(la == R(0) && lb == R(1) && lc == R(0));
  CHECK_THROWS(circle_segment::center(seg, cx, cy), ErrorCode::InvalidArgument);
  CHECK_THROWS(circle_segment::squared_radius(seg), ErrorCode::InvalidArgument);
  CHECK_THROWS(circle_segment::supporting_line(circ, la, lb, lc), ErrorCode::InvalidArgument);
  CHECK_THROWS(circle_segment::make_segment(P(1, 1), P(1, 1)), ErrorCode::InvalidArgument);
  CHECK_THROWS(circle_segment::make_segment(p_up, P(1, 1)), ErrorCode::NotRepresentable);

  // -- segment on an explicit line, with sqrt endpoints --------------------
  // line y = 0  (0*x + 1*y + 0 = 0)
  Geom seg_line = circle_segment::make_segment_on_line(R(0), R(1), R(0), P(-5, 0), P(5, 0));
  CHECK(circle_segment::is_linear(seg_line));
  circle_segment::supporting_line(seg_line, la, lb, lc);
  CHECK(la == R(0) && lb == R(1) && lc == R(0));
  // line x - y = 0 with the endpoints (0,0) and (sqrt(2), sqrt(2))
  Geom d = circle_segment::make_point_sqrt(SqrtExt{R(0), R(1), R(2)}, SqrtExt{R(0), R(1), R(2)});
  Geom seg_diag = circle_segment::make_segment_on_line(R(1), R(-1), R(0), P(0, 0), d);
  CHECK(circle_segment::is_linear(seg_diag));
  CHECK_STR(o.curve_repr(seg_diag), "Segment((0, 0), (0 + 1*sqrt(2), 0 + 1*sqrt(2)))");
  CHECK_THROWS(circle_segment::make_segment_on_line(R(0), R(1), R(0), P(0, 0), P(1, 1)),
               ErrorCode::InvalidArgument);
  CHECK_THROWS(circle_segment::make_segment_on_line(R(0), R(0), R(1), P(0, 0), P(1, 1)),
               ErrorCode::InvalidArgument);
}

// ===========================================================================
// 4. make_x_monotone + 5. x-monotone accessors
// ===========================================================================
static void test_x_monotone(std::vector<Geom>& circle_pieces) {
  g_section = "x-monotone";
  const KindOps& o = OPS();

  // -- full CCW circle of radius 2 at the origin ---------------------------
  Geom circ = circle_segment::make_full_circle_r(R(0), R(0), R(2), +1);
  CHECK(!o.is_x_monotone(circ));
  CHECK_THROWS(o.to_x_monotone(circ), ErrorCode::NotXMonotone);
  o.make_x_monotone(circ, circle_pieces);
  // CGAL splits a full circle at its two vertical tangency points:
  // piece 0 = leftmost -> rightmost, piece 1 = rightmost -> leftmost, both keeping the circle's
  // orientation.  With the radius ctor the tangency points are RATIONAL: (-2,0) and (2,0).
  CHECK_EQ(circle_pieces.size(), std::size_t(2));
  CHECK(circle_pieces[0].type == GeomType::XCurve);
  const Geom& lower = circle_pieces[0];
  const Geom& upper = circle_pieces[1];
  CHECK(points_equal(o.xcurve_source(lower), P(-2, 0)));
  CHECK(points_equal(o.xcurve_target(lower), P(2, 0)));
  CHECK(points_equal(o.xcurve_source(upper), P(2, 0)));
  CHECK(points_equal(o.xcurve_target(upper), P(-2, 0)));
  CHECK(o.xcurve_has_source(lower) && o.xcurve_has_target(lower));
  CHECK(o.xcurve_is_directed_right(lower));
  CHECK(!o.xcurve_is_directed_right(upper));
  CHECK_EQ(o.compare_endpoints_xy(lower), -1);
  CHECK_EQ(o.compare_endpoints_xy(upper), +1);
  CHECK(!o.xcurve_is_vertical(lower));
  CHECK(points_equal(o.xcurve_min_vertex(upper), P(-2, 0)));
  CHECK(points_equal(o.xcurve_max_vertex(upper), P(2, 0)));
  // CCW traversal starting at the leftmost point goes DOWNWARD, so piece 0 is the lower arc:
  // its midpoint (0,-2) is below the centre.
  CHECK_EQ(o.compare_y_at_x(P(0, 0), lower), +1);   // (0,0) is above the lower arc
  CHECK_EQ(o.compare_y_at_x(P(0, 0), upper), -1);   // and below the upper arc
  CHECK(o.is_in_x_range(upper, P(0, 0)));
  CHECK(!o.is_in_x_range(upper, P(3, 0)));
  CHECK_EQ(o.parameter_space_in_x(upper, ARR_MIN_END), int(ARR_INTERIOR));
  CHECK_EQ(o.parameter_space_in_y(upper, ARR_MAX_END), int(ARR_INTERIOR));

  // construct_opposite flips the direction but Equal_2 is direction-insensitive
  Geom opp = o.construct_opposite(upper);
  CHECK(o.xcurve_is_directed_right(opp));
  CHECK(o.curve_equal(opp, upper));
  CHECK(points_equal(o.xcurve_source(opp), P(-2, 0)));

  // -- a single x-monotone arc --------------------------------------------
  Geom arc = circle_segment::make_arc(R(0), R(0), R(4), +1, P(2, 0), P(-2, 0));
  std::vector<Geom> pieces;                          // NB: make_x_monotone APPENDS, so clear()
  o.make_x_monotone(arc, pieces);
  CHECK_EQ(pieces.size(), std::size_t(1));          // no interior vertical tangency point
  CHECK(o.is_x_monotone(arc));
  CHECK(o.curve_equal(o.to_x_monotone(arc), upper));

  // -- an arc spanning ONE vertical tangency point -------------------------
  // CCW from (0,-2) to (0,2) runs through the rightmost point (2,0) => 2 pieces.
  Geom arc2 = circle_segment::make_arc(R(0), R(0), R(4), +1, P(0, -2), P(0, 2));
  pieces.clear();
  o.make_x_monotone(arc2, pieces);
  CHECK_EQ(pieces.size(), std::size_t(2));
  CHECK(points_equal(o.xcurve_source(pieces[0]), P(0, -2)));
  CHECK(points_equal(o.xcurve_target(pieces[0]), P(2, 0)));      // the tangency point
  CHECK(points_equal(o.xcurve_source(pieces[1]), P(2, 0)));
  CHECK(points_equal(o.xcurve_target(pieces[1]), P(0, 2)));

  // -- a segment is already x-monotone; a vertical one is vertical ---------
  Geom seg = circle_segment::make_segment(P(-5, 0), P(5, 0));
  pieces.clear();
  o.make_x_monotone(seg, pieces);
  CHECK_EQ(pieces.size(), std::size_t(1));
  CHECK(!o.xcurve_is_vertical(pieces[0]));
  Geom vseg = circle_segment::make_segment(P(0, -3), P(0, 3));
  pieces.clear();
  o.make_x_monotone(vseg, pieces);
  CHECK_EQ(pieces.size(), std::size_t(1));
  CHECK(o.xcurve_is_vertical(pieces[0]));

  // -- degenerate circle (squared radius 0) -> a single isolated point -----
  Geom dot = circle_segment::make_full_circle(R(1), R(2), R(0), +1);
  pieces.clear();
  o.make_x_monotone(dot, pieces);
  CHECK_EQ(pieces.size(), std::size_t(1));
  CHECK(pieces[0].type == GeomType::Point);
  CHECK(points_equal(pieces[0], P(1, 2)));
  CHECK(!o.is_x_monotone(dot));
  CHECK_THROWS(o.to_x_monotone(dot), ErrorCode::NotXMonotone);

  // -- to_curve: X_monotone_curve_2 -> Curve_2 -----------------------------
  Geom back = o.to_curve(upper);
  CHECK(back.type == GeomType::Curve);
  CHECK(circle_segment::is_circular(back));
  CHECK(!circle_segment::is_full(back));
  CHECK_EQ(circle_segment::orientation(back), 1);
  pieces.clear();
  o.make_x_monotone(back, pieces);
  CHECK_EQ(pieces.size(), std::size_t(1));          // a half circle stays one piece
  CHECK(o.curve_equal(pieces[0], upper));
  Geom lin_back = o.to_curve(o.to_x_monotone(seg));
  CHECK(circle_segment::is_linear(lin_back));
  CHECK_STR(o.curve_repr(lin_back), "Segment((-5, 0), (5, 0))");
  // a Curve box where an x-monotone curve is required
  CHECK_THROWS(o.to_curve(circ), ErrorCode::NotXMonotone);
  CHECK_THROWS(o.xcurve_source(circ), ErrorCode::NotXMonotone);
}

// ===========================================================================
// 6. approximate()
// ===========================================================================
static void test_approximate(const std::vector<Geom>& circle_pieces) {
  g_section = "approximate";
  const KindOps& o = OPS();
  const Geom& upper = circle_pieces[1];   // (2,0) -> (-2,0), the upper half of x^2+y^2=4

  std::vector<double> poly;
  const double tol = 1e-3;
  o.approximate(upper, tol, nullptr, poly);
  CHECK(poly.size() >= 6 && poly.size() % 2 == 0);
  const std::size_t n = poly.size() / 2;
  // The polyline runs source -> target, i.e. from (2,0) to (-2,0).
  CHECK_CLOSE(poly[0], 2.0, 1e-12);
  CHECK_CLOSE(poly[1], 0.0, 1e-12);
  CHECK_CLOSE(poly[2 * (n - 1)], -2.0, 1e-12);
  CHECK_CLOSE(poly[2 * (n - 1) + 1], 0.0, 1e-12);
  // every emitted vertex lies ON the circle, and x decreases monotonically
  bool on_circle = true, decreasing = true, upper_half = true;
  for (std::size_t i = 0; i < n; ++i) {
    if (!close_to(radius_at(poly, i), 2.0, 1e-12)) on_circle = false;
    if (poly[2 * i + 1] < -1e-12) upper_half = false;
    if (i > 0 && poly[2 * i] > poly[2 * (i - 1)] + 1e-12) decreasing = false;
  }
  CHECK(on_circle);
  CHECK(decreasing);
  CHECK(upper_half);
  // the chord midpoints stay within `tol` of the true arc (CGAL's `error` is the perpendicular
  // deviation, rendering_and_approximation.md gotcha 3)
  double worst = 0.0;
  for (std::size_t i = 1; i < n; ++i) {
    const double mx = 0.5 * (poly[2 * i] + poly[2 * (i - 1)]);
    const double my = 0.5 * (poly[2 * i + 1] + poly[2 * (i - 1) + 1]);
    worst = std::max(worst, std::fabs(2.0 - std::sqrt(mx * mx + my * my)));
  }
  CHECK(worst <= tol);
  std::printf("  [approximate] upper half circle, tol=%g: %zu points, worst sagitta %.3e\n", tol,
              n, worst);

  // a coarser tolerance must not produce more points
  std::vector<double> coarse;
  o.approximate(upper, 0.5, nullptr, coarse);
  CHECK(coarse.size() <= poly.size());

  // -- a linear x-monotone curve yields exactly its two endpoints ----------
  Geom seg = o.to_x_monotone(circle_segment::make_segment(P(-5, 1), P(5, 1)));
  o.approximate(seg, tol, nullptr, poly);
  CHECK_EQ(poly.size(), std::size_t(4));
  CHECK_CLOSE(poly[0], -5.0, 0.0);
  CHECK_CLOSE(poly[1], 1.0, 0.0);
  CHECK_CLOSE(poly[2], 5.0, 0.0);
  CHECK_CLOSE(poly[3], 1.0, 0.0);
  // ... and reversed for the opposite curve
  o.approximate(o.construct_opposite(seg), tol, nullptr, poly);
  CHECK_CLOSE(poly[0], 5.0, 0.0);
  CHECK_CLOSE(poly[2], -5.0, 0.0);

  // -- a whole (CCW) circle comes out as a closed ring ---------------------
  Geom circ = circle_segment::make_full_circle_r(R(0), R(0), R(2), +1);
  o.approximate(circ, tol, nullptr, poly);
  const std::size_t m = poly.size() / 2;
  CHECK(m > 8);
  CHECK_CLOSE(poly[0], -2.0, 1e-12);                    // starts at the leftmost point
  CHECK_CLOSE(poly[1], 0.0, 1e-12);
  CHECK_CLOSE(poly[2 * (m - 1)], -2.0, 1e-12);          // and closes there
  CHECK_CLOSE(poly[2 * (m - 1) + 1], 0.0, 1e-12);
  CHECK(poly[3] < 0.0);   // CCW from the leftmost point goes DOWN first
  bool ring_ok = true;
  for (std::size_t i = 0; i < m; ++i)
    if (!close_to(radius_at(poly, i), 2.0, 1e-12)) ring_ok = false;
  CHECK(ring_ok);
  // the shared junction points (the two tangency points) are emitted once each
  int at_left = 0, at_right = 0;
  for (std::size_t i = 0; i < m; ++i) {
    if (close_to(poly[2 * i], -2.0, 1e-12) && close_to(poly[2 * i + 1], 0.0, 1e-12)) ++at_left;
    if (close_to(poly[2 * i], 2.0, 1e-12) && close_to(poly[2 * i + 1], 0.0, 1e-12)) ++at_right;
  }
  CHECK_EQ(at_left, 2);    // first and last (the ring is closed)
  CHECK_EQ(at_right, 1);   // the interior junction is de-duplicated

  // a CW circle runs the other way
  Geom circ_cw = circle_segment::make_full_circle_r(R(0), R(0), R(2), -1);
  o.approximate(circ_cw, tol, nullptr, poly);
  CHECK_CLOSE(poly[0], -2.0, 1e-12);
  CHECK(poly[3] > 0.0);    // CW from the leftmost point goes UP first

  // -- approximate_length of the full circle: 2*pi*2 = 12.566370614359172 --
  const double len = o.approximate_length(circ, 1e-4);
  CHECK_CLOSE(len, 4.0 * std::acos(-1.0), 1e-3);
  CHECK(len < 4.0 * std::acos(-1.0));   // a chord polygon always underestimates
  std::printf("  [approximate] full circle length (tol 1e-4) = %.9f (4*pi = %.9f)\n", len,
              4.0 * std::acos(-1.0));
  // approximate_length of a quarter arc of radius 2 = 2 * (pi/2) = pi
  Geom quarter = circle_segment::make_arc(R(0), R(0), R(4), +1, P(2, 0), P(0, 2));
  CHECK_CLOSE(o.approximate_length(quarter, 1e-5), std::acos(-1.0), 1e-4);
  CHECK(o.curve_is_bounded(quarter));
  CHECK(o.curve_is_bounded(o.to_x_monotone(quarter)));
  // ... and of a straight segment: |(3,4) - (0,0)| = 5 exactly
  CHECK_CLOSE(o.approximate_length(circle_segment::make_segment(P(0, 0), P(3, 4)), 1e-3), 5.0,
              1e-12);

  // a degenerate circle approximates to its centre, once
  o.approximate(circle_segment::make_full_circle(R(1), R(2), R(0), +1), tol, nullptr, poly);
  CHECK_EQ(poly.size(), std::size_t(2));
  CHECK_CLOSE(poly[0], 1.0, 0.0);
  CHECK_CLOSE(poly[1], 2.0, 0.0);

  // -- tolerance validation (error <= 0 SIGSEGVs inside CGAL) --------------
  CHECK_THROWS(o.approximate(upper, 0.0, nullptr, poly), ErrorCode::InvalidArgument);
  CHECK_THROWS(o.approximate(upper, -1.0, nullptr, poly), ErrorCode::InvalidArgument);
  CHECK_THROWS(o.approximate(upper, std::nan(""), nullptr, poly), ErrorCode::InvalidArgument);
  // a very small tolerance is clamped to 1e-12 and must still terminate
  o.approximate(o.to_x_monotone(circle_segment::make_segment(P(0, 0), P(1, 0))), 1e-300, nullptr,
                poly);
  CHECK_EQ(poly.size(), std::size_t(4));
}

// ===========================================================================
// 7. curve_bbox
// ===========================================================================
static void test_bbox(const std::vector<Geom>& circle_pieces) {
  g_section = "bbox";
  const KindOps& o = OPS();

  // full circle of radius 2 at the origin: exactly [-2,2] x [-2,2]
  BBox b = o.curve_bbox(circle_segment::make_full_circle_r(R(0), R(0), R(2), +1));
  CHECK_EQ(b.dim, 2);
  CHECK_CLOSE(b.lo[0], -2.0, 0.0);
  CHECK_CLOSE(b.lo[1], -2.0, 0.0);
  CHECK_CLOSE(b.hi[0], 2.0, 0.0);
  CHECK_CLOSE(b.hi[1], 2.0, 0.0);

  // upper half: [-2,2] x [0,2]; lower half: [-2,2] x [-2,0]
  b = o.curve_bbox(circle_pieces[1]);
  CHECK_CLOSE(b.lo[0], -2.0, 0.0);
  CHECK_CLOSE(b.hi[0], 2.0, 0.0);
  CHECK_CLOSE(b.lo[1], 0.0, 0.0);
  CHECK_CLOSE(b.hi[1], 2.0, 0.0);
  b = o.curve_bbox(circle_pieces[0]);
  CHECK_CLOSE(b.lo[1], -2.0, 0.0);
  CHECK_CLOSE(b.hi[1], 0.0, 0.0);

  // quarter arc (0,0)-centred, CCW from (2,0) to (0,2): [0,2] x [0,2] and the y-extremum is an
  // ENDPOINT, not the top of the circle — the box must stay tight.
  Geom quarter = circle_segment::make_arc(R(0), R(0), R(4), +1, P(2, 0), P(0, 2));
  b = o.curve_bbox(quarter);
  CHECK_CLOSE(b.lo[0], 0.0, 0.0);
  CHECK_CLOSE(b.hi[0], 2.0, 0.0);
  CHECK_CLOSE(b.lo[1], 0.0, 0.0);
  CHECK_CLOSE(b.hi[1], 2.0, 0.0);

  // an arc that spans a vertical tangency point: CCW (0,-2) -> (0,2) through (2,0)
  b = o.curve_bbox(circle_segment::make_arc(R(0), R(0), R(4), +1, P(0, -2), P(0, 2)));
  CHECK_CLOSE(b.lo[0], 0.0, 0.0);
  CHECK_CLOSE(b.hi[0], 2.0, 0.0);
  CHECK_CLOSE(b.lo[1], -2.0, 0.0);
  CHECK_CLOSE(b.hi[1], 2.0, 0.0);

  // a circle with an irrational radius: sqrt(2) = 1.4142135623730951, so the box must ENCLOSE
  // [-sqrt(2), sqrt(2)]^2 (the interval endpoints are certified, hence <= / >=).
  b = o.curve_bbox(circle_segment::make_full_circle(R(0), R(0), R(2), +1));
  CHECK(b.lo[0] <= -std::sqrt(2.0) && b.hi[0] >= std::sqrt(2.0));
  CHECK(b.lo[1] <= -std::sqrt(2.0) && b.hi[1] >= std::sqrt(2.0));
  CHECK(b.hi[0] - std::sqrt(2.0) < 1e-15);   // and it must be TIGHT

  // segments
  b = o.curve_bbox(circle_segment::make_segment(P(0, 0), P(4, -1)));
  CHECK_CLOSE(b.lo[0], 0.0, 0.0);
  CHECK_CLOSE(b.lo[1], -1.0, 0.0);
  CHECK_CLOSE(b.hi[0], 4.0, 0.0);
  CHECK_CLOSE(b.hi[1], 0.0, 0.0);

  // a degenerate circle boxes to its centre
  b = o.curve_bbox(circle_segment::make_full_circle(R(1), R(2), R(0), +1));
  CHECK_CLOSE(b.lo[0], 1.0, 0.0);
  CHECK_CLOSE(b.hi[1], 2.0, 0.0);
}

// ===========================================================================
// 8. convert_curve from other kinds (boxed as raw CGAL objects; those TUs are NOT linked)
// ===========================================================================
static void test_convert_curve() {
  g_section = "convert_curve";
  const KindOps& o = OPS();
  using SK = SegmentTypes::Kernel;
  std::vector<Geom> out;

  // -- from Kind::Segment (Arr_segment_2<Epeck>) ---------------------------
  SegmentTypes::Curve_2 s(SK::Point_2(0, 0), SK::Point_2(3, 4));
  Geom gseg = make_geom(Kind::Segment, GeomType::XCurve, s);
  o.convert_curve(gseg, out);
  CHECK_EQ(out.size(), std::size_t(1));
  CHECK(out[0].kind == Kind::CircleSegment);
  CHECK(circle_segment::is_linear(out[0]));
  CHECK_STR(o.curve_repr(out[0]), "Segment((0, 0), (3, 4))");

  // -- from Kind::Linear ---------------------------------------------------
  LinearTypes::Curve_2 lseg(SK::Point_2(1, 1), SK::Point_2(2, 5));     // a bounded segment
  o.convert_curve(make_geom(Kind::Linear, GeomType::XCurve, lseg), out);
  CHECK_EQ(out.size(), std::size_t(1));
  CHECK_STR(o.curve_repr(out[0]), "Segment((1, 1), (2, 5))");
  const SK::Point_2 o0(0, 0), o1(1, 1);
  LinearTypes::Curve_2 lline{SK::Line_2(o0, o1)};
  CHECK_THROWS(o.convert_curve(make_geom(Kind::Linear, GeomType::XCurve, lline), out),
               ErrorCode::NotRepresentable);
  LinearTypes::Curve_2 lray{SK::Ray_2(o0, o1)};
  CHECK_THROWS(o.convert_curve(make_geom(Kind::Linear, GeomType::XCurve, lray), out),
               ErrorCode::NotRepresentable);

  // -- from Kind::Polyline: one circle-segment segment per subcurve --------
  std::vector<PolylineTypes::Segment_2> subs;
  subs.push_back(PolylineTypes::Segment_2(SK::Point_2(0, 0), SK::Point_2(1, 1)));
  subs.push_back(PolylineTypes::Segment_2(SK::Point_2(1, 1), SK::Point_2(2, 0)));
  PolylineTypes::Curve_2 pc(subs.begin(), subs.end());
  o.convert_curve(make_geom(Kind::Polyline, GeomType::Curve, pc), out);
  CHECK_EQ(out.size(), std::size_t(2));
  CHECK_STR(o.curve_repr(out[0]), "Segment((0, 0), (1, 1))");
  CHECK_STR(o.curve_repr(out[1]), "Segment((1, 1), (2, 0))");

  // -- identity for our own kind -------------------------------------------
  Geom own = circle_segment::make_full_circle_r(R(0), R(0), R(2), +1);
  o.convert_curve(own, out);
  CHECK_EQ(out.size(), std::size_t(1));
  CHECK(circle_segment::is_full(out[0]));

  // -- a kind with no exact circle-segment image ---------------------------
  // (a deliberately fake box: convert_curve only needs the kind tag and the stored C++ type to
  //  fall through to the final NotRepresentable, so no conic/Bezier headers are needed here)
  Geom fake_conic = make_geom(Kind::Conic, GeomType::Curve, double(0.0));
  CHECK_THROWS(o.convert_curve(fake_conic, out), ErrorCode::NotRepresentable);
  CHECK_THROWS(o.convert_curve(P(0, 0), out), ErrorCode::InvalidArgument);   // a point
}

// ===========================================================================
// 9. traits functors on this kind
// ===========================================================================
static void test_traits_ops(const std::vector<Geom>& circle_pieces) {
  g_section = "traits";
  const KindOps& o = OPS();
  const Geom& lower = circle_pieces[0];
  const Geom& upper = circle_pieces[1];

  // Arr_circle_segment_traits_2 has NO Construct_x_monotone_curve_2 (gotcha 3)
  CHECK_THROWS(o.construct_x_monotone_curve(P(0, 0), P(1, 1)), ErrorCode::Unsupported);

  // -- split / merge -------------------------------------------------------
  Geom left, right;
  o.split(upper, P(0, 2), left, right);              // (0,2) is the top of x^2+y^2=4
  CHECK(points_equal(o.xcurve_min_vertex(left), P(-2, 0)));
  CHECK(points_equal(o.xcurve_max_vertex(left), P(0, 2)));
  CHECK(points_equal(o.xcurve_min_vertex(right), P(0, 2)));
  CHECK(points_equal(o.xcurve_max_vertex(right), P(2, 0)));
  CHECK(o.are_mergeable(left, right));
  CHECK(o.curve_equal(o.merge(left, right), upper));
  // a segment and an arc never share a supporting curve, so they are never mergeable
  Geom tail = o.to_x_monotone(circle_segment::make_segment(P(-5, 0), P(-2, 0)));
  CHECK(!o.are_mergeable(left, tail));
  CHECK_THROWS(o.merge(upper, P(0, 0)), ErrorCode::InvalidArgument);

  // -- trim ----------------------------------------------------------------
  Geom trimmed = o.trim(upper, P(0, 2), P(-2, 0));
  CHECK(o.curve_equal(trimmed, left));
  CHECK_THROWS(o.trim(upper, P(0, 2), P(0, 2)), ErrorCode::InvalidArgument);

  // -- intersect: the two half circles meet at their two shared endpoints --
  std::vector<IntersectionResult> hits;
  o.intersect(lower, upper, hits);
  CHECK_EQ(hits.size(), std::size_t(2));
  if (hits.size() == 2) {
    CHECK(hits[0].is_point && hits[1].is_point);
    const bool got_left = points_equal(hits[0].point, P(-2, 0)) ||
                          points_equal(hits[1].point, P(-2, 0));
    const bool got_right = points_equal(hits[0].point, P(2, 0)) ||
                           points_equal(hits[1].point, P(2, 0));
    CHECK(got_left);
    CHECK(got_right);
  }

  // -- intersect producing an IRRATIONAL point -----------------------------
  // x^2 + y^2 = 4 meets the vertical line x = 1 at y = +-sqrt(3); the upper arc keeps the "+".
  Geom vline = o.to_x_monotone(circle_segment::make_segment(P(1, -3), P(1, 3)));
  hits.clear();
  o.intersect(upper, vline, hits);
  CHECK_EQ(hits.size(), std::size_t(1));
  if (hits.size() == 1) {
    CHECK(hits[0].is_point);
    const Geom& q = hits[0].point;
    double qx, qy;
    approx_xy(q, qx, qy);
    CHECK_CLOSE(qx, 1.0, 1e-15);
    CHECK_CLOSE(qy, std::sqrt(3.0), 1e-15);          // 1.7320508075688772
    CHECK(!o.point_is_rational(q));                  // sqrt(3) is irrational
    std::vector<Geom> nums;
    o.point_exact(q, nums);
    CHECK(number_kind(nums[0]) == NumberKind::Rational);
    CHECK(number_kind(nums[1]) == NumberKind::SqrtExt);
    SqrtExt sy = number_to_sqrt_ext(nums[1]);
    // whatever representation CGAL chose, the value squares to 3: (a + b*sqrt(c))^2 with a == 0
    CHECK(sy.a == R(0));
    CHECK(sy.b * sy.b * sy.c == R(3));
    std::vector<std::pair<double, double>> iv;
    o.point_interval(q, iv);
    CHECK(iv[1].first <= std::sqrt(3.0) && std::sqrt(3.0) <= iv[1].second);
    std::printf("  [traits] circle x line -> y = %s\n", o.point_repr(q).c_str());
  }

  // -- overlap -------------------------------------------------------------
  Geom half_of_upper;
  {
    Geom l2, r2;
    o.split(upper, P(0, 2), l2, r2);
    half_of_upper = l2;
  }
  hits.clear();
  o.intersect(upper, half_of_upper, hits);
  CHECK_EQ(hits.size(), std::size_t(1));
  if (hits.size() == 1) {
    CHECK(!hits[0].is_point);
    CHECK(o.curve_equal(hits[0].overlap, half_of_upper));
  }

  // -- compare_y_at_x_left / right at the shared point (2,0) ---------------
  // Immediately to the LEFT of (2,0) the upper arc is above the lower one.
  CHECK_EQ(o.compare_y_at_x_left(upper, lower, P(2, 0)), +1);
  CHECK_EQ(o.compare_y_at_x_left(lower, upper, P(2, 0)), -1);
  CHECK_EQ(o.compare_y_at_x_right(upper, lower, P(-2, 0)), +1);

  // -- errors --------------------------------------------------------------
  CHECK_THROWS(o.compare_y_at_x(P(5, 0), upper), ErrorCode::InvalidArgument);  // out of x-range
  CHECK_THROWS(o.curve_bbox(P(0, 0)), ErrorCode::InvalidArgument);             // a point
  Geom foreign = make_geom(Kind::Segment, GeomType::Point,
                           SegmentTypes::Point_2(SegmentTypes::FT(0), SegmentTypes::FT(0)));
  CHECK_THROWS(o.point_compare_xy(foreign, P(0, 0)), ErrorCode::KindMismatch);
}

// ===========================================================================
// 10. full arrangement round trip
// ===========================================================================
static void test_arrangement() {
  g_section = "arrangement";
  const KindOps& o = OPS();
  std::unique_ptr<ArrBase> arr = make_arrangement(Kind::CircleSegment);
  CHECK(arr->kind() == Kind::CircleSegment);
  CHECK(arr->is_empty());
  CHECK_EQ(arr->number_of_faces(), std::size_t(1));

  // ---- insert a full circle: 2 arcs meeting at (-2,0) and (2,0) ----------
  // V = 2, E = 2, F = 2 (disk + unbounded);  Euler 2 - 2 + 2 = 2.
  CH ch_circle = arr->insert_curve(circle_segment::make_full_circle_r(R(0), R(0), R(2), +1));
  CHECK_EQ(arr->number_of_vertices(), std::size_t(2));
  CHECK_EQ(arr->number_of_edges(), std::size_t(2));
  CHECK_EQ(arr->number_of_faces(), std::size_t(2));
  CHECK_EQ(arr->number_of_induced_edges(ch_circle), std::size_t(2));

  // ---- add the horizontal segment (-5,0) -> (5,0) ------------------------
  // It cuts the circle at (-2,0) and (2,0) (already vertices) and adds (-5,0), (5,0).
  // V = 4, E = 5 (3 segment pieces + 2 arcs), F = 3 (upper half disk, lower half disk,
  // unbounded).  This reproduces the smoke test in traits_circle_segment.md §6.
  CH ch_h = arr->insert_curve(circle_segment::make_segment(P(-5, 0), P(5, 0)));
  CHECK_EQ(arr->number_of_vertices(), std::size_t(4));
  CHECK_EQ(arr->number_of_edges(), std::size_t(5));
  CHECK_EQ(arr->number_of_faces(), std::size_t(3));

  // ---- add the vertical segment (0,-3) -> (0,3) --------------------------
  // New vertices: (0,-3), (0,-2), (0,0), (0,2), (0,3)  => V = 9.
  // Edges: 4 horizontal + 4 vertical + 4 arc halves      => E = 12.
  // Euler: 9 - 12 + F = 2 => F = 5 (4 quarter disks + the unbounded face).
  CH ch_v = arr->insert_curve(circle_segment::make_segment(P(0, -3), P(0, 3)));
  CHECK_EQ(arr->number_of_vertices(), std::size_t(9));
  CHECK_EQ(arr->number_of_edges(), std::size_t(12));
  CHECK_EQ(arr->number_of_halfedges(), std::size_t(24));
  CHECK_EQ(arr->number_of_faces(), std::size_t(5));
  CHECK_EQ(arr->number_of_unbounded_faces(), std::size_t(1));
  CHECK_EQ(arr->number_of_curves(), std::size_t(3));
  CHECK_EQ(arr->number_of_isolated_vertices(), std::size_t(0));
  CHECK(arr->is_valid());
  CHECK(!arr->is_empty());
  CHECK_EQ(arr->number_of_induced_edges(ch_v), std::size_t(4));
  CHECK_EQ(arr->number_of_induced_edges(ch_h), std::size_t(4));
  CHECK_EQ(arr->number_of_induced_edges(ch_circle), std::size_t(4));

  // ---- iteration ---------------------------------------------------------
  std::vector<VH> vs;
  std::vector<HH> hs, es;
  std::vector<FH> fs;
  std::vector<CH> cs;
  arr->vertices(vs);
  arr->halfedges(hs);
  arr->edges(es);
  arr->faces(fs);
  arr->curves(cs);
  CHECK_EQ(vs.size(), std::size_t(9));
  CHECK_EQ(hs.size(), std::size_t(24));
  CHECK_EQ(es.size(), std::size_t(12));
  CHECK_EQ(fs.size(), std::size_t(5));
  CHECK_EQ(cs.size(), std::size_t(3));
  std::size_t degree_sum = 0;
  for (VH v : vs) {
    CHECK(arr->vertex_valid(v));
    degree_sum += arr->vertex_degree(v);
  }
  CHECK_EQ(degree_sum, std::size_t(24));   // sum of degrees == number of halfedges
  CHECK(circle_segment::is_full(arr->curve_geometry(cs[0])));
  CHECK_STR(o.curve_repr(arr->curve_geometry(cs[1])), "Segment((-5, 0), (5, 0))");

  // the bbox of the vertex approximations: x in [-5,5], y in [-3,3]
  BBox bb = arr->bbox();
  CHECK_CLOSE(bb.lo[0], -5.0, 0.0);
  CHECK_CLOSE(bb.hi[0], 5.0, 0.0);
  CHECK_CLOSE(bb.lo[1], -3.0, 0.0);
  CHECK_CLOSE(bb.hi[1], 3.0, 0.0);

  // ---- point location ----------------------------------------------------
  CHECK(arr->supports_point_location(PL_NAIVE));
  CHECK(arr->supports_point_location(PL_SIMPLE));
  CHECK(arr->supports_point_location(PL_WALK));
  CHECK(arr->supports_point_location(PL_TRAPEZOID));
  // gotcha 3: Arr_landmarks_point_location does not COMPILE for this traits (no
  // Construct_x_monotone_curve_2), and the triangulation strategy needs a Kernel + straight
  // edges — KindPolicy<CircleSegmentTypes> switches both off.
  CHECK(!arr->supports_point_location(PL_LANDMARKS));
  CHECK(!arr->supports_point_location(PL_TRIANGULATION));
  CHECK_THROWS(arr->locate(P(1, 1), PL_LANDMARKS), ErrorCode::Unsupported);
  CHECK_THROWS(arr->attach_point_location(PL_LANDMARKS), ErrorCode::Unsupported);
  CHECK_THROWS(arr->locate(P(1, 1), PL_TRIANGULATION), ErrorCode::Unsupported);

  const int strategies[4] = {PL_NAIVE, PL_SIMPLE, PL_WALK, PL_TRAPEZOID};
  // (1,1) is inside the quarter disk x>0, y>0 (1 + 1 = 2 < 4)
  Located ref = arr->locate(P(1, 1), PL_NAIVE);
  CHECK_EQ(ref.type, 2);
  CHECK(!arr->face_is_unbounded(ref.as_face()));
  for (int s : strategies) {
    Located l = arr->locate(P(1, 1), s);
    CHECK_EQ(l.type, 2);
    CHECK(l.p == ref.p);
    arr->attach_point_location(s);
    CHECK(arr->has_point_location(s));
    Located l2 = arr->locate(P(1, 1), s);
    CHECK(l2.p == ref.p);
  }
  // (0,0) is the central vertex; (3,0) lies on the horizontal edge (2,0)-(5,0);
  // (10,10) is in the unbounded face.
  CHECK_EQ(arr->locate(P(0, 0), PL_WALK).type, 0);
  CHECK_EQ(arr->locate(P(3, 0), PL_WALK).type, 1);
  Located outside = arr->locate(P(10, 10), PL_WALK);
  CHECK_EQ(outside.type, 2);
  CHECK(arr->face_is_unbounded(outside.as_face()));
  CHECK(outside.p == arr->unbounded_face().p);
  // ray shooting (simple / walk / trapezoid support it; naive does not)
  // from (1, 1/2), inside the upper-right quarter disk: shooting up hits the arc edge at
  // (1, sqrt(3)); shooting down hits the horizontal edge (0,0)-(2,0).
  Geom inner = o.make_point(R(1), R(1, 2));
  CHECK_EQ(arr->ray_shoot_up(inner, PL_WALK).type, 1);
  CHECK_EQ(arr->ray_shoot_down(inner, PL_WALK).type, 1);
  CHECK(arr->ray_shoot_up(inner, PL_WALK).p == arr->ray_shoot_up(inner, PL_SIMPLE).p ||
        arr->ray_shoot_up(inner, PL_WALK).p == arr->he_twin(arr->ray_shoot_up(inner, PL_SIMPLE).as_halfedge()).p);
  CHECK_THROWS(arr->ray_shoot_up(inner, PL_NAIVE), ErrorCode::Unsupported);
  for (int s : strategies) arr->detach_point_location(s);

  // ---- batched locate ----------------------------------------------------
  std::vector<Geom> queries = {P(1, 1), P(0, 0), P(10, 10), P(1, 1), P(-1, -1)};
  std::vector<Located> results;
  arr->batched_locate(queries, results);
  CHECK_EQ(results.size(), queries.size());
  if (results.size() == 5) {
    CHECK_EQ(results[0].type, 2);
    CHECK(results[0].p == ref.p);
    CHECK_EQ(results[1].type, 0);                       // the vertex (0,0)
    CHECK_EQ(results[2].type, 2);
    CHECK(arr->face_is_unbounded(results[2].as_face()));
    CHECK(results[3].p == ref.p);                       // duplicated query keeps its slot
    CHECK_EQ(results[4].type, 2);
    CHECK(!arr->face_is_unbounded(results[4].as_face()));
    CHECK(results[4].p != ref.p);                       // the opposite quarter disk
  }

  // ---- faces / ccb / directed curves -------------------------------------
  FH quarter = ref.as_face();
  CHECK(arr->face_has_outer_ccb(quarter));
  CHECK_EQ(arr->face_number_of_outer_ccbs(quarter), std::size_t(1));
  CHECK_EQ(arr->face_number_of_inner_ccbs(quarter), std::size_t(0));
  std::vector<HH> ccb;
  arr->he_ccb(arr->face_outer_ccb(quarter), ccb);
  // the quarter disk is bounded by: the segment (0,0)-(2,0), the arc (2,0)-(0,2)
  // and the segment (0,2)-(0,0)  => 3 halfedges.
  CHECK_EQ(ccb.size(), std::size_t(3));
  // he_directed_curve is oriented source -> target, and the targets chain around the ccb
  for (std::size_t i = 0; i < ccb.size(); ++i) {
    Geom dc = arr->he_directed_curve(ccb[i]);
    CHECK(points_equal(o.xcurve_source(dc), arr->vertex_point(arr->he_source(ccb[i]))));
    CHECK(points_equal(o.xcurve_target(dc), arr->vertex_point(arr->he_target(ccb[i]))));
    CHECK(arr->he_target(ccb[i]).p == arr->he_source(ccb[(i + 1) % ccb.size()]).p);
    CHECK(arr->he_face(ccb[i]).p == quarter.p);
    // he_curve() is the stored curve; it is the directed one or its opposite
    Geom sc = arr->he_curve(ccb[i]);
    CHECK(o.curve_equal(sc, dc));
  }
  std::vector<Geom> outer;
  std::vector<std::vector<Geom>> holes;
  arr->face_polygon(quarter, outer, holes);
  CHECK_EQ(outer.size(), std::size_t(3));
  CHECK_EQ(holes.size(), std::size_t(0));
  int n_arcs = 0, n_segs = 0;
  for (const Geom& g : outer) {
    if (circle_segment::is_circular(g)) ++n_arcs; else ++n_segs;
  }
  CHECK_EQ(n_arcs, 1);
  CHECK_EQ(n_segs, 2);
  // the boundary chains: target(i) == source(i+1)
  for (std::size_t i = 0; i < outer.size(); ++i)
    CHECK(points_equal(o.xcurve_target(outer[i]),
                       o.xcurve_source(outer[(i + 1) % outer.size()])));

  // the unbounded face has no outer ccb but exactly one hole (the figure is connected)
  FH uf = arr->unbounded_face();
  CHECK(arr->face_is_unbounded(uf));
  CHECK_EQ(arr->face_number_of_outer_ccbs(uf), std::size_t(0));
  CHECK_EQ(arr->face_number_of_inner_ccbs(uf), std::size_t(1));
  arr->face_polygon(uf, outer, holes);
  CHECK_EQ(outer.size(), std::size_t(0));
  CHECK_EQ(holes.size(), std::size_t(1));
  CHECK_THROWS(arr->face_outer_ccb(uf), ErrorCode::InvalidArgument);

  // ---- originating curves ------------------------------------------------
  Located on_edge = arr->locate(P(3, 0), PL_WALK);
  std::vector<CH> origs;
  arr->originating_curves(on_edge.as_halfedge(), origs);
  CHECK_EQ(origs.size(), std::size_t(1));
  CHECK(origs[0].p == ch_h.p);

  // ---- zone / do_intersect ----------------------------------------------
  // The segment (1/2,1/2) -> (3/2,3/2) starts inside the quarter disk (1/2 < 2) and leaves it
  // through the arc at (sqrt(2), sqrt(2)) (2 < 4.5): face, edge, face.
  Geom probe = circle_segment::make_segment(o.make_point(R(1, 2), R(1, 2)),
                                            o.make_point(R(3, 2), R(3, 2)));
  std::vector<Located> zone;
  arr->zone(probe, zone);
  CHECK_EQ(zone.size(), std::size_t(3));
  if (zone.size() == 3) {
    CHECK_EQ(zone[0].type, 2);
    CHECK(zone[0].p == quarter.p);
    CHECK_EQ(zone[1].type, 1);
    CHECK_EQ(zone[2].type, 2);
    CHECK(arr->face_is_unbounded(zone[2].as_face()));
  }
  CHECK(arr->do_intersect(probe));
  // entirely inside the quarter disk => no intersection
  Geom inside = circle_segment::make_segment(o.make_point(R(1, 4), R(1, 4)),
                                             o.make_point(R(1, 2), R(1, 2)));
  CHECK(!arr->do_intersect(inside));
  CHECK_EQ(arr->number_of_edges(), std::size_t(12));   // zone/do_intersect do not modify

  // ---- vertical decomposition -------------------------------------------
  std::vector<VerticalDecompositionEntry> dec;
  arr->decompose(dec);
  CHECK_EQ(dec.size(), std::size_t(9));                // one entry per vertex
  // the leftmost vertex is (-5,0); nothing is above or below it inside the arrangement
  if (!dec.empty()) {
    double vx, vy;
    approx_xy(arr->vertex_point(dec[0].v), vx, vy);
    CHECK_CLOSE(vx, -5.0, 0.0);
    CHECK_EQ(dec[0].below.type, 2);                    // the unbounded face
    CHECK_EQ(dec[0].above.type, 2);
  }

  // ---- bulk export -------------------------------------------------------
  std::vector<double> coords;
  arr->vertex_coordinates(coords);
  CHECK_EQ(coords.size(), std::size_t(18));            // 9 vertices x 2
  std::vector<std::size_t> idx;
  arr->edge_vertex_indices(idx);
  CHECK_EQ(idx.size(), std::size_t(24));               // 12 edges x 2
  std::vector<std::vector<std::vector<std::size_t>>> fb;
  arr->face_boundaries(fb);
  CHECK_EQ(fb.size(), std::size_t(5));

  // ---- clone -------------------------------------------------------------
  std::unique_ptr<ArrBase> copy = arr->clone();
  CHECK_EQ(copy->number_of_vertices(), std::size_t(9));
  CHECK_EQ(copy->number_of_edges(), std::size_t(12));
  CHECK_EQ(copy->number_of_faces(), std::size_t(5));
  CHECK_EQ(copy->number_of_curves(), std::size_t(3));
  CHECK(copy->is_valid());
  CHECK(!copy->vertex_valid(vs[0]));                   // handles do not cross arrangements
  CHECK(arr->vertex_valid(vs[0]));

  // ---- remove_curve ------------------------------------------------------
  // Removing the vertical segment deletes its 4 edges.  CGAL's _remove_curve uses
  // Base_arr::remove_edge(he) (remove_source = remove_target = true) and does NOT merge the
  // survivors: (0,3) and (0,-3) become isolated and are deleted, while (0,0), (0,2) and (0,-2)
  // keep degree 2.  => V = 7, E = 8, F = 3 (two half disks + the unbounded face);
  // Euler: 7 - 8 + 3 = 2.
  const std::size_t removed = arr->remove_curve(ch_v);
  CHECK_EQ(removed, std::size_t(4));
  CHECK_EQ(arr->number_of_vertices(), std::size_t(7));
  CHECK_EQ(arr->number_of_edges(), std::size_t(8));
  CHECK_EQ(arr->number_of_faces(), std::size_t(3));
  CHECK_EQ(arr->number_of_curves(), std::size_t(2));
  CHECK(arr->is_valid());
  CHECK(!arr->curve_valid(ch_v));
  CHECK_THROWS(arr->number_of_induced_edges(ch_v), ErrorCode::InvalidHandle);
  // the clone is untouched
  CHECK_EQ(copy->number_of_edges(), std::size_t(12));

  // ---- inserting an X_MONOTONE box goes through KindOps::to_curve --------
  std::unique_ptr<ArrBase> arr2 = make_arrangement(Kind::CircleSegment);
  std::vector<Geom> pieces;
  o.make_x_monotone(circle_segment::make_full_circle_r(R(0), R(0), R(2), +1), pieces);
  arr2->insert_curve(pieces[1]);                       // the upper half arc, as an XCurve box
  CHECK_EQ(arr2->number_of_vertices(), std::size_t(2));
  CHECK_EQ(arr2->number_of_edges(), std::size_t(1));
  CHECK_EQ(arr2->number_of_faces(), std::size_t(1));
  CHECK(arr2->is_valid());

  // ---- aggregate insertion ----------------------------------------------
  std::unique_ptr<ArrBase> arr3 = make_arrangement(Kind::CircleSegment);
  std::vector<Geom> batch = {circle_segment::make_full_circle_r(R(0), R(0), R(2), +1),
                             circle_segment::make_segment(P(-5, 0), P(5, 0)),
                             circle_segment::make_segment(P(0, -3), P(0, 3))};
  std::vector<CH> handles;
  arr3->insert_curves(batch, handles);
  CHECK_EQ(handles.size(), std::size_t(3));
  CHECK_EQ(arr3->number_of_vertices(), std::size_t(9));
  CHECK_EQ(arr3->number_of_edges(), std::size_t(12));
  CHECK_EQ(arr3->number_of_faces(), std::size_t(5));
  CHECK(arr3->is_valid());

  // ---- overlay of two circles -------------------------------------------
  // Circles of radius 2 at (0,0) and at (2,0) cross at (1, +-sqrt(3)).
  // Each arrangement alone: V=2 E=2 F=2.  The overlay: V = 2 + 2 + 2 = 6, E = 8,
  // F = 4 (lens, left crescent, right crescent, unbounded); Euler 6 - 8 + 4 = 2.
  std::unique_ptr<ArrBase> a = make_arrangement(Kind::CircleSegment);
  std::unique_ptr<ArrBase> b = make_arrangement(Kind::CircleSegment);
  std::unique_ptr<ArrBase> r = make_arrangement(Kind::CircleSegment);
  a->insert_curve(circle_segment::make_full_circle_r(R(0), R(0), R(2), +1));
  b->insert_curve(circle_segment::make_full_circle_r(R(2), R(0), R(2), +1));
  overlay(*a, *b, *r, nullptr, nullptr);
  CHECK_EQ(r->number_of_vertices(), std::size_t(6));
  CHECK_EQ(r->number_of_edges(), std::size_t(8));
  CHECK_EQ(r->number_of_faces(), std::size_t(4));
  CHECK_EQ(r->number_of_curves(), std::size_t(2));
  CHECK(r->is_valid());
}

// ===========================================================================
int main() {
  std::cout << "arr2d circle_segment kind test — " << build_info() << "\n";
  init_all_kinds();

  test_registry();
  test_points();
  test_curve_constructors();
  std::vector<Geom> circle_pieces;
  test_x_monotone(circle_pieces);
  test_approximate(circle_pieces);
  test_bbox(circle_pieces);
  test_convert_curve();
  test_traits_ops(circle_pieces);
  test_arrangement();

  std::cout << g_checks << " checks, " << g_failures << " failures\n";
  return g_failures == 0 ? 0 : 1;
}
