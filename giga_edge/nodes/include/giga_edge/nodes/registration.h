#pragma once

namespace giga_edge {

// Makes sure that every graph node is registered in NodeRegistry.
//
// Nodes register themselves with static initializers in their headers (REGISTER_NODE);
// when linking against the static SDK libraries, the linker drops them as "unused"
// if no translation unit refers to them. Code inside the SDK tree includes
// giga_edge/nodes/all.h, but its headers are not part of the installed SDK.
// Calling this function (before creating a Runtime) pulls in the SDK registration
// unit and solves both problems.
void EnsureNodesRegistered();

} // namespace giga_edge
