// =============================================================================================
// arr2d — self-contained C++ test for Kind::Conic (src/arr2d/src/kind_conic.cpp).
//
// It exercises the point API, every constructor of namespace arr2d::conic, the x-monotone
// accessors, approximate()/curve_bbox(), convert_point()/convert_curve() from the other kinds
// (their raw CGAL objects are boxed directly here, so no other kind TU has to be linked), the
// CGAL 6.1 hyperbolic-axis gate, and a full arrangement round trip through
// arr2d::make_arrangement(Kind::Conic).
//
// Every expected number is hand-derived; the derivation is in the comment next to the check.
//
// ---------------------------------------------------------------------------------------------
// BUILD & RUN — variant A (the default: WITHOUT the Boolean-set-operations TU).
// The kind registrar stores &arr2d::make_polygon_set_conic as a plain function pointer, so the
// symbol must exist at link time even though it is never called here; -DARR2D_TEST_STUB_BSO makes
// this file define a null-returning stub for it.
//
//   REPO=/Users/sthv/PycharmProjects/arrangement-2d
//   B=/private/tmp/claude-501/.../scratchpad/kind_conic        # any scratch directory
//   CXX="/usr/bin/clang++ -std=c++17 -O0 -g -DCGAL_USE_CORE -DCGAL_USE_GMP -DCGAL_USE_MPFR \
//        -I/opt/homebrew/include -I$REPO/src/arr2d/include"
//   $CXX -c $REPO/src/arr2d/src/registry.cpp    -o $B/registry.o
//   $CXX -c $REPO/src/arr2d/src/numbers.cpp     -o $B/numbers.o
//   $CXX -c $REPO/src/arr2d/src/overlay.cpp     -o $B/overlay.o
//   $CXX -c $REPO/src/arr2d/src/kind_conic.cpp  -o $B/kind_conic.o
//   $CXX -DARR2D_TEST_STUB_BSO -c $REPO/src/arr2d/tests/test_kind_conic.cpp -o $B/test_kind_conic.o
//   /usr/bin/clang++ $B/registry.o $B/numbers.o $B/overlay.o $B/kind_conic.o $B/test_kind_conic.o \
//        -L/opt/homebrew/lib -lgmp -lmpfr -o $B/test_kind_conic
//   $B/test_kind_conic ; echo "exit=$?"          # must print "0 failures" and exit 0
//
// BUILD & RUN — variant B (WITH the real Boolean-set-operations TU, once bso_conic.cpp exists):
// drop -DARR2D_TEST_STUB_BSO from the test TU and add the BSO object to the link line:
//
//   $CXX -c $REPO/src/arr2d/src/bso_conic.cpp -o $B/bso_conic.o
//   $CXX -c $REPO/src/arr2d/tests/test_kind_conic.cpp -o $B/test_kind_conic.o
//   /usr/bin/clang++ $B/registry.o $B/numbers.o $B/overlay.o $B/kind_conic.o $B/bso_conic.o \
//        $B/test_kind_conic.o -L/opt/homebrew/lib -lgmp -lmpfr -o $B/test_kind_conic
//
// Exiting normally (return 0) is part of the test: any CGAL object holding a CORE::Expr that
// outlives main() aborts at process exit with `! blocks.empty()` (CGAL/CORE/MemoryPool.h:125).
// =============================================================================================

#include "arr2d/arrangement.hpp"
#include "arr2d/common.hpp"
#include "arr2d/numbers.hpp"
#include "arr2d/ops.hpp"
#include "arr2d/registry.hpp"

#include "arr2d/impl/number_conv.hpp"

// Raw geometry types of the kinds we convert FROM (headers only; their TUs are not linked).
#include "arr2d/kinds/bezier_types.hpp"
#include "arr2d/kinds/circle_segment_types.hpp"
#include "arr2d/kinds/conic_types.hpp"
#include "arr2d/kinds/linear_types.hpp"
#include "arr2d/kinds/polyline_types.hpp"
#include "arr2d/kinds/segment_types.hpp"

#ifdef ARR2D_TEST_STUB_BSO
#include "arr2d/bso.hpp"
namespace arr2d {
/// Stub for variant A of the build (see the header comment): the registrar only stores the
/// address of this function, it is never called by this test.
std::unique_ptr<PolygonSetBase> make_polygon_set_conic() { return nullptr; }
}  // namespace arr2d
#endif

#include <cmath>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace arr2d;

// =============================================================================================
// tiny harness
// =============================================================================================
static int g_checks = 0;
static int g_fails = 0;
static std::string g_section = "";

static void section(const char* s) {
  g_section = s;
  std::cout << "\n--- " << s << " ---\n";
}

#define CHECK(cond)                                                                       \
  do {                                                                                    \
    ++g_checks;                                                                           \
    if (!(cond)) {                                                                        \
      ++g_fails;                                                                          \
      std::cout << "FAIL [" << g_section << "] " << __LINE__ << ": " #cond "\n";           \
    }                                                                                     \
  } while (0)

#define CHECK_MSG(cond, msg)                                                              \
  do {                                                                                    \
    ++g_checks;                                                                           \
    if (!(cond)) {                                                                        \
      ++g_fails;                                                                          \
      std::cout << "FAIL [" << g_section << "] " << __LINE__ << ": " #cond " | " << msg    \
                << "\n";                                                                  \
    }                                                                                     \
  } while (0)

template <class A, class B>
static void check_eq_impl(int line, const A& a, const B& b, const char* expr) {
  ++g_checks;
  if (!(a == b)) {
    ++g_fails;
    std::cout << "FAIL [" << g_section << "] " << line << ": " << expr << " -> " << a
              << " (expected " << b << ")\n";
  }
}
#define CHECK_EQ(a, b) check_eq_impl(__LINE__, (a), (b), #a)

static void check_near_impl(int line, double a, double b, double tol, const char* expr) {
  ++g_checks;
  if (!(std::fabs(a - b) <= tol)) {
    ++g_fails;
    std::cout << "FAIL [" << g_section << "] " << line << ": " << expr << " -> " << a
              << " (expected " << b << " +- " << tol << ")\n";
  }
}
#define CHECK_NEAR(a, b, tol) check_near_impl(__LINE__, (a), (b), (tol), #a)

/// Runs `fn` and checks that it throws arr2d::Error with the given code.
template <class F>
static void check_throws(int line, ErrorCode code, F fn, const char* what) {
  ++g_checks;
  try {
    fn();
  } catch (const Error& e) {
    if (e.code == code) return;
    ++g_fails;
    std::cout << "FAIL [" << g_section << "] " << line << ": " << what
              << " threw Error(code=" << int(e.code) << ") \"" << e.what() << "\", expected code "
              << int(code) << "\n";
    return;
  } catch (const std::exception& e) {
    ++g_fails;
    std::cout << "FAIL [" << g_section << "] " << line << ": " << what
              << " threw a non-arr2d exception: " << e.what() << "\n";
    return;
  }
  ++g_fails;
  std::cout << "FAIL [" << g_section << "] " << line << ": " << what << " did not throw\n";
}
#define CHECK_THROWS(code, expr) check_throws(__LINE__, code, [&] { expr; }, #expr)

// =============================================================================================
// shorthands
// =============================================================================================
static const KindOps& O() { return ops(Kind::Conic); }

static Rational Q(long n, long d = 1) { return Rational(n) / Rational(d); }

/// A conic-kind point with rational coordinates.
static Geom CP(long nx, long dx, long ny, long dy) { return O().make_point(Q(nx, dx), Q(ny, dy)); }
static Geom CP(long x, long y) { return O().make_point(Q(x), Q(y)); }

/// A SEGMENT-kind point (Epeck::Point_2) with rational coordinates.  The constructors that need
/// exact rational input (five points, rational Bezier, the rational segment fast path) must be fed
/// points of a rational-coordinate kind: a Conic_point_2 stores CORE::Expr coordinates that cannot
/// be tested for rationality soundly, so conic points are refused there by design.
static Geom SP(long nx, long dx, long ny, long dy) {
  using FT = SegmentTypes::FT;
  return make_geom(Kind::Segment, GeomType::Point,
                   SegmentTypes::Point_2(FT(nx) / FT(dx), FT(ny) / FT(dy)));
}
static Geom SP(long x, long y) { return SP(x, 1, y, 1); }

static double px(const Geom& p) { double v[3]; O().point_approx(p, v); return v[0]; }
static double py(const Geom& p) { double v[3]; O().point_approx(p, v); return v[1]; }

static std::string coeff_string(const Geom& c) {
  Rational co[6];
  conic::coefficients(c, co);
  std::string s;
  for (int i = 0; i < 6; ++i) {
    std::string n, d;
    rational_to_strings(co[i], n, d);
    s += (i ? "," : "") + n + (d == "1" ? "" : "/" + d);
  }
  return s;
}

static std::size_t n_pieces(const Geom& c) {
  std::vector<Geom> out;
  O().make_x_monotone(c, out);
  return out.size();
}

/// exact CORE::Expr sqrt of a rational (through the traits' Nt_traits — never a static).
static Algebraic alg_sqrt(const Rational& r) {
  return ConicTypes::traits().nt_traits()->sqrt(Algebraic(r));
}

// The gate predicate is a TU-level helper of kind_conic.cpp; re-declared here so the systematic
// cases of docs/dev/cgal61_api/traits_conic.md §13 can be checked directly.
namespace arr2d {
namespace conic {
namespace detail {
bool hyperbolic_axis_is_sound(const Rational& r, const Rational& s, const Rational& t,
                              const Rational& u, const Rational& v, const Rational& w);
Rational conic_discriminant_N(const Rational& r, const Rational& s, const Rational& t,
                              const Rational& u, const Rational& v, const Rational& w);
}  // namespace detail
}  // namespace conic
}  // namespace arr2d

// =============================================================================================
// 1. registry + points
// =============================================================================================
static void test_points() {
  section("registry & points");
  CHECK(kind_available(Kind::Conic));
  CHECK_EQ(std::string(O().name()), std::string("conic"));
  CHECK_EQ(O().dimension(), 2);
  CHECK(!O().is_unbounded_kind());
  CHECK(O().has_polygon_set());
  CHECK(kind_has_polygon_set(Kind::Conic));

  const Geom p = CP(1, 2, 3, 4);          // (1/2, 3/4)
  CHECK_NEAR(px(p), 0.5, 0.0);            // exactly representable
  CHECK_NEAR(py(p), 0.75, 0.0);
  CHECK_EQ(O().approximate_coordinate(p, 0), 0.5);
  CHECK_EQ(O().approximate_coordinate(p, 1), 0.75);
  CHECK_THROWS(ErrorCode::InvalidArgument, O().approximate_coordinate(p, 2));
  CHECK_EQ(O().point_repr(p), std::string("ConicPoint(~0.5, ~0.75)"));

  // Certified intervals: 1/2 and 3/4 are exact doubles, so both intervals are degenerate.
  std::vector<std::pair<double, double>> iv;
  O().point_interval(p, iv);
  CHECK_EQ(iv.size(), std::size_t(2));
  CHECK(iv[0].first <= 0.5 && 0.5 <= iv[0].second);
  CHECK(iv[1].first <= 0.75 && 0.75 <= iv[1].second);

  // Exact coordinates are always Algebraic boxes for this kind, and NEVER rational:
  // CORE has no sound rationality test for Expr (exact_coordinates_contract.md gotcha 1).
  std::vector<Geom> nums;
  O().point_exact(p, nums);
  CHECK_EQ(nums.size(), std::size_t(2));
  CHECK(number_kind(nums[0]) == NumberKind::Algebraic);
  CHECK(number_kind(nums[1]) == NumberKind::Algebraic);
  CHECK_NEAR(number_to_double(nums[0]), 0.5, 0.0);
  CHECK(!O().point_is_rational(p));
  std::vector<Rational> rats;
  CHECK_THROWS(ErrorCode::NotRepresentable, O().point_exact_rational(p, rats));

  // Comparisons.
  const Geom q = CP(1, 2, 1, 1);          // (1/2, 1)
  const Geom r = CP(2, 1, 0, 1);          // (2, 0)
  CHECK_EQ(O().point_compare_x(p, q), 0);       // same x
  CHECK_EQ(O().point_compare_xy(p, q), -1);     // 3/4 < 1
  CHECK_EQ(O().point_compare_xy(q, p), 1);
  CHECK_EQ(O().point_compare_x(p, r), -1);
  CHECK(O().point_equal(p, CP(1, 2, 3, 4)));
  CHECK(!O().point_equal(p, q));

  // An algebraic point: (sqrt(2), 3).
  const Geom sq2 = box_core_expr(alg_sqrt(Q(2)));
  const Geom pa = conic::make_point_algebraic(sq2, box_rational(Q(3)));
  CHECK_NEAR(px(pa), 1.4142135623730951, 1e-15);
  CHECK_NEAR(py(pa), 3.0, 0.0);
  std::vector<std::pair<double, double>> iva;
  O().point_interval(pa, iva);
  CHECK(iva[0].first <= 1.4142135623730951 && 1.4142135623730951 <= iva[0].second);
  // sqrt-extension input: 1 + 2*sqrt(3) = 4.4641016151377544
  SqrtExt se{Q(1), Q(2), Q(3)};
  const Geom pb = conic::make_point_algebraic(box_sqrt_ext(se), box_rational(Q(0)));
  CHECK_NEAR(px(pb), 1.0 + 2.0 * std::sqrt(3.0), 1e-14);
  CHECK_THROWS(ErrorCode::InvalidArgument, conic::make_point_algebraic(CP(0, 0), box_rational(Q(1))));

  CHECK_THROWS(ErrorCode::Unsupported, O().make_point_3(Q(1), Q(2), Q(3)));
}

// =============================================================================================
// 2. convert_point from every other kind
// =============================================================================================
static void test_convert_point() {
  section("convert_point");
  // Conic -> Conic is the identity.
  const Geom p = CP(3, 4);
  const Geom same = O().convert_point(p);
  CHECK(O().point_equal(p, same));

  // Segment / Linear / Polyline all use Epeck::Point_2.
  const SegmentTypes::Point_2 ep(SegmentTypes::FT(7) / SegmentTypes::FT(2), SegmentTypes::FT(-3));
  for (Kind k : {Kind::Segment, Kind::Linear, Kind::Polyline}) {
    const Geom g = make_geom(k, GeomType::Point, ep);
    const Geom c = O().convert_point(g);
    CHECK_EQ(int(c.kind), int(Kind::Conic));
    CHECK_NEAR(px(c), 3.5, 0.0);
    CHECK_NEAR(py(c), -3.0, 0.0);
  }

  // Circle-segment points carry sqrt-extension coordinates: x = 0 + 1*sqrt(2), y = 1.
  using CsPoint = CircleSegmentTypes::Point_2;
  using CoordNT = CsPoint::CoordNT;
  using CsFT = CircleSegmentTypes::FT;
  const CsPoint cs(CoordNT(CsFT(0), CsFT(1), CsFT(2)), CoordNT(CsFT(1)));
  const Geom gcs = make_geom(Kind::CircleSegment, GeomType::Point, cs);
  const Geom ccs = O().convert_point(gcs);
  CHECK_NEAR(px(ccs), std::sqrt(2.0), 1e-15);      // exact conversion a + b*sqrt(c) -> CORE::Expr
  CHECK_NEAR(py(ccs), 1.0, 0.0);
  // ... and a sqrt-extension that is really rational (1 + 3*sqrt(4) == 7) still converts exactly.
  const CsPoint cs2(CoordNT(CsFT(1), CsFT(3), CsFT(4)), CoordNT(CsFT(0)));
  const Geom ccs2 = O().convert_point(make_geom(Kind::CircleSegment, GeomType::Point, cs2));
  CHECK_NEAR(px(ccs2), 7.0, 0.0);

  // A sphere point has no planar coordinates.
  // (No sphere object is constructed here; the kind tag alone decides.)
  const Geom fake_sphere = make_geom(Kind::Sphere, GeomType::Point, ep);
  CHECK_THROWS(ErrorCode::KindMismatch, O().convert_point(fake_sphere));
  // A curve where a point is expected.
  CHECK_THROWS(ErrorCode::InvalidArgument,
               O().convert_point(make_geom(Kind::Conic, GeomType::Curve, ep)));
}

// =============================================================================================
// 3. the hyperbolic-axis gate (traits_conic.md §13)
// =============================================================================================
static void test_hyperbolic_gate() {
  section("hyperbolic gate");
  using conic::detail::hyperbolic_axis_is_sound;
  using conic::detail::conic_discriminant_N;

  // Non-hyperbolic conics are always sound.
  CHECK(hyperbolic_axis_is_sound(Q(1), Q(4), Q(0), Q(0), Q(0), Q(-4)));   // ellipse
  CHECK(hyperbolic_axis_is_sound(Q(9), Q(1), Q(6), Q(-40), Q(20), Q(0))); // parabola (4rs-t^2 = 0)

  // §13.3: T == 0 is the branch CGAL gets right -> SAFE.
  //   x^2/4 - y^2 = 1  ->  (1/4, -1, 0, 0, 0, -1), t == 0.
  CHECK(hyperbolic_axis_is_sound(Q(1, 4), Q(-1), Q(0), Q(0), Q(0), Q(-1)));
  //   x*y = 1 -> (0,0,1,0,0,-1): theta = 45 deg, 2*theta mod pi = pi/2, gamma = 45 deg -> SAFE.
  CHECK(hyperbolic_axis_is_sound(Q(0), Q(0), Q(1), Q(0), Q(0), Q(-1)));
  //   x*y = 1 rotated by (c,s) = (4/5,3/5): (-12/25, 12/25, 7/25, 0, 0, -1) -> UNSAFE
  //   (this is the exact case measured in traits_conic.md §13.2/§13.6).
  CHECK(!hyperbolic_axis_is_sound(Q(-12, 25), Q(12, 25), Q(7, 25), Q(0), Q(0), Q(-1)));
  //   ... invariant under a global negation and under a positive rescaling (§13.3 step 0).
  CHECK(!hyperbolic_axis_is_sound(Q(12, 25), Q(-12, 25), Q(-7, 25), Q(0), Q(0), Q(1)));
  CHECK(!hyperbolic_axis_is_sound(Q(-12), Q(12), Q(7), Q(0), Q(0), Q(-25)));
  //   x^2/4 - y^2 = 1 rotated a little is UNSAFE while the unrotated one is SAFE (§13.3: the
  //   T == 0 discontinuity).  Rational rotation (c,s) = ((1-m^2)/(1+m^2), 2m/(1+m^2)) with
  //   m = 1/20, i.e. (399/401, 40/401), theta = 2*atan(1/20) = 5.7248 deg.  The geometric form of
  //   the criterion for x^2/a^2 - y^2/b^2 = 1 rotated by theta is
  //     UNSAFE  <=>  cos(4 theta) > (a^2-b^2)/(a^2+b^2) = (4-1)/(4+1) = 3/5,
  //   and cos(22.899 deg) = 0.9212 > 0.6.  The rotated coefficients of r x^2 + s y^2 (t = 0) are
  //     r' = r c^2 + s s^2, s' = r s^2 + s c^2, t' = 2 c s (r - s).
  {
    const Rational c = Q(399, 401), s = Q(40, 401), r0 = Q(1, 4), s0 = Q(-1);
    const Rational rr = r0 * c * c + s0 * s * s;
    const Rational ss = r0 * s * s + s0 * c * c;
    const Rational tt = 2 * c * s * (r0 - s0);
    CHECK(CGAL::sign(4 * rr * ss - tt * tt) == CGAL::NEGATIVE);
    CHECK(!hyperbolic_axis_is_sound(rr, ss, tt, Q(0), Q(0), Q(-1)));
  }
  // A degenerate hyperbola (line pair x*y = 0) has N == 0 -> reported unsound.
  CHECK_EQ(int(CGAL::sign(conic_discriminant_N(Q(0), Q(0), Q(1), Q(0), Q(0), Q(0)))), int(CGAL::ZERO));
  CHECK(!hyperbolic_axis_is_sound(Q(0), Q(0), Q(1), Q(0), Q(0), Q(0)));

  // ---- EMPIRICAL confirmation: measure the Extra_data line CGAL actually stores ---------------
  // traits_conic.md §13.1: for a hyperbolic arc the stored line a x + b y + c = 0 must SEPARATE
  // the two branches, i.e. sign(a x + b y + c) must be constant and non-zero along one branch.
  // §13.5 classifies a case by evaluating that sign at points spread along the arc's own branch:
  // all signs equal and non-zero => separating (SAFE); otherwise a chord (UNSAFE).
  {
    // (a) the SAFE conic x^2/4 - y^2 = 1.  Rational points of the right branch:
    //     (x, y) = (2(1+m^2)/(1-m^2), 2m/(1-m^2)),  m in (-1, 1)
    //     m = 0, +-1/3, +-1/2  ->  (2,0), (5/2, +-3/4), (10/3, +-4/3)
    //     (check m = 1/3: (5/2)^2/4 - (3/4)^2 = 25/16 - 9/16 = 1.)
    const Geom tgt = conic::make_point_algebraic(box_rational(Q(4)), box_core_expr(alg_sqrt(Q(3))));
    const Geom safe_arc =
        conic::make_arc(Q(1, 4), Q(-1), Q(0), Q(0), Q(0), Q(-1), +1, CP(2, 0), tgt);
    const ConicTypes::Curve_2& sc = safe_arc.as<ConicTypes::Curve_2>();
    CHECK(sc.extra_data() != nullptr);        // a hyperbola always gets the axis
    const Rational sx[5] = {Q(2), Q(5, 2), Q(5, 2), Q(10, 3), Q(10, 3)};
    const Rational sy[5] = {Q(0), Q(3, 4), Q(-3, 4), Q(4, 3), Q(-4, 3)};
    int pos = 0, neg = 0, zero = 0;
    for (int i = 0; i < 5; ++i) {
      const int sg = int(sc.sign_of_extra_data(to_core_expr(sx[i]), to_core_expr(sy[i])));
      if (sg > 0) ++pos; else if (sg < 0) ++neg; else ++zero;
    }
    CHECK_MSG(zero == 0 && (pos == 5 || neg == 5),
              "SAFE conic: signs along the branch were pos=" << pos << " neg=" << neg
                                                             << " zero=" << zero);

    // (b) the UNSAFE conic (x*y = 1 rotated by (c,s) = (4/5,3/5)), i.e.
    //     -12/25 x^2 + 12/25 y^2 + 7/25 xy - 1 = 0.  Rational points of the branch X > 0:
    //     (x, y) = ((4X - 3/X)/5, (3X + 4/X)/5) for X = 1/4, 1/2, 1, 2, 4:
    //       (-11/5, 67/20), (-4/5, 19/10), (1/5, 7/5), (13/10, 8/5), (61/20, 13/5)
    //     (check (-11/5, 67/20): -12*(121/25) + 12*(4489/400) + 7*(-737/100) - 25 = 0 exactly.)
    conic::set_allow_hyperbolic(true);
    const Geom unsafe_arc = conic::make_arc(Q(-12, 25), Q(12, 25), Q(7, 25), Q(0), Q(0), Q(-1), +1,
                                            CP(1, 5, 7, 5), CP(13, 10, 8, 5));
    conic::set_allow_hyperbolic(false);
    const ConicTypes::Curve_2& uc = unsafe_arc.as<ConicTypes::Curve_2>();
    CHECK(uc.extra_data() != nullptr);
    const Rational ux[5] = {Q(-11, 5), Q(-4, 5), Q(1, 5), Q(13, 10), Q(61, 20)};
    const Rational uy[5] = {Q(67, 20), Q(19, 10), Q(7, 5), Q(8, 5), Q(13, 5)};
    int upos = 0, uneg = 0, uzero = 0;
    for (int i = 0; i < 5; ++i) {
      const int sg = int(uc.sign_of_extra_data(to_core_expr(ux[i]), to_core_expr(uy[i])));
      if (sg > 0) ++upos; else if (sg < 0) ++uneg; else ++uzero;
    }
    // A chord through the centre cuts the branch: the signs must NOT all agree.  This is the
    // measurement that proves the predicate's "UNSAFE" verdict is real and not conservative.
    CHECK_MSG(!(uzero == 0 && (upos == 5 || uneg == 5)),
              "UNSAFE conic: signs along the branch were pos=" << upos << " neg=" << uneg
                                                              << " zero=" << uzero);
  }

  // ---- behaviour of the constructors --------------------------------------------------------
  CHECK(!conic::allow_hyperbolic());
  // SAFE hyperbolic arc on x^2/4 - y^2 = 1 from (2,0) to (4, sqrt(3)): allowed by default.
  const Geom h_tgt = conic::make_point_algebraic(box_rational(Q(4)), box_core_expr(alg_sqrt(Q(3))));
  const Geom safe_arc =
      conic::make_arc(Q(1, 4), Q(-1), Q(0), Q(0), Q(0), Q(-1), +1, CP(2, 0), h_tgt);
  // CGAL integerises by x4 and keeps the sign (the requested CCW matches the natural orientation).
  CHECK_EQ(coeff_string(safe_arc), std::string("1,-4,0,0,0,-4"));
  CHECK_EQ(conic::conic_type(safe_arc), int(conic::HYPERBOLA));
  CHECK_EQ(conic::orientation(safe_arc), 1);
  CHECK_EQ(n_pieces(safe_arc), std::size_t(1));

  // UNSAFE hyperbolic arc (the §13.6 case): refused by default...
  const Geom us = CP(1, 5, 7, 5);      // (1/5, 7/5)   on the rotated x*y = 1
  const Geom ut = CP(13, 10, 8, 5);    // (13/10, 8/5)
  CHECK_THROWS(ErrorCode::Unsupported,
               conic::make_arc(Q(-12, 25), Q(12, 25), Q(7, 25), Q(0), Q(0), Q(-1), +1, us, ut));
  // ... and accepted once the user opts in (this particular arc does not straddle the bogus
  // chord, so CGAL builds it without asserting — traits_conic.md §13.6).
  conic::set_allow_hyperbolic(true);
  CHECK(conic::allow_hyperbolic());
  {
    const Geom bad =
        conic::make_arc(Q(-12, 25), Q(12, 25), Q(7, 25), Q(0), Q(0), Q(-1), +1, us, ut);
    CHECK_EQ(coeff_string(bad), std::string("-12,12,7,0,0,-25"));
    CHECK_EQ(conic::conic_type(bad), int(conic::HYPERBOLA));
  }
  conic::set_allow_hyperbolic(false);
  CHECK(!conic::allow_hyperbolic());
  // A degenerate hyperbola is rejected as InvalidArgument even with a non-collinear orientation.
  CHECK_THROWS(ErrorCode::InvalidArgument,
               conic::make_arc(Q(0), Q(0), Q(1), Q(0), Q(0), Q(0), +1, CP(1, 0), CP(0, 1)));
}

// =============================================================================================
// 4. every constructor of namespace arr2d::conic
// =============================================================================================
static void test_constructors() {
  section("constructors");

  // ---- make_full: the ellipse x^2 + 4y^2 - 4 = 0 --------------------------------------------
  {
    const Geom e = conic::make_full(Q(1), Q(4), Q(0), Q(0), Q(0), Q(-4));
    CHECK(conic::is_full(e));
    // The natural Conic_2 orientation of an ellipse with r > 0 is CLOCKWISE (traits_conic.md 2),
    // and set_full() computes it, so the coefficients are stored unchanged.
    CHECK_EQ(conic::orientation(e), -1);
    CHECK_EQ(coeff_string(e), std::string("1,4,0,0,0,-4"));
    CHECK_EQ(conic::conic_type(e), int(conic::ELLIPSE));
    CHECK_EQ(n_pieces(e), std::size_t(2));       // a full conic always splits into 2 arcs
    CHECK(!O().is_x_monotone(e));
    CHECK(O().curve_is_bounded(e));
    const BBox bb = O().curve_bbox(e);
    CHECK_NEAR(bb.lo[0], -2.0, 1e-12);
    CHECK_NEAR(bb.hi[0], 2.0, 1e-12);
    CHECK_NEAR(bb.lo[1], -1.0, 1e-12);
    CHECK_NEAR(bb.hi[1], 1.0, 1e-12);
    CHECK_EQ(O().curve_repr(e),
             std::string("Conic(full 1, 4, 0, 0, 0, -4, orientation=cw)"));
    // Non-ellipses and empty/degenerate conics are refused (CGAL only asserts / returns garbage).
    CHECK_THROWS(ErrorCode::InvalidArgument,
                 conic::make_full(Q(1), Q(-1), Q(0), Q(0), Q(0), Q(-1)));  // hyperbola
    CHECK_THROWS(ErrorCode::InvalidArgument,
                 conic::make_full(Q(1), Q(1), Q(0), Q(0), Q(0), Q(1)));    // imaginary ellipse
    CHECK_THROWS(ErrorCode::InvalidArgument,
                 conic::make_full(Q(1), Q(1), Q(0), Q(0), Q(0), Q(0)));    // a single point
  }

  // ---- make_arc: the quarter of x^2 + 4y^2 = 4 from (2,0) to (0,1), counterclockwise --------
  {
    const Geom a = conic::make_arc(Q(1), Q(4), Q(0), Q(0), Q(0), Q(-4), +1, CP(2, 0), CP(0, 1));
    CHECK(!conic::is_full(a));
    CHECK_EQ(conic::orientation(a), 1);
    // Construct_curve_2 NEGATES the coefficients when the requested orientation differs from the
    // natural one (traits_conic.md gotcha 2): CCW on an ellipse => stored r < 0.
    CHECK_EQ(coeff_string(a), std::string("-1,-4,0,0,0,4"));
    CHECK_EQ(conic::conic_type(a), int(conic::ELLIPSE));
    CHECK_EQ(n_pieces(a), std::size_t(1));
    CHECK(O().is_x_monotone(a));
    const BBox bb = O().curve_bbox(a);
    CHECK_NEAR(bb.lo[0], 0.0, 1e-12);
    CHECK_NEAR(bb.hi[0], 2.0, 1e-12);
    CHECK_NEAR(bb.lo[1], 0.0, 1e-12);
    CHECK_NEAR(bb.hi[1], 1.0, 1e-12);
    CHECK_EQ(O().curve_repr(a),
             std::string("ConicArc(-1, -4, 0, 0, 0, 4, orientation=ccw, source=(~2, ~0), "
                         "target=(~0, ~1))"));
    // Endpoints off the conic produce an INVALID arc silently in CGAL (traits_conic.md gotcha 3);
    // we turn that into Error(InvalidArgument).
    CHECK_THROWS(ErrorCode::InvalidArgument,
                 conic::make_arc(Q(1), Q(4), Q(0), Q(0), Q(0), Q(-4), +1, CP(2, 0), CP(0, 5)));
    CHECK_THROWS(ErrorCode::InvalidArgument,
                 conic::make_arc(Q(1), Q(4), Q(0), Q(0), Q(0), Q(-4), +1, CP(2, 0), CP(2, 0)));
  }

  // ---- make_arc_with_defining_conics ---------------------------------------------------------
  // Base x^2 + 4y^2 - 4 = 0; the source is its intersection with C1: x - 2 = 0, the target with
  // C2: y - 1 = 0 (the verified example of traits_conic.md §2.6).
  {
    const Rational base[6] = {Q(1), Q(4), Q(0), Q(0), Q(0), Q(-4)};
    const Rational c1[6] = {Q(0), Q(0), Q(0), Q(1), Q(0), Q(-2)};   //  x - 2 = 0
    const Rational c2[6] = {Q(0), Q(0), Q(0), Q(0), Q(1), Q(-1)};   //  y - 1 = 0
    const Geom a =
        conic::make_arc_with_defining_conics(base, +1, 2.0, 0.0, c1, 0.0, 1.0, c2);
    CHECK_EQ(coeff_string(a), std::string("-1,-4,0,0,0,4"));
    CHECK_EQ(conic::orientation(a), 1);
    std::vector<Geom> mx;
    O().make_x_monotone(a, mx);
    CHECK_EQ(mx.size(), std::size_t(1));
    CHECK_NEAR(px(O().xcurve_source(mx[0])), 2.0, 1e-12);
    CHECK_NEAR(py(O().xcurve_source(mx[0])), 0.0, 1e-12);
    CHECK_NEAR(px(O().xcurve_target(mx[0])), 0.0, 1e-12);
    CHECK_NEAR(py(O().xcurve_target(mx[0])), 1.0, 1e-12);
    // No intersection anywhere near the given approximations -> invalid arc -> Error.
    const Rational far[6] = {Q(0), Q(0), Q(0), Q(1), Q(0), Q(-100)};   // x - 100 = 0
    CHECK_THROWS(ErrorCode::InvalidArgument,
                 conic::make_arc_with_defining_conics(base, +1, 100.0, 0.0, far, 0.0, 1.0, c2));
  }

  // ---- make_circle / make_circle_arc ---------------------------------------------------------
  {
    // Full circle x^2 + y^2 = 4, counterclockwise.  The natural orientation of (1,1,0,...) is
    // CLOCKWISE, so the CCW form is the negated one: (-1,-1,0,0,0,4).
    const Geom c = conic::make_circle(Q(0), Q(0), Q(4), +1);
    CHECK(conic::is_full(c));
    CHECK_EQ(conic::orientation(c), 1);
    CHECK_EQ(coeff_string(c), std::string("-1,-1,0,0,0,4"));
    CHECK_EQ(conic::conic_type(c), int(conic::ELLIPSE));   // a circle IS an ellipse here
    CHECK_EQ(n_pieces(c), std::size_t(2));
    const Geom cw = conic::make_circle(Q(0), Q(0), Q(4), -1);
    CHECK_EQ(conic::orientation(cw), -1);
    CHECK_EQ(coeff_string(cw), std::string("1,1,0,0,0,-4"));
    CHECK_THROWS(ErrorCode::InvalidArgument, conic::make_circle(Q(0), Q(0), Q(4), 0));
    CHECK_THROWS(ErrorCode::InvalidArgument, conic::make_circle(Q(0), Q(0), Q(0), +1));
    CHECK_THROWS(ErrorCode::InvalidArgument, conic::make_circle(Q(0), Q(0), Q(-4), +1));

    // Quarter circle from (2,0) to (0,2), counterclockwise.
    const Geom arc = conic::make_circle_arc(Q(0), Q(0), Q(4), +1, CP(2, 0), CP(0, 2));
    CHECK_EQ(coeff_string(arc), std::string("-1,-1,0,0,0,4"));
    CHECK_EQ(n_pieces(arc), std::size_t(1));
    const BBox bb = O().curve_bbox(arc);
    CHECK_NEAR(bb.lo[0], 0.0, 1e-12);
    CHECK_NEAR(bb.hi[0], 2.0, 1e-12);
    CHECK_NEAR(bb.lo[1], 0.0, 1e-12);
    CHECK_NEAR(bb.hi[1], 2.0, 1e-12);
    // The upper half circle (2,0) -> (-2,0) counterclockwise IS x-monotone: the circle's only two
    // vertical tangency points are exactly its endpoints.
    const Geom half = conic::make_circle_arc(Q(0), Q(0), Q(4), +1, CP(2, 0), CP(-2, 0));
    CHECK_EQ(n_pieces(half), std::size_t(1));
    // The three-quarter arc (2,0) -> (0,2) -> (-2,0) -> (0,-2) has (-2,0) in its interior and
    // therefore splits into 2 x-monotone pieces.
    const Geom three_quarter = conic::make_circle_arc(Q(0), Q(0), Q(4), +1, CP(2, 0), CP(0, -2));
    CHECK_EQ(n_pieces(three_quarter), std::size_t(2));
    CHECK_THROWS(ErrorCode::InvalidArgument,
                 conic::make_circle_arc(Q(0), Q(0), Q(4), 0, CP(2, 0), CP(0, 2)));
    CHECK_THROWS(ErrorCode::InvalidArgument,
                 conic::make_circle_arc(Q(0), Q(0), Q(4), +1, CP(2, 0), CP(0, 3)));  // off circle
  }

  // ---- make_ellipse: centre (1,2), a = 2 along (3,4), b = 1 ----------------------------------
  {
    // Derivation (see kind_conic.cpp): n = 25, A = b^2 dx^2 + a^2 dy^2 = 9 + 64 = 73,
    // B = b^2 dy^2 + a^2 dx^2 = 16 + 36 = 52, C = 2 dx dy (b^2 - a^2) = 24*(-3) = -72,
    // u = -2*73*1 - (-72)*2 = -146 + 144 = -2,   v = -2*52*2 - (-72)*1 = -208 + 72 = -136,
    // w = 73 + 52*4 + (-72)*2 - 4*1*25 = 73 + 208 - 144 - 100 = 37.
    // Sanity: 4rs - t^2 = 4*73*52 - 72^2 = 15184 - 5184 = 10000 = 4 a^2 b^2 n^2 = 4*4*1*625.
    const Geom e = conic::make_ellipse(Q(1), Q(2), Q(2), Q(1), Q(3), Q(4), -1);
    CHECK(conic::is_full(e));
    CHECK_EQ(conic::orientation(e), -1);
    CHECK_EQ(coeff_string(e), std::string("73,52,-72,-2,-136,37"));
    CHECK_EQ(conic::conic_type(e), int(conic::ELLIPSE));
    CHECK_EQ(n_pieces(e), std::size_t(2));
    const Geom eccw = conic::make_ellipse(Q(1), Q(2), Q(2), Q(1), Q(3), Q(4), +1);
    CHECK_EQ(conic::orientation(eccw), 1);
    CHECK_EQ(coeff_string(eccw), std::string("-73,-52,72,2,136,-37"));
    // Extent: half-width  = sqrt(a^2 c^2 + b^2 s^2) with (c,s) = (3/5,4/5) = sqrt(4*.36+1*.64)
    //                     = sqrt(2.08) = 1.442220510185596
    //         half-height = sqrt(a^2 s^2 + b^2 c^2) = sqrt(4*.64+1*.36) = sqrt(2.92)
    //                     = 1.708800749063506
    const BBox bb = O().curve_bbox(e);
    CHECK_NEAR(bb.lo[0], 1.0 - std::sqrt(2.08), 1e-9);
    CHECK_NEAR(bb.hi[0], 1.0 + std::sqrt(2.08), 1e-9);
    CHECK_NEAR(bb.lo[1], 2.0 - std::sqrt(2.92), 1e-9);
    CHECK_NEAR(bb.hi[1], 2.0 + std::sqrt(2.92), 1e-9);
    // A circle is the a == b case: centre (0,0), a = b = 2 -> x^2 + y^2 = 4.
    const Geom circ = conic::make_ellipse(Q(0), Q(0), Q(2), Q(2), Q(1), Q(0), +1);
    CHECK_EQ(coeff_string(circ), std::string("-1,-1,0,0,0,4"));
    CHECK_THROWS(ErrorCode::InvalidArgument,
                 conic::make_ellipse(Q(0), Q(0), Q(0), Q(2), Q(1), Q(0), +1));
    CHECK_THROWS(ErrorCode::InvalidArgument,
                 conic::make_ellipse(Q(0), Q(0), Q(2), Q(2), Q(0), Q(0), +1));
  }

  // ---- make_segment ---------------------------------------------------------------------------
  {
    // Rational endpoints -> the Rat_segment_2 overload, which stores the supporting line in
    // (u, v, w) = (y2-y1, x1-x2, x2 y1 - x1 y2) = (2, -4, 0) -> integerised to (1, -2, 0).
    const Geom s = conic::make_segment(SP(0, 0), SP(4, 2));
    CHECK_EQ(coeff_string(s), std::string("0,0,0,1,-2,0"));
    CHECK_EQ(conic::orientation(s), 0);
    CHECK_EQ(conic::conic_type(s), int(conic::LINE_PAIR_OR_SEGMENT));
    CHECK_EQ(n_pieces(s), std::size_t(1));
    const BBox bb = O().curve_bbox(s);
    CHECK_NEAR(bb.lo[0], 0.0, 1e-12);
    CHECK_NEAR(bb.hi[0], 4.0, 1e-12);
    CHECK_THROWS(ErrorCode::InvalidArgument, conic::make_segment(SP(1, 1), SP(1, 1)));
    CHECK_THROWS(ErrorCode::InvalidArgument, conic::make_segment(CP(1, 1), CP(1, 1)));
    // DOCUMENTED LIMITATION: conic points are never recognised as rational (CORE has no sound
    // rationality test for Expr), so the same segment given as conic points takes the exact
    // "special segment" route instead and reports all-zero coefficients.
    CHECK_EQ(coeff_string(conic::make_segment(CP(0, 0), CP(4, 2))), std::string("0,0,0,0,0,0"));

    // orientation == 0 (COLLINEAR) selects CGAL's "line pair segment" branch of Traits::set():
    // the degenerate conic x^2 - y^2 = 0 is the pair of lines y = x and y = -x, and the segment
    // (0,0) -> (2,2) lies on one of them (the midpoint (1,1) satisfies the equation).  This path
    // never reaches build_hyperbolic_arc_data(), so the hyperbolic gate must NOT fire even though
    // 4rs - t^2 = -4 < 0 and N == 0.
    const Geom lp = conic::make_arc(Q(1), Q(-1), Q(0), Q(0), Q(0), Q(0), 0, CP(0, 0), CP(2, 2));
    CHECK_EQ(conic::orientation(lp), 0);
    CHECK_EQ(conic::conic_type(lp), int(conic::LINE_PAIR_OR_SEGMENT));
    CHECK_EQ(coeff_string(lp), std::string("1,-1,0,0,0,0"));
    CHECK_EQ(n_pieces(lp), std::size_t(1));
    CHECK_EQ(O().compare_y_at_x(CP(1, 1), O().to_x_monotone(lp)), 0);
    // ... but a segment whose endpoints straddle the two lines is rejected (CGAL's midpoint test).
    CHECK_THROWS(ErrorCode::InvalidArgument,
                 conic::make_arc(Q(1), Q(-1), Q(0), Q(0), Q(0), Q(0), 0, CP(-2, 2), CP(2, 2)));

    // Algebraic endpoints -> the "special segment" overload: all six coefficients are 0.
    const Geom alg_end = conic::make_point_algebraic(box_core_expr(alg_sqrt(Q(2))), box_rational(Q(1)));
    const Geom s2 = conic::make_segment(SP(0, 0), alg_end);
    CHECK_EQ(coeff_string(s2), std::string("0,0,0,0,0,0"));
    CHECK_EQ(n_pieces(s2), std::size_t(1));
    std::vector<Geom> mx;
    O().make_x_monotone(s2, mx);
    CHECK_NEAR(px(O().xcurve_target(mx[0])), std::sqrt(2.0), 1e-15);
  }

  // ---- make_from_five_points ------------------------------------------------------------------
  {
    // Five points on x^2 + 4y^2 - 4 = 0, in order along the CCW arc from (2,0) to (-6/5,4/5).
    const Geom c = conic::make_from_five_points(SP(2, 0), SP(8, 5, 3, 5), SP(6, 5, 4, 5), SP(0, 1),
                                                SP(-6, 5, 4, 5));
    CHECK_EQ(coeff_string(c), std::string("-1,-4,0,0,0,4"));
    CHECK_EQ(conic::orientation(c), 1);
    CHECK_EQ(conic::conic_type(c), int(conic::ELLIPSE));
    CHECK_EQ(n_pieces(c), std::size_t(1));   // the arc stays in the upper half plane
    // p1, p2, p5 collinear -> refused before CGAL's own precondition fires.
    CHECK_THROWS(ErrorCode::InvalidArgument,
                 conic::make_from_five_points(SP(0, 0), SP(1, 1), SP(2, 5), SP(3, 7), SP(2, 2)));
    // p3 on the other side of the chord p1-p5.
    CHECK_THROWS(ErrorCode::InvalidArgument,
                 conic::make_from_five_points(SP(2, 0), SP(8, 5, 3, 5), SP(6, 5, -4, 5), SP(0, 1),
                                              SP(-6, 5, 4, 5)));
    // Conic points cannot be used: their coordinates are algebraic and untestable -- this holds
    // even for a conic point that WAS built from rationals (the documented limitation).
    const Geom alg_p = conic::make_point_algebraic(box_core_expr(alg_sqrt(Q(2))), box_rational(Q(0)));
    CHECK_THROWS(ErrorCode::NotRepresentable,
                 conic::make_from_five_points(alg_p, SP(8, 5, 3, 5), SP(6, 5, 4, 5), SP(0, 1),
                                              SP(-6, 5, 4, 5)));
    CHECK_THROWS(ErrorCode::NotRepresentable,
                 conic::make_from_five_points(CP(2, 0), SP(8, 5, 3, 5), SP(6, 5, 4, 5), SP(0, 1),
                                              SP(-6, 5, 4, 5)));
  }

  // ---- make_from_rational_bezier ---------------------------------------------------------------
  {
    // P0 = (0,0), P1 = (1,2), P2 = (3,1), weights (1,1,1) -> a parabola (w1^2 == w0 w2).
    // Coefficients from traits_conic.md §12.1:
    //   a = (y1-y2, x2-x1, x1 y2 - x2 y1) = (1, 2, -5)
    //   b = (y2-y0, x0-x2, x2 y0 - x0 y2) = (1, -3, 0)
    //   c = (y0-y1, x1-x0, x0 y1 - x1 y0) = (-2, 1, 0)
    //   K = 1, M = 4
    //   r = 1*1 - 4*1*(-2) = 9      s = 9 - 4*2*1 = 1        t = 2*(-3) - 4*(1*1 + 2*(-2)) = 6
    //   u = 0 - 4*(1*0 + (-5)*(-2)) = -40   v = 0 - 4*(2*0 + (-5)*1) = 20    w = 0 - 4*(-5)*0 = 0
    //   4rs - t^2 = 36 - 36 = 0  -> parabola.
    // orientation(P0,P1,P2) = sign(1*1 - 2*3) = -1 -> CLOCKWISE.
    const Geom b = conic::make_from_rational_bezier(SP(0, 0), SP(1, 2), SP(3, 1), Q(1), Q(1), Q(1));
    CHECK_EQ(coeff_string(b), std::string("9,1,6,-40,20,0"));
    CHECK_EQ(conic::orientation(b), -1);
    CHECK_EQ(conic::conic_type(b), int(conic::PARABOLA));
    CHECK_EQ(n_pieces(b), std::size_t(1));
    // The shoulder B(1/2) = (w0 P0 + 2 w1 P1 + w2 P2)/(w0+2w1+w2) = ((0+2+3)/4, (0+4+1)/4)
    //                     = (5/4, 5/4) must lie on the arc.
    std::vector<Geom> mx;
    O().make_x_monotone(b, mx);
    CHECK_EQ(O().compare_y_at_x(CP(5, 4, 5, 4), mx[0]), 0);

    // Weights (1, 1/2, 1): w1^2 = 1/4 < w0 w2 = 1 -> an ellipse.
    const Geom e =
        conic::make_from_rational_bezier(SP(0, 0), SP(1, 2), SP(3, 1), Q(1), Q(1, 2), Q(1));
    CHECK_EQ(conic::conic_type(e), int(conic::ELLIPSE));
    // Weights (1, 2, 1): w1^2 = 4 > 1 -> a hyperbola, and this one is UNSAFE (it is the
    // traits_conic.md §13.5 Bezier whose source lies exactly on CGAL's bogus chord).
    CHECK_THROWS(ErrorCode::Unsupported,
                 conic::make_from_rational_bezier(SP(0, 0), SP(1, 2), SP(3, 1), Q(1), Q(2), Q(1)));
    // Collinear control points -> a straight segment (the conic would be a degenerate line pair).
    const Geom seg = conic::make_from_rational_bezier(SP(0, 0), SP(1, 1), SP(2, 2), Q(1), Q(1), Q(1));
    CHECK_EQ(conic::conic_type(seg), int(conic::LINE_PAIR_OR_SEGMENT));
    CHECK_NEAR(px(O().xcurve_target(O().to_x_monotone(seg))), 2.0, 1e-12);
    // Non-positive weights are rejected.
    CHECK_THROWS(ErrorCode::InvalidArgument,
                 conic::make_from_rational_bezier(SP(0, 0), SP(1, 2), SP(3, 1), Q(1), Q(0), Q(1)));
    CHECK_THROWS(ErrorCode::InvalidArgument,
                 conic::make_from_rational_bezier(SP(0, 0), SP(1, 2), SP(3, 1), Q(1), Q(-1), Q(1)));
  }

  // ---- make_from_circle_segment ----------------------------------------------------------------
  {
    using CsK = CircleSegmentTypes::Kernel;
    using CsCurve = CircleSegmentTypes::Curve_2;
    using CsPoint = CircleSegmentTypes::Point_2;
    using CoordNT = CsPoint::CoordNT;
    using CsFT = CircleSegmentTypes::FT;

    // (a) full circle, counterclockwise (the default orientation of Kernel::Circle_2).
    const CsK::Circle_2 circ(CsK::Point_2(CsFT(0), CsFT(0)), CsFT(4));
    const CsCurve full(circ);
    const Geom gfull = make_geom(Kind::CircleSegment, GeomType::Curve, full);
    const Geom cfull = conic::make_from_circle_segment(gfull);
    CHECK(conic::is_full(cfull));
    CHECK_EQ(conic::orientation(cfull), 1);
    CHECK_EQ(coeff_string(cfull), std::string("-1,-1,0,0,0,4"));

    // (b) circular arc (2,0) -> (0,2) on the same circle.
    const CsPoint s(CoordNT(CsFT(2)), CoordNT(CsFT(0)));
    const CsPoint t(CoordNT(CsFT(0)), CoordNT(CsFT(2)));
    const CsCurve carc(circ, s, t);
    const Geom cconic = conic::make_from_circle_segment(
        make_geom(Kind::CircleSegment, GeomType::Curve, carc));
    CHECK_EQ(coeff_string(cconic), std::string("-1,-1,0,0,0,4"));
    CHECK_NEAR(px(O().xcurve_source(O().to_x_monotone(cconic))), 2.0, 1e-12);
    CHECK_NEAR(py(O().xcurve_target(O().to_x_monotone(cconic))), 2.0, 1e-12);

    // (c) rational line segment.
    const CsCurve cseg(CsK::Point_2(CsFT(0), CsFT(0)), CsK::Point_2(CsFT(4), CsFT(2)));
    const Geom cs2 =
        conic::make_from_circle_segment(make_geom(Kind::CircleSegment, GeomType::Curve, cseg));
    CHECK_EQ(coeff_string(cs2), std::string("0,0,0,1,-2,0"));

    // (d) segment with sqrt-extension endpoints on the line y = x: (0,0) -> (sqrt2, sqrt2).
    const CsPoint ls(CoordNT(CsFT(0)), CoordNT(CsFT(0)));
    const CsPoint lt(CoordNT(CsFT(0), CsFT(1), CsFT(2)), CoordNT(CsFT(0), CsFT(1), CsFT(2)));
    const CsCurve cline(CsK::Line_2(CsFT(1), CsFT(-1), CsFT(0)), ls, lt);
    const Geom cs3 =
        conic::make_from_circle_segment(make_geom(Kind::CircleSegment, GeomType::Curve, cline));
    CHECK_EQ(coeff_string(cs3), std::string("0,0,0,0,0,0"));   // special (algebraic) segment
    CHECK_NEAR(px(O().xcurve_target(O().to_x_monotone(cs3))), std::sqrt(2.0), 1e-15);

    // Wrong kind.
    CHECK_THROWS(ErrorCode::KindMismatch,
                 conic::make_from_circle_segment(conic::make_circle(Q(0), Q(0), Q(4), 1)));
  }
}

// =============================================================================================
// 5. x-monotone accessors, to_curve, traits functors
// =============================================================================================
static void test_xcurve_api() {
  section("x-monotone accessors");
  // Quarter of x^2 + 4y^2 = 4 from (2,0) to (0,1), counterclockwise: the arc runs right-to-left.
  const Geom a = conic::make_arc(Q(1), Q(4), Q(0), Q(0), Q(0), Q(-4), +1, CP(2, 0), CP(0, 1));
  const Geom x = O().to_x_monotone(a);
  CHECK_EQ(int(x.type), int(GeomType::XCurve));
  CHECK_NEAR(px(O().xcurve_source(x)), 2.0, 1e-12);
  CHECK_NEAR(py(O().xcurve_source(x)), 0.0, 1e-12);
  CHECK_NEAR(px(O().xcurve_target(x)), 0.0, 1e-12);
  CHECK_NEAR(py(O().xcurve_target(x)), 1.0, 1e-12);
  CHECK(O().xcurve_has_source(x));
  CHECK(O().xcurve_has_target(x));
  CHECK_NEAR(px(O().xcurve_min_vertex(x)), 0.0, 1e-12);   // min vertex == the target here
  CHECK_NEAR(px(O().xcurve_max_vertex(x)), 2.0, 1e-12);
  CHECK(!O().xcurve_is_vertical(x));
  CHECK(!O().xcurve_is_directed_right(x));                 // source (2,0) > target (0,1)
  CHECK_EQ(O().compare_endpoints_xy(x), 1);                // LARGER
  const Geom opp = O().construct_opposite(x);
  CHECK(O().xcurve_is_directed_right(opp));
  CHECK_EQ(O().compare_endpoints_xy(opp), -1);
  CHECK(O().curve_equal(x, opp));                          // Equal_2 is direction insensitive
  CHECK_EQ(O().parameter_space_in_x(x, 0), int(ARR_INTERIOR));
  CHECK_EQ(O().parameter_space_in_y(x, 1), int(ARR_INTERIOR));

  // conic::coefficients / orientation / is_full also accept an XCurve box.
  CHECK_EQ(coeff_string(x), coeff_string(a));
  CHECK_EQ(conic::orientation(x), 1);
  CHECK(!conic::is_full(x));
  CHECK_EQ(conic::conic_type(x), int(conic::ELLIPSE));

  // to_curve rebuilds an equivalent general Curve_2 (same stored coefficients and endpoints).
  const Geom back = O().to_curve(x);
  CHECK_EQ(int(back.type), int(GeomType::Curve));
  CHECK_EQ(coeff_string(back), coeff_string(a));
  CHECK_EQ(conic::orientation(back), conic::orientation(a));

  // to_curve on a special segment goes through the two-point overload (all-zero coefficients
  // cannot be fed back through the coefficient overload).
  const Geom alg_end = conic::make_point_algebraic(box_core_expr(alg_sqrt(Q(2))), box_rational(Q(1)));
  const Geom sseg = conic::make_segment(CP(0, 0), alg_end);
  const Geom sback = O().to_curve(O().to_x_monotone(sseg));
  CHECK_EQ(coeff_string(sback), std::string("0,0,0,0,0,0"));
  // ... and on a degree-1 arc with real (u, v, w) it goes through the coefficient overload.
  const Geom rseg = conic::make_segment(SP(0, 0), SP(4, 2));
  const Geom rback = O().to_curve(O().to_x_monotone(rseg));
  CHECK_EQ(coeff_string(rback), std::string("0,0,0,1,-2,0"));
  CHECK_EQ(conic::orientation(rback), 0);
  // ... and on one of the two x-monotone pieces of a full circle it yields a proper (non-full) arc
  // carrying that piece's endpoints.
  {
    const Geom circle = conic::make_circle(Q(0), Q(0), Q(4), +1);
    std::vector<Geom> mx;
    O().make_x_monotone(circle, mx);
    CHECK_EQ(mx.size(), std::size_t(2));
    const Geom piece_curve = O().to_curve(mx[0]);
    CHECK(!conic::is_full(piece_curve));
    CHECK_EQ(coeff_string(piece_curve), std::string("-1,-1,0,0,0,4"));
    CHECK_EQ(n_pieces(piece_curve), std::size_t(1));
  }

  // Split / merge / trim / intersect through the traits functors.
  {
    // Split the quarter arc at (sqrt(2), 1/sqrt(2)) -- instead use the rational point on
    // x^2+4y^2=4 with x = 6/5, y = 4/5: 36/25 + 4*16/25 = 36/25 + 64/25 = 100/25 = 4. OK.
    const Geom mid = CP(6, 5, 4, 5);
    CHECK_EQ(O().compare_y_at_x(mid, x), 0);
    CHECK(O().is_in_x_range(x, mid));
    Geom left, right;
    O().split(x, mid, left, right);
    CHECK_NEAR(px(O().xcurve_min_vertex(left)), 0.0, 1e-12);
    CHECK_NEAR(px(O().xcurve_max_vertex(left)), 1.2, 1e-12);
    CHECK_NEAR(px(O().xcurve_min_vertex(right)), 1.2, 1e-12);
    CHECK_NEAR(px(O().xcurve_max_vertex(right)), 2.0, 1e-12);
    CHECK(O().are_mergeable(left, right));
    const Geom merged = O().merge(left, right);
    CHECK(O().curve_equal(merged, x));
    // Trim between the two interior rational points (6/5,4/5) and (8/5,3/5)
    // (both satisfy x^2 + 4y^2 = 4: 36/25 + 64/25 = 4 and 64/25 + 36/25 = 4).
    const Geom tr = O().trim(x, mid, CP(8, 5, 3, 5));
    CHECK_NEAR(px(O().xcurve_min_vertex(tr)), 1.2, 1e-12);
    CHECK_NEAR(px(O().xcurve_max_vertex(tr)), 1.6, 1e-12);
  }
  {
    // The upper half of the ellipse x^2 + 4y^2 = 4 meets the horizontal segment y = 1/2 at
    // x = +-sqrt(3): 3 + 4*(1/4) = 4.
    const Geom full = conic::make_full(Q(1), Q(4), Q(0), Q(0), Q(0), Q(-4));
    std::vector<Geom> mx;
    O().make_x_monotone(full, mx);
    CHECK_EQ(mx.size(), std::size_t(2));
    const Geom seg = O().to_x_monotone(conic::make_segment(CP(-2, 1, 1, 2), CP(2, 1, 1, 2)));
    int total_points = 0;
    for (const Geom& piece : mx) {
      std::vector<IntersectionResult> is;
      O().intersect(piece, seg, is);
      for (const IntersectionResult& r : is)
        if (r.is_point) {
          ++total_points;
          CHECK_NEAR(std::fabs(px(r.point)), std::sqrt(3.0), 1e-12);
          CHECK_NEAR(py(r.point), 0.5, 1e-12);
        }
    }
    CHECK_EQ(total_points, 2);
  }
  // Construct_x_monotone_curve_2(p, q): the conic traits' "special segment" overload.
  {
    const Geom sx = O().construct_x_monotone_curve(CP(0, 0), CP(3, 4));
    CHECK_NEAR(px(O().xcurve_max_vertex(sx)), 3.0, 1e-12);
    CHECK_EQ(O().compare_y_at_x(CP(3, 2, 2, 1), sx), 0);   // (1.5, 2) is the midpoint
    CHECK_THROWS(ErrorCode::InvalidArgument, O().construct_x_monotone_curve(CP(1, 1), CP(1, 1)));
  }
  // Type errors.
  CHECK_THROWS(ErrorCode::NotXMonotone, O().xcurve_source(a));
  CHECK_THROWS(ErrorCode::NotXMonotone, O().to_curve(a));
  CHECK_THROWS(ErrorCode::KindMismatch,
               O().curve_bbox(make_geom(Kind::Segment, GeomType::Curve, int(0))));
}

// =============================================================================================
// 6. approximate()
// =============================================================================================
static void test_approximate() {
  section("approximate");
  // ---- an x-monotone arc: the quarter of x^2 + 4y^2 = 4 from (2,0) to (0,1), CCW ------------
  {
    const Geom a = conic::make_arc(Q(1), Q(4), Q(0), Q(0), Q(0), Q(-4), +1, CP(2, 0), CP(0, 1));
    const Geom x = O().to_x_monotone(a);
    const double tol = 1e-3;
    std::vector<double> pts;
    O().approximate(x, tol, nullptr, pts);
    CHECK(pts.size() >= 4 && pts.size() % 2 == 0);
    // The polyline follows the arc from SOURCE to TARGET (ops.hpp contract).
    CHECK_NEAR(pts[0], 2.0, 1e-12);
    CHECK_NEAR(pts[1], 0.0, 1e-12);
    CHECK_NEAR(pts[pts.size() - 2], 0.0, 1e-12);
    CHECK_NEAR(pts[pts.size() - 1], 1.0, 1e-12);
    // Every vertex lies ON the ellipse (Approximate_2 samples the exact parametrisation):
    // distance ~ |x^2 + 4y^2 - 4| / |grad| with grad = (2x, 8y).
    double worst_on = 0.0;
    for (std::size_t i = 0; i + 1 < pts.size(); i += 2) {
      const double X = pts[i], Y = pts[i + 1];
      const double f = X * X + 4 * Y * Y - 4.0;
      const double g = std::sqrt(4 * X * X + 64 * Y * Y);
      if (g > 0) worst_on = std::max(worst_on, std::fabs(f) / g);
    }
    CHECK_MSG(worst_on < 1e-9, "worst vertex-to-curve distance = " << worst_on);
    // And every chord midpoint is within `tol` of the ellipse (the documented Hausdorff bound).
    double worst_mid = 0.0;
    for (std::size_t i = 0; i + 3 < pts.size(); i += 2) {
      const double X = 0.5 * (pts[i] + pts[i + 2]);
      const double Y = 0.5 * (pts[i + 1] + pts[i + 3]);
      const double f = X * X + 4 * Y * Y - 4.0;
      const double g = std::sqrt(4 * X * X + 64 * Y * Y);
      if (g > 0) worst_mid = std::max(worst_mid, std::fabs(f) / g);
    }
    CHECK_MSG(worst_mid <= tol, "worst chord-midpoint deviation = " << worst_mid);
    // A coarser tolerance must give fewer points.
    std::vector<double> coarse;
    O().approximate(x, 1e-1, nullptr, coarse);
    CHECK(coarse.size() < pts.size());
    // approximate_length of the quarter ellipse (x = 2 cos t, y = sin t, t in [0, pi/2]) is
    //   int_0^{pi/2} sqrt(4 sin^2 t + cos^2 t) dt = 2 * E(m = 3/4) = 2 * 1.2110560... = 2.4221121
    // (E = complete elliptic integral of the 2nd kind with parameter m = e^2 = 1 - b^2/a^2 = 3/4).
    const double len = O().approximate_length(x, 1e-5);
    CHECK_NEAR(len, 2.4221121, 1e-3);
  }
  // ---- a full circle: the chained pieces must close up ---------------------------------------
  {
    const Geom c = conic::make_circle(Q(0), Q(0), Q(4), +1);
    const double tol = 1e-3;
    std::vector<double> pts;
    O().approximate(c, tol, nullptr, pts);
    CHECK(pts.size() >= 8);
    // Starts and ends at the same point (the polyline is closed).
    CHECK_NEAR(pts[0], pts[pts.size() - 2], 1e-12);
    CHECK_NEAR(pts[1], pts[pts.size() - 1], 1e-12);
    // The x-monotone split of a full circle happens at the two vertical tangency points.
    CHECK_NEAR(std::fabs(pts[0]), 2.0, 1e-12);
    CHECK_NEAR(pts[1], 0.0, 1e-12);
    double worst_on = 0.0, worst_mid = 0.0;
    for (std::size_t i = 0; i + 1 < pts.size(); i += 2)
      worst_on = std::max(worst_on, std::fabs(std::hypot(pts[i], pts[i + 1]) - 2.0));
    for (std::size_t i = 0; i + 3 < pts.size(); i += 2)
      worst_mid = std::max(worst_mid, 2.0 - std::hypot(0.5 * (pts[i] + pts[i + 2]),
                                                       0.5 * (pts[i + 1] + pts[i + 3])));
    CHECK_MSG(worst_on < 1e-9, "worst vertex-to-circle distance = " << worst_on);
    CHECK_MSG(worst_mid <= tol, "worst chord-midpoint deviation = " << worst_mid);
    // Circumference of a circle of radius 2 is 4*pi = 12.566370614359172.
    CHECK_NEAR(O().approximate_length(c, 1e-6), 4.0 * M_PI, 1e-4);
  }
  // ---- a segment: exactly its two endpoints ----------------------------------------------------
  {
    const Geom s = O().to_x_monotone(conic::make_segment(CP(0, 0), CP(4, 2)));
    std::vector<double> pts;
    O().approximate(s, 1e-3, nullptr, pts);
    CHECK_EQ(pts.size(), std::size_t(4));
    CHECK_NEAR(pts[0], 0.0, 0.0);
    CHECK_NEAR(pts[3], 2.0, 0.0);
    CHECK_NEAR(O().approximate_length(s, 1e-3), std::sqrt(20.0), 1e-12);
  }
  // ---- tolerance validation (error <= 0 SIGSEGVs inside CGAL: rendering_and_approximation.md 7)
  {
    const Geom s = O().to_x_monotone(conic::make_segment(CP(0, 0), CP(4, 2)));
    std::vector<double> pts;
    CHECK_THROWS(ErrorCode::InvalidArgument, O().approximate(s, 0.0, nullptr, pts));
    CHECK_THROWS(ErrorCode::InvalidArgument, O().approximate(s, -1.0, nullptr, pts));
    CHECK_THROWS(ErrorCode::InvalidArgument,
                 O().approximate(s, std::nan(""), nullptr, pts));
    // A tiny tolerance is clamped to 1e-12 instead of recursing forever.
    pts.clear();
    O().approximate(s, 1e-300, nullptr, pts);
    CHECK_EQ(pts.size(), std::size_t(4));
  }
}

// =============================================================================================
// 7. convert_curve from the other kinds (raw CGAL objects boxed here)
// =============================================================================================
static void test_convert_curve() {
  section("convert_curve");
  using EpeckFTs = SegmentTypes::FT;
  const SegmentTypes::Point_2 a(EpeckFTs(0), EpeckFTs(0));
  const SegmentTypes::Point_2 b(EpeckFTs(4), EpeckFTs(2));

  // ---- Segment ------------------------------------------------------------------------------
  {
    const SegmentTypes::Curve_2 seg(a, b);
    std::vector<Geom> out;
    O().convert_curve(make_geom(Kind::Segment, GeomType::XCurve, seg), out);
    CHECK_EQ(out.size(), std::size_t(1));
    CHECK_EQ(int(out[0].kind), int(Kind::Conic));
    CHECK_EQ(coeff_string(out[0]), std::string("0,0,0,1,-2,0"));
  }
  // ---- Linear: a segment converts, a ray does not ---------------------------------------------
  {
    const LinearTypes::Curve_2 lseg((LinearTypes::Kernel::Segment_2(a, b)));
    std::vector<Geom> out;
    O().convert_curve(make_geom(Kind::Linear, GeomType::XCurve, lseg), out);
    CHECK_EQ(out.size(), std::size_t(1));
    CHECK_EQ(coeff_string(out[0]), std::string("0,0,0,1,-2,0"));
    const LinearTypes::Curve_2 lray((LinearTypes::Kernel::Ray_2(a, b)));
    std::vector<Geom> out2;
    CHECK_THROWS(ErrorCode::Unsupported,
                 O().convert_curve(make_geom(Kind::Linear, GeomType::XCurve, lray), out2));
  }
  // ---- Polyline: one conic segment per subcurve -------------------------------------------------
  {
    const SegmentTypes::Point_2 c(EpeckFTs(6), EpeckFTs(-1));
    std::vector<PolylineTypes::Segment_2> segs;
    segs.push_back(PolylineTypes::Segment_2(a, b));
    segs.push_back(PolylineTypes::Segment_2(b, c));
    const PolylineTypes::Curve_2 pl(segs.begin(), segs.end());
    std::vector<Geom> out;
    O().convert_curve(make_geom(Kind::Polyline, GeomType::Curve, pl), out);
    CHECK_EQ(out.size(), std::size_t(2));
    CHECK_EQ(coeff_string(out[0]), std::string("0,0,0,1,-2,0"));
    // second segment (4,2) -> (6,-1): (u,v,w) = (y2-y1, x1-x2, x2 y1 - x1 y2)
    //                               = (-3, -2, 6*2 - 4*(-1)) = (-3, -2, 16)
    CHECK_EQ(coeff_string(out[1]), std::string("0,0,0,-3,-2,16"));
    CHECK_NEAR(px(O().xcurve_target(O().to_x_monotone(out[1]))), 6.0, 1e-12);
  }
  // ---- CircleSegment -----------------------------------------------------------------------------
  {
    using CsK = CircleSegmentTypes::Kernel;
    using CsFT = CircleSegmentTypes::FT;
    const CsK::Circle_2 circ(CsK::Point_2(CsFT(0), CsFT(0)), CsFT(4));
    const CircleSegmentTypes::Curve_2 full(circ);
    std::vector<Geom> out;
    O().convert_curve(make_geom(Kind::CircleSegment, GeomType::Curve, full), out);
    CHECK_EQ(out.size(), std::size_t(1));
    CHECK(conic::is_full(out[0]));
    CHECK_EQ(coeff_string(out[0]), std::string("-1,-1,0,0,0,4"));
  }
  // ---- Bezier: degree 1 and degree 2 --------------------------------------------------------------
  {
    using BRat = BezierTypes::Rational;
    std::vector<BezierTypes::Rat_point_2> cps;
    cps.push_back(BezierTypes::Rat_point_2(BRat(0), BRat(0)));
    cps.push_back(BezierTypes::Rat_point_2(BRat(4), BRat(2)));
    const BezierTypes::Curve_2 lin(cps.begin(), cps.end());
    std::vector<Geom> out;
    O().convert_curve(make_geom(Kind::Bezier, GeomType::Curve, lin), out);
    CHECK_EQ(out.size(), std::size_t(1));
    CHECK_EQ(coeff_string(out[0]), std::string("0,0,0,1,-2,0"));

    // A polynomial quadratic Bezier is the rational one with weights (1,1,1) -> the same parabola
    // as in test_constructors().
    std::vector<BezierTypes::Rat_point_2> q;
    q.push_back(BezierTypes::Rat_point_2(BRat(0), BRat(0)));
    q.push_back(BezierTypes::Rat_point_2(BRat(1), BRat(2)));
    q.push_back(BezierTypes::Rat_point_2(BRat(3), BRat(1)));
    const BezierTypes::Curve_2 quad(q.begin(), q.end());
    std::vector<Geom> out2;
    O().convert_curve(make_geom(Kind::Bezier, GeomType::Curve, quad), out2);
    CHECK_EQ(out2.size(), std::size_t(1));
    CHECK_EQ(coeff_string(out2[0]), std::string("9,1,6,-40,20,0"));
    CHECK_EQ(conic::conic_type(out2[0]), int(conic::PARABOLA));

    // A cubic is not a conic.
    std::vector<BezierTypes::Rat_point_2> cub = q;
    cub.push_back(BezierTypes::Rat_point_2(BRat(5), BRat(-1)));
    const BezierTypes::Curve_2 cubic(cub.begin(), cub.end());
    std::vector<Geom> out3;
    CHECK_THROWS(ErrorCode::NotRepresentable,
                 O().convert_curve(make_geom(Kind::Bezier, GeomType::Curve, cubic), out3));
  }
  // ---- Conic -> Conic is the identity, sphere is refused --------------------------------------
  {
    const Geom c = conic::make_circle(Q(0), Q(0), Q(4), +1);
    std::vector<Geom> out;
    O().convert_curve(c, out);
    CHECK_EQ(out.size(), std::size_t(1));
    CHECK(conic::is_full(out[0]));
    std::vector<Geom> out2;
    CHECK_THROWS(ErrorCode::KindMismatch,
                 O().convert_curve(make_geom(Kind::Sphere, GeomType::Curve, int(1)), out2));
  }
}

// =============================================================================================
// 8. the arrangement round trip
// =============================================================================================
static void test_arrangement() {
  section("arrangement");
  std::unique_ptr<ArrBase> arr = make_arrangement(Kind::Conic);
  CHECK_EQ(int(arr->kind()), int(Kind::Conic));
  CHECK(arr->is_empty());
  CHECK_EQ(arr->number_of_faces(), std::size_t(1));

  // (1) full circle C1: x^2 + y^2 = 4 (counterclockwise).
  //     Inserting a full conic splits it at its two vertical tangency points (-2,0) and (2,0)
  //     => V = 2, E = 2, F = 2 (inside + unbounded).
  const Geom c1 = conic::make_circle(Q(0), Q(0), Q(4), +1);
  const CH h1 = arr->insert_curve(c1);
  CHECK(arr->curve_valid(h1));
  CHECK_EQ(arr->number_of_vertices(), std::size_t(2));
  CHECK_EQ(arr->number_of_edges(), std::size_t(2));
  CHECK_EQ(arr->number_of_faces(), std::size_t(2));
  CHECK(arr->is_valid());

  // (2) full circle C2: (x-2)^2 + y^2 = 4 (counterclockwise).
  //     Its tangency points are (0,0) and (4,0); C1 and C2 meet at x = 1, y = +-sqrt(3).
  //     V = 2 + 2 + 2 = 6.  Each circle is cut into 4 arcs => E = 8.
  //     Euler (connected, planar, incl. the unbounded face): F = 2 - V + E = 4
  //     (unbounded, left lune, lens, right lune).
  const Geom c2 = conic::make_circle(Q(2), Q(0), Q(4), +1);
  const CH h2 = arr->insert_curve(c2);
  CHECK_EQ(arr->number_of_vertices(), std::size_t(6));
  CHECK_EQ(arr->number_of_edges(), std::size_t(8));
  CHECK_EQ(arr->number_of_faces(), std::size_t(4));
  CHECK_EQ(arr->number_of_unbounded_faces(), std::size_t(1));
  CHECK(arr->is_valid());

  // (3) the segment y = 0 from (-3,0) to (5,0).  It passes through (-2,0), (0,0), (2,0), (4,0),
  //     i.e. all four tangency vertices, and adds its own two endpoints.
  //     V = 6 + 2 = 8;  E = 8 arcs + 5 segment pieces = 13;  F = 2 - 8 + 13 = 7
  //     (unbounded + the 3 circle regions each cut in half by y = 0).
  const Geom c3 = conic::make_segment(SP(-3, 0), SP(5, 0));
  const CH h3 = arr->insert_curve(c3);
  CHECK_EQ(arr->number_of_vertices(), std::size_t(8));
  CHECK_EQ(arr->number_of_edges(), std::size_t(13));
  CHECK_EQ(arr->number_of_halfedges(), std::size_t(26));
  CHECK_EQ(arr->number_of_faces(), std::size_t(7));
  CHECK_EQ(arr->number_of_unbounded_faces(), std::size_t(1));
  CHECK_EQ(arr->number_of_curves(), std::size_t(3));
  CHECK_EQ(arr->number_of_isolated_vertices(), std::size_t(0));
  CHECK_EQ(arr->number_of_vertices_at_infinity(), std::size_t(0));
  CHECK(arr->is_valid());

  // ---- iteration -------------------------------------------------------------------------------
  std::vector<VH> vs;
  std::vector<HH> hs, es;
  std::vector<FH> fs;
  std::vector<CH> cs;
  arr->vertices(vs);
  arr->halfedges(hs);
  arr->edges(es);
  arr->faces(fs);
  arr->curves(cs);
  CHECK_EQ(vs.size(), std::size_t(8));
  CHECK_EQ(hs.size(), std::size_t(26));
  CHECK_EQ(es.size(), std::size_t(13));
  CHECK_EQ(fs.size(), std::size_t(7));
  CHECK_EQ(cs.size(), std::size_t(3));
  // Vertex degrees: (-3,0) and (5,0) have degree 1; the four tangency vertices have degree 3
  // (two arcs + one segment piece... (-2,0),(2,0),(0,0),(4,0) each meet 2 circle arcs and 2
  // segment pieces except the two extreme ones); the two circle-circle vertices have degree 4.
  std::size_t degree_sum = 0;
  for (const VH& v : vs) degree_sum += arr->vertex_degree(v);
  CHECK_EQ(degree_sum, std::size_t(26));    // sum of degrees == number of halfedges
  // History: the segment induced 5 edges, each circle 4.
  CHECK_EQ(arr->number_of_induced_edges(h1), std::size_t(4));
  CHECK_EQ(arr->number_of_induced_edges(h2), std::size_t(4));
  CHECK_EQ(arr->number_of_induced_edges(h3), std::size_t(5));
  std::vector<HH> induced;
  arr->induced_edges(h3, induced);
  CHECK_EQ(induced.size(), std::size_t(5));
  for (const HH& h : induced) CHECK_EQ(arr->number_of_originating_curves(h), std::size_t(1));

  // ---- point location -------------------------------------------------------------------------
  // (1, 1/2) is inside both circles (1 + 1/4 < 4 and 1 + 1/4 < 4) and above y = 0
  //   -> the upper half of the lens, a bounded face.
  const Geom inside = CP(1, 1, 1, 2);
  const Located base = arr->locate(inside, PL_DEFAULT);
  CHECK_EQ(base.type, 2);
  CHECK(!arr->face_is_unbounded(base.as_face()));
  const int strategies[] = {PL_NAIVE, PL_SIMPLE, PL_WALK, PL_LANDMARKS, PL_TRAPEZOID,
                            PL_TRIANGULATION};
  for (int s : strategies) {
    if (arr->supports_point_location(s)) {
      const Located l = arr->locate(inside, s);
      CHECK_MSG(l.type == 2 && l.p == base.p,
                "strategy " << point_location_name(s) << " disagreed");
      arr->attach_point_location(s);
      CHECK(arr->has_point_location(s));
      const Located l2 = arr->locate(inside, s);
      CHECK(l2.p == base.p);
      arr->detach_point_location(s);
      CHECK(!arr->has_point_location(s));
    } else {
      // The conic kind supports every strategy except triangulation (KindPolicy<ConicTypes>).
      CHECK_EQ(s, int(PL_TRIANGULATION));
      CHECK_THROWS(ErrorCode::Unsupported, arr->locate(inside, s));
      CHECK_THROWS(ErrorCode::Unsupported, arr->attach_point_location(s));
    }
  }
  // A vertex and an edge.
  CHECK_EQ(arr->locate(CP(2, 0), PL_WALK).type, 0);        // the tangency vertex (2,0)
  CHECK_EQ(arr->locate(CP(3, 0), PL_WALK).type, 1);        // interior of a segment piece
  CHECK_EQ(arr->locate(CP(10, 10), PL_WALK).type, 2);
  CHECK(arr->face_is_unbounded(arr->locate(CP(10, 10), PL_WALK).as_face()));

  // Vertical ray shooting: from (1, 1/2) upwards we hit the upper arc of one of the circles.
  const Located up = arr->ray_shoot_up(inside, PL_WALK);
  CHECK(up.type == 1 || up.type == 0);
  const Located down = arr->ray_shoot_down(inside, PL_WALK);
  CHECK_EQ(down.type, 1);                                   // the segment y = 0

  // ---- batched locate ---------------------------------------------------------------------------
  {
    std::vector<Geom> pts;
    pts.push_back(CP(10, 10));      // unbounded face
    pts.push_back(inside);          // bounded face
    pts.push_back(CP(2, 0));        // vertex
    pts.push_back(CP(3, 0));        // halfedge
    pts.push_back(CP(10, 10));      // duplicate, must be handled positionally
    std::vector<Located> res;
    arr->batched_locate(pts, res);
    CHECK_EQ(res.size(), std::size_t(5));
    CHECK_EQ(res[0].type, 2);
    CHECK(arr->face_is_unbounded(res[0].as_face()));
    CHECK_EQ(res[1].type, 2);
    CHECK(!arr->face_is_unbounded(res[1].as_face()));
    CHECK_EQ(res[2].type, 0);
    CHECK_EQ(res[3].type, 1);
    CHECK(res[4].p == res[0].p);
  }

  // ---- he_curve / he_directed_curve consistency around a face ------------------------------------
  {
    // Pick a bounded face and walk its outer CCB: the directed curve of every halfedge must go
    // from the halfedge's source point to its target point, and the targets must chain.
    FH bounded;
    bool found = false;
    for (const FH& f : fs) {
      if (!arr->face_is_unbounded(f) && arr->face_has_outer_ccb(f)) { bounded = f; found = true; break; }
    }
    CHECK(found);
    std::vector<HH> ccb;
    arr->he_ccb(arr->face_outer_ccb(bounded), ccb);
    CHECK(ccb.size() >= 2);
    for (std::size_t i = 0; i < ccb.size(); ++i) {
      const HH& h = ccb[i];
      const Geom stored = arr->he_curve(h);
      CHECK_EQ(int(stored.kind), int(Kind::Conic));
      const Geom dir = arr->he_directed_curve(h);
      const Geom sp = arr->vertex_point(arr->he_source(h));
      const Geom tp = arr->vertex_point(arr->he_target(h));
      CHECK(O().point_equal(O().xcurve_source(dir), sp));
      CHECK(O().point_equal(O().xcurve_target(dir), tp));
      // The stored curve is the same geometry, possibly reversed.
      CHECK(O().curve_equal(stored, dir));
      // targets chain: target(h_i) == source(h_{i+1})
      const HH& nx = ccb[(i + 1) % ccb.size()];
      CHECK(O().point_equal(tp, arr->vertex_point(arr->he_source(nx))));
      CHECK_EQ(arr->he_face(h).p, bounded.p);
    }
    // face_polygon delivers exactly those directed curves.
    std::vector<Geom> outer;
    std::vector<std::vector<Geom>> holes;
    arr->face_polygon(bounded, outer, holes);
    CHECK_EQ(outer.size(), ccb.size());
    CHECK_EQ(holes.size(), std::size_t(0));
    for (const Geom& g : outer) CHECK_EQ(int(g.type), int(GeomType::XCurve));
  }

  // ---- zone / do_intersect ------------------------------------------------------------------------
  {
    // A vertical segment x = 1 from (1,-3) to (1,3) crosses both circles.
    const Geom v = conic::make_segment(CP(1, -3), CP(1, 3));
    std::vector<Located> zone;
    arr->zone(v, zone);
    CHECK(zone.size() >= 5);
    CHECK_EQ(zone.front().type, 2);        // starts in the unbounded face
    CHECK(arr->do_intersect(v));
    // ... and the arrangement is unchanged by zone()/do_intersect().
    CHECK_EQ(arr->number_of_edges(), std::size_t(13));
    const Geom far = conic::make_segment(CP(20, 20), CP(30, 30));
    CHECK(!arr->do_intersect(far));
  }

  // ---- vertical decomposition ----------------------------------------------------------------------
  {
    std::vector<VerticalDecompositionEntry> dec;
    arr->decompose(dec);
    CHECK_EQ(dec.size(), std::size_t(8));    // one entry per vertex
    for (const VerticalDecompositionEntry& d : dec) CHECK(arr->vertex_valid(d.v));
  }

  // ---- bulk export -------------------------------------------------------------------------------
  {
    std::vector<double> coords;
    arr->vertex_coordinates(coords);
    CHECK_EQ(coords.size(), std::size_t(16));           // 8 vertices x 2
    std::vector<std::size_t> idx;
    arr->edge_vertex_indices(idx);
    CHECK_EQ(idx.size(), std::size_t(26));              // 13 edges x 2
    std::vector<std::vector<std::vector<std::size_t>>> fb;
    arr->face_boundaries(fb);
    CHECK_EQ(fb.size(), std::size_t(7));
    const BBox bb = arr->bbox();
    CHECK_NEAR(bb.lo[0], -3.0, 1e-12);
    CHECK_NEAR(bb.hi[0], 5.0, 1e-12);
    CHECK_NEAR(bb.lo[1], -std::sqrt(3.0), 1e-12);
    CHECK_NEAR(bb.hi[1], std::sqrt(3.0), 1e-12);
  }

  // ---- clone --------------------------------------------------------------------------------------
  {
    std::unique_ptr<ArrBase> cl = arr->clone();
    CHECK_EQ(cl->number_of_vertices(), std::size_t(8));
    CHECK_EQ(cl->number_of_edges(), std::size_t(13));
    CHECK_EQ(cl->number_of_faces(), std::size_t(7));
    CHECK_EQ(cl->number_of_curves(), std::size_t(3));
    CHECK(cl->is_valid());
    // Handles of the original are foreign to the clone.
    CHECK(!cl->vertex_valid(vs[0]));
    CHECK(arr->vertex_valid(vs[0]));
  }

  // ---- remove_curve --------------------------------------------------------------------------------
  {
    const std::size_t removed = arr->remove_curve(h3);
    CHECK_EQ(removed, std::size_t(5));
    CHECK_EQ(arr->number_of_curves(), std::size_t(2));
    CHECK_EQ(arr->number_of_edges(), std::size_t(8));
    CHECK_EQ(arr->number_of_vertices(), std::size_t(6));
    CHECK_EQ(arr->number_of_faces(), std::size_t(4));
    CHECK(arr->is_valid());
    CHECK(!arr->curve_valid(h3));
    CHECK_THROWS(ErrorCode::InvalidHandle, arr->remove_curve(h3));
  }

  // ---- insert an x-monotone curve directly (goes through KindOps::to_curve) --------------------------
  {
    std::unique_ptr<ArrBase> a2 = make_arrangement(Kind::Conic);
    const Geom arc = conic::make_arc(Q(1), Q(4), Q(0), Q(0), Q(0), Q(-4), +1, CP(2, 0), CP(0, 1));
    const Geom x = O().to_x_monotone(arc);
    a2->insert_curve(x);                                   // XCurve box -> ops().to_curve()
    CHECK_EQ(a2->number_of_vertices(), std::size_t(2));
    CHECK_EQ(a2->number_of_edges(), std::size_t(1));
    CHECK_EQ(a2->number_of_faces(), std::size_t(1));
    CHECK(a2->is_valid());
    // insert_point / insert_non_intersecting_curve.
    const VH iso = a2->insert_point(CP(5, 5));
    CHECK(a2->vertex_is_isolated(iso));
    CHECK_EQ(a2->number_of_isolated_vertices(), std::size_t(1));
    const Geom seg = O().to_x_monotone(conic::make_segment(CP(10, 0), CP(12, 0)));
    const HH nh = a2->insert_non_intersecting_curve(seg);
    CHECK(a2->halfedge_valid(nh));
    CHECK_EQ(a2->number_of_edges(), std::size_t(2));
    CHECK(a2->is_valid());
  }

  // ---- kind mismatch --------------------------------------------------------------------------------
  {
    const SegmentTypes::Point_2 ep(SegmentTypes::FT(0), SegmentTypes::FT(0));
    CHECK_THROWS(ErrorCode::KindMismatch,
                 arr->locate(make_geom(Kind::Segment, GeomType::Point, ep), PL_WALK));
  }
}

// =============================================================================================
int main() {
  std::cout << "arr2d conic kind test\n";
  std::cout << "  " << build_info() << "\n";
  init_all_kinds();

  test_points();
  test_convert_point();
  test_hyperbolic_gate();
  test_constructors();
  test_xcurve_api();
  test_approximate();
  test_convert_curve();
  test_arrangement();

  std::cout << "\n=== " << g_checks << " checks, " << g_fails << " failures ===\n";
  // Returning normally is part of the test: nothing holding a CORE::Expr may be destroyed after
  // CORE's memory pools (traits_conic.md §13.11).
  return g_fails == 0 ? 0 : 1;
}
