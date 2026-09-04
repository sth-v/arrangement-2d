// arr2d — C++ test for the type-erased 2D Boolean set operations
// (impl/polygon_set_impl.hpp + src/bso_<kind>.cpp).
//
// Build (one TU per kind compiles in 30-120 s at -O0):
//
//   clang++ -std=c++17 -O0 -g -DCGAL_USE_CORE -DCGAL_USE_GMP -DCGAL_USE_MPFR \
//           -I/opt/homebrew/include -Isrc/arr2d/include \
//           src/arr2d/tests/test_bso.cpp \
//           src/arr2d/src/{registry,numbers,overlay}.cpp \
//           src/arr2d/src/bso_{segment,circle_segment,conic,bezier}.cpp \
//           src/arr2d/src/kind_{segment,circle_segment,conic,bezier}.cpp \
//           -L/opt/homebrew/lib -lgmp -lmpfr -o test_bso
//
// Selective builds: define ARR2D_TEST_SEGMENT / ARR2D_TEST_CIRCLE_SEGMENT /
// ARR2D_TEST_CONIC / ARR2D_TEST_BEZIER to 0 to drop a kind (each one is independent).
// Link requirements per kind:
//   segment        bso_segment.cpp only (the Boolean part needs no kind TU); kind_segment.cpp
//                  is optional and enables the to_arrangement() checks.
//   circle_segment bso_circle_segment.cpp only (same rule).
//   conic          bso_conic.cpp + kind_conic.cpp (ConicTypes::traits()).
//   bezier         bso_bezier.cpp + kind_bezier.cpp (BezierTypes::traits()).
//
// The program must exit normally (status 0) — that is also the check that no CORE-backed
// object lives in static storage (CGAL/CORE/MemoryPool.h:125 `! blocks.empty()` at exit).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "arr2d/arrangement.hpp"
#include "arr2d/bso.hpp"
#include "arr2d/common.hpp"
#include "arr2d/ops.hpp"
#include "arr2d/polygon_set.hpp"
#include "arr2d/registry.hpp"

#ifndef ARR2D_TEST_SEGMENT
#define ARR2D_TEST_SEGMENT 1
#endif
#ifndef ARR2D_TEST_CIRCLE_SEGMENT
#define ARR2D_TEST_CIRCLE_SEGMENT 1
#endif
#ifndef ARR2D_TEST_CONIC
#define ARR2D_TEST_CONIC 1
#endif
#ifndef ARR2D_TEST_BEZIER
#define ARR2D_TEST_BEZIER 1
#endif

#if ARR2D_TEST_SEGMENT
#include "arr2d/kinds/segment_types.hpp"
#include <CGAL/Polygon_2.h>
#endif
#if ARR2D_TEST_CIRCLE_SEGMENT
#include "arr2d/kinds/circle_segment_types.hpp"
#endif
#if ARR2D_TEST_CONIC
#include "arr2d/kinds/conic_types.hpp"
#endif
#if ARR2D_TEST_BEZIER
#include "arr2d/kinds/bezier_types.hpp"
#endif

using arr2d::ErrorCode;
using arr2d::Geom;
using arr2d::GeomType;
using arr2d::Kind;
using arr2d::PolygonGeom;
using arr2d::PolygonSetBase;

// ---------------------------------------------------------------------------
// tiny test harness
// ---------------------------------------------------------------------------
namespace {

int g_pass = 0;
int g_fail = 0;
int g_skip = 0;
const char* g_section = "?";

void section(const char* s) {
  g_section = s;
  std::printf("\n--- %s\n", s);
}

void check(bool ok, const std::string& what) {
  if (ok) {
    ++g_pass;
  } else {
    ++g_fail;
    std::printf("  FAIL [%s] %s\n", g_section, what.c_str());
  }
}

template <class A, class B>
void check_eq(const A& got, const B& want, const std::string& what) {
  const bool ok = (got == static_cast<A>(want));
  if (ok) {
    ++g_pass;
  } else {
    ++g_fail;
    std::printf("  FAIL [%s] %s: got %lld, want %lld\n", g_section, what.c_str(),
                static_cast<long long>(got), static_cast<long long>(want));
  }
}

void skip(const std::string& what) {
  ++g_skip;
  std::printf("  SKIP [%s] %s\n", g_section, what.c_str());
}

/// Run `fn`, expecting arr2d::Error with `code` and a message containing `needle`.
template <class F>
void expect_error(F fn, ErrorCode code, const char* needle, const std::string& what) {
  try {
    fn();
  } catch (const arr2d::Error& e) {
    const bool ok = (e.code == code) && (std::string(e.what()).find(needle) != std::string::npos);
    if (!ok)
      std::printf("  (got code=%d msg='%s')\n", static_cast<int>(e.code), e.what());
    check(ok, what);
    return;
  } catch (const std::exception& e) {
    std::printf("  (got std::exception '%s')\n", e.what());
    check(false, what);
    return;
  }
  check(false, what + " (no exception thrown)");
}

[[maybe_unused]] std::size_t ring_count(const PolygonGeom& p) { return p.outer.size(); }

/// Total number of boundary curves of a polygon (outer + holes).
[[maybe_unused]] std::size_t curve_count(const PolygonGeom& p) {
  std::size_t n = p.outer.size();
  for (const auto& h : p.holes) n += h.size();
  return n;
}

}  // namespace

// ===========================================================================
// Segment kind
// ===========================================================================
#if ARR2D_TEST_SEGMENT
namespace {

using SegTypes = arr2d::SegmentTypes;
using SegPt = SegTypes::Point_2;
using SegXcv = SegTypes::X_monotone_curve_2;

Geom seg_geom(const SegPt& p, const SegPt& q) {
  return arr2d::make_geom(Kind::Segment, GeomType::XCurve, SegXcv(p, q));
}

/// A closed chain of directed segments through `pts` (in the given order).
std::vector<Geom> seg_ring(const std::vector<std::pair<int, int>>& pts) {
  std::vector<Geom> out;
  const std::size_t n = pts.size();
  for (std::size_t i = 0; i < n; ++i) {
    const SegPt a(pts[i].first, pts[i].second);
    const SegPt b(pts[(i + 1) % n].first, pts[(i + 1) % n].second);
    out.push_back(seg_geom(a, b));
  }
  return out;
}

/// Counterclockwise axis-aligned rectangle.
std::vector<Geom> seg_square(int x0, int y0, int x1, int y1) {
  return seg_ring({{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}});
}
/// Clockwise axis-aligned rectangle.
std::vector<Geom> seg_square_cw(int x0, int y0, int x1, int y1) {
  return seg_ring({{x0, y0}, {x0, y1}, {x1, y1}, {x1, y0}});
}

PolygonGeom poly(std::vector<Geom> outer) {
  PolygonGeom p;
  p.outer = std::move(outer);
  return p;
}

/// The same ring as a CGAL::Polygon_2, to cross-check orientation().
CGAL::Polygon_2<SegTypes::Kernel> cgal_polygon(const std::vector<Geom>& ring) {
  CGAL::Polygon_2<SegTypes::Kernel> pgn;
  for (const Geom& g : ring) pgn.push_back(g.as<SegXcv>().target());
  return pgn;
}

/// Number of halfedges on the outer CCB of `f`.
std::size_t ccb_length(const arr2d::ArrBase& arr, arr2d::FH f) {
  std::vector<arr2d::HH> hes;
  arr.he_ccb(arr.face_outer_ccb(f), hes);
  return hes.size();
}

void test_segment() {
  section("segment: validation helpers");
  std::unique_ptr<PolygonSetBase> ps = arr2d::make_polygon_set_segment();
  check_eq(static_cast<int>(ps->kind()), static_cast<int>(Kind::Segment), "kind() == segment");

  const std::vector<Geom> ccw = seg_square(0, 0, 2, 2);
  const std::vector<Geom> cw = seg_square_cw(0, 0, 2, 2);
  check_eq(ps->orientation(ccw), 1, "orientation(ccw square) == +1");
  check_eq(ps->orientation(cw), -1, "orientation(cw square) == -1");
  // Agreement with CGAL::Polygon_2::orientation() for a simple polygon (the reason we can use
  // the Gps_traits_adaptor functor uniformly instead).
  check_eq(static_cast<int>(cgal_polygon(ccw).orientation()), 1, "CGAL::Polygon_2 ccw == +1");
  check_eq(static_cast<int>(cgal_polygon(cw).orientation()), -1, "CGAL::Polygon_2 cw == -1");

  check(ps->is_closed_chain(ccw), "is_closed_chain(square)");
  check(ps->is_closed_chain(std::vector<Geom>{}), "is_closed_chain(empty) == true (CGAL rule)");
  check(!ps->is_closed_chain(std::vector<Geom>{ccw[0]}), "is_closed_chain(single curve) == false");
  std::vector<Geom> broken = ccw;
  broken[2] = seg_geom(SegPt(2, 2), SegPt(9, 9));
  check(!ps->is_closed_chain(broken), "is_closed_chain(broken chain) == false");
  check_eq(ps->orientation(broken), 0, "orientation(broken chain) == 0");
  check_eq(ps->orientation(std::vector<Geom>{ccw[0]}), 0, "orientation(single curve) == 0");

  check(ps->is_valid_polygon(poly(ccw)), "is_valid_polygon(ccw square)");
  check(!ps->is_valid_polygon(poly(cw)), "is_valid_polygon(cw square) == false");
  check(!ps->is_valid_polygon(poly(broken)), "is_valid_polygon(broken chain) == false");
  {  // bow tie: closed, ccw-ish, but self-intersecting
    PolygonGeom bow = poly(seg_ring({{0, 0}, {2, 2}, {2, 0}, {0, 2}}));
    check(!ps->is_valid_polygon(bow), "is_valid_polygon(bow tie) == false");
  }
  {  // square with a clockwise hole -> valid; counterclockwise hole -> invalid
    PolygonGeom p = poly(seg_square(0, 0, 10, 10));
    p.holes.push_back(seg_square_cw(3, 3, 7, 7));
    check(ps->is_valid_polygon(p), "is_valid_polygon(square + cw hole)");
    PolygonGeom bad = poly(seg_square(0, 0, 10, 10));
    bad.holes.push_back(seg_square(3, 3, 7, 7));
    check(!ps->is_valid_polygon(bad), "is_valid_polygon(square + ccw hole) == false");
  }

  section("segment: insert / errors");
  ps->insert(poly(ccw));
  check_eq(ps->number_of_polygons_with_holes(), 1u, "npwh after insert == 1");
  check(!ps->is_empty(), "not empty after insert");
  check(!ps->is_plane(), "not the plane after insert");
  check(ps->is_valid(), "is_valid() after insert");
  expect_error([&] { arr2d::make_polygon_set_segment()->insert(poly(cw)); },
               ErrorCode::InvalidArgument, "orientation",
               "insert(cw square) -> Error(InvalidArgument, ...orientation...)");
  expect_error([&] { arr2d::make_polygon_set_segment()->insert(poly(broken)); },
               ErrorCode::InvalidArgument, "closed chain",
               "insert(broken chain) -> Error(InvalidArgument, ...closed chain...)");
  {
    PolygonGeom bad = poly(seg_square(0, 0, 10, 10));
    bad.holes.push_back(seg_square(3, 3, 7, 7));  // ccw hole
    expect_error([&] { arr2d::make_polygon_set_segment()->insert(bad); },
                 ErrorCode::InvalidArgument, "clockwise holes",
                 "insert(ccw hole) -> Error(InvalidArgument, ...clockwise holes...)");
  }
  {
    PolygonGeom bad;  // point box where a curve is required
    bad.outer.push_back(arr2d::make_geom(Kind::Segment, GeomType::Point, SegPt(0, 0)));
    bad.outer.push_back(seg_geom(SegPt(0, 0), SegPt(1, 1)));
    expect_error([&] { arr2d::make_polygon_set_segment()->is_valid_polygon(bad); },
                 ErrorCode::InvalidArgument, "curve",
                 "is_valid_polygon(point box) -> Error(InvalidArgument)");
  }

  section("segment: insert_polygons (disjoint)");
  {
    auto s = arr2d::make_polygon_set_segment();
    std::vector<PolygonGeom> two{poly(seg_square(0, 0, 1, 1)), poly(seg_square(5, 5, 6, 6))};
    s->insert_polygons(two);
    check_eq(s->number_of_polygons_with_holes(), 2u, "insert_polygons -> npwh == 2");
    check_eq(s->arrangement_number_of_edges(), 8u, "insert_polygons -> 8 edges");
    check_eq(s->arrangement_number_of_faces(), 3u, "insert_polygons -> 3 faces");
    check(s->is_valid(), "insert_polygons -> is_valid()");
  }

  section("segment: boolean operations on [0,2]^2 and [1,3]^2");
  auto make_a = [] {
    auto s = arr2d::make_polygon_set_segment();
    s->insert(poly(seg_square(0, 0, 2, 2)));
    return s;
  };
  auto make_b = [] {
    auto s = arr2d::make_polygon_set_segment();
    s->insert(poly(seg_square(1, 1, 3, 3)));
    return s;
  };
  std::vector<PolygonGeom> out;
  {  // union: the L-shaped octagon
    auto a = make_a();
    a->join(*make_b());
    check_eq(a->number_of_polygons_with_holes(), 1u, "join -> 1 polygon");
    a->polygons_with_holes(out);
    check_eq(out.size(), 1u, "join -> polygons_with_holes size 1");
    check_eq(ring_count(out[0]), 8u, "join -> outer boundary has 8 curves");
    check_eq(out[0].holes.size(), 0u, "join -> no holes");
    check(!out[0].unbounded, "join -> bounded");
    check_eq(a->orientation(out[0].outer), 1, "join -> outer boundary is ccw");
    check(a->is_valid_polygon(out[0]), "join -> the produced polygon is valid CGAL input");
    check_eq(a->arrangement_number_of_faces(), 2u, "join -> arrangement has 2 faces");
    check_eq(a->arrangement_number_of_edges(), 8u, "join -> arrangement has 8 edges");
    check(a->is_valid(), "join -> is_valid()");

    // round trip: feed the output back into a fresh set
    auto rt = arr2d::make_polygon_set_segment();
    rt->insert_polygons(out);
    check_eq(rt->number_of_polygons_with_holes(), 1u, "round trip -> 1 polygon");
    check_eq(rt->arrangement_number_of_edges(), 8u, "round trip -> 8 edges");
    check(rt->is_valid(), "round trip -> is_valid()");
    check(rt->do_intersect(*a), "round trip -> intersects the original");
  }
  {  // intersection: the [1,2]^2 square
    auto a = make_a();
    a->intersection(*make_b());
    check_eq(a->number_of_polygons_with_holes(), 1u, "intersection -> 1 polygon");
    a->polygons_with_holes(out);
    check_eq(ring_count(out[0]), 4u, "intersection -> 4 curves");
  }
  {  // difference: an L with 6 vertices
    auto a = make_a();
    a->difference(*make_b());
    check_eq(a->number_of_polygons_with_holes(), 1u, "difference -> 1 polygon");
    a->polygons_with_holes(out);
    check_eq(ring_count(out[0]), 6u, "difference -> 6 curves");
  }
  {  // symmetric difference of these two squares: ONE p-w-h (the two L pieces touch at the two
     // corners (1,2) and (2,1), so CGAL emits the octagon with the [1,2]^2 square as a hole).
    auto a = make_a();
    a->symmetric_difference(*make_b());
    check_eq(a->number_of_polygons_with_holes(), 1u, "symmetric_difference -> 1 polygon");
    a->polygons_with_holes(out);
    check_eq(ring_count(out[0]), 8u, "symmetric_difference -> outer has 8 curves");
    check_eq(out[0].holes.size(), 1u, "symmetric_difference -> 1 hole");
    check_eq(out[0].holes[0].size(), 4u, "symmetric_difference -> hole has 4 curves");
    check_eq(a->orientation(out[0].holes[0]), -1, "symmetric_difference -> hole is cw");
    check_eq(a->arrangement_number_of_faces(), 4u, "symmetric_difference -> 4 faces");
    check_eq(a->arrangement_number_of_edges(), 12u, "symmetric_difference -> 12 edges");
  }
  {  // symmetric difference of two DISJOINT squares: two polygons
    auto a = arr2d::make_polygon_set_segment();
    a->insert(poly(seg_square(0, 0, 1, 1)));
    auto b = arr2d::make_polygon_set_segment();
    b->insert(poly(seg_square(5, 5, 6, 6)));
    a->symmetric_difference(*b);
    check_eq(a->number_of_polygons_with_holes(), 2u, "symmetric_difference(disjoint) -> 2 polygons");
  }

  section("segment: complement / plane / empty");
  {
    auto a = make_a();
    a->complement();
    check(!a->is_plane(), "complement(square) is not the plane");
    check(!a->is_empty(), "complement(square) is not empty");
    check_eq(a->number_of_polygons_with_holes(), 1u, "complement -> 1 polygon");
    a->polygons_with_holes(out);
    check(out[0].unbounded, "complement -> unbounded polygon");
    check_eq(out[0].outer.size(), 0u, "complement -> empty outer boundary");
    check_eq(out[0].holes.size(), 1u, "complement -> 1 hole");
    check_eq(out[0].holes[0].size(), 4u, "complement -> hole has 4 curves");
    check_eq(a->orientation(out[0].holes[0]), -1, "complement -> hole is clockwise");
    check(a->is_valid_polygon(out[0]), "complement -> the unbounded polygon is valid CGAL input");
    check_eq(a->oriented_side(arr2d::make_geom(Kind::Segment, GeomType::Point, SegPt(9, 9))), 1,
             "complement: (9,9) is inside");
    check_eq(a->oriented_side(arr2d::make_geom(Kind::Segment, GeomType::Point, SegPt(1, 1))), -1,
             "complement: (1,1) is outside");
    a->complement();
    check_eq(a->number_of_polygons_with_holes(), 1u, "complement twice -> back to 1 polygon");
    a->polygons_with_holes(out);
    check_eq(ring_count(out[0]), 4u, "complement twice -> the original square");

    auto rt = arr2d::make_polygon_set_segment();
    auto c2 = make_a();
    c2->complement();
    c2->polygons_with_holes(out);
    rt->insert(out[0]);
    check_eq(rt->number_of_polygons_with_holes(), 1u, "unbounded round trip -> 1 polygon");
    check_eq(rt->oriented_side(arr2d::make_geom(Kind::Segment, GeomType::Point, SegPt(9, 9))), 1,
             "unbounded round trip: (9,9) inside");
  }
  {
    auto e = arr2d::make_polygon_set_segment();
    check(e->is_empty(), "fresh set is empty");
    check(!e->is_plane(), "fresh set is not the plane");
    check_eq(e->number_of_polygons_with_holes(), 0u, "fresh set has 0 polygons");
    e->complement();
    check(e->is_plane(), "complement(empty) is the plane");
    check(!e->is_empty(), "complement(empty) is not empty");
    e->polygons_with_holes(out);
    check_eq(out.size(), 1u, "plane -> 1 polygon");
    check(out[0].unbounded && out[0].outer.empty() && out[0].holes.empty(),
          "plane -> unbounded polygon with no boundary at all");
    e->clear();
    check(e->is_empty(), "clear() after complement -> empty (clear resets `contained`)");
  }

  section("segment: queries");
  {
    auto a = make_a();
    auto pt = [](int x, int y) {
      return arr2d::make_geom(Kind::Segment, GeomType::Point, SegPt(x, y));
    };
    check_eq(a->oriented_side(pt(1, 1)), 1, "oriented_side(inside) == +1");
    check_eq(a->oriented_side(pt(0, 0)), 0, "oriented_side(vertex) == 0");
    check_eq(a->oriented_side(pt(1, 0)), 0, "oriented_side(edge) == 0");
    check_eq(a->oriented_side(pt(9, 9)), -1, "oriented_side(outside) == -1");
    expect_error([&] { a->oriented_side(seg_geom(SegPt(0, 0), SegPt(1, 1))); },
                 ErrorCode::InvalidArgument, "point", "oriented_side(curve box) -> Error");

    PolygonGeom found;
    check(a->locate(pt(1, 1), found), "locate(inside) == true");
    check_eq(ring_count(found), 4u, "locate -> the square");
    PolygonGeom none;
    check(!a->locate(pt(9, 9), none), "locate(outside) == false");

    auto b = make_b();
    check(a->do_intersect(*b), "do_intersect(overlapping) == true");
    check_eq(a->oriented_side_of_set(*b), 1, "oriented_side(overlapping set) == +1");
    auto far = arr2d::make_polygon_set_segment();
    far->insert(poly(seg_square(9, 9, 10, 10)));
    check(!a->do_intersect(*far), "do_intersect(disjoint) == false");
    check_eq(a->oriented_side_of_set(*far), -1, "oriented_side(disjoint set) == -1");
    auto touch = arr2d::make_polygon_set_segment();
    touch->insert(poly(seg_square(2, 0, 4, 2)));
    check(!touch->do_intersect(*a), "do_intersect(edge-touching) == false");
    check_eq(a->oriented_side_of_set(*touch), 0, "oriented_side(edge-touching set) == 0");
  }

  section("segment: *_polygon variants");
  {
    auto a = make_a();
    a->join_polygon(poly(seg_square(1, 1, 3, 3)));
    a->polygons_with_holes(out);
    check_eq(ring_count(out[0]), 8u, "join_polygon -> octagon");

    auto b = make_a();
    b->intersection_polygon(poly(seg_square(1, 1, 3, 3)));
    b->polygons_with_holes(out);
    check_eq(ring_count(out[0]), 4u, "intersection_polygon -> square");

    auto c = make_a();
    c->difference_polygon(poly(seg_square(1, 1, 3, 3)));
    c->polygons_with_holes(out);
    check_eq(ring_count(out[0]), 6u, "difference_polygon -> L");

    auto d = make_a();
    d->symmetric_difference_polygon(poly(seg_square(1, 1, 3, 3)));
    d->polygons_with_holes(out);
    check_eq(curve_count(out[0]), 12u, "symmetric_difference_polygon -> 8 + 4 curves");

    expect_error([&] { make_a()->join_polygon(poly(cw)); }, ErrorCode::InvalidArgument,
                 "orientation", "join_polygon(cw) -> Error(InvalidArgument)");
  }

  section("segment: clone / self-operations / kind mismatch");
  {
    auto a = make_a();
    auto c = a->clone();
    check_eq(static_cast<int>(c->kind()), static_cast<int>(Kind::Segment), "clone keeps the kind");
    check_eq(c->number_of_polygons_with_holes(), 1u, "clone -> 1 polygon");
    check(c->is_valid(), "clone -> is_valid()");
    c->join(*make_b());
    check_eq(c->number_of_polygons_with_holes(), 1u, "clone modified independently");
    c->polygons_with_holes(out);
    check_eq(ring_count(out[0]), 8u, "clone after join -> octagon");
    a->polygons_with_holes(out);
    check_eq(ring_count(out[0]), 4u, "original untouched by the clone's join");

    auto pl = arr2d::make_polygon_set_segment();
    pl->complement();  // the whole plane
    auto plc = pl->clone();
    check(plc->is_plane(), "clone of the plane is the plane");

    auto empty_clone = arr2d::make_polygon_set_segment()->clone();
    check(empty_clone->is_empty(), "clone of the empty set is empty");

    // self-operations must not free the arrangement they read (gotcha 3)
    auto s = make_a();
    s->join(*s);
    check_eq(s->number_of_polygons_with_holes(), 1u, "join(self) is a no-op");
    s->intersection(*s);
    check_eq(s->number_of_polygons_with_holes(), 1u, "intersection(self) is a no-op");
    check(s->do_intersect(*s), "do_intersect(self) == true");
    check_eq(s->oriented_side_of_set(*s), 1, "oriented_side(self) == +1");
    s->symmetric_difference(*s);
    check(s->is_empty(), "symmetric_difference(self) -> empty");
    auto s2 = make_a();
    s2->difference(*s2);
    check(s2->is_empty(), "difference(self) -> empty");
  }
#if ARR2D_TEST_CIRCLE_SEGMENT
  {
    auto a = make_a();
    auto other = arr2d::make_polygon_set_circle_segment();
    expect_error([&] { a->join(*other); }, ErrorCode::KindMismatch, "circle_segment",
                 "join(circle_segment set) -> Error(KindMismatch)");
    expect_error([&] { a->do_intersect(*other); }, ErrorCode::KindMismatch, "circle_segment",
                 "do_intersect(circle_segment set) -> Error(KindMismatch)");
  }
#endif

  section("segment: registry factory + to_arrangement");
  if (arr2d::kind_available(Kind::Segment)) {
    // The path Cython actually uses: registry.hpp make_polygon_set(kind).
    check(arr2d::kind_has_polygon_set(Kind::Segment), "registry: segment has Boolean ops");
    std::unique_ptr<PolygonSetBase> via = arr2d::make_polygon_set(Kind::Segment);
    check_eq(static_cast<int>(via->kind()), static_cast<int>(Kind::Segment),
             "registry make_polygon_set(segment) -> a segment set");
    via->insert(poly(seg_square(0, 0, 2, 2)));
    check_eq(via->number_of_polygons_with_holes(), 1u, "registry-built set works");
  }
  if (!arr2d::kind_available(Kind::Segment)) {
    skip("to_arrangement needs kind_segment.cpp (arr2d::make_arrangement(Kind::Segment))");
  } else {
    {  // the L-shaped union: one contained face with an 8-halfedge outer ccb
      auto a = make_a();
      a->join(*make_b());
      std::vector<arr2d::FH> contained;
      std::unique_ptr<arr2d::ArrBase> arr = a->to_arrangement(contained);
      check_eq(arr->number_of_faces(), 2u, "to_arrangement(join) -> 2 faces");
      check_eq(arr->number_of_edges(), 8u, "to_arrangement(join) -> 8 edges");
      check_eq(arr->number_of_vertices(), 8u, "to_arrangement(join) -> 8 vertices");
      check_eq(arr->number_of_curves(), 8u, "to_arrangement(join) -> 8 history curves");
      check(arr->is_valid(), "to_arrangement(join) -> valid arrangement");
      check_eq(contained.size(), 1u, "to_arrangement(join) -> 1 contained face");
      check(!arr->face_is_unbounded(contained[0]), "contained face is bounded");
      check_eq(ccb_length(*arr, contained[0]), 8u, "contained face outer ccb has 8 halfedges");
    }
    {  // symmetric difference: two contained faces (the polygon-with-holes has two pieces)
      auto a = make_a();
      a->symmetric_difference(*make_b());
      std::vector<arr2d::FH> contained;
      std::unique_ptr<arr2d::ArrBase> arr = a->to_arrangement(contained);
      check_eq(arr->number_of_faces(), 4u, "to_arrangement(symdiff) -> 4 faces");
      check_eq(arr->number_of_edges(), 12u, "to_arrangement(symdiff) -> 12 edges");
      check_eq(contained.size(), 2u, "to_arrangement(symdiff) -> 2 contained faces");
      bool all_bounded = true;
      for (arr2d::FH f : contained) all_bounded = all_bounded && !arr->face_is_unbounded(f);
      check(all_bounded, "to_arrangement(symdiff): both contained faces are bounded");
    }
    {  // complement: the contained face is the unbounded one
      auto a = make_a();
      a->complement();
      std::vector<arr2d::FH> contained;
      std::unique_ptr<arr2d::ArrBase> arr = a->to_arrangement(contained);
      check_eq(arr->number_of_edges(), 4u, "to_arrangement(complement) -> 4 edges");
      check_eq(contained.size(), 1u, "to_arrangement(complement) -> 1 contained face");
      check(arr->face_is_unbounded(contained[0]), "to_arrangement(complement): unbounded face");
      check(contained[0] == arr->unbounded_face(), "contained face == unbounded_face()");
    }
    {  // the whole plane: no curves at all, the unbounded face is contained
      auto a = arr2d::make_polygon_set_segment();
      a->complement();
      std::vector<arr2d::FH> contained;
      std::unique_ptr<arr2d::ArrBase> arr = a->to_arrangement(contained);
      check_eq(arr->number_of_edges(), 0u, "to_arrangement(plane) -> 0 edges");
      check_eq(contained.size(), 1u, "to_arrangement(plane) -> 1 contained face");
      check(arr->face_is_unbounded(contained[0]), "to_arrangement(plane): unbounded face");
    }
    {  // the empty set: nothing at all
      auto a = arr2d::make_polygon_set_segment();
      std::vector<arr2d::FH> contained;
      std::unique_ptr<arr2d::ArrBase> arr = a->to_arrangement(contained);
      check_eq(arr->number_of_edges(), 0u, "to_arrangement(empty) -> 0 edges");
      check_eq(contained.size(), 0u, "to_arrangement(empty) -> no contained face");
    }
    {  // a square with a hole: one contained face with one inner ccb
      auto a = arr2d::make_polygon_set_segment();
      PolygonGeom p = poly(seg_square(0, 0, 10, 10));
      p.holes.push_back(seg_square_cw(3, 3, 7, 7));
      a->insert(p);
      std::vector<arr2d::FH> contained;
      std::unique_ptr<arr2d::ArrBase> arr = a->to_arrangement(contained);
      check_eq(arr->number_of_faces(), 3u, "to_arrangement(square+hole) -> 3 faces");
      check_eq(arr->number_of_edges(), 8u, "to_arrangement(square+hole) -> 8 edges");
      check_eq(contained.size(), 1u, "to_arrangement(square+hole) -> 1 contained face");
      check_eq(arr->face_number_of_inner_ccbs(contained[0]), 1u, "contained face has 1 hole");
      check_eq(ccb_length(*arr, contained[0]), 4u, "contained face outer ccb has 4 halfedges");
    }
  }
}

}  // namespace
#endif  // ARR2D_TEST_SEGMENT

// ===========================================================================
// Curved kinds: a full circle (two x-monotone arcs) per kind
// ===========================================================================
#if ARR2D_TEST_CIRCLE_SEGMENT || ARR2D_TEST_CONIC || ARR2D_TEST_BEZIER
namespace {

/// Turn a general curve into its x-monotone pieces with `traits`, dropping isolated points,
/// and box each piece as a Geom of `kind`.  `Reverse` makes the ring clockwise -> ccw.
template <class Traits, class Curve>
std::vector<Geom> x_monotone_ring(Kind kind, Traits& traits, const Curve& c) {
  using Xcv = typename Traits::X_monotone_curve_2;
  using Pt = typename Traits::Point_2;
  std::vector<std::variant<Pt, Xcv>> pieces;
  traits.make_x_monotone_2_object()(c, std::back_inserter(pieces));
  std::vector<Geom> out;
  for (const auto& v : pieces)
    if (std::holds_alternative<Xcv>(v))
      out.push_back(arr2d::make_geom(kind, GeomType::XCurve, std::get<Xcv>(v)));
  return out;
}

/// Reverse a ring: opposite curve order and opposite direction of every curve.
template <class Traits>
std::vector<Geom> reversed_ring(Kind kind, Traits& traits, const std::vector<Geom>& ring) {
  using Xcv = typename Traits::X_monotone_curve_2;
  auto opp = traits.construct_opposite_2_object();  // non-const local: some are non-const
  std::vector<Geom> out;
  for (auto it = ring.rbegin(); it != ring.rend(); ++it)
    out.push_back(arr2d::make_geom(kind, GeomType::XCurve, Xcv(opp(it->as<Xcv>()))));
  return out;
}

/// Make `ring` counterclockwise (CGAL's outer-boundary precondition).
template <class Traits>
std::vector<Geom> ccw_ring(const PolygonSetBase& ps, Kind kind, Traits& traits,
                           std::vector<Geom> ring) {
  if (ps.orientation(ring) < 0) return reversed_ring(kind, traits, ring);
  return ring;
}

/// The shared body of the curved-kind tests.
void run_curved_kind(const char* name, Kind kind, std::unique_ptr<PolygonSetBase> ps,
                     const std::vector<Geom>& ring_a, const std::vector<Geom>& ring_a_cw,
                     const std::vector<Geom>& ring_b, const Geom& inside_point,
                     const Geom& outside_point, std::size_t expect_join_curves) {
  PolygonGeom pa;
  pa.outer = ring_a;
  PolygonGeom pb;
  pb.outer = ring_b;

  check_eq(static_cast<int>(ps->kind()), static_cast<int>(kind),
           std::string(name) + ": kind()");
  check(ps->is_closed_chain(ring_a), std::string(name) + ": ring is a closed chain");
  check_eq(ps->orientation(ring_a), 1, std::string(name) + ": ring is counterclockwise");
  check(ps->is_valid_polygon(pa), std::string(name) + ": is_valid_polygon(ring)");

  ps->insert(pa);
  check_eq(ps->number_of_polygons_with_holes(), 1u, std::string(name) + ": npwh after insert");
  check(ps->is_valid(), std::string(name) + ": is_valid() after insert");
  check_eq(ps->oriented_side(inside_point), 1, std::string(name) + ": oriented_side(inside)");
  check_eq(ps->oriented_side(outside_point), -1, std::string(name) + ": oriented_side(outside)");

  std::vector<PolygonGeom> out;
  ps->polygons_with_holes(out);
  check_eq(out.size(), 1u, std::string(name) + ": polygons_with_holes size");
  check_eq(ring_count(out[0]), ring_a.size(), std::string(name) + ": same number of curves back");
  check_eq(ps->orientation(out[0].outer), 1, std::string(name) + ": output ring is ccw");
  check(ps->is_valid_polygon(out[0]), std::string(name) + ": output ring is valid CGAL input");

  // round trip through a fresh set
  {
    auto rt = ps->clone();
    rt->clear();
    rt->insert(out[0]);
    check_eq(rt->number_of_polygons_with_holes(), 1u, std::string(name) + ": round trip npwh");
    check_eq(rt->oriented_side(inside_point), 1, std::string(name) + ": round trip contains the point");
  }

  // union with the second (overlapping) shape
  {
    auto other = ps->clone();
    other->clear();
    other->insert(pb);
    check(ps->do_intersect(*other), std::string(name) + ": the two shapes overlap");
    auto u = ps->clone();
    u->join(*other);
    check_eq(u->number_of_polygons_with_holes(), 1u, std::string(name) + ": join -> 1 polygon");
    u->polygons_with_holes(out);
    check_eq(ring_count(out[0]), expect_join_curves, std::string(name) + ": join curve count");
    check(u->is_valid(), std::string(name) + ": join -> is_valid()");

    auto i = ps->clone();
    i->intersection(*other);
    check_eq(i->number_of_polygons_with_holes(), 1u,
             std::string(name) + ": intersection -> 1 polygon");
    auto d = ps->clone();
    d->difference(*other);
    check_eq(d->number_of_polygons_with_holes(), 1u, std::string(name) + ": difference -> 1 polygon");
  }

  // complement
  {
    auto c = ps->clone();
    c->complement();
    check(!c->is_plane(), std::string(name) + ": complement is not the plane");
    c->polygons_with_holes(out);
    check_eq(out.size(), 1u, std::string(name) + ": complement -> 1 polygon");
    check(out[0].unbounded, std::string(name) + ": complement -> unbounded");
    check_eq(out[0].holes.size(), 1u, std::string(name) + ": complement -> 1 hole");
    check_eq(c->orientation(out[0].holes[0]), -1, std::string(name) + ": complement hole is cw");
  }

  // a clockwise ring must be rejected with an orientation message (CGAL never fixes it)
  {
    check(ps->is_closed_chain(ring_a_cw), std::string(name) + ": cw ring is a closed chain");
    check_eq(ps->orientation(ring_a_cw), -1, std::string(name) + ": cw ring orientation == -1");
    PolygonGeom bad;
    bad.outer = ring_a_cw;
    check(!ps->is_valid_polygon(bad), std::string(name) + ": is_valid_polygon(cw ring) == false");
    auto s = ps->clone();
    s->clear();
    expect_error([&] { s->insert(bad); }, ErrorCode::InvalidArgument, "orientation",
                 std::string(name) + ": insert(cw ring) -> Error(InvalidArgument, ...orientation...)");
  }

  // to_arrangement (needs the kind TU)
  if (!arr2d::kind_available(kind)) {
    skip(std::string(name) + ": to_arrangement needs the kind TU");
  } else {
    check(arr2d::kind_has_polygon_set(kind), std::string(name) + ": registry reports Boolean ops");
    check_eq(static_cast<int>(arr2d::make_polygon_set(kind)->kind()), static_cast<int>(kind),
             std::string(name) + ": registry make_polygon_set()");
    std::vector<arr2d::FH> contained;
    std::unique_ptr<arr2d::ArrBase> arr = ps->to_arrangement(contained);
    check_eq(arr->number_of_edges(), ring_a.size(), std::string(name) + ": to_arrangement edges");
    check_eq(contained.size(), 1u, std::string(name) + ": to_arrangement -> 1 contained face");
    check(!arr->face_is_unbounded(contained[0]),
          std::string(name) + ": to_arrangement contained face is bounded");
    check(arr->is_valid(), std::string(name) + ": to_arrangement -> valid arrangement");
  }
}

}  // namespace
#endif  // any curved kind

#if ARR2D_TEST_CIRCLE_SEGMENT
namespace {
void test_circle_segment() {
  section("circle_segment");
  using T = arr2d::CircleSegmentTypes;
  // A local, deliberately leaked traits instance: this test does not need
  // kind_circle_segment.cpp (Arr_circle_segment_traits_2 carries no geometry state).
  static T::Traits* tr = new T::Traits();
  auto circle = [&](int cx, int cy, int r) {
    T::Curve_2 c(T::Kernel::Circle_2(T::Kernel::Point_2(cx, cy),
                                     T::Kernel::FT(r) * T::Kernel::FT(r),
                                     CGAL::COUNTERCLOCKWISE));
    return x_monotone_ring(Kind::CircleSegment, *tr, c);
  };
  auto ps = arr2d::make_polygon_set_circle_segment();
  std::vector<Geom> a = ccw_ring(*ps, Kind::CircleSegment, *tr, circle(0, 0, 2));
  std::vector<Geom> b = ccw_ring(*ps, Kind::CircleSegment, *tr, circle(2, 0, 2));
  check_eq(a.size(), 2u, "circle_segment: a full circle is 2 x-monotone arcs");
  const Geom inside = arr2d::make_geom(Kind::CircleSegment, GeomType::Point, T::Point_2(0, 0));
  const Geom outside = arr2d::make_geom(Kind::CircleSegment, GeomType::Point, T::Point_2(10, 10));
  std::vector<Geom> a_cw = reversed_ring(Kind::CircleSegment, *tr, a);
  run_curved_kind("circle_segment", Kind::CircleSegment, std::move(ps), a, a_cw, b, inside,
                  outside, 4u);
}
}  // namespace
#endif

#if ARR2D_TEST_CONIC
namespace {
void test_conic() {
  section("conic");
  using T = arr2d::ConicTypes;
  T::Traits& tr = T::traits();  // the process-wide (leaked) conic traits: kind_conic.cpp
  auto circle = [&](int cx, int cy, int r) {
    auto ctr = tr.construct_curve_2_object();
    T::Curve_2 c = ctr(T::Rat_circle_2(T::Rat_point_2(cx, cy), T::Rational(r) * T::Rational(r)));
    return x_monotone_ring(Kind::Conic, tr, c);
  };
  auto ps = arr2d::make_polygon_set_conic();
  // CGAL's conic Construct_curve_2 emits the two x-monotone arcs of a full circle in CLOCKWISE
  // order, so the ring has to be flipped (orientation is a hard CGAL precondition, gotcha 5).
  std::vector<Geom> a = ccw_ring(*ps, Kind::Conic, tr, circle(0, 0, 2));
  std::vector<Geom> b = ccw_ring(*ps, Kind::Conic, tr, circle(2, 0, 2));
  check_eq(a.size(), 2u, "conic: a full circle is 2 x-monotone arcs");
  const Geom inside = arr2d::make_geom(
      Kind::Conic, GeomType::Point, T::Point_2(T::Algebraic(0), T::Algebraic(0)));
  const Geom outside = arr2d::make_geom(
      Kind::Conic, GeomType::Point, T::Point_2(T::Algebraic(10), T::Algebraic(10)));
  std::vector<Geom> a_cw = reversed_ring(Kind::Conic, tr, a);
  run_curved_kind("conic", Kind::Conic, std::move(ps), a, a_cw, b, inside, outside, 4u);
}
}  // namespace
#endif

#if ARR2D_TEST_BEZIER
namespace {
void test_bezier() {
  section("bezier");
  using T = arr2d::BezierTypes;
  T::Traits& tr = T::traits();  // process-wide (leaked) Bezier traits: kind_bezier.cpp
  using P = T::Rat_point_2;
  auto pieces = [&](std::initializer_list<P> cps) {
    std::vector<P> v(cps);
    T::Curve_2 c(v.begin(), v.end());
    return x_monotone_ring(Kind::Bezier, tr, c);
  };
  auto ps = arr2d::make_polygon_set_bezier();
  // A lens: the lower quadratic Bezier from (0,0) to (2,0) plus the upper one traversed
  // backwards.  Both are x-monotone (x(t) is linear), so each gives exactly one piece.
  auto lens = [&](int dx) {
    std::vector<Geom> lower = pieces({P(dx, 0), P(dx + 1, -2), P(dx + 2, 0)});
    std::vector<Geom> upper = pieces({P(dx, 0), P(dx + 1, 2), P(dx + 2, 0)});
    std::vector<Geom> ring = lower;
    std::vector<Geom> rev = reversed_ring(Kind::Bezier, tr, upper);
    ring.insert(ring.end(), rev.begin(), rev.end());
    return ring;
  };
  std::vector<Geom> a = ccw_ring(*ps, Kind::Bezier, tr, lens(0));
  std::vector<Geom> b = ccw_ring(*ps, Kind::Bezier, tr, lens(1));
  check_eq(a.size(), 2u, "bezier: the lens has 2 x-monotone curves");
  const Geom inside = arr2d::make_geom(Kind::Bezier, GeomType::Point,
                                       T::Point_2(T::Rational(1), T::Rational(0)));
  const Geom outside = arr2d::make_geom(Kind::Bezier, GeomType::Point,
                                        T::Point_2(T::Rational(10), T::Rational(10)));
  std::vector<Geom> a_cw = reversed_ring(Kind::Bezier, tr, a);
  run_curved_kind("bezier", Kind::Bezier, std::move(ps), a, a_cw, b, inside, outside, 4u);
}
}  // namespace
#endif

// ===========================================================================
int main() {
  arr2d::init_all_kinds();
  std::printf("kinds available: segment=%d circle_segment=%d conic=%d bezier=%d\n",
              static_cast<int>(arr2d::kind_available(Kind::Segment)),
              static_cast<int>(arr2d::kind_available(Kind::CircleSegment)),
              static_cast<int>(arr2d::kind_available(Kind::Conic)),
              static_cast<int>(arr2d::kind_available(Kind::Bezier)));

#if ARR2D_TEST_SEGMENT
  test_segment();
#endif
#if ARR2D_TEST_CIRCLE_SEGMENT
  test_circle_segment();
#endif
#if ARR2D_TEST_CONIC
  test_conic();
#endif
#if ARR2D_TEST_BEZIER
  test_bezier();
#endif

  std::printf("\n===== %d passed, %d failed, %d skipped =====\n", g_pass, g_fail, g_skip);
  return g_fail == 0 ? 0 : 1;
}
