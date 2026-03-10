#pragma once
// =============================================================================
// Wind Waker Static Recompilation - Input System
//
// Thin wrapper that re-exports gcrecomp's input API under the ww:: namespace.
// All input implementation lives in the gcrecomp library (lib/gcrecomp).
// =============================================================================

#include "gcrecomp/input/input.h"

namespace ww {
    namespace input = gcrecomp::input;
}
