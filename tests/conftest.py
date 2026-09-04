"""Shared pytest fixtures for arrangement_2d."""
import pytest

a2 = pytest.importorskip("arrangement_2d")


@pytest.fixture
def square_arr():
    """Unit square (0,0)-(4,4) plus a horizontal chord y=2 from x=-1..5: V=8 E=9 F=3."""
    arr = a2.Arrangement("segment")
    arr.insert([
        a2.Segment((0, 0), (4, 0)), a2.Segment((4, 0), (4, 4)),
        a2.Segment((4, 4), (0, 4)), a2.Segment((0, 4), (0, 0)),
        a2.Segment((-1, 2), (5, 2)),
    ])
    return arr


ALL_KINDS = ["segment", "linear", "circle_segment", "polyline", "bezier", "conic", "sphere"]


@pytest.fixture(params=ALL_KINDS)
def kind(request):
    return request.param
