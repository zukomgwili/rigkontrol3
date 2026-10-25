// Declaration-only compiler probe. No implementations, registration, or driver.
#include <IOKit/audio/IOAudioDevice.h>
#include <IOKit/audio/IOAudioEngine.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/usb/IOUSBHostDevice.h>
#include <IOKit/usb/IOUSBHostInterface.h>
#include <IOKit/usb/IOUSBHostPipe.h>

class ProbeRetainedDevice : public IOAudioDevice {
    OSDeclareDefaultStructors(ProbeRetainedDevice)
public:
    using IOAudioDevice::newUserClient;
    bool initHardware(IOService *) override;
    IOReturn performPowerStateChange(IOAudioDevicePowerState, IOAudioDevicePowerState, UInt32 *) override;
    IOReturn newUserClient(task_t, void *, UInt32, OSDictionary *, IOUserClient **) override;
};
class ProbeRetainedEngine : public IOAudioEngine {
    OSDeclareDefaultStructors(ProbeRetainedEngine)
public:
    bool initHardware(IOService *) override;
    UInt32 getCurrentSampleFrame() override;
    IOReturn performAudioEngineStart() override;
    IOReturn performAudioEngineStop() override;
    IOReturn clipOutputSamples(const void *, void *, UInt32, UInt32, const IOAudioStreamFormat *, IOAudioStream *) override;
    IOReturn convertInputSamples(const void *, void *, UInt32, UInt32, const IOAudioStreamFormat *, IOAudioStream *) override;
};
class ProbeRetainedUserClient : public IOUserClient {
    OSDeclareDefaultStructors(ProbeRetainedUserClient)
public:
    using IOUserClient::initWithTask;
    bool initWithTask(task_t, void *, UInt32, OSDictionary *) override;
    IOReturn clientClose() override;
    IOExternalMethod *getTargetAndMethodForIndex(IOService **, UInt32) override;
    IOExternalAsyncMethod *getAsyncTargetAndMethodForIndex(IOService **, UInt32) override;
};
using KernelIso = IOReturn (IOUSBHostPipe::*)(IOMemoryDescriptor *, IOUSBHostIsochronousTransaction *, uint32_t, uint64_t, IOOptionBits, IOUSBHostIsochronousTransactionCompletion *);
using KernelBulk = IOReturn (IOUSBHostPipe::*)(IOMemoryDescriptor *, uint32_t, IOUSBHostCompletion *, uint32_t);
using KernelClear = IOReturn (IOUSBHostPipe::*)(bool);
using KernelAlt = IOReturn (IOUSBHostInterface::*)(uint8_t);
using KernelCopyPipe = IOUSBHostPipe *(IOUSBHostInterface::*)(uint8_t);
static_assert(__is_same(decltype(static_cast<KernelIso>(&IOUSBHostPipe::io)), KernelIso), "typed modern ISO overload");
static_assert(__is_same(decltype(static_cast<KernelBulk>(&IOUSBHostPipe::io)), KernelBulk), "typed modern bulk overload");
static_assert(__is_same(decltype(&IOUSBHostPipe::clearStall), KernelClear), "typed controller-only choice");
static_assert(__is_same(decltype(&IOUSBHostInterface::selectAlternateSetting), KernelAlt), "typed alternate setting");
static_assert(__is_same(decltype(&IOUSBHostInterface::copyPipe), KernelCopyPipe), "typed retained pipe lookup");
