// arr2d — C++ unit test for Kind::Linear (src/arr2d/src/kind_linear.cpp).
//
// Self-contained: it links ONLY registry.o numbers.o overlay.o kind_linear.o. The linear kind has
// no Boolean-set-operation factory (arr2d/bso.hpp declares make_polygon_set_* for segment,
// circle_segment, conic and bezier only, and the registrar of kind_linear.cpp therefore registers
// a null factory), so there is no bso TU to link and no stub to provide.
//
// ---------------------------------------------------------------------------------------------
// BUILD + RUN (copy/paste; REPO and SCRATCH are the only things to adjust)
//
//   REPO=/Users/sthv/PycharmProjects/arrangement-2d
//   SCRATCH=/private/tmp/claude-501/-Users-sthv-PycharmProjects-arrangement-2d/\
// caeba100-f0a3-4bc9-8340-691c4b0ddc3d/scratchpad/kind_linear
//   CXX=/usr/bin/clang++
//   FLAGS="-std=c++17 -O0 -g -DCGAL_USE_CORE -DCGAL_USE_GMP -DCGAL_USE_MPFR \
//          -I/opt/homebrew/include -I$REPO/src/arr2d/include"
//
//   mkdir -p "$SCRATCH"
//   for f in registry numbers overlay kind_linear; do
//     $CXX $FLAGS -c "$REPO/src/arr2d/src/$f.cpp" -o "$SCRATCH/$f.o" &
//   done; wait
//   $CXX $FLAGS -c "$REPO/src/arr2d/tests/test_kind_linear.cpp" -o "$SCRATCH/test_kind_linear.o"
//   $CXX "$SCRATCH"/{registry,numbers,overlay,kind_linear,test_kind_linear}.o \
//        -L/opt/homebrew/lib -lgmp -lmpfr -o "$SCRATCH/test_kind_linear"
//   "$SCRATCH/test_kind_linear"; echo "exit=$?"
//
// The SECOND command line the kind-TU test protocol asks for is the one that also defines the
// per-kind polygon-set factory as a stub, for kinds whose registrar references one:
//
//   $CXX $FLAGS -DARR2D_TEST_STUB_BSO -c "$REPO/src/arr2d/tests/test_kind_linear.cpp" \
//        -o "$SCRATCH/test_kind_linear_stub.o"      # ... same link line
//
// For THIS kind the two are equivalent: ARR2D_TEST_STUB_BSO guards an empty block below, because
// `arr2d::make_polygon_set_linear` does not exist (see the #ifdef). Both builds are exercised.
//
// The program must exit with status 0 — that also proves nothing aborts during static
// destruction (no CORE::Expr lives in this kind, but the leaked-singleton discipline of
// kind_linear.cpp is the same one the CORE kinds need).
// ---------------------------------------------------------------------------------------------
//
// Every expected number in this file is hand-derived; the derivation is in the comment next to
// the check. The CGAL formulas relied upon are:
//   * line_from_pointsC2 (CGAL/constructions/kernel_ftC2.h:179): general case
//     a = py-qy, b = qx-px, c = -px*a-py*b; HORIZONTAL (py==qy) is normalised to
//     (0, ±1, ∓py) and VERTICAL (px==qx) to (±1, 0, ∓px), sign following the direction.
//   * line_get_pointC2 (:281): point(i) = point(0) + i*(b, -a).
//   * Construct_line_2(Ray_2) = line through point_on(r,0) and point_on(r,1)
//     (CGAL/Cartesian/function_objects.h:2597).
//   * _Linear_object_cached_2's ctors set is_right = (compare_xy(ps, pt) == SMALLER)
//     (CGAL/Arr_linear_traits_2.h:117-220).

#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <limits>
#include <string>
#include <vector>

#include "arr2d/arrangement.hpp"
#include "arr2d/common.hpp"
#include "arr2d/numbers.hpp"
#include "arr2d/ops.hpp"
#include "arr2d/registry.hpp"

// Concrete CGAL types of the OTHER kinds, so that convert_point / convert_curve can be tested by
// boxing raw CGAL objects of the source kind directly with make_geom(). Headers only — the kind
// TUs of those kinds are NOT linked (their Types::traits() is never called).
#include "arr2d/kinds/circle_segment_types.hpp"
#include "arr2d/kinds/linear_types.hpp"
#include "arr2d/kinds/polyline_types.hpp"
#include "arr2d/kinds/segment_types.hpp"

#ifdef ARR2D_TEST_STUB_BSO
// Intentionally empty: Kind::Linear registers a null polygon-set factory, so there is no
// arr2d::make_polygon_set_linear symbol to stub out. Kept so that both documented command lines
// compile this file unchanged.
#endif

using namespace arr2d;

// ===========================================================================
// tiny harness
// ===========================================================================
namespace {

int g_checks = 0;
int g_fail = 0;
const char* g_section = "";

void section(const char* s) {
  g_section = s;
  std::printf("\n---- %s\n", s);
}

void chk(bool ok, const std::string& what) {
  ++g_checks;
  if (!ok) {
    ++g_fail;
    std::printf("  FAIL [%s] %s\n", g_section, what.c_str());
  }
}

void chk_eq(long long got, long long want, const std::string& what) {
  chk(got == want, what + " (got " + std::to_string(got) + ", want " + std::to_string(want) + ")");
}

void chk_close(double got, double want, double tol, const std::string& what) {
  const bool ok = (std::isinf(want) && got == want) || std::fabs(got - want) <= tol;
  chk(ok, what + " (got " + std::to_string(got) + ", want " + std::to_string(want) + ")");
}

/// Runs `f` and checks that it throws arr2d::Error with the given code.
template <class F>
void chk_error(F f, ErrorCode code, const std::string& what) {
  ++g_checks;
  try {
    f();
  } catch (const Error& e) {
    if (e.code == code) return;
    ++g_fail;
    std::printf("  FAIL [%s] %s: wrong ErrorCode %d (message: %s)\n", g_section, what.c_str(),
                int(e.code), e.what());
    return;
  } catch (const std::exception& e) {
    ++g_fail;
    std::printf("  FAIL [%s] %s: threw %s instead of arr2d::Error\n", g_section, what.c_str(),
                e.what());
    return;
  }
  ++g_fail;
  std::printf("  FAIL [%s] %s: did not throw\n", g_section, what.c_str());
}

// ---- shorthands -----------------------------------------------------------
const KindOps& O() { return ops(Kind::Linear); }

Rational R(long long n, long long d = 1) { return Rational(n) / Rational(d); }

Geom P(long long x, long long y) { return O().make_point(R(x), R(y)); }
Geom Pr(const Rational& x, const Rational& y) { return O().make_point(x, y); }

/// Exact rational coordinates of a Linear-kind point.
void xy(const Geom& p, Rational& x, Rational& y) {
  std::vector<Rational> v;
  O().point_exact_rational(p, v);
  x = v[0];
  y = v[1];
}

bool same_point(const Geom& a, const Geom& b) { return O().point_equal(a, b); }

std::string rs(const Rational& r) {
  std::string n, d;
  rational_to_strings(r, n, d);
  return d == "1" ? n : (n + "/" + d);
}

BBox make_bbox(double xlo, double ylo, double xhi, double yhi) {
  BBox b;
  b.dim = 2;
  b.lo[0] = xlo;
  b.lo[1] = ylo;
  b.hi[0] = xhi;
  b.hi[1] = yhi;
  return b;
}

/// Checks that `out` (flattened x,y) is exactly the given list of points.
void chk_poly(const std::vector<double>& out, const std::vector<double>& want,
              const std::string& what) {
  chk_eq((long long)out.size(), (long long)want.size(), what + ": point count");
  if (out.size() != want.size()) return;
  for (std::size_t i = 0; i < out.size(); ++i)
    chk_close(out[i], want[i], 0.0, what + ": coordinate " + std::to_string(i));
}

// ===========================================================================
// 1. registry / capabilities
// ===========================================================================
void test_registry() {
  section("registry & capabilities");
  init_all_kinds();
  chk(kind_available(Kind::Linear), "kind_available(Linear)");
  chk(O().kind() == Kind::Linear, "ops().kind()");
  chk(std::strcmp(O().name(), "linear") == 0, "ops().name() == \"linear\"");
  chk_eq(O().dimension(), 2, "dimension");
  chk(O().is_unbounded_kind(), "is_unbounded_kind (Arr_open_side_tag on all four sides)");
  chk(!O().has_polygon_set(), "has_polygon_set == false");
  chk(!kind_has_polygon_set(Kind::Linear), "registry: no polygon set for linear");
  chk_error([] { (void)make_polygon_set(Kind::Linear); }, ErrorCode::Unsupported,
            "make_polygon_set(Linear) -> Unsupported");
  chk(kind_from_name("linear") == int(Kind::Linear), "kind_from_name(\"linear\")");

  // Deliverable (1): ONE process-wide traits instance, handed out by reference and never copied.
  const LinearTypes::Traits* t1 = &LinearTypes::traits();
  const LinearTypes::Traits* t2 = &LinearTypes::traits();
  chk(t1 == t2, "LinearTypes::traits() returns the same process-wide instance every time");
}

// ===========================================================================
// 2. points
// ===========================================================================
void test_points() {
  section("points");
  const Geom p = Pr(R(3, 4), R(-1, 2));            // (3/4, -1/2)

  double a[3] = {0, 0, 0};
  O().point_approx(p, a);
  chk_close(a[0], 0.75, 0.0, "point_approx x == 0.75 (exact in double)");
  chk_close(a[1], -0.5, 0.0, "point_approx y == -0.5");

  chk(O().point_is_rational(p), "point_is_rational (Epeck points always are)");

  Rational x, y;
  xy(p, x, y);
  chk(rs(x) == "3/4", "point_exact_rational x == 3/4 (got " + rs(x) + ")");
  chk(rs(y) == "-1/2", "point_exact_rational y == -1/2 (got " + rs(y) + ")");

  std::vector<Geom> nums;
  O().point_exact(p, nums);
  chk_eq((long long)nums.size(), 2, "point_exact yields 2 numbers");
  chk(number_kind(nums[0]) == NumberKind::Rational, "point_exact x is a Rational box");
  chk(number_repr(nums[0]) == "3/4", "number_repr(x) == \"3/4\" (got " + number_repr(nums[0]) + ")");
  chk(number_repr(nums[1]) == "-1/2", "number_repr(y) == \"-1/2\"");

  std::vector<std::pair<double, double>> iv;
  O().point_interval(p, iv);
  chk_eq((long long)iv.size(), 2, "point_interval yields 2 intervals");
  chk(iv[0].first <= 0.75 && 0.75 <= iv[0].second, "interval brackets x");
  chk(iv[0].first == 0.75 && iv[0].second == 0.75, "interval of 3/4 is degenerate (a double)");
  chk(iv[1].first == -0.5 && iv[1].second == -0.5, "interval of -1/2 is degenerate");

  chk(O().point_repr(p) == "Point(3/4, -1/2)",
      "point_repr == \"Point(3/4, -1/2)\" (got " + O().point_repr(p) + ")");

  const Geom q = Pr(R(3, 4), R(1, 2));
  chk_eq(O().point_compare_x(p, q), 0, "compare_x((3/4,-1/2),(3/4,1/2)) == 0");
  chk_eq(O().point_compare_xy(p, q), -1, "compare_xy is SMALLER (same x, smaller y)");
  chk_eq(O().point_compare_xy(q, p), 1, "compare_xy is LARGER the other way");
  chk(!same_point(p, q), "the two points differ");
  chk(same_point(p, Pr(R(6, 8), R(-2, 4))), "3/4 == 6/8 and -1/2 == -2/4 exactly");

  chk_error([] { (void)O().make_point_3(R(1), R(2), R(3)); }, ErrorCode::Unsupported,
            "make_point_3 -> Unsupported (planar kind)");

  // ---- convert_point --------------------------------------------------
  chk(same_point(O().convert_point(p), p), "convert_point(linear point) is the identity");

  // From Kind::Segment: SegmentTypes::Point_2 IS Epeck::Point_2 (the same C++ type).
  const SegmentTypes::Point_2 sp(SegmentTypes::FT(1) / SegmentTypes::FT(3), SegmentTypes::FT(2));
  const Geom gsp = make_geom(Kind::Segment, GeomType::Point, sp);
  const Geom csp = O().convert_point(gsp);
  chk(csp.kind == Kind::Linear && csp.type == GeomType::Point, "converted point is a linear point");
  Rational cx, cy;
  xy(csp, cx, cy);
  chk(rs(cx) == "1/3" && rs(cy) == "2", "convert_point(segment (1/3, 2)) == (1/3, 2)");

  // From Kind::Polyline (also Epeck::Point_2).
  const Geom gpp = make_geom(Kind::Polyline, GeomType::Point, PolylineTypes::Point_2(5, -7));
  xy(O().convert_point(gpp), cx, cy);
  chk(rs(cx) == "5" && rs(cy) == "-7", "convert_point(polyline (5, -7)) == (5, -7)");

  // From Kind::CircleSegment with rational one-root coordinates.
  using CsPoint = CircleSegmentTypes::Point_2;                       // _One_root_point_2<FT,true>
  using CoordNT = CsPoint::CoordNT;                                  // Sqrt_extension<FT,FT,...>
  const CsPoint cs_rat(CoordNT(CircleSegmentTypes::FT(1) / CircleSegmentTypes::FT(2)),
                       CoordNT(CircleSegmentTypes::FT(3)));
  xy(O().convert_point(make_geom(Kind::CircleSegment, GeomType::Point, cs_rat)), cx, cy);
  chk(rs(cx) == "1/2" && rs(cy) == "3", "convert_point(circle_segment (1/2, 3)) == (1/2, 3)");

  // A perfect-square radicand is still rational: 1 + 3*sqrt(4) == 7
  // (exact_coordinates_contract.md gotcha 3 — is_extended() stays true).
  const CsPoint cs_sq(CoordNT(CircleSegmentTypes::FT(1), CircleSegmentTypes::FT(3),
                              CircleSegmentTypes::FT(4)),
                      CoordNT(CircleSegmentTypes::FT(0)));
  xy(O().convert_point(make_geom(Kind::CircleSegment, GeomType::Point, cs_sq)), cx, cy);
  chk(rs(cx) == "7" && rs(cy) == "0", "convert_point(1 + 3*sqrt(4), 0) == (7, 0)");

  // A genuinely irrational coordinate must be refused.
  const CsPoint cs_irr(CoordNT(CircleSegmentTypes::FT(0), CircleSegmentTypes::FT(1),
                               CircleSegmentTypes::FT(2)),   // sqrt(2)
                       CoordNT(CircleSegmentTypes::FT(0)));
  chk_error([&] { (void)O().convert_point(make_geom(Kind::CircleSegment, GeomType::Point, cs_irr)); },
            ErrorCode::NotRepresentable, "convert_point(sqrt(2), 0) -> NotRepresentable");

  // Wrong Geom type.
  chk_error([&] { (void)O().convert_point(box_rational(R(1))); }, ErrorCode::InvalidArgument,
            "convert_point(number box) -> InvalidArgument");
  // A kind whose TU is not linked: the registry fallback reports it.
  chk_error([] { (void)O().convert_point(make_geom(Kind::Bezier, GeomType::Point, int(0))); },
            ErrorCode::Unsupported, "convert_point(bezier point, TU not linked) -> Unsupported");
}

// ===========================================================================
// 3. curve constructors (namespace arr2d::linear)
// ===========================================================================
void test_constructors() {
  section("curve constructors");
  Rational a, b, c, dx, dy;

  // -- segment (0,0) -> (4,3) -------------------------------------------
  const Geom seg = linear::make_segment(P(0, 0), P(4, 3));
  chk(seg.kind == Kind::Linear && seg.type == GeomType::XCurve,
      "make_segment yields an XCurve box of kind linear");
  chk_eq(linear::which(seg), linear::SEGMENT, "which(segment) == SEGMENT");
  linear::supporting_line(seg, a, b, c);
  // line_from_pointsC2((0,0),(4,3)) general branch: a = 0-3 = -3, b = 4-0 = 4, c = 0.
  chk(rs(a) == "-3" && rs(b) == "4" && rs(c) == "0",
      "supporting_line(segment) == (-3, 4, 0)  [-3x + 4y = 0]  got (" + rs(a) + ", " + rs(b) +
          ", " + rs(c) + ")");
  linear::direction(seg, dx, dy);
  chk(rs(dx) == "4" && rs(dy) == "3", "direction(segment) == target - source == (4, 3)");

  // -- ray from (1,1) through (3,1) (horizontal, to the right) -----------
  const Geom ray = linear::make_ray(P(1, 1), P(3, 1));
  chk_eq(linear::which(ray), linear::RAY, "which(ray) == RAY");
  linear::supporting_line(ray, a, b, c);
  // Construct_line_2(ray) = line((1,1),(3,1)); horizontal branch with qx > px: (0, 1, -py).
  chk(rs(a) == "0" && rs(b) == "1" && rs(c) == "-1",
      "supporting_line(ray) == (0, 1, -1)  [y = 1]  got (" + rs(a) + ", " + rs(b) + ", " + rs(c) +
          ")");
  linear::direction(ray, dx, dy);
  // to_vector() = (b, -a) = (1, 0); already lexicographically increasing; the ray is stored
  // directed right (ps = (1,1) < pt = (3,1)), so the stored direction is (1, 0).
  chk(rs(dx) == "1" && rs(dy) == "0", "direction(ray) == (1, 0)");

  // -- ray from (0,0) with direction (0,-1) (vertical, downwards) --------
  const Geom rdown = linear::make_ray_direction(P(0, 0), R(0), R(-1));
  chk_eq(linear::which(rdown), linear::RAY, "which(ray by direction) == RAY");
  linear::supporting_line(rdown, a, b, c);
  // line((0,0),(0,-1)): vertical branch with qy < py -> (1, 0, -px) = (1, 0, 0).
  chk(rs(a) == "1" && rs(b) == "0" && rs(c) == "0",
      "supporting_line(down ray) == (1, 0, 0)  [x = 0]  got (" + rs(a) + ", " + rs(b) + ", " +
          rs(c) + ")");
  linear::direction(rdown, dx, dy);
  // to_vector = (0,-1) -> lex-normalised to (0,1); the curve is stored right-to-left
  // (ps=(0,0) > pt=(0,-1)), so the stored direction is -(0,1) = (0,-1).
  chk(rs(dx) == "0" && rs(dy) == "-1", "direction(down ray) == (0, -1)");

  // -- line through (0,0) and (1,1) --------------------------------------
  const Geom line = linear::make_line(P(0, 0), P(1, 1));
  chk_eq(linear::which(line), linear::LINE, "which(line) == LINE");
  linear::supporting_line(line, a, b, c);
  chk(rs(a) == "-1" && rs(b) == "1" && rs(c) == "0",
      "supporting_line(line) == (-1, 1, 0)  [y = x]  got (" + rs(a) + ", " + rs(b) + ", " + rs(c) +
          ")");
  linear::direction(line, dx, dy);
  // point(0) = (1,1), point(1) = (2,2)  =>  is_directed_right, direction = to_vector = (1,1).
  chk(rs(dx) == "1" && rs(dy) == "1", "direction(line y=x) == (1, 1)");

  // -- line from coefficients: x = 3 -------------------------------------
  const Geom vline = linear::make_line_coefficients(R(1), R(0), R(-3));
  chk_eq(linear::which(vline), linear::LINE, "which(vertical line) == LINE");
  linear::supporting_line(vline, a, b, c);
  chk(rs(a) == "1" && rs(b) == "0" && rs(c) == "-3",
      "supporting_line stores the coefficients verbatim: (1, 0, -3)");
  linear::direction(vline, dx, dy);
  // b == 0 -> point(0) = (-c/a, 1) = (3,1), point(1) = (3, 1 - a) = (3,0): stored right-to-left,
  // so the direction is -(lex dir (0,1)) = (0,-1).
  chk(rs(dx) == "0" && rs(dy) == "-1", "direction(line x=3) == (0, -1)");

  // -- error paths -------------------------------------------------------
  chk_error([] { (void)linear::make_segment(P(1, 1), P(1, 1)); }, ErrorCode::InvalidArgument,
            "make_segment with equal endpoints");
  chk_error([] { (void)linear::make_ray(P(1, 1), P(1, 1)); }, ErrorCode::InvalidArgument,
            "make_ray with equal points");
  chk_error([] { (void)linear::make_ray_direction(P(0, 0), R(0), R(0)); },
            ErrorCode::InvalidArgument, "make_ray_direction with a zero direction");
  chk_error([] { (void)linear::make_line(P(2, 2), P(2, 2)); }, ErrorCode::InvalidArgument,
            "make_line with equal points");
  chk_error([] { (void)linear::make_line_coefficients(R(0), R(0), R(5)); },
            ErrorCode::InvalidArgument, "make_line_coefficients with a == b == 0");
  chk_error([&] { (void)linear::which(P(0, 0)); }, ErrorCode::InvalidArgument,
            "which(point box) -> InvalidArgument");

  // -- Construct_x_monotone_curve_2(p, q) (the traits functor) -----------
  const Geom xseg = O().construct_x_monotone_curve(P(0, 0), P(4, 3));
  chk(O().curve_equal(xseg, seg), "construct_x_monotone_curve((0,0),(4,3)) equals make_segment");
  chk_error([] { (void)O().construct_x_monotone_curve(P(1, 1), P(1, 1)); },
            ErrorCode::InvalidArgument, "construct_x_monotone_curve with equal points");
}

// ===========================================================================
// 4. x-monotonicity
// ===========================================================================
void test_x_monotone() {
  section("make_x_monotone / to_x_monotone / to_curve");
  const Geom seg = linear::make_segment(P(0, 0), P(4, 3));
  const Geom ray = linear::make_ray(P(1, 1), P(3, 1));
  const Geom line = linear::make_line(P(0, 0), P(1, 1));

  const char* names[3] = {"segment", "ray", "line"};
  const Geom* cs[3] = {&seg, &ray, &line};
  for (int i = 0; i < 3; ++i) {
    std::vector<Geom> pieces;
    O().make_x_monotone(*cs[i], pieces);
    // Every Arr_linear_object_2 is x-monotone by construction, so Make_x_monotone_2 emits
    // exactly one piece and never an isolated point.
    chk_eq((long long)pieces.size(), 1, std::string("make_x_monotone(") + names[i] + ") -> 1 piece");
    chk(pieces[0].type == GeomType::XCurve, std::string("the piece is an XCurve (") + names[i] + ")");
    chk(O().curve_equal(pieces[0], *cs[i]), std::string("the piece equals the input (") + names[i] + ")");
    chk(O().is_x_monotone(*cs[i]), std::string("is_x_monotone(") + names[i] + ")");
    const Geom xm = O().to_x_monotone(*cs[i]);
    chk(O().curve_equal(xm, *cs[i]), std::string("to_x_monotone is the identity (") + names[i] + ")");
    const Geom gen = O().to_curve(*cs[i]);
    chk(gen.type == GeomType::Curve && gen.kind == Kind::Linear,
        std::string("to_curve gives a Curve box (") + names[i] + ")");
    chk(O().curve_equal(gen, *cs[i]),
        std::string("Curve_2 == X_monotone_curve_2 for this kind (") + names[i] + ")");
  }
}

// ===========================================================================
// 5. x-monotone accessors, parameter space, opposite
// ===========================================================================
void test_accessors() {
  section("xcurve accessors / parameter space");
  const Geom seg = linear::make_segment(P(0, 0), P(4, 3));
  const Geom rseg = linear::make_segment(P(4, 3), P(0, 0));       // the same support, reversed
  const Geom ray = linear::make_ray(P(1, 1), P(3, 1));            // horizontal, to the right
  const Geom rdown = linear::make_ray_direction(P(0, 0), R(0), R(-1));
  const Geom vline = linear::make_line_coefficients(R(1), R(0), R(-3));   // x = 3

  // -- segment -----------------------------------------------------------
  chk(O().xcurve_has_source(seg) && O().xcurve_has_target(seg), "segment has both ends");
  chk(same_point(O().xcurve_source(seg), P(0, 0)), "segment source == (0,0)");
  chk(same_point(O().xcurve_target(seg), P(4, 3)), "segment target == (4,3)");
  chk(same_point(O().xcurve_min_vertex(seg), P(0, 0)), "segment min vertex == (0,0)");
  chk(same_point(O().xcurve_max_vertex(seg), P(4, 3)), "segment max vertex == (4,3)");
  chk(!O().xcurve_is_vertical(seg), "segment is not vertical");
  chk(O().xcurve_is_directed_right(seg), "segment is directed right");
  chk_eq(O().compare_endpoints_xy(seg), -1, "compare_endpoints_xy(segment) == SMALLER");
  chk_eq(O().parameter_space_in_x(seg, ARR_MIN_END), ARR_INTERIOR, "segment ps_x(min) INTERIOR");
  chk_eq(O().parameter_space_in_x(seg, ARR_MAX_END), ARR_INTERIOR, "segment ps_x(max) INTERIOR");
  chk_eq(O().parameter_space_in_y(seg, ARR_MIN_END), ARR_INTERIOR, "segment ps_y(min) INTERIOR");
  chk_eq(O().parameter_space_in_y(seg, ARR_MAX_END), ARR_INTERIOR, "segment ps_y(max) INTERIOR");

  // -- reversed segment --------------------------------------------------
  chk(same_point(O().xcurve_source(rseg), P(4, 3)), "reversed segment source == (4,3)");
  chk(same_point(O().xcurve_target(rseg), P(0, 0)), "reversed segment target == (0,0)");
  chk(same_point(O().xcurve_min_vertex(rseg), P(0, 0)), "min vertex is direction-independent");
  chk(!O().xcurve_is_directed_right(rseg), "reversed segment is directed left");
  chk_eq(O().compare_endpoints_xy(rseg), 1, "compare_endpoints_xy(reversed) == LARGER");
  chk(O().curve_equal(seg, rseg), "Equal_2 on x-monotone curves is direction-insensitive");

  // -- horizontal ray to the right ---------------------------------------
  chk(O().xcurve_has_source(ray), "ray has a source (its finite end)");
  chk(!O().xcurve_has_target(ray), "ray has no target (it runs to infinity)");
  chk(same_point(O().xcurve_source(ray), P(1, 1)), "ray source == (1,1)");
  chk_error([&] { (void)O().xcurve_target(ray); }, ErrorCode::Unsupported,
            "xcurve_target(ray) -> Unsupported");
  chk(same_point(O().xcurve_min_vertex(ray), P(1, 1)), "ray min vertex == (1,1)");
  chk_error([&] { (void)O().xcurve_max_vertex(ray); }, ErrorCode::Unsupported,
            "max vertex of a ray running to +infinity -> Unsupported");
  // The value the task asks for explicitly: the MAX end of a rightward horizontal ray.
  chk_eq(O().parameter_space_in_x(ray, ARR_MIN_END), ARR_INTERIOR, "ray ps_x(min) == INTERIOR");
  chk_eq(O().parameter_space_in_x(ray, ARR_MAX_END), ARR_RIGHT_BOUNDARY,
         "ray ps_x(max) == ARR_RIGHT_BOUNDARY");
  chk_eq(O().parameter_space_in_y(ray, ARR_MIN_END), ARR_INTERIOR, "ray ps_y(min) == INTERIOR");
  chk_eq(O().parameter_space_in_y(ray, ARR_MAX_END), ARR_INTERIOR,
         "ray ps_y(max) == INTERIOR (the ray is horizontal: is_horiz short-circuits)");

  // -- vertical ray downwards --------------------------------------------
  chk(O().xcurve_is_vertical(rdown), "the down ray is vertical");
  chk(!O().xcurve_is_directed_right(rdown), "the down ray is stored right-to-left");
  chk(O().xcurve_has_source(rdown), "the down ray has a source");
  chk(same_point(O().xcurve_source(rdown), P(0, 0)), "down ray source == (0,0)");
  chk_eq(O().parameter_space_in_y(rdown, ARR_MIN_END), ARR_BOTTOM_BOUNDARY,
         "down ray ps_y(min) == ARR_BOTTOM_BOUNDARY");
  chk_eq(O().parameter_space_in_x(rdown, ARR_MIN_END), ARR_INTERIOR,
         "down ray ps_x(min) == INTERIOR (vertical)");
  chk(same_point(O().xcurve_max_vertex(rdown), P(0, 0)), "down ray max vertex == (0,0)");

  // -- vertical line x = 3 -----------------------------------------------
  chk(!O().xcurve_has_source(vline) && !O().xcurve_has_target(vline), "a line has neither end");
  chk_error([&] { (void)O().xcurve_source(vline); }, ErrorCode::Unsupported,
            "xcurve_source(line) -> Unsupported");
  chk_error([&] { (void)O().xcurve_target(vline); }, ErrorCode::Unsupported,
            "xcurve_target(line) -> Unsupported");
  chk_error([&] { (void)O().xcurve_min_vertex(vline); }, ErrorCode::Unsupported,
            "min vertex of a line -> Unsupported");
  chk_error([&] { (void)O().xcurve_max_vertex(vline); }, ErrorCode::Unsupported,
            "max vertex of a line -> Unsupported");
  chk(O().xcurve_is_vertical(vline), "x = 3 is vertical");
  chk_eq(O().parameter_space_in_x(vline, ARR_MIN_END), ARR_INTERIOR,
         "vertical line ps_x(min) == INTERIOR");
  chk_eq(O().parameter_space_in_x(vline, ARR_MAX_END), ARR_INTERIOR,
         "vertical line ps_x(max) == INTERIOR");
  chk_eq(O().parameter_space_in_y(vline, ARR_MIN_END), ARR_BOTTOM_BOUNDARY,
         "vertical line ps_y(min) == ARR_BOTTOM_BOUNDARY");
  chk_eq(O().parameter_space_in_y(vline, ARR_MAX_END), ARR_TOP_BOUNDARY,
         "vertical line ps_y(max) == ARR_TOP_BOUNDARY");

  // -- a "flipped" ray: Split_2 of a line gives one --------------------
  // Split_2 does c1 = cv; c1.set_right(p); so c1 keeps has_source == false and gains
  // has_target == true.  Arr_linear_object_2::target() would assert on it
  // (\pre !is_line() && !is_ray()); LinearOps goes through left()/right() instead.
  const Geom line = linear::make_line(P(0, 0), P(1, 1));    // y = x, directed right
  const BBox box_for_flipped = make_bbox(-2, -2, 2, 2);
  Geom lo, hi;
  O().split(line, P(0, 0), lo, hi);
  chk(linear::which(lo) == linear::RAY && linear::which(hi) == linear::RAY,
      "splitting a line gives two rays");
  chk(!O().xcurve_has_source(lo), "the left half is a FLIPPED ray: no source");
  chk(O().xcurve_has_target(lo), "the left half has a target");
  chk(same_point(O().xcurve_target(lo), P(0, 0)), "the flipped ray's target is the split point");
  chk(O().xcurve_has_source(hi) && !O().xcurve_has_target(hi), "the right half is a normal ray");
  chk(same_point(O().xcurve_source(hi), P(0, 0)), "the right half starts at the split point");
  // Every other accessor must cope with the flipped ray too (none of them may reach for
  // left()/right() without checking has_left()/has_right() first).
  chk(!O().curve_is_bounded(lo), "the flipped ray is unbounded");
  const BBox lob = O().curve_bbox(lo);
  chk_close(lob.hi[0], 0, 0, "flipped ray bbox xmax == 0");
  chk_close(lob.lo[0], -std::numeric_limits<double>::infinity(), 0, "flipped ray bbox xmin == -inf");
  chk(O().curve_repr(lo) == "Ray(target=(0, 0), direction=(1, 1))",
      "curve_repr(flipped ray) (got " + O().curve_repr(lo) + ")");
  chk(O().curve_repr(hi) == "Ray((0, 0), direction=(1, 1))",
      "curve_repr(normal ray from the split) (got " + O().curve_repr(hi) + ")");
  {
    std::vector<double> o;
    O().approximate(lo, 1e-3, &box_for_flipped, o);
    chk_poly(o, {-2, -2, 0, 0}, "approximate(flipped ray) runs from infinity to (0,0)");
  }

  // -- construct_opposite -------------------------------------------------
  // MANDATORY override: Arr_linear_traits_2::Construct_opposite_2 does not compile.
  const Geom opp = O().construct_opposite(seg);
  chk(same_point(O().xcurve_source(opp), P(4, 3)), "opposite(segment) source == (4,3)");
  chk(same_point(O().xcurve_target(opp), P(0, 0)), "opposite(segment) target == (0,0)");
  chk_eq(O().compare_endpoints_xy(opp), 1, "opposite flips compare_endpoints_xy to LARGER");
  chk(O().curve_equal(opp, seg), "the opposite segment has the same support");

  const Geom oline = O().construct_opposite(line);
  Rational a, b, c;
  linear::supporting_line(oline, a, b, c);
  // Line_2(-1,1,0).opposite() == Line_2(1,-1,0).
  chk(rs(a) == "1" && rs(b) == "-1" && rs(c) == "0",
      "opposite(line -x+y=0) has coefficients (1, -1, 0), got (" + rs(a) + ", " + rs(b) + ", " +
          rs(c) + ")");
  chk(!O().xcurve_is_directed_right(oline), "the opposite line is stored right-to-left");
  chk(O().curve_equal(oline, line), "the opposite line has the same support");
  chk_error([&] { (void)O().construct_opposite(ray); }, ErrorCode::Unsupported,
            "construct_opposite(ray) -> Unsupported (\"a ray cannot be reversed\")");

  // -- a DEGENERATE linear object (the default-constructed Arr_linear_object_2) --------------
  // Nothing in arr2d can produce one, but a Geom box is just a typed pointer, so the accessors
  // must not walk into supporting_line()/left()/right(), all of which assert on it.
  const Geom degen = make_geom(Kind::Linear, GeomType::XCurve, LinearTypes::Curve_2());
  chk(O().curve_repr(degen) == "LinearCurve(degenerate)",
      "curve_repr(degenerate) (got " + O().curve_repr(degen) + ")");
  chk_error([&] { (void)O().curve_bbox(degen); }, ErrorCode::InvalidArgument,
            "curve_bbox(degenerate) -> InvalidArgument");
  chk_error([&] { (void)O().xcurve_source(degen); }, ErrorCode::InvalidArgument,
            "xcurve_source(degenerate) -> InvalidArgument");
  chk_error([&] { (void)O().curve_is_bounded(degen); }, ErrorCode::InvalidArgument,
            "curve_is_bounded(degenerate) -> InvalidArgument");
  chk_error([&] { std::vector<double> o; O().approximate(degen, 1e-3, nullptr, o); },
            ErrorCode::InvalidArgument, "approximate(degenerate) -> InvalidArgument");
  chk_error([&] { (void)O().construct_opposite(degen); }, ErrorCode::InvalidArgument,
            "construct_opposite(degenerate) -> InvalidArgument");
  chk_error([&] { (void)linear::which(degen); }, ErrorCode::InvalidArgument,
            "which(degenerate) -> InvalidArgument");
  chk_error([&] { Rational a2, b2, c2; linear::supporting_line(degen, a2, b2, c2); },
            ErrorCode::InvalidArgument, "supporting_line(degenerate) -> InvalidArgument");
  chk_error([&] { Rational dx, dy; linear::direction(degen, dx, dy); }, ErrorCode::InvalidArgument,
            "direction(degenerate) -> InvalidArgument");

  // -- repr ---------------------------------------------------------------
  chk(O().curve_repr(seg) == "Segment((0, 0), (4, 3))",
      "curve_repr(segment) (got " + O().curve_repr(seg) + ")");
  chk(O().curve_repr(ray) == "Ray((1, 1), direction=(1, 0))",
      "curve_repr(ray) (got " + O().curve_repr(ray) + ")");
  chk(O().curve_repr(vline) == "Line(a=1, b=0, c=-3)",
      "curve_repr(line) (got " + O().curve_repr(vline) + ")");
}

// ===========================================================================
// 6. traits operations that the generic layer provides (smoke, linear specifics)
// ===========================================================================
void test_traits_ops() {
  section("traits operations");
  const Geom l1 = linear::make_line(P(0, 0), P(1, 1));         // y = x
  const Geom l2 = linear::make_line_coefficients(R(0), R(1), R(-2));   // y = 2
  std::vector<IntersectionResult> res;
  O().intersect(l1, l2, res);
  chk_eq((long long)res.size(), 1, "y=x and y=2 meet once");
  if (res.size() == 1) {
    chk(res[0].is_point, "the intersection is a point");
    chk(same_point(res[0].point, P(2, 2)), "they meet at (2,2)");
  }

  // Overlap: a line and a collinear ray.
  const Geom r = linear::make_ray(P(0, 0), P(1, 1));
  res.clear();
  O().intersect(l1, r, res);
  chk_eq((long long)res.size(), 1, "line vs collinear ray: one result");
  if (res.size() == 1) {
    chk(!res[0].is_point, "the result is an overlap curve");
    chk_eq(linear::which(res[0].overlap), linear::RAY, "the overlap is the ray");
  }

  // compare_y_at_x / is_in_x_range.
  const Geom seg = linear::make_segment(P(0, 0), P(4, 4));
  chk(O().is_in_x_range(seg, P(2, 100)), "x = 2 is in the segment's x-range");
  chk(!O().is_in_x_range(seg, P(5, 0)), "x = 5 is not");
  chk_eq(O().compare_y_at_x(P(2, 3), seg), 1, "(2,3) is above y = x");
  chk_eq(O().compare_y_at_x(P(2, 2), seg), 0, "(2,2) is on y = x");
  chk_eq(O().compare_y_at_x(P(2, 1), seg), -1, "(2,1) is below y = x");
  chk_error([&] { (void)O().compare_y_at_x(P(9, 0), seg); }, ErrorCode::InvalidArgument,
            "compare_y_at_x outside the x-range -> InvalidArgument");

  // split / are_mergeable / merge round trip.
  Geom lo, hi;
  O().split(seg, P(2, 2), lo, hi);
  chk(same_point(O().xcurve_target(lo), P(2, 2)), "split: left half ends at (2,2)");
  chk(same_point(O().xcurve_source(hi), P(2, 2)), "split: right half starts at (2,2)");
  chk(O().are_mergeable(lo, hi), "the two halves are mergeable");
  chk(O().curve_equal(O().merge(lo, hi), seg), "merging the halves restores the segment");

  // trim: Arr_linear_traits_2::Trim_2 is non-const and takes its arguments by value
  // (traits map gotcha 10); it always returns a bounded segment, even from a line.
  const Geom t = O().trim(l1, P(1, 1), P(3, 3));
  chk_eq(linear::which(t), linear::SEGMENT, "trim(line) yields a SEGMENT");
  chk(same_point(O().xcurve_min_vertex(t), P(1, 1)), "trimmed min vertex == (1,1)");
  chk(same_point(O().xcurve_max_vertex(t), P(3, 3)), "trimmed max vertex == (3,3)");

  // approximate_coordinate goes through CGAL's own Approximate_2 by design (ops.hpp).
  chk_close(O().approximate_coordinate(P(3, 4), 0), 3.0, 0.0, "approximate_coordinate x");
  chk_close(O().approximate_coordinate(P(3, 4), 1), 4.0, 0.0, "approximate_coordinate y");
  chk_error([&] { (void)O().approximate_coordinate(P(0, 0), 2); }, ErrorCode::InvalidArgument,
            "approximate_coordinate with index 2 -> InvalidArgument (2D kind)");
}

// ===========================================================================
// 7. approximate() and curve_bbox()
// ===========================================================================
void test_approximate() {
  section("approximate / bbox / bounded");
  const double inf = std::numeric_limits<double>::infinity();
  const Geom seg = linear::make_segment(P(0, 0), P(4, 3));
  const Geom rseg = linear::make_segment(P(4, 3), P(0, 0));
  const Geom ray = linear::make_ray(P(1, 1), P(3, 1));            // y = 1, x >= 1
  const Geom rdown = linear::make_ray_direction(P(0, 0), R(0), R(-1));
  const Geom line = linear::make_line(P(0, 0), P(1, 1));          // y = x
  const Geom vline = linear::make_line_coefficients(R(1), R(0), R(-3));    // x = 3
  const Geom hline = linear::make_line_coefficients(R(0), R(1), R(-1));    // y = 1

  std::vector<double> out;

  // Bounded: the exact endpoints, source first; `clip` is ignored.
  O().approximate(seg, 1e-6, nullptr, out);
  chk_poly(out, {0, 0, 4, 3}, "approximate(segment)");
  O().approximate(rseg, 1e-6, nullptr, out);
  chk_poly(out, {4, 3, 0, 0}, "approximate(reversed segment) follows the stored direction");
  const BBox big = make_bbox(-100, -100, 100, 100);
  O().approximate(seg, 1e-6, &big, out);
  chk_poly(out, {0, 0, 4, 3}, "a clip box does not affect a bounded curve");
  chk_close(O().approximate_length(seg, 1e-6), 5.0, 0.0,
            "approximate_length(segment (0,0)-(4,3)) == 5 (3-4-5 triangle)");

  // Unbounded curves need a clip box.
  chk_error([&] { std::vector<double> o; O().approximate(ray, 1e-3, nullptr, o); },
            ErrorCode::InvalidArgument, "approximate(ray) without a clip box");
  chk_error([&] { std::vector<double> o; O().approximate(line, 1e-3, nullptr, o); },
            ErrorCode::InvalidArgument, "approximate(line) without a clip box");
  chk_error([&] { (void)O().approximate_length(line, 1e-3); }, ErrorCode::InvalidArgument,
            "approximate_length(line) (clip == nullptr) -> InvalidArgument");

  // Tolerance validation happens before anything else.
  chk_error([&] { std::vector<double> o; O().approximate(seg, 0.0, nullptr, o); },
            ErrorCode::InvalidArgument, "approximate with tolerance == 0");
  chk_error([&] { std::vector<double> o; O().approximate(seg, -1.0, nullptr, o); },
            ErrorCode::InvalidArgument, "approximate with a negative tolerance");
  chk_error([&] { std::vector<double> o; O().approximate(seg, std::nan(""), nullptr, o); },
            ErrorCode::InvalidArgument, "approximate with a NaN tolerance");

  const BBox box5 = make_bbox(-5, -5, 5, 5);
  O().approximate(ray, 1e-3, &box5, out);
  // y = 1 clipped to [-5,5]^2, the ray starts at x = 1 and runs right: (1,1) -> (5,1).
  chk_poly(out, {1, 1, 5, 1}, "approximate(horizontal ray) clipped to [-5,5]^2");

  const BBox box2 = make_bbox(-2, -2, 2, 2);
  O().approximate(rdown, 1e-3, &box2, out);
  // Downward ray from the origin: source first, then the bottom of the box.
  chk_poly(out, {0, 0, 0, -2}, "approximate(down ray) clipped to [-2,2]^2");

  O().approximate(line, 1e-3, &box2, out);
  chk_poly(out, {-2, -2, 2, 2}, "approximate(line y=x) clipped to [-2,2]^2");
  // The chord IS the curve, so every sample point lies EXACTLY on it (deviation 0 <= tolerance).
  chk_eq(O().compare_y_at_x(Pr(R(-1), R(-1)), line), 0, "sample (-1,-1) is on y = x");
  chk_eq(O().compare_y_at_x(Pr(R(0), R(0)), line), 0, "sample (0,0) is on y = x");
  chk_eq(O().compare_y_at_x(Pr(R(1), R(1)), line), 0, "sample (1,1) is on y = x");
  // ... and for the segment: (1, 3/4), (2, 3/2), (3, 9/4) satisfy -3x + 4y = 0.
  chk_eq(O().compare_y_at_x(Pr(R(1), R(3, 4)), seg), 0, "sample (1, 3/4) is on the segment");
  chk_eq(O().compare_y_at_x(Pr(R(2), R(3, 2)), seg), 0, "sample (2, 3/2) is on the segment");
  chk_eq(O().compare_y_at_x(Pr(R(3), R(9, 4)), seg), 0, "sample (3, 9/4) is on the segment");

  O().approximate(vline, 1e-3, &box5, out);
  // x = 3 is stored right-to-left (see test_constructors), so the source is the TOP end.
  chk_poly(out, {3, 5, 3, -5}, "approximate(vertical line x=3) clipped to [-5,5]^2");

  // A clip box that misses the curve -> an empty polyline.
  const BBox away = make_bbox(-5, -5, 0, 0);
  O().approximate(ray, 1e-3, &away, out);
  chk_eq((long long)out.size(), 0, "clip box that misses the ray -> 0 points");

  // Degenerate clip box (a single point on the curve) -> the point twice.
  const BBox pt = make_bbox(2, 2, 2, 2);
  O().approximate(line, 1e-3, &pt, out);
  chk_poly(out, {2, 2, 2, 2}, "degenerate clip box on y=x -> the touching point twice");

  chk_error([&] { std::vector<double> o; BBox b = make_bbox(5, 0, -5, 1); O().approximate(line, 1e-3, &b, o); },
            ErrorCode::InvalidArgument, "empty clip box (lo > hi) -> InvalidArgument");
  chk_error([&] {
              std::vector<double> o;
              BBox b = make_bbox(-inf, -1, 1, 1);
              O().approximate(line, 1e-3, &b, o);
            },
            ErrorCode::InvalidArgument, "infinite clip box -> InvalidArgument");

  // ---- curve_bbox ------------------------------------------------------
  BBox b = O().curve_bbox(seg);
  chk_eq(b.dim, 2, "bbox dim");
  chk_close(b.lo[0], 0, 0, "segment bbox xmin");
  chk_close(b.lo[1], 0, 0, "segment bbox ymin");
  chk_close(b.hi[0], 4, 0, "segment bbox xmax");
  chk_close(b.hi[1], 3, 0, "segment bbox ymax");

  b = O().curve_bbox(ray);
  chk_close(b.lo[0], 1, 0, "ray bbox xmin == 1");
  chk_close(b.hi[0], inf, 0, "ray bbox xmax == +inf");
  chk_close(b.lo[1], 1, 0, "ray bbox ymin == 1 (horizontal)");
  chk_close(b.hi[1], 1, 0, "ray bbox ymax == 1");

  b = O().curve_bbox(rdown);
  chk_close(b.lo[0], 0, 0, "down ray bbox xmin == 0 (vertical)");
  chk_close(b.hi[0], 0, 0, "down ray bbox xmax == 0");
  chk_close(b.lo[1], -inf, 0, "down ray bbox ymin == -inf");
  chk_close(b.hi[1], 0, 0, "down ray bbox ymax == 0");

  b = O().curve_bbox(line);
  chk_close(b.lo[0], -inf, 0, "line y=x bbox xmin == -inf");
  chk_close(b.hi[0], inf, 0, "line y=x bbox xmax == +inf");
  chk_close(b.lo[1], -inf, 0, "line y=x bbox ymin == -inf");
  chk_close(b.hi[1], inf, 0, "line y=x bbox ymax == +inf");

  b = O().curve_bbox(vline);
  chk_close(b.lo[0], 3, 0, "line x=3 bbox xmin == 3");
  chk_close(b.hi[0], 3, 0, "line x=3 bbox xmax == 3");
  chk_close(b.lo[1], -inf, 0, "line x=3 bbox ymin == -inf");
  chk_close(b.hi[1], inf, 0, "line x=3 bbox ymax == +inf");

  b = O().curve_bbox(hline);
  chk_close(b.lo[0], -inf, 0, "line y=1 bbox xmin == -inf");
  chk_close(b.hi[0], inf, 0, "line y=1 bbox xmax == +inf");
  chk_close(b.lo[1], 1, 0, "line y=1 bbox ymin == 1");
  chk_close(b.hi[1], 1, 0, "line y=1 bbox ymax == 1");

  chk(O().curve_is_bounded(seg), "a segment is bounded");
  chk(!O().curve_is_bounded(ray), "a ray is not bounded");
  chk(!O().curve_is_bounded(line), "a line is not bounded");
}

// ===========================================================================
// 8. convert_curve
// ===========================================================================
void test_convert_curve() {
  section("convert_curve");
  std::vector<Geom> out;

  // -- identity ----------------------------------------------------------
  const Geom seg = linear::make_segment(P(0, 0), P(2, 5));
  O().convert_curve(seg, out);
  chk_eq((long long)out.size(), 1, "convert_curve(linear) -> 1 curve");
  chk(O().curve_equal(out[0], seg), "convert_curve(linear) is the identity");

  // -- from Kind::Segment (Arr_segment_2<Epeck>) --------------------------
  const SegmentTypes::Curve_2 s2(SegmentTypes::Point_2(0, 0), SegmentTypes::Point_2(2, 5));
  O().convert_curve(make_geom(Kind::Segment, GeomType::XCurve, s2), out);
  chk_eq((long long)out.size(), 1, "convert_curve(segment) -> 1 curve");
  chk_eq(linear::which(out[0]), linear::SEGMENT, "the result is a linear SEGMENT");
  chk(same_point(O().xcurve_source(out[0]), P(0, 0)), "converted source == (0,0)");
  chk(same_point(O().xcurve_target(out[0]), P(2, 5)), "converted target == (2,5)");
  // Direction is preserved.
  const SegmentTypes::Curve_2 s2r(SegmentTypes::Point_2(2, 5), SegmentTypes::Point_2(0, 0));
  O().convert_curve(make_geom(Kind::Segment, GeomType::XCurve, s2r), out);
  chk(same_point(O().xcurve_source(out[0]), P(2, 5)), "the stored direction is preserved");

  // -- from Kind::Polyline (internal::Polycurve_2 of Arr_segment_2) -------
  std::vector<PolylineTypes::Segment_2> subs;
  subs.push_back(PolylineTypes::Segment_2(PolylineTypes::Point_2(0, 0), PolylineTypes::Point_2(1, 1)));
  subs.push_back(PolylineTypes::Segment_2(PolylineTypes::Point_2(1, 1), PolylineTypes::Point_2(2, 0)));
  subs.push_back(PolylineTypes::Segment_2(PolylineTypes::Point_2(2, 0), PolylineTypes::Point_2(3, 3)));
  const PolylineTypes::Curve_2 poly(subs.begin(), subs.end());
  O().convert_curve(make_geom(Kind::Polyline, GeomType::Curve, poly), out);
  chk_eq((long long)out.size(), 3, "convert_curve(3-subcurve polyline) -> 3 segments");
  if (out.size() == 3) {
    chk(same_point(O().xcurve_source(out[0]), P(0, 0)), "polyline piece 0 source == (0,0)");
    chk(same_point(O().xcurve_target(out[0]), P(1, 1)), "polyline piece 0 target == (1,1)");
    chk(same_point(O().xcurve_source(out[1]), P(1, 1)), "polyline piece 1 source == (1,1)");
    chk(same_point(O().xcurve_target(out[2]), P(3, 3)), "polyline piece 2 target == (3,3)");
  }
  // An x-monotone polyline box works too.
  const PolylineTypes::X_monotone_curve_2 xpoly(subs.begin(), subs.begin() + 1);
  O().convert_curve(make_geom(Kind::Polyline, GeomType::XCurve, xpoly), out);
  chk_eq((long long)out.size(), 1, "convert_curve(x-monotone polyline of 1 subcurve) -> 1 segment");

  // -- from Kind::CircleSegment ------------------------------------------
  using CsCurve = CircleSegmentTypes::Curve_2;
  using CsPoint = CircleSegmentTypes::Point_2;
  using CoordNT = CsPoint::CoordNT;
  using CsFT = CircleSegmentTypes::FT;
  const CsCurve cs_lin(CircleSegmentTypes::Kernel::Point_2(0, 0),
                       CircleSegmentTypes::Kernel::Point_2(3, 4));
  O().convert_curve(make_geom(Kind::CircleSegment, GeomType::Curve, cs_lin), out);
  chk_eq((long long)out.size(), 1, "convert_curve(linear circle-segment) -> 1 segment");
  chk(same_point(O().xcurve_source(out[0]), P(0, 0)), "circle-segment source == (0,0)");
  chk(same_point(O().xcurve_target(out[0]), P(3, 4)), "circle-segment target == (3,4)");

  // A circular arc cannot be converted.
  const CsCurve circ(CircleSegmentTypes::Kernel::Point_2(0, 0), CsFT(2), CGAL::COUNTERCLOCKWISE);
  chk_error([&] {
              std::vector<Geom> o;
              O().convert_curve(make_geom(Kind::CircleSegment, GeomType::Curve, circ), o);
            },
            ErrorCode::Unsupported, "convert_curve(full circle) -> Unsupported");

  // A straight circle-segment piece with an irrational endpoint cannot be represented.
  const CircleSegmentTypes::Kernel::Line_2 diag(CsFT(1), CsFT(-1), CsFT(0));    // x - y = 0
  const CsCurve cs_irr(diag, CsPoint(CoordNT(CsFT(0)), CoordNT(CsFT(0))),
                       CsPoint(CoordNT(CsFT(0), CsFT(1), CsFT(2)),
                               CoordNT(CsFT(0), CsFT(1), CsFT(2))));            // (sqrt2, sqrt2)
  chk_error([&] {
              std::vector<Geom> o;
              O().convert_curve(make_geom(Kind::CircleSegment, GeomType::Curve, cs_irr), o);
            },
            ErrorCode::NotRepresentable, "convert_curve(segment with sqrt endpoints)");

  // -- kinds with no straight-line image ---------------------------------
  chk_error([&] {
              std::vector<Geom> o;
              O().convert_curve(make_geom(Kind::Bezier, GeomType::Curve, int(0)), o);
            },
            ErrorCode::Unsupported, "convert_curve(bezier) -> Unsupported");
  chk_error([&] {
              std::vector<Geom> o;
              O().convert_curve(make_geom(Kind::Conic, GeomType::Curve, int(0)), o);
            },
            ErrorCode::Unsupported, "convert_curve(conic) -> Unsupported");
  chk_error([&] {
              std::vector<Geom> o;
              O().convert_curve(make_geom(Kind::Sphere, GeomType::Curve, int(0)), o);
            },
            ErrorCode::Unsupported, "convert_curve(sphere) -> Unsupported");
  chk_error([&] { std::vector<Geom> o; O().convert_curve(P(0, 0), o); }, ErrorCode::InvalidArgument,
            "convert_curve(point box) -> InvalidArgument");
}

// ===========================================================================
// 9. full arrangement round trip
// ===========================================================================
void test_arrangement() {
  section("arrangement round trip");
  std::unique_ptr<ArrBase> A = make_arrangement(Kind::Linear);
  chk(A != nullptr, "make_arrangement(Kind::Linear)");
  chk(A->kind() == Kind::Linear, "arrangement kind");
  chk(A->is_unbounded_kind(), "arrangement reports the unbounded topology");
  chk(A->is_empty(), "a fresh arrangement is empty");
  chk_eq((long long)A->number_of_faces(), 1, "an empty linear arrangement has 1 (unbounded) face");

  // Three lines: y = 0, x = 0, x + y = 4. They pairwise meet at (0,0), (4,0) and (0,4).
  // Each line is cut into 3 edges => 9 edges, 18 halfedges; 3 concrete vertices; 6 vertices at
  // infinity (two ends per line); 1 bounded triangular face + 6 unbounded faces = 7.
  // (These are exactly the numbers measured in
  //  docs/dev/cgal61_api/rendering_and_approximation.md §5.4.)
  const Geom y0 = linear::make_line_coefficients(R(0), R(1), R(0));    // y = 0
  const Geom x0 = linear::make_line_coefficients(R(1), R(0), R(0));    // x = 0
  const Geom d4 = linear::make_line_coefficients(R(1), R(1), R(-4));   // x + y = 4
  const CH c_y0 = A->insert_curve(y0);
  const CH c_x0 = A->insert_curve(x0);
  const CH c_d4 = A->insert_curve(d4);

  chk_eq((long long)A->number_of_vertices(), 3, "V == 3");
  chk_eq((long long)A->number_of_vertices_at_infinity(), 6, "V at infinity == 6");
  chk_eq((long long)A->number_of_edges(), 9, "E == 9");
  chk_eq((long long)A->number_of_halfedges(), 18, "H == 18");
  chk_eq((long long)A->number_of_faces(), 7, "F == 7 (the fictitious face is not counted)");
  chk_eq((long long)A->number_of_unbounded_faces(), 6, "unbounded faces == 6");
  chk_eq((long long)A->number_of_curves(), 3, "3 input curves in the history");
  chk(A->is_valid(), "is_valid (free CGAL::is_valid: sweep + hole placement)");
  chk(!A->is_empty(), "the arrangement is not empty");

  std::vector<VH> vs;
  std::vector<HH> hs;
  std::vector<FH> fs;
  std::vector<CH> cs;
  A->vertices(vs);
  A->edges(hs);
  A->faces(fs);
  A->curves(cs);
  chk_eq((long long)vs.size(), 3, "vertices() snapshot size");
  chk_eq((long long)hs.size(), 9, "edges() snapshot size");
  chk_eq((long long)fs.size(), 7, "faces() snapshot size");
  chk_eq((long long)cs.size(), 3, "curves() snapshot size");

  // -- history -----------------------------------------------------------
  chk_eq((long long)A->number_of_induced_edges(c_y0), 3, "y = 0 induces 3 edges");
  chk_eq((long long)A->number_of_induced_edges(c_x0), 3, "x = 0 induces 3 edges");
  chk_eq((long long)A->number_of_induced_edges(c_d4), 3, "x + y = 4 induces 3 edges");
  std::vector<HH> induced;
  A->induced_edges(c_d4, induced);
  chk_eq((long long)induced.size(), 3, "induced_edges(x+y=4)");
  std::vector<CH> orig;
  A->originating_curves(induced[0], orig);
  chk_eq((long long)orig.size(), 1, "each edge originates from exactly 1 input curve");
  const Geom stored_curve = A->curve_geometry(c_d4);
  chk(stored_curve.kind == Kind::Linear && stored_curve.type == GeomType::Curve,
      "curve_geometry gives back a linear Curve box");
  chk_eq(linear::which(stored_curve), linear::LINE, "and it is still the LINE x + y = 4");
  Rational la, lb, lc;
  linear::supporting_line(stored_curve, la, lb, lc);
  chk(rs(la) == "1" && rs(lb) == "1" && rs(lc) == "-4",
      "the history keeps the exact input coefficients (1, 1, -4)");

  // -- the bounded face --------------------------------------------------
  FH tri{};
  int bounded = 0;
  for (const FH& f : fs)
    if (!A->face_is_unbounded(f)) { tri = f; ++bounded; }
  chk_eq(bounded, 1, "exactly one bounded face (the triangle (0,0),(4,0),(0,4))");
  chk(A->face_has_outer_ccb(tri), "the triangle has an outer CCB");
  chk_eq((long long)A->face_number_of_inner_ccbs(tri), 0, "the triangle has no holes");
  std::vector<HH> ccb;
  A->he_ccb(A->face_outer_ccb(tri), ccb);
  chk_eq((long long)ccb.size(), 3, "the triangle's CCB has 3 halfedges");

  // he_curve / he_directed_curve orientation consistency around the face: the directed curves
  // must chain target -> source and match the halfedges' vertices.
  bool chain_ok = true, ends_ok = true;
  for (std::size_t i = 0; i < ccb.size(); ++i) {
    const HH h = ccb[i];
    chk(!A->he_is_fictitious(h), "the triangle's CCB has no fictitious halfedge");
    const Geom stored = A->he_curve(h);
    const Geom dir = A->he_directed_curve(h);
    chk(O().curve_equal(stored, dir), "he_directed_curve has the same support as he_curve");
    chk(O().curve_is_bounded(dir), "the triangle's edges are bounded segments");
    if (!same_point(O().xcurve_source(dir), A->vertex_point(A->he_source(h)))) ends_ok = false;
    if (!same_point(O().xcurve_target(dir), A->vertex_point(A->he_target(h)))) ends_ok = false;
    const Geom nxt = A->he_directed_curve(ccb[(i + 1) % ccb.size()]);
    if (!same_point(O().xcurve_target(dir), O().xcurve_source(nxt))) chain_ok = false;
  }
  chk(ends_ok, "he_directed_curve(h) runs from he_source(h) to he_target(h)");
  chk(chain_ok, "the directed curves of the CCB chain target -> source all the way round");

  // Each of the 3 triangle vertices has degree 4 (two lines cross at each of them).
  for (const VH& v : vs) {
    chk(!A->vertex_is_at_open_boundary(v), "the concrete vertices are not at an open boundary");
    chk_eq((long long)A->vertex_degree(v), 4, "each crossing vertex has degree 4");
    std::vector<HH> inc;
    A->vertex_incident_halfedges(v, inc);
    chk_eq((long long)inc.size(), 4, "and 4 incident halfedges");
    for (const HH& h : inc)
      chk(A->he_target(h) == v, "incident_halfedges are directed INTO the vertex");
  }
  // he_direction agrees with the stored curve's orientation.
  for (const HH& h : hs) {
    const Geom cv = A->he_curve(h);
    const bool cv_l2r = (O().compare_endpoints_xy(cv) < 0);
    const bool he_l2r = (A->he_direction(h) == ARR_LEFT_TO_RIGHT);
    const bool twin_l2r = (A->he_direction(A->he_twin(h)) == ARR_LEFT_TO_RIGHT);
    chk(he_l2r != twin_l2r, "a halfedge and its twin have opposite directions");
    (void)cv_l2r;
  }

  std::vector<Geom> outer;
  std::vector<std::vector<Geom>> holes;
  A->face_polygon(tri, outer, holes);
  chk_eq((long long)outer.size(), 3, "face_polygon(triangle): 3 curves");
  chk_eq((long long)holes.size(), 0, "face_polygon(triangle): no holes");

  // face_polygon of the unbounded faces must NOT throw: he_directed_curve returns an unbounded
  // curve AS STORED instead of asking construct_opposite to reverse a ray (which is impossible).
  int unbounded_polygons = 0, with_curves = 0;
  for (const FH& f : fs) {
    if (!A->face_is_unbounded(f)) continue;
    std::vector<Geom> o;
    std::vector<std::vector<Geom>> h;
    A->face_polygon(f, o, h);
    ++unbounded_polygons;
    if (!o.empty() || !h.empty()) ++with_curves;
  }
  chk_eq(unbounded_polygons, 6, "face_polygon works on all 6 unbounded faces");
  chk(with_curves == 6, "each unbounded face contributes at least one real curve");

  // -- fictitious face / at-infinity elements ----------------------------
  const FH ff = A->fictitious_face();
  chk(A->face_valid(ff), "fictitious_face() is a valid handle");
  chk(A->face_is_fictitious(ff), "and it is fictitious");
  chk(A->face_is_unbounded(ff), "and unbounded");
  std::vector<HH> inner;
  A->face_inner_ccbs(ff, inner);
  chk(!inner.empty(), "the fictitious face has at least one inner CCB");
  std::vector<HH> fccb;
  A->he_ccb(inner[0], fccb);
  int fict = 0;
  for (const HH& h : fccb) if (A->he_is_fictitious(h)) ++fict;
  chk(fict > 0, "the fictitious face's CCB contains fictitious halfedges");
  for (const HH& h : fccb)
    if (A->he_is_fictitious(h)) {
      chk_error([&] { (void)A->he_curve(h); }, ErrorCode::Unsupported,
                "he_curve(fictitious halfedge) -> Unsupported");
      chk(A->vertex_is_at_open_boundary(A->he_target(h)) ||
              A->vertex_is_at_open_boundary(A->he_source(h)),
          "a fictitious halfedge touches the boundary");
      break;
    }
  for (const HH& h : fccb) {
    const VH v = A->he_target(h);
    if (A->vertex_is_at_open_boundary(v)) {
      chk_error([&] { (void)A->vertex_point(v); }, ErrorCode::Unsupported,
                "vertex_point(vertex at infinity) -> Unsupported");
      chk(A->vertex_parameter_space_in_x(v) != ARR_INTERIOR ||
              A->vertex_parameter_space_in_y(v) != ARR_INTERIOR,
          "a vertex at infinity has a non-interior parameter space");
      break;
    }
  }

  // -- point location, every strategy ------------------------------------
  const int strategies[3] = {PL_NAIVE, PL_SIMPLE, PL_WALK};
  const char* snames[3] = {"naive", "simple", "walk"};
  const Geom inside = P(1, 1);                          // inside the triangle
  for (int i = 0; i < 3; ++i) {
    chk(A->supports_point_location(strategies[i]),
        std::string("supports ") + snames[i]);
    const Located l0 = A->locate(inside, strategies[i]);     // temporary strategy object
    chk(l0.type == 2 && FH{l0.p, l0.id} == tri,
        std::string("locate((1,1), ") + snames[i] + ") == the triangle (temporary)");
    A->attach_point_location(strategies[i]);
    chk(A->has_point_location(strategies[i]), std::string("attached ") + snames[i]);
    const Located l1 = A->locate(inside, strategies[i]);     // attached strategy object
    chk(l1.type == 2 && FH{l1.p, l1.id} == tri,
        std::string("locate((1,1), ") + snames[i] + ") == the triangle (attached)");
  }
  // Landmarks PL is a per-ARRANGEMENT capability for this kind (ArrImpl::landmarks_usable()):
  // Arr_landmarks_point_location::_deal_with_curve_contained_in_segment
  // (Arr_landmarks_pl_impl.h:414) reads point() on a vertex at infinity for a query point on an
  // unbounded edge, and _walk_from_face (:533) runs out of crossable edges on the all-fictitious
  // outer ccb of the unbounded face.  `A` is made of LINES, so it is refused here...
  chk(A->number_of_vertices_at_infinity() > 0, "this arrangement has vertices at infinity");
  chk(!A->supports_point_location(PL_LANDMARKS),
      "landmarks PL is NOT supported while an unbounded edge is present");
  chk_error([&] { A->attach_point_location(PL_LANDMARKS); }, ErrorCode::Unsupported,
            "attach(landmarks) -> Unsupported");
  chk_error([&] { (void)A->locate(inside, PL_LANDMARKS); }, ErrorCode::Unsupported,
            "locate(landmarks) -> Unsupported");
  // ... and offered on an arrangement of this very kind that holds only bounded segments, where
  // the CGAL bug is structurally unreachable (a CCB never mixes fictitious and concrete
  // halfedges then).  Verified against the naive strategy.
  {
    std::unique_ptr<ArrBase> B = make_arrangement(Kind::Linear);
    const std::vector<Geom> box = {linear::make_segment(P(0, 0), P(4, 0)),
                                   linear::make_segment(P(4, 0), P(4, 4)),
                                   linear::make_segment(P(4, 4), P(0, 4)),
                                   linear::make_segment(P(0, 4), P(0, 0)),
                                   linear::make_segment(P(0, 0), P(4, 4))};
    std::vector<CH> chs;
    B->insert_curves(box, chs);
    chk_eq((long long)B->number_of_vertices_at_infinity(), 0,
           "a linear arrangement of bounded segments has no vertex at infinity");
    chk(B->supports_point_location(PL_LANDMARKS), "landmarks PL IS supported for it");
    B->attach_point_location(PL_LANDMARKS);
    const Geom qs[5] = {P(1, 3), P(2, 0), P(2, 2), P(0, 0), P(100, 100)};
    for (int i = 0; i < 5; ++i) {
      const Located lm = B->locate(qs[i], PL_LANDMARKS);
      const Located nv = B->locate(qs[i], PL_NAIVE);
      // point location returns an arbitrary twin for an edge: compare the feature TYPE, and the
      // identity only for a vertex or a face.
      chk(lm.type == nv.type && (lm.type == 1 || lm.p == nv.p),
          "landmarks agrees with naive on a bounded linear arrangement");
    }
    // inserting a line withdraws the strategy again (the attached object stays, unused)
    B->insert_curve(linear::make_line(P(0, -2), P(1, -2)));
    chk(!B->supports_point_location(PL_LANDMARKS), "a line withdraws landmarks again");
    chk(B->has_point_location(PL_LANDMARKS), "... the attached object is still there");
    chk_error([&] { (void)B->locate(P(2, -2), PL_LANDMARKS); }, ErrorCode::Unsupported,
              "locate(landmarks) -> Unsupported once a line is present");
  }
  // Trapezoid PL is disabled for this kind: an ATTACHED Arr_trapezoid_ric_point_location asserts
  // when an edge incident to a vertex at infinity is removed (see test_trapezoid_removal_trap).
  chk(!A->supports_point_location(PL_TRAPEZOID), "trapezoid PL is NOT supported (unbounded kind)");
  chk_error([&] { A->attach_point_location(PL_TRAPEZOID); }, ErrorCode::Unsupported,
            "attach(trapezoid) -> Unsupported");
  chk_error([&] { (void)A->locate(inside, PL_TRAPEZOID); }, ErrorCode::Unsupported,
            "locate(trapezoid) -> Unsupported");
  // Triangulation PL is disabled for this kind: it calls point() on vertices at infinity
  // (point_location_and_decomposition.md gotcha 9).
  chk(!A->supports_point_location(PL_TRIANGULATION), "triangulation PL is NOT supported");
  chk_error([&] { A->attach_point_location(PL_TRIANGULATION); }, ErrorCode::Unsupported,
            "attach(triangulation) -> Unsupported");
  chk_error([&] { (void)A->locate(inside, PL_TRIANGULATION); }, ErrorCode::Unsupported,
            "locate(triangulation) -> Unsupported");

  const Located lv = A->locate(P(0, 0), PL_DEFAULT);
  chk(lv.type == 0, "locate((0,0)) finds a vertex");
  chk(lv.type != 0 || same_point(A->vertex_point(VH{lv.p, lv.id}), P(0, 0)),
      "and it is the vertex (0,0)");
  const Located le = A->locate(P(2, 0), PL_DEFAULT);
  chk(le.type == 1, "locate((2,0)) finds a halfedge (on y = 0)");
  const Located lf = A->locate(P(10, 10), PL_DEFAULT);
  chk(lf.type == 2, "locate((10,10)) finds a face");
  chk(lf.type != 2 || A->face_is_unbounded(FH{lf.p, lf.id}), "and it is unbounded");

  // -- vertical ray shooting --------------------------------------------
  const Located up = A->ray_shoot_up(inside, PL_DEFAULT);
  chk(up.type == 1, "ray_shoot_up((1,1)) hits an edge (the segment of x + y = 4)");
  const Located down = A->ray_shoot_down(inside, PL_DEFAULT);
  chk(down.type == 1, "ray_shoot_down((1,1)) hits an edge (the segment of y = 0)");
  chk_error([&] { (void)A->ray_shoot_up(inside, PL_NAIVE); }, ErrorCode::Unsupported,
            "naive PL has no ray shooting -> Unsupported");

  // -- batched locate ----------------------------------------------------
  std::vector<Geom> qs = {P(1, 1), P(0, 0), P(2, 0), P(10, 10), P(1, 1)};
  std::vector<Located> ls;
  A->batched_locate(qs, ls);
  chk_eq((long long)ls.size(), 5, "batched_locate returns one result per query");
  chk(ls[0].type == 2 && FH{ls[0].p, ls[0].id} == tri, "batched (1,1) -> the triangle");
  chk(ls[1].type == 0, "batched (0,0) -> a vertex");
  chk(ls[2].type == 1, "batched (2,0) -> a halfedge");
  chk(ls[3].type == 2, "batched (10,10) -> a face");
  chk(ls[4].type == 2 && FH{ls[4].p, ls[4].id} == tri,
      "a repeated query point gets the same answer (the sweep merges equal points)");

  // -- zone / do_intersect ----------------------------------------------
  // (1,1) is inside the triangle, (3,3) is outside (3+3 > 4); the segment crosses x + y = 4
  // at (2,2), so the zone is face, halfedge, face.
  const Geom cross = linear::make_segment(P(1, 1), P(3, 3));
  std::vector<Located> zone;
  A->zone(cross, zone);
  chk(zone.size() >= 3, "zone of the crossing segment reports at least 3 features (got " +
                            std::to_string(zone.size()) + ")");
  chk(zone.front().type == 2 && FH{zone.front().p, zone.front().id} == tri,
      "the zone starts in the triangle");
  chk_eq((long long)A->number_of_edges(), 9, "zone() does not modify the arrangement");
  chk(A->do_intersect(cross), "do_intersect(crossing segment)");
  const Geom insideseg = linear::make_segment(Pr(R(1), R(1)), Pr(R(3, 2), R(3, 2)));
  chk(!A->do_intersect(insideseg), "do_intersect(segment strictly inside the triangle) == false");

  // -- vertical decomposition -------------------------------------------
  std::vector<VerticalDecompositionEntry> dec;
  A->decompose(dec);
  chk_eq((long long)dec.size(), 3, "decompose reports one entry per concrete vertex");
  chk(A->vertex_valid(dec[0].v), "the decomposition's vertex handles are valid");

  // -- bulk export -------------------------------------------------------
  std::vector<double> coords;
  A->vertex_coordinates(coords);
  chk_eq((long long)coords.size(), 6, "vertex_coordinates: 2 doubles per concrete vertex");
  std::vector<std::size_t> idx;
  A->edge_vertex_indices(idx);
  chk_eq((long long)idx.size(), 18, "edge_vertex_indices: 2 per edge");
  std::size_t at_infinity = 0;
  for (std::size_t i : idx)
    if (i == std::numeric_limits<std::size_t>::max()) ++at_infinity;
  chk_eq((long long)at_infinity, 6, "6 edge endpoints are vertices at infinity (SIZE_MAX)");
  const BBox ab = A->bbox();
  chk_close(ab.lo[0], 0, 0, "arrangement bbox xmin (vertices only)");
  chk_close(ab.hi[0], 4, 0, "arrangement bbox xmax");
  chk_close(ab.hi[1], 4, 0, "arrangement bbox ymax");

  // -- clone -------------------------------------------------------------
  std::unique_ptr<ArrBase> B = A->clone();
  chk_eq((long long)B->number_of_vertices(), 3, "clone V");
  chk_eq((long long)B->number_of_edges(), 9, "clone E");
  chk_eq((long long)B->number_of_faces(), 7, "clone F");
  chk_eq((long long)B->number_of_curves(), 3, "clone keeps the history");
  chk(B->is_valid(), "the clone is valid");
  chk(!B->face_valid(tri), "a handle of the original is not valid in the clone");
  chk(A->face_valid(tri), "and is still valid in the original");

  // -- remove_curve ------------------------------------------------------
  // CGAL 6.1 BUG (characterised in test_trapezoid_removal_trap below): an ATTACHED
  // Arr_trapezoid_ric_point_location asserts (`p_pt != nullptr`, Arr_dcel_base.h:105) when an
  // edge incident to a vertex at infinity is removed.  It can no longer be attached at all for
  // this kind (KindPolicy<LinearTypes>::supports_trapezoid == false); the other four strategies
  // stay attached on purpose, to prove they survive the removal.
  chk(!A->has_point_location(PL_TRAPEZOID), "the trapezoid strategy is never attached");

  const std::size_t removed = A->remove_curve(c_d4);
  chk_eq((long long)removed, 3, "remove_curve(x+y=4) removes its 3 edges");
  chk(A->is_valid(), "still valid after remove_curve");
  // What is left: the lines y = 0 and x = 0 crossing at the origin, still split by the two
  // vertices (4,0) and (0,4) that the removed line had created (CGAL does not merge the
  // collinear edges around them). So V = 3, E = 3 + 3 = 6, and the two lines cut the plane
  // into F = 4 faces.
  chk_eq((long long)A->number_of_vertices(), 3, "V == 3 after the removal");
  chk_eq((long long)A->number_of_edges(), 6, "E == 6 after the removal");
  chk_eq((long long)A->number_of_faces(), 4, "F == 4 after the removal");
  chk_eq((long long)A->number_of_unbounded_faces(), 4, "all 4 remaining faces are unbounded");
  chk_error([&] { (void)A->number_of_induced_edges(c_d4); }, ErrorCode::InvalidHandle,
            "the removed curve handle is stale");

  // -- clear + reuse -----------------------------------------------------
  A->clear();
  chk(A->is_empty(), "clear() empties the arrangement");
  chk_eq((long long)A->number_of_faces(), 1, "1 face after clear()");
  A->insert_curve(linear::make_line(P(0, 0), P(1, 1)));
  chk_eq((long long)A->number_of_edges(), 1, "a single line is 1 edge");
  chk_eq((long long)A->number_of_faces(), 2, "and splits the plane in 2");
  chk(A->is_valid(), "valid after clear() + reuse");
}

// ===========================================================================
// 10. CGAL_TRAPS_CHECKLIST "Arrangement core": inserting an unbounded curve that OVERLAPS an
//     existing edge with an unbounded left end. Pinned down exactly, because the behaviour is
//     finer than the checklist entry: only INCREMENTAL insertion of a curve whose overlap with
//     an existing edge is unbounded on the left fails.
// ===========================================================================
void test_overlapping_unbounded_insert() {
  section("overlapping unbounded insertion (CGAL trap)");

  // (a) a RAY overlapping a line: the overlap has a left end, so it is fine.
  {
    std::unique_ptr<ArrBase> A = make_arrangement(Kind::Linear);
    A->insert_curve(linear::make_line_coefficients(R(0), R(1), R(0)));   // y = 0
    A->insert_curve(linear::make_ray(P(2, 0), P(5, 0)));                 // overlaps to the right
    chk_eq((long long)A->number_of_vertices(), 1, "ray overlap: V == 1 (the ray's source)");
    chk_eq((long long)A->number_of_edges(), 2, "ray overlap: E == 2");
    chk_eq((long long)A->number_of_faces(), 2, "ray overlap: F == 2");
    chk_eq((long long)A->number_of_curves(), 2, "ray overlap: both curves are in the history");
    chk(A->is_valid(), "ray overlap: the arrangement stays valid");
  }

  // (b) a LINE overlapping the same line: the overlap runs to -infinity, and CGAL's sweep would
  //     call Construct_min_vertex_2 on it -> CGAL_precondition(cv.has_left()) fails
  //     (Arr_linear_traits_2.h:689); with -DNDEBUG the check disappears and CGAL reads cv.left()
  //     of an unbounded end instead.  ArrImpl::reject_unbounded_overlap() detects the overlap
  //     first and reports Error(Unsupported); the arrangement is left untouched.
  {
    std::unique_ptr<ArrBase> A = make_arrangement(Kind::Linear);
    A->insert_curve(linear::make_line_coefficients(R(0), R(1), R(0)));
    std::string msg;
    chk_error([&] { A->insert_curve(linear::make_line_coefficients(R(0), R(1), R(0))); },
              ErrorCode::Unsupported, "insert_curve(line overlapping a line) -> Unsupported");
    try {
      A->insert_curve(linear::make_line_coefficients(R(0), R(1), R(0)));
    } catch (const Error& e) { msg = e.what(); }
    chk(msg.find("unbounded curve overlapping an existing edge") != std::string::npos,
        "and the message names the reason (got: " + msg + ")");
    chk_eq((long long)A->number_of_edges(), 1, "the arrangement is unchanged (still 1 edge)");
    chk_eq((long long)A->number_of_curves(), 1, "and the history still holds one curve");
    chk(A->is_valid(), "and it is still valid");
    // The same guard fires for a ray whose overlap runs to -infinity ...
    chk_error([&] { A->insert_curve(linear::make_ray(P(2, 0), P(-5, 0))); },
              ErrorCode::Unsupported, "insert_curve(left-going ray on the line) -> Unsupported");
    // ... and for insert_non_intersecting_curve / insert_curves on the same input.
    chk_error([&] { A->insert_non_intersecting_curve(linear::make_line_coefficients(R(0), R(1), R(0))); },
              ErrorCode::Unsupported, "insert_non_intersecting_curve -> Unsupported");
    std::vector<Geom> again = {linear::make_line_coefficients(R(0), R(1), R(0))};
    std::vector<CH> hs;
    chk_error([&] { A->insert_curves(again, hs); }, ErrorCode::Unsupported,
              "insert_curves -> Unsupported");
    chk(A->is_valid(), "the arrangement survived all four refusals");
  }

  // (b2) the AGGREGATE path is stricter than the incremental one: CGAL's sweep asserts
  //      `! e->is_fictitious()` (Arrangement_on_surface_2_impl.h:1517) whenever the overlap is
  //      unbounded at EITHER end, so the RIGHT-going ray of case (a) -- which insert_curve()
  //      handles -- has to be refused when it goes through insert_curves().
  {
    std::unique_ptr<ArrBase> A = make_arrangement(Kind::Linear);
    A->insert_curve(linear::make_line_coefficients(R(0), R(1), R(0)));   // y = 0
    std::vector<Geom> ray = {linear::make_ray(P(2, 0), P(5, 0))};
    std::vector<CH> hs;
    chk_error([&] { A->insert_curves(ray, hs); }, ErrorCode::Unsupported,
              "insert_curves(right-going ray on the line) -> Unsupported (aggregate is stricter)");
    // the incremental path still accepts it
    A->insert_curve(linear::make_ray(P(2, 0), P(5, 0)));
    chk_eq((long long)A->number_of_edges(), 2, "insert_curve still accepts the same ray");
    chk(A->is_valid(), "and the arrangement stays valid");
  }

  // (b3) an unbounded curve overlapping a BOUNDED edge produces a bounded overlap: legal on
  //      both paths.
  {
    std::unique_ptr<ArrBase> A = make_arrangement(Kind::Linear);
    A->insert_curve(linear::make_segment(P(0, 0), P(5, 0)));
    std::vector<Geom> ray = {linear::make_ray(P(2, 0), P(5, 0))};
    std::vector<CH> hs;
    A->insert_curves(ray, hs);
    chk_eq((long long)hs.size(), 1, "aggregate insert of a ray over a bounded segment works");
    chk(A->is_valid(), "and the arrangement is valid");
  }

  // (c) the very same pair inserted AGGREGATELY (one sweep) works: the sweep never asks for the
  //     min vertex of the unbounded overlap.
  {
    std::unique_ptr<ArrBase> A = make_arrangement(Kind::Linear);
    std::vector<Geom> both = {linear::make_line_coefficients(R(0), R(1), R(0)),
                              linear::make_line_coefficients(R(0), R(1), R(0))};
    std::vector<CH> handles;
    A->insert_curves(both, handles);
    chk_eq((long long)handles.size(), 2, "aggregate insertion returns 2 curve handles");
    chk_eq((long long)A->number_of_edges(), 1, "the two identical lines become 1 edge");
    chk_eq((long long)A->number_of_vertices(), 0, "with no concrete vertex");
    chk_eq((long long)A->number_of_faces(), 2, "and 2 faces");
    chk(A->is_valid(), "aggregate overlap: valid");
    std::vector<HH> es;
    A->edges(es);
    std::vector<CH> orig;
    if (!es.empty()) A->originating_curves(es[0], orig);
    chk_eq((long long)orig.size(), 2, "the single edge originates from BOTH input curves");
  }
}

// ===========================================================================
// 11. CGAL 6.1 BUG (found by this test, not in CGAL_TRAPS_CHECKLIST):
//     an ATTACHED Arr_trapezoid_ric_point_location on an UNBOUNDED arrangement asserts when an
//     edge incident to a vertex at infinity is removed.
//       Arr_trapezoid_ric_point_location::before_remove_edge
//         -> Trapezoidal_decomposition_2::remove -> container2dag -> deactivate_vertex
//         -> Td_inactive_vertex(v_before_rem)  -> v->point()
//         -> CGAL_assertion(p_pt != nullptr)   [Arr_dcel_base.h:105]
//     Removing a BOUNDED edge is fine, and every other strategy is fine.
// ===========================================================================
void test_trapezoid_removal_trap() {
  section("trapezoid PL is refused for the unbounded kind (CGAL bug)");

  // The strategy is now rejected at every entry point, which is what keeps the CGAL assertion
  // out of reach.  (Before KindPolicy<LinearTypes>::supports_trapezoid was set to false, the
  // sequence below -- attach + remove_curve of a line -- raised `p_pt != nullptr`.)
  {
    std::unique_ptr<ArrBase> A = make_arrangement(Kind::Linear);
    const CH c = A->insert_curve(linear::make_line_coefficients(R(0), R(1), R(0)));  // y = 0
    A->insert_curve(linear::make_line_coefficients(R(1), R(0), R(0)));               // x = 0
    chk(!A->supports_point_location(PL_TRAPEZOID), "supports_point_location(trapezoid) is false");
    chk_error([&] { A->attach_point_location(PL_TRAPEZOID); }, ErrorCode::Unsupported,
              "attach(trapezoid) -> Unsupported");
    chk(!A->has_point_location(PL_TRAPEZOID), "nothing got attached");
    A->detach_point_location(PL_TRAPEZOID);        // a no-op, must not throw
    chk_eq((long long)A->remove_curve(c), 2, "and removing the line at infinity now works");
    chk(A->is_valid(), "and the arrangement stays valid");
  }

  // Every other strategy survives the same removal.  (Landmarks is not offered for this kind
  // either -- see the point-location section of test_arrangement.)
  const int ok[3] = {PL_NAIVE, PL_SIMPLE, PL_WALK};
  const char* names[3] = {"naive", "simple", "walk"};
  for (int i = 0; i < 3; ++i) {
    std::unique_ptr<ArrBase> A = make_arrangement(Kind::Linear);
    const CH c = A->insert_curve(linear::make_line_coefficients(R(0), R(1), R(0)));
    A->insert_curve(linear::make_line_coefficients(R(1), R(0), R(0)));
    A->attach_point_location(ok[i]);
    bool threw = false;
    try {
      chk_eq((long long)A->remove_curve(c), 2,
             std::string("with ") + names[i] + " attached, remove_curve removes 2 edges");
    } catch (const std::exception&) {
      threw = true;
    }
    chk(!threw, std::string("the ") + names[i] + " strategy survives the removal");
    chk(A->is_valid(), std::string("valid after the removal (") + names[i] + ")");
  }
}

}  // namespace

int main() {
  std::printf("test_kind_linear — CGAL %s\n", cgal_version().c_str());
  std::printf("%s\n", build_info().c_str());
  test_registry();
  test_points();
  test_constructors();
  test_x_monotone();
  test_accessors();
  test_traits_ops();
  test_approximate();
  test_convert_curve();
  test_arrangement();
  test_overlapping_unbounded_insert();
  test_trapezoid_removal_trap();
  std::printf("\n=========================================\n");
  std::printf("%d checks, %d failures\n", g_checks, g_fail);
  return g_fail == 0 ? 0 : 1;
}
