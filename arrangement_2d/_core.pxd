# Cython declarations of the arr2d C++ core (src/arr2d/include/arr2d/*.hpp).
# Keep in sync with the headers; names mirror C++ exactly.
# distutils: language = c++

from libc.stdint cimport uint64_t, int64_t
from libcpp cimport bool as cbool
from libcpp.string cimport string
from libcpp.vector cimport vector
from libcpp.pair cimport pair
from libcpp.memory cimport unique_ptr, shared_ptr


cdef extern from "_exc_bridge.hpp":
    # Rethrows the in-flight C++ exception and converts it into a Python exception
    # (arr2d::Error -> arrangement_2d.errors.*; CGAL::Failure_exception -> CGALError subclasses;
    #  std::exception -> RuntimeError). Used as the `except +` handler everywhere below.
    cdef void arr2d_translate_exception()


cdef extern from "arr2d/common.hpp" namespace "arr2d":
    cdef enum class Kind "arr2d::Kind":
        Segment "arr2d::Kind::Segment"
        Linear "arr2d::Kind::Linear"
        CircleSegment "arr2d::Kind::CircleSegment"
        Polyline "arr2d::Kind::Polyline"
        Bezier "arr2d::Kind::Bezier"
        Conic "arr2d::Kind::Conic"
        Sphere "arr2d::Kind::Sphere"
        NumKinds "arr2d::Kind::NumKinds"

    const char* kind_name(Kind k)
    int kind_from_name(const string& name)

    cdef cppclass Rational:
        Rational()
        Rational(const Rational&)

    cdef enum class ErrorCode "arr2d::ErrorCode":
        Generic "arr2d::ErrorCode::Generic"
        KindMismatch "arr2d::ErrorCode::KindMismatch"
        InvalidHandle "arr2d::ErrorCode::InvalidHandle"
        NotXMonotone "arr2d::ErrorCode::NotXMonotone"
        NotRepresentable "arr2d::ErrorCode::NotRepresentable"
        Unsupported "arr2d::ErrorCode::Unsupported"
        InvalidArgument "arr2d::ErrorCode::InvalidArgument"
        CallbackFailed "arr2d::ErrorCode::CallbackFailed"

    cdef cppclass Error:
        ErrorCode code
        const char* what()

    ctypedef void (*PyObjectHook)(void*)
    void set_pyobject_hooks(PyObjectHook incref, PyObjectHook decref)

    cdef cppclass PyRef:
        void* obj
        PyRef()
        void set(void* o)
        void* get()
        cbool empty()

    cdef enum class GeomType "arr2d::GeomType":
        Point "arr2d::GeomType::Point"
        Curve "arr2d::GeomType::Curve"
        XCurve "arr2d::GeomType::XCurve"
        Number "arr2d::GeomType::Number"

    cdef cppclass Geom:
        Kind kind
        GeomType type
        Geom()
        Geom(const Geom&)
        cbool empty()

    cdef struct VH:
        void* p
        uint64_t id
    cdef struct HH:
        void* p
        uint64_t id
    cdef struct FH:
        void* p
        uint64_t id
    cdef struct CH:
        void* p
        uint64_t id

    cdef cppclass Located:
        int type
        void* p
        uint64_t id
        VH as_vertex()
        HH as_halfedge()
        FH as_face()

    cdef enum HalfedgeDirection:
        ARR_LEFT_TO_RIGHT
        ARR_RIGHT_TO_LEFT
    cdef enum ParameterSpace:
        ARR_LEFT_BOUNDARY
        ARR_RIGHT_BOUNDARY
        ARR_BOTTOM_BOUNDARY
        ARR_TOP_BOUNDARY
        ARR_INTERIOR
    cdef enum CurveEnd:
        ARR_MIN_END
        ARR_MAX_END
    cdef enum PointLocationStrategy:
        PL_DEFAULT
        PL_NAIVE
        PL_SIMPLE
        PL_WALK
        PL_LANDMARKS
        PL_TRAPEZOID
        PL_TRIANGULATION
        PL_NUM_STRATEGIES
    const char* point_location_name(int strategy)
    int point_location_from_name(const string& name)


cdef extern from "arr2d/numbers.hpp" namespace "arr2d":
    cdef enum class NumberKind "arr2d::NumberKind":
        NRational "arr2d::NumberKind::Rational"
        NSqrtExt "arr2d::NumberKind::SqrtExt"
        NAlgebraic "arr2d::NumberKind::Algebraic"

    cdef cppclass SqrtExt:
        Rational a
        Rational b
        Rational c
        SqrtExt()

    Rational rational_from_double(double d) except +arr2d_translate_exception
    Rational rational_from_int64(int64_t v) except +arr2d_translate_exception
    Rational rational_from_strings(const string& num, const string& den) except +arr2d_translate_exception
    Rational rational_from_string(const string& s) except +arr2d_translate_exception
    void rational_to_strings(const Rational& r, string& num, string& den) except +arr2d_translate_exception
    double rational_to_double(const Rational& r) except +arr2d_translate_exception
    int rational_sign(const Rational& r) except +arr2d_translate_exception
    int rational_compare(const Rational& a, const Rational& b) except +arr2d_translate_exception
    cbool rational_is_integer(const Rational& r) except +arr2d_translate_exception

    Geom box_rational(const Rational& r) except +arr2d_translate_exception
    Geom box_sqrt_ext(const SqrtExt& s) except +arr2d_translate_exception
    NumberKind number_kind(const Geom& n) except +arr2d_translate_exception
    double number_to_double(const Geom& n) except +arr2d_translate_exception
    pair[double, double] number_interval(const Geom& n, int bits) except +arr2d_translate_exception
    cbool number_is_rational(const Geom& n) except +arr2d_translate_exception
    Rational number_to_rational(const Geom& n) except +arr2d_translate_exception
    SqrtExt number_to_sqrt_ext(const Geom& n) except +arr2d_translate_exception
    int number_sign(const Geom& n) except +arr2d_translate_exception
    int number_compare(const Geom& a, const Geom& b) except +arr2d_translate_exception
    string number_repr(const Geom& n) except +arr2d_translate_exception


cdef extern from "arr2d/ops.hpp" namespace "arr2d":
    cdef cppclass IntersectionResult:
        cbool is_point
        Geom point
        size_t multiplicity
        Geom overlap

    cdef cppclass BBox:
        double lo[3]
        double hi[3]
        int dim
        BBox()

    cdef cppclass KindOps:
        Kind kind()
        const char* name()
        int dimension()
        cbool is_unbounded_kind()
        cbool has_polygon_set()
        Geom make_point(const Rational& x, const Rational& y) except +arr2d_translate_exception
        Geom make_point_3(const Rational& x, const Rational& y, const Rational& z) except +arr2d_translate_exception
        void point_approx(const Geom& p, double* xyz) except +arr2d_translate_exception
        void point_interval(const Geom& p, vector[pair[double, double]]& out) except +arr2d_translate_exception
        cbool point_is_rational(const Geom& p) except +arr2d_translate_exception
        void point_exact_rational(const Geom& p, vector[Rational]& out) except +arr2d_translate_exception
        void point_exact(const Geom& p, vector[Geom]& numbers) except +arr2d_translate_exception
        int point_compare_x(const Geom& p, const Geom& q) except +arr2d_translate_exception
        int point_compare_xy(const Geom& p, const Geom& q) except +arr2d_translate_exception
        cbool point_equal(const Geom& p, const Geom& q) except +arr2d_translate_exception
        string point_repr(const Geom& p) except +arr2d_translate_exception
        Geom convert_point(const Geom& p) except +arr2d_translate_exception
        cbool is_x_monotone(const Geom& c) except +arr2d_translate_exception
        void make_x_monotone(const Geom& c, vector[Geom]& out) except +arr2d_translate_exception
        Geom to_x_monotone(const Geom& c) except +arr2d_translate_exception
        Geom to_curve(const Geom& xc) except +arr2d_translate_exception
        Geom xcurve_source(const Geom& xc) except +arr2d_translate_exception
        Geom xcurve_target(const Geom& xc) except +arr2d_translate_exception
        cbool xcurve_has_source(const Geom& xc) except +arr2d_translate_exception
        cbool xcurve_has_target(const Geom& xc) except +arr2d_translate_exception
        Geom xcurve_min_vertex(const Geom& xc) except +arr2d_translate_exception
        Geom xcurve_max_vertex(const Geom& xc) except +arr2d_translate_exception
        cbool xcurve_is_vertical(const Geom& xc) except +arr2d_translate_exception
        cbool xcurve_is_directed_right(const Geom& xc) except +arr2d_translate_exception
        int compare_endpoints_xy(const Geom& xc) except +arr2d_translate_exception
        Geom construct_opposite(const Geom& xc) except +arr2d_translate_exception
        BBox curve_bbox(const Geom& c) except +arr2d_translate_exception
        cbool curve_is_bounded(const Geom& c) except +arr2d_translate_exception
        void approximate(const Geom& c, double tolerance, const BBox* clip, vector[double]& out) except +arr2d_translate_exception
        double approximate_length(const Geom& c, double tolerance) except +arr2d_translate_exception
        string curve_repr(const Geom& c) except +arr2d_translate_exception
        cbool curve_equal(const Geom& a, const Geom& b) except +arr2d_translate_exception
        void convert_curve(const Geom& c, vector[Geom]& out) except +arr2d_translate_exception
        int compare_y_at_x(const Geom& p, const Geom& xc) except +arr2d_translate_exception
        int compare_y_at_x_left(const Geom& xc1, const Geom& xc2, const Geom& p) except +arr2d_translate_exception
        int compare_y_at_x_right(const Geom& xc1, const Geom& xc2, const Geom& p) except +arr2d_translate_exception
        cbool is_in_x_range(const Geom& xc, const Geom& p) except +arr2d_translate_exception
        void split(const Geom& xc, const Geom& p, Geom& left, Geom& right) except +arr2d_translate_exception
        void intersect(const Geom& xc1, const Geom& xc2, vector[IntersectionResult]& out) except +arr2d_translate_exception
        cbool are_mergeable(const Geom& xc1, const Geom& xc2) except +arr2d_translate_exception
        Geom merge(const Geom& xc1, const Geom& xc2) except +arr2d_translate_exception
        Geom trim(const Geom& xc, const Geom& src, const Geom& tgt) except +arr2d_translate_exception
        int parameter_space_in_x(const Geom& xc, int curve_end) except +arr2d_translate_exception
        int parameter_space_in_y(const Geom& xc, int curve_end) except +arr2d_translate_exception
        Geom construct_x_monotone_curve(const Geom& p, const Geom& q) except +arr2d_translate_exception
        double approximate_coordinate(const Geom& p, int i) except +arr2d_translate_exception


cdef extern from "arr2d/ops.hpp" namespace "arr2d::segment":
    Geom segment_make "arr2d::segment::make"(const Geom& p, const Geom& q) except +arr2d_translate_exception
    Geom segment_make_xy "arr2d::segment::make_xy"(const Rational& x1, const Rational& y1, const Rational& x2, const Rational& y2) except +arr2d_translate_exception
    void segment_endpoints "arr2d::segment::endpoints"(const Geom& s, Geom& source, Geom& target) except +arr2d_translate_exception
    void segment_supporting_line "arr2d::segment::supporting_line"(const Geom& s, Rational& a, Rational& b, Rational& c) except +arr2d_translate_exception

cdef extern from "arr2d/ops.hpp" namespace "arr2d::linear":
    cdef enum LinearWhich "arr2d::linear::Which":
        LINEAR_SEGMENT "arr2d::linear::SEGMENT"
        LINEAR_RAY "arr2d::linear::RAY"
        LINEAR_LINE "arr2d::linear::LINE"
    Geom linear_make_segment "arr2d::linear::make_segment"(const Geom& p, const Geom& q) except +arr2d_translate_exception
    Geom linear_make_ray "arr2d::linear::make_ray"(const Geom& source, const Geom& towards) except +arr2d_translate_exception
    Geom linear_make_ray_direction "arr2d::linear::make_ray_direction"(const Geom& source, const Rational& dx, const Rational& dy) except +arr2d_translate_exception
    Geom linear_make_line "arr2d::linear::make_line"(const Geom& p, const Geom& q) except +arr2d_translate_exception
    Geom linear_make_line_coefficients "arr2d::linear::make_line_coefficients"(const Rational& a, const Rational& b, const Rational& c) except +arr2d_translate_exception
    int linear_which "arr2d::linear::which"(const Geom& c) except +arr2d_translate_exception
    void linear_supporting_line "arr2d::linear::supporting_line"(const Geom& c, Rational& a, Rational& b, Rational& c_) except +arr2d_translate_exception
    void linear_direction "arr2d::linear::direction"(const Geom& c, Rational& dx, Rational& dy) except +arr2d_translate_exception

cdef extern from "arr2d/ops.hpp" namespace "arr2d::circle_segment":
    Geom cs_make_point_sqrt "arr2d::circle_segment::make_point_sqrt"(const SqrtExt& x, const SqrtExt& y) except +arr2d_translate_exception
    void cs_point_sqrt "arr2d::circle_segment::point_sqrt"(const Geom& p, SqrtExt& x, SqrtExt& y) except +arr2d_translate_exception
    Geom cs_make_full_circle "arr2d::circle_segment::make_full_circle"(const Rational& cx, const Rational& cy, const Rational& squared_radius, int orientation) except +arr2d_translate_exception
    Geom cs_make_full_circle_r "arr2d::circle_segment::make_full_circle_r"(const Rational& cx, const Rational& cy, const Rational& radius, int orientation) except +arr2d_translate_exception
    Geom cs_make_arc "arr2d::circle_segment::make_arc"(const Rational& cx, const Rational& cy, const Rational& squared_radius, int orientation, const Geom& source, const Geom& target) except +arr2d_translate_exception
    Geom cs_make_arc_r "arr2d::circle_segment::make_arc_r"(const Rational& cx, const Rational& cy, const Rational& radius, int orientation, const Geom& source, const Geom& target) except +arr2d_translate_exception
    cbool cs_has_rational_radius "arr2d::circle_segment::has_rational_radius"(const Geom& c) except +arr2d_translate_exception
    Rational cs_radius "arr2d::circle_segment::radius"(const Geom& c) except +arr2d_translate_exception
    Geom cs_make_arc_three_points "arr2d::circle_segment::make_arc_three_points"(const Geom& p, const Geom& q, const Geom& r) except +arr2d_translate_exception
    Geom cs_make_segment "arr2d::circle_segment::make_segment"(const Geom& p, const Geom& q) except +arr2d_translate_exception
    Geom cs_make_segment_on_line "arr2d::circle_segment::make_segment_on_line"(const Rational& a, const Rational& b, const Rational& c, const Geom& source, const Geom& target) except +arr2d_translate_exception
    cbool cs_is_full "arr2d::circle_segment::is_full"(const Geom& c) except +arr2d_translate_exception
    cbool cs_is_linear "arr2d::circle_segment::is_linear"(const Geom& c) except +arr2d_translate_exception
    cbool cs_is_circular "arr2d::circle_segment::is_circular"(const Geom& c) except +arr2d_translate_exception
    int cs_orientation "arr2d::circle_segment::orientation"(const Geom& c) except +arr2d_translate_exception
    void cs_center "arr2d::circle_segment::center"(const Geom& c, Rational& cx, Rational& cy) except +arr2d_translate_exception
    Rational cs_squared_radius "arr2d::circle_segment::squared_radius"(const Geom& c) except +arr2d_translate_exception
    void cs_supporting_line "arr2d::circle_segment::supporting_line"(const Geom& c, Rational& a, Rational& b, Rational& c_) except +arr2d_translate_exception

cdef extern from "arr2d/ops.hpp" namespace "arr2d::polyline":
    Geom polyline_make "arr2d::polyline::make"(const vector[Geom]& points) except +arr2d_translate_exception
    Geom polyline_make_from_segments "arr2d::polyline::make_from_segments"(const vector[Geom]& segments) except +arr2d_translate_exception
    Geom polyline_make_x_monotone "arr2d::polyline::make_x_monotone"(const vector[Geom]& points) except +arr2d_translate_exception
    size_t polyline_number_of_subcurves "arr2d::polyline::number_of_subcurves"(const Geom& c) except +arr2d_translate_exception
    Geom polyline_subcurve "arr2d::polyline::subcurve"(const Geom& c, size_t i) except +arr2d_translate_exception
    size_t polyline_number_of_points "arr2d::polyline::number_of_points"(const Geom& c) except +arr2d_translate_exception
    Geom polyline_point "arr2d::polyline::point"(const Geom& c, size_t i) except +arr2d_translate_exception

cdef extern from "arr2d/ops.hpp" namespace "arr2d::bezier":
    Geom bezier_make "arr2d::bezier::make"(const vector[Rational]& control_xy) except +arr2d_translate_exception
    Geom bezier_make_from_points "arr2d::bezier::make_from_points"(const vector[Geom]& control_points) except +arr2d_translate_exception
    size_t bezier_number_of_control_points "arr2d::bezier::number_of_control_points"(const Geom& c) except +arr2d_translate_exception
    void bezier_control_point "arr2d::bezier::control_point"(const Geom& c, size_t i, Rational& x, Rational& y) except +arr2d_translate_exception
    size_t bezier_curve_id "arr2d::bezier::curve_id"(const Geom& c) except +arr2d_translate_exception
    Geom bezier_supporting_curve "arr2d::bezier::supporting_curve"(const Geom& xc) except +arr2d_translate_exception
    unsigned bezier_xid "arr2d::bezier::xid"(const Geom& xc) except +arr2d_translate_exception
    void bezier_parameter_range "arr2d::bezier::parameter_range"(const Geom& xc, double& t_min, double& t_max) except +arr2d_translate_exception
    Geom bezier_point_at "arr2d::bezier::point_at"(const Geom& c, const Rational& t) except +arr2d_translate_exception
    void bezier_evaluate_approx "arr2d::bezier::evaluate_approx"(const Geom& c, double t, double& x, double& y) except +arr2d_translate_exception
    void bezier_sample "arr2d::bezier::sample"(const Geom& c, double t0, double t1, size_t n, vector[double]& out) except +arr2d_translate_exception
    cbool bezier_has_no_self_intersections "arr2d::bezier::has_no_self_intersections"(const Geom& c) except +arr2d_translate_exception
    void bezier_point_originators "arr2d::bezier::point_originators"(const Geom& p, vector[pair[size_t, double]]& out) except +arr2d_translate_exception
    Geom bezier_point_parameter "arr2d::bezier::point_parameter"(const Geom& p, size_t curve_id) except +arr2d_translate_exception

cdef extern from "arr2d/ops.hpp" namespace "arr2d::conic":
    cdef enum ConicType "arr2d::conic::ConicType":
        CONIC_UNKNOWN "arr2d::conic::UNKNOWN"
        CONIC_ELLIPSE "arr2d::conic::ELLIPSE"
        CONIC_PARABOLA "arr2d::conic::PARABOLA"
        CONIC_HYPERBOLA "arr2d::conic::HYPERBOLA"
        CONIC_LINE_PAIR_OR_SEGMENT "arr2d::conic::LINE_PAIR_OR_SEGMENT"
    Geom conic_make_full "arr2d::conic::make_full"(const Rational& r, const Rational& s, const Rational& t, const Rational& u, const Rational& v, const Rational& w) except +arr2d_translate_exception
    Geom conic_make_arc "arr2d::conic::make_arc"(const Rational& r, const Rational& s, const Rational& t, const Rational& u, const Rational& v, const Rational& w, int orientation, const Geom& source, const Geom& target) except +arr2d_translate_exception
    Geom conic_make_arc_with_defining_conics "arr2d::conic::make_arc_with_defining_conics"(const Rational* coeffs, int orientation, double approx_source_x, double approx_source_y, const Rational* source_conic, double approx_target_x, double approx_target_y, const Rational* target_conic) except +arr2d_translate_exception
    Geom conic_make_circle "arr2d::conic::make_circle"(const Rational& cx, const Rational& cy, const Rational& squared_radius, int orientation) except +arr2d_translate_exception
    Geom conic_make_circle_arc "arr2d::conic::make_circle_arc"(const Rational& cx, const Rational& cy, const Rational& squared_radius, int orientation, const Geom& source, const Geom& target) except +arr2d_translate_exception
    Geom conic_make_ellipse "arr2d::conic::make_ellipse"(const Rational& cx, const Rational& cy, const Rational& a, const Rational& b, const Rational& dx, const Rational& dy, int orientation) except +arr2d_translate_exception
    Geom conic_make_segment "arr2d::conic::make_segment"(const Geom& p, const Geom& q) except +arr2d_translate_exception
    Geom conic_make_from_five_points "arr2d::conic::make_from_five_points"(const Geom& p1, const Geom& p2, const Geom& p3, const Geom& p4, const Geom& p5) except +arr2d_translate_exception
    Geom conic_make_from_rational_bezier "arr2d::conic::make_from_rational_bezier"(const Geom& p0, const Geom& p1, const Geom& p2, const Rational& w0, const Rational& w1, const Rational& w2) except +arr2d_translate_exception
    Geom conic_make_from_circle_segment "arr2d::conic::make_from_circle_segment"(const Geom& circle_segment_curve) except +arr2d_translate_exception
    void conic_coefficients "arr2d::conic::coefficients"(const Geom& c, Rational* out) except +arr2d_translate_exception
    int conic_orientation "arr2d::conic::orientation"(const Geom& c) except +arr2d_translate_exception
    cbool conic_is_full "arr2d::conic::is_full"(const Geom& c) except +arr2d_translate_exception
    int conic_conic_type "arr2d::conic::conic_type"(const Geom& c) except +arr2d_translate_exception
    Geom conic_make_point_algebraic "arr2d::conic::make_point_algebraic"(const Geom& x, const Geom& y) except +arr2d_translate_exception
    void conic_set_allow_hyperbolic "arr2d::conic::set_allow_hyperbolic"(cbool allow)
    cbool conic_allow_hyperbolic "arr2d::conic::allow_hyperbolic"()

cdef extern from "arr2d/ops.hpp" namespace "arr2d::sphere":
    cdef enum SpherePointLocation "arr2d::sphere::PointLocation":
        SPH_NO_BOUNDARY "arr2d::sphere::NO_BOUNDARY"
        SPH_MIN_BOUNDARY "arr2d::sphere::MIN_BOUNDARY"
        SPH_MID_BOUNDARY "arr2d::sphere::MID_BOUNDARY"
        SPH_MAX_BOUNDARY "arr2d::sphere::MAX_BOUNDARY"
    Geom sphere_make_point "arr2d::sphere::make_point"(const Rational& x, const Rational& y, const Rational& z) except +arr2d_translate_exception
    void sphere_point_xyz "arr2d::sphere::point_xyz"(const Geom& p, Rational& x, Rational& y, Rational& z) except +arr2d_translate_exception
    int sphere_point_location "arr2d::sphere::point_location"(const Geom& p) except +arr2d_translate_exception
    Geom sphere_make_arc "arr2d::sphere::make_arc"(const Geom& p, const Geom& q) except +arr2d_translate_exception
    Geom sphere_make_arc_with_normal "arr2d::sphere::make_arc_with_normal"(const Geom& p, const Geom& q, const Geom& normal) except +arr2d_translate_exception
    Geom sphere_make_full_circle "arr2d::sphere::make_full_circle"(const Geom& normal) except +arr2d_translate_exception
    Geom sphere_make_x_monotone_arc "arr2d::sphere::make_x_monotone_arc"(const Geom& p, const Geom& q) except +arr2d_translate_exception
    cbool sphere_is_full "arr2d::sphere::is_full"(const Geom& c) except +arr2d_translate_exception
    cbool sphere_is_vertical "arr2d::sphere::is_vertical"(const Geom& c) except +arr2d_translate_exception
    cbool sphere_is_meridian "arr2d::sphere::is_meridian"(const Geom& c) except +arr2d_translate_exception
    cbool sphere_is_degenerate "arr2d::sphere::is_degenerate"(const Geom& c) except +arr2d_translate_exception
    Geom sphere_normal "arr2d::sphere::normal"(const Geom& c) except +arr2d_translate_exception


cdef extern from "arr2d/arrangement.hpp" namespace "arr2d":
    cdef enum class ObsEvent "arr2d::ObsEvent":
        BeforeAssign "arr2d::ObsEvent::BeforeAssign"
        NumEvents "arr2d::ObsEvent::NumEvents"
    const char* obs_event_name(ObsEvent e)      # snake_case name, e.g. "after_split_face" -> dispatch by name

    cdef cppclass ObsEventData:
        ObsEvent event
        VH v1
        VH v2
        HH h1
        HH h2
        HH h3
        FH f1
        FH f2
        cbool flag
        int i1
        int i2
        int i3
        const Geom* g1
        const Geom* g2

    ctypedef void (*ObserverFn)(void* user, const ObsEventData& ev)

    cdef enum class OverlayEvent "arr2d::OverlayEvent":
        VertexVertex "arr2d::OverlayEvent::VertexVertex"
        VertexEdge "arr2d::OverlayEvent::VertexEdge"
        VertexFace "arr2d::OverlayEvent::VertexFace"
        EdgeVertex "arr2d::OverlayEvent::EdgeVertex"
        FaceVertex "arr2d::OverlayEvent::FaceVertex"
        EdgeEdgeVertex "arr2d::OverlayEvent::EdgeEdgeVertex"
        EdgeEdge "arr2d::OverlayEvent::EdgeEdge"
        EdgeFace "arr2d::OverlayEvent::EdgeFace"
        FaceEdge "arr2d::OverlayEvent::FaceEdge"
        FaceFace "arr2d::OverlayEvent::FaceFace"

    cdef cppclass OverlayEventData:
        OverlayEvent event
        void* a
        uint64_t a_id
        void* b
        uint64_t b_id
        void* r
        uint64_t r_id

    ctypedef void (*OverlayFn)(void* user, const OverlayEventData& ev)

    cdef cppclass VerticalDecompositionEntry:
        VH v
        Located below
        Located above

    cdef cppclass ArrBase:
        Kind kind()
        const KindOps& ops()
        cbool is_unbounded_kind()
        size_t number_of_vertices() except +arr2d_translate_exception
        size_t number_of_isolated_vertices() except +arr2d_translate_exception
        size_t number_of_vertices_at_infinity() except +arr2d_translate_exception
        size_t number_of_halfedges() except +arr2d_translate_exception
        size_t number_of_edges() except +arr2d_translate_exception
        size_t number_of_faces() except +arr2d_translate_exception
        size_t number_of_unbounded_faces() except +arr2d_translate_exception
        size_t number_of_curves() except +arr2d_translate_exception
        cbool is_empty() except +arr2d_translate_exception
        cbool is_valid() except +arr2d_translate_exception
        void clear() except +arr2d_translate_exception
        unique_ptr[ArrBase] clone() except +arr2d_translate_exception
        void assign(const ArrBase& other) except +arr2d_translate_exception
        void vertices(vector[VH]& out) except +arr2d_translate_exception
        void halfedges(vector[HH]& out) except +arr2d_translate_exception
        void edges(vector[HH]& out) except +arr2d_translate_exception
        void faces(vector[FH]& out) except +arr2d_translate_exception
        void unbounded_faces(vector[FH]& out) except +arr2d_translate_exception
        void curves(vector[CH]& out) except +arr2d_translate_exception
        FH unbounded_face() except +arr2d_translate_exception
        FH fictitious_face() except +arr2d_translate_exception
        cbool vertex_valid(VH v)
        cbool halfedge_valid(HH h)
        cbool face_valid(FH f)
        cbool curve_valid(CH c)
        Geom vertex_point(VH v) except +arr2d_translate_exception
        size_t vertex_degree(VH v) except +arr2d_translate_exception
        cbool vertex_is_isolated(VH v) except +arr2d_translate_exception
        FH vertex_face(VH v) except +arr2d_translate_exception
        void vertex_incident_halfedges(VH v, vector[HH]& out) except +arr2d_translate_exception
        cbool vertex_is_at_open_boundary(VH v) except +arr2d_translate_exception
        int vertex_parameter_space_in_x(VH v) except +arr2d_translate_exception
        int vertex_parameter_space_in_y(VH v) except +arr2d_translate_exception
        PyRef& vertex_data(VH v) except +arr2d_translate_exception
        VH he_source(HH h) except +arr2d_translate_exception
        VH he_target(HH h) except +arr2d_translate_exception
        HH he_twin(HH h) except +arr2d_translate_exception
        HH he_next(HH h) except +arr2d_translate_exception
        HH he_prev(HH h) except +arr2d_translate_exception
        FH he_face(HH h) except +arr2d_translate_exception
        Geom he_curve(HH h) except +arr2d_translate_exception
        Geom he_directed_curve(HH h) except +arr2d_translate_exception
        int he_direction(HH h) except +arr2d_translate_exception
        cbool he_is_fictitious(HH h) except +arr2d_translate_exception
        cbool he_is_on_inner_ccb(HH h) except +arr2d_translate_exception
        cbool he_is_on_outer_ccb(HH h) except +arr2d_translate_exception
        void he_ccb(HH h, vector[HH]& out) except +arr2d_translate_exception
        PyRef& he_data(HH h) except +arr2d_translate_exception
        cbool face_is_unbounded(FH f) except +arr2d_translate_exception
        cbool face_is_fictitious(FH f) except +arr2d_translate_exception
        cbool face_has_outer_ccb(FH f) except +arr2d_translate_exception
        size_t face_number_of_outer_ccbs(FH f) except +arr2d_translate_exception
        size_t face_number_of_inner_ccbs(FH f) except +arr2d_translate_exception
        size_t face_number_of_isolated_vertices(FH f) except +arr2d_translate_exception
        HH face_outer_ccb(FH f) except +arr2d_translate_exception
        void face_outer_ccbs(FH f, vector[HH]& out) except +arr2d_translate_exception
        void face_inner_ccbs(FH f, vector[HH]& out) except +arr2d_translate_exception
        void face_isolated_vertices(FH f, vector[VH]& out) except +arr2d_translate_exception
        PyRef& face_data(FH f) except +arr2d_translate_exception
        void face_polygon(FH f, vector[Geom]& outer, vector[vector[Geom]]& holes) except +arr2d_translate_exception
        VH insert_point_in_face_interior(const Geom& p, FH f) except +arr2d_translate_exception
        HH insert_in_face_interior(const Geom& xc, FH f) except +arr2d_translate_exception
        HH insert_from_left_vertex(const Geom& xc, VH v) except +arr2d_translate_exception
        HH insert_from_right_vertex(const Geom& xc, VH v) except +arr2d_translate_exception
        HH insert_at_vertices(const Geom& xc, VH v1, VH v2) except +arr2d_translate_exception
        VH modify_vertex(VH v, const Geom& p) except +arr2d_translate_exception
        FH remove_isolated_vertex(VH v) except +arr2d_translate_exception
        HH modify_edge(HH h, const Geom& xc) except +arr2d_translate_exception
        HH split_edge(HH h, const Geom& xc1, const Geom& xc2) except +arr2d_translate_exception
        HH merge_edge(HH h1, HH h2, const Geom& xc) except +arr2d_translate_exception
        FH remove_edge(HH h, cbool remove_source, cbool remove_target) except +arr2d_translate_exception
        CH insert_curve(const Geom& c) except +arr2d_translate_exception
        void insert_curves(const vector[Geom]& cs, vector[CH]& out) except +arr2d_translate_exception
        HH insert_non_intersecting_curve(const Geom& xc) except +arr2d_translate_exception
        void insert_non_intersecting_curves(const vector[Geom]& xcs) except +arr2d_translate_exception
        VH insert_point(const Geom& p) except +arr2d_translate_exception
        cbool remove_vertex(VH v) except +arr2d_translate_exception
        size_t remove_curve(CH c) except +arr2d_translate_exception
        HH split_edge_at_point(HH h, const Geom& p) except +arr2d_translate_exception
        HH merge_edge_history(HH h1, HH h2) except +arr2d_translate_exception
        Geom curve_geometry(CH c) except +arr2d_translate_exception
        size_t number_of_induced_edges(CH c) except +arr2d_translate_exception
        void induced_edges(CH c, vector[HH]& out) except +arr2d_translate_exception
        size_t number_of_originating_curves(HH h) except +arr2d_translate_exception
        void originating_curves(HH h, vector[CH]& out) except +arr2d_translate_exception
        Located locate(const Geom& p, int strategy) except +arr2d_translate_exception
        Located ray_shoot_up(const Geom& p, int strategy) except +arr2d_translate_exception
        Located ray_shoot_down(const Geom& p, int strategy) except +arr2d_translate_exception
        void batched_locate(const vector[Geom]& pts, vector[Located]& out) except +arr2d_translate_exception
        cbool supports_point_location(int strategy) except +arr2d_translate_exception
        void attach_point_location(int strategy) except +arr2d_translate_exception
        void detach_point_location(int strategy) except +arr2d_translate_exception
        cbool has_point_location(int strategy) except +arr2d_translate_exception
        void zone(const Geom& c, vector[Located]& out) except +arr2d_translate_exception
        cbool do_intersect(const Geom& c) except +arr2d_translate_exception
        void decompose(vector[VerticalDecompositionEntry]& out) except +arr2d_translate_exception
        int add_observer(void* user, ObserverFn fn) except +arr2d_translate_exception
        void remove_observer(int token) except +arr2d_translate_exception
        void overlay_with(const ArrBase& other, ArrBase& result, void* user, OverlayFn fn) except +arr2d_translate_exception
        void vertex_coordinates(vector[double]& out) except +arr2d_translate_exception
        void edge_vertex_indices(vector[size_t]& out) except +arr2d_translate_exception
        void face_boundaries(vector[vector[vector[size_t]]]& out) except +arr2d_translate_exception
        BBox bbox() except +arr2d_translate_exception

    void overlay(const ArrBase& a, const ArrBase& b, ArrBase& r, void* user, OverlayFn fn) except +arr2d_translate_exception


cdef extern from "arr2d/polygon_set.hpp" namespace "arr2d":
    cdef cppclass PolygonGeom:
        vector[Geom] outer
        vector[vector[Geom]] holes
        cbool unbounded
        PolygonGeom()

    cdef cppclass PolygonSetBase:
        Kind kind()
        unique_ptr[PolygonSetBase] clone() except +arr2d_translate_exception
        void clear() except +arr2d_translate_exception
        int orientation(const vector[Geom]& boundary) except +arr2d_translate_exception
        cbool is_valid_polygon(const PolygonGeom& p) except +arr2d_translate_exception
        cbool is_closed_chain(const vector[Geom]& boundary) except +arr2d_translate_exception
        void insert(const PolygonGeom& p) except +arr2d_translate_exception
        void insert_polygons(const vector[PolygonGeom]& ps) except +arr2d_translate_exception
        void join(const PolygonSetBase& other) except +arr2d_translate_exception
        void intersection(const PolygonSetBase& other) except +arr2d_translate_exception
        void difference(const PolygonSetBase& other) except +arr2d_translate_exception
        void symmetric_difference(const PolygonSetBase& other) except +arr2d_translate_exception
        void complement() except +arr2d_translate_exception
        void join_polygon(const PolygonGeom& p) except +arr2d_translate_exception
        void intersection_polygon(const PolygonGeom& p) except +arr2d_translate_exception
        void difference_polygon(const PolygonGeom& p) except +arr2d_translate_exception
        void symmetric_difference_polygon(const PolygonGeom& p) except +arr2d_translate_exception
        size_t number_of_polygons_with_holes() except +arr2d_translate_exception
        cbool is_empty() except +arr2d_translate_exception
        cbool is_plane() except +arr2d_translate_exception
        void polygons_with_holes(vector[PolygonGeom]& out) except +arr2d_translate_exception
        int oriented_side(const Geom& point) except +arr2d_translate_exception
        int oriented_side_of_set(const PolygonSetBase& other) except +arr2d_translate_exception
        cbool locate(const Geom& point, PolygonGeom& out) except +arr2d_translate_exception
        cbool do_intersect(const PolygonSetBase& other) except +arr2d_translate_exception
        cbool is_valid() except +arr2d_translate_exception
        unique_ptr[ArrBase] to_arrangement(vector[FH]& contained) except +arr2d_translate_exception
        size_t arrangement_number_of_faces() except +arr2d_translate_exception
        size_t arrangement_number_of_edges() except +arr2d_translate_exception


cdef extern from "arr2d/registry.hpp" namespace "arr2d":
    void init_all_kinds() except +arr2d_translate_exception
    cbool kind_available(Kind k)
    const KindOps& ops(Kind k) except +arr2d_translate_exception
    unique_ptr[ArrBase] make_arrangement(Kind k) except +arr2d_translate_exception
    unique_ptr[PolygonSetBase] make_polygon_set(Kind k) except +arr2d_translate_exception
    cbool kind_has_polygon_set(Kind k)
    string cgal_version()
    string build_info()


# ---------------------------------------------------------------------------
# Shared Cython-level conventions used by the .pxi parts of _core.pyx
# ---------------------------------------------------------------------------
cdef class Point:
    cdef Geom g

cdef class Curve:
    cdef Geom g

cdef class Arrangement:
    cdef unique_ptr[ArrBase] arr
    cdef Kind _kind
    cdef object _observers          # dict token -> Python observer
    cdef object _pending            # pending exception info from callbacks (or None)
    cdef object __weakref__

cdef class Vertex:
    cdef Arrangement arr
    cdef VH h

cdef class Halfedge:
    cdef Arrangement arr
    cdef HH h

cdef class Face:
    cdef Arrangement arr
    cdef FH h

cdef class CurveHandle:
    cdef Arrangement arr
    cdef CH h

cdef class PolygonSet:
    cdef unique_ptr[PolygonSetBase] ps
    cdef Kind _kind
