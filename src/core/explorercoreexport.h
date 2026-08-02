#pragma once

#include <QtCore/qglobal.h>

// explorer-core uses CMake's WINDOWS_EXPORT_ALL_SYMBOLS on the producer. The
// import decoration on consumers is still required for stable Qt signal-member
// pointer identity across MinGW DLL boundaries.
#if defined(_WIN32)
#  if defined(EXPLORER_CORE_LIBRARY)
#    define EXPLORER_CORE_EXPORT
#  else
#    define EXPLORER_CORE_EXPORT Q_DECL_IMPORT
#  endif
#else
#  define EXPLORER_CORE_EXPORT
#endif
