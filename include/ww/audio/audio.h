#pragma once
// =============================================================================
// Wind Waker Static Recompilation - Audio System
//
// Thin wrapper that re-exports gcrecomp's audio API under the ww:: namespace.
// All audio implementation lives in the gcrecomp library (lib/gcrecomp).
// =============================================================================

#include "gcrecomp/audio/audio.h"

namespace ww {
    namespace audio = gcrecomp::audio;
}
