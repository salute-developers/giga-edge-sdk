#include "giga_edge/nodes/registration.h"

#include "giga_edge/nodes/all.h" // IWYU pragma: keep

namespace giga_edge {

// The registration itself happens in the static initializers of the included
// headers; the function only has to exist for the linker to pull in this
// translation unit together with them.
void EnsureNodesRegistered() {}

} // namespace giga_edge
