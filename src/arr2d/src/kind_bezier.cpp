// arr2d — Kind::Bezier.
//
// Contents (see docs/dev/DESIGN.md §1-2 and the task contract):
//   1. BezierTypes::traits()            — the ONE process-wide traits object.
//   2. class BezierOps                  — KindOps for this kind (KindOpsBase<BezierTypes> + the
//                                         kind-specific virtuals + the Split_2 workaround).
//   3. namespace arr2d::bezier          — every free function declared in ops.hpp.
//   4. template class ArrImpl<BezierTypes>  — the arrangement instantiation.
//   5. a static registrar               — registers ops / arrangement factory / polygon-set factory.
//
// Traits: CGAL::Arr_Bezier_curve_traits_2<Cartesian<CORE::BigRat>, Cartesian<CORE::Expr>,
//         CGAL::CORE_algebraic_number_traits>.  POLYNOMIAL Bezier curves with rational control
//         points, any degree.  Rational (weighted) Bezier curves are NOT representable here; a
//         rational *quadratic* one is exactly a conic arc (Kind::Conic, see DESIGN.md).
//
// ---------------------------------------------------------------------------------------------
// LIFETIME / MEMORY CONTRACT OF THIS TU (all four singletons are deliberately LEAKED)
// ---------------------------------------------------------------------------------------------
// (a) `BezierTypes::traits()` returns a heap-allocated, never-deleted Arr_Bezier_curve_traits_2.
//     It owns the Bezier_cache + Intersection_map that every arrangement of this kind shares
//     (traits_bezier.md gotcha 2: a *copy* of the traits aliases those two structures with
//     m_owner == false, so the original must outlive every copy — being immortal, it does).
//     Every ArrImpl<BezierTypes> is constructed with `&Types::traits()`, so
//     `Arrangement_on_surface_2::m_own_traits == false`; `assign()` (used by ArrImpl::clone())
//     then copies the *pointer* (Arrangement_on_surface_2_impl.h:200,
//     `m_geom_traits = (arr.m_own_traits) ? new Traits_adaptor_2 : arr.m_geom_traits;`).  The
//     "copy constructor default-constructs a fresh traits" trap (traits_bezier.md gotcha 10) is
//     therefore avoided: ArrImpl never copy-constructs a CGAL arrangement.  VERIFIED by reading
//     that source; the clone test in test_kind_bezier.cpp exercises the path.
//     It MUST be leaked: a traits/cache holding CORE::Expr values that is destroyed during static
//     teardown aborts with `CGAL error: assertion violation! ! blocks.empty()`
//     (CGAL/CORE/MemoryPool.h:125) — CORE's memory pools are gone by then.
// (b) `bezier_cache()` is OUR OWN Traits::Bezier_cache.  The traits' cache is `private` and every
//     functor keeps its own copy of the pointer (traits_bezier.md gotcha 3), so direct calls to
//     Point_2::make_exact(cache) / originator parameters need a cache of our own.  _Bezier_cache
//     is non-copyable, purely memoising and keyed by Curve_2::id(), so a second cache is correct
//     (it only duplicates work).  Leaked for the same CORE reason.
// (c) `retention()` is the Curve_2 RETENTION REGISTRY.  `Curve_2::id()` is
//     `reinterpret_cast<size_t>(rep pointer)` and BOTH caches are keyed by it and are never
//     invalidated (traits_bezier.md gotcha 7).  If the last handle to a Curve_2 dies, its rep is
//     freed, the address can be reused by a new curve, and that new curve silently inherits the
//     stale vertical tangencies / intersections of the dead one.  Every Curve_2 that enters this
//     TU is therefore pushed into a std::deque<Curve_2> that lives for the whole process, keyed
//     by an unordered_set<size_t> of ids so a curve is stored once.
//     MEMORY IMPLICATION (documented, intentional): Bezier curves are never freed.  A process
//     that builds N distinct Bezier curves keeps all N alive (one rep = control-point deque +
//     two lazily built integer polynomials), and the traits' caches grow monotonically as well
//     (there is no _Bezier_cache::clear()).  This is the only safe behaviour with CGAL 6.1.
// (d) the BezierOps singleton (registrar below) — leaked as required by
//     docs/dev/STAGE1_NOTES.md ("the per-kind KindOps singleton should be leaked too"); it also
//     owns a deliberately leaked Arr_traits_adaptor_2 (see kind_ops_base.hpp).
//
// ---------------------------------------------------------------------------------------------
// CGAL 6.1 traps handled here (docs/dev/CGAL_TRAPS_CHECKLIST.md "Bezier kind" + traits_bezier.md)
// ---------------------------------------------------------------------------------------------
//  * Point_2::x()/y() have `\pre is_exact()` and dereference a null Algebraic* in a release build
//    (traits_bezier.md gotcha 4, exact_coordinates_contract.md gotcha 4).  EVERY read of x()/y()
//    in this file goes through ensure_exact() first.
//  * parameter_range() is an approximation of an approximation and can be 14 % wrong before the
//    endpoints are exact (gotcha 5).  bezier::parameter_range() and approximate() use the exact
//    route of §10.1 instead: source().get_originator(supporting_curve(), xid())->parameter().
//  * Curve_2::id() / cache aliasing — the retention registry, see (c) above.
//  * No Approximate_2 at all (gotcha 8): approximate() is implemented here with de Casteljau in
//    doubles + flatness-driven adaptive subdivision (rendering_and_approximation.md §3.1).
//  * Split_2 silently accepts a rational point that is nowhere near the curve
//    (exact_coordinates_contract.md gotcha 5): BezierOps::split() validates first.
//  * Construct_opposite_2::operator() is non-const, Merge_2 / Trim_2 have private constructors
//    (gotcha 12) — all handled by KindOpsBase (it fetches every functor into a non-const local).
//  * Multiplicity is 0/1 only and means "unknown / simple" (gotcha 11) — passed through verbatim
//    by KindOpsBase::intersect().
//  * Comparing two Point_2 mutates both reps and may merge their originator lists (gotcha 9).
//    Nothing here keeps a raw pointer or an iterator into a rep across such a call.
//  * approximate()'s tolerance is validated (> 0, tiny values clamped to 1e-12) BEFORE any
//    subdivision runs (rendering_and_approximation.md gotcha 7).
//  * CGAL::to_double() is never used for a user-visible coordinate (number_types_and_errors.md
//    gotcha 2); everything goes through to_double_correctly_rounded() from number_conv.hpp.
#include "arr2d/kinds/bezier_types.hpp"

#include <CGAL/convex_hull_2.h>   // CGAL::is_convex_2, for control_polygon_is_convex()

#include "arr2d/impl/kind_ops_base.hpp"
#include "arr2d/impl/number_conv.hpp"
#include "arr2d/impl/arr_impl.hpp"

#include "arr2d/bso.hpp"
#include "arr2d/numbers.hpp"
#include "arr2d/ops.hpp"
#include "arr2d/registry.hpp"

// Other kinds' CGAL types, for convert_point() / convert_curve().  These headers are
// self-contained: nothing below calls SegmentTypes::traits() & co, so this TU does NOT depend on
// the other kind TUs at link time (that is what lets test_kind_bezier.cpp link kind_bezier.o
// alone).  The conversions work directly on the boxed CGAL objects, identified with Geom::holds<>.
#include "arr2d/kinds/linear_types.hpp"
#include "arr2d/kinds/polyline_types.hpp"
#include "arr2d/kinds/segment_types.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iterator>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace arr2d {

// ===========================================================================================
// 1. The process-wide traits object
// ===========================================================================================

/// ONE process-wide instance, heap-allocated and never destroyed (see (a) in the file header).
/// Lifetime: it outlives every Curve_2 / X_monotone_curve_2 / Point_2 / arrangement / polygon set
/// of this kind, because it is only released when the process image goes away.
BezierTypes::Traits& BezierTypes::traits() {
  static BezierTypes::Traits* t = new BezierTypes::Traits();
  return *t;
}

namespace {

using BT = BezierTypes;
using Traits_2 = BT::Traits;
using Point_2 = BT::Point_2;
using Curve_2 = BT::Curve_2;
using Xcv_2 = BT::X_monotone_curve_2;
using Rat_point_2 = BT::Rat_point_2;
using Bezier_cache = Traits_2::Bezier_cache;
using Originator = Point_2::Originator;
using Nt_traits = BT::Nt_traits;

// ---------------------------------------------------------------------------
// Our own Bezier cache (see (b) in the file header).  Leaked on purpose.
// ---------------------------------------------------------------------------
Bezier_cache& bezier_cache() {
  static Bezier_cache* c = new Bezier_cache();
  return *c;
}

// ---------------------------------------------------------------------------
// Curve_2 retention registry (see (c) in the file header).  Leaked on purpose.
// ---------------------------------------------------------------------------
struct Retention {
  std::deque<Curve_2> curves;             ///< owning storage; deque never moves its elements
  std::unordered_set<std::size_t> ids;    ///< Curve_2::id() values already stored
};

Retention& retention() {
  static Retention* r = new Retention();
  return *r;
}

/// Keep `c`'s representation alive for the rest of the process, so that its id() can never be
/// recycled while the (never-cleared) Bezier caches still hold entries for it.
void retain(const Curve_2& c) {
  if (c.number_of_control_points() == 0) return;   // a default-constructed curve has no rep data
  Retention& r = retention();
  if (r.ids.insert(c.id()).second) r.curves.push_back(c);
}

// ---------------------------------------------------------------------------
// Self-intersection registry (CGAL 6.1 memory-corruption guard, see below).
// Leaked on purpose: the points hold CORE::Expr coordinates and a static-storage CORE value
// aborts the process at exit (CGAL_TRAPS_CHECKLIST "Process/build").
// ---------------------------------------------------------------------------
struct SelfIsect {
  std::unordered_map<std::size_t, std::vector<Point_2>> map;   ///< Curve_2::id() -> crossings
  std::unordered_set<std::size_t> undecidable;                 ///< see self_intersection_points()
};

SelfIsect& self_isect() {
  static SelfIsect* r = new SelfIsect();
  return *r;
}

/// SOUND certificate of simplicity, used INSTEAD of `Curve_2::has_no_self_intersections()`.
///
/// CGAL 6.1's flag is `! Bezier_bounding_rational_traits::may_have_self_intersections(ctrl)`
/// (Bezier_curve_2.h:205), which answers "no self-intersections" when the control polygon is
/// convex OR when `_compute_angular_span()` claims the control-polygon edge directions span less
/// than 180 degrees (Bezier_bounding_rational_traits.h:291-308).  The first test is a theorem;
/// the SECOND ONE IS UNSOUND.  It classifies every edge vector against
/// `dir = front -> back` with `orientation(v, ORIGIN, dir)`, which cannot tell "same direction"
/// from "opposite direction": an edge ANTIPODAL to `dir` comes out COLLINEAR and is silently
/// dropped from the span.  MEASURED with the quartic (-4,-4)(-3,4)(-3,-6)(1,3)(-4,-2), whose
/// edge directions span 204 degrees but whose second edge (0,-10) is antipodal to
/// `dir = (0,2)`: CGAL reports `has_no_self_intersections() == true` although the curve crosses
/// itself twice (exactly, at ~(-3.6153,-1.6270) and ~(-2.5932,-0.7837)).  The consequence is not
/// only that our guard would miss it: `_Bezier_x_monotone_2::_exact_intersect` short-circuits on
/// the same flag (Bezier_x_monotone_2.h:2120-2125), so CGAL builds a SILENTLY WRONG arrangement
/// for that curve — measured V=3 E=2 F=1, i.e. both crossings missing.
///
/// The sound replacement, in the same spirit and just as cheap:
///   (a) all NON-ZERO control-polygon edge vectors lie in one OPEN half-plane {u : u.w > 0}.
///       The derivative of a degree-n Bezier is the degree-(n-1) Bezier on the control points
///       n*(P_{i+1} - P_i), so B'(t).w is a Bernstein combination (weights >= 0, summing to 1,
///       all strictly positive on (0,1)) of strictly positive numbers: B'(t).w > 0, hence
///       t -> B(t).w is strictly increasing and B is injective.  Exact O(n^2) rational test:
///       such a `w` exists iff some edge vector has every other one within [0,180) degrees
///       counterclockwise of it.
///   (b) otherwise, CGAL's own (sound) convexity test: a convex control polygon gives a convex,
///       hence simple, Bezier curve.  `CGAL::is_convex_2` also accepts a COLLINEAR control
///       polygon, so self_intersection_points() screens the reversing straight-line motions out
///       before reaching it.
bool control_polygon_in_open_halfplane(const Curve_2& B) {
  std::vector<std::pair<Rational, Rational>> v;   // non-zero control-polygon edge vectors
  auto it = B.control_points_begin();
  const auto end = B.control_points_end();
  if (it == end) return true;                     // no control points at all
  Rat_point_2 prev = *it;
  for (++it; it != end; ++it) {
    const Rational dx = it->x() - prev.x();
    const Rational dy = it->y() - prev.y();
    prev = *it;
    if (dx != 0 || dy != 0) v.emplace_back(dx, dy);
  }
  if (v.empty()) return true;                     // every control point coincides: a single point
  for (const auto& a : v) {
    bool all_within = true;
    for (const auto& b : v) {
      const Rational cross = a.first * b.second - a.second * b.first;
      if (cross > 0) continue;                                            // strictly CCW of a
      if (cross == 0 && (a.first * b.first + a.second * b.second) > 0) continue;   // parallel to a
      all_within = false;
      break;
    }
    if (all_within) return true;
  }
  return false;
}

bool control_polygon_is_convex(const Curve_2& B) {
  return CGAL::is_convex_2(B.control_points_begin(), B.control_points_end(), BT::Rat_kernel());
}

/// n choose k, exactly (n is a Bezier degree, so tiny).
Integer binomial(std::size_t n, std::size_t k) {
  if (k > n) return Integer(0);
  if (k > n - k) k = n - k;
  Integer r(1);
  for (std::size_t i = 0; i < k; ++i) {
    r *= Integer(static_cast<long>(n - i));
    r /= Integer(static_cast<long>(i + 1));
  }
  return r;
}

/// EXACT injectivity test for a curve whose control points are COLLINEAR.
///
/// CGAL_TRAPS_CHECKLIST "Bezier kind" used to list this as OPEN: a Bezier curve whose control
/// points are collinear and whose motion along that line REVERSES traces the same segment twice,
/// which no arrangement can represent.  `Curve_2::has_no_self_intersections()` calls it simple
/// (the control polygon is "convex"), so CGAL's sweep never looks for the overlap and then breaks
/// in an input-dependent way — measured: (0,0)(3,3)(1,1) raises
/// `CGAL assertion violation: org != p.originators_end()` (Bezier_x_monotone_2.h:1008) and
/// (0,0)(4,4)(-1,-1)(2,2) HANGS forever, i.e. a denial of service from ordinary user input.
/// `CGAL::is_convex_2` cannot tell "convex" from "collinear", so the screen has to run BEFORE
/// control_polygon_is_convex().
///
/// With every control point on the line P0 + s*d the curve is B(t) = P0 + lambda(t)*d, where
/// lambda is the Bernstein polynomial of the scalars lambda_i = (P_i - P0).d.  B is injective on
/// [0,1] iff lambda is, i.e. iff lambda' does not change sign there.  A coefficient test is only
/// SUFFICIENT (the control scalars 0,1,0,1 give lambda'(t) = 3(2t-1)^2 >= 0, a monotone and
/// perfectly legal curve whose differences alternate — that is why this was left open before), so
/// decide it exactly: isolate lambda's real roots with the Nt_traits the TU already uses and check
/// that the sign of lambda' is constant between them.
///
/// \return  1 = collinear and injective (safe), 0 = collinear and NOT injective (must be refused),
///         -1 = the control polygon is not collinear (nothing decided here).
int collinear_motion_is_injective(const Curve_2& B) {
  std::vector<Rat_point_2> P(B.control_points_begin(), B.control_points_end());
  if (P.size() < 2) return 1;
  std::size_t k = 1;
  while (k < P.size() && P[k].x() == P[0].x() && P[k].y() == P[0].y()) ++k;
  if (k == P.size()) return 1;                       // every control point coincides
  const Rational dx = P[k].x() - P[0].x();
  const Rational dy = P[k].y() - P[0].y();
  for (std::size_t i = 1; i < P.size(); ++i) {
    const Rational ax = P[i].x() - P[0].x();
    const Rational ay = P[i].y() - P[0].y();
    if (dx * ay - dy * ax != 0) return -1;           // not collinear
  }

  const std::size_t n = P.size() - 1;
  std::vector<Rational> lam(n + 1);
  for (std::size_t i = 0; i <= n; ++i)
    lam[i] = (P[i].x() - P[0].x()) * dx + (P[i].y() - P[0].y()) * dy;

  // Bernstein -> monomial:  lambda(t) = sum_i lam_i C(n,i) t^i (1-t)^(n-i).
  std::vector<Rational> f(n + 1, Rational(0));
  for (std::size_t i = 0; i <= n; ++i) {
    if (lam[i] == 0) continue;
    const Rational ci = Rational(binomial(n, i)) * lam[i];
    for (std::size_t j = 0; i + j <= n; ++j) {
      Rational term = ci * Rational(binomial(n - i, j));
      if (j % 2 == 1) term = -term;
      f[i + j] += term;
    }
  }
  if (n == 0) return 1;
  std::vector<Rational> g(n);
  for (std::size_t d = 0; d < n; ++d) g[d] = f[d + 1] * Rational(static_cast<long>(d + 1));

  Nt_traits nt;
  Nt_traits::Polynomial gp;
  Integer denom;
  if (!nt.construct_polynomial(g.data(), static_cast<unsigned int>(n - 1), gp, denom))
    return 0;   // lambda' is identically zero: the whole curve is one point, traced forever
  std::vector<Algebraic> roots;
  nt.compute_polynomial_roots(gp, std::back_inserter(roots));   // ascending
  std::vector<Algebraic> bounds;
  bounds.push_back(Algebraic(0));
  for (const Algebraic& r : roots) {
    Algebraic below_one = r - Algebraic(1);
    if (r.sign() > 0 && below_one.sign() < 0) bounds.push_back(r);
  }
  bounds.push_back(Algebraic(1));

  int s = 0;
  for (std::size_t i = 0; i + 1 < bounds.size(); ++i) {
    Algebraic mid = (bounds[i] + bounds[i + 1]) / Algebraic(2);
    Algebraic val = nt.evaluate_at(gp, mid);
    const int sv = val.sign();
    if (sv == 0) return 0;                 // lambda' vanishes on a whole interval
    if (s == 0) s = sv;
    else if (s != sv) return 0;            // the motion reverses: the curve overlaps itself
  }
  return 1;
}

/// The exact points at which the supporting curve `B` crosses ITSELF (empty for the vast
/// majority of curves).  Computed through the `id1 == id2` branch of
/// `_Bezier_cache::get_intersections` (Bezier_cache.h:342-389), which is a completely separate,
/// SOUND code path from the two-curve pairing loop that the guard below protects against.
///
/// `undecidable` collects the curves for which CGAL cannot answer at all: that branch needs a
/// non-constant X(t) AND a non-constant Y(t) (`CGAL_assertion(degX > 0)` / `(degY > 0)`,
/// Bezier_cache.h:664/681) and, for degree 1 in both, builds a 0x0 Sylvester matrix and SIGSEGVs
/// (measured, `_compute_resultant`).  A curve reaches this only when it is a STRAIGHT
/// axis-parallel motion that REVERSES (a constant coordinate forces every edge vector onto one
/// axis, and if they were all in an open half-plane the certificate above would already have
/// answered) — i.e. a curve that overlaps ITSELF along an interval.  check_sweepable() refuses
/// those outright rather than letting CGAL assert mid-sweep or treat them as simple.
const std::vector<Point_2>& self_intersection_points(const Curve_2& B) {
  auto& si = self_isect();
  auto& m = si.map;
  auto it = m.find(B.id());
  if (it != m.end()) return it->second;
  std::vector<Point_2>& out = m[B.id()];
  if (control_polygon_in_open_halfplane(B)) return out;          // (a): sound and cheap
  // (a2) A COLLINEAR control polygon: CGAL::is_convex_2 accepts it, so the exact test below has
  // to run BEFORE (b).  It decides both the legal case (a monotone motion whose differences
  // alternate, e.g. the control scalars 0,1,0,1) and the illegal one (a motion that reverses and
  // therefore traces the same segment twice, which CGAL 6.1 answers with an assertion or an
  // infinite loop).  This also subsumes the axis-parallel sub-case screened below.
  {
    const int inj = collinear_motion_is_injective(B);
    if (inj == 1) return out;                                    // collinear and injective
    if (inj == 0) { si.undecidable.insert(B.id()); return out; } // collinear and reversing
  }
  Nt_traits nt;
  if (nt.degree(B.x_polynomial()) < 1 || nt.degree(B.y_polynomial()) < 1) {
    // A constant coordinate puts every control-polygon edge on one axis, and (a) has already
    // ruled out their all pointing the same way: this is a straight axis-parallel motion that
    // REVERSES, i.e. a curve overlapping its own image.  CGAL cannot decide it (see above) and
    // its own `has_no_self_intersections()` calls it simple because the control points are
    // collinear, after which the sweep inserts the two coincident halves as separate edges.
    // Tested BEFORE the convexity certificate (b), which would otherwise accept it.
    si.undecidable.insert(B.id());
    return out;
  }
  if (control_polygon_is_convex(B)) return out;                  // (b): CGAL's sound test
  retain(B);   // both caches are keyed by id(): the rep must outlive them
  bool ovlp = false;
  const auto& lst = bezier_cache().get_intersections(
      B.id(), B.x_polynomial(), B.x_norm(), B.y_polynomial(), B.y_norm(),
      B.id(), B.x_polynomial(), B.x_norm(), B.y_polynomial(), B.y_norm(), ovlp);
  for (const auto& ip : lst) {
    // The self-intersection branch also reports the trivial pairs (s, s).
    if (CGAL::compare(ip.s, ip.t) == CGAL::EQUAL) continue;
    out.push_back(Point_2(B, ip.s));
  }
  return out;
}

/// True when CGAL 6.1 cannot decide `B`'s self-intersections at all (see above).  Only ever true
/// after self_intersection_points(B) has run.
bool self_intersections_undecidable(const Curve_2& B) {
  self_intersection_points(B);
  return self_isect().undecidable.count(B.id()) != 0;
}

// ---------------------------------------------------------------------------
// Small formatting helpers
// ---------------------------------------------------------------------------

std::string fmt_rational(const Rational& r) {
  std::string num, den;
  rational_to_strings(r, num, den);
  return (den == "1") ? num : (num + "/" + den);
}

/// Shortest decimal form that reads back as the same double (Python-repr style).
std::string fmt_double(double v) {
  char buf[64];
  for (int prec = 15; prec <= 17; ++prec) {
    std::snprintf(buf, sizeof(buf), "%.*g", prec, v);
    if (std::strtod(buf, nullptr) == v) break;
  }
  return std::string(buf);
}

// ---------------------------------------------------------------------------
// Point helpers
// ---------------------------------------------------------------------------

/// Make `p` exact if it is not already, so that x()/y() and the originators' parameters may be
/// read.  Returns false when the point cannot be exactified.
///
/// Guard rail: `_Bezier_point_2_rep::_make_exact` (Bezier_point_2.h:1537) handles exactly one or
/// two originators — with an empty originator list it walks off `_origs.begin()`.  A point with
/// no originators that is not already exact can only be a default-constructed one, so it is
/// rejected here instead of being handed to CGAL.
bool ensure_exact(const Point_2& p) {
  if (p.is_exact()) return true;
  if (p.originators_begin() == p.originators_end()) return false;
  p.make_exact(bezier_cache());   // const member; const_casts the rep internally
  return p.is_exact();
}

/// Exact rational bounding box of a point (always available, no cache needed;
/// traits_bezier.md §10.3(e)).  NOTE the out-parameter order of CGAL's get_bbox is
/// (min_x, min_y, max_x, max_y) — NOT the Bbox_2 convention (gotcha 13).
void point_bbox(const Point_2& p, Rational& min_x, Rational& min_y, Rational& max_x,
                Rational& max_y) {
  p.get_bbox(min_x, min_y, max_x, max_y);
}

/// Approximate coordinates, correctly rounded whenever the point can be exactified.
void point_xy_double(const Point_2& p, double& x, double& y) {
  if (p.is_rational()) {
    // operator Rat_point_2() is the only exact-rational route (\pre is_rational()).
    const Rat_point_2 rp = static_cast<Rat_point_2>(p);
    x = to_double_correctly_rounded(rp.x());
    y = to_double_correctly_rounded(rp.y());
    return;
  }
  if (ensure_exact(p)) {
    // CGAL's own Point_2::approximate() would use CGAL::to_double(Expr) here, which is not
    // correctly rounded (number_types_and_errors.md gotcha 2).
    x = to_double_correctly_rounded(p.x());
    y = to_double_correctly_rounded(p.y());
    return;
  }
  // Last resort (a point that cannot be exactified): the centre of its exact rational bbox,
  // which is what _Bezier_point_2::approximate() returns as well.
  Rational x0, y0, x1, y1;
  point_bbox(p, x0, y0, x1, y1);
  x = to_double_correctly_rounded(Rational((x0 + x1) / Rational(2)));
  y = to_double_correctly_rounded(Rational((y0 + y1) / Rational(2)));
}

/// The parameter of one originator as a double: the exact algebraic value when available,
/// otherwise the midpoint of its (exact rational) bounding interval — which is what CGAL's
/// X_monotone_curve_2::parameter_range() always uses (traits_bezier.md gotcha 5).
double originator_t(const Originator& o) {
  if (o.has_parameter()) return to_double_correctly_rounded(o.parameter());
  const auto& b = o.point_bound();
  return to_double_correctly_rounded(Rational((b.t_min + b.t_max) / Rational(2)));
}

/// Parameter values of an x-monotone piece at its SOURCE and TARGET, in that (stored) order.
/// Implements traits_bezier.md §10.1 (the public replacement of the private `_t_range`):
/// both endpoints are made exact first, then the originator bound to (supporting curve, xid)
/// is read.  Falls back to CGAL's parameter_range() only if an originator is missing, which
/// should not happen for a curve built by the traits.
void xcv_t_range(const Xcv_2& c, double& t_src, double& t_tgt) {
  const Curve_2& B = c.supporting_curve();
  const Point_2& s = c.source();
  const Point_2& t = c.target();
  ensure_exact(s);
  ensure_exact(t);
  // std::list iterators: make_exact() writes into the EXISTING originator, so looking the
  // originators up after exactification is correct (and required — the parameter is only set
  // by make_exact).
  auto so = s.get_originator(B, c.xid());
  auto to = t.get_originator(B, c.xid());
  if (so == s.originators_end() || to == t.originators_end()) {
    const std::pair<double, double> pr = c.parameter_range();
    t_src = pr.first;
    t_tgt = pr.second;
    return;
  }
  t_src = originator_t(*so);
  t_tgt = originator_t(*to);
}

// ---------------------------------------------------------------------------
// de Casteljau in doubles (rendering_and_approximation.md §3.1)
// ---------------------------------------------------------------------------

struct P2d {
  double x = 0.0, y = 0.0;
};

/// Recursion / output caps for the adaptive subdivision.  The flatness criterion converges
/// quadratically, so depth 24 covers tolerances down to the 1e-12 floor for curves of any
/// sensible size; the point cap bounds the memory a pathological request can ask for.
constexpr int kMaxDepth = 24;
constexpr std::size_t kMaxPoints = 1u << 20;

void control_points_double(const Curve_2& B, std::vector<P2d>& out) {
  out.clear();
  out.reserve(B.number_of_control_points());
  for (auto it = B.control_points_begin(); it != B.control_points_end(); ++it)
    out.push_back(P2d{to_double_correctly_rounded(it->x()), to_double_correctly_rounded(it->y())});
}

/// de Casteljau split at u: L spans [0,u], R spans [u,1] (both in the original parameter).
void split_at(const std::vector<P2d>& c, double u, std::vector<P2d>& L, std::vector<P2d>& R) {
  std::vector<P2d> v = c;
  const std::size_t n = v.size();
  L.clear();
  L.reserve(n);
  R.assign(n, P2d{});
  L.push_back(v[0]);
  R[n - 1] = v[n - 1];
  for (std::size_t last = n - 1; last > 0; --last) {
    for (std::size_t i = 0; i < last; ++i)
      v[i] = P2d{v[i].x + u * (v[i + 1].x - v[i].x), v[i].y + u * (v[i + 1].y - v[i].y)};
    L.push_back(v[0]);
    R[last - 1] = v[last - 1];
  }
}

/// Control polygon of the sub-arc over [a, b] (0 <= a < b <= 1).
std::vector<P2d> restrict_to(const std::vector<P2d>& c, double a, double b) {
  if (a < 0.0) a = 0.0;
  if (b > 1.0) b = 1.0;
  if (!(b > a) || c.size() < 2) return std::vector<P2d>{c.front(), c.back()};
  std::vector<P2d> L, R;
  if (a > 0.0) {
    if (!(1.0 - a > 0.0)) return std::vector<P2d>{c.back(), c.back()};
    split_at(c, a, L, R);                     // R spans [a, 1]
    const double u = (b - a) / (1.0 - a);
    if (!(u > 0.0) || u >= 1.0) return R;
    std::vector<P2d> L2, R2;
    split_at(R, u, L2, R2);                   // L2 spans [a, b]
    return L2;
  }
  if (b >= 1.0) return c;
  split_at(c, b, L, R);
  return L;
}

/// Maximum distance from a control point to the chord's supporting line (standard flatness
/// bound; by the convex-hull property the whole sub-arc is then within that distance of the
/// chord).
double flatness(const std::vector<P2d>& c) {
  const P2d& p0 = c.front();
  const P2d& pn = c.back();
  const double dx = pn.x - p0.x, dy = pn.y - p0.y;
  const double len = std::hypot(dx, dy);
  double m = 0.0;
  for (std::size_t i = 1; i + 1 < c.size(); ++i) {
    const double d = (len < 1e-300)
                         ? std::hypot(c[i].x - p0.x, c[i].y - p0.y)
                         : std::fabs((c[i].x - p0.x) * dy - (c[i].y - p0.y) * dx) / len;
    m = std::max(m, d);
  }
  return m;
}

/// Emit the sub-arc as chords, appending every chord's END point (the caller seeds `out` with
/// the start point).
void emit_flat(const std::vector<P2d>& c, double tol, int depth, std::vector<P2d>& out) {
  if (depth >= kMaxDepth || out.size() >= kMaxPoints || flatness(c) < tol) {
    out.push_back(c.back());
    return;
  }
  std::vector<P2d> L, R;
  split_at(c, 0.5, L, R);
  emit_flat(L, tol, depth + 1, out);
  emit_flat(R, tol, depth + 1, out);
}

/// Evaluate a double control polygon at t by de Casteljau.
P2d eval_double(const std::vector<P2d>& c, double t) {
  std::vector<P2d> v = c;
  for (std::size_t last = v.size() - 1; last > 0; --last)
    for (std::size_t i = 0; i < last; ++i)
      v[i] = P2d{v[i].x + t * (v[i + 1].x - v[i].x), v[i].y + t * (v[i + 1].y - v[i].y)};
  return v[0];
}

// ---------------------------------------------------------------------------
// Cross-kind helpers (headers only, no other kind TU is referenced)
// ---------------------------------------------------------------------------

using Epeck_point_2 = CGAL::Epeck::Point_2;                       ///< segment / linear / polyline Point_2
using Seg_curve_2 = SegmentTypes::Curve_2;                        ///< Arr_segment_2<Epeck>
using Lin_curve_2 = LinearTypes::Curve_2;                         ///< Arr_linear_object_2<Epeck>
using Poly_curve_2 = PolylineTypes::Curve_2;                      ///< internal::Polycurve_2<...>
using Poly_xcurve_2 = PolylineTypes::X_monotone_curve_2;          ///< internal::X_monotone_polycurve_2<...>

[[noreturn]] void bad(ErrorCode code, const std::string& msg) {
  throw_error(code, "bezier: " + msg);
}

/// Exact rational coordinates of a point of ANY kind.  Kinds whose Point_2 is Epeck::Point_2
/// (segment / linear / polyline) and our own Bezier points are handled directly, so that this TU
/// never needs another kind TU to be linked; every other kind goes through the registry (and
/// therefore needs its TU).
void rational_xy_of_point(const Geom& p, Rational& x, Rational& y) {
  require_type(p, GeomType::Point, "point");
  if (p.holds<Point_2>()) {
    const Point_2& bp = p.as<Point_2>();
    if (!bp.is_rational())
      bad(ErrorCode::NotRepresentable,
          "the Bezier point has algebraic coordinates and cannot be written as exact rationals");
    const Rat_point_2 rp = static_cast<Rat_point_2>(bp);
    x = rp.x();
    y = rp.y();
    return;
  }
  if (p.holds<Epeck_point_2>()) {
    const Epeck_point_2& q = p.as<Epeck_point_2>();
    x = to_rational(q.x());   // goes through .exact(); never CGAL::to_double
    y = to_rational(q.y());
    return;
  }
  if (p.kind == Kind::Sphere)
    bad(ErrorCode::KindMismatch,
        "a sphere point is a 3D direction and has no (x, y) planar coordinates");
  if (!kind_available(p.kind))
    bad(ErrorCode::Unsupported,
        std::string("cannot convert a point of kind '") + kind_name(p.kind) +
            "': that kind is not linked into this build (only segment, linear, polyline and "
            "bezier points are converted without it)");
  const KindOps& src = arr2d::ops(p.kind);
  if (src.dimension() != 2)
    bad(ErrorCode::KindMismatch, std::string("kind '") + kind_name(p.kind) + "' is not planar");
  std::vector<Rational> xy;
  src.point_exact_rational(p, xy);   // throws NotRepresentable for algebraic coordinates
  if (xy.size() < 2) bad(ErrorCode::NotRepresentable, "the source point has no rational coordinates");
  x = xy[0];
  y = xy[1];
}

/// Build (and retain) a Bezier curve from >= 2 rational control points.
Geom make_curve(const std::vector<Rat_point_2>& pts) {
  if (pts.size() < 2)
    bad(ErrorCode::InvalidArgument, "a Bezier curve needs at least 2 control points");
  bool all_equal = true;
  for (std::size_t i = 1; i < pts.size() && all_equal; ++i)
    if (pts[i].x() != pts[0].x() || pts[i].y() != pts[0].y()) all_equal = false;
  if (all_equal)
    bad(ErrorCode::InvalidArgument,
        "all control points coincide: the resulting curve would be a single point, not a curve");
  // CGAL precondition (Bezier_curve_2.h): at least 2 control points.  The "no two identical
  // consecutive control points" precondition of the doc comment is commented out in 6.1
  // (duplicates are tolerated by the polynomial construction), so it is not enforced here.
  Curve_2 B(pts.begin(), pts.end());
  retain(B);
  return make_geom(Kind::Bezier, GeomType::Curve, B);
}

/// A degree-1 Bezier curve (a straight segment) between two rational points.
Geom make_linear_curve(const Rational& x0, const Rational& y0, const Rational& x1,
                       const Rational& y1) {
  std::vector<Rat_point_2> pts;
  pts.emplace_back(x0, y0);
  pts.emplace_back(x1, y1);
  return make_curve(pts);
}

Geom make_linear_curve(const Epeck_point_2& a, const Epeck_point_2& b) {
  return make_linear_curve(to_rational(a.x()), to_rational(a.y()), to_rational(b.x()),
                           to_rational(b.y()));
}

/// Polyline (general or x-monotone) -> one degree-1 Bezier curve per sub-segment.
template <class Polycurve>
void convert_polycurve(const Polycurve& pc, std::vector<Geom>& out) {
  const std::size_t n = pc.number_of_subcurves();
  if (n == 0)
    bad(ErrorCode::InvalidArgument, "the polyline has no sub-segments");
  for (std::size_t i = 0; i < n; ++i) {
    const auto& seg = pc[i];   // Arr_segment_2<Epeck>
    out.push_back(make_linear_curve(seg.source(), seg.target()));
  }
}

}  // namespace

// ===========================================================================================
// 2. BezierOps
// ===========================================================================================

class BezierOps final : public KindOpsBase<BezierTypes> {
 public:
  using Base = KindOpsBase<BezierTypes>;

  bool has_polygon_set() const override { return true; }

  // ------------------------------------------------------------------ points

  Geom make_point(const Rational& x, const Rational& y) const override {
    // _Bezier_point_2(const Rational&, const Rational&): sets both the rational and the algebraic
    // coordinates; is_exact() && is_rational().  No originators (the point is not on any curve).
    return box_point(Point_2(x, y));
  }

  Geom make_point_3(const Rational&, const Rational&, const Rational&) const override {
    throw_error(ErrorCode::Unsupported,
                "bezier: make_point_3() is meaningless for this kind — Bezier arrangements are "
                "planar (bounded planar topology); only the 'sphere' kind uses 3D directions");
  }

  void point_approx(const Geom& p, double* xyz) const override {
    point_xy_double(point(p), xyz[0], xyz[1]);
  }

  void point_interval(const Geom& p, std::vector<std::pair<double, double>>& out) const override {
    const Point_2& q = point(p);
    out.clear();
    out.reserve(2);
    if (q.is_rational()) {
      const Rat_point_2 rp = static_cast<Rat_point_2>(q);
      out.push_back(interval_of(rp.x()));
      out.push_back(interval_of(rp.y()));
      return;
    }
    if (ensure_exact(q)) {
      // CORE refines on demand; 53 bits of relative precision is the double-tight default.
      out.push_back(interval_of(q.x(), 53));
      out.push_back(interval_of(q.y(), 53));
      return;
    }
    // Not exactifiable: the exact rational bbox is still a certified enclosure.
    Rational x0, y0, x1, y1;
    point_bbox(q, x0, y0, x1, y1);
    const std::pair<double, double> ix = interval_of(x0), ax = interval_of(x1);
    const std::pair<double, double> iy = interval_of(y0), ay = interval_of(y1);
    out.emplace_back(ix.first, ax.second);
    out.emplace_back(iy.first, ay.second);
  }

  bool point_is_rational(const Geom& p) const override {
    // Only what CGAL knows: p_rat_x/p_rat_y are set.  make_exact() NEVER makes an algebraic point
    // rational (traits_bezier.md §4.3), and CORE offers no safe rationality test for an Expr
    // (number_types_and_errors.md gotcha 4), so an algebraic coordinate that happens to be
    // rational is reported as non-rational.  Documented, deliberate, conservative.
    return point(p).is_rational();
  }

  void point_exact_rational(const Geom& p, std::vector<Rational>& out) const override {
    const Point_2& q = point(p);
    if (!q.is_rational())
      bad(ErrorCode::NotRepresentable,
          "the point has algebraic coordinates (CORE::Expr) and is not representable as a pair of "
          "exact rationals");
    const Rat_point_2 rp = static_cast<Rat_point_2>(q);
    out.clear();
    out.push_back(rp.x());
    out.push_back(rp.y());
  }

  void point_exact(const Geom& p, std::vector<Geom>& numbers) const override {
    const Point_2& q = point(p);
    numbers.clear();
    numbers.reserve(2);
    if (q.is_rational()) {
      const Rat_point_2 rp = static_cast<Rat_point_2>(q);
      numbers.push_back(box_rational(rp.x()));
      numbers.push_back(box_rational(rp.y()));
      return;
    }
    if (!ensure_exact(q))
      bad(ErrorCode::NotRepresentable,
          "the point has no exact representation (it carries no originator and only a bounding "
          "box is known)");
    numbers.push_back(box_core_expr(q.x()));   // \pre is_exact() — guaranteed by ensure_exact
    numbers.push_back(box_core_expr(q.y()));
  }

  std::string point_repr(const Geom& p) const override {
    const Point_2& q = point(p);
    double x = 0.0, y = 0.0;
    point_xy_double(q, x, y);
    const char* tilde = q.is_rational() ? "" : "~";
    return std::string("BezierPoint(") + tilde + fmt_double(x) + ", " + tilde + fmt_double(y) + ")";
  }

  Geom convert_point(const Geom& p) const override {
    require_type(p, GeomType::Point, "point");
    if (p.kind == Kind::Bezier) return p;
    Rational x, y;
    rational_xy_of_point(p, x, y);
    return box_point(Point_2(x, y));
  }

  // ------------------------------------------------------------------ curves

  Geom to_curve(const Geom& xc) const override {
    const Xcv_2& c = xcurve(xc);
    const Curve_2& B = c.supporting_curve();
    retain(B);
    // NOTE: an x-monotone Bezier piece is a *sub-arc* of its supporting curve; to_curve() returns
    // the WHOLE supporting curve, as ops.hpp specifies for this kind.
    return box_curve(B);
  }

  Geom xcurve_source(const Geom& xc) const override { return box_point(xcurve(xc).source()); }
  Geom xcurve_target(const Geom& xc) const override { return box_point(xcurve(xc).target()); }
  bool xcurve_has_source(const Geom& xc) const override { xcurve(xc); return true; }
  bool xcurve_has_target(const Geom& xc) const override { xcurve(xc); return true; }
  bool curve_is_bounded(const Geom& c) const override {
    require_any_curve(c, Kind::Bezier, "curve");
    // All four side categories of Arr_Bezier_curve_traits_2 are Arr_oblivious_side_tag: the
    // traits only supports the bounded planar topology, and a Bezier curve is a polynomial map of
    // the compact interval [0, 1].
    return true;
  }

  BBox curve_bbox(const Geom& c) const override {
    require_any_curve(c, Kind::Bezier, "curve");
    BBox b;
    b.dim = 2;
    if (c.holds<Curve_2>() && c.type == GeomType::Curve) {
      // Curve_2::bbox() is the double bbox of the control polygon, computed once at construction:
      // a superset of the curve by the convex-hull property.
      const CGAL::Bbox_2& bb = curve(c).bbox();
      b.lo[0] = bb.xmin();
      b.lo[1] = bb.ymin();
      b.hi[0] = bb.xmax();
      b.hi[1] = bb.ymax();
      return b;
    }
    // X-monotone piece: the control polygon RESTRICTED to the piece's parameter range (the same
    // de Casteljau clipping approximate() uses).  By the convex-hull property this is a superset
    // of the arc — strictly tighter than the whole curve's bbox and, unlike a sampled polyline's
    // bbox, guaranteed not to cut a bulge off.  The exact endpoint approximations are unioned in.
    const Xcv_2& xc = xcurve(c);
    std::vector<P2d> ctrl;
    control_points_double(xc.supporting_curve(), ctrl);
    double ts = 0.0, tt = 0.0;
    xcv_t_range(xc, ts, tt);
    const std::vector<P2d> sub = restrict_to(ctrl, std::min(ts, tt), std::max(ts, tt));
    double sx = 0.0, sy = 0.0, ex = 0.0, ey = 0.0;
    point_xy_double(xc.source(), sx, sy);
    point_xy_double(xc.target(), ex, ey);
    b.lo[0] = b.hi[0] = sx;
    b.lo[1] = b.hi[1] = sy;
    auto acc = [&b](double x, double y) {
      b.lo[0] = std::min(b.lo[0], x);
      b.hi[0] = std::max(b.hi[0], x);
      b.lo[1] = std::min(b.lo[1], y);
      b.hi[1] = std::max(b.hi[1], y);
    };
    acc(ex, ey);
    for (const P2d& p : sub) acc(p.x, p.y);
    return b;
  }

  /// Polyline approximation.  Arr_Bezier_curve_traits_2 has NO Approximate_2 of any kind
  /// (traits_bezier.md gotcha 8, rendering_and_approximation.md gotcha 11), and Curve_2::sample()
  /// is a uniform sampler with no error control, so this is implemented from scratch:
  ///   1. validate the tolerance BEFORE any subdivision (gotcha 7: error <= 0 means unbounded
  ///      recursion in CGAL's own subdividers; here it would be an infinite loop too);
  ///   2. clip the (double) control polygon to the piece's EXACT parameter range with de
  ///      Casteljau (twice), which needs the exact t values of §10.1 — hence the make_exact;
  ///   3. subdivide at the midpoint until every sub-arc's control polygon is flatter than the
  ///      tolerance (convex-hull property ⇒ every emitted chord is within `tolerance` of the
  ///      corresponding sub-arc);
  ///   4. overwrite the two extreme points with the endpoints' own approximations, so that the
  ///      polyline starts and ends exactly at approximate(source) / approximate(target) — the
  ///      "vertex-consistent" endpoint policy of rendering_and_approximation.md §3.1.
  /// Output order follows the curve from SOURCE to TARGET as stored.  `clip` is ignored: Bezier
  /// curves are bounded.
  void approximate(const Geom& c, double tolerance, const BBox* clip,
                   std::vector<double>& out) const override {
    require_any_curve(c, Kind::Bezier, "curve");
    (void)clip;
    const double tol = validated_tolerance(tolerance);

    std::vector<P2d> ctrl;
    double ts = 0.0, tt = 1.0;
    double sx = 0.0, sy = 0.0, ex = 0.0, ey = 0.0;
    if (c.holds<Curve_2>() && c.type == GeomType::Curve) {
      const Curve_2& B = curve(c);
      control_points_double(B, ctrl);
      ts = 0.0;
      tt = 1.0;
      // B(0) and B(1) are the first / last control points exactly.
      sx = ctrl.front().x;
      sy = ctrl.front().y;
      ex = ctrl.back().x;
      ey = ctrl.back().y;
    } else {
      const Xcv_2& xc = xcurve(c);
      control_points_double(xc.supporting_curve(), ctrl);
      xcv_t_range(xc, ts, tt);
      point_xy_double(xc.source(), sx, sy);
      point_xy_double(xc.target(), ex, ey);
    }

    const bool reversed = ts > tt;
    const double lo = std::min(ts, tt), hi = std::max(ts, tt);
    const std::vector<P2d> sub = restrict_to(ctrl, lo, hi);

    std::vector<P2d> pts;
    pts.push_back(sub.front());
    emit_flat(sub, tol, 0, pts);
    if (reversed) std::reverse(pts.begin(), pts.end());
    // Endpoint snapping (see step 4): CGAL's own sampling of an inexact endpoint can be off by
    // the bbox half-diagonal (rendering_and_approximation.md gotcha 12).
    pts.front() = P2d{sx, sy};
    pts.back() = P2d{ex, ey};

    out.clear();
    out.reserve(2 * pts.size());
    for (const P2d& p : pts) {
      out.push_back(p.x);
      out.push_back(p.y);
    }
  }

  std::string curve_repr(const Geom& c) const override {
    require_any_curve(c, Kind::Bezier, "curve");
    const bool is_x = !(c.holds<Curve_2>() && c.type == GeomType::Curve);
    const Curve_2& B = is_x ? xcurve(c).supporting_curve() : curve(c);
    std::string s = "BezierCurve([";
    bool first = true;
    for (auto it = B.control_points_begin(); it != B.control_points_end(); ++it) {
      if (!first) s += ", ";
      first = false;
      s += "(" + fmt_rational(it->x()) + ", " + fmt_rational(it->y()) + ")";
    }
    s += "]";
    if (is_x) {
      double ts = 0.0, tt = 0.0;
      xcv_t_range(xcurve(c), ts, tt);
      s += ", t=[" + fmt_double(std::min(ts, tt)) + ", " + fmt_double(std::max(ts, tt)) + "]";
    }
    s += ")";
    return s;
  }

  /// Exact conversions into a polynomial Bezier curve.  Supported sources:
  ///   * bezier                  — identity;
  ///   * segment                 — one degree-1 Bezier curve;
  ///   * linear (segments only)  — one degree-1 Bezier curve;
  ///   * polyline                — one degree-1 Bezier curve per sub-segment (several curves).
  /// Everything else is refused with Error(Unsupported) and a reason: a circular arc, a conic
  /// arc and a geodesic arc are not polynomial Bezier curves (a circle is a *rational* Bezier
  /// curve; CGAL's Bezier traits handles polynomial curves only — DESIGN.md).
  void convert_curve(const Geom& c, std::vector<Geom>& out) const override {
    if (c.empty()) bad(ErrorCode::InvalidArgument, "empty geometry");
    if (c.type != GeomType::Curve && c.type != GeomType::XCurve)
      bad(ErrorCode::InvalidArgument, "convert_curve() needs a curve or an x-monotone curve");
    out.clear();
    if (c.kind == Kind::Bezier) {
      // Identity, and the box TYPE is preserved on purpose: turning an x-monotone piece into a
      // general Curve_2 would hand back the whole supporting curve, i.e. different geometry.
      out.push_back(c);
      return;
    }
    if (c.holds<Seg_curve_2>()) {
      const Seg_curve_2& s = c.as<Seg_curve_2>();
      if (s.is_degenerate())
        bad(ErrorCode::InvalidArgument, "the source segment is degenerate (its endpoints coincide)");
      out.push_back(make_linear_curve(s.source(), s.target()));
      return;
    }
    if (c.holds<Lin_curve_2>()) {
      const Lin_curve_2& l = c.as<Lin_curve_2>();
      if (!l.is_segment())
        bad(ErrorCode::Unsupported,
            "only a bounded segment of the 'linear' kind converts to a Bezier curve; a ray or a "
            "line is unbounded and the Bezier traits is a bounded-planar traits");
      // \pre of source()/target(): !is_line() and !is_ray() — guaranteed by is_segment().
      out.push_back(make_linear_curve(l.source(), l.target()));
      return;
    }
    if (c.holds<Poly_xcurve_2>()) {   // must be tested before Poly_curve_2 (it derives from it)
      convert_polycurve(c.as<Poly_xcurve_2>(), out);
      return;
    }
    if (c.holds<Poly_curve_2>()) {
      convert_polycurve(c.as<Poly_curve_2>(), out);
      return;
    }
    bad(ErrorCode::Unsupported,
        std::string("cannot convert a curve of kind '") + kind_name(c.kind) +
            "' into a polynomial Bezier curve (only segment, linear segments and polyline "
            "convert exactly; circular / conic / geodesic arcs are not polynomial Bezier "
            "curves, and a kind whose TU is not linked cannot be inspected)");
  }

  // ------------------------------------------------------- traits workarounds

  // NOTE: CGAL's Bezier Split_2 does NOT verify that the split point lies on the curve — its only
  // precondition is `p.get_originator(_curve, _xid) != p.originators_end() || p.is_rational()`
  // (Bezier_x_monotone_2.h:1291), so ANY free rational point is accepted and produces a corrupt
  // pair of subcurves (exact_coordinates_contract.md gotcha 5, verified there with (1000,1000)).
  // The geodesic Split_2 has the same hole.  `KindOpsBase::split` therefore validates the point
  // for EVERY kind (in-x-range, on the curve, not an endpoint) before calling Split_2, which is
  // what this override used to do for Bezier alone — hence no override here any more.

  /// CGAL 6.1 guard: a curve that passes through ANOTHER curve's SELF-intersection point.
  ///
  /// That configuration trips TWO independent CGAL 6.1 defects, and only the first of them is
  /// repaired in this project:
  ///
  ///  (1) REPAIRED — `_Bezier_cache::get_intersections` pairs the resultant roots of the two
  ///      SUPPORTING curves assuming a bijection between them (`pts2.erase()` as it goes, and
  ///      `for (k = n_pts2 - 1; !found && k > 0; k--)` with an UNSIGNED `k`, Bezier_cache.h:458
  ///      /:488).  A self-intersection point is reached at two parameters, so the two root
  ///      lists differ in length: depending on which curve got the smaller `Curve_2::id()`
  ///      either `pts2` runs empty and `k` wraps to 4294967295 to index an EMPTY vector
  ///      (measured: SIGSEGV) or one of the two pairings is silently dropped (measured:
  ///      `CGAL_assertion(_is_in_range(t, cache))`, Bezier_x_monotone_2.h:933).
  ///      `impl/bezier_cache_fix.hpp` replaces that member with a correct many-to-many matching,
  ///      so NOTHING in this project can reach the out-of-bounds read any more — including this
  ///      guard itself, which has to call `compare_y_at_x(self-intersection point, other curve)`
  ///      and therefore goes through `_Bezier_cache::get_intersections` on the very pair it is
  ///      about to refuse whenever that point is not rational.
  ///
  ///  (2) NOT REPAIRED — the shared point is reached by three x-monotone branches (two of the
  ///      self-intersecting curve, one of the other), so `_Bezier_point_2` accumulates THREE
  ///      originators, and both `_Bezier_point_2_rep::_refine()`
  ///      (`CGAL_assertion(_origs.size() == 2)`, Bezier_point_2.h:1421, reached from
  ///      `fit_to_bbox()` inside `_clip_control_polygon`) and `make_exact()` (same assertion,
  ///      :1603) can only handle two.  Whether it is reached depends on whether CGAL's
  ///      approximate isolation happens to suffice — measured: it does for a quadratic or a
  ///      cubic through the crossing (the arrangement then comes out correct), it does not for a
  ///      vertical segment through it or for a line that also cuts the loop elsewhere.  Repairing
  ///      it means generalising CGAL's simultaneous two-curve refinement to k curves, which is
  ///      real geometry work inside third-party code, so the configuration stays refused.
  ///
  /// Detected exactly and refused here.  The same configuration is reached through CGAL's sweep
  /// (`Arrangement.insert` / `zone` / `do_intersect` / `overlay`), which never calls this method;
  /// `check_sweepable()` below is the pre-flight guard ArrImpl runs for those entry points.
  void intersect(const Geom& xc1, const Geom& xc2,
                 std::vector<IntersectionResult>& out) const override {
    const Xcv_2& a = xcurve(xc1);
    const Xcv_2& b = xcurve(xc2);
    const Curve_2& A = a.supporting_curve();
    const Curve_2& B = b.supporting_curve();
    if (A.id() != B.id()) {
      reject_meeting_at_self_intersection(A, B);
      reject_meeting_at_self_intersection(B, A);
    } else if (!self_intersection_points(A).empty()) {
      // Two pieces of the SAME supporting curve go through CGAL's own self-intersection branch,
      // which is sound — unless CGAL's has_no_self_intersections() flag wrongly short-circuits it.
      reject_curve_cgal_calls_simple(A);
    }
    Base::intersect(xc1, xc2, out);
  }

  // ---- sweep pre-flight (KindOps) ------------------------------------------------------
  //
  // CGAL's sweep-line and zone code call `Intersect_2` on pairs of x-monotone curves that our
  // `intersect()` above never sees, so the guard has to run BEFORE the sweep starts, on the
  // whole set of curves that will meet inside CGAL.  Measured with the LOOP cubic
  // (-1,0)(3,10)(-3,10)(1,0), which crosses itself at (0,3), and the linear Bezier
  // (-2,3)-(2,3) through that point: `insert`, `insert_curves`, `zone` and `do_intersect` all
  // SIGSEGV in ~5 runs out of 6, in either insertion order.
  bool needs_sweep_precheck() const override { return true; }

  /// O(n) cheap tests (`Curve_2::has_no_self_intersections()`, memoised per curve id) plus, only
  /// when some supporting curve really does cross itself, one point-on-curve test per
  /// (self-intersecting curve, other curve) pair.  The overwhelmingly common case — no curve in
  /// the whole set self-intersects — costs one hash lookup per distinct supporting curve.
  void check_sweepable(const std::vector<Geom>& curves) const override {
    std::vector<Curve_2> distinct;                 // supporting curves, deduplicated by id()
    std::unordered_set<std::size_t> seen;
    for (const Geom& g : curves) {
      if (g.type != GeomType::Curve && g.type != GeomType::XCurve) continue;
      const Curve_2& C = (g.type == GeomType::XCurve) ? xcurve(g).supporting_curve() : curve(g);
      if (seen.insert(C.id()).second) distinct.push_back(C);
    }
    std::vector<const Curve_2*> crossing;          // the ones that cross themselves (usually none)
    for (const Curve_2& C : distinct) {
      if (!self_intersection_points(C).empty()) {
        reject_curve_cgal_calls_simple(C);
        crossing.push_back(&C);
      } else if (self_intersections_undecidable(C)) {
        bad(ErrorCode::Unsupported,
            "this curve is a straight motion along a line that reverses on itself, so it "
            "overlaps its own image and no arrangement can represent it; CGAL 6.1 treats it as "
            "simple (its control polygon is 'convex') and then either asserts "
            "(org != p.originators_end(), Bezier_x_monotone_2.h:1008) or hangs forever");
      }
    }
    if (crossing.empty()) return;
    for (const Curve_2* A : crossing)
      for (const Curve_2& B : distinct)
        if (B.id() != A->id()) reject_meeting_at_self_intersection(*A, B);
  }

  /// `Curve_2::id()` is the address of the shared representation, and this TU retains every
  /// Curve_2 rep for the life of the process (see the LIFETIME contract at the top), so the id
  /// is a stable identity for as long as anything can compare it.  ArrImpl uses it to satisfy
  /// the split_edge / merge_edge / modify_edge precondition ("the supplied x-monotone curves are
  /// pieces of the edge curve they replace") in O(1): a piece of a supporting curve the
  /// arrangement already holds introduces no new PAIR, hence nothing for check_sweepable() to
  /// find.
  std::uint64_t supporting_curve_id(const Geom& xc) const override {
    return static_cast<std::uint64_t>(xcurve(xc).supporting_curve().id());
  }

 private:
  /// Throws Unsupported when CGAL 6.1's own `has_no_self_intersections()` flag disagrees with the
  /// exact answer.  `_Bezier_x_monotone_2::_exact_intersect` short-circuits on that flag for two
  /// x-monotone pieces of the SAME supporting curve (Bezier_x_monotone_2.h:2120-2125), so the
  /// sweep silently drops every crossing of such a curve — measured with the quartic
  /// (-4,-4)(-3,4)(-3,-6)(1,3)(-4,-2): CGAL builds V=3 E=2 F=1 where the two crossings should be
  /// vertices.  Refused rather than returned as a wrong arrangement; see
  /// certainly-no-self-intersections above for why the flag is wrong.
  void reject_curve_cgal_calls_simple(const Curve_2& C) const {
    if (!C.has_no_self_intersections()) return;
    bad(ErrorCode::Unsupported,
        "this curve crosses itself, but CGAL 6.1's Curve_2::has_no_self_intersections() reports "
        "it as simple (its angular-span test drops a control-polygon edge antipodal to "
        "front->back, Bezier_bounding_rational_traits.h:859) and its sweep short-circuits on "
        "that flag, so the arrangement would silently lose the self-intersection vertices");
  }

  /// Throws Unsupported when a self-intersection point of `A` lies anywhere on the supporting
  /// curve `B` (see intersect() above).  The whole supporting curve matters, not just the
  /// x-monotone sub-arc, because CGAL's pairing loop works on the supporting curves.
  void reject_meeting_at_self_intersection(const Curve_2& A, const Curve_2& B) const {
    const std::vector<Point_2>& pts = self_intersection_points(A);
    if (pts.empty()) return;
    std::vector<Make_x_monotone_result> pieces;
    auto mx = traits().make_x_monotone_2_object();
    mx(B, std::back_inserter(pieces));
    auto in_range = adaptor().is_in_x_range_2_object();
    auto cmp = adaptor().compare_y_at_x_2_object();
    for (const Point_2& p : pts) {
      for (const auto& item : pieces) {
        const Xcv_2* piece = std::get_if<Xcv_2>(&item);
        if (piece == nullptr) continue;
        if (!in_range(*piece, p)) continue;
        if (cmp(p, *piece) != CGAL::EQUAL) continue;
        bad(ErrorCode::Unsupported,
            "the two curves meet at a point where one of them crosses ITSELF; that point is "
            "reached by three x-monotone branches at once and CGAL 6.1 can refine or exactly "
            "evaluate a Bezier point with at most two (_Bezier_point_2_rep::_refine / "
            "make_exact, Bezier_point_2.h:1421/1603)");
      }
    }
  }

  double validated_tolerance(double tolerance) const {
    if (!(tolerance > 0.0) || !std::isfinite(tolerance))
      invalid("approximate: the tolerance must be a positive finite number");
    // rendering_and_approximation.md gotcha 7: a non-positive tolerance makes every CGAL
    // subdivider recurse for ever; ours would too.  Tiny values are clamped to the documented
    // floor so that the recursion terminates through the flatness test rather than the depth cap.
    return tolerance < 1e-12 ? 1e-12 : tolerance;
  }
};

// ===========================================================================================
// 3. namespace arr2d::bezier — the kind-specific free functions of ops.hpp
// ===========================================================================================

namespace bezier {

Geom make(const std::vector<Rational>& control_xy) {
  if (control_xy.size() % 2 != 0)
    bad(ErrorCode::InvalidArgument,
        "make(): the control coordinates are flattened x0,y0,x1,y1,... so their number must be even");
  if (control_xy.size() < 4)
    bad(ErrorCode::InvalidArgument, "make(): at least 2 control points (4 coordinates) are required");
  std::vector<Rat_point_2> pts;
  pts.reserve(control_xy.size() / 2);
  for (std::size_t i = 0; i + 1 < control_xy.size(); i += 2)
    pts.emplace_back(control_xy[i], control_xy[i + 1]);
  return make_curve(pts);
}

Geom make_from_points(const std::vector<Geom>& control_points) {
  if (control_points.size() < 2)
    bad(ErrorCode::InvalidArgument, "make_from_points(): at least 2 control points are required");
  std::vector<Rat_point_2> pts;
  pts.reserve(control_points.size());
  for (const Geom& g : control_points) {
    Rational x, y;
    rational_xy_of_point(g, x, y);   // any kind, rational coordinates required
    pts.emplace_back(x, y);
  }
  return make_curve(pts);
}

std::size_t number_of_control_points(const Geom& c) {
  require_any_curve(c, Kind::Bezier, "curve");
  const Curve_2& B = (c.holds<Curve_2>() && c.type == GeomType::Curve)
                         ? c.as<Curve_2>()
                         : c.as<Xcv_2>().supporting_curve();
  return static_cast<std::size_t>(B.number_of_control_points());
}

void control_point(const Geom& c, std::size_t i, Rational& x, Rational& y) {
  require_any_curve(c, Kind::Bezier, "curve");
  const Curve_2& B = (c.holds<Curve_2>() && c.type == GeomType::Curve)
                         ? c.as<Curve_2>()
                         : c.as<Xcv_2>().supporting_curve();
  const std::size_t n = static_cast<std::size_t>(B.number_of_control_points());
  if (i >= n)
    bad(ErrorCode::InvalidArgument,
        "control_point(): index " + std::to_string(i) + " is out of range (the curve has " +
            std::to_string(n) + " control points)");
  const Rat_point_2& p = B.control_point(static_cast<unsigned int>(i));
  x = p.x();
  y = p.y();
}

std::size_t curve_id(const Geom& c) {
  require_any_curve(c, Kind::Bezier, "curve");
  const Curve_2& B = (c.holds<Curve_2>() && c.type == GeomType::Curve)
                         ? c.as<Curve_2>()
                         : c.as<Xcv_2>().supporting_curve();
  // CGAL's id is reinterpret_cast<size_t>(rep pointer); it is stable for the whole process
  // because every curve we ever see is kept alive by the retention registry.
  return B.id();
}

Geom supporting_curve(const Geom& xc) {
  require_xcurve(xc, Kind::Bezier, "curve");
  const Curve_2& B = xc.as<Xcv_2>().supporting_curve();
  retain(B);
  return make_geom(Kind::Bezier, GeomType::Curve, B);
}

unsigned xid(const Geom& xc) {
  require_xcurve(xc, Kind::Bezier, "curve");
  return xc.as<Xcv_2>().xid();
}

/// Parameter interval [t_min, t_max] of an x-monotone piece, ORDERED (t_min <= t_max) — the
/// source-to-target order is available through the endpoints themselves.
///
/// CGAL's X_monotone_curve_2::parameter_range() is deliberately NOT used: it averages the
/// endpoints' bound intervals and was measured 14 % wrong on a real cubic before the endpoints
/// were exact (traits_bezier.md gotcha 5).  This makes both endpoints exact first and reads the
/// originators' exact algebraic parameters (traits_bezier.md §10.1).
void parameter_range(const Geom& xc, double& t_min, double& t_max) {
  require_xcurve(xc, Kind::Bezier, "curve");
  double ts = 0.0, tt = 0.0;
  xcv_t_range(xc.as<Xcv_2>(), ts, tt);
  t_min = std::min(ts, tt);
  t_max = std::max(ts, tt);
}

Geom point_at(const Geom& c, const Rational& t) {
  require_any_curve(c, Kind::Bezier, "curve");
  const bool is_x = !(c.holds<Curve_2>() && c.type == GeomType::Curve);
  const Curve_2& B = is_x ? c.as<Xcv_2>().supporting_curve() : c.as<Curve_2>();
  // CGAL precondition of both _Bezier_point_2(B, t0) and Curve_2::operator()(t): 0 <= t <= 1.
  if (t < Rational(0) || t > Rational(1))
    bad(ErrorCode::InvalidArgument,
        "point_at(): the parameter must lie in [0, 1] (got " + fmt_rational(t) + ")");
  retain(B);
  if (is_x) {
    // Tag the originator with the x-monotone subcurve id, which is what X_monotone_curve_2's
    // constructor and Split_2 look up (traits_bezier.md §10.4).
    return make_geom(Kind::Bezier, GeomType::Point, Point_2(B, c.as<Xcv_2>().xid(), t));
  }
  return make_geom(Kind::Bezier, GeomType::Point, Point_2(B, t));
}

void evaluate_approx(const Geom& c, double t, double& x, double& y) {
  require_any_curve(c, Kind::Bezier, "curve");
  const Curve_2& B = (c.holds<Curve_2>() && c.type == GeomType::Curve)
                         ? c.as<Curve_2>()
                         : c.as<Xcv_2>().supporting_curve();
  if (!std::isfinite(t) || t < 0.0 || t > 1.0)
    bad(ErrorCode::InvalidArgument, "evaluate_approx(): the parameter must lie in [0, 1]");
  std::vector<P2d> ctrl;
  control_points_double(B, ctrl);
  const P2d p = eval_double(ctrl, t);   // plain de Casteljau in doubles
  x = p.x;
  y = p.y;
}

void sample(const Geom& c, double t0, double t1, std::size_t n, std::vector<double>& out) {
  require_any_curve(c, Kind::Bezier, "curve");
  const Curve_2& B = (c.holds<Curve_2>() && c.type == GeomType::Curve)
                         ? c.as<Curve_2>()
                         : c.as<Xcv_2>().supporting_curve();
  if (n < 2)
    bad(ErrorCode::InvalidArgument, "sample(): at least 2 samples are required");
  if (!std::isfinite(t0) || !std::isfinite(t1) || t0 < 0.0 || t0 > 1.0 || t1 < 0.0 || t1 > 1.0)
    bad(ErrorCode::InvalidArgument, "sample(): both parameters must lie in [0, 1]");
  // Curve_2::sample() insists on std::pair<double,double> as the output value type; it emits
  // exactly max(n, 2) samples, both ends inclusive, and accepts t0 > t1 (negative step).
  std::vector<std::pair<double, double>> pts;
  B.sample(t0, t1, static_cast<unsigned int>(n), std::back_inserter(pts));
  out.clear();
  out.reserve(2 * pts.size());
  for (const auto& p : pts) {
    out.push_back(p.first);
    out.push_back(p.second);
  }
}

bool has_no_self_intersections(const Geom& c) {
  require_any_curve(c, Kind::Bezier, "curve");
  const Curve_2& B = (c.holds<Curve_2>() && c.type == GeomType::Curve)
                         ? c.as<Curve_2>()
                         : c.as<Xcv_2>().supporting_curve();
  // EXACT, not conservative: `Curve_2::has_no_self_intersections()` answers "no
  // self-intersections" for curves that do have them (see control_polygon_in_open_halfplane()
  // above for the measured counter-example and the reason), so this goes through our own sound
  // certificate and, when that is inconclusive, through the exact `id1 == id2` branch of the
  // Bezier cache.  The exact branch is memoised per supporting curve; it is a resultant plus
  // real-root isolation and costs microseconds for a cubic but tens of seconds for a
  // high-degree wiggly curve — the same computation the arrangement pre-flight has to do
  // anyway before such a curve can be inserted.
  return self_intersection_points(B).empty() && !self_intersections_undecidable(B);
}

void point_originators(const Geom& p, std::vector<std::pair<std::size_t, double>>& out) {
  require_point(p, Kind::Bezier);
  const Point_2& q = p.as<Point_2>();
  // Make the point exact first so that the originators carry their exact parameter (make_exact
  // writes it into the existing originator); otherwise only the bound midpoint is available.
  ensure_exact(q);
  out.clear();
  for (auto it = q.originators_begin(); it != q.originators_end(); ++it)
    out.emplace_back(static_cast<std::size_t>(it->curve().id()), originator_t(*it));
}

Geom point_parameter(const Geom& p, std::size_t curve_id) {
  require_point(p, Kind::Bezier);
  const Point_2& q = p.as<Point_2>();
  ensure_exact(q);
  for (auto it = q.originators_begin(); it != q.originators_end(); ++it) {
    if (static_cast<std::size_t>(it->curve().id()) != curve_id) continue;
    if (!it->has_parameter())
      throw_error(ErrorCode::Unsupported,
                  "bezier: the exact parameter of this point on curve id " +
                      std::to_string(curve_id) +
                      " is not available (the point could not be made exact; only its bounding "
                      "interval is known)");
    return box_core_expr(it->parameter());
  }
  throw_error(ErrorCode::Unsupported,
              "bezier: the point has no originator on the curve with id " +
                  std::to_string(curve_id) + " (it does not lie on that curve, or it was built "
                                             "without a reference to it)");
}

}  // namespace bezier

}  // namespace arr2d

// ===========================================================================================
// 4. The arrangement instantiation
// ===========================================================================================
//
// KindPolicy<BezierTypes> is already specialised in impl/arr_impl.hpp (naive / simple / walk /
// trapezoid + vertical ray shooting + the free CGAL::is_valid; NO landmarks — the traits has
// neither Approximate_2 nor Construct_x_monotone_curve_2 — and NO triangulation, which needs a
// Kernel and straight edges).  A kind TU adds nothing to it.
template class arr2d::ArrImpl<arr2d::BezierTypes>;

// ===========================================================================================
// 5. Registrar
// ===========================================================================================

namespace {
struct Registrar {
  Registrar() {
    // Leaked on purpose (docs/dev/STAGE1_NOTES.md): a KindOps singleton must never be destroyed
    // during static teardown — it owns a leaked Arr_traits_adaptor_2 that copies the Bezier
    // traits, and anything CORE-backed freed after CORE's MemoryPool is gone aborts the process.
    static arr2d::BezierOps* ops = new arr2d::BezierOps();
    arr2d::register_kind(
        arr2d::Kind::Bezier,
        arr2d::KindEntry{ops,
                         [] {
                           return std::unique_ptr<arr2d::ArrBase>(
                               new arr2d::ArrImpl<arr2d::BezierTypes>());
                         },
                         &arr2d::make_polygon_set_bezier});
  }
};
Registrar registrar;
}  // namespace
