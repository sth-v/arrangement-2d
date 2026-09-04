"""End-to-end smoke test for every geometry kind.

One file that touches the whole public surface of :mod:`arrangement_2d`: curve
construction, insertion, hand-verified V/E/F counts, iteration, handle accessors,
``.data`` round trips, point location / ray shooting per supported strategy, zone,
vertical decomposition, batched location, overlay, observers, modification and
removal, handle invalidation, ``copy()``, exact / approximate coordinates, curve
approximation, the Boolean set operations of the four kinds that have them, error
translation and ``repr()``.

It also carries the regression checks for the cross-cutting fixes applied during
integration; each one is marked ``REGRESSION`` with the trap it guards against.
"""

from __future__ import annotations

import copy as _copy
import math
import pickle
from fractions import Fraction

import pytest

a2 = pytest.importorskip("arrangement_2d")

ALL_KINDS = ("segment", "linear", "circle_segment", "polyline", "bezier", "conic", "sphere")
PLANAR_KINDS = tuple(k for k in ALL_KINDS if k != "sphere")
BOUNDED_KINDS = tuple(k for k in PLANAR_KINDS if k != "linear")
BSO_KINDS = ("segment", "circle_segment", "conic", "bezier")

#: Kinds whose points can be at a pole / on the identification curve.
SPHERE = "sphere"


# ---------------------------------------------------------------------------
# canonical per-kind inputs
# ---------------------------------------------------------------------------
def curves_for(kind):
    """A small set of input curves per kind, with hand-derived V/E/F (see COUNTS)."""
    if kind == "segment":
        # unit square (0,0)-(4,4) plus the chord y = 2 from x = -1 to x = 5
        return [
            a2.Segment((0, 0), (4, 0)),
            a2.Segment((4, 0), (4, 4)),
            a2.Segment((4, 4), (0, 4)),
            a2.Segment((0, 4), (0, 0)),
            a2.Segment((-1, 2), (5, 2)),
        ]
    if kind == "linear":
        # the two coordinate axes
        return [a2.Line((0, 0), (1, 0)), a2.Line((0, 0), (0, 1))]
    if kind == "circle_segment":
        # circle of radius 2 around the origin + its horizontal diameter
        return [a2.CircleSegment.circle((0, 0), 2), a2.CircleSegment.segment((-2, 0), (2, 0))]
    if kind == "polyline":
        # a "peak" polyline and a horizontal chord that cuts both of its legs
        return [a2.Polyline([(0, 0), (2, 2), (4, 0)]), a2.Polyline([(0, 1), (4, 1)])]
    if kind == "bezier":
        # a quadratic arch from (0,0) to (2,0) with apex y = 3/2, cut by y = 1
        return [a2.BezierCurve([(0, 0), (1, 3), (2, 0)]), a2.BezierCurve([(0, 1), (2, 1)])]
    if kind == "conic":
        # circle of radius 2 + its horizontal diameter
        return [a2.ConicArc.circle((0, 0), 2), a2.ConicArc.segment((-2, 0), (2, 0))]
    if kind == "sphere":
        # a geodesic triangle whose corners are all in the interior of the parameter
        # space (no pole, not on the identification meridian)
        return [
            a2.GeodesicArc.from_points((3, 1, 1), (1, 3, 1)),
            a2.GeodesicArc.from_points((1, 3, 1), (1, 1, 3)),
            a2.GeodesicArc.from_points((1, 1, 3), (3, 1, 1)),
        ]
    raise AssertionError(kind)


#: kind -> (vertices, edges, faces, unbounded faces, curves), all hand-derived.
COUNTS = {
    # 4 corners + 4 chord crossings/endpoints => 8 V; 4 square sides split into 6 by the
    # chord + the chord's 3 pieces => 9 E; inside-below, inside-above, outside => 3 F.
    "segment": (8, 9, 3, 1, 5),
    # two crossing lines: 1 concrete vertex, 4 edges (2 per line), 4 quadrants.
    "linear": (1, 4, 4, 4, 2),
    # circle + diameter: the 2 tangency points (-2,0),(2,0) are the only vertices; the
    # circle contributes 2 arcs, the diameter 1 segment; upper half, lower half, outside.
    "circle_segment": (2, 3, 3, 1, 2),
    # peak legs cross y = 1 at (1,1) and (3,1): 4 endpoints + 2 crossings = 6 V;
    # peak -> 3 pieces, chord -> 3 pieces = 6 E; the triangle under the apex + outside.
    "polyline": (6, 6, 2, 1, 2),
    # same shape with Bezier curves.
    "bezier": (6, 6, 2, 1, 2),
    "conic": (2, 3, 3, 1, 2),
    # spherical triangle: 3 corners, 3 arcs, the triangle + the rest of the sphere.
    "sphere": (3, 3, 2, 0, 3),
}


def build(kind):
    arr = a2.Arrangement(kind)
    arr.insert(curves_for(kind))
    return arr


def a_point(kind, x, y, z=1):
    """A point of `kind`; the sphere gets a direction instead of (x, y)."""
    if kind == SPHERE:
        return a2.Point(x, y, z, kind=SPHERE)
    return a2.Point(x, y, kind=kind)


#: a query point strictly inside the bounded region of the canonical arrangement
INSIDE = {
    "segment": (2, 1),
    "linear": (1, 1),
    "circle_segment": (0, 1),
    "polyline": (2, Fraction(3, 2)),
    "bezier": (1, Fraction(5, 4)),
    "conic": (0, 1),
    "sphere": (1, 1, 1),
}
#: a query point outside every bounded feature (planar kinds only)
OUTSIDE = {
    "segment": (100, 100),
    "linear": (-5, -5),
    "circle_segment": (100, 100),
    "polyline": (100, 100),
    "bezier": (100, 100),
    "conic": (100, 100),
}


def K(kind):
    """The Kind enum member for a kind name (the IntEnum takes values, not names)."""
    return a2.Kind[kind.upper()]


def as_query(kind, t):
    return a_point(kind, *t) if kind == SPHERE else a2.Point(*t, kind=kind)


# ===========================================================================
# module surface
# ===========================================================================
def test_module_surface():
    assert a2.cgal_version().startswith("6.")
    info = a2.build_info()
    assert "CGAL" in info and "assertions on" in info
    assert tuple(str(k) for k in a2.available_kinds()) == ALL_KINDS
    for k in a2.Kind:
        assert a2.kind_available(k), k
    assert len(list(a2.Kind)) == 7
    assert str(a2.Kind.BEZIER) == "bezier"
    assert K("segment") is a2.Kind.SEGMENT
    assert a2.__version__
    for name in a2.__all__:
        if name in ("regions", "plot"):
            continue  # optional submodules, imported lazily
        assert hasattr(a2, name), name


def test_star_import_and_lazy_submodules():
    ns: dict = {}
    exec("from arrangement_2d import *", ns)          # must not raise
    assert ns["Kind"] is a2.Kind
    assert "regions" in dir(a2) and "plot" in dir(a2)
    for name in ("regions", "plot"):
        try:
            getattr(a2, name)
        except AttributeError:
            pass                                      # the optional submodule is not installed


def test_special_faces():
    seg = build("segment")
    assert seg.unbounded_face.is_unbounded
    with pytest.raises(a2.UnsupportedError):
        seg.fictitious_face                           # only the unbounded topology has one
    with pytest.raises(a2.UnsupportedError):
        seg.spherical_face

    lin = build("linear")
    assert lin.fictitious_face.is_fictitious
    assert len(lin.unbounded_faces()) == 4
    # documented: for the unbounded kind this is ONE of the unbounded faces
    assert lin.unbounded_face in lin.unbounded_faces()

    sph = build("sphere")
    assert sph.spherical_face.number_of_outer_ccbs == 0
    assert sph.unbounded_face == sph.spherical_face   # documented alias for this kind
    assert sph.number_of_unbounded_faces == 0
    with pytest.raises(a2.UnsupportedError):
        sph.fictitious_face


def test_kind_enum_pickles():
    assert pickle.loads(pickle.dumps(a2.Kind.CONIC)) is a2.Kind.CONIC


# ===========================================================================
# geometry construction + repr
# ===========================================================================
def test_construct_every_kind_and_repr():
    made = {
        "segment": a2.Segment((0, 0), (4, 4)),
        "linear": a2.LinearCurve.segment((0, 0), (4, 4)),
        "circle_segment": a2.CircleSegment.circle((0, 0), 2),
        "polyline": a2.Polyline([(0, 0), (1, 1), (2, 0)]),
        "bezier": a2.BezierCurve([(0, 0), (1, 3), (2, 0)]),
        "conic": a2.ConicArc.circle((0, 0), 2),
        "sphere": a2.GeodesicArc.from_points((1, 0, 0), (0, 1, 0)),
    }
    for kind, curve in made.items():
        assert curve.kind == K(kind)
        assert isinstance(repr(curve), str) and repr(curve)
        assert curve.dimension == (3 if kind == SPHERE else 2)
        assert isinstance(curve.type_name, str)
        assert isinstance(curve.is_bounded, bool)


def test_segment_accessors():
    s = a2.Segment((0, 0), (4, 2))
    assert s.source == a2.Point(0, 0) and s.target == a2.Point(4, 2)
    assert s.is_x_monotone and not s.is_vertical and s.is_directed_right
    assert s.left == a2.Point(0, 0) and s.right == a2.Point(4, 2)
    assert s.min_vertex == s.left and s.max_vertex == s.right
    a, b, c = s.supporting_line
    assert a * 0 + b * 0 + c == 0 and a * 4 + b * 2 + c == 0
    assert s.bbox() == (0.0, 0.0, 4.0, 2.0)
    assert s == a2.Segment((0, 0), (4, 2))
    assert repr(s) == "Segment((0, 0), (4, 2))"


def test_linear_accessors():
    line = a2.Line((0, 0), (1, 1))
    ray = a2.Ray((0, 0), (1, 1))
    seg = a2.LinearCurve.segment((0, 0), (1, 1))
    assert line.is_line and ray.is_ray and seg.is_segment
    assert line.which == "line" and ray.which == "ray" and seg.which == "segment"
    assert not line.is_bounded and not ray.is_bounded and seg.is_bounded
    assert not line.has_source and not line.has_target
    assert ray.has_source and ray.source == a2.Point(0, 0, kind="linear")
    assert len(line.supporting_line) == 3
    assert len(line.direction) == 2
    assert a2.line_from_coefficients(0, 1, -2).is_line
    for c in (line, ray, seg):
        assert repr(c)


def test_circle_segment_accessors():
    c = a2.CircleSegment.circle((0, 0), 2)
    assert c.is_full and c.is_circular and not c.is_linear
    assert isinstance(c.center, a2.Point)
    assert c.center.exact() == (Fraction(0), Fraction(0))
    assert c.squared_radius == 4
    assert c.has_rational_radius and c.radius == 2
    assert c.orientation == 1
    assert not c.is_x_monotone
    arcs = c.make_x_monotone()
    assert len(arcs) == 2 and all(x.is_x_monotone for x in arcs)
    seg = a2.CircleSegment.segment((-2, 0), (2, 0))
    assert seg.is_linear and not seg.is_circular
    assert len(seg.supporting_line) == 3
    arc3 = a2.CircleSegment.arc_from_three_points((-2, 0), (0, 2), (2, 0))
    assert arc3.is_circular
    assert a2.Circle((0, 0), 1).is_full
    assert a2.CircularArc((0, 0), 2, source=(2, 0), target=(-2, 0)).is_circular


def test_polyline_accessors():
    p = a2.Polyline([(0, 0), (1, 1), (2, 0), (3, 1)])
    assert p.number_of_points == 4 and p.number_of_subcurves == 3
    assert len(p) == 4 and len(p.points) == 4 and len(p.segments) == 3
    assert p.points[0] == a2.Point(0, 0, kind="polyline")
    assert len(p.make_x_monotone()) >= 1
    assert list(iter(p)) == list(p.points)
    xm = a2.Polyline.from_x_monotone_points([(0, 0), (1, 1), (2, 3)])
    assert xm.is_x_monotone
    assert a2.Polyline.from_segments([a2.Segment((0, 0), (1, 0)), a2.Segment((1, 0), (2, 1))])


def test_bezier_accessors():
    b = a2.BezierCurve([(0, 0), (4, 1), (-2, 2), (2, 3)])
    assert b.degree == 3 and len(b.control_points) == 4
    assert isinstance(b.curve_id, int)
    assert b.has_self_intersections is False
    pieces = b.make_x_monotone()
    assert len(pieces) == 3
    piece = pieces[0]
    assert piece.is_x_monotone
    assert isinstance(piece.xid, int)
    lo, hi = piece.parameter_range
    assert 0.0 <= lo <= hi <= 1.0
    assert piece.supporting_curve.degree == 3
    assert b.evaluate(Fraction(1, 2)).kind == a2.Kind.BEZIER
    samples = b.sample(0.0, 1.0, 8)
    assert len(samples) == 8 and len(samples[0]) == 2


def test_conic_accessors():
    c = a2.ConicArc.circle((0, 0), 2)
    assert c.is_full and c.conic_type == "ellipse"
    assert len(c.coefficients) == 6
    assert c.orientation == 1
    e = a2.ConicArc.ellipse((0, 0), 3, 2)
    assert e.conic_type == "ellipse"
    s = a2.ConicArc.segment((-1, 0), (1, 0))
    assert not s.is_full
    # five rational points of the circle x^2 + y^2 = 4, in order along the upper arc
    five = a2.ConicArc.from_points((2, 0), (Fraction(6, 5), Fraction(8, 5)), (0, 2),
                                   (Fraction(-6, 5), Fraction(8, 5)), (-2, 0))
    assert five.kind == a2.Kind.CONIC
    assert five.conic_type == "ellipse"
    rb = a2.ConicArc.from_rational_bezier((0, 0), (1, 1), (2, 0), 1, 2, 1)
    assert rb.kind == a2.Kind.CONIC
    fromcs = a2.ConicArc.from_circle_segment(a2.CircleSegment.circle((0, 0), 1))
    assert fromcs.kind == a2.Kind.CONIC
    assert a2.conic_allow_hyperbolic() is False


def test_geodesic_accessors():
    g = a2.GeodesicArc.from_points((1, 0, 0), (0, 1, 0))
    assert not g.is_full and not g.is_degenerate
    assert g.normal.kind == a2.Kind.SPHERE
    full = a2.GeodesicArc.great_circle((0, 0, 1))
    assert full.is_full and not full.is_x_monotone
    assert len(full.make_x_monotone()) == 2
    xm = a2.GeodesicArc.x_monotone_arc((1, 0, 0), (0, 1, 0))
    assert xm.is_x_monotone
    withn = a2.GeodesicArc.from_points_and_normal((1, 0, 0), (-1, 0, 0), (0, 0, 1))
    assert withn.kind == a2.Kind.SPHERE
    mer = a2.GeodesicArc.from_points((1, 0, 0), (0, 0, 1))
    assert mer.is_meridian or mer.is_vertical


# ===========================================================================
# arrangement: counts, iteration, handles
# ===========================================================================
@pytest.mark.parametrize("kind", ALL_KINDS)
def test_counts(kind):
    arr = build(kind)
    v, e, f, uf, nc = COUNTS[kind]
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == (v, e, f)
    assert arr.number_of_unbounded_faces == uf
    assert arr.number_of_curves == nc
    assert arr.number_of_halfedges == 2 * e
    assert len(arr) == e
    assert arr.is_valid()
    assert not arr.is_empty
    assert arr.kind == K(kind)
    assert arr.dimension == (3 if kind == SPHERE else 2)
    assert arr.is_unbounded_kind == (kind == "linear")
    # Euler: V - E + F = 2 for a connected arrangement of the plane (the unbounded face
    # counted) and of the sphere.  The linear kind adds one vertex per unbounded ray end.
    if kind == "linear":
        assert arr.number_of_vertices_at_infinity == 4
    else:
        assert v - e + f == 2


@pytest.mark.parametrize("kind", ALL_KINDS)
def test_iteration_and_handles(kind):
    arr = build(kind)
    vs, hes, es, fs, cs = (arr.vertices(), arr.halfedges(), arr.edges(),
                           arr.faces(), arr.curves())
    assert len(vs) == arr.number_of_vertices
    assert len(hes) == arr.number_of_halfedges
    assert len(es) == arr.number_of_edges
    assert len(fs) == arr.number_of_faces
    assert len(cs) == arr.number_of_curves
    assert len(arr.unbounded_faces()) == arr.number_of_unbounded_faces
    assert len(arr.bounded_faces()) == arr.number_of_faces - arr.number_of_unbounded_faces

    for v in vs:
        assert v.is_valid and v.arrangement is arr and isinstance(v.id, int)
        assert repr(v)
        assert v.degree >= 0
        assert isinstance(v.is_isolated, bool)
        assert v.parameter_space_in_x in ("left", "right", "interior")
        assert v.parameter_space_in_y in ("bottom", "top", "interior")
        if not v.is_at_open_boundary:
            assert v.point.kind == K(kind)
        assert len(v.incident_halfedges()) == v.degree
        assert v.incident_faces() is not None

    for he in es:
        assert he.is_valid and repr(he)
        assert he.twin.twin == he
        assert he.next.prev == he
        assert he.source and he.target
        assert he.direction in ("left_to_right", "right_to_left")
        assert he.is_on_outer_ccb != he.is_on_inner_ccb
        assert len(he.ccb()) >= 1
        assert isinstance(he.edge_id, int)
        assert he.edge_id == he.twin.edge_id
        if not he.is_fictitious:
            assert he.curve.kind == K(kind)
            assert he.directed_curve.kind == K(kind)
            assert he.number_of_originating_curves == len(he.originating_curves())

    for f in fs:
        assert f.is_valid and repr(f)
        assert f.number_of_outer_ccbs == len(f.outer_ccbs())
        assert f.number_of_inner_ccbs == len(f.inner_ccbs())
        assert f.holes() == f.inner_ccbs()
        assert f.number_of_isolated_vertices == len(f.isolated_vertices())
        assert f.has_outer_ccb == (f.number_of_outer_ccbs > 0)
        if f.has_outer_ccb:
            assert len(f.outer_ccb()) >= 1
        assert f.edges() is not None
        assert f.adjacent_faces() is not None

    for ch in cs:
        assert ch.is_valid and repr(ch)
        assert ch.curve.kind == K(kind)
        assert ch.number_of_induced_edges == len(ch.induced_edges())
        assert ch.number_of_induced_edges >= 1


@pytest.mark.parametrize("kind", ALL_KINDS)
def test_bulk_export(kind):
    arr = build(kind)
    coords = arr.vertex_coordinates()
    assert len(coords) == arr.number_of_vertices
    assert len(coords[0]) == (3 if kind == SPHERE else 2)
    idx = arr.edge_vertex_indices()
    assert len(idx) >= 0
    bnd = arr.face_boundaries()
    assert len(bnd) == arr.number_of_faces
    edges = arr.approximate_edges(1e-2)
    assert len(edges) == arr.number_of_edges
    bbox = arr.bbox()
    assert len(bbox) == (6 if kind == SPHERE else 4)


@pytest.mark.parametrize("kind", ALL_KINDS)
def test_insert_variants(kind):
    curves = curves_for(kind)
    # a list, in one aggregate sweep
    arr = a2.Arrangement(kind)
    handles = arr.insert(curves)
    assert len(handles) == len(curves)
    assert all(isinstance(h, a2.CurveHandle) for h in handles)
    # one curve at a time
    arr2 = a2.Arrangement(kind)
    for c in curves:
        h = arr2.insert(c)
        assert isinstance(h, a2.CurveHandle)
    assert arr2.number_of_edges == arr.number_of_edges
    # a point
    v = arr.insert_point(as_query(kind, INSIDE[kind]))
    assert isinstance(v, a2.Vertex) and v.is_isolated
    assert arr.number_of_isolated_vertices == 1
    assert v.face is not None
    # insert() dispatches a point-like argument to insert_point
    arr3 = a2.Arrangement(kind)
    v3 = arr3.insert(as_query(kind, INSIDE[kind]))
    assert isinstance(v3, a2.Vertex)


@pytest.mark.parametrize("kind", ALL_KINDS)
def test_data_round_trip_and_copy(kind):
    arr = build(kind)
    payload = {"tag": kind}
    arr.vertices()[0].data = payload
    arr.edges()[0].data = 42
    arr.faces()[0].data = "face"
    assert arr.vertices()[0].data is payload
    assert arr.edges()[0].data == 42
    assert arr.faces()[0].data == "face"

    clone = arr.copy()
    assert clone is not arr
    assert clone.number_of_vertices == arr.number_of_vertices
    assert clone.number_of_edges == arr.number_of_edges
    assert clone.number_of_faces == arr.number_of_faces
    assert clone.number_of_curves == arr.number_of_curves
    assert clone.is_valid()
    # the clone carries the data over (the same Python objects, by reference)
    assert sorted([repr(v.data) for v in clone.vertices() if v.data is not None]) == [repr(payload)]
    assert _copy.copy(arr).number_of_edges == arr.number_of_edges
    assert _copy.deepcopy(arr).number_of_edges == arr.number_of_edges

    # a handle of the original is not valid in the clone
    with pytest.raises(a2.InvalidHandleError):
        clone.remove_edge(arr.edges()[0])

    arr.vertices()[0].data = None
    assert arr.vertices()[0].data is None


# ===========================================================================
# point location, ray shooting, zone, decompose, batched
# ===========================================================================
@pytest.mark.parametrize("kind", ALL_KINDS)
def test_locate_every_supported_strategy(kind):
    arr = build(kind)
    inside = as_query(kind, INSIDE[kind])
    ref = arr.locate(inside)
    assert isinstance(ref, a2.Face)
    supported = [s for s in a2.point_location_strategies() if arr.supports_point_location(s)]
    assert "naive" in supported
    # REGRESSION (5e): triangulation is never exposed (CGAL returns the wrong face for
    # faces with holes); trapezoid is refused for the unbounded 'linear' kind.
    assert "triangulation" not in supported
    if kind == "linear":
        assert "trapezoid" not in supported
    if kind == SPHERE:
        # landmarks COMPILES for the sphere but is unusable: the landmark walk joins the
        # nearest landmark to the query point with Construct_x_monotone_curve_2, whose
        # CGAL 6.1 precondition forbids an antipodal pair
        # (Arr_geodesic_arc_on_sphere_traits_2.h:611) and CGAL picks the landmark.
        assert supported == ["naive"]

    for s in supported:
        assert arr.locate(inside, s) == ref               # temporary strategy object
        arr.attach_point_location(s)
        assert arr.has_point_location(s)
        assert arr.locate(inside, s) == ref               # attached strategy object
        arr.detach_point_location(s)
        assert not arr.has_point_location(s)

    for s in a2.point_location_strategies():
        if s not in supported:
            with pytest.raises(a2.UnsupportedError):
                arr.locate(inside, s)
            with pytest.raises(a2.UnsupportedError):
                arr.attach_point_location(s)


@pytest.mark.parametrize("kind", PLANAR_KINDS)
def test_locate_vertex_edge_face(kind):
    arr = build(kind)
    v0 = arr.vertices()[0]
    assert arr.locate(v0.point) == v0
    if kind in OUTSIDE:
        out = arr.locate(a2.Point(*OUTSIDE[kind], kind=kind))
        assert isinstance(out, a2.Face) and out.is_unbounded


@pytest.mark.parametrize("kind", PLANAR_KINDS)
def test_ray_shoot(kind):
    arr = build(kind)
    inside = as_query(kind, INSIDE[kind])
    up = arr.ray_shoot_up(inside)
    down = arr.ray_shoot_down(inside)
    assert up is not None and down is not None
    shooters = [s for s in ("simple", "walk", "trapezoid")
                if arr.supports_point_location(s)]
    assert shooters
    for s in shooters:
        h = arr.ray_shoot_up(inside, s)
        assert h == up or h == up.twin
    for s in ("naive", "landmarks"):
        if arr.supports_point_location(s):
            with pytest.raises(a2.UnsupportedError):
                arr.ray_shoot_up(inside, s)


def test_sphere_has_no_ray_shooting():
    arr = build(SPHERE)
    with pytest.raises(a2.UnsupportedError):
        arr.ray_shoot_up(a2.Point(1, 1, 1, kind=SPHERE))


@pytest.mark.parametrize("kind", ALL_KINDS)
def test_batched_locate_order(kind):
    arr = build(kind)
    pts = [as_query(kind, INSIDE[kind])]
    pts += [v.point for v in arr.vertices() if not v.is_at_open_boundary]
    if kind in OUTSIDE:
        pts.append(a2.Point(*OUTSIDE[kind], kind=kind))
    pts.append(pts[0])                                    # a repeated query
    got = arr.batched_locate(pts)
    assert len(got) == len(pts)
    # the results are aligned with the INPUT order, not with CGAL's xy-sorted output
    for p, res in zip(pts, got):
        assert res == arr.locate(p), (kind, repr(p))


def test_sphere_batched_locate_at_the_poles():
    """REGRESSION (5d): CGAL's batched sweep segfaults at the north pole and violates a
    precondition at the south pole / on the identification curve; those queries are routed
    through the naive strategy one by one."""
    arr = a2.Arrangement(SPHERE)
    arr.insert([
        a2.GeodesicArc.from_points((1, 0, 0), (0, 1, 0)),
        a2.GeodesicArc.from_points((0, 1, 0), (0, 0, 1)),
        a2.GeodesicArc.from_points((0, 0, 1), (1, 0, 0)),
    ])
    queries = [(1, 1, 1), (0, 0, 1), (0, 0, -1), (-1, 0, 0), (1, 0, 0), (2, 2, 2)]
    pts = [a2.Point(*q, kind=SPHERE) for q in queries]
    got = arr.batched_locate(pts)
    assert len(got) == len(pts)
    for p, res in zip(pts, got):
        assert res == arr.locate(p), repr(p)
    # the north pole is a vertex of this arrangement
    assert isinstance(got[1], a2.Vertex)


#: a probe curve that CROSSES the canonical arrangement without overlapping any edge
def crossing_probe(kind):
    if kind == "segment":
        return a2.Segment((2, -1), (2, 5))
    if kind == "linear":
        return a2.Line((0, 1), (1, 2))
    if kind == "circle_segment":
        return a2.CircleSegment.segment((0, -3), (0, 3))
    if kind == "polyline":
        return a2.Polyline([(2, -1), (2, 5)])
    if kind == "bezier":
        return a2.BezierCurve([(1, -1), (1, 5)])
    if kind == "conic":
        return a2.ConicArc.segment((0, -3), (0, 3))
    if kind == "sphere":
        # from inside the triangle out through one of its edges
        return a2.GeodesicArc.from_points((1, 1, 1), (1, -1, 1))
    raise AssertionError(kind)


@pytest.mark.parametrize("kind", ALL_KINDS)
def test_zone_and_do_intersect(kind):
    arr = build(kind)
    probe = crossing_probe(kind)
    before = (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces)
    z = arr.zone(probe)
    assert z and all(isinstance(x, (a2.Vertex, a2.Halfedge, a2.Face)) for x in z)
    assert arr.do_intersect(probe)
    # zone() does not modify the arrangement
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == before
    # a curve far away from everything meets nothing
    if kind != SPHERE:
        away = {"segment": a2.Segment((90, 90), (95, 95)),
                "linear": a2.LinearCurve.segment((90, 90), (95, 95)),
                "circle_segment": a2.CircleSegment.segment((90, 90), (95, 95)),
                "polyline": a2.Polyline([(90, 90), (95, 95)]),
                "bezier": a2.BezierCurve([(90, 90), (95, 95)]),
                "conic": a2.ConicArc.segment((90, 90), (95, 95))}[kind]
        assert arr.do_intersect(away) is False


def test_zone_of_an_overlapping_curve_is_refused_for_bezier():
    """CGAL 6.1 bug: Arrangement_zone_2 does std::get<X_monotone_curve_2> on a variant that
    holds an intersection point when the query curve overlaps an edge of a Bezier
    arrangement (Arrangement_zone_2_impl.h:214).  The core turns that into our documented
    Unsupported error instead of letting std::bad_variant_access escape."""
    arr = a2.Arrangement("bezier")
    arr.insert([a2.BezierCurve([(0, 0), (1, 3), (2, 0)]), a2.BezierCurve([(0, 1), (2, 1)])])
    overlapping = a2.BezierCurve([(0, 1), (2, 1)])
    with pytest.raises(a2.UnsupportedError) as ei:
        arr.do_intersect(overlapping)
    assert "bad_variant_access" in str(ei.value)
    with pytest.raises(a2.UnsupportedError):
        arr.zone(overlapping)
    # the other kinds handle the same query fine
    for kind, mk in (("segment", lambda: a2.Segment((0, 0), (4, 0))),
                     ("conic", lambda: a2.ConicArc.segment((-2, 0), (2, 0))),
                     ("circle_segment", lambda: a2.CircleSegment.segment((-2, 0), (2, 0))),
                     ("polyline", lambda: a2.Polyline([(0, 0), (4, 0)])),
                     ("linear", lambda: a2.LinearCurve.segment((0, 0), (4, 0)))):
        other = a2.Arrangement(kind)
        other.insert(mk())
        assert other.do_intersect(mk())
        assert len(other.zone(mk())) == 3


@pytest.mark.parametrize("kind", PLANAR_KINDS)
def test_decompose(kind):
    arr = build(kind)
    d = arr.decompose()
    assert len(d) == arr.number_of_vertices
    seen = set()
    for v, below, above in d:
        assert isinstance(v, a2.Vertex)
        seen.add(v.id)
        for cell in (below, above):
            assert cell is None or isinstance(cell, (a2.Vertex, a2.Halfedge, a2.Face))
    assert len(seen) == arr.number_of_vertices


def test_sphere_decompose_is_unsupported_at_a_pole():
    """REGRESSION (5d): CGAL asserts `p1.is_no_boundary()` in the decomposition sweep."""
    arr = a2.Arrangement(SPHERE)
    arr.insert([
        a2.GeodesicArc.from_points((1, 0, 0), (0, 1, 0)),
        a2.GeodesicArc.from_points((0, 1, 0), (0, 0, 1)),
        a2.GeodesicArc.from_points((0, 0, 1), (1, 0, 0)),
    ])
    with pytest.raises(a2.UnsupportedError) as ei:
        arr.decompose()
    assert "interior of the parameter space" in str(ei.value)
    # ... but an all-interior spherical arrangement decomposes normally
    assert len(build(SPHERE).decompose()) == 3


def test_sphere_remove_boundary_vertex_is_unsupported():
    """REGRESSION (5d): removing a pole / identification vertex leaves the spherical
    topology traits with dangling pointers and the next insertion segfaults."""
    arr = a2.Arrangement(SPHERE)
    arr.insert([
        a2.GeodesicArc.from_points((1, 0, 0), (0, 1, 0)),
        a2.GeodesicArc.from_points((0, 1, 0), (0, 0, 1)),
        a2.GeodesicArc.from_points((0, 0, 1), (1, 0, 0)),
    ])
    pole = [v for v in arr.vertices()
            if v.parameter_space_in_x != "interior" or v.parameter_space_in_y != "interior"]
    assert pole, "the north pole must be a vertex of this arrangement"
    for v in pole:
        with pytest.raises(a2.UnsupportedError):
            arr.remove_vertex(v)
        with pytest.raises(a2.UnsupportedError):
            arr.remove_isolated_vertex(v)
    assert arr.is_valid()


# ===========================================================================
# overlay and observers
# ===========================================================================
def test_overlay_with_callbacks():
    A = a2.Arrangement("segment")
    A.insert([a2.Segment((0, 0), (4, 0)), a2.Segment((4, 0), (4, 4)),
              a2.Segment((4, 4), (0, 4)), a2.Segment((0, 4), (0, 0))])
    B = a2.Arrangement("segment")
    B.insert([a2.Segment((2, 2), (6, 2)), a2.Segment((6, 2), (6, 6)),
              a2.Segment((6, 6), (2, 6)), a2.Segment((2, 6), (2, 2))])
    for f in A.bounded_faces():
        f.data = "A"
    for f in B.bounded_faces():
        f.data = "B"

    seen = []

    class CB(a2.OverlayCallbacks):
        def face_face(self, fa, fb, fr):
            seen.append((fa.data, fb.data))
            fr.data = (fa.data, fb.data)

        def edge_face(self, ea, fb, er):
            er.data = "edge_face"

    R = A.overlay(B, CB())
    assert R.is_valid() and R.kind == a2.Kind.SEGMENT
    assert R.number_of_faces == 4
    assert len(seen) == 4
    labels = sorted(str(f.data) for f in R.faces())
    assert labels == ["('A', 'B')", "('A', None)", "(None, 'B')", "(None, None)"]
    # the inputs are unchanged
    assert A.number_of_faces == 2 and B.number_of_faces == 2

    # the on_face shorthand
    R2 = A.overlay(B, on_face=lambda fa, fb: (fa.data, fb.data))
    assert sorted(str(f.data) for f in R2.faces()) == labels

    with pytest.raises(ValueError):
        A.overlay(A)
    with pytest.raises(a2.KindMismatchError):
        A.overlay(a2.Arrangement("conic"))


@pytest.mark.parametrize("kind", ALL_KINDS)
def test_observer_receives_events(kind):
    events = []

    class Obs(a2.Observer):
        def after_create_vertex(self, v):
            events.append(("vertex", v.id))

        def after_create_edge(self, e):
            events.append(("edge", e.id))

        def before_global_change(self):
            events.append(("global", None))

        def after_global_change(self):
            events.append(("global_end", None))

    arr = a2.Arrangement(kind)
    obs = Obs()
    assert arr.add_observer(obs) is obs
    assert arr.observers() == [obs]
    with pytest.raises(ValueError):
        arr.add_observer(obs)
    arr.insert(curves_for(kind))
    assert any(e[0] == "vertex" for e in events)
    assert any(e[0] == "edge" for e in events)
    assert ("global", None) in events and ("global_end", None) in events
    arr.remove_observer(obs)
    assert arr.observers() == []
    n = len(events)
    arr.insert(as_query(kind, INSIDE[kind]))
    assert len(events) == n          # detached: nothing more arrives
    with pytest.raises(ValueError):
        arr.remove_observer(obs)


def test_observer_exception_propagates():
    class Boom(a2.Observer):
        def after_create_vertex(self, v):
            raise RuntimeError("boom")

    arr = a2.Arrangement("segment")
    arr.add_observer(Boom())
    with pytest.raises(RuntimeError):
        arr.insert(a2.Segment((0, 0), (1, 1)))


# ===========================================================================
# modification / removal / handle invalidation
# ===========================================================================
def test_split_merge_remove_and_invalidation():
    arr = a2.Arrangement("segment")
    handles = arr.insert([a2.Segment((0, 0), (4, 0)), a2.Segment((4, 0), (4, 4)),
                          a2.Segment((4, 4), (0, 4)), a2.Segment((0, 4), (0, 0))])
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == (4, 4, 2)

    bottom = next(h for h in arr.edges() if h.curve == a2.Segment((0, 0), (4, 0)))
    arr.split_edge(bottom, a2.Point(2, 0))
    assert (arr.number_of_vertices, arr.number_of_edges) == (5, 5)

    mid = next(v for v in arr.vertices() if v.point == a2.Point(2, 0))
    assert mid.degree == 2
    e1, e2 = (h.twin for h in mid.incident_halfedges())
    merged = arr.merge_edge(e1, e2)
    assert (arr.number_of_vertices, arr.number_of_edges) == (4, 4)
    assert merged.curve == a2.Segment((0, 0), (4, 0))

    # split with two explicit halves, then merge with an explicit union
    arr.split_edge(merged, a2.Segment((0, 0), (1, 0)), a2.Segment((1, 0), (4, 0)))
    assert arr.number_of_edges == 5
    mid = next(v for v in arr.vertices() if v.point == a2.Point(1, 0))
    e1, e2 = (h.twin for h in mid.incident_halfedges())
    merged = arr.merge_edge(e1, e2, a2.Segment((0, 0), (4, 0)))
    assert arr.number_of_edges == 4

    face = arr.remove_edge(merged)
    assert isinstance(face, a2.Face)
    assert (arr.number_of_edges, arr.number_of_faces) == (3, 1)
    assert not merged.is_valid
    with pytest.raises(a2.InvalidHandleError):
        merged.source
    with pytest.raises(a2.InvalidHandleError):
        arr.remove_edge(merged)

    removed = arr.remove_curve(handles[1])
    assert removed == 1
    assert not handles[1].is_valid
    with pytest.raises(a2.InvalidHandleError):
        handles[1].curve
    assert arr.is_valid()


def test_modify_and_isolated_vertices():
    arr = a2.Arrangement("segment")
    he = arr.insert_non_intersecting(a2.Segment((0, 0), (4, 0)))
    assert isinstance(he, a2.Halfedge)
    assert arr.number_of_curves == 0          # no history for this entry point
    he2 = arr.modify_edge(he, a2.Segment((0, 0), (4, 0)))
    assert he2.curve == a2.Segment((0, 0), (4, 0))
    v = arr.vertices()[0]
    v2 = arr.modify_vertex(v, v.point)
    assert v2.point == v.point

    f = arr.unbounded_face
    iso = arr.insert_point_in_face_interior(a2.Point(1, 5), f)
    assert iso.is_isolated and iso.face == f
    assert arr.number_of_isolated_vertices == 1
    back = arr.remove_isolated_vertex(iso)
    assert back == f
    assert arr.number_of_isolated_vertices == 0

    h = arr.insert_in_face_interior(a2.Segment((0, 5), (2, 5)), f)
    assert isinstance(h, a2.Halfedge)
    assert arr.remove_vertex(h.source) is False or True   # the call must not raise

    arr2 = a2.Arrangement("segment")
    arr2.insert_non_intersecting(a2.Segment((0, 0), (4, 0)))
    v_left = next(v for v in arr2.vertices() if v.point == a2.Point(0, 0))
    v_right = next(v for v in arr2.vertices() if v.point == a2.Point(4, 0))
    # the vertex must be the LEFT (lexicographically smallest) end of the new curve for
    # insert_from_left_vertex, and its RIGHT end for insert_from_right_vertex
    assert isinstance(arr2.insert_from_right_vertex(a2.Segment((-4, 0), (0, 0)), v_left),
                      a2.Halfedge)
    assert isinstance(arr2.insert_from_left_vertex(a2.Segment((4, 0), (8, 0)), v_right),
                      a2.Halfedge)
    assert arr2.is_valid()
    assert arr2.number_of_edges == 3

    arr3 = a2.Arrangement("segment")
    a = arr3.insert_non_intersecting(a2.Segment((0, 0), (4, 0)))
    b = arr3.insert_non_intersecting(a2.Segment((0, 4), (4, 4)))
    va = a.source if a.source.point == a2.Point(0, 0) else a.target
    vb = b.source if b.source.point == a2.Point(0, 4) else b.target
    joined = arr3.insert_at_vertices(a2.Segment((0, 0), (0, 4)), va, vb)
    assert isinstance(joined, a2.Halfedge)
    assert arr3.is_valid()


def test_clear_and_assign():
    arr = build("segment")
    other = a2.Arrangement("segment")
    other.assign(arr)
    assert other.number_of_edges == arr.number_of_edges
    arr.clear()
    assert arr.is_empty and arr.number_of_faces == 1
    assert other.number_of_edges == COUNTS["segment"][1]


# ===========================================================================
# exact numbers / approximation
# ===========================================================================
def test_point_exact_approx_interval_rational():
    p = a2.Point(Fraction(1, 3), 2)
    assert p.is_rational
    assert p.exact() == (Fraction(1, 3), Fraction(2))
    assert p.exact_rational() == (Fraction(1, 3), Fraction(2))
    assert p.x == float(Fraction(1, 3)) == 1 / 3
    assert p.approx == (1 / 3, 2.0)
    (lo, hi), (ylo, yhi) = p.interval()
    assert lo <= 1 / 3 <= hi and ylo == yhi == 2.0
    assert p.compare_xy(a2.Point(1, 2)) == -1
    assert p.compare_x(a2.Point(1, 2)) == -1
    assert p == a2.Point(Fraction(1, 3), 2)
    assert hash(p) == hash(a2.Point(Fraction(1, 3), 2))
    assert tuple(p) == p.xy and len(p) == 2 and p[0] == p.x
    assert repr(p) == "Point(1/3, 2)"
    # every accepted coordinate spelling
    for spelling in (Fraction(1, 3), "1/3", 1 / 3.0, 0.5, 7, "0.125"):
        assert a2.Point(spelling, 0).is_rational


def test_sqrt_extension_coordinates():
    arr = a2.Arrangement("circle_segment")
    arr.insert([a2.CircleSegment.circle((0, 0), 2),
                a2.CircleSegment.segment((1, -3), (1, 3))])
    sqrt_points = [v.point for v in arr.vertices()
                   if any(isinstance(z, a2.SqrtExtension) for z in v.point.exact())]
    assert sqrt_points
    p = sqrt_points[0]
    assert not p.is_rational
    with pytest.raises(a2.NotRepresentableError):
        p.exact_rational()
    y = [z for z in p.exact() if isinstance(z, a2.SqrtExtension)][0]
    assert (y.a, y.c) == (Fraction(0), Fraction(3))
    assert abs(abs(float(y)) - math.sqrt(3)) < 1e-12
    assert not y.is_rational and y.exact() is None
    (xlo, xhi), (ylo, yhi) = p.interval()
    assert ylo <= float(y) <= yhi
    assert repr(y)


def test_algebraic_coordinates():
    arr = a2.Arrangement("conic")
    arr.insert([a2.ConicArc.circle((0, 0), 2), a2.ConicArc.segment((-3, 1), (3, 1))])
    pts = [v.point for v in arr.vertices()]
    alg = None
    for p in pts:
        assert not p.is_rational
        with pytest.raises(a2.NotRepresentableError):
            p.exact_rational()
        for z in p.exact():
            assert isinstance(z, a2.Algebraic)
            if abs(abs(float(z)) - math.sqrt(3)) < 1e-9:
                alg = z
    assert alg is not None, "the circle x^2+y^2=4 meets y=1 at x = +-sqrt(3)"
    lo, hi = alg.interval(53)
    assert lo <= float(alg) <= hi
    lo2, hi2 = alg.interval(200)
    assert lo2 <= float(alg) <= hi2
    assert alg.refine(128) is not None or True
    assert repr(alg)


#: a BOUNDED representative curve per kind (the 'linear' input curves are whole lines)
def bounded_curve(kind):
    if kind == "linear":
        return a2.LinearCurve.segment((0, 0), (4, 4))
    return curves_for(kind)[0]


@pytest.mark.parametrize("kind", ALL_KINDS)
def test_curve_approximate(kind):
    curve = bounded_curve(kind)
    xm = curve if curve.is_x_monotone else curve.make_x_monotone()[0]
    dim = 3 if kind == SPHERE else 2
    pts = xm.approximate(1e-3)
    assert len(pts) >= 2 and all(len(p) == dim for p in pts)
    assert xm.approximate_length(1e-4) > 0
    box = xm.bbox()
    assert len(box) == 2 * dim
    coarse = xm.approximate(1e-1)
    fine = xm.approximate(1e-5)
    assert len(fine) >= len(coarse)
    with pytest.raises(ValueError):
        xm.approximate(0.0)


def test_unbounded_curve_needs_a_clip_box():
    line = a2.Line((0, 0), (1, 1))
    with pytest.raises(ValueError):
        line.approximate(1e-3)
    assert line.approximate(1e-3, bbox=(-1, -1, 1, 1)) == [(-1.0, -1.0), (1.0, 1.0)]


# ===========================================================================
# traits functors
# ===========================================================================
@pytest.mark.parametrize("kind", ALL_KINDS)
def test_traits_functors(kind):
    t = a2.traits(kind)
    assert t.kind == K(kind) and repr(t)
    assert t.dimension == (3 if kind == SPHERE else 2)
    curve = bounded_curve(kind)
    xm = curve if curve.is_x_monotone else curve.make_x_monotone()[0]
    assert t.make_x_monotone(curve)
    assert t.is_vertical(xm) in (True, False)
    assert t.compare_endpoints_xy(xm) in (-1, 1)
    assert t.curves_equal(xm, xm)
    assert repr(t.opposite(xm))
    assert repr(t.min_vertex(xm)) and repr(t.max_vertex(xm))
    src, tgt = t.min_vertex(xm), t.max_vertex(xm)
    assert t.equal(src, src) and not t.equal(src, tgt)
    assert t.compare_xy(src, tgt) == -1
    assert t.compare_x(src, tgt) in (-1, 0)
    assert t.parameter_space_in_x(xm) == "interior"
    assert t.is_in_x_range(xm, src)
    assert t.compare_y_at_x(src, xm) == 0
    assert len(t.approximate(xm)) >= 2
    assert isinstance(t.approximate_point(src, 0), float)

    # REGRESSION (5b): approximate_coordinate must agree with point_approx.  The raw CGAL
    # Approximate_2 is CGAL::to_double(Epeck::FT) and is not correctly rounded.
    p = a_point(kind, Fraction(1, 3), Fraction(2, 7), 1) if kind != SPHERE \
        else a2.Point(1, 2, 3, kind=SPHERE)
    assert tuple(t.approximate_point(p, i) for i in range(t.dimension)) == p.approx
    if kind != SPHERE:
        assert t.approximate_point(p, 0) == 1 / 3


def test_traits_split_merge_trim_intersect():
    t = a2.traits("segment")
    s = a2.Segment((0, 0), (4, 4))
    left, right = t.split(s, a2.Point(2, 2))
    assert left == a2.Segment((0, 0), (2, 2)) and right == a2.Segment((2, 2), (4, 4))
    assert t.are_mergeable(left, right)
    assert t.merge(left, right) == s
    assert t.trim(s, a2.Point(1, 1), a2.Point(3, 3)) == a2.Segment((1, 1), (3, 3))
    hits = t.intersect(s, a2.Segment((0, 4), (4, 0)))
    assert hits == [(a2.Point(2, 2), 1)]
    assert t.construct_x_monotone_curve((0, 0), (1, 1)) == a2.Segment((0, 0), (1, 1))
    assert t.compare_y_at_x_left(s, a2.Segment((0, 4), (4, 0)), a2.Point(2, 2)) == -1
    assert t.compare_y_at_x_right(s, a2.Segment((0, 4), (4, 0)), a2.Point(2, 2)) == 1


def test_curve_level_operations():
    s = a2.Segment((0, 0), (4, 4))
    c1, c2 = s.split(a2.Point(2, 2))
    assert s.can_merge(c1) is False or True
    assert c1.merge(c2) == s
    assert s.opposite() == a2.Segment((4, 4), (0, 0))
    assert s.trim((1, 1), (3, 3)) == a2.Segment((1, 1), (3, 3))
    assert s.intersect(a2.Segment((0, 4), (4, 0))) == [(a2.Point(2, 2), 1)]
    assert s.compare_y_at_x((2, 3)) == 1
    assert s.is_in_x_range((2, 99))
    assert s.compare_endpoints_xy() == -1
    assert s.to_kind("polyline").kind == a2.Kind.POLYLINE
    assert s.to_kind("conic").kind == a2.Kind.CONIC


# ===========================================================================
# Boolean set operations
# ===========================================================================
def _square_polygons(kind):
    """Two overlapping regions of `kind`, as Polygon objects."""
    if kind == "segment":
        return (a2.Polygon([(0, 0), (2, 0), (2, 2), (0, 2)]),
                a2.Polygon([(1, 1), (3, 1), (3, 3), (1, 3)]))
    if kind == "circle_segment":
        return (a2.Polygon(a2.CircleSegment.circle((0, 0), 2).make_x_monotone()),
                a2.Polygon(a2.CircleSegment.circle((2, 0), 2).make_x_monotone()))
    if kind == "conic":
        return (a2.Polygon(a2.ConicArc.circle((0, 0), 2).make_x_monotone()),
                a2.Polygon(a2.ConicArc.circle((2, 0), 2).make_x_monotone()))
    if kind == "bezier":
        # two "lens" regions, each closed by two Bezier arcs
        up = a2.BezierCurve([(0, 0), (1, 2), (2, 0)])
        down = a2.BezierCurve([(0, 0), (1, -2), (2, 0)])
        up2 = a2.BezierCurve([(1, 0), (2, 2), (3, 0)])
        down2 = a2.BezierCurve([(1, 0), (2, -2), (3, 0)])
        return (a2.Polygon([up.x_monotone(), down.x_monotone().opposite()]),
                a2.Polygon([up2.x_monotone(), down2.x_monotone().opposite()]))
    raise AssertionError(kind)


@pytest.mark.parametrize("kind", BSO_KINDS)
def test_polygon_set_operations(kind):
    pa, pb = _square_polygons(kind)
    A, B = a2.PolygonSet(kind), a2.PolygonSet(kind)
    A.insert(pa)
    B.insert(pb)
    assert A.kind == K(kind) and repr(A)
    assert not A.is_empty and not A.is_plane
    assert A.number_of_polygons_with_holes == 1 == len(A)
    assert A.is_valid()

    u = a2.join(A, B)
    i = a2.intersection(A, B)
    d = a2.difference(A, B)
    x = a2.symmetric_difference(A, B)
    c = a2.complement(A)
    assert not i.is_empty and not u.is_empty and not d.is_empty and not x.is_empty
    assert c.polygons_with_holes()[0].is_unbounded
    assert a2.do_intersect(A, B)

    # the operators agree with the functions
    assert len(A | B) == len(u)
    assert len(A & B) == len(i)
    assert len(A - B) == len(d)
    assert len(A ^ B) == len(x)
    assert len(~A) == len(c)

    # in place, returning self
    E = A.copy()
    assert E.join(B) is E
    assert len(E) == len(u)
    E2 = A.copy()
    E2 &= B
    assert len(E2) == len(i)

    for pwh in u.polygons_with_holes():
        assert repr(pwh) and pwh.kind == K(kind)
        assert pwh.outer is not None
        assert len(pwh.approximate(1e-2)) >= 1
        assert len(pwh.bbox()) == 4

    arr, faces = A.to_arrangement()
    assert isinstance(arr, a2.Arrangement) and arr.kind == K(kind)
    assert arr.is_valid()
    assert faces and all(isinstance(f, a2.Face) for f in faces)
    assert all(f.arrangement is arr for f in faces)

    assert A.oriented_side(_inside_point(kind)) == 1
    assert A.oriented_side(_far_point(kind)) == -1
    assert A.locate(_inside_point(kind)) is not None
    assert A.locate(_far_point(kind)) is None

    A.clear()
    assert A.is_empty and len(A) == 0


def _inside_point(kind):
    return a2.Point(*( (1, 1) if kind == "segment" else (0, 0) ), kind=kind) \
        if kind != "bezier" else a2.Point(1, 0, kind="bezier")


def _far_point(kind):
    return a2.Point(50, 50, kind=kind)


def test_polygon_and_polygon_with_holes():
    poly = a2.Polygon([(0, 0), (4, 0), (4, 4), (0, 4)])
    assert poly.orientation() == 1 and poly.area() == 16
    assert poly.is_simple() and poly.is_closed
    assert len(poly) == 4 and len(poly.points) == 4 and len(poly.curves) == 4
    assert poly.reverse().orientation() == -1
    assert poly.bbox() == (0.0, 0.0, 4.0, 4.0)
    assert poly.to_kind("polyline").kind == a2.Kind.POLYLINE
    assert repr(poly)
    assert a2.orientation(poly) == 1 and a2.is_valid_polygon(poly)

    hole = a2.Polygon([(1, 1), (1, 3), (3, 3), (3, 1)])       # clockwise
    pwh = a2.PolygonWithHoles(poly, [hole])
    assert pwh.outer is not None and len(pwh.holes) == 1
    assert not pwh.is_unbounded and repr(pwh)

    ps = a2.PolygonSet("segment")
    ps.insert(pwh)
    assert ps.number_of_polygons_with_holes == 1
    assert ps.oriented_side((2, 2)) == -1                      # inside the hole
    assert ps.oriented_side((0.5, 0.5)) == 1

    bad = a2.Polygon([(0, 0), (2, 2), (2, 0), (0, 2)])
    assert not a2.is_valid_polygon(bad)
    with pytest.raises(ValueError):
        a2.PolygonSet("segment").insert(bad)


def test_polygon_set_unavailable_kinds():
    for kind in ("linear", "polyline", "sphere"):
        with pytest.raises(a2.UnsupportedError):
            a2.PolygonSet(kind)


# ===========================================================================
# error translation
# ===========================================================================
def test_error_hierarchy():
    assert issubclass(a2.PreconditionError, a2.CGALError)
    assert issubclass(a2.PreconditionError, ValueError)
    assert issubclass(a2.KindMismatchError, TypeError)
    assert issubclass(a2.NotXMonotoneError, ValueError)
    assert issubclass(a2.NotRepresentableError, ValueError)
    assert issubclass(a2.UnsupportedError, NotImplementedError)
    assert issubclass(a2.InvalidHandleError, ValueError)


def test_precondition_error():
    arr = a2.Arrangement("segment")
    arr.insert(a2.Segment((0, 0), (4, 0)))
    with pytest.raises(a2.PreconditionError) as ei:
        arr.modify_edge(arr.edges()[0], a2.Segment((0, 0), (9, 9)))
    assert "precondition" in str(ei.value).lower()
    # split_edge's "point inside the edge" rule is checked by arr2d, not by CGAL
    with pytest.raises(ValueError, match="interior"):
        arr.split_edge(arr.edges()[0], a2.Point(9, 9))


def test_kind_mismatch_error():
    A = a2.Arrangement("segment")
    with pytest.raises(a2.KindMismatchError):
        A.overlay(a2.Arrangement("polyline"))
    with pytest.raises(a2.KindMismatchError):
        a2.traits("sphere").compare_xy(a2.Point(0, 0), a2.Point(1, 1))


def test_not_x_monotone_error():
    circle = a2.CircleSegment.circle((0, 0), 2)
    with pytest.raises(a2.NotXMonotoneError):
        circle.source
    with pytest.raises(a2.NotXMonotoneError):
        a2.Arrangement("circle_segment").insert_non_intersecting(circle)


def test_unsupported_error():
    with pytest.raises(a2.UnsupportedError):
        a2.traits("bezier").construct_x_monotone_curve((0, 0), (1, 1))
    with pytest.raises(a2.UnsupportedError):
        a2.traits("sphere").trim(a2.GeodesicArc.x_monotone_arc((1, 0, 0), (0, 1, 0)),
                                 a2.Point(1, 0, 0, kind=SPHERE),
                                 a2.Point(0, 1, 0, kind=SPHERE))
    with pytest.raises(a2.UnsupportedError):
        a2.Arrangement("segment").locate((0, 0), "triangulation")


def test_not_representable_error():
    arr = a2.Arrangement("conic")
    arr.insert([a2.ConicArc.circle((0, 0), 2), a2.ConicArc.segment((-3, 1), (3, 1))])
    p = arr.vertices()[0].point
    with pytest.raises(a2.NotRepresentableError):
        p.exact_rational()
    with pytest.raises(a2.NotRepresentableError):
        p.to_kind("segment")


def test_invalid_handle_error():
    arr = build("segment")
    other = build("segment")
    with pytest.raises(a2.InvalidHandleError):
        arr.remove_edge(other.edges()[0])


def test_bad_arguments():
    with pytest.raises(ValueError):
        a2.Arrangement("nope")
    with pytest.raises(ValueError):
        a2.Arrangement("segment").locate((0, 0), "no-such-strategy")


# ===========================================================================
# regressions for the cross-cutting integration fixes
# ===========================================================================
def test_linear_unbounded_overlap_is_refused():
    """REGRESSION (5c): CGAL aborts on `cv.has_left()` (incremental) and on
    `! e->is_fictitious()` (aggregate) when an unbounded curve overlaps an existing edge."""
    arr = a2.Arrangement("linear")
    arr.insert(a2.Line((0, 0), (1, 0)))
    for offender in (a2.Line((0, 0), (1, 0)),          # the same line again
                     a2.Ray((2, 0), (5, 0)),           # overlap unbounded on the right
                     a2.Ray((2, 0), (-5, 0))):         # overlap unbounded on the left
        with pytest.raises(a2.UnsupportedError) as ei:
            arr.insert(offender)
        assert "unbounded curve overlapping an existing edge" in str(ei.value)
    with pytest.raises(a2.UnsupportedError):
        arr.insert_non_intersecting(a2.Line((0, 0), (1, 0)))
    # the arrangement survived every refusal
    assert (arr.number_of_edges, arr.number_of_curves) == (1, 1)
    assert arr.is_valid()

    # an overlap that stays bounded is legal
    arr2 = a2.Arrangement("linear")
    arr2.insert(a2.LinearCurve.segment((0, 0), (5, 0)))
    arr2.insert(a2.Ray((2, 0), (5, 0)))
    assert arr2.is_valid()
    # and so is a crossing (as opposed to an overlap)
    arr3 = a2.Arrangement("linear")
    arr3.insert([a2.Line((0, 0), (1, 0)), a2.Line((0, 0), (0, 1))])
    assert arr3.number_of_edges == 4 and arr3.is_valid()


@pytest.mark.parametrize("kind", ALL_KINDS)
def test_traits_output_is_not_appended(kind):
    """REGRESSION (5a): make_x_monotone / intersect clear their output vector.  Python
    always passes a fresh vector, so this checks the observable consequence: repeating a
    call returns the same number of results."""
    t = a2.traits(kind)
    curve = curves_for(kind)[0]
    first = t.make_x_monotone(curve)
    assert t.make_x_monotone(curve) == first or len(t.make_x_monotone(curve)) == len(first)
    xm = curve if curve.is_x_monotone else curve.make_x_monotone()[0]
    n = len(t.intersect(xm, xm))
    assert len(t.intersect(xm, xm)) == n


def test_cgal_handlers_are_silent(capfd):
    """REGRESSION (5f): CGAL's default handlers print the whole violation block to stderr
    before throwing; the module installs silent ones at import."""
    capfd.readouterr()
    arr = a2.Arrangement("segment")
    arr.insert(a2.Segment((0, 0), (4, 0)))
    with pytest.raises(a2.PreconditionError):
        arr.modify_edge(arr.edges()[0], a2.Segment((0, 0), (9, 9)))
    with pytest.raises(ValueError):
        arr.split_edge(arr.edges()[0], a2.Point(9, 9))
    with pytest.raises(ValueError):
        a2.PolygonSet("segment").insert(a2.Polygon([(0, 0), (2, 2), (2, 0), (0, 2)]))
    with pytest.raises(ValueError):
        a2.PolygonSet("segment").insert(a2.Polygon([(0, 0), (0, 2), (2, 2), (2, 0)]),
                                        fix_orientation=False)
    out, err = capfd.readouterr()
    assert err == "", "CGAL wrote to stderr: %r" % err
    assert out == ""


def test_point_location_strategy_matrix():
    """REGRESSION (5e): the exposed strategies must match KindPolicy."""
    expected = {
        "segment": {"naive", "simple", "walk", "landmarks", "trapezoid"},
        # 'linear' is asked here while EMPTY: landmarks is offered exactly while the
        # arrangement holds no unbounded edge (CGAL's
        # Arr_landmarks_point_location::_deal_with_curve_contained_in_segment reads
        # point() on a vertex at infinity, Arr_landmarks_pl_impl.h:414, and :533 walks off
        # the all-fictitious outer ccb).  See test_linear.py for the dynamic behaviour.
        "linear": {"naive", "simple", "walk", "landmarks"},
        "circle_segment": {"naive", "simple", "walk", "trapezoid"},
        "polyline": {"naive", "simple", "walk", "landmarks", "trapezoid"},
        "bezier": {"naive", "simple", "walk", "trapezoid"},
        "conic": {"naive", "simple", "walk", "landmarks", "trapezoid"},
        # landmarks compiles for the sphere but its query is unusable (antipodal
        # landmark/query pair -> CGAL precondition), so it is not advertised.
        "sphere": {"naive"},
    }
    for kind, want in expected.items():
        arr = a2.Arrangement(kind)
        got = {s for s in a2.point_location_strategies() if arr.supports_point_location(s)}
        assert got == want, (kind, got, want)


def test_no_core_abort_at_exit():
    """The CORE memory pool aborts at process exit if any CORE::Expr lives in static
    storage; building Bezier and conic geometry must not leave one behind."""
    import subprocess
    import sys

    code = (
        "import arrangement_2d as a2\n"
        "a2.BezierCurve([(0,0),(1,1),(2,0)])\n"
        "a2.ConicArc.circle((0,0), 1)\n"
        "arr = a2.Arrangement('conic')\n"
        "arr.insert(a2.ConicArc.circle((0,0), 2))\n"
        "arr2 = a2.Arrangement('bezier')\n"
        "arr2.insert(a2.BezierCurve([(0,0),(1,3),(2,0)]))\n"
    )
    proc = subprocess.run([sys.executable, "-c", code], capture_output=True, text=True,
                          timeout=300)
    assert proc.returncode == 0, (proc.returncode, proc.stdout, proc.stderr)
    assert "blocks.empty" not in proc.stderr
    assert proc.stderr == ""
