"""Exhaustive tests for the ``segment`` kind (``Arr_segment_traits_2<Epeck>``).

Everything here uses only the public API (``import arrangement_2d as a2``).

Every expected count is derived by hand in a comment next to the assertion; the
recurring reference figure is the *unit square with a chord*::

        (0,4)          (4,4)
          +--------------+
          |              |
    (-1,2)+--------------+(5,2)      <- the chord y = 2, x in [-1, 5]
          |              |
          +--------------+
        (0,0)          (4,0)

    V = 4 square corners + 2 chord/square crossings + 2 chord tips = 8
    E = 4 square sides (2 of them split in two by the chord) + 3 chord pieces
      = (2 + 2 + 1 + 1) + 3 = 9
    F = unbounded + lower half + upper half = 3
    V - E + F = 8 - 9 + 3 = 2 = 1 + C with C = 1 connected component.

That figure is the ``square_arr`` fixture of ``tests/conftest.py``.
"""

from __future__ import annotations

import copy as _copy
import random
from decimal import Decimal
from fractions import Fraction

import pytest

a2 = pytest.importorskip("arrangement_2d")

S = a2.Segment


# ---------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------

def square_segments():
    """The four sides of the axis-parallel square (0,0)-(4,4), CCW."""
    return [S((0, 0), (4, 0)), S((4, 0), (4, 4)), S((4, 4), (0, 4)), S((0, 4), (0, 0))]


def counts(arr):
    """``(V, E, F)`` of an arrangement."""
    return (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces)


def edge_keys(arr):
    """Sorted canonical keys ``(left_xy, right_xy)`` of every edge.

    Uses ``left``/``right`` (the lexicographic endpoints) so the key does not depend
    on which of the two halfedges ``edges()`` happens to hand out.
    """
    return sorted((e.curve.left.approx, e.curve.right.approx) for e in arr.edges())


def vertex_points(arr):
    """Sorted approximate coordinates of every (concrete) vertex."""
    return sorted(v.point.approx for v in arr.vertices())


def connected_components(arr):
    """Number of connected components of the arrangement's 1-skeleton.

    Nodes are the vertices, links are the edges; an isolated vertex is its own
    component.  Plain union-find with path halving.
    """
    parent = {v.id: v.id for v in arr.vertices()}

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    for e in arr.edges():
        ra, rb = find(e.source.id), find(e.target.id)
        if ra != rb:
            parent[ra] = rb
    return len({find(v) for v in parent})


def assert_euler(arr):
    """V - E + F == 1 + C (C = connected components; F counts the unbounded face)."""
    v, e, f = counts(arr)
    c = connected_components(arr)
    assert v - e + f == 1 + c, (
        "Euler check failed: V=%d E=%d F=%d C=%d -> %d != %d" % (v, e, f, c, v - e + f, 1 + c)
    )


def random_segments(rng, n, lo=-6, hi=6):
    """`n` random segments with integer endpoints in ``[lo, hi]^2`` (never degenerate)."""
    out = []
    while len(out) < n:
        x1, y1, x2, y2 = (rng.randint(lo, hi) for _ in range(4))
        if (x1, y1) != (x2, y2):
            out.append(S((x1, y1), (x2, y2)))
    return out


# ===========================================================================
# 1. Segment: construction, accessors, exact / approximate coordinates
# ===========================================================================

def test_segment_construction_from_tuples_and_points():
    from_tuples = S((1, 2), (3, 4))
    from_points = S(a2.Point(1, 2), a2.Point(3, 4))
    assert from_tuples.kind is a2.Kind.SEGMENT
    assert from_tuples == from_points
    assert from_tuples.source == a2.Point(1, 2)
    assert from_tuples.target == a2.Point(3, 4)


def test_segment_from_coordinates_classmethod():
    assert a2.Segment.from_coordinates(0, 0, 4, 2) == S((0, 0), (4, 2))


def test_segment_rejects_a_degenerate_curve():
    # A segment whose endpoints coincide is not a valid Arr_segment_traits_2 curve.
    with pytest.raises(ValueError):
        S((1, 1), (1, 1))


def test_segment_accepts_every_supported_number_type():
    # int, float (its exact binary value), Fraction, Decimal and numeric strings.
    seg = S((Fraction(1, 3), 0.5), (Decimal("0.25"), "2/7"))
    assert seg.source.exact() == (Fraction(1, 3), Fraction(1, 2))
    assert seg.target.exact() == (Fraction(1, 4), Fraction(2, 7))
    assert seg.source.is_rational and seg.target.is_rational


def test_segment_float_endpoints_are_exact_binary_values():
    # 0.1 is not 1/10; the exact binary value of the double must come back.
    seg = S((0.1, 0.0), (1.0, 1.0))
    assert seg.source.exact()[0] == Fraction(0.1)
    assert seg.source.exact()[0] != Fraction(1, 10)


def test_segment_exact_rational_and_approx():
    seg = S((Fraction(1, 3), Fraction(-2, 5)), (2, 3))
    assert seg.source.exact_rational() == (Fraction(1, 3), Fraction(-2, 5))
    assert seg.source.approx == pytest.approx((1 / 3, -0.4))
    assert seg.source.xy == seg.source.approx
    assert seg.source.x == pytest.approx(1 / 3)
    assert seg.source.y == pytest.approx(-0.4)


def test_segment_point_interval_encloses_the_exact_value():
    seg = S((Fraction(1, 3), 0), (1, 1))
    (xlo, xhi), (ylo, yhi) = seg.source.interval()
    assert xlo <= 1 / 3 <= xhi
    assert Fraction(xlo) <= Fraction(1, 3) <= Fraction(xhi)
    assert (ylo, yhi) == (0.0, 0.0)


def test_segment_min_max_vertex_and_left_right():
    # (4,2) is lexicographically larger than (0,0); direction is irrelevant here.
    seg = S((4, 2), (0, 0))
    assert seg.min_vertex == a2.Point(0, 0)
    assert seg.max_vertex == a2.Point(4, 2)
    assert seg.left == seg.min_vertex
    assert seg.right == seg.max_vertex
    # source/target keep the stored orientation
    assert seg.source == a2.Point(4, 2)
    assert seg.target == a2.Point(0, 0)


def test_segment_direction_predicates():
    rising = S((0, 0), (4, 2))
    falling = S((4, 2), (0, 0))
    vertical = S((1, 5), (1, -1))
    assert rising.is_directed_right and not falling.is_directed_right
    # Compare_endpoints_xy_2: -1 when source < target, +1 otherwise.
    assert rising.compare_endpoints_xy() == -1
    assert falling.compare_endpoints_xy() == 1
    assert vertical.is_vertical and not rising.is_vertical


def test_segment_is_x_monotone_and_make_x_monotone():
    seg = S((0, 0), (4, 2))
    assert seg.is_x_monotone
    assert seg.type_name == "x_monotone_curve"
    pieces = seg.make_x_monotone()
    assert len(pieces) == 1 and pieces[0] == seg           # already x-monotone
    assert seg.x_monotone() == seg
    assert seg.to_curve().kind is a2.Kind.SEGMENT


def test_segment_supporting_line_coefficients():
    # The segment (0,0)-(4,2) lies on 2x - 4y = 0, i.e. a*x + b*y + c = 0 up to a
    # non-zero factor; check the relation instead of the normalisation.
    a, b, c = S((0, 0), (4, 2)).supporting_line
    assert (a, b, c) != (0, 0, 0)
    for x, y in ((0, 0), (4, 2), (2, 1)):
        assert a * x + b * y + c == 0


def test_segment_bbox_and_dimension():
    seg = S((4, -1), (0, 3))
    assert seg.bbox() == (0.0, -1.0, 4.0, 3.0)
    assert seg.dimension == 2
    assert seg.is_bounded and seg.has_source and seg.has_target


def test_segment_parameter_space_is_interior():
    # A bounded segment has no end on the parameter-space boundary: CGAL's
    # ARR_INTERIOR is 4 for both ends and both axes.
    seg = S((0, 0), (4, 2))
    assert seg.parameter_space_in_x(0) == seg.parameter_space_in_x(1) == 4
    assert seg.parameter_space_in_y(0) == seg.parameter_space_in_y(1) == 4


def test_segment_equality_is_geometric_and_ignores_direction():
    # Arr_segment_traits_2::Equal_2 compares left()/right(), not source/target.
    assert S((0, 0), (4, 2)) == S((4, 2), (0, 0))
    assert S((0, 0), (4, 2)) != S((0, 0), (4, 3))
    assert (S((0, 0), (4, 2)) == a2.Polyline([(0, 0), (4, 2)])) is False   # other kind
    with pytest.raises(TypeError):
        hash(S((0, 0), (1, 1)))


def test_segment_opposite_flips_source_and_target():
    seg = S((0, 0), (4, 2))
    opp = seg.opposite()
    assert opp.source == seg.target and opp.target == seg.source
    assert opp == seg                       # same geometry
    assert opp.compare_endpoints_xy() == 1


def test_segment_split_at_an_interior_point():
    left, right = S((0, 0), (4, 2)).split((2, 1))
    assert left == S((0, 0), (2, 1))
    assert right == S((2, 1), (4, 2))


def test_segment_trim():
    assert S((0, 0), (4, 2)).trim((1, Fraction(1, 2)), (3, Fraction(3, 2))) == \
        S((1, Fraction(1, 2)), (3, Fraction(3, 2)))


def test_segment_merge_and_can_merge():
    a, b = S((0, 0), (2, 1)), S((2, 1), (4, 2))
    assert a.can_merge(b)
    assert a.merge(b) == S((0, 0), (4, 2))
    # collinear but not touching -> not mergeable
    assert not S((0, 0), (1, 0)).can_merge(S((2, 0), (3, 0)))
    # touching but not collinear -> not mergeable
    assert not S((0, 0), (2, 0)).can_merge(S((2, 0), (2, 2)))


def test_segment_intersect_transversal_and_overlap():
    # The two diagonals of the square meet transversally at (2,2), multiplicity 1.
    res = S((0, 0), (4, 4)).intersect(S((0, 4), (4, 0)))
    assert len(res) == 1
    point, multiplicity = res[0]
    assert point == a2.Point(2, 2) and multiplicity == 1
    # Overlapping collinear segments give a curve, not a point.
    res = S((0, 0), (4, 0)).intersect(S((2, 0), (6, 0)))
    assert len(res) == 1 and isinstance(res[0], a2.Curve)
    assert res[0] == S((2, 0), (4, 0))
    # Disjoint segments
    assert S((0, 0), (1, 0)).intersect(S((0, 5), (1, 5))) == []


def test_segment_compare_y_at_x_and_x_range():
    seg = S((0, 0), (4, 2))          # y = x/2
    assert seg.compare_y_at_x((2, 1)) == 0      # on the curve
    assert seg.compare_y_at_x((2, 0)) == -1     # below
    assert seg.compare_y_at_x((2, 5)) == 1      # above
    assert seg.is_in_x_range((2, 100))
    assert not seg.is_in_x_range((5, 0))


def test_segment_compare_y_at_x_left_and_right():
    rising = S((0, 0), (4, 4))
    falling = S((0, 4), (4, 0))
    # they cross at (2,2): left of it the rising one is below, right of it above
    assert rising.compare_y_at_x_left(falling, (2, 2)) == -1
    assert rising.compare_y_at_x_right(falling, (2, 2)) == 1


def test_segment_approximate_and_length():
    seg = S((0, 0), (3, 4))
    # Arr_segment_traits_2 ignores the tolerance and emits both endpoints only.
    assert seg.approximate(1e-3) == [(0.0, 0.0), (3.0, 4.0)]
    assert seg.approximate_length() == pytest.approx(5.0)


def test_segment_to_kind_polyline_and_back():
    seg = S((0, 0), (4, 2))
    poly = seg.to_kind("polyline")
    assert poly.kind is a2.Kind.POLYLINE
    assert poly.to_kind(a2.Kind.SEGMENT) == seg
    assert seg.to_kind(a2.Kind.SEGMENT) is seg      # identity conversion


def test_segment_repr_is_exact():
    assert repr(S((0, 0), (4, 2))) == "Segment((0, 0), (4, 2))"
    assert repr(S((Fraction(1, 3), 0), (1, 1))) == "Segment((1/3, 0), (1, 1))"


# ===========================================================================
# 2. Arrangement of segments: insertion
# ===========================================================================

def test_empty_arrangement():
    arr = a2.Arrangement("segment")
    # An empty planar bounded arrangement has exactly one (unbounded) face.
    assert counts(arr) == (0, 0, 1)
    assert arr.number_of_halfedges == 0
    assert arr.number_of_isolated_vertices == 0
    assert arr.number_of_unbounded_faces == 1
    assert arr.number_of_vertices_at_infinity == 0
    assert arr.number_of_curves == 0
    assert arr.is_empty and len(arr) == 0
    assert arr.is_valid()
    assert arr.kind is a2.Kind.SEGMENT
    assert arr.is_unbounded_kind is False
    assert arr.unbounded_face.is_unbounded
    assert_euler(arr)


def test_arrangement_kind_can_be_named_enumerated_or_inferred():
    assert a2.Arrangement("segment").kind is a2.Kind.SEGMENT
    assert a2.Arrangement(a2.Kind.SEGMENT).kind is a2.Kind.SEGMENT
    assert a2.Arrangement(S((0, 0), (1, 1))).kind is a2.Kind.SEGMENT
    assert a2.Arrangement().kind is a2.Kind.SEGMENT          # the default kind
    with pytest.raises(ValueError):
        a2.Arrangement("nope")


def test_segment_arrangement_has_no_fictitious_or_spherical_face():
    arr = a2.Arrangement("segment")
    # the bounded planar topology has exactly one unbounded face and nothing else
    assert arr.unbounded_faces() == [arr.unbounded_face]
    assert arr.dimension == 2
    with pytest.raises(a2.UnsupportedError):
        arr.fictitious_face
    with pytest.raises(a2.UnsupportedError):
        arr.spherical_face


def test_arrangements_compare_by_identity():
    arr = a2.Arrangement("segment")
    assert arr == arr
    assert arr != a2.Arrangement("segment")


def test_insert_single_curve_returns_a_curve_handle():
    arr = a2.Arrangement("segment")
    ch = arr.insert(S((0, 0), (4, 0)))
    assert isinstance(ch, a2.CurveHandle)
    # one segment: 2 vertices, 1 edge, still only the unbounded face
    assert counts(arr) == (2, 1, 1)
    assert arr.number_of_halfedges == 2
    assert arr.number_of_curves == 1
    assert ch.curve == S((0, 0), (4, 0))
    assert ch.number_of_induced_edges == 1
    assert arr.is_valid()
    assert_euler(arr)


def test_insert_list_of_curves_builds_the_square():
    arr = a2.Arrangement("segment")
    handles = arr.insert(square_segments())
    # 4 corners, 4 sides, unbounded face + the square's interior
    assert counts(arr) == (4, 4, 2)
    assert len(handles) == 4 and all(isinstance(h, a2.CurveHandle) for h in handles)
    assert arr.number_of_curves == 4
    assert arr.number_of_unbounded_faces == 1
    assert len(arr.bounded_faces()) == 1
    assert arr.is_valid()
    assert_euler(arr)


def test_insert_curves_is_the_explicit_aggregate_form():
    arr = a2.Arrangement("segment")
    handles = arr.insert_curves(square_segments())
    assert [h.curve for h in handles] == square_segments()   # input order preserved
    assert counts(arr) == (4, 4, 2)


def test_insert_empty_iterables_are_no_ops():
    arr = a2.Arrangement("segment")
    assert arr.insert([]) == []
    assert arr.insert_curves([]) == []
    assert arr.insert_non_intersecting_curves([]) is None
    assert counts(arr) == (0, 0, 1)


def test_insert_point_creates_an_isolated_vertex():
    arr = a2.Arrangement("segment")
    v = arr.insert_point((2, 3))
    assert isinstance(v, a2.Vertex)
    assert v.is_isolated and v.degree == 0
    assert counts(arr) == (1, 0, 1)
    assert arr.number_of_isolated_vertices == 1
    assert arr.number_of_curves == 0          # insert_point records no history
    assert v.point == a2.Point(2, 3)
    assert v.face.is_unbounded
    assert_euler(arr)


def test_insert_point_on_an_existing_edge_splits_it():
    arr = a2.Arrangement("segment")
    ch = arr.insert(S((0, 0), (4, 0)))
    v = arr.insert_point((2, 0))
    # the edge is split in two: V 2 -> 3, E 1 -> 2
    assert counts(arr) == (3, 2, 1)
    assert v.degree == 2 and not v.is_isolated
    assert ch.number_of_induced_edges == 2        # history follows the split
    assert arr.number_of_curves == 1
    assert_euler(arr)


def test_insert_point_at_an_existing_vertex_returns_it():
    arr = a2.Arrangement("segment")
    arr.insert(S((0, 0), (4, 0)))
    v = arr.insert_point((0, 0))
    assert v.point == a2.Point(0, 0)
    assert counts(arr) == (2, 1, 1)               # nothing added


def test_insert_dispatches_a_bare_tuple_to_insert_point():
    arr = a2.Arrangement("segment")
    v = arr.insert((2, 3))
    assert isinstance(v, a2.Vertex) and v.is_isolated


def test_insert_a_polygon():
    arr = a2.Arrangement("segment")
    handles = arr.insert(a2.Polygon([(0, 0), (4, 0), (4, 4), (0, 4)]))
    assert len(handles) == 4                       # one handle per side
    assert counts(arr) == (4, 4, 2)


def test_square_with_chord_reference_figure(square_arr):
    # See the module docstring for the hand derivation.
    assert counts(square_arr) == (8, 9, 3)
    assert square_arr.number_of_halfedges == 18
    assert square_arr.number_of_curves == 5
    assert square_arr.number_of_unbounded_faces == 1
    assert square_arr.is_valid()
    assert_euler(square_arr)


def test_insert_non_intersecting_single_curve():
    arr = a2.Arrangement("segment")
    he = arr.insert_non_intersecting(S((0, 0), (2, 0)))
    assert isinstance(he, a2.Halfedge)
    assert counts(arr) == (2, 1, 1)
    assert arr.number_of_curves == 0              # no history for this form
    assert he.curve == S((0, 0), (2, 0))
    assert he.originating_curves() == []
    assert arr.is_valid()


def test_insert_non_intersecting_curves_aggregate():
    arr = a2.Arrangement("segment")
    arr.insert_non_intersecting_curves(
        [S((0, 0), (2, 0)), S((0, 1), (2, 1)), S((0, 2), (2, 2))])
    # three parallel disjoint segments: 6 vertices, 3 edges, 1 face
    assert counts(arr) == (6, 3, 1)
    assert arr.number_of_curves == 0
    assert arr.is_valid()
    assert_euler(arr)


def test_insert_non_intersecting_curves_may_share_endpoints():
    arr = a2.Arrangement("segment")
    arr.insert_non_intersecting(S((0, 0), (4, 0)))
    arr.insert_non_intersecting(S((4, 0), (4, 4)))
    # sharing an existing vertex is allowed: 3 vertices, 2 edges
    assert counts(arr) == (3, 2, 1)
    assert arr.is_valid()


def test_insert_non_intersecting_rejects_an_overlapping_curve():
    arr = a2.Arrangement("segment")
    arr.insert_non_intersecting(S((0, 0), (4, 0)))
    # CGAL locates the new curve's endpoints first: (1,0) lands on an edge -> refused.
    with pytest.raises(a2.PreconditionError):
        arr.insert_non_intersecting(S((1, 0), (3, 0)))
    assert counts(arr) == (2, 1, 1)               # arrangement untouched
    assert arr.is_valid()


def test_insert_non_intersecting_violation_never_passes_unnoticed():
    """A curve crossing the interior of an edge is a precondition violation.

    CGAL 6.1 only locates the *endpoints* of the new curve, so a transversal
    crossing whose endpoints are in face interiors is not rejected -- but then the
    DCEL is left inconsistent and ``is_valid()`` says so.  Either signal is
    acceptable; silence with a valid arrangement would be a bug.
    """
    arr = a2.Arrangement("segment")
    arr.insert_non_intersecting(S((0, 0), (4, 0)))
    try:
        arr.insert_non_intersecting(S((2, -2), (2, 2)))
    except a2.PreconditionError:
        assert counts(arr) == (2, 1, 1)
        assert arr.is_valid()
    else:
        assert not arr.is_valid()


def test_insert_in_face_interior_with_a_curve():
    arr = a2.Arrangement("segment")
    arr.insert(square_segments())
    inner = arr.bounded_faces()[0]
    he = arr.insert_in_face_interior(S((1, 1), (3, 1)), inner)
    # a floating "antenna" inside the square: V 4 -> 6, E 4 -> 5, F unchanged
    assert counts(arr) == (6, 5, 2)
    assert he.face.id == inner.id
    assert inner.number_of_inner_ccbs == 1        # the antenna is a hole of the face
    assert arr.number_of_curves == 4              # no history for this form
    assert arr.is_valid()
    assert_euler(arr)


def test_insert_in_face_interior_with_a_point():
    arr = a2.Arrangement("segment")
    arr.insert(square_segments())
    inner = arr.bounded_faces()[0]
    v = arr.insert_in_face_interior((2, 2), inner)
    assert isinstance(v, a2.Vertex) and v.is_isolated
    assert counts(arr) == (5, 4, 2)
    assert v.face.id == inner.id
    assert inner.number_of_isolated_vertices == 1
    assert_euler(arr)


def test_insert_point_in_face_interior():
    arr = a2.Arrangement("segment")
    arr.insert(square_segments())
    outer = arr.unbounded_face
    v = arr.insert_point_in_face_interior((10, 10), outer)
    assert v.is_isolated and v.face.is_unbounded
    assert counts(arr) == (5, 4, 2)
    assert arr.number_of_isolated_vertices == 1
    assert_euler(arr)


def test_insert_in_face_interior_violation_never_passes_unnoticed():
    """The inserted curve must not touch the face boundary (CGAL precondition).

    As with ``insert_non_intersecting`` CGAL 6.1 does not verify this; the only
    guarantee we can assert is that the breach shows up either as an exception or
    as ``is_valid() == False``.
    """
    arr = a2.Arrangement("segment")
    arr.insert(square_segments())
    inner = arr.bounded_faces()[0]
    try:
        arr.insert_in_face_interior(S((0, 0), (2, 2)), inner)   # touches a corner
    except a2.CGALError:
        assert counts(arr) == (4, 4, 2)
        assert arr.is_valid()
    else:
        assert not arr.is_valid()


def test_insert_from_left_vertex():
    arr = a2.Arrangement("segment")
    he0 = arr.insert_non_intersecting(S((0, 0), (2, 0)))
    right = he0.target                                  # the vertex (2,0)
    he = arr.insert_from_left_vertex(S((2, 0), (4, 2)), right)
    assert he.source.point == a2.Point(2, 0)            # directed away from v
    assert he.target.point == a2.Point(4, 2)
    assert counts(arr) == (3, 2, 1)
    assert arr.is_valid()


def test_insert_from_left_vertex_on_an_isolated_vertex():
    arr = a2.Arrangement("segment")
    v = arr.insert_point((0, 0))
    he = arr.insert_from_left_vertex(S((0, 0), (2, 2)), v)
    assert he.source.point == a2.Point(0, 0)
    assert counts(arr) == (2, 1, 1)
    assert arr.number_of_isolated_vertices == 0         # v is no longer isolated
    assert arr.is_valid()


def test_insert_from_right_vertex():
    arr = a2.Arrangement("segment")
    he0 = arr.insert_non_intersecting(S((0, 0), (2, 0)))
    left = he0.source                                   # the vertex (0,0)
    he = arr.insert_from_right_vertex(S((-2, 2), (0, 0)), left)
    assert he.source.point == a2.Point(0, 0)
    assert he.target.point == a2.Point(-2, 2)
    assert counts(arr) == (3, 2, 1)
    assert arr.is_valid()


def test_insert_from_left_vertex_rejects_the_wrong_end():
    arr = a2.Arrangement("segment")
    he0 = arr.insert_non_intersecting(S((0, 0), (2, 0)))
    with pytest.raises(a2.PreconditionError):
        # (0,0) is not the left (min) end of the curve (5,5)-(6,6)
        arr.insert_from_left_vertex(S((5, 5), (6, 6)), he0.source)
    assert counts(arr) == (2, 1, 1)
    assert arr.is_valid()


def test_insert_at_vertices_closes_a_face():
    arr = a2.Arrangement("segment")
    he0 = arr.insert_non_intersecting(S((0, 0), (2, 0)))
    left, right = he0.source, he0.target
    a = arr.insert_from_right_vertex(S((-2, 2), (0, 0)), left)
    b = arr.insert_from_left_vertex(S((2, 0), (4, 2)), right)
    assert counts(arr) == (4, 3, 1)                     # open chain of 3 edges
    arr.insert_at_vertices(S((-2, 2), (4, 2)), a.target, b.target)
    # the chain is closed: a new bounded face appears
    assert counts(arr) == (4, 4, 2)
    assert len(arr.bounded_faces()) == 1
    assert arr.is_valid()
    assert_euler(arr)


def test_insert_at_vertices_between_two_isolated_vertices():
    arr = a2.Arrangement("segment")
    p = arr.insert_point((0, 0))
    q = arr.insert_point((3, 3))
    he = arr.insert_at_vertices(S((0, 0), (3, 3)), p, q)
    # the returned halfedge is directed v1 -> v2
    assert (he.source.point, he.target.point) == (a2.Point(0, 0), a2.Point(3, 3))
    assert arr.number_of_isolated_vertices == 0
    assert counts(arr) == (2, 1, 1)
    assert arr.is_valid()


def test_insert_at_vertices_argument_order_sets_the_direction():
    arr = a2.Arrangement("segment")
    p = arr.insert_point((0, 0))
    q = arr.insert_point((3, 3))
    he = arr.insert_at_vertices(S((0, 0), (3, 3)), q, p)
    assert (he.source.point, he.target.point) == (a2.Point(3, 3), a2.Point(0, 0))
    assert arr.is_valid()


def test_insert_at_vertices_rejects_wrong_endpoints_without_corrupting():
    # CGAL_TRAPS_CHECKLIST: insert_at_vertices may free v1's isolated-vertex record
    # before validating.  The two-vertex overload validates first, so the
    # arrangement must survive a rejected call intact.
    arr = a2.Arrangement("segment")
    p = arr.insert_point((0, 0))
    q = arr.insert_point((1, 1))
    with pytest.raises(a2.PreconditionError):
        arr.insert_at_vertices(S((0, 0), (2, 2)), p, q)     # q is not (2,2)
    assert counts(arr) == (2, 0, 1)
    assert arr.number_of_isolated_vertices == 2
    assert arr.is_valid()


def test_aggregate_and_incremental_insertion_agree():
    segs = [S((0, 0), (4, 4)), S((0, 4), (4, 0)), S((0, 2), (4, 2))]
    agg = a2.Arrangement("segment")
    agg.insert(segs)
    inc = a2.Arrangement("segment")
    for s in segs:
        inc.insert(s)
    # all three pass through (2,2): V = 6 endpoints + 1 crossing = 7,
    # E = 3 * 2 = 6, F = 1 (a star has no bounded face)
    assert counts(agg) == (7, 6, 1)
    assert counts(agg) == counts(inc)
    assert edge_keys(agg) == edge_keys(inc)
    assert_euler(agg)


def test_insert_rejects_a_curve_of_an_unconvertible_kind():
    arr = a2.Arrangement("segment")
    with pytest.raises(a2.NotRepresentableError):
        arr.insert(a2.GeodesicArc.from_points((1, 0, 0), (0, 1, 0)))
    with pytest.raises(a2.NotRepresentableError):
        arr.insert(a2.CircleSegment.circle((0, 0), radius=1))
    assert counts(arr) == (0, 0, 1)


def test_insert_non_intersecting_rejects_a_non_x_monotone_curve():
    arr = a2.Arrangement("segment")
    # a V-shaped polyline is not a single segment
    with pytest.raises(ValueError):
        arr.insert_non_intersecting(a2.Polyline([(0, 0), (1, 1), (0, 2)]))
    assert counts(arr) == (0, 0, 1)


# ===========================================================================
# 3. Modification
# ===========================================================================

def test_modify_vertex_with_an_equal_point():
    arr = a2.Arrangement("segment")
    arr.insert(S((0, 0), (4, 0)))
    v = next(v for v in arr.vertices() if v.point.approx == (0.0, 0.0))
    w = arr.modify_vertex(v, (0, 0))
    assert w.id == v.id and w.point == a2.Point(0, 0)
    assert counts(arr) == (2, 1, 1)


def test_modify_vertex_rejects_a_different_point():
    arr = a2.Arrangement("segment")
    arr.insert(S((0, 0), (4, 0)))
    v = next(v for v in arr.vertices() if v.point.approx == (0.0, 0.0))
    with pytest.raises(a2.PreconditionError):
        arr.modify_vertex(v, (1, 1))
    assert counts(arr) == (2, 1, 1)
    assert arr.is_valid()


def test_modify_edge_with_an_equal_curve_keeps_the_history():
    arr = a2.Arrangement("segment")
    ch = arr.insert(S((0, 0), (4, 0)))
    he = arr.modify_edge(arr.edges()[0], S((4, 0), (0, 0)))    # same geometry, flipped
    assert he.curve == S((0, 0), (4, 0))
    assert [c.id for c in he.originating_curves()] == [ch.id]
    assert counts(arr) == (2, 1, 1)
    assert arr.is_valid()


def test_modify_edge_rejects_a_different_curve():
    arr = a2.Arrangement("segment")
    arr.insert(S((0, 0), (4, 0)))
    with pytest.raises(a2.PreconditionError):
        arr.modify_edge(arr.edges()[0], S((0, 0), (5, 0)))
    assert counts(arr) == (2, 1, 1)
    assert arr.is_valid()


def test_split_edge_at_a_point_is_history_aware():
    arr = a2.Arrangement("segment")
    ch = arr.insert(S((0, 0), (4, 0)))
    he = arr.edges()[0]
    source_before = he.source.point
    out = arr.split_edge(he, (2, 0))
    # V 2 -> 3, E 1 -> 2, the returned halfedge keeps he's source and ends at the split
    assert counts(arr) == (3, 2, 1)
    assert out.source.point == source_before
    assert out.target.point == a2.Point(2, 0)
    assert ch.number_of_induced_edges == 2
    assert edge_keys(arr) == [((0.0, 0.0), (2.0, 0.0)), ((2.0, 0.0), (4.0, 0.0))]
    for e in arr.edges():
        assert [c.id for c in e.originating_curves()] == [ch.id]
    assert arr.is_valid()
    assert_euler(arr)


def test_split_edge_with_two_explicit_curves():
    arr = a2.Arrangement("segment")
    ch = arr.insert(S((0, 0), (4, 0)))
    arr.split_edge(arr.edges()[0], S((0, 0), (1, 0)), S((1, 0), (4, 0)))
    assert counts(arr) == (3, 2, 1)
    assert edge_keys(arr) == [((0.0, 0.0), (1.0, 0.0)), ((1.0, 0.0), (4.0, 0.0))]
    # the binding copies the edge's originating-curve set onto both halves
    for e in arr.edges():
        assert [c.id for c in e.originating_curves()] == [ch.id]
    assert ch.number_of_induced_edges == 2
    assert arr.is_valid()


def test_split_edge_with_a_bad_argument_count():
    arr = a2.Arrangement("segment")
    arr.insert(S((0, 0), (4, 0)))
    with pytest.raises(TypeError):
        arr.split_edge(arr.edges()[0])
    with pytest.raises(TypeError):
        arr.split_edge(arr.edges()[0], S((0, 0), (1, 0)), S((1, 0), (2, 0)),
                       S((2, 0), (4, 0)))


def test_merge_edge_history_form():
    arr = a2.Arrangement("segment")
    ch = arr.insert(S((0, 0), (4, 0)))
    arr.split_edge(arr.edges()[0], (2, 0))
    left, right = sorted(arr.edges(), key=lambda e: e.curve.left.x)
    he = arr.merge_edge(left, right)
    assert he.curve == S((0, 0), (4, 0))
    assert counts(arr) == (2, 1, 1)
    assert ch.number_of_induced_edges == 1
    assert arr.is_valid()
    assert_euler(arr)


def test_merge_edge_with_an_explicit_curve():
    arr = a2.Arrangement("segment")
    ch = arr.insert(S((0, 0), (4, 0)))
    arr.split_edge(arr.edges()[0], S((0, 0), (1, 0)), S((1, 0), (4, 0)))
    left, right = sorted(arr.edges(), key=lambda e: e.curve.left.x)
    he = arr.merge_edge(left, right, S((0, 0), (4, 0)))
    assert he.curve == S((0, 0), (4, 0))
    assert [c.id for c in he.originating_curves()] == [ch.id]
    assert counts(arr) == (2, 1, 1)
    assert arr.is_valid()


def test_merge_edge_refuses_edges_of_different_input_curves():
    # arrangement_with_history gotcha 7: are_mergeable also compares the
    # originating-curve sets, so two collinear edges from two different inputs
    # cannot be merged by the history-aware form.
    arr = a2.Arrangement("segment")
    arr.insert(S((0, 0), (2, 0)))
    arr.insert(S((2, 0), (4, 0)))
    left, right = sorted(arr.edges(), key=lambda e: e.curve.left.x)
    with pytest.raises(ValueError):
        arr.merge_edge(left, right)
    assert counts(arr) == (3, 2, 1)
    assert arr.is_valid()


def test_merge_edge_explicit_form_works_across_input_curves():
    arr = a2.Arrangement("segment")
    c1 = arr.insert(S((0, 0), (2, 0)))
    c2 = arr.insert(S((2, 0), (4, 0)))
    left, right = sorted(arr.edges(), key=lambda e: e.curve.left.x)
    he = arr.merge_edge(left, right, S((0, 0), (4, 0)))
    assert counts(arr) == (2, 1, 1)
    # the surviving edge keeps only the first edge's originating-curve set
    assert [c.id for c in he.originating_curves()] == [c1.id]
    assert c1.number_of_induced_edges == 1
    assert c2.number_of_induced_edges == 0
    assert arr.is_valid()


def test_merge_edge_refuses_edges_without_a_shared_vertex():
    arr = a2.Arrangement("segment")
    arr.insert_non_intersecting_curves([S((0, 0), (1, 0)), S((2, 0), (3, 0))])
    e1, e2 = sorted(arr.edges(), key=lambda e: e.curve.left.x)
    with pytest.raises(ValueError):
        arr.merge_edge(e1, e2)
    assert counts(arr) == (4, 2, 1)


def test_remove_edge_removes_endpoints_that_become_isolated():
    arr = a2.Arrangement("segment")
    arr.insert(S((0, 0), (4, 0)))
    arr.split_edge(arr.edges()[0], (2, 0))
    left = next(e for e in arr.edges() if e.curve.left.approx == (0.0, 0.0))
    face = arr.remove_edge(left)                       # defaults: remove both ends
    # (0,0) becomes isolated -> removed; (2,0) keeps degree 1 -> kept
    assert counts(arr) == (2, 1, 1)
    assert vertex_points(arr) == [(2.0, 0.0), (4.0, 0.0)]
    assert face.is_unbounded
    assert arr.is_valid()
    assert_euler(arr)


def test_remove_edge_can_keep_the_endpoints():
    arr = a2.Arrangement("segment")
    arr.insert(S((0, 0), (4, 0)))
    arr.split_edge(arr.edges()[0], (2, 0))
    left = next(e for e in arr.edges() if e.curve.left.approx == (0.0, 0.0))
    arr.remove_edge(left, remove_source=False, remove_target=False)
    # (0,0) stays behind as an isolated vertex
    assert counts(arr) == (3, 1, 1)
    assert arr.number_of_isolated_vertices == 1
    assert vertex_points(arr) == [(0.0, 0.0), (2.0, 0.0), (4.0, 0.0)]
    assert arr.is_valid()
    assert_euler(arr)


def test_remove_edge_opens_a_face():
    arr = a2.Arrangement("segment")
    arr.insert(square_segments())
    face = arr.remove_edge(arr.edges()[0])
    # the square's interior merges into the unbounded face
    assert counts(arr) == (4, 3, 1)
    assert face.is_unbounded
    assert arr.is_valid()
    assert_euler(arr)


def test_remove_edge_does_not_merge_collinear_neighbours():
    # Arrangement_2::remove_edge (the member function this binds) only drops an
    # endpoint that becomes *isolated*; it never merges two surviving collinear
    # edges the way the free CGAL::remove_edge does.
    arr = a2.Arrangement("segment")
    arr.insert([S((0, 0), (6, 0)), S((2, 0), (2, 3))])
    assert counts(arr) == (4, 3, 1)
    spur = next(e for e in arr.edges() if e.curve.is_vertical)
    arr.remove_edge(spur)
    assert counts(arr) == (3, 2, 1)
    mid = next(v for v in arr.vertices() if v.point.approx == (2.0, 0.0))
    assert mid.degree == 2                     # still there, not merged away
    assert edge_keys(arr) == [((0.0, 0.0), (2.0, 0.0)), ((2.0, 0.0), (6.0, 0.0))]
    assert arr.is_valid()


def test_remove_edge_refuses_an_invalid_handle():
    arr = a2.Arrangement("segment")
    arr.insert(S((0, 0), (4, 0)))
    he = arr.edges()[0]
    arr.remove_edge(he)
    assert not he.is_valid
    with pytest.raises(a2.InvalidHandleError):
        arr.remove_edge(he)


def test_remove_vertex_merges_two_mergeable_edges():
    arr = a2.Arrangement("segment")
    arr.insert(S((0, 0), (4, 0)))
    arr.split_edge(arr.edges()[0], (2, 0))
    mid = next(v for v in arr.vertices() if v.point.approx == (2.0, 0.0))
    assert arr.remove_vertex(mid) is True
    assert counts(arr) == (2, 1, 1)
    assert edge_keys(arr) == [((0.0, 0.0), (4.0, 0.0))]
    assert arr.is_valid()
    assert_euler(arr)


def test_remove_vertex_refuses_a_bend():
    arr = a2.Arrangement("segment")
    arr.insert([S((0, 0), (2, 0)), S((2, 0), (3, 3))])
    mid = next(v for v in arr.vertices() if v.point.approx == (2.0, 0.0))
    assert mid.degree == 2
    assert arr.remove_vertex(mid) is False       # the two curves are not mergeable
    assert counts(arr) == (3, 2, 1)
    assert arr.is_valid()


def test_remove_vertex_refuses_a_higher_degree_vertex():
    arr = a2.Arrangement("segment")
    arr.insert([S((0, 0), (4, 0)), S((2, 0), (2, 3))])
    mid = next(v for v in arr.vertices() if v.point.approx == (2.0, 0.0))
    assert mid.degree == 3
    assert arr.remove_vertex(mid) is False
    assert counts(arr) == (4, 3, 1)


def test_remove_vertex_on_an_isolated_vertex():
    arr = a2.Arrangement("segment")
    v = arr.insert_point((2, 2))
    assert arr.remove_vertex(v) is True
    assert counts(arr) == (0, 0, 1)
    assert not v.is_valid


def test_remove_isolated_vertex_returns_its_face():
    arr = a2.Arrangement("segment")
    arr.insert(square_segments())
    inner = arr.bounded_faces()[0]
    v = arr.insert_point_in_face_interior((2, 2), inner)
    assert counts(arr) == (5, 4, 2)
    face = arr.remove_isolated_vertex(v)
    assert face.id == inner.id and not face.is_unbounded
    assert counts(arr) == (4, 4, 2)
    assert arr.number_of_isolated_vertices == 0
    assert not v.is_valid
    with pytest.raises(a2.InvalidHandleError):
        v.point
    assert arr.is_valid()
    assert_euler(arr)


def test_remove_isolated_vertex_refuses_a_connected_vertex():
    arr = a2.Arrangement("segment")
    arr.insert(square_segments())
    # arr2d checks `is_isolated()` itself (so the check survives an assertions-off build),
    # hence a plain ValueError rather than a CGAL PreconditionError.
    with pytest.raises(ValueError, match="isolated"):
        arr.remove_isolated_vertex(arr.vertices()[0])
    assert counts(arr) == (4, 4, 2)
    assert arr.is_valid()


def test_remove_curve_drops_only_its_own_edges(square_arr):
    chord = next(c for c in square_arr.curves()
                 if c.curve == S((-1, 2), (5, 2)))
    assert chord.number_of_induced_edges == 3
    removed = square_arr.remove_curve(chord)
    assert removed == 3
    # back to the square, but its left and right sides stay split at y = 2
    assert counts(square_arr) == (6, 6, 2)
    assert square_arr.is_valid()
    assert_euler(square_arr)


def test_remove_curve_erases_the_history_node(square_arr):
    # CGAL's _remove_curve erases the node from m_curves and the handle dangles
    # (arrangement_with_history.md 2.12), so number_of_curves DOES drop.
    chord = next(c for c in square_arr.curves() if c.curve == S((-1, 2), (5, 2)))
    before = square_arr.number_of_curves
    square_arr.remove_curve(chord)
    assert square_arr.number_of_curves == before - 1
    assert not chord.is_valid
    with pytest.raises(a2.InvalidHandleError):
        chord.number_of_induced_edges


def test_remove_curve_keeps_edges_shared_with_another_curve():
    arr = a2.Arrangement("segment")
    c1 = arr.insert(S((0, 0), (4, 0)))
    c2 = arr.insert(S((2, 0), (6, 0)))
    # the overlap splits both curves: edges (0,0)-(2,0), (2,0)-(4,0), (4,0)-(6,0)
    assert counts(arr) == (4, 3, 1)
    shared = next(e for e in arr.edges() if e.curve.left.approx == (2.0, 0.0))
    assert sorted(c.id for c in shared.originating_curves()) == sorted([c1.id, c2.id])
    removed = arr.remove_curve(c1)
    # only (0,0)-(2,0) belonged to c1 alone
    assert removed == 1
    assert counts(arr) == (3, 2, 1)
    assert [c.id for c in shared.originating_curves()] == [c2.id]
    assert c2.number_of_induced_edges == 2
    assert arr.is_valid()
    assert_euler(arr)


def test_remove_curve_of_a_duplicate_removes_no_edge():
    arr = a2.Arrangement("segment")
    c1 = arr.insert(S((0, 0), (4, 0)))
    c2 = arr.insert(S((0, 0), (4, 0)))          # the very same segment twice
    assert counts(arr) == (2, 1, 1)
    assert arr.number_of_curves == 2
    edge = arr.edges()[0]
    assert edge.number_of_originating_curves == 2
    assert arr.remove_curve(c1) == 0            # the edge survives through c2
    assert counts(arr) == (2, 1, 1)
    assert [c.id for c in arr.edges()[0].originating_curves()] == [c2.id]


def test_clear_invalidates_every_handle():
    arr = a2.Arrangement("segment")
    handles = arr.insert(square_segments())
    v, e, f = arr.vertices()[0], arr.edges()[0], arr.bounded_faces()[0]
    arr.clear()
    assert counts(arr) == (0, 0, 1)
    assert arr.number_of_curves == 0
    assert not v.is_valid and not e.is_valid and not f.is_valid
    assert not handles[0].is_valid
    assert arr.is_valid()


def test_copy_preserves_counts_history_and_data():
    arr = a2.Arrangement("segment")
    arr.insert(square_segments() + [S((-1, 2), (5, 2))])
    arr.vertices()[0].data = {"tag": 42}
    clone = arr.copy()
    assert counts(clone) == counts(arr) == (8, 9, 3)
    assert clone.number_of_curves == 5
    assert sorted(str(c.curve) for c in clone.curves()) == \
        sorted(str(c.curve) for c in arr.curves())
    assert {"tag": 42} in [v.data for v in clone.vertices()]
    assert clone.is_valid()
    # the copy is independent
    clone.clear()
    assert counts(arr) == (8, 9, 3)
    assert counts(_copy.copy(arr)) == (8, 9, 3)
    assert counts(_copy.deepcopy(arr)) == (8, 9, 3)


def test_assign_replaces_the_content():
    src = a2.Arrangement("segment")
    src.insert(square_segments())
    dst = a2.Arrangement("segment")
    dst.insert(S((10, 10), (11, 11)))
    dst.assign(src)
    assert counts(dst) == (4, 4, 2)
    assert dst.number_of_curves == 4
    assert dst.is_valid()


# ===========================================================================
# 4. Curve history
# ===========================================================================

def test_curves_returns_the_inputs_in_insertion_order():
    arr = a2.Arrangement("segment")
    segs = square_segments()
    arr.insert(segs)
    assert [c.curve for c in arr.curves()] == segs
    assert arr.number_of_curves == len(segs)
    assert all(c.is_valid and c.arrangement is arr for c in arr.curves())


def test_induced_edges_of_the_chord(square_arr):
    chord = next(c for c in square_arr.curves() if c.curve == S((-1, 2), (5, 2)))
    edges = chord.induced_edges()
    # the chord is cut by the square's two vertical sides into 3 pieces
    assert len(edges) == 3 == chord.number_of_induced_edges
    assert sorted((e.curve.left.approx, e.curve.right.approx) for e in edges) == [
        ((-1.0, 2.0), (0.0, 2.0)),
        ((0.0, 2.0), (4.0, 2.0)),
        ((4.0, 2.0), (5.0, 2.0)),
    ]


def test_induced_edges_of_a_split_side(square_arr):
    # the left side (0,4)-(0,0) is cut in two by the chord at (0,2)
    left_side = next(c for c in square_arr.curves() if c.curve == S((0, 4), (0, 0)))
    assert left_side.number_of_induced_edges == 2
    assert sorted((e.curve.left.approx, e.curve.right.approx)
                  for e in left_side.induced_edges()) == [
        ((0.0, 0.0), (0.0, 2.0)),
        ((0.0, 2.0), (0.0, 4.0)),
    ]


def test_originating_curves_of_every_edge(square_arr):
    # in this figure no two input curves overlap, so every edge has exactly one
    for e in square_arr.edges():
        assert e.number_of_originating_curves == 1
        assert len(e.originating_curves()) == 1
        assert e.originating_curves()[0].arrangement is square_arr


def test_originating_curves_of_an_overlap():
    arr = a2.Arrangement("segment")
    c1 = arr.insert(S((0, 0), (4, 0)))
    c2 = arr.insert(S((2, 0), (6, 0)))
    by_left = {e.curve.left.approx: e for e in arr.edges()}
    assert [c.id for c in by_left[(0.0, 0.0)].originating_curves()] == [c1.id]
    assert sorted(c.id for c in by_left[(2.0, 0.0)].originating_curves()) == \
        sorted([c1.id, c2.id])
    assert [c.id for c in by_left[(4.0, 0.0)].originating_curves()] == [c2.id]
    assert c1.number_of_induced_edges == 2
    assert c2.number_of_induced_edges == 2


def test_history_survives_a_split_then_a_merge():
    arr = a2.Arrangement("segment")
    ch = arr.insert(S((0, 0), (4, 0)))
    arr.split_edge(arr.edges()[0], (1, 0))
    assert ch.number_of_induced_edges == 2
    e1 = next(e for e in arr.edges() if e.curve.left.approx == (0.0, 0.0))
    arr.split_edge(e1, (Fraction(1, 2), 0))
    assert ch.number_of_induced_edges == 3
    assert counts(arr) == (4, 3, 1)
    a, b = sorted(arr.edges(), key=lambda e: e.curve.left.x)[:2]
    arr.merge_edge(a, b)
    assert ch.number_of_induced_edges == 2
    assert counts(arr) == (3, 2, 1)
    assert arr.is_valid()


def test_remove_edge_keeps_the_curve_node_in_the_history():
    # arrangement_with_history.md gotcha 9: remove_edge unregisters the edge from
    # its curves but leaves the Curve_halfedges node behind, so number_of_curves
    # stays put and the curve reports zero induced edges.  (remove_curve, in
    # contrast, erases the node -- see test_remove_curve_erases_the_history_node.)
    arr = a2.Arrangement("segment")
    ch = arr.insert(S((0, 0), (4, 0)))
    arr.remove_edge(arr.edges()[0])
    assert counts(arr) == (0, 0, 1)
    assert arr.number_of_curves == 1
    assert ch.is_valid
    assert ch.number_of_induced_edges == 0
    assert ch.induced_edges() == []
    assert ch.curve == S((0, 0), (4, 0))


def test_history_less_insertion_forms_leave_edges_without_originating_curves():
    # gotcha 9: the inherited base modification functions take a data-extended
    # curve with an empty data list, so their edges belong to no input curve.
    arr = a2.Arrangement("segment")
    arr.insert(square_segments())
    he = arr.insert_in_face_interior(S((1, 1), (3, 1)), arr.bounded_faces()[0])
    assert he.number_of_originating_curves == 0
    assert he.originating_curves() == []
    assert arr.number_of_curves == 4          # unchanged by the insertion

    other = a2.Arrangement("segment")
    p = other.insert_point((0, 0))
    q = other.insert_point((3, 3))
    at = other.insert_at_vertices(S((0, 0), (3, 3)), p, q)
    assert at.number_of_originating_curves == 0
    assert other.number_of_curves == 0


def test_curve_handle_curve_is_the_original_not_a_piece(square_arr):
    chord = next(c for c in square_arr.curves() if c.curve == S((-1, 2), (5, 2)))
    assert chord.curve.left == a2.Point(-1, 2)
    assert chord.curve.right == a2.Point(5, 2)


# ===========================================================================
# 5. Validity and the Euler characteristic
# ===========================================================================

def test_is_valid_on_the_canonical_figures(square_arr):
    assert square_arr.is_valid() is True
    empty = a2.Arrangement("segment")
    assert empty.is_valid() is True


@pytest.mark.parametrize("segs,expected,components", [
    # a single segment: 2 vertices, 1 edge, 1 face, 1 component
    ([S((0, 0), (1, 0))], (2, 1, 1), 1),
    # two disjoint segments
    ([S((0, 0), (1, 0)), S((5, 5), (6, 5))], (4, 2, 1), 2),
    # a closed square
    (square_segments(), (4, 4, 2), 1),
    # two disjoint triangles: V=6 E=6 F=3
    ([S((0, 0), (2, 0)), S((2, 0), (1, 2)), S((1, 2), (0, 0)),
      S((10, 0), (12, 0)), S((12, 0), (11, 2)), S((11, 2), (10, 0))], (6, 6, 3), 2),
    # an X: 4 tips + the crossing = 5 vertices, 4 edges, 1 face
    ([S((0, 0), (4, 4)), S((0, 4), (4, 0))], (5, 4, 1), 1),
])
def test_euler_characteristic_on_hand_built_figures(segs, expected, components):
    arr = a2.Arrangement("segment")
    arr.insert(segs)
    assert counts(arr) == expected
    assert connected_components(arr) == components
    v, e, f = expected
    assert v - e + f == 1 + components
    assert arr.is_valid()
    assert_euler(arr)


def test_euler_characteristic_counts_isolated_vertices_as_components():
    arr = a2.Arrangement("segment")
    arr.insert(square_segments())
    arr.insert_point_in_face_interior((2, 2), arr.bounded_faces()[0])
    # V=5 E=4 F=2 -> 3 = 1 + C with C = 1 (square) + 1 (isolated vertex)
    assert counts(arr) == (5, 4, 2)
    assert connected_components(arr) == 2
    assert_euler(arr)


@pytest.mark.parametrize("seed", [1, 2, 3, 5, 8, 13, 21, 34])
def test_euler_characteristic_on_random_segments(seed):
    rng = random.Random(seed)
    arr = a2.Arrangement("segment")
    arr.insert(random_segments(rng, 10))
    assert arr.is_valid()
    assert_euler(arr)


@pytest.mark.parametrize("seed", [101, 202, 303])
def test_random_incremental_insertion_stays_valid_and_eulerian(seed):
    rng = random.Random(seed)
    arr = a2.Arrangement("segment")
    for seg in random_segments(rng, 12):
        arr.insert(seg)
        assert arr.is_valid()
        assert_euler(arr)


@pytest.mark.parametrize("seed", [7, 11])
def test_random_arrangement_survives_a_removal_round(seed):
    rng = random.Random(seed)
    arr = a2.Arrangement("segment")
    curves = arr.insert(random_segments(rng, 8))
    assert arr.is_valid()
    for ch in curves:
        if ch.is_valid:
            arr.remove_curve(ch)
            assert arr.is_valid()
            assert_euler(arr)
    # every input curve is gone, so no edge can be left
    assert arr.number_of_edges == 0
    assert arr.number_of_curves == 0


@pytest.mark.parametrize("seed", [4, 6])
def test_random_aggregate_matches_incremental(seed):
    rng = random.Random(seed)
    segs = random_segments(rng, 9)
    agg = a2.Arrangement("segment")
    agg.insert(segs)
    inc = a2.Arrangement("segment")
    for s in segs:
        inc.insert(s)
    assert counts(agg) == counts(inc)
    assert edge_keys(agg) == edge_keys(inc)
    assert vertex_points(agg) == vertex_points(inc)


# ===========================================================================
# 6. Traits functors
# ===========================================================================

def test_traits_facade_identity():
    arr = a2.Arrangement("segment")
    t = arr.traits
    assert isinstance(t, a2.Traits)
    assert t.kind is a2.Kind.SEGMENT
    assert t.dimension == 2
    assert t is arr.traits                                  # cached per arrangement
    assert t is a2.traits("segment")                        # and globally per kind
    assert t is a2.traits(a2.Kind.SEGMENT)
    assert t == a2.traits("segment")
    assert repr(t) == "Traits(kind='segment')"


def test_traits_compare_x_xy_and_equal():
    t = a2.traits("segment")
    assert t.compare_x((0, 0), (1, 5)) == -1
    assert t.compare_x((1, 0), (1, 5)) == 0
    assert t.compare_x((2, 0), (1, 5)) == 1
    assert t.compare_xy((1, 0), (1, 5)) == -1               # same x, smaller y
    assert t.compare_xy((1, 5), (1, 5)) == 0
    assert t.equal((1, 2), (1, 2)) is True
    assert t.equal((1, 2), (1, 3)) is False


def test_traits_curves_equal_ignores_direction():
    t = a2.traits("segment")
    assert t.curves_equal(S((0, 0), (1, 1)), S((1, 1), (0, 0))) is True
    assert t.curves_equal(S((0, 0), (1, 1)), S((0, 0), (1, 2))) is False


def test_traits_compare_y_at_x_family():
    t = a2.traits("segment")
    base = S((0, 0), (4, 0))
    assert t.compare_y_at_x((2, 1), base) == 1
    assert t.compare_y_at_x((2, 0), base) == 0
    assert t.compare_y_at_x((2, -1), base) == -1
    rising, falling = S((0, 0), (4, 4)), S((0, 4), (4, 0))
    assert t.compare_y_at_x_left(rising, falling, (2, 2)) == -1
    assert t.compare_y_at_x_right(rising, falling, (2, 2)) == 1


def test_traits_min_max_vertex_and_opposite():
    t = a2.traits("segment")
    seg = S((4, 4), (0, 0))
    assert t.min_vertex(seg) == a2.Point(0, 0)
    assert t.max_vertex(seg) == a2.Point(4, 4)
    assert t.opposite(seg).source == a2.Point(0, 0)
    assert t.compare_endpoints_xy(S((4, 0), (0, 0))) == 1
    assert t.compare_endpoints_xy(S((0, 0), (4, 0))) == -1


def test_traits_is_vertical_and_is_in_x_range():
    t = a2.traits("segment")
    assert t.is_vertical(S((1, 0), (1, 4))) is True
    assert t.is_vertical(S((0, 0), (1, 4))) is False
    assert t.is_in_x_range(S((0, 0), (4, 0)), (2, 9)) is True
    assert t.is_in_x_range(S((0, 0), (4, 0)), (5, 0)) is False


def test_traits_make_x_monotone_split_and_merge():
    t = a2.traits("segment")
    assert t.make_x_monotone(S((0, 0), (4, 4))) == [S((0, 0), (4, 4))]
    left, right = t.split(S((0, 0), (4, 4)), (2, 2))
    assert (left, right) == (S((0, 0), (2, 2)), S((2, 2), (4, 4)))
    assert t.are_mergeable(left, right) is True
    assert t.merge(left, right) == S((0, 0), (4, 4))
    assert t.are_mergeable(S((0, 0), (1, 0)), S((2, 0), (3, 0))) is False


def test_traits_intersect():
    t = a2.traits("segment")
    # the diagonals of the square cross transversally at (2,2): multiplicity 1
    assert t.intersect(S((0, 0), (4, 4)), S((0, 4), (4, 0))) == [(a2.Point(2, 2), 1)]
    overlap = t.intersect(S((0, 0), (4, 0)), S((2, 0), (6, 0)))
    assert len(overlap) == 1 and isinstance(overlap[0], a2.Curve)
    assert overlap[0] == S((2, 0), (4, 0))
    assert t.intersect(S((0, 0), (1, 0)), S((0, 9), (1, 9))) == []


def test_traits_trim_and_construct_x_monotone_curve():
    t = a2.traits("segment")
    assert t.trim(S((0, 0), (4, 4)), (1, 1), (3, 3)) == S((1, 1), (3, 3))
    assert t.construct_x_monotone_curve((0, 0), (4, 4)) == S((0, 0), (4, 4))


def test_traits_parameter_space_of_a_bounded_segment():
    t = a2.traits("segment")
    seg = S((0, 0), (1, 1))
    assert t.parameter_space_in_x(seg, "min") == "interior"
    assert t.parameter_space_in_x(seg, "max") == "interior"
    assert t.parameter_space_in_y(seg, "min") == "interior"
    assert t.parameter_space_in_y(seg, "max") == "interior"


def test_traits_approximate_and_approximate_point():
    t = a2.traits("segment")
    assert t.approximate(S((0, 0), (3, 4)), 1e-3) == [(0.0, 0.0), (3.0, 4.0)]
    assert t.approximate_point((3, 4), 0) == 3.0
    assert t.approximate_point((3, 4), 1) == 4.0


def test_traits_reject_foreign_geometry():
    t = a2.traits("segment")
    with pytest.raises(ValueError):
        t.compare_x((0, 0, 0), (1, 1, 1))            # a 3D point for a planar kind
    with pytest.raises(a2.NotRepresentableError):
        t.intersect(a2.CircleSegment.circle((0, 0), radius=1), S((0, 0), (1, 1)))


def test_traits_output_lists_are_fresh():
    t = a2.traits("segment")
    first = t.make_x_monotone(S((0, 0), (1, 1)))
    second = t.make_x_monotone(S((0, 0), (2, 2)))
    assert len(first) == len(second) == 1        # no accumulation between calls


# ===========================================================================
# 7. Bulk export and bbox
# ===========================================================================

def _as_tuples(rows):
    """Normalise a bulk-export result (numpy array or list of tuples) to tuples."""
    return [tuple(float(x) for x in row) for row in rows]


def _as_int_tuples(rows):
    return [tuple(int(x) for x in row) for row in rows]


def test_vertex_coordinates_matches_vertices(square_arr):
    coords = _as_tuples(square_arr.vertex_coordinates())
    assert len(coords) == square_arr.number_of_vertices == 8
    assert coords == [v.point.approx for v in square_arr.vertices()]
    assert sorted(coords) == sorted([
        (-1.0, 2.0), (0.0, 0.0), (0.0, 2.0), (0.0, 4.0),
        (4.0, 0.0), (4.0, 2.0), (4.0, 4.0), (5.0, 2.0),
    ])


def test_edge_vertex_indices_matches_edges(square_arr):
    coords = _as_tuples(square_arr.vertex_coordinates())
    idx = _as_int_tuples(square_arr.edge_vertex_indices())
    assert len(idx) == square_arr.number_of_edges == 9
    for (i, j), edge in zip(idx, square_arr.edges()):
        assert i >= 0 and j >= 0                     # no vertices at infinity here
        assert coords[i] == edge.source.point.approx
        assert coords[j] == edge.target.point.approx
    # every edge of the reference figure, as an unordered endpoint pair
    assert sorted(tuple(sorted((coords[i], coords[j]))) for i, j in idx) == sorted([
        ((0.0, 0.0), (4.0, 0.0)),      # bottom
        ((4.0, 0.0), (4.0, 2.0)),      # right lower
        ((4.0, 2.0), (4.0, 4.0)),      # right upper
        ((0.0, 4.0), (4.0, 4.0)),      # top
        ((0.0, 2.0), (0.0, 4.0)),      # left upper
        ((0.0, 0.0), (0.0, 2.0)),      # left lower
        ((-1.0, 2.0), (0.0, 2.0)),     # chord tail (left)
        ((0.0, 2.0), (4.0, 2.0)),      # chord inside
        ((4.0, 2.0), (5.0, 2.0)),      # chord tail (right)
    ])


def test_face_boundaries(square_arr):
    coords = _as_tuples(square_arr.vertex_coordinates())
    boundaries = square_arr.face_boundaries()
    assert len(boundaries) == square_arr.number_of_faces == 3
    faces = square_arr.faces()
    for face, cycles in zip(faces, boundaries):
        assert len(cycles) == face.number_of_outer_ccbs + face.number_of_inner_ccbs
        for cycle in cycles:
            assert all(0 <= int(i) < len(coords) for i in cycle)
    by_face = dict(zip((f.id for f in faces), boundaries))
    # the unbounded face has no outer CCB and exactly one hole; that hole walks the
    # 6 square edges plus the 2 chord antennae twice each -> 10 halfedges
    uf = square_arr.unbounded_face
    assert uf.number_of_outer_ccbs == 0 and uf.number_of_inner_ccbs == 1
    assert len(by_face[uf.id]) == 1
    assert len(by_face[uf.id][0]) == 10
    # each bounded face is a 4-gon
    for f in square_arr.bounded_faces():
        assert len(by_face[f.id]) == 1
        assert len(by_face[f.id][0]) == 4
        assert sorted(coords[int(i)] for i in by_face[f.id][0]) in (
            sorted([(0.0, 0.0), (4.0, 0.0), (4.0, 2.0), (0.0, 2.0)]),
            sorted([(0.0, 2.0), (4.0, 2.0), (4.0, 4.0), (0.0, 4.0)]),
        )


def test_face_boundaries_of_an_empty_arrangement():
    arr = a2.Arrangement("segment")
    boundaries = arr.face_boundaries()
    assert len(boundaries) == 1        # only the unbounded face
    assert boundaries[0] == []         # which has no cycle at all


def test_approximate_edges(square_arr):
    polylines = square_arr.approximate_edges(1e-3)
    assert len(polylines) == square_arr.number_of_edges == 9
    for poly, edge in zip(polylines, square_arr.edges()):
        pts = _as_tuples(poly)
        # a segment is approximated by its two endpoints, source -> target
        assert len(pts) == 2
        assert pts[0] == edge.source.point.approx
        assert pts[1] == edge.target.point.approx


def test_approximate_edges_accepts_a_clip_box(square_arr):
    # a bounded kind ignores the box but must still accept it
    polylines = square_arr.approximate_edges(1e-3, bbox=(-10, -10, 10, 10))
    assert len(polylines) == 9


def test_bbox_of_the_reference_figure(square_arr):
    # x from -1 (chord tip) to 5 (chord tip); y from 0 to 4 (square)
    assert square_arr.bbox() == (-1.0, 0.0, 5.0, 4.0)


def test_bbox_of_an_empty_arrangement():
    assert a2.Arrangement("segment").bbox() == (0.0, 0.0, 0.0, 0.0)


def test_bbox_of_isolated_vertices_only():
    arr = a2.Arrangement("segment")
    arr.insert_point((-3, 7))
    arr.insert_point((2, -1))
    assert arr.bbox() == (-3.0, -1.0, 2.0, 7.0)


def test_bulk_exports_of_an_empty_arrangement():
    arr = a2.Arrangement("segment")
    assert len(arr.vertex_coordinates()) == 0
    assert len(arr.edge_vertex_indices()) == 0
    assert arr.approximate_edges(1e-3) == []


# ===========================================================================
# 8. Faces, halfedges and vertices of a segment arrangement
# ===========================================================================

def test_face_structure_of_the_reference_figure(square_arr):
    uf = square_arr.unbounded_face
    assert uf.is_unbounded and not uf.is_fictitious
    assert not uf.has_outer_ccb
    with pytest.raises(ValueError):
        uf.outer_ccb()
    assert len(uf.holes()) == 1 == len(uf.inner_ccbs())
    bounded = square_arr.bounded_faces()
    assert len(bounded) == 2
    for f in bounded:
        assert f.has_outer_ccb
        assert len(f.outer_ccb()) == 4          # each half of the square is a 4-gon
        assert len(f.edges()) == 4
        assert f.number_of_inner_ccbs == 0
        assert f.number_of_isolated_vertices == 0
    # the two halves are adjacent to each other and to the unbounded face
    ids = {f.id for f in bounded} | {uf.id}
    for f in bounded:
        assert {g.id for g in f.adjacent_faces()} <= ids


def test_face_polygon_of_a_bounded_face():
    arr = a2.Arrangement("segment")
    arr.insert(square_segments())
    pwh = arr.bounded_faces()[0].polygon()
    assert isinstance(pwh, a2.PolygonWithHoles)
    assert pwh.holes == ()
    assert len(pwh.outer.curves) == 4
    assert pwh.outer.area() == 16               # the 4x4 square
    assert pwh.outer.is_simple()


def test_face_polygon_of_the_unbounded_face_is_unsupported():
    arr = a2.Arrangement("segment")
    arr.insert(square_segments())
    with pytest.raises(a2.UnsupportedError):
        arr.unbounded_face.polygon()


def test_face_boundary_points():
    arr = a2.Arrangement("segment")
    arr.insert(square_segments())
    outer, holes = arr.bounded_faces()[0].boundary_points(1e-3)
    assert holes == []
    assert outer[0] == outer[-1]                # closed ring
    assert sorted(set(outer)) == [(0.0, 0.0), (0.0, 4.0), (4.0, 0.0), (4.0, 4.0)]


def test_halfedge_topology(square_arr):
    face = square_arr.bounded_faces()[0]
    ccb = face.outer_ccb()
    for he in ccb:
        assert he.face.id == face.id
        assert he.is_on_outer_ccb and not he.is_on_inner_ccb
        assert not he.is_fictitious
        assert he.twin.twin.id == he.id
        assert he.next.prev.id == he.id
        assert he.prev.next.id == he.id
        assert he.next.source.id == he.target.id
        assert he.edge_id == min(he.id, he.twin.id) == he.twin.edge_id
        assert len(he.ccb()) == len(ccb)
        # directed_curve follows source -> target, curve is the stored orientation
        assert he.directed_curve.source == he.source.point
        assert he.directed_curve.target == he.target.point
        assert he.directed_curve == he.curve
        assert he.direction in ("left_to_right", "right_to_left")


def test_vertex_topology(square_arr):
    v = next(v for v in square_arr.vertices() if v.point.approx == (4.0, 2.0))
    # (4,2) is where the chord meets the right side: 2 side pieces + 2 chord pieces
    assert v.degree == 4
    assert len(v.incident_halfedges()) == 4
    assert all(h.target.id == v.id for h in v.incident_halfedges())
    assert not v.is_isolated and not v.is_at_open_boundary
    assert v.parameter_space_in_x == "interior"
    assert v.parameter_space_in_y == "interior"
    # three distinct faces meet there: the two halves and the unbounded face
    assert len(v.incident_faces()) == 3
    with pytest.raises(ValueError):
        v.face                                  # only isolated vertices have one


def test_handles_compare_and_hash_by_identity(square_arr):
    v1 = square_arr.vertices()[0]
    v2 = square_arr.vertices()[0]
    assert v1 is not v2 and v1 == v2 and hash(v1) == hash(v2)
    assert v1.arrangement is square_arr
    assert v1 != square_arr.vertices()[1]
    e1 = square_arr.edges()[0]
    assert e1 == square_arr.edges()[0]
    f1 = square_arr.faces()[0]
    assert f1 == square_arr.faces()[0]


def test_element_data_round_trip(square_arr):
    v, e, f = square_arr.vertices()[0], square_arr.edges()[0], square_arr.faces()[0]
    assert (v.data, e.data, f.data) == (None, None, None)
    payload = {"n": 1}
    v.data = payload
    e.data = "edge"
    f.data = 3.5
    assert square_arr.vertices()[0].data is payload
    assert square_arr.edges()[0].data == "edge"
    assert square_arr.faces()[0].data == 3.5
    v.data = None
    assert square_arr.vertices()[0].data is None


def test_edges_returns_one_halfedge_per_edge(square_arr):
    edges = square_arr.edges()
    assert len(edges) == 9
    assert len({e.edge_id for e in edges}) == 9
    halfedges = square_arr.halfedges()
    assert len(halfedges) == 18 == square_arr.number_of_halfedges
    assert len({h.edge_id for h in halfedges}) == 9


def test_repr_and_len(square_arr):
    assert len(square_arr) == square_arr.number_of_edges == 9
    assert repr(square_arr) == (
        "Arrangement(kind='segment', vertices=8, edges=9, faces=3, curves=5)")
