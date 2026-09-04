"""Tests for :mod:`arrangement_2d.regions` -- the high level region helpers.

Every helper is exercised on hand-verified inputs, across the geometry kinds it applies
to, together with its documented error cases (foreign / stale handles, kinds without
Boolean set operations, curves that leave the face they should split).
"""

from __future__ import annotations

import math
from fractions import Fraction

import pytest

a2 = pytest.importorskip("arrangement_2d")
regions = a2.regions

ALL_KINDS = ("segment", "linear", "circle_segment", "polyline", "bezier", "conic", "sphere")
BSO_KINDS = ("segment", "circle_segment", "conic", "bezier")


# ---------------------------------------------------------------------------
# fixtures / builders
# ---------------------------------------------------------------------------

def square(x0=0, y0=0, x1=4, y1=4):
    """The four segments of an axis-parallel rectangle."""
    return [
        a2.Segment((x0, y0), (x1, y0)),
        a2.Segment((x1, y0), (x1, y1)),
        a2.Segment((x1, y1), (x0, y1)),
        a2.Segment((x0, y1), (x0, y0)),
    ]


@pytest.fixture
def split_square():
    """A 4x4 square cut in two by the chord ``y = 2``: 2 bounded faces of area 8."""
    arr = a2.Arrangement("segment")
    arr.insert(square() + [a2.Segment((0, 2), (4, 2))])
    return arr


@pytest.fixture
def square_with_hole():
    """A 4x4 square containing a 2x2 square: an annular face (area 12) and a disk (4)."""
    arr = a2.Arrangement("segment")
    arr.insert(square() + square(1, 1, 3, 3))
    return arr


def curves_for(kind):
    """A small closed figure per kind (mirrors tests/test_smoke.py)."""
    if kind == "segment":
        return square()
    if kind == "linear":
        return [a2.Line((0, 0), (1, 0)), a2.Line((0, 0), (0, 1))]
    if kind == "circle_segment":
        return [a2.CircleSegment.circle((0, 0), 2),
                a2.CircleSegment.segment((-2, 0), (2, 0))]
    if kind == "polyline":
        return [a2.Polyline([(0, 0), (2, 2), (4, 0), (0, 0)])]
    if kind == "bezier":
        return [a2.BezierCurve([(0, 0), (1, 3), (2, 0)]), a2.BezierCurve([(0, 0), (2, 0)])]
    if kind == "conic":
        return [a2.ConicArc.circle((0, 0), 2), a2.ConicArc.segment((-2, 0), (2, 0))]
    if kind == "sphere":
        return [a2.GeodesicArc.from_points((3, 1, 1), (1, 3, 1)),
                a2.GeodesicArc.from_points((1, 3, 1), (1, 1, 3)),
                a2.GeodesicArc.from_points((1, 1, 3), (3, 1, 1))]
    raise AssertionError(kind)


def arrangement_for(kind):
    arr = a2.Arrangement(kind)
    arr.insert(curves_for(kind))
    return arr


# ---------------------------------------------------------------------------
# bounded_faces
# ---------------------------------------------------------------------------

def test_bounded_faces_segment(split_square):
    faces = regions.bounded_faces(split_square)
    assert len(faces) == 2
    assert all(not f.is_unbounded for f in faces)
    assert faces == split_square.bounded_faces()


@pytest.mark.parametrize("kind", ALL_KINDS)
def test_bounded_faces_every_kind(kind):
    arr = arrangement_for(kind)
    faces = regions.bounded_faces(arr)
    assert all(not f.is_unbounded for f in faces)
    if kind == "linear":
        assert faces == []                    # four unbounded quadrants
    else:
        assert len(faces) >= 1
    # the fictitious face is never among them
    assert all(not f.is_fictitious for f in faces)


def test_bounded_faces_rejects_non_arrangement():
    with pytest.raises(TypeError):
        regions.bounded_faces("not an arrangement")


# ---------------------------------------------------------------------------
# face_containing
# ---------------------------------------------------------------------------

def test_face_containing_inside(split_square):
    lower = regions.face_containing(split_square, (2, 1))
    upper = regions.face_containing(split_square, (2, 3))
    assert lower is not None and upper is not None
    assert lower != upper
    assert not lower.is_unbounded and not upper.is_unbounded
    assert regions.face_containing(split_square, (10, 10)).is_unbounded


def test_face_containing_on_boundary_default_is_none(split_square):
    assert regions.face_containing(split_square, (2, 2)) is None     # on the chord
    assert regions.face_containing(split_square, (0, 0)) is None     # on a vertex


def test_face_containing_on_boundary_any(split_square):
    face = regions.face_containing(split_square, (2, 2), on_boundary="any")
    assert isinstance(face, a2.Face)
    assert not face.is_unbounded
    vertex_face = regions.face_containing(split_square, (0, 0), on_boundary="any")
    assert isinstance(vertex_face, a2.Face)


def test_face_containing_on_boundary_raise(split_square):
    with pytest.raises(ValueError):
        regions.face_containing(split_square, (2, 2), on_boundary="raise")


def test_face_containing_bad_on_boundary(split_square):
    with pytest.raises(ValueError):
        regions.face_containing(split_square, (1, 1), on_boundary="whatever")


def test_face_containing_honours_strategy(split_square):
    for strategy in ("naive", "walk"):
        if not split_square.supports_point_location(strategy):
            continue
        face = regions.face_containing(split_square, (2, 1), strategy=strategy)
        assert face == regions.face_containing(split_square, (2, 1))


def test_face_containing_isolated_vertex():
    arr = a2.Arrangement("segment")
    arr.insert(square())
    v = arr.insert_point((2, 2))
    assert v.is_isolated
    assert regions.face_containing(arr, (2, 2)) is None
    face = regions.face_containing(arr, (2, 2), on_boundary="any")
    assert face == v.face


# ---------------------------------------------------------------------------
# face_area
# ---------------------------------------------------------------------------

def test_face_area_exact_for_segment(split_square):
    areas = sorted(regions.face_area(f) for f in regions.bounded_faces(split_square))
    assert areas == [Fraction(8), Fraction(8)]
    assert all(isinstance(a, Fraction) for a in areas)


def test_face_area_subtracts_holes(square_with_hole):
    by_holes = {f.number_of_inner_ccbs: regions.face_area(f)
                for f in regions.bounded_faces(square_with_hole)}
    assert by_holes[1] == Fraction(12)        # 4x4 minus the 2x2 hole
    assert by_holes[0] == Fraction(4)


def test_face_area_polyline_is_exact():
    arr = a2.Arrangement("polyline")
    arr.insert(a2.Polyline([(0, 0), (4, 0), (4, 4), (0, 4), (0, 0)]))
    face = regions.bounded_faces(arr)[0]
    assert regions.face_area(face) == Fraction(16)


def test_face_area_curved_is_approximate():
    arr = a2.Arrangement("circle_segment")
    arr.insert(a2.Circle((0, 0), 2))
    disk = regions.bounded_faces(arr)[0]
    area = regions.face_area(disk)
    assert isinstance(area, float)
    assert abs(area - 4.0 * math.pi) < 0.05


def test_face_area_unbounded_face_is_unsupported(split_square):
    with pytest.raises(a2.UnsupportedError):
        regions.face_area(split_square.unbounded_face)


def test_face_area_rejects_non_face(split_square):
    with pytest.raises(TypeError):
        regions.face_area(split_square)


# ---------------------------------------------------------------------------
# faces_polygons
# ---------------------------------------------------------------------------

def test_faces_polygons_segment(split_square):
    out = regions.faces_polygons(split_square)
    assert len(out) == 2
    for fb in out:
        assert isinstance(fb.face, a2.Face)
        assert fb.holes == []
        assert len(fb.outer) >= 4
        assert fb.outer[0] == fb.outer[-1]    # the ring is closed
    # namedtuple protocol
    face, outer, holes = out[0]
    assert (face, outer, holes) == tuple(out[0])


def test_faces_polygons_reports_holes(square_with_hole):
    out = {fb.face.id: fb for fb in regions.faces_polygons(square_with_hole)}
    with_hole = [fb for fb in out.values() if fb.holes]
    assert len(with_hole) == 1
    assert len(with_hole[0].holes) == 1
    assert len(with_hole[0].holes[0]) >= 4


def test_faces_polygons_subset_and_unbounded(split_square):
    faces = regions.bounded_faces(split_square)
    assert len(regions.faces_polygons(split_square, faces=[faces[0]])) == 1
    everything = regions.faces_polygons(split_square, include_unbounded=True)
    assert len(everything) == split_square.number_of_faces
    unbounded = [fb for fb in everything if fb.face.is_unbounded]
    assert len(unbounded) == 1
    assert unbounded[0].outer == []           # no outer CCB
    assert len(unbounded[0].holes) == 1


def test_faces_polygons_tolerance_refines_curves():
    arr = a2.Arrangement("circle_segment")
    arr.insert(a2.Circle((0, 0), 2))
    coarse = regions.faces_polygons(arr, 0.5)[0]
    fine = regions.faces_polygons(arr, 1e-4)[0]
    assert len(fine.outer) > len(coarse.outer)
    for x, y in fine.outer:
        assert abs(math.hypot(x, y) - 2.0) < 1e-3


@pytest.mark.parametrize("kind", ALL_KINDS)
def test_faces_polygons_every_kind(kind):
    arr = arrangement_for(kind)
    for fb in regions.faces_polygons(arr, 1e-2):
        assert fb.face.arrangement is arr
        assert len(fb.outer) >= 3 or fb.outer == []


def test_faces_polygons_rejects_foreign_face(split_square):
    other = a2.Arrangement("segment")
    other.insert(square())
    with pytest.raises(a2.InvalidHandleError):
        regions.faces_polygons(split_square, faces=other.bounded_faces())


# ---------------------------------------------------------------------------
# shared_edges
# ---------------------------------------------------------------------------

def test_shared_edges(split_square):
    faces = regions.bounded_faces(split_square)
    shared = regions.shared_edges(faces)
    assert len(shared) == 1
    assert shared[0].curve == a2.Segment((0, 2), (4, 2))
    assert {shared[0].face.id, shared[0].twin.face.id} == {f.id for f in faces}


def test_shared_edges_empty_cases(split_square):
    assert regions.shared_edges([]) == []
    assert regions.shared_edges(regions.bounded_faces(split_square)[:1]) == []


def test_shared_edges_disjoint_faces():
    arr = a2.Arrangement("segment")
    arr.insert(square(0, 0, 1, 1) + square(5, 0, 6, 1))
    assert regions.shared_edges(regions.bounded_faces(arr)) == []


def test_shared_edges_two_edges_between_one_pair():
    # a full circle meets the unbounded face along BOTH of its x-monotone arcs
    arr = a2.Arrangement("circle_segment")
    arr.insert(a2.Circle((0, 0), 2))
    disk = regions.bounded_faces(arr)[0]
    assert len(regions.shared_edges([disk, arr.unbounded_face])) == 2


def test_shared_edges_rejects_mixed_arrangements(split_square):
    other = a2.Arrangement("segment")
    other.insert(square())
    faces = regions.bounded_faces(split_square) + other.bounded_faces()
    with pytest.raises(a2.InvalidHandleError):
        regions.shared_edges(faces)


# ---------------------------------------------------------------------------
# merge_faces
# ---------------------------------------------------------------------------

def test_merge_faces_removes_the_chord(split_square):
    merged = regions.merge_faces(split_square, regions.bounded_faces(split_square))
    assert len(merged) == 1
    assert split_square.is_valid()
    assert split_square.number_of_edges == 4
    assert split_square.number_of_vertices == 4     # the chord endpoints are gone too
    assert regions.face_area(merged[0]) == Fraction(16)


def test_merge_faces_keeps_vertices_when_asked(split_square):
    merged = regions.merge_faces(split_square, regions.bounded_faces(split_square),
                                 remove_vertices=False)
    assert len(merged) == 1
    assert split_square.is_valid()
    assert split_square.number_of_vertices == 6     # the two chord endpoints survive
    assert split_square.number_of_edges == 6


def test_merge_faces_disconnected_group_stays_split():
    arr = a2.Arrangement("segment")
    arr.insert(square(0, 0, 1, 1) + square(5, 0, 6, 1))
    merged = regions.merge_faces(arr, regions.bounded_faces(arr))
    assert len(merged) == 2
    assert arr.number_of_edges == 8


def test_merge_faces_with_the_unbounded_face_empties_the_arrangement():
    arr = a2.Arrangement("segment")
    arr.insert(square())
    merged = regions.merge_faces(arr, [regions.bounded_faces(arr)[0], arr.unbounded_face])
    assert len(merged) == 1
    assert merged[0].is_unbounded
    assert arr.number_of_edges == 0
    assert arr.number_of_vertices == 0
    assert arr.is_valid()


def test_merge_faces_removes_both_edges_of_a_pair():
    arr = a2.Arrangement("circle_segment")
    arr.insert(a2.Circle((0, 0), 2))
    disk = regions.bounded_faces(arr)[0]
    merged = regions.merge_faces(arr, [disk, arr.unbounded_face])
    assert len(merged) == 1
    assert arr.number_of_edges == 0           # both arcs are gone, nothing dangles
    assert arr.is_valid()


def test_merge_faces_three_faces_in_a_row():
    arr = a2.Arrangement("segment")
    arr.insert(square(0, 0, 3, 1) + [a2.Segment((1, 0), (1, 1)), a2.Segment((2, 0), (2, 1))])
    faces = regions.bounded_faces(arr)
    assert len(faces) == 3
    merged = regions.merge_faces(arr, faces)
    assert len(merged) == 1
    assert regions.face_area(merged[0]) == Fraction(3)
    assert arr.is_valid()


def test_merge_faces_partial_group(square_with_hole):
    """Merging the annulus with the disk inside it fills the hole."""
    faces = regions.bounded_faces(square_with_hole)
    merged = regions.merge_faces(square_with_hole, faces)
    assert len(merged) == 1
    assert merged[0].number_of_inner_ccbs == 0
    assert regions.face_area(merged[0]) == Fraction(16)
    assert square_with_hole.is_valid()


def test_merge_faces_single_face_is_a_no_op(split_square):
    faces = regions.bounded_faces(split_square)
    assert regions.merge_faces(split_square, faces[:1]) == faces[:1]
    assert split_square.number_of_edges == 7          # nothing was removed


def test_merge_faces_invalidates_the_absorbed_handle(split_square):
    faces = regions.bounded_faces(split_square)
    merged = regions.merge_faces(split_square, faces)
    dead = [f for f in faces if f.id != merged[0].id]
    assert len(dead) == 1
    assert not dead[0].is_valid
    with pytest.raises(a2.InvalidHandleError):
        regions.merge_faces(split_square, [dead[0], merged[0]])


def test_merge_faces_rejects_foreign_face(split_square):
    other = a2.Arrangement("segment")
    other.insert(square())
    with pytest.raises(a2.InvalidHandleError):
        regions.merge_faces(split_square, other.bounded_faces())


@pytest.mark.parametrize("kind", ("segment", "polyline", "circle_segment", "conic"))
def test_merge_faces_every_planar_kind(kind):
    arr = a2.Arrangement(kind)
    if kind == "polyline":
        arr.insert([a2.Polyline([(0, 0), (4, 0), (4, 4), (0, 4), (0, 0)]),
                    a2.Polyline([(0, 2), (4, 2)])])
    elif kind == "segment":
        arr.insert(square() + [a2.Segment((0, 2), (4, 2))])
    elif kind == "circle_segment":
        arr.insert([a2.CircleSegment.circle((0, 0), 2),
                    a2.CircleSegment.segment((-2, 0), (2, 0))])
    else:
        arr.insert([a2.ConicArc.circle((0, 0), 2), a2.ConicArc.segment((-2, 0), (2, 0))])
    faces = regions.bounded_faces(arr)
    assert len(faces) == 2
    merged = regions.merge_faces(arr, faces)
    assert len(merged) == 1
    assert arr.is_valid()


# ---------------------------------------------------------------------------
# split_face
# ---------------------------------------------------------------------------

def test_split_face_basic():
    arr = a2.Arrangement("segment")
    arr.insert(square())
    face = regions.bounded_faces(arr)[0]
    pieces = regions.split_face(arr, face, a2.Segment((0, 2), (4, 2)))
    assert len(pieces) == 2
    assert sorted(regions.face_area(f) for f in pieces) == [Fraction(8), Fraction(8)]
    assert arr.is_valid()


def test_split_face_accepts_several_curves():
    arr = a2.Arrangement("segment")
    arr.insert(square())
    face = regions.bounded_faces(arr)[0]
    pieces = regions.split_face(arr, face, [a2.Segment((0, 2), (4, 2)),
                                            a2.Segment((2, 0), (2, 4))])
    assert len(pieces) == 4
    assert sorted(regions.face_area(f) for f in pieces) == [Fraction(4)] * 4


def test_split_face_curve_that_does_not_separate():
    arr = a2.Arrangement("segment")
    arr.insert(square())
    face = regions.bounded_faces(arr)[0]
    pieces = regions.split_face(arr, face, a2.Segment((0, 2), (2, 2)))
    assert pieces == [face]                   # an antenna splits nothing
    assert arr.number_of_edges == 6


def test_split_face_rejects_a_curve_leaving_the_face():
    arr = a2.Arrangement("segment")
    arr.insert(square() + [a2.Segment((0, 2), (4, 2))])
    face = regions.face_containing(arr, (2, 1))
    edges_before = arr.number_of_edges
    with pytest.raises(ValueError):
        regions.split_face(arr, face, a2.Segment((2, 1), (2, 3)))
    assert arr.number_of_edges == edges_before        # nothing was inserted


def test_split_face_rejects_empty_iterable(split_square):
    face = regions.bounded_faces(split_square)[0]
    with pytest.raises(ValueError):
        regions.split_face(split_square, face, [])


def test_split_face_rejects_foreign_face(split_square):
    other = a2.Arrangement("segment")
    other.insert(square())
    with pytest.raises(a2.InvalidHandleError):
        regions.split_face(split_square, other.bounded_faces()[0],
                           a2.Segment((1, 1), (2, 2)))


def test_split_face_inside_a_hole():
    arr = a2.Arrangement("segment")
    arr.insert(square())
    face = regions.bounded_faces(arr)[0]
    pieces = regions.split_face(arr, face, square(1, 1, 3, 3))
    assert len(pieces) == 2
    assert sorted(regions.face_area(f) for f in pieces) == [Fraction(4), Fraction(12)]
    assert arr.is_valid()


def test_split_face_circular_arc():
    arr = a2.Arrangement("circle_segment")
    arr.insert(a2.CircleSegment.circle((0, 0), 4))
    disk = regions.bounded_faces(arr)[0]
    pieces = regions.split_face(arr, disk, a2.CircleSegment.circle((0, 0), 2))
    assert len(pieces) == 2
    assert arr.is_valid()


# ---------------------------------------------------------------------------
# connected components
# ---------------------------------------------------------------------------

def test_connected_components_one_component(split_square):
    comps = regions.connected_components(split_square)
    assert len(comps) == 1
    assert len(comps[0].vertices) == split_square.number_of_vertices
    assert len(comps[0].edges) == split_square.number_of_edges
    assert regions.number_of_connected_components(split_square) == 1


def test_connected_components_two_components():
    arr = a2.Arrangement("segment")
    arr.insert(square(0, 0, 1, 1) + square(5, 0, 6, 1))
    comps = regions.connected_components(arr)
    assert [len(c.vertices) for c in comps] == [4, 4]
    assert [len(c.edges) for c in comps] == [4, 4]
    assert regions.number_of_connected_components(arr) == 2


def test_connected_components_isolated_vertex():
    arr = a2.Arrangement("segment")
    arr.insert(a2.Segment((0, 0), (1, 0)))
    v = arr.insert_point((5, 5))
    comps = regions.connected_components(arr)
    assert len(comps) == 2
    lonely = [c for c in comps if not c.edges]
    assert len(lonely) == 1
    assert lonely[0].vertices == [v]


def test_connected_components_euler_relation():
    """``V - E + F == 1 + C`` for a bounded planar arrangement."""
    arr = a2.Arrangement("segment")
    arr.insert(square(0, 0, 4, 4) + [a2.Segment((0, 2), (4, 2))] + square(8, 0, 9, 1))
    c = regions.number_of_connected_components(arr)
    assert (arr.number_of_vertices - arr.number_of_edges + arr.number_of_faces
            == 1 + c)


def test_connected_components_vertices_at_infinity():
    arr = a2.Arrangement("linear")
    arr.insert([a2.Line((0, 0), (1, 0)), a2.Line((0, 3), (1, 3))])
    comps = regions.connected_components(arr)
    assert len(comps) == 2
    # the two lines have no concrete vertex at all
    assert [len(c.vertices) for c in comps] == [0, 0]
    assert [len(c.edges) for c in comps] == [1, 1]
    with_inf = regions.connected_components(arr, include_vertices_at_infinity=True)
    assert [len(c.vertices) for c in with_inf] == [2, 2]
    assert all(v.is_at_open_boundary for c in with_inf for v in c.vertices)


def test_connected_components_empty_arrangement():
    arr = a2.Arrangement("segment")
    assert regions.connected_components(arr) == []
    assert regions.number_of_connected_components(arr) == 0


@pytest.mark.parametrize("kind", ALL_KINDS)
def test_connected_components_every_kind(kind):
    arr = arrangement_for(kind)
    comps = regions.connected_components(arr)
    assert sum(len(c.edges) for c in comps) == arr.number_of_edges
    assert sum(len(c.vertices) for c in comps) == arr.number_of_vertices


# ---------------------------------------------------------------------------
# extract_regions
# ---------------------------------------------------------------------------

def test_extract_regions_groups_adjacent_faces(split_square):
    groups = regions.extract_regions(split_square)
    assert len(groups) == 1
    assert len(groups[0]) == 2


def test_extract_regions_by_data():
    arr = a2.Arrangement("segment")
    arr.insert(square(0, 0, 3, 1)
               + [a2.Segment((1, 0), (1, 1)), a2.Segment((2, 0), (2, 1))])
    faces = sorted(regions.bounded_faces(arr), key=lambda f: f.polygon().outer.bbox()[0])
    faces[0].data = "keep"
    faces[1].data = "drop"
    faces[2].data = "keep"
    groups = regions.extract_regions(arr, lambda f: f.data == "keep")
    assert [len(g) for g in groups] == [1, 1]          # not adjacent to each other
    groups = regions.extract_regions(arr, lambda f: f.data is not None)
    assert [len(g) for g in groups] == [3]             # all three touch


def test_extract_regions_no_match(split_square):
    assert regions.extract_regions(split_square, lambda f: False) == []


def test_extract_regions_explicit_faces(split_square):
    faces = regions.bounded_faces(split_square)
    groups = regions.extract_regions(split_square, faces=[faces[0]])
    assert groups == [[faces[0]]]


def test_extract_regions_can_include_the_unbounded_face(split_square):
    groups = regions.extract_regions(split_square, faces=split_square.faces())
    assert len(groups) == 1
    assert len(groups[0]) == 3


def test_extract_regions_then_merge():
    arr = a2.Arrangement("segment")
    arr.insert(square(0, 0, 4, 4)
               + [a2.Segment((0, 2), (4, 2))]
               + square(8, 0, 10, 2))
    for f in regions.bounded_faces(arr):
        f.data = "yes"
    groups = regions.extract_regions(arr, lambda f: f.data == "yes")
    assert [len(g) for g in groups] == [2, 1]
    merged = [regions.merge_faces(arr, g) for g in groups]
    assert [len(m) for m in merged] == [1, 1]
    assert arr.is_valid()


# ---------------------------------------------------------------------------
# Boolean helpers
# ---------------------------------------------------------------------------

def test_supports_boolean_ops():
    assert regions.supports_boolean_ops("segment")
    assert regions.supports_boolean_ops(a2.Kind.CONIC)
    assert regions.supports_boolean_ops(a2.Segment((0, 0), (1, 1)))
    assert not regions.supports_boolean_ops("polyline")
    assert not regions.supports_boolean_ops("linear")
    assert not regions.supports_boolean_ops("sphere")
    for kind in BSO_KINDS:
        assert regions.supports_boolean_ops(kind)
        assert regions.supports_boolean_ops(a2.Arrangement(kind))


def test_union_outline_drops_interior_edges(split_square):
    outline = regions.union_outline(split_square)
    assert isinstance(outline, a2.PolygonSet)
    assert outline.number_of_polygons_with_holes == 1
    pwh = outline.polygons_with_holes()[0]
    # the chord is interior and disappears; the two vertices it left on the left and
    # right sides stay, so the outline is the square as six collinear-in-pairs edges
    assert len(pwh.outer) == 6
    assert pwh.outer.area() == Fraction(16)
    assert pwh.holes == ()
    assert outline.oriented_side((2, 2)) == 1
    assert outline.oriented_side((5, 5)) == -1


def test_union_outline_keeps_an_unfilled_hole(square_with_hole):
    annulus = [f for f in regions.bounded_faces(square_with_hole)
               if f.number_of_inner_ccbs == 1]
    outline = regions.union_outline(square_with_hole, annulus)
    pwh = outline.polygons_with_holes()[0]
    assert len(pwh.holes) == 1
    assert outline.oriented_side((2, 2)) == -1        # the hole is not part of the set
    filled = regions.union_outline(square_with_hole)
    assert filled.polygons_with_holes()[0].holes == ()
    assert filled.oriented_side((2, 2)) == 1


def test_union_outline_disjoint_faces():
    arr = a2.Arrangement("segment")
    arr.insert(square(0, 0, 1, 1) + square(5, 0, 6, 1))
    assert regions.union_outline(arr).number_of_polygons_with_holes == 2


def test_union_outline_circle_segment():
    arr = a2.Arrangement("circle_segment")
    arr.insert([a2.Circle((0, 0), 2), a2.Circle((2, 0), 2)])
    outline = regions.union_outline(arr)
    assert outline.number_of_polygons_with_holes == 1
    assert outline.oriented_side((0, 0)) == 1
    assert outline.oriented_side((10, 10)) == -1


def test_union_outline_does_not_modify_the_arrangement(split_square):
    before = (split_square.number_of_vertices, split_square.number_of_edges,
              split_square.number_of_faces)
    regions.union_outline(split_square)
    after = (split_square.number_of_vertices, split_square.number_of_edges,
             split_square.number_of_faces)
    assert before == after


@pytest.mark.parametrize("kind", ("linear", "polyline", "sphere"))
def test_union_outline_unsupported_kind(kind):
    arr = arrangement_for(kind)
    with pytest.raises(a2.UnsupportedError):
        regions.union_outline(arr)


def test_union_outline_of_an_unbounded_face_is_unsupported(split_square):
    with pytest.raises(a2.UnsupportedError):
        regions.union_outline(split_square, [split_square.unbounded_face])


# ---------------------------------------------------------------------------
# module surface
# ---------------------------------------------------------------------------

def test_lazy_submodule_attribute():
    import arrangement_2d

    assert arrangement_2d.regions is regions
    assert "regions" in dir(arrangement_2d)


def test_all_names_exist():
    for name in regions.__all__:
        assert hasattr(regions, name), name


def test_every_public_name_is_documented():
    for name in regions.__all__:
        obj = getattr(regions, name)
        assert obj.__doc__, name
