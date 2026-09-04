"""Tests for the ``circle_segment`` geometry kind.

``Kind.CIRCLE_SEGMENT`` wraps ``CGAL::Arr_circle_segment_traits_2<Epeck>``: circular
arcs, full circles and straight line segments, whose point coordinates live in the
one-root field ``a + b*sqrt(c)`` (CGAL's ``Sqrt_extension``, exposed as
:class:`arrangement_2d.SqrtExtension`).

Everything here goes through the public API (``import arrangement_2d as a2``) and every
expected number is hand-derived in a comment next to the assertion.  The recurring
derivations are:

* **x-monotone subdivision.**  A circle is split at its two vertical tangency points
  ``(cx-r, cy)`` and ``(cx+r, cy)``, so a full circle always yields exactly 2 x-monotone
  arcs and both tangency points become arrangement vertices.
* **Euler's formula.**  For a bounded planar arrangement (the single unbounded face
  counted) ``V - E + F = 1 + C`` with ``C`` the number of connected components of the
  induced graph; every V/E/F triple below is cross-checked with it.
* **Circle-circle intersections.**  Two circles of radius ``r`` whose centres are ``d``
  apart meet in 2 points when ``0 < d < 2r``, in 1 point when ``d == 2r`` (external
  tangency) or ``d == |r1-r2|`` (internal tangency), and in none otherwise.
* **Lens area.**  Two unit discs whose centres are 1 apart overlap in a lens of area
  ``2*r^2*acos(d/2r) - (d/2)*sqrt(4r^2-d^2) = 2*acos(1/2) - sqrt(3)/2``.

The shared fixtures in ``tests/conftest.py`` are segment-kind only (``square_arr``) or
parametrised over every kind (``kind``), so this file defines its own small builders.
"""

from __future__ import annotations

import math
from fractions import Fraction

import pytest

a2 = pytest.importorskip("arrangement_2d")

KIND = "circle_segment"

#: exact area of the lens shared by two unit discs whose centres are 1 apart:
#: 2*r^2*acos(d/(2r)) - (d/2)*sqrt(4r^2 - d^2) with r = 1, d = 1.
LENS = 2.0 * math.acos(0.5) - math.sqrt(3.0) / 2.0


# ---------------------------------------------------------------------------
# small builders (each test stays independent; nothing is shared between them)
# ---------------------------------------------------------------------------
def circle(cx, cy, r, orientation="ccw"):
    return a2.CircleSegment.circle((cx, cy), r, orientation=orientation)


def disc(cx, cy, r):
    """The closed disc as a :class:`a2.Polygon` (its 2 x-monotone boundary arcs)."""
    return a2.Polygon([circle(cx, cy, r)])


def pset(*polygons):
    """A :class:`a2.PolygonSet` holding the (pairwise disjoint) polygons."""
    s = a2.PolygonSet(KIND)
    for p in polygons:
        s.insert(p)
    return s


def arrangement(*curves):
    arr = a2.Arrangement(KIND)
    arr.insert(list(curves))
    return arr


def upper_half(r=2, cx=0, cy=0):
    """The upper half of the circle, as an x-monotone curve directed right-to-left."""
    return a2.CircleSegment.arc((cx, cy), r, source=(cx + r, cy),
                                target=(cx - r, cy)).x_monotone()


def lower_half(r=2, cx=0, cy=0):
    """The lower half of the circle, as an x-monotone curve directed left-to-right."""
    return a2.CircleSegment.arc((cx, cy), r, source=(cx - r, cy),
                                target=(cx + r, cy)).x_monotone()


def shoelace(points):
    """Signed area of a closed polyline given as ``[(x, y), ...]`` (not repeating p0)."""
    n = len(points)
    acc = 0.0
    for i in range(n):
        j = (i + 1) % n
        acc += points[i][0] * points[j][1] - points[j][0] * points[i][1]
    return 0.5 * acc


def pwh_area(pwh, tolerance=1e-4):
    """Approximate area of a PolygonWithHoles (outer CCW positive, holes CW negative)."""
    outer, holes = pwh.approximate(tolerance)
    total = shoelace(outer) if outer is not None else 0.0
    for h in holes:
        total += shoelace(h)          # holes are clockwise -> negative contribution
    return total


def euler(arr, components=1):
    """``V - E + F`` must equal ``1 + components`` for a bounded planar arrangement."""
    return (arr.number_of_vertices - arr.number_of_edges + arr.number_of_faces
            == 1 + components)


def sqrt_value(se):
    """The value of a :class:`a2.SqrtExtension` recomputed independently in Python."""
    return float(se.a) + float(se.b) * math.sqrt(float(se.c))


# ===========================================================================
# 1. circles: radius vs squared_radius
# ===========================================================================
def test_circle_from_rational_radius():
    c = circle(0, 0, 2)
    assert c.kind == a2.Kind.CIRCLE_SEGMENT
    assert c.is_full and c.is_circular and not c.is_linear
    assert c.center == a2.Point(0, 0, kind=KIND)
    # r = 2  =>  r^2 = 4
    assert c.squared_radius == Fraction(4)
    assert c.has_rational_radius
    assert c.radius == Fraction(2) and isinstance(c.radius, Fraction)
    assert c.orientation == 1                       # "ccw" is the default
    # a full circle is not a single x-monotone curve
    assert not c.is_x_monotone


def test_circle_from_squared_radius_same_shape():
    by_r = circle(0, 0, 2)
    by_r2 = a2.CircleSegment.circle((0, 0), squared_radius=4)
    # sqrt(4) = 2 is exactly rational, so both report the same rational radius
    assert by_r2.squared_radius == Fraction(4)
    assert by_r2.has_rational_radius and by_r2.radius == Fraction(2)
    # ... and both subdivide into the same two arcs with rational tangency points
    left_r = [x.min_vertex.exact() for x in by_r.make_x_monotone()]
    left_r2 = [x.min_vertex.exact() for x in by_r2.make_x_monotone()]
    assert left_r == left_r2 == [(Fraction(-2), Fraction(0)), (Fraction(-2), Fraction(0))]


def test_circle_with_irrational_radius():
    c = a2.CircleSegment.circle((0, 0), squared_radius=2)
    assert c.squared_radius == Fraction(2)
    # sqrt(2) is irrational -> no exact rational radius, .radius degrades to a float
    assert not c.has_rational_radius
    assert isinstance(c.radius, float)
    assert abs(c.radius - math.sqrt(2.0)) < 1e-15
    # the bbox is the tight (+-sqrt(2), +-sqrt(2)) box
    xmin, ymin, xmax, ymax = c.bbox()
    assert abs(xmin + math.sqrt(2)) < 1e-15 and abs(xmax - math.sqrt(2)) < 1e-15
    assert abs(ymin + math.sqrt(2)) < 1e-15 and abs(ymax - math.sqrt(2)) < 1e-15


def test_circle_bbox_is_tight():
    # r = 2 around (1, -1)  =>  x in [-1, 3], y in [-3, 1]
    assert circle(1, -1, 2).bbox() == (-1.0, -3.0, 3.0, 1.0)


def test_circle_orientation_spellings():
    for spelling, expected in (("ccw", 1), ("counterclockwise", 1), (1, 1),
                               ("cw", -1), ("clockwise", -1), (-1, -1)):
        assert circle(0, 0, 1, orientation=spelling).orientation == expected


def test_circle_orientation_must_be_known():
    with pytest.raises(ValueError):
        circle(0, 0, 1, orientation="none")


def test_circle_needs_exactly_one_radius():
    with pytest.raises(TypeError):
        a2.CircleSegment.circle((0, 0))                       # neither
    with pytest.raises(TypeError):
        a2.CircleSegment.circle((0, 0), 2, squared_radius=4)  # both


def test_circle_rejects_negative_radii():
    # API-misuse errors carry ErrorCode::InvalidArgument, which errors.py maps to the
    # plain builtin ValueError (not to a CGALError subclass).
    with pytest.raises(ValueError):
        circle(0, 0, -2)
    with pytest.raises(ValueError):
        a2.CircleSegment.circle((0, 0), squared_radius=-1)


def test_degenerate_zero_radius_circle_is_a_point():
    # CGAL accepts r = 0 as a legal degenerate curve; it has no x-monotone piece,
    # Make_x_monotone_2 emits the centre as an isolated point instead.
    c = circle(1, 1, 0)
    assert c.is_full and c.squared_radius == 0
    pieces = c.make_x_monotone()
    assert len(pieces) == 1
    assert isinstance(pieces[0], a2.Point)
    assert pieces[0] == a2.Point(1, 1, kind=KIND)


def test_circle_alias():
    c = a2.Circle((0, 0), 3)
    assert c.is_full and c.squared_radius == 9 and c.radius == 3


# ===========================================================================
# 2. arcs
# ===========================================================================
def test_arc_upper_half_from_center_and_radius():
    # CCW from (2,0) to (-2,0) is the upper half of x^2 + y^2 = 4
    arc = a2.CircleSegment.arc((0, 0), 2, source=(2, 0), target=(-2, 0))
    assert arc.is_circular and not arc.is_full and not arc.is_linear
    assert arc.orientation == 1
    assert arc.center == a2.Point(0, 0, kind=KIND) and arc.squared_radius == 4
    assert arc.source == a2.Point(2, 0, kind=KIND)
    assert arc.target == a2.Point(-2, 0, kind=KIND)
    # already x-monotone: exactly one piece
    assert len(arc.make_x_monotone()) == 1
    # bbox is tight: the arc reaches y = 2 at its top, never y = -2
    assert arc.bbox() == (-2.0, 0.0, 2.0, 2.0)


def test_arc_lower_half_from_center_and_radius():
    arc = a2.CircleSegment.arc((0, 0), 2, source=(-2, 0), target=(2, 0))
    assert len(arc.make_x_monotone()) == 1
    assert arc.bbox() == (-2.0, -2.0, 2.0, 0.0)


def test_arc_spanning_a_tangency_point_needs_two_pieces():
    # CCW from (0,-2) to (0,2) passes through the rightmost point (2,0), which is a
    # vertical tangency point => the arc splits there into 2 x-monotone pieces.
    arc = a2.CircleSegment.arc((0, 0), 2, source=(0, -2), target=(0, 2))
    pieces = arc.make_x_monotone()
    assert len(pieces) == 2
    assert pieces[0].source == a2.Point(0, -2, kind=KIND)
    assert pieces[0].target == a2.Point(2, 0, kind=KIND)      # junction at the tangency
    assert pieces[1].source == a2.Point(2, 0, kind=KIND)
    assert pieces[1].target == a2.Point(0, 2, kind=KIND)
    # the arc spans the whole right half, so the bbox reaches x = 2
    assert arc.bbox() == (0.0, -2.0, 2.0, 2.0)


def test_arc_by_squared_radius_keyword():
    arc = a2.CircleSegment.arc((0, 0), squared_radius=4, source=(2, 0), target=(-2, 0))
    assert arc.squared_radius == 4 and arc.is_circular
    assert len(arc.make_x_monotone()) == 1


def test_arc_endpoints_must_lie_on_the_circle():
    # (1,0) is not on x^2 + y^2 = 4
    with pytest.raises(ValueError):
        a2.CircleSegment.arc((0, 0), 2, source=(1, 0), target=(-2, 0))
    with pytest.raises(ValueError):
        a2.CircleSegment.arc((0, 0), 2, source=(2, 0), target=(0, 0))


def test_arc_endpoints_must_be_distinct():
    with pytest.raises(ValueError):
        a2.CircleSegment.arc((0, 0), 2, source=(2, 0), target=(2, 0))


def test_arc_needs_exactly_one_radius():
    with pytest.raises(TypeError):
        a2.CircleSegment.arc((0, 0), source=(2, 0), target=(-2, 0))
    with pytest.raises(TypeError):
        a2.CircleSegment.arc((0, 0), 2, squared_radius=4, source=(2, 0), target=(-2, 0))


def test_circular_arc_alias():
    arc = a2.CircularArc((0, 0), 2, source=(2, 0), target=(-2, 0))
    assert arc.is_circular and arc.squared_radius == 4


def test_arc_from_three_points():
    # (2,0), (0,2), (-2,0) all lie on x^2 + y^2 = 4; going 2,0 -> 0,2 -> -2,0 turns
    # left (cross product of (-2,2) and (-2,-2) is (-2)(-2) - (2)(-2) = 8 > 0) => CCW.
    arc = a2.CircleSegment.arc_from_three_points((2, 0), (0, 2), (-2, 0))
    assert arc.is_circular
    assert arc.center == a2.Point(0, 0, kind=KIND)
    assert arc.squared_radius == Fraction(4)
    assert arc.orientation == 1
    assert arc.source == a2.Point(2, 0, kind=KIND)
    assert arc.target == a2.Point(-2, 0, kind=KIND)


def test_arc_from_three_points_clockwise():
    # mirror of the previous one: 2,0 -> 0,-2 -> -2,0 turns right => CW
    arc = a2.CircleSegment.arc_from_three_points((2, 0), (0, -2), (-2, 0))
    assert arc.orientation == -1
    assert arc.squared_radius == Fraction(4)


def test_arc_from_three_points_off_centre():
    # circle through (0,0), (2,2), (4,0): centre (2, y) with 4 + y^2 = (y-2)^2 + 0
    # => 4 + y^2 = y^2 - 4y + 4 => y = 0; r^2 = 4.
    arc = a2.CircleSegment.arc_from_three_points((0, 0), (2, 2), (4, 0))
    assert arc.center == a2.Point(2, 0, kind=KIND)
    assert arc.squared_radius == Fraction(4)


def test_arc_from_three_collinear_points_is_a_segment():
    # CGAL degrades a collinear triple to the straight segment p1 -> p3 (documented
    # behaviour, not an error).
    curve = a2.CircleSegment.arc_from_three_points((0, 0), (1, 1), (2, 2))
    assert curve.is_linear and not curve.is_circular
    assert curve.source == a2.Point(0, 0, kind=KIND)
    assert curve.target == a2.Point(2, 2, kind=KIND)


def test_arc_from_three_points_needs_distinct_ends():
    with pytest.raises(ValueError):
        a2.CircleSegment.arc_from_three_points((0, 0), (1, 1), (0, 0))


# ===========================================================================
# 3. segments
# ===========================================================================
def test_segment_accessors():
    s = a2.CircleSegment.segment((0, 0), (3, 4))
    assert s.is_linear and not s.is_circular and not s.is_full
    assert s.orientation == 0                       # a straight segment has no turn
    assert s.source == a2.Point(0, 0, kind=KIND)
    assert s.target == a2.Point(3, 4, kind=KIND)
    assert s.bbox() == (0.0, 0.0, 3.0, 4.0)
    # 3-4-5 triangle
    assert abs(s.approximate_length(1e-9) - 5.0) < 1e-12
    # the supporting line 4x - 3y = 0, normalised by CGAL to (-4, 3, 0)
    a, b, c = s.supporting_line
    assert (a, b, c) == (Fraction(-4), Fraction(3), Fraction(0))
    # every point of the segment satisfies a*x + b*y + c == 0
    assert a * 3 + b * 4 + c == 0


def test_segment_is_x_monotone_in_one_piece():
    s = a2.CircleSegment.segment((0, 0), (3, 4))
    assert len(s.make_x_monotone()) == 1
    assert not s.x_monotone().is_vertical


def test_vertical_segment():
    v = a2.CircleSegment.segment((1, -3), (1, 3)).x_monotone()
    assert v.is_vertical
    assert len(a2.CircleSegment.segment((1, -3), (1, 3)).make_x_monotone()) == 1
    assert v.bbox() == (1.0, -3.0, 1.0, 3.0)


def test_segment_endpoints_must_differ():
    with pytest.raises(ValueError):
        a2.CircleSegment.segment((1, 1), (1, 1))


def test_segment_on_line_with_sqrt_endpoint():
    # the line x - y = 0 from (0,0) to (sqrt(2), sqrt(2))
    root2 = a2.SqrtExtension(0, 1, 2)
    end = a2.Point.from_sqrt_extension(root2, root2)
    s = a2.CircleSegment.segment_on_line(1, -1, 0, (0, 0), end)
    assert s.is_linear
    assert s.target == end
    # length = sqrt(2)*sqrt(2) = 2
    assert abs(s.approximate_length(1e-9) - 2.0) < 1e-12


def test_segment_on_line_rejects_bad_input():
    with pytest.raises(ValueError):
        a2.CircleSegment.segment_on_line(0, 0, 1, (0, 0), (1, 1))   # a = b = 0
    with pytest.raises(ValueError):
        a2.CircleSegment.segment_on_line(1, 0, 0, (1, 0), (1, 1))   # (1,0) not on x = 0


def test_circular_accessors_reject_segments_and_vice_versa():
    seg = a2.CircleSegment.segment((0, 0), (1, 1))
    arc = a2.CircleSegment.arc((0, 0), 2, source=(2, 0), target=(-2, 0))
    with pytest.raises(ValueError):
        seg.center
    with pytest.raises(ValueError):
        seg.squared_radius
    with pytest.raises(ValueError):
        arc.supporting_line


# ===========================================================================
# 4. make_x_monotone piece counts
# ===========================================================================
def test_make_x_monotone_full_circle_gives_two_arcs():
    # the two vertical tangency points (-2,0) and (2,0) cut the circle in half
    pieces = circle(0, 0, 2).make_x_monotone()
    assert len(pieces) == 2
    lower, upper = pieces
    # CCW starting at the leftmost point: first the lower arc, then the upper one
    assert lower.source == a2.Point(-2, 0, kind=KIND)
    assert lower.target == a2.Point(2, 0, kind=KIND)
    assert lower.is_directed_right
    assert upper.source == a2.Point(2, 0, kind=KIND)
    assert upper.target == a2.Point(-2, 0, kind=KIND)
    assert not upper.is_directed_right
    # the lower piece is below and the upper piece above the centre
    assert lower.compare_y_at_x(a2.Point(0, 0, kind=KIND)) == 1     # (0,0) above lower
    assert upper.compare_y_at_x(a2.Point(0, 0, kind=KIND)) == -1    # (0,0) below upper


def test_make_x_monotone_cw_circle_also_gives_two_arcs():
    pieces = circle(0, 0, 2, orientation="cw").make_x_monotone()
    assert len(pieces) == 2
    assert all(p.min_vertex == a2.Point(-2, 0, kind=KIND) for p in pieces)
    assert all(p.max_vertex == a2.Point(2, 0, kind=KIND) for p in pieces)


def test_make_x_monotone_half_circle_gives_one_arc():
    assert len(a2.CircleSegment.arc((0, 0), 2, source=(2, 0),
                                    target=(-2, 0)).make_x_monotone()) == 1
    assert len(a2.CircleSegment.arc((0, 0), 2, source=(-2, 0),
                                    target=(2, 0)).make_x_monotone()) == 1


def test_make_x_monotone_quarter_arcs_give_one_piece_each():
    # (2,0) -> (0,2) and (0,2) -> (-2,0) both stay on one side of both tangencies
    for src, tgt in (((2, 0), (0, 2)), ((0, 2), (-2, 0)),
                     ((-2, 0), (0, -2)), ((0, -2), (2, 0))):
        arc = a2.CircleSegment.arc((0, 0), 2, source=src, target=tgt)
        assert len(arc.make_x_monotone()) == 1, (src, tgt)


def test_make_x_monotone_three_quarter_arc_gives_two_pieces():
    # CCW from (0,2) all the way round to (2,0) crosses the leftmost tangency (-2,0)
    # (and only that one) => 2 pieces.
    arc = a2.CircleSegment.arc((0, 0), 2, source=(0, 2), target=(2, 0))
    pieces = arc.make_x_monotone()
    assert len(pieces) == 2
    junctions = {pieces[0].target.xy, pieces[1].source.xy}
    assert junctions == {(-2.0, 0.0)}


def test_x_monotone_promotion_of_a_full_circle_raises():
    with pytest.raises(a2.NotXMonotoneError):
        circle(0, 0, 2).x_monotone()


def test_x_monotone_box_flag():
    arc = a2.CircleSegment.arc((0, 0), 2, source=(2, 0), target=(-2, 0))
    # .arc() builds a general Curve_2 box even when the geometry is x-monotone
    assert not arc.is_x_monotone and arc.type_name == "curve"
    xm = arc.x_monotone()
    assert xm.is_x_monotone and xm.type_name == "x_monotone_curve"
    # round trip back to a general curve still subdivides into one piece
    assert len(xm.to_curve().make_x_monotone()) == 1


# ===========================================================================
# 5. sqrt-extension coordinates
# ===========================================================================
def test_sqrt_extension_basics():
    se = a2.SqrtExtension(1, 1, 2)          # 1 + sqrt(2) = 2.41421356...
    assert (se.a, se.b, se.c) == (Fraction(1), Fraction(1), Fraction(2))
    assert not se.is_rational
    assert se.exact() is None
    assert se.sign() == 1 and bool(se)
    assert abs(float(se) - (1.0 + math.sqrt(2.0))) < 1e-15
    lo, hi = se.interval()
    assert lo <= 1.0 + math.sqrt(2.0) <= hi
    assert repr(se) == "SqrtExtension(1, 1, 2)"


def test_sqrt_extension_perfect_square_radicand_is_rational():
    # 1 + 3*sqrt(4) = 1 + 6 = 7; is_extended() would still be true in CGAL, so the
    # binding must decide rationality by value (CGAL_TRAPS_CHECKLIST, "Numbers").
    se = a2.SqrtExtension(1, 3, 4)
    assert se.is_rational
    assert se.exact() == Fraction(7)
    assert float(se) == 7.0
    assert hash(se) == hash(Fraction(7))


def test_sqrt_extension_zero_coefficient_is_rational():
    se = a2.SqrtExtension(Fraction(3, 2), 0, 5)
    assert se.is_rational and se.exact() == Fraction(3, 2)


def test_sqrt_extension_rejects_negative_radicand():
    with pytest.raises(ValueError):
        a2.SqrtExtension(0, 1, -1)


def test_sqrt_extension_irrational_is_unhashable():
    with pytest.raises(TypeError):
        hash(a2.SqrtExtension(0, 1, 3))


def test_sqrt_extension_ordering_and_negation():
    se = a2.SqrtExtension(1, 1, 2)          # ~2.414
    assert se > 2 and se < 3
    assert -se == a2.SqrtExtension(-1, -1, 2)
    assert a2.SqrtExtension(2, 0, 0) == 2
    assert a2.SqrtExtension(0, 0, 7).sign() == 0


def test_point_from_sqrt_extension_round_trip():
    root2 = a2.SqrtExtension(0, 1, 2)
    p = a2.Point.from_sqrt_extension(root2, 0)
    assert p.kind == a2.Kind.CIRCLE_SEGMENT
    assert not p.is_rational
    x, y = p.exact()
    assert isinstance(x, a2.SqrtExtension) and isinstance(y, Fraction)
    assert abs(sqrt_value(x) - math.sqrt(2.0)) < 1e-15
    assert y == 0
    # feeding exact() back in reproduces the very same point
    assert a2.Point.from_sqrt_extension(x, y) == p


def test_point_from_sqrt_extension_accepts_plain_rationals():
    p = a2.Point.from_sqrt_extension(Fraction(1, 2), 3)
    assert p.is_rational
    assert p.exact() == (Fraction(1, 2), Fraction(3))
    assert p == a2.Point(Fraction(1, 2), 3, kind=KIND)


def test_irrational_point_has_no_rational_form():
    p = a2.Point.from_sqrt_extension(a2.SqrtExtension(0, 1, 2), 0)
    with pytest.raises(a2.NotRepresentableError):
        p.exact_rational()
    with pytest.raises(TypeError):
        hash(p)
    with pytest.raises(a2.NotRepresentableError):
        p.to_kind("segment")


def test_irrational_point_interval_brackets_the_value():
    p = a2.Point.from_sqrt_extension(a2.SqrtExtension(0, 1, 2), 0)
    (xlo, xhi), (ylo, yhi) = p.interval()
    assert xlo <= math.sqrt(2.0) <= xhi
    assert ylo == yhi == 0.0
    assert abs(p.x - math.sqrt(2.0)) < 1e-15


def test_circle_with_irrational_radius_has_sqrt_tangency_points():
    # x^2 + y^2 = 2 has vertical tangencies at (+-sqrt(2), 0)
    pieces = a2.CircleSegment.circle((0, 0), squared_radius=2).make_x_monotone()
    left = pieces[0].min_vertex
    assert not left.is_rational
    x, y = left.exact()
    assert isinstance(x, a2.SqrtExtension)
    # b^2 * c == 2 whatever normalisation CGAL chose for (b, c)
    assert x.a == 0 and x.b ** 2 * x.c == 2 and x.b < 0
    assert y == 0


def test_intersection_of_arc_and_vertical_line_is_a_sqrt_point():
    # upper half of x^2 + y^2 = 4 meets x = 1 at (1, sqrt(3))
    hits = upper_half().intersect(a2.CircleSegment.segment((1, -3), (1, 3)).x_monotone())
    assert len(hits) == 1
    point, multiplicity = hits[0]
    assert multiplicity == 1                       # transversal crossing
    assert not point.is_rational
    x, y = point.exact()
    assert x == Fraction(1)
    assert isinstance(y, a2.SqrtExtension)
    assert y.a == 0 and y.b ** 2 * y.c == 3        # y = sqrt(3)
    assert abs(sqrt_value(y) - math.sqrt(3.0)) < 1e-15
    # the round trip through from_sqrt_extension rebuilds the same point
    assert a2.Point.from_sqrt_extension(x, y) == point


def test_sqrt_point_compares_exactly_with_rational_points():
    root2 = a2.Point.from_sqrt_extension(a2.SqrtExtension(0, 1, 2), 0)   # (1.414.., 0)
    assert root2.compare_xy(a2.Point(1, 0, kind=KIND)) == 1
    assert root2.compare_xy(a2.Point(2, 0, kind=KIND)) == -1
    assert root2.compare_x(a2.Point(Fraction(707, 500), 0, kind=KIND)) == 1  # 1.414 < sqrt2
    assert root2 != a2.Point(Fraction(1414, 1000), 0, kind=KIND)


# ===========================================================================
# 6. arrangements: V/E/F on hand-derived configurations
# ===========================================================================
def test_single_circle_arrangement():
    arr = arrangement(circle(0, 0, 2))
    # V: the 2 vertical tangency points; E: the 2 x-monotone arcs;
    # F: the disc and the unbounded face.  Euler: 2 - 2 + 2 = 2.
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == (2, 2, 2)
    assert arr.number_of_unbounded_faces == 1
    assert arr.number_of_curves == 1
    assert arr.is_valid() and euler(arr)
    assert {v.point.xy for v in arr.vertices()} == {(-2.0, 0.0), (2.0, 0.0)}


def test_circle_with_its_diameter():
    arr = arrangement(circle(0, 0, 2), a2.CircleSegment.segment((-2, 0), (2, 0)))
    # the diameter's endpoints coincide with the tangency points: V = 2;
    # E = 2 arcs + 1 chord = 3; F = upper half-disc, lower half-disc, outside = 3.
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == (2, 3, 3)
    assert arr.is_valid() and euler(arr)


def test_two_crossing_circles_vef():
    # r = 2 at (0,0) and (2,0): d = 2 < 2r so they cross in 2 points, (1, +-sqrt(3)).
    arr = arrangement(circle(0, 0, 2), circle(2, 0, 2))
    # V = 2 tangencies of A {(-2,0),(2,0)} + 2 of B {(0,0),(4,0)} + 2 crossings = 6
    # E = each circle carries 4 vertices => 4 arcs each = 8
    # F = lens, A-minus-lens, B-minus-lens, outside = 4.   Euler: 6 - 8 + 4 = 2.
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == (6, 8, 4)
    assert arr.number_of_unbounded_faces == 1
    assert arr.is_valid() and euler(arr)
    # the two crossing points are the degree-4 vertices
    deg4 = [v for v in arr.vertices() if v.degree == 4]
    assert len(deg4) == 2
    for v in deg4:
        x, y = v.point.exact()
        assert x == Fraction(1)
        assert isinstance(y, a2.SqrtExtension) and y.b ** 2 * y.c == 3


def test_two_crossing_unit_discs_vef_and_exact_crossings():
    # r = 1 at (0,0) and (1,0): crossings at (1/2, +-sqrt(3)/2)
    arr = arrangement(circle(0, 0, 1), circle(1, 0, 1))
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == (6, 8, 4)
    assert arr.is_valid() and euler(arr)
    crossings = [v.point for v in arr.vertices() if v.degree == 4]
    assert len(crossings) == 2
    for p in crossings:
        x, y = p.exact()
        assert x == Fraction(1, 2)
        assert y.b ** 2 * y.c == Fraction(3, 4)        # y = +- sqrt(3)/2
        assert abs(abs(sqrt_value(y)) - math.sqrt(3.0) / 2) < 1e-15


def test_external_tangency_vef():
    # r = 1 at (0,0) and (2,0): d = 2 = r1 + r2 => a single contact point (1,0).
    arr = arrangement(circle(0, 0, 1), circle(2, 0, 1))
    # V = {(-1,0), (1,0), (3,0)}: the contact point is also both circles' tangency point.
    # E = 2 arcs per circle = 4; F = disc A, disc B, outside = 3.  Euler: 3 - 4 + 3 = 2.
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == (3, 4, 3)
    assert arr.is_valid() and euler(arr)
    contact = [v for v in arr.vertices() if v.degree == 4]
    assert len(contact) == 1
    assert contact[0].point == a2.Point(1, 0, kind=KIND)   # rational tangency point


def test_internal_tangency_vef():
    # r = 2 at (0,0) and r = 1 at (1,0): d = 1 = |r1 - r2| => internal contact at (2,0).
    arr = arrangement(circle(0, 0, 2), circle(1, 0, 1))
    # V = {(-2,0), (0,0), (2,0)}; E = 2 + 2 = 4; F = inner disc, crescent, outside = 3.
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == (3, 4, 3)
    assert arr.is_valid() and euler(arr)
    contact = [v for v in arr.vertices() if v.degree == 4]
    assert len(contact) == 1 and contact[0].point == a2.Point(2, 0, kind=KIND)


def test_disjoint_circles_are_two_components():
    arr = arrangement(circle(0, 0, 1), circle(10, 0, 1))
    # 2 tangency points per circle, 2 arcs per circle, 2 discs + 1 outside.
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == (4, 4, 3)
    # two connected components => V - E + F = 1 + 2 = 3
    assert euler(arr, components=2)
    assert arr.is_valid()


def test_line_tangent_to_circle_vef():
    # y = 2 touches x^2 + y^2 = 4 at exactly one point (0,2).
    arr = arrangement(circle(0, 0, 2), a2.CircleSegment.segment((-3, 2), (3, 2)))
    # V = 2 circle tangencies + (0,2) + the 2 segment endpoints = 5
    # E = circle split at 3 points -> 3 arcs, segment split at (0,2) -> 2 = 5
    # F = disc + outside = 2.   Euler: 5 - 5 + 2 = 2.
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == (5, 5, 2)
    assert arr.is_valid() and euler(arr)
    touch = [v for v in arr.vertices() if v.degree == 4]
    assert len(touch) == 1 and touch[0].point == a2.Point(0, 2, kind=KIND)


def test_three_mutually_crossing_circles_vef():
    # r = 2 at (0,0), (2,0), (1,2).  Pairwise centre distances: 2, sqrt(5), sqrt(5),
    # all strictly between 0 and 2r = 4 => every pair crosses twice: 6 crossings.
    # Plus 2 vertical tangency points per circle = 6, and none of the 12 coincide.
    arr = arrangement(circle(0, 0, 2), circle(2, 0, 2), circle(1, 2, 2))
    # each circle carries 2 tangencies + 4 crossings = 6 vertices => 6 arcs; 3*6 = 18 E.
    # Euler with one component: F = 2 - V + E = 2 - 12 + 18 = 8.
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == (12, 18, 8)
    assert arr.is_valid() and euler(arr)


def test_half_disc_arrangement():
    arr = arrangement(a2.CircleSegment.arc((0, 0), 2, source=(2, 0), target=(-2, 0)),
                      a2.CircleSegment.segment((-2, 0), (2, 0)))
    # V = the 2 shared endpoints; E = arc + chord; F = half disc + outside.
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == (2, 2, 2)
    assert arr.is_valid() and euler(arr)


def test_overlay_of_two_circles():
    a = arrangement(circle(0, 0, 2))
    b = arrangement(circle(2, 0, 2))
    r = a.overlay(b)
    # same configuration as test_two_crossing_circles_vef
    assert (r.number_of_vertices, r.number_of_edges, r.number_of_faces) == (6, 8, 4)
    assert r.is_valid() and r.kind == a2.Kind.CIRCLE_SEGMENT


def test_arrangement_bbox_uses_vertices_only():
    arr = arrangement(circle(0, 0, 2))
    # documented behaviour: the box encloses the *vertex approximations*, and the only
    # vertices are the two tangency points (-2,0) and (2,0) -- the arcs bulge outside it.
    assert arr.bbox() == (-2.0, 0.0, 2.0, 0.0)


# ===========================================================================
# 7. approximation
# ===========================================================================
def test_approximate_arc_points_lie_on_the_circle():
    arc = upper_half(r=2)
    points = arc.approximate(1e-3)
    assert len(points) >= 2
    # every emitted vertex is exactly on x^2 + y^2 = 4 (to double precision)
    assert max(abs(math.hypot(x, y) - 2.0) for x, y in points) < 1e-12
    # the polyline runs source -> target, i.e. right to left, and never leaves y >= 0
    assert points[0] == (2.0, 0.0) and points[-1] == (-2.0, 0.0)
    assert all(points[i][0] > points[i + 1][0] for i in range(len(points) - 1))
    assert all(y >= 0.0 for _, y in points)


def test_approximate_chord_deviation_within_tolerance():
    arc = upper_half(r=2)
    for tol in (1e-2, 1e-3, 1e-4):
        points = arc.approximate(tol)
        worst = 0.0
        for i in range(len(points) - 1):
            mx = (points[i][0] + points[i + 1][0]) / 2.0
            my = (points[i][1] + points[i + 1][1]) / 2.0
            # the deepest point of a chord of a circle is its midpoint; its distance to
            # the arc is |r - |midpoint - centre||.
            worst = max(worst, abs(2.0 - math.hypot(mx, my)))
        assert worst < tol, (tol, worst)


def test_approximate_finer_tolerance_never_gives_fewer_points():
    arc = upper_half(r=2)
    counts = [len(arc.approximate(t)) for t in (1e-1, 1e-2, 1e-3, 1e-4)]
    assert counts == sorted(counts)
    assert counts[0] < counts[-1]


def test_approximate_full_circle_is_a_closed_ring():
    points = circle(0, 0, 2).approximate(1e-3)
    # the general Curve_2 is approximated piece by piece and the ring closes on itself
    assert points[0] == points[-1] == (-2.0, 0.0)
    # CCW orientation: leaving the leftmost point we go downwards first
    assert points[1][1] < 0.0
    assert max(abs(math.hypot(x, y) - 2.0) for x, y in points) < 1e-12
    # inscribed polygon area <= pi r^2 = 4 pi, and close to it
    area = shoelace(points[:-1])
    assert 0 < area <= 4 * math.pi
    assert abs(area - 4 * math.pi) / (4 * math.pi) < 1e-3


def test_approximate_cw_circle_runs_the_other_way():
    points = circle(0, 0, 2, orientation="cw").approximate(1e-3)
    assert points[0] == points[-1] == (-2.0, 0.0)
    assert points[1][1] > 0.0                       # clockwise: upwards first
    assert shoelace(points[:-1]) < 0                # negative signed area


def test_approximate_length_of_a_circle():
    length = circle(0, 0, 2).approximate_length(1e-4)
    # an inscribed chord polygon is always shorter than the arc: length < 4*pi
    assert length < 4 * math.pi
    assert abs(length - 4 * math.pi) / (4 * math.pi) < 1e-4


def test_approximate_length_of_a_segment_is_exact():
    s = a2.CircleSegment.segment((0, 0), (3, 4))
    assert abs(s.approximate_length(1e-3) - 5.0) < 1e-12


def test_approximate_of_a_straight_curve_gives_its_endpoints():
    s = a2.CircleSegment.segment((-2, 0), (2, 0)).x_monotone()
    assert s.approximate(1e-3) == [(-2.0, 0.0), (2.0, 0.0)]
    assert s.opposite().approximate(1e-3) == [(2.0, 0.0), (-2.0, 0.0)]


def test_approximate_rejects_a_non_positive_tolerance():
    # CGAL's Approximate_2 segfaults on error <= 0 (CGAL_TRAPS_CHECKLIST, "Rendering"),
    # so the binding must reject it before the call.
    arc = upper_half()
    with pytest.raises(ValueError):
        arc.approximate(0.0)
    with pytest.raises(ValueError):
        arc.approximate(-1.0)


def test_approximate_degenerate_circle_gives_one_point():
    assert circle(1, 1, 0).approximate(1e-3) == [(1.0, 1.0)]


# ===========================================================================
# 8. traits functors on circle-segment curves
# ===========================================================================
def test_traits_object():
    t = a2.traits(KIND)
    assert t.kind == a2.Kind.CIRCLE_SEGMENT and t.dimension == 2
    assert len(t.make_x_monotone(circle(0, 0, 2))) == 2
    assert t.equal((1, 1), (1, 1))
    assert t.curves_equal(upper_half(), upper_half())


def test_construct_x_monotone_curve_is_unsupported():
    # Arr_circle_segment_traits_2 has no Construct_x_monotone_curve_2
    # (CGAL_TRAPS_CHECKLIST, "Circle-segment kind").
    with pytest.raises(a2.UnsupportedError):
        a2.traits(KIND).construct_x_monotone_curve((0, 0), (1, 1))


def test_split_and_merge_round_trip():
    arc = upper_half(r=2)
    left, right = arc.split((0, 2))                 # the topmost point of the circle
    assert left.min_vertex == a2.Point(-2, 0, kind=KIND)
    assert left.max_vertex == a2.Point(0, 2, kind=KIND)
    assert right.min_vertex == a2.Point(0, 2, kind=KIND)
    assert right.max_vertex == a2.Point(2, 0, kind=KIND)
    assert left.can_merge(right)
    assert left.merge(right) == arc


def test_two_half_circles_are_not_mergeable():
    # they share BOTH endpoints, so neither "right end == other's left end" nor the
    # mirror image holds; CGAL therefore refuses to merge them (and their union would
    # not be x-monotone anyway).
    assert not upper_half().can_merge(lower_half())


def test_arc_and_segment_are_not_mergeable():
    assert not upper_half().can_merge(
        a2.CircleSegment.segment((-2, 0), (2, 0)).x_monotone())


def test_trim():
    arc = upper_half(r=2)
    trimmed = arc.trim((0, 2), (-2, 0))
    assert trimmed.min_vertex == a2.Point(-2, 0, kind=KIND)
    assert trimmed.max_vertex == a2.Point(0, 2, kind=KIND)
    with pytest.raises(ValueError):
        arc.trim((0, 2), (0, 2))


def test_opposite_reverses_direction_but_not_geometry():
    arc = upper_half(r=2)                # directed right-to-left
    opp = arc.opposite()
    assert arc.is_directed_right is False and opp.is_directed_right is True
    assert arc.compare_endpoints_xy() == 1 and opp.compare_endpoints_xy() == -1
    assert opp.orientation == -arc.orientation      # CCW arc reversed is CW
    assert opp == arc                               # same point set


def test_min_max_vertices_and_x_range():
    arc = upper_half(r=2)
    assert arc.min_vertex == arc.left == a2.Point(-2, 0, kind=KIND)
    assert arc.max_vertex == arc.right == a2.Point(2, 0, kind=KIND)
    assert arc.is_in_x_range(a2.Point(0, 5, kind=KIND))
    assert not arc.is_in_x_range(a2.Point(5, 0, kind=KIND))
    assert not arc.is_vertical


def test_compare_y_at_x_against_the_two_halves():
    up, low = upper_half(r=2), lower_half(r=2)
    origin = a2.Point(0, 0, kind=KIND)
    assert up.compare_y_at_x(origin) == -1          # origin is below the upper arc
    assert low.compare_y_at_x(origin) == 1          # ... and above the lower one
    assert up.compare_y_at_x(a2.Point(0, 2, kind=KIND)) == 0     # on the arc
    # immediately right of the shared left endpoint the upper arc is above
    assert up.compare_y_at_x_right(low, a2.Point(-2, 0, kind=KIND)) == 1
    # immediately left of the shared right endpoint too
    assert up.compare_y_at_x_left(low, a2.Point(2, 0, kind=KIND)) == 1


def test_intersect_two_half_circles_at_their_endpoints():
    hits = upper_half(r=2).intersect(lower_half(r=2))
    # the two halves meet exactly at the tangency points (-2,0) and (2,0)
    assert len(hits) == 2
    points = sorted((p.xy for p, _mult in hits))
    assert points == [(-2.0, 0.0), (2.0, 0.0)]


def test_intersect_overlapping_arcs_reports_an_overlap_curve():
    arc = upper_half(r=2)
    sub = a2.CircleSegment.arc((0, 0), 2, source=(0, 2), target=(-2, 0)).x_monotone()
    hits = arc.intersect(sub)
    assert len(hits) == 1
    overlap = hits[0]
    assert isinstance(overlap, a2.Curve) and not isinstance(overlap, tuple)
    assert overlap == sub


def test_intersect_disjoint_arcs_is_empty():
    assert upper_half(r=2, cx=0).intersect(upper_half(r=1, cx=100)) == []


def test_intersect_rejects_a_foreign_kind():
    with pytest.raises(a2.CGALError):
        upper_half().intersect(a2.BezierCurve([(0, 0), (1, 1)]))


# ===========================================================================
# 9. point location / queries
# ===========================================================================
def test_supported_point_location_strategies():
    arr = arrangement(circle(0, 0, 2), a2.CircleSegment.segment((-2, 0), (2, 0)))
    supported = {s for s in a2.point_location_strategies()
                 if arr.supports_point_location(s)}
    # landmarks needs Construct_x_monotone_curve_2 (absent) and the triangulation
    # strategy is not exposed at all -- CGAL_TRAPS_CHECKLIST, "Point location".
    assert supported == {"naive", "simple", "walk", "trapezoid"}


def test_unsupported_point_location_strategies_raise():
    arr = arrangement(circle(0, 0, 2))
    for bad in ("landmarks", "triangulation"):
        with pytest.raises(a2.UnsupportedError):
            arr.locate((0, 1), strategy=bad)


def test_locate_face_edge_and_unbounded():
    arr = arrangement(circle(0, 0, 2), a2.CircleSegment.segment((-2, 0), (2, 0)))
    inside = arr.locate((0, 1))
    assert isinstance(inside, a2.Face) and not inside.is_unbounded
    on_edge = arr.locate((0, 0))                    # interior of the chord
    assert isinstance(on_edge, a2.Halfedge)
    at_vertex = arr.locate((2, 0))
    assert isinstance(at_vertex, a2.Vertex)
    assert arr.locate((100, 100)).is_unbounded


def test_locate_agrees_across_strategies():
    arr = arrangement(circle(0, 0, 2), a2.CircleSegment.segment((-2, 0), (2, 0)))
    ids = {s: arr.locate((0, 1), strategy=s).id
           for s in ("naive", "simple", "walk", "trapezoid")}
    assert len(set(ids.values())) == 1, ids


def test_locate_with_a_sqrt_query_point():
    arr = arrangement(circle(0, 0, 2))
    # (sqrt(2), sqrt(2)) is exactly on the circle: x^2 + y^2 = 2 + 2 = 4
    root2 = a2.SqrtExtension(0, 1, 2)
    p = a2.Point.from_sqrt_extension(root2, root2)
    located = arr.locate(p)
    assert isinstance(located, a2.Halfedge)
    assert located.curve.compare_y_at_x(p) == 0


def test_ray_shooting():
    arr = arrangement(circle(0, 0, 2), a2.CircleSegment.segment((-2, 0), (2, 0)))
    up = arr.ray_shoot_up((0, Fraction(1, 2)))
    assert isinstance(up, a2.Halfedge) and up.curve.is_circular
    down = arr.ray_shoot_down((0, Fraction(1, 2)))
    assert isinstance(down, a2.Halfedge) and down.curve.is_linear


def test_batched_locate():
    arr = arrangement(circle(0, 0, 2), a2.CircleSegment.segment((-2, 0), (2, 0)))
    results = arr.batched_locate([(0, 1), (0, -1), (100, 100), (0, 0)])
    assert len(results) == 4
    assert isinstance(results[0], a2.Face) and not results[0].is_unbounded
    assert isinstance(results[1], a2.Face) and not results[1].is_unbounded
    assert results[0].id != results[1].id           # upper vs lower half disc
    assert results[2].is_unbounded
    assert isinstance(results[3], a2.Halfedge)      # on the chord


def test_zone_and_do_intersect():
    arr = arrangement(circle(0, 0, 2), a2.CircleSegment.segment((-2, 0), (2, 0)))
    edges_before = arr.number_of_edges
    # a segment from inside the upper half disc out through the arc
    z = arr.zone(a2.CircleSegment.segment((0, Fraction(1, 2)), (0, 3)))
    # upper half-disc face, the arc it crosses, the unbounded face = 3 features
    assert len(z) == 3
    assert [type(x).__name__ for x in z] == ["Face", "Halfedge", "Face"]
    assert arr.number_of_edges == edges_before      # zone() never modifies
    assert arr.do_intersect(a2.CircleSegment.segment((0, Fraction(1, 2)), (0, 3)))
    # wholly inside one face -> no intersection with any edge
    assert not arr.do_intersect(
        a2.CircleSegment.segment((0, Fraction(1, 4)), (0, Fraction(1, 2))))
    # wholly outside
    assert not arr.do_intersect(a2.CircleSegment.segment((-5, 5), (5, 5)))


def test_decompose():
    arr = arrangement(circle(0, 0, 2), a2.CircleSegment.segment((-2, 0), (2, 0)))
    entries = arr.decompose()
    # one entry per vertex; this arrangement has exactly the 2 tangency points
    assert len(entries) == 2
    xs = sorted(v.point.xy for v, _below, _above in entries)
    assert xs == [(-2.0, 0.0), (2.0, 0.0)]


# ===========================================================================
# 10. curve history and modification
# ===========================================================================
def test_curve_history_induced_edges():
    arr = a2.Arrangement(KIND)
    h_circle = arr.insert(circle(0, 0, 2))
    h_chord = arr.insert(a2.CircleSegment.segment((-3, 0), (3, 0)))
    # the circle contributes its 2 arcs; the long chord is cut at (-2,0) and (2,0)
    # into 3 pieces.
    assert h_circle.number_of_induced_edges == 2
    assert h_chord.number_of_induced_edges == 3
    # V = (-3,0), (-2,0), (2,0), (3,0); E = 2 + 3; F = upper, lower, outside.
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == (4, 5, 3)
    assert arr.number_of_curves == 2
    assert euler(arr)


def test_originating_curves():
    arr = a2.Arrangement(KIND)
    h_circle = arr.insert(circle(0, 0, 2))
    arr.insert(a2.CircleSegment.segment((-3, 0), (3, 0)))
    arcs = [e for e in arr.edges() if e.curve.is_circular]
    assert len(arcs) == 2
    for e in arcs:
        originators = e.originating_curves()
        assert len(originators) == 1
        assert originators[0].id == h_circle.id


def test_remove_curve():
    arr = a2.Arrangement(KIND)
    arr.insert(circle(0, 0, 2))
    h_chord = arr.insert(a2.CircleSegment.segment((-3, 0), (3, 0)))
    removed = arr.remove_curve(h_chord)
    assert removed == 3                             # the 3 induced chord pieces
    # back to the bare circle
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == (2, 2, 2)
    assert arr.number_of_curves == 1
    assert arr.is_valid()
    assert not h_chord.is_valid


def test_insert_non_intersecting_arcs():
    arr = a2.Arrangement(KIND)
    lower, upper = circle(0, 0, 2).make_x_monotone()
    he = arr.insert_non_intersecting(lower)
    assert isinstance(he, a2.Halfedge)
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == (2, 1, 1)
    arr.insert_non_intersecting_curves([upper])
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == (2, 2, 2)
    assert arr.number_of_curves == 0                # no history for this entry point
    assert arr.is_valid()


def test_insert_non_intersecting_rejects_a_full_circle():
    with pytest.raises(a2.NotXMonotoneError):
        a2.Arrangement(KIND).insert_non_intersecting(circle(0, 0, 1))


def test_split_and_merge_edge():
    arr = arrangement(circle(0, 0, 2))
    lower = [e for e in arr.edges() if e.curve.compare_y_at_x((0, -2)) == 0][0]
    he = arr.split_edge(lower, (0, -2))
    # splitting one of the 2 arcs adds one vertex and one edge, faces unchanged
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == (3, 3, 2)
    assert arr.is_valid()
    merged = arr.merge_edge(he, he.next)
    assert isinstance(merged, a2.Halfedge)
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == (2, 2, 2)
    assert arr.is_valid()


def test_remove_edge_opens_the_disc():
    arr = arrangement(circle(0, 0, 2))
    face = arr.remove_edge(arr.edges()[0])
    # one arc left: the disc merges into the unbounded face
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == (2, 1, 1)
    assert face.is_unbounded
    assert arr.is_valid()


def test_insert_isolated_and_sqrt_points():
    arr = arrangement(circle(0, 0, 2))
    v = arr.insert_point((0, 1))
    assert v.is_isolated and arr.number_of_isolated_vertices == 1
    root2 = a2.SqrtExtension(0, 1, 2)
    on_circle = a2.Point.from_sqrt_extension(root2, root2)
    v2 = arr.insert_point(on_circle)
    # (sqrt(2), sqrt(2)) lies on the circle, so it splits an arc rather than staying
    # isolated: V goes 2 -> 3 (isolated) -> 4.
    assert arr.number_of_vertices == 4
    assert not v2.point.is_rational
    assert arr.is_valid()


def test_observer_sees_the_circle_being_built():
    events = {"vertex": 0, "edge": 0, "split_face": 0}

    class Counter(a2.Observer):
        def after_create_vertex(self, v):
            events["vertex"] += 1

        def after_create_edge(self, e):
            events["edge"] += 1

        def after_split_face(self, f, new_f, is_hole):
            events["split_face"] += 1

    arr = a2.Arrangement(KIND)
    obs = Counter()
    arr.add_observer(obs)
    arr.insert(circle(0, 0, 2))
    arr.remove_observer(obs)
    # 2 tangency vertices, 2 arcs, and the second arc closes the disc (1 face split)
    assert events == {"vertex": 2, "edge": 2, "split_face": 1}


def test_copy_preserves_geometry_and_data():
    arr = arrangement(circle(0, 0, 2), a2.CircleSegment.segment((-2, 0), (2, 0)))
    arr.vertices()[0].data = "tag"
    clone = arr.copy()
    assert (clone.number_of_vertices, clone.number_of_edges,
            clone.number_of_faces) == (2, 3, 3)
    assert clone.number_of_curves == 2 and clone.is_valid()
    assert sorted(str(v.data) for v in clone.vertices()) == ["None", "tag"]
    # the clone's handles are independent of the original's
    stale = arr.vertices()[0]
    arr.clear()
    assert not stale.is_valid
    assert clone.number_of_vertices == 2


def test_face_polygon_of_a_half_disc():
    arr = arrangement(circle(0, 0, 2), a2.CircleSegment.segment((-2, 0), (2, 0)))
    face = arr.locate((0, 1))
    assert face.number_of_outer_ccbs == 1 and face.number_of_inner_ccbs == 0
    pwh = face.polygon()
    assert len(pwh.outer) == 2 and len(pwh.holes) == 0
    kinds = sorted(c.is_circular for c in pwh.outer)
    assert kinds == [False, True]                   # one chord + one arc
    # the exact boundary is a closed, counterclockwise ring
    assert pwh.outer.is_closed and pwh.outer.orientation() == 1
    # ... and its approximate area is the half disc, pi*2^2/2 = 2*pi
    outer_points, holes = pwh.approximate(1e-4)
    assert holes == []
    assert abs(shoelace(outer_points) - 2 * math.pi) / (2 * math.pi) < 1e-3


def test_unbounded_face_has_the_disc_as_a_hole():
    arr = arrangement(circle(0, 0, 2))
    uf = arr.unbounded_face
    assert uf.is_unbounded
    assert uf.number_of_outer_ccbs == 0 and uf.number_of_inner_ccbs == 1
    assert len(uf.inner_ccbs()[0]) == 2             # the 2 arcs seen from outside
    with pytest.raises(a2.UnsupportedError):
        uf.polygon()


# ===========================================================================
# 11. Boolean set operations on discs
# ===========================================================================
def test_polygon_from_a_circle():
    p = disc(0, 0, 1)
    assert p.kind == a2.Kind.CIRCLE_SEGMENT
    assert len(p) == 2                              # the 2 x-monotone arcs
    assert p.is_closed and p.orientation() == 1     # CCW circle -> CCW ring
    assert p.is_simple()
    # the ring's "vertices" are the two vertical tangency points
    assert sorted(q.xy for q in p.points) == [(-1.0, 0.0), (1.0, 0.0)]
    assert p.bbox() == (-1.0, -1.0, 1.0, 1.0)
    # Polygon.area() is the shoelace area of approximate(1e-3) for curved kinds
    assert abs(p.area() - math.pi) / math.pi < 1e-3


def test_polygon_reverse_flips_orientation():
    p = disc(0, 0, 1)
    r = p.reverse()
    assert r.orientation() == -1
    assert not r.is_simple()                        # CGAL wants CCW outer boundaries
    assert a2.orientation(p) == 1 and a2.orientation(r) == -1
    assert a2.is_valid_polygon(p) and not a2.is_valid_polygon(r)


def test_polygon_set_of_one_disc():
    s = pset(disc(0, 0, 1))
    assert s.kind == a2.Kind.CIRCLE_SEGMENT
    assert s.number_of_polygons_with_holes == 1 and len(s) == 1
    assert not s.is_empty and not s.is_plane
    assert s.is_valid()
    # oriented_side: +1 inside, 0 on the boundary, -1 outside
    assert s.oriented_side((0, 0)) == 1
    assert s.oriented_side((1, 0)) == 0
    assert s.oriented_side((5, 5)) == -1
    # (sqrt(2)/2, sqrt(2)/2) is on the unit circle: 1/2 + 1/2 = 1
    half_root2 = a2.SqrtExtension(0, Fraction(1, 2), 2)
    assert s.oriented_side(a2.Point.from_sqrt_extension(half_root2, half_root2)) == 0


def test_polygon_set_locate():
    s = pset(disc(0, 0, 1))
    found = s.locate((0, 0))
    assert isinstance(found, a2.PolygonWithHoles)
    assert len(found.outer) == 2
    assert s.locate((9, 9)) is None


def test_union_of_two_overlapping_discs():
    u = pset(disc(0, 0, 1)) | pset(disc(1, 0, 1))
    assert u.number_of_polygons_with_holes == 1
    pwh = u.polygons_with_holes()[0]
    # the union boundary is 4 arcs: each circle's outside half is cut in two at its own
    # vertical tangency point ((-1,0) for A, (2,0) for B).
    assert len(pwh.outer) == 4 and len(pwh.holes) == 0
    assert all(c.is_circular for c in pwh.outer)
    assert pwh.outer.orientation() == 1 and not pwh.is_unbounded
    # area = 2*pi*r^2 - lens
    assert abs(pwh_area(pwh) - (2 * math.pi - LENS)) / (2 * math.pi - LENS) < 1e-3


def test_intersection_of_two_overlapping_discs_is_a_lens():
    i = pset(disc(0, 0, 1)) & pset(disc(1, 0, 1))
    assert i.number_of_polygons_with_holes == 1
    pwh = i.polygons_with_holes()[0]
    # the lens boundary is 4 arcs as well (each inner half split at a tangency point)
    assert len(pwh.outer) == 4 and len(pwh.holes) == 0
    assert abs(pwh_area(pwh) - LENS) / LENS < 1e-3
    # the lens spans x in [0, 1] and y in [-sqrt(3)/2, sqrt(3)/2]
    xmin, ymin, xmax, ymax = pwh.bbox()
    assert abs(xmin - 0.0) < 1e-12 and abs(xmax - 1.0) < 1e-12
    assert abs(ymax - math.sqrt(3) / 2) < 1e-12 and abs(ymin + math.sqrt(3) / 2) < 1e-12


def test_difference_of_two_overlapping_discs():
    d = pset(disc(0, 0, 1)) - pset(disc(1, 0, 1))
    assert d.number_of_polygons_with_holes == 1
    pwh = d.polygons_with_holes()[0]
    assert len(pwh.outer) == 4 and len(pwh.holes) == 0
    assert abs(pwh_area(pwh) - (math.pi - LENS)) / (math.pi - LENS) < 1e-3
    assert d.oriented_side((Fraction(-1, 2), 0)) == 1     # in the crescent
    assert d.oriented_side((Fraction(1, 2), 0)) == -1     # in the removed lens


def test_symmetric_difference_of_two_overlapping_discs():
    s = pset(disc(0, 0, 1)) ^ pset(disc(1, 0, 1))
    # the two crescents touch at the two crossing points, so CGAL emits ONE
    # relatively-simple polygon with a hole rather than two polygons.
    assert s.number_of_polygons_with_holes == 1
    pwh = s.polygons_with_holes()[0]
    assert len(pwh.outer) == 4 and len(pwh.holes) == 1
    assert len(pwh.holes[0]) == 4                   # the lens, as a clockwise hole
    assert pwh.holes[0].orientation() == -1
    # area = union - intersection = (2 pi - lens) - lens
    expected = 2 * math.pi - 2 * LENS
    assert abs(pwh_area(pwh) - expected) / expected < 1e-3


def test_union_of_disjoint_discs_is_two_polygons():
    u = pset(disc(0, 0, 1)) | pset(disc(10, 0, 1))
    assert u.number_of_polygons_with_holes == 2
    assert all(len(p.outer) == 2 for p in u.polygons_with_holes())
    assert abs(sum(pwh_area(p) for p in u.polygons_with_holes())
               - 2 * math.pi) / (2 * math.pi) < 1e-3


def test_intersection_of_disjoint_discs_is_empty():
    i = pset(disc(0, 0, 1)) & pset(disc(10, 0, 1))
    assert i.is_empty and i.number_of_polygons_with_holes == 0
    assert not pset(disc(0, 0, 1)).do_intersect(pset(disc(10, 0, 1)))
    assert pset(disc(0, 0, 1)).do_intersect(pset(disc(1, 0, 1)))


def test_externally_tangent_discs():
    a, b = pset(disc(0, 0, 1)), pset(disc(2, 0, 1))
    # regularised Boolean semantics: a single contact point has empty interior
    assert (a & b).is_empty
    assert not a.do_intersect(b)
    assert a.oriented_side(b) == 0                  # touching, not overlapping
    # the union is still emitted as one relatively-simple polygon
    assert (a | b).number_of_polygons_with_holes == 1
    assert abs(pwh_area((a | b).polygons_with_holes()[0])
               - 2 * math.pi) / (2 * math.pi) < 1e-3


def test_annulus_has_one_hole():
    ann = pset(disc(0, 0, 3)) - pset(disc(0, 0, 1))
    assert ann.number_of_polygons_with_holes == 1
    pwh = ann.polygons_with_holes()[0]
    assert len(pwh.outer) == 2 and len(pwh.holes) == 1 and len(pwh.holes[0]) == 2
    assert pwh.outer.orientation() == 1 and pwh.holes[0].orientation() == -1
    assert ann.oriented_side((0, 0)) == -1          # in the hole
    assert ann.oriented_side((2, 0)) == 1           # in the ring
    assert ann.oriented_side((9, 9)) == -1          # outside
    # area = pi*(3^2 - 1^2) = 8 pi
    assert abs(pwh_area(pwh) - 8 * math.pi) / (8 * math.pi) < 1e-3


def test_complement_of_a_disc():
    c = pset(disc(0, 0, 1))
    c.complement()
    assert not c.is_plane and not c.is_empty
    assert c.number_of_polygons_with_holes == 1
    pwh = c.polygons_with_holes()[0]
    assert pwh.is_unbounded and pwh.outer is None
    assert len(pwh.holes) == 1 and len(pwh.holes[0]) == 2
    assert c.oriented_side((9, 9)) == 1
    assert c.oriented_side((0, 0)) == -1
    # complementing twice gives the disc back
    c.complement()
    assert c.number_of_polygons_with_holes == 1
    assert abs(pwh_area(c.polygons_with_holes()[0]) - math.pi) / math.pi < 1e-3


def test_polygon_set_free_functions_match_the_methods():
    a, b = pset(disc(0, 0, 1)), pset(disc(1, 0, 1))
    assert a2.join(a, b).number_of_polygons_with_holes == 1
    assert a2.intersection(a, b).number_of_polygons_with_holes == 1
    assert a2.difference(a, b).number_of_polygons_with_holes == 1
    assert a2.symmetric_difference(a, b).number_of_polygons_with_holes == 1
    assert a2.complement(a).number_of_polygons_with_holes == 1
    assert a2.do_intersect(a, b)
    assert a2.oriented_side(a, (0, 0)) == 1
    # the operands are untouched by the free functions
    assert a.number_of_polygons_with_holes == 1 and b.number_of_polygons_with_holes == 1


def test_polygon_set_copy_is_independent():
    a = pset(disc(0, 0, 1))
    b = a.copy()
    b.join(pset(disc(10, 0, 1)))
    assert b.number_of_polygons_with_holes == 2
    assert a.number_of_polygons_with_holes == 1
    b.clear()
    assert b.is_empty and a.number_of_polygons_with_holes == 1


def test_polygon_set_rejects_a_clockwise_boundary():
    s = a2.PolygonSet(KIND)
    # CGAL requires CCW outer boundaries; fix_orientation=True repairs it silently,
    # fix_orientation=False must report it.
    with pytest.raises(ValueError):
        s.insert(disc(0, 0, 1).reverse(), fix_orientation=False)
    s.insert(disc(0, 0, 1).reverse())               # fixed for us
    assert s.number_of_polygons_with_holes == 1


def test_polygon_set_rejects_a_foreign_kind():
    with pytest.raises(a2.KindMismatchError):
        pset(disc(0, 0, 1)).join(a2.PolygonSet("segment"))


def test_polygon_set_of_an_irrational_radius_disc():
    p = a2.Polygon([a2.CircleSegment.circle((0, 0), squared_radius=2)])
    s = a2.PolygonSet(KIND)
    s.insert(p)
    assert s.number_of_polygons_with_holes == 1
    assert s.oriented_side((0, 0)) == 1
    assert s.oriented_side((2, 0)) == -1             # 4 > 2, outside
    assert s.oriented_side((1, 1)) == 0              # 1 + 1 == 2, exactly on the circle
    # area = pi * r^2 = 2 pi
    assert abs(p.area() - 2 * math.pi) / (2 * math.pi) < 1e-3


def test_polygon_set_of_a_half_disc():
    poly = a2.Polygon([upper_half(r=2),
                       a2.CircleSegment.segment((-2, 0), (2, 0)).x_monotone()])
    assert poly.is_closed and poly.orientation() == 1
    s = a2.PolygonSet(KIND)
    s.insert(poly)
    assert s.number_of_polygons_with_holes == 1
    assert s.oriented_side((0, 1)) == 1
    assert s.oriented_side((0, -1)) == -1
    assert s.oriented_side((0, 0)) == 0             # on the diameter
    assert abs(poly.area() - 2 * math.pi) / (2 * math.pi) < 1e-3


# ===========================================================================
# 12. PolygonSet.to_arrangement
# ===========================================================================
def test_to_arrangement_of_a_single_disc():
    arr, contained = pset(disc(0, 0, 1)).to_arrangement()
    assert arr.kind == a2.Kind.CIRCLE_SEGMENT
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == (2, 2, 2)
    assert arr.is_valid()
    assert len(contained) == 1
    assert not contained[0].is_unbounded
    assert contained[0].number_of_outer_ccbs == 1


def test_to_arrangement_of_a_union():
    u = pset(disc(0, 0, 1)) | pset(disc(1, 0, 1))
    arr, contained = u.to_arrangement()
    # only the union's 4 boundary arcs are inserted (the inner halves are gone), so
    # V = the 2 crossings + the 2 outer tangency points = 4, E = 4, F = 2.
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == (4, 4, 2)
    assert arr.is_valid() and euler(arr)
    assert len(contained) == 1 and not contained[0].is_unbounded


def test_to_arrangement_of_a_lens():
    lens = pset(disc(0, 0, 1)) & pset(disc(1, 0, 1))
    arr, contained = lens.to_arrangement()
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == (4, 4, 2)
    assert len(contained) == 1
    face = contained[0]
    assert not face.is_unbounded and face.number_of_inner_ccbs == 0
    assert len(face.outer_ccb()) == 4


def test_to_arrangement_of_an_annulus():
    ann = pset(disc(0, 0, 3)) - pset(disc(0, 0, 1))
    arr, contained = ann.to_arrangement()
    # 2 circles, 2 tangency vertices and 2 arcs each; F = hole, ring, outside = 3.
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == (4, 4, 3)
    assert euler(arr, components=2)                 # two disjoint circles
    assert len(contained) == 1
    face = contained[0]
    assert not face.is_unbounded
    assert face.number_of_outer_ccbs == 1 and face.number_of_inner_ccbs == 1


def test_to_arrangement_of_a_complement():
    c = pset(disc(0, 0, 1))
    c.complement()
    arr, contained = c.to_arrangement()
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == (2, 2, 2)
    assert len(contained) == 1
    assert contained[0].is_unbounded


def test_to_arrangement_of_an_empty_set():
    arr, contained = a2.PolygonSet(KIND).to_arrangement()
    assert arr.number_of_edges == 0
    assert contained == []


# ===========================================================================
# 13. conversions, equality and repr
# ===========================================================================
def test_curve_equality_is_geometric():
    assert upper_half() == upper_half()
    assert upper_half() != lower_half()
    # opposite() keeps the point set, so the curves stay equal
    assert upper_half() == upper_half().opposite()
    # cross-kind comparison never raises, it just says "not equal"
    assert upper_half() != a2.Segment((0, 0), (1, 1))


def test_full_circles_compare_geometrically():
    # Curve.__eq__ promotes both operands to a single x-monotone curve; a full circle
    # cannot be promoted, so the comparison falls back to a PIECEWISE comparison of the
    # two Make_x_monotone_2 decompositions (which come out in parametric order).
    # It used to fall back to identity instead, which made two equal circles unequal.
    c1, c2 = circle(0, 0, 2), circle(0, 0, 2)
    assert c1 == c1
    assert c1 == c2
    assert c1 != circle(0, 0, 3)
    assert c1 != circle(1, 0, 2)
    # ... and their x-monotone pieces still compare exactly
    assert c1.make_x_monotone()[0] == c2.make_x_monotone()[0]


def test_curves_are_unhashable():
    with pytest.raises(TypeError):
        hash(upper_half())


def test_straight_curve_converts_to_and_from_the_segment_kind():
    s = a2.CircleSegment.segment((0, 0), (3, 4))
    as_segment = s.to_kind("segment")
    assert isinstance(as_segment, a2.Segment)
    assert as_segment.source == a2.Point(0, 0) and as_segment.target == a2.Point(3, 4)
    back = a2.Segment((0, 0), (3, 4)).to_kind(KIND)
    assert back.kind == a2.Kind.CIRCLE_SEGMENT and back.is_linear


def test_arcs_do_not_convert_to_the_segment_kind():
    with pytest.raises(a2.NotRepresentableError):
        upper_half().to_kind("segment")
    with pytest.raises(a2.NotRepresentableError):
        circle(0, 0, 1).to_kind("segment")


def test_circle_segment_curves_convert_to_the_conic_kind():
    arc = upper_half(r=2).to_kind("conic")
    assert arc.kind == a2.Kind.CONIC
    full = circle(0, 0, 1).to_kind("conic")
    assert full.kind == a2.Kind.CONIC and full.is_full


def test_points_convert_between_rational_kinds():
    p = a2.Point(1, 2, kind=KIND)
    assert p.to_kind("segment") == a2.Point(1, 2)
    assert a2.Point(1, 2).to_kind(KIND) == p
    assert p.to_kind(KIND) is p


def test_repr_round_trips_the_shape_description():
    assert repr(circle(0, 0, 2)) == (
        "Circle(center=(0, 0), squared_radius=4, orientation=ccw)")
    assert repr(circle(0, 0, 2, orientation="cw")) == (
        "Circle(center=(0, 0), squared_radius=4, orientation=cw)")
    assert repr(a2.CircleSegment.segment((0, 0), (1, 2))) == "Segment((0, 0), (1, 2))"
    assert repr(upper_half(r=2)) == (
        "CircularArc(center=(0, 0), squared_radius=4, orientation=ccw, "
        "source=(2, 0), target=(-2, 0))")
    assert repr(a2.Point.from_sqrt_extension(a2.SqrtExtension(0, 1, 3), 0)) == (
        "(0 + 1*sqrt(3), 0)")
    assert repr(a2.Point.from_sqrt_extension(a2.SqrtExtension(0, -1, 3), 0)) == (
        "(0 - 1*sqrt(3), 0)")


def test_kind_mismatch_when_inserting_an_unconvertible_curve():
    arr = a2.Arrangement("segment")
    with pytest.raises(a2.NotRepresentableError):
        arr.insert(circle(0, 0, 1))
    # the other direction works: a straight segment converts into a circle-segment curve
    cs_arr = a2.Arrangement(KIND)
    handle = cs_arr.insert(a2.Segment((0, 0), (1, 1)))
    assert handle.number_of_induced_edges == 1


def test_bulk_export():
    arr = arrangement(circle(0, 0, 2), a2.CircleSegment.segment((-2, 0), (2, 0)))
    coords = arr.vertex_coordinates()
    assert len(coords) == 2 and len(coords[0]) == 2
    assert sorted(tuple(c) for c in coords) == [(-2.0, 0.0), (2.0, 0.0)]
    edges = arr.edge_vertex_indices()
    assert len(edges) == 3                          # 2 arcs + the chord
    assert all(set(map(int, e)) == {0, 1} for e in edges)
    boundaries = arr.face_boundaries()
    assert len(boundaries) == 3                     # 3 faces
    approx = arr.approximate_edges(1e-2)
    assert len(approx) == 3
    # the chord is a straight edge: exactly 2 points; the arcs need many more
    assert sorted(len(chunk) for chunk in approx)[0] == 2


# ===========================================================================
# 14. error-class contract, aggregate insertion, misc invariants
# ===========================================================================
def test_api_misuse_raises_plain_value_error():
    # arrangement_2d/errors.py maps ErrorCode::InvalidArgument to the *builtin*
    # ValueError, so these are NOT CGALError subclasses; the geometry-kind errors are.
    with pytest.raises(ValueError) as excinfo:
        circle(0, 0, -1)
    assert not isinstance(excinfo.value, a2.CGALError)
    assert "circle_segment" in str(excinfo.value)


def test_geometry_errors_use_the_documented_classes():
    with pytest.raises(a2.NotXMonotoneError):
        circle(0, 0, 1).source                       # a full circle has no source
    with pytest.raises(a2.NotRepresentableError):
        circle(0, 0, 1).to_kind("segment")
    with pytest.raises(a2.UnsupportedError):
        a2.traits(KIND).construct_x_monotone_curve((0, 0), (1, 1))
    arr = arrangement(circle(0, 0, 1))
    v = arr.vertices()[0]
    arr.clear()
    with pytest.raises(a2.InvalidHandleError):
        v.point
    # all four are CGALError subclasses
    for cls in (a2.NotXMonotoneError, a2.NotRepresentableError,
                a2.UnsupportedError, a2.InvalidHandleError):
        assert issubclass(cls, a2.CGALError)


def test_insert_iterable_returns_one_handle_per_curve():
    arr = a2.Arrangement(KIND)
    handles = arr.insert([circle(0, 0, 2), a2.CircleSegment.segment((-3, 0), (3, 0))])
    assert isinstance(handles, list) and len(handles) == 2
    assert all(isinstance(h, a2.CurveHandle) for h in handles)
    assert [h.number_of_induced_edges for h in handles] == [2, 3]
    # a single point insert yields a Vertex instead
    assert isinstance(arr.insert(a2.Point(0, 1, kind=KIND)), a2.Vertex)


def test_aggregate_insertion_matches_incremental_insertion():
    curves = [circle(0, 0, 2), circle(2, 0, 2)]
    incremental = a2.Arrangement(KIND)
    for c in curves:
        incremental.insert(c)
    aggregate = a2.Arrangement(KIND)
    handles = aggregate.insert_curves(curves)
    assert len(handles) == 2
    assert ((aggregate.number_of_vertices, aggregate.number_of_edges,
             aggregate.number_of_faces)
            == (incremental.number_of_vertices, incremental.number_of_edges,
                incremental.number_of_faces)
            == (6, 8, 4))
    assert aggregate.is_valid()


def test_assign_replaces_the_content():
    src = arrangement(circle(0, 0, 2))
    dst = a2.Arrangement(KIND)
    dst.insert(a2.CircleSegment.segment((10, 10), (11, 11)))
    dst.assign(src)
    assert (dst.number_of_vertices, dst.number_of_edges, dst.number_of_faces) == (2, 2, 2)
    assert dst.number_of_curves == 1


def test_chain_of_five_circles_euler():
    # r = 2 centred at x = 0, 3, 6, 9, 12.  Neighbouring centres are 3 apart
    # (0 < 3 < 4 = 2r) so each of the 4 neighbouring pairs crosses twice; centres two
    # steps apart are 6 > 4 apart and never meet.
    arr = a2.Arrangement(KIND)
    arr.insert([circle(3 * i, 0, 2) for i in range(5)])
    # V = 5*2 tangency points + 4*2 crossings = 18 (all distinct: tangencies sit at
    # x = 3i +- 2, crossings at x = 3i + 3/2 with y != 0)
    # E: the end circles carry 4 vertices each (4 arcs), the 3 middle ones 6 (6 arcs)
    #    => 2*4 + 3*6 = 26
    # F = 2 - V + E = 10 (one connected component)
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == (18, 26, 10)
    assert arr.is_valid() and euler(arr)


def test_compare_y_at_x_outside_the_x_range_raises():
    with pytest.raises(ValueError):
        upper_half(r=2).compare_y_at_x(a2.Point(5, 0, kind=KIND))


def test_bounded_curve_ends_are_interior():
    arc = upper_half(r=2)
    assert arc.is_bounded and arc.has_source and arc.has_target
    assert arc.dimension == 2
    # CGAL's ARR_INTERIOR == 4 for every end of a bounded curve
    for end in (0, 1):
        assert arc.parameter_space_in_x(end) == 4
        assert arc.parameter_space_in_y(end) == 4


def test_polygon_construction_validates_the_chain():
    with pytest.raises(ValueError):
        a2.Polygon([a2.CircleSegment.segment((0, 0), (1, 0)),
                    a2.CircleSegment.segment((2, 0), (3, 0))])       # broken chain
    with pytest.raises(ValueError):
        a2.Polygon([circle(0, 0, 0)])                                # degenerates to a point
    with pytest.raises(ValueError):
        a2.Polygon([])


def test_polygon_of_straight_curves_converts_to_the_segment_kind():
    tri = a2.Polygon([a2.CircleSegment.segment((0, 0), (1, 0)),
                      a2.CircleSegment.segment((1, 0), (1, 1)),
                      a2.CircleSegment.segment((1, 1), (0, 0))])
    assert tri.is_closed and tri.orientation() == 1 and tri.is_simple()
    as_segments = tri.to_kind("segment")
    assert as_segments.kind == a2.Kind.SEGMENT
    # the segment kind computes an exact area: half of the unit square
    assert as_segments.area() == Fraction(1, 2)


def test_polygon_set_arrangement_size():
    s = pset(disc(0, 0, 1))
    # the underlying GPS arrangement holds the 2 boundary arcs and 2 faces
    assert s.arrangement_size == (2, 2)
    assert s.is_valid()
    assert repr(s) == "<PolygonSet kind='circle_segment' polygons=1>"


def test_face_edges_and_adjacent_faces():
    arr = arrangement(circle(0, 0, 2), a2.CircleSegment.segment((-2, 0), (2, 0)))
    face = arr.locate((0, 1))
    assert len(face.edges()) == 2                    # one arc + the chord
    assert len(face.outer_ccb()) == 2
    # across the chord lies the lower half disc, across the arc the unbounded face
    neighbours = face.adjacent_faces()
    assert len(neighbours) == 2
    assert sorted(f.is_unbounded for f in neighbours) == [False, True]
    assert face.number_of_isolated_vertices == 0 and face.isolated_vertices() == []


def test_halfedge_directed_curve_matches_its_vertices():
    arr = arrangement(circle(0, 0, 2))
    for he in arr.halfedges():
        directed = he.directed_curve
        assert directed.source == he.source.point
        assert directed.target == he.target.point
        assert he.twin.twin.id == he.id
        assert he.edge_id == min(he.id, he.twin.id)
        assert not he.is_fictitious


def test_union_oriented_side_at_the_exact_crossing_point():
    u = pset(disc(0, 0, 1)) | pset(disc(1, 0, 1))
    # the two unit circles cross at (1/2, +-sqrt(3)/2) -- a boundary point of the union
    crossing = a2.Point.from_sqrt_extension(Fraction(1, 2),
                                            a2.SqrtExtension(0, Fraction(1, 2), 3))
    assert u.oriented_side(crossing) == 0
    assert u.oriented_side((Fraction(1, 2), 0)) == 1
    assert u.oriented_side((3, 0)) == -1


def test_higher_precision_interval_still_brackets_the_value():
    p = a2.Point.from_sqrt_extension(a2.SqrtExtension(0, 1, 2), 0)
    for bits in (53, 128, 200):
        (xlo, xhi), _ = p.interval(bits)
        assert xlo <= math.sqrt(2.0) <= xhi
    se = a2.SqrtExtension(0, 1, 2)
    lo, hi = se.refine(256)
    assert lo <= math.sqrt(2.0) <= hi


# ===========================================================================
# 15. specialised insertion primitives, overlay callbacks, hand-built polygons
# ===========================================================================
def test_insertion_primitives_build_a_dangling_chain_then_close_it():
    arr = arrangement(circle(0, 0, 4))               # V=2 E=2 F=2
    inner = arr.locate((0, 0))
    he = arr.insert_in_face_interior(
        a2.CircleSegment.segment((-1, 1), (1, 1)).x_monotone(), inner)
    # a free-floating edge inside the disc: +2 vertices, +1 edge, no new face
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == (4, 3, 2)
    assert he.source.point == a2.Point(-1, 1, kind=KIND)
    assert he.target.point == a2.Point(1, 1, kind=KIND)

    he2 = arr.insert_from_left_vertex(
        a2.CircleSegment.segment((1, 1), (2, 2)).x_monotone(), he.target)
    # attaching a curve at an existing vertex: +1 vertex, +1 edge
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == (5, 4, 2)

    arr.insert_point_in_face_interior(a2.Point(0, -2, kind=KIND), arr.locate((0, -2)))
    assert arr.number_of_isolated_vertices == 1
    assert arr.number_of_vertices == 6

    # closing the chain (-1,1) -> (2,2) creates a new (bounded) face
    arr.insert_at_vertices(a2.CircleSegment.segment((-1, 1), (2, 2)).x_monotone(),
                           he.source, he2.target)
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == (6, 5, 3)
    assert arr.is_valid()


def test_modify_vertex_requires_the_same_point():
    arr = a2.Arrangement(KIND)
    arr.insert(a2.CircleSegment.segment((0, 0), (2, 0)))
    v = [x for x in arr.vertices() if x.point == a2.Point(2, 0, kind=KIND)][0]
    with pytest.raises(a2.PreconditionError):
        arr.modify_vertex(v, a2.Point(3, 0, kind=KIND))
    # replacing it with the very same point is a no-op that succeeds
    assert arr.modify_vertex(v, a2.Point(2, 0, kind=KIND)).point == v.point


def test_modify_edge_with_an_equal_curve():
    arr = a2.Arrangement(KIND)
    arr.insert(a2.CircleSegment.segment((0, 0), (2, 0)))
    e = arr.edges()[0]
    same = a2.CircleSegment.segment((0, 0), (2, 0)).x_monotone()
    assert arr.modify_edge(e, same).curve == same
    assert arr.is_valid()


def test_overlay_callback_event_counts():
    counts = {name: 0 for name in
              ("vertex_vertex", "vertex_edge", "vertex_face", "edge_vertex",
               "face_vertex", "edge_edge_vertex", "edge_edge", "edge_face",
               "face_edge", "face_face")}

    class Recorder(a2.OverlayCallbacks):
        def vertex_vertex(self, va, vb, vr):
            counts["vertex_vertex"] += 1

        def vertex_edge(self, va, eb, vr):
            counts["vertex_edge"] += 1

        def vertex_face(self, va, fb, vr):
            counts["vertex_face"] += 1

        def edge_vertex(self, ea, vb, vr):
            counts["edge_vertex"] += 1

        def face_vertex(self, fa, vb, vr):
            counts["face_vertex"] += 1

        def edge_edge_vertex(self, ea, eb, vr):
            counts["edge_edge_vertex"] += 1

        def edge_edge(self, ea, eb, er):
            counts["edge_edge"] += 1

        def edge_face(self, ea, fb, er):
            counts["edge_face"] += 1

        def face_edge(self, fa, eb, er):
            counts["face_edge"] += 1

        def face_face(self, fa, fb, fr):
            counts["face_face"] += 1

    a = arrangement(circle(0, 0, 2))
    b = arrangement(circle(2, 0, 2))
    r = a.overlay(b, Recorder())
    assert (r.number_of_vertices, r.number_of_edges, r.number_of_faces) == (6, 8, 4)
    # 2 result vertices are A-edge x B-edge crossings, the other 4 are a tangency
    # vertex of one input lying in a face of the other (2 from A, 2 from B).
    assert counts["edge_edge_vertex"] == 2
    assert counts["vertex_face"] == 2 and counts["face_vertex"] == 2
    assert counts["vertex_vertex"] == counts["vertex_edge"] == counts["edge_vertex"] == 0
    # each circle is cut into 4 arcs; every result edge comes from one input edge
    # lying inside a face of the other arrangement (no overlapping edges here).
    assert counts["edge_face"] == 4 and counts["face_edge"] == 4
    assert counts["edge_edge"] == 0
    # 4 result faces: lens, A-only, B-only, outside
    assert counts["face_face"] == 4


def test_overlay_on_face_shortcut_copies_data():
    a = arrangement(circle(0, 0, 2))
    b = arrangement(circle(2, 0, 2))
    for f in a.faces():
        f.data = "outA" if f.is_unbounded else "A"
    for f in b.faces():
        f.data = "outB" if f.is_unbounded else "B"
    r = a.overlay(b, on_face=lambda fa, fb: (fa.data, fb.data))
    labels = sorted(f.data for f in r.faces())
    # exactly the four combinations, one per result face
    assert labels == [("A", "B"), ("A", "outB"), ("outA", "B"), ("outA", "outB")]


def test_polygon_with_holes_built_by_hand():
    outer = disc(0, 0, 3)
    hole = disc(0, 0, 1).reverse()                   # holes must run clockwise
    pwh = a2.PolygonWithHoles(outer, [hole])
    assert not pwh.is_unbounded
    assert len(pwh.outer) == 2 and len(pwh.holes) == 1
    assert pwh.outer.orientation() == 1 and pwh.holes[0].orientation() == -1
    assert pwh.kind == a2.Kind.CIRCLE_SEGMENT
    assert pwh.bbox() == (-3.0, -3.0, 3.0, 3.0)
    s = a2.PolygonSet(KIND)
    s.insert(pwh)
    assert s.number_of_polygons_with_holes == 1
    assert s.oriented_side((0, 0)) == -1             # inside the hole
    assert s.oriented_side((2, 0)) == 1              # inside the ring
    # area = pi*(9 - 1) = 8 pi
    assert abs(pwh_area(pwh) - 8 * math.pi) / (8 * math.pi) < 1e-3


def test_general_arc_approximation_runs_source_to_target():
    # this arc spans the rightmost tangency point, so it is approximated as two pieces
    # concatenated with the duplicated junction dropped.
    arc = a2.CircleSegment.arc((0, 0), 2, source=(0, -2), target=(0, 2))
    points = arc.approximate(1e-2)
    assert points[0] == (0.0, -2.0) and points[-1] == (0.0, 2.0)
    assert max(abs(math.hypot(x, y) - 2.0) for x, y in points) < 1e-12
    # the junction (2,0) appears exactly once, in the middle
    assert sum(1 for p in points if p == (2.0, 0.0)) == 1
    # y increases monotonically along the arc
    assert all(points[i][1] < points[i + 1][1] for i in range(len(points) - 1))


def test_approximate_edges_of_a_circle_arrangement():
    arr = arrangement(circle(0, 0, 2))
    chunks = arr.approximate_edges(1e-2)
    assert len(chunks) == 2                          # one polyline per edge
    for chunk in chunks:
        assert len(chunk) > 2
        assert max(abs(math.hypot(x, y) - 2.0) for x, y in chunk) < 1e-12
