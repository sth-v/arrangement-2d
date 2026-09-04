# -*- coding: utf-8 -*-
# _polygon_set.pxi — 2D Boolean set operations (CGAL General_polygon_set_2).
#
# Included by _core.pyx LAST (after _geometry.pxi and _arrangement.pxi).
#
# Supported kinds: segment, circle_segment, conic, bezier (registry.hpp
# kind_has_polygon_set); every other kind raises UnsupportedError from
# arr2d::make_polygon_set.
#
# CGAL notes that shape this binding (docs/dev/cgal61_api/boolean_set_operations.md):
#   * gotcha 5: outer boundaries must be counterclockwise and holes clockwise — this is a
#     CGAL *precondition*, not something CGAL fixes.  PolygonSet.insert(fix_orientation=True)
#     (the default) normalises the orientation for the caller.
#   * gotcha 3: every binary Boolean operation deletes and replaces the underlying
#     arrangement, so an operand must never be the set itself; the operations below clone
#     the operand when `other is self`.
#   * gotcha 2: the *aggregated* do_intersect returns the inverted answer, so only the
#     binary form is used here.


# ===========================================================================
# Coercion helpers
# ===========================================================================

cdef bint _is_ringish(object o):
    """True when `o` looks like a whole polygon ring rather than a single vertex/curve."""
    if isinstance(o, (Polygon, PolygonWithHoles)):
        return True
    if isinstance(o, (Point, Curve, str, bytes, bytearray)):
        return False
    try:
        if len(o) == 0:
            return False
        e = o[0]
    except Exception:
        return False
    if isinstance(e, (Point, Curve, Polygon, PolygonWithHoles)):
        return True
    try:
        len(e)
    except TypeError:
        return False
    return True


cdef int _collect_polygons(list out, object obj, Kind kind, int depth) except -1:
    cdef list items
    if isinstance(obj, (Polygon, PolygonWithHoles)):
        out.append(obj)
        return 0
    if isinstance(obj, (str, bytes, bytearray)):
        raise TypeError("expected a Polygon, a PolygonWithHoles or an iterable of those")
    if depth > 8:
        raise TypeError("polygon container nested too deeply")
    try:
        items = list(obj)
    except TypeError:
        raise TypeError("expected a Polygon, a PolygonWithHoles or an iterable of those, "
                        "got %s" % (type(obj).__name__,))
    if len(items) == 0:
        return 0
    if _is_ringish(items[0]):
        for it in items:
            _collect_polygons(out, it, kind, depth + 1)
        return 0
    out.append(Polygon(items, <int>kind))
    return 0


cdef list _polygon_items(object obj, Kind kind):
    """A flat list of Polygon / PolygonWithHoles objects."""
    cdef list out = []
    _collect_polygons(out, obj, kind, 0)
    return out


cdef object _fix_orientation(object item):
    """Return `item` with a counterclockwise outer boundary and clockwise holes."""
    cdef Polygon p
    cdef PolygonWithHoles pwh
    cdef Polygon outer
    cdef list holes
    cdef bint changed = False
    if isinstance(item, Polygon):
        p = <Polygon>item
        if p.orientation() < 0:
            return p.reverse()
        return p
    if isinstance(item, PolygonWithHoles):
        pwh = <PolygonWithHoles>item
        outer = pwh._outer
        if outer is not None and outer.orientation() < 0:
            outer = outer.reverse()
            changed = True
        holes = []
        for h in pwh._holes:
            if (<Polygon>h).orientation() > 0:
                holes.append((<Polygon>h).reverse())
                changed = True
            else:
                holes.append(h)
        if not changed:
            return pwh
        return PolygonWithHoles(outer, holes)
    raise TypeError("expected a Polygon or a PolygonWithHoles, got %s"
                    % (type(item).__name__,))


cdef Kind _polygon_kind_of(object obj, int depth) except *:
    """Best guess at the geometry kind of a polygon-ish object (defaults to segment)."""
    if isinstance(obj, PolygonSet):
        return (<PolygonSet>obj)._kind
    if isinstance(obj, Polygon):
        return (<Polygon>obj)._kind
    if isinstance(obj, PolygonWithHoles):
        return (<PolygonWithHoles>obj)._kind
    if isinstance(obj, Curve):
        return (<Curve>obj).g.kind
    if isinstance(obj, Point):
        return (<Point>obj).g.kind
    if depth > 8 or isinstance(obj, (str, bytes, bytearray)):
        return _K_SEGMENT
    try:
        for it in obj:
            return _polygon_kind_of(it, depth + 1)
    except TypeError:
        pass
    return _K_SEGMENT


cdef PolygonSet _new_polygon_set_from(object obj, Kind kind):
    """A fresh PolygonSet of `kind` holding the union of everything in `obj`."""
    cdef PolygonSet out = PolygonSet(<int>kind)
    cdef PolygonSet src
    cdef PolygonGeom pg
    if isinstance(obj, PolygonSet):
        src = <PolygonSet>obj
        if <int>src._kind != <int>kind:
            raise KindMismatchError("cannot mix polygon sets of kind '%s' and '%s'"
                                    % (kind_name(src._kind), kind_name(kind)))
        out.ps = src.ps.get().clone()
        return out
    # join_polygon (rather than insert) so that overlapping input polygons are allowed.
    for item in _polygon_items(obj, kind):
        pg = _as_polygon_geom(_fix_orientation(item), kind)
        out.ps.get().join_polygon(pg)
    return out


cdef PolygonSet _coerce_set(object obj, Kind kind):
    """`obj` as a PolygonSet of `kind` (the object itself when it already is one)."""
    cdef PolygonSet s
    if isinstance(obj, PolygonSet):
        s = <PolygonSet>obj
        if <int>s._kind != <int>kind:
            raise KindMismatchError("cannot mix polygon sets of kind '%s' and '%s'"
                                    % (kind_name(s._kind), kind_name(kind)))
        return s
    return _new_polygon_set_from(obj, kind)


cdef bint _is_polygonish(object o):
    return isinstance(o, (PolygonSet, Polygon, PolygonWithHoles, list, tuple))


# ===========================================================================
# PolygonSet
# ===========================================================================

cdef class PolygonSet:
    """A point set bounded by curves of one geometry kind, closed under Boolean operations.

    ``PolygonSet(kind="segment")``.  Boolean set operations exist for the kinds
    ``segment``, ``circle_segment``, ``conic`` and ``bezier``; every other kind raises
    :class:`UnsupportedError`.

    The in-place methods (:meth:`join`, :meth:`intersection`, :meth:`difference`,
    :meth:`symmetric_difference`, :meth:`complement`) return ``self``; the operators
    ``|``, ``&``, ``-``, ``^`` and ``~`` return new sets.

    ``bezier`` kind: inserting a polygon, every Boolean operation and the two set-vs-set
    queries raise :class:`UnsupportedError` when the operands' boundary curves include a
    curve that passes through ANOTHER curve's own SELF-intersection point (the x-monotone
    arcs need not touch: CGAL pairs the SUPPORTING curves).  CGAL 6.1 cannot handle that
    configuration: the shared point is reached by three x-monotone branches at once and
    ``_Bezier_point_2`` can refine or exactly evaluate a point with at most two
    (``CGAL_assertion(_origs.size() == 2)``, ``Bezier_point_2.h:1421`` / ``:1603``).

    :meth:`insert` refuses it as well although CGAL's non-intersecting insertion would
    accept it: the check is what establishes the invariant "a polygon set never holds a
    dangerous pair", which is what makes :meth:`complement`, :meth:`polygons_with_holes`,
    :meth:`to_arrangement` and every future sweeping query safe without each needing its
    own scan -- and it keeps the contract the same as the arrangement's, where the very
    same pair of curves is refused by every insertion path.
    """

    def __cinit__(self, kind="segment"):
        self._kind = _ckind(kind)
        self.ps = make_polygon_set(self._kind)

    def __init__(self, kind="segment"):
        # everything happens in __cinit__; this only documents the signature
        pass

    # ---------------------------------------------------------------- basics
    @property
    def kind(self):
        """The geometry :class:`Kind` of the boundary curves."""
        return _pykind(self._kind)

    @property
    def is_empty(self):
        """True when the set contains no point."""
        return self.ps.get().is_empty()

    @property
    def is_plane(self):
        """True when the set is the whole plane."""
        return self.ps.get().is_plane()

    @property
    def number_of_polygons_with_holes(self):
        """Number of connected components (polygons with holes)."""
        return self.ps.get().number_of_polygons_with_holes()

    @property
    def arrangement_size(self):
        """``(number_of_faces, number_of_edges)`` of the underlying arrangement."""
        return (self.ps.get().arrangement_number_of_faces(),
                self.ps.get().arrangement_number_of_edges())

    # ---------------------------------------------------------------- construction
    def insert(self, polygon, *, fix_orientation=True):
        """Insert one or more polygons that are **disjoint** from the current content.

        `polygon` may be a :class:`Polygon`, a :class:`PolygonWithHoles`, a bare sequence
        of points or curves (taken as one polygon), or an iterable of any of those.

        With ``fix_orientation=True`` (the default) outer boundaries are made
        counterclockwise and holes clockwise, which CGAL requires as a precondition.
        Use :meth:`join` when the new polygons may overlap what is already there.
        """
        cdef vector[PolygonGeom] pgs
        cdef PolygonGeom pg
        cdef list items = _polygon_items(polygon, self._kind)
        for item in items:
            if fix_orientation:
                item = _fix_orientation(item)
            pg = _as_polygon_geom(item, self._kind)
            pgs.push_back(pg)
        if pgs.size() == 1:
            self.ps.get().insert(pgs[0])
        elif pgs.size() > 1:
            self.ps.get().insert_polygons(pgs)

    def clear(self):
        """Remove everything (the set becomes empty)."""
        self.ps.get().clear()

    def copy(self):
        """A deep copy of this set."""
        cdef PolygonSet out = PolygonSet(<int>self._kind)
        out.ps = self.ps.get().clone()
        return out

    def __copy__(self):
        return self.copy()

    def __deepcopy__(self, memo):
        return self.copy()

    # ---------------------------------------------------------------- Boolean operations
    def join(self, other):
        """Union with `other`, in place; returns ``self``."""
        cdef PolygonSet o = _coerce_set(other, self._kind)
        if o is self:
            o = self.copy()
        self.ps.get().join(o.ps.get()[0])
        return self

    def intersection(self, other):
        """Intersection with `other`, in place; returns ``self``."""
        cdef PolygonSet o = _coerce_set(other, self._kind)
        if o is self:
            o = self.copy()
        self.ps.get().intersection(o.ps.get()[0])
        return self

    def difference(self, other):
        """Subtract `other`, in place; returns ``self``."""
        cdef PolygonSet o = _coerce_set(other, self._kind)
        if o is self:
            o = self.copy()
        self.ps.get().difference(o.ps.get()[0])
        return self

    def symmetric_difference(self, other):
        """Symmetric difference with `other`, in place; returns ``self``."""
        cdef PolygonSet o = _coerce_set(other, self._kind)
        if o is self:
            o = self.copy()
        self.ps.get().symmetric_difference(o.ps.get()[0])
        return self

    def complement(self):
        """Replace the set by its complement, in place; returns ``self``."""
        self.ps.get().complement()
        return self

    # ---------------------------------------------------------------- queries
    def polygons_with_holes(self):
        """The connected components as a list of :class:`PolygonWithHoles`."""
        cdef vector[PolygonGeom] out
        cdef size_t i
        self.ps.get().polygons_with_holes(out)
        return [_wrap_polygon_geom(out[i]) for i in range(out.size())]

    def oriented_side(self, point):
        """+1 inside, 0 on the boundary, -1 outside.

        `point` may also be a :class:`PolygonSet` / :class:`Polygon` /
        :class:`PolygonWithHoles`, in which case CGAL's set-vs-set oriented side is
        returned (+1 overlap, 0 touching only, -1 disjoint).
        """
        cdef Geom p
        cdef PolygonSet o
        if isinstance(point, (PolygonSet, Polygon, PolygonWithHoles)):
            o = _coerce_set(point, self._kind)
            return self.ps.get().oriented_side_of_set(o.ps.get()[0])
        p = _as_point(point, self._kind)
        return self.ps.get().oriented_side(p)

    def locate(self, point):
        """The :class:`PolygonWithHoles` containing `point`, or ``None``."""
        cdef Geom p = _as_point(point, self._kind)
        cdef PolygonGeom out
        if not self.ps.get().locate(p, out):
            return None
        return _wrap_polygon_geom(out)

    def do_intersect(self, other):
        """True when this set and `other` have a common point."""
        cdef PolygonSet o = _coerce_set(other, self._kind)
        return self.ps.get().do_intersect(o.ps.get()[0])

    def is_valid(self):
        """True when the underlying arrangement represents a valid set."""
        return self.ps.get().is_valid()

    def to_arrangement(self):
        """``(arrangement, contained_faces)``.

        Builds a fresh :class:`Arrangement` (with history) of the same kind holding every
        boundary curve, together with the list of :class:`Face` objects that belong to
        the set.
        """
        cdef vector[FH] contained
        cdef unique_ptr[ArrBase] p
        cdef Arrangement arr
        cdef size_t i
        p = self.ps.get().to_arrangement(contained)
        arr = _arrangement_from_ptr(p)
        return (arr, [_wrap_face(arr, contained[i]) for i in range(contained.size())])

    # ---------------------------------------------------------------- operators
    def __or__(self, other):
        if not _is_polygonish(other):
            return NotImplemented
        return _new_polygon_set_from(self, self._kind).join(other)

    def __ror__(self, other):
        if not _is_polygonish(other):
            return NotImplemented
        return _new_polygon_set_from(other, self._kind).join(self)

    def __and__(self, other):
        if not _is_polygonish(other):
            return NotImplemented
        return _new_polygon_set_from(self, self._kind).intersection(other)

    def __rand__(self, other):
        if not _is_polygonish(other):
            return NotImplemented
        return _new_polygon_set_from(other, self._kind).intersection(self)

    def __sub__(self, other):
        if not _is_polygonish(other):
            return NotImplemented
        return _new_polygon_set_from(self, self._kind).difference(other)

    def __rsub__(self, other):
        if not _is_polygonish(other):
            return NotImplemented
        return _new_polygon_set_from(other, self._kind).difference(self)

    def __xor__(self, other):
        if not _is_polygonish(other):
            return NotImplemented
        return _new_polygon_set_from(self, self._kind).symmetric_difference(other)

    def __rxor__(self, other):
        if not _is_polygonish(other):
            return NotImplemented
        return _new_polygon_set_from(other, self._kind).symmetric_difference(self)

    def __invert__(self):
        return _new_polygon_set_from(self, self._kind).complement()

    def __ior__(self, other):
        if not _is_polygonish(other):
            return NotImplemented
        return self.join(other)

    def __iand__(self, other):
        if not _is_polygonish(other):
            return NotImplemented
        return self.intersection(other)

    def __isub__(self, other):
        if not _is_polygonish(other):
            return NotImplemented
        return self.difference(other)

    def __ixor__(self, other):
        if not _is_polygonish(other):
            return NotImplemented
        return self.symmetric_difference(other)

    # ---------------------------------------------------------------- protocol
    def __len__(self):
        return self.ps.get().number_of_polygons_with_holes()

    def __iter__(self):
        return iter(self.polygons_with_holes())

    def __repr__(self):
        return "<PolygonSet kind='%s' polygons=%d%s>" % (
            kind_name(self._kind),
            self.ps.get().number_of_polygons_with_holes(),
            " plane" if self.ps.get().is_plane() else
            (" empty" if self.ps.get().is_empty() else ""))


# ===========================================================================
# Module-level Boolean helpers (they never modify their arguments)
# ===========================================================================

def join(a, b):
    """A new :class:`PolygonSet` with the union of `a` and `b`."""
    cdef Kind k = _polygon_kind_of(a, 0)
    return _new_polygon_set_from(a, k).join(b)


def intersection(a, b):
    """A new :class:`PolygonSet` with the intersection of `a` and `b`."""
    cdef Kind k = _polygon_kind_of(a, 0)
    return _new_polygon_set_from(a, k).intersection(b)


def difference(a, b):
    """A new :class:`PolygonSet` with ``a - b``."""
    cdef Kind k = _polygon_kind_of(a, 0)
    return _new_polygon_set_from(a, k).difference(b)


def symmetric_difference(a, b):
    """A new :class:`PolygonSet` with the symmetric difference of `a` and `b`."""
    cdef Kind k = _polygon_kind_of(a, 0)
    return _new_polygon_set_from(a, k).symmetric_difference(b)


def complement(a):
    """A new :class:`PolygonSet` with the complement of `a`."""
    cdef Kind k = _polygon_kind_of(a, 0)
    return _new_polygon_set_from(a, k).complement()


def do_intersect(a, b):
    """True when `a` and `b` share at least one point."""
    cdef Kind k = _polygon_kind_of(a, 0)
    cdef PolygonSet sa = _coerce_set(a, k)
    return sa.do_intersect(b)


def oriented_side(set_or_polygon, point):
    """+1 inside, 0 on the boundary, -1 outside of `set_or_polygon`."""
    cdef Kind k = _polygon_kind_of(set_or_polygon, 0)
    cdef PolygonSet s = _coerce_set(set_or_polygon, k)
    return s.oriented_side(point)


def is_valid_polygon(polygon_or_pwh, kind=None):
    """True when CGAL accepts the polygon (closed, relatively simple, holes inside).

    Raises :class:`UnsupportedError` for kinds without Boolean set operations.
    """
    cdef Kind k
    cdef PolygonSet ps
    cdef PolygonGeom pg
    if kind is None:
        k = _polygon_kind_of(polygon_or_pwh, 0)
    else:
        k = _ckind(kind)
    if not kind_has_polygon_set(k):
        raise UnsupportedError("polygon validity cannot be checked for kind '%s': CGAL "
                               "has no Boolean set operations for it" % (kind_name(k),))
    ps = _polygon_set_for_kind(k)
    pg = _as_polygon_geom(polygon_or_pwh, k)
    return ps.ps.get().is_valid_polygon(pg)


def orientation(polygon):
    """+1 for a counterclockwise boundary, -1 for clockwise, 0 when undecidable.

    Accepts a :class:`Polygon`, a :class:`PolygonWithHoles` (its outer boundary) or
    anything :class:`Polygon` accepts.
    """
    cdef PolygonWithHoles pwh
    if isinstance(polygon, PolygonWithHoles):
        pwh = <PolygonWithHoles>polygon
        if pwh._outer is None:
            return 0
        return pwh._outer.orientation()
    if isinstance(polygon, Polygon):
        return (<Polygon>polygon).orientation()
    return Polygon(polygon).orientation()
