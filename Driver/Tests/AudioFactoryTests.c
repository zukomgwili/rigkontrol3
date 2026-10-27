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
    address.mSelector=kAudioDevicePropertyNominalSampleRate;Float64 rate=0;
    assert((*driver)->GetPropertyData(driver,device,0,&address,0,NULL,sizeof(rate),&written,&rate)==0&&rate==44100);
    const Float64 expected_rates[]={44100,48000,88200,96000,192000};
    address.mSelector=kAudioDevicePropertyAvailableNominalSampleRates;AudioValueRange rates[5];UInt32 size=0;
    assert((*driver)->GetPropertyDataSize(driver,device,0,&address,0,NULL,&size)==0&&size==sizeof(rates));
    assert((*driver)->GetPropertyData(driver,device,0,&address,0,NULL,sizeof(rates),&written,rates)==0&&written==sizeof(rates));
    for(unsigned i=0;i<5;++i)assert(rates[i].mMinimum==expected_rates[i]&&rates[i].mMaximum==expected_rates[i]);
    address.mSelector=kAudioDevicePropertyStreams;AudioObjectID streams[2]={0};
    assert((*driver)->GetPropertyData(driver,device,0,&address,0,NULL,sizeof(streams),&written,streams)==0&&written==sizeof(streams));
    for(unsigned s=0;s<2;++s)for(unsigned p=0;p<2;++p) {
        AudioStreamBasicDescription current={0};
        address.mSelector=p?kAudioStreamPropertyPhysicalFormat:kAudioStreamPropertyVirtualFormat;
        assert((*driver)->GetPropertyData(driver,streams[s],0,&address,0,NULL,sizeof(current),&written,&current)==0&&current.mSampleRate==44100);
        AudioStreamRangedDescription formats[5];
        address.mSelector=p?kAudioStreamPropertyAvailablePhysicalFormats:kAudioStreamPropertyAvailableVirtualFormats;
        assert((*driver)->GetPropertyDataSize(driver,streams[s],0,&address,0,NULL,&size)==0&&size==sizeof(formats));
        assert((*driver)->GetPropertyData(driver,streams[s],0,&address,0,NULL,sizeof(formats),&written,formats)==0&&written==sizeof(formats));
        for(unsigned i=0;i<5;++i)assert(formats[i].mFormat.mSampleRate==expected_rates[i]&&
            formats[i].mSampleRateRange.mMinimum==expected_rates[i]&&formats[i].mSampleRateRange.mMaximum==expected_rates[i]&&
            formats[i].mFormat.mFormatID==kAudioFormatLinearPCM&&formats[i].mFormat.mFormatFlags==kAudioFormatFlagsNativeFloatPacked&&
            formats[i].mFormat.mBytesPerPacket==8&&formats[i].mFormat.mFramesPerPacket==1&&formats[i].mFormat.mBytesPerFrame==8&&
            formats[i].mFormat.mChannelsPerFrame==2&&formats[i].mFormat.mBitsPerChannel==32&&formats[i].mFormat.mReserved==0);
    }
    assert((*driver)->Release(driver)==1);
    CFRelease(plugin);
    puts("Audio CFPlugIn: real ARM bundle factory, COM, 44.1k default and five-rate metadata passed (no Initialize/hardware)");return 0;
}
