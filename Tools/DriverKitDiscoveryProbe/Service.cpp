// No USB, audio, user client, selectors or NI device-information responses.
#include <DriverKit/IOUserServer.h>
#include "de_caiaq_driver_NIUSBAudioDevice.h"

kern_return_t IMPL(de_caiaq_driver_NIUSBAudioDevice, Start)
{
    kern_return_t result = Start(provider, SUPERDISPATCH);
    if (result != kIOReturnSuccess) return result;
    // Independent probe markers are in this service's Info.plist personality.
    result = RegisterService();
    if (result != kIOReturnSuccess) Stop(provider, SUPERDISPATCH);
    return result;
}

kern_return_t IMPL(de_caiaq_driver_NIUSBAudioDevice, Stop)
{
    return Stop(provider, SUPERDISPATCH);
}
