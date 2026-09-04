// arr2d — definitions of the generic KindOpsBase<Types> methods declared in kind_ops_base.hpp.
//
// This file is included at the bottom of kind_ops_base.hpp; do not include it directly.
//
// Conventions used throughout:
//   * A functor is fetched at the point of use and stored in a NON-const local. Several CGAL
//     6.1 functors declare a non-const operator() (Bezier Construct_opposite_2, geodesic
//     Compare_endpoints_xy_2 / Construct_opposite_2, linear Trim_2), and every functor holds a
//     raw back-pointer into the traits, so caching them is never safe
//     (traits_adapters_and_misc.md gotcha 6, traits_bezier.md gotcha 12,
//      traits_geodesic_sphere.md gotcha 6, traits_segment_linear_polyline.md gotcha 10).
//   * Optional functors go through `if constexpr (has_xxx)`; the else branch throws
//     Error(Unsupported, "<kind>: <Functor> not available").
//   * Preconditions that are cheap to test are tested here and reported as Error(...).
//     The remaining ones are CGAL_precondition's of CGAL itself; this build keeps CGAL
//     preconditions enabled (DESIGN.md §4: -UNDEBUG), so they surface as
//     CGAL::Precondition_exception, which the Cython layer translates. Every such reliance is
//     spelled out in a comment at the call site.
#pragma once

#include <cmath>
#include <cstddef>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace arr2d {

// ---------------------------------------------------------------------------
// construction
// ---------------------------------------------------------------------------

template <class Types>
KindOpsBase<Types>::KindOpsBase()
    : m_traits(&Types::traits()),
      // Arr_traits_adaptor_2 derives from the traits and copy-constructs it. See the comment
      // on m_adaptor in kind_ops_base.hpp for the per-kind safety argument; in short: copy
      // CONSTRUCTION is safe for all seven kinds, copy ASSIGNMENT is not (polyline), which is
      // why the adaptor is held by pointer and never assigned. The allocation is never freed
      // on purpose (see the member's documentation: destroying a conic/Bezier traits during
      // static teardown aborts inside CORE's MemoryPool).
      m_adaptor(new Adaptor(Types::traits())) {}

// ---------------------------------------------------------------------------
// small helpers
// ---------------------------------------------------------------------------

template <class Types>
int KindOpsBase<Types>::param_space_int(CGAL::Arr_parameter_space ps) {
  // CGAL 6.1 spells these as `const Arr_parameter_space` objects (Arr_enums.h), not as
  // enumerators, so an if/else chain is used instead of a switch.
  if (ps == CGAL::ARR_LEFT_BOUNDARY) return ARR_LEFT_BOUNDARY;
  if (ps == CGAL::ARR_RIGHT_BOUNDARY) return ARR_RIGHT_BOUNDARY;
  if (ps == CGAL::ARR_BOTTOM_BOUNDARY) return ARR_BOTTOM_BOUNDARY;
  if (ps == CGAL::ARR_TOP_BOUNDARY) return ARR_TOP_BOUNDARY;
  return ARR_INTERIOR;
}

template <class Types>
CGAL::Arr_curve_end KindOpsBase<Types>::curve_end(int e) const {
  if (e == int(arr2d::ARR_MIN_END)) return CGAL::ARR_MIN_END;
  if (e == int(arr2d::ARR_MAX_END)) return CGAL::ARR_MAX_END;
  invalid("curve end must be 0 (min end) or 1 (max end)");
}

template <class Types>
const typename Types::X_monotone_curve_2& KindOpsBase<Types>::xcurve(const Geom& g) {
  require_any_curve(g, Types::kind, "curve");   // right kind, and a Curve or XCurve box
  // Accept both box types when Curve_2 and X_monotone_curve_2 are the same C++ type
  // (segment, linear): the box then really does hold an X_monotone_curve_2.
  if (g.template holds<X_monotone_curve_2>()) return g.template as<X_monotone_curve_2>();
  if (g.type == GeomType::Curve)
    throw_error(ErrorCode::NotXMonotone,
                std::string(kind_name(Types::kind)) +
                    ": a general curve was given where an x-monotone curve is required (use to_x_monotone())");
  // Wrong C++ type in the box / empty geometry: as<>() produces the precise diagnostic.
  return g.template as<X_monotone_curve_2>();
}

template <class Types>
const typename Types::Curve_2& KindOpsBase<Types>::curve(const Geom& g) const {
  require_any_curve(g, Types::kind, "curve");   // right kind, and a Curve or XCurve box
  // Same C++ type for both box kinds (segment, linear) -> either box works.
  if (g.template holds<Curve_2>()) return g.template as<Curve_2>();
  if (g.type == GeomType::XCurve)
    invalid("an x-monotone curve was given where a general curve is required (convert it with to_curve() first)");
  return g.template as<Curve_2>();
}

// ---------------------------------------------------------------------------
// points
// ---------------------------------------------------------------------------

template <class Types>
int KindOpsBase<Types>::point_compare_x(const Geom& p, const Geom& q) const {
  // CGAL precondition (sphere only): neither point lies on the boundary of the parameter
  // space (poles / identification curve) — traits_geodesic_sphere.md §4.5.
  auto cmp = traits().compare_x_2_object();
  return cmp_int(cmp(point(p), point(q)));
}

template <class Types>
int KindOpsBase<Types>::point_compare_xy(const Geom& p, const Geom& q) const {
  // Same sphere-only CGAL precondition as point_compare_x.
  auto cmp = traits().compare_xy_2_object();
  return cmp_int(cmp(point(p), point(q)));
}

template <class Types>
bool KindOpsBase<Types>::point_equal(const Geom& p, const Geom& q) const {
  // NB (Bezier): Equal_2/Compare_xy_2 mutate the point representations through const
  // (refinement + originator merging) — traits_bezier.md gotcha 9. Not thread-safe.
  auto eq = traits().equal_2_object();
  return eq(point(p), point(q));
}

// ---------------------------------------------------------------------------
// x-monotonicity
// ---------------------------------------------------------------------------

template <class Types>
void KindOpsBase<Types>::make_x_monotone(const Geom& c, std::vector<Geom>& out) const {
  require_any_curve(c, Types::kind, "curve");
  out.clear();   // ops.hpp contract: every output vector is cleared first
  // An x-monotone curve of a kind whose Curve_2 differs from X_monotone_curve_2 cannot be fed
  // to Make_x_monotone_2 at all; it is already the answer.
  if (c.type == GeomType::XCurve && !c.template holds<Curve_2>()) {
    out.push_back(box_xcurve(xcurve(c)));
    return;
  }
  std::vector<Make_x_monotone_result> res;
  auto mx = traits().make_x_monotone_2_object();
  mx(curve(c), std::back_inserter(res));
  out.reserve(res.size());
  for (const auto& item : res) {
    if (const Point_2* p = std::get_if<Point_2>(&item)) out.push_back(box_point(*p));
    else out.push_back(box_xcurve(*std::get_if<X_monotone_curve_2>(&item)));
  }
}

template <class Types>
bool KindOpsBase<Types>::is_x_monotone(const Geom& c) const {
  require_any_curve(c, Types::kind, "curve");
  if (c.type == GeomType::XCurve) return true;
  std::vector<Make_x_monotone_result> res;
  auto mx = traits().make_x_monotone_2_object();
  mx(curve(c), std::back_inserter(res));
  return res.size() == 1 && std::get_if<X_monotone_curve_2>(&res[0]) != nullptr;
}

template <class Types>
Geom KindOpsBase<Types>::to_x_monotone(const Geom& c) const {
  require_any_curve(c, Types::kind, "curve");
  if (c.type == GeomType::XCurve) return c;   // identity
  // A Curve box (including the kinds where Curve_2 == X_monotone_curve_2) is decided by the
  // traits, not by us: exactly one piece and no isolated point => x-monotone.
  std::vector<Make_x_monotone_result> res;
  auto mx = traits().make_x_monotone_2_object();
  mx(curve(c), std::back_inserter(res));
  if (res.size() == 1) {
    if (const X_monotone_curve_2* xc = std::get_if<X_monotone_curve_2>(&res[0])) return box_xcurve(*xc);
    throw_error(ErrorCode::NotXMonotone,
                std::string(kind_name(Types::kind)) + ": the curve is a single isolated point, not an x-monotone curve");
  }
  throw_error(ErrorCode::NotXMonotone,
              std::string(kind_name(Types::kind)) + ": the curve is not x-monotone (it splits into " +
                  std::to_string(res.size()) + " pieces)");
}

// ---------------------------------------------------------------------------
// x-monotone curve accessors
// ---------------------------------------------------------------------------

template <class Types>
Geom KindOpsBase<Types>::xcurve_min_vertex(const Geom& xc) const {
  const X_monotone_curve_2& c = xcurve(xc);
  // Construct_min_vertex_2 has a CGAL precondition that the minimal end is bounded (it fires
  // for a Linear ray/line whose left end runs to infinity). Is_closed_2 is the adaptor's
  // boundary-aware test: ARR_INTERIOR or a non-open side => there is a real vertex.
  auto closed = adaptor().is_closed_2_object();
  if (!closed(c, CGAL::ARR_MIN_END))
    throw_error(ErrorCode::Unsupported, std::string(kind_name(Types::kind)) +
                                            ": the curve's minimal end lies at infinity, it has no vertex there");
  auto minv = traits().construct_min_vertex_2_object();
  return box_point(minv(c));
}

template <class Types>
Geom KindOpsBase<Types>::xcurve_max_vertex(const Geom& xc) const {
  const X_monotone_curve_2& c = xcurve(xc);
  auto closed = adaptor().is_closed_2_object();
  if (!closed(c, CGAL::ARR_MAX_END))
    throw_error(ErrorCode::Unsupported, std::string(kind_name(Types::kind)) +
                                            ": the curve's maximal end lies at infinity, it has no vertex there");
  auto maxv = traits().construct_max_vertex_2_object();
  return box_point(maxv(c));
}

template <class Types>
bool KindOpsBase<Types>::xcurve_is_vertical(const Geom& xc) const {
  // CGAL precondition (sphere): the arc is not degenerate.
  auto isv = traits().is_vertical_2_object();
  return isv(xcurve(xc));
}

template <class Types>
int KindOpsBase<Types>::compare_endpoints_xy(const Geom& xc) const {
  if constexpr (has_compare_endpoints) {
    // Non-const operator() in the geodesic traits (traits_geodesic_sphere.md gotcha 6) ->
    // the functor must live in a non-const local.
    auto cmp = traits().compare_endpoints_xy_2_object();
    return cmp_int(cmp(xcurve(xc)));
  } else {
    unsupported("Compare_endpoints_xy_2");
  }
}

template <class Types>
bool KindOpsBase<Types>::xcurve_is_directed_right(const Geom& xc) const {
  return compare_endpoints_xy(xc) < 0;   // SMALLER == directed right
}

template <class Types>
Geom KindOpsBase<Types>::construct_opposite(const Geom& xc) const {
  if constexpr (has_construct_opposite) {
    // Non-const operator() in the Bezier and geodesic traits (traits_bezier.md gotcha 12,
    // traits_geodesic_sphere.md gotcha 6) -> non-const local copy of the functor.
    auto opp = traits().construct_opposite_2_object();
    return box_xcurve(opp(xcurve(xc)));
  } else {
    // Linear lands here: Arr_linear_traits_2::Construct_opposite_2::operator() does not
    // compile (traits_segment_linear_polyline.md gotcha 3); the Linear kind TU overrides
    // this method.
    unsupported("Construct_opposite_2");
  }
}

template <class Types>
bool KindOpsBase<Types>::curve_equal(const Geom& a, const Geom& b) const {
  auto eq = traits().equal_2_object();
  return eq(xcurve(a), xcurve(b));
}

// ---------------------------------------------------------------------------
// traits predicates
// ---------------------------------------------------------------------------

template <class Types>
int KindOpsBase<Types>::compare_y_at_x(const Geom& p, const Geom& xc) const {
  const Point_2& pt = point(p);
  const X_monotone_curve_2& c = xcurve(xc);
  // CGAL precondition of Compare_y_at_x_2: p is in the x-range of the curve. Checked here
  // (the adaptor synthesizes Is_in_x_range_2 for every traits) so that misuse becomes
  // Error(InvalidArgument) instead of an assertion failure.
  auto in_range = adaptor().is_in_x_range_2_object();
  if (!in_range(c, pt)) invalid("compare_y_at_x: the point is not in the x-range of the curve");
  // Further CGAL precondition (sphere only): p is not a contraction point (a pole).
  auto cmp = adaptor().compare_y_at_x_2_object();
  return cmp_int(cmp(pt, c));
}

template <class Types>
int KindOpsBase<Types>::compare_y_at_x_left(const Geom& xc1, const Geom& xc2, const Geom& p) const {
  // CGAL precondition: the two curves intersect at p and both are defined to its left.
  // (For traits with Has_left_category == Tag_false the adaptor emulates the predicate; all
  // seven of our kinds declare Tag_true.)
  auto cmp = adaptor().compare_y_at_x_left_2_object();
  return cmp_int(cmp(xcurve(xc1), xcurve(xc2), point(p)));
}

template <class Types>
int KindOpsBase<Types>::compare_y_at_x_right(const Geom& xc1, const Geom& xc2, const Geom& p) const {
  // CGAL precondition: the two curves intersect at p and both are defined to its right.
  auto cmp = adaptor().compare_y_at_x_right_2_object();
  return cmp_int(cmp(xcurve(xc1), xcurve(xc2), point(p)));
}

template <class Types>
bool KindOpsBase<Types>::is_in_x_range(const Geom& xc, const Geom& p) const {
  auto in_range = adaptor().is_in_x_range_2_object();   // synthesized by the adaptor for every traits
  return in_range(xcurve(xc), point(p));
}

// ---------------------------------------------------------------------------
// traits constructions
// ---------------------------------------------------------------------------

template <class Types>
void KindOpsBase<Types>::split(const Geom& xc, const Geom& p, Geom& left, Geom& right) const {
  if constexpr (has_split) {
    // CGAL precondition: p lies on the curve and is not one of its endpoints.  Six of the seven
    // traits check it, but the GEODESIC one does NOT: Arr_geodesic_arc_on_sphere_traits_2::
    // Split_2 (Arr_geodesic_arc_on_sphere_traits_2.h:2258-2266) only rejects a degenerate arc and
    // the two endpoints, so any point whatsoever is accepted and two arcs that do not lie on the
    // stored great circle come back (Arrangement.split_edge then corrupts the arrangement:
    // is_valid() turns False and the next insert dies in Multiset.h:2170).  Check it here for
    // every kind — one Compare_y_at_x_2 is exactly what the other traits' own preconditions cost.
    // (BezierOps::split overrides this method with the same test plus its own diagnostics.)
    bool in_x = false;
    try {
      auto in_range = adaptor().is_in_x_range_2_object();
      in_x = in_range(xcurve(xc), point(p));
    } catch (...) {
      // The predicates have preconditions of their own (the sphere's Compare_x_2 needs
      // `is_no_boundary()`, i.e. no pole and no identification point); a point that cannot even
      // be tested is certainly not a legal split point.
      in_x = false;
    }
    if (!in_x) invalid("split: the point is not in the x-range of the curve");
    bool on_curve = false;
    try {
      auto cmp = adaptor().compare_y_at_x_2_object();
      on_curve = (cmp(point(p), xcurve(xc)) == CGAL::EQUAL);
    } catch (...) {
      on_curve = false;
    }
    if (!on_curve) invalid("split: the point does not lie on the curve");
    // CGAL's documented precondition: p is in the INTERIOR of the curve.  An end that lies at
    // infinity (a Linear ray / line) has no vertex to compare with, hence the Is_closed_2 gate.
    {
      auto closed = adaptor().is_closed_2_object();
      auto eq = traits().equal_2_object();
      auto minv = traits().construct_min_vertex_2_object();
      auto maxv = traits().construct_max_vertex_2_object();
      const X_monotone_curve_2& c = xcurve(xc);
      const Point_2& pt = point(p);
      if ((closed(c, CGAL::ARR_MIN_END) && eq(pt, minv(c))) ||
          (closed(c, CGAL::ARR_MAX_END) && eq(pt, maxv(c))))
        invalid("split: the point is an endpoint of the curve, not an interior point");
    }
    X_monotone_curve_2 c1, c2;
    auto sp = traits().split_2_object();
    sp(xcurve(xc), point(p), c1, c2);
    // Split_2 always returns c1 = the part left of p and c2 = the part right of p, keeping the
    // original direction flag on both halves.
    left = box_xcurve(c1);
    right = box_xcurve(c2);
  } else {
    unsupported("Split_2");
  }
}

template <class Types>
void KindOpsBase<Types>::intersect(const Geom& xc1, const Geom& xc2, std::vector<IntersectionResult>& out) const {
  std::vector<Intersection_result> res;
  auto isect = traits().intersect_2_object();
  isect(xcurve(xc1), xcurve(xc2), std::back_inserter(res));
  out.clear();   // ops.hpp contract: every output vector is cleared first
  out.reserve(res.size());
  for (const auto& item : res) {
    IntersectionResult r;
    if (const Intersection_point* ip = std::get_if<Intersection_point>(&item)) {
      r.is_point = true;
      r.point = box_point(ip->first);
      // Multiplicity is unsigned int for most traits and std::size_t for conic/geodesic; the
      // type-erased field is unsigned. Bezier reports only 0 ("unknown") or 1
      // (traits_bezier.md gotcha 11).
      r.multiplicity = static_cast<unsigned>(ip->second);
    } else {
      r.is_point = false;
      r.overlap = box_xcurve(*std::get_if<X_monotone_curve_2>(&item));
    }
    out.push_back(std::move(r));
  }
}

template <class Types>
bool KindOpsBase<Types>::are_mergeable(const Geom& xc1, const Geom& xc2) const {
  if constexpr (has_are_mergeable) {
    // CGAL precondition (linear): neither curve is degenerate.
    auto mergeable = traits().are_mergeable_2_object();
    return mergeable(xcurve(xc1), xcurve(xc2));
  } else {
    unsupported("Are_mergeable_2");
  }
}

template <class Types>
Geom KindOpsBase<Types>::merge(const Geom& xc1, const Geom& xc2) const {
  if constexpr (has_merge) {
    const X_monotone_curve_2& a = xcurve(xc1);
    const X_monotone_curve_2& b = xcurve(xc2);
    if constexpr (has_are_mergeable) {
      // Merge_2's precondition is "the curves are mergeable"; test it so that misuse is a
      // clean Error instead of a CGAL assertion (and so that traits whose Merge_2 does not
      // check the precondition cannot produce garbage).
      auto mergeable = traits().are_mergeable_2_object();
      if (!mergeable(a, b)) invalid("merge: the two x-monotone curves are not mergeable");
    }
    X_monotone_curve_2 res;
    // Bezier's Merge_2 has a private constructor: it must come from merge_2_object()
    // (traits_bezier.md gotcha 12). Conic's Conic_x_monotone_arc_2::merge() member does not
    // compile (traits_conic.md gotcha 6) — the functor is the only route there too.
    auto mrg = traits().merge_2_object();
    mrg(a, b, res);
    return box_xcurve(res);
  } else {
    unsupported("Merge_2");
  }
}

template <class Types>
Geom KindOpsBase<Types>::trim(const Geom& xc, const Geom& src, const Geom& tgt) const {
  if constexpr (has_trim) {
    const X_monotone_curve_2& c = xcurve(xc);
    const Point_2& s = point(src);
    const Point_2& t = point(tgt);
    auto eq = traits().equal_2_object();
    if (eq(s, t)) invalid("trim: the two endpoints must be distinct");
    // Remaining CGAL precondition: both points lie on the curve (compare_y_at_x == EQUAL).
    // Bezier's Trim_2 has a private constructor -> obtain it from trim_2_object()
    // (traits_bezier.md gotcha 12). Arr_linear_traits_2::Trim_2::operator() is non-const and
    // takes its arguments by value (traits_segment_linear_polyline.md gotcha 10), hence the
    // non-const local; trimming a linear ray/line always yields a bounded segment.
    auto trm = traits().trim_2_object();
    return box_xcurve(trm(c, s, t));
  } else {
    // The geodesic traits has no Trim_2 at all.
    unsupported("Trim_2");
  }
}

template <class Types>
int KindOpsBase<Types>::parameter_space_in_x(const Geom& xc, int end) const {
  // The adaptor's Parameter_space_in_x_2 is tag-dispatched: for traits whose left/right sides
  // are oblivious it returns ARR_INTERIOR without ever calling the traits. That is what makes
  // this safe for conic, whose own Parameter_space_in_x_2(xcv, ce) is a
  // CGAL_error_msg("Not implemented yet!") stub (traits_conic.md gotcha 14).
  auto ps = adaptor().parameter_space_in_x_2_object();
  return param_space_int(ps(xcurve(xc), curve_end(end)));
}

template <class Types>
int KindOpsBase<Types>::parameter_space_in_y(const Geom& xc, int end) const {
  auto ps = adaptor().parameter_space_in_y_2_object();
  return param_space_int(ps(xcurve(xc), curve_end(end)));
}

template <class Types>
Geom KindOpsBase<Types>::construct_x_monotone_curve(const Geom& p, const Geom& q) const {
  if constexpr (has_construct_xcurve) {
    const Point_2& a = point(p);
    const Point_2& b = point(q);
    auto eq = traits().equal_2_object();
    if (eq(a, b)) invalid("construct_x_monotone_curve: the two points must be distinct");
    // segment / linear / polyline (2-point polyline) / sphere (minor great-circle arc) /
    // conic (the "special segment" overload, traits_conic.md §3). Further CGAL preconditions:
    // sphere -> the two directions must not be antipodal.
    auto ctr = traits().construct_x_monotone_curve_2_object();
    return box_xcurve(ctr(a, b));
  } else {
    // circle-segment and Bezier have neither Construct_x_monotone_curve_2 nor Construct_curve_2
    // (traits_circle_segment.md gotcha 3, traits_bezier.md gotcha 8).
    unsupported("Construct_x_monotone_curve_2");
  }
}

// ---------------------------------------------------------------------------
// approximation
// ---------------------------------------------------------------------------

template <class Types>
double KindOpsBase<Types>::approximate_coordinate(const Geom& p, int i) const {
  if (i < 0 || i >= dimension()) invalid("coordinate index out of range");
  // ops.hpp contract: approximate_coordinate agrees with point_approx.  CGAL's own
  // Approximate_2 is deliberately NOT used: for every Epeck-based traits it is
  // `CGAL::to_double(Lazy_exact_nt)`, which is not correctly rounded (measured: 1/3 ->
  // 0.33333333333333337 instead of 0.33333333333333331; number_types_and_errors.md gotcha 2,
  // CGAL_TRAPS_CHECKLIST "Numbers / coordinates"), and for the sphere it returns the raw
  // unnormalised component.  point_approx() is the kind's correctly-rounded conversion.
  double xyz[3] = {0.0, 0.0, 0.0};
  point_approx(p, xyz);
  return xyz[i];
}

template <class Types>
double KindOpsBase<Types>::approximate_length(const Geom& c, double tolerance) const {
  // Sum of the chord lengths of the kind's own polyline approximation. `clip` is null, so
  // unbounded curves are rejected by the kind's approximate() implementation.
  std::vector<double> pts;
  approximate(c, tolerance, nullptr, pts);
  const std::size_t d = static_cast<std::size_t>(dimension());
  if (d == 0 || pts.size() < 2 * d) return 0.0;
  double len = 0.0;
  for (std::size_t i = d; i + d <= pts.size(); i += d) {
    double sq = 0.0;
    for (std::size_t k = 0; k < d; ++k) {
      const double delta = pts[i + k] - pts[i - d + k];
      sq += delta * delta;
    }
    len += std::sqrt(sq);
  }
  return len;
}

}  // namespace arr2d
