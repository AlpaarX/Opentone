// SPDX-License-Identifier: AGPL-3.0-only
#include "app/UiModule.h"
extern "C" OPENTONE_UI_EXPORT const opentone::UiApi* opentoneGetUiApi()
{
    static const opentone::UiApi incompatible { 999, nullptr, nullptr, nullptr };
    return &incompatible;
}
