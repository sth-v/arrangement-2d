// arr2d — repair of CGAL 6.1's Bezier intersection-parameter pairing.
//
// WHAT IS BROKEN (CGAL 6.1, Arr_geometry_traits/Bezier_cache.h)
// -------------------------------------------------------------
// `_Bezier_cache::get_intersections(id1, ..., id2, ...)` computes, for the two SUPPORTING Bezier
// curves C1 and C2, the list of parameter pairs (s, t) with C1(s) == C2(t).  It does so by
// solving two resultants separately:
//
//     s_vals = roots in [0,1] of Res_t(X2(t)-X1(s), Y2(t)-Y1(s))       (:398)
//     t_vals = ALL real roots  of Res_s(X1(s)-X2(t), Y1(s)-Y2(t))      (:408, find_out_of_range)
//
// and then *pairs them up geometrically* (:466-528).  The pairing assumes a BIJECTION between the
// two lists: for every point of `pts1` it removes ("erases") the matching entry from `pts2`.
// That assumption is false exactly when one of the two curves passes through a point where the
// OTHER one crosses ITSELF: the self-intersecting curve reaches that single geometric point at
// two distinct parameters, so one list is one entry longer than the other.  Two failure modes,
// selected by which curve got the smaller `Curve_2::id()` (= its rep address, so effectively by
// the allocator):
//
//   * the self-intersecting curve is C1 (longer `pts1`): `pts2` runs empty, and
//     `for (k = n_pts2 - 1; !found && k > 0; k--)` with `unsigned int k` (declared :458, loop
//     :488) makes `n_pts2 - 1 == -1` wrap to 4294967295, so `dist_vec[k]` reads far out of
//     bounds of an EMPTY vector.  The garbage `My_point_2` feeds a null `CORE::Expr` rep to
//     `CGAL::compare` -> SIGSEGV (measured ~5 runs out of 6, ASLR-dependent).
//   * the self-intersecting curve is C2 (longer `pts2`): no out-of-bounds access, but the loop
//     stops at the FIRST match and erases it, so the second parameter at which the
//     self-intersecting curve reaches the shared point is never reported.  The list is then
//     silently incomplete and `_Bezier_x_monotone_2::point_position` trips
//     `CGAL_assertion(_is_in_range(t, cache))` (Bezier_x_monotone_2.h:933) — measured,
//     deterministically, with the loop cubic (-1,0)(3,10)(-3,10)(1,0) and the linear Bezier
//     (-2,3)-(2,3) through its crossing point (0,3).
//
// WHAT THIS HEADER DOES
// ---------------------
// It defines an EXPLICIT SPECIALIZATION of that one member function for the one instantiation
// arr2d uses (`Nt_traits == CGAL::CORE_algebraic_number_traits`).  A member function of a class
// template may be explicitly specialized, and such a specialization is a member of the class, so
// it has full access to the private nested types (`My_point_2`, `Distance_iter`, ...) and to the
// private helpers (`_intersection_params`, `_self_intersection_params`, `nt_traits`,
// `intersect_map`) — no copy of the surrounding class and no patched copy of the CGAL header on
// the include path is needed.  [temp.expl.spec]/6 requires the specialization to be declared
// before the first use that would cause an implicit instantiation, which is why
// `kinds/bezier_types.hpp` includes THIS header before `<CGAL/Arr_Bezier_curve_traits_2.h>` and
// is the only place in the project that includes the Bezier traits.
//
// The body is CGAL's, with the pairing loop replaced by a correct MANY-TO-MANY matching:
//
//   * `pts2` is never modified, so `n_pts2` is a constant and can never become 0 mid-loop
//     (the out-of-bounds read is structurally gone; the loop counter is signed as well);
//   * for each point of `pts1` the candidates are sorted by approximate distance and eliminated
//     from the most distant down to the second closest with the exact test `My_point_2::equals`,
//     exactly as CGAL does — but WITHOUT removing anything, so the same t-value may legitimately
//     be paired with several s-values;
//   * if nothing was eliminated as equal, CGAL's conclusion still holds and is still used: all
//     other candidates are provably different points, hence the closest one is the match and no
//     (expensive, positive) exact comparison is needed for it.  This keeps the cost of the
//     overwhelmingly common bijective case identical to CGAL's;
//   * if something WAS found equal, we are in the degenerate configuration; the closest
//     candidate is then tested too and EVERY match is reported, so a point reached by C1 at
//     s_a, s_b and by C2 at t_c yields both (s_a, t_c) and (s_b, t_c).
//
// Every consumer of the list filters by parameter range and therefore wants exactly such a list:
// `_Bezier_x_monotone_2::_exact_intersect` (`if (_is_in_range(iit->s) && cv._is_in_range(iit->t))`,
// Bezier_x_monotone_2.h:2404), `_compare_to_side` (:2232, it only reads the parameter of its own
// curve) and `_Bezier_point_2::make_exact` (bounding-interval test, Bezier_point_2.h:1645).
// `point_position` (Bezier_x_monotone_2.h:925-940) returns on the FIRST entry whose s matches and
// asserts that entry's t to lie in the subcurve's range; with the complete list that assertion is
// no longer reached in any configuration measured here, and with the truncated list it was
// (:933).
//
// WHAT THIS HEADER DOES *NOT* FIX.  The same configuration also makes the shared point carry
// THREE originating x-monotone branches (two of the self-intersecting curve, one of the other),
// and `_Bezier_point_2_rep::_refine()` / `make_exact()` assert `_origs.size() == 2`
// (Bezier_point_2.h:1421 / :1603).  Repairing that means generalising CGAL's simultaneous
// two-curve bound refinement to k curves, so `BezierOps::check_sweepable()` keeps refusing the
// configuration.  This header still matters for the paths that reach the cache anyway — including
// that guard itself, which calls `compare_y_at_x(self-intersection point, other curve)` and so
// goes through `get_intersections` on the very pair it is about to refuse whenever that point is
// not rational — and it removes the memory corruption at its root.
//
// VERSION LOCK: the specialization silently replaces CGAL's definition, so it is compiled only
// for the CGAL 6.1.x releases it was read against; on any other version `ARR2D_BEZIER_CACHE_FIXED`
// is 0, nothing is specialized and CGAL's own definition is used unchanged (the configuration is
// refused either way).
//
// Worth filing upstream; see docs/dev/CGAL_TRAPS_CHECKLIST.md "Bezier kind".
#pragma once

// ORDERING GUARD.  [temp.expl.spec]/6: the specialization below must be declared before anything
// can implicitly instantiate the member.  Every user of _Bezier_cache<CORE_algebraic_number_traits>
// reaches it through one of these three CGAL headers, so if any of them is already included in
// this translation unit we would be silently compiling CGAL's broken definition into the library
// (an ODR violation against the TUs that did get the fix).  Fail loudly instead.
#if defined(CGAL_ARR_BEZIER_CURVE_TRAITS_2_H) || defined(CGAL_BEZIER_POINT_2_H) || \
    defined(CGAL_BEZIER_X_MONOTONE_2_H)
#error "arr2d/impl/bezier_cache_fix.hpp must be included BEFORE CGAL's Bezier traits headers \
(include arr2d/kinds/bezier_types.hpp instead of <CGAL/Arr_Bezier_curve_traits_2.h>)"
#endif

#include <algorithm>
#include <list>
#include <map>
#include <vector>

#include <CGAL/version.h>
#include <CGAL/CORE_algebraic_number_traits.h>
#include <CGAL/number_utils.h>
#include <CGAL/Arr_geometry_traits/Bezier_cache.h>

// CGAL 6.1.x only (read against 1060101000).  Bump deliberately after re-reading the source.
#if CGAL_VERSION_NR >= 1060100000 && CGAL_VERSION_NR < 1060200000
#define ARR2D_BEZIER_CACHE_FIXED 1
#else
#define ARR2D_BEZIER_CACHE_FIXED 0
#endif

#if ARR2D_BEZIER_CACHE_FIXED

namespace CGAL {

template <>
inline const _Bezier_cache<CGAL::CORE_algebraic_number_traits>::Intersection_list&
_Bezier_cache<CGAL::CORE_algebraic_number_traits>::get_intersections
        (const Curve_id& id1,
         const Polynomial& polyX_1, const Integer& normX_1,
         const Polynomial& polyY_1, const Integer& normY_1,
         const Curve_id& id2,
         const Polynomial& polyX_2, const Integer& normX_2,
         const Polynomial& polyY_2, const Integer& normY_2,
         bool& do_ovlp)
{
  CGAL_precondition (id1 <= id2);

  // Construct the pair of curve IDs, and try to find it in the map.
  Curve_pair                curve_pair (id1, id2);
  Intersect_map_iterator    map_iter = intersect_map.find (curve_pair);

  if (map_iter != intersect_map.end())
  {
    // Found in the map: return the cached information.
    do_ovlp = map_iter->second.second;
    return (map_iter->second.first);
  }

  // We need to compute the intersection-parameter pairs.
  Intersection_info&      info = intersect_map[curve_pair];

  // Check if we have to compute a self intersection (a special case),
  // or a regular intersection between two curves.
  if (id1 == id2)
  {
    // ---- unchanged CGAL code: the self-intersection branch is a different, sound algorithm
    // (an explicit double loop over the parameter list, no pairing heuristic at all).
    Parameter_list          s_vals;

    _self_intersection_params (polyX_1, polyY_1, s_vals);

    typename Parameter_list::iterator  s_it;
    typename Parameter_list::iterator  t_it;
    const Algebraic                    one (1);
    const Algebraic&                   denX = nt_traits.convert (normX_1);
    const Algebraic&                   denY = nt_traits.convert (normY_1);

    for (s_it = s_vals.begin(); s_it != s_vals.end(); ++s_it)
    {
      if (CGAL::sign (*s_it) == NEGATIVE) continue;
      if (CGAL::compare (*s_it, one) == LARGER) break;

      const Algebraic&  x = nt_traits.evaluate_at (polyX_1, *s_it);
      const Algebraic&  y = nt_traits.evaluate_at (polyY_1, *s_it);

      for (t_it = s_it; t_it != s_vals.end(); ++t_it)
      {
        if (CGAL::compare (*t_it, one) == LARGER) break;

        if (CGAL::compare (nt_traits.evaluate_at (polyX_1, *t_it), x) == EQUAL &&
            CGAL::compare (nt_traits.evaluate_at (polyY_1, *t_it), y) == EQUAL)
        {
          info.first.push_back (Intersection_point (*s_it, *t_it, x / denX, y / denY));
        }
      }
    }

    info.second = false;
    return (info.first);
  }

  // Compute s-values and t-values such that (X_1(s), Y_1(s)) and
  // (X_2(t), Y_2(t)) are the intersection points.
  Parameter_list          s_vals;

  do_ovlp = _intersection_params (polyX_1, normX_1, polyY_1, normY_1,
                                  polyX_2, normX_2, polyY_2, normY_2,
                                  s_vals);

  if (do_ovlp)
  {
    // Update the cache and return an empty list of intersection parameters.
    info.second = true;
    return (info.first);
  }

  Parameter_list          t_vals;

  do_ovlp = _intersection_params (polyX_2, normX_2, polyY_2, normY_2,
                                  polyX_1, normX_1, polyY_1, normY_1,
                                  t_vals, true);

  CGAL_assertion (! do_ovlp);

  // The s-values are restricted to [0,1]; the t-values are NOT (a point that curve 1 reaches
  // inside its parameter range may be reached by curve 2 outside of it), so every s has a
  // partner t but not the other way round.  Nothing else may be assumed about the two lists:
  // in particular they need NOT have the same length, and a single t may be the partner of
  // several s-values (that is precisely the self-intersection configuration this file exists
  // for).

  typename Parameter_list::iterator  s_it;
  const Algebraic&                   denX_1 = nt_traits.convert (normX_1);
  const Algebraic&                   denY_1 = nt_traits.convert (normY_1);
  Point_list                         pts1;

  for (s_it = s_vals.begin(); s_it != s_vals.end(); ++s_it)
  {
    const Algebraic&  x = nt_traits.evaluate_at (polyX_1, *s_it) / denX_1;
    const Algebraic&  y = nt_traits.evaluate_at (polyY_1, *s_it) / denY_1;

    pts1.push_back (My_point_2 (s_it, x, y));
  }

  typename Parameter_list::iterator  t_it;
  const Algebraic&                   denX_2 = nt_traits.convert (normX_2);
  const Algebraic&                   denY_2 = nt_traits.convert (normY_2);
  Point_list                         pts2;

  for (t_it = t_vals.begin(); t_it != t_vals.end(); ++t_it)
  {
    const Algebraic&  x = nt_traits.evaluate_at (polyX_2, *t_it) / denX_2;
    const Algebraic&  y = nt_traits.evaluate_at (polyY_2, *t_it) / denY_2;

    pts2.push_back (My_point_2 (t_it, x, y));
  }

  // ---- many-to-many matching (this is the repaired part) ---------------------------------
  const bool          x2_simpler = nt_traits.degree(polyX_2) < nt_traits.degree(polyX_1);
  const bool          y2_simpler = nt_traits.degree(polyY_2) < nt_traits.degree(polyY_1);
  const Algebraic     one (1);
  const int           n_pts2 = static_cast<int>(pts2.size());   // CONSTANT: pts2 is never erased

  std::vector<Distance_iter>  dist_vec;
  std::vector<int>            matched;
  dist_vec.reserve (n_pts2 > 0 ? static_cast<std::size_t>(n_pts2) : 0u);

  for (Point_iter pit1 = pts1.begin(); n_pts2 > 0 && pit1 != pts1.end(); ++pit1)
  {
    // Sort every candidate of pts2 by its approximate distance from *pit1.
    dist_vec.clear();
    for (Point_iter pit2 = pts2.begin(); pit2 != pts2.end(); ++pit2)
    {
      const double dx = pit1->app_x - pit2->app_x;
      const double dy = pit1->app_y - pit2->app_y;
      dist_vec.push_back (Distance_iter (dx*dx + dy*dy, pit2));
    }
    std::sort (dist_vec.begin(), dist_vec.end(), Less_distance_iter());

    // Eliminate, from the most distant candidate down to the second closest.  `equals` is an
    // exact algebraic comparison; on a distant candidate it is decided by interval arithmetic
    // and is cheap.  Anything found EQUAL here is a genuine second (or third, ...) parameter
    // at which curve 2 reaches the very same point.
    matched.clear();
    for (int k = n_pts2 - 1; k > 0; --k)
      if (pit1->equals (*dist_vec[k].second)) matched.push_back (k);

    if (matched.empty())
    {
      // All the other candidates are provably different points, so the closest one is the
      // partner of *pit1 and needs no exact test (CGAL's optimisation, still sound).
      matched.push_back (0);
    }
    else
    {
      // Degenerate configuration: test the closest candidate too, and keep every match.
      if (pit1->equals (*dist_vec[0].second)) matched.push_back (0);
      std::sort (matched.begin(), matched.end());
    }

    const Algebraic&  s = pit1->parameter();
    const bool        s_in_range = (CGAL::sign (s) != NEGATIVE &&
                                    CGAL::compare (s, one) != LARGER);

    bool  simplified = false;
    for (std::size_t i = 0; i < matched.size(); ++i)
    {
      Point_iter  pit2 = dist_vec[matched[i]].second;

      // Try to simplify the representation of the intersection point (all the matches carry
      // the same value, so it is done once, from the first of them).
      if (! simplified)
      {
        if (x2_simpler) pit1->x = pit2->x;
        if (y2_simpler) pit1->y = pit2->y;
        simplified = true;
      }

      // Check that s- and t-values both lie in the legal range of [0,1].
      const Algebraic&  t = pit2->parameter();

      if (s_in_range &&
          CGAL::sign (t) != NEGATIVE && CGAL::compare (t, one) != LARGER)
      {
        info.first.push_back (Intersection_point (s, t, pit1->x, pit1->y));
      }
    }
  }

  info.second = false;
  return (info.first);
}

}  // namespace CGAL

#endif  // ARR2D_BEZIER_CACHE_FIXED
