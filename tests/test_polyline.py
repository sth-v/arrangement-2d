"""Tests for the ``polyline`` geometry kind (``Arr_polyline_traits_2<Arr_segment_traits_2<Epeck>>``).

Everything here goes through the public API (``import arrangement_2d as a2``).  Every
expected number is derived by hand in a comment next to the assertion; nothing is copied
from a previous run of the library.

Conventions used throughout
---------------------------
* A *polyline* is a chain of straight segments handled as **one** curve.  Its interior
  vertices are NOT arrangement vertices unless something else touches them: an arrangement
  edge stores a whole ``X_monotone_curve_2``, which for this kind may itself be a chain.
  That is what makes "one input curve induces many edges" (and "one edge carries many
  points") the interesting property of this kind.
* ``Polyline(points)`` builds a *general* curve (``Curve_2``); ``Polyline.from_x_monotone_points(points)``
  builds an ``X_monotone_curve_2`` and requires the chain to be x-monotone already.
* Euler's formula for a planar arrangement with ``C`` connected components (the unbounded
  face counted in ``F``) is ``V - E + F = 1 + C``; for a connected one ``V - E + F = 2``.
  Every arrangement count below is cross-checked against it.
"""

from __future__ import annotations

from decimal import Decimal
from fractions import Fraction

import pytest

a2 = pytest.importorskip("arrangement_2d")


# ===========================================================================
# Small helpers (no expectations live here — only shorthand)
# ===========================================================================

def xy(point):
    """A point's exact ``(Fraction, Fraction)`` coordinates."""
    return point.exact()


def counts(arr):
    """``(V, E, F)`` of an arrangement."""
    return (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces)


def check_euler(arr, components=1):
    """V - E + F == 1 + C (planar, bounded topology, unbounded face included in F)."""
    v, e, f = counts(arr)
    assert v - e + f == 1 + components, (v, e, f)


def peak():
    """The "peak" polyline (0,0) -> (2,2) -> (4,0): x is 0,2,4, so it is x-monotone."""
    return a2.Polyline([(0, 0), (2, 2), (4, 0)])


def chord():
    """The horizontal chord y = 1 from x = 0 to x = 4."""
    return a2.Polyline([(0, 1), (4, 1)])


@pytest.fixture
def peak_arr():
    """peak() + chord(): hand-derived V=6 E=6 F=2 (see test_arrangement_peak_and_chord)."""
    arr = a2.Arrangement("polyline")
    arr.insert([peak(), chord()])
    return arr


# ===========================================================================
# 1. Construction from points
# ===========================================================================

def test_construct_from_points_basic():
    p = a2.Polyline([(0, 0), (1, 1), (2, 0)])
    assert p.kind == a2.Kind.POLYLINE
    # 3 points chained pairwise -> 3 - 1 = 2 straight sub-segments.
    assert p.number_of_points == 3
    assert p.number_of_subcurves == 2
    # Polyline(points) builds a general Curve_2, not an X_monotone_curve_2.
    assert p.is_x_monotone is False
    assert p.type_name == "curve"


def test_construct_two_points_is_a_single_subcurve():
    p = a2.Polyline([(0, 0), (3, 4)])
    # 2 points -> 1 sub-segment, and the point list is exactly the input.
    assert (p.number_of_points, p.number_of_subcurves) == (2, 1)
    assert [xy(q) for q in p.points] == [(Fraction(0), Fraction(0)),
                                         (Fraction(3), Fraction(4))]


def test_construct_long_chain_subcurve_count():
    pts = [(i, i % 2) for i in range(10)]      # 10 distinct points, x strictly increasing
    p = a2.Polyline(pts)
    assert (p.number_of_points, p.number_of_subcurves) == (10, 9)


def test_construct_accepts_point_objects_of_the_same_and_other_planar_kinds():
    # Segment / linear / polyline points are all Epeck::Point_2, so they convert exactly.
    p = a2.Polyline([a2.Point(0, 0), a2.Point(1, 1, kind="polyline"),
                     a2.Point(2, 0, kind="linear")])
    assert p.number_of_subcurves == 2
    assert [xy(q) for q in p.points] == [(Fraction(0), Fraction(0)),
                                         (Fraction(1), Fraction(1)),
                                         (Fraction(2), Fraction(0))]


def test_construct_coordinates_are_exact():
    # int / float / Fraction / Decimal / "n/d" strings all become exact rationals:
    # 0.25 is a dyadic rational so float -> Fraction(1, 4) is lossless.
    p = a2.Polyline([(Fraction(1, 3), Decimal("0.5")), ("2/7", 0.25)])
    assert xy(p.points[0]) == (Fraction(1, 3), Fraction(1, 2))
    assert xy(p.points[1]) == (Fraction(2, 7), Fraction(1, 4))


def test_construct_requires_at_least_two_points():
    with pytest.raises(ValueError, match="at least 2 points"):
        a2.Polyline([])
    with pytest.raises(ValueError, match="at least 2 points"):
        a2.Polyline([(0, 0)])


def test_construct_rejects_consecutive_duplicate_points():
    # A repeated point would make a degenerate sub-segment; the core names the two indices.
    with pytest.raises(ValueError) as exc:
        a2.Polyline([(0, 0), (0, 0), (1, 1)])
    assert "points 0 and 1 are equal" in str(exc.value)
    assert "must be distinct" in str(exc.value)


def test_construct_rejects_consecutive_duplicate_points_at_a_later_index():
    # The check walks the whole chain, not only the first pair: here indices 2 and 3.
    with pytest.raises(ValueError) as exc:
        a2.Polyline([(0, 0), (1, 1), (2, 2), (2, 2), (3, 0)])
    assert "points 2 and 3 are equal" in str(exc.value)


def test_construct_rejects_duplicates_written_differently():
    # Equality is exact and numeric, not textual: 1/2 == 0.5 == Fraction(1, 2).
    with pytest.raises(ValueError, match="points 0 and 1 are equal"):
        a2.Polyline([(Fraction(1, 2), 0), (0.5, 0), (1, 1)])


def test_construct_allows_a_repeated_non_consecutive_point():
    # Only *consecutive* points must differ, so a closed ring (first point == last point)
    # is a legal single curve: 4 points -> 3 sub-segments.
    ring = a2.Polyline([(0, 0), (1, 0), (1, 1), (0, 0)])
    assert (ring.number_of_points, ring.number_of_subcurves) == (4, 3)
    assert xy(ring.points[0]) == xy(ring.points[3])


def test_construct_rejects_three_dimensional_coordinates():
    with pytest.raises(ValueError, match="need 2 coordinates"):
        a2.Polyline([(0, 0, 0), (1, 1, 1)])


def test_construct_rejects_a_string():
    with pytest.raises(TypeError):
        a2.Polyline("ab")


def test_construct_rejects_points_without_exact_rational_coordinates():
    # Conic points are CORE::Expr; the traps checklist says CORE has no safe rationality
    # test for Expr, so every conic point is refused rather than silently rounded.
    with pytest.raises(a2.NotRepresentableError):
        a2.Polyline([a2.Point(0, 0, kind="conic"), a2.Point(1, 1, kind="conic")])


# ===========================================================================
# 2. Construction from segments
# ===========================================================================

def test_from_segments_builds_the_chain():
    p = a2.Polyline.from_segments([a2.Segment((0, 0), (1, 0)),
                                   a2.Segment((1, 0), (2, 1))])
    # 2 segments -> 2 sub-segments and 3 points; the shared point appears once.
    assert (p.number_of_subcurves, p.number_of_points) == (2, 3)
    assert [xy(q) for q in p.points] == [(Fraction(0), Fraction(0)),
                                         (Fraction(1), Fraction(0)),
                                         (Fraction(2), Fraction(1))]


def test_from_segments_single_segment():
    p = a2.Polyline.from_segments([a2.Segment((0, 0), (1, 1))])
    assert p.number_of_subcurves == 1


def test_from_segments_accepts_other_straight_kinds():
    # A bounded LinearCurve is exactly a segment, so it converts without approximation.
    p = a2.Polyline.from_segments([a2.LinearCurve.segment((0, 0), (1, 0)),
                                   a2.LinearCurve.segment((1, 0), (2, 1))])
    assert p.number_of_subcurves == 2


def test_from_segments_round_trips_the_segments_property():
    p = peak()
    assert a2.Polyline.from_segments(p.segments) == p


def test_from_segments_rejects_a_gap():
    with pytest.raises(ValueError) as exc:
        a2.Polyline.from_segments([a2.Segment((0, 0), (1, 0)),
                                   a2.Segment((2, 0), (3, 0))])
    assert "chain end to end" in str(exc.value)


def test_from_segments_rejects_an_empty_sequence():
    with pytest.raises(ValueError, match="at least one segment"):
        a2.Polyline.from_segments([])


def test_from_segments_rejects_a_multi_segment_element():
    # Every element must be ONE straight segment; a 2-subcurve polyline is not.
    with pytest.raises(ValueError):
        a2.Polyline.from_segments([peak()])


def test_constructor_dispatches_a_curve_list_to_from_segments():
    p = a2.Polyline([a2.Segment((0, 0), (1, 0)), a2.Segment((1, 0), (2, 1))])
    assert p == a2.Polyline.from_segments([a2.Segment((0, 0), (1, 0)),
                                           a2.Segment((1, 0), (2, 1))])


def test_constructor_rejects_mixing_curves_and_points():
    with pytest.raises(TypeError):
        a2.Polyline([a2.Segment((0, 0), (1, 1)), (2, 2)])


# ===========================================================================
# 3. Accessors: points / segments / number_of_subcurves / sequence protocol
# ===========================================================================

def test_points_and_segments_agree():
    p = a2.Polyline([(0, 0), (1, 1), (2, 0), (3, 1)])
    # 4 points -> 3 segments; segment i runs from point i to point i+1.
    assert len(p.points) == 4
    assert len(p.segments) == 3
    for i, seg in enumerate(p.segments):
        assert xy(seg.source) == xy(p.points[i])
        assert xy(seg.target) == xy(p.points[i + 1])


def test_number_of_points_is_number_of_subcurves_plus_one():
    for n in (2, 3, 5, 8):
        p = a2.Polyline([(i, (i * i) % 3) for i in range(n)])
        assert p.number_of_points == p.number_of_subcurves + 1 == n


def test_points_and_segments_are_of_the_segment_kind():
    # The vertices/sub-segments of a polyline are plain segment-kind objects (same C++
    # types); they compare equal to polyline-kind points, so the kind tag is cosmetic.
    p = peak()
    assert p.points[0].kind == a2.Kind.SEGMENT
    assert p.segments[0].kind == a2.Kind.SEGMENT
    assert p.points[0] == a2.Point(0, 0, kind="polyline")


def test_sequence_protocol():
    p = a2.Polyline([(0, 0), (1, 1), (2, 0)])
    assert len(p) == 3                      # len() is the POINT count
    assert xy(p[0]) == (Fraction(0), Fraction(0))
    assert xy(p[-1]) == (Fraction(2), Fraction(0))
    assert [xy(q) for q in p[0:2]] == [(Fraction(0), Fraction(0)),
                                       (Fraction(1), Fraction(1))]
    assert [xy(q) for q in p] == [xy(q) for q in p.points]


def test_getitem_out_of_range_raises_index_error():
    p = a2.Polyline([(0, 0), (1, 1), (2, 0)])
    with pytest.raises(IndexError):
        p[3]
    with pytest.raises(IndexError):
        p[-4]


def test_repr_lists_the_exact_points():
    p = a2.Polyline([(Fraction(1, 3), 0), (1, 1)])
    assert repr(p) == "Polyline([(1/3, 0), (1, 1)])"


def test_bbox_is_the_box_of_the_vertices():
    # x in {0, 2, 4}, y in {0, 2} -> (0, 0, 4, 2).
    assert peak().bbox() == (0.0, 0.0, 4.0, 2.0)


def test_polyline_is_bounded_and_planar():
    p = peak()
    assert p.is_bounded is True
    assert p.dimension == 2


def test_equality_is_geometric_and_ignores_the_direction():
    p = peak()
    assert p == a2.Polyline([(0, 0), (2, 2), (4, 0)])
    # Equal_2 compares the point sets of the chains, so the reversal is "equal".
    assert p == a2.Polyline([(4, 0), (2, 2), (0, 0)])
    # A general-curve box and an x-monotone box of the same chain are equal too.
    assert p == a2.Polyline.from_x_monotone_points([(0, 0), (2, 2), (4, 0)])
    assert p != a2.Polyline([(0, 0), (2, 3), (4, 0)])


def test_curves_are_not_hashable():
    with pytest.raises(TypeError):
        hash(peak())


# ===========================================================================
# 4. x-monotone construction and subdivision
# ===========================================================================

def test_x_monotone_constructor_builds_an_x_monotone_box():
    xm = a2.Polyline.from_x_monotone_points([(0, 0), (1, 1), (2, 3)])
    assert xm.is_x_monotone is True
    assert xm.type_name == "x_monotone_curve"
    assert (xm.number_of_points, xm.number_of_subcurves) == (3, 2)


def test_x_monotone_constructor_rejects_a_non_monotone_chain():
    # x goes 0 -> 2 -> 1: the third point turns back, which the traits forbids.
    with pytest.raises(a2.PreconditionError):
        a2.Polyline.from_x_monotone_points([(0, 0), (2, 2), (1, 0)])


def test_x_monotone_constructor_accepts_a_vertical_chain():
    # A vertical polyline is x-monotone by convention (monotone in y instead).
    v = a2.Polyline.from_x_monotone_points([(0, 0), (0, 1), (0, 2)])
    assert v.is_vertical is True
    assert v.is_directed_right is True          # (0,0) <xy (0,2)
    assert xy(v.source) == (Fraction(0), Fraction(0))
    assert xy(v.target) == (Fraction(0), Fraction(2))


def test_x_monotone_constructor_rejects_a_vertical_chain_that_turns_back():
    # y goes 0 -> 2 -> 1 on x = 0: monotone in neither direction.
    with pytest.raises(a2.PreconditionError):
        a2.Polyline.from_x_monotone_points([(0, 0), (0, 2), (0, 1)])


def test_x_monotone_constructor_accepts_a_right_to_left_chain():
    r = a2.Polyline.from_x_monotone_points([(4, 0), (2, 2), (0, 0)])
    assert r.is_directed_right is False
    # source/target follow the stored direction; left/right are lexicographic.
    assert xy(r.source) == (Fraction(4), Fraction(0))
    assert xy(r.target) == (Fraction(0), Fraction(0))
    assert xy(r.left) == (Fraction(0), Fraction(0))
    assert xy(r.right) == (Fraction(4), Fraction(0))
    assert r.compare_endpoints_xy() == 1        # +1 == "not directed right"


def test_x_monotone_constructor_validates_like_the_general_one():
    with pytest.raises(ValueError, match="at least 2 points"):
        a2.Polyline.from_x_monotone_points([(0, 0)])
    with pytest.raises(ValueError, match="points 0 and 1 are equal"):
        a2.Polyline.from_x_monotone_points([(0, 0), (0, 0), (1, 1)])


def test_x_monotone_endpoints_and_min_max_of_a_left_to_right_chain():
    xm = a2.Polyline.from_x_monotone_points([(0, 0), (1, 1), (2, 3)])
    assert xy(xm.source) == xy(xm.min_vertex) == (Fraction(0), Fraction(0))
    assert xy(xm.target) == xy(xm.max_vertex) == (Fraction(2), Fraction(3))
    assert xm.is_directed_right is True
    assert xm.is_vertical is False
    assert xm.has_source and xm.has_target      # polylines are always bounded


def test_make_x_monotone_keeps_an_x_monotone_chain_whole():
    # x is 0, 2, 4: already monotone -> exactly one piece, equal to the input.
    pieces = peak().make_x_monotone()
    assert len(pieces) == 1
    assert pieces[0] == peak()
    assert pieces[0].is_x_monotone is True


def test_make_x_monotone_splits_at_every_turn():
    # x goes 0,1,0,1,0: it turns at points 1, 2 and 3 -> 4 x-monotone pieces.
    saw = a2.Polyline([(0, 0), (1, 1), (0, 2), (1, 3), (0, 4)])
    pieces = saw.make_x_monotone()
    assert len(pieces) == 4
    assert all(piece.number_of_subcurves == 1 for piece in pieces)


def test_make_x_monotone_pieces_chain_and_cover_the_input():
    zig = a2.Polyline([(0, 0), (2, 2), (1, 0), (3, 3)])   # x: 0, 2, 1, 3 -> 3 pieces
    pieces = zig.make_x_monotone()
    assert len(pieces) == 3
    # Consecutive pieces share exactly the turning point, and the ends match the input.
    flat = [xy(pieces[0].points[0])]
    for i, piece in enumerate(pieces):
        if i:
            assert xy(piece.points[0]) == flat[-1]
        flat.extend(xy(q) for q in piece.points[1:])
    assert flat == [xy(q) for q in zig.points]


def test_make_x_monotone_of_a_closed_square_gives_four_pieces():
    # (0,0)->(4,0) right, (4,0)->(4,4) vertical, (4,4)->(0,4) left, (0,4)->(0,0) vertical:
    # the direction changes at every corner, so no two sides merge into one piece.
    square = a2.Polyline([(0, 0), (4, 0), (4, 4), (0, 4), (0, 0)])
    pieces = square.make_x_monotone()
    assert len(pieces) == 4
    assert sum(piece.number_of_subcurves for piece in pieces) == 4


def test_x_monotone_accessors_reject_a_non_monotone_curve():
    zig = a2.Polyline([(0, 0), (2, 2), (1, 0)])
    with pytest.raises(a2.NotXMonotoneError):
        zig.source
    with pytest.raises(a2.NotXMonotoneError):
        zig.split((1, 1))


def test_to_curve_of_an_x_monotone_box_keeps_the_points():
    xm = a2.Polyline.from_x_monotone_points([(0, 0), (1, 1), (2, 3)])
    general = xm.to_curve()
    assert general.type_name == "curve"
    assert [xy(q) for q in general.points] == [xy(q) for q in xm.points]
    # to_curve() on a general curve is the identity.
    assert peak().to_curve().type_name == "curve"


def test_curve_x_monotone_method_is_shadowed_by_the_constructor():
    """``Curve.x_monotone()`` (promote this curve to one x-monotone curve) must be reachable
    on ``Polyline``.  A cdef class shares one namespace for class and instance attributes, so
    a classmethod named ``x_monotone`` would shadow the inherited instance method for every
    instance; the constructor is therefore called ``Polyline.from_x_monotone_points``
    (and ``GeodesicArc.x_monotone_arc`` for the sphere kind).
    """
    p = peak()                       # already x-monotone, so this must simply succeed
    promoted = p.x_monotone()
    assert promoted.is_x_monotone is True
    assert promoted == p


# ===========================================================================
# 5. Traits operations on x-monotone polylines
# ===========================================================================

def test_opposite_reverses_the_point_order():
    xm = a2.Polyline.from_x_monotone_points([(0, 0), (1, 1), (2, 3)])
    rev = xm.opposite()
    assert [xy(q) for q in rev.points] == [xy(q) for q in xm.points][::-1]
    assert rev.is_directed_right is False


def test_split_at_an_interior_vertex():
    xm = a2.Polyline.from_x_monotone_points([(0, 0), (1, 1), (2, 3)])
    left, right = xm.split((1, 1))
    # Splitting exactly at the middle vertex gives two single-segment pieces.
    assert left.number_of_subcurves == right.number_of_subcurves == 1
    assert [xy(q) for q in left.points] == [(Fraction(0), Fraction(0)),
                                            (Fraction(1), Fraction(1))]
    assert [xy(q) for q in right.points] == [(Fraction(1), Fraction(1)),
                                             (Fraction(2), Fraction(3))]


def test_split_inside_a_subcurve():
    xm = a2.Polyline.from_x_monotone_points([(0, 0), (1, 1), (2, 3)])
    # (1/2, 1/2) is the midpoint of the first sub-segment (y = x there).
    left, right = xm.split((Fraction(1, 2), Fraction(1, 2)))
    assert left.number_of_subcurves == 1            # (0,0) -> (1/2,1/2)
    assert right.number_of_subcurves == 2           # (1/2,1/2) -> (1,1) -> (2,3)
    assert xy(left.target) == (Fraction(1, 2), Fraction(1, 2))
    assert xy(right.source) == (Fraction(1, 2), Fraction(1, 2))


def test_split_rejects_a_point_off_the_curve():
    xm = a2.Polyline.from_x_monotone_points([(0, 0), (1, 1), (2, 3)])
    # arr2d checks the split point itself, so this is a plain ValueError.
    with pytest.raises(ValueError, match="does not lie on the curve"):
        xm.split((1, 5))


def test_trim_between_two_interior_points():
    xm = a2.Polyline.from_x_monotone_points([(0, 0), (1, 1), (2, 3)])
    # From the midpoint of segment 0 to the midpoint of segment 1 ((3/2, 2) lies on the
    # line from (1,1) to (2,3): y = 1 + 2*(x-1)).  The trimmed chain keeps vertex (1,1).
    piece = xm.trim((Fraction(1, 2), Fraction(1, 2)), (Fraction(3, 2), Fraction(2)))
    assert [xy(q) for q in piece.points] == [(Fraction(1, 2), Fraction(1, 2)),
                                             (Fraction(1), Fraction(1)),
                                             (Fraction(3, 2), Fraction(2))]


def test_merge_two_chains_that_share_an_endpoint():
    a = a2.Polyline.from_x_monotone_points([(0, 0), (2, 2)])
    b = a2.Polyline.from_x_monotone_points([(2, 2), (4, 0)])
    assert a.can_merge(b) is True
    merged = a.merge(b)
    # 1 + 1 sub-segments joined at (2,2) -> 2 sub-segments, 3 points.
    assert (merged.number_of_subcurves, merged.number_of_points) == (2, 3)
    assert merged == peak()


def test_merge_rejects_curves_that_do_not_touch():
    a = a2.Polyline.from_x_monotone_points([(0, 0), (2, 2)])
    b = a2.Polyline.from_x_monotone_points([(0, 1), (4, 1)])
    assert a.can_merge(b) is False
    with pytest.raises(ValueError, match="not mergeable"):
        a.merge(b)


def test_intersect_two_crossing_polylines():
    # The peak's legs are y = x on [0,2] and y = 4 - x on [2,4] (slope -1 from (2,2) to
    # (4,0)).  The chord y = 1 meets y = x at (1,1) and y = 4 - x at (3,1).
    hits = a2.Polyline.from_x_monotone_points([(0, 0), (2, 2), (4, 0)]).intersect(
        a2.Polyline.from_x_monotone_points([(0, 1), (4, 1)]))
    assert len(hits) == 2
    pts = [xy(pt) for pt, _mult in hits]
    assert pts == [(Fraction(1), Fraction(1)), (Fraction(3), Fraction(1))]
    # Both are transversal crossings of two straight pieces -> multiplicity 1.
    assert [mult for _pt, mult in hits] == [1, 1]


def test_intersect_at_an_endpoint_reports_unknown_multiplicity():
    a = a2.Polyline.from_x_monotone_points([(0, 0), (2, 2)])
    b = a2.Polyline.from_x_monotone_points([(1, 1), (3, 0)])     # starts ON a, at (1,1)
    hits = a.intersect(b)
    assert len(hits) == 1
    pt, mult = hits[0]
    assert xy(pt) == (Fraction(1), Fraction(1))
    assert mult == 0                                  # 0 == "not computed" by CGAL


def test_intersect_overlapping_polylines_returns_a_curve():
    a = a2.Polyline.from_x_monotone_points([(0, 1), (4, 1)])
    b = a2.Polyline.from_x_monotone_points([(1, 1), (3, 1)])      # a sub-chord of a
    hits = a.intersect(b)
    assert len(hits) == 1
    overlap = hits[0]
    assert isinstance(overlap, a2.Polyline)
    assert [xy(q) for q in overlap.points] == [(Fraction(1), Fraction(1)),
                                               (Fraction(3), Fraction(1))]


def test_compare_y_at_x():
    xm = a2.Polyline.from_x_monotone_points([(0, 0), (2, 2), (4, 0)])
    # At x = 1 the curve is at y = 1 (first leg y = x).
    assert xm.compare_y_at_x((1, 0)) == -1            # point below
    assert xm.compare_y_at_x((1, 1)) == 0             # point on
    assert xm.compare_y_at_x((1, 5)) == 1             # point above
    # At x = 3 the second leg (y = 4 - x) is at y = 1.
    assert xm.compare_y_at_x((3, 1)) == 0


def test_is_in_x_range():
    xm = a2.Polyline.from_x_monotone_points([(0, 0), (2, 2), (4, 0)])
    assert xm.is_in_x_range((1, 99)) is True          # 0 <= 1 <= 4
    assert xm.is_in_x_range((4, 0)) is True           # closed range
    assert xm.is_in_x_range((9, 0)) is False


def test_compare_y_at_x_left_and_right_at_a_crossing():
    up = a2.Polyline.from_x_monotone_points([(0, 0), (2, 2), (4, 0)])
    flat = a2.Polyline.from_x_monotone_points([(0, 1), (4, 1)])
    # They cross at (1,1); immediately left of it `up` (slope +1) is below the chord,
    # immediately right of it `up` is above.
    assert up.compare_y_at_x_left(flat, (1, 1)) == -1
    assert up.compare_y_at_x_right(flat, (1, 1)) == 1


# ===========================================================================
# 6. Arrangements of polylines — hand-derived V / E / F
# ===========================================================================

def test_arrangement_peak_and_chord(peak_arr):
    """peak (0,0)-(2,2)-(4,0) plus the chord y=1 from x=0 to x=4.

    Crossings: y = x meets y = 1 at (1,1); y = 4 - x meets y = 1 at (3,1).
    V = 4 curve endpoints (0,0) (4,0) (0,1) (4,1) + 2 crossings          = 6
    E = peak cut twice -> 3, chord cut twice -> 3                        = 6
    F = the triangle (1,1)-(2,2)-(3,1) closed by the chord + unbounded   = 2
    Note (2,2) is NOT a vertex: it stays interior to one edge's chain.
    """
    assert counts(peak_arr) == (6, 6, 2)
    check_euler(peak_arr)
    assert peak_arr.is_valid() is True
    assert peak_arr.number_of_unbounded_faces == 1
    corners = sorted(xy(v.point) for v in peak_arr.vertices())
    assert corners == [(Fraction(0), Fraction(0)), (Fraction(0), Fraction(1)),
                       (Fraction(1), Fraction(1)), (Fraction(3), Fraction(1)),
                       (Fraction(4), Fraction(0)), (Fraction(4), Fraction(1))]


def test_arrangement_of_two_crossing_v_shapes():
    """V-up (0,0)-(2,4)-(4,0) and V-down (0,4)-(2,0)-(4,4).

    Legs: up1 y=2x on [0,2], up2 y=8-2x on [2,4]; down1 y=4-2x on [0,2],
    down2 y=2x-4 on [2,4].  up1 x down1 -> (1,2); up2 x down2 -> (3,2);
    the other pairs are parallel (slopes +-2), so exactly 2 crossings.
    V = 4 endpoints + 2 crossings = 6
    E = each curve cut twice -> 3 + 3 = 6
    F = the closed loop (1,2)-(2,4)-(3,2)-(2,0)-(1,2) + unbounded = 2
    """
    arr = a2.Arrangement("polyline")
    arr.insert([a2.Polyline([(0, 0), (2, 4), (4, 0)]),
                a2.Polyline([(0, 4), (2, 0), (4, 4)])])
    assert counts(arr) == (6, 6, 2)
    check_euler(arr)
    assert arr.is_valid() is True


def test_arrangement_of_a_zigzag_and_a_chord():
    """zigzag (0,0)-(1,2)-(2,0)-(3,2)-(4,0) plus the chord y = 1 on [0,4].

    The four legs have slopes +-2 and each meets y = 1 once, at
    x = 0.5, 1.5, 2.5, 3.5 -> 4 crossings.
    V = 2 zigzag endpoints + 2 chord endpoints + 4 crossings = 8
    E = zigzag cut 4 times -> 5, chord cut 4 times -> 5      = 10
    F = 2 peaks above the chord + 1 valley below it + unbounded = 4
    """
    arr = a2.Arrangement("polyline")
    arr.insert([a2.Polyline([(0, 0), (1, 2), (2, 0), (3, 2), (4, 0)]),
                a2.Polyline([(0, 1), (4, 1)])])
    assert counts(arr) == (8, 10, 4)
    check_euler(arr)
    crossings = sorted(xy(v.point) for v in arr.vertices()
                       if v.point.exact()[1] == Fraction(1)
                       and Fraction(0) < v.point.exact()[0] < Fraction(4))
    assert crossings == [(Fraction(1, 2), Fraction(1)), (Fraction(3, 2), Fraction(1)),
                         (Fraction(5, 2), Fraction(1)), (Fraction(7, 2), Fraction(1))]


def test_arrangement_of_a_closed_polyline():
    """One closed polyline (0,0)-(4,0)-(2,3)-(0,0) inserted as a single curve.

    It splits into 2 x-monotone pieces: (0,0)->(4,0) (left to right) and
    (4,0)->(2,3)->(0,0) (right to left).  Nothing else touches (2,3), so
    V = 2 (only the two turning points that are shared by the pieces)
    E = 2, F = the triangle interior + unbounded = 2.
    """
    arr = a2.Arrangement("polyline")
    arr.insert(a2.Polyline([(0, 0), (4, 0), (2, 3), (0, 0)]))
    assert counts(arr) == (2, 2, 2)
    check_euler(arr)
    # The apex (2,3) survives inside an edge's curve rather than as a vertex.
    apex_edges = [e for e in arr.edges() if e.curve.number_of_points == 3]
    assert len(apex_edges) == 1
    assert (Fraction(2), Fraction(3)) in [xy(q) for q in apex_edges[0].curve.points]


def test_arrangement_of_a_closed_square_and_a_diagonal():
    """The square (0,0)-(4,0)-(4,4)-(0,4)-(0,0) as ONE polyline, then the diagonal.

    Square alone: 4 x-monotone pieces (bottom, right, top, left) meeting at the 4
    corners -> V=4 E=4 F=2.  The diagonal (0,0)-(4,4) joins two existing corners and
    cuts the interior in two: V stays 4, E = 5, F = 3.
    """
    arr = a2.Arrangement("polyline")
    square = arr.insert(a2.Polyline([(0, 0), (4, 0), (4, 4), (0, 4), (0, 0)]))
    assert counts(arr) == (4, 4, 2)
    assert square.number_of_induced_edges == 4
    diag = arr.insert(a2.Polyline([(0, 0), (4, 4)]))
    assert counts(arr) == (4, 5, 3)
    assert diag.number_of_induced_edges == 1
    check_euler(arr)


def test_arrangement_of_a_self_intersecting_polyline():
    """The "bow tie" (0,0)-(2,2)-(0,2)-(2,0): its first and third legs cross at (1,1).

    x-monotone pieces: (0,0)->(2,2), (2,2)->(0,2), (0,2)->(2,0)  -> 3 pieces.
    The crossing (1,1) splits pieces 1 and 3 -> 5 edges.
    V = 4 chain points + (1,1) = 5, E = 5,
    F = the loop (1,1)-(2,2)-(0,2)-(1,1) + unbounded = 2 (the two other legs are antennae).
    """
    arr = a2.Arrangement("polyline")
    handle = arr.insert(a2.Polyline([(0, 0), (2, 2), (0, 2), (2, 0)]))
    assert counts(arr) == (5, 5, 2)
    check_euler(arr)
    assert handle.number_of_induced_edges == 5
    assert arr.is_valid() is True


def test_arrangement_counts_match_the_equivalent_segment_arrangement(square_arr):
    """The conftest ``square_arr`` (V=8 E=9 F=3) rebuilt from polylines.

    Same 5 input curves, this time as single-segment polylines: the arrangement is a
    purely combinatorial object, so the counts must be identical.
    """
    arr = a2.Arrangement("polyline")
    arr.insert([a2.Polyline([(0, 0), (4, 0)]), a2.Polyline([(4, 0), (4, 4)]),
                a2.Polyline([(4, 4), (0, 4)]), a2.Polyline([(0, 4), (0, 0)]),
                a2.Polyline([(-1, 2), (5, 2)])])
    assert counts(arr) == counts(square_arr) == (8, 9, 3)
    check_euler(arr)


def test_square_arr_edges_converted_to_polylines_rebuild_it(square_arr):
    """Every edge curve of the segment arrangement converts exactly to a polyline."""
    arr = a2.Arrangement("polyline")
    arr.insert([e.curve.to_kind("polyline") for e in square_arr.edges()])
    assert counts(arr) == counts(square_arr) == (8, 9, 3)


def test_arrangement_is_empty_and_has_only_the_unbounded_face():
    arr = a2.Arrangement("polyline")
    assert arr.is_empty is True
    assert counts(arr) == (0, 0, 1)          # only the unbounded face exists
    assert arr.is_unbounded_kind is False    # polylines are a bounded-topology kind
    assert arr.unbounded_face.is_unbounded is True


def test_arrangement_locate_and_zone_across_the_peak(peak_arr):
    """A vertical probe x = 2 from y = 0 to y = 5 through the peak arrangement.

    Going up it visits: the unbounded face (below the chord), the chord edge at (2,1),
    the bounded triangle, the peak edge at (2,2), the unbounded face again -> 5 items.
    """
    zone = peak_arr.zone(a2.Polyline([(2, 0), (2, 5)]))
    assert len(zone) == 5
    assert [type(item).__name__ for item in zone] == \
        ["Face", "Halfedge", "Face", "Halfedge", "Face"]
    assert zone[0].is_unbounded and zone[4].is_unbounded
    assert zone[2].is_unbounded is False
    assert peak_arr.do_intersect(a2.Polyline([(2, 0), (2, 5)])) is True
    assert peak_arr.do_intersect(a2.Polyline([(10, 10), (11, 11)])) is False
    # (2, 3/2) is inside the triangle (the chord is at y=1, the peak at y=2 for x=2).
    inside = peak_arr.locate((2, Fraction(3, 2)))
    assert isinstance(inside, a2.Face) and inside.is_unbounded is False
    assert peak_arr.locate((10, 10)).is_unbounded is True


def test_arrangement_bounded_face_polygon_keeps_the_interior_vertex(peak_arr):
    """The triangle face is bounded by 2 edges, one of which is a 3-point chain."""
    faces = peak_arr.bounded_faces()
    assert len(faces) == 1
    poly = faces[0].polygon()
    assert len(poly.outer.curves) == 2
    assert sorted(c.number_of_points for c in poly.outer.curves) == [2, 3]
    # boundary_points walks the whole chain, so the apex (2,2) shows up there.
    outer, holes = faces[0].boundary_points()
    assert holes == []
    assert (2.0, 2.0) in outer


def test_all_documented_point_location_strategies_work_for_polylines(peak_arr):
    """The polyline traits has no ``Kernel`` typedef, so the triangulation strategy is
    unavailable (and it is unsafe anyway -- it returns the wrong face for faces with
    holes).  Every other strategy, landmarks included, must locate the same face:
    ``Arr_polyline_traits_2`` adds the ``Construct_x_monotone_curve_2(p, q)`` overload
    that landmarks needs (docs/dev/cgal61_api/point_location_and_decomposition.md §16 --
    note that CGAL_TRAPS_CHECKLIST.md still lists polyline as landmark-less)."""
    assert peak_arr.supports_point_location("triangulation") is False
    for strategy in ("naive", "simple", "walk", "landmarks", "trapezoid"):
        assert peak_arr.supports_point_location(strategy) is True
        found = peak_arr.locate((2, Fraction(3, 2)), strategy=strategy)
        assert isinstance(found, a2.Face) and found.is_unbounded is False


# ===========================================================================
# 7. Curve history: one input curve -> many edges
# ===========================================================================

def test_history_one_curve_induces_many_edges(peak_arr):
    """Each of the 2 input curves is cut into 3 edges by the 2 crossings."""
    handles = peak_arr.curves()
    assert peak_arr.number_of_curves == len(handles) == 2
    assert sorted(h.number_of_induced_edges for h in handles) == [3, 3]
    for h in handles:
        assert len(h.induced_edges()) == h.number_of_induced_edges
        assert h.is_valid is True


def test_history_insert_returns_one_handle_per_input_curve():
    arr = a2.Arrangement("polyline")
    handles = arr.insert([peak(), chord()])
    assert isinstance(handles, list) and len(handles) == 2
    # The handles keep the ORIGINAL, uncut curves, in input order.
    assert handles[0].curve == peak()
    assert handles[1].curve == chord()
    # A single curve argument returns the handle itself, not a list.
    single = a2.Arrangement("polyline").insert(peak())
    assert isinstance(single, a2.CurveHandle)


def test_history_induced_edges_cover_the_input_curve():
    """The peak is cut at (1,1) and (3,1) into 3 edges whose chains, concatenated in
    x order, reproduce the input points (0,0) (1,1) (2,2) (3,1) (4,0)."""
    arr = a2.Arrangement("polyline")
    handle, _ = arr.insert([peak(), chord()])
    pieces = []
    for he in handle.induced_edges():
        pts = [xy(q) for q in he.directed_curve.points]
        if pts[0] > pts[-1]:              # normalise every piece to left-to-right
            pts.reverse()
        pieces.append(pts)
    pieces.sort()
    flat = pieces[0][:]
    for piece in pieces[1:]:
        assert piece[0] == flat[-1]       # the pieces chain end to end
        flat.extend(piece[1:])
    assert flat == [(Fraction(0), Fraction(0)), (Fraction(1), Fraction(1)),
                    (Fraction(2), Fraction(2)), (Fraction(3), Fraction(1)),
                    (Fraction(4), Fraction(0))]


def test_history_an_edge_of_a_split_curve_keeps_the_interior_vertices(peak_arr):
    """The middle edge of the peak is the chain (1,1)-(2,2)-(3,1): one edge, 3 points."""
    chains = [e.curve for e in peak_arr.edges() if e.curve.number_of_points == 3]
    assert len(chains) == 1
    assert sorted(xy(q) for q in chains[0].points) == [
        (Fraction(1), Fraction(1)), (Fraction(2), Fraction(2)), (Fraction(3), Fraction(1))]


def test_history_originating_curves_of_every_edge(peak_arr):
    """No input curve overlaps another here, so every edge has exactly 1 originator, and
    the 6 edges are shared 3/3 between the 2 curves."""
    owners = {}
    for e in peak_arr.edges():
        origins = e.originating_curves()
        assert len(origins) == e.number_of_originating_curves == 1
        owners.setdefault(origins[0].id, []).append(e.edge_id)
    assert sorted(len(v) for v in owners.values()) == [3, 3]


def test_history_overlapping_curves_share_one_edge():
    """(0,0)-(4,0) and (1,0)-(3,0) overlap on [1,3].

    V = (0,0) (1,0) (3,0) (4,0) = 4, E = 3, F = 1 (nothing is enclosed).
    The middle edge belongs to BOTH input curves; the long curve induces 3 edges, the
    short one only the middle edge.
    """
    arr = a2.Arrangement("polyline")
    long_c, short_c = arr.insert([a2.Polyline([(0, 0), (4, 0)]),
                                  a2.Polyline([(1, 0), (3, 0)])])
    assert counts(arr) == (4, 3, 1)
    check_euler(arr)
    assert long_c.number_of_induced_edges == 3
    assert short_c.number_of_induced_edges == 1
    shared = [e for e in arr.edges() if e.number_of_originating_curves == 2]
    assert len(shared) == 1
    assert [xy(q) for q in shared[0].curve.points] in (
        [(Fraction(1), Fraction(0)), (Fraction(3), Fraction(0))],
        [(Fraction(3), Fraction(0)), (Fraction(1), Fraction(0))])


def test_history_remove_curve_removes_exactly_its_edges(peak_arr):
    """Removing the peak deletes its 3 edges and its 2 private endpoints (0,0) and (4,0);
    the chord's 3 edges and its 4 vertices (0,1) (1,1) (3,1) (4,1) stay, and the bounded
    face disappears -> V=4 E=3 F=1."""
    peak_handle = [h for h in peak_arr.curves() if h.curve.number_of_points == 3][0]
    assert peak_arr.remove_curve(peak_handle) == 3
    assert counts(peak_arr) == (4, 3, 1)
    check_euler(peak_arr)
    assert peak_arr.number_of_curves == 1
    assert peak_handle.is_valid is False       # the handle died with its curve


def test_history_survives_a_copy(peak_arr):
    clone = peak_arr.copy()
    assert counts(clone) == counts(peak_arr)
    assert clone.number_of_curves == 2
    assert sorted(h.number_of_induced_edges for h in clone.curves()) == [3, 3]


def test_directed_curve_follows_the_halfedge_orientation():
    arr = a2.Arrangement("polyline")
    arr.insert_non_intersecting(a2.Polyline.from_x_monotone_points([(0, 0), (1, 2), (3, 3)]))
    edge = arr.edges()[0]
    assert [xy(q) for q in edge.directed_curve.points] == \
        [xy(edge.source.point)] + [xy(edge.curve.points[1])] + [xy(edge.target.point)]
    twin = edge.twin
    assert [xy(q) for q in twin.directed_curve.points] == \
        [xy(q) for q in edge.directed_curve.points][::-1]
    # .curve is the STORED curve and is the same object for both halfedges.
    assert [xy(q) for q in twin.curve.points] == [xy(q) for q in edge.curve.points]
    assert edge.edge_id == twin.edge_id


def test_split_and_merge_an_edge_at_an_interior_vertex():
    """Splitting the peak edge at its apex (2,2) makes 2 edges of 1 segment each; merging
    them back restores the 2-segment chain."""
    arr = a2.Arrangement("polyline")
    he = arr.insert_non_intersecting(a2.Polyline.from_x_monotone_points([(0, 0), (2, 2), (4, 0)]))
    assert (arr.number_of_vertices, arr.number_of_edges) == (2, 1)
    arr.split_edge(he, (2, 2))
    assert (arr.number_of_vertices, arr.number_of_edges) == (3, 2)
    assert sorted(e.curve.number_of_subcurves for e in arr.edges()) == [1, 1]
    first, second = arr.edges()
    merged = arr.merge_edge(first, second)
    assert (arr.number_of_vertices, arr.number_of_edges) == (2, 1)
    assert merged.curve.number_of_subcurves == 2


def test_insert_non_intersecting_requires_an_x_monotone_curve():
    arr = a2.Arrangement("polyline")
    with pytest.raises(a2.NotXMonotoneError):
        arr.insert_non_intersecting(a2.Polyline([(0, 0), (2, 2), (1, 0)]))
    # ...and records no history.
    assert arr.number_of_curves == 0


def test_overlay_of_two_polyline_arrangements(peak_arr):
    """Overlay with a vertical polyline (1,0)-(1,3).

    That segment passes exactly through the existing vertex (1,1) and touches nothing
    else, so it contributes its 2 endpoints and is split there into 2 edges:
    V = 6 + 2 = 8, E = 6 + 2 = 8, F = 2 (it never enters the triangle, which lies at
    x > 1).  V - E + F = 2 (still connected, through (1,1)).
    """
    other = a2.Arrangement("polyline")
    other.insert(a2.Polyline([(1, 0), (1, 3)]))
    result = peak_arr.overlay(other)
    assert counts(result) == (8, 8, 2)
    check_euler(result)
    assert result.kind == a2.Kind.POLYLINE


# ===========================================================================
# 8. approximate()
# ===========================================================================

def test_approximate_returns_the_exact_vertices():
    # For a polyline the approximation IS the curve: no subdivision is possible.
    assert peak().approximate() == [(0.0, 0.0), (2.0, 2.0), (4.0, 0.0)]


def test_approximate_ignores_the_tolerance():
    p = a2.Polyline([(0, 0), (1, 3), (2, 0), (5, 1)])
    assert p.approximate(1e-12) == p.approximate(1000.0) == \
        [(0.0, 0.0), (1.0, 3.0), (2.0, 0.0), (5.0, 1.0)]


def test_approximate_works_on_a_non_x_monotone_curve():
    zig = a2.Polyline([(0, 0), (2, 2), (1, 0)])
    assert zig.approximate() == [(0.0, 0.0), (2.0, 2.0), (1.0, 0.0)]


def test_approximate_coordinates_are_correctly_rounded():
    # 1/3 correctly rounded to double is 0.333...31, not the 0.333...37 that
    # CGAL::to_double(Epeck::FT) produces (traps checklist, "Numbers / coordinates").
    p = a2.Polyline([(Fraction(1, 3), 0), (1, 1)])
    assert p.approximate()[0][0] == float(Fraction(1, 3)) == 0.3333333333333333


def test_approximate_rejects_a_non_positive_tolerance():
    # error <= 0 segfaults or hangs the subdividing traits, so it is refused everywhere.
    for bad in (0.0, -1.0):
        with pytest.raises(ValueError, match="must be a positive number"):
            peak().approximate(bad)


def test_approximate_length_is_the_chain_length():
    # Two legs of length sqrt(2^2 + 2^2) = 2*sqrt(2) each -> 4*sqrt(2).
    assert peak().approximate_length() == pytest.approx(4 * 2 ** 0.5)


def test_arrangement_approximate_edges(peak_arr):
    """6 edges; their approximations have 2 or 3 points (the apex chain has 3)."""
    chains = peak_arr.approximate_edges(1e-3)
    assert len(chains) == 6
    assert sorted(len(c) for c in chains) == [2, 2, 2, 2, 2, 3]


# ===========================================================================
# 9. Conversion to and from the segment kind
# ===========================================================================

def test_segment_converts_to_a_one_subcurve_polyline():
    p = a2.Segment((0, 0), (1, 2)).to_kind("polyline")
    assert isinstance(p, a2.Polyline)
    assert p.kind == a2.Kind.POLYLINE
    assert p.number_of_subcurves == 1
    assert [xy(q) for q in p.points] == [(Fraction(0), Fraction(0)),
                                         (Fraction(1), Fraction(2))]
    # The conversion is exact and lands in an x-monotone box (a segment always is).
    assert p.is_x_monotone is True


def test_polyline_of_one_subcurve_converts_back_to_a_single_segment():
    seg = a2.Polyline([(0, 0), (2, 2)]).to_kind("segment")
    assert isinstance(seg, a2.Segment)
    assert xy(seg.source) == (Fraction(0), Fraction(0))
    assert xy(seg.target) == (Fraction(2), Fraction(2))


def test_polyline_of_many_subcurves_converts_to_a_list_of_segments():
    segs = peak().to_kind("segment")
    assert isinstance(segs, list) and len(segs) == 2
    assert all(isinstance(s, a2.Segment) for s in segs)
    assert [(xy(s.source), xy(s.target)) for s in segs] == [
        ((Fraction(0), Fraction(0)), (Fraction(2), Fraction(2))),
        ((Fraction(2), Fraction(2)), (Fraction(4), Fraction(0)))]


def test_to_kind_polyline_is_the_identity_for_a_polyline():
    p = peak()
    assert p.to_kind("polyline") is p
    assert p.to_kind(a2.Kind.POLYLINE) is p


def test_bounded_linear_curve_converts_to_a_polyline():
    p = a2.LinearCurve.segment((0, 0), (1, 1)).to_kind("polyline")
    assert p.number_of_subcurves == 1


def test_unbounded_linear_curves_do_not_convert_to_a_polyline():
    # A polyline is a finite chain of bounded segments.
    for curve in (a2.LinearCurve.ray((0, 0), (1, 1)),
                  a2.LinearCurve.line((0, 0), (1, 1))):
        with pytest.raises(a2.NotRepresentableError):
            curve.to_kind("polyline")


def test_curved_kinds_do_not_convert_to_a_polyline():
    # circle_segment has no Construct_x_monotone_curve_2(p, q), so the straightness proof
    # is impossible; conic endpoints are algebraic, not rational.  Both are refused
    # instead of being silently approximated.
    with pytest.raises(a2.NotRepresentableError):
        a2.CircleSegment.arc((0, 0), 1, source=(1, 0), target=(0, 1)).to_kind("polyline")
    with pytest.raises(a2.NotRepresentableError):
        a2.ConicArc.segment((0, 0), (1, 1)).to_kind("polyline")


def test_point_converts_between_the_segment_and_polyline_kinds():
    p = a2.Point(Fraction(1, 3), 2).to_kind("polyline")
    assert p.kind == a2.Kind.POLYLINE
    assert p.exact() == (Fraction(1, 3), Fraction(2))
    assert p.is_rational is True
    assert p == a2.Point(Fraction(1, 3), 2)     # exact across kinds


def test_inserting_a_segment_into_a_polyline_arrangement_converts_it():
    arr = a2.Arrangement("polyline")
    handle = arr.insert(a2.Segment((0, 0), (1, 1)))
    assert isinstance(handle.curve, a2.Polyline)
    assert counts(arr) == (2, 1, 1)             # one edge, no enclosed face


def test_inserting_a_multi_segment_polyline_into_a_segment_arrangement():
    """A polyline splits into one segment per sub-segment, and each becomes its own
    history curve: 2 sub-segments -> 2 edges and 2 curve handles.

    ``insert()`` returns only the FIRST handle here because its contract is "one Curve in,
    one CurveHandle out"; the others are still reachable through ``arr.curves()``.
    """
    arr = a2.Arrangement("segment")
    handle = arr.insert(peak())
    assert counts(arr) == (3, 2, 1)
    assert arr.number_of_curves == 2
    assert handle in arr.curves()
    assert sorted(str(c.curve) for c in arr.curves()) == \
        ["Segment((0, 0), (2, 2))", "Segment((2, 2), (4, 0))"]


def test_polyline_kind_has_no_boolean_set_operations():
    # CGAL ships no Gps traits for polylines; convert to the segment kind instead.
    with pytest.raises(a2.UnsupportedError, match="polyline"):
        a2.PolygonSet("polyline")


def test_polygon_of_polyline_curves():
    """A unit-ish square whose four sides are single-segment polylines.

    Inserting it gives V=4 E=4 F=2 (the square interior plus the unbounded face).
    """
    poly = a2.Polygon([a2.Polyline.from_x_monotone_points([(0, 0), (2, 0)]),
                       a2.Polyline.from_x_monotone_points([(2, 0), (2, 2)]),
                       a2.Polyline.from_x_monotone_points([(0, 2), (2, 2)]).opposite(),
                       a2.Polyline.from_x_monotone_points([(0, 0), (0, 2)]).opposite()],
                      kind="polyline")
    assert poly.kind == a2.Kind.POLYLINE
    assert poly.is_closed is True
    assert poly.orientation() == 1              # counterclockwise
    arr = a2.Arrangement("polyline")
    arr.insert(poly)
    assert counts(arr) == (4, 4, 2)
    check_euler(arr)


# ===========================================================================
# 10. Traits object of the polyline kind
# ===========================================================================

def test_traits_object_exposes_the_polyline_functors():
    tr = a2.traits("polyline")
    assert tr.kind == a2.Kind.POLYLINE
    assert tr.dimension == 2
    # Make_x_monotone_2 on the zigzag: x goes 0, 2, 1 -> 2 pieces.
    assert len(tr.make_x_monotone(a2.Polyline([(0, 0), (2, 2), (1, 0)]))) == 2
    # Construct_x_monotone_curve_2(p, q) exists for this kind (landmarks needs it).
    straight = tr.construct_x_monotone_curve((0, 0), (3, 3))
    assert straight.number_of_subcurves == 1
    assert tr.compare_xy((0, 0), (1, 1)) == -1
    assert tr.equal((0, 0), (0, 0)) is True
