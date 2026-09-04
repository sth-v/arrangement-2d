"""High level *region* helpers on top of :class:`arrangement_2d.Arrangement`.

An arrangement gives you the exact planar subdivision; this module gives you the
operations one usually wants *on top* of it, expressed in terms of whole regions
rather than of single DCEL elements:

============================================  ==============================================
:func:`bounded_faces`                         the faces of finite area
:func:`face_containing`                       the face a point falls in
:func:`face_area`                             signed area of a face (exact where possible)
:func:`faces_polygons`                        polyline approximations of many faces at once
:func:`shared_edges`                          the edges between the faces of a group
:func:`merge_faces`                           merge a group of faces by removing those edges
:func:`split_face`                            cut one face with a curve
:func:`connected_components`                  connected components of the curve network
:func:`number_of_connected_components`        their number (the ``C`` of ``V - E + F = 1 + C``)
:func:`extract_regions`                       select faces and group the adjacent ones
:func:`union_outline`                         the union of faces as a :class:`PolygonSet`
:func:`supports_boolean_ops`                  whether a kind has Boolean set operations
============================================  ==============================================

Everything here is plain Python built on the public API, so the helpers work for every
geometry :class:`~arrangement_2d.Kind` unless a docstring says otherwise (the Boolean
helpers need a kind with Boolean set operations; :func:`face_area` is only exact for the
kinds with rational vertices).

The module is imported lazily::

    import arrangement_2d as a2
    a2.regions.bounded_faces(arr)          # imports arrangement_2d.regions on first use

    from arrangement_2d.regions import merge_faces
"""

from __future__ import annotations

import collections
from typing import Any, Callable, Iterable, Optional

from . import _core
from ._core import (
    Arrangement,
    Curve,
    Face,
    Halfedge,
    Kind,
    PolygonSet,
    Vertex,
)
from .errors import InvalidHandleError, UnsupportedError

__all__ = [
    "Component",
    "FaceBoundary",
    "bounded_faces",
    "connected_components",
    "extract_regions",
    "face_area",
    "face_containing",
    "faces_polygons",
    "merge_faces",
    "number_of_connected_components",
    "shared_edges",
    "split_face",
    "supports_boolean_ops",
    "union_outline",
]


# ---------------------------------------------------------------------------
# result records
# ---------------------------------------------------------------------------

class FaceBoundary(collections.namedtuple("FaceBoundary", ("face", "outer", "holes"))):
    """One face and its polyline approximation, as returned by :func:`faces_polygons`.

    :ivar face: the :class:`~arrangement_2d.Face` itself.
    :ivar outer: the outer boundary as a list of coordinate tuples, counterclockwise
        (empty for a face without an outer CCB -- an unbounded or spherical face).
    :ivar holes: one list of coordinate tuples per hole, each clockwise.
    """

    __slots__ = ()


class Component(collections.namedtuple("Component", ("vertices", "edges"))):
    """One connected component of the curve network, as returned by
    :func:`connected_components`.

    :ivar vertices: the :class:`~arrangement_2d.Vertex` objects of the component.  An
        isolated vertex forms a component of its own (with no edges).
    :ivar edges: one :class:`~arrangement_2d.Halfedge` per edge of the component.
    """

    __slots__ = ()


# ---------------------------------------------------------------------------
# small internal helpers
# ---------------------------------------------------------------------------

def _check_arrangement(arr: Any) -> Arrangement:
    if not isinstance(arr, Arrangement):
        raise TypeError("expected an Arrangement, got %s" % (type(arr).__name__,))
    return arr


def _check_face(arr: Arrangement, face: Any) -> Face:
    """Validate that *face* is a live face of *arr* and return it."""
    if not isinstance(face, Face):
        raise TypeError("expected a Face, got %s" % (type(face).__name__,))
    if face.arrangement is not arr:
        raise InvalidHandleError("this Face belongs to a different arrangement")
    if not face.is_valid:
        raise InvalidHandleError("this Face is no longer part of its arrangement")
    return face


def _face_list(arr: Arrangement, faces: Iterable[Face]) -> list:
    """*faces* as a list of live faces of *arr*, deduplicated, in the given order."""
    out: list = []
    seen: set = set()
    for f in faces:
        _check_face(arr, f)
        if f.id in seen:
            continue
        seen.add(f.id)
        out.append(f)
    return out


def _live(faces: Iterable[Face]) -> list:
    """The faces of *faces* that are still part of their arrangement."""
    return [f for f in faces if f.is_valid]


# ---------------------------------------------------------------------------
# faces
# ---------------------------------------------------------------------------

def bounded_faces(arr: Arrangement) -> list:
    """Every face of *arr* with a finite area.

    The unbounded faces are left out, and so is the *fictitious* face of the ``linear``
    kind (it is invisible to CGAL's face iterator anyway -- reach it through
    :attr:`Arrangement.fictitious_face`).  On the sphere no face is unbounded, so every
    face is returned, including the spherical (reference) face.

    :param arr: the arrangement to look at.
    :returns: the bounded faces, in the arrangement's own face order.
    :rtype: list[Face]

    ::

        >>> import arrangement_2d as a2
        >>> arr = a2.Arrangement("segment")
        >>> _ = arr.insert([a2.Segment((0, 0), (1, 0)), a2.Segment((1, 0), (0, 1)),
        ...                 a2.Segment((0, 1), (0, 0))])
        >>> len(a2.regions.bounded_faces(arr))
        1
    """
    return _check_arrangement(arr).bounded_faces()


def face_containing(
    arr: Arrangement,
    point: Any,
    *,
    strategy: Optional[str] = None,
    on_boundary: str = "none",
) -> Optional[Face]:
    """The face of *arr* that contains *point*.

    :param arr: the arrangement to query.
    :param point: a :class:`~arrangement_2d.Point`, an ``(x, y)`` tuple (``(x, y, z)`` on
        the sphere) or anything else :meth:`Arrangement.locate` accepts.
    :param strategy: point location strategy (see
        :meth:`Arrangement.attach_point_location`); ``None`` uses an attached strategy if
        there is one and a walk query otherwise.
    :param on_boundary: what to do when *point* lies exactly on an edge or on a vertex,
        where no single face contains it:

        * ``"none"`` (the default) -- return ``None``;
        * ``"any"`` -- return one of the incident faces (the face to the left of the
          located halfedge, or the face of an incident halfedge of the located vertex);
        * ``"raise"`` -- raise :class:`ValueError`.

    :returns: the containing :class:`~arrangement_2d.Face`, or ``None``.
    :rtype: Face | None
    :raises ValueError: *on_boundary* is not one of the three names, or it is
        ``"raise"`` and the point is on the boundary.

    ::

        >>> arr.insert(a2.Segment((0, 0), (4, 4)))            # doctest: +SKIP
        >>> a2.regions.face_containing(arr, (1, 3))           # doctest: +SKIP
        Face(id=..., unbounded=..., ...)
    """
    _check_arrangement(arr)
    if on_boundary not in ("none", "any", "raise"):
        raise ValueError(
            "on_boundary must be 'none', 'any' or 'raise', not %r" % (on_boundary,)
        )
    located = arr.locate(point, strategy)
    if isinstance(located, Face):
        return located
    if on_boundary == "none":
        return None
    if on_boundary == "raise":
        raise ValueError(
            "the point lies on %s, not inside a face"
            % ("a vertex" if isinstance(located, Vertex) else "an edge",)
        )
    if isinstance(located, Halfedge):
        he = located if not located.is_fictitious else located.twin
        return he.face
    if isinstance(located, Vertex):
        if located.is_isolated:
            return located.face
        for he in located.incident_halfedges():
            if not he.is_fictitious:
                return he.face
        return None
    return None


def face_area(face: Face) -> Any:
    """The area of *face* (outer boundary minus holes).

    The result is an exact :class:`fractions.Fraction` for the three kinds with rational
    vertices -- ``segment``, ``linear`` and ``polyline`` -- and a ``float`` computed from
    a polyline approximation of the boundary (deviation ``1e-3``) for the curved kinds
    ``circle_segment``, ``conic`` and ``bezier``.

    :param face: a bounded face.
    :returns: the area; positive, because a face's outer CCB runs counterclockwise.
    :rtype: fractions.Fraction | float
    :raises UnsupportedError: *face* has no outer CCB (an unbounded, fictitious or
        spherical face has no finite area).

    ::

        >>> import arrangement_2d as a2
        >>> arr = a2.Arrangement("segment")
        >>> _ = arr.insert([a2.Segment((0, 0), (2, 0)), a2.Segment((2, 0), (2, 2)),
        ...                 a2.Segment((2, 2), (0, 2)), a2.Segment((0, 2), (0, 0))])
        >>> a2.regions.face_area(a2.regions.bounded_faces(arr)[0])
        Fraction(4, 1)
    """
    if not isinstance(face, Face):
        raise TypeError("expected a Face, got %s" % (type(face).__name__,))
    pwh = face.polygon()                      # raises UnsupportedError without outer CCB
    total = pwh.outer.area()
    for hole in pwh.holes:
        total = total + hole.area()           # holes are clockwise: negative area
    return total


def faces_polygons(
    arr: Arrangement,
    tolerance: float = 1e-3,
    *,
    faces: Optional[Iterable[Face]] = None,
    include_unbounded: bool = False,
) -> list:
    """Approximate the boundaries of many faces at once.

    Every curved edge is replaced by a polyline that stays within *tolerance* of it, so
    the result is directly usable for drawing, for point-in-polygon tests in floating
    point, or for export to a format that only knows polygons.

    :param arr: the arrangement the faces belong to.
    :param tolerance: maximum deviation of the approximation, in coordinate units
        (ignored by the ``segment``, ``linear`` and ``polyline`` kinds, whose edges are
        already straight).
    :param faces: the faces to approximate; by default every *bounded* face of *arr*.
    :param include_unbounded: also approximate unbounded faces when *faces* is ``None``.
        Such a face has no outer boundary, so its :attr:`FaceBoundary.outer` is empty and
        only its holes are filled in.
    :returns: one :class:`FaceBoundary` per face, in the order of *faces*.
    :rtype: list[FaceBoundary]

    ::

        >>> for fb in a2.regions.faces_polygons(arr, 1e-2):      # doctest: +SKIP
        ...     print(fb.face.id, len(fb.outer), len(fb.holes))
    """
    _check_arrangement(arr)
    if faces is None:
        selected = arr.faces() if include_unbounded else arr.bounded_faces()
    else:
        selected = _face_list(arr, faces)
    out = []
    for f in selected:
        outer, holes = f.boundary_points(tolerance)
        out.append(FaceBoundary(f, outer, holes))
    return out


# ---------------------------------------------------------------------------
# merging / splitting
# ---------------------------------------------------------------------------

def shared_edges(faces: Iterable[Face]) -> list:
    """The edges that separate two *different* faces of *faces*.

    An edge is "shared" when the faces on its two sides are two distinct members of the
    group; these are exactly the edges :func:`merge_faces` removes.  Edges on the outer
    rim of the group, and edges that already have the same face on both sides (an
    antenna), are not returned.

    :param faces: an iterable of faces of one arrangement.
    :returns: one :class:`~arrangement_2d.Halfedge` per shared edge (the halfedge whose
        left face comes first in *faces*), in discovery order.
    :rtype: list[Halfedge]
    :raises InvalidHandleError: the faces do not all belong to the same arrangement, or
        one of them is stale.
    """
    group = list(faces)
    if not group:
        return []
    arr = group[0].arrangement if isinstance(group[0], Face) else None
    if arr is None:
        raise TypeError("expected an iterable of Face, got %s"
                        % (type(group[0]).__name__,))
    group = _face_list(arr, group)
    ids = {f.id for f in group}
    out: list = []
    seen: set = set()
    for f in group:
        for he in f.edges():
            if he.is_fictitious:
                continue
            other = he.twin.face
            if other.id == f.id or other.id not in ids:
                continue
            key = he.edge_id
            if key in seen:
                continue
            seen.add(key)
            out.append(he)
    return out


def merge_faces(
    arr: Arrangement,
    faces: Iterable[Face],
    *,
    remove_vertices: bool = True,
) -> list:
    """Merge *faces* into as few faces as possible by removing the edges between them.

    Every edge whose two incident faces are two distinct members of *faces* is removed
    (:meth:`Arrangement.remove_edge`), which merges the faces on its two sides.  The
    arrangement is modified in place; the merged region keeps the boundary it had towards
    the rest of the arrangement.

    Faces of the group that are not adjacent stay separate, so the result is a list: one
    face per connected group.  Use :func:`extract_regions` to obtain such connected
    groups in the first place.

    :param arr: the arrangement to modify.
    :param faces: the faces to merge; they must all be live faces of *arr*.
    :param remove_vertices: also drop the vertices that the removals leave isolated or
        redundant (a degree-2 vertex whose two edges can be merged back into one curve),
        so that the merged region has a clean boundary.  Set it to ``False`` to keep
        every vertex.
    :returns: the surviving faces (the merged ones), a sublist of *faces*.
    :rtype: list[Face]
    :raises InvalidHandleError: a face is stale or belongs to another arrangement.

    .. note::
       The edges are collected **before** anything is removed, which also takes care of
       two faces that share more than one edge: the second such edge has the merged face
       on both sides and is removed as well (otherwise it would be left dangling in the
       interior of the result).

    .. note::
       ``remove_vertices=True`` cannot drop a vertex that lies on a pole or on the
       identification curve of the ``sphere`` kind (CGAL 6.1 leaves dangling topology
       pointers behind, so :meth:`Arrangement.remove_vertex` refuses it); such a vertex
       is kept and the merge itself is unaffected.

    ::

        >>> region = a2.regions.merge_faces(arr, arr.bounded_faces())   # doctest: +SKIP
        >>> len(region)                                                 # doctest: +SKIP
        1
    """
    _check_arrangement(arr)
    group = _face_list(arr, faces)
    if len(group) < 2:
        return _live(group)

    targets = shared_edges(group)
    touched_vertices: list = []
    for he in targets:
        touched_vertices.append(he.source)
        touched_vertices.append(he.target)

    # The edges are removed one by one WITHOUT the vertex cleanup, so no edge is ever
    # merged into another one and every remaining target handle stays valid; the vertices
    # are cleaned up afterwards, in one pass.
    for he in targets:
        if not he.is_valid:
            continue
        arr.remove_edge(he, False, False)

    if remove_vertices:
        for v in touched_vertices:
            if not v.is_valid:
                continue
            try:
                arr.remove_vertex(v)
            except UnsupportedError:
                # sphere kind: a pole / identification vertex cannot be removed safely.
                continue
    return _live(group)


def split_face(arr: Arrangement, face: Face, curve: Any) -> list:
    """Cut *face* with *curve* and return the pieces.

    *curve* (or every curve of an iterable) must lie inside *face*: its interior may not
    cross the face boundary, although its endpoints may lie on it.  The condition is
    verified with :meth:`Arrangement.zone` **before** anything is inserted, so a curve
    that would leave the face is rejected with the arrangement untouched.

    :param arr: the arrangement to modify.
    :param face: the face to cut.
    :param curve: a :class:`~arrangement_2d.Curve` of (or convertible to) ``arr.kind``,
        or an iterable of such curves -- they are then inserted together with one
        sweep.
    :returns: *face* together with every face the insertion created, ordered by id.  A
        curve that does not actually separate anything (one that ends inside the face,
        for instance) leaves ``[face]``.
    :rtype: list[Face]
    :raises ValueError: *curve* is not contained in *face*, or the iterable is empty.
    :raises InvalidHandleError: *face* is stale or belongs to another arrangement.

    ::

        >>> import arrangement_2d as a2
        >>> arr = a2.Arrangement("segment")
        >>> _ = arr.insert([a2.Segment((0, 0), (2, 0)), a2.Segment((2, 0), (2, 2)),
        ...                 a2.Segment((2, 2), (0, 2)), a2.Segment((0, 2), (0, 0))])
        >>> inside = a2.regions.bounded_faces(arr)[0]
        >>> pieces = a2.regions.split_face(arr, inside, a2.Segment((0, 1), (2, 1)))
        >>> len(pieces)
        2
    """
    _check_arrangement(arr)
    _check_face(arr, face)
    if isinstance(curve, Curve):
        curves = [curve]
    else:
        curves = list(curve)
        if not curves:
            raise ValueError("split_face() needs at least one curve")

    fid = face.id
    for c in curves:
        for element in arr.zone(c):
            if isinstance(element, Face) and element.id != fid:
                raise ValueError(
                    "the curve %r is not contained in face %d: its zone also visits "
                    "face %d" % (c, fid, element.id)
                )

    before = max([f.id for f in arr.faces()], default=0)
    arr.insert(curves)
    out = [face] if face.is_valid else []
    out.extend(f for f in arr.faces() if f.id > before)
    out.sort(key=lambda f: f.id)
    return out


# ---------------------------------------------------------------------------
# connectivity
# ---------------------------------------------------------------------------

def connected_components(
    arr: Arrangement,
    *,
    include_vertices_at_infinity: bool = False,
) -> list:
    """The connected components of the curve network of *arr*.

    Two vertices are in the same component when an edge path connects them; an isolated
    vertex is a component of its own.  Fictitious halfedges (the bounding rectangle of
    the unbounded ``linear`` topology) are not edges of the network and never connect
    anything.

    :param arr: the arrangement to analyse.
    :param include_vertices_at_infinity: list the vertices at infinity of the ``linear``
        kind in :attr:`Component.vertices` as well.  They always take part in the
        connectivity; they are left out of the listing by default because they carry no
        point (:attr:`Vertex.point` raises for them).
    :returns: one :class:`Component` per component, in the order in which the components
        are first met while walking :meth:`Arrangement.vertices` and then
        :meth:`Arrangement.edges`.
    :rtype: list[Component]

    ::

        >>> import arrangement_2d as a2
        >>> arr = a2.Arrangement("segment")
        >>> _ = arr.insert([a2.Segment((0, 0), (1, 0)), a2.Segment((5, 5), (6, 5))])
        >>> [len(c.vertices) for c in a2.regions.connected_components(arr)]
        [2, 2]
    """
    _check_arrangement(arr)
    parent: dict = {}
    vertices: dict = {}
    order: list = []

    def find(x):
        root = x
        while parent[root] != root:
            root = parent[root]
        while parent[x] != root:
            parent[x], x = root, parent[x]
        return root

    def add(v):
        if v.id not in parent:
            parent[v.id] = v.id
            vertices[v.id] = v
            order.append(v.id)
        return v.id

    def union(a, b):
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[rb] = ra

    for v in arr.vertices():
        add(v)
    edges: dict = {}
    for he in arr.edges():
        if he.is_fictitious:
            continue
        s = add(he.source)
        t = add(he.target)
        union(s, t)
        edges.setdefault(find(s), []).append(he)

    groups: dict = {}
    group_order: list = []
    for vid in order:
        root = find(vid)
        if root not in groups:
            groups[root] = []
            group_order.append(root)
        v = vertices[vid]
        if include_vertices_at_infinity or not v.is_at_open_boundary:
            groups[root].append(v)

    # the edge lists were bucketed under intermediate roots; re-bucket under the final one
    final_edges: dict = {}
    for root, hes in edges.items():
        final_edges.setdefault(find(root), []).extend(hes)

    return [Component(groups[r], final_edges.get(r, [])) for r in group_order]


def number_of_connected_components(arr: Arrangement) -> int:
    """The number of connected components of the curve network of *arr*.

    This is the ``C`` of the generalised Euler relation ``V - E + F = 1 + C`` (with ``V``
    the concrete vertices, ``E`` the concrete edges and ``F`` every face including the
    unbounded one).

    :param arr: the arrangement to analyse.
    :rtype: int

    ::

        >>> a2.regions.number_of_connected_components(arr)      # doctest: +SKIP
        2
    """
    return len(connected_components(arr))


def extract_regions(
    arr: Arrangement,
    predicate: Optional[Callable[[Face], bool]] = None,
    *,
    faces: Optional[Iterable[Face]] = None,
) -> list:
    """Select faces and group the adjacent ones into regions.

    A *region* is a maximal set of selected faces connected through shared edges; it is
    exactly what :func:`merge_faces` can merge into a single face and what
    :func:`union_outline` turns into one connected polygon.

    :param arr: the arrangement to look at.
    :param predicate: a callable ``face -> bool``; ``None`` selects every candidate face.
        A typical predicate reads :attr:`Face.data`, e.g.
        ``lambda f: f.data == "inside"``.
    :param faces: the candidate faces; by default every *bounded* face of *arr* (an
        unbounded face is a legitimate candidate too -- pass ``arr.faces()`` to include
        it).
    :returns: one list of faces per region, ordered by the position of the region's first
        face in the candidate list; the faces inside a region keep that order too.
    :rtype: list[list[Face]]

    ::

        >>> import arrangement_2d as a2
        >>> arr = a2.Arrangement("segment")
        >>> _ = arr.insert([a2.Segment((0, 0), (3, 0)), a2.Segment((3, 0), (3, 1)),
        ...                 a2.Segment((3, 1), (0, 1)), a2.Segment((0, 1), (0, 0)),
        ...                 a2.Segment((1, 0), (1, 1)), a2.Segment((2, 0), (2, 1))])
        >>> for f in arr.bounded_faces():
        ...     f.data = "keep" if f.polygon().outer.bbox()[0] != 1.0 else "drop"
        >>> [len(r) for r in a2.regions.extract_regions(arr, lambda f: f.data == "keep")]
        [1, 1]
    """
    _check_arrangement(arr)
    if faces is None:
        candidates = arr.bounded_faces()
    else:
        candidates = _face_list(arr, faces)
    if predicate is not None:
        candidates = [f for f in candidates if predicate(f)]
    if not candidates:
        return []

    index = {f.id: i for i, f in enumerate(candidates)}
    parent = list(range(len(candidates)))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    for i, f in enumerate(candidates):
        for he in f.edges():
            if he.is_fictitious:
                continue
            j = index.get(he.twin.face.id)
            if j is None or j == i:
                continue
            ri, rj = find(i), find(j)
            if ri != rj:
                parent[rj] = ri

    groups: dict = {}
    group_order: list = []
    for i, f in enumerate(candidates):
        root = find(i)
        if root not in groups:
            groups[root] = []
            group_order.append(root)
        groups[root].append(f)
    return [groups[r] for r in group_order]


# ---------------------------------------------------------------------------
# Boolean set operations
# ---------------------------------------------------------------------------

_BSO_KINDS: Optional[frozenset] = None


def supports_boolean_ops(kind: Any) -> bool:
    """Whether *kind* has 2D Boolean set operations in this build.

    ``segment``, ``circle_segment``, ``conic`` and ``bezier`` do;  ``linear``,
    ``polyline`` and ``sphere`` do not (CGAL's ``General_polygon_set_2`` needs a bounded
    planar traits with the general-polygon adapters).  The answer is obtained by asking
    the extension module, not from a hard-coded list.

    :param kind: a :class:`~arrangement_2d.Kind`, a kind name, or any object with a
        ``.kind`` (a curve, a polygon, an arrangement...).
    :rtype: bool

    ::

        >>> import arrangement_2d as a2
        >>> a2.regions.supports_boolean_ops("segment"), a2.regions.supports_boolean_ops("polyline")
        (True, False)
    """
    global _BSO_KINDS
    if _BSO_KINDS is None:
        found = set()
        for k in _core.available_kinds():
            try:
                PolygonSet(k)
            except UnsupportedError:
                continue
            found.add(k)
        _BSO_KINDS = frozenset(found)
    return _kind_of(kind) in _BSO_KINDS


def _kind_of(kind: Any) -> Kind:
    """Any kind-like object -> the :class:`Kind` member.

    Every spelling the extension module understands is accepted (a :class:`Kind`, an
    ``int``, one of the kind name aliases, or an object with a ``.kind``); the
    normalisation is done by the module itself rather than duplicated here.
    """
    return _core.traits(kind).kind


def union_outline(
    arr: Arrangement,
    faces: Optional[Iterable[Face]] = None,
) -> PolygonSet:
    """The union of the given faces as a :class:`~arrangement_2d.PolygonSet`.

    Every face contributes its exact boundary (:meth:`Face.polygon`), so the result is
    exact: no approximation happens anywhere.  Joining the faces fills in the edges
    between them, which makes this the "outline" of the covered area -- a face's hole
    survives only if no face of the group fills it.

    :param arr: the arrangement the faces belong to; its
        :attr:`~arrangement_2d.Arrangement.kind` must have Boolean set operations
        (:func:`supports_boolean_ops`).
    :param faces: the faces to unite; by default every *bounded* face of *arr*, i.e. the
        whole region covered by the arrangement.
    :returns: a fresh polygon set (the arrangement is not modified).
    :rtype: PolygonSet
    :raises UnsupportedError: the kind has no Boolean set operations, or one of the faces
        has no outer CCB (an unbounded or spherical face is not a polygon).

    ::

        >>> import arrangement_2d as a2
        >>> arr = a2.Arrangement("segment")
        >>> _ = arr.insert([a2.Segment((0, 0), (2, 0)), a2.Segment((2, 0), (2, 2)),
        ...                 a2.Segment((2, 2), (0, 2)), a2.Segment((0, 2), (0, 0)),
        ...                 a2.Segment((0, 1), (2, 1))])
        >>> outline = a2.regions.union_outline(arr)
        >>> outline.number_of_polygons_with_holes
        1
        >>> len(outline.polygons_with_holes()[0].outer)      # the chord is gone
        4
    """
    _check_arrangement(arr)
    selected = arr.bounded_faces() if faces is None else _face_list(arr, faces)
    out = PolygonSet(arr.kind)                # raises UnsupportedError for a kind without
    for f in selected:
        out.join(f.polygon())
    return out
