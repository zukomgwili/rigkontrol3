/* Loads the built CFPlugIn and exercises COM only. Never calls Start, opens
 * CoreAudio devices, installs a bundle, or performs USB I/O. */
#include <CoreFoundation/CoreFoundation.h>
#include <CoreMIDI/MIDIDriver.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef void *(*Factory)(CFAllocatorRef,CFUUIDRef);

int main(int argc,char **argv) {
    assert(argc == 2);
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL,(const UInt8 *)argv[1],(CFIndex)strlen(argv[1]),true);
    assert(url != NULL);
    CFPlugInRef plugin = CFPlugInCreate(NULL,url);
    CFRelease(url);
    assert(plugin != NULL);
    CFBundleRef bundle = CFPlugInGetBundle(plugin);
    assert(CFEqual(CFBundleGetIdentifier(bundle),CFSTR("org.rigkontrol3.ni-arm.midi")));
    void *symbol = CFBundleGetFunctionPointerForName(bundle,CFSTR("RK3MIDIDriverFactory"));
    assert(symbol != NULL);
    Factory factory = NULL;
    _Static_assert(sizeof(factory) == sizeof(symbol),"Darwin function pointer ABI");
    memcpy(&factory,&symbol,sizeof(factory));
    assert(factory(NULL,IUnknownUUID) == NULL);
    MIDIDriverRef driver = factory(NULL,kMIDIDriverTypeID);
    assert(driver != NULL);
    void *other = (void *)(uintptr_t)1;
    assert((*driver)->QueryInterface(driver,CFUUIDGetUUIDBytes(kMIDIDriverInterfaceID),&other) == E_NOINTERFACE);
    assert(other == NULL);
    assert((*driver)->QueryInterface(driver,CFUUIDGetUUIDBytes(kMIDIDriverInterface2ID),&other) == S_OK);
    assert(other == driver && (*driver)->AddRef(driver) == 3);
    assert((*driver)->Release(driver) == 2);
    assert((*driver)->Release(driver) == 1);
    assert((*driver)->QueryInterface(driver,CFUUIDGetUUIDBytes(IUnknownUUID),&other) == S_OK && other == driver);
    assert((*driver)->Release(driver) == 1);
    assert((*driver)->FindDevices(driver,0) == noErr);
    assert((*driver)->Configure(driver,0) == noErr);
    assert((*driver)->Flush(driver,0,NULL,NULL) == noErr);
    assert((*driver)->Stop(driver) == noErr);
    assert((*driver)->Release(driver) == 0);
    CFRelease(plugin);
    puts("RK3 MIDI CFPlugIn: built bundle factory and interface2 COM passed (no Start / no hardware)");
    return 0;
}
