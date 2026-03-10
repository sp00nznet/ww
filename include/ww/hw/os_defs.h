#pragma once
// =============================================================================
// Wind Waker Static Recompilation - OS Definitions
//
// Thin wrapper that re-exports gcrecomp's OS definitions under ww::os.
// All definitions live in the gcrecomp library (lib/gcrecomp).
// =============================================================================

#include "gcrecomp/hw/os_defs.h"

namespace ww {
    namespace os = gcrecomp::os;
}
