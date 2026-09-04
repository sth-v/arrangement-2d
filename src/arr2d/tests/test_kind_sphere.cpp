// arr2d — self-contained C++ test for Kind::Sphere (src/arr2d/src/kind_sphere.cpp).
//
// Every expected number below is either hand-derived (the derivation is in the comment) or
// quoted from docs/dev/cgal61_api/traits_geodesic_sphere.md / rendering_and_approximation.md,
// which measured it against the installed CGAL 6.1 headers.
//
// ---------------------------------------------------------------------------------------
// BUILD AND RUN
//
//   S=<repo>/src/arr2d
//   O=<scratch>
//   CXX="/usr/bin/clang++ -std=c++17 -O0 -g -DCGAL_USE_CORE -DCGAL_USE_GMP -DCGAL_USE_MPFR \
//        -I/opt/homebrew/include -I$S/include"
//   # (run these in bash/sh, which word-splits $CXX; in zsh write ${=CXX})
//
//   # 1. the four objects this test links (kinds self-register through static registrars,
//   #    so linking only the sphere kind is enough):
//   $CXX -c $S/src/registry.cpp    -o $O/registry.o
//   $CXX -c $S/src/numbers.cpp     -o $O/numbers.o
//   $CXX -c $S/src/overlay.cpp     -o $O/overlay.o
//   $CXX -c $S/src/kind_sphere.cpp -o $O/kind_sphere.o
//
//   # 2a. the normal build — WITHOUT any Boolean-set-operations TU:
//   $CXX $S/tests/test_kind_sphere.cpp $O/registry.o $O/numbers.o $O/overlay.o \
//        $O/kind_sphere.o -L/opt/homebrew/lib -lgmp -lmpfr -o $O/test_kind_sphere
//   $O/test_kind_sphere            # must print "0 failures" and exit 0
//
//   # 2b. the same build with the BSO stub define (accepted for uniformity with the other
//   #     kind tests; it is a NO-OP here):
//   $CXX -DARR2D_TEST_STUB_BSO $S/tests/test_kind_sphere.cpp $O/registry.o $O/numbers.o \
//        $O/overlay.o $O/kind_sphere.o -L/opt/homebrew/lib -lgmp -lmpfr -o $O/test_kind_sphere
//
// WHY 2b IS A NO-OP FOR THIS KIND: the sphere has no Boolean set operations, so
// kind_sphere.cpp registers `KindEntry::make_polygon_set == nullptr` and never *names* any
// `arr2d::make_polygon_set_*` symbol.  There is therefore nothing to stub out and no bso TU to
// link; `arr2d::bso.hpp` does not even declare a sphere factory.  The `#ifdef
// ARR2D_TEST_STUB_BSO` block below is intentionally empty and exists only so that the two
// command lines above both work.
//
// EXPECTED STDERR: two blocks of CGAL "precondition violation! p1.is_no_boundary()" text.
// They come from CGAL's DEFAULT error handler, which prints before throwing; the test catches
// the resulting CGAL::Precondition_exception and counts it as a PASS (see the two
// "documented CGAL trap" sections).  The production Cython layer installs a silent handler
// (CGAL_TRAPS_CHECKLIST.md "Process / build").
// ---------------------------------------------------------------------------------------
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <vector>

#include "arr2d/arrangement.hpp"
#include "arr2d/common.hpp"
#include "arr2d/numbers.hpp"
#include "arr2d/ops.hpp"
#include "arr2d/registry.hpp"

// Only headers of another kind, to build a SOURCE-kind object for convert_point/convert_curve.
// Linking kind_segment.cpp is deliberately NOT required: the raw CGAL object is boxed by hand
// with make_geom(Kind::Segment, ...), so no SegmentTypes::traits() definition is needed.
#include "arr2d/kinds/segment_types.hpp"

#ifdef ARR2D_TEST_STUB_BSO
// Intentionally empty — see the file header: the sphere kind registers a null polygon-set
// factory and references no arr2d::make_polygon_set_* symbol.
#endif

using namespace arr2d;

// ---------------------------------------------------------------------------
// tiny check framework
// ---------------------------------------------------------------------------
namespace {

int g_checks = 0;
int g_failures = 0;
const char* g_section = "";

void section(const char* s) {
  g_section = s;
  std::printf("\n== %s ==\n", s);
}

void check(bool ok, const std::string& what) {
  ++g_checks;
  if (!ok) {
    ++g_failures;
    std::printf("  FAIL [%s] %s\n", g_section, what.c_str());
  }
}

void check_eq_sz(std::size_t got, std::size_t want, const std::string& what) {
  check(got == want, what + ": got " + std::to_string(got) + ", want " + std::to_string(want));
}

void check_eq_int(int got, int want, const std::string& what) {
  check(got == want, what + ": got " + std::to_string(got) + ", want " + std::to_string(want));
}

void check_near(double got, double want, double tol, const std::string& what) {
  const bool ok = std::abs(got - want) <= tol;
  if (!ok) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s: got %.17g, want %.17g (tol %g)", what.c_str(), got, want, tol);
    check(false, buf);
  } else {
    check(true, what);
  }
}

/// Runs `fn`, expecting an arr2d::Error with the given code.
template <class F>
void expect_error(ErrorCode code, F fn, const std::string& what) {
  try {
    fn();
  } catch (const Error& e) {
    check(e.code == code,
          what + ": wrong ErrorCode (got " + std::to_string(int(e.code)) + ", want " +
              std::to_string(int(code)) + "); message: " + e.what());
    return;
  } catch (const std::exception& e) {
    check(false, what + ": expected arr2d::Error, got " + std::string(e.what()));
    return;
  }
  check(false, what + ": no exception was thrown");
}

/// Runs `fn`, expecting ANY std::exception (used for the CGAL preconditions we deliberately
/// let propagate).
template <class F>
void expect_any_throw(F fn, const std::string& what) {
  try {
    fn();
  } catch (const std::exception&) {
    check(true, what);
    return;
  }
  check(false, what + ": no exception was thrown");
}

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------
Geom Pt(long x, long y, long z) {
  return sphere::make_point(Rational(x), Rational(y), Rational(z));
}

std::string xyz_str(const KindOps& o, const Geom& p) {
  double v[3];
  o.point_approx(p, v);
  char buf[128];
  std::snprintf(buf, sizeof(buf), "(%.6f, %.6f, %.6f)", v[0], v[1], v[2]);
  return buf;
}

struct V3 {
  double x = 0, y = 0, z = 0;
};
V3 at(const std::vector<double>& flat, std::size_t i) {
  return V3{flat[3 * i], flat[3 * i + 1], flat[3 * i + 2]};
}
double dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
double norm(const V3& a) { return std::sqrt(dot(a, a)); }

/// Checks that `flat` (3 doubles per point) is a valid polyline approximation of the geodesic
/// arc `xc`: unit-length samples, all on the arc's supporting great circle, endpoints exactly
/// the arc's endpoints, monotone angular progression, and every chord's sagitta <= tolerance.
void check_arc_approximation(const KindOps& o, const Geom& xc, double tol,
                             const std::vector<double>& flat, const std::string& tag) {
  const std::size_t n = flat.size() / 3;
  check(flat.size() % 3 == 0, tag + ": 3 doubles per point");
  check(n >= 2, tag + ": at least the two endpoints");
  if (n < 2) return;

  double src[3], tgt[3], nrm[3];
  o.point_approx(o.xcurve_source(xc), src);
  o.point_approx(o.xcurve_target(xc), tgt);
  o.point_approx(sphere::normal(xc), nrm);
  const V3 N{nrm[0], nrm[1], nrm[2]};

  check_near(flat[0], src[0], 1e-12, tag + ": first point is the source (x)");
  check_near(flat[1], src[1], 1e-12, tag + ": first point is the source (y)");
  check_near(flat[2], src[2], 1e-12, tag + ": first point is the source (z)");
  check_near(flat[3 * (n - 1) + 0], tgt[0], 1e-12, tag + ": last point is the target (x)");
  check_near(flat[3 * (n - 1) + 1], tgt[1], 1e-12, tag + ": last point is the target (y)");
  check_near(flat[3 * (n - 1) + 2], tgt[2], 1e-12, tag + ": last point is the target (z)");

  double max_off_sphere = 0.0, max_off_plane = 0.0, max_sagitta = 0.0, total_angle = 0.0;
  for (std::size_t i = 0; i < n; ++i) {
    const V3 p = at(flat, i);
    max_off_sphere = std::max(max_off_sphere, std::abs(norm(p) - 1.0));
    max_off_plane = std::max(max_off_plane, std::abs(dot(p, N)));
    if (i + 1 < n) {
      const V3 q = at(flat, i + 1);
      double c = dot(p, q);
      c = std::max(-1.0, std::min(1.0, c));
      const double dtheta = std::acos(c);
      total_angle += dtheta;
      // sagitta of a chord subtending dtheta on the unit sphere
      max_sagitta = std::max(max_sagitta, 1.0 - std::cos(0.5 * dtheta));
    }
  }
  check(max_off_sphere <= 1e-12, tag + ": every sample is a unit vector (max deviation " +
                                     std::to_string(max_off_sphere) + ")");
  check(max_off_plane <= 1e-12, tag + ": every sample lies on the supporting great circle "
                                      "(max |p.n| " + std::to_string(max_off_plane) + ")");
  check(max_sagitta <= tol, tag + ": max chord sagitta " + std::to_string(max_sagitta) +
                                " <= tolerance " + std::to_string(tol));
  // The samples are equally spaced, so the summed chord angles reproduce the arc's own angle.
  const double true_angle = std::acos(std::max(-1.0, std::min(1.0, src[0] * tgt[0] + src[1] * tgt[1] +
                                                                        src[2] * tgt[2])));
  check_near(total_angle, true_angle, 1e-9, tag + ": summed sample angles == the arc's angle");
}

}  // namespace

// ===========================================================================
int main() {
  std::printf("test_kind_sphere — CGAL %s\n", cgal_version().c_str());
  std::printf("%s\n", build_info().c_str());

  init_all_kinds();
  check(kind_available(Kind::Sphere), "the sphere kind registered itself");
  const KindOps& o = ops(Kind::Sphere);

  // -----------------------------------------------------------------------
  section("kind metadata");
  check_eq_int(int(o.kind()), int(Kind::Sphere), "ops().kind()");
  check(std::string(o.name()) == "sphere", "ops().name() == \"sphere\"");
  check_eq_int(o.dimension(), 3, "dimension() == 3 (points are 3-D directions)");
  check(!o.is_unbounded_kind(), "the sphere is not the 'unbounded planar' kind");
  check(!o.has_polygon_set(), "has_polygon_set() == false (no Gps traits for the sphere)");
  check(!kind_has_polygon_set(Kind::Sphere), "registry: kind_has_polygon_set(Sphere) == false");
  expect_error(ErrorCode::Unsupported, [] { make_polygon_set(Kind::Sphere); },
               "make_polygon_set(Sphere) -> Unsupported");
  check_eq_int(kind_from_name("sphere"), int(Kind::Sphere), "kind_from_name(\"sphere\")");
  check_eq_int(kind_from_name("geodesic"), int(Kind::Sphere), "kind_from_name(\"geodesic\")");

  // -----------------------------------------------------------------------
  section("points: construction, location, repr, exact");
  const Geom A = Pt(1, 0, 0);          // on the equator, x > 0        -> interior
  const Geom B = Pt(0, 1, 0);
  const Geom C = Pt(1, 1, 1);
  const Geom Pp = Pt(3, 1, -1);
  const Geom Qq = Pt(1, 3, 2);

  check(o.point_repr(A) == "SpherePoint(1, 0, 0)", "point_repr(A) = " + o.point_repr(A));
  check(o.point_repr(C) == "SpherePoint(1, 1, 1)", "point_repr(C) = " + o.point_repr(C));
  {
    // Rational (unnormalised) components survive verbatim: the traits never normalises a
    // direction (traits_geodesic_sphere.md gotcha 1).
    const Geom scaled = sphere::make_point(Rational(3, 2), Rational(-5, 4), Rational(7, 8));
    check(o.point_repr(scaled) == "SpherePoint(3/2, -5/4, 7/8)",
          "point_repr keeps exact rationals: " + o.point_repr(scaled));
    Rational x, y, z;
    sphere::point_xyz(scaled, x, y, z);
    check(rational_compare(x, Rational(3, 2)) == 0, "point_xyz x == 3/2");
    check(rational_compare(y, Rational(-5, 4)) == 0, "point_xyz y == -5/4");
    check(rational_compare(z, Rational(7, 8)) == 0, "point_xyz z == 7/8");
  }
  {
    // The direction (2,0,0) IS the direction (1,0,0): CGAL compares directions, not vectors.
    const Geom twice_a = Pt(2, 0, 0);
    check(o.point_equal(A, twice_a), "Equal_2: (1,0,0) == (2,0,0) as directions");
    check(o.point_repr(twice_a) == "SpherePoint(2, 0, 0)",
          "...but the stored components are NOT normalised");
    check_eq_int(o.point_compare_xy(A, twice_a), 0, "compare_xy((1,0,0),(2,0,0)) == EQUAL");
  }

  // Location_type mapping (traits_geodesic_sphere.md §1 + §4.4 Construct_point_2::init):
  //   dy != 0            -> NO_BOUNDARY;    dy == 0 && dx > 0 -> NO_BOUNDARY
  //   dy == 0 && dx < 0  -> MID_BOUNDARY (the identification meridian)
  //   dx == dy == 0      -> dz < 0 ? MIN_BOUNDARY (south pole) : MAX_BOUNDARY (north pole)
  check_eq_int(sphere::point_location(A), sphere::NO_BOUNDARY, "location((1,0,0)) == NO_BOUNDARY");
  check_eq_int(sphere::point_location(C), sphere::NO_BOUNDARY, "location((1,1,1)) == NO_BOUNDARY");
  check_eq_int(sphere::point_location(Pt(-1, 0, 0)), sphere::MID_BOUNDARY,
               "location((-1,0,0)) == MID_BOUNDARY (identification)");
  check_eq_int(sphere::point_location(Pt(-1, 0, 3)), sphere::MID_BOUNDARY,
               "location((-1,0,3)) == MID_BOUNDARY (identification, any z)");
  check_eq_int(sphere::point_location(Pt(0, 0, 1)), sphere::MAX_BOUNDARY,
               "location((0,0,1)) == MAX_BOUNDARY (north pole)");
  check_eq_int(sphere::point_location(Pt(0, 0, -1)), sphere::MIN_BOUNDARY,
               "location((0,0,-1)) == MIN_BOUNDARY (south pole)");
  check_eq_int(sphere::point_location(Pt(-1, 1, 0)), sphere::NO_BOUNDARY,
               "location((-1,1,0)) == NO_BOUNDARY (dy != 0)");

  check(o.point_is_rational(C), "point_is_rational == true (the components are rationals)");
  {
    std::vector<Rational> ex;
    o.point_exact_rational(Pt(2, -4, 6), ex);
    check_eq_sz(ex.size(), 3, "point_exact_rational writes 3 values");
    check(rational_compare(ex[0], Rational(2)) == 0 && rational_compare(ex[1], Rational(-4)) == 0 &&
              rational_compare(ex[2], Rational(6)) == 0,
          "point_exact_rational returns the UNNORMALISED (2,-4,6)");
    std::vector<Geom> nums;
    o.point_exact(Pt(2, -4, 6), nums);
    check_eq_sz(nums.size(), 3, "point_exact writes 3 boxed numbers");
    check(number_kind(nums[0]) == NumberKind::Rational, "point_exact boxes Rationals");
    check_near(number_to_double(nums[1]), -4.0, 0.0, "point_exact[1] == -4");
  }

  // -----------------------------------------------------------------------
  section("points: approximation and certified intervals");
  {
    // C = (1,1,1) normalises to (1/sqrt(3), 1/sqrt(3), 1/sqrt(3)); 1/sqrt(3) = 0.5773502691896258.
    const double inv_sqrt3 = 1.0 / std::sqrt(3.0);
    double v[3];
    o.point_approx(C, v);
    for (int i = 0; i < 3; ++i)
      check_near(v[i], inv_sqrt3, 1e-15, "point_approx(C)[" + std::to_string(i) + "] == 1/sqrt(3)");
    check_near(norm(V3{v[0], v[1], v[2]}), 1.0, 1e-15, "point_approx returns a UNIT vector");

    // approximate_coordinate agrees with point_approx (this TU normalises both; CGAL's own
    // Approximate_2 point overload does not — rendering_and_approximation.md gotcha 17).
    for (int i = 0; i < 3; ++i)
      check_near(o.approximate_coordinate(C, i), v[i], 0.0,
                 "approximate_coordinate == point_approx[" + std::to_string(i) + "]");
    expect_error(ErrorCode::InvalidArgument, [&] { o.approximate_coordinate(C, 3); },
                 "approximate_coordinate(p, 3) -> InvalidArgument");

    // Scaling the direction does not change the value, but the double normalisation is not
    // bit-exact: (7,7,7)/|(7,7,7)| rounds to 0.57735026918962573 while (1,1,1)/|(1,1,1)|
    // rounds to 0.57735026918962584 — 1 ulp apart, both inside the certified interval below.
    double w[3];
    o.point_approx(Pt(7, 7, 7), w);
    for (int i = 0; i < 3; ++i) {
      check_near(w[i], v[i], 1e-15, "point_approx((7,7,7)) ~= point_approx((1,1,1))");
      check_near(w[i], inv_sqrt3, 1e-15, "point_approx((7,7,7)) ~= 1/sqrt(3)");
    }

    std::vector<std::pair<double, double>> iv;
    o.point_interval(C, iv);
    check_eq_sz(iv.size(), 3, "point_interval writes 3 intervals");
    for (int i = 0; i < 3; ++i) {
      check(iv[i].first <= inv_sqrt3 && inv_sqrt3 <= iv[i].second,
            "interval[" + std::to_string(i) + "] encloses 1/sqrt(3)");
      check(iv[i].first <= v[i] && v[i] <= iv[i].second,
            "interval[" + std::to_string(i) + "] encloses point_approx");
      check(iv[i].second - iv[i].first < 1e-15,
            "interval[" + std::to_string(i) + "] is tight (width " +
                std::to_string(iv[i].second - iv[i].first) + ")");
    }
    // A rational direction whose normalised coordinates ARE rational: (3,4,0) -> (0.6,0.8,0).
    std::vector<std::pair<double, double>> iv2;
    o.point_interval(Pt(3, 4, 0), iv2);
    check(iv2[0].first <= 0.6 && 0.6 <= iv2[0].second, "interval of (3,4,0) encloses 0.6");
    check(iv2[1].first <= 0.8 && 0.8 <= iv2[1].second, "interval of (3,4,0) encloses 0.8");
    check(iv2[2].first <= 0.0 && 0.0 <= iv2[2].second, "interval of (3,4,0) encloses 0");
    double q[3];
    o.point_approx(Pt(3, 4, 0), q);
    check_near(q[0], 0.6, 1e-16, "point_approx((3,4,0)).x == 0.6");
    check_near(q[1], 0.8, 1e-16, "point_approx((3,4,0)).y == 0.8");
  }
  {
    // Components far outside the double range: the exact rescaling path of point_approx /
    // point_interval must still produce a unit vector.
    Rational huge(1);
    for (int i = 0; i < 400; ++i) huge *= 10;
    const Geom big = sphere::make_point(huge, huge, Rational(0));
    double v[3];
    o.point_approx(big, v);
    check_near(norm(V3{v[0], v[1], v[2]}), 1.0, 1e-15,
               "point_approx of a 10^400-sized direction is still unit length");
    check_near(v[0], std::sqrt(0.5), 1e-15, "  ... and equals (1/sqrt2, 1/sqrt2, 0)");
    std::vector<std::pair<double, double>> iv;
    o.point_interval(big, iv);
    check(iv[0].first <= std::sqrt(0.5) && std::sqrt(0.5) <= iv[0].second,
          "point_interval of a 10^400-sized direction encloses 1/sqrt(2)");
  }

  // -----------------------------------------------------------------------
  section("points: comparison and conversion");
  // Compare_x_2 / Compare_xy_2 compare longitude CCW from the identification (-1,0,*), then
  // latitude.  Longitudes: (1,0,0) = 180 deg from the identification, (0,1,0) = 270 deg.
  check_eq_int(o.point_compare_x(A, B), -1, "compare_x((1,0,0),(0,1,0)) == SMALLER");
  check_eq_int(o.point_compare_x(B, A), 1, "compare_x((0,1,0),(1,0,0)) == LARGER");
  check_eq_int(o.point_compare_xy(A, Pt(1, 0, 5)), -1,
               "compare_xy: same longitude -> lower latitude is SMALLER");
  check_eq_int(o.point_compare_xy(Pt(1, 0, 5), A), 1, "compare_xy is antisymmetric");
  check(!o.point_equal(A, B), "Equal_2((1,0,0),(0,1,0)) == false");
  // CGAL precondition of Compare_x_2/Compare_xy_2: `p.is_no_boundary()`.  We let it propagate
  // (traits_geodesic_sphere.md §4.5); it arrives as CGAL::Precondition_exception.
  expect_any_throw([&] { o.point_compare_xy(Pt(0, 0, 1), A); },
                   "compare_xy with a pole raises the documented CGAL precondition");

  check(o.convert_point(A).ptr == A.ptr, "convert_point(sphere point) is the identity");
  {
    // Box a SEGMENT-kind point by hand (headers only, no kind_segment.o needed).
    SegmentTypes::Point_2 sp(SegmentTypes::FT(1), SegmentTypes::FT(2));
    const Geom seg_point = make_geom(Kind::Segment, GeomType::Point, sp);
    expect_error(ErrorCode::KindMismatch, [&] { o.convert_point(seg_point); },
                 "convert_point(segment point) -> KindMismatch (planar point, 3-D kind)");
  }
  expect_error(ErrorCode::Unsupported, [&] { o.make_point(Rational(1), Rational(2)); },
               "make_point(x, y) -> Unsupported (the sphere needs make_point_3)");
  expect_error(ErrorCode::InvalidArgument,
               [] { sphere::make_point(Rational(0), Rational(0), Rational(0)); },
               "make_point(0,0,0) -> InvalidArgument");
  check(o.make_point_3(Rational(0), Rational(0), Rational(-2)).kind == Kind::Sphere,
        "KindOps::make_point_3 boxes a sphere point");

  // -----------------------------------------------------------------------
  section("curves: every constructor of namespace arr2d::sphere");
  const Geom AB = sphere::make_arc(A, B);   // quarter of the equator
  const Geom BC = sphere::make_arc(B, C);
  const Geom CA = sphere::make_arc(C, A);
  const Geom PQ = sphere::make_arc(Pp, Qq);
  check(AB.kind == Kind::Sphere && AB.type == GeomType::Curve, "make_arc returns a Curve box");
  // The normal of the minor arc p->q is the cross product p x q:
  //   (1,0,0) x (0,1,0) = (0,0,1);  (0,1,0) x (1,1,1) = (1,0,-1);  (1,1,1) x (1,0,0) = (0,1,-1)
  //   (3,1,-1) x (1,3,2) = (1*2-(-1)*3, (-1)*1-3*2, 3*3-1*1) = (5,-7,8)
  check(o.point_repr(sphere::normal(AB)) == "SpherePoint(0, 0, 1)",
        "normal(arc (1,0,0)->(0,1,0)) == (0,0,1): " + o.point_repr(sphere::normal(AB)));
  check(o.point_repr(sphere::normal(BC)) == "SpherePoint(1, 0, -1)",
        "normal(arc (0,1,0)->(1,1,1)) == (1,0,-1)");
  check(o.point_repr(sphere::normal(CA)) == "SpherePoint(0, 1, -1)",
        "normal(arc (1,1,1)->(1,0,0)) == (0,1,-1)");
  check(o.point_repr(sphere::normal(PQ)) == "SpherePoint(5, -7, 8)",
        "normal(arc (3,1,-1)->(1,3,2)) == (5,-7,8)");
  check(o.curve_repr(AB) == "GeodesicArc(source=(1, 0, 0), target=(0, 1, 0), normal=(0, 0, 1))",
        "curve_repr(AB) = " + o.curve_repr(AB));
  check(!sphere::is_full(AB) && !sphere::is_vertical(AB) && !sphere::is_meridian(AB) &&
            !sphere::is_degenerate(AB),
        "AB: not full / not vertical / not a meridian / not degenerate");
  check(o.is_x_monotone(AB), "AB is x-monotone");
  check(o.curve_is_bounded(AB), "every sphere curve is bounded");

  expect_error(ErrorCode::InvalidArgument, [&] { sphere::make_arc(A, A); },
               "make_arc(p, p) -> InvalidArgument (equal endpoints)");
  expect_error(ErrorCode::InvalidArgument, [&] { sphere::make_arc(A, Pt(-1, 0, 0)); },
               "make_arc(p, -p) -> InvalidArgument (antipodal endpoints)");
  expect_error(ErrorCode::InvalidArgument, [&] { sphere::make_arc(A, Pt(-2, 0, 0)); },
               "make_arc(p, -2p) -> InvalidArgument (antipodal DIRECTIONS)");

  // make_arc_with_normal: the only way to build a MAJOR arc / an arc between antipodal points.
  const Geom MAJOR = sphere::make_arc_with_normal(A, Pt(-1, 0, 0), Pt(0, 1, 0));
  check(o.curve_repr(MAJOR) == "GeodesicArc(source=(1, 0, 0), target=(-1, 0, 0), normal=(0, 1, 0))",
        "make_arc_with_normal: " + o.curve_repr(MAJOR));
  check(sphere::is_vertical(MAJOR), "the (0,1,0)-normal arc lies on a meridian -> vertical");
  expect_error(ErrorCode::InvalidArgument,
               [&] { sphere::make_arc_with_normal(A, B, Pt(0, 1, 0)); },
               "make_arc_with_normal: target off the plane -> InvalidArgument");
  expect_error(ErrorCode::InvalidArgument,
               [&] { sphere::make_arc_with_normal(C, B, Pt(0, 0, 1)); },
               "make_arc_with_normal: source off the plane -> InvalidArgument");
  expect_error(ErrorCode::InvalidArgument,
               [&] { sphere::make_arc_with_normal(A, A, Pt(0, 1, 0)); },
               "make_arc_with_normal(p, p, n) -> InvalidArgument");

  // make_full_circle: goes through Construct_curve_2(normal); the raw
  // Arr_geodesic_arc_on_sphere_3(Direction_3) ctor does not compile (gotcha 2).
  const Geom EQUATOR = sphere::make_full_circle(Pt(0, 0, 1));
  const Geom MERIDIAN_CIRCLE = sphere::make_full_circle(Pt(0, 1, 0));
  check(sphere::is_full(EQUATOR), "make_full_circle -> is_full");
  check(!sphere::is_vertical(EQUATOR), "the equator (normal (0,0,1)) is not vertical");
  check(sphere::is_vertical(MERIDIAN_CIRCLE), "the (0,1,0) great circle IS vertical");
  check(o.curve_repr(EQUATOR) == "GreatCircle(normal=(0, 0, 1))",
        "curve_repr(full circle) = " + o.curve_repr(EQUATOR));
  check(!o.is_x_monotone(EQUATOR), "a full great circle is not x-monotone");

  // make_x_monotone_arc: Construct_x_monotone_curve_2(p, q) guarded by an x-monotonicity test.
  const Geom XAB = sphere::make_x_monotone_arc(A, B);
  check(XAB.type == GeomType::XCurve, "make_x_monotone_arc returns an XCurve box");
  check(o.curve_repr(XAB) == o.curve_repr(AB), "the x-monotone quarter equator == AB");
  expect_error(ErrorCode::InvalidArgument,
               [&] { sphere::make_x_monotone_arc(Pt(-1, 1, 0), Pt(-1, -1, 0)); },
               "make_x_monotone_arc across the identification -> InvalidArgument");
  expect_error(ErrorCode::InvalidArgument,
               [&] { o.construct_x_monotone_curve(Pt(-1, 1, 0), Pt(-1, -1, 0)); },
               "KindOps::construct_x_monotone_curve is guarded the same way");
  check(o.curve_equal(o.construct_x_monotone_curve(A, B), XAB),
        "KindOps::construct_x_monotone_curve(A, B) == make_x_monotone_arc(A, B)");

  // -----------------------------------------------------------------------
  section("make_x_monotone piece counts");
  {
    // Full NON-vertical great circle -> 2 pieces, split at the identification point
    // p1 = (-dz, 0, dx) = (-1,0,0) and its antipode p2 = (1,0,0)
    // (traits_geodesic_sphere.md §4.7 table; full x-monotone arcs are disabled in 6.1).
    std::vector<Geom> pieces;
    o.make_x_monotone(EQUATOR, pieces);
    check_eq_sz(pieces.size(), 2, "full equator -> 2 x-monotone pieces");
    check(o.curve_repr(pieces[0]) ==
              "GeodesicArc(source=(-1, 0, 0), target=(1, 0, 0), normal=(0, 0, 1))",
          "equator piece 0 = " + o.curve_repr(pieces[0]));
    check(o.curve_repr(pieces[1]) ==
              "GeodesicArc(source=(1, 0, 0), target=(-1, 0, 0), normal=(0, 0, 1))",
          "equator piece 1 = " + o.curve_repr(pieces[1]));
    check_eq_int(sphere::point_location(o.xcurve_source(pieces[0])), sphere::MID_BOUNDARY,
                 "the split point (-1,0,0) is tagged MID_BOUNDARY");
  }
  {
    // Full VERTICAL great circle -> 2 meridians split at BOTH poles.
    std::vector<Geom> pieces;
    o.make_x_monotone(MERIDIAN_CIRCLE, pieces);
    check_eq_sz(pieces.size(), 2, "full meridian circle -> 2 x-monotone pieces");
    check(sphere::is_meridian(pieces[0]) && sphere::is_meridian(pieces[1]),
          "both pieces are meridians (south pole -> north pole)");
    check_eq_int(sphere::point_location(o.xcurve_source(pieces[0])), sphere::MIN_BOUNDARY,
                 "piece 0 starts at the south pole");
    check_eq_int(sphere::point_location(o.xcurve_target(pieces[0])), sphere::MAX_BOUNDARY,
                 "piece 0 ends at the north pole");
  }
  {
    // Non-vertical arc crossing the identification -> 2 pieces, joint on the meridian.
    const Geom crossing = sphere::make_arc(Pt(-1, 1, 0), Pt(-1, -1, 0));
    check(!o.is_x_monotone(crossing), "an arc crossing the identification is not x-monotone");
    std::vector<Geom> pieces;
    o.make_x_monotone(crossing, pieces);
    check_eq_sz(pieces.size(), 2, "identification-crossing arc -> 2 pieces");
    check_eq_int(sphere::point_location(o.xcurve_target(pieces[0])), sphere::MID_BOUNDARY,
                 "the joint lies ON the identification meridian");
    check(o.point_equal(o.xcurve_target(pieces[0]), o.xcurve_source(pieces[1])),
          "the two pieces chain at the joint");
    expect_error(ErrorCode::NotXMonotone, [&] { o.to_x_monotone(crossing); },
                 "to_x_monotone of a 2-piece curve -> NotXMonotone");
  }
  {
    // Antipodal endpoints with normal (0,1,0): the arc runs CCW around +y, i.e. through the
    // SOUTH pole -> 2 pieces split there.
    std::vector<Geom> pieces;
    o.make_x_monotone(MAJOR, pieces);
    check_eq_sz(pieces.size(), 2, "antipodal (0,1,0)-normal arc -> 2 pieces");
    check_eq_int(sphere::point_location(o.xcurve_target(pieces[0])), sphere::MIN_BOUNDARY,
                 "split at the south pole (CCW around +y from (1,0,0) heads to -z)");
  }
  {
    // A vertical arc with one endpoint at a pole is already x-monotone.
    const Geom polar = sphere::make_arc(Pt(1, 1, 0), Pt(0, 0, 1));
    std::vector<Geom> pieces;
    o.make_x_monotone(polar, pieces);
    check_eq_sz(pieces.size(), 1, "arc ending at the north pole -> 1 piece");
    check(sphere::is_vertical(pieces[0]), "... and it is vertical");
    check(o.is_x_monotone(polar), "is_x_monotone(arc to a pole) == true");
  }
  {
    // An x-monotone curve fed to make_x_monotone comes back unchanged.
    std::vector<Geom> pieces;
    o.make_x_monotone(XAB, pieces);
    check_eq_sz(pieces.size(), 1, "make_x_monotone(XCurve) -> the same curve");
    check(o.curve_equal(pieces[0], XAB), "... and it is equal to the input");
  }

  // -----------------------------------------------------------------------
  section("x-monotone curve accessors");
  {
    check(o.point_equal(o.xcurve_source(XAB), A), "xcurve_source(XAB) == (1,0,0)");
    check(o.point_equal(o.xcurve_target(XAB), B), "xcurve_target(XAB) == (0,1,0)");
    check(o.xcurve_has_source(XAB) && o.xcurve_has_target(XAB),
          "both ends of a geodesic arc are real directions");
    check(o.point_equal(o.xcurve_min_vertex(XAB), A), "min_vertex == the source (directed right)");
    check(o.point_equal(o.xcurve_max_vertex(XAB), B), "max_vertex == the target");
    check(o.xcurve_is_directed_right(XAB), "XAB is directed right");
    check_eq_int(o.compare_endpoints_xy(XAB), -1, "compare_endpoints_xy(XAB) == SMALLER");
    check(!o.xcurve_is_vertical(XAB), "XAB is not vertical");

    const Geom opp = o.construct_opposite(XAB);
    check(o.point_equal(o.xcurve_source(opp), B), "construct_opposite swaps source/target");
    check(o.point_equal(o.xcurve_target(opp), A), "...");
    check_eq_int(o.compare_endpoints_xy(opp), 1, "the opposite is directed LEFT");
    check(o.curve_equal(opp, XAB), "Equal_2 on curves is direction-insensitive");

    // parameter space: an interior arc is interior at both ends; an arc ending on the
    // identification reports LEFT/RIGHT there, one ending at a pole reports TOP/BOTTOM
    // (traits_geodesic_sphere.md §4.6).
    check_eq_int(o.parameter_space_in_x(XAB, ARR_MIN_END), ARR_INTERIOR, "ps_x(XAB, min)");
    check_eq_int(o.parameter_space_in_y(XAB, ARR_MAX_END), ARR_INTERIOR, "ps_y(XAB, max)");
    const Geom to_pole = sphere::make_x_monotone_arc(Pt(1, 1, 0), Pt(0, 0, 1));
    check_eq_int(o.parameter_space_in_y(to_pole, ARR_MAX_END), ARR_TOP_BOUNDARY,
                 "ps_y(arc to the north pole, max) == TOP_BOUNDARY");
    const Geom to_ident = sphere::make_x_monotone_arc(Pt(0, 1, 0), Pt(-1, 0, 0));
    check_eq_int(o.parameter_space_in_x(to_ident, ARR_MAX_END), ARR_RIGHT_BOUNDARY,
                 "ps_x(arc ending on the identification, max) == RIGHT_BOUNDARY");
    expect_error(ErrorCode::InvalidArgument, [&] { o.parameter_space_in_x(XAB, 7); },
                 "parameter_space_in_x with a bad curve end -> InvalidArgument");

    // to_curve round trip (Curve_2 <-> X_monotone_curve_2 are different C++ types here).
    const Geom back = o.to_curve(XAB);
    check(back.type == GeomType::Curve, "to_curve returns a Curve box");
    check(o.curve_repr(back) == o.curve_repr(XAB), "to_curve preserves source/target/normal");
    std::vector<Geom> again;
    o.make_x_monotone(back, again);
    check_eq_sz(again.size(), 1, "to_curve(xc) is still x-monotone");
    check(o.curve_equal(again[0], XAB), "... and equal to the original");
    expect_error(ErrorCode::NotXMonotone, [&] { o.xcurve_source(AB); },
                 "xcurve_source on a Curve box -> NotXMonotone");
    check(o.to_curve(AB).ptr == AB.ptr, "to_curve on a Curve box is the identity");
  }

  // -----------------------------------------------------------------------
  section("traits functors on x-monotone arcs");
  {
    const Geom mid = Pt(1, 1, 0);   // the midpoint direction of the quarter equator
    check(o.is_in_x_range(XAB, mid), "(1,1,0) is in the x-range of the quarter equator");
    check_eq_int(o.compare_y_at_x(mid, XAB), 0, "(1,1,0) lies ON the quarter equator");
    check_eq_int(o.compare_y_at_x(Pt(1, 1, 1), XAB), 1, "(1,1,1) is above it");
    check_eq_int(o.compare_y_at_x(Pt(1, 1, -1), XAB), -1, "(1,1,-1) is below it");
    expect_error(ErrorCode::InvalidArgument, [&] { o.compare_y_at_x(Pt(-1, 1, 0), XAB); },
                 "compare_y_at_x outside the x-range -> InvalidArgument");

    Geom left, right;
    o.split(XAB, mid, left, right);
    check(o.point_equal(o.xcurve_target(left), mid), "split: left half ends at the split point");
    check(o.point_equal(o.xcurve_source(right), mid), "split: right half starts there");
    check(o.are_mergeable(left, right), "the two halves are mergeable");
    const Geom merged = o.merge(left, right);
    check(o.curve_equal(merged, XAB), "merge(left, right) == the original arc");
    // Two arcs sharing BOTH endpoints are NOT mergeable (that would be a full x-monotone arc,
    // which CGAL 6.1 disables) — traits_geodesic_sphere.md §4.7.
    check(!o.are_mergeable(XAB, o.construct_opposite(XAB)),
          "an arc and its opposite are not mergeable");

    // Intersect_2: the quarter equator vs a vertical arc through (1,1,0).
    const Geom vert = sphere::make_x_monotone_arc(Pt(1, 1, 1), Pt(1, 1, -1));
    std::vector<IntersectionResult> xs;
    o.intersect(XAB, vert, xs);
    check_eq_sz(xs.size(), 1, "quarter equator x vertical arc -> 1 intersection");
    if (xs.size() == 1) {
      check(xs[0].is_point, "the intersection is a point");
      check(o.point_equal(xs[0].point, mid),
            "at the direction (1,1,0) (CGAL returns it UNNORMALISED: " +
                o.point_repr(xs[0].point) + ")");
      check_eq_sz(xs[0].multiplicity, 1, "transversal crossing -> multiplicity 1");
    }
    std::vector<IntersectionResult> overlap;
    o.intersect(XAB, left, overlap);
    check_eq_sz(overlap.size(), 1, "arc x sub-arc -> 1 result");
    check(!overlap.empty() && !overlap[0].is_point, "... and it is an overlap curve");
    check(!overlap.empty() && overlap[0].is_point == false && o.curve_equal(overlap[0].overlap, left),
          "the overlap is exactly the sub-arc");

    // The geodesic traits has no Trim_2 at all.
    expect_error(ErrorCode::Unsupported, [&] { o.trim(XAB, A, mid); },
                 "trim -> Unsupported (Arr_geodesic_arc_on_sphere_traits_2 has no Trim_2)");
  }

  // -----------------------------------------------------------------------
  section("approximate() and curve_bbox()");
  {
    // Reference measurements from rendering_and_approximation.md §2.4 for exactly this arc
    // ("Quarter great circle (1,0,0) -> (0,1,0)"): error 1 -> 2 points, 0.1 -> 3, 0.01 -> 7,
    // 0.001 -> 19.
    const double tols[4] = {1.0, 0.1, 0.01, 0.001};
    const std::size_t want[4] = {2, 3, 7, 19};
    for (int i = 0; i < 4; ++i) {
      std::vector<double> flat;
      o.approximate(XAB, tols[i], nullptr, flat);
      check_eq_sz(flat.size() / 3, want[i],
                  "approximate(quarter equator, tol=" + std::to_string(tols[i]) + ") point count");
      check_arc_approximation(o, XAB, tols[i], flat,
                              "quarter equator @ " + std::to_string(tols[i]));
    }
  }
  {
    // A generic, non-axis-aligned arc: the chord P->Q with normal (5,-7,8).
    const Geom XPQ = sphere::make_x_monotone_arc(Pp, Qq);
    std::vector<double> flat;
    o.approximate(XPQ, 1e-3, nullptr, flat);
    check(flat.size() / 3 >= 3, "approximate(chord, 1e-3) produced several points");
    check_arc_approximation(o, XPQ, 1e-3, flat, "chord (3,1,-1)->(1,3,2) @ 1e-3");
    // clip is ignored for this (bounded) kind.
    BBox clip;
    clip.dim = 3;
    std::vector<double> flat2;
    o.approximate(XPQ, 1e-3, &clip, flat2);
    check(flat2 == flat, "approximate ignores the clip box (no unbounded sphere curve)");
    // A Curve box that happens to be x-monotone goes through Make_x_monotone_2 and produces
    // exactly the same polyline as its XCurve counterpart.
    std::vector<double> flat3;
    o.approximate(PQ, 1e-3, nullptr, flat3);
    check(flat3 == flat, "approximate(Curve box) == approximate(XCurve box) for an x-monotone arc");
  }
  {
    // A full great circle: 2 x-monotone pieces of pi each; at tol 0.01,
    // dtheta = 2*acos(0.99) = 0.28379, num_segs = ceil(pi/dtheta) = 12 -> 13 points per piece,
    // and the shared joint (1,0,0) is emitted once -> 13 + 13 - 1 = 25 points, closing back on
    // the starting point (-1,0,0).
    std::vector<double> flat;
    o.approximate(EQUATOR, 0.01, nullptr, flat);
    check_eq_sz(flat.size() / 3, 25, "approximate(full equator, tol=0.01) point count");
    const std::size_t n = flat.size() / 3;
    check_near(flat[0], -1.0, 1e-12, "the full circle starts at the identification point (-1,0,0)");
    check_near(flat[1], 0.0, 1e-12, "  ... y");
    check_near(flat[3 * (n - 1) + 0], -1.0, 1e-12, "  ... and closes back onto it");
    check_near(flat[3 * (n - 1) + 1], 0.0, 1e-12, "  ... y");
    double max_z = 0.0, max_r = 0.0, max_sag = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
      const V3 p = at(flat, i);
      max_z = std::max(max_z, std::abs(p.z));
      max_r = std::max(max_r, std::abs(norm(p) - 1.0));
      if (i + 1 < n) {
        double c = std::max(-1.0, std::min(1.0, dot(p, at(flat, i + 1))));
        max_sag = std::max(max_sag, 1.0 - std::cos(0.5 * std::acos(c)));
      }
    }
    check(max_z == 0.0, "every equator sample has z == 0");
    check(max_r <= 1e-12, "every equator sample is unit length");
    check(max_sag <= 0.01, "every chord of the equator approximation is within the tolerance");
  }
  {
    // Tolerance validation happens BEFORE CGAL is called: error <= 0 hangs forever inside
    // Approximate_2 and error > 2 makes acos() return NaN (rendering_and_approximation.md
    // gotcha 7).
    std::vector<double> flat;
    expect_error(ErrorCode::InvalidArgument, [&] { o.approximate(XAB, 0.0, nullptr, flat); },
                 "approximate(tolerance = 0) -> InvalidArgument (CGAL would loop forever)");
    expect_error(ErrorCode::InvalidArgument, [&] { o.approximate(XAB, -1.0, nullptr, flat); },
                 "approximate(tolerance < 0) -> InvalidArgument");
    expect_error(ErrorCode::InvalidArgument, [&] { o.approximate(XAB, 2.5, nullptr, flat); },
                 "approximate(tolerance > 2) -> InvalidArgument (acos domain error)");
    o.approximate(XAB, 1e-18, nullptr, flat);   // clamped to 1e-12, must not hang
    check(flat.size() / 3 > 1000, "a sub-1e-12 tolerance is clamped, not rejected (" +
                                      std::to_string(flat.size() / 3) + " points)");
  }
  {
    // Arc length: a quarter great circle has length pi/2 = 1.5707963268.  The uniform chord
    // approximation is slightly shorter; at tol 1e-4 the measured value is 1.570744831.
    const double len = o.approximate_length(XAB, 1e-4);
    check_near(len, 1.5707963267948966, 1e-4, "approximate_length(quarter equator, 1e-4) ~ pi/2");
    check(len < 1.5707963267948966, "the chord approximation is shorter than the true arc");
    const double full = o.approximate_length(EQUATOR, 1e-6);
    check_near(full, 2.0 * 3.14159265358979323846, 1e-5,
               "approximate_length(full equator, 1e-6) ~ 2*pi");
  }
  {
    BBox b = o.curve_bbox(XAB);
    check_eq_int(b.dim, 3, "curve_bbox is 3-D for the sphere kind");
    check_near(b.lo[0], 0.0, 1e-6, "bbox(quarter equator).lo.x == 0");
    check_near(b.lo[1], 0.0, 1e-6, "bbox(quarter equator).lo.y == 0");
    check_near(b.lo[2], 0.0, 0.0, "bbox(quarter equator).lo.z == 0");
    check_near(b.hi[0], 1.0, 1e-6, "bbox(quarter equator).hi.x == 1");
    check_near(b.hi[1], 1.0, 1e-6, "bbox(quarter equator).hi.y == 1");
    check_near(b.hi[2], 0.0, 0.0, "bbox(quarter equator).hi.z == 0");
    BBox f = o.curve_bbox(EQUATOR);
    check_near(f.lo[0], -1.0, 1e-5, "bbox(full equator).lo.x == -1");
    check_near(f.hi[1], 1.0, 1e-5, "bbox(full equator).hi.y == 1 (approximate: 0.999999)");
    check_near(f.lo[2], 0.0, 0.0, "bbox(full equator) is flat in z");
    check_near(f.hi[2], 0.0, 0.0, "  ...");
  }

  // -----------------------------------------------------------------------
  section("convert_curve");
  {
    std::vector<Geom> out;
    o.convert_curve(AB, out);
    check_eq_sz(out.size(), 1, "convert_curve(sphere curve) is the identity");
    // Box a SEGMENT-kind Curve_2 by hand from rationals; no kind_segment.o is linked.
    SegmentTypes::Point_2 s(SegmentTypes::FT(0), SegmentTypes::FT(0));
    SegmentTypes::Point_2 t(SegmentTypes::FT(1), SegmentTypes::FT(2));
    SegmentTypes::Curve_2 seg(s, t);
    const Geom seg_curve = make_geom(Kind::Segment, GeomType::Curve, seg);
    expect_error(ErrorCode::Unsupported,
                 [&] { std::vector<Geom> v; o.convert_curve(seg_curve, v); },
                 "convert_curve(segment curve -> sphere) -> Unsupported");
  }

  // -----------------------------------------------------------------------
  section("arrangement: build the spherical triangle + a crossing chord");
  //
  // A = (1,0,0), B = (0,1,0), C = (1,1,1) form a spherical triangle whose edges lie on the
  // great circles with normals (0,0,1) [the equator], (1,0,-1) [x = z] and (0,1,-1) [y = z].
  // The interior is { z > 0, x > z, y > z }.
  //
  // The chord P = (3,1,-1) -> Q = (1,3,2):
  //   * P has z < 0 and Q has z > 0     -> the minor arc crosses the equator (edge AB) once;
  //   * (1,0,-1).P = 4 > 0, (1,0,-1).Q = -1 < 0 -> it crosses the plane x = z (edge BC) once;
  //   * (0,1,-1).P = 2 > 0, (0,1,-1).Q = 1 > 0  -> it never reaches the plane y = z (edge CA).
  // So it enters the triangle through AB and leaves through BC:
  //   V = 3 (triangle) + 2 (P, Q) + 2 (crossings) = 7
  //   E = AB split in 2, BC split in 2, CA whole, chord split in 3            = 8
  //   F = 2 pieces of the triangle + the spherical face                       = 3
  //   Euler on the sphere: V - E + F = 7 - 8 + 3 = 2  (checked below).
  std::unique_ptr<ArrBase> arr = make_arrangement(Kind::Sphere);
  check_eq_int(int(arr->kind()), int(Kind::Sphere), "make_arrangement(Sphere)->kind()");
  check(arr->is_empty(), "a fresh spherical arrangement is empty");
  check_eq_sz(arr->number_of_faces(), 1, "... but already has ONE face (the spherical face)");
  check_eq_sz(arr->number_of_unbounded_faces(), 0,
              "number_of_unbounded_faces() is always 0 on the sphere");

  std::vector<CH> tri_handles;
  {
    // Aggregate (sweep) insertion is the recommended path on the sphere
    // (traits_geodesic_sphere.md §10.11 item 7).
    std::vector<Geom> curves{AB, BC, CA};
    arr->insert_curves(curves, tri_handles);
  }
  check_eq_sz(tri_handles.size(), 3, "insert_curves returned 3 curve handles");
  check_eq_sz(arr->number_of_vertices(), 3, "triangle: V == 3");
  check_eq_sz(arr->number_of_edges(), 3, "triangle: E == 3");
  check_eq_sz(arr->number_of_halfedges(), 6, "triangle: H == 6");
  check_eq_sz(arr->number_of_faces(), 2, "triangle: F == 2");
  check_eq_sz(arr->number_of_curves(), 3, "triangle: 3 input curves in the history");
  check(arr->is_valid(), "triangle: is_valid()");

  const CH chord = arr->insert_curve(PQ);   // incremental insert (nothing lies on the meridian)
  check_eq_sz(arr->number_of_vertices(), 7, "triangle + chord: V == 7");
  check_eq_sz(arr->number_of_edges(), 8, "triangle + chord: E == 8");
  check_eq_sz(arr->number_of_faces(), 3, "triangle + chord: F == 3");
  check_eq_sz(arr->number_of_curves(), 4, "triangle + chord: 4 input curves");
  check(arr->is_valid(), "triangle + chord: is_valid()");
  check_eq_sz(arr->number_of_vertices() - arr->number_of_edges() + arr->number_of_faces(), 2,
              "Euler characteristic V - E + F == 2 on the sphere");
  check_eq_sz(arr->number_of_induced_edges(chord), 3, "the chord induces 3 edges");
  check_eq_sz(arr->number_of_isolated_vertices(), 0, "no isolated vertices");
  check_eq_sz(arr->number_of_vertices_at_infinity(), 0, "no vertices at infinity on the sphere");

  // -----------------------------------------------------------------------
  section("arrangement: iteration, faces and the spherical face");
  std::vector<VH> verts;
  std::vector<HH> edges, halfedges;
  std::vector<FH> faces;
  std::vector<CH> curves;
  arr->vertices(verts);
  arr->edges(edges);
  arr->halfedges(halfedges);
  arr->faces(faces);
  arr->curves(curves);
  check_eq_sz(verts.size(), 7, "vertices() yields 7 handles");
  check_eq_sz(edges.size(), 8, "edges() yields 8 handles");
  check_eq_sz(halfedges.size(), 16, "halfedges() yields 16 handles");
  check_eq_sz(faces.size(), 3, "faces() yields 3 handles");
  check_eq_sz(curves.size(), 4, "curves() yields 4 handles");
  {
    std::size_t degree_sum = 0;
    for (VH v : verts) {
      check(arr->vertex_valid(v), "every vertex handle validates");
      check(!arr->vertex_is_at_open_boundary(v),
            "no sphere vertex lies at an open boundary (there are no fictitious cells)");
      check_eq_int(arr->vertex_parameter_space_in_x(v), ARR_INTERIOR,
                   "this arrangement avoids the identification meridian");
      check_eq_int(arr->vertex_parameter_space_in_y(v), ARR_INTERIOR,
                   "this arrangement avoids both poles");
      degree_sum += arr->vertex_degree(v);
    }
    check_eq_sz(degree_sum, 16, "sum of vertex degrees == 2E == 16");
  }
  {
    std::size_t with_outer = 0, without_outer = 0;
    for (FH f : faces) {
      check(!arr->face_is_unbounded(f), "Arr_spherical_topology_traits_2::is_unbounded is false");
      check(!arr->face_is_fictitious(f), "there are no fictitious faces on the sphere");
      if (arr->face_number_of_outer_ccbs(f) == 0) ++without_outer; else ++with_outer;
    }
    check_eq_sz(without_outer, 1, "exactly ONE face has no outer CCB (the spherical face)");
    check_eq_sz(with_outer, 2, "the other two faces have one outer CCB each");
  }
  {
    const FH sph = arr->unbounded_face();
    check_eq_sz(arr->face_number_of_outer_ccbs(sph), 0,
                "unbounded_face() is the spherical face (0 outer CCBs)");
    check_eq_sz(arr->face_number_of_inner_ccbs(sph), 1, "it has the whole drawing as one hole");
    // Face::outer_ccb() has CGAL_precondition(number_of_outer_ccbs() == 1) — never call it
    // blindly on the sphere (traits_geodesic_sphere.md gotcha 5).
    expect_error(ErrorCode::InvalidArgument, [&] { arr->face_outer_ccb(sph); },
                 "face_outer_ccb(spherical face) -> InvalidArgument, not a CGAL assertion");
    // The spherical face contains the north pole (§6.4 "is_in_face ... contains everything").
    const Located np = arr->locate(Pt(0, 0, 1), PL_NAIVE);
    check(np.type == 2 && np.as_face() == sph, "the north pole is located in the spherical face");
    std::vector<Geom> outer;
    std::vector<std::vector<Geom>> holes;
    arr->face_polygon(sph, outer, holes);
    check(outer.empty(), "face_polygon(spherical face): no outer boundary");
    check_eq_sz(holes.size(), 1, "face_polygon(spherical face): one hole");
  }
  // The sphere has no fictitious face at all (Arrangement_on_surface_2::fictitious_face() does
  // not even compile for this topology — gotcha 2).
  expect_error(ErrorCode::Unsupported, [&] { arr->fictitious_face(); },
               "fictitious_face() -> Unsupported for the sphere");

  // -----------------------------------------------------------------------
  section("arrangement: he_curve / he_directed_curve orientation around a face");
  {
    std::size_t ccb3 = 0, ccb4 = 0;
    for (FH f : faces) {
      if (arr->face_number_of_outer_ccbs(f) == 0) continue;
      std::vector<HH> ccb;
      arr->he_ccb(arr->face_outer_ccb(f), ccb);
      if (ccb.size() == 3) ++ccb3;
      if (ccb.size() == 4) ++ccb4;
      for (std::size_t i = 0; i < ccb.size(); ++i) {
        const HH h = ccb[i];
        const Geom stored = arr->he_curve(h);
        const Geom directed = arr->he_directed_curve(h);
        check(o.curve_equal(stored, directed),
              "he_directed_curve is the same geodesic as he_curve (only the direction differs)");
        check(o.point_equal(o.xcurve_source(directed), arr->vertex_point(arr->he_source(h))),
              "he_directed_curve().source() == he_source()'s point");
        check(o.point_equal(o.xcurve_target(directed), arr->vertex_point(arr->he_target(h))),
              "he_directed_curve().target() == he_target()'s point");
        // targets chain around the CCB
        const Geom next = arr->he_directed_curve(ccb[(i + 1) % ccb.size()]);
        check(o.point_equal(o.xcurve_target(directed), o.xcurve_source(next)),
              "the directed curves chain target -> source around the CCB");
        check(arr->he_target(h) == arr->he_source(arr->he_next(h)),
              "he_next() continues at the halfedge's target");
        check(arr->he_twin(arr->he_twin(h)) == h, "twin(twin(h)) == h");
        check(arr->he_face(h) == f, "every halfedge of the CCB is incident to the face");
        // The stored curve is shared with the twin; exactly one of the two halfedges runs
        // along it in the stored direction.
        const bool stored_l2r = (o.compare_endpoints_xy(stored) < 0);
        const bool he_l2r = (arr->he_direction(h) == ARR_LEFT_TO_RIGHT);
        const Geom twin_directed = arr->he_directed_curve(arr->he_twin(h));
        check(o.point_equal(o.xcurve_source(twin_directed), o.xcurve_target(directed)),
              "the twin's directed curve is the reverse");
        check(stored_l2r == he_l2r ? o.point_equal(o.xcurve_source(directed), o.xcurve_source(stored))
                                   : o.point_equal(o.xcurve_source(directed), o.xcurve_target(stored)),
              "he_direction() agrees with the stored curve's own direction");
      }
    }
    check_eq_sz(ccb3, 1, "one bounded face is bounded by 3 curves");
    check_eq_sz(ccb4, 1, "the other bounded face is bounded by 4 curves");
  }
  {
    // face_polygon of the two bounded faces: 3 and 4 directed x-monotone curves, no holes.
    std::size_t total = 0;
    for (FH f : faces) {
      if (arr->face_number_of_outer_ccbs(f) == 0) continue;
      std::vector<Geom> outer;
      std::vector<std::vector<Geom>> holes;
      arr->face_polygon(f, outer, holes);
      check(holes.empty(), "a bounded face of this arrangement has no holes");
      total += outer.size();
      for (std::size_t i = 0; i < outer.size(); ++i)
        check(o.point_equal(o.xcurve_target(outer[i]), o.xcurve_source(outer[(i + 1) % outer.size()])),
              "face_polygon's curves form a closed chain");
    }
    check_eq_sz(total, 7, "face_polygon: 3 + 4 curves over the two bounded faces");
  }

  // -----------------------------------------------------------------------
  section("arrangement: point location (every strategy)");
  {
    const Geom inside = Pt(2, 2, 1);   // z>0, x>z, y>z -> inside the triangle
    const Located naive = arr->locate(inside, PL_NAIVE);
    check_eq_int(naive.type, 2, "naive locate((2,2,1)) -> a face");
    const Located dflt = arr->locate(inside, PL_DEFAULT);
    check(dflt.p == naive.p, "PL_DEFAULT agrees with naive");
    const Located lm = arr->locate(inside, PL_LANDMARKS);
    check(lm.p == naive.p, "landmarks agrees with naive");
    check(arr->supports_point_location(PL_NAIVE), "naive is supported");
    check(arr->supports_point_location(PL_LANDMARKS), "landmarks is supported (uses Approximate_2)");
    // Arr_walk_along_line / Arr_simple need Topology_traits::initial_face(), which the spherical
    // topology traits does not have; Arr_trapezoid_ric compiles but aborts at construction;
    // Arr_triangulation needs a planar kernel point (traits_geodesic_sphere.md §10.9).
    const int unsupported[] = {PL_SIMPLE, PL_WALK, PL_TRAPEZOID, PL_TRIANGULATION};
    for (int s : unsupported) {
      check(!arr->supports_point_location(s),
            std::string("strategy '") + point_location_name(s) + "' is NOT supported");
      expect_error(ErrorCode::Unsupported, [&] { arr->locate(inside, s); },
                   std::string("locate with '") + point_location_name(s) + "' -> Unsupported");
      expect_error(ErrorCode::Unsupported, [&] { arr->attach_point_location(s); },
                   std::string("attach '") + point_location_name(s) + "' -> Unsupported");
    }
    // attached strategies
    arr->attach_point_location(PL_NAIVE);
    arr->attach_point_location(PL_LANDMARKS);
    check(arr->has_point_location(PL_NAIVE) && arr->has_point_location(PL_LANDMARKS),
          "naive and landmarks attach");
    check(arr->locate(inside, PL_LANDMARKS).p == naive.p, "attached landmarks still agrees");
    arr->detach_point_location(PL_LANDMARKS);
    check(!arr->has_point_location(PL_LANDMARKS), "landmarks detaches");
    arr->detach_point_location(PL_NAIVE);

    // locating a vertex and an edge
    const Located at_a = arr->locate(A, PL_NAIVE);
    check_eq_int(at_a.type, 0, "locate((1,0,0)) -> the vertex A");
    check(at_a.as_vertex().p != nullptr && o.point_equal(arr->vertex_point(at_a.as_vertex()), A),
          "... with the right point");
    const Located on_ca = arr->locate(Pt(2, 1, 1), PL_NAIVE);   // on the great circle y = z, between C and A
    check_eq_int(on_ca.type, 1, "locate((2,1,1)) -> a halfedge of the edge CA");

    // No point-location strategy on the sphere can shoot vertical rays.
    expect_error(ErrorCode::Unsupported, [&] { arr->ray_shoot_up(inside, PL_DEFAULT); },
                 "ray_shoot_up -> Unsupported on the sphere");
    expect_error(ErrorCode::Unsupported, [&] { arr->ray_shoot_down(inside, PL_DEFAULT); },
                 "ray_shoot_down -> Unsupported on the sphere");
  }

  // -----------------------------------------------------------------------
  section("arrangement: batched_locate");
  {
    // NOTE (documented CGAL trap): every query point must satisfy `is_no_boundary()`.
    // CGAL's batched sweep SEGFAULTS for a north-pole query and raises the Compare_xy_2
    // precondition for a south-pole one (see the report's open issues) — filter with
    // arr2d::sphere::point_location() first.
    std::vector<Geom> queries{Pt(2, 2, 1), Pt(1, 1, 10), A, Pt(2, 1, 1), Pt(2, 2, 1)};
    for (const Geom& q : queries)
      check_eq_int(sphere::point_location(q), sphere::NO_BOUNDARY,
                   "batched_locate query points must be boundary free");
    std::vector<Located> got;
    arr->batched_locate(queries, got);
    check_eq_sz(got.size(), queries.size(), "batched_locate returns one result per query");
    for (std::size_t i = 0; i < queries.size(); ++i) {
      const Located ref = arr->locate(queries[i], PL_NAIVE);
      check_eq_int(got[i].type, ref.type,
                   "batched_locate[" + std::to_string(i) + "] type matches naive PL for " +
                       xyz_str(o, queries[i]));
      if (ref.type == 2 || ref.type == 0)
        check(got[i].p == ref.p, "batched_locate[" + std::to_string(i) + "] identity matches");
    }
    check(got[0].p == got[4].p, "a duplicated query point gets the same answer twice");
  }

  // -----------------------------------------------------------------------
  section("arrangement: zone / do_intersect / decompose");
  {
    // An arc far away in the (x<0, y<0) region: entirely inside the spherical face.
    const Geom far = sphere::make_x_monotone_arc(Pt(-1, -1, -1), Pt(-2, -1, -1));
    std::vector<Located> z;
    arr->zone(far, z);
    check_eq_sz(z.size(), 1, "zone of a far-away arc -> 1 feature");
    check(z[0].type == 2 && z[0].as_face() == arr->unbounded_face(),
          "... the spherical face");
    check(!arr->do_intersect(far), "do_intersect(far arc) == false");

    // An arc crossing the triangle: face, edge, face, edge, face.
    const Geom cross = sphere::make_x_monotone_arc(Pt(5, 2, -1), Pt(1, 4, 3));
    std::vector<Located> z2;
    arr->zone(cross, z2);
    check_eq_sz(z2.size(), 5, "zone of a crossing arc -> 5 features");
    const int want_types[5] = {2, 1, 2, 1, 2};
    for (std::size_t i = 0; i < z2.size() && i < 5; ++i)
      check_eq_int(z2[i].type, want_types[i],
                   "zone feature " + std::to_string(i) + " is a " +
                       (want_types[i] == 2 ? "face" : "halfedge"));
    check(arr->do_intersect(cross), "do_intersect(crossing arc) == true");
    check(arr->do_intersect(sphere::make_arc(Pt(5, 2, -1), Pt(1, 4, 3))),
          "do_intersect also accepts a general Curve box");
    check_eq_sz(arr->number_of_edges(), 8, "zone / do_intersect do not modify the arrangement");
  }
  {
    // decompose works only while EVERY vertex is in the interior of the parameter space
    // (traits_geodesic_sphere.md §10.4.2); this arrangement satisfies that.
    std::vector<VerticalDecompositionEntry> dec;
    arr->decompose(dec);
    check_eq_sz(dec.size(), 7, "decompose emits one entry per vertex");
    std::size_t with_above = 0;
    for (const VerticalDecompositionEntry& e : dec) {
      check(arr->vertex_valid(e.v), "decompose returns valid vertex handles");
      if (e.above.type >= 0) ++with_above;
    }
    check(with_above == dec.size(),
          "on the sphere 'above' is never empty: it wraps to the spherical face");
  }

  // -----------------------------------------------------------------------
  section("arrangement: history, remove_curve, clone");
  {
    std::vector<HH> induced;
    arr->induced_edges(chord, induced);
    check_eq_sz(induced.size(), 3, "induced_edges(chord) == 3");
    for (HH h : induced) {
      std::vector<CH> orig;
      arr->originating_curves(h, orig);
      check_eq_sz(orig.size(), 1, "each induced edge has exactly 1 originating curve");
      check(!orig.empty() && orig[0] == chord, "... and it is the chord");
    }
    const Geom stored = arr->curve_geometry(chord);
    check(stored.type == GeomType::Curve, "curve_geometry returns the input Curve_2");
    check(o.curve_repr(stored) == o.curve_repr(PQ), "... unchanged: " + o.curve_repr(stored));
  }
  {
    std::unique_ptr<ArrBase> copy = arr->clone();
    check_eq_sz(copy->number_of_vertices(), 7, "clone: V == 7");
    check_eq_sz(copy->number_of_edges(), 8, "clone: E == 8");
    check_eq_sz(copy->number_of_faces(), 3, "clone: F == 3");
    check_eq_sz(copy->number_of_curves(), 4, "clone: 4 curves");
    check(copy->is_valid(), "clone: is_valid()");
    check(!copy->vertex_valid(verts[0]), "a handle of the original is invalid in the clone");
    check(arr->vertex_valid(verts[0]), "... and still valid in the original");
    BBox bb = copy->bbox();
    check_eq_int(bb.dim, 3, "ArrBase::bbox() is 3-D for the sphere");
    check(bb.lo[0] >= -1.0 && bb.hi[0] <= 1.0, "bbox x within [-1, 1] (unit directions)");
    std::vector<double> coords;
    copy->vertex_coordinates(coords);
    check_eq_sz(coords.size(), 21, "vertex_coordinates: 3 doubles x 7 vertices");
    for (std::size_t i = 0; i + 3 <= coords.size(); i += 3)
      check_near(norm(at(coords, i / 3)), 1.0, 1e-12, "every exported vertex is a unit vector");
    std::vector<std::size_t> idx;
    copy->edge_vertex_indices(idx);
    check_eq_sz(idx.size(), 16, "edge_vertex_indices: 2 indices x 8 edges");
    std::vector<std::vector<std::vector<std::size_t>>> fb;
    copy->face_boundaries(fb);
    check_eq_sz(fb.size(), 3, "face_boundaries: one entry per face");
  }
  {
    // Removing the chord removes its 3 edges and the two dangling degree-1 vertices P and Q;
    // the two crossing vertices survive with degree 2 (they still split AB and BC), so
    // V = 7 - 2 = 5, E = 8 - 3 = 5, F = 3 - 1 = 2, and V - E + F = 2 still holds.
    const std::size_t removed = arr->remove_curve(chord);
    check_eq_sz(removed, 3, "remove_curve(chord) removed 3 edges");
    check_eq_sz(arr->number_of_vertices(), 5, "after remove_curve: V == 5");
    check_eq_sz(arr->number_of_edges(), 5, "after remove_curve: E == 5");
    check_eq_sz(arr->number_of_faces(), 2, "after remove_curve: F == 2");
    check_eq_sz(arr->number_of_curves(), 3, "after remove_curve: 3 curves left");
    check(arr->is_valid(), "after remove_curve: is_valid()");
    expect_error(ErrorCode::InvalidHandle, [&] { arr->number_of_induced_edges(chord); },
                 "the removed curve handle is now invalid");
  }
  {
    arr->clear();
    check(arr->is_empty(), "clear() empties the arrangement");
    check_eq_sz(arr->number_of_faces(), 1, "... back to the single spherical face");
    check(arr->is_valid(), "... and it is valid");
    check(!arr->vertex_valid(verts[0]), "handles are invalidated by clear()");
  }

  // -----------------------------------------------------------------------
  section("arrangement: the identification meridian and the poles");
  {
    // The equator (normal (0,0,1)) plus the great circle through both poles with normal
    // (0,1,0) — half of which LIES ON the identification meridian.  Aggregate insertion is
    // order independent and never hits the Compare_xy_2 abort of the incremental path
    // (traits_geodesic_sphere.md gotcha G1).
    // Measured structure (§6.6 "equator + meridian circle"): V=4, E=6, F=4.
    std::unique_ptr<ArrBase> a2 = make_arrangement(Kind::Sphere);
    std::vector<Geom> cs{EQUATOR, MERIDIAN_CIRCLE};
    std::vector<CH> hs;
    a2->insert_curves(cs, hs);
    check_eq_sz(a2->number_of_vertices(), 4, "equator + meridian circle: V == 4");
    check_eq_sz(a2->number_of_edges(), 6, "equator + meridian circle: E == 6");
    check_eq_sz(a2->number_of_faces(), 4, "equator + meridian circle: F == 4 (3 lunes + spherical)");
    check(a2->is_valid(), "equator + meridian circle: is_valid()");
    check_eq_sz(a2->number_of_vertices() - a2->number_of_edges() + a2->number_of_faces(), 2,
                "Euler: 4 - 6 + 4 == 2");

    int n_pole = 0, n_ident = 0, n_interior = 0;
    std::vector<VH> vs;
    a2->vertices(vs);
    for (VH v : vs) {
      const Geom p = a2->vertex_point(v);
      switch (sphere::point_location(p)) {
        case sphere::MIN_BOUNDARY:
        case sphere::MAX_BOUNDARY: ++n_pole; break;
        case sphere::MID_BOUNDARY: ++n_ident; break;
        default: ++n_interior; break;
      }
      check(!a2->vertex_is_at_open_boundary(v), "even boundary vertices are concrete on the sphere");
    }
    check_eq_int(n_pole, 2, "two pole vertices");
    check_eq_int(n_ident, 1, "ONE vertex on the identification meridian (not two)");
    check_eq_int(n_interior, 1, "one interior vertex, at (1,0,0)");

    // decompose is unusable as soon as any vertex is on a pole / the identification: CGAL's
    // sweep calls Compare_x_2 whose precondition is `p.is_no_boundary()`
    // (traits_geodesic_sphere.md §10.4.2).  It arrives as a catchable CGAL exception.
    expect_any_throw([&] { std::vector<VerticalDecompositionEntry> d; a2->decompose(d); },
                     "decompose with pole/identification vertices raises the CGAL precondition");
  }

  // -----------------------------------------------------------------------
  section("misc error paths");
  {
    std::unique_ptr<ArrBase> a3 = make_arrangement(Kind::Sphere);
    SegmentTypes::Point_2 sp(SegmentTypes::FT(1), SegmentTypes::FT(2));
    const Geom seg_point = make_geom(Kind::Segment, GeomType::Point, sp);
    expect_error(ErrorCode::KindMismatch, [&] { a3->insert_point(seg_point); },
                 "inserting a segment-kind point -> KindMismatch");
    expect_error(ErrorCode::KindMismatch, [&] { o.point_repr(seg_point); },
                 "point_repr of a foreign kind -> KindMismatch");
    expect_error(ErrorCode::InvalidArgument, [&] { o.curve_repr(A); },
                 "curve_repr of a point box -> InvalidArgument");
    expect_error(ErrorCode::InvalidArgument, [&] { o.point_repr(AB); },
                 "point_repr of a curve box -> InvalidArgument");
    const VH stale{nullptr, 0};
    check(!a3->vertex_valid(stale), "a null handle is not valid");
    expect_error(ErrorCode::InvalidHandle, [&] { a3->vertex_point(stale); },
                 "using a null vertex handle -> InvalidHandle");
    // insert_point on the sphere, then remove it again through remove_vertex (this vertex is
    // interior, so the isolated-boundary-vertex corruption of gotcha G4 cannot apply).
    const VH v = a3->insert_point(Pt(1, 2, 3));
    check_eq_sz(a3->number_of_vertices(), 1, "insert_point creates one vertex");
    check(a3->vertex_is_isolated(v), "... isolated");
    check(a3->vertex_face(v) == a3->unbounded_face(), "... inside the spherical face");
    check(a3->remove_vertex(v), "remove_vertex of an isolated interior vertex succeeds");
    check(a3->is_empty(), "... and the arrangement is empty again");
  }

  // -----------------------------------------------------------------------
  std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
  if (g_failures == 0) std::printf("test_kind_sphere: OK\n");
  // Returning normally (never std::exit / abort) is part of the test: it proves that no
  // static-destruction abort happens for this kind.
  return g_failures == 0 ? 0 : 1;
}
