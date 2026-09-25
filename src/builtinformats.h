#pragma once

#include "core/formatlist.h"

// The formats this application is built with, each with its concrete adapter.
// The single place backends are constructed.
FormatList builtInFormats();
