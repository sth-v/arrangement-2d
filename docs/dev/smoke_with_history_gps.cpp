#include <CGAL/Exact_predicates_exact_constructions_kernel.h>
#include <CGAL/Arr_segment_traits_2.h>
#include <CGAL/Arr_linear_traits_2.h>
#include <CGAL/Arr_circle_segment_traits_2.h>
#include <CGAL/Arr_polyline_traits_2.h>
#include <CGAL/Arrangement_with_history_2.h>
#include <CGAL/Arrangement_on_surface_with_history_2.h>
#include <CGAL/Arr_extended_dcel.h>
#include <CGAL/Arr_overlay_2.h>
#include <CGAL/Arr_default_overlay_traits.h>
#include <CGAL/Arr_observer.h>
#include <CGAL/Arr_walk_along_line_point_location.h>
#include <CGAL/Arr_trapezoid_ric_point_location.h>
#include <CGAL/Arr_landmarks_point_location.h>
#include <CGAL/Arr_vertical_decomposition_2.h>
#include <CGAL/Arr_batched_point_location.h>
#include <CGAL/CORE_algebraic_number_traits.h>
#include <CGAL/Cartesian.h>
#include <CGAL/Arr_Bezier_curve_traits_2.h>
#include <CGAL/Arr_conic_traits_2.h>
#include <CGAL/Arr_geodesic_arc_on_sphere_traits_2.h>
#include <CGAL/Arr_spherical_topology_traits_2.h>
#include <CGAL/Boolean_set_operations_2.h>
#include <CGAL/General_polygon_set_2.h>
#include <CGAL/Gps_traits_2.h>
#include <CGAL/Gps_circle_segment_traits_2.h>
#include <CGAL/Polygon_set_2.h>
#include <CGAL/Exact_rational.h>
#include <CGAL/Exact_integer.h>
#include <CGAL/Fraction_traits.h>
#include <iostream>
#include <vector>
#include <typeinfo>
#include <cxxabi.h>

struct PyData { int tag = 0; };
typedef CGAL::Exact_predicates_exact_constructions_kernel Kernel;
typedef CGAL::Arr_segment_traits_2<Kernel> SegTraits;
typedef CGAL::Arr_extended_dcel<SegTraits, PyData, PyData, PyData> SegDcel;
typedef CGAL::Arrangement_with_history_2<SegTraits, SegDcel> SegArr;

typedef CGAL::Arr_linear_traits_2<Kernel> LinTraits;
typedef CGAL::Arrangement_with_history_2<LinTraits, CGAL::Arr_extended_dcel<LinTraits, PyData, PyData, PyData>> LinArr;

typedef CGAL::Arr_circle_segment_traits_2<Kernel> CircTraits;
typedef CGAL::Arrangement_with_history_2<CircTraits, CGAL::Arr_extended_dcel<CircTraits, PyData, PyData, PyData>> CircArr;

typedef CGAL::Arr_polyline_traits_2<SegTraits> PolyTraits;
typedef CGAL::Arrangement_with_history_2<PolyTraits, CGAL::Arr_extended_dcel<PolyTraits, PyData, PyData, PyData>> PolyArr;

typedef CGAL::CORE_algebraic_number_traits Nt_traits;
typedef Nt_traits::Rational Rational;
typedef Nt_traits::Algebraic Algebraic;
typedef CGAL::Cartesian<Rational> Rat_kernel;
typedef CGAL::Cartesian<Algebraic> Alg_kernel;
typedef CGAL::Arr_Bezier_curve_traits_2<Rat_kernel, Alg_kernel, Nt_traits> Bezier_traits;
typedef CGAL::Arrangement_with_history_2<Bezier_traits, CGAL::Arr_extended_dcel<Bezier_traits, PyData, PyData, PyData>> BezArr;
typedef CGAL::Arr_conic_traits_2<Rat_kernel, Alg_kernel, Nt_traits> Conic_traits;
typedef CGAL::Arrangement_with_history_2<Conic_traits, CGAL::Arr_extended_dcel<Conic_traits, PyData, PyData, PyData>> ConicArr;

typedef CGAL::Arr_geodesic_arc_on_sphere_traits_2<Kernel> GeoTraits;
typedef CGAL::Arr_extended_dcel<GeoTraits, PyData, PyData, PyData> GeoDcel;
typedef CGAL::Arr_spherical_topology_traits_2<GeoTraits, GeoDcel> SphTopol;
typedef CGAL::Arrangement_on_surface_with_history_2<GeoTraits, SphTopol> SphArr;

template <class Arr> struct Obs : public CGAL::Arr_observer<Arr> {
  int n_split_face = 0, n_create_edge = 0, n_remove_edge = 0;
  Obs(Arr& a) : CGAL::Arr_observer<Arr>(a) {}
  virtual void after_split_face(typename Arr::Face_handle, typename Arr::Face_handle, bool) { n_split_face++; }
  virtual void after_create_edge(typename Arr::Halfedge_handle) { n_create_edge++; }
  virtual void before_remove_edge(typename Arr::Halfedge_handle) { n_remove_edge++; }
};

std::string demangle(const char* n) { int st; char* r = abi::__cxa_demangle(n, 0, 0, &st); std::string s = r ? r : n; free(r); return s; }

int main() {
  std::cout << "Exact_rational = " << demangle(typeid(CGAL::Exact_rational).name()) << "\n";
  std::cout << "Epeck_ft = " << demangle(typeid(CGAL::Epeck_ft).name()) << "\n";
  std::cout << "CORE Rational = " << demangle(typeid(Rational).name()) << " Algebraic=" << demangle(typeid(Algebraic).name()) << "\n";

  SegArr arr;
  Obs<SegArr> obs(arr);
  std::vector<SegTraits::Curve_2> segs = {
    SegTraits::Curve_2(Kernel::Point_2(0,0), Kernel::Point_2(4,0)),
    SegTraits::Curve_2(Kernel::Point_2(4,0), Kernel::Point_2(4,4)),
    SegTraits::Curve_2(Kernel::Point_2(4,4), Kernel::Point_2(0,4)),
    SegTraits::Curve_2(Kernel::Point_2(0,4), Kernel::Point_2(0,0)),
    SegTraits::Curve_2(Kernel::Point_2(-1,2), Kernel::Point_2(5,2)) };
  CGAL::insert(arr, segs.begin(), segs.end());
  auto ch = CGAL::insert(arr, SegTraits::Curve_2(Kernel::Point_2(2,-1), Kernel::Point_2(2,5)));
  std::cout << "seg hist arr: V=" << arr.number_of_vertices() << " E=" << arr.number_of_edges() << " F=" << arr.number_of_faces() << " curves=" << arr.number_of_curves() << " induced=" << arr.number_of_induced_edges(ch) << "\n";
  std::cout << "observer: split_face=" << obs.n_split_face << " create_edge=" << obs.n_create_edge << "\n";
  for (auto f = arr.faces_begin(); f != arr.faces_end(); ++f) f->data().tag = 7;
  // exact coords
  {
    auto v = arr.vertices_begin();
    CGAL::Exact_rational ex = CGAL::exact(v->point().x());
    typedef CGAL::Fraction_traits<CGAL::Exact_rational> FT;
    FT::Numerator_type num; FT::Denominator_type den; FT::Decompose()(ex, num, den);
    std::ostringstream os; os << num << "/" << den; std::cout << "exact x: " << os.str() << "\n";
  }
  // remove curve
  CGAL::remove_curve(arr, ch);
  std::cout << "after remove_curve: E=" << arr.number_of_edges() << " remove_edge notif=" << obs.n_remove_edge << "\n";
  // point location on with-history
  CGAL::Arr_walk_along_line_point_location<SegArr> pl(arr);
  auto res = pl.locate(Kernel::Point_2(2,1));
  std::cout << "located face? " << (std::get_if<SegArr::Face_const_handle>(&res) != nullptr) << "\n";
  CGAL::Arr_trapezoid_ric_point_location<SegArr> tr(arr);
  CGAL::Arr_landmarks_point_location<SegArr> lm(arr);
  auto r2 = tr.ray_shoot_up(Kernel::Point_2(2,1)); (void)r2;
  // overlay with history
  SegArr arr2, out;
  CGAL::insert(arr2, SegTraits::Curve_2(Kernel::Point_2(1,-1), Kernel::Point_2(1,5)));
  CGAL::Arr_default_overlay_traits<SegArr> ovl;
  CGAL::overlay(arr, arr2, out, ovl);
  std::cout << "overlay hist: V=" << out.number_of_vertices() << " E=" << out.number_of_edges() << " F=" << out.number_of_faces() << " curves=" << out.number_of_curves() << "\n";
  // vertical decomposition
  {
    typedef std::pair<SegArr::Vertex_const_handle, std::pair<std::optional<std::variant<SegArr::Vertex_const_handle, SegArr::Halfedge_const_handle, SegArr::Face_const_handle>>, std::optional<std::variant<SegArr::Vertex_const_handle, SegArr::Halfedge_const_handle, SegArr::Face_const_handle>>>> VD;
    std::vector<VD> vds;
    CGAL::decompose(arr, std::back_inserter(vds));
    std::cout << "vert decomp entries: " << vds.size() << "\n";
  }
  // batched PL
  {
    std::vector<Kernel::Point_2> pts = {Kernel::Point_2(2,1), Kernel::Point_2(9,9)};
    std::vector<std::pair<Kernel::Point_2, std::variant<SegArr::Vertex_const_handle, SegArr::Halfedge_const_handle, SegArr::Face_const_handle>>> out_pl;
    CGAL::locate(arr, pts.begin(), pts.end(), std::back_inserter(out_pl));
    std::cout << "batched: " << out_pl.size() << "\n";
  }
  // copy
  SegArr arr_copy(arr);
  std::cout << "copy: F=" << arr_copy.number_of_faces() << " tag=" << arr_copy.faces_begin()->data().tag << " curves=" << arr_copy.number_of_curves() << "\n";

  LinArr larr;
  CGAL::insert(larr, LinTraits::Curve_2(Kernel::Line_2(Kernel::Point_2(0,0), Kernel::Point_2(1,1))));
  CGAL::insert(larr, LinTraits::Curve_2(Kernel::Ray_2(Kernel::Point_2(0,1), Kernel::Point_2(1,0))));
  std::cout << "lin hist: V=" << larr.number_of_vertices() << " E=" << larr.number_of_edges() << " F=" << larr.number_of_faces() << " unb=" << larr.number_of_unbounded_faces() << "\n";

  CircArr carr;
  CGAL::insert(carr, CircTraits::Curve_2(Kernel::Circle_2(Kernel::Point_2(0,0), 4)));
  CGAL::insert(carr, CircTraits::Curve_2(Kernel::Circle_2(Kernel::Point_2(2,0), 4)));
  std::cout << "circ hist: V=" << carr.number_of_vertices() << " E=" << carr.number_of_edges() << " F=" << carr.number_of_faces() << "\n";
  for (auto v = carr.vertices_begin(); v != carr.vertices_end(); ++v) {
    auto x = v->point().x();
    std::cout << "  x rational? " << x.is_rational() << " approx " << CGAL::to_double(x) << " alpha=" << CGAL::to_double(x.alpha()) << " beta=" << CGAL::to_double(x.beta()) << " gamma=" << CGAL::to_double(x.gamma()) << "\n";
  }

  PolyArr parr;
  PolyTraits ptraits;
  std::vector<Kernel::Point_2> pp = {Kernel::Point_2(0,0), Kernel::Point_2(1,1), Kernel::Point_2(2,0)};
  auto pline = ptraits.construct_curve_2_object()(pp.begin(), pp.end());
  CGAL::insert(parr, pline);
  std::cout << "poly hist: V=" << parr.number_of_vertices() << " E=" << parr.number_of_edges() << "\n";

  BezArr barr;
  Obs<BezArr> bobs(barr);
  std::vector<Rat_kernel::Point_2> ctrl = { Rat_kernel::Point_2(0,0), Rat_kernel::Point_2(1,3), Rat_kernel::Point_2(3,3), Rat_kernel::Point_2(4,0) };
  std::vector<Rat_kernel::Point_2> ctrl2 = { Rat_kernel::Point_2(0,2), Rat_kernel::Point_2(4,2) };
  auto bh1 = CGAL::insert(barr, Bezier_traits::Curve_2(ctrl.begin(), ctrl.end()));
  auto bh2 = CGAL::insert(barr, Bezier_traits::Curve_2(ctrl2.begin(), ctrl2.end()));
  std::cout << "bez hist: V=" << barr.number_of_vertices() << " E=" << barr.number_of_edges() << " F=" << barr.number_of_faces() << " induced1=" << barr.number_of_induced_edges(bh1) << "\n";
  for (auto v = barr.vertices_begin(); v != barr.vertices_end(); ++v) {
    auto a = v->point().approximate();
    std::cout << "  v approx: " << a.first << "," << a.second << " exact? " << v->point().is_exact() << " rational? " << v->point().is_rational() << "\n";
  }
  for (auto e = barr.edges_begin(); e != barr.edges_end(); ++e) {
    auto pr = e->curve().parameter_range();
    std::cout << "  edge t-range approx: " << pr.first << ".." << pr.second << " origs=" << barr.number_of_originating_curves(e) << "\n";
  }
  CGAL::remove_curve(barr, bh2);
  std::cout << "bez after remove: E=" << barr.number_of_edges() << " F=" << barr.number_of_faces() << "\n";

  ConicArr coarr;
  Conic_traits ctraits;
  auto ctr_cv = ctraits.construct_curve_2_object();
  auto ell = ctr_cv(4, 1, 0, 0, 0, -16);
  auto ch_c = CGAL::insert(coarr, ell);
  CGAL::insert(coarr, ctr_cv(Rat_kernel::Segment_2(Rat_kernel::Point_2(-5,0), Rat_kernel::Point_2(5,0))));
  std::cout << "conic hist: V=" << coarr.number_of_vertices() << " E=" << coarr.number_of_edges() << " F=" << coarr.number_of_faces() << "\n";
  {
    auto approx = ctraits.approximate_2_object();
    std::vector<Conic_traits::Approximate_point_2> pts;
    auto e = coarr.edges_begin();
    approx(e->curve(), 0.01, std::back_inserter(pts), true);
    std::cout << "conic approx pts: " << pts.size() << "\n";
  }

  SphArr sarr;
  GeoTraits gtraits;
  auto ctr_p = gtraits.construct_point_2_object();
  auto ctr_cv2 = gtraits.construct_curve_2_object();
  auto p1 = ctr_p(1,0,0), p2 = ctr_p(0,1,0), p3 = ctr_p(0,0,1);
  auto sh = CGAL::insert(sarr, ctr_cv2(p1, p2));
  CGAL::insert(sarr, ctr_cv2(p2, p3));
  CGAL::insert(sarr, ctr_cv2(p3, p1));
  std::cout << "sph hist: V=" << sarr.number_of_vertices() << " E=" << sarr.number_of_edges() << " F=" << sarr.number_of_faces() << " induced=" << sarr.number_of_induced_edges(sh) << "\n";
  for (auto f = sarr.faces_begin(); f != sarr.faces_end(); ++f) std::cout << "  face outer_ccbs=" << f->number_of_outer_ccbs() << " inner=" << f->number_of_inner_ccbs() << " unbounded=" << f->is_unbounded() << "\n";

  // GPS with Bezier
  {
    typedef CGAL::Gps_traits_2<Bezier_traits> Gps_bez;
    typedef CGAL::General_polygon_set_2<Gps_bez> Bez_set;
    typedef Gps_bez::Polygon_2 BPoly;
    Bezier_traits btraits;
    auto mkx = btraits.make_x_monotone_2_object();
    BPoly poly;
    std::vector<Rat_kernel::Point_2> c1 = { Rat_kernel::Point_2(0,0), Rat_kernel::Point_2(2,4), Rat_kernel::Point_2(4,0) };
    std::vector<Rat_kernel::Point_2> c2 = { Rat_kernel::Point_2(4,0), Rat_kernel::Point_2(0,0) };
    for (auto* cp : {&c1, &c2}) {
      Bezier_traits::Curve_2 bc(cp->begin(), cp->end());
      std::vector<std::variant<Bezier_traits::Point_2, Bezier_traits::X_monotone_curve_2>> objs;
      mkx(bc, std::back_inserter(objs));
      for (auto& o : objs) if (auto* x = std::get_if<Bezier_traits::X_monotone_curve_2>(&o)) poly.push_back(*x);
    }
    Bez_set S; S.insert(poly);
    std::cout << "bezier gps polygons: " << S.number_of_polygons_with_holes() << " arr F=" << S.arrangement().number_of_faces() << "\n";
  }
  // GPS circle segments
  {
    typedef CGAL::Gps_circle_segment_traits_2<Kernel> Gps_circ;
    typedef CGAL::General_polygon_set_2<Gps_circ> Circ_set;
    Gps_circ::Polygon_2 poly;
    Kernel::Circle_2 circ(Kernel::Point_2(0,0), 4);
    CircTraits ct;
    auto mkx = ct.make_x_monotone_2_object();
    std::vector<std::variant<CircTraits::Point_2, CircTraits::X_monotone_curve_2>> objs;
    mkx(CircTraits::Curve_2(circ), std::back_inserter(objs));
    for (auto& o : objs) if (auto* x = std::get_if<CircTraits::X_monotone_curve_2>(&o)) poly.push_back(*x);
    Circ_set S; S.insert(poly);
    Gps_circ::Polygon_2 poly2;
    objs.clear();
    mkx(CircTraits::Curve_2(Kernel::Circle_2(Kernel::Point_2(3,0), 4)), std::back_inserter(objs));
    for (auto& o : objs) if (auto* x = std::get_if<CircTraits::X_monotone_curve_2>(&o)) poly2.push_back(*x);
    S.join(poly2);
    std::cout << "circle gps polygons: " << S.number_of_polygons_with_holes() << " arr F=" << S.arrangement().number_of_faces() << "\n";
  }
  // GPS conic
  {
    typedef CGAL::Gps_traits_2<Conic_traits> Gps_con;
    typedef CGAL::General_polygon_set_2<Gps_con> Con_set;
    Gps_con::Polygon_2 poly;
    auto mkx = ctraits.make_x_monotone_2_object();
    std::vector<std::variant<Conic_traits::Point_2, Conic_traits::X_monotone_curve_2>> objs;
    mkx(ctr_cv(4, 1, 0, 0, 0, -16), std::back_inserter(objs));
    for (auto& o : objs) if (auto* x = std::get_if<Conic_traits::X_monotone_curve_2>(&o)) poly.push_back(*x);
    Con_set S; S.insert(poly);
    std::cout << "conic gps polygons: " << S.number_of_polygons_with_holes() << "\n";
  }
  std::cout << "OK\n";
  return 0;
}
