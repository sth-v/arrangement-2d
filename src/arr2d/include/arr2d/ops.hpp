// arr2d — per-kind geometry operations (type-erased) + kind-specific free functions.
//
// KindOps: one implementation per Kind (kind_<name>.cpp), obtained via registry.hpp ops(kind).
//   * generic point/curve accessors used by the Python geometry classes and by ArrBase,
//   * the complete ArrangementTraits_2 functor set ("traits ops"), so Python can use
//     the traits directly (compare_xy, intersect, split, merge, make_x_monotone, ...).
// Kind-specific constructors/accessors are plain functions in nested namespaces
// (arr2d::segment, arr2d::linear, ...). All of them take/return Geom boxes.
//
// Conventions
//   * Every output vector parameter (`out`, `numbers`, `left/right` excepted) is CLEARED first, then filled
//     (this includes make_x_monotone and intersect).
//   * Constructors and convert_curve return XCurve boxes whenever the result is x-monotone by
//     construction (segments, linear objects, polyline sub-pieces, ...); general curves are Curve boxes.
//   * Kind-specific accessors that return sub-objects of another kind (polyline::point / ::subcurve
//     return Kind::Segment boxes) must be converted with convert_point / convert_curve before being
//     passed to another kind's KindOps.
//   * approximate_coordinate agrees with point_approx (both correctly rounded) — it does NOT expose
//     CGAL's raw Approximate_2, which is not correctly rounded for Epeck-based traits.
//   * Comparison results are ints: -1 (SMALLER/CLOCKWISE/NEGATIVE), 0 (EQUAL), +1 (LARGER/CCW/POSITIVE).
//   * Points passed to a kind must have that kind (use convert_point first); Error(KindMismatch) otherwise.
//   * Functions that need an x-monotone curve throw Error(NotXMonotone) if given a general Curve.
//   * Optional traits functors that a kind lacks throw Error(Unsupported).
//   * "Planar" coordinates are (x, y); the sphere kind uses (x, y, z) directions.
#pragma once

#include <array>
#include <string>
#include <utility>
#include <vector>

#include "arr2d/common.hpp"
#include "arr2d/numbers.hpp"

namespace arr2d {

/// One intersection record produced by KindOps::intersect.
struct IntersectionResult {
  bool is_point = true;      ///< true: `point` + `multiplicity`; false: `overlap` (an x-monotone curve)
  Geom point;
  std::size_t multiplicity = 0; ///< as reported by the traits (0 = unknown / not computed; Bezier reports only 0/1)
  Geom overlap;
};

/// Axis-aligned bounding box in doubles (planar: xmin,ymin,xmax,ymax; sphere: xmin,ymin,zmin,xmax,ymax,zmax as 6 values).
struct BBox {
  double lo[3] = {0, 0, 0};
  double hi[3] = {0, 0, 0};
  int dim = 2;
};

class KindOps {
 public:
  virtual ~KindOps() = default;
  virtual Kind kind() const = 0;
  virtual const char* name() const = 0;
  virtual int dimension() const = 0;                 ///< 2 (planar) or 3 (sphere: points are directions)
  virtual bool is_unbounded_kind() const = 0;        ///< true only for Linear (unbounded planar topology)
  virtual bool has_polygon_set() const = 0;          ///< Boolean set operations available for this kind

  // ---------------------------------------------------------------- points
  /// Planar kinds: (x, y). Sphere: use make_point_3.
  virtual Geom make_point(const Rational& x, const Rational& y) const = 0;
  virtual Geom make_point_3(const Rational& x, const Rational& y, const Rational& z) const = 0;  ///< sphere only (Unsupported otherwise)
  /// Approximate coordinates (dimension() values written to `xyz`).
  virtual void point_approx(const Geom& p, double* xyz) const = 0;
  /// Certified intervals per coordinate.
  virtual void point_interval(const Geom& p, std::vector<std::pair<double, double>>& out) const = 0;
  /// True if every coordinate is rational (Epeck points: always; circle: sqrt-free; Bezier/conic: only if provably rational).
  virtual bool point_is_rational(const Geom& p) const = 0;
  /// Exact rational coordinates; throws NotRepresentable if !point_is_rational.
  virtual void point_exact_rational(const Geom& p, std::vector<Rational>& out) const = 0;
  /// Exact coordinates as boxed numbers (Rational / SqrtExt / Algebraic).
  virtual void point_exact(const Geom& p, std::vector<Geom>& numbers) const = 0;
  virtual int point_compare_x(const Geom& p, const Geom& q) const = 0;   ///< traits Compare_x_2
  virtual int point_compare_xy(const Geom& p, const Geom& q) const = 0;  ///< traits Compare_xy_2 (sphere: lexicographic in the parameter space)
  virtual bool point_equal(const Geom& p, const Geom& q) const = 0;      ///< traits Equal_2
  virtual std::string point_repr(const Geom& p) const = 0;
  /// Convert a point of ANY kind into this kind (through exact rationals). Throws NotRepresentable
  /// when the source coordinates are not rational (and no exact path exists), KindMismatch for dimension mismatch.
  virtual Geom convert_point(const Geom& p) const = 0;

  // ---------------------------------------------------------------- curves (general and x-monotone)
  virtual bool is_x_monotone(const Geom& c) const = 0;          ///< XCurve -> true; Curve -> test via Make_x_monotone_2 (exactly one x-monotone piece and no isolated points)
  /// Traits Make_x_monotone_2: subdivides a Curve into x-monotone curves and isolated points, in order.
  virtual void make_x_monotone(const Geom& c, std::vector<Geom>& out) const = 0;
  /// Promote a Curve that is x-monotone into an XCurve box (throws NotXMonotone otherwise); identity for XCurve.
  virtual Geom to_x_monotone(const Geom& c) const = 0;
  /// The general Curve_2 corresponding to an x-monotone curve (identity for kinds where the types coincide;
  /// Bezier: supporting curve; conic/circle: the arc itself as Curve_2). Unsupported if no sensible mapping exists.
  virtual Geom to_curve(const Geom& xc) const = 0;

  virtual Geom xcurve_source(const Geom& xc) const = 0;     ///< as stored (may be the lexicographically larger end); Unsupported for lines/rays without that end
  virtual Geom xcurve_target(const Geom& xc) const = 0;
  virtual bool xcurve_has_source(const Geom& xc) const = 0; ///< false for a Linear ray/line end at infinity
  virtual bool xcurve_has_target(const Geom& xc) const = 0;
  virtual Geom xcurve_min_vertex(const Geom& xc) const = 0; ///< traits Construct_min_vertex_2 (lexicographically smaller endpoint)
  virtual Geom xcurve_max_vertex(const Geom& xc) const = 0;
  virtual bool xcurve_is_vertical(const Geom& xc) const = 0;
  virtual bool xcurve_is_directed_right(const Geom& xc) const = 0;   ///< source lexicographically smaller than target (traits Compare_endpoints_xy_2 == SMALLER)
  virtual int compare_endpoints_xy(const Geom& xc) const = 0;        ///< -1 if directed right (source < target), +1 otherwise
  virtual Geom construct_opposite(const Geom& xc) const = 0;         ///< same curve, reversed direction
  virtual BBox curve_bbox(const Geom& c) const = 0;                  ///< approximate bbox (Curve or XCurve); for unbounded curves: +-inf
  virtual bool curve_is_bounded(const Geom& c) const = 0;

  /// Polyline approximation of an x-monotone curve (or a full closed curve for
  /// CircleSegment/Conic full circles/ellipses; sphere full circles) with a tolerance in
  /// coordinate units. `clip` is a bbox used to clip unbounded curves (lines/rays); it
  /// must be provided for unbounded curves. Output is flattened (dimension() values per
  /// point) and follows the curve from source to target as stored.
  virtual void approximate(const Geom& c, double tolerance, const BBox* clip, std::vector<double>& out) const = 0;
  /// Approximate length of a bounded curve (sum of chord lengths of approximate(tolerance)).
  virtual double approximate_length(const Geom& c, double tolerance) const = 0;

  virtual std::string curve_repr(const Geom& c) const = 0;
  virtual bool curve_equal(const Geom& a, const Geom& b) const = 0;   ///< traits Equal_2 on x-monotone curves (same support and endpoints, direction-insensitive as CGAL defines it)
  /// Convert a curve of ANY kind into this kind when an exact conversion exists (see DESIGN.md
  /// conversion matrix); may produce several curves (e.g. a polyline into segments). Throws
  /// NotRepresentable / Unsupported otherwise.
  virtual void convert_curve(const Geom& c, std::vector<Geom>& out) const = 0;

  // ---------------------------------------------------------------- traits functors
  virtual int compare_y_at_x(const Geom& p, const Geom& xc) const = 0;                       ///< -1 below, 0 on, +1 above (precondition: p in x-range)
  virtual int compare_y_at_x_left(const Geom& xc1, const Geom& xc2, const Geom& p) const = 0; ///< order of the curves immediately left of their common point p
  virtual int compare_y_at_x_right(const Geom& xc1, const Geom& xc2, const Geom& p) const = 0;
  virtual bool is_in_x_range(const Geom& xc, const Geom& p) const = 0;
  virtual void split(const Geom& xc, const Geom& p, Geom& left, Geom& right) const = 0;        ///< p in interior of xc
  virtual void intersect(const Geom& xc1, const Geom& xc2, std::vector<IntersectionResult>& out) const = 0;
  virtual bool are_mergeable(const Geom& xc1, const Geom& xc2) const = 0;
  virtual Geom merge(const Geom& xc1, const Geom& xc2) const = 0;
  virtual Geom trim(const Geom& xc, const Geom& src, const Geom& tgt) const = 0;              ///< Trim_2 (Unsupported if missing)
  virtual int parameter_space_in_x(const Geom& xc, int curve_end) const = 0;                 ///< Arr_parameter_space (ARR_INTERIOR for bounded kinds)
  virtual int parameter_space_in_y(const Geom& xc, int curve_end) const = 0;
  /// Construct_x_monotone_curve_2(p, q): a straight x-monotone curve between two points, for
  /// kinds that support it (segment, linear, polyline, conic, sphere); Unsupported otherwise.
  virtual Geom construct_x_monotone_curve(const Geom& p, const Geom& q) const = 0;
  /// Approximate_2 on a point coordinate i (0/1/2).
  virtual double approximate_coordinate(const Geom& p, int i) const = 0;
};

// ===========================================================================
// Kind-specific constructors / accessors (implemented in the kind TUs).
// Points given to planar constructors may be of ANY kind with rational coordinates
// unless stated otherwise; they are converted. Returned points/curves have the kind of the namespace.
// ===========================================================================

namespace segment {          // Kind::Segment — Arr_segment_2<Epeck> (Curve_2 == X_monotone_curve_2)
Geom make(const Geom& p, const Geom& q);                       ///< p != q (Precondition error otherwise)
Geom make_xy(const Rational& x1, const Rational& y1, const Rational& x2, const Rational& y2);
void endpoints(const Geom& s, Geom& source, Geom& target);
void supporting_line(const Geom& s, Rational& a, Rational& b, Rational& c);   ///< a x + b y + c = 0
}  // namespace segment

namespace linear {           // Kind::Linear — Arr_linear_object_2<Epeck>
enum Which : int { SEGMENT = 0, RAY = 1, LINE = 2 };
Geom make_segment(const Geom& p, const Geom& q);
Geom make_ray(const Geom& source, const Geom& towards);        ///< ray from `source` through `towards`
Geom make_ray_direction(const Geom& source, const Rational& dx, const Rational& dy);
Geom make_line(const Geom& p, const Geom& q);
Geom make_line_coefficients(const Rational& a, const Rational& b, const Rational& c);   ///< a x + b y + c = 0
int which(const Geom& c);
void supporting_line(const Geom& c, Rational& a, Rational& b, Rational& c_);
void direction(const Geom& c, Rational& dx, Rational& dy);     ///< for rays/lines: the direction as stored; for segments: target - source
}  // namespace linear

namespace circle_segment {   // Kind::CircleSegment — _Circle_segment_2<Epeck> / _X_monotone_circle_segment_2
/// A point with sqrt-extension coordinates x = ax + bx*sqrt(cx), y = ay + by*sqrt(cy).
Geom make_point_sqrt(const SqrtExt& x, const SqrtExt& y);
void point_sqrt(const Geom& p, SqrtExt& x, SqrtExt& y);
Geom make_full_circle(const Rational& cx, const Rational& cy, const Rational& squared_radius, int orientation /* +1 ccw, -1 cw */);
/// Same with an explicit rational RADIUS (preferred when the radius is rational: CGAL then keeps the vertical tangency points rational).
Geom make_full_circle_r(const Rational& cx, const Rational& cy, const Rational& radius, int orientation);
/// Arc of the circle (cx,cy,r^2) from source to target in the given orientation. Endpoints must lie on the circle (CircleSegment-kind points, may have sqrt coordinates).
Geom make_arc(const Rational& cx, const Rational& cy, const Rational& squared_radius, int orientation, const Geom& source, const Geom& target);
Geom make_arc_r(const Rational& cx, const Rational& cy, const Rational& radius, int orientation, const Geom& source, const Geom& target);
bool has_rational_radius(const Geom& c);                       ///< always false in CGAL 6.1: _Circle_segment_2 keeps its radius flag protected (no accessor)
Rational radius(const Geom& c);                                ///< Unsupported in CGAL 6.1 (see has_rational_radius); use squared_radius
/// Arc through three rational points (source, mid, target).
Geom make_arc_three_points(const Geom& p, const Geom& q, const Geom& r);
/// Line segment (as a circle-segment curve).
Geom make_segment(const Geom& p, const Geom& q);
/// Segment with sqrt-extension endpoints on the given line a x + b y + c = 0 (endpoints must be on the line).
Geom make_segment_on_line(const Rational& a, const Rational& b, const Rational& c, const Geom& source, const Geom& target);
bool is_full(const Geom& c);
bool is_linear(const Geom& c);
bool is_circular(const Geom& c);
int orientation(const Geom& c);                                ///< +1 ccw, -1 cw, 0 for linear
void center(const Geom& c, Rational& cx, Rational& cy);        ///< circular only
Rational squared_radius(const Geom& c);                        ///< circular only
void supporting_line(const Geom& c, Rational& a, Rational& b, Rational& c_);   ///< linear only
}  // namespace circle_segment

namespace polyline {         // Kind::Polyline — Arr_polyline_traits_2<Arr_segment_traits_2<Epeck>>
Geom make(const std::vector<Geom>& points);                    ///< >= 2 points, consecutive distinct; general Curve_2
Geom make_from_segments(const std::vector<Geom>& segments);    ///< Segment-kind or Polyline sub-segments, chained end to end
Geom make_x_monotone(const std::vector<Geom>& points);         ///< points must form an x-monotone chain (Precondition error otherwise)
std::size_t number_of_subcurves(const Geom& c);
Geom subcurve(const Geom& c, std::size_t i);                   ///< as a Segment-kind Geom
std::size_t number_of_points(const Geom& c);
Geom point(const Geom& c, std::size_t i);                      ///< Segment-kind point
}  // namespace polyline

namespace bezier {           // Kind::Bezier — _Bezier_curve_2 / _Bezier_x_monotone_2 / _Bezier_point_2
Geom make(const std::vector<Rational>& control_xy);            ///< flattened x0,y0,x1,y1,... (>= 2 control points) -> general Curve_2
Geom make_from_points(const std::vector<Geom>& control_points);
std::size_t number_of_control_points(const Geom& c);           ///< Curve or XCurve (via supporting curve)
void control_point(const Geom& c, std::size_t i, Rational& x, Rational& y);
std::size_t curve_id(const Geom& c);                           ///< CGAL's serial id of the supporting curve
Geom supporting_curve(const Geom& xc);                          ///< XCurve -> Curve
unsigned xid(const Geom& xc);                                  ///< index of the x-monotone piece within its supporting curve
void parameter_range(const Geom& xc, double& t_min, double& t_max);   ///< ORDERED (t_min <= t_max) parameter interval of an x-monotone piece, from the exact endpoint parameters; direction via xcurve_source/target
/// Evaluate the supporting curve at a rational parameter: exact Bezier point (carrying the originator).
Geom point_at(const Geom& c, const Rational& t);
/// Evaluate approximately (doubles) at t in [0,1]; works on Curve or XCurve (supporting curve).
void evaluate_approx(const Geom& c, double t, double& x, double& y);
/// Uniform samples of the supporting curve for t in [t0, t1] (n >= 2), flattened x,y.
void sample(const Geom& c, double t0, double t1, std::size_t n, std::vector<double>& out);
bool has_no_self_intersections(const Geom& c);
/// For a Bezier point: (curve id, approximate t) for each originating curve it lies on.
void point_originators(const Geom& p, std::vector<std::pair<std::size_t, double>>& out);
/// Exact algebraic parameter of the point on the originating curve with the given id (boxed Algebraic number); throws if none / not known.
Geom point_parameter(const Geom& p, std::size_t curve_id);
}  // namespace bezier

namespace conic {            // Kind::Conic — Conic_arc_2 / Conic_x_monotone_arc_2 / Conic_point_2
enum ConicType : int { UNKNOWN = 0, ELLIPSE = 1, PARABOLA = 2, HYPERBOLA = 3, LINE_PAIR_OR_SEGMENT = 4 };
/// Full conic r x^2 + s y^2 + t x y + u x + v y + w = 0 (must be an ellipse / circle, i.e. bounded).
Geom make_full(const Rational& r, const Rational& s, const Rational& t, const Rational& u, const Rational& v, const Rational& w);
/// Arc of the conic between source and target (Conic-kind points, may be algebraic) traversed with `orientation` (+1 ccw, -1 cw; 0 for segments of degenerate conics).
Geom make_arc(const Rational& r, const Rational& s, const Rational& t, const Rational& u, const Rational& v, const Rational& w,
              int orientation, const Geom& source, const Geom& target);
/// Arc whose endpoints are defined as intersections of the arc's conic with two other conics (approximate endpoint coordinates disambiguate).
Geom make_arc_with_defining_conics(const Rational coeffs[6], int orientation,
                                   double approx_source_x, double approx_source_y, const Rational source_conic[6],
                                   double approx_target_x, double approx_target_y, const Rational target_conic[6]);
Geom make_circle(const Rational& cx, const Rational& cy, const Rational& squared_radius, int orientation);
Geom make_circle_arc(const Rational& cx, const Rational& cy, const Rational& squared_radius, int orientation, const Geom& source, const Geom& target);
/// Ellipse with center (cx,cy), semi-axis lengths a (along direction (dx,dy)) and b (perpendicular). The direction need not be normalized.
Geom make_ellipse(const Rational& cx, const Rational& cy, const Rational& a, const Rational& b, const Rational& dx, const Rational& dy, int orientation);
Geom make_segment(const Geom& p, const Geom& q);
Geom make_from_five_points(const Geom& p1, const Geom& p2, const Geom& p3, const Geom& p4, const Geom& p5);   ///< rational points; p1 source, p5 target
/// Rational quadratic Bezier with rational control points and positive rational weights -> exact conic arc from p0 to p2.
Geom make_from_rational_bezier(const Geom& p0, const Geom& p1, const Geom& p2, const Rational& w0, const Rational& w1, const Rational& w2);
/// Exact conversion of a circle-segment kind curve (circle / arc / segment; sqrt endpoints allowed).
Geom make_from_circle_segment(const Geom& circle_segment_curve);
/// Coefficients as stored by CGAL (integerized, sign normalized by CGAL: they define the same conic as the input up to a scalar).
void coefficients(const Geom& c, Rational out[6]);
int orientation(const Geom& c);
bool is_full(const Geom& c);
int conic_type(const Geom& c);
/// A Conic-kind point with algebraic coordinates given as boxed numbers (Rational or Algebraic).
Geom make_point_algebraic(const Geom& x, const Geom& y);
/// CGAL 6.1's hyperbolic-arc support is unreliable (assertion failures in build_hyperbolic_arc_data).
/// By default every constructor throws Error(Unsupported) for hyperbolic supporting conics
/// (4rs - t^2 < 0); this switch lets expert users opt in.
void set_allow_hyperbolic(bool allow);
bool allow_hyperbolic();
}  // namespace conic

namespace sphere {           // Kind::Sphere — geodesic arcs on the unit sphere, points are directions
enum PointLocation : int { NO_BOUNDARY = 0, MIN_BOUNDARY = 1, MID_BOUNDARY = 2, MAX_BOUNDARY = 3 };  ///< CGAL Arr_extended_direction_3::Location_type
Geom make_point(const Rational& x, const Rational& y, const Rational& z);   ///< direction (not normalized); (0,0,0) invalid
void point_xyz(const Geom& p, Rational& x, Rational& y, Rational& z);
int point_location(const Geom& p);
Geom make_arc(const Geom& p, const Geom& q);                   ///< minor great-circle arc from p to q (p != +-q); general Curve_2
Geom make_arc_with_normal(const Geom& p, const Geom& q, const Geom& normal);   ///< arc from p to q on the great circle with the given normal (allows antipodal p,q and major arcs)
Geom make_full_circle(const Geom& normal);                     ///< full great circle with the given normal
Geom make_x_monotone_arc(const Geom& p, const Geom& q);        ///< Construct_x_monotone_curve_2 (Precondition error if not x-monotone)
bool is_full(const Geom& c);
bool is_vertical(const Geom& c);
bool is_meridian(const Geom& c);
bool is_degenerate(const Geom& c);
Geom normal(const Geom& c);                                    ///< as a Sphere-kind point (direction)
}  // namespace sphere

}  // namespace arr2d
