// arr2d — the type-erased overlay entry point.
//
// This is a thin dispatcher: the real work happens in ArrImpl<Types>::overlay_with(), which is
// instantiated once per kind (impl/arr_impl.hpp).  Keeping the free function in its own TU means
// the Cython layer can call arr2d::overlay() without seeing any CGAL type.
//
// Contract (see arrangement.hpp):
//   * a, b and r must all have the same kind; otherwise Error(KindMismatch) — the kind check is
//     the dynamic_cast inside overlay_with().
//   * r must be an EMPTY arrangement and a distinct object from a and b, else
//     Error(InvalidArgument).  (CGAL itself only has CGAL_precondition(&r != &a && &r != &b) and
//     silently clear()s r; we reject both cases explicitly.)
//   * `fn` may be null; when it is not, it is called once per created result element with the
//     matching OverlayEvent and the (pointer, id) triple of the A, B and R features.
//   * The result records fresh history entries for all input curves of a followed by all of b
//     (CGAL duplicates the Curve_halfedges nodes, so input curve handles do NOT identify result
//     curves — only their position in curves() does).
#include "arr2d/arrangement.hpp"

namespace arr2d {

void overlay(const ArrBase& a, const ArrBase& b, ArrBase& r, void* user, OverlayFn fn) {
  a.overlay_with(b, r, user, fn);
}

}  // namespace arr2d
