#pragma once
// =============================================================================
// Wind Waker Static Recompilation - Hardware Constants
//
// Thin wrapper that re-exports gcrecomp's hardware constants under ww::hw.
// All definitions live in the gcrecomp library (lib/gcrecomp).
// =============================================================================

#include "gcrecomp/hw/gc_hw.h"

namespace ww {
    namespace hw = gcrecomp::hw;
}
