"""Tests for arrangement_2d.cleanup (tolerance-based snapping before the exact arrangement)."""
import pytest

a2 = pytest.importorskip("arrangement_2d")
from arrangement_2d import cleanup, regions  # noqa: E402


def square(x0=0.0, y0=0.0, s=4.0):
    return [((x0, y0), (x0 + s, y0)), ((x0 + s, y0), (x0 + s, y0 + s)),
            ((x0 + s, y0 + s), (x0, y0 + s)), ((x0, y0 + s), (x0, y0))]


def bounded_faces(arr):
    return [f for f in arr.faces() if not f.is_unbounded]


# ---------------------------------------------------------------- near_miss_report

def test_report_counts_exact_and_near_misses():
    segs = square() + [((1.0, 1e-7), (1.0, 2.0))]          # T-junction missing the bottom edge by 1e-7
    segs += [((2.0, 2.0), (3.0, 2.0)), ((3.0 + 1e-8, 2.0), (3.0, 3.0))]   # endpoint gap 1e-8
    rep = cleanup.near_miss_report(segs, tolerances=(1e-9, 1e-6, 1e-3))
    assert rep.n_segments == 7 and rep.n_zero_length == 0 and rep.n_exact_duplicates == 0
    assert rep.n_endpoints == 14
    # the four square corners coincide exactly with another endpoint (8 endpoints)
    assert rep.n_exact_endpoint_coincidences == 8
    assert rep.endpoint_gaps[1e-9] == 0 and rep.endpoint_gaps[1e-6] == 2    # both ends of the 1e-8 gap
    assert rep.t_junction_gaps[1e-9] == 0 and rep.t_junction_gaps[1e-6] >= 1
    assert "near misses" in str(rep)


def test_report_flags_duplicates_and_zero_length():
    segs = square() + [square()[0], ((1.0, 1.0), (1.0, 1.0))]
    rep = cleanup.near_miss_report(segs)
    assert rep.n_exact_duplicates == 1 and rep.n_zero_length == 1


# ---------------------------------------------------------------- snap_segments

def test_snap_closes_endpoint_gap():
    # a square whose last corner misses the first by 1e-9: exact arithmetic sees an open chain
    segs = square()[:3] + [((0.0, 4.0), (0.0, 1e-9))]
    raw = a2.Arrangement("segment"); raw.insert([a2.Segment(p, q) for p, q in segs])
    assert len(bounded_faces(raw)) == 0
    res = cleanup.snap_segments(segs, tolerance=1e-6)
    assert res.endpoints_merged >= 1 and res.iterations >= 1
    arr = a2.Arrangement("segment"); arr.insert([a2.Segment(p, q) for p, q in res.segments])
    assert len(bounded_faces(arr)) == 1
    assert regions.face_area(bounded_faces(arr)[0]) == 16


def test_snap_closes_t_junction_and_splits_the_edge():
    # a square with a chord whose lower end misses the bottom edge by 1e-9
    segs = square() + [((2.0, 1e-9), (2.0, 4.0))]
    raw = a2.Arrangement("segment"); raw.insert([a2.Segment(p, q) for p, q in segs])
    assert len(bounded_faces(raw)) == 1                        # chord does not close: one face
    res = cleanup.snap_segments(segs, tolerance=1e-6)
    assert res.t_junctions_snapped == 1
    assert len(res.segments) == 6                              # bottom edge split at (2, 1e-9)
    arr = a2.Arrangement("segment"); arr.insert([a2.Segment(p, q) for p, q in res.segments])
    assert len(bounded_faces(arr)) == 2
    assert sorted(float(regions.face_area(f)) for f in bounded_faces(arr)) == pytest.approx([8.0, 8.0], abs=1e-6)


def test_snap_removes_duplicates_and_degenerate():
    segs = square() + [square()[0], ((1.0, 1.0), (1.0, 1.0)), ((4.0, 0.0), (0.0, 0.0))]   # duplicate, degenerate, reversed duplicate
    res = cleanup.snap_segments(segs, tolerance=1e-6)
    assert res.removed_degenerate == 1 and res.removed_duplicates == 2
    assert len(res.segments) == 4


def test_snap_is_idempotent_and_does_not_touch_clean_input():
    segs = square() + [((0.0, 0.0), (4.0, 4.0))]
    res = cleanup.snap_segments(segs, tolerance=1e-6)
    assert res.iterations == 1 and res.endpoints_merged == 0 and res.t_junctions_snapped == 0
    assert sorted(res.segments) == sorted(segs)


def test_snap_makes_near_overlaps_exact_so_the_arrangement_merges_them():
    # two collinear segments that overlap, one shifted by 1e-9 in y -> exact arithmetic: two crossing-free
    # distinct edges; after snapping the endpoints onto the other segment they overlap exactly and the
    # arrangement stores the overlap as ONE edge with two originating curves.
    segs = [((0.0, 0.0), (4.0, 0.0)), ((2.0, 1e-9), (6.0, 1e-9))]
    res = cleanup.snap_segments(segs, tolerance=1e-6)
    arr = a2.Arrangement("segment"); arr.insert([a2.Segment(p, q) for p, q in res.segments])
    assert arr.number_of_vertices == 4 and arr.number_of_edges == 3


def test_snap_rejects_bad_tolerance():
    with pytest.raises(ValueError):
        cleanup.snap_segments(square(), tolerance=0)


# ---------------------------------------------------------------- dangling edges / clean_arrangement

def test_remove_dangling_edges_iterates_to_the_root():
    # square + an antenna of two segments hanging inside + an isolated segment outside
    segs = square() + [((1.0, 1.0), (2.0, 2.0)), ((2.0, 2.0), (3.0, 3.0)), ((10.0, 10.0), (12.0, 12.0))]
    arr = a2.Arrangement("segment"); arr.insert([a2.Segment(p, q) for p, q in segs])
    assert arr.number_of_edges == 7
    n = cleanup.remove_dangling_edges(arr)
    assert n == 3
    assert arr.number_of_edges == 4 and arr.number_of_vertices == 4 and arr.number_of_faces == 2
    assert all(he.face != he.twin.face for he in arr.edges())
    assert arr.is_valid()


def test_clean_arrangement_end_to_end():
    segs = square() + [((2.0, 1e-9), (2.0, 4.0 + 1e-9)), ((5.0, 5.0), (6.0, 6.0))]
    arr = cleanup.clean_arrangement(segs, tolerance=1e-6)
    assert arr.kind == a2.Kind.SEGMENT
    assert len(bounded_faces(arr)) == 2 and arr.number_of_edges == 7
    assert regions.number_of_connected_components(arr) == 1
    arr2 = cleanup.clean_arrangement(segs, tolerance=1e-6, remove_dangling=False)
    assert arr2.number_of_edges == 8


def test_segments_from_polylines():
    segs = cleanup.segments_from_polylines([[(0, 0), (1, 0), (1, 0), (1, 1)], [(5, 5), (5, 5)]])
    assert segs == [((0.0, 0.0), (1.0, 0.0)), ((1.0, 0.0), (1.0, 1.0))]


def test_cleanup_is_lazily_importable_from_the_package():
    assert a2.cleanup is cleanup
    assert "cleanup" in dir(a2)
