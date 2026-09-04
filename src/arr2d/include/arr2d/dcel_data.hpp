// arr2d — extended-DCEL data stored on every vertex / halfedge / face.
#pragma once
#include <cstdint>
#include "arr2d/common.hpp"

namespace arr2d {

/// Data attached to each DCEL element. `id` is 0 until the owning ArrImpl assigns a unique id
/// (lazily, on first handle creation, or on rescans after global changes). `data` is the
/// Python object exposed as `.data` (nullptr == None).
struct ElementData {
  std::uint64_t id = 0;
  PyRef data;
};
using VData = ElementData;
using HData = ElementData;
using FData = ElementData;

}  // namespace arr2d
