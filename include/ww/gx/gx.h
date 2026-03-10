#pragma once
// =============================================================================
// Wind Waker Static Recompilation - GX Graphics API
//
// Thin wrapper that re-exports gcrecomp's GX API under the ww:: namespace.
// All GX implementation lives in the gcrecomp library (lib/gcrecomp).
// =============================================================================

#include "gcrecomp/gx/gx.h"

namespace ww {
    namespace gx = gcrecomp::gx;
}
