// arr2d — kind registry: the only entry points Cython needs to create objects.
#pragma once

#include <memory>
#include <string>

#include "arr2d/common.hpp"
#include "arr2d/ops.hpp"
#include "arr2d/arrangement.hpp"
#include "arr2d/polygon_set.hpp"

namespace arr2d {

struct KindEntry {
  const KindOps* ops = nullptr;
  std::unique_ptr<ArrBase> (*make_arrangement)() = nullptr;
  std::unique_ptr<PolygonSetBase> (*make_polygon_set)() = nullptr;   ///< null if unsupported
};

/// Called by each kind TU (kind_<name>.cpp) from its register function.
void register_kind(Kind k, const KindEntry& entry);

/// Registers every kind compiled into the library. Idempotent. Cython calls it at import.
void init_all_kinds();

bool kind_available(Kind k);
const KindOps& ops(Kind k);                                   ///< throws Error(Unsupported) if not available
std::unique_ptr<ArrBase> make_arrangement(Kind k);
std::unique_ptr<PolygonSetBase> make_polygon_set(Kind k);     ///< throws Error(Unsupported) if the kind has no Boolean ops
bool kind_has_polygon_set(Kind k);

/// Version / build info for diagnostics.
std::string cgal_version();
std::string build_info();   ///< compile flags summary, exact number type name

// Kind TUs register themselves at load time with a static registrar object:
//   namespace { struct Registrar { Registrar() { register_kind(Kind::Segment, KindEntry{...}); } } registrar; }
// so that any subset of kind TUs can be linked (per-kind C++ tests). init_all_kinds() is kept
// as the explicit initialisation entry point (it is a no-op today besides returning).
}  // namespace arr2d
