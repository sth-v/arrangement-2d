"""Tests for :mod:`arrangement_2d.plot` -- the optional matplotlib helpers.

The whole module is skipped when matplotlib is not installed (it is an optional
dependency); what is tested without it is only that importing
``arrangement_2d.plot`` works anyway and that the plotting functions then raise a clear
:class:`ImportError`.
"""

from __future__ import annotations

import math
import sys

import pytest

a2 = pytest.importorskip("arrangement_2d")
plot = a2.plot                                  # importable even without matplotlib


# ---------------------------------------------------------------------------
# what still works without matplotlib
# ---------------------------------------------------------------------------

def test_module_is_importable_and_lazy():
    import arrangement_2d

    assert arrangement_2d.plot is plot
    assert isinstance(plot.has_matplotlib(), bool)


def test_plot_functions_raise_importerror_without_matplotlib(monkeypatch):
    """The matplotlib import happens inside the call, and its failure is explained."""
    for name in ("matplotlib", "matplotlib.pyplot", "matplotlib.collections",
                 "matplotlib.patches", "matplotlib.path"):
        monkeypatch.setitem(sys.modules, name, None)
    arr = a2.Arrangement("segment")
    with pytest.raises(ImportError, match="matplotlib"):
        plot.plot_arrangement(arr)
    with pytest.raises(ImportError, match="matplotlib"):
        plot.plot_polygon_set(a2.PolygonSet("segment"))
    with pytest.raises(ImportError, match="matplotlib"):
        plot.plot_curves([])
    assert plot.has_matplotlib() is False


def test_lonlat_is_pure_python():
    assert plot.lonlat(1, 0, 0) == (0.0, 0.0)
    lon, lat = plot.lonlat(0, 0, 5)
    assert lat == pytest.approx(math.pi / 2)
    lon, lat = plot.lonlat(-1, 0, 0)
    assert abs(lon) == pytest.approx(math.pi)
    assert plot.lonlat(0, 0, 0) == (0.0, 0.0)


def test_public_names_are_documented():
    for name in plot.__all__:
        obj = getattr(plot, name)
        assert obj.__doc__, name


# ---------------------------------------------------------------------------
# everything below needs matplotlib
# ---------------------------------------------------------------------------

try:
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.collections import LineCollection, PatchCollection, PathCollection

    HAVE_MATPLOTLIB = True
except ImportError:                                   # pragma: no cover - environment
    plt = None
    LineCollection = PatchCollection = PathCollection = None
    HAVE_MATPLOTLIB = False

#: Everything below this point needs matplotlib; the four tests above deliberately do
#: not, so they keep running on a machine without it.
needs_matplotlib = pytest.mark.skipif(not HAVE_MATPLOTLIB,
                                      reason="matplotlib is not installed")


@pytest.fixture
def ax():
    fig, axes = plt.subplots()
    yield axes
    plt.close(fig)


def lines(axes):
    return [c for c in axes.collections if isinstance(c, LineCollection)]


def patches(axes):
    return [c for c in axes.collections if isinstance(c, PatchCollection)]


def markers(axes):
    return [c for c in axes.collections if isinstance(c, PathCollection)]


def square(x0=0, y0=0, x1=4, y1=4):
    return [
        a2.Segment((x0, y0), (x1, y0)),
        a2.Segment((x1, y0), (x1, y1)),
        a2.Segment((x1, y1), (x0, y1)),
        a2.Segment((x0, y1), (x0, y0)),
    ]


@pytest.fixture
def split_square():
    arr = a2.Arrangement("segment")
    arr.insert(square() + [a2.Segment((0, 2), (4, 2))])
    return arr


def segment_count(collection):
    return len(collection.get_segments())


def _ring_area(points):
    total = 0.0
    n = len(points)
    for i in range(n):
        x1, y1 = points[i]
        x2, y2 = points[(i + 1) % n]
        total += x1 * y2 - x2 * y1
    return 0.5 * total


def subpath_areas(path):
    """Signed area of every sub-polygon of a compound path.

    A positive area is a counterclockwise ring and a negative one a clockwise ring;
    that is exactly what makes matplotlib's non-zero winding rule punch the holes out
    of the fill.  (``Path.contains_point`` cannot be used to check this: it reports a
    point as contained when it lies in *any* subpath, holes included.)
    """
    areas = []
    current = []
    for (x, y), code in zip(path.vertices, path.codes):
        if code == path.MOVETO:
            if current:
                areas.append(_ring_area(current))
            current = [(x, y)]
        elif code == path.LINETO:
            current.append((x, y))
        elif code == path.CLOSEPOLY:
            if current:
                areas.append(_ring_area(current))
            current = []
    if current:
        areas.append(_ring_area(current))
    return areas


# ---------------------------------------------------------------------------
# plot_arrangement
# ---------------------------------------------------------------------------

@needs_matplotlib
def test_plot_arrangement_draws_edges_and_vertices(ax, split_square):
    out = plot.plot_arrangement(split_square, ax)
    assert out is ax
    assert len(lines(ax)) == 1
    assert segment_count(lines(ax)[0]) == split_square.number_of_edges
    assert len(markers(ax)) == 1
    assert len(markers(ax)[0].get_offsets()) == split_square.number_of_vertices
    assert patches(ax) == []                      # face_colors defaults to None


@needs_matplotlib
def test_plot_arrangement_uses_gca_when_no_axes_given(split_square):
    fig, axes = plt.subplots()
    try:
        plt.sca(axes)
        assert plot.plot_arrangement(split_square) is axes
    finally:
        plt.close(fig)


@needs_matplotlib
def test_plot_arrangement_toggles(ax, split_square):
    plot.plot_arrangement(split_square, ax, show_vertices=False)
    assert markers(ax) == []
    assert len(lines(ax)) == 1
    ax.clear()
    plot.plot_arrangement(split_square, ax, show_edges=False)
    assert lines(ax) == []
    assert len(markers(ax)) == 1


@needs_matplotlib
def test_plot_arrangement_sets_aspect_and_limits(ax, split_square):
    plot.plot_arrangement(split_square, ax)
    assert ax.get_aspect() == 1.0                 # "equal"
    xmin, xmax = ax.get_xlim()
    ymin, ymax = ax.get_ylim()
    assert xmin <= 0.0 and xmax >= 4.0
    assert ymin <= 0.0 and ymax >= 4.0
    ax.clear()
    plot.plot_arrangement(split_square, ax, aspect=None, autoscale=False)


@needs_matplotlib
def test_plot_arrangement_face_colors_index(ax, split_square):
    plot.plot_arrangement(split_square, ax, face_colors="index")
    assert len(patches(ax)) == 1
    assert len(patches(ax)[0].get_paths()) == 2
    assert len(patches(ax)[0].get_facecolor()) == 2
    first, second = patches(ax)[0].get_facecolor()
    assert list(first) != list(second)


@needs_matplotlib
def test_plot_arrangement_face_colors_data(ax, split_square):
    faces = split_square.bounded_faces()
    faces[0].data = "a"
    faces[1].data = "a"
    plot.plot_arrangement(split_square, ax, face_colors="data")
    colors = patches(ax)[0].get_facecolor()
    assert len(colors) == 2
    assert list(colors[0]) == list(colors[1])      # same data -> same colour
    ax.clear()
    faces[1].data = "b"
    plot.plot_arrangement(split_square, ax, face_colors="data")
    colors = patches(ax)[0].get_facecolor()
    assert list(colors[0]) != list(colors[1])


@needs_matplotlib
def test_plot_arrangement_face_colors_data_skips_none(ax, split_square):
    faces = split_square.bounded_faces()
    faces[0].data = "only me"
    plot.plot_arrangement(split_square, ax, face_colors="data")
    assert len(patches(ax)[0].get_paths()) == 1


@needs_matplotlib
def test_plot_arrangement_face_colors_unhashable_data(ax, split_square):
    for f in split_square.bounded_faces():
        f.data = ["a", "list", "is", "unhashable"]
    plot.plot_arrangement(split_square, ax, face_colors="data")
    colors = patches(ax)[0].get_facecolor()
    assert len(colors) == 2
    assert list(colors[0]) == list(colors[1])


@needs_matplotlib
def test_plot_arrangement_face_colors_callable(ax, split_square):
    seen = []

    def color_of(face):
        seen.append(face)
        return "red" if face.is_unbounded else "green"

    plot.plot_arrangement(split_square, ax, face_colors=color_of)
    assert len(seen) == 2
    assert len(patches(ax)[0].get_paths()) == 2


@needs_matplotlib
def test_plot_arrangement_face_colors_mapping(ax, split_square):
    faces = split_square.bounded_faces()
    plot.plot_arrangement(split_square, ax, face_colors={faces[0].id: "red"})
    assert len(patches(ax)[0].get_paths()) == 1
    ax.clear()
    plot.plot_arrangement(split_square, ax, face_colors={faces[0]: "red", faces[1]: "b"})
    assert len(patches(ax)[0].get_paths()) == 2
    ax.clear()
    faces[0].data = "region"
    plot.plot_arrangement(split_square, ax, face_colors={"region": "red"})
    assert len(patches(ax)[0].get_paths()) == 1


@needs_matplotlib
def test_plot_arrangement_face_colors_sequence_and_single(ax, split_square):
    plot.plot_arrangement(split_square, ax, face_colors=["red", "blue"])
    assert len(patches(ax)[0].get_paths()) == 2
    ax.clear()
    plot.plot_arrangement(split_square, ax, face_colors=["red"])      # short sequence
    assert len(patches(ax)[0].get_paths()) == 1
    ax.clear()
    plot.plot_arrangement(split_square, ax, face_colors="red")
    assert len(patches(ax)[0].get_paths()) == 2
    ax.clear()
    plot.plot_arrangement(split_square, ax, face_colors=(0.2, 0.4, 0.6))
    assert len(patches(ax)[0].get_paths()) == 2


@needs_matplotlib
def test_plot_arrangement_face_colors_bad_type(ax, split_square):
    with pytest.raises(TypeError):
        plot.plot_arrangement(split_square, ax, face_colors=object())


@needs_matplotlib
def test_plot_arrangement_explicit_faces(ax, split_square):
    faces = split_square.bounded_faces()
    plot.plot_arrangement(split_square, ax, faces=faces[:1], face_colors="index")
    assert len(patches(ax)[0].get_paths()) == 1


@needs_matplotlib
def test_plot_arrangement_face_with_hole_is_one_compound_path(ax):
    arr = a2.Arrangement("segment")
    arr.insert(square() + square(1, 1, 3, 3))
    annulus = [f for f in arr.bounded_faces() if f.number_of_inner_ccbs == 1]
    plot.plot_arrangement(arr, ax, faces=annulus, face_colors="index")
    path = patches(ax)[0].get_paths()[0]
    # outer ring (4 corners) + hole ring (4 corners), each closed
    assert len(path.vertices) == 10
    assert list(path.codes).count(path.MOVETO) == 2
    assert list(path.codes).count(path.CLOSEPOLY) == 2


@needs_matplotlib
def test_plot_arrangement_rejects_non_arrangement(ax):
    with pytest.raises(TypeError):
        plot.plot_arrangement("not an arrangement", ax)


@needs_matplotlib
def test_plot_arrangement_empty(ax):
    arr = a2.Arrangement("segment")
    plot.plot_arrangement(arr, ax, face_colors="index")
    assert lines(ax) == []
    assert markers(ax) == []


# ---------------------------------------------------------------------------
# curved / unbounded / spherical kinds
# ---------------------------------------------------------------------------

@needs_matplotlib
def test_plot_arrangement_tolerance_refines_curves(ax):
    arr = a2.Arrangement("circle_segment")
    arr.insert(a2.Circle((0, 0), 2))
    plot.plot_arrangement(arr, ax, tolerance=0.5)
    coarse = sum(len(s) for s in lines(ax)[0].get_segments())
    ax.clear()
    plot.plot_arrangement(arr, ax, tolerance=1e-4)
    fine = sum(len(s) for s in lines(ax)[0].get_segments())
    assert fine > coarse
    for segment in lines(ax)[0].get_segments():
        for x, y in segment:
            assert abs(math.hypot(x, y) - 2.0) < 1e-3


@needs_matplotlib
def test_plot_arrangement_annulus_hole_is_punched(ax):
    arr = a2.Arrangement("circle_segment")
    arr.insert([a2.Circle((0, 0), 4), a2.Circle((0, 0), 2)])
    annulus = [f for f in arr.bounded_faces() if f.number_of_inner_ccbs == 1]
    assert len(annulus) == 1
    plot.plot_arrangement(arr, ax, tolerance=1e-2, faces=annulus, face_colors="index")
    path = patches(ax)[0].get_paths()[0]
    assert list(path.codes).count(path.MOVETO) == 2
    outer, hole = subpath_areas(path)
    assert outer == pytest.approx(math.pi * 16.0, abs=0.1)     # counterclockwise
    assert hole == pytest.approx(-math.pi * 4.0, abs=0.1)      # clockwise: a hole


@needs_matplotlib
def test_plot_arrangement_linear_kind_clips(ax):
    arr = a2.Arrangement("linear")
    arr.insert([a2.Line((0, 0), (1, 0)), a2.Line((0, 0), (0, 1))])
    plot.plot_arrangement(arr, ax)                 # no bbox: the padded one is used
    assert len(lines(ax)) == 1
    assert segment_count(lines(ax)[0]) == arr.number_of_edges
    ax.clear()
    plot.plot_arrangement(arr, ax, bbox=(-5, -5, 5, 5))
    for segment in lines(ax)[0].get_segments():
        for x, y in segment:
            assert -5.0 <= x <= 5.0 and -5.0 <= y <= 5.0
    assert ax.get_xlim()[0] <= -5.0 and ax.get_xlim()[1] >= 5.0


@needs_matplotlib
def test_plot_arrangement_sphere_projects_and_does_not_fill(ax):
    arr = a2.Arrangement("sphere")
    arr.insert([a2.GeodesicArc.from_points((3, 1, 1), (1, 3, 1)),
                a2.GeodesicArc.from_points((1, 3, 1), (1, 1, 3)),
                a2.GeodesicArc.from_points((1, 1, 3), (3, 1, 1))])
    plot.plot_arrangement(arr, ax, tolerance=1e-2, face_colors="index")
    assert patches(ax) == []                       # spherical faces are never filled
    assert len(lines(ax)) == 1
    for segment in lines(ax)[0].get_segments():
        for lon, lat in segment:
            assert -math.pi <= lon <= math.pi
            assert -math.pi / 2 <= lat <= math.pi / 2
    assert len(markers(ax)[0].get_offsets()) == arr.number_of_vertices


@needs_matplotlib
def test_plot_arrangement_custom_projection(ax, split_square):
    plot.plot_arrangement(split_square, ax, projection=lambda x, y: (y, x))
    for segment in lines(ax)[0].get_segments():
        for x, y in segment:
            assert 0.0 <= x <= 4.0 and 0.0 <= y <= 4.0
    offsets = markers(ax)[0].get_offsets()
    assert len(offsets) == split_square.number_of_vertices


@needs_matplotlib
@pytest.mark.parametrize("kind", ("segment", "linear", "circle_segment", "polyline",
                                  "bezier", "conic", "sphere"))
@needs_matplotlib
def test_plot_arrangement_every_kind(ax, kind):
    arr = a2.Arrangement(kind)
    if kind == "segment":
        arr.insert(square())
    elif kind == "linear":
        arr.insert([a2.Line((0, 0), (1, 0)), a2.Line((0, 0), (0, 1))])
    elif kind == "circle_segment":
        arr.insert([a2.CircleSegment.circle((0, 0), 2)])
    elif kind == "polyline":
        arr.insert([a2.Polyline([(0, 0), (2, 2), (4, 0), (0, 0)])])
    elif kind == "bezier":
        arr.insert([a2.BezierCurve([(0, 0), (1, 3), (2, 0)]),
                    a2.BezierCurve([(0, 0), (2, 0)])])
    elif kind == "conic":
        arr.insert([a2.ConicArc.circle((0, 0), 2)])
    else:
        arr.insert([a2.GeodesicArc.from_points((3, 1, 1), (1, 3, 1)),
                    a2.GeodesicArc.from_points((1, 3, 1), (1, 1, 3)),
                    a2.GeodesicArc.from_points((1, 1, 3), (3, 1, 1))])
    plot.plot_arrangement(arr, ax, tolerance=1e-2, face_colors="index")
    assert segment_count(lines(ax)[0]) >= arr.number_of_edges


# ---------------------------------------------------------------------------
# plot_polygon_set
# ---------------------------------------------------------------------------

@needs_matplotlib
def test_plot_polygon_set_basic(ax):
    ps = a2.PolygonSet("segment")
    ps.insert(a2.Polygon([(0, 0), (4, 0), (4, 4), (0, 4)]))
    assert plot.plot_polygon_set(ps, ax) is ax
    assert len(patches(ax)) == 1
    assert len(patches(ax)[0].get_paths()) == 1
    assert len(lines(ax)) == 1


@needs_matplotlib
def test_plot_polygon_set_with_hole(ax):
    ps = a2.PolygonSet("segment")
    ps.insert(a2.Polygon([(0, 0), (4, 0), (4, 4), (0, 4)]))
    ps.difference(a2.Polygon([(1, 1), (3, 1), (3, 3), (1, 3)]))
    plot.plot_polygon_set(ps, ax)
    path = patches(ax)[0].get_paths()[0]
    assert list(path.codes).count(path.MOVETO) == 2
    assert subpath_areas(path) == [16.0, -4.0]         # 4x4 counterclockwise, 2x2 hole
    assert len(lines(ax)[0].get_segments()) == 2       # outer ring + hole ring


@needs_matplotlib
def test_plot_polygon_set_unbounded_complement(ax):
    ps = a2.PolygonSet("segment")
    ps.insert(a2.Polygon([(0, 0), (4, 0), (4, 4), (0, 4)]))
    complement = ~ps
    pwh = complement.polygons_with_holes()[0]
    assert pwh.is_unbounded
    plot.plot_polygon_set(complement, ax, bbox=(-10, -10, 10, 10))
    path = patches(ax)[0].get_paths()[0]
    # the clip rectangle is filled and the square is punched out of it
    assert subpath_areas(path) == [400.0, -16.0]
    ax.clear()
    plot.plot_polygon_set(complement, ax)              # no bbox: derived from the holes
    assert len(patches(ax)) == 1


@needs_matplotlib
def test_plot_polygon_set_accepts_polygons_and_iterables(ax):
    polygon = a2.Polygon([(0, 0), (1, 0), (1, 1)])
    plot.plot_polygon_set(polygon, ax)
    assert len(patches(ax)[0].get_paths()) == 1
    ax.clear()
    plot.plot_polygon_set(a2.PolygonWithHoles(polygon), ax)
    assert len(patches(ax)[0].get_paths()) == 1
    ax.clear()
    plot.plot_polygon_set([polygon, a2.Polygon([(5, 5), (6, 5), (6, 6)])], ax)
    assert len(patches(ax)[0].get_paths()) == 2
    ax.clear()
    plot.plot_polygon_set([[(0, 0), (1, 0), (1, 1)]], ax)
    assert len(patches(ax)[0].get_paths()) == 1


@needs_matplotlib
def test_plot_polygon_set_curved(ax):
    ps = a2.PolygonSet("circle_segment")
    ps.insert(a2.PolygonWithHoles(a2.Polygon([a2.Circle((0, 0), 2)])))
    plot.plot_polygon_set(ps, ax, tolerance=1e-3)
    for segment in lines(ax)[0].get_segments():
        for x, y in segment:
            assert abs(math.hypot(x, y) - 2.0) < 1e-2


@needs_matplotlib
def test_plot_polygon_set_colour_switches(ax):
    ps = a2.PolygonSet("segment")
    ps.insert(a2.Polygon([(0, 0), (4, 0), (4, 4), (0, 4)]))
    plot.plot_polygon_set(ps, ax, face_color=None)
    assert patches(ax) == []
    assert len(lines(ax)) == 1
    ax.clear()
    plot.plot_polygon_set(ps, ax, edge_color=None)
    assert len(patches(ax)) == 1
    assert lines(ax) == []


@needs_matplotlib
def test_plot_polygon_set_empty(ax):
    plot.plot_polygon_set(a2.PolygonSet("segment"), ax)
    assert patches(ax) == []
    assert lines(ax) == []


@needs_matplotlib
def test_plot_polygon_set_rejects_junk(ax):
    with pytest.raises(TypeError):
        plot.plot_polygon_set(42, ax)


# ---------------------------------------------------------------------------
# plot_curves
# ---------------------------------------------------------------------------

@needs_matplotlib
def test_plot_curves(ax):
    assert plot.plot_curves([a2.Segment((0, 0), (1, 1))], ax) is ax
    assert len(lines(ax)) == 1
    assert segment_count(lines(ax)[0]) == 1


@needs_matplotlib
def test_plot_curves_mixed_kinds(ax):
    plot.plot_curves([a2.Segment((0, 0), (1, 1)), a2.ConicArc.circle((0, 0), 1)],
                     ax, tolerance=1e-2)
    assert segment_count(lines(ax)[0]) == 2


@needs_matplotlib
def test_plot_curves_unbounded_needs_a_bbox(ax):
    with pytest.raises(ValueError):
        plot.plot_curves([a2.Line((0, 0), (1, 1))], ax)
    plot.plot_curves([a2.Line((0, 0), (1, 1))], ax, bbox=(-2, -2, 2, 2))
    for segment in lines(ax)[-1].get_segments():
        for x, y in segment:
            assert -2.0 <= x <= 2.0 and -2.0 <= y <= 2.0


@needs_matplotlib
def test_plot_curves_sphere_is_projected(ax):
    arc = a2.GeodesicArc.from_points((1, 0, 0), (0, 1, 0))
    plot.plot_curves([arc], ax, tolerance=1e-2)
    for segment in lines(ax)[0].get_segments():
        for lon, lat in segment:
            assert -math.pi <= lon <= math.pi


@needs_matplotlib
def test_plot_curves_rejects_non_curve(ax):
    with pytest.raises(TypeError):
        plot.plot_curves([(0, 0)], ax)


@needs_matplotlib
def test_plot_curves_empty(ax):
    plot.plot_curves([], ax)
    assert segment_count(lines(ax)[0]) == 0
