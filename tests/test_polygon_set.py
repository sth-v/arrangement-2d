"""Boolean set operations (:class:`arrangement_2d.PolygonSet`) and the polygon containers.

Everything here uses only the public API (``import arrangement_2d as a2``).  Every
expected number is derived by hand in a comment next to the assertion; the counts that
CGAL alone decides (how many x-monotone curves a boundary is cut into, whether two
touching pieces are reported as one polygon-with-holes or two) are derived from the
geometry as well and cross-checked against the measurements recorded in
``docs/dev/STAGE2_NOTES.md`` (section "polygon_set_impl.hpp, bso_segment.cpp ...").

The main working shapes are axis-parallel squares of the ``segment`` kind:

    A = [0,2] x [0,2]      B = [1,3] x [1,3]      (overlapping)
    D = [5,7] x [5,7]                             (disjoint from A)
    T = [2,4] x [0,2]                             (touching A along x = 2)
    O = [0,4] x [0,4]      I = [1,3] x [1,3]      (nested: O contains I)

A short ``circle_segment`` section repeats the core of the battery on curved boundaries.
"""

from __future__ import annotations

import math
from fractions import Fraction

import pytest

a2 = pytest.importorskip("arrangement_2d")


# ---------------------------------------------------------------------------
# helpers (module level, so that every test stays independent and cheap)
# ---------------------------------------------------------------------------
def sq(x0, y0, x1, y1):
    """The counterclockwise axis-parallel rectangle [x0,x1] x [y0,y1] as a Polygon."""
    return a2.Polygon([(x0, y0), (x1, y0), (x1, y1), (x0, y1)])


def pset(*polygons, kind="segment"):
    """A PolygonSet holding the union of `polygons` (join, so overlaps are allowed)."""
    s = a2.PolygonSet(kind)
    for p in polygons:
        s.join(p)
    return s


def A():
    """[0,2]^2."""
    return pset(sq(0, 0, 2, 2))


def B():
    """[1,3]^2 — overlaps A in [1,2]^2."""
    return pset(sq(1, 1, 3, 3))


def areas(polygon_set):
    """Sorted exact areas of the outer boundaries of every polygon-with-holes."""
    return sorted(p.outer.area() for p in polygon_set.polygons_with_holes())


def total_area(polygon_set):
    """Exact area of the set (outer boundaries are CCW = +, holes are CW = -)."""
    acc = Fraction(0)
    for pwh in polygon_set.polygons_with_holes():
        if pwh.outer is not None:
            acc += pwh.outer.area()
        for h in pwh.holes:
            acc += h.area()
    return acc


def circle_polygon(cx, cy, r):
    """A full circle of the ``circle_segment`` kind as a 2-arc Polygon."""
    return a2.Polygon(a2.CircleSegment.circle((cx, cy), r).make_x_monotone())


# ===========================================================================
# 1. Polygon construction
# ===========================================================================
def test_polygon_from_points_auto_closes():
    # 4 points -> 4 edges: the chain is closed automatically (0,2)->(0,0).
    p = sq(0, 0, 2, 2)
    assert len(p) == 4
    assert len(p.curves) == 4
    assert p.is_closed
    assert p.kind == a2.Kind.SEGMENT
    assert repr(p)


def test_polygon_from_points_explicit_closing_point():
    # The repeated first point is dropped, so this is the same 4-edge polygon.
    closed = a2.Polygon([(0, 0), (2, 0), (2, 2), (0, 2), (0, 0)])
    assert len(closed) == 4
    assert closed == sq(0, 0, 2, 2)


def test_polygon_from_curves():
    # 4 explicit segments that chain end to end -> the same square.
    curves = [a2.Segment((0, 0), (2, 0)), a2.Segment((2, 0), (2, 2)),
              a2.Segment((2, 2), (0, 2)), a2.Segment((0, 2), (0, 0))]
    p = a2.Polygon(curves)
    assert len(p) == 4 and p.is_closed
    assert p == sq(0, 0, 2, 2)


def test_polygon_chaining_error():
    # curve 0 ends at (1,0) but curve 1 starts at (2,0): the chain is broken.
    with pytest.raises(ValueError) as exc:
        a2.Polygon([a2.Segment((0, 0), (1, 0)), a2.Segment((2, 0), (2, 2))])
    assert "broken" in str(exc.value)


def test_polygon_needs_three_points():
    with pytest.raises(ValueError) as exc:
        a2.Polygon([(0, 0), (1, 1)])
    assert "at least 3" in str(exc.value)


def test_polygon_empty_input_rejected():
    with pytest.raises(ValueError):
        a2.Polygon([])


def test_polygon_repeated_point_rejected():
    # (0,0) twice in a row would give a degenerate zero-length edge.
    with pytest.raises(ValueError):
        a2.Polygon([(0, 0), (0, 0), (2, 0), (2, 2)])


def test_polygon_splits_non_x_monotone_curves():
    # A full circle is not x-monotone: it is cut at its two vertical tangency points
    # (-2,0) and (2,0) into exactly 2 x-monotone arcs.
    p = a2.Polygon([a2.CircleSegment.circle((0, 0), 2)])
    assert len(p) == 2
    assert p.is_closed
    assert p.kind == a2.Kind.CIRCLE_SEGMENT


def test_polygon_points_and_sequence_protocol():
    p = sq(0, 0, 2, 2)
    # closed chain -> the 4 curve sources, in traversal order
    assert [pt.xy for pt in p.points] == [(0.0, 0.0), (2.0, 0.0), (2.0, 2.0), (0.0, 2.0)]
    assert len(list(iter(p))) == 4
    assert p[0].source.xy == (0.0, 0.0)


def test_polygon_open_chain():
    # Two chained segments that do not close: still a legal Polygon object, but not a
    # legal CGAL polygon; `points` then also reports the last target -> 3 points.
    open_chain = a2.Polygon([a2.Segment((0, 0), (1, 0)), a2.Segment((1, 0), (1, 1))])
    assert not open_chain.is_closed
    assert len(open_chain.points) == 3
    assert open_chain.orientation() == 0          # undecidable for an open chain
    assert not a2.is_valid_polygon(open_chain)
    with pytest.raises(ValueError):
        open_chain.area()                          # area() needs a closed polygon


def test_polygon_to_kind_and_equality():
    p = sq(0, 0, 2, 2)
    assert p.to_kind("segment") is p                     # same kind -> identity
    poly = p.to_kind("polyline")
    assert poly.kind == a2.Kind.POLYLINE and len(poly) == 4
    assert poly.orientation() == 1                       # orientation survives conversion
    assert p != poly                                     # different kind -> not equal
    assert p != p.reverse()                              # opposite direction
    assert not (p == 5)
    with pytest.raises(TypeError):
        hash(p)


def test_polygon_with_holes_construction():
    outer = sq(0, 0, 4, 4)                # CCW
    hole = sq(1, 1, 3, 3).reverse()       # CW, as CGAL requires
    pwh = a2.PolygonWithHoles(outer, [hole])
    assert pwh.outer is outer
    assert len(pwh.holes) == 1
    assert not pwh.is_unbounded
    assert pwh.kind == a2.Kind.SEGMENT
    assert pwh.bbox() == (0.0, 0.0, 4.0, 4.0)
    outer_pts, hole_pts = pwh.approximate()
    assert len(outer_pts) == 4 and [len(h) for h in hole_pts] == [4]
    assert repr(pwh)


def test_polygon_with_holes_unbounded():
    # outer=None is CGAL's "whole plane minus the holes" polygon.
    up = a2.PolygonWithHoles(None, [sq(1, 1, 3, 3).reverse()])
    assert up.is_unbounded and up.outer is None
    assert up.approximate()[0] is None
    assert up.bbox() == (1.0, 1.0, 3.0, 3.0)   # falls back to the holes
    s = a2.PolygonSet("segment")
    s.insert(up)
    assert s.oriented_side((9, 9)) == 1        # far away -> inside the unbounded region
    assert s.oriented_side((2, 2)) == -1       # inside the hole -> outside the set


# ===========================================================================
# 2. orientation() / reverse() / area() / approximate()
# ===========================================================================
def test_polygon_orientation_ccw_and_cw():
    assert sq(0, 0, 2, 2).orientation() == 1
    # same square listed clockwise
    assert a2.Polygon([(0, 0), (0, 2), (2, 2), (2, 0)]).orientation() == -1


def test_polygon_reverse_roundtrip():
    p = sq(0, 0, 2, 2)
    r = p.reverse()
    assert r.orientation() == -1
    assert len(r) == 4 and r.is_closed
    # reversing twice restores direction *and* the curve order
    assert r.reverse() == p
    # reverse() walks the curves backwards and flips each one, so the reversed chain
    # starts at the source of the last curve, i.e. at the original starting point, and
    # then runs the other way round: (0,0) -> (0,2) -> (2,2) -> (2,0).
    assert [pt.xy for pt in r.points] == [(0.0, 0.0), (0.0, 2.0), (2.0, 2.0), (2.0, 0.0)]


def test_polygon_area_exact_for_segment_kind():
    a = sq(0, 0, 2, 2).area()
    assert a == 4 and isinstance(a, Fraction)          # 2 x 2
    assert sq(0, 0, 2, 2).reverse().area() == -4       # signed: CW is negative


def test_polygon_area_triangle_is_exact_rational():
    # triangle (0,0), (1,0), (0,1): area = 1/2 * 1 * 1
    tri = a2.Polygon([(0, 0), (1, 0), (0, 1)])
    assert tri.area() == Fraction(1, 2)
    # a rational (non-integer) coordinate stays exact: 1/3 * 3 / 2 = 1/2
    tri2 = a2.Polygon([(0, 0), (Fraction(1, 3), 0), (0, 3)])
    assert tri2.area() == Fraction(1, 2)


def test_polygon_approximate_and_bbox():
    p = sq(0, 0, 2, 2)
    # straight segments: 4 vertices, the closing point is not repeated
    assert p.approximate() == [(0.0, 0.0), (2.0, 0.0), (2.0, 2.0), (0.0, 2.0)]
    assert p.bbox() == (0.0, 0.0, 2.0, 2.0)


def test_polygon_is_simple():
    assert sq(0, 0, 2, 2).is_simple()
    assert not a2.Polygon([(0, 0), (2, 2), (2, 0), (0, 2)]).is_simple()   # bow tie
    # is_simple() needs the Boolean set operations, which the polyline kind has not
    with pytest.raises(a2.UnsupportedError):
        sq(0, 0, 2, 2).to_kind("polyline").is_simple()


def test_module_orientation_accepts_polygon_pwh_and_points():
    assert a2.orientation(sq(0, 0, 2, 2)) == 1
    assert a2.orientation([(0, 0), (2, 0), (2, 2), (0, 2)]) == 1
    assert a2.orientation([(0, 0), (0, 2), (2, 2), (2, 0)]) == -1
    # a PolygonWithHoles reports its OUTER boundary's orientation
    pwh = a2.PolygonWithHoles(sq(0, 0, 4, 4), [sq(1, 1, 3, 3).reverse()])
    assert a2.orientation(pwh) == 1
    assert a2.orientation(a2.PolygonWithHoles(None, [])) == 0   # nothing to orient


def test_polygon_orientation_of_degenerate_ring():
    # A closed but completely flat ring (0,0) -> (1,0) -> (2,0) -> (0,0) encloses no
    # area, so its orientation is undecidable.  polygon_set.hpp documents
    # `orientation()` as "+1 ccw, -1 cw, 0 degenerate/not closed" and the implementation
    # comment states it must not throw.
    flat = a2.Polygon([(0, 0), (1, 0), (2, 0)])
    assert flat.area() == 0
    assert not a2.is_valid_polygon(flat)
    assert flat.orientation() == 0


# ===========================================================================
# 3. PolygonSet.insert
# ===========================================================================
def test_polygon_set_basics():
    s = a2.PolygonSet("segment")
    assert s.kind == a2.Kind.SEGMENT
    assert s.is_empty and not s.is_plane
    assert len(s) == 0 and s.number_of_polygons_with_holes == 0
    assert s.is_valid()
    assert s.arrangement_size == (1, 0)      # only the unbounded face, no edge
    assert repr(s)


def test_insert_ccw_square():
    s = a2.PolygonSet("segment")
    s.insert(sq(0, 0, 2, 2))
    assert len(s) == 1 and not s.is_empty
    assert s.is_valid()
    # the underlying arrangement is the square: 4 edges, 2 faces (inside + unbounded)
    assert s.arrangement_size == (2, 4)
    assert total_area(s) == 4


def test_insert_fix_orientation_repairs_clockwise_input():
    cw = a2.Polygon([(0, 0), (0, 2), (2, 2), (2, 0)])
    assert cw.orientation() == -1
    s = a2.PolygonSet("segment")
    s.insert(cw)                                  # fix_orientation=True by default
    assert len(s) == 1
    assert total_area(s) == 4
    # what comes back out is the counterclockwise square
    assert s.polygons_with_holes()[0].outer.orientation() == 1


def test_insert_without_fix_orientation_rejects_clockwise():
    cw = a2.Polygon([(0, 0), (0, 2), (2, 2), (2, 0)])
    s = a2.PolygonSet("segment")
    with pytest.raises(ValueError) as exc:
        s.insert(cw, fix_orientation=False)
    assert "clockwise orientation" in str(exc.value)
    assert s.is_empty                             # nothing was inserted


def test_insert_polygon_with_holes():
    pwh = a2.PolygonWithHoles(sq(0, 0, 4, 4), [sq(1, 1, 3, 3).reverse()])
    s = a2.PolygonSet("segment")
    s.insert(pwh)
    assert len(s) == 1
    assert total_area(s) == 16 - 4                # 4x4 square minus the 2x2 hole
    assert s.oriented_side((2, 2)) == -1          # the hole is not part of the set
    assert s.oriented_side((0.5, 0.5)) == 1


def test_insert_fixes_hole_orientation():
    # the hole is given counterclockwise; fix_orientation reverses it
    pwh = a2.PolygonWithHoles(sq(0, 0, 4, 4), [sq(1, 1, 3, 3)])
    s = a2.PolygonSet("segment")
    s.insert(pwh)
    assert len(s) == 1 and total_area(s) == 12
    assert s.polygons_with_holes()[0].holes[0].orientation() == -1


def test_insert_without_fix_orientation_rejects_ccw_hole():
    pwh = a2.PolygonWithHoles(sq(0, 0, 4, 4), [sq(1, 1, 3, 3)])
    with pytest.raises(ValueError) as exc:
        a2.PolygonSet("segment").insert(pwh, fix_orientation=False)
    assert "hole 0 has counterclockwise orientation" in str(exc.value)


def test_insert_accepts_several_input_shapes():
    # a bare sequence of points is one polygon
    s1 = a2.PolygonSet("segment")
    s1.insert([(0, 0), (1, 0), (1, 1), (0, 1)])
    assert len(s1) == 1 and total_area(s1) == 1

    # a sequence of point sequences is one polygon each (they must be disjoint)
    s2 = a2.PolygonSet("segment")
    s2.insert([[(0, 0), (1, 0), (1, 1), (0, 1)], [(3, 3), (4, 3), (4, 4), (3, 4)]])
    assert len(s2) == 2 and total_area(s2) == 2

    # a list of Polygon objects
    s3 = a2.PolygonSet("segment")
    s3.insert([sq(0, 0, 1, 1), sq(3, 3, 4, 4)])
    assert len(s3) == 2

    # an empty iterable inserts nothing
    s4 = a2.PolygonSet("segment")
    s4.insert([])
    assert s4.is_empty


def test_insert_rejects_self_intersecting_polygon():
    bow_tie = a2.Polygon([(0, 0), (2, 2), (2, 0), (0, 2)])
    assert not a2.is_valid_polygon(bow_tie)
    with pytest.raises(ValueError) as exc:
        a2.PolygonSet("segment").insert(bow_tie)
    assert "not simple" in str(exc.value)


def test_insert_requires_content_to_be_disjoint():
    # CGAL's insert() precondition (polygon_set.hpp): the new polygon must be COMPLETELY
    # disjoint from what is already there -- it is deliberately not checked (it would
    # cost a full Boolean operation).  [0,2]^2 and [2,4]x[0,2] share the edge x = 2, so
    # the result is undefined: CGAL either trips an assertion or builds a set that fails
    # is_valid().  What it never does is compute the union, which is why join() exists.
    s = a2.PolygonSet("segment")
    s.insert(sq(0, 0, 2, 2))
    try:
        s.insert(sq(2, 0, 4, 2))
    except a2.CGALError:
        pass                                        # the assertion path
    else:
        assert not s.is_valid()                     # the silently-broken path

    ok = a2.PolygonSet("segment")
    ok.insert(sq(0, 0, 2, 2))
    ok.join(sq(2, 0, 4, 2))
    assert len(ok) == 1 and total_area(ok) == 8    # the 4x2 rectangle


def test_insert_of_degenerate_ring_reports_a_bad_argument():
    # A flat, zero-area ring is not a valid polygon; insert() must reject it the same way
    # it rejects a bow tie (ValueError from the validation in polygon_set_impl.hpp),
    # not escape as an internal CGAL assertion.
    flat = a2.Polygon([(0, 0), (1, 0), (2, 0)])
    with pytest.raises(ValueError):
        a2.PolygonSet("segment").insert(flat)


# ===========================================================================
# 4. Boolean operations — hand-derived polygon and curve counts
# ===========================================================================
def test_join_overlapping_squares():
    # A = [0,2]^2, B = [1,3]^2 overlap in [1,2]^2.  The union is an L-shaped octagon:
    # (0,0) (2,0) (2,1) (3,1) (3,3) (1,3) (1,2) (0,2) -> 8 vertices, 8 edges, 1 polygon.
    u = a2.join(A(), B())
    assert len(u) == 1
    pwh = u.polygons_with_holes()[0]
    assert len(pwh.outer) == 8 and not pwh.holes
    assert pwh.outer.orientation() == 1
    # |A| + |B| - |A n B| = 4 + 4 - 1
    assert total_area(u) == 7


def test_intersection_overlapping_squares():
    # A n B = [1,2]^2: a 4-edge square of area 1.
    i = a2.intersection(A(), B())
    assert len(i) == 1
    assert len(i.polygons_with_holes()[0].outer) == 4
    assert total_area(i) == 1


def test_difference_overlapping_squares():
    # A - B is the L (0,0) (2,0) (2,1) (1,1) (1,2) (0,2): 6 vertices, 6 edges, area 3.
    d = a2.difference(A(), B())
    assert len(d) == 1
    assert len(d.polygons_with_holes()[0].outer) == 6
    assert total_area(d) == 3
    # and it is oriented counterclockwise, ready to be re-inserted
    assert d.polygons_with_holes()[0].outer.orientation() == 1


def test_symmetric_difference_overlapping_squares():
    # A ^ B is the union octagon minus the [1,2]^2 overlap.  The two L pieces touch at
    # the two corners (1,2) and (2,1), so CGAL reports ONE relatively simple
    # polygon-with-holes: the 8-curve octagon with the 4-curve square hole
    # (measured and explained in STAGE2_NOTES.md), not two polygons.
    x = a2.symmetric_difference(A(), B())
    assert len(x) == 1
    pwh = x.polygons_with_holes()[0]
    assert len(pwh.outer) == 8
    assert [len(h) for h in pwh.holes] == [4]
    assert pwh.outer.orientation() == 1 and pwh.holes[0].orientation() == -1
    # 7 (union) - 1 (overlap) = 6
    assert total_area(x) == 6


def test_boolean_operations_on_disjoint_squares():
    a, d = A(), pset(sq(5, 5, 7, 7))
    assert len(a2.join(a, d)) == 2                      # two separate components
    assert a2.intersection(a, d).is_empty
    assert len(a2.difference(a, d)) == 1 and total_area(a2.difference(a, d)) == 4
    assert len(a2.symmetric_difference(a, d)) == 2
    assert not a2.do_intersect(a, d)


def test_boolean_operations_on_touching_squares():
    # A = [0,2]^2 and T = [2,4]x[0,2] share the edge x = 2.  Their union is the
    # rectangle [0,4]x[0,2], but the two seam vertices (2,0) and (2,2) survive, so the
    # bottom and the top side are each split in two: 4 + 2 = 6 boundary curves.
    a, t = A(), pset(sq(2, 0, 4, 2))
    u = a2.join(a, t)
    assert len(u) == 1
    assert len(u.polygons_with_holes()[0].outer) == 6
    assert total_area(u) == 8
    # a shared edge has zero area, so the intersection is empty and the sets do not
    # "intersect" in the set-theoretic sense CGAL uses
    assert a2.intersection(a, t).is_empty
    assert not a2.do_intersect(a, t)


def test_nested_squares_difference_makes_a_hole():
    # O = [0,4]^2 minus I = [1,3]^2 -> one polygon with one hole; the hole comes back
    # clockwise, as CGAL requires.
    n = a2.difference(pset(sq(0, 0, 4, 4)), pset(sq(1, 1, 3, 3)))
    assert len(n) == 1
    pwh = n.polygons_with_holes()[0]
    assert len(pwh.outer) == 4 and len(pwh.holes) == 1 and len(pwh.holes[0]) == 4
    assert pwh.outer.orientation() == 1
    assert pwh.holes[0].orientation() == -1
    assert total_area(n) == 16 - 4


def test_nested_intersection_is_the_inner_square():
    inner = a2.intersection(pset(sq(0, 0, 4, 4)), pset(sq(1, 1, 3, 3)))
    assert len(inner) == 1 and total_area(inner) == 4
    # the outer square is unchanged by a union with something it already contains
    outer = a2.join(pset(sq(0, 0, 4, 4)), pset(sq(1, 1, 3, 3)))
    assert len(outer) == 1 and total_area(outer) == 16


def test_member_operator_module_and_inplace_agree():
    expected = {
        "join": (1, Fraction(7)),
        "intersection": (1, Fraction(1)),
        "difference": (1, Fraction(3)),
        "symmetric_difference": (1, Fraction(6)),
    }
    ops = {"join": lambda a, b: a | b,
           "intersection": lambda a, b: a & b,
           "difference": lambda a, b: a - b,
           "symmetric_difference": lambda a, b: a ^ b}
    def _ior(a, b):
        a |= b
        return a

    def _iand(a, b):
        a &= b
        return a

    def _isub(a, b):
        a -= b
        return a

    def _ixor(a, b):
        a ^= b
        return a

    inplace = {"join": _ior, "intersection": _iand,
               "difference": _isub, "symmetric_difference": _ixor}

    for name, (count, area) in expected.items():
        module = getattr(a2, name)(A(), B())
        member = getattr(A(), name)(B())               # in place, returns self
        operator = ops[name](A(), B())
        in_place = inplace[name](A(), B())
        for result in (module, member, operator, in_place):
            assert len(result) == count, name
            assert total_area(result) == area, name


def test_member_operations_return_self_and_mutate():
    s = A()
    assert s.join(B()) is s
    assert total_area(s) == 7
    s2 = A()
    assert s2.intersection(B()) is s2 and total_area(s2) == 1
    s3 = A()
    assert s3.difference(B()) is s3 and total_area(s3) == 3
    s4 = A()
    assert s4.symmetric_difference(B()) is s4 and total_area(s4) == 6
    s5 = A()
    assert s5.complement() is s5 and s5.polygons_with_holes()[0].is_unbounded


def test_operators_do_not_mutate_their_operands():
    a, b = A(), B()
    for result in (a | b, a & b, a - b, a ^ b, ~a):
        assert isinstance(result, a2.PolygonSet)
    assert total_area(a) == 4 and total_area(b) == 4


def test_operators_accept_polygons_on_either_side():
    a = A()
    poly = sq(1, 1, 3, 3)
    assert total_area(a | poly) == 7
    assert total_area(poly | a) == 7                 # __ror__
    assert total_area(a - poly) == 3
    assert total_area(poly - a) == 3                 # __rsub__: B - A is symmetric here
    assert total_area([(0, 0), (1, 0), (1, 1), (0, 1)] | a) == 4   # a raw ring, inside A
    assert a.__or__(5) is NotImplemented
    with pytest.raises(TypeError):
        a | 5


def test_self_operations_are_idempotent_or_empty():
    a = A()
    assert total_area(a.copy().join(a)) == 4          # A | A == A
    assert total_area(a.copy().intersection(a)) == 4  # A & A == A
    assert a.copy().difference(a).is_empty            # A - A == {}
    assert a.copy().symmetric_difference(a).is_empty  # A ^ A == {}
    # the same set object on both sides (the alias case CGAL cannot handle itself)
    s = A()
    assert s.symmetric_difference(s).is_empty
    s2 = A()
    assert total_area(s2.join(s2)) == 4
    assert a.do_intersect(a)
    assert a.oriented_side(a) == 1


def test_module_level_functions_accept_raw_polygons():
    # the kind is taken from the first argument
    u = a2.join(sq(0, 0, 2, 2), sq(1, 1, 3, 3))
    assert isinstance(u, a2.PolygonSet) and u.kind == a2.Kind.SEGMENT
    assert total_area(u) == 7
    assert total_area(a2.intersection(sq(0, 0, 2, 2), sq(1, 1, 3, 3))) == 1
    assert total_area(a2.difference(sq(0, 0, 2, 2), sq(1, 1, 3, 3))) == 3
    assert total_area(a2.symmetric_difference(sq(0, 0, 2, 2), sq(1, 1, 3, 3))) == 6
    assert a2.do_intersect(sq(0, 0, 2, 2), sq(1, 1, 3, 3))
    assert a2.oriented_side(sq(0, 0, 2, 2), (1, 1)) == 1
    assert a2.oriented_side(sq(0, 0, 2, 2), (9, 9)) == -1


# ===========================================================================
# 5. complement / is_plane
# ===========================================================================
def test_complement_of_a_square():
    c = a2.complement(A())
    assert len(c) == 1
    pwh = c.polygons_with_holes()[0]
    assert pwh.is_unbounded and pwh.outer is None
    # the complement of the square is the plane with the square as a (clockwise) hole
    assert [len(h) for h in pwh.holes] == [4]
    assert pwh.holes[0].orientation() == -1
    assert not c.is_plane and not c.is_empty
    assert c.oriented_side((9, 9)) == 1
    assert c.oriented_side((1, 1)) == -1
    assert c.oriented_side((0, 1)) == 0            # the boundary belongs to neither


def test_complement_is_an_involution():
    back = a2.complement(a2.complement(A()))
    assert len(back) == 1
    assert total_area(back) == 4                   # ~~A == A
    assert total_area(~~A()) == 4                  # the operator agrees
    assert (A() | ~A()).is_plane                   # A u ~A is the whole plane


def test_complement_of_empty_is_the_plane():
    empty = a2.PolygonSet("segment")
    plane = a2.complement(empty)
    assert plane.is_plane and not plane.is_empty
    assert len(plane) == 1
    pwh = plane.polygons_with_holes()[0]
    assert pwh.is_unbounded and not pwh.holes     # no boundary at all
    assert a2.complement(plane).is_empty


def test_intersection_with_complement_is_empty():
    a = A()
    assert (a & ~a).is_empty                       # A n ~A == {}
    assert (a - a).is_empty


# ===========================================================================
# 6. oriented_side / locate / do_intersect
# ===========================================================================
def test_oriented_side_of_a_point():
    a = A()
    assert a.oriented_side((1, 1)) == 1            # interior
    assert a.oriented_side((1, 0)) == 0            # edge
    assert a.oriented_side((0, 0)) == 0            # vertex
    assert a.oriented_side((3, 3)) == -1           # exterior


def test_oriented_side_of_a_set():
    a = A()
    assert a.oriented_side(B()) == 1                       # overlapping -> +1
    assert a.oriented_side(pset(sq(2, 0, 4, 2))) == 0      # touching only -> 0
    assert a.oriented_side(pset(sq(5, 5, 7, 7))) == -1     # disjoint -> -1
    # a bare Polygon is accepted too
    assert a.oriented_side(sq(1, 1, 3, 3)) == 1


def test_locate_returns_the_containing_polygon():
    n = a2.difference(pset(sq(0, 0, 4, 4)), pset(sq(1, 1, 3, 3)))
    found = n.locate((0.5, 0.5))
    assert found is not None
    assert len(found.outer) == 4 and len(found.holes) == 1
    assert found.outer.area() == 16
    assert n.locate((2, 2)) is None                # inside the hole
    assert n.locate((9, 9)) is None                # outside everything


def test_locate_picks_the_right_component():
    two = pset(sq(0, 0, 2, 2), sq(5, 5, 7, 7))
    assert two.locate((1, 1)).outer.area() == 4
    assert two.locate((6, 6)).outer.area() == 4
    assert two.locate((3, 3)) is None
    # the two components have different bounding boxes
    assert two.locate((1, 1)).bbox() == (0.0, 0.0, 2.0, 2.0)
    assert two.locate((6, 6)).bbox() == (5.0, 5.0, 7.0, 7.0)


def test_queries_on_the_empty_set_and_on_the_plane():
    empty = a2.PolygonSet("segment")
    assert empty.oriented_side((0, 0)) == -1        # nothing is inside
    assert empty.locate((0, 0)) is None
    assert not empty.do_intersect(A())

    plane = a2.complement(empty)
    assert plane.oriented_side((0, 0)) == 1         # everything is inside
    found = plane.locate((0, 0))
    assert found is not None and found.is_unbounded and not found.holes
    assert plane.do_intersect(A())
    assert not plane.do_intersect(empty)            # nothing to meet


def test_do_intersect():
    a = A()
    assert a.do_intersect(B())                              # overlapping
    assert not a.do_intersect(pset(sq(5, 5, 7, 7)))         # disjoint
    assert not a.do_intersect(pset(sq(2, 0, 4, 2)))         # edge-touching only
    assert a.do_intersect(sq(1, 1, 3, 3))                   # raw Polygon accepted
    assert a2.do_intersect(A(), B())


# ===========================================================================
# 7. polygons_with_holes() round trip
# ===========================================================================
def test_polygons_with_holes_round_trip():
    x = a2.symmetric_difference(A(), B())          # octagon with a square hole
    pwhs = x.polygons_with_holes()
    again = a2.PolygonSet("segment")
    for pwh in pwhs:
        again.insert(pwh)                          # the output is valid CGAL input
    assert len(again) == len(x) == 1
    assert total_area(again) == total_area(x) == 6
    back = again.polygons_with_holes()[0]
    assert len(back.outer) == 8 and [len(h) for h in back.holes] == [4]


def test_polygons_with_holes_iteration_matches():
    two = pset(sq(0, 0, 2, 2), sq(5, 5, 7, 7))
    assert len(list(two)) == 2 == two.number_of_polygons_with_holes == len(two)
    assert areas(two) == [4, 4]
    assert all(isinstance(p, a2.PolygonWithHoles) for p in two)


def test_output_polygon_curves_chain_and_are_x_monotone():
    # what polygons_with_holes() hands out is a proper directed, closed chain of
    # x-monotone curves (the invariant the whole PolygonGeom exchange format rests on)
    pwh = a2.join(A(), B()).polygons_with_holes()[0]
    curves = pwh.outer.curves
    assert len(curves) == 8
    assert all(c.is_x_monotone for c in curves)
    for i, c in enumerate(curves):
        assert c.target.xy == curves[(i + 1) % len(curves)].source.xy
    assert pwh.outer.is_closed


def test_to_arrangement_result_is_independent_of_later_operations():
    # CGAL's binary Boolean operations delete and replace the set's internal
    # arrangement (boolean_set_operations gotcha 3); to_arrangement() therefore builds a
    # SEPARATE arrangement, whose handles survive any later operation on the set.
    s = A()
    arr, contained = s.to_arrangement()
    face = contained[0]
    s.join(sq(5, 5, 7, 7))                          # replaces the internal arrangement
    assert len(s) == 2
    assert arr.number_of_edges == 4                 # the exported copy is untouched
    assert face.is_valid
    assert face.polygon().outer.area() == 4


def test_join_can_fill_a_hole():
    ring = a2.PolygonSet("segment")
    ring.insert(a2.PolygonWithHoles(sq(0, 0, 4, 4), [sq(1, 1, 3, 3).reverse()]))
    assert total_area(ring) == 12
    filled = a2.join(ring, sq(1, 1, 3, 3))
    assert len(filled) == 1
    pwh = filled.polygons_with_holes()[0]
    assert len(pwh.outer) == 4 and not pwh.holes    # the hole is gone
    assert total_area(filled) == 16


def test_operations_keep_the_set_valid():
    for result in (a2.join(A(), B()), a2.intersection(A(), B()), a2.difference(A(), B()),
                   a2.symmetric_difference(A(), B()), a2.complement(A()),
                   a2.join(A(), pset(sq(5, 5, 7, 7)))):
        assert result.is_valid()


def test_copy_is_independent():
    a = A()
    c = a.copy()
    c.join(B())
    assert total_area(c) == 7 and total_area(a) == 4
    import copy as _copy
    assert total_area(_copy.deepcopy(a)) == 4
    assert total_area(_copy.copy(a)) == 4


def test_clear():
    a = A()
    a.clear()
    assert a.is_empty and len(a) == 0 and not a.is_plane
    assert a.polygons_with_holes() == []


# ===========================================================================
# 8. to_arrangement()
# ===========================================================================
def test_to_arrangement_of_a_square():
    arr, contained = A().to_arrangement()
    assert isinstance(arr, a2.Arrangement) and arr.kind == a2.Kind.SEGMENT
    # the square: 4 vertices, 4 edges, 2 faces (inside + unbounded); V - E + F = 2
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == (4, 4, 2)
    assert arr.number_of_curves == 4               # inserted with history
    assert arr.is_valid()
    assert len(contained) == 1
    face = contained[0]
    assert not face.is_unbounded
    assert len(face.outer_ccb()) == 4
    assert face == arr.locate((1, 1))              # it is the face containing (1,1)
    assert all(f.arrangement is arr for f in contained)


def test_to_arrangement_of_a_union():
    arr, contained = a2.join(A(), B()).to_arrangement()
    # the union octagon: 8 vertices, 8 edges, 2 faces
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == (8, 8, 2)
    assert arr.is_valid()
    assert len(contained) == 1
    assert contained[0].polygon().outer.area() == 7


def test_to_arrangement_of_a_symmetric_difference_has_two_contained_faces():
    # The octagon + hole boundary has 8 + 4 = 12 curves; the hole's corners (1,2) and
    # (2,1) lie ON the octagon, so 10 distinct vertices (8 + 4 - 2 shared).
    # V - E + F = 10 - 12 + F = 2 -> F = 4: the unbounded face, the [1,2]^2 hole and the
    # TWO L-shaped pieces the two touching corners separate.
    arr, contained = a2.symmetric_difference(A(), B()).to_arrangement()
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == (10, 12, 4)
    assert arr.is_valid()
    assert len(contained) == 2
    assert all(not f.is_unbounded for f in contained)
    # both L pieces have area 3: 4 - 1 each
    assert sorted(f.polygon().outer.area() for f in contained) == [3, 3]


def test_to_arrangement_of_a_polygon_with_a_hole():
    n = a2.difference(pset(sq(0, 0, 4, 4)), pset(sq(1, 1, 3, 3)))
    arr, contained = n.to_arrangement()
    # 4 + 4 vertices, 4 + 4 edges, 3 faces (unbounded, ring, hole); 8 - 8 + 3 = 3 = 1 + C
    # with C = 2 connected components, as Euler's generalised formula predicts.
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == (8, 8, 3)
    assert len(contained) == 1
    face = contained[0]
    assert face.number_of_outer_ccbs == 1 and face.number_of_inner_ccbs == 1
    assert len(face.outer_ccb()) == 4 and len(face.inner_ccbs()[0]) == 4
    assert face == arr.locate((0.5, 0.5))


def test_to_arrangement_of_a_complement_contains_the_unbounded_face():
    arr, contained = a2.complement(A()).to_arrangement()
    # only the square's 4 curves are inserted; the set is everything outside it
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == (4, 4, 2)
    assert len(contained) == 1
    assert contained[0].is_unbounded
    assert contained[0] == arr.unbounded_face


def test_to_arrangement_of_empty_and_plane():
    arr, contained = a2.PolygonSet("segment").to_arrangement()
    assert (arr.number_of_edges, arr.number_of_faces) == (0, 1)
    assert contained == []                          # nothing belongs to the empty set

    arr2, contained2 = a2.complement(a2.PolygonSet("segment")).to_arrangement()
    assert (arr2.number_of_edges, arr2.number_of_faces) == (0, 1)
    assert len(contained2) == 1 and contained2[0].is_unbounded


# ===========================================================================
# 9. is_valid_polygon
# ===========================================================================
def test_is_valid_polygon_orientation_rules():
    assert a2.is_valid_polygon(sq(0, 0, 2, 2))                 # CCW outer: valid
    assert not a2.is_valid_polygon(sq(0, 0, 2, 2).reverse())   # CW outer: invalid


def test_is_valid_polygon_self_intersecting():
    assert not a2.is_valid_polygon(a2.Polygon([(0, 0), (2, 2), (2, 0), (0, 2)]))


def test_is_valid_polygon_with_holes():
    outer = sq(0, 0, 4, 4)
    assert a2.is_valid_polygon(a2.PolygonWithHoles(outer, [sq(1, 1, 3, 3).reverse()]))
    # a counterclockwise hole is rejected
    assert not a2.is_valid_polygon(a2.PolygonWithHoles(outer, [sq(1, 1, 3, 3)]))
    # a hole that is not inside the outer boundary is rejected
    assert not a2.is_valid_polygon(a2.PolygonWithHoles(outer, [sq(5, 5, 6, 6).reverse()]))


def test_is_valid_polygon_accepts_raw_input_and_an_explicit_kind():
    assert a2.is_valid_polygon([(0, 0), (2, 0), (2, 2), (0, 2)])
    assert a2.is_valid_polygon(sq(0, 0, 2, 2), kind="segment")
    # a kind without Boolean set operations cannot answer the question
    with pytest.raises(a2.UnsupportedError):
        a2.is_valid_polygon(sq(0, 0, 2, 2), kind="polyline")


# ===========================================================================
# 10. Arrangement bridge: Face.polygon() -> PolygonSet -> Arrangement
# ===========================================================================
def test_face_polygon_feeds_a_polygon_set(square_arr):
    # conftest fixture: the [0,4]^2 square cut by the chord y = 2 (V=8, E=9, F=3).
    assert (square_arr.number_of_vertices, square_arr.number_of_edges,
            square_arr.number_of_faces) == (8, 9, 3)
    bounded = [f for f in square_arr.faces() if not f.is_unbounded]
    assert len(bounded) == 2                        # the two 4 x 2 halves

    s = a2.PolygonSet("segment")
    for face in bounded:
        pwh = face.polygon()
        assert pwh.outer.orientation() == 1         # Face.polygon() is CCW by contract
        assert pwh.outer.area() == 8                # 4 x 2
        assert not pwh.holes
        s.join(pwh)                                 # join: the halves share the chord

    assert len(s) == 1
    # the union is the whole [0,4]^2 square, but the chord endpoints (0,2) and (4,2)
    # survive as vertices, so its boundary has 4 + 2 = 6 curves
    assert len(s.polygons_with_holes()[0].outer) == 6
    assert total_area(s) == 16


def test_face_polygon_round_trip_back_to_an_arrangement(square_arr):
    bounded = [f for f in square_arr.faces() if not f.is_unbounded]
    s = a2.PolygonSet("segment")
    for face in bounded:
        s.join(face.polygon())
    arr, contained = s.to_arrangement()
    # 6 boundary curves -> 6 vertices, 6 edges, 2 faces (6 - 6 + 2 = 2)
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == (6, 6, 2)
    assert arr.is_valid()
    assert len(contained) == 1
    assert contained[0].polygon().outer.area() == 16


def test_face_with_a_hole_polygon_round_trip():
    # [0,4]^2 with the [1,3]^2 square inside it: the ring face has one inner CCB.
    arr = a2.Arrangement("segment")
    arr.insert([a2.Segment((0, 0), (4, 0)), a2.Segment((4, 0), (4, 4)),
                a2.Segment((4, 4), (0, 4)), a2.Segment((0, 4), (0, 0)),
                a2.Segment((1, 1), (3, 1)), a2.Segment((3, 1), (3, 3)),
                a2.Segment((3, 3), (1, 3)), a2.Segment((1, 3), (1, 1))])
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == (8, 8, 3)
    ring = [f for f in arr.faces()
            if not f.is_unbounded and f.number_of_inner_ccbs == 1]
    assert len(ring) == 1
    pwh = ring[0].polygon()
    assert len(pwh.outer) == 4 and len(pwh.holes) == 1
    assert pwh.outer.orientation() == 1 and pwh.holes[0].orientation() == -1
    assert a2.is_valid_polygon(pwh)

    s = a2.PolygonSet("segment")
    s.insert(pwh)
    assert total_area(s) == 12                      # 16 - 4
    assert s.oriented_side((2, 2)) == -1            # the hole is outside the set
    arr2, contained = s.to_arrangement()
    assert (arr2.number_of_vertices, arr2.number_of_edges, arr2.number_of_faces) == (8, 8, 3)
    assert len(contained) == 1
    assert contained[0].number_of_inner_ccbs == 1


def test_unbounded_face_has_no_polygon(square_arr):
    with pytest.raises(a2.UnsupportedError):
        square_arr.unbounded_face.polygon()


def test_polygon_set_to_arrangement_faces_have_polygons():
    s = A()
    arr, contained = s.to_arrangement()
    pwh = contained[0].polygon()
    # the face polygon and the set's own polygon describe the same square
    assert pwh.outer.area() == s.polygons_with_holes()[0].outer.area() == 4
    assert a2.is_valid_polygon(pwh)


# ===========================================================================
# 11. kinds and errors
# ===========================================================================
def test_polygon_set_unsupported_kinds():
    # CGAL has Boolean set operations only for segment / circle_segment / conic / bezier
    for kind in ("linear", "polyline", "sphere"):
        with pytest.raises(a2.UnsupportedError):
            a2.PolygonSet(kind)


def test_polygon_set_supported_kinds_construct():
    for kind in ("segment", "circle_segment", "conic", "bezier"):
        s = a2.PolygonSet(kind)
        assert s.is_empty and s.kind == a2.Kind[kind.upper()]


def test_kind_mismatch_between_sets():
    seg = a2.PolygonSet("segment")
    circ = a2.PolygonSet("circle_segment")
    with pytest.raises(a2.KindMismatchError):
        seg.join(circ)
    with pytest.raises(a2.KindMismatchError):
        seg.do_intersect(circ)
    with pytest.raises(a2.KindMismatchError):
        seg.intersection(circ)


# ===========================================================================
# 12. circle_segment kind (a shorter run of the same battery)
# ===========================================================================
def test_circle_polygon_construction():
    p = circle_polygon(0, 0, 2)
    # a full circle is cut at its vertical tangency points (-2,0) and (2,0): 2 arcs
    assert len(p) == 2
    assert p.is_closed and p.kind == a2.Kind.CIRCLE_SEGMENT
    assert p.orientation() == 1
    assert p.reverse().orientation() == -1
    assert a2.is_valid_polygon(p)
    assert p.bbox() == pytest.approx((-2.0, -2.0, 2.0, 2.0))
    # area() falls back to the 1e-3 approximation for non-segment kinds
    assert p.area() == pytest.approx(math.pi * 4, rel=1e-3)


def test_circle_polygon_set_insert_and_queries():
    s = a2.PolygonSet("circle_segment")
    s.insert(circle_polygon(0, 0, 2))
    assert len(s) == 1 and s.is_valid()
    assert s.arrangement_size == (2, 2)             # 2 arcs -> 2 edges, 2 faces
    assert s.oriented_side((0, 0)) == 1             # the centre is inside
    assert s.oriented_side((2, 0)) == 0             # on the circle
    assert s.oriented_side((9, 9)) == -1
    assert s.locate((0, 0)) is not None
    assert s.locate((9, 9)) is None


def test_circle_segment_boolean_operations():
    # two circles of radius 2 centred at (0,0) and (2,0); they cross at (1, +-sqrt 3).
    a = pset(circle_polygon(0, 0, 2), kind="circle_segment")
    b = pset(circle_polygon(2, 0, 2), kind="circle_segment")
    # union: the outer arc of each circle, each cut at its own vertical tangency point
    # ((-2,0) for the left circle, (4,0) for the right one) -> 2 + 2 = 4 curves
    u = a2.join(a, b)
    assert len(u) == 1 and len(u.polygons_with_holes()[0].outer) == 4
    # intersection (the lens): the right arc of the left circle (through (2,0)) and the
    # left arc of the right circle (through (0,0)), each split at that tangency point
    i = a2.intersection(a, b)
    assert len(i) == 1 and len(i.polygons_with_holes()[0].outer) == 4
    # difference (a crescent): 2 arcs of each circle again
    d = a2.difference(a, b)
    assert len(d) == 1 and len(d.polygons_with_holes()[0].outer) == 4
    assert a2.do_intersect(a, b)
    assert a.oriented_side(b) == 1


def test_circle_segment_complement_and_to_arrangement():
    s = pset(circle_polygon(0, 0, 2), kind="circle_segment")
    c = a2.complement(s)
    assert len(c) == 1
    pwh = c.polygons_with_holes()[0]
    assert pwh.is_unbounded and [len(h) for h in pwh.holes] == [2]
    assert c.oriented_side((9, 9)) == 1 and c.oriented_side((0, 0)) == -1

    arr, contained = s.to_arrangement()
    # the two arcs meet at (-2,0) and (2,0): 2 vertices, 2 edges, 2 faces
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == (2, 2, 2)
    assert arr.is_valid()
    assert len(contained) == 1 and not contained[0].is_unbounded


def test_circle_segment_polygon_from_straight_curves():
    # a square whose sides are straight circle-segment curves
    ring = [a2.CircleSegment.segment((0, 0), (2, 0)), a2.CircleSegment.segment((2, 0), (2, 2)),
            a2.CircleSegment.segment((2, 2), (0, 2)), a2.CircleSegment.segment((0, 2), (0, 0))]
    p = a2.Polygon(ring)
    assert len(p) == 4 and p.is_closed
    assert p.orientation() == 1 and a2.is_valid_polygon(p)
    s = a2.PolygonSet("circle_segment")
    s.insert(p)
    assert len(s) == 1
    assert s.oriented_side((1, 1)) == 1


def test_circle_segment_polygon_from_points():
    # DESIGN.md 3: `Polygon(points, kind=...)` turns points into straight boundary
    # curves for any kind; for circle_segment the straight curve comes from the kind's
    # own segment constructor (Arr_circle_segment_traits_2 has no
    # Construct_x_monotone_curve_2 -- CGAL_TRAPS_CHECKLIST, circle-segment section).
    p = a2.Polygon([(0, 0), (2, 0), (2, 2), (0, 2)], kind="circle_segment")
    assert len(p) == 4 and p.is_closed
    assert p.orientation() == 1
    s = a2.PolygonSet("circle_segment")
    s.insert(p)
    assert len(s) == 1
    assert s.oriented_side((1, 1)) == 1


def test_bezier_polygon_from_points_is_unsupported():
    # documented: the Bezier traits has no straight x-monotone curve constructor
    with pytest.raises(a2.UnsupportedError):
        a2.Polygon([(0, 0), (2, 0), (2, 2), (0, 2)], kind="bezier")
