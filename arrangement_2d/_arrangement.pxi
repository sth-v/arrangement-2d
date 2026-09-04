# -*- coding: utf-8 -*-
# Part of arrangement_2d._core (textual include; see _core.pyx).
#
# The arrangement itself: Arrangement, its DCEL handles (Vertex / Halfedge / Face /
# CurveHandle), the traits functor facade (Traits), observers and overlay.
#
# Every DCEL handle stores (arrangement, raw pointer, unique id).  The C++ core validates
# the pair on every access and raises InvalidHandleError for a stale handle, so a removed
# element can never crash the interpreter (DESIGN.md "Handles and validity").
#
# CCB walks may contain *fictitious* halfedges in the unbounded (`linear`) kind
# (arrangement_core.md gotcha 4): `Halfedge.curve` raises UnsupportedError for those --
# test `Halfedge.is_fictitious` first.  Likewise vertices at infinity have no point
# (gotcha 5): test `Vertex.is_at_open_boundary`.

# ---------------------------------------------------------------------------
# Small conversions
# ---------------------------------------------------------------------------

cdef tuple _PARAMETER_SPACE_NAMES = ("left", "right", "bottom", "top", "interior")
cdef tuple _HALFEDGE_DIRECTION_NAMES = ("left_to_right", "right_to_left")


cdef inline object _param_space_name(int ps):
    if 0 <= ps < 5:
        return _PARAMETER_SPACE_NAMES[ps]
    return "interior"


cdef int _curve_end_arg(object end) except -1:
    """``"min"`` / ``"max"`` / ``"source"`` / ``"target"`` / ``0`` / ``1`` -> ``ARR_MIN_END`` / ``ARR_MAX_END``."""
    if end is None:
        return <int>ARR_MIN_END
    if isinstance(end, str):
        s = (<str>end).strip().lower()
        if s in ("min", "min_end", "source", "left", "start"):
            return <int>ARR_MIN_END
        if s in ("max", "max_end", "target", "right", "end"):
            return <int>ARR_MAX_END
        raise ValueError("unknown curve end %r; expected 'min' or 'max'" % (end,))
    if isinstance(end, (int, numbers.Integral)) and not isinstance(end, bool):
        if int(end) == 0:
            return <int>ARR_MIN_END
        if int(end) == 1:
            return <int>ARR_MAX_END
        raise ValueError("unknown curve end %r; expected 0 (min) or 1 (max)" % (end,))
    raise TypeError("expected a curve end ('min'/'max' or 0/1), got %r" % (type(end).__name__,))


cdef int _pl_strategy(object strategy) except -99:
    """``None`` / name / int -> an ``arr2d::PointLocationStrategy`` value."""
    cdef int s
    if strategy is None:
        return <int>PL_DEFAULT
    if isinstance(strategy, str):
        s = point_location_from_name(_to_bytes((<str>strategy).strip().lower()))
        if s == -2:
            raise ValueError(
                "unknown point-location strategy %r; valid names are %s (or None/'default')"
                % (strategy, ", ".join(repr(n) for n in point_location_strategies()))
            )
        return s
    if isinstance(strategy, (int, numbers.Integral)) and not isinstance(strategy, bool):
        s = <int>int(strategy)
        if s < <int>PL_DEFAULT or s >= <int>PL_NUM_STRATEGIES:
            raise ValueError(
                "unknown point-location strategy %r; valid values are -1..%d"
                % (strategy, <int>PL_NUM_STRATEGIES - 1)
            )
        return s
    raise TypeError(
        "expected a point-location strategy name or None, got %r" % (type(strategy).__name__,)
    )


cdef inline ArrBase* _abase(Arrangement a) except NULL:
    """The underlying ``arr2d::ArrBase*`` of *a* (raises if *a* is not usable)."""
    if a is None:
        raise TypeError("expected an Arrangement")
    if a.arr.get() == NULL:
        raise RuntimeError("Arrangement is not initialised")
    return a.arr.get()


cdef inline int _check_pending(Arrangement a) except -1:
    """Re-raise an exception recorded by an observer / overlay callback, if any."""
    if a._pending is not None:
        exc = a._pending
        a._pending = None
        raise exc
    return 0


# ---------------------------------------------------------------------------
# Handle wrapping
# ---------------------------------------------------------------------------

cdef Vertex _wrap_vertex(Arrangement arr, VH h):
    """Wrap a raw vertex handle of *arr* in a :class:`Vertex`."""
    cdef Vertex v = Vertex.__new__(Vertex)
    v.arr = arr
    v.h = h
    return v


cdef Halfedge _wrap_halfedge(Arrangement arr, HH h):
    """Wrap a raw halfedge handle of *arr* in a :class:`Halfedge`."""
    cdef Halfedge e = Halfedge.__new__(Halfedge)
    e.arr = arr
    e.h = h
    return e


cdef Face _wrap_face(Arrangement arr, FH h):
    """Wrap a raw face handle of *arr* in a :class:`Face`."""
    cdef Face f = Face.__new__(Face)
    f.arr = arr
    f.h = h
    return f


cdef CurveHandle _wrap_curve_handle(Arrangement arr, CH h):
    """Wrap a raw input-curve handle of *arr* in a :class:`CurveHandle`."""
    cdef CurveHandle c = CurveHandle.__new__(CurveHandle)
    c.arr = arr
    c.h = h
    return c


cdef object _wrap_located(Arrangement arr, const Located& l):
    """``arr2d::Located`` -> :class:`Vertex`, :class:`Halfedge`, :class:`Face` or ``None``."""
    if l.type == 0:
        return _wrap_vertex(arr, l.as_vertex())
    if l.type == 1:
        return _wrap_halfedge(arr, l.as_halfedge())
    if l.type == 2:
        return _wrap_face(arr, l.as_face())
    return None


cdef VH _vh_of(Arrangement arr, object v) except *:
    if not isinstance(v, Vertex):
        raise TypeError("expected a Vertex, got %r" % (type(v).__name__,))
    if (<Vertex>v).arr is not arr:
        raise InvalidHandleError("this Vertex belongs to a different arrangement")
    return (<Vertex>v).h


cdef HH _hh_of(Arrangement arr, object h) except *:
    if not isinstance(h, Halfedge):
        raise TypeError("expected a Halfedge, got %r" % (type(h).__name__,))
    if (<Halfedge>h).arr is not arr:
        raise InvalidHandleError("this Halfedge belongs to a different arrangement")
    return (<Halfedge>h).h


cdef FH _fh_of(Arrangement arr, object f) except *:
    if not isinstance(f, Face):
        raise TypeError("expected a Face, got %r" % (type(f).__name__,))
    if (<Face>f).arr is not arr:
        raise InvalidHandleError("this Face belongs to a different arrangement")
    return (<Face>f).h


cdef CH _ch_of(Arrangement arr, object c) except *:
    if not isinstance(c, CurveHandle):
        raise TypeError("expected a CurveHandle, got %r" % (type(c).__name__,))
    if (<CurveHandle>c).arr is not arr:
        raise InvalidHandleError("this CurveHandle belongs to a different arrangement")
    return (<CurveHandle>c).h


# ---------------------------------------------------------------------------
# Shared geometry helpers
# ---------------------------------------------------------------------------

cdef object _wrap_geom_auto(const Geom* g):
    """Boxed point/curve (as delivered by an observer event) -> Point / Curve / None."""
    if g == NULL or g[0].empty():
        return None
    if g[0].type == GeomType.Point:
        return _wrap_point(g[0])
    return _wrap_curve(g[0])


cdef list _approximate_flat(const KindOps* o, const Geom& g, double tolerance,
                            object bbox, int dim):
    """Run ``KindOps::approximate`` and return a list of coordinate tuples."""
    cdef vector[double] out
    cdef BBox clip
    cdef BBox* clipp = NULL
    if bbox is not None:
        clip = _as_bbox(bbox)
        clipp = &clip
    o.approximate(g, tolerance, clipp, out)
    cdef size_t n = out.size()
    cdef size_t i
    cdef list pts = []
    i = 0
    while i + <size_t>dim <= n:
        if dim == 3:
            pts.append((out[i], out[i + 1], out[i + 2]))
        else:
            pts.append((out[i], out[i + 1]))
        i += <size_t>dim
    return pts


# ---------------------------------------------------------------------------
# Traits — direct access to the CGAL traits functors of a kind
# ---------------------------------------------------------------------------

cdef dict _TRAITS_CACHE = {}


cdef class Traits:
    """Direct access to the CGAL geometry-traits functors of one :class:`Kind`.

    Obtained from ``arrangement.traits`` or the module-level :func:`traits` function.
    Every method takes ordinary Python geometry (points as :class:`Point` or ``(x, y)``
    tuples, curves as any :class:`Curve`) and converts it to this kind first, so you can
    mix kinds freely as long as an exact conversion exists.

    Functors a traits class does not provide raise :class:`UnsupportedError` naming the
    kind and the functor (e.g. ``"bezier: Construct_x_monotone_curve_2 not available"``).
    """

    cdef Kind _kind
    cdef const KindOps* _ops

    def __cinit__(self, kind="segment"):
        self._kind = _ckind(kind)
        self._ops = &ops(self._kind)

    @property
    def kind(self):
        """The geometry :class:`Kind` these traits belong to."""
        return _kind_enum(self._kind)

    @property
    def dimension(self):
        """``2`` for planar kinds, ``3`` for the sphere kind (points are directions).

        :rtype: int
        """
        return self._ops.dimension()

    # ---- points ----------------------------------------------------------

    def compare_x(self, p, q):
        """``Compare_x_2``: compare two points by x only.

        :returns: ``-1`` if ``p.x < q.x``, ``0`` if equal, ``+1`` otherwise.
        :rtype: int
        """
        cdef Geom gp = _as_point(p, self._kind)
        cdef Geom gq = _as_point(q, self._kind)
        return self._ops.point_compare_x(gp, gq)

    def compare_xy(self, p, q):
        """``Compare_xy_2``: lexicographic comparison of two points.

        :rtype: int
        """
        cdef Geom gp = _as_point(p, self._kind)
        cdef Geom gq = _as_point(q, self._kind)
        return self._ops.point_compare_xy(gp, gq)

    def equal(self, p, q):
        """``Equal_2`` for points: exact equality.

        :rtype: bool
        """
        cdef Geom gp = _as_point(p, self._kind)
        cdef Geom gq = _as_point(q, self._kind)
        return bool(self._ops.point_equal(gp, gq))

    def curves_equal(self, c1, c2):
        """``Equal_2`` for x-monotone curves (same support and endpoints).

        :rtype: bool
        """
        cdef Geom a = _as_curve(c1, self._kind, True)
        cdef Geom b = _as_curve(c2, self._kind, True)
        return bool(self._ops.curve_equal(a, b))

    # ---- curve / point predicates ----------------------------------------

    def compare_y_at_x(self, point, curve):
        """``Compare_y_at_x_2``: is *point* below (-1), on (0) or above (+1) *curve*?

        CGAL precondition: *point* must be in the x-range of *curve*
        (check with :meth:`is_in_x_range`).

        :rtype: int
        """
        cdef Geom p = _as_point(point, self._kind)
        cdef Geom c = _as_curve(curve, self._kind, True)
        return self._ops.compare_y_at_x(p, c)

    def compare_y_at_x_left(self, c1, c2, point):
        """``Compare_y_at_x_left_2``: order of two curves immediately to the left of *point*.

        CGAL precondition: both curves pass through *point* and are defined to its left.

        :rtype: int
        """
        cdef Geom a = _as_curve(c1, self._kind, True)
        cdef Geom b = _as_curve(c2, self._kind, True)
        cdef Geom p = _as_point(point, self._kind)
        return self._ops.compare_y_at_x_left(a, b, p)

    def compare_y_at_x_right(self, c1, c2, point):
        """``Compare_y_at_x_right_2``: order of two curves immediately to the right of *point*.

        :rtype: int
        """
        cdef Geom a = _as_curve(c1, self._kind, True)
        cdef Geom b = _as_curve(c2, self._kind, True)
        cdef Geom p = _as_point(point, self._kind)
        return self._ops.compare_y_at_x_right(a, b, p)

    def is_in_x_range(self, curve, point):
        """``Is_in_x_range_2`` (synthesised by the traits adaptor).

        :rtype: bool
        """
        cdef Geom c = _as_curve(curve, self._kind, True)
        cdef Geom p = _as_point(point, self._kind)
        return bool(self._ops.is_in_x_range(c, p))

    def is_vertical(self, curve):
        """``Is_vertical_2`` for an x-monotone curve.

        :rtype: bool
        """
        cdef Geom c = _as_curve(curve, self._kind, True)
        return bool(self._ops.xcurve_is_vertical(c))

    def compare_endpoints_xy(self, curve):
        """``Compare_endpoints_xy_2``: ``-1`` when the curve is directed right, ``+1`` otherwise.

        :rtype: int
        """
        cdef Geom c = _as_curve(curve, self._kind, True)
        return self._ops.compare_endpoints_xy(c)

    # ---- curve constructions ---------------------------------------------

    def make_x_monotone(self, curve):
        """``Make_x_monotone_2``: subdivide *curve* into x-monotone pieces and isolated points.

        :returns: a list of :class:`Curve` and :class:`Point` objects, in order along the curve.
        :rtype: list
        """
        cdef Geom c = _as_curve(curve, self._kind, False)
        cdef vector[Geom] out
        self._ops.make_x_monotone(c, out)
        cdef list res = []
        cdef size_t i
        for i in range(out.size()):
            if out[i].type == GeomType.Point:
                res.append(_wrap_point(out[i]))
            else:
                res.append(_wrap_curve(out[i]))
        return res

    def split(self, curve, point):
        """``Split_2``: split an x-monotone curve at a point in its interior.

        :returns: ``(left_part, right_part)`` -- the first ends at *point*, the second starts there.
        :rtype: tuple[Curve, Curve]
        """
        cdef Geom c = _as_curve(curve, self._kind, True)
        cdef Geom p = _as_point(point, self._kind)
        cdef Geom left
        cdef Geom right
        self._ops.split(c, p, left, right)
        return (_wrap_curve(left), _wrap_curve(right))

    def intersect(self, c1, c2):
        """``Intersect_2``: intersections of two x-monotone curves.

        :returns: a list whose entries are either ``(Point, multiplicity)`` tuples
            (``multiplicity == 0`` means "not reported by the traits") or :class:`Curve`
            objects for overlapping pieces, in lexicographic order.
        :rtype: list
        """
        cdef Geom a = _as_curve(c1, self._kind, True)
        cdef Geom b = _as_curve(c2, self._kind, True)
        cdef vector[IntersectionResult] out
        self._ops.intersect(a, b, out)
        cdef list res = []
        cdef size_t i
        for i in range(out.size()):
            if out[i].is_point:
                res.append((_wrap_point(out[i].point), int(out[i].multiplicity)))
            else:
                res.append(_wrap_curve(out[i].overlap))
        return res

    def are_mergeable(self, c1, c2):
        """``Are_mergeable_2``: can the two x-monotone curves be merged into one?

        :rtype: bool
        """
        cdef Geom a = _as_curve(c1, self._kind, True)
        cdef Geom b = _as_curve(c2, self._kind, True)
        return bool(self._ops.are_mergeable(a, b))

    def merge(self, c1, c2):
        """``Merge_2``: merge two mergeable x-monotone curves.

        :rtype: Curve
        """
        cdef Geom a = _as_curve(c1, self._kind, True)
        cdef Geom b = _as_curve(c2, self._kind, True)
        return _wrap_curve(self._ops.merge(a, b))

    def trim(self, curve, source, target):
        """``Trim_2``: the sub-curve of *curve* between two points on it.

        :rtype: Curve
        """
        cdef Geom c = _as_curve(curve, self._kind, True)
        cdef Geom s = _as_point(source, self._kind)
        cdef Geom t = _as_point(target, self._kind)
        return _wrap_curve(self._ops.trim(c, s, t))

    def opposite(self, curve):
        """``Construct_opposite_2``: the same curve with reversed direction.

        :rtype: Curve
        """
        cdef Geom c = _as_curve(curve, self._kind, True)
        return _wrap_curve(self._ops.construct_opposite(c))

    def min_vertex(self, curve):
        """``Construct_min_vertex_2``: the lexicographically smaller endpoint.

        :rtype: Point
        """
        cdef Geom c = _as_curve(curve, self._kind, True)
        return _wrap_point(self._ops.xcurve_min_vertex(c))

    def max_vertex(self, curve):
        """``Construct_max_vertex_2``: the lexicographically larger endpoint.

        :rtype: Point
        """
        cdef Geom c = _as_curve(curve, self._kind, True)
        return _wrap_point(self._ops.xcurve_max_vertex(c))

    def construct_x_monotone_curve(self, p, q):
        """``Construct_x_monotone_curve_2(p, q)``: the straight x-monotone curve joining two points.

        Available for the ``segment``, ``linear``, ``polyline``, ``conic`` and ``sphere``
        kinds; raises :class:`UnsupportedError` for the others.

        :rtype: Curve
        """
        cdef Geom gp = _as_point(p, self._kind)
        cdef Geom gq = _as_point(q, self._kind)
        return _wrap_curve(self._ops.construct_x_monotone_curve(gp, gq))

    # ---- boundary ---------------------------------------------------------

    def parameter_space_in_x(self, curve, end="min"):
        """``Parameter_space_in_x_2``: where the given end of *curve* lies in x.

        :param end: ``"min"`` or ``"max"``.
        :returns: ``"interior"``, ``"left"`` or ``"right"``.
        :rtype: str
        """
        cdef Geom c = _as_curve(curve, self._kind, True)
        return _param_space_name(self._ops.parameter_space_in_x(c, _curve_end_arg(end)))

    def parameter_space_in_y(self, curve, end="min"):
        """``Parameter_space_in_y_2``: where the given end of *curve* lies in y.

        :param end: ``"min"`` or ``"max"``.
        :returns: ``"interior"``, ``"bottom"`` or ``"top"``.
        :rtype: str
        """
        cdef Geom c = _as_curve(curve, self._kind, True)
        return _param_space_name(self._ops.parameter_space_in_y(c, _curve_end_arg(end)))

    # ---- approximation ----------------------------------------------------

    def approximate(self, curve, double tolerance=1e-3, bbox=None):
        """``Approximate_2``: polyline approximation of a curve.

        :param tolerance: maximum deviation, in coordinate units.
        :param bbox: ``(xmin, ymin, xmax, ymax)`` used to clip unbounded curves
            (rays and lines of the ``linear`` kind *require* it).
        :returns: a list of ``(x, y)`` (or ``(x, y, z)`` for the sphere kind) float tuples,
            from the curve's source to its target.
        :rtype: list[tuple[float, ...]]
        """
        cdef Geom c = _as_curve(curve, self._kind, False)
        return _approximate_flat(self._ops, c, tolerance, bbox, self._ops.dimension())

    def approximate_point(self, point, int coordinate=0):
        """``Approximate_2`` on a single point coordinate.

        :param coordinate: ``0`` = x, ``1`` = y, ``2`` = z (sphere kind).
        :rtype: float
        """
        cdef Geom p = _as_point(point, self._kind)
        return self._ops.approximate_coordinate(p, coordinate)

    def __repr__(self):
        return "Traits(kind='%s')" % (_kind_name(self._kind),)


def traits(kind):
    """Return the (cached) :class:`Traits` facade of a geometry *kind*.

    :param kind: a :class:`Kind`, kind name, integer or any object with a ``.kind``.
    :rtype: Traits
    """
    cdef Kind k = _ckind(kind)
    cdef object key = <int>k
    obj = _TRAITS_CACHE.get(key)
    if obj is None:
        obj = Traits(_kind_enum(k))
        _TRAITS_CACHE[key] = obj
    return obj


# ---------------------------------------------------------------------------
# Observers
# ---------------------------------------------------------------------------
#
# Argument codes used to build the callback argument tuple from an ObsEventData.

cdef enum _ObsArgCode:
    _A_V1 = 1
    _A_V2 = 2
    _A_H1 = 3
    _A_H2 = 4
    _A_H3 = 5
    _A_F1 = 6
    _A_F2 = 7
    _A_G1 = 8
    _A_G2 = 9
    _A_FLAG = 10
    _A_CURVE_END = 11    # i1: -1 -> None, else "min"/"max"
    _A_PS_X = 12         # i2 as a parameter-space name
    _A_PS_Y = 13         # i3 as a parameter-space name


# name -> tuple of argument codes.  Mirrors the per-event comments in
# src/arr2d/include/arr2d/arrangement.hpp (which mirror CGAL's Aos_observer signatures).
cdef dict _OBS_ARGS = {
    "before_assign": (),
    "after_assign": (),
    "before_clear": (),
    "after_clear": (),
    "before_global_change": (),
    "after_global_change": (),
    "before_attach": (),
    "after_attach": (),
    "before_detach": (),
    "after_detach": (),
    "before_create_vertex": (_A_G1,),
    "after_create_vertex": (_A_V1,),
    "before_create_boundary_vertex": (_A_G1, _A_CURVE_END, _A_PS_X, _A_PS_Y),
    "after_create_boundary_vertex": (_A_V1,),
    "before_create_edge": (_A_G1, _A_V1, _A_V2),
    "after_create_edge": (_A_H1,),
    "before_modify_vertex": (_A_V1, _A_G1),
    "after_modify_vertex": (_A_V1,),
    "before_modify_edge": (_A_H1, _A_G1),
    "after_modify_edge": (_A_H1,),
    "before_split_edge": (_A_H1, _A_V1, _A_G1, _A_G2),
    "after_split_edge": (_A_H1, _A_H2),
    "before_split_fictitious_edge": (_A_H1, _A_V1),
    "after_split_fictitious_edge": (_A_H1, _A_H2),
    "before_split_face": (_A_F1, _A_H1),
    "after_split_face": (_A_F1, _A_F2, _A_FLAG),
    "before_split_outer_ccb": (_A_F1, _A_H1, _A_H2),
    "after_split_outer_ccb": (_A_F1, _A_H1, _A_H2),
    "before_split_inner_ccb": (_A_F1, _A_H1, _A_H2),
    "after_split_inner_ccb": (_A_F1, _A_H1, _A_H2),
    "before_add_outer_ccb": (_A_F1, _A_H1),
    "after_add_outer_ccb": (_A_H1,),
    "before_add_inner_ccb": (_A_F1, _A_H1),
    "after_add_inner_ccb": (_A_H1,),
    "before_add_isolated_vertex": (_A_F1, _A_V1),
    "after_add_isolated_vertex": (_A_V1,),
    "before_merge_edge": (_A_H1, _A_H2, _A_G1),
    "after_merge_edge": (_A_H1,),
    "before_merge_fictitious_edge": (_A_H1, _A_H2),
    "after_merge_fictitious_edge": (_A_H1,),
    "before_merge_face": (_A_F1, _A_F2, _A_H1),
    "after_merge_face": (_A_F1,),
    "before_merge_outer_ccb": (_A_F1, _A_H1, _A_H2, _A_H3),
    "after_merge_outer_ccb": (_A_F1, _A_H1),
    "before_merge_inner_ccb": (_A_F1, _A_H1, _A_H2, _A_H3),
    "after_merge_inner_ccb": (_A_F1, _A_H1),
    "before_move_outer_ccb": (_A_F1, _A_F2, _A_H1),
    "after_move_outer_ccb": (_A_H1,),
    "before_move_inner_ccb": (_A_F1, _A_F2, _A_H1),
    "after_move_inner_ccb": (_A_H1,),
    "before_move_isolated_vertex": (_A_F1, _A_F2, _A_V1),
    "after_move_isolated_vertex": (_A_V1,),
    "before_remove_vertex": (_A_V1,),
    "after_remove_vertex": (),
    "before_remove_edge": (_A_H1,),
    "after_remove_edge": (),
    "before_remove_outer_ccb": (_A_F1, _A_H1),
    "after_remove_outer_ccb": (_A_F1,),
    "before_remove_inner_ccb": (_A_F1, _A_H1),
    "after_remove_inner_ccb": (_A_F1,),
}


# The event names, in ObsEvent order, straight from the C++ core.
cdef list _make_obs_event_names():
    cdef list names = []
    cdef int i
    for i in range(<int>ObsEvent.NumEvents):
        names.append(_from_cstr(obs_event_name(<ObsEvent>i)))
    return names


cdef list _OBS_EVENT_NAMES = _make_obs_event_names()
# Per-event argument spec, indexed like _OBS_EVENT_NAMES (empty tuple for anything the
# C++ core adds that this file does not know about yet).
cdef list _OBS_EVENT_ARGS = [_OBS_ARGS.get(name, ()) for name in _OBS_EVENT_NAMES]


class Observer(object):
    """Base class for arrangement observers.

    Subclass it, override the notifications you care about and attach the instance with
    :meth:`Arrangement.add_observer`.  Every method is a no-op here, and methods you do
    **not** override are never called at all (the dispatcher compares them against this
    class), so an observer that only implements ``after_split_face`` costs almost nothing.

    All handles handed to a notification are live at that moment.  ``before_remove_*``
    notifications still see the element; ``after_remove_*`` do not receive it at all.
    Curves and points are passed as :class:`Curve` / :class:`Point` objects.

    An exception raised inside a notification is captured and re-raised from the
    arrangement method that triggered it (CGAL cannot unwind through the sweep).
    """

    # ---- global operations ------------------------------------------------
    def before_assign(self): """Called before the arrangement is overwritten by another one."""
    def after_assign(self): """Called after the arrangement has been overwritten."""
    def before_clear(self): """Called before the arrangement is cleared."""
    def after_clear(self): """Called after the arrangement has been cleared."""
    def before_global_change(self): """Called before a global (sweep-based) operation starts."""
    def after_global_change(self): """Called after a global (sweep-based) operation finished."""
    def before_attach(self): """Called before this observer is attached to an arrangement."""
    def after_attach(self): """Called after this observer has been attached."""
    def before_detach(self): """Called before this observer is detached."""
    def after_detach(self): """Called after this observer has been detached."""

    # ---- vertices ---------------------------------------------------------
    def before_create_vertex(self, point): """A vertex is about to be created at *point*."""
    def after_create_vertex(self, v): """Vertex *v* was created."""
    def before_create_boundary_vertex(self, geometry, end, ps_x, ps_y):
        """A vertex on the parameter-space boundary is about to be created.

        *geometry* is a :class:`Point` (then *end* is ``None``) or the :class:`Curve`
        whose *end* (``"min"`` / ``"max"``) reaches the boundary; *ps_x* / *ps_y* are
        parameter-space names (``"interior"``, ``"left"``, ``"right"``, ``"bottom"``,
        ``"top"``).
        """
    def after_create_boundary_vertex(self, v): """Boundary vertex *v* was created."""
    def before_modify_vertex(self, v, point): """Vertex *v* is about to be moved to *point*."""
    def after_modify_vertex(self, v): """Vertex *v* was modified."""
    def before_add_isolated_vertex(self, f, v): """Isolated vertex *v* is about to be added to face *f*."""
    def after_add_isolated_vertex(self, v): """Isolated vertex *v* was added."""
    def before_move_isolated_vertex(self, from_face, to_face, v):
        """Isolated vertex *v* is about to move between two faces."""
    def after_move_isolated_vertex(self, v): """Isolated vertex *v* was moved."""
    def before_remove_vertex(self, v): """Vertex *v* is about to be removed."""
    def after_remove_vertex(self): """A vertex was removed."""

    # ---- edges ------------------------------------------------------------
    def before_create_edge(self, curve, v1, v2): """An edge for *curve* between *v1* and *v2* is about to be created."""
    def after_create_edge(self, e): """Edge *e* (one of the two halfedges) was created."""
    def before_modify_edge(self, e, curve): """Edge *e* is about to be re-associated with *curve*."""
    def after_modify_edge(self, e): """Edge *e* was modified."""
    def before_split_edge(self, e, v, c1, c2):
        """Edge *e* is about to be split at vertex *v* into curves *c1* and *c2*."""
    def after_split_edge(self, e1, e2): """An edge was split into *e1* and *e2*."""
    def before_split_fictitious_edge(self, e, v):
        """A fictitious edge (unbounded kinds only) is about to be split at *v*."""
    def after_split_fictitious_edge(self, e1, e2): """A fictitious edge was split into *e1* and *e2*."""
    def before_merge_edge(self, e1, e2, curve): """Edges *e1* and *e2* are about to be merged into *curve*."""
    def after_merge_edge(self, e): """Two edges were merged into *e*."""
    def before_merge_fictitious_edge(self, e1, e2): """Two fictitious edges are about to be merged."""
    def after_merge_fictitious_edge(self, e): """Two fictitious edges were merged into *e*."""
    def before_remove_edge(self, e): """Edge *e* is about to be removed."""
    def after_remove_edge(self): """An edge was removed."""

    # ---- faces ------------------------------------------------------------
    def before_split_face(self, f, e): """Face *f* is about to be split by edge *e*."""
    def after_split_face(self, f, new_f, is_hole):
        """Face *f* was split; *new_f* is the new face, *is_hole* says whether it became a hole."""
    def before_merge_face(self, f1, f2, e): """Faces *f1* and *f2* are about to merge (edge *e* disappears)."""
    def after_merge_face(self, f): """Two faces were merged into *f*."""

    # ---- connected components of the boundary -----------------------------
    def before_split_outer_ccb(self, f, ccb, e): """An outer CCB of *f* is about to be split at *e*."""
    def after_split_outer_ccb(self, f, ccb1, ccb2): """An outer CCB of *f* was split into two."""
    def before_split_inner_ccb(self, f, ccb, e): """An inner CCB (hole) of *f* is about to be split at *e*."""
    def after_split_inner_ccb(self, f, ccb1, ccb2): """An inner CCB of *f* was split into two."""
    def before_add_outer_ccb(self, f, e): """A new outer CCB is about to be added to *f*."""
    def after_add_outer_ccb(self, ccb): """A new outer CCB was added."""
    def before_add_inner_ccb(self, f, e): """A new inner CCB (hole) is about to be added to *f*."""
    def after_add_inner_ccb(self, ccb): """A new inner CCB was added."""
    def before_merge_outer_ccb(self, f, ccb1, ccb2, e): """Two outer CCBs of *f* are about to merge (edge *e* is removed)."""
    def after_merge_outer_ccb(self, f, ccb): """Two outer CCBs of *f* were merged."""
    def before_merge_inner_ccb(self, f, ccb1, ccb2, e): """Two inner CCBs of *f* are about to merge."""
    def after_merge_inner_ccb(self, f, ccb): """Two inner CCBs of *f* were merged."""
    def before_move_outer_ccb(self, from_face, to_face, ccb): """An outer CCB is about to move between faces."""
    def after_move_outer_ccb(self, ccb): """An outer CCB was moved."""
    def before_move_inner_ccb(self, from_face, to_face, ccb): """An inner CCB is about to move between faces."""
    def after_move_inner_ccb(self, ccb): """An inner CCB was moved."""
    def before_remove_outer_ccb(self, f, ccb): """An outer CCB of *f* is about to be removed."""
    def after_remove_outer_ccb(self, f): """An outer CCB of *f* was removed."""
    def before_remove_inner_ccb(self, f, ccb): """An inner CCB of *f* is about to be removed."""
    def after_remove_inner_ccb(self, f): """An inner CCB of *f* was removed."""


def _install_missing_observer_methods():
    """Give :class:`Observer` a no-op for every event the C++ core reports.

    Guarantees the dispatcher can always find a base implementation to compare against,
    even if ``arr2d::ObsEvent`` gains an event this file does not list explicitly.
    """
    for name in _OBS_EVENT_NAMES:
        if name is None or hasattr(Observer, name):
            continue

        def _noop(self, *args, _name=name):
            """Auto-generated no-op notification."""
            return None

        _noop.__name__ = name
        _noop.__qualname__ = "Observer." + name
        setattr(Observer, name, _noop)


_install_missing_observer_methods()


cdef class _ObsCtx:
    """Per-observer dispatch context handed to the C++ core as an opaque ``void*``."""

    cdef Arrangement arr
    cdef object observer
    cdef dict table          # event name -> bound method (only overridden ones)
    cdef int token


cdef dict _observer_table(object obs):
    """Bound methods of *obs* that actually override :class:`Observer`'s no-ops."""
    cdef dict table = {}
    for name in _OBS_EVENT_NAMES:
        if name is None:
            continue
        meth = getattr(obs, name, None)
        if meth is None:
            continue
        base = getattr(Observer, name, None)
        if base is not None and getattr(meth, "__func__", None) is base:
            continue                      # not overridden: skip it for speed
        table[name] = meth
    return table


cdef tuple _observer_args(Arrangement arr, const ObsEventData& ev, tuple spec):
    cdef list args = []
    cdef int code
    for code in spec:
        if code == _A_V1:
            args.append(_wrap_vertex(arr, ev.v1))
        elif code == _A_V2:
            args.append(_wrap_vertex(arr, ev.v2))
        elif code == _A_H1:
            args.append(_wrap_halfedge(arr, ev.h1))
        elif code == _A_H2:
            args.append(_wrap_halfedge(arr, ev.h2))
        elif code == _A_H3:
            args.append(_wrap_halfedge(arr, ev.h3))
        elif code == _A_F1:
            args.append(_wrap_face(arr, ev.f1))
        elif code == _A_F2:
            args.append(_wrap_face(arr, ev.f2))
        elif code == _A_G1:
            args.append(_wrap_geom_auto(ev.g1))
        elif code == _A_G2:
            args.append(_wrap_geom_auto(ev.g2))
        elif code == _A_FLAG:
            args.append(bool(ev.flag))
        elif code == _A_CURVE_END:
            args.append(None if ev.i1 < 0 else ("min" if ev.i1 == <int>ARR_MIN_END else "max"))
        elif code == _A_PS_X:
            args.append(_param_space_name(ev.i2))
        elif code == _A_PS_Y:
            args.append(_param_space_name(ev.i3))
        else:
            args.append(None)
    return tuple(args)


cdef void _observer_dispatch(void* user, const ObsEventData& ev) noexcept:
    """C callback handed to ``ArrBase::add_observer``.

    Must not throw: a Python exception is stored on the arrangement and re-raised by
    the method that triggered the notification.
    """
    cdef _ObsCtx ctx = None
    cdef Arrangement arr = None
    cdef int idx
    try:
        if user == NULL:
            return
        ctx = <_ObsCtx>user
        arr = ctx.arr
        if arr is None or ctx.table is None:
            return                                     # context was cleared by the GC
        idx = <int>ev.event
        if idx < 0 or idx >= len(_OBS_EVENT_NAMES):
            return
        name = _OBS_EVENT_NAMES[idx]
        meth = ctx.table.get(name)
        if meth is None:
            return
        meth(*_observer_args(arr, ev, <tuple>_OBS_EVENT_ARGS[idx]))
    except BaseException as exc:
        try:
            if ctx is not None and ctx.arr is not None and ctx.arr._pending is None:
                ctx.arr._pending = exc
        except BaseException:
            pass


# ---------------------------------------------------------------------------
# Overlay
# ---------------------------------------------------------------------------

cdef enum _OverlayHandleKind:
    _E_VERTEX = 0
    _E_HALFEDGE = 1
    _E_FACE = 2

# (method name, type of the A handle, type of the B handle, type of the R handle),
# indexed by arr2d::OverlayEvent.
cdef tuple _OVERLAY_SPEC = (
    ("vertex_vertex", _E_VERTEX, _E_VERTEX, _E_VERTEX),
    ("vertex_edge", _E_VERTEX, _E_HALFEDGE, _E_VERTEX),
    ("vertex_face", _E_VERTEX, _E_FACE, _E_VERTEX),
    ("edge_vertex", _E_HALFEDGE, _E_VERTEX, _E_VERTEX),
    ("face_vertex", _E_FACE, _E_VERTEX, _E_VERTEX),
    ("edge_edge_vertex", _E_HALFEDGE, _E_HALFEDGE, _E_VERTEX),
    ("edge_edge", _E_HALFEDGE, _E_HALFEDGE, _E_HALFEDGE),
    ("edge_face", _E_HALFEDGE, _E_FACE, _E_HALFEDGE),
    ("face_edge", _E_FACE, _E_HALFEDGE, _E_HALFEDGE),
    ("face_face", _E_FACE, _E_FACE, _E_FACE),
)


class OverlayCallbacks(object):
    """Base class for :meth:`Arrangement.overlay` callbacks (CGAL's ``OverlayTraits``).

    Subclass it and override the notifications you need.  In every method the first
    argument is a handle into the first arrangement (``A``), the second into the second
    (``B``) and the third into the freshly built result (``R``); set ``r.data = ...`` to
    decorate the result.

    Methods you do not override are skipped entirely.  Any exception raised is re-raised
    from :meth:`Arrangement.overlay` once the sweep has finished.
    """

    def vertex_vertex(self, va, vb, vr):
        """A vertex of A and a vertex of B coincide at result vertex *vr*."""

    def vertex_edge(self, va, eb, vr):
        """Vertex *va* of A lies on edge *eb* of B; *vr* is the result vertex."""

    def vertex_face(self, va, fb, vr):
        """Vertex *va* of A lies inside face *fb* of B; *vr* is the result vertex."""

    def edge_vertex(self, ea, vb, vr):
        """Vertex *vb* of B lies on edge *ea* of A; *vr* is the result vertex."""

    def face_vertex(self, fa, vb, vr):
        """Vertex *vb* of B lies inside face *fa* of A; *vr* is the result vertex."""

    def edge_edge_vertex(self, ea, eb, vr):
        """Edges *ea* (A) and *eb* (B) intersect at the new result vertex *vr*."""

    def edge_edge(self, ea, eb, er):
        """Edges *ea* (A) and *eb* (B) overlap along the result edge *er*."""

    def edge_face(self, ea, fb, er):
        """Edge *ea* of A runs through face *fb* of B; *er* is the result edge."""

    def face_edge(self, fa, eb, er):
        """Edge *eb* of B runs through face *fa* of A; *er* is the result edge."""

    def face_face(self, fa, fb, fr):
        """Faces *fa* (A) and *fb* (B) overlap in the result face *fr*."""


cdef class _OverlayCtx:
    """Per-overlay dispatch context handed to the C++ core as an opaque ``void*``."""

    cdef Arrangement a
    cdef Arrangement b
    cdef Arrangement r
    cdef dict table          # method name -> callable(a, b, r)
    cdef object on_vertex
    cdef object on_edge
    cdef object on_face


cdef object _wrap_overlay_handle(Arrangement arr, int what, void* p, uint64_t ident):
    cdef VH v
    cdef HH h
    cdef FH f
    if p == NULL:
        return None
    if what == _E_VERTEX:
        v.p = p
        v.id = ident
        return _wrap_vertex(arr, v)
    if what == _E_HALFEDGE:
        h.p = p
        h.id = ident
        return _wrap_halfedge(arr, h)
    f.p = p
    f.id = ident
    return _wrap_face(arr, f)


cdef void _overlay_dispatch(void* user, const OverlayEventData& ev) noexcept:
    """C callback handed to ``ArrBase::overlay_with``. Must not throw."""
    cdef _OverlayCtx ctx = None
    cdef int idx
    try:
        if user == NULL:
            return
        ctx = <_OverlayCtx>user
        if ctx.r is None:
            return
        idx = <int>ev.event
        if idx < 0 or idx >= len(_OVERLAY_SPEC):
            return
        spec = <tuple>_OVERLAY_SPEC[idx]
        name = spec[0]
        conv = ctx.table.get(name) if ctx.table is not None else None
        which = spec[3]
        hook = None
        if which == _E_VERTEX:
            hook = ctx.on_vertex
        elif which == _E_HALFEDGE:
            hook = ctx.on_edge
        else:
            hook = ctx.on_face
        if conv is None and hook is None:
            return
        ea = _wrap_overlay_handle(ctx.a, <int>spec[1], ev.a, ev.a_id)
        eb = _wrap_overlay_handle(ctx.b, <int>spec[2], ev.b, ev.b_id)
        er = _wrap_overlay_handle(ctx.r, which, ev.r, ev.r_id)
        if conv is not None:
            conv(ea, eb, er)
        if hook is not None and er is not None:
            er.data = hook(ea, eb)
    except BaseException as exc:
        try:
            if ctx is not None and ctx.r is not None and ctx.r._pending is None:
                ctx.r._pending = exc
        except BaseException:
            pass


cdef dict _overlay_table(object callbacks):
    """Callbacks of *callbacks* that actually do something (name -> callable)."""
    cdef dict table = {}
    if callbacks is None:
        return table
    if isinstance(callbacks, dict):
        for key, value in (<dict>callbacks).items():
            if value is None:
                continue
            if key not in [spec[0] for spec in _OVERLAY_SPEC]:
                raise ValueError(
                    "unknown overlay callback %r; expected one of %s"
                    % (key, ", ".join(spec[0] for spec in _OVERLAY_SPEC))
                )
            if not callable(value):
                raise TypeError("overlay callback %r is not callable" % (key,))
            table[key] = value
        return table
    for spec in _OVERLAY_SPEC:
        name = spec[0]
        meth = getattr(callbacks, name, None)
        if meth is None:
            continue
        base = getattr(OverlayCallbacks, name, None)
        if base is not None and getattr(meth, "__func__", None) is base:
            continue
        table[name] = meth
    return table


# ---------------------------------------------------------------------------
# Arrangement
# ---------------------------------------------------------------------------

cdef object _NO_INIT = object()   # sentinel: build an Arrangement without allocating


cdef Arrangement _arrangement_from_ptr(unique_ptr[ArrBase]& p):
    """Take ownership of a C++ arrangement and wrap it in a new :class:`Arrangement`."""
    if p.get() == NULL:
        raise RuntimeError("null arrangement pointer")
    cdef Arrangement a = Arrangement.__new__(Arrangement, _NO_INIT)
    a.arr = move(p)
    a._kind = a.arr.get().kind()
    return a


cdef class Arrangement:
    """A 2D arrangement of curves of one geometry :class:`Kind`, with curve history.

    Wraps ``CGAL::Arrangement_with_history_2`` (or
    ``Arrangement_on_surface_with_history_2`` for the sphere kind) over an extended DCEL,
    so every vertex, halfedge and face carries an arbitrary Python object (``.data``) and
    every input curve can be queried for the edges it induces (and removed again).

    :param kind: the geometry kind (default ``"segment"``); a :class:`Kind`, a kind name,
        an integer or any object with a ``.kind``.

    >>> arr = Arrangement("segment")                       # doctest: +SKIP
    >>> arr.insert([Segment((0, 0), (1, 1)), Segment((0, 1), (1, 0))])   # doctest: +SKIP
    """

    def __cinit__(self, kind="segment"):
        self._observers = {}
        self._pending = None
        if kind is _NO_INIT:
            self._kind = <Kind>0
            return
        self._kind = _ckind(kind)
        self.arr = make_arrangement(self._kind)

    def __init__(self, kind="segment"):
        # everything happens in __cinit__; defined so that help() shows the signature
        pass

    # ---- identity / kind --------------------------------------------------

    @property
    def kind(self):
        """The geometry :class:`Kind` of this arrangement."""
        return _kind_enum(self._kind)

    @property
    def is_unbounded_kind(self):
        """``True`` for kinds with an unbounded planar topology (``linear``).

        Those arrangements have fictitious vertices, halfedges and a fictitious face
        outside the bounding rectangle.

        :rtype: bool
        """
        return bool(_abase(self).is_unbounded_kind())

    @property
    def dimension(self):
        """``2`` for planar kinds, ``3`` for the sphere kind.

        :rtype: int
        """
        cdef const KindOps* o = &ops(self._kind)
        return o.dimension()

    @property
    def traits(self):
        """The :class:`Traits` facade of this arrangement's kind."""
        return traits(_kind_enum(self._kind))

    # ---- sizes ------------------------------------------------------------

    @property
    def number_of_vertices(self):
        """Number of concrete vertices (vertices at infinity are not counted).

        :rtype: int
        """
        return _abase(self).number_of_vertices()

    @property
    def number_of_isolated_vertices(self):
        """Number of vertices that are not incident to any edge.

        :rtype: int
        """
        return _abase(self).number_of_isolated_vertices()

    @property
    def number_of_vertices_at_infinity(self):
        """Number of vertices on the open boundary (``0`` for bounded kinds).

        :rtype: int
        """
        return _abase(self).number_of_vertices_at_infinity()

    @property
    def number_of_halfedges(self):
        """Number of concrete halfedges (twice :attr:`number_of_edges`).

        :rtype: int
        """
        return _abase(self).number_of_halfedges()

    @property
    def number_of_edges(self):
        """Number of edges (halfedge pairs).

        :rtype: int
        """
        return _abase(self).number_of_edges()

    @property
    def number_of_faces(self):
        """Number of faces, including the unbounded one, excluding the fictitious one.

        :rtype: int
        """
        return _abase(self).number_of_faces()

    @property
    def number_of_unbounded_faces(self):
        """Number of unbounded faces (``0`` on the sphere: nothing is unbounded there).

        :rtype: int
        """
        return _abase(self).number_of_unbounded_faces()

    @property
    def number_of_curves(self):
        """Number of input curves recorded in the history.

        :rtype: int
        """
        return _abase(self).number_of_curves()

    def __len__(self):
        """The number of edges."""
        return _abase(self).number_of_edges()

    @property
    def is_empty(self):
        """``True`` when the arrangement has no vertices, edges or isolated features.

        :rtype: bool
        """
        return bool(_abase(self).is_empty())

    def is_valid(self):
        """Run CGAL's full topological and geometric validity check.

        :rtype: bool
        """
        return bool(_abase(self).is_valid())

    # ---- whole-arrangement operations ------------------------------------

    def clear(self):
        """Remove everything, leaving an empty arrangement of the same kind."""
        _abase(self).clear()
        _check_pending(self)

    def copy(self):
        """Return a deep copy, including the curve history and the element ``data``.

        The ``.data`` objects themselves are **shared**, not copied: the copy holds new
        references to the very same Python objects (this mirrors ``copy.copy``).
        Observers and attached point-location strategies are *not* copied.

        :rtype: Arrangement
        """
        cdef unique_ptr[ArrBase] p = _abase(self).clone()
        return _arrangement_from_ptr(p)

    def __copy__(self):
        return self.copy()

    def __deepcopy__(self, memo):
        """Same as :meth:`copy`: the DCEL is duplicated, ``.data`` objects are shared.

        The element data cannot be deep-copied without breaking identity relations that
        callbacks may rely on, so it is deliberately shared; deep-copy them yourself if
        you need to.
        """
        return self.copy()

    def assign(self, Arrangement other):
        """Replace the contents of this arrangement with a copy of *other* (same kind).

        Every handle into this arrangement becomes invalid.
        """
        if other is None:
            raise TypeError("expected an Arrangement")
        _abase(self).assign(_abase(other)[0])
        _check_pending(self)

    # ---- iteration --------------------------------------------------------

    def vertices(self):
        """All concrete vertices, in CGAL iteration order.

        :rtype: list[Vertex]
        """
        cdef vector[VH] out
        _abase(self).vertices(out)
        cdef list res = []
        cdef size_t i
        for i in range(out.size()):
            res.append(_wrap_vertex(self, out[i]))
        return res

    def halfedges(self):
        """All concrete halfedges (both of each pair).

        :rtype: list[Halfedge]
        """
        cdef vector[HH] out
        _abase(self).halfedges(out)
        cdef list res = []
        cdef size_t i
        for i in range(out.size()):
            res.append(_wrap_halfedge(self, out[i]))
        return res

    def edges(self):
        """One representative halfedge per edge.

        :rtype: list[Halfedge]
        """
        cdef vector[HH] out
        _abase(self).edges(out)
        cdef list res = []
        cdef size_t i
        for i in range(out.size()):
            res.append(_wrap_halfedge(self, out[i]))
        return res

    def faces(self):
        """All faces (the fictitious face of unbounded kinds is excluded).

        :rtype: list[Face]
        """
        cdef vector[FH] out
        _abase(self).faces(out)
        cdef list res = []
        cdef size_t i
        for i in range(out.size()):
            res.append(_wrap_face(self, out[i]))
        return res

    def unbounded_faces(self):
        """All unbounded faces (empty on the sphere).

        :rtype: list[Face]
        """
        cdef vector[FH] out
        _abase(self).unbounded_faces(out)
        cdef list res = []
        cdef size_t i
        for i in range(out.size()):
            res.append(_wrap_face(self, out[i]))
        return res

    def bounded_faces(self):
        """All faces that are not unbounded.

        :rtype: list[Face]
        """
        cdef vector[FH] out
        cdef ArrBase* a = _abase(self)
        a.faces(out)
        cdef list res = []
        cdef size_t i
        for i in range(out.size()):
            if not a.face_is_unbounded(out[i]):
                res.append(_wrap_face(self, out[i]))
        return res

    def curves(self):
        """All input curves recorded in the history.

        :rtype: list[CurveHandle]
        """
        cdef vector[CH] out
        _abase(self).curves(out)
        cdef list res = []
        cdef size_t i
        for i in range(out.size()):
            res.append(_wrap_curve_handle(self, out[i]))
        return res

    @property
    def unbounded_face(self):
        """The distinguished outer face.

        * bounded planar kinds: the single unbounded face;
        * ``linear``: *one* of the unbounded faces (use :meth:`unbounded_faces` for all);
        * ``sphere``: the spherical (reference) face -- the unique face without an outer
          CCB, the one containing the north pole.  It is also available as
          :attr:`spherical_face`.

        :rtype: Face
        """
        return _wrap_face(self, _abase(self).unbounded_face())

    @property
    def spherical_face(self):
        """The spherical (reference) face; ``sphere`` kind only.

        On the sphere nothing is unbounded (``number_of_unbounded_faces() == 0``); the
        distinguished face is the unique one with no outer CCB.

        :rtype: Face
        :raises UnsupportedError: for any other kind.
        """
        if self._kind != Kind.Sphere:
            raise UnsupportedError(
                "spherical_face is only defined for the 'sphere' kind, not for '%s'; "
                "use unbounded_face instead" % (_kind_name(self._kind),)
            )
        return _wrap_face(self, _abase(self).unbounded_face())

    @property
    def fictitious_face(self):
        """The fictitious face outside the bounding rectangle; ``linear`` kind only.

        :rtype: Face
        :raises UnsupportedError: for every other kind (bounded topologies have none,
            and the spherical topology traits has no ``initial_face`` at all).
        """
        return _wrap_face(self, _abase(self).fictitious_face())

    # ---- insertion --------------------------------------------------------

    def insert(self, obj):
        """Insert a point, a curve, a polygon or any iterable of curves.

        Dispatches on the argument:

        * a :class:`Point` or an ``(x, y)`` / ``(x, y, z)`` tuple -> :meth:`insert_point`,
          returning the :class:`Vertex`;
        * a single :class:`Curve` -> :meth:`insert_curves` with one curve, returning its
          :class:`CurveHandle`;
        * a :class:`Polygon`, a :class:`PolygonWithHoles` or any iterable of curves /
          polygons -> aggregate (sweep-line) insertion, returning a list of
          :class:`CurveHandle`.

        Curves of another kind are converted when an exact conversion exists; general
        (non x-monotone) curves are subdivided by CGAL.  Everything inserted this way is
        recorded in the curve history.

        :rtype: Vertex | CurveHandle | list[CurveHandle]
        """
        if isinstance(obj, Point) or _looks_like_point(obj, self._kind):
            return self.insert_point(obj)
        if isinstance(obj, Curve):
            handles = self.insert_curves([obj])
            return handles[0] if handles else None
        return self.insert_curves(obj)

    def insert_curves(self, curves):
        """Aggregate (sweep-line based) insertion of many curves, with history.

        :param curves: a :class:`Curve`, a :class:`Polygon` / :class:`PolygonWithHoles`,
            or any iterable of those.
        :returns: one :class:`CurveHandle` per input curve, in input order.
        :rtype: list[CurveHandle]
        """
        cdef list items = _convert_curves(curves, self._kind)
        cdef vector[Geom] gs
        cdef vector[CH] out
        cdef Py_ssize_t i
        gs.reserve(len(items))
        for i in range(len(items)):
            gs.push_back((<Curve>items[i]).g)
        _abase(self).insert_curves(gs, out)
        _check_pending(self)
        cdef list res = []
        cdef size_t j
        for j in range(out.size()):
            res.append(_wrap_curve_handle(self, out[j]))
        return res

    def insert_point(self, point):
        """Insert an isolated point (``CGAL::insert_point``); no history is recorded.

        If the point already lies on an edge the edge is split; if it coincides with an
        existing vertex that vertex is returned.

        :rtype: Vertex
        """
        cdef Geom p = _as_point(point, self._kind)
        cdef VH v = _abase(self).insert_point(p)
        _check_pending(self)
        return _wrap_vertex(self, v)

    def insert_non_intersecting(self, curve):
        """Insert one x-monotone curve known to be disjoint from the arrangement's interior.

        Much faster than :meth:`insert`, but **no history is recorded** and CGAL raises
        :class:`PreconditionError` if the curve does intersect existing features.

        :rtype: Halfedge
        """
        cdef Geom c = _as_curve(curve, self._kind, True)
        cdef HH h = _abase(self).insert_non_intersecting_curve(c)
        _check_pending(self)
        return _wrap_halfedge(self, h)

    def insert_non_intersecting_curves(self, curves):
        """Aggregate insertion of pairwise interior-disjoint x-monotone curves (no history)."""
        cdef list items = _convert_curves(curves, self._kind)
        cdef vector[Geom] gs
        cdef Py_ssize_t i
        cdef Geom g
        cdef const KindOps* o = &ops(self._kind)
        gs.reserve(len(items))
        for i in range(len(items)):
            g = o.to_x_monotone((<Curve>items[i]).g)
            gs.push_back(g)
        _abase(self).insert_non_intersecting_curves(gs)
        _check_pending(self)

    def insert_in_face_interior(self, obj, face):
        """Insert a curve or a point in the interior of *face*.

        The feature must lie entirely inside the face and must not touch its boundary
        (CGAL precondition).

        :param obj: an x-monotone :class:`Curve` (-> a new :class:`Halfedge`) or a
            :class:`Point` / coordinate tuple (-> a new isolated :class:`Vertex`).
        :rtype: Halfedge | Vertex
        """
        cdef FH f = _fh_of(self, face)
        cdef Geom g
        cdef HH h
        cdef VH v
        if isinstance(obj, Point) or _looks_like_point(obj, self._kind):
            g = _as_point(obj, self._kind)
            v = _abase(self).insert_point_in_face_interior(g, f)
            _check_pending(self)
            return _wrap_vertex(self, v)
        g = _as_curve(obj, self._kind, True)
        h = _abase(self).insert_in_face_interior(g, f)
        _check_pending(self)
        return _wrap_halfedge(self, h)

    def insert_point_in_face_interior(self, point, face):
        """Insert an isolated vertex in the interior of *face*.

        :rtype: Vertex
        """
        cdef FH f = _fh_of(self, face)
        cdef Geom p = _as_point(point, self._kind)
        cdef VH v = _abase(self).insert_point_in_face_interior(p, f)
        _check_pending(self)
        return _wrap_vertex(self, v)

    def insert_from_left_vertex(self, curve, v):
        """Insert an x-monotone curve whose **left** endpoint is the existing vertex *v*.

        :rtype: Halfedge
        """
        cdef VH vh = _vh_of(self, v)
        cdef Geom c = _as_curve(curve, self._kind, True)
        cdef HH h = _abase(self).insert_from_left_vertex(c, vh)
        _check_pending(self)
        return _wrap_halfedge(self, h)

    def insert_from_right_vertex(self, curve, v):
        """Insert an x-monotone curve whose **right** endpoint is the existing vertex *v*.

        :rtype: Halfedge
        """
        cdef VH vh = _vh_of(self, v)
        cdef Geom c = _as_curve(curve, self._kind, True)
        cdef HH h = _abase(self).insert_from_right_vertex(c, vh)
        _check_pending(self)
        return _wrap_halfedge(self, h)

    def insert_at_vertices(self, curve, v1, v2):
        """Insert an x-monotone curve whose two endpoints are the existing vertices *v1*, *v2*.

        :rtype: Halfedge
        """
        cdef VH a = _vh_of(self, v1)
        cdef VH b = _vh_of(self, v2)
        cdef Geom c = _as_curve(curve, self._kind, True)
        cdef HH h = _abase(self).insert_at_vertices(c, a, b)
        _check_pending(self)
        return _wrap_halfedge(self, h)

    # ---- modification -----------------------------------------------------

    def modify_vertex(self, v, point):
        """Move vertex *v* to *point* (the point must be geometrically equivalent).

        :rtype: Vertex
        """
        cdef VH vh = _vh_of(self, v)
        cdef Geom p = _as_point(point, self._kind)
        cdef VH out = _abase(self).modify_vertex(vh, p)
        _check_pending(self)
        return _wrap_vertex(self, out)

    def modify_edge(self, he, curve):
        """Re-associate the edge of *he* with an equivalent x-monotone *curve*.

        :rtype: Halfedge
        """
        cdef HH h = _hh_of(self, he)
        cdef Geom c = _as_curve(curve, self._kind, True)
        cdef HH out = _abase(self).modify_edge(h, c)
        _check_pending(self)
        return _wrap_halfedge(self, out)

    def split_edge(self, he, *args):
        """Split the edge of *he* in two.

        Two forms:

        * ``split_edge(he, point)`` -- split at a point in the edge's interior.  This is
          the history-aware variant: the two new edges keep the originating curves.
        * ``split_edge(he, c1, c2)`` -- split into two explicitly given x-monotone curves
          whose concatenation is the edge's curve.  This is ``Arrangement_2::split_edge``
          and does **not** update the history.

        :returns: the halfedge directed like *he* whose target is the new vertex.
        :rtype: Halfedge
        """
        cdef HH h = _hh_of(self, he)
        cdef Geom p
        cdef Geom c1
        cdef Geom c2
        cdef HH out
        if len(args) == 1:
            p = _as_point(args[0], self._kind)
            out = _abase(self).split_edge_at_point(h, p)
        elif len(args) == 2:
            c1 = _as_curve(args[0], self._kind, True)
            c2 = _as_curve(args[1], self._kind, True)
            out = _abase(self).split_edge(h, c1, c2)
        else:
            raise TypeError(
                "split_edge() takes a split point or two x-monotone curves, got %d extra arguments"
                % (len(args),)
            )
        _check_pending(self)
        return _wrap_halfedge(self, out)

    def merge_edge(self, he1, he2, curve=None):
        """Merge the two edges of *he1* and *he2*, which share a degree-2 vertex.

        With *curve* omitted the history-aware variant is used and CGAL computes the
        merged curve itself; pass an explicit x-monotone *curve* to use
        ``Arrangement_2::merge_edge`` instead (which does not update the history).

        Note that with curve history two edges are mergeable only if their *sets of
        originating curves are equal* (arrangement_with_history.md gotcha 7): collinear
        edges coming from two different input curves report ``are_mergeable == False``.

        :rtype: Halfedge
        """
        cdef HH a = _hh_of(self, he1)
        cdef HH b = _hh_of(self, he2)
        cdef Geom c
        cdef HH out
        if curve is None:
            out = _abase(self).merge_edge_history(a, b)
        else:
            c = _as_curve(curve, self._kind, True)
            out = _abase(self).merge_edge(a, b, c)
        _check_pending(self)
        return _wrap_halfedge(self, out)

    def remove_edge(self, he, bint remove_source=True, bint remove_target=True):
        """Remove the edge of *he*.

        :param remove_source: also remove the source vertex if it becomes isolated
            or redundant (degree 2 with mergeable curves).
        :param remove_target: likewise for the target vertex.
        :returns: the face the removed edge was contained in.
        :rtype: Face
        """
        cdef HH h = _hh_of(self, he)
        cdef FH f = _abase(self).remove_edge(h, remove_source, remove_target)
        _check_pending(self)
        return _wrap_face(self, f)

    def remove_vertex(self, v):
        """Remove vertex *v* if it is isolated or has degree 2 with mergeable edges.

        :returns: ``True`` if the vertex was removed.
        :rtype: bool
        """
        cdef VH vh = _vh_of(self, v)
        cdef bint ok = _abase(self).remove_vertex(vh)
        _check_pending(self)
        return bool(ok)

    def remove_isolated_vertex(self, v):
        """Remove the isolated vertex *v*.

        :returns: the face that contained it.
        :rtype: Face
        """
        cdef VH vh = _vh_of(self, v)
        cdef FH f = _abase(self).remove_isolated_vertex(vh)
        _check_pending(self)
        return _wrap_face(self, f)

    def remove_curve(self, ch):
        """Remove an input curve and every edge induced *only* by it.

        Edges also induced by other input curves survive (they just lose one originating
        curve).  The curve node itself stays in the history with zero induced edges, so
        :attr:`number_of_curves` does not change -- that is CGAL's behaviour
        (arrangement_with_history.md gotcha 9).

        :returns: the number of removed edges.
        :rtype: int
        """
        cdef CH c = _ch_of(self, ch)
        cdef size_t n = _abase(self).remove_curve(c)
        _check_pending(self)
        return n

    # ---- queries ----------------------------------------------------------

    def locate(self, point, strategy=None):
        """Locate *point* in the arrangement.

        :param strategy: ``None`` (an attached strategy, else a walk query) or one of
            :func:`point_location_strategies`.
        :returns: the :class:`Vertex`, :class:`Halfedge` or :class:`Face` containing the point.
        :rtype: Vertex | Halfedge | Face
        """
        cdef Geom p = _as_point(point, self._kind)
        cdef Located l = _abase(self).locate(p, _pl_strategy(strategy))
        return _wrap_located(self, l)

    def ray_shoot_up(self, point, strategy=None):
        """Shoot a vertical ray upwards from *point* and return the first feature hit.

        Only the ``simple``, ``walk`` and ``trapezoid`` strategies support ray shooting
        (point_location_and_decomposition.md gotcha 2); ``None`` picks a supporting one.
        A ray that escapes returns the unbounded face containing it (or a fictitious
        halfedge in the ``linear`` kind), never ``None``.

        :rtype: Vertex | Halfedge | Face | None
        """
        cdef Geom p = _as_point(point, self._kind)
        cdef Located l = _abase(self).ray_shoot_up(p, _pl_strategy(strategy))
        return _wrap_located(self, l)

    def ray_shoot_down(self, point, strategy=None):
        """Shoot a vertical ray downwards from *point* and return the first feature hit.

        :rtype: Vertex | Halfedge | Face | None
        """
        cdef Geom p = _as_point(point, self._kind)
        cdef Located l = _abase(self).ray_shoot_down(p, _pl_strategy(strategy))
        return _wrap_located(self, l)

    def batched_locate(self, points):
        """Locate many points at once with one sweep (``CGAL::locate``).

        Much faster than repeated :meth:`locate` for large point sets.

        :param points: an iterable of :class:`Point` objects or coordinate tuples.
        :returns: one result per input point, **in the input order**
            (CGAL returns them xy-sorted; the core restores the original order).
        :rtype: list[Vertex | Halfedge | Face | None]
        """
        cdef vector[Geom] gs
        cdef vector[Located] out
        cdef list items = list(points)
        cdef Py_ssize_t i
        gs.reserve(len(items))
        for i in range(len(items)):
            gs.push_back(_as_point(items[i], self._kind))
        _abase(self).batched_locate(gs, out)
        cdef list res = []
        cdef size_t j
        for j in range(out.size()):
            res.append(_wrap_located(self, out[j]))
        return res

    def supports_point_location(self, strategy):
        """Whether *strategy* can be used on this arrangement.

        ``triangulation`` needs a bounded arrangement of segments;
        ``landmarks``/``naive``/``triangulation`` cannot do ray shooting.

        :rtype: bool
        """
        return bool(_abase(self).supports_point_location(_pl_strategy(strategy)))

    def attach_point_location(self, strategy):
        """Build and keep a point-location structure for later queries.

        Attached strategies observe the arrangement and stay up to date (``trapezoid`` and
        ``landmarks`` do; note that the landmarks generator rebuilds its whole kd-tree on
        every local change -- point_location_and_decomposition.md gotcha 10 -- so prefer
        aggregate insertion or the trapezoid strategy for incremental work).
        """
        _abase(self).attach_point_location(_pl_strategy(strategy))
        _check_pending(self)

    def detach_point_location(self, strategy):
        """Drop a previously attached point-location structure."""
        _abase(self).detach_point_location(_pl_strategy(strategy))
        _check_pending(self)

    def has_point_location(self, strategy):
        """Whether a point-location structure for *strategy* is currently attached.

        :rtype: bool
        """
        return bool(_abase(self).has_point_location(_pl_strategy(strategy)))

    def zone(self, curve):
        """The features the *curve* would pass through, in order along the curve.

        Nothing is inserted.  A general (non x-monotone) curve is subdivided first.

        :rtype: list[Vertex | Halfedge | Face | None]
        """
        cdef Geom c = _as_curve(curve, self._kind, False)
        cdef vector[Located] out
        _abase(self).zone(c, out)
        _check_pending(self)
        cdef list res = []
        cdef size_t i
        for i in range(out.size()):
            res.append(_wrap_located(self, out[i]))
        return res

    def do_intersect(self, curve):
        """Whether *curve* intersects any vertex or edge of the arrangement.

        :rtype: bool
        """
        cdef Geom c = _as_curve(curve, self._kind, False)
        cdef bint r = _abase(self).do_intersect(c)
        _check_pending(self)
        return bool(r)

    def decompose(self):
        """Vertical decomposition (``CGAL::decompose``).

        :returns: one ``(vertex, below, above)`` triple per vertex, in xy-lexicographic
            order.  *below* / *above* are the feature the vertical ray hits, or ``None``.
            In an unbounded arrangement "nothing above/below" is reported as a
            **fictitious halfedge**, not as ``None``
            (point_location_and_decomposition.md gotcha 7) -- test ``.is_fictitious``.
        :rtype: list[tuple[Vertex, object, object]]
        """
        cdef vector[VerticalDecompositionEntry] out
        _abase(self).decompose(out)
        cdef list res = []
        cdef size_t i
        for i in range(out.size()):
            res.append((
                _wrap_vertex(self, out[i].v),
                _wrap_located(self, out[i].below),
                _wrap_located(self, out[i].above),
            ))
        return res

    # ---- overlay ----------------------------------------------------------

    def overlay(self, Arrangement other, callbacks=None, *,
                on_vertex=None, on_edge=None, on_face=None):
        """Overlay this arrangement with *other* and return the result.

        The result is a fresh arrangement of the same kind containing every vertex, edge
        and face induced by both inputs; its curve history holds copies of *all* input
        curves of both arrangements (arrangement_with_history.md gotcha 14 -- input curve
        handles do not identify output curves).

        :param callbacks: an :class:`OverlayCallbacks` instance (or a ``dict`` mapping
            callback names to callables) invoked for every created result feature.
        :param on_vertex: shorthand ``f(a_feature, b_feature) -> value`` whose result is
            stored in ``.data`` of every created result vertex.
        :param on_edge: likewise for result edges.
        :param on_face: likewise for result faces.
        :rtype: Arrangement

        The inputs are not modified, but CGAL does squat their internal vertex pointers
        during the sweep (global_functions_overlay_observer.md gotcha 7), so an overlay is
        not thread-safe with respect to its own inputs.
        """
        if other is None:
            raise TypeError("overlay() needs another Arrangement")
        if other._kind != self._kind:
            raise KindMismatchError(
                "cannot overlay a '%s' arrangement with a '%s' one"
                % (_kind_name(self._kind), _kind_name(other._kind))
            )
        if other is self:
            raise ValueError("cannot overlay an arrangement with itself")
        for name, hook in (("on_vertex", on_vertex), ("on_edge", on_edge), ("on_face", on_face)):
            if hook is not None and not callable(hook):
                raise TypeError("%s must be callable, got %r" % (name, type(hook).__name__))

        cdef Arrangement result = Arrangement(_kind_enum(self._kind))
        cdef _OverlayCtx ctx = _OverlayCtx.__new__(_OverlayCtx)
        ctx.a = self
        ctx.b = other
        ctx.r = result
        ctx.table = _overlay_table(callbacks)
        ctx.on_vertex = on_vertex
        ctx.on_edge = on_edge
        ctx.on_face = on_face

        _abase(self).overlay_with(_abase(other)[0], _abase(result)[0],
                                  <void*>ctx, <OverlayFn>_overlay_dispatch)
        _check_pending(result)
        _check_pending(self)
        return result

    # ---- observers --------------------------------------------------------

    def add_observer(self, observer):
        """Attach an :class:`Observer` (or any object with matching methods).

        Only methods that actually override :class:`Observer`'s no-ops are ever called.
        The arrangement keeps a strong reference to the observer until it is removed.

        :returns: the observer, so ``obs = arr.add_observer(MyObserver())`` reads well.
        """
        if observer is None:
            raise TypeError("expected an observer object")
        for ctx_obj in self._observers.values():
            if (<_ObsCtx>ctx_obj).observer is observer:
                raise ValueError("this observer is already attached to this arrangement")
        cdef _ObsCtx ctx = _ObsCtx.__new__(_ObsCtx)
        ctx.arr = self
        ctx.observer = observer
        ctx.table = _observer_table(observer)
        cdef int token = _abase(self).add_observer(<void*>ctx, <ObserverFn>_observer_dispatch)
        ctx.token = token
        self._observers[token] = ctx
        _check_pending(self)
        return observer

    def remove_observer(self, observer):
        """Detach a previously added observer.

        :raises ValueError: the observer is not attached.
        """
        cdef int token
        for key, ctx_obj in list(self._observers.items()):
            if (<_ObsCtx>ctx_obj).observer is observer:
                token = <int>key
                _abase(self).remove_observer(token)
                del self._observers[key]
                _check_pending(self)
                return
        raise ValueError("this observer is not attached to this arrangement")

    def observers(self):
        """The observers currently attached, in attachment order.

        :rtype: list
        """
        return [(<_ObsCtx>c).observer for c in self._observers.values()]

    # ---- bulk export ------------------------------------------------------

    def vertex_coordinates(self):
        """Approximate coordinates of every concrete vertex, in :meth:`vertices` order.

        :returns: a numpy array of shape ``(n, 2)`` (``(n, 3)`` for the sphere kind) if
            numpy is importable, otherwise a list of tuples.
        """
        cdef vector[double] out
        cdef const KindOps* o = &ops(self._kind)
        _abase(self).vertex_coordinates(out)
        cdef int dim = o.dimension()
        cdef size_t n = out.size()
        cdef size_t i
        cdef list flat = []
        for i in range(n):
            flat.append(out[i])
        if _np is not None:
            return _np.asarray(flat, dtype=_np.float64).reshape((-1, dim))
        cdef list res = []
        i = 0
        while i + <size_t>dim <= n:
            if dim == 3:
                res.append((flat[i], flat[i + 1], flat[i + 2]))
            else:
                res.append((flat[i], flat[i + 1]))
            i += <size_t>dim
        return res

    def edge_vertex_indices(self):
        """Source/target vertex indices of every edge, in :meth:`edges` order.

        Indices refer to :meth:`vertices` / :meth:`vertex_coordinates`.

        :returns: a numpy array of shape ``(m, 2)`` of integers, or a list of pairs.
        """
        cdef vector[size_t] out
        _abase(self).edge_vertex_indices(out)
        cdef size_t n = out.size()
        cdef size_t i
        cdef list flat = []
        for i in range(n):
            flat.append(out[i])
        if _np is not None:
            return _np.asarray(flat, dtype=_np.int64).reshape((-1, 2))
        cdef list res = []
        i = 0
        while i + 2 <= n:
            res.append((flat[i], flat[i + 1]))
            i += 2
        return res

    def face_boundaries(self):
        """Vertex indices along the boundary cycles of every face, in :meth:`faces` order.

        Each face yields a list of cycles (outer CCBs first, then inner CCBs / holes);
        each cycle is a sequence of indices into :meth:`vertices`.  Fictitious vertices
        and halfedges are skipped, so a cycle of an unbounded face may be "open".

        :returns: ``list[list[array|list[int]]]``
        """
        cdef vector[vector[vector[size_t]]] out
        _abase(self).face_boundaries(out)
        cdef list res = []
        cdef size_t i, j, k
        cdef list face_cycles
        cdef list cycle
        for i in range(out.size()):
            face_cycles = []
            for j in range(out[i].size()):
                cycle = []
                for k in range(out[i][j].size()):
                    cycle.append(out[i][j][k])
                if _np is not None:
                    face_cycles.append(_np.asarray(cycle, dtype=_np.int64))
                else:
                    face_cycles.append(cycle)
            res.append(face_cycles)
        return res

    def approximate_edges(self, double tolerance=1e-3, bbox=None):
        """Polyline approximation of every edge, in :meth:`edges` order.

        :param tolerance: maximum deviation in coordinate units.
        :param bbox: ``(xmin, ymin, xmax, ymax)`` used to clip unbounded edges; for the
            ``linear`` kind a clipping box is required and, when omitted, the
            arrangement's own :meth:`bbox` padded by 10 % (at least 1 unit) is used.
        :returns: a list of ``(k, 2)`` numpy arrays (``(k, 3)`` for the sphere kind), or
            lists of coordinate tuples when numpy is unavailable.
        """
        cdef ArrBase* a = _abase(self)
        cdef const KindOps* o = &ops(self._kind)
        cdef int dim = o.dimension()
        cdef vector[HH] hs
        a.edges(hs)
        if bbox is None and o.is_unbounded_kind():
            bbox = self._padded_bbox()
        cdef list res = []
        cdef size_t i
        cdef Geom g
        for i in range(hs.size()):
            if a.he_is_fictitious(hs[i]):
                continue
            g = a.he_directed_curve(hs[i])
            pts = _approximate_flat(o, g, tolerance, bbox, dim)
            if _np is not None:
                res.append(_np.asarray(pts, dtype=_np.float64).reshape((-1, dim)))
            else:
                res.append(pts)
        return res

    def _padded_bbox(self):
        """The arrangement bbox padded by 10 % (at least 1 unit); used to clip unbounded curves."""
        cdef BBox b = _abase(self).bbox()
        cdef double xmin = b.lo[0], ymin = b.lo[1], xmax = b.hi[0], ymax = b.hi[1]
        cdef double pad_x = max(1.0, 0.1 * (xmax - xmin))
        cdef double pad_y = max(1.0, 0.1 * (ymax - ymin))
        return (xmin - pad_x, ymin - pad_y, xmax + pad_x, ymax + pad_y)

    def bbox(self):
        """Bounding box of the vertex approximations.

        :returns: ``(xmin, ymin, xmax, ymax)``, or ``(xmin, ymin, zmin, xmax, ymax, zmax)``
            for the sphere kind.  All zeros for an empty arrangement.
        :rtype: tuple[float, ...]
        """
        cdef BBox b = _abase(self).bbox()
        if b.dim == 3:
            return (b.lo[0], b.lo[1], b.lo[2], b.hi[0], b.hi[1], b.hi[2])
        return (b.lo[0], b.lo[1], b.hi[0], b.hi[1])

    # ---- misc -------------------------------------------------------------

    def __repr__(self):
        cdef ArrBase* a
        try:
            a = _abase(self)
        except Exception:
            return "Arrangement(<uninitialised>)"
        return ("Arrangement(kind='%s', vertices=%d, edges=%d, faces=%d, curves=%d)"
                % (_kind_name(self._kind), a.number_of_vertices(), a.number_of_edges(),
                   a.number_of_faces(), a.number_of_curves()))

    def __eq__(self, other):
        """Arrangements compare by identity (there is no cheap geometric equality)."""
        return self is other

    def __ne__(self, other):
        return self is not other

    def __hash__(self):
        return <Py_ssize_t><void*>self


cdef bint _looks_like_point(object obj, Kind kind) except *:
    """Heuristic: is *obj* a bare coordinate tuple/list/array for this kind?

    Used only by :meth:`Arrangement.insert` and :meth:`insert_in_face_interior` to tell a
    point from an iterable of curves.  A sequence of 2 (or 3 for the sphere kind) plain
    numbers is a point; anything containing a Curve/Point/Polygon is not.
    """
    cdef Py_ssize_t n
    cdef int want = 3 if kind == Kind.Sphere else 2
    if isinstance(obj, Point) or isinstance(obj, Curve):
        return isinstance(obj, Point)
    if not isinstance(obj, (tuple, list)):
        # numpy arrays of shape (2,) / (3,) count as points too
        if _np is not None and isinstance(obj, _np.ndarray):
            return obj.ndim == 1 and obj.shape[0] == want
        return False
    n = len(obj)
    if n != want:
        return False
    for item in obj:
        if isinstance(item, (Point, Curve)):
            return False
        if not isinstance(item, (int, float, numbers.Number, str, SqrtExtension, Algebraic)):
            return False
    return True


# ---------------------------------------------------------------------------
# Vertex
# ---------------------------------------------------------------------------

cdef class Vertex:
    """A vertex of an :class:`Arrangement`.

    Vertices are lightweight handles: they compare and hash by the identity of the
    underlying DCEL record.  Accessing a vertex that has been removed raises
    :class:`InvalidHandleError`.
    """

    def __init__(self, *args, **kwargs):
        raise TypeError("Vertex objects are created by an Arrangement, not directly")

    @property
    def point(self):
        """The vertex point.

        :rtype: Point
        :raises UnsupportedError: for a vertex at infinity (test
            :attr:`is_at_open_boundary` first -- such vertices have no point at all,
            arrangement_core.md gotcha 5).
        """
        return _wrap_point(_abase(self.arr).vertex_point(self.h))

    @property
    def degree(self):
        """Number of edges incident to this vertex (``0`` for an isolated vertex).

        :rtype: int
        """
        return _abase(self.arr).vertex_degree(self.h)

    @property
    def is_isolated(self):
        """``True`` when no edge is incident to this vertex.

        :rtype: bool
        """
        return bool(_abase(self.arr).vertex_is_isolated(self.h))

    @property
    def face(self):
        """The face containing this vertex; **isolated vertices only**.

        :rtype: Face
        :raises ValueError: the vertex is not isolated (use :meth:`incident_faces`).
        """
        return _wrap_face(self.arr, _abase(self.arr).vertex_face(self.h))

    def incident_halfedges(self):
        """The halfedges whose **target** is this vertex, in CGAL's circular order.

        :rtype: list[Halfedge]
        """
        cdef vector[HH] out
        _abase(self.arr).vertex_incident_halfedges(self.h, out)
        cdef list res = []
        cdef size_t i
        for i in range(out.size()):
            res.append(_wrap_halfedge(self.arr, out[i]))
        return res

    def incident_faces(self):
        """The distinct faces around this vertex.

        For an isolated vertex this is ``[self.face]``; otherwise it is the set of faces
        incident to the halfedges around the vertex, in circular order without repeats.

        :rtype: list[Face]
        """
        cdef ArrBase* a = _abase(self.arr)
        if a.vertex_is_isolated(self.h):
            return [_wrap_face(self.arr, a.vertex_face(self.h))]
        cdef vector[HH] out
        a.vertex_incident_halfedges(self.h, out)
        cdef list res = []
        cdef set seen = set()
        cdef size_t i
        cdef FH f
        cdef object key
        for i in range(out.size()):
            f = a.he_face(out[i])
            key = (<Py_ssize_t>f.p, f.id)
            if key in seen:
                continue
            seen.add(key)
            res.append(_wrap_face(self.arr, f))
        return res

    @property
    def is_at_open_boundary(self):
        """``True`` for a vertex at infinity (unbounded kinds) -- it has no point.

        :rtype: bool
        """
        return bool(_abase(self.arr).vertex_is_at_open_boundary(self.h))

    @property
    def parameter_space_in_x(self):
        """Where the vertex lies in the x parameter space.

        :returns: ``"interior"``, ``"left"`` or ``"right"``.
        :rtype: str
        """
        return _param_space_name(_abase(self.arr).vertex_parameter_space_in_x(self.h))

    @property
    def parameter_space_in_y(self):
        """Where the vertex lies in the y parameter space.

        :returns: ``"interior"``, ``"bottom"`` or ``"top"``.
        :rtype: str
        """
        return _param_space_name(_abase(self.arr).vertex_parameter_space_in_y(self.h))

    @property
    def data(self):
        """An arbitrary Python object stored on this vertex (``None`` by default).

        The reference is owned by the arrangement and survives :meth:`Arrangement.copy`
        (the copy shares the same object).
        """
        cdef PyRef* r = &_abase(self.arr).vertex_data(self.h)
        cdef void* p = r.get()
        if p == NULL:
            return None
        return <object>p

    @data.setter
    def data(self, value):
        cdef PyRef* r = &_abase(self.arr).vertex_data(self.h)
        if value is None:
            r.set(NULL)
        else:
            r.set(<void*>value)

    @property
    def id(self):
        """A unique 64-bit id of this DCEL vertex within its arrangement.

        :rtype: int
        """
        return self.h.id

    @property
    def arrangement(self):
        """The :class:`Arrangement` this vertex belongs to."""
        return self.arr

    @property
    def is_valid(self):
        """``False`` once the vertex has been removed from the arrangement.

        :rtype: bool
        """
        return bool(_abase(self.arr).vertex_valid(self.h))

    def __repr__(self):
        cdef ArrBase* a = _abase(self.arr)
        if not a.vertex_valid(self.h):
            return "Vertex(<invalid>, id=%d)" % (self.h.id,)
        if a.vertex_is_at_open_boundary(self.h):
            return "Vertex(id=%d, at_infinity, x=%s, y=%s)" % (
                self.h.id,
                _param_space_name(a.vertex_parameter_space_in_x(self.h)),
                _param_space_name(a.vertex_parameter_space_in_y(self.h)),
            )
        return "Vertex(id=%d, point=%r, degree=%d)" % (
            self.h.id, self.point, a.vertex_degree(self.h))

    def __eq__(self, other):
        if not isinstance(other, Vertex):
            return NotImplemented
        return (self.arr is (<Vertex>other).arr
                and self.h.p == (<Vertex>other).h.p
                and self.h.id == (<Vertex>other).h.id)

    def __ne__(self, other):
        result = self.__eq__(other)
        if result is NotImplemented:
            return result
        return not result

    def __hash__(self):
        return hash((id(self.arr), <Py_ssize_t>self.h.p, self.h.id))


# ---------------------------------------------------------------------------
# Halfedge
# ---------------------------------------------------------------------------

cdef class Halfedge:
    """One of the two directed halfedges of an arrangement edge.

    A halfedge is directed from :attr:`source` to :attr:`target` and has the incident
    :attr:`face` on its left.  ``he.twin`` is the opposite halfedge of the same edge.
    """

    def __init__(self, *args, **kwargs):
        raise TypeError("Halfedge objects are created by an Arrangement, not directly")

    @property
    def source(self):
        """The vertex this halfedge starts at.

        :rtype: Vertex
        """
        return _wrap_vertex(self.arr, _abase(self.arr).he_source(self.h))

    @property
    def target(self):
        """The vertex this halfedge ends at.

        :rtype: Vertex
        """
        return _wrap_vertex(self.arr, _abase(self.arr).he_target(self.h))

    @property
    def twin(self):
        """The opposite halfedge of the same edge.

        :rtype: Halfedge
        """
        return _wrap_halfedge(self.arr, _abase(self.arr).he_twin(self.h))

    @property
    def next(self):
        """The next halfedge along the same CCB.

        :rtype: Halfedge
        """
        return _wrap_halfedge(self.arr, _abase(self.arr).he_next(self.h))

    @property
    def prev(self):
        """The previous halfedge along the same CCB.

        :rtype: Halfedge
        """
        return _wrap_halfedge(self.arr, _abase(self.arr).he_prev(self.h))

    @property
    def face(self):
        """The face incident to this halfedge (the one on its left).

        :rtype: Face
        """
        return _wrap_face(self.arr, _abase(self.arr).he_face(self.h))

    @property
    def curve(self):
        """The x-monotone curve of the edge, as stored (shared with the twin).

        :rtype: Curve
        :raises UnsupportedError: for a fictitious halfedge (it has no curve at all --
            arrangement_core.md gotcha 4).
        """
        return _wrap_curve(_abase(self.arr).he_curve(self.h))

    @property
    def directed_curve(self):
        """The edge's curve oriented from :attr:`source` to :attr:`target`.

        :rtype: Curve
        """
        return _wrap_curve(_abase(self.arr).he_directed_curve(self.h))

    @property
    def direction(self):
        """``"left_to_right"`` or ``"right_to_left"``: how the stored curve is traversed.

        :rtype: str
        """
        cdef int d = _abase(self.arr).he_direction(self.h)
        if 0 <= d < 2:
            return _HALFEDGE_DIRECTION_NAMES[d]
        return "left_to_right"

    @property
    def is_fictitious(self):
        """``True`` for the artificial halfedges of the unbounded (``linear``) topology.

        Fictitious halfedges appear in CCB walks but never in :meth:`Arrangement.edges`
        or :meth:`Arrangement.halfedges`; they carry no curve.

        :rtype: bool
        """
        return bool(_abase(self.arr).he_is_fictitious(self.h))

    @property
    def is_on_inner_ccb(self):
        """``True`` when this halfedge lies on an inner CCB (a hole) of its face.

        :rtype: bool
        """
        return bool(_abase(self.arr).he_is_on_inner_ccb(self.h))

    @property
    def is_on_outer_ccb(self):
        """``True`` when this halfedge lies on an outer CCB of its face.

        :rtype: bool
        """
        return bool(_abase(self.arr).he_is_on_outer_ccb(self.h))

    def ccb(self):
        """The whole connected component of the boundary containing this halfedge.

        Starts at ``self`` and follows ``next()`` all the way round.  In the ``linear``
        kind the cycle may contain fictitious halfedges.

        :rtype: list[Halfedge]
        """
        cdef vector[HH] out
        _abase(self.arr).he_ccb(self.h, out)
        cdef list res = []
        cdef size_t i
        for i in range(out.size()):
            res.append(_wrap_halfedge(self.arr, out[i]))
        return res

    def originating_curves(self):
        """The input curves that induced this edge (curve history).

        An edge covered by several overlapping input curves has several originators.

        :rtype: list[CurveHandle]
        """
        cdef vector[CH] out
        _abase(self.arr).originating_curves(self.h, out)
        cdef list res = []
        cdef size_t i
        for i in range(out.size()):
            res.append(_wrap_curve_handle(self.arr, out[i]))
        return res

    @property
    def number_of_originating_curves(self):
        """How many input curves induced this edge.

        :rtype: int
        """
        return _abase(self.arr).number_of_originating_curves(self.h)

    @property
    def data(self):
        """An arbitrary Python object stored on this halfedge (``None`` by default).

        The two halfedges of an edge have *independent* data.
        """
        cdef PyRef* r = &_abase(self.arr).he_data(self.h)
        cdef void* p = r.get()
        if p == NULL:
            return None
        return <object>p

    @data.setter
    def data(self, value):
        cdef PyRef* r = &_abase(self.arr).he_data(self.h)
        if value is None:
            r.set(NULL)
        else:
            r.set(<void*>value)

    @property
    def id(self):
        """A unique 64-bit id of this halfedge within its arrangement.

        :rtype: int
        """
        return self.h.id

    @property
    def edge_id(self):
        """``min(self.id, self.twin.id)`` -- an id shared by both halfedges of the edge.

        :rtype: int
        """
        cdef HH t = _abase(self.arr).he_twin(self.h)
        return self.h.id if self.h.id < t.id else t.id

    @property
    def arrangement(self):
        """The :class:`Arrangement` this halfedge belongs to."""
        return self.arr

    @property
    def is_valid(self):
        """``False`` once the edge has been removed from the arrangement.

        :rtype: bool
        """
        return bool(_abase(self.arr).halfedge_valid(self.h))

    def __repr__(self):
        cdef ArrBase* a = _abase(self.arr)
        if not a.halfedge_valid(self.h):
            return "Halfedge(<invalid>, id=%d)" % (self.h.id,)
        if a.he_is_fictitious(self.h):
            return "Halfedge(id=%d, fictitious)" % (self.h.id,)
        return "Halfedge(id=%d, curve=%r, direction='%s')" % (
            self.h.id, self.curve, self.direction)

    def __eq__(self, other):
        if not isinstance(other, Halfedge):
            return NotImplemented
        return (self.arr is (<Halfedge>other).arr
                and self.h.p == (<Halfedge>other).h.p
                and self.h.id == (<Halfedge>other).h.id)

    def __ne__(self, other):
        result = self.__eq__(other)
        if result is NotImplemented:
            return result
        return not result

    def __hash__(self):
        return hash((id(self.arr), <Py_ssize_t>self.h.p, self.h.id))


# ---------------------------------------------------------------------------
# Face
# ---------------------------------------------------------------------------

cdef list _chain_approx(list curves, double tolerance):
    """Approximate a chain of directed curves into one point list (no duplicated joints)."""
    cdef list pts = []
    for c in curves:
        part = c.approximate(tolerance)
        if not part:
            continue
        if pts and pts[-1] == part[0]:
            part = part[1:]
        pts.extend(part)
    return pts


cdef list _face_ccbs(Face face, bint outer):
    """The outer (or inner) CCBs of *face*, each as a list of halfedges."""
    cdef ArrBase* a = _abase(face.arr)
    cdef vector[HH] reps
    if outer:
        a.face_outer_ccbs(face.h, reps)
    else:
        a.face_inner_ccbs(face.h, reps)
    cdef list res = []
    cdef vector[HH] cycle
    cdef size_t i, j
    cdef list one
    for i in range(reps.size()):
        cycle.clear()
        a.he_ccb(reps[i], cycle)
        one = []
        for j in range(cycle.size()):
            one.append(_wrap_halfedge(face.arr, cycle[j]))
        res.append(one)
    return res


cdef class Face:
    """A face of an :class:`Arrangement`.

    A face is bounded by zero or one outer CCB (the sphere kind may have more) and any
    number of inner CCBs (holes); it may also contain isolated vertices.
    """

    def __init__(self, *args, **kwargs):
        raise TypeError("Face objects are created by an Arrangement, not directly")

    @property
    def is_unbounded(self):
        """``True`` for a face of infinite area (always ``False`` on the sphere).

        :rtype: bool
        """
        return bool(_abase(self.arr).face_is_unbounded(self.h))

    @property
    def is_fictitious(self):
        """``True`` only for the fictitious face of the unbounded (``linear``) topology.

        :rtype: bool
        """
        return bool(_abase(self.arr).face_is_fictitious(self.h))

    @property
    def has_outer_ccb(self):
        """``True`` when the face has an outer boundary.

        :rtype: bool
        """
        return bool(_abase(self.arr).face_has_outer_ccb(self.h))

    @property
    def number_of_outer_ccbs(self):
        """Number of outer CCBs (0 or 1 in the plane; the sphere may have more).

        :rtype: int
        """
        return _abase(self.arr).face_number_of_outer_ccbs(self.h)

    @property
    def number_of_inner_ccbs(self):
        """Number of inner CCBs (holes).

        :rtype: int
        """
        return _abase(self.arr).face_number_of_inner_ccbs(self.h)

    @property
    def number_of_isolated_vertices(self):
        """Number of isolated vertices inside this face.

        :rtype: int
        """
        return _abase(self.arr).face_number_of_isolated_vertices(self.h)

    def outer_ccb(self):
        """The halfedges of the (first) outer CCB, in traversal order.

        :rtype: list[Halfedge]
        :raises ValueError: the face has no outer CCB (unbounded / spherical face).
        """
        cdef HH h = _abase(self.arr).face_outer_ccb(self.h)
        cdef vector[HH] out
        _abase(self.arr).he_ccb(h, out)
        cdef list res = []
        cdef size_t i
        for i in range(out.size()):
            res.append(_wrap_halfedge(self.arr, out[i]))
        return res

    def outer_ccbs(self):
        """All outer CCBs, each as a list of halfedges.

        :rtype: list[list[Halfedge]]
        """
        return _face_ccbs(self, True)

    def inner_ccbs(self):
        """All inner CCBs (holes), each as a list of halfedges.

        :rtype: list[list[Halfedge]]
        """
        return _face_ccbs(self, False)

    def holes(self):
        """Alias of :meth:`inner_ccbs` (CGAL's legacy "hole" spelling).

        :rtype: list[list[Halfedge]]
        """
        return _face_ccbs(self, False)

    def isolated_vertices(self):
        """The isolated vertices contained in this face.

        :rtype: list[Vertex]
        """
        cdef vector[VH] out
        _abase(self.arr).face_isolated_vertices(self.h, out)
        cdef list res = []
        cdef size_t i
        for i in range(out.size()):
            res.append(_wrap_vertex(self.arr, out[i]))
        return res

    def edges(self):
        """Every halfedge on the boundary of this face (all outer and inner CCBs).

        :rtype: list[Halfedge]
        """
        cdef list res = []
        for cycle in self.outer_ccbs():
            res.extend(cycle)
        for cycle in self.inner_ccbs():
            res.extend(cycle)
        return res

    def adjacent_faces(self):
        """The distinct faces sharing an edge with this one.

        Fictitious halfedges are skipped; the face itself is not included.

        :rtype: list[Face]
        """
        cdef ArrBase* a = _abase(self.arr)
        cdef list res = []
        cdef set seen = set()
        cdef FH f
        cdef object key
        cdef object self_key = (<Py_ssize_t>self.h.p, self.h.id)
        for he in self.edges():
            if (<Halfedge>he).arr is not self.arr:
                continue
            if a.he_is_fictitious((<Halfedge>he).h):
                continue
            f = a.he_face(a.he_twin((<Halfedge>he).h))
            key = (<Py_ssize_t>f.p, f.id)
            if key == self_key or key in seen:
                continue
            seen.add(key)
            res.append(_wrap_face(self.arr, f))
        return res

    def polygon(self):
        """The exact boundary of this face as a :class:`PolygonWithHoles`.

        The outer boundary runs counterclockwise and the holes clockwise (the face is on
        the left of every curve), which is exactly what the Boolean set operations
        expect.

        :rtype: PolygonWithHoles
        :raises UnsupportedError: the face has no outer CCB (an unbounded face, the
            fictitious face, or the spherical face); such a region is not a bounded
            polygon.
        """
        cdef vector[Geom] outer
        cdef vector[vector[Geom]] holes
        _abase(self.arr).face_polygon(self.h, outer, holes)
        if outer.size() == 0:
            raise UnsupportedError(
                "face %d has no outer CCB (unbounded, fictitious or spherical face); "
                "it cannot be expressed as a bounded polygon" % (self.h.id,)
            )
        cdef object kind = _kind_enum(self.arr._kind)
        cdef list curves = []
        cdef size_t i, j
        for i in range(outer.size()):
            curves.append(_wrap_curve(outer[i]))
        cdef list hole_polys = []
        cdef list hcurves
        for i in range(holes.size()):
            hcurves = []
            for j in range(holes[i].size()):
                hcurves.append(_wrap_curve(holes[i][j]))
            hole_polys.append(Polygon(hcurves, kind=kind))
        return PolygonWithHoles(Polygon(curves, kind=kind), hole_polys)

    def boundary_points(self, double tolerance=1e-3):
        """Approximate the face boundary as polylines.

        :param tolerance: maximum deviation in coordinate units.
        :returns: ``(outer_points, [hole_points, ...])`` where each element is a list of
            coordinate tuples following the boundary (the face on the left).  The outer
            list is empty when the face has no outer CCB.
        :rtype: tuple[list, list[list]]
        """
        cdef vector[Geom] outer
        cdef vector[vector[Geom]] holes
        _abase(self.arr).face_polygon(self.h, outer, holes)
        cdef size_t i, j
        cdef list outer_pts = []
        cdef list curves = []
        for i in range(outer.size()):
            curves.append(_wrap_curve(outer[i]))
        outer_pts = _chain_approx(curves, tolerance)
        cdef list hole_pts = []
        cdef list hcurves
        for i in range(holes.size()):
            hcurves = []
            for j in range(holes[i].size()):
                hcurves.append(_wrap_curve(holes[i][j]))
            hole_pts.append(_chain_approx(hcurves, tolerance))
        return (outer_pts, hole_pts)

    @property
    def data(self):
        """An arbitrary Python object stored on this face (``None`` by default)."""
        cdef PyRef* r = &_abase(self.arr).face_data(self.h)
        cdef void* p = r.get()
        if p == NULL:
            return None
        return <object>p

    @data.setter
    def data(self, value):
        cdef PyRef* r = &_abase(self.arr).face_data(self.h)
        if value is None:
            r.set(NULL)
        else:
            r.set(<void*>value)

    @property
    def id(self):
        """A unique 64-bit id of this face within its arrangement.

        :rtype: int
        """
        return self.h.id

    @property
    def arrangement(self):
        """The :class:`Arrangement` this face belongs to."""
        return self.arr

    @property
    def is_valid(self):
        """``False`` once the face has been merged away or the arrangement cleared.

        :rtype: bool
        """
        return bool(_abase(self.arr).face_valid(self.h))

    def __repr__(self):
        cdef ArrBase* a = _abase(self.arr)
        if not a.face_valid(self.h):
            return "Face(<invalid>, id=%d)" % (self.h.id,)
        return ("Face(id=%d, unbounded=%s, outer_ccbs=%d, holes=%d, isolated=%d)"
                % (self.h.id, bool(a.face_is_unbounded(self.h)),
                   a.face_number_of_outer_ccbs(self.h),
                   a.face_number_of_inner_ccbs(self.h),
                   a.face_number_of_isolated_vertices(self.h)))

    def __eq__(self, other):
        if not isinstance(other, Face):
            return NotImplemented
        return (self.arr is (<Face>other).arr
                and self.h.p == (<Face>other).h.p
                and self.h.id == (<Face>other).h.id)

    def __ne__(self, other):
        result = self.__eq__(other)
        if result is NotImplemented:
            return result
        return not result

    def __hash__(self):
        return hash((id(self.arr), <Py_ssize_t>self.h.p, self.h.id))


# ---------------------------------------------------------------------------
# CurveHandle
# ---------------------------------------------------------------------------

cdef class CurveHandle:
    """A handle on one input curve stored in an arrangement's history.

    Obtained from :meth:`Arrangement.insert` / :meth:`Arrangement.insert_curves` or
    :meth:`Arrangement.curves`.  It knows the edges its curve induced and can be removed
    again with :meth:`Arrangement.remove_curve`.
    """

    def __init__(self, *args, **kwargs):
        raise TypeError("CurveHandle objects are created by an Arrangement, not directly")

    @property
    def curve(self):
        """The input curve as it was inserted (a general, not necessarily x-monotone, curve).

        :rtype: Curve
        """
        return _wrap_curve(_abase(self.arr).curve_geometry(self.h))

    def induced_edges(self):
        """The edges this curve induced, as one halfedge each.

        CGAL iterates them in memory-address order, not along the curve
        (arrangement_with_history.md gotcha 10); sort them yourself if you need
        determinism.

        :rtype: list[Halfedge]
        """
        cdef vector[HH] out
        _abase(self.arr).induced_edges(self.h, out)
        cdef list res = []
        cdef size_t i
        for i in range(out.size()):
            res.append(_wrap_halfedge(self.arr, out[i]))
        return res

    @property
    def number_of_induced_edges(self):
        """How many edges this curve currently induces.

        :rtype: int
        """
        return _abase(self.arr).number_of_induced_edges(self.h)

    @property
    def id(self):
        """A unique 64-bit id of this input curve within its arrangement.

        :rtype: int
        """
        return self.h.id

    @property
    def arrangement(self):
        """The :class:`Arrangement` this curve belongs to."""
        return self.arr

    @property
    def is_valid(self):
        """``False`` once the arrangement dropped this curve (e.g. after ``clear()``).

        :rtype: bool
        """
        return bool(_abase(self.arr).curve_valid(self.h))

    def __repr__(self):
        cdef ArrBase* a = _abase(self.arr)
        if not a.curve_valid(self.h):
            return "CurveHandle(<invalid>, id=%d)" % (self.h.id,)
        return "CurveHandle(id=%d, curve=%r, induced_edges=%d)" % (
            self.h.id, self.curve, a.number_of_induced_edges(self.h))

    def __eq__(self, other):
        if not isinstance(other, CurveHandle):
            return NotImplemented
        return (self.arr is (<CurveHandle>other).arr
                and self.h.p == (<CurveHandle>other).h.p
                and self.h.id == (<CurveHandle>other).h.id)

    def __ne__(self, other):
        result = self.__eq__(other)
        if result is NotImplemented:
            return result
        return not result

    def __hash__(self):
        return hash((id(self.arr), <Py_ssize_t>self.h.p, self.h.id))
