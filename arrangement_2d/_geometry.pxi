# -*- coding: utf-8 -*-
# _geometry.pxi — Python geometry classes on top of the type-erased arr2d core.
#
# Included by _core.pyx AFTER _errors.pxi and _numbers.pxi and BEFORE _arrangement.pxi
# and _polygon_set.pxi.  Everything here is either a `cdef class` (Point and Curve are
# declared in _core.pxd; the rest are local to the module) or a module-level helper used
# by the other .pxi parts.
#
# Design notes
#   * Every object is an immutable box around one `arr2d::Geom` (kind + Point/Curve/XCurve
#     + shared_ptr to the concrete CGAL object).  Nothing here owns CGAL types directly.
#   * Kind-specific constructors are the free functions of ops.hpp (segment_make, cs_make_arc,
#     bezier_make, conic_make_arc, sphere_make_point, ...); everything generic goes through
#     `ops(kind)` (the per-kind KindOps virtual table).
#   * All C++ entry points are declared `except +arr2d_translate_exception`, so an
#     arr2d::Error or a CGAL failure becomes the right Python exception automatically.
#     We only raise Python errors for things the core cannot see (wrong Python types,
#     wrong tuple lengths, mutually exclusive keyword arguments, ...).

import math as _math
from fractions import Fraction as _Fraction


# ===========================================================================
# Kind constants and small conversions
# ===========================================================================

cdef Kind _K_SEGMENT = <Kind>0
cdef Kind _K_LINEAR = <Kind>1
cdef Kind _K_CIRCLE_SEGMENT = <Kind>2
cdef Kind _K_POLYLINE = <Kind>3
cdef Kind _K_BEZIER = <Kind>4
cdef Kind _K_CONIC = <Kind>5
cdef Kind _K_SPHERE = <Kind>6

# GeomType values (arr2d::GeomType): 0 Point, 1 Curve, 2 XCurve, 3 Number.
cdef int _GT_POINT = 0
cdef int _GT_CURVE = 1
cdef int _GT_XCURVE = 2
cdef int _GT_NUMBER = 3

cdef object _KIND_ENUM = None


cdef object _pykind(Kind k):
    """The public ``Kind`` enum member for a C++ ``arr2d::Kind`` value."""
    global _KIND_ENUM
    if _KIND_ENUM is None:
        # `Kind` is the C++ enum at compile time; the public IntEnum lives in the module dict.
        _KIND_ENUM = globals()["Kind"]
    return _KIND_ENUM(<int>k)


cdef inline bint _is_point_geom(const Geom& g):
    return <int>g.type == _GT_POINT


cdef inline bint _is_xcurve_geom(const Geom& g):
    return <int>g.type == _GT_XCURVE


cdef object _bbox_tuple(const BBox& b):
    """(xmin, ymin, xmax, ymax) or (xmin, ymin, zmin, xmax, ymax, zmax)."""
    if b.dim == 3:
        return (b.lo[0], b.lo[1], b.lo[2], b.hi[0], b.hi[1], b.hi[2])
    return (b.lo[0], b.lo[1], b.hi[0], b.hi[1])


cdef BBox _as_bbox(object b) except *:
    """Convert a (xmin, ymin, xmax, ymax) / (xmin, ymin, zmin, xmax, ymax, zmax) tuple."""
    cdef BBox out
    cdef Py_ssize_t n
    cdef list vals
    try:
        vals = [float(v) for v in b]
    except TypeError:
        raise TypeError("bbox must be a sequence of 4 (planar) or 6 (sphere) numbers, "
                        "got %r" % (type(b).__name__,))
    n = len(vals)
    out.lo[0] = 0.0; out.lo[1] = 0.0; out.lo[2] = 0.0
    out.hi[0] = 0.0; out.hi[1] = 0.0; out.hi[2] = 0.0
    if n == 4:
        out.dim = 2
        out.lo[0] = vals[0]; out.lo[1] = vals[1]
        out.hi[0] = vals[2]; out.hi[1] = vals[3]
    elif n == 6:
        out.dim = 3
        out.lo[0] = vals[0]; out.lo[1] = vals[1]; out.lo[2] = vals[2]
        out.hi[0] = vals[3]; out.hi[1] = vals[4]; out.hi[2] = vals[5]
    else:
        raise ValueError("bbox must be (xmin, ymin, xmax, ymax) or "
                         "(xmin, ymin, zmin, xmax, ymax, zmax), got %d values" % (n,))
    return out


cdef SqrtExt _as_sqrt_ext(object v) except *:
    """A ``SqrtExt`` (a + b*sqrt(c)) from a SqrtExtension or from any rational number."""
    cdef SqrtExt s
    s.a = _to_rational(0)
    s.b = _to_rational(0)
    s.c = _to_rational(0)
    if isinstance(v, SqrtExtension):
        s.a = _to_rational(v.a)
        s.b = _to_rational(v.b)
        s.c = _to_rational(v.c)
    else:
        s.a = _to_rational(v)
    return s


cdef Geom _as_number_geom(object v) except *:
    """Box a Python number / Algebraic / SqrtExtension as an arr2d Number geometry."""
    cdef Algebraic a
    if isinstance(v, Algebraic):
        a = <Algebraic>v
        return a.g
    if isinstance(v, SqrtExtension):
        return box_sqrt_ext(_as_sqrt_ext(v))
    return box_rational(_to_rational(v))


cdef Rational _squared(object v) except *:
    """v*v as an exact Rational (v may be int/float/Fraction/str/...)."""
    f = _fraction(_to_rational(v))
    return _to_rational(f * f)


cdef vector[Rational] _rational_xy(object obj) except *:
    """Exact rational coordinates of a point-like object (Point of any kind, or a sequence)."""
    cdef vector[Rational] out
    cdef Point p
    if isinstance(obj, Point):
        p = <Point>obj
        ops(p.g.kind).point_exact_rational(p.g, out)
        return out
    if isinstance(obj, (str, bytes, bytearray)):
        raise TypeError("a point cannot be built from a string")
    for v in obj:
        out.push_back(_to_rational(v))
    return out


# ===========================================================================
# Boxing helpers used by every other .pxi part
# ===========================================================================

cdef Point _wrap_point(const Geom& g):
    """Wrap a Point-typed Geom in a :class:`Point` (no copy of the CGAL object)."""
    cdef Point p = Point.__new__(Point)
    p.g = g
    return p


cdef Curve _wrap_curve(const Geom& g):
    """Wrap a Curve/XCurve-typed Geom in the :class:`Curve` subclass of its kind."""
    cdef int k = <int>g.kind
    cdef Curve c
    if k == 0:
        c = Segment.__new__(Segment)
    elif k == 1:
        c = LinearCurve.__new__(LinearCurve)
    elif k == 2:
        c = CircleSegment.__new__(CircleSegment)
    elif k == 3:
        c = Polyline.__new__(Polyline)
    elif k == 4:
        c = BezierCurve.__new__(BezierCurve)
    elif k == 5:
        c = ConicArc.__new__(ConicArc)
    elif k == 6:
        c = GeodesicArc.__new__(GeodesicArc)
    else:
        c = Curve.__new__(Curve)
    c.g = g
    return c


cdef object _wrap_geom(const Geom& g):
    """Wrap any Geom: Point / Curve / number."""
    if <int>g.type == _GT_POINT:
        return _wrap_point(g)
    if <int>g.type == _GT_NUMBER:
        return _wrap_number(g)
    return _wrap_curve(g)


cdef Geom _as_point(object obj, Kind kind) except *:
    """Coerce `obj` to a Point-typed Geom of `kind`.

    Accepts a :class:`Point` of any kind (converted with ``ops(kind).convert_point``) or a
    sequence of 2 numbers (3 for the sphere kind).
    """
    cdef Point p
    cdef vector[Rational] c
    cdef int dim
    cdef Py_ssize_t n
    if isinstance(obj, Point):
        p = <Point>obj
        if <int>p.g.kind == <int>kind:
            return p.g
        return ops(kind).convert_point(p.g)
    if isinstance(obj, (Curve, Polygon, PolygonWithHoles, str, bytes, bytearray)):
        raise TypeError("expected a point, got a %s" % (type(obj).__name__,))
    dim = ops(kind).dimension()
    try:
        n = len(obj)
    except TypeError:
        raise TypeError("cannot interpret %r as a point of kind '%s'; give a Point or a "
                        "sequence of %d numbers" % (type(obj).__name__, kind_name(kind), dim))
    if n != <Py_ssize_t>dim:
        raise ValueError("points of kind '%s' need %d coordinates, got %d"
                         % (kind_name(kind), dim, n))
    c = _rational_xy(obj)
    if dim == 3:
        return ops(kind).make_point_3(c[0], c[1], c[2])
    return ops(kind).make_point(c[0], c[1])


cdef Geom _as_curve(object obj, Kind kind, bint x_monotone) except *:
    """Coerce a :class:`Curve` to a Geom of `kind`, optionally promoted to x-monotone.

    Raises ValueError when the conversion is exact but yields more than one curve
    (use :meth:`Curve.to_kind` for that case).
    """
    cdef Curve c
    cdef vector[Geom] out
    cdef Geom g
    if not isinstance(obj, Curve):
        raise TypeError("expected a Curve, got %s" % (type(obj).__name__,))
    c = <Curve>obj
    if <int>c.g.kind == <int>kind:
        g = c.g
    else:
        ops(kind).convert_curve(c.g, out)
        if out.size() != 1:
            raise ValueError("converting a '%s' curve to kind '%s' yields %d curves; "
                             "use .to_kind() and handle the pieces"
                             % (kind_name(c.g.kind), kind_name(kind), <int>out.size()))
        g = out[0]
    if x_monotone:
        g = ops(kind).to_x_monotone(g)
    return g


cdef int _collect_curves(list out, object obj, Kind kind, int depth) except -1:
    cdef Curve c
    cdef vector[Geom] conv
    cdef size_t i
    cdef Polygon poly
    cdef PolygonWithHoles pwh
    if isinstance(obj, Curve):
        c = <Curve>obj
        if <int>c.g.kind == <int>kind:
            out.append(c)
        else:
            ops(kind).convert_curve(c.g, conv)
            for i in range(conv.size()):
                out.append(_wrap_curve(conv[i]))
        return 0
    if isinstance(obj, Polygon):
        poly = <Polygon>obj
        for sub in poly._curves:
            _collect_curves(out, sub, kind, depth + 1)
        return 0
    if isinstance(obj, PolygonWithHoles):
        pwh = <PolygonWithHoles>obj
        if pwh._outer is not None:
            _collect_curves(out, pwh._outer, kind, depth + 1)
        for h in pwh._holes:
            _collect_curves(out, h, kind, depth + 1)
        return 0
    if isinstance(obj, (str, bytes, bytearray)) or isinstance(obj, Point):
        raise TypeError("expected a curve, a polygon or an iterable of those, got %s"
                        % (type(obj).__name__,))
    if depth > 8:
        raise TypeError("curve container nested too deeply")
    try:
        it = iter(obj)
    except TypeError:
        raise TypeError("expected a curve, a polygon or an iterable of those, got %s"
                        % (type(obj).__name__,))
    for item in it:
        _collect_curves(out, item, kind, depth + 1)
    return 0


cdef list _convert_curves(object obj, Kind kind):
    """A flat list of :class:`Curve` objects of `kind`.

    `obj` may be a Curve, a Polygon / PolygonWithHoles (their boundary curves) or any
    (possibly nested) iterable of those.  Curves that are not x-monotone are kept as
    general curve boxes.
    """
    cdef list out = []
    _collect_curves(out, obj, kind, 0)
    return out


# ===========================================================================
# Point
# ===========================================================================

cdef class Point:
    """A point of one geometry kind.

    ``Point(x, y)`` builds a rational point of kind ``SEGMENT``; ``Point(x, y, z)`` a
    direction on the unit sphere (kind ``SPHERE``); ``Point(x, y, kind=...)`` a point of
    any planar kind.  Coordinates accept ``int`` / ``float`` / ``Fraction`` / ``Decimal`` /
    ``"n/d"`` strings / numpy scalars — everything is converted exactly.

    Points are immutable; ``==`` is exact.
    """

    def __init__(self, x, y, z=None, kind=None):
        cdef Kind k
        cdef Rational rx, ry, rz
        if kind is None:
            if z is None:
                k = _K_SEGMENT
            else:
                k = _K_SPHERE
        else:
            k = _ckind(kind)
        rx = _to_rational(x)
        ry = _to_rational(y)
        if z is None:
            self.g = ops(k).make_point(rx, ry)
        else:
            rz = _to_rational(z)
            self.g = ops(k).make_point_3(rx, ry, rz)

    # ---------------------------------------------------------------- factories
    @classmethod
    def from_sqrt_extension(cls, x, y):
        """A circle-segment point with sqrt-extension coordinates.

        `x` and `y` are :class:`SqrtExtension` objects (``a + b*sqrt(c)``) or plain
        rational numbers.  Returns a Point of kind ``CIRCLE_SEGMENT``.
        """
        cdef SqrtExt sx = _as_sqrt_ext(x)
        cdef SqrtExt sy = _as_sqrt_ext(y)
        return _wrap_point(cs_make_point_sqrt(sx, sy))

    @classmethod
    def from_algebraic(cls, x, y):
        """A conic point with algebraic coordinates.

        `x` and `y` may be :class:`Algebraic`, :class:`SqrtExtension`, ``Fraction`` or any
        rational number.  Returns a Point of kind ``CONIC``.
        """
        cdef Geom gx = _as_number_geom(x)
        cdef Geom gy = _as_number_geom(y)
        return _wrap_point(conic_make_point_algebraic(gx, gy))

    # ---------------------------------------------------------------- basics
    @property
    def kind(self):
        """The geometry :class:`Kind` of this point."""
        return _pykind(self.g.kind)

    @property
    def dimension(self):
        """2 for planar kinds, 3 for the sphere kind (points are directions)."""
        return ops(self.g.kind).dimension()

    @property
    def approx(self):
        """Approximate coordinates as a tuple of floats (2 or 3 values)."""
        cdef double xyz[3]
        cdef int d = ops(self.g.kind).dimension()
        xyz[0] = 0.0
        xyz[1] = 0.0
        xyz[2] = 0.0
        ops(self.g.kind).point_approx(self.g, xyz)
        if d == 3:
            return (xyz[0], xyz[1], xyz[2])
        return (xyz[0], xyz[1])

    @property
    def x(self):
        """Approximate x coordinate (float)."""
        return self.approx[0]

    @property
    def y(self):
        """Approximate y coordinate (float)."""
        return self.approx[1]

    @property
    def z(self):
        """Approximate z coordinate (float); sphere kind only."""
        cdef int d = ops(self.g.kind).dimension()
        if d < 3:
            raise AttributeError("points of kind '%s' are 2-dimensional and have no z "
                                 "coordinate" % (kind_name(self.g.kind),))
        return self.approx[2]

    @property
    def xy(self):
        """``(x, y)`` as floats."""
        a = self.approx
        return (a[0], a[1])

    @property
    def xyz(self):
        """``(x, y, z)`` as floats; sphere kind only."""
        cdef int d = ops(self.g.kind).dimension()
        if d < 3:
            raise AttributeError("points of kind '%s' are 2-dimensional; use .xy"
                                 % (kind_name(self.g.kind),))
        return self.approx

    @property
    def is_rational(self):
        """True when every coordinate is exactly representable as a ``Fraction``."""
        return ops(self.g.kind).point_is_rational(self.g)

    @property
    def location(self):
        """Sphere kind only: ``"interior"``, ``"min_boundary"``, ``"mid_boundary"`` or
        ``"max_boundary"`` (CGAL ``Arr_extended_direction_3::Location_type``)."""
        cdef int loc
        if <int>self.g.kind != <int>_K_SPHERE:
            raise AttributeError("'location' is only defined for points of kind 'sphere', "
                                 "not '%s'" % (kind_name(self.g.kind),))
        loc = sphere_point_location(self.g)
        if loc == 1:
            return "min_boundary"
        if loc == 2:
            return "mid_boundary"
        if loc == 3:
            return "max_boundary"
        return "interior"

    # ---------------------------------------------------------------- exact values
    def exact(self):
        """Exact coordinates as a tuple of ``Fraction`` / :class:`SqrtExtension` /
        :class:`Algebraic`."""
        cdef vector[Geom] nums
        cdef size_t i
        ops(self.g.kind).point_exact(self.g, nums)
        return tuple([_wrap_number(nums[i]) for i in range(nums.size())])

    def exact_rational(self):
        """Exact coordinates as a tuple of ``Fraction``.

        Raises :class:`NotRepresentableError` when a coordinate is irrational.
        """
        cdef vector[Rational] rs
        cdef size_t i
        ops(self.g.kind).point_exact_rational(self.g, rs)
        return tuple([_fraction(rs[i]) for i in range(rs.size())])

    def interval(self, int bits=53):
        """Certified enclosing intervals, one ``(lo, hi)`` pair per coordinate.

        `bits` is the requested relative precision; it only has an effect for algebraic
        coordinates (values above 53 force the slower exact path).
        """
        cdef vector[pair[double, double]] iv
        cdef vector[Geom] nums
        cdef pair[double, double] pv
        cdef size_t i
        cdef list out
        if bits <= 53:
            ops(self.g.kind).point_interval(self.g, iv)
            return tuple([(iv[i].first, iv[i].second) for i in range(iv.size())])
        ops(self.g.kind).point_exact(self.g, nums)
        out = []
        for i in range(nums.size()):
            pv = number_interval(nums[i], bits)
            out.append((pv.first, pv.second))
        return tuple(out)

    # ---------------------------------------------------------------- conversion / comparison
    def to_kind(self, kind):
        """This point converted to another geometry kind (exact; may raise
        :class:`NotRepresentableError`)."""
        cdef Kind k = _ckind(kind)
        if <int>k == <int>self.g.kind:
            return self
        return _wrap_point(ops(k).convert_point(self.g))

    def compare_xy(self, other):
        """Lexicographic comparison (traits ``Compare_xy_2``): -1, 0 or +1."""
        cdef Geom q = _as_point(other, self.g.kind)
        return ops(self.g.kind).point_compare_xy(self.g, q)

    def compare_x(self, other):
        """Comparison of the x coordinates only (traits ``Compare_x_2``): -1, 0 or +1."""
        cdef Geom q = _as_point(other, self.g.kind)
        return ops(self.g.kind).point_compare_x(self.g, q)

    # ---------------------------------------------------------------- protocol
    def __eq__(self, other):
        cdef Point o
        cdef vector[Rational] a
        cdef vector[Rational] b
        cdef size_t i
        if not isinstance(other, Point):
            return NotImplemented
        o = <Point>other
        if <int>self.g.kind == <int>o.g.kind:
            return ops(self.g.kind).point_equal(self.g, o.g)
        if not ops(self.g.kind).point_is_rational(self.g):
            return NotImplemented
        if not ops(o.g.kind).point_is_rational(o.g):
            return NotImplemented
        ops(self.g.kind).point_exact_rational(self.g, a)
        ops(o.g.kind).point_exact_rational(o.g, b)
        if a.size() != b.size():
            return False
        for i in range(a.size()):
            if rational_compare(a[i], b[i]) != 0:
                return False
        return True

    def __ne__(self, other):
        r = self.__eq__(other)
        if r is NotImplemented:
            return r
        return not r

    def __hash__(self):
        cdef vector[Rational] rs
        cdef size_t i
        if not ops(self.g.kind).point_is_rational(self.g):
            raise TypeError("only points with rational coordinates are hashable; this "
                            "point of kind '%s' has irrational coordinates"
                            % (kind_name(self.g.kind),))
        ops(self.g.kind).point_exact_rational(self.g, rs)
        return hash(tuple([_fraction(rs[i]) for i in range(rs.size())]))

    def __len__(self):
        return ops(self.g.kind).dimension()

    def __iter__(self):
        return iter(self.approx)

    def __getitem__(self, i):
        return self.approx[i]

    def __repr__(self):
        if self.g.empty():
            return "<Point uninitialised>"
        return ops(self.g.kind).point_repr(self.g)


# ===========================================================================
# Curve (abstract base)
# ===========================================================================

cdef Geom _xg(Curve c) except *:
    """`c` as an x-monotone Geom (raises NotXMonotoneError when it is not x-monotone).

    Curve is declared in the fixed _core.pxd, so this cannot be a cdef method of it.
    """
    if <int>c.g.type == _GT_XCURVE:
        return c.g
    return ops(c.g.kind).to_x_monotone(c.g)


cdef class Curve:
    """Base class of every curve.  Never instantiated directly.

    A curve box holds either a general ``Curve_2`` or an ``X_monotone_curve_2``; the
    x-monotone accessors (``source``, ``target``, ``left``, ``right``, ``split``,
    ``intersect``, ...) promote a general curve automatically and raise
    :class:`NotXMonotoneError` when it is not x-monotone.
    """

    # NOTE: this signature must NOT be (*args, **kwargs).  Cython 3.3.0 only emits the
    # vectorcall tp_new helper (__pyx_tp_new_vectorcall_..._Curve) for a cdef class whose
    # __init__ has a fixed signature, yet every subclass with a fixed-signature __init__
    # calls the *base* helper -> "use of undeclared identifier" at C++ compile time.
    def __init__(self):
        raise TypeError("Curve is an abstract base class; construct a Segment, "
                        "LinearCurve, CircleSegment, Polyline, BezierCurve, ConicArc "
                        "or GeodesicArc instead")

    # ---------------------------------------------------------------- basics
    @property
    def kind(self):
        """The geometry :class:`Kind` of this curve."""
        return _pykind(self.g.kind)

    @property
    def is_x_monotone(self):
        """True when this box holds an ``X_monotone_curve_2``."""
        return <int>self.g.type == _GT_XCURVE

    @property
    def dimension(self):
        """2 for planar kinds, 3 for the sphere kind."""
        return ops(self.g.kind).dimension()

    @property
    def type_name(self):
        """``"curve"`` or ``"x_monotone_curve"``."""
        if <int>self.g.type == _GT_XCURVE:
            return "x_monotone_curve"
        return "curve"

    def make_x_monotone(self):
        """Subdivide into x-monotone curves and isolated points (traits
        ``Make_x_monotone_2``); returns a list of :class:`Curve` and :class:`Point`."""
        cdef vector[Geom] out
        cdef size_t i
        ops(self.g.kind).make_x_monotone(self.g, out)
        return [_wrap_geom(out[i]) for i in range(out.size())]

    def x_monotone(self):
        """This curve promoted to a single x-monotone curve.

        Raises :class:`NotXMonotoneError` when it splits into several pieces.
        """
        return _wrap_curve(_xg(self))

    def to_curve(self):
        """The general ``Curve_2`` behind this (x-monotone) curve."""
        return _wrap_curve(ops(self.g.kind).to_curve(self.g))

    # ---------------------------------------------------------------- endpoints
    @property
    def has_source(self):
        """False for the infinite end of a linear ray/line."""
        return ops(self.g.kind).xcurve_has_source(_xg(self))

    @property
    def has_target(self):
        """False for the infinite end of a linear ray/line."""
        return ops(self.g.kind).xcurve_has_target(_xg(self))

    @property
    def source(self):
        """The source endpoint as stored (x-monotone curves only)."""
        return _wrap_point(ops(self.g.kind).xcurve_source(_xg(self)))

    @property
    def target(self):
        """The target endpoint as stored (x-monotone curves only)."""
        return _wrap_point(ops(self.g.kind).xcurve_target(_xg(self)))

    @property
    def min_vertex(self):
        """The lexicographically smaller endpoint (traits ``Construct_min_vertex_2``)."""
        return _wrap_point(ops(self.g.kind).xcurve_min_vertex(_xg(self)))

    @property
    def max_vertex(self):
        """The lexicographically larger endpoint (traits ``Construct_max_vertex_2``)."""
        return _wrap_point(ops(self.g.kind).xcurve_max_vertex(_xg(self)))

    @property
    def left(self):
        """Alias of :attr:`min_vertex`."""
        return _wrap_point(ops(self.g.kind).xcurve_min_vertex(_xg(self)))

    @property
    def right(self):
        """Alias of :attr:`max_vertex`."""
        return _wrap_point(ops(self.g.kind).xcurve_max_vertex(_xg(self)))

    @property
    def is_vertical(self):
        """True for a vertical x-monotone curve."""
        return ops(self.g.kind).xcurve_is_vertical(_xg(self))

    @property
    def is_directed_right(self):
        """True when the source is lexicographically smaller than the target."""
        return ops(self.g.kind).xcurve_is_directed_right(_xg(self))

    def compare_endpoints_xy(self):
        """-1 when the curve is directed right (source < target), +1 otherwise."""
        return ops(self.g.kind).compare_endpoints_xy(_xg(self))

    def parameter_space_in_x(self, curve_end=1):
        """``Arr_parameter_space`` in x of one curve end (0 = min end, 1 = max end)."""
        return ops(self.g.kind).parameter_space_in_x(_xg(self), <int>curve_end)

    def parameter_space_in_y(self, curve_end=1):
        """``Arr_parameter_space`` in y of one curve end (0 = min end, 1 = max end)."""
        return ops(self.g.kind).parameter_space_in_y(_xg(self), <int>curve_end)

    # ---------------------------------------------------------------- shape
    @property
    def is_bounded(self):
        """False for linear rays and lines."""
        return ops(self.g.kind).curve_is_bounded(self.g)

    def bbox(self):
        """Approximate bounding box: ``(xmin, ymin, xmax, ymax)``
        (``(xmin, ymin, zmin, xmax, ymax, zmax)`` for the sphere kind)."""
        cdef BBox b = ops(self.g.kind).curve_bbox(self.g)
        return _bbox_tuple(b)

    def approximate(self, double tolerance=1e-3, bbox=None):
        """A polyline approximation as a list of coordinate tuples.

        `tolerance` is an absolute error in coordinate units.  `bbox` clips unbounded
        curves and is required for them.
        """
        cdef BBox clip
        cdef const BBox* cp = NULL
        cdef vector[double] out
        cdef int d
        cdef size_t n, i
        cdef list res
        if bbox is not None:
            clip = _as_bbox(bbox)
            cp = &clip
        elif not ops(self.g.kind).curve_is_bounded(self.g):
            raise ValueError("approximating an unbounded curve needs a clipping box: "
                             "pass bbox=(xmin, ymin, xmax, ymax)")
        ops(self.g.kind).approximate(self.g, tolerance, cp, out)
        d = ops(self.g.kind).dimension()
        n = out.size() // <size_t>d
        res = []
        if d == 3:
            for i in range(n):
                res.append((out[3 * i], out[3 * i + 1], out[3 * i + 2]))
        else:
            for i in range(n):
                res.append((out[2 * i], out[2 * i + 1]))
        return res

    def approximate_length(self, double tolerance=1e-3):
        """Approximate arc length (sum of the chords of ``approximate(tolerance)``)."""
        return ops(self.g.kind).approximate_length(self.g, tolerance)

    # ---------------------------------------------------------------- traits operations
    def opposite(self):
        """The same curve with the opposite direction."""
        return _wrap_curve(ops(self.g.kind).construct_opposite(_xg(self)))

    def split(self, point):
        """Split at an interior point; returns ``(left_part, right_part)``."""
        cdef Geom left
        cdef Geom right
        cdef Geom p = _as_point(point, self.g.kind)
        ops(self.g.kind).split(_xg(self), p, left, right)
        return (_wrap_curve(left), _wrap_curve(right))

    def trim(self, p, q):
        """The sub-curve between the two given points (traits ``Trim_2``)."""
        cdef Geom gp = _as_point(p, self.g.kind)
        cdef Geom gq = _as_point(q, self.g.kind)
        return _wrap_curve(ops(self.g.kind).trim(_xg(self), gp, gq))

    def can_merge(self, other):
        """True when ``merge(other)`` is allowed (traits ``Are_mergeable_2``)."""
        cdef Geom b = _as_curve(other, self.g.kind, True)
        return ops(self.g.kind).are_mergeable(_xg(self), b)

    def merge(self, other):
        """Merge two mergeable x-monotone curves into one."""
        cdef Geom b = _as_curve(other, self.g.kind, True)
        return _wrap_curve(ops(self.g.kind).merge(_xg(self), b))

    def intersect(self, other):
        """Intersections with another curve.

        Returns a list whose entries are ``(Point, multiplicity)`` tuples for transversal
        or tangential intersection points and :class:`Curve` objects for overlaps.
        A multiplicity of 0 means "unknown / not computed" (CGAL reports 0 for several
        traits, notably Bezier).
        """
        cdef Geom a = _xg(self)
        cdef Geom b = _as_curve(other, self.g.kind, True)
        cdef vector[IntersectionResult] res
        cdef size_t i
        cdef list out = []
        ops(self.g.kind).intersect(a, b, res)
        for i in range(res.size()):
            if res[i].is_point:
                out.append((_wrap_point(res[i].point), <int>res[i].multiplicity))
            else:
                out.append(_wrap_curve(res[i].overlap))
        return out

    def compare_y_at_x(self, point):
        """-1 when the point lies below the curve, 0 on it, +1 above."""
        cdef Geom p = _as_point(point, self.g.kind)
        return ops(self.g.kind).compare_y_at_x(p, _xg(self))

    def compare_y_at_x_left(self, other, point):
        """Vertical order of the two curves immediately to the left of their common point."""
        cdef Geom b = _as_curve(other, self.g.kind, True)
        cdef Geom p = _as_point(point, self.g.kind)
        return ops(self.g.kind).compare_y_at_x_left(_xg(self), b, p)

    def compare_y_at_x_right(self, other, point):
        """Vertical order of the two curves immediately to the right of their common point."""
        cdef Geom b = _as_curve(other, self.g.kind, True)
        cdef Geom p = _as_point(point, self.g.kind)
        return ops(self.g.kind).compare_y_at_x_right(_xg(self), b, p)

    def is_in_x_range(self, point):
        """True when the point's x coordinate lies in the curve's x range."""
        cdef Geom p = _as_point(point, self.g.kind)
        return ops(self.g.kind).is_in_x_range(_xg(self), p)

    def to_kind(self, kind):
        """This curve converted to another geometry kind.

        Returns a single :class:`Curve` when the conversion is one-to-one, otherwise a
        list of curves (e.g. a polyline converted to segments).
        """
        cdef Kind k = _ckind(kind)
        cdef vector[Geom] out
        cdef size_t i
        if <int>k == <int>self.g.kind:
            return self
        ops(k).convert_curve(self.g, out)
        if out.size() == 1:
            return _wrap_curve(out[0])
        return [_wrap_curve(out[i]) for i in range(out.size())]

    # ---------------------------------------------------------------- protocol
    def __eq__(self, other):
        cdef Curve o
        cdef Geom a
        cdef Geom b
        if not isinstance(other, Curve):
            return NotImplemented
        o = <Curve>other
        if <int>self.g.kind != <int>o.g.kind:
            return NotImplemented
        try:
            a = _xg(self)
            b = _xg(o)
        except NotXMonotoneError:
            return NotImplemented
        return ops(self.g.kind).curve_equal(a, b)

    def __ne__(self, other):
        r = self.__eq__(other)
        if r is NotImplemented:
            return r
        return not r

    def __hash__(self):
        raise TypeError("Curve objects are not hashable (curve equality is geometric, "
                        "not structural)")

    def __repr__(self):
        if self.g.empty():
            return "<%s uninitialised>" % (type(self).__name__,)
        return ops(self.g.kind).curve_repr(self.g)


# ===========================================================================
# Segment  (Kind.SEGMENT)
# ===========================================================================

cdef class Segment(Curve):
    """A straight line segment with rational endpoints (kind ``SEGMENT``)."""

    def __init__(self, p, q):
        cdef Geom a = _as_point(p, _K_SEGMENT)
        cdef Geom b = _as_point(q, _K_SEGMENT)
        self.g = segment_make(a, b)

    @classmethod
    def from_coordinates(cls, x1, y1, x2, y2):
        """``Segment((x1, y1), (x2, y2))`` without building the two Points."""
        return _wrap_curve(segment_make_xy(_to_rational(x1), _to_rational(y1),
                                          _to_rational(x2), _to_rational(y2)))

    @property
    def source(self):
        """The source endpoint."""
        cdef Geom s
        cdef Geom t
        segment_endpoints(self.g, s, t)
        return _wrap_point(s)

    @property
    def target(self):
        """The target endpoint."""
        cdef Geom s
        cdef Geom t
        segment_endpoints(self.g, s, t)
        return _wrap_point(t)

    @property
    def supporting_line(self):
        """``(a, b, c)`` as ``Fraction`` for the supporting line ``a*x + b*y + c = 0``."""
        cdef Rational a
        cdef Rational b
        cdef Rational c
        segment_supporting_line(self.g, a, b, c)
        return (_fraction(a), _fraction(b), _fraction(c))


# ===========================================================================
# LinearCurve  (Kind.LINEAR: segments, rays and lines)
# ===========================================================================

cdef class LinearCurve(Curve):
    """A segment, a ray or a full line (kind ``LINEAR``, unbounded planar topology).

    Build one with :meth:`segment`, :meth:`ray`, :meth:`ray_from_direction`,
    :meth:`line` or :meth:`line_from_coefficients` (or the module-level ``Line`` /
    ``Ray`` / ``line_from_coefficients`` helpers).
    """

    def __init__(self, *args, **kwargs):
        raise TypeError("LinearCurve has no public constructor; use LinearCurve.segment(p, q), "
                        ".ray(source, towards), .ray_from_direction(source, dx, dy), "
                        ".line(p, q) or .line_from_coefficients(a, b, c)")

    @classmethod
    def segment(cls, p, q):
        """The bounded segment from `p` to `q`."""
        return _wrap_curve(linear_make_segment(_as_point(p, _K_LINEAR),
                                               _as_point(q, _K_LINEAR)))

    @classmethod
    def ray(cls, source, towards):
        """The ray starting at `source` and passing through `towards`."""
        return _wrap_curve(linear_make_ray(_as_point(source, _K_LINEAR),
                                           _as_point(towards, _K_LINEAR)))

    @classmethod
    def ray_from_direction(cls, source, dx, dy):
        """The ray starting at `source` with direction ``(dx, dy)``."""
        return _wrap_curve(linear_make_ray_direction(_as_point(source, _K_LINEAR),
                                                     _to_rational(dx), _to_rational(dy)))

    @classmethod
    def line(cls, p, q):
        """The full line through `p` and `q`."""
        return _wrap_curve(linear_make_line(_as_point(p, _K_LINEAR),
                                            _as_point(q, _K_LINEAR)))

    @classmethod
    def line_from_coefficients(cls, a, b, c):
        """The full line ``a*x + b*y + c = 0``."""
        return _wrap_curve(linear_make_line_coefficients(_to_rational(a), _to_rational(b),
                                                         _to_rational(c)))

    @property
    def which(self):
        """``"segment"``, ``"ray"`` or ``"line"``."""
        cdef int w = linear_which(self.g)
        if w == 1:
            return "ray"
        if w == 2:
            return "line"
        return "segment"

    @property
    def is_segment(self):
        """True for a bounded segment."""
        return linear_which(self.g) == 0

    @property
    def is_ray(self):
        """True for a ray (one endpoint at infinity)."""
        return linear_which(self.g) == 1

    @property
    def is_line(self):
        """True for a full line (both ends at infinity)."""
        return linear_which(self.g) == 2

    @property
    def supporting_line(self):
        """``(a, b, c)`` as ``Fraction`` for the supporting line ``a*x + b*y + c = 0``."""
        cdef Rational a
        cdef Rational b
        cdef Rational c
        linear_supporting_line(self.g, a, b, c)
        return (_fraction(a), _fraction(b), _fraction(c))

    @property
    def direction(self):
        """``(dx, dy)`` as ``Fraction``: the stored direction (``target - source`` for
        segments)."""
        cdef Rational dx
        cdef Rational dy
        linear_direction(self.g, dx, dy)
        return (_fraction(dx), _fraction(dy))


def Line(p, q):
    """The full line through `p` and `q` (kind ``LINEAR``)."""
    return LinearCurve.line(p, q)


def Ray(source, towards):
    """The ray from `source` through `towards` (kind ``LINEAR``)."""
    return LinearCurve.ray(source, towards)


def line_from_coefficients(a, b, c):
    """The full line ``a*x + b*y + c = 0`` (kind ``LINEAR``)."""
    return LinearCurve.line_from_coefficients(a, b, c)


# ===========================================================================
# CircleSegment  (Kind.CIRCLE_SEGMENT)
# ===========================================================================

cdef class CircleSegment(Curve):
    """A circular arc, a full circle or a line segment (kind ``CIRCLE_SEGMENT``).

    Endpoint coordinates are one-root numbers ``a + b*sqrt(c)``; build such points with
    :meth:`Point.from_sqrt_extension`.
    """

    def __init__(self, *args, **kwargs):
        raise TypeError("CircleSegment has no public constructor; use "
                        "CircleSegment.circle(...), .arc(...), .arc_from_three_points(...), "
                        ".segment(p, q) or .segment_on_line(a, b, c, source, target)")

    @classmethod
    def circle(cls, center, radius=None, *, squared_radius=None, orientation="ccw"):
        """A full circle around `center`.

        Give exactly one of `radius` or `squared_radius`.  A rational `radius` is
        preferred: CGAL then keeps the vertical tangency points rational, which halves
        the arithmetic cost of everything downstream (traits_circle_segment gotcha 4).
        """
        cdef vector[Rational] c = _rational_xy(center)
        cdef int orient = <int>_orientation_arg(orientation)
        if (radius is None) == (squared_radius is None):
            raise TypeError("give exactly one of radius=... or squared_radius=...")
        if c.size() != 2:
            raise ValueError("circle center needs 2 rational coordinates")
        if radius is not None:
            return _wrap_curve(cs_make_full_circle_r(c[0], c[1], _to_rational(radius), orient))
        return _wrap_curve(cs_make_full_circle(c[0], c[1], _to_rational(squared_radius), orient))

    @classmethod
    def arc(cls, center, radius=None, *, squared_radius=None, source, target,
            orientation="ccw"):
        """A circular arc of the circle ``(center, radius)`` from `source` to `target`.

        The endpoints must lie on the circle (a CGAL precondition); they may be
        sqrt-extension points (see :meth:`Point.from_sqrt_extension`).
        """
        cdef vector[Rational] c = _rational_xy(center)
        cdef int orient = <int>_orientation_arg(orientation)
        cdef Geom s
        cdef Geom t
        if (radius is None) == (squared_radius is None):
            raise TypeError("give exactly one of radius=... or squared_radius=...")
        if c.size() != 2:
            raise ValueError("circle center needs 2 rational coordinates")
        s = _as_point(source, _K_CIRCLE_SEGMENT)
        t = _as_point(target, _K_CIRCLE_SEGMENT)
        if radius is not None:
            return _wrap_curve(cs_make_arc_r(c[0], c[1], _to_rational(radius), orient, s, t))
        return _wrap_curve(cs_make_arc(c[0], c[1], _to_rational(squared_radius), orient, s, t))

    @classmethod
    def arc_from_three_points(cls, p, q, r):
        """The arc from `p` through `q` to `r` (all three with rational coordinates)."""
        return _wrap_curve(cs_make_arc_three_points(_as_point(p, _K_CIRCLE_SEGMENT),
                                                    _as_point(q, _K_CIRCLE_SEGMENT),
                                                    _as_point(r, _K_CIRCLE_SEGMENT)))

    @classmethod
    def segment(cls, p, q):
        """A straight line segment as a circle-segment curve."""
        return _wrap_curve(cs_make_segment(_as_point(p, _K_CIRCLE_SEGMENT),
                                           _as_point(q, _K_CIRCLE_SEGMENT)))

    @classmethod
    def segment_on_line(cls, a, b, c, source, target):
        """A segment of the line ``a*x + b*y + c = 0`` with (possibly sqrt) endpoints."""
        return _wrap_curve(cs_make_segment_on_line(_to_rational(a), _to_rational(b),
                                                   _to_rational(c),
                                                   _as_point(source, _K_CIRCLE_SEGMENT),
                                                   _as_point(target, _K_CIRCLE_SEGMENT)))

    @property
    def is_full(self):
        """True for a full circle."""
        return cs_is_full(self.g)

    @property
    def is_linear(self):
        """True for a straight segment."""
        return cs_is_linear(self.g)

    @property
    def is_circular(self):
        """True for a circular arc or a full circle."""
        return cs_is_circular(self.g)

    @property
    def orientation(self):
        """+1 counterclockwise, -1 clockwise, 0 for a straight segment."""
        return cs_orientation(self.g)

    @property
    def center(self):
        """The centre of the supporting circle as a :class:`Point` (circular curves only)."""
        cdef Rational cx
        cdef Rational cy
        cs_center(self.g, cx, cy)
        return _wrap_point(ops(_K_CIRCLE_SEGMENT).make_point(cx, cy))

    @property
    def squared_radius(self):
        """The squared radius as a ``Fraction`` (circular curves only)."""
        return _fraction(cs_squared_radius(self.g))

    @property
    def has_rational_radius(self):
        """True when the curve was built from an explicit rational radius."""
        return cs_has_rational_radius(self.g)

    @property
    def radius(self):
        """The radius: a ``Fraction`` when :attr:`has_rational_radius`, else a float."""
        if cs_has_rational_radius(self.g):
            return _fraction(cs_radius(self.g))
        return _math.sqrt(rational_to_double(cs_squared_radius(self.g)))

    @property
    def supporting_line(self):
        """``(a, b, c)`` as ``Fraction`` (straight segments only)."""
        cdef Rational a
        cdef Rational b
        cdef Rational c
        cs_supporting_line(self.g, a, b, c)
        return (_fraction(a), _fraction(b), _fraction(c))


def Circle(center, radius=None, *, squared_radius=None, orientation="ccw"):
    """A full circle of kind ``CIRCLE_SEGMENT`` (alias of :meth:`CircleSegment.circle`)."""
    return CircleSegment.circle(center, radius, squared_radius=squared_radius,
                                orientation=orientation)


def CircularArc(center, radius=None, *, squared_radius=None, source, target,
                orientation="ccw"):
    """A circular arc of kind ``CIRCLE_SEGMENT`` (alias of :meth:`CircleSegment.arc`)."""
    return CircleSegment.arc(center, radius, squared_radius=squared_radius,
                             source=source, target=target, orientation=orientation)


# ===========================================================================
# Polyline  (Kind.POLYLINE)
# ===========================================================================

cdef class Polyline(Curve):
    """A chain of straight segments handled as a single curve (kind ``POLYLINE``)."""

    def __init__(self, points):
        cdef vector[Geom] pts
        cdef vector[Geom] segs
        cdef list items = list(points)
        if len(items) and isinstance(items[0], Curve):
            for it in items:
                segs.push_back(_as_curve(it, _K_SEGMENT, True))
            self.g = polyline_make_from_segments(segs)
            return
        if len(items) < 2:
            raise ValueError("a polyline needs at least 2 points")
        for it in items:
            pts.push_back(_as_point(it, _K_POLYLINE))
        self.g = polyline_make(pts)

    @classmethod
    def from_segments(cls, segments):
        """A polyline from a chain of :class:`Segment` objects (or convertible curves)."""
        cdef vector[Geom] segs
        for it in segments:
            segs.push_back(_as_curve(it, _K_SEGMENT, True))
        if segs.size() == 0:
            raise ValueError("a polyline needs at least one segment")
        return _wrap_curve(polyline_make_from_segments(segs))

    @classmethod
    def x_monotone(cls, points):
        """An x-monotone polyline; the points must already form an x-monotone chain."""
        cdef vector[Geom] pts
        for it in points:
            pts.push_back(_as_point(it, _K_POLYLINE))
        if pts.size() < 2:
            raise ValueError("a polyline needs at least 2 points")
        return _wrap_curve(polyline_make_x_monotone(pts))

    @property
    def number_of_subcurves(self):
        """Number of straight sub-segments."""
        return polyline_number_of_subcurves(self.g)

    @property
    def number_of_points(self):
        """Number of vertices (``number_of_subcurves + 1``)."""
        return polyline_number_of_points(self.g)

    @property
    def points(self):
        """The vertices as a list of :class:`Point` (kind ``SEGMENT``)."""
        cdef size_t n = polyline_number_of_points(self.g)
        cdef size_t i
        return [_wrap_point(polyline_point(self.g, i)) for i in range(n)]

    @property
    def segments(self):
        """The sub-segments as a list of :class:`Segment`."""
        cdef size_t n = polyline_number_of_subcurves(self.g)
        cdef size_t i
        return [_wrap_curve(polyline_subcurve(self.g, i)) for i in range(n)]

    def __len__(self):
        return polyline_number_of_points(self.g)

    def __getitem__(self, i):
        cdef Py_ssize_t n = <Py_ssize_t>polyline_number_of_points(self.g)
        cdef Py_ssize_t idx
        if isinstance(i, slice):
            return self.points[i]
        idx = <Py_ssize_t>i
        if idx < 0:
            idx += n
        if idx < 0 or idx >= n:
            raise IndexError("polyline point index out of range")
        return _wrap_point(polyline_point(self.g, <size_t>idx))

    def __iter__(self):
        return iter(self.points)


# ===========================================================================
# BezierCurve  (Kind.BEZIER)
# ===========================================================================

cdef class BezierCurve(Curve):
    """A polynomial Bezier curve with rational control points (kind ``BEZIER``).

    CGAL's Bezier traits only supports *polynomial* Bezier curves.  Rational quadratic
    Bezier curves are exactly conic arcs — use :meth:`from_rational` (or
    :meth:`ConicArc.from_rational_bezier`) for those.
    """

    def __init__(self, control_points):
        cdef vector[Rational] flat
        cdef vector[Rational] c
        cdef list items = list(control_points)
        cdef size_t i
        if len(items) < 2:
            raise ValueError("a Bezier curve needs at least 2 control points")
        for it in items:
            c = _rational_xy(it)
            if c.size() != 2:
                raise ValueError("Bezier control points need 2 rational coordinates")
            flat.push_back(c[0])
            flat.push_back(c[1])
        self.g = bezier_make(flat)

    @classmethod
    def from_rational(cls, control_points, weights=None):
        """A *rational* Bezier curve as an exact :class:`ConicArc`.

        Only degrees 1 and 2 (2 or 3 control points) are exactly representable: a rational
        quadratic Bezier is a conic arc.  Higher degrees raise
        :class:`NotRepresentableError`.
        """
        cdef list pts = list(control_points)
        cdef list ws
        cdef Py_ssize_t n = len(pts)
        if weights is None:
            ws = [1] * n
        else:
            ws = list(weights)
        if len(ws) != n:
            raise ValueError("need one weight per control point (%d != %d)" % (len(ws), n))
        if n == 2:
            # A rational linear Bezier is the segment p0..p1 (the weights only
            # reparametrise it), so the exact conic representation is that segment.
            return _wrap_curve(conic_make_segment(_as_point(pts[0], _K_CONIC),
                                                  _as_point(pts[1], _K_CONIC)))
        if n == 3:
            return _wrap_curve(conic_make_from_rational_bezier(
                _as_point(pts[0], _K_CONIC), _as_point(pts[1], _K_CONIC),
                _as_point(pts[2], _K_CONIC),
                _to_rational(ws[0]), _to_rational(ws[1]), _to_rational(ws[2])))
        raise NotRepresentableError(
            "rational Bezier curves of degree %d are not exactly representable: CGAL 6.1 "
            "has no algebraic-curve traits, and only rational quadratics (3 control "
            "points) are conics. Approximate the curve with a polynomial Bezier or a "
            "polyline instead." % (n - 1,))

    @property
    def control_points(self):
        """The control points as a list of :class:`Point` of kind ``SEGMENT``."""
        cdef size_t n = bezier_number_of_control_points(self.g)
        cdef size_t i
        cdef Rational x
        cdef Rational y
        cdef list out = []
        for i in range(n):
            bezier_control_point(self.g, i, x, y)
            out.append(_wrap_point(ops(_K_SEGMENT).make_point(x, y)))
        return out

    @property
    def degree(self):
        """Polynomial degree (``number of control points - 1``)."""
        return <Py_ssize_t>bezier_number_of_control_points(self.g) - 1

    @property
    def curve_id(self):
        """CGAL's serial id of the supporting curve."""
        return bezier_curve_id(self.g)

    @property
    def xid(self):
        """Index of this x-monotone piece inside its supporting curve."""
        return bezier_xid(_xg(self))

    @property
    def supporting_curve(self):
        """The general Bezier curve behind this x-monotone piece."""
        return _wrap_curve(bezier_supporting_curve(_xg(self)))

    @property
    def parameter_range(self):
        """``(t_min, t_max)`` of this x-monotone piece, as floats (approximate)."""
        cdef double t0 = 0.0
        cdef double t1 = 0.0
        bezier_parameter_range(_xg(self), t0, t1)
        return (t0, t1)

    @property
    def has_self_intersections(self):
        """True when the supporting curve intersects itself."""
        return not bezier_has_no_self_intersections(self.g)

    def evaluate(self, t):
        """Evaluate the supporting curve at parameter `t`.

        A ``float`` `t` gives an approximate ``(x, y)`` tuple; an ``int`` / ``Fraction``
        (or anything exactly rational) gives an exact :class:`Point` of kind ``BEZIER``.
        """
        cdef double x = 0.0
        cdef double y = 0.0
        if isinstance(t, float):
            bezier_evaluate_approx(self.g, <double>t, x, y)
            return (x, y)
        return _wrap_point(bezier_point_at(self.g, _to_rational(t)))

    def sample(self, double t0=0.0, double t1=1.0, size_t n=64):
        """`n` uniform samples of the supporting curve for ``t`` in ``[t0, t1]``."""
        cdef vector[double] out
        cdef size_t i
        cdef size_t m
        if n < 2:
            raise ValueError("sample() needs n >= 2")
        bezier_sample(self.g, t0, t1, n, out)
        m = out.size() // 2
        return [(out[2 * i], out[2 * i + 1]) for i in range(m)]

    def point_originators(self, point):
        """``[(curve_id, t), ...]`` for every Bezier curve the point lies on."""
        cdef Geom p = _as_point(point, _K_BEZIER)
        cdef vector[pair[size_t, double]] out
        cdef size_t i
        bezier_point_originators(p, out)
        return [(out[i].first, out[i].second) for i in range(out.size())]

    def parameter_at(self, point, curve_id=None):
        """The exact algebraic parameter of `point` on the curve with id `curve_id`.

        `curve_id` defaults to this curve's own id.  Returns an :class:`Algebraic`
        (or a ``Fraction`` when the value is provably rational).
        """
        cdef Geom p = _as_point(point, _K_BEZIER)
        cdef size_t cid
        if curve_id is None:
            cid = bezier_curve_id(self.g)
        else:
            cid = <size_t>curve_id
        return _wrap_number(bezier_point_parameter(p, cid))


# ===========================================================================
# ConicArc  (Kind.CONIC)
# ===========================================================================

cdef class ConicArc(Curve):
    """An arc of a conic ``r x^2 + s y^2 + t x y + u x + v y + w = 0`` (kind ``CONIC``).

    CGAL 6.1's hyperbolic-arc support is unreliable, so hyperbolic supporting conics are
    rejected by default; see :func:`conic_allow_hyperbolic`.
    """

    def __init__(self, *args, **kwargs):
        raise TypeError("ConicArc has no public constructor; use "
                        "ConicArc.from_coefficients(...), .circle(...), .ellipse(...), "
                        ".segment(p, q), .from_points(p1..p5), .from_rational_bezier(...), "
                        ".from_circle_segment(curve) or .arc_with_defining_conics(...)")

    @classmethod
    def from_coefficients(cls, r, s, t, u, v, w, *, orientation=None, source=None,
                          target=None):
        """The conic ``r x^2 + s y^2 + t x y + u x + v y + w = 0``.

        With neither `source` nor `target` the full (bounded, i.e. elliptic) conic is
        built.  With both, the arc between them is built and `orientation` is required.
        """
        cdef Rational rr = _to_rational(r)
        cdef Rational rs = _to_rational(s)
        cdef Rational rt = _to_rational(t)
        cdef Rational ru = _to_rational(u)
        cdef Rational rv = _to_rational(v)
        cdef Rational rw = _to_rational(w)
        cdef int orient
        if source is None and target is None:
            if orientation is not None:
                raise TypeError("a full conic has no orientation argument; give "
                                "source=... and target=... to build an arc")
            return _wrap_curve(conic_make_full(rr, rs, rt, ru, rv, rw))
        if source is None or target is None:
            raise TypeError("give both source=... and target=... (or neither)")
        if orientation is None:
            raise TypeError("orientation=... is required when building a conic arc")
        orient = <int>_orientation_arg(orientation)
        return _wrap_curve(conic_make_arc(rr, rs, rt, ru, rv, rw, orient,
                                          _as_point(source, _K_CONIC),
                                          _as_point(target, _K_CONIC)))

    @classmethod
    def circle(cls, center, radius=None, *, squared_radius=None, orientation="ccw",
               source=None, target=None):
        """A full circle or a circular arc, as a conic (kind ``CONIC``)."""
        cdef vector[Rational] c = _rational_xy(center)
        cdef int orient = <int>_orientation_arg(orientation)
        cdef Rational sq
        if (radius is None) == (squared_radius is None):
            raise TypeError("give exactly one of radius=... or squared_radius=...")
        if c.size() != 2:
            raise ValueError("circle center needs 2 rational coordinates")
        if radius is not None:
            sq = _squared(radius)
        else:
            sq = _to_rational(squared_radius)
        if source is None and target is None:
            return _wrap_curve(conic_make_circle(c[0], c[1], sq, orient))
        if source is None or target is None:
            raise TypeError("give both source=... and target=... (or neither)")
        return _wrap_curve(conic_make_circle_arc(c[0], c[1], sq, orient,
                                                 _as_point(source, _K_CONIC),
                                                 _as_point(target, _K_CONIC)))

    @classmethod
    def ellipse(cls, center, a, b, *, direction=(1, 0), orientation="ccw"):
        """A full ellipse with semi-axes `a` (along `direction`) and `b` (perpendicular)."""
        cdef vector[Rational] c = _rational_xy(center)
        cdef vector[Rational] d = _rational_xy(direction)
        cdef int orient = <int>_orientation_arg(orientation)
        if c.size() != 2:
            raise ValueError("ellipse center needs 2 rational coordinates")
        if d.size() != 2:
            raise ValueError("ellipse direction needs 2 rational coordinates")
        return _wrap_curve(conic_make_ellipse(c[0], c[1], _to_rational(a), _to_rational(b),
                                              d[0], d[1], orient))

    @classmethod
    def segment(cls, p, q):
        """A straight line segment as a (degenerate) conic arc."""
        return _wrap_curve(conic_make_segment(_as_point(p, _K_CONIC),
                                              _as_point(q, _K_CONIC)))

    @classmethod
    def from_points(cls, p1, p2, p3, p4, p5):
        """The conic arc through five rational points (``p1`` source, ``p5`` target)."""
        return _wrap_curve(conic_make_from_five_points(
            _as_point(p1, _K_CONIC), _as_point(p2, _K_CONIC), _as_point(p3, _K_CONIC),
            _as_point(p4, _K_CONIC), _as_point(p5, _K_CONIC)))

    @classmethod
    def from_rational_bezier(cls, p0, p1, p2, w0=1, w1=1, w2=1):
        """The exact conic arc of a rational quadratic Bezier curve."""
        return _wrap_curve(conic_make_from_rational_bezier(
            _as_point(p0, _K_CONIC), _as_point(p1, _K_CONIC), _as_point(p2, _K_CONIC),
            _to_rational(w0), _to_rational(w1), _to_rational(w2)))

    @classmethod
    def from_circle_segment(cls, curve):
        """The exact conic arc of a circle-segment curve (circle, arc or segment)."""
        cdef Geom g = _as_curve(curve, _K_CIRCLE_SEGMENT, False)
        return _wrap_curve(conic_make_from_circle_segment(g))

    @classmethod
    def arc_with_defining_conics(cls, coeffs, orientation, approx_source, source_conic,
                                 approx_target, target_conic):
        """An arc whose endpoints are intersections with two other conics.

        `coeffs`, `source_conic` and `target_conic` are 6-tuples ``(r, s, t, u, v, w)``;
        `approx_source` / `approx_target` are approximate ``(x, y)`` pairs that
        disambiguate which intersection point is meant.
        """
        cdef vector[Rational] a = _coeffs6(coeffs, "coeffs")
        cdef vector[Rational] sc = _coeffs6(source_conic, "source_conic")
        cdef vector[Rational] tc = _coeffs6(target_conic, "target_conic")
        cdef int orient = <int>_orientation_arg(orientation)
        cdef double sx = float(approx_source[0])
        cdef double sy = float(approx_source[1])
        cdef double tx = float(approx_target[0])
        cdef double ty = float(approx_target[1])
        return _wrap_curve(conic_make_arc_with_defining_conics(
            a.data(), orient, sx, sy, sc.data(), tx, ty, tc.data()))

    @property
    def coefficients(self):
        """``(r, s, t, u, v, w)`` as ``Fraction``, **as stored by CGAL**.

        CGAL integerises the coefficients and negates them when the natural orientation of
        the conic differs from the requested one (traits_conic gotcha 2), so these are not
        necessarily the numbers that were passed in — they define the same conic up to a
        non-zero scalar.
        """
        cdef vector[Rational] out
        cdef size_t i
        out.resize(6)
        conic_coefficients(self.g, out.data())
        return tuple([_fraction(out[i]) for i in range(6)])

    @property
    def orientation(self):
        """+1 counterclockwise, -1 clockwise, 0 for a straight segment."""
        return conic_orientation(self.g)

    @property
    def is_full(self):
        """True for a full (closed) conic."""
        return conic_is_full(self.g)

    @property
    def conic_type(self):
        """``"ellipse"``, ``"parabola"``, ``"hyperbola"``, ``"segment"`` or ``"unknown"``."""
        cdef int t = conic_conic_type(self.g)
        if t == 1:
            return "ellipse"
        if t == 2:
            return "parabola"
        if t == 3:
            return "hyperbola"
        if t == 4:
            return "segment"
        return "unknown"


cdef vector[Rational] _coeffs6(object obj, str what) except *:
    cdef vector[Rational] out
    cdef list items = list(obj)
    if len(items) != 6:
        raise ValueError("%s must have 6 coefficients (r, s, t, u, v, w), got %d"
                         % (what, len(items)))
    for v in items:
        out.push_back(_to_rational(v))
    return out


def _py_conic_allow_hyperbolic(allow=None):
    """Get or set the "allow hyperbolic supporting conics" switch.

    CGAL 6.1's ``build_hyperbolic_arc_data`` picks the wrong branch-separating axis for
    many hyperbolas (assertion failures in debug builds, wrong point containment under
    ``NDEBUG``), so every conic constructor rejects hyperbolic supporting conics
    (``4rs - t^2 < 0``) by default.  Pass ``True`` to opt in anyway.

    Returns the flag value in effect after the call.
    """
    if allow is not None:
        conic_set_allow_hyperbolic(<cbool>bool(allow))
    return conic_allow_hyperbolic()


# `conic_allow_hyperbolic` is already the (compile-time) name of the C++ accessor declared
# in _core.pxd, so the Python wrapper is published into the module namespace by hand.
_py_conic_allow_hyperbolic.__name__ = "conic_allow_hyperbolic"
_py_conic_allow_hyperbolic.__qualname__ = "conic_allow_hyperbolic"
globals()["conic_allow_hyperbolic"] = _py_conic_allow_hyperbolic


# ===========================================================================
# GeodesicArc  (Kind.SPHERE)
# ===========================================================================

cdef class GeodesicArc(Curve):
    """A geodesic arc on the unit sphere (kind ``SPHERE``).

    Points of this kind are (unnormalised) directions ``(x, y, z)``; two directions are
    equal iff they are positive multiples of each other.
    """

    def __init__(self, *args, **kwargs):
        raise TypeError("GeodesicArc has no public constructor; use "
                        "GeodesicArc.from_points(p, q), .from_points_and_normal(p, q, n), "
                        ".great_circle(normal) or .x_monotone(p, q)")

    @classmethod
    def from_points(cls, p, q):
        """The minor great-circle arc from `p` to `q` (`p` must not be `q` or its antipode)."""
        return _wrap_curve(sphere_make_arc(_as_point(p, _K_SPHERE),
                                           _as_point(q, _K_SPHERE)))

    @classmethod
    def from_points_and_normal(cls, p, q, normal):
        """The arc from `p` to `q` on the great circle with the given `normal`.

        This form also allows antipodal endpoints and major arcs.
        """
        return _wrap_curve(sphere_make_arc_with_normal(_as_point(p, _K_SPHERE),
                                                       _as_point(q, _K_SPHERE),
                                                       _as_point(normal, _K_SPHERE)))

    @classmethod
    def great_circle(cls, normal):
        """The full great circle whose plane has the given `normal`."""
        return _wrap_curve(sphere_make_full_circle(_as_point(normal, _K_SPHERE)))

    @classmethod
    def x_monotone(cls, p, q):
        """The x-monotone arc from `p` to `q` (a CGAL precondition if it is not)."""
        return _wrap_curve(sphere_make_x_monotone_arc(_as_point(p, _K_SPHERE),
                                                      _as_point(q, _K_SPHERE)))

    @property
    def is_full(self):
        """True for a full great circle."""
        return sphere_is_full(self.g)

    @property
    def is_vertical(self):
        """True when the arc lies on a meridian (vertical in the parameter space)."""
        return sphere_is_vertical(self.g)

    @property
    def is_meridian(self):
        """True when the arc lies on a meridian through both poles."""
        return sphere_is_meridian(self.g)

    @property
    def is_degenerate(self):
        """True when source and target coincide."""
        return sphere_is_degenerate(self.g)

    @property
    def normal(self):
        """The normal of the supporting great circle, as a :class:`Point` of kind
        ``SPHERE``."""
        return _wrap_point(sphere_normal(self.g))


# ===========================================================================
# Polygon
# ===========================================================================

cdef Geom _make_straight(const Geom& p, const Geom& q, Kind k) except *:
    """A straight x-monotone curve of kind `k` between two points of that kind."""
    if <int>k == <int>_K_CIRCLE_SEGMENT:
        # Arr_circle_segment_traits_2 has no Construct_x_monotone_curve_2 at all
        # (traits_circle_segment gotcha 3) -> use the kind's own segment constructor.
        return cs_make_segment(p, q)
    if <int>k == <int>_K_BEZIER:
        raise UnsupportedError(
            "kind 'bezier' cannot build a polygon from points: the Bezier traits has no "
            "straight x-monotone curve constructor. Build the Polygon from BezierCurve "
            "objects, or use kind='segment' / 'conic'.")
    return ops(k).construct_x_monotone_curve(p, q)


cdef list _polygon_curves_from_points(list items, Kind k):
    cdef list pts = []
    cdef Py_ssize_t n, i
    cdef list out = []
    cdef Geom a
    cdef Geom b
    for it in items:
        pts.append(_wrap_point(_as_point(it, k)))
    n = len(pts)
    if n >= 2 and ops(k).point_equal((<Point>pts[0]).g, (<Point>pts[n - 1]).g):
        pts = pts[:n - 1]
        n -= 1
    if n < 3:
        raise ValueError("a polygon needs at least 3 distinct points, got %d" % (n,))
    for i in range(n):
        a = (<Point>pts[i]).g
        b = (<Point>pts[(i + 1) % n]).g
        out.append(_wrap_curve(_make_straight(a, b, k)))
    return out


cdef list _polygon_curves_from_curves(list items, Kind k):
    cdef list out = []
    cdef vector[Geom] conv
    cdef vector[Geom] pieces
    cdef Geom g
    cdef Curve c
    cdef size_t i, j
    for it in items:
        if not isinstance(it, Curve):
            raise TypeError("Polygon: expected Curve objects, got %s"
                            % (type(it).__name__,))
        c = <Curve>it
        conv.clear()
        if <int>c.g.kind == <int>k:
            conv.push_back(c.g)
        else:
            ops(k).convert_curve(c.g, conv)
        for i in range(conv.size()):
            g = conv[i]
            if <int>g.type == _GT_XCURVE:
                out.append(_wrap_curve(g))
                continue
            pieces.clear()
            ops(k).make_x_monotone(g, pieces)
            for j in range(pieces.size()):
                if <int>pieces[j].type == _GT_POINT:
                    raise ValueError("Polygon: a boundary curve degenerates into an "
                                     "isolated point")
                out.append(_wrap_curve(pieces[j]))
    return out


cdef int _validate_chain(list curves, Kind k) except -1:
    cdef Py_ssize_t i, n = len(curves)
    cdef Geom t
    cdef Geom s
    if n == 0:
        raise ValueError("a polygon needs at least one curve")
    for i in range(n - 1):
        t = ops(k).xcurve_target((<Curve>curves[i]).g)
        s = ops(k).xcurve_source((<Curve>curves[i + 1]).g)
        if not ops(k).point_equal(t, s):
            raise ValueError("polygon boundary is broken between curve %d and curve %d: "
                             "%s != %s" % (i, i + 1, ops(k).point_repr(t),
                                           ops(k).point_repr(s)))
    return 0


cdef Polygon _polygon_unchecked(list curves, Kind k):
    """A Polygon built from a chain that is known to be valid (no re-validation)."""
    cdef Polygon p = Polygon.__new__(Polygon)
    p._curves = curves
    p._kind = k
    return p


cdef Polygon _polygon_from_geoms(const vector[Geom]& gs):
    cdef Polygon p = Polygon.__new__(Polygon)
    cdef size_t i
    cdef list cs = []
    for i in range(gs.size()):
        cs.append(_wrap_curve(gs[i]))
    p._curves = cs
    if gs.size() > 0:
        p._kind = gs[0].kind
    else:
        p._kind = _K_SEGMENT
    return p


cdef vector[Geom] _ring_geoms(Polygon p, Kind kind) except *:
    """The polygon's boundary as directed x-monotone Geoms of `kind`."""
    cdef vector[Geom] out
    for c in p._curves:
        out.push_back(_as_curve(c, kind, True))
    return out


cdef double _signed_area_2d(list pts):
    cdef Py_ssize_t n = len(pts)
    cdef Py_ssize_t i, j
    cdef double acc = 0.0
    for i in range(n):
        j = (i + 1) % n
        acc += <double>pts[i][0] * <double>pts[j][1] - <double>pts[j][0] * <double>pts[i][1]
    return 0.5 * acc


cdef class Polygon:
    """A closed (or open) chain of directed x-monotone curves of one geometry kind.

    ``Polygon(points)`` builds a polygon of straight segments from a sequence of points
    (open or closed; it is closed automatically).  ``Polygon(curves)`` takes curves that
    chain end to end; curves that are not x-monotone are split first.  `kind` converts
    everything to that geometry kind.
    """

    cdef list _curves
    cdef Kind _kind

    def __init__(self, curves_or_points, kind=None):
        cdef Kind k
        cdef list items
        cdef bint curve_mode = False
        cdef list curves
        items = list(curves_or_points)
        if len(items) == 0:
            raise ValueError("a polygon needs at least one curve or three points")
        for it in items:
            if isinstance(it, Curve):
                curve_mode = True
                break
        if curve_mode:
            if kind is not None:
                k = _ckind(kind)
            else:
                k = (<Curve>items[0]).g.kind
            curves = _polygon_curves_from_curves(items, k)
        else:
            if kind is not None:
                k = _ckind(kind)
            else:
                k = _K_SEGMENT
            curves = _polygon_curves_from_points(items, k)
        _validate_chain(curves, k)
        self._curves = curves
        self._kind = k

    # ---------------------------------------------------------------- basics
    @property
    def curves(self):
        """The boundary curves, in order (a copy of the internal list)."""
        return list(self._curves)

    @property
    def kind(self):
        """The geometry :class:`Kind` of every boundary curve."""
        return _pykind(self._kind)

    @property
    def points(self):
        """The chain's vertices as a list of :class:`Point`.

        For a closed polygon these are the ``n`` curve sources; for an open chain the
        target of the last curve is appended.  Note that for the ``POLYLINE`` kind each
        boundary curve may have interior vertices that do not appear here — use
        :meth:`approximate` for the full vertex list.
        """
        cdef Py_ssize_t i, n = len(self._curves)
        cdef list out = []
        for i in range(n):
            out.append(_wrap_point(ops(self._kind).xcurve_source((<Curve>self._curves[i]).g)))
        if not self.is_closed:
            out.append(_wrap_point(ops(self._kind).xcurve_target(
                (<Curve>self._curves[n - 1]).g)))
        return out

    @property
    def is_closed(self):
        """True when the last curve's target equals the first curve's source."""
        cdef Py_ssize_t n = len(self._curves)
        cdef Geom t = ops(self._kind).xcurve_target((<Curve>self._curves[n - 1]).g)
        cdef Geom s = ops(self._kind).xcurve_source((<Curve>self._curves[0]).g)
        return ops(self._kind).point_equal(t, s)

    def __len__(self):
        return len(self._curves)

    def __iter__(self):
        return iter(self._curves)

    def __getitem__(self, i):
        return self._curves[i]

    # ---------------------------------------------------------------- geometry
    def orientation(self):
        """+1 for a counterclockwise boundary, -1 for clockwise, 0 when undecidable.

        Exact for the kinds that have Boolean set operations (segment, circle_segment,
        conic, bezier); for the others it is the sign of the signed area of
        ``approximate(1e-3)``.
        """
        cdef vector[Geom] ring
        cdef PolygonSet ps
        cdef double a
        if kind_has_polygon_set(self._kind):
            ps = _polygon_set_for_kind(self._kind)
            ring = _ring_geoms(self, self._kind)
            return ps.ps.get().orientation(ring)
        a = _signed_area_2d(self.approximate(1e-3))
        if a > 0.0:
            return 1
        if a < 0.0:
            return -1
        return 0

    def reverse(self):
        """A new :class:`Polygon` with the opposite orientation."""
        cdef list out = []
        cdef Py_ssize_t i
        for i in range(len(self._curves) - 1, -1, -1):
            out.append(_wrap_curve(ops(self._kind).construct_opposite(
                (<Curve>self._curves[i]).g)))
        return _polygon_unchecked(out, self._kind)

    def to_kind(self, kind):
        """This polygon with every boundary curve converted to another geometry kind."""
        cdef Kind k = _ckind(kind)
        cdef list curves
        if <int>k == <int>self._kind:
            return self
        curves = _polygon_curves_from_curves(list(self._curves), k)
        _validate_chain(curves, k)
        return _polygon_unchecked(curves, k)

    def approximate(self, double tolerance=1e-3):
        """The boundary as a list of coordinate tuples (the closing point is not repeated)."""
        cdef list pts = []
        cdef list sub
        for c in self._curves:
            sub = (<Curve>c).approximate(tolerance)
            if len(pts) and len(sub) and pts[len(pts) - 1] == sub[0]:
                sub = sub[1:]
            pts.extend(sub)
        if len(pts) > 1 and pts[0] == pts[len(pts) - 1]:
            pts.pop()
        return pts

    def bbox(self):
        """Approximate bounding box of the boundary."""
        cdef BBox b
        cdef BBox t
        cdef bint first = True
        cdef int i
        for c in self._curves:
            t = ops(self._kind).curve_bbox((<Curve>c).g)
            if first:
                b = t
                first = False
            else:
                for i in range(3):
                    if t.lo[i] < b.lo[i]:
                        b.lo[i] = t.lo[i]
                    if t.hi[i] > b.hi[i]:
                        b.hi[i] = t.hi[i]
        if first:
            raise ValueError("an empty polygon has no bounding box")
        return _bbox_tuple(b)

    def area(self):
        """The **signed** area (positive for a counterclockwise boundary).

        Exact (a ``Fraction``) for the ``SEGMENT`` kind, an approximate ``float`` computed
        from ``approximate(1e-3)`` for every other kind.
        """
        cdef Py_ssize_t n, i, j
        cdef list xs
        cdef list ys
        if <int>self._kind == <int>_K_SEGMENT:
            if not self.is_closed:
                raise ValueError("area() needs a closed polygon")
            pts = self.points
            n = len(pts)
            xs = []
            ys = []
            for p in pts:
                ex = p.exact_rational()
                xs.append(ex[0])
                ys.append(ex[1])
            acc = _Fraction(0)
            for i in range(n):
                j = (i + 1) % n
                acc = acc + (xs[i] * ys[j] - xs[j] * ys[i])
            return acc / 2
        return _signed_area_2d(self.approximate(1e-3))

    def is_simple(self):
        """True when CGAL accepts this boundary as a valid (relatively simple) polygon.

        Only available for kinds with Boolean set operations; raises
        :class:`UnsupportedError` otherwise.
        """
        cdef PolygonSet ps
        cdef PolygonGeom pg
        if not kind_has_polygon_set(self._kind):
            raise UnsupportedError("polygon validity cannot be checked for kind '%s': "
                                   "CGAL has no Boolean set operations for it"
                                   % (kind_name(self._kind),))
        ps = _polygon_set_for_kind(self._kind)
        pg = _as_polygon_geom(self, self._kind)
        return ps.ps.get().is_valid_polygon(pg)

    # ---------------------------------------------------------------- protocol
    def __eq__(self, other):
        cdef Polygon o
        cdef Py_ssize_t i, n
        if not isinstance(other, Polygon):
            return NotImplemented
        o = <Polygon>other
        if <int>self._kind != <int>o._kind:
            return False
        n = len(self._curves)
        if n != len(o._curves):
            return False
        for i in range(n):
            if not ops(self._kind).curve_equal((<Curve>self._curves[i]).g,
                                               (<Curve>o._curves[i]).g):
                return False
        return True

    def __ne__(self, other):
        r = self.__eq__(other)
        if r is NotImplemented:
            return r
        return not r

    def __hash__(self):
        raise TypeError("Polygon objects are not hashable")

    def __repr__(self):
        return "<Polygon kind='%s' curves=%d closed=%s>" % (
            kind_name(self._kind), len(self._curves), self.is_closed)


# ===========================================================================
# PolygonWithHoles
# ===========================================================================

cdef class PolygonWithHoles:
    """An outer boundary (or the whole plane) with zero or more holes.

    ``outer`` is a :class:`Polygon` (or anything :class:`Polygon` accepts), or ``None``
    for the unbounded "whole plane minus the holes" polygon.
    """

    cdef Polygon _outer
    cdef tuple _holes
    cdef Kind _kind

    def __init__(self, outer, holes=()):
        cdef Polygon o = None
        cdef list hs = []
        cdef Py_ssize_t i
        if outer is not None:
            if isinstance(outer, Polygon):
                o = <Polygon>outer
            else:
                o = Polygon(outer)
        for h in holes:
            if isinstance(h, Polygon):
                hs.append(h)
            else:
                hs.append(Polygon(h))
        if o is not None:
            self._kind = o._kind
        elif len(hs):
            self._kind = (<Polygon>hs[0])._kind
        else:
            self._kind = _K_SEGMENT
        for i in range(len(hs)):
            if <int>(<Polygon>hs[i])._kind != <int>self._kind:
                hs[i] = (<Polygon>hs[i]).to_kind(<int>self._kind)
        self._outer = o
        self._holes = tuple(hs)

    @property
    def outer(self):
        """The outer boundary :class:`Polygon`, or ``None`` when unbounded."""
        return self._outer

    @property
    def holes(self):
        """The holes as a tuple of :class:`Polygon`."""
        return self._holes

    @property
    def is_unbounded(self):
        """True when there is no outer boundary (the whole plane minus the holes)."""
        return self._outer is None

    @property
    def kind(self):
        """The geometry :class:`Kind` of every boundary curve."""
        return _pykind(self._kind)

    def approximate(self, double tolerance=1e-3):
        """``(outer_points, [hole_points, ...])``; ``outer_points`` is ``None`` when
        unbounded."""
        cdef list holes = []
        for h in self._holes:
            holes.append((<Polygon>h).approximate(tolerance))
        if self._outer is None:
            return (None, holes)
        return (self._outer.approximate(tolerance), holes)

    def bbox(self):
        """Approximate bounding box of the outer boundary (of the holes when unbounded)."""
        cdef BBox b
        cdef BBox t
        cdef bint first = True
        cdef int i
        cdef list rings
        if self._outer is not None:
            return self._outer.bbox()
        rings = list(self._holes)
        if not rings:
            raise ValueError("the unbounded polygon without holes has no bounding box")
        for h in rings:
            t = _as_bbox((<Polygon>h).bbox())
            if first:
                b = t
                first = False
            else:
                for i in range(3):
                    if t.lo[i] < b.lo[i]:
                        b.lo[i] = t.lo[i]
                    if t.hi[i] > b.hi[i]:
                        b.hi[i] = t.hi[i]
        return _bbox_tuple(b)

    def __repr__(self):
        return "<PolygonWithHoles kind='%s' outer=%s holes=%d>" % (
            kind_name(self._kind),
            "unbounded" if self._outer is None else "(%d curves)" % (len(self._outer),),
            len(self._holes))


# ===========================================================================
# PolygonGeom <-> Python conversions (used by _polygon_set.pxi and _arrangement.pxi)
# ===========================================================================

cdef object _wrap_polygon_geom(const PolygonGeom& pg):
    """Wrap an ``arr2d::PolygonGeom`` as a :class:`PolygonWithHoles` (always)."""
    cdef size_t i
    cdef list holes = []
    outer = None
    if pg.outer.size() > 0:
        outer = _polygon_from_geoms(pg.outer)
    for i in range(pg.holes.size()):
        holes.append(_polygon_from_geoms(pg.holes[i]))
    return PolygonWithHoles(outer, holes)


cdef PolygonGeom _as_polygon_geom(object polygon_or_pwh, Kind kind) except *:
    """Convert a Polygon / PolygonWithHoles (or anything Polygon accepts) to a
    ``PolygonGeom`` whose curves are directed x-monotone curves of `kind`."""
    cdef PolygonGeom pg
    cdef vector[Geom] ring
    cdef Polygon p
    cdef PolygonWithHoles pwh
    if isinstance(polygon_or_pwh, PolygonWithHoles):
        pwh = <PolygonWithHoles>polygon_or_pwh
        if pwh._outer is None:
            pg.unbounded = True
        else:
            pg.outer = _ring_geoms(pwh._outer, kind)
        for h in pwh._holes:
            ring = _ring_geoms(<Polygon>h, kind)
            pg.holes.push_back(ring)
        return pg
    if isinstance(polygon_or_pwh, Polygon):
        p = <Polygon>polygon_or_pwh
    else:
        p = Polygon(polygon_or_pwh, <int>kind)
    pg.outer = _ring_geoms(p, kind)
    return pg


# ---------------------------------------------------------------------------
# One lazily created PolygonSet per kind, used only for its stateless helpers
# (orientation / is_valid_polygon).  Defined here because Polygon needs it.
# ---------------------------------------------------------------------------

cdef dict _PS_HELPERS = {}


cdef PolygonSet _polygon_set_for_kind(Kind k):
    cdef object obj = _PS_HELPERS.get(<int>k)
    if obj is None:
        obj = PolygonSet(<int>k)
        _PS_HELPERS[<int>k] = obj
    return <PolygonSet>obj
