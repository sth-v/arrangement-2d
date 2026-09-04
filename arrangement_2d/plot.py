"""Matplotlib helpers for :mod:`arrangement_2d`.

Nothing here is imported until you call one of the functions: matplotlib is an optional
dependency and is imported *inside* the plotting functions, so importing
``arrangement_2d.plot`` on a machine without matplotlib works and only calling a plot
function fails (with a clear :class:`ImportError`).

===============================  =========================================================
:func:`plot_arrangement`         faces, edges and vertices of an :class:`Arrangement`
:func:`plot_polygon_set`         a :class:`PolygonSet` / :class:`PolygonWithHoles` / polygon
:func:`plot_curves`              a bare list of curves
:func:`has_matplotlib`           whether matplotlib can be imported at all
===============================  =========================================================

Everything curved is drawn from an exact CGAL approximation whose deviation you control
with ``tolerance``; the plot is therefore as accurate as you ask for, and never more
expensive than that.

::

    import matplotlib.pyplot as plt
    import arrangement_2d as a2

    arr = a2.Arrangement("circle_segment")
    arr.insert([a2.Circle((0, 0), 2), a2.Circle((2, 0), 2)])
    a2.plot.plot_arrangement(arr, tolerance=1e-2, face_colors="index")
    plt.show()
"""

from __future__ import annotations

import collections.abc
import math
from typing import Any, Callable, Iterable, Optional, Sequence

from ._core import (
    Arrangement,
    Curve,
    Face,
    Kind,
    Polygon,
    PolygonSet,
    PolygonWithHoles,
)

__all__ = [
    "has_matplotlib",
    "lonlat",
    "plot_arrangement",
    "plot_curves",
    "plot_polygon_set",
]


# ---------------------------------------------------------------------------
# matplotlib access (lazy on purpose)
# ---------------------------------------------------------------------------

def has_matplotlib() -> bool:
    """Whether matplotlib is importable in this interpreter.

    :rtype: bool

    ::

        >>> import arrangement_2d as a2
        >>> a2.plot.has_matplotlib() in (True, False)
        True
    """
    try:
        import matplotlib  # noqa: F401
    except Exception:
        return False
    return True


def _mpl():
    """Import and return ``(pyplot, collections, patches, path)`` of matplotlib.

    :raises ImportError: matplotlib is not installed.
    """
    try:
        import matplotlib.collections as mcollections
        import matplotlib.patches as mpatches
        import matplotlib.path as mpath
        import matplotlib.pyplot as plt
    except ImportError as exc:  # pragma: no cover - depends on the environment
        raise ImportError(
            "arrangement_2d.plot needs matplotlib; install it with "
            "'pip install matplotlib'"
        ) from exc
    return plt, mcollections, mpatches, mpath


def _axes(ax, aspect: Optional[str]):
    """Return *ax*, or a fresh one from ``plt.gca()``."""
    plt, _, _, _ = _mpl()
    if ax is None:
        ax = plt.gca()
    if aspect:
        ax.set_aspect(aspect)
    return ax


# ---------------------------------------------------------------------------
# geometry plumbing
# ---------------------------------------------------------------------------

def lonlat(x: float, y: float, z: float) -> tuple:
    """Equirectangular projection of a direction on the sphere.

    :param x: direction components; they need not be normalised.
    :param y: see *x*.
    :param z: see *x*.
    :returns: ``(longitude, latitude)`` in radians, longitude in ``(-pi, pi]`` and
        latitude in ``[-pi/2, pi/2]``.
    :rtype: tuple[float, float]

    This is the default projection :func:`plot_arrangement` uses for the ``sphere`` kind;
    pass your own ``projection=`` callable with the same signature for anything else.
    """
    norm = math.sqrt(x * x + y * y + z * z)
    if norm == 0.0:
        return (0.0, 0.0)
    return (math.atan2(y, x), math.asin(max(-1.0, min(1.0, z / norm))))


def _chain_to_xy(chain: Iterable, dim: int, projection) -> list:
    """One approximated curve -> a list of 2D polylines.

    A projection may split the chain into several pieces: a geodesic that crosses the
    antimeridian jumps from ``+pi`` to ``-pi`` in longitude, and drawing that jump would
    put a spurious line right across the plot.
    """
    if projection is None:
        pts = [(float(p[0]), float(p[1])) for p in chain]
        return [pts] if len(pts) >= 2 else []
    out: list = []
    current: list = []
    previous = None
    for p in chain:
        coords = [float(v) for v in p][:dim]
        q = projection(*coords)
        q = (float(q[0]), float(q[1]))
        if previous is not None and abs(q[0] - previous[0]) > math.pi:
            # the chain wrapped around the antimeridian: start a new piece
            if len(current) >= 2:
                out.append(current)
            current = []
        current.append(q)
        previous = q
    if len(current) >= 2:
        out.append(current)
    return out


def _signed_area(ring: Sequence) -> float:
    total = 0.0
    n = len(ring)
    for i in range(n):
        x1, y1 = ring[i][0], ring[i][1]
        x2, y2 = ring[(i + 1) % n][0], ring[(i + 1) % n][1]
        total += x1 * y2 - x2 * y1
    return 0.5 * total


def _closed_ring(ring: Sequence) -> list:
    """*ring* as a list of 2-tuples without a repeated closing point."""
    pts = [(float(p[0]), float(p[1])) for p in ring]
    if len(pts) > 1 and pts[0] == pts[-1]:
        pts.pop()
    return pts


def _orient(ring: list, ccw: bool) -> list:
    """*ring*, reversed if its orientation is not the requested one."""
    if not ring:
        return ring
    area = _signed_area(ring)
    if (area < 0.0 and ccw) or (area > 0.0 and not ccw):
        return list(reversed(ring))
    return ring


def _ring_path(outer: Sequence, holes: Sequence, mpath):
    """A compound :class:`matplotlib.path.Path` for one outer ring and its holes.

    The outer ring is forced counterclockwise and every hole clockwise, so the default
    non-zero winding rule punches the holes out of the fill.
    """
    Path = mpath.Path
    verts: list = []
    codes: list = []
    rings = []
    ring = _orient(_closed_ring(outer), True)
    if len(ring) >= 3:
        rings.append(ring)
    for h in holes:
        ring = _orient(_closed_ring(h), False)
        if len(ring) >= 3:
            rings.append(ring)
    if not rings:
        return None
    for ring in rings:
        verts.extend(ring)
        verts.append(ring[0])
        codes.append(Path.MOVETO)
        codes.extend([Path.LINETO] * (len(ring) - 1))
        codes.append(Path.CLOSEPOLY)
    return Path(verts, codes)


def _bounds(points: Iterable) -> Optional[tuple]:
    """``(xmin, ymin, xmax, ymax)`` of an iterable of 2D points, or ``None``."""
    xmin = ymin = float("inf")
    xmax = ymax = float("-inf")
    empty = True
    for p in points:
        x, y = float(p[0]), float(p[1])
        empty = False
        xmin = min(xmin, x)
        ymin = min(ymin, y)
        xmax = max(xmax, x)
        ymax = max(ymax, y)
    return None if empty else (xmin, ymin, xmax, ymax)


def _pad(box: tuple, fraction: float = 0.05) -> tuple:
    xmin, ymin, xmax, ymax = box
    px = max(fraction * (xmax - xmin), 1e-9 if xmax > xmin else 1.0)
    py = max(fraction * (ymax - ymin), 1e-9 if ymax > ymin else 1.0)
    return (xmin - px, ymin - py, xmax + px, ymax + py)


# ---------------------------------------------------------------------------
# colours
# ---------------------------------------------------------------------------

def _color_cycle(ax) -> list:
    """The colours of the axes' property cycle (never empty)."""
    import matplotlib as mpl

    try:
        colors = list(mpl.rcParams["axes.prop_cycle"].by_key().get("color", []))
    except (KeyError, AttributeError):        # pragma: no cover - exotic rc settings
        colors = []
    return colors or ["C%d" % i for i in range(10)]


def _resolve_face_colors(faces: Sequence, face_colors: Any, ax) -> list:
    """One colour (or ``None`` = do not fill) per face of *faces*.

    See :func:`plot_arrangement` for the accepted spellings of *face_colors*.
    """
    if face_colors is None:
        return [None] * len(faces)

    if callable(face_colors) and not isinstance(face_colors, (str, bytes)):
        return [face_colors(f) for f in faces]

    if isinstance(face_colors, str) and face_colors in ("index", "data"):
        palette = _color_cycle(ax)
        if face_colors == "index":
            return [palette[i % len(palette)] for i in range(len(faces))]
        out: list = []
        seen: dict = {}
        for f in faces:
            value = f.data
            if value is None:
                out.append(None)
                continue
            try:
                hash(value)
                key: Any = value
            except TypeError:                 # an unhashable value still groups by text
                key = repr(value)
            if key not in seen:
                seen[key] = palette[len(seen) % len(palette)]
            out.append(seen[key])
        return out

    if isinstance(face_colors, dict):
        out = []
        for f in faces:
            if f in face_colors:
                out.append(face_colors[f])
                continue
            if f.id in face_colors:
                out.append(face_colors[f.id])
                continue
            data = f.data
            try:
                hit = data in face_colors
            except TypeError:
                hit = False
            out.append(face_colors[data] if hit else None)
        return out

    if isinstance(face_colors, (str, bytes)) or (
        isinstance(face_colors, tuple)
        and len(face_colors) in (3, 4)
        and all(isinstance(v, (int, float)) and not isinstance(v, bool)
                for v in face_colors)
    ):
        return [face_colors] * len(faces)

    if isinstance(face_colors, collections.abc.Sequence):
        seq = list(face_colors)
        return [seq[i] if i < len(seq) else None for i in range(len(faces))]

    raise TypeError(
        "face_colors must be None, 'index', 'data', a colour, a callable, a mapping or a "
        "sequence, not %s" % (type(face_colors).__name__,)
    )


# ---------------------------------------------------------------------------
# public plotting functions
# ---------------------------------------------------------------------------

def plot_arrangement(
    arr: Arrangement,
    ax=None,
    *,
    tolerance: float = 1e-3,
    bbox: Optional[Sequence] = None,
    show_vertices: bool = True,
    show_edges: bool = True,
    faces: Optional[Iterable[Face]] = None,
    face_colors: Any = None,
    face_alpha: float = 0.6,
    edge_color: Any = "0.15",
    linewidth: float = 1.2,
    vertex_color: Any = "0.15",
    vertex_size: float = 12.0,
    projection: Optional[Callable] = None,
    aspect: Optional[str] = "equal",
    autoscale: bool = True,
):
    """Draw an :class:`~arrangement_2d.Arrangement` on a matplotlib axes.

    Faces are filled first, then the edges, then the vertices.  Every curved edge is
    replaced by a polyline that stays within *tolerance* of the exact curve.

    :param arr: the arrangement to draw.
    :param ax: the axes to draw on; ``None`` uses ``matplotlib.pyplot.gca()``.
    :param tolerance: maximum deviation of the curve approximation, in coordinate units.
        Ignored by the kinds whose edges are already straight (``segment``, ``linear``,
        ``polyline``).
    :param bbox: ``(xmin, ymin, xmax, ymax)`` used to clip the unbounded edges of the
        ``linear`` kind and to set the view limits.  A clipping box is *required* for
        unbounded curves; when it is omitted the arrangement's own bounding box padded by
        10 % is used, which is what :meth:`Arrangement.approximate_edges` does.
    :param show_vertices: draw a marker on every concrete vertex (vertices at infinity
        have no location and are never drawn).
    :param show_edges: draw the edges.
    :param faces: the faces to fill; by default every bounded face
        (:meth:`Arrangement.bounded_faces`).  An unbounded face has no outer boundary and
        cannot be filled.
    :param face_colors: how to colour the filled faces:

        * ``None`` (the default) -- do not fill anything;
        * ``"index"`` -- the i-th face gets the i-th colour of the axes' property cycle;
        * ``"data"`` -- one colour per distinct :attr:`Face.data` value; a face whose
          data is ``None`` is not filled;
        * a single matplotlib colour -- all faces in that colour;
        * a callable ``face -> colour | None``;
        * a mapping, looked up by :class:`~arrangement_2d.Face`, then by ``face.id``,
          then by ``face.data``; a missing key means "do not fill";
        * a sequence, indexed by the position of the face in *faces*.

    :param face_alpha: alpha of the face fills.
    :param edge_color: matplotlib colour of the edges.
    :param linewidth: line width of the edges.
    :param vertex_color: matplotlib colour of the vertex markers.
    :param vertex_size: marker area (the ``s`` of ``Axes.scatter``).
    :param projection: a callable mapping the arrangement's coordinates to ``(x, y)``.
        The ``sphere`` kind has three coordinates and uses :func:`lonlat` by default;
        the planar kinds use no projection by default.
    :param aspect: passed to ``Axes.set_aspect``; ``"equal"`` by default so that circles
        look like circles.  ``None`` leaves the axes alone.
    :param autoscale: extend the axes' data limits to the drawn geometry.
    :returns: the axes that was drawn on.
    :rtype: matplotlib.axes.Axes
    :raises ImportError: matplotlib is not installed.

    .. note::
       Faces are **not** filled for the ``sphere`` kind: a spherical face has no planar
       outline, and its projection would be cut by the antimeridian.  Its edges and
       vertices are drawn in the projected coordinates.

    ::

        >>> import matplotlib; matplotlib.use("Agg")
        >>> import arrangement_2d as a2
        >>> arr = a2.Arrangement("segment")
        >>> _ = arr.insert([a2.Segment((0, 0), (1, 0)), a2.Segment((1, 0), (0, 1)),
        ...                 a2.Segment((0, 1), (0, 0))])
        >>> ax = a2.plot.plot_arrangement(arr, face_colors="index")
        >>> len(ax.collections) >= 1
        True
    """
    if not isinstance(arr, Arrangement):
        raise TypeError("expected an Arrangement, got %s" % (type(arr).__name__,))
    plt, mcollections, mpatches, mpath = _mpl()
    ax = _axes(ax, aspect)

    spherical = arr.kind == Kind.SPHERE
    dim = 3 if spherical else 2
    if projection is None and spherical:
        projection = lonlat
    drawn_points: list = []

    # ---- faces ------------------------------------------------------------
    if not spherical:
        face_list = list(arr.bounded_faces()) if faces is None else list(faces)
        colors = _resolve_face_colors(face_list, face_colors, ax)
        patches: list = []
        patch_colors: list = []
        for face, color in zip(face_list, colors):
            if color is None:
                continue
            outer, holes = face.boundary_points(tolerance)
            if not outer:
                continue
            path = _ring_path(outer, holes, mpath)
            if path is None:
                continue
            patches.append(mpatches.PathPatch(path))
            patch_colors.append(color)
            drawn_points.extend(_closed_ring(outer))
        if patches:
            collection = mcollections.PatchCollection(
                patches, facecolors=patch_colors, edgecolors="none",
                alpha=face_alpha, zorder=1,
            )
            ax.add_collection(collection)

    # ---- edges ------------------------------------------------------------
    if show_edges:
        segments: list = []
        for chain in arr.approximate_edges(tolerance, bbox):
            for piece in _chain_to_xy(chain, dim, projection):
                if len(piece) >= 2:
                    segments.append(piece)
                    drawn_points.extend(piece)
        if segments:
            ax.add_collection(mcollections.LineCollection(
                segments, colors=edge_color, linewidths=linewidth, zorder=2,
            ))

    # ---- vertices ---------------------------------------------------------
    if show_vertices:
        coords = arr.vertex_coordinates()
        pts: list = []
        for c in coords:
            if dim == 3:
                pts.append(projection(float(c[0]), float(c[1]), float(c[2])))
            elif projection is not None:
                pts.append(projection(float(c[0]), float(c[1])))
            else:
                pts.append((float(c[0]), float(c[1])))
        if pts:
            ax.scatter([p[0] for p in pts], [p[1] for p in pts],
                       s=vertex_size, c=vertex_color, zorder=3)
            drawn_points.extend(pts)

    # ---- limits -----------------------------------------------------------
    if bbox is not None and len(bbox) == 4 and not spherical:
        ax.update_datalim([(bbox[0], bbox[1]), (bbox[2], bbox[3])])
    if autoscale:
        box = _bounds(drawn_points)
        if box is not None:
            box = _pad(box)
            ax.update_datalim([(box[0], box[1]), (box[2], box[3])])
        ax.autoscale_view()
    return ax


def plot_polygon_set(
    polygons: Any,
    ax=None,
    *,
    tolerance: float = 1e-3,
    bbox: Optional[Sequence] = None,
    face_color: Any = "C0",
    face_alpha: float = 0.5,
    edge_color: Any = "C0",
    linewidth: float = 1.5,
    aspect: Optional[str] = "equal",
    autoscale: bool = True,
):
    """Draw a :class:`~arrangement_2d.PolygonSet` (or a single polygon).

    :param polygons: a :class:`~arrangement_2d.PolygonSet`, a
        :class:`~arrangement_2d.PolygonWithHoles`, a :class:`~arrangement_2d.Polygon`, or
        an iterable of those.
    :param ax: the axes to draw on; ``None`` uses ``matplotlib.pyplot.gca()``.
    :param tolerance: maximum deviation of the curve approximation, in coordinate units.
    :param bbox: ``(xmin, ymin, xmax, ymax)``.  An *unbounded* polygon with holes (the
        result of :meth:`PolygonSet.complement`, for instance) has no outer boundary; it
        is drawn as this rectangle with its holes punched out.  Without *bbox* the
        bounding box of the holes, padded by 25 %, is used.
    :param face_color: matplotlib colour of the fill; ``None`` draws only the boundary.
    :param face_alpha: alpha of the fill.
    :param edge_color: matplotlib colour of the boundary; ``None`` draws no boundary.
    :param linewidth: line width of the boundary.
    :param aspect: passed to ``Axes.set_aspect`` (``"equal"`` by default).
    :param autoscale: extend the axes' data limits to the drawn geometry.
    :returns: the axes that was drawn on.
    :rtype: matplotlib.axes.Axes
    :raises ImportError: matplotlib is not installed.

    ::

        >>> import matplotlib; matplotlib.use("Agg")
        >>> import arrangement_2d as a2
        >>> square = a2.Polygon([(0, 0), (2, 0), (2, 2), (0, 2)])
        >>> ps = a2.PolygonSet("segment")
        >>> ps.insert(square)
        >>> ax = a2.plot.plot_polygon_set(ps)
        >>> len(ax.collections) >= 1
        True
    """
    plt, mcollections, mpatches, mpath = _mpl()
    ax = _axes(ax, aspect)

    rings = _polygon_rings(polygons, tolerance)
    drawn_points: list = []
    for outer, holes in rings:
        for ring in ([outer] if outer else []) + list(holes):
            drawn_points.extend(_closed_ring(ring))

    if bbox is None:
        box = _bounds(drawn_points)
        clip = _pad(box, 0.25) if box is not None else (-1.0, -1.0, 1.0, 1.0)
    else:
        clip = tuple(float(v) for v in bbox)
    rect = [(clip[0], clip[1]), (clip[2], clip[1]), (clip[2], clip[3]), (clip[0], clip[3])]

    patches: list = []
    boundaries: list = []
    for outer, holes in rings:
        ring = _closed_ring(outer) if outer else rect
        path = _ring_path(ring, holes, mpath)
        if path is not None:
            patches.append(mpatches.PathPatch(path))
        for r in ([outer] if outer else []) + list(holes):
            closed = _closed_ring(r)
            if len(closed) >= 2:
                boundaries.append(closed + [closed[0]])

    if patches and face_color is not None:
        ax.add_collection(mcollections.PatchCollection(
            patches, facecolors=face_color, edgecolors="none",
            alpha=face_alpha, zorder=1,
        ))
    if boundaries and edge_color is not None:
        ax.add_collection(mcollections.LineCollection(
            boundaries, colors=edge_color, linewidths=linewidth, zorder=2,
        ))
    if autoscale:
        box = _bounds(drawn_points if drawn_points else rect)
        if box is not None:
            box = _pad(box)
            ax.update_datalim([(box[0], box[1]), (box[2], box[3])])
        ax.autoscale_view()
    return ax


def _polygon_rings(polygons: Any, tolerance: float) -> list:
    """*polygons* -> a list of ``(outer_points | None, [hole_points, ...])``."""
    out: list = []
    if isinstance(polygons, PolygonSet):
        items: Iterable = polygons.polygons_with_holes()
    elif isinstance(polygons, (Polygon, PolygonWithHoles)):
        items = [polygons]
    else:
        try:
            items = list(polygons)
        except TypeError:
            raise TypeError(
                "expected a PolygonSet, a PolygonWithHoles, a Polygon or an iterable of "
                "those, got %s" % (type(polygons).__name__,)
            )
    for item in items:
        if isinstance(item, PolygonSet):
            out.extend(_polygon_rings(item, tolerance))
        elif isinstance(item, PolygonWithHoles):
            outer, holes = item.approximate(tolerance)
            out.append((outer, holes))
        elif isinstance(item, Polygon):
            out.append((item.approximate(tolerance), []))
        else:
            out.append((Polygon(item).approximate(tolerance), []))
    return out


def plot_curves(
    curves: Iterable[Curve],
    ax=None,
    *,
    tolerance: float = 1e-3,
    bbox: Optional[Sequence] = None,
    color: Any = "C0",
    linewidth: float = 1.5,
    projection: Optional[Callable] = None,
    aspect: Optional[str] = "equal",
    autoscale: bool = True,
):
    """Draw a bare list of curves (no arrangement needed).

    :param curves: an iterable of :class:`~arrangement_2d.Curve` objects; they may be of
        different kinds.
    :param ax: the axes to draw on; ``None`` uses ``matplotlib.pyplot.gca()``.
    :param tolerance: maximum deviation of the curve approximation, in coordinate units.
    :param bbox: ``(xmin, ymin, xmax, ymax)``; required for unbounded curves (a
        ``linear`` line or ray), which have to be clipped before they can be drawn.
    :param color: matplotlib colour.
    :param linewidth: line width.
    :param projection: a callable mapping the coordinates to ``(x, y)``; three-dimensional
        (``sphere``) curves use :func:`lonlat` by default.
    :param aspect: passed to ``Axes.set_aspect`` (``"equal"`` by default).
    :param autoscale: extend the axes' data limits to the drawn geometry.
    :returns: the axes that was drawn on.
    :rtype: matplotlib.axes.Axes
    :raises ImportError: matplotlib is not installed.
    :raises ValueError: an unbounded curve was given without a *bbox*.

    ::

        >>> import matplotlib; matplotlib.use("Agg")
        >>> import arrangement_2d as a2
        >>> ax = a2.plot.plot_curves([a2.Segment((0, 0), (1, 1))])
        >>> len(ax.collections)
        1
    """
    plt, mcollections, mpatches, mpath = _mpl()
    ax = _axes(ax, aspect)
    segments: list = []
    drawn_points: list = []
    for curve in curves:
        if not isinstance(curve, Curve):
            raise TypeError("expected a Curve, got %s" % (type(curve).__name__,))
        dim = 3 if curve.kind == Kind.SPHERE else 2
        proj = projection
        if proj is None and dim == 3:
            proj = lonlat
        for piece in _chain_to_xy(curve.approximate(tolerance, bbox), dim, proj):
            if len(piece) >= 2:
                segments.append(piece)
                drawn_points.extend(piece)
    ax.add_collection(mcollections.LineCollection(
        segments, colors=color, linewidths=linewidth, zorder=2,
    ))
    if autoscale:
        box = _bounds(drawn_points)
        if box is not None:
            box = _pad(box)
            ax.update_datalim([(box[0], box[1]), (box[2], box[3])])
        ax.autoscale_view()
    return ax
