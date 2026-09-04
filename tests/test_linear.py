"""The ``linear`` kind: segments, rays and lines under the *unbounded* planar topology.

``Arr_linear_traits_2<Epeck>`` is the only planar kind whose arrangement lives on an
unbounded surface, so it is the only one that has

* curves with ends at infinity (``LinearCurve.ray`` / ``.line``),
* **vertices at infinity** (``Arrangement.number_of_vertices_at_infinity``),
* **fictitious halfedges** -- the artificial bounding rectangle CGAL wraps around the
  arrangement.  They are invisible to ``halfedges()`` / ``edges()`` and are only reachable
  by walking a CCB; they carry no curve,
* a **fictitious face** outside that rectangle (``Arrangement.fictitious_face``),
* several **unbounded faces** (``unbounded_faces()``; ``unbounded_face`` is only *one* of
  them and is documented to drift).

Every expected number below is derived by hand in the comment next to it.  The
configurations used throughout:

``AXES``      the two coordinate axes                     -> V=1 V@inf=4 E=4 F=4 (all unbounded)
``TRIANGLE``  y=0, x=0 and x+y=4                          -> V=3 V@inf=6 E=9 F=7 (6 unbounded)

Notes that shaped the expectations live in ``docs/dev/STAGE2_NOTES.md`` (section
``kind_linear.cpp``) and ``docs/dev/CGAL_TRAPS_CHECKLIST.md``.
"""

from __future__ import annotations

from fractions import Fraction

import pytest

a2 = pytest.importorskip("arrangement_2d")


# ---------------------------------------------------------------------------
# builders (kept as functions, not fixtures, so every test is independent)
# ---------------------------------------------------------------------------
def axes() -> "a2.Arrangement":
    """The two coordinate axes: y = 0 and x = 0."""
    arr = a2.Arrangement("linear")
    arr.insert([a2.Line((0, 0), (1, 0)), a2.Line((0, 0), (0, 1))])
    return arr


def triangle_lines() -> "a2.Arrangement":
    """y = 0, x = 0 and x + y = 4; the only bounded face is the triangle (0,0)(4,0)(0,4)."""
    arr = a2.Arrangement("linear")
    arr.insert([
        a2.Line((0, 0), (1, 0)),                         # y = 0
        a2.Line((0, 0), (0, 1)),                         # x = 0
        a2.LinearCurve.line_from_coefficients(1, 1, -4),  # x + y - 4 = 0
    ])
    return arr


def frame_size(arr) -> int:
    """Number of fictitious halfedges on the fictitious face's single inner CCB."""
    return sum(len(ccb) for ccb in arr.fictitious_face.inner_ccbs())


# ===========================================================================
# 1. curve construction and the which / is_* accessors
# ===========================================================================
def test_segment_construction():
    s = a2.LinearCurve.segment((0, 0), (4, 3))
    assert s.kind == a2.Kind.LINEAR
    assert s.which == "segment"
    assert (s.is_segment, s.is_ray, s.is_line) == (True, False, False)
    assert s.has_source and s.has_target
    assert s.source == a2.Point(0, 0, kind="linear")
    assert s.target == a2.Point(4, 3, kind="linear")
    assert s.is_bounded and s.is_x_monotone
    assert repr(s) == "Segment((0, 0), (4, 3))"


def test_ray_construction_from_two_points():
    r = a2.LinearCurve.ray((1, 1), (3, 1))
    assert r.which == "ray"
    assert (r.is_segment, r.is_ray, r.is_line) == (False, True, False)
    # a ray has a source but no target: its second end runs to infinity
    assert r.has_source and not r.has_target
    assert r.source == a2.Point(1, 1, kind="linear")
    assert not r.is_bounded
    assert repr(r) == "Ray((1, 1), direction=(1, 0))"


def test_ray_from_direction():
    r = a2.LinearCurve.ray_from_direction((0, 0), 0, -1)
    assert r.is_ray and r.is_vertical
    assert r.source == a2.Point(0, 0, kind="linear")
    # a vertical ray pointing down is stored right-to-left (its min end is at -inf)
    assert not r.is_directed_right
    assert r.direction == (Fraction(0), Fraction(-1))


def test_line_construction_from_two_points():
    ln = a2.LinearCurve.line((0, 0), (1, 1))
    assert ln.which == "line"
    assert (ln.is_segment, ln.is_ray, ln.is_line) == (False, False, True)
    assert not ln.has_source and not ln.has_target
    assert not ln.is_bounded and ln.is_x_monotone


def test_line_from_coefficients_is_stored_verbatim():
    # a*x + b*y + c = 0 with (a, b, c) = (1, 0, -3)  <=>  the vertical line x = 3
    ln = a2.LinearCurve.line_from_coefficients(1, 0, -3)
    assert ln.is_line and ln.is_vertical
    assert ln.supporting_line == (Fraction(1), Fraction(0), Fraction(-3))
    assert repr(ln) == "Line(a=1, b=0, c=-3)"


def test_module_level_aliases():
    assert a2.Line((0, 0), (1, 1)) == a2.LinearCurve.line((0, 0), (1, 1))
    assert a2.Ray((0, 0), (1, 1)) == a2.LinearCurve.ray((0, 0), (1, 1))
    assert a2.line_from_coefficients(1, 0, -3) == \
        a2.LinearCurve.line_from_coefficients(1, 0, -3)
    for c in (a2.Line((0, 0), (1, 1)), a2.Ray((0, 0), (1, 1))):
        assert isinstance(c, a2.LinearCurve) and c.kind == a2.Kind.LINEAR


def test_which_partitions_the_three_shapes():
    kinds = {c.which for c in (a2.LinearCurve.segment((0, 0), (1, 1)),
                               a2.Ray((0, 0), (1, 1)),
                               a2.Line((0, 0), (1, 1)))}
    assert kinds == {"segment", "ray", "line"}
    for c in (a2.LinearCurve.segment((0, 0), (1, 1)), a2.Ray((0, 0), (1, 1)),
              a2.Line((0, 0), (1, 1))):
        # exactly one of the three predicates is true
        assert [c.is_segment, c.is_ray, c.is_line].count(True) == 1


def test_flipped_ray_produced_by_split_has_a_target_but_no_source():
    """``Split_2`` on a line yields a ray stored "backwards" (STAGE2_NOTES, kind_linear)."""
    left, right = a2.Line((0, 0), (1, 1)).split((0, 0))
    assert left.is_ray and right.is_ray
    # the left half runs from -inf up to (0, 0): no source, a target
    assert not left.has_source and left.has_target
    assert left.target == a2.Point(0, 0, kind="linear")
    assert repr(left) == "Ray(target=(0, 0), direction=(1, 1))"
    # the right half is an ordinary ray starting at (0, 0)
    assert right.has_source and not right.has_target
    assert right.source == a2.Point(0, 0, kind="linear")


# ===========================================================================
# 2. supporting_line / direction
# ===========================================================================
def test_supporting_line_of_a_segment():
    # CGAL's line_from_pointsC2(p, q) = (py - qy, qx - px, -a*px - b*py):
    # p=(0,0) q=(4,3) -> a = 0-3 = -3, b = 4-0 = 4, c = 0.
    s = a2.LinearCurve.segment((0, 0), (4, 3))
    assert s.supporting_line == (Fraction(-3), Fraction(4), Fraction(0))


def test_supporting_line_of_a_horizontal_ray_is_normalised():
    # a horizontal support is normalised by CGAL to y - y0 = 0, i.e. (0, 1, -y0).
    r = a2.LinearCurve.ray((1, 1), (3, 1))
    assert r.supporting_line == (Fraction(0), Fraction(1), Fraction(-1))


def test_supporting_line_of_a_line_through_two_points():
    # p=(0,0) q=(1,1) -> a = 0-1 = -1, b = 1-0 = 1, c = 0  (i.e. -x + y = 0)
    assert a2.Line((0, 0), (1, 1)).supporting_line == \
        (Fraction(-1), Fraction(1), Fraction(0))


def test_direction_is_target_minus_source_for_a_segment():
    assert a2.LinearCurve.segment((0, 0), (4, 3)).direction == (Fraction(4), Fraction(3))
    # NOT normalised (documented open issue in STAGE2_NOTES): (8,6) stays (8,6)
    assert a2.LinearCurve.segment((0, 0), (8, 6)).direction == (Fraction(8), Fraction(6))


def test_direction_of_lines_follows_the_stored_traversal():
    # for a line the direction is the support's to_vector() == (b, -a), sign-corrected
    # to the stored traversal.  y = x has (a,b) = (-1,1) -> (1, 1), directed right.
    ln = a2.Line((0, 0), (1, 1))
    assert ln.direction == (Fraction(1), Fraction(1)) and ln.is_directed_right
    # x = 3 has (a,b) = (1,0) -> (0, -1): a vertical line is stored top-to-bottom.
    vl = a2.LinearCurve.line_from_coefficients(1, 0, -3)
    assert vl.direction == (Fraction(0), Fraction(-1)) and not vl.is_directed_right


# ===========================================================================
# 3. construction errors
# ===========================================================================
@pytest.mark.parametrize("build, needle", [
    (lambda: a2.LinearCurve.segment((0, 0), (0, 0)), "endpoints must be distinct"),
    (lambda: a2.LinearCurve.ray((1, 1), (1, 1)), "must be distinct"),
    (lambda: a2.LinearCurve.line((1, 1), (1, 1)), "two points must be distinct"),
    (lambda: a2.LinearCurve.ray_from_direction((0, 0), 0, 0), "zero vector"),
    (lambda: a2.LinearCurve.line_from_coefficients(0, 0, 1), "not both be zero"),
])
def test_degenerate_constructions_are_rejected(build, needle):
    with pytest.raises(ValueError) as ei:
        build()
    assert needle in str(ei.value)


def test_linear_curve_has_no_public_constructor():
    with pytest.raises(TypeError):
        a2.LinearCurve((0, 0), (1, 1))


# ===========================================================================
# 4. curve equality / endpoint accessors at infinity / parameter space
# ===========================================================================
def test_curve_equality_is_geometric_and_direction_insensitive():
    assert a2.Line((0, 0), (1, 1)) == a2.Line((2, 2), (5, 5))     # same support
    assert a2.Line((0, 0), (1, 1)) == a2.Line((1, 1), (0, 0))     # opposite traversal
    assert a2.Line((0, 0), (1, 1)) != a2.Line((0, 0), (1, -1))
    with pytest.raises(TypeError):
        hash(a2.Line((0, 0), (1, 1)))                             # equality is geometric


@pytest.mark.parametrize("attr", ["source", "target", "left", "right",
                                  "min_vertex", "max_vertex"])
def test_every_endpoint_of_a_line_is_at_infinity(attr):
    with pytest.raises(a2.UnsupportedError):
        getattr(a2.Line((0, 0), (1, 1)), attr)


def test_ray_endpoints_only_the_finite_one_is_available():
    r = a2.LinearCurve.ray((1, 1), (3, 1))     # to the right: min = (1,1), max = +inf
    assert r.source == a2.Point(1, 1, kind="linear")
    assert r.left == a2.Point(1, 1, kind="linear")
    assert r.min_vertex == a2.Point(1, 1, kind="linear")
    for attr in ("target", "right", "max_vertex"):
        with pytest.raises(a2.UnsupportedError):
            getattr(r, attr)


def test_parameter_space_of_curve_ends():
    # Arr_parameter_space: 0 LEFT, 1 RIGHT, 2 BOTTOM, 3 TOP, 4 INTERIOR.
    # curve_end: 0 = min end, 1 = max end.
    diag = a2.Line((0, 0), (1, 1))
    # a diagonal line leaves through a corner: it is on a left/right AND a bottom/top
    # boundary at once (CGAL_TRAPS_CHECKLIST, "Arrangement core").
    assert (diag.parameter_space_in_x(0), diag.parameter_space_in_y(0)) == (0, 2)
    assert (diag.parameter_space_in_x(1), diag.parameter_space_in_y(1)) == (1, 3)

    vert = a2.LinearCurve.line_from_coefficients(1, 0, -3)     # x = 3
    assert (vert.parameter_space_in_x(0), vert.parameter_space_in_y(0)) == (4, 2)
    assert (vert.parameter_space_in_x(1), vert.parameter_space_in_y(1)) == (4, 3)

    horiz_ray = a2.LinearCurve.ray((1, 1), (3, 1))
    assert (horiz_ray.parameter_space_in_x(0), horiz_ray.parameter_space_in_y(0)) == (4, 4)
    assert (horiz_ray.parameter_space_in_x(1), horiz_ray.parameter_space_in_y(1)) == (1, 4)

    seg = a2.LinearCurve.segment((0, 0), (4, 3))
    assert seg.parameter_space_in_x(0) == seg.parameter_space_in_y(1) == 4


def test_bbox_is_infinite_exactly_where_the_curve_is():
    assert a2.LinearCurve.segment((0, 0), (4, 3)).bbox() == (0.0, 0.0, 4.0, 3.0)
    assert a2.LinearCurve.ray((1, 1), (3, 1)).bbox() == (1.0, 1.0, float("inf"), 1.0)
    assert a2.Line((0, 0), (1, 1)).bbox() == \
        (float("-inf"), float("-inf"), float("inf"), float("inf"))


# ===========================================================================
# 5. curve algebra: opposite / split / trim / merge / intersect
# ===========================================================================
def test_opposite_of_a_segment_and_of_a_line():
    s = a2.LinearCurve.segment((0, 0), (4, 3))
    assert s.opposite() == s and s.opposite().source == a2.Point(4, 3, kind="linear")
    ln = a2.Line((0, 0), (1, 1))            # (a,b,c) = (-1,1,0)
    assert ln.opposite().supporting_line == (Fraction(1), Fraction(-1), Fraction(0))
    assert ln.opposite().is_directed_right is not ln.is_directed_right


def test_a_ray_cannot_be_reversed():
    """``Arr_linear_object_2`` always stores a ray from its source, so there is no
    representation for the reversed one (STAGE2_NOTES: LinearOps::construct_opposite)."""
    with pytest.raises(a2.UnsupportedError) as ei:
        a2.LinearCurve.ray((1, 1), (3, 1)).opposite()
    assert "ray cannot be reversed" in str(ei.value)


def test_split_a_line_gives_two_rays():
    left, right = a2.Line((0, 0), (1, 1)).split((0, 0))
    assert left.which == right.which == "ray"
    assert left.target == right.source == a2.Point(0, 0, kind="linear")
    assert left.can_merge(right) and right.can_merge(left)
    assert left.merge(right) == a2.Line((0, 0), (1, 1))


def test_trim_a_line_to_a_segment():
    t = a2.Line((0, 0), (1, 1)).trim((1, 1), (3, 3))
    assert t.which == "segment"
    assert t.min_vertex == a2.Point(1, 1, kind="linear")
    assert t.max_vertex == a2.Point(3, 3, kind="linear")


def test_intersect_transversal_parallel_and_overlap():
    # y = x meets y = 2 in (2, 2) with multiplicity 1
    hit = a2.Line((0, 0), (1, 1)).intersect(a2.LinearCurve.line_from_coefficients(0, 1, -2))
    assert hit == [(a2.Point(2, 2, kind="linear"), 1)]
    # parallel lines: no intersection at all
    assert a2.Line((0, 0), (1, 0)).intersect(a2.Line((0, 1), (1, 1))) == []
    # a collinear ray overlaps the line in that very ray (a Curve, not a (point, mult))
    over = a2.Line((0, 0), (1, 0)).intersect(a2.LinearCurve.ray((2, 0), (5, 0)))
    assert len(over) == 1 and isinstance(over[0], a2.LinearCurve)
    assert over[0].is_ray and over[0].source == a2.Point(2, 0, kind="linear")


def test_compare_y_at_x_and_is_in_x_range():
    ln = a2.Line((0, 0), (1, 1))                 # y = x
    assert ln.compare_y_at_x((0, 1)) == 1        # the point is above
    assert ln.compare_y_at_x((0, 0)) == 0
    assert ln.compare_y_at_x((0, -1)) == -1
    assert ln.is_in_x_range((100, 0))            # a line spans every x
    assert not a2.LinearCurve.ray((1, 1), (3, 1)).is_in_x_range((0, 0))


def test_to_kind_conversions():
    seg = a2.LinearCurve.segment((0, 0), (4, 3))
    assert seg.to_kind("segment").kind == a2.Kind.SEGMENT
    with pytest.raises(a2.NotRepresentableError):
        a2.LinearCurve.ray((0, 0), (1, 0)).to_kind("segment")
    with pytest.raises(a2.NotRepresentableError):
        a2.Line((0, 0), (1, 1)).to_kind("segment")
    # points move freely in both directions
    assert a2.Point(1, 2).to_kind("linear").kind == a2.Kind.LINEAR
    assert a2.Point(1, 2, kind="linear").to_kind("segment").kind == a2.Kind.SEGMENT


# ===========================================================================
# 6. approximate() and the clip box
# ===========================================================================
def test_approximate_a_bounded_segment_ignores_the_clip_box():
    s = a2.LinearCurve.segment((0, 0), (4, 3))
    assert s.approximate(1e-3) == [(0.0, 0.0), (4.0, 3.0)]
    assert s.approximate(1e-3, bbox=(0, 0, 1, 1)) == [(0.0, 0.0), (4.0, 3.0)]
    assert s.approximate_length() == 5.0            # the 3-4-5 triangle, exactly


def test_approximate_an_unbounded_curve_requires_a_clip_box():
    with pytest.raises(ValueError) as ei:
        a2.Line((0, 0), (1, 1)).approximate(1e-3)
    assert "clipping box" in str(ei.value)
    with pytest.raises(ValueError):
        a2.LinearCurve.ray((1, 1), (3, 1)).approximate(1e-3)


def test_approximate_clips_a_line_exactly_to_the_box_corners():
    # y = x clipped to [-2,2]^2 meets the box in (-2,-2) and (2,2)
    assert a2.Line((0, 0), (1, 1)).approximate(1e-3, bbox=(-2, -2, 2, 2)) == \
        [(-2.0, -2.0), (2.0, 2.0)]


def test_approximate_a_ray_starts_at_its_source():
    r = a2.LinearCurve.ray((1, 1), (5, 1))
    assert r.approximate(1e-3, bbox=(-5, -5, 5, 5)) == [(1.0, 1.0), (5.0, 1.0)]
    # a box the ray never reaches yields an empty polyline (STAGE2_NOTES open issue)
    assert r.approximate(1e-3, bbox=(-5, -5, -1, -1)) == []


def test_approximate_follows_the_stored_direction_of_a_vertical_line():
    # x = 3 is stored top-to-bottom (direction (0,-1)), so the chord comes out that way
    vl = a2.LinearCurve.line_from_coefficients(1, 0, -3)
    assert vl.approximate(1e-3, bbox=(-5, -5, 5, 5)) == [(3.0, 5.0), (3.0, -5.0)]


@pytest.mark.parametrize("tol", [0.0, -1.0])
def test_approximate_rejects_a_non_positive_tolerance(tol):
    with pytest.raises(ValueError) as ei:
        a2.Line((0, 0), (1, 1)).approximate(tol, bbox=(-1, -1, 1, 1))
    assert "tolerance must be a positive number" in str(ei.value)


# ===========================================================================
# 7. arrangement counts (every number hand-derived)
# ===========================================================================
def test_the_linear_kind_is_the_unbounded_planar_one():
    arr = a2.Arrangement("linear")
    assert arr.kind == a2.Kind.LINEAR and arr.is_unbounded_kind
    assert not a2.Arrangement("segment").is_unbounded_kind


def test_empty_arrangement_already_has_the_fictitious_frame():
    arr = a2.Arrangement("linear")
    # nothing inserted: no concrete vertex/edge, the whole plane is one unbounded face
    assert (arr.number_of_vertices, arr.number_of_edges) == (0, 0)
    assert arr.number_of_vertices_at_infinity == 0
    assert (arr.number_of_faces, arr.number_of_unbounded_faces) == (1, 1)
    assert arr.is_empty and arr.is_valid()
    # the frame is a rectangle: 4 corner vertices, 4 fictitious edges
    assert frame_size(arr) == 4
    assert arr.fictitious_face.is_fictitious and arr.fictitious_face.is_unbounded


def test_single_line_counts():
    arr = a2.Arrangement("linear")
    arr.insert(a2.Line((0, 0), (1, 1)))
    # a lone line has no concrete vertex; its 2 ends become vertices at infinity;
    # 1 edge; it cuts the plane into 2 half planes, both unbounded.
    assert (arr.number_of_vertices, arr.number_of_vertices_at_infinity) == (0, 2)
    assert (arr.number_of_edges, arr.number_of_halfedges) == (1, 2)
    assert (arr.number_of_faces, arr.number_of_unbounded_faces) == (2, 2)
    assert arr.bounded_faces() == []
    assert frame_size(arr) == 4 + 2          # 4 corners + the 2 ends split the frame
    assert arr.is_valid()


def test_single_ray_counts():
    arr = a2.Arrangement("linear")
    arr.insert(a2.LinearCurve.ray((0, 0), (1, 0)))
    # the source (0,0) is a concrete vertex, the far end is at infinity;
    # a single ray does not separate the plane: still one face.
    assert (arr.number_of_vertices, arr.number_of_vertices_at_infinity) == (1, 1)
    assert (arr.number_of_edges, arr.number_of_faces) == (1, 1)
    assert arr.number_of_unbounded_faces == 1
    assert frame_size(arr) == 4 + 1
    assert arr.is_valid()


def test_single_segment_counts():
    arr = a2.Arrangement("linear")
    arr.insert(a2.LinearCurve.segment((0, 0), (4, 0)))
    # a bounded curve in an unbounded topology: 2 concrete vertices, nothing at infinity,
    # the frame is untouched (4 fictitious edges) and the plane stays one face.
    assert (arr.number_of_vertices, arr.number_of_vertices_at_infinity) == (2, 0)
    assert (arr.number_of_edges, arr.number_of_faces) == (1, 1)
    assert frame_size(arr) == 4
    assert arr.is_valid()


def test_two_crossing_lines_counts():
    arr = axes()
    # the axes meet in (0,0): 1 concrete vertex, 4 ends at infinity, each line split
    # into 2 rays -> 4 edges / 8 halfedges, and 4 quadrants, all unbounded.
    assert (arr.number_of_vertices, arr.number_of_vertices_at_infinity) == (1, 4)
    assert (arr.number_of_edges, arr.number_of_halfedges) == (4, 8)
    assert (arr.number_of_faces, arr.number_of_unbounded_faces) == (4, 4)
    assert arr.number_of_curves == 2 and len(arr) == 4
    assert frame_size(arr) == 4 + 4
    assert arr.is_valid()


def test_three_lines_in_general_position_counts():
    arr = triangle_lines()
    # pairwise intersections (0,0), (4,0), (0,4) -> 3 vertices; 6 ends at infinity;
    # every line is cut twice -> 3 pieces each -> 9 edges;
    # 3 lines in general position cut the plane into 7 regions, 1 of them bounded.
    assert (arr.number_of_vertices, arr.number_of_vertices_at_infinity) == (3, 6)
    assert (arr.number_of_edges, arr.number_of_halfedges) == (9, 18)
    assert (arr.number_of_faces, arr.number_of_unbounded_faces) == (7, 6)
    assert len(arr.bounded_faces()) == 1
    assert frame_size(arr) == 4 + 6
    assert arr.is_valid()


def test_three_parallel_lines_counts():
    arr = a2.Arrangement("linear")
    arr.insert([a2.LinearCurve.line_from_coefficients(0, 1, 0),
                a2.LinearCurve.line_from_coefficients(0, 1, -1),
                a2.LinearCurve.line_from_coefficients(0, 1, -2)])
    # parallel lines never meet: no concrete vertex, 3 edges, 6 ends at infinity,
    # and 3 lines slice the plane into 4 horizontal strips.
    assert (arr.number_of_vertices, arr.number_of_vertices_at_infinity) == (0, 6)
    assert (arr.number_of_edges, arr.number_of_faces) == (3, 4)
    assert arr.number_of_unbounded_faces == 4
    assert arr.is_valid()


def test_number_of_vertices_at_infinity_counts_one_per_unbounded_end():
    for curves, expected in [
        ([a2.LinearCurve.segment((0, 0), (1, 1))], 0),          # bounded: no end at inf
        ([a2.LinearCurve.ray((0, 0), (1, 0))], 1),              # one end at infinity
        ([a2.Line((0, 0), (1, 1))], 2),                         # two ends
        ([a2.Line((0, 0), (1, 0)), a2.Line((0, 0), (0, 1))], 4),
    ]:
        arr = a2.Arrangement("linear")
        arr.insert(curves)
        assert arr.number_of_vertices_at_infinity == expected, curves


@pytest.mark.parametrize("build, components", [
    (axes, 1),
    (triangle_lines, 1),
    # a lone bounded segment is a second connected component next to the frame
    (lambda: _one(a2.LinearCurve.segment((0, 0), (4, 0))), 2),
    (lambda: _one(a2.Line((0, 0), (1, 1))), 1),
    (lambda: a2.Arrangement("linear"), 1),
])
def test_euler_characteristic_including_the_fictitious_frame(build, components):
    """V - E + F = 1 + C once the frame is counted in.

    The visible counters hide the frame, so add: the 4 corner vertices and the vertices
    at infinity, the fictitious halfedge pairs, and the fictitious face itself.
    """
    arr = build()
    v = arr.number_of_vertices + arr.number_of_vertices_at_infinity + 4
    e = arr.number_of_edges + frame_size(arr)
    f = arr.number_of_faces + 1
    assert v - e + f == 1 + components


def _one(curve):
    arr = a2.Arrangement("linear")
    arr.insert(curve)
    return arr


# ===========================================================================
# 8. special faces
# ===========================================================================
def test_fictitious_face_shape():
    arr = axes()
    ff = arr.fictitious_face
    assert ff.is_fictitious and ff.is_unbounded
    # it lies *outside* the frame, so the frame is a hole of it: no outer CCB, 1 inner CCB
    assert ff.number_of_outer_ccbs == 0 and ff.number_of_inner_ccbs == 1
    assert all(h.is_fictitious for h in ff.inner_ccbs()[0])
    # and it is not part of the public face lists
    assert ff not in arr.faces() and ff not in arr.unbounded_faces()


def test_fictitious_face_is_linear_only(square_arr):
    """The bounded topologies have no fictitious face at all (uses the shared fixture)."""
    with pytest.raises(a2.UnsupportedError):
        square_arr.fictitious_face
    assert a2.Arrangement("linear").fictitious_face.is_fictitious


def test_unbounded_faces_versus_the_drifting_unbounded_face():
    arr = axes()
    ufs = arr.unbounded_faces()
    assert len(ufs) == arr.number_of_unbounded_faces == 4
    assert all(f.is_unbounded and not f.is_fictitious for f in ufs)
    # `unbounded_face` is documented to be only *one* of them for this kind
    assert arr.unbounded_face in ufs
    assert arr.bounded_faces() == []
    assert len(arr.faces()) == len(ufs) + len(arr.bounded_faces())


def test_every_unbounded_face_ccb_mixes_real_and_fictitious_halfedges():
    arr = axes()
    for f in arr.unbounded_faces():
        ccb = f.outer_ccb()
        # each quadrant is bounded by 2 real rays and 2 frame pieces
        assert len(ccb) == 4
        assert sum(h.is_fictitious for h in ccb) == 2
        assert sum(not h.is_fictitious for h in ccb) == 2


def test_bounded_face_of_a_linear_arrangement():
    arr = triangle_lines()
    face = arr.bounded_faces()[0]
    assert not face.is_unbounded and face.has_outer_ccb
    ccb = face.outer_ccb()
    assert len(ccb) == 3 and not any(h.is_fictitious for h in ccb)
    # the triangle (0,0) (4,0) (0,4)
    corners = {h.target.point.xy for h in ccb}
    assert corners == {(0.0, 0.0), (4.0, 0.0), (0.0, 4.0)}


# ===========================================================================
# 9. fictitious halfedges
# ===========================================================================
def test_fictitious_halfedges_never_show_up_in_the_public_iterators():
    arr = axes()
    assert not any(h.is_fictitious for h in arr.halfedges())
    assert not any(h.is_fictitious for h in arr.edges())
    assert len(arr.halfedges()) == 8 and len(arr.edges()) == 4
    # they are only reachable through a CCB walk
    assert any(h.is_fictitious for h in arr.unbounded_faces()[0].outer_ccb())


def test_fictitious_halfedge_carries_no_curve():
    arr = axes()
    fict = arr.fictitious_face.inner_ccbs()[0][0]
    assert fict.is_fictitious
    for attr in ("curve", "directed_curve"):
        with pytest.raises(a2.UnsupportedError) as ei:
            getattr(fict, attr)
        assert "fictitious halfedges carry no curve" in str(ei.value)


def test_fictitious_halfedge_topology_accessors_still_work():
    arr = axes()
    fict = arr.fictitious_face.inner_ccbs()[0][0]
    assert fict.is_valid and fict.arrangement is arr
    assert fict.twin.is_fictitious and fict.next.is_fictitious and fict.prev.is_fictitious
    assert fict.face.is_fictitious
    assert fict.is_on_inner_ccb and not fict.is_on_outer_ccb
    assert fict.edge_id == min(fict.id, fict.twin.id)
    assert "fictitious" in repr(fict)
    assert fict.direction in ("left_to_right", "right_to_left")
    # the whole frame is one cycle: 4 corners + 4 curve ends
    assert len(fict.ccb()) == 8


def test_fictitious_halfedge_accepts_user_data():
    arr = axes()
    fict = arr.fictitious_face.inner_ccbs()[0][0]
    assert fict.data is None
    fict.data = {"tag": 1}
    assert fict.data == {"tag": 1}


def test_fictitious_halfedge_originating_curves_must_not_hit_a_cgal_assertion():
    """A fictitious halfedge has no curve, so its history query must be refused cleanly.

    CGAL_TRAPS_CHECKLIST ("Arrangement core") requires guarding every ``curve()`` access
    with ``is_fictitious()``: ``Arr_dcel_base.h`` dereferences a null curve pointer, which
    is a CGAL assertion here and undefined behaviour in a plain ``-DNDEBUG`` build.
    ``Halfedge.curve`` guards; ``originating_curves`` does not.
    """
    arr = axes()
    fict = arr.fictitious_face.inner_ccbs()[0][0]
    with pytest.raises(a2.UnsupportedError):
        fict.originating_curves()
    with pytest.raises(a2.UnsupportedError):
        fict.number_of_originating_curves


@pytest.mark.parametrize("op", ["remove", "split", "modify"])
def test_modifying_a_fictitious_edge_is_refused(op):
    arr = axes()
    fict = arr.fictitious_face.inner_ccbs()[0][0]
    with pytest.raises(ValueError) as ei:
        if op == "remove":
            arr.remove_edge(fict)
        elif op == "split":
            arr.split_edge(fict, a2.Point(0, 0, kind="linear"))
        else:
            arr.modify_edge(fict, a2.Line((0, 0), (1, 0)))
    assert "fictitious edge" in str(ei.value)
    assert arr.is_valid()


# ===========================================================================
# 10. vertices at infinity
# ===========================================================================
def test_vertex_at_infinity_has_no_point():
    arr = axes()
    fict = arr.fictitious_face.inner_ccbs()[0][0]
    v = fict.target
    assert v.is_at_open_boundary and not v.is_isolated
    with pytest.raises(a2.UnsupportedError) as ei:
        v.point
    assert "open boundary" in str(ei.value)
    assert "at_infinity" in repr(v)


def test_vertices_at_infinity_are_invisible_to_the_vertex_iterator():
    arr = axes()
    assert [v.point.xy for v in arr.vertices()] == [(0.0, 0.0)]
    assert not any(v.is_at_open_boundary for v in arr.vertices())


def test_frame_corners_are_at_the_open_boundary_but_are_not_counted():
    """The 4 rectangle corners are boundary vertices too, yet
    ``number_of_vertices_at_infinity`` counts only the curve ends."""
    arr = axes()
    on_frame = [h.target for h in arr.fictitious_face.inner_ccbs()[0]]
    assert len(on_frame) == 8 and all(v.is_at_open_boundary for v in on_frame)
    spaces = sorted((v.parameter_space_in_x, v.parameter_space_in_y) for v in on_frame)
    # 4 corners (both coordinates on a boundary) + the 4 axis ends (one coordinate only)
    corners = [s for s in spaces if "interior" not in s]
    ends = [s for s in spaces if "interior" in s]
    assert len(corners) == 4 and len(ends) == 4
    assert sorted(corners) == [("left", "bottom"), ("left", "top"),
                               ("right", "bottom"), ("right", "top")]
    assert sorted(ends) == [("interior", "bottom"), ("interior", "top"),
                            ("left", "interior"), ("right", "interior")]
    assert arr.number_of_vertices_at_infinity == 4


def test_vertex_at_infinity_incidences():
    arr = axes()
    ends = [h.target for h in arr.fictitious_face.inner_ccbs()[0]
            if h.target.parameter_space_in_x == "right"
            and h.target.parameter_space_in_y == "interior"]
    assert len(ends) == 1
    v = ends[0]
    # the end of the positive x half of y = 0: 2 fictitious frame edges + 1 real ray
    assert v.degree == 3
    assert sorted(h.is_fictitious for h in v.incident_halfedges()) == [False, True, True]
    with pytest.raises(ValueError):
        v.face                                    # only isolated vertices have one


# ===========================================================================
# 11. directed_curve / direction
# ===========================================================================
def test_directed_curve_flips_a_bounded_segment():
    arr = a2.Arrangement("linear")
    arr.insert(a2.LinearCurve.segment((0, 0), (4, 0)))
    he = arr.edges()[0]
    stored = he.curve
    assert he.twin.curve == stored                 # the curve is shared with the twin
    assert {he.direction, he.twin.direction} == {"left_to_right", "right_to_left"}
    for h in (he, he.twin):
        d = h.directed_curve
        # a bounded curve CAN be reversed, so directed_curve really runs source -> target
        assert d.source == h.source.point and d.target == h.target.point
    # the two directed curves are opposite parametrisations of the same support
    # (Curve.__eq__ is geometric, so they compare equal -- compare the endpoints)
    assert he.directed_curve.source == he.twin.directed_curve.target
    assert he.directed_curve.target == he.twin.directed_curve.source


def test_directed_curve_of_a_ray_is_returned_as_stored():
    """An unbounded curve cannot be reversed, so ``directed_curve`` is the stored curve
    even when the halfedge runs the other way (arrangement.hpp contract; STAGE2_NOTES)."""
    arr = a2.Arrangement("linear")
    arr.insert(a2.LinearCurve.ray((0, 0), (1, 0)))
    he, twin = arr.edges()[0], arr.edges()[0].twin
    forward = he if he.direction == "left_to_right" else twin
    backward = twin if forward is he else he
    # the forward halfedge really goes source -> target along the stored ray
    assert forward.source.point == a2.Point(0, 0, kind="linear")
    assert forward.target.is_at_open_boundary
    assert forward.directed_curve == forward.curve
    # the backward one is traversed against the stored orientation, and its
    # `directed_curve` is *still* the stored ray (it has no reversal)
    assert backward.direction == "right_to_left"
    assert backward.source.is_at_open_boundary
    assert backward.target.point == a2.Point(0, 0, kind="linear")
    assert backward.directed_curve == backward.curve
    assert backward.directed_curve.source == a2.Point(0, 0, kind="linear")


def test_direction_agrees_with_which_end_is_the_source():
    arr = axes()
    for he in arr.halfedges():
        c = he.curve
        if he.direction == "left_to_right":
            # the source is the curve's min end
            assert he.source.is_at_open_boundary == (not c.has_source)
            if c.has_source:
                assert he.source.point == c.source
        else:
            assert he.target.is_at_open_boundary == (not c.has_source)
            if c.has_source:
                assert he.target.point == c.source


def test_halfedge_identity_versus_edge_id():
    arr = axes()
    he = arr.edges()[0]
    assert he != he.twin                      # handles compare by element identity
    assert he.edge_id == he.twin.edge_id      # ... but they are the same edge
    assert he.twin.twin == he


# ===========================================================================
# 12. face_polygon / boundary_points
# ===========================================================================
def test_bounded_face_polygon_and_boundary_points():
    arr = triangle_lines()
    face = arr.bounded_faces()[0]
    pwh = face.polygon()
    assert not pwh.is_unbounded and len(pwh.holes) == 0
    assert len(pwh.outer.curves) == 3
    assert pwh.outer.orientation() == 1                    # counterclockwise
    assert {p.xy for p in pwh.outer.points} == {(0.0, 0.0), (4.0, 0.0), (0.0, 4.0)}
    outer, holes = face.boundary_points(1e-3)
    assert len(holes) == 0
    # the approximated ring repeats its first point to close
    assert outer[0] == outer[-1] and len(outer) == 4
    assert set(outer) == {(0.0, 0.0), (4.0, 0.0), (0.0, 4.0)}


def test_unbounded_face_has_no_bounded_polygon():
    """``face_polygon`` drops the fictitious halfedges, so an unbounded face's boundary is
    an *open* chain of rays -- not a ``Polygon``, which must chain end to end."""
    arr = axes()
    for f in arr.unbounded_faces():
        with pytest.raises(a2.UnsupportedError):
            f.polygon()


def test_fictitious_face_polygon_and_boundary_points_are_empty():
    arr = axes()
    ff = arr.fictitious_face
    with pytest.raises(a2.UnsupportedError) as ei:
        ff.polygon()
    assert "no outer CCB" in str(ei.value)
    # every halfedge of its only CCB is fictitious, so nothing is approximated
    assert ff.boundary_points(1e-3) == ([], [[]])


def test_unbounded_face_boundary_points_needs_a_clip_box_that_the_api_cannot_take():
    """API GAP: ``Face.boundary_points`` has no ``bbox=`` parameter, so an unbounded face
    can never be approximated (``Curve.approximate`` refuses an unbounded curve without
    one).  ``Arrangement.approximate_edges`` does take one -- see the report."""
    arr = axes()
    with pytest.raises(ValueError) as ei:
        arr.unbounded_faces()[0].boundary_points(1e-3)
    assert "clipping box" in str(ei.value)


def test_unbounded_face_open_chain_via_the_ccb_walk():
    """The supported way to render an unbounded face: walk the CCB, skip the fictitious
    runs and clip the remaining curves yourself."""
    arr = axes()
    face = arr.unbounded_faces()[0]
    ccb = face.outer_ccb()
    # the CCB is a cycle, so rotate it to start on a fictitious run before splitting
    start = next(i for i, h in enumerate(ccb) if h.is_fictitious)
    ccb = ccb[start:] + ccb[:start]
    chains, current = [], []
    for he in ccb:
        if he.is_fictitious:
            if current:
                chains.append(current)
                current = []
        else:
            current.append(he.directed_curve.approximate(1e-3, bbox=(-5, -5, 5, 5)))
    if current:
        chains.append(current)
    # a quadrant is bounded by exactly one open chain of 2 rays
    assert len(chains) == 1 and len(chains[0]) == 2
    for pts in chains[0]:
        assert len(pts) == 2 and all(abs(c) <= 5.0 for p in pts for c in p)


def test_face_edges_include_fictitious_but_adjacent_faces_do_not():
    arr = axes()
    face = arr.unbounded_faces()[0]
    assert sum(h.is_fictitious for h in face.edges()) == 2
    # documented: adjacent_faces() skips fictitious halfedges
    neighbours = face.adjacent_faces()
    assert len(neighbours) == 2
    assert all(not f.is_fictitious and f != face for f in neighbours)
    # ... which makes the fictitious face's own neighbour list empty
    assert arr.fictitious_face.adjacent_faces() == []
    assert len(arr.fictitious_face.edges()) == 8


# ===========================================================================
# 13. point location
# ===========================================================================
# 'landmarks' is absent from this tuple because every builder below inserts LINES: CGAL 6.1's
# Arr_landmarks_point_location::_deal_with_curve_contained_in_segment
# (Arr_point_location/Arr_landmarks_pl_impl.h:414) compares he->source()->point() with
# he->target()->point() without the is_at_open_boundary() guard its sibling code at :309/:329
# uses, so a query point on an unbounded edge reaches a vertex at infinity with a null point,
# and _walk_from_face (:533) runs out of crossable edges on the all-fictitious outer ccb of the
# unbounded face.  The strategy is offered again as soon as the arrangement holds no unbounded
# edge -- see test_landmarks_is_offered_exactly_while_the_arrangement_is_bounded.
LINEAR_STRATEGIES = ("naive", "simple", "walk")


@pytest.mark.parametrize("strategy", LINEAR_STRATEGIES)
def test_locate_a_face_and_a_vertex(strategy):
    arr = axes()
    inside = arr.locate((3, 4), strategy)
    assert isinstance(inside, a2.Face) and inside.is_unbounded
    on_vertex = arr.locate((0, 0), strategy)
    assert isinstance(on_vertex, a2.Vertex) and on_vertex.point.xy == (0.0, 0.0)


@pytest.mark.parametrize("strategy", LINEAR_STRATEGIES)
def test_locate_a_point_on_a_bounded_edge(strategy):
    arr = triangle_lines()
    on_edge = arr.locate((2, 0), strategy)         # inside the base of the triangle
    assert isinstance(on_edge, a2.Halfedge) and not on_edge.is_fictitious
    assert on_edge.curve.is_segment


@pytest.mark.parametrize("strategy", LINEAR_STRATEGIES)
def test_locate_a_point_on_an_unbounded_edge(strategy):
    """A query on a ray is a perfectly ordinary point-location query.

    Every strategy this arrangement advertises must answer it.  ``landmarks`` is not one of
    them (see LINEAR_STRATEGIES): on an arrangement with unbounded edges it reaches a vertex
    at infinity and trips ``CGAL_assertion(p_pt != nullptr)`` in ``Arr_dcel_base.h``, so it
    is not offered for as long as such an edge is present.
    """
    arr = axes()
    on_edge = arr.locate((2, 0), strategy)
    assert isinstance(on_edge, a2.Halfedge) and not on_edge.is_fictitious
    assert on_edge.curve.is_ray


def test_all_supported_strategies_agree_on_the_answer():
    arr = triangle_lines()
    answers = {s: arr.locate((1, 1), s) for s in LINEAR_STRATEGIES}
    ids = {f.id for f in answers.values()}
    assert len(ids) == 1
    assert not next(iter(answers.values())).is_unbounded    # (1,1) is inside the triangle


def test_trapezoid_and_triangulation_are_refused_for_the_linear_kind():
    """``supports_point_location`` matrix from test_smoke; the trapezoid RIC structure
    crashes on removal of an edge incident to a vertex at infinity (STAGE2_NOTES,
    kind_linear "interface change requests"), the triangulation needs a bounded
    arrangement of segments, and landmarks reads the null point of a vertex at infinity
    for any query on an unbounded edge (Arr_landmarks_pl_impl.h:414) -- and this
    arrangement is made of lines."""
    arr = axes()
    for s in ("trapezoid", "triangulation", "landmarks"):
        assert not arr.supports_point_location(s)
        with pytest.raises(a2.UnsupportedError):
            arr.locate((1, 1), s)
        with pytest.raises(a2.UnsupportedError):
            arr.attach_point_location(s)
    for s in LINEAR_STRATEGIES:
        assert arr.supports_point_location(s)


def test_landmarks_is_offered_exactly_while_the_arrangement_is_bounded():
    """``landmarks`` is a per-ARRANGEMENT capability for this kind, not a per-kind one.

    CGAL 6.1's landmark walk only misbehaves where a CCB mixes fictitious and concrete
    halfedges, which is exactly what an unbounded edge creates; with none present the
    strategy answers like every other one.  So it is advertised while the arrangement holds
    no ray and no line, and withdrawn (attached structure included) as soon as one appears.
    """
    arr = a2.Arrangement("linear")
    assert arr.supports_point_location("landmarks")          # empty: nothing unbounded yet
    arr.insert([a2.Segment((0, 0), (4, 0)), a2.Segment((4, 0), (4, 4)),
                a2.Segment((4, 4), (0, 4)), a2.Segment((0, 4), (0, 0)),
                a2.Segment((0, 0), (4, 4))])
    assert arr.number_of_vertices_at_infinity == 0
    assert arr.supports_point_location("landmarks")
    arr.attach_point_location("landmarks")
    # CGAL's point location returns an arbitrary twin for a query on an edge, so compare
    # halfedge answers by edge_id (CGAL_TRAPS_CHECKLIST, "Point location")
    def key(r):
        return ("E", r.edge_id) if isinstance(r, a2.Halfedge) else (type(r).__name__, r.id)
    for q in ((1, 3), (2, 0), (2, 2), (0, 0), (100, 100), (3, 1), (4, 2), (0, 2)):
        assert key(arr.locate(q, "landmarks")) == key(arr.locate(q, "naive")), q
    # a line makes the strategy unavailable -- also for the attached object and for
    # strategy=None, which must fall back to the walk instead of using it
    line = arr.insert(a2.Line((0, -2), (1, -2)))
    assert arr.number_of_vertices_at_infinity == 2
    assert not arr.supports_point_location("landmarks")
    assert arr.has_point_location("landmarks")               # still attached, just not used
    with pytest.raises(a2.UnsupportedError) as ei:
        arr.locate((2, -2), "landmarks")
    assert "unbounded edge" in str(ei.value)
    assert key(arr.locate((2, -2))) == key(arr.locate((2, -2), "walk"))
    # ... and it comes back once the line is gone again
    arr.remove_curve(line)
    assert arr.number_of_vertices_at_infinity == 0
    assert arr.supports_point_location("landmarks")
    assert key(arr.locate((2, 2), "landmarks")) == key(arr.locate((2, 2), "naive"))


@pytest.mark.parametrize("strategy", LINEAR_STRATEGIES)
def test_attach_and_detach_a_point_location_structure(strategy):
    arr = triangle_lines()
    assert not arr.has_point_location(strategy)
    arr.attach_point_location(strategy)
    assert arr.has_point_location(strategy)
    assert arr.locate((1, 1)).id == arr.locate((1, 1), strategy).id
    arr.detach_point_location(strategy)
    assert not arr.has_point_location(strategy)


def test_attached_strategy_survives_an_insertion():
    arr = axes()
    arr.attach_point_location("walk")
    arr.insert(a2.LinearCurve.line_from_coefficients(1, 1, -4))
    assert arr.number_of_edges == 9 and arr.is_valid()
    assert not arr.locate((1, 1)).is_unbounded          # now inside the triangle
    arr.detach_point_location("walk")


@pytest.mark.parametrize("strategy", ["simple", "walk", None])
def test_vertical_ray_shooting(strategy):
    arr = axes()
    up = arr.ray_shoot_up((1, -1), strategy)
    # from (1,-1) the first thing above is the positive x half of y = 0
    assert isinstance(up, a2.Halfedge) and not up.is_fictitious
    assert up.curve.is_ray and up.curve.source == a2.Point(0, 0, kind="linear")
    down = arr.ray_shoot_down((1, -1), strategy)
    # nothing below: the ray escapes, so the unbounded face it lives in is reported
    assert isinstance(down, a2.Face) and down.is_unbounded


@pytest.mark.parametrize("strategy", ["naive", "landmarks"])
def test_ray_shooting_is_refused_by_strategies_that_cannot_do_it(strategy):
    arr = axes()
    for method in ("ray_shoot_up", "ray_shoot_down"):
        with pytest.raises(a2.UnsupportedError) as ei:
            getattr(arr, method)((1, -1), strategy)
        assert "ray shooting" in str(ei.value)


def test_batched_locate_keeps_the_input_order():
    arr = triangle_lines()
    queries = [(1, 1), (0, 0), (2, 0), (100, 100), (1, 1)]
    got = arr.batched_locate(queries)
    assert [type(r).__name__ for r in got] == \
        ["Face", "Vertex", "Halfedge", "Face", "Face"]
    assert got[1].point.xy == (0.0, 0.0)
    assert not got[0].is_unbounded and got[3].is_unbounded
    assert got[0].id == got[4].id == arr.locate((1, 1), "walk").id


def test_ray_shooting_inside_the_bounded_triangle():
    arr = triangle_lines()
    up = arr.ray_shoot_up((1, 1), "walk")
    down = arr.ray_shoot_down((1, 1), "walk")
    # from inside the triangle the ray hits the hypotenuse above and the base below
    assert up.curve == a2.LinearCurve.segment((0, 4), (4, 0))
    assert down.curve == a2.LinearCurve.segment((0, 0), (4, 0))


def test_ray_shooting_that_escapes_reports_an_unbounded_face():
    arr = triangle_lines()
    up = arr.ray_shoot_up((10, 10), "walk")
    assert isinstance(up, a2.Face) and up.is_unbounded
    # ... while downwards it hits the positive half of y = 0
    down = arr.ray_shoot_down((10, 10), "walk")
    assert isinstance(down, a2.Halfedge) and not down.is_fictitious
    assert down.curve.is_ray and down.curve.source == a2.Point(4, 0, kind="linear")


def test_point_location_never_returns_a_fictitious_halfedge():
    arr = axes()
    for q in [(0, 0), (2, 0), (0, 3), (-7, -7), (1000, 1000)]:
        r = arr.locate(q, "walk")
        assert not isinstance(r, a2.Halfedge) or not r.is_fictitious


# ===========================================================================
# 14. zone / do_intersect
# ===========================================================================
def test_zone_of_a_curve_that_stays_inside_one_face():
    arr = axes()
    z = arr.zone(a2.LinearCurve.segment((1, 1), (3, 3)))
    assert len(z) == 1 and isinstance(z[0], a2.Face)
    assert arr.number_of_edges == 4              # zone() must not modify anything


def test_zone_of_a_line_alternates_faces_and_edges():
    arr = axes()
    z = arr.zone(a2.Line((0, 1), (1, 2)))        # y = x + 1 crosses both axes
    assert [type(x).__name__ for x in z] == \
        ["Face", "Halfedge", "Face", "Halfedge", "Face"]
    assert not any(getattr(x, "is_fictitious", False) for x in z)


def test_zone_through_a_vertex():
    arr = axes()
    z = arr.zone(a2.Line((0, 0), (1, 1)))        # y = x runs through the origin
    assert [type(x).__name__ for x in z] == ["Face", "Vertex", "Face"]


def test_do_intersect():
    arr = axes()
    assert arr.do_intersect(a2.Line((0, 1), (1, 2)))
    assert arr.do_intersect(a2.LinearCurve.segment((-1, 1), (1, -1)))
    assert not arr.do_intersect(a2.LinearCurve.segment((1, 1), (3, 3)))


# ===========================================================================
# 15. vertical decomposition
# ===========================================================================
def test_decompose_reports_fictitious_halfedges_instead_of_none():
    """point_location_and_decomposition.md gotcha 7: in an unbounded arrangement
    "nothing above/below" comes back as a fictitious halfedge."""
    arr = axes()
    entries = arr.decompose()
    assert len(entries) == 1                     # one entry per concrete vertex
    v, below, above = entries[0]
    assert v.point.xy == (0.0, 0.0)
    for feature in (below, above):
        assert isinstance(feature, a2.Halfedge) and feature.is_fictitious
        assert feature.face.is_fictitious or feature.twin.face.is_fictitious


def test_decompose_reports_none_along_a_vertical_edge():
    """CGAL skips the vertical edge the ray would run along and reports nothing there."""
    arr = a2.Arrangement("linear")
    arr.insert(a2.LinearCurve.segment((0, 0), (0, 4)))       # a vertical segment
    entries = arr.decompose()
    assert [v.point.xy for v, _, _ in entries] == [(0.0, 0.0), (0.0, 4.0)]
    (_, b0, a0), (_, b1, a1) = entries
    assert b0.is_fictitious and a0 is None       # above (0,0) lies the edge itself
    assert b1 is None and a1.is_fictitious       # below (0,4) lies the edge itself


def test_decompose_is_xy_lexicographic_and_finds_real_features():
    arr = a2.Arrangement("linear")
    arr.insert([a2.LinearCurve.segment((0, 0), (4, 0)),
                a2.LinearCurve.segment((4, 0), (2, 3)),
                a2.LinearCurve.segment((2, 3), (0, 0))])
    entries = arr.decompose()
    assert [v.point.xy for v, _, _ in entries] == [(0.0, 0.0), (2.0, 3.0), (4.0, 0.0)]
    _, below_apex, above_apex = entries[1]
    # straight below the apex (2,3) lies the base segment; above it, nothing
    assert isinstance(below_apex, a2.Halfedge) and not below_apex.is_fictitious
    assert below_apex.curve == a2.LinearCurve.segment((0, 0), (4, 0))
    assert above_apex.is_fictitious


# ===========================================================================
# 16. overlay
# ===========================================================================
def test_overlay_of_two_line_arrangements():
    a = axes()                                            # y = 0 and x = 0
    b = a2.Arrangement("linear")
    b.insert(a2.LinearCurve.line_from_coefficients(0, 1, -1))   # y = 1
    r = a.overlay(b)
    # three lines: y=0, x=0, y=1.  y=0 and y=1 are parallel, so only 2 vertices:
    # (0,0) and (0,1).  Edges: y=0 -> 2, y=1 -> 2, x=0 cut twice -> 3, total 7.
    # Faces: the vertical line halves each of the 3 horizontal strips -> 6, all unbounded.
    assert (r.number_of_vertices, r.number_of_vertices_at_infinity) == (2, 6)
    assert (r.number_of_edges, r.number_of_faces) == (7, 6)
    assert r.number_of_unbounded_faces == 6
    assert r.is_valid() and r.kind == a2.Kind.LINEAR
    # the result history carries copies of every input curve of both operands
    assert r.number_of_curves == 3
    # the inputs are untouched
    assert (a.number_of_edges, b.number_of_edges) == (4, 1)


def test_overlay_callbacks_fire_for_every_result_feature():
    a = axes()
    b = a2.Arrangement("linear")
    b.insert(a2.LinearCurve.line_from_coefficients(0, 1, -1))
    seen = []

    class CB(a2.OverlayCallbacks):
        def face_face(self, fa, fb, fr):
            seen.append("face_face")
        def edge_face(self, ea, fb, er):
            seen.append("edge_face")
        def face_edge(self, fa, eb, er):
            seen.append("face_edge")
        def vertex_face(self, va, fb, vr):
            seen.append("vertex_face")
        def edge_edge_vertex(self, ea, eb, vr):
            seen.append("edge_edge_vertex")

    r = a.overlay(b, CB())
    counts = {name: seen.count(name) for name in set(seen)}
    # one face_face per result face (6), one edge_face per edge coming from A alone (the
    # 4 axis pieces plus the one x=0 piece cut by y=1 -> 5), one face_edge per edge coming
    # from B alone (y=1 is cut into 2), one vertex_face for A's (0,0) inside a face of B,
    # and one edge_edge_vertex where x=0 meets y=1.
    assert counts == {"face_face": 6, "edge_face": 5, "face_edge": 2,
                      "vertex_face": 1, "edge_edge_vertex": 1}
    assert r.number_of_faces == 6


def test_overlay_on_face_shorthand_sets_result_data():
    a = axes()
    b = a2.Arrangement("linear")
    b.insert(a2.LinearCurve.line_from_coefficients(0, 1, -1))
    a.unbounded_face.data = "A"
    for f in b.faces():
        f.data = "B"
    r = a.overlay(b, on_face=lambda fa, fb: (fa.data, fb.data))
    assert len(r.faces()) == 6
    assert all(isinstance(f.data, tuple) and len(f.data) == 2 for f in r.faces())
    assert {f.data[1] for f in r.faces()} == {"B"}


def test_overlay_of_an_empty_arrangement_is_a_copy():
    a = axes()
    r = a.overlay(a2.Arrangement("linear"))
    assert (r.number_of_vertices, r.number_of_edges, r.number_of_faces) == \
        (a.number_of_vertices, a.number_of_edges, a.number_of_faces)
    assert r.number_of_vertices_at_infinity == 4 and r.is_valid()


# ===========================================================================
# 17. the CGAL overlap-of-unbounded-curves trap
# ===========================================================================
def test_inserting_the_same_line_twice_raises_a_clean_error():
    """CGAL_TRAPS_CHECKLIST: "Inserting an unbounded curve (line/ray) that OVERLAPS an
    existing edge with an unbounded left end aborts (``cv.has_left()`` precondition)."
    The binding must refuse it instead of crashing."""
    arr = a2.Arrangement("linear")
    arr.insert(a2.Line((0, 0), (1, 0)))
    with pytest.raises(a2.UnsupportedError) as ei:
        arr.insert(a2.Line((0, 0), (1, 0)))
    assert "unbounded curve overlapping an existing edge" in str(ei.value)
    assert (arr.number_of_edges, arr.number_of_curves) == (1, 1)
    assert arr.is_valid()


@pytest.mark.parametrize("offender", [
    a2.Line((5, 0), (9, 0)),                       # the same line from other points
    a2.LinearCurve.line_from_coefficients(0, 1, 0),  # the same line from coefficients
    a2.LinearCurve.ray((2, 0), (5, 0)),            # overlap unbounded to the right
    a2.LinearCurve.ray((2, 0), (-5, 0)),           # overlap unbounded to the left
])
def test_every_unbounded_overlap_is_refused(offender):
    arr = a2.Arrangement("linear")
    arr.insert(a2.Line((0, 0), (1, 0)))
    with pytest.raises(a2.UnsupportedError) as ei:
        arr.insert(offender)
    assert "unbounded curve overlapping an existing edge" in str(ei.value)
    assert arr.number_of_edges == 1 and arr.is_valid()


def test_the_refusal_covers_every_incremental_entry_point():
    arr = a2.Arrangement("linear")
    arr.insert(a2.Line((0, 0), (1, 0)))
    dup = a2.Line((3, 0), (7, 0))
    for call in (lambda: arr.insert(dup),
                 lambda: arr.insert_curves([dup]),
                 lambda: arr.insert_non_intersecting(dup)):
        with pytest.raises(a2.UnsupportedError):
            call()
    assert (arr.number_of_edges, arr.number_of_curves) == (1, 1)
    assert arr.is_valid()


def test_insert_non_intersecting_guards_only_in_its_singular_form():
    """``insert_non_intersecting`` runs the overlap check, the plural overload does not
    (arr_impl.hpp): there CGAL's own sweep precondition fires instead."""
    arr = a2.Arrangement("linear")
    arr.insert_non_intersecting(a2.Line((0, 0), (1, 0)))
    with pytest.raises(a2.UnsupportedError):
        arr.insert_non_intersecting(a2.Line((5, 0), (9, 0)))
    with pytest.raises(a2.CGALError):
        arr.insert_non_intersecting_curves([a2.Line((5, 0), (9, 0))])


def test_an_aggregate_sweep_of_duplicate_lines_is_allowed():
    """The sweep-based path handles the overlap itself: both curves land in the history
    and induce the single shared edge (STAGE2_NOTES measured E=1, C=2)."""
    arr = a2.Arrangement("linear")
    handles = arr.insert([a2.Line((0, 0), (1, 0)), a2.Line((5, 0), (9, 0))])
    assert len(handles) == 2
    assert (arr.number_of_edges, arr.number_of_curves) == (1, 2)
    assert arr.is_valid()


def test_a_bounded_overlap_is_legal():
    arr = a2.Arrangement("linear")
    arr.insert(a2.Line((0, 0), (1, 0)))
    arr.insert(a2.LinearCurve.segment((1, 0), (3, 0)))
    # the line is cut at (1,0) and (3,0): 2 concrete vertices, 3 edges
    assert (arr.number_of_vertices, arr.number_of_edges) == (2, 3)
    assert arr.is_valid()
    # a ray whose overlap has a finite left end is fine too
    arr2 = a2.Arrangement("linear")
    arr2.insert(a2.LinearCurve.segment((0, 0), (5, 0)))
    arr2.insert(a2.LinearCurve.ray((2, 0), (5, 0)))
    assert arr2.is_valid()


def test_crossing_is_not_overlapping():
    arr = a2.Arrangement("linear")
    arr.insert(a2.Line((0, 0), (1, 0)))
    arr.insert(a2.Line((0, 0), (0, 1)))            # crosses, does not overlap
    assert (arr.number_of_edges, arr.number_of_faces) == (4, 4)
    assert arr.is_valid()


# ===========================================================================
# 18. modification and history
# ===========================================================================
def test_split_and_merge_an_unbounded_edge():
    arr = a2.Arrangement("linear")
    arr.insert(a2.Line((0, 0), (1, 0)))
    he = arr.edges()[0]
    arr.split_edge(he, a2.Point(2, 0, kind="linear"))
    # the line becomes two rays meeting in the new vertex (2,0)
    assert (arr.number_of_vertices, arr.number_of_edges) == (1, 2)
    assert arr.vertices()[0].point.xy == (2.0, 0.0)
    assert all(e.curve.is_ray for e in arr.edges())
    assert arr.is_valid()
    merged = arr.merge_edge(arr.edges()[0], arr.edges()[1])
    assert merged.curve.is_line
    assert (arr.number_of_vertices, arr.number_of_edges) == (0, 1)
    assert arr.is_valid()


def test_remove_curve_of_a_line():
    arr = a2.Arrangement("linear")
    handle = arr.insert(a2.Line((0, 0), (1, 0)))
    assert handle.number_of_induced_edges == 1
    assert arr.remove_curve(handle) == 1
    assert (arr.number_of_edges, arr.number_of_faces) == (0, 1)
    assert arr.number_of_vertices_at_infinity == 0
    assert frame_size(arr) == 4                  # the frame shrank back to a rectangle
    assert arr.is_valid()
    assert not handle.is_valid
    with pytest.raises(a2.InvalidHandleError):
        handle.curve


def test_remove_curve_leaves_the_vertices_it_created():
    """STAGE2_NOTES: CGAL does not merge the collinear edges around the orphaned
    degree-2 vertices left behind by ``remove_curve``."""
    arr = triangle_lines()
    handles = {repr(c.curve): c for c in arr.curves()}
    removed = arr.remove_curve(handles["Line(a=1, b=1, c=-4)"])
    assert removed == 3                          # x+y=4 was cut into 3 pieces
    # the 3 vertices stay (only the two on the removed line lose degree), and the two
    # axes are still split at (0,4)/(4,0) as well as at the origin -> 6 edges, 4 faces.
    assert (arr.number_of_vertices, arr.number_of_edges) == (3, 6)
    assert arr.number_of_faces == 4
    assert arr.is_valid()


def test_remove_edge_merges_two_unbounded_faces():
    arr = axes()
    face = arr.remove_edge(arr.edges()[0])
    assert isinstance(face, a2.Face) and face.is_unbounded
    # one of the four quadrants disappears with the ray that separated it
    assert (arr.number_of_edges, arr.number_of_faces) == (3, 3)
    assert arr.number_of_vertices == 1 and arr.is_valid()


def test_insert_from_left_and_right_vertex_with_rays():
    arr = a2.Arrangement("linear")
    arr.insert(a2.LinearCurve.segment((0, 0), (4, 0)))
    by_point = {v.point.xy: v for v in arr.vertices()}
    arr.insert_from_left_vertex(a2.LinearCurve.ray((4, 0), (9, 0)), by_point[(4.0, 0.0)])
    arr.insert_from_right_vertex(a2.LinearCurve.ray_from_direction((0, 0), -1, 0),
                                 by_point[(0.0, 0.0)])
    # the segment plus the two rays make the whole x axis: 2 vertices, 3 edges,
    # 2 ends at infinity, and the plane split into an upper and a lower half.
    assert (arr.number_of_vertices, arr.number_of_edges) == (2, 3)
    assert (arr.number_of_vertices_at_infinity, arr.number_of_faces) == (2, 2)
    assert arr.is_valid()


def test_insert_in_face_interior_and_non_intersecting_take_unbounded_curves():
    arr = a2.Arrangement("linear")
    he = arr.insert_in_face_interior(a2.Line((0, 0), (1, 1)), arr.unbounded_face)
    assert he.curve.is_line
    assert (arr.number_of_edges, arr.number_of_faces) == (1, 2)
    # neither entry point records history
    assert arr.number_of_curves == 0
    arr2 = a2.Arrangement("linear")
    arr2.insert_non_intersecting(a2.Line((0, 0), (1, 1)))
    assert arr2.number_of_edges == 1 and arr2.number_of_curves == 0
    assert arr2.is_valid()


def test_history_of_two_crossing_lines():
    arr = a2.Arrangement("linear")
    h1, h2 = arr.insert([a2.Line((0, 0), (1, 0)), a2.Line((0, 0), (0, 1))])
    assert h1.number_of_induced_edges == h2.number_of_induced_edges == 2
    assert len(arr.curves()) == arr.number_of_curves == 2
    assert h1.curve.is_line and h2.curve.is_line
    for e in arr.edges():
        assert e.number_of_originating_curves == 1
        assert len(e.originating_curves()) == 1
    induced = {e.edge_id for e in h1.induced_edges()} | {e.edge_id for e in h2.induced_edges()}
    assert induced == {e.edge_id for e in arr.edges()}


def test_insert_an_isolated_point_into_an_unbounded_face():
    arr = axes()
    v = arr.insert_point((7, 7))
    assert v.is_isolated and v.degree == 0
    assert v.face.is_unbounded
    assert arr.number_of_isolated_vertices == 1
    assert arr.number_of_vertices == 2 and arr.is_valid()
    assert arr.remove_isolated_vertex(v).is_unbounded
    assert arr.number_of_vertices == 1


def test_insert_accepts_a_segment_kind_curve_by_conversion():
    arr = a2.Arrangement("linear")
    handle = arr.insert(a2.Segment((0, 0), (4, 0)))
    assert handle.curve.kind == a2.Kind.LINEAR and handle.curve.is_segment
    assert arr.number_of_edges == 1


def test_a_bounded_kind_refuses_an_unbounded_curve():
    with pytest.raises(a2.NotRepresentableError):
        a2.Arrangement("segment").insert(a2.Line((0, 0), (1, 1)))


def test_insert_dispatches_on_its_argument():
    arr = a2.Arrangement("linear")
    assert isinstance(arr.insert(a2.Line((0, 0), (1, 0))), a2.CurveHandle)
    assert isinstance(arr.insert([a2.Line((0, 0), (0, 1))]), list)
    assert isinstance(arr.insert_point((5, 5)), a2.Vertex)
    assert len(arr) == arr.number_of_edges == 4


def test_modify_vertex_and_edge_accept_only_an_equal_geometry():
    arr = a2.Arrangement("linear")
    arr.insert(a2.LinearCurve.segment((0, 0), (4, 0)))
    v, he = arr.vertices()[0], arr.edges()[0]
    assert arr.modify_vertex(v, v.point).point == v.point
    assert arr.modify_edge(he, he.curve).curve == he.curve
    with pytest.raises(a2.PreconditionError):
        arr.modify_vertex(v, (9, 9))


def test_curve_handle_follows_a_split():
    arr = a2.Arrangement("linear")
    handle = arr.insert(a2.Line((0, 0), (1, 0)))
    arr.split_edge(arr.edges()[0], a2.Point(2, 0, kind="linear"))
    assert handle.number_of_induced_edges == 2
    assert {e.curve.which for e in handle.induced_edges()} == {"ray"}
    assert all(len(e.originating_curves()) == 1 for e in handle.induced_edges())


def test_data_round_trips_on_every_element_including_the_fictitious_face():
    arr = axes()
    arr.fictitious_face.data = "outside"
    arr.unbounded_faces()[0].data = 42
    arr.vertices()[0].data = {"origin": True}
    arr.edges()[0].data = [1, 2]
    clone = arr.copy()
    assert clone.fictitious_face.data == "outside"
    assert sorted(str(f.data) for f in clone.faces()) == ["42", "None", "None", "None"]
    assert clone.vertices()[0].data == {"origin": True}
    assert sorted(str(e.data) for e in clone.edges()) == \
        ["None", "None", "None", "[1, 2]"]


def test_ccb_of_a_bounded_face_is_a_closed_cycle():
    arr = triangle_lines()
    face = arr.bounded_faces()[0]
    ccb = face.outer_ccb()
    n = len(ccb)
    for i, he in enumerate(ccb):
        assert he.face == face
        assert he.next == ccb[(i + 1) % n]
        assert he.target.id == ccb[(i + 1) % n].source.id
        # directed_curve really chains target -> source around the cycle
        assert he.directed_curve.target == ccb[(i + 1) % n].directed_curve.source
    assert ccb[0].ccb() == ccb


def test_vertex_incidences_at_the_crossing_of_two_lines():
    arr = axes()
    v = arr.vertices()[0]
    assert v.degree == 4 and not v.is_isolated
    incident = v.incident_halfedges()
    assert len(incident) == 4
    # incident_halfedges() reports the halfedges pointing INTO the vertex
    assert all(h.target.id == v.id for h in incident)
    assert all(not h.is_fictitious for h in incident)
    assert len(v.incident_faces()) == 4          # the four quadrants


# ===========================================================================
# 19. bulk export
# ===========================================================================
def test_vertex_coordinates_and_edge_indices_mark_ends_at_infinity():
    arr = axes()
    coords = arr.vertex_coordinates()
    assert len(coords) == 1 and tuple(coords[0]) == (0.0, 0.0)
    pairs = [tuple(int(i) for i in row) for row in arr.edge_vertex_indices()]
    assert len(pairs) == 4
    # every edge is a ray: one concrete end (index 0) and one at infinity (-1)
    assert all(sorted(p) == [-1, 0] for p in pairs)


def test_face_boundaries_skip_the_vertices_at_infinity():
    arr = triangle_lines()
    boundaries = arr.face_boundaries()
    assert len(boundaries) == arr.number_of_faces == 7
    n = len(arr.vertex_coordinates())
    for face in boundaries:
        for ccb in face:
            assert all(0 <= int(i) < n for i in ccb)
    # exactly one face lists all three concrete vertices: the triangle
    full = [f for f in boundaries if len(f) == 1 and len(f[0]) == 3]
    assert len(full) == 1


def test_approximate_edges_uses_a_padded_box_by_default():
    arr = axes()
    chains = arr.approximate_edges(1e-3)
    assert len(chains) == 4
    assert all(len(c) == 2 for c in chains)
    # every clipped ray starts or ends at the origin
    assert all(any(tuple(p) == (0.0, 0.0) for p in c) for c in chains)
    clipped = arr.approximate_edges(1e-3, bbox=(-5, -5, 5, 5))
    assert {tuple(float(v) for v in p) for c in clipped for p in c} == \
        {(0.0, 0.0), (5.0, 0.0), (-5.0, 0.0), (0.0, 5.0), (0.0, -5.0)}


def test_arrangement_bbox_only_sees_the_concrete_vertices():
    assert axes().bbox() == (0.0, 0.0, 0.0, 0.0)
    assert triangle_lines().bbox() == (0.0, 0.0, 4.0, 4.0)


# ===========================================================================
# 20. copy / clear / misc
# ===========================================================================
def test_copy_preserves_the_unbounded_topology():
    arr = triangle_lines()
    clone = arr.copy()
    assert (clone.number_of_vertices, clone.number_of_edges, clone.number_of_faces) == \
        (3, 9, 7)
    assert clone.number_of_vertices_at_infinity == 6
    assert clone.number_of_unbounded_faces == 6 and clone.number_of_curves == 3
    assert frame_size(clone) == 10
    assert clone.fictitious_face.is_fictitious and clone.is_valid()
    # the clone is independent
    clone.clear()
    assert arr.number_of_edges == 9


def test_clear_rebuilds_the_fictitious_frame():
    arr = triangle_lines()
    arr.clear()
    assert arr.is_empty and arr.number_of_faces == 1
    assert arr.number_of_vertices_at_infinity == 0
    assert frame_size(arr) == 4
    arr.insert(a2.Line((0, 0), (1, 0)))
    assert (arr.number_of_edges, arr.number_of_faces) == (1, 2)
    assert frame_size(arr) == 6 and arr.is_valid()


def test_boolean_set_operations_are_unavailable_for_the_linear_kind():
    with pytest.raises(a2.UnsupportedError) as ei:
        a2.PolygonSet("linear")
    assert "not available for kind 'linear'" in str(ei.value)


def test_traits_functors_on_linear_curves():
    t = a2.traits("linear")
    assert t.kind == a2.Kind.LINEAR and t.dimension == 2
    ln = a2.Line((0, 0), (1, 1))
    assert t.make_x_monotone(ln) == [ln]                     # already x-monotone
    assert t.compare_endpoints_xy(ln) == -1                  # directed right
    assert t.compare_endpoints_xy(a2.LinearCurve.line_from_coefficients(1, 0, 0)) == 1
    assert t.curves_equal(ln, a2.Line((2, 2), (5, 5)))
    assert t.compare_y_at_x((0, 1), ln) == 1
    assert t.intersect(ln, a2.Line((0, 0), (1, -1))) == \
        [(a2.Point(0, 0, kind="linear"), 1)]
    # the linear traits does have Construct_x_monotone_curve_2 (the functor the landmarks
    # strategy needs; the strategy itself is still refused for this kind because CGAL's
    # landmark walk reads the null point of a vertex at infinity)
    assert t.construct_x_monotone_curve((0, 0), (3, 3)) == \
        a2.LinearCurve.segment((0, 0), (3, 3))
    for end in ("min", "max"):
        assert t.parameter_space_in_x(ln, end) in ("left", "right")
        assert t.parameter_space_in_y(ln, end) in ("bottom", "top")


def test_repr_of_arrangement_and_handles():
    arr = axes()
    assert "linear" in repr(arr)
    assert "Vertex(" in repr(arr.vertices()[0])
    assert "Halfedge(" in repr(arr.edges()[0])
    assert "Face(" in repr(arr.unbounded_faces()[0])
    assert "CurveHandle(" in repr(arr.curves()[0])


def test_is_valid_holds_after_a_long_edit_sequence():
    arr = a2.Arrangement("linear")
    arr.insert([a2.Line((0, 0), (1, 0)), a2.Line((0, 0), (0, 1))])
    assert arr.is_valid()
    arr.insert(a2.LinearCurve.line_from_coefficients(1, 1, -4))
    assert arr.is_valid() and arr.number_of_edges == 9
    arr.insert(a2.LinearCurve.segment((1, 1), (1, 2)))
    assert arr.is_valid()
    arr.insert_point((10, 10))
    assert arr.is_valid()
    for handle in list(arr.curves()):
        arr.remove_curve(handle)
        assert arr.is_valid()
    assert arr.number_of_curves == 0
    assert arr.number_of_edges == 0
    assert arr.number_of_vertices_at_infinity == 0
