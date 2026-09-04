#include <CGAL/Exact_predicates_exact_constructions_kernel.h>
#include <CGAL/Arr_segment_traits_2.h>
#include <CGAL/Arr_linear_traits_2.h>
#include <CGAL/Arr_circle_segment_traits_2.h>
#include <CGAL/Arrangement_2.h>
#include <CGAL/Arr_overlay_2.h>
#include <CGAL/Arr_default_overlay_traits.h>
#include <CGAL/Arr_extended_dcel.h>
#include <CGAL/Arr_walk_along_line_point_location.h>
#include <CGAL/Arr_landmarks_point_location.h>
#include <CGAL/Arr_trapezoid_ric_point_location.h>
#include <CGAL/Arr_naive_point_location.h>
#include <CGAL/CORE_algebraic_number_traits.h>
#include <CGAL/Cartesian.h>
#include <CGAL/Arr_Bezier_curve_traits_2.h>
#include <CGAL/Arr_conic_traits_2.h>
#include <CGAL/Arr_polyline_traits_2.h>
#include <CGAL/Arr_geodesic_arc_on_sphere_traits_2.h>
#include <CGAL/Arr_spherical_topology_traits_2.h>
#include <CGAL/Arrangement_on_surface_2.h>
#include <CGAL/Boolean_set_operations_2.h>
#include <CGAL/Polygon_2.h>
#include <CGAL/Polygon_with_holes_2.h>
#include <CGAL/Polygon_set_2.h>
#include <iostream>
#include <vector>

typedef CGAL::Exact_predicates_exact_constructions_kernel Kernel;
typedef CGAL::Arr_segment_traits_2<Kernel> SegTraits;
typedef CGAL::Arrangement_2<SegTraits> SegArr;
typedef CGAL::Arr_linear_traits_2<Kernel> LinTraits;
typedef CGAL::Arrangement_2<LinTraits> LinArr;
typedef CGAL::Arr_circle_segment_traits_2<Kernel> CircTraits;
typedef CGAL::Arrangement_2<CircTraits> CircArr;
typedef CGAL::Arr_polyline_traits_2<SegTraits> PolyTraits;
typedef CGAL::Arrangement_2<PolyTraits> PolyArr;

typedef CGAL::CORE_algebraic_number_traits Nt_traits;
typedef Nt_traits::Rational NT;
typedef Nt_traits::Rational Rational;
typedef Nt_traits::Algebraic Algebraic;
typedef CGAL::Cartesian<Rational> Rat_kernel;
typedef CGAL::Cartesian<Algebraic> Alg_kernel;
typedef CGAL::Arr_Bezier_curve_traits_2<Rat_kernel, Alg_kernel, Nt_traits> Bezier_traits;
typedef CGAL::Arrangement_2<Bezier_traits> BezArr;
typedef CGAL::Arr_conic_traits_2<Rat_kernel, Alg_kernel, Nt_traits> Conic_traits;
typedef CGAL::Arrangement_2<Conic_traits> ConicArr;

typedef CGAL::Arr_geodesic_arc_on_sphere_traits_2<Kernel> GeoTraits;
typedef CGAL::Arr_spherical_topology_traits_2<GeoTraits> SphTopol;
typedef CGAL::Arrangement_on_surface_2<GeoTraits, SphTopol> SphArr;

int main() {
  // Segments
  SegArr arr;
  std::vector<SegTraits::Curve_2> segs;
  segs.push_back(SegTraits::Curve_2(Kernel::Point_2(0,0), Kernel::Point_2(4,0)));
  segs.push_back(SegTraits::Curve_2(Kernel::Point_2(4,0), Kernel::Point_2(4,4)));
  segs.push_back(SegTraits::Curve_2(Kernel::Point_2(4,4), Kernel::Point_2(0,4)));
  segs.push_back(SegTraits::Curve_2(Kernel::Point_2(0,4), Kernel::Point_2(0,0)));
  segs.push_back(SegTraits::Curve_2(Kernel::Point_2(-1,2), Kernel::Point_2(5,2)));
  CGAL::insert(arr, segs.begin(), segs.end());
  std::cout << "seg arr: V=" << arr.number_of_vertices() << " E=" << arr.number_of_edges() << " F=" << arr.number_of_faces() << "\n";

  // Point location
  CGAL::Arr_walk_along_line_point_location<SegArr> pl(arr);
  auto res = pl.locate(Kernel::Point_2(2,1));
  if (auto f = std::get_if<SegArr::Face_const_handle>(&res)) std::cout << "located in face, unbounded=" << (*f)->is_unbounded() << "\n";
  CGAL::Arr_landmarks_point_location<SegArr> lm(arr);
  CGAL::Arr_trapezoid_ric_point_location<SegArr> tr(arr);
  (void)lm; (void)tr;

  // Overlay
  SegArr arr2, out;
  std::vector<SegTraits::Curve_2> segs2 = { SegTraits::Curve_2(Kernel::Point_2(2,-1), Kernel::Point_2(2,5)) };
  CGAL::insert(arr2, segs2.begin(), segs2.end());
  CGAL::Arr_default_overlay_traits<SegArr> ovl;
  CGAL::overlay(arr, arr2, out, ovl);
  std::cout << "overlay: V=" << out.number_of_vertices() << " E=" << out.number_of_edges() << " F=" << out.number_of_faces() << "\n";

  // Linear (unbounded)
  LinArr larr;
  CGAL::insert(larr, LinTraits::Curve_2(Kernel::Line_2(Kernel::Point_2(0,0), Kernel::Point_2(1,1))));
  CGAL::insert(larr, LinTraits::Curve_2(Kernel::Line_2(Kernel::Point_2(0,1), Kernel::Point_2(1,0))));
  std::cout << "lin arr: V=" << larr.number_of_vertices() << " E=" << larr.number_of_edges() << " F=" << larr.number_of_faces() << " unb=" << larr.number_of_unbounded_faces() << "\n";

  // Circles
  CircArr carr;
  Kernel::Circle_2 c1(Kernel::Point_2(0,0), 4);
  Kernel::Circle_2 c2(Kernel::Point_2(2,0), 4);
  CGAL::insert(carr, CircTraits::Curve_2(c1));
  CGAL::insert(carr, CircTraits::Curve_2(c2));
  std::cout << "circ arr: V=" << carr.number_of_vertices() << " E=" << carr.number_of_edges() << " F=" << carr.number_of_faces() << "\n";

  // Bezier
  BezArr barr;
  std::vector<Rat_kernel::Point_2> ctrl = { Rat_kernel::Point_2(0,0), Rat_kernel::Point_2(1,3), Rat_kernel::Point_2(3,3), Rat_kernel::Point_2(4,0) };
  Bezier_traits::Curve_2 bc(ctrl.begin(), ctrl.end());
  std::vector<Rat_kernel::Point_2> ctrl2 = { Rat_kernel::Point_2(0,2), Rat_kernel::Point_2(4,2) };
  Bezier_traits::Curve_2 bc2(ctrl2.begin(), ctrl2.end());
  CGAL::insert(barr, bc);
  CGAL::insert(barr, bc2);
  std::cout << "bez arr: V=" << barr.number_of_vertices() << " E=" << barr.number_of_edges() << " F=" << barr.number_of_faces() << "\n";
  for (auto v = barr.vertices_begin(); v != barr.vertices_end(); ++v) {
    std::cout << "  v: " << CGAL::to_double(v->point().x()) << "," << CGAL::to_double(v->point().y()) << "\n";
  }

  // Conic
  ConicArr coarr;
  Conic_traits ctraits;
  auto ctr_cv = ctraits.construct_curve_2_object();
  // Full ellipse: 4x^2 + y^2 = 16  =>  r=4, s=1, t=0, u=0, v=0, w=-16
  Conic_traits::Curve_2 ell = ctr_cv(4, 1, 0, 0, 0, -16);
  CGAL::insert(coarr, ell);
  std::cout << "conic arr: V=" << coarr.number_of_vertices() << " E=" << coarr.number_of_edges() << " F=" << coarr.number_of_faces() << "\n";

  // Sphere
  SphArr sarr;
  GeoTraits gtraits;
  auto ctr_p = gtraits.construct_point_2_object();
  auto ctr_cv2 = gtraits.construct_curve_2_object();
  auto p1 = ctr_p(1,0,0), p2 = ctr_p(0,1,0), p3 = ctr_p(0,0,1);
  CGAL::insert(sarr, ctr_cv2(p1, p2));
  CGAL::insert(sarr, ctr_cv2(p2, p3));
  CGAL::insert(sarr, ctr_cv2(p3, p1));
  std::cout << "sph arr: V=" << sarr.number_of_vertices() << " E=" << sarr.number_of_edges() << " F=" << sarr.number_of_faces() << "\n";

  // Boolean ops
  CGAL::Polygon_2<Kernel> P, Q;
  P.push_back(Kernel::Point_2(0,0)); P.push_back(Kernel::Point_2(2,0)); P.push_back(Kernel::Point_2(2,2)); P.push_back(Kernel::Point_2(0,2));
  Q.push_back(Kernel::Point_2(1,1)); Q.push_back(Kernel::Point_2(3,1)); Q.push_back(Kernel::Point_2(3,3)); Q.push_back(Kernel::Point_2(1,3));
  CGAL::Polygon_set_2<Kernel> S; S.insert(P); S.join(Q);
  std::cout << "polygon set faces: " << S.number_of_polygons_with_holes() << " arr faces=" << S.arrangement().number_of_faces() << "\n";
  std::cout << "OK\n";
  return 0;
}
