// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_core/juce_core.h>

#if defined(OPENTONE_SHARED_RUNTIME)
 #define OPENTONE_API JUCE_API
#else
 #define OPENTONE_API
#endif
