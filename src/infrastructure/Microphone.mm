// SPDX-License-Identifier: AGPL-3.0-only
#include "infrastructure/Microphone.h"
#import <AVFoundation/AVFoundation.h>
#import <dispatch/dispatch.h>
namespace opentone
{
void requestMicrophonePermission(std::function<void(bool)> reply)
{
    const auto status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
    if (status != AVAuthorizationStatusNotDetermined)
    { reply(status == AVAuthorizationStatusAuthorized); return; }
    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL granted)
    {
        dispatch_async(dispatch_get_main_queue(), ^{ reply(granted == YES); });
    }];
}
}
