// arr2d — KindOpsBase<Types>: the generic part of KindOps, implemented once for all kinds
// on top of CGAL::Arr_traits_adaptor_2<Types::Traits> (which synthesizes Is_in_x_range_2,
// Parameter_space_in_x/y_2 defaults, Compare_y_position_2, ...).
//
// Each kind TU derives:   class SegmentOps final : public KindOpsBase<SegmentTypes> { ... };
// and implements ONLY the pure virtuals listed under "kind-specific" below, plus the free
// functions of its namespace in ops.hpp.
//
// Detection idiom: optional functors (Approximate_2, Construct_x_monotone_curve_2, Trim_2,
// Construct_opposite_2, Compare_endpoints_xy_2, Parameter_space_in_x_2 ...) are used only
// when Types::Traits provides a working `<name>_object()`; otherwise the method throws
// Error(Unsupported). Known CGAL 6.1 breakages must be handled explicitly (see the API maps):
//   * Arr_linear_traits_2::construct_opposite_2_object()(xcv) does not compile -> Linear
//     overrides construct_opposite (segments flipped manually, rays/lines -> Unsupported? no:
//     lines: opposite direction line; rays: Unsupported).
//   * Bezier Construct_opposite_2::operator() is non-const; Merge_2/Trim_2 must come from the
//     traits object (private ctors).
//   * Conic: use the traits functors only (member functions on the arc classes are deprecated/broken).
//
// ---------------------------------------------------------------------------------------
// What this header guarantees (see kind_ops_base_impl.hpp for the definitions):
//
//   * Every optional functor is probed with the detection idiom on an *expression* of the
//     form `traits.xxx_2_object()(args...)`. A missing functor never breaks the build; the
//     corresponding method throws Error(Unsupported, "<kind>: <functor> not available").
//   * Functors are never cached: they are fetched at the point of use and copied into a
//     *non-const* local, because several CGAL 6.1 functors declare a non-const operator()
//     (Bezier Construct_opposite_2; geodesic Compare_endpoints_xy_2 / Construct_opposite_2;
//     linear Trim_2). Adaptor/decorator functors also keep raw back-pointers into the traits
//     (traits_adapters_and_misc.md gotcha 6), so storing them would dangle.
//   * The adaptor is used for the *synthesized* predicates only (Compare_y_at_x_2,
//     Compare_y_at_x_left/right_2, Is_in_x_range_2, Is_closed_2, Parameter_space_in_x/y_2);
//     everything else goes through the process-wide traits object returned by Types::traits(),
//     which is the very object the arrangements of that kind are constructed with (shared
//     Bezier / conic caches).
// ---------------------------------------------------------------------------------------
#pragma once

#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <CGAL/Arrangement_2/Arr_traits_adaptor_2.h>
#include <CGAL/enum.h>
#include <CGAL/Arr_enums.h>

#include "arr2d/common.hpp"
#include "arr2d/ops.hpp"

namespace arr2d {

// ===========================================================================
// Detection idiom for the optional traits functors.
//
// Each detector probes the *call expression* (not just the accessor), so a traits class
// that declares an accessor with an unusable signature is reported as "not available"
// instead of breaking the build. Note that a functor whose *declaration* is fine but whose
// *body* does not compile (Arr_linear_traits_2::Construct_opposite_2, which calls the
// non-existent Arr_linear_object_2::get_pt()/get_ps(), see
// traits_segment_linear_polyline.md gotcha 3) cannot be detected this way — such cases are
// black-listed explicitly below by Kind.
// ===========================================================================
namespace kob_detail {

template <class T> using cref = const T&;

// -- Trim_2 -----------------------------------------------------------------
template <class T, class = void>
struct has_trim : std::false_type {};
template <class T>
struct has_trim<T, std::void_t<decltype(std::declval<const T&>().trim_2_object()(
                       std::declval<cref<typename T::X_monotone_curve_2>>(),
                       std::declval<cref<typename T::Point_2>>(),
                       std::declval<cref<typename T::Point_2>>()))>> : std::true_type {};

// -- Construct_opposite_2 ---------------------------------------------------
template <class T, class = void>
struct has_construct_opposite : std::false_type {};
template <class T>
struct has_construct_opposite<T, std::void_t<decltype(std::declval<const T&>().construct_opposite_2_object()(
                                     std::declval<cref<typename T::X_monotone_curve_2>>()))>> : std::true_type {};

// -- Compare_endpoints_xy_2 -------------------------------------------------
template <class T, class = void>
struct has_compare_endpoints : std::false_type {};
template <class T>
struct has_compare_endpoints<T, std::void_t<decltype(std::declval<const T&>().compare_endpoints_xy_2_object()(
                                    std::declval<cref<typename T::X_monotone_curve_2>>()))>> : std::true_type {};

// -- Construct_x_monotone_curve_2(p, q) -------------------------------------
template <class T, class = void>
struct has_construct_xcurve_pq : std::false_type {};
template <class T>
struct has_construct_xcurve_pq<T, std::void_t<decltype(std::declval<const T&>().construct_x_monotone_curve_2_object()(
                                      std::declval<cref<typename T::Point_2>>(),
                                      std::declval<cref<typename T::Point_2>>()))>> : std::true_type {};

// -- Approximate_2(p, int i) ------------------------------------------------
template <class T, class = void>
struct has_approximate_coord : std::false_type {};
template <class T>
struct has_approximate_coord<T, std::void_t<decltype(std::declval<const T&>().approximate_2_object()(
                                    std::declval<cref<typename T::Point_2>>(), 0))>> : std::true_type {};

// -- Are_mergeable_2 / Merge_2 ----------------------------------------------
template <class T, class = void>
struct has_are_mergeable : std::false_type {};
template <class T>
struct has_are_mergeable<T, std::void_t<decltype(std::declval<const T&>().are_mergeable_2_object()(
                                std::declval<cref<typename T::X_monotone_curve_2>>(),
                                std::declval<cref<typename T::X_monotone_curve_2>>()))>> : std::true_type {};

template <class T, class = void>
struct has_merge : std::false_type {};
template <class T>
struct has_merge<T, std::void_t<decltype(std::declval<const T&>().merge_2_object()(
                        std::declval<cref<typename T::X_monotone_curve_2>>(),
                        std::declval<cref<typename T::X_monotone_curve_2>>(),
                        std::declval<typename T::X_monotone_curve_2&>()))>> : std::true_type {};

// -- Split_2 ----------------------------------------------------------------
template <class T, class = void>
struct has_split : std::false_type {};
template <class T>
struct has_split<T, std::void_t<decltype(std::declval<const T&>().split_2_object()(
                        std::declval<cref<typename T::X_monotone_curve_2>>(),
                        std::declval<cref<typename T::Point_2>>(),
                        std::declval<typename T::X_monotone_curve_2&>(),
                        std::declval<typename T::X_monotone_curve_2&>()))>> : std::true_type {};

}  // namespace kob_detail

template <class Types>
class KindOpsBase : public KindOps {
 public:
  using Traits = typename Types::Traits;
  using Adaptor = CGAL::Arr_traits_adaptor_2<Traits>;
  using Point_2 = typename Types::Point_2;
  using Curve_2 = typename Types::Curve_2;
  using X_monotone_curve_2 = typename Types::X_monotone_curve_2;

  /// The traits classes do NOT export these; every caller must declare them itself
  /// (traits_bezier.md gotcha 1, traits_adapters_and_misc.md gotcha 2). `Multiplicity`
  /// differs per traits: unsigned int for segment/linear/circle-segment/polyline/Bezier,
  /// std::size_t for conic and geodesic.
  using Multiplicity = typename Traits::Multiplicity;
  using Make_x_monotone_result = std::variant<Point_2, X_monotone_curve_2>;
  using Intersection_point = std::pair<Point_2, Multiplicity>;
  using Intersection_result = std::variant<Intersection_point, X_monotone_curve_2>;

  KindOpsBase();
  ~KindOpsBase() override = default;
  KindOpsBase(const KindOpsBase&) = delete;             ///< owns a raw (deliberately leaked) adaptor
  KindOpsBase& operator=(const KindOpsBase&) = delete;

  Kind kind() const override { return Types::kind; }
  const char* name() const override { return kind_name(Types::kind); }
  int dimension() const override { return Types::is_sphere ? 3 : 2; }
  bool is_unbounded_kind() const override { return Types::is_unbounded; }

  // ---- compile-time feature flags (also reported by KindOps users / tests) ----
  static constexpr bool k_is_linear = (Types::kind == Kind::Linear);
  /// Arr_linear_traits_2::Construct_opposite_2::operator() does not compile (it calls
  /// Arr_linear_object_2::get_pt()/get_ps(), which do not exist) — the declaration is fine,
  /// so only an explicit black-list can keep it out of the instantiated vtable.
  /// traits_segment_linear_polyline.md gotcha 3. The Linear kind TU overrides construct_opposite.
  static constexpr bool has_construct_opposite = kob_detail::has_construct_opposite<Traits>::value && !k_is_linear;
  static constexpr bool has_compare_endpoints = kob_detail::has_compare_endpoints<Traits>::value;
  static constexpr bool has_trim = kob_detail::has_trim<Traits>::value;
  static constexpr bool has_construct_xcurve = kob_detail::has_construct_xcurve_pq<Traits>::value;
  static constexpr bool has_approximate_coord = kob_detail::has_approximate_coord<Traits>::value;
  static constexpr bool has_are_mergeable = kob_detail::has_are_mergeable<Traits>::value;
  static constexpr bool has_merge = kob_detail::has_merge<Traits>::value;
  static constexpr bool has_split = kob_detail::has_split<Traits>::value;

  // ---- boxing helpers usable by every kind TU and by ArrImpl ----
  static Geom box_point(const Point_2& p) { return make_geom(Types::kind, GeomType::Point, p); }
  static Geom box_curve(const Curve_2& c) { return make_geom(Types::kind, GeomType::Curve, c); }
  static Geom box_xcurve(const X_monotone_curve_2& c) { return make_geom(Types::kind, GeomType::XCurve, c); }
  static const Point_2& point(const Geom& g) { require_point(g, Types::kind); return g.template as<Point_2>(); }
  /// XCurve box -> the x-monotone curve. A *Curve* box is accepted when Curve_2 and
  /// X_monotone_curve_2 are the same C++ type for this kind (segment, linear), because then the
  /// box really does hold an X_monotone_curve_2. Otherwise: Error(NotXMonotone).
  static const X_monotone_curve_2& xcurve(const Geom& g);
  /// Curve or XCurve -> the general Curve_2 (for kinds where the two types coincide, both box types hold the same C++ type)
  const Curve_2& curve(const Geom& g) const;

  // ---- generic implementations (traits-adaptor based) ----
  int point_compare_x(const Geom& p, const Geom& q) const override;
  int point_compare_xy(const Geom& p, const Geom& q) const override;
  bool point_equal(const Geom& p, const Geom& q) const override;
  bool is_x_monotone(const Geom& c) const override;
  void make_x_monotone(const Geom& c, std::vector<Geom>& out) const override;
  Geom to_x_monotone(const Geom& c) const override;
  Geom xcurve_min_vertex(const Geom& xc) const override;
  Geom xcurve_max_vertex(const Geom& xc) const override;
  bool xcurve_is_vertical(const Geom& xc) const override;
  bool xcurve_is_directed_right(const Geom& xc) const override;
  int compare_endpoints_xy(const Geom& xc) const override;
  Geom construct_opposite(const Geom& xc) const override;
  bool curve_equal(const Geom& a, const Geom& b) const override;
  int compare_y_at_x(const Geom& p, const Geom& xc) const override;
  int compare_y_at_x_left(const Geom& xc1, const Geom& xc2, const Geom& p) const override;
  int compare_y_at_x_right(const Geom& xc1, const Geom& xc2, const Geom& p) const override;
  bool is_in_x_range(const Geom& xc, const Geom& p) const override;
  void split(const Geom& xc, const Geom& p, Geom& left, Geom& right) const override;
  void intersect(const Geom& xc1, const Geom& xc2, std::vector<IntersectionResult>& out) const override;
  bool are_mergeable(const Geom& xc1, const Geom& xc2) const override;
  Geom merge(const Geom& xc1, const Geom& xc2) const override;
  Geom trim(const Geom& xc, const Geom& src, const Geom& tgt) const override;
  int parameter_space_in_x(const Geom& xc, int curve_end) const override;
  int parameter_space_in_y(const Geom& xc, int curve_end) const override;
  Geom construct_x_monotone_curve(const Geom& p, const Geom& q) const override;
  double approximate_coordinate(const Geom& p, int i) const override;
  double approximate_length(const Geom& c, double tolerance) const override;   ///< via approximate()

  // ---- kind-specific (pure virtual here; the kind TU implements them) ----
  // Geom make_point(const Rational&, const Rational&) const
  // Geom make_point_3(...) const                       (planar kinds: throw Unsupported)
  // void point_approx(const Geom&, double*) const
  // void point_interval(const Geom&, std::vector<std::pair<double,double>>&) const
  // bool point_is_rational(const Geom&) const
  // void point_exact_rational(const Geom&, std::vector<Rational>&) const
  // void point_exact(const Geom&, std::vector<Geom>&) const
  // std::string point_repr(const Geom&) const
  // Geom convert_point(const Geom&) const
  // Geom to_curve(const Geom&) const
  // Geom xcurve_source/xcurve_target(const Geom&) const ; bool xcurve_has_source/has_target
  // BBox curve_bbox(const Geom&) const ; bool curve_is_bounded(const Geom&) const
  // void approximate(const Geom&, double, const BBox*, std::vector<double>&) const
  // std::string curve_repr(const Geom&) const
  // void convert_curve(const Geom&, std::vector<Geom>&) const
  // bool has_polygon_set() const

 protected:
  const Traits& traits() const { return *m_traits; }
  const Adaptor& adaptor() const { return *m_adaptor; }

  /// Error(Unsupported, "<kind>: <functor> not available") — the one message shape used for
  /// every optional functor a traits class does not provide.
  [[noreturn]] void unsupported(const char* functor) const {
    throw_error(ErrorCode::Unsupported, std::string(kind_name(Types::kind)) + ": " + functor + " not available");
  }
  [[noreturn]] void invalid(const std::string& msg) const {
    throw_error(ErrorCode::InvalidArgument, std::string(kind_name(Types::kind)) + ": " + msg);
  }

  /// CGAL::Comparison_result (SMALLER=-1, EQUAL=0, LARGER=1) -> our int convention.
  static int cmp_int(CGAL::Comparison_result r) { return static_cast<int>(r); }
  /// CGAL::Arr_parameter_space -> arr2d::ParameterSpace. CGAL 6.1 spells the values as const
  /// objects (`const Arr_parameter_space ARR_LEFT_BOUNDARY = LEFT_BOUNDARY;` in Arr_enums.h),
  /// hence the if/else chain rather than a switch.
  static int param_space_int(CGAL::Arr_parameter_space ps);
  /// arr2d::CurveEnd (0/1) -> CGAL::Arr_curve_end (throws InvalidArgument on anything else).
  CGAL::Arr_curve_end curve_end(int e) const;

  const Traits* m_traits;                       ///< == &Types::traits() (process-wide instance, never copied)
  /// Arr_traits_adaptor_2 constructed FROM the traits object, and DELIBERATELY LEAKED (raw
  /// owning pointer, never deleted). KindOps objects are per-kind process-wide singletons, so
  /// the leak is bounded by 7 adaptors; destroying one during static teardown, on the other
  /// hand, is unsafe: the conic adaptor's copy of the intersection cache holds CORE::Expr
  /// values, and freeing a CORE object after CORE's own static MemoryPool has been destroyed
  /// aborts with `CGAL error: assertion violation! ! blocks.empty()`
  /// (CGAL/CORE/MemoryPool.h:125) — verified. The same applies to the process-wide traits
  /// object itself: kind TUs for bezier/conic must return a leaked instance from
  /// Types::traits() (`static Traits* t = new Traits(); return *t;`).
  ///
  /// The adaptor derives from the traits and copy-constructs it; copy-construction was
  /// checked per kind and is safe:
  ///   segment / linear / circle-segment / sphere : value types (the circle-segment traits only
  ///       copies its intersection-cache map; the geodesic traits derives from a stateless kernel).
  ///       Copy-ASSIGNING a polyline traits would double-free
  ///       (traits_segment_linear_polyline.md gotcha 2), so the adaptor is held by pointer and
  ///       is never assigned to.
  ///   polyline : Arr_polycurve_basic_traits_2's copy ctor allocates a *fresh* sub-traits and the
  ///       copy owns it (Arr_segment_traits_2 is stateless, so nothing is lost).
  ///   bezier : the copy aliases the original's Bezier_cache / Intersection_map with
  ///       m_owner == false (traits_bezier.md gotcha 2). Safe *and* desirable here, because
  ///       Types::traits() is a process-wide instance that outlives this object and the cache
  ///       therefore stays shared with the arrangements. The copy never frees the cache.
  ///   conic : the copy shares the kernels through shared_ptr and starts with its own copy of
  ///       the intersection cache (traits_conic.md §1.2). Correct, only slightly less cached.
  const Adaptor* m_adaptor;
};

}  // namespace arr2d

#include "arr2d/impl/kind_ops_base_impl.hpp"
