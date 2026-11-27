#pragma once

#include "giga_edge/modules/model/interpreter/params.h"

#include <string>
#include <string_view>

namespace giga_edge {

extern const std::string DEFAULT_INTERPPRETER_TYPE;

bool IsInterpreterTypeSupported(const std::string& type);

InterpreterParams::InterpreterType ParseIntepreterType(const std::string& type);

std::string_view InterpreterTypeName(InterpreterParams::InterpreterType type);

} // namespace giga_edge
