/* Loads the real bundle factory without initializing hardware. */
#include <CoreAudio/AudioServerPlugIn.h>
#include <CoreFoundation/CFPlugInCOM.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv){
    assert(argc==2);
    CFURLRef url=CFURLCreateFromFileSystemRepresentation(NULL,(const UInt8 *)argv[1],strlen(argv[1]),true);
    assert(url);CFPlugInRef plugin=CFPlugInCreate(NULL,url);CFRelease(url);assert(plugin);
    CFBundleRef bundle=CFPlugInGetBundle(plugin);
    assert(CFEqual(CFBundleGetIdentifier(bundle),CFSTR("org.rigkontrol3.ni-arm.audio")));
    void *symbol=CFBundleGetFunctionPointerForName(bundle,CFSTR("RK3AudioFactory"));assert(symbol);
    void *(*factory)(CFAllocatorRef,CFUUIDRef)=NULL;
    _Static_assert(sizeof(symbol)==sizeof(factory),"Darwin factory pointer ABI");memcpy(&factory,&symbol,sizeof(symbol));
    assert(!factory(NULL,IUnknownUUID));
    AudioServerPlugInDriverRef driver=factory(NULL,kAudioServerPlugInTypeUUID);assert(driver);
    void *other=NULL;
    assert((*driver)->QueryInterface(driver,CFUUIDGetUUIDBytes(kAudioServerPlugInDriverInterfaceUUID),&other)==S_OK&&other==driver);
    assert((*driver)->Release(driver)>=1);
    AudioObjectPropertyAddress address={kAudioPlugInPropertyDeviceList,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
    AudioObjectID device=0;UInt32 written=0;
    assert((*driver)->GetPropertyData(driver,kAudioObjectPlugInObject,0,&address,0,NULL,sizeof(device),&written,&device)==0&&device==2);
    assert((*driver)->Release(driver)==1);
    CFRelease(plugin);
    puts("Audio CFPlugIn: real ARM bundle factory, COM and device metadata passed (no Initialize/hardware)");return 0;
}
