// Software-only test carrier. Default/--inspect never submits a system request.
#import <Foundation/Foundation.h>
#import <SystemExtensions/SystemExtensions.h>
#include <stdio.h>
#include <string.h>

static NSString *const HostIdentifier=@"org.rigkontrol3.ni-discovery-probe";
static NSString *const ExtensionIdentifier=@"org.rigkontrol3.ni-discovery-probe.driver";

@interface ProbeRequestDelegate : NSObject <OSSystemExtensionRequestDelegate>
@property(nonatomic) BOOL finished;
@property(nonatomic) int result;
@end
@implementation ProbeRequestDelegate
- (OSSystemExtensionReplacementAction)request:(OSSystemExtensionRequest *)request
    actionForReplacingExtension:(OSSystemExtensionProperties *)existing withExtension:(OSSystemExtensionProperties *)replacement {
    (void)request;(void)existing;(void)replacement;
    fputs("Existing extension found; replacement is cancelled. No discovery result is implied.\n",stderr);
    return OSSystemExtensionReplacementActionCancel;
}
- (void)requestNeedsUserApproval:(OSSystemExtensionRequest *)request {
    (void)request;
    fputs("macOS requires normal user approval. This request has not completed.\n",stderr);
}
- (void)request:(OSSystemExtensionRequest *)request didFinishWithResult:(OSSystemExtensionRequestResult)result {
    (void)request;
    if(result==OSSystemExtensionRequestCompleted) {
        fputs("System extension request completed. Run the separate class-discovery client to test matching.\n",stdout);
        self.result=0;
    } else {
        fputs("System extension request requires a reboot; activation/discovery is not yet proved.\n",stdout);
        self.result=2;
    }
    self.finished=YES;
}
- (void)request:(OSSystemExtensionRequest *)request didFailWithError:(NSError *)error {
    (void)request;fprintf(stderr,"System extension request failed: %s\n",error.description.UTF8String);
    self.result=1;self.finished=YES;
}
@end

int main(int argc,const char *argv[]) {
    @autoreleasepool {
        BOOL inspect=argc==1||(argc==2&&strcmp(argv[1],"--inspect")==0);
        BOOL activate=argc==2&&strcmp(argv[1],"--activate")==0;
        BOOL deactivate=argc==2&&strcmp(argv[1],"--deactivate")==0;
        if(!inspect&&!activate&&!deactivate) {
            fputs("Usage: RK3DiscoveryProbeHost [--inspect|--activate|--deactivate]\n"
                "Default inspection performs no system-extension request. This carrier has no USB/audio/MIDI/NI selectors.\n",stderr);
            return argc==2&&strcmp(argv[1],"--help")==0?0:2;
        }
        NSBundle *host=[NSBundle mainBundle];
        NSURL *extensionURL=[host.bundleURL URLByAppendingPathComponent:
            @"Contents/Library/SystemExtensions/org.rigkontrol3.ni-discovery-probe.driver.dext" isDirectory:YES];
        NSBundle *extension=[NSBundle bundleWithURL:extensionURL];
        BOOL valid=[host.bundleIdentifier isEqualToString:HostIdentifier]&&
            [extension.bundleIdentifier isEqualToString:ExtensionIdentifier];
        if(inspect) {
            NSDictionary *description=@{@"purpose":@"software-only class discovery probe",
                @"host":host.bundleIdentifier?:@"<absent>",@"embedded_extension":extension.bundleIdentifier?:@"<absent>",
                @"extension_path":extensionURL.path?:@"<absent>",@"bundle_layout_valid":@(valid),
                @"request_submitted":@NO,@"limit":@"Bundle inspection is not signing, activation or NI compatibility proof."};
            NSData *data=[NSJSONSerialization dataWithJSONObject:description options:NSJSONWritingPrettyPrinted error:NULL];
            if(data){fwrite(data.bytes,1,data.length,stdout);fputc('\n',stdout);}return valid?0:2;
        }
        if(!valid){fputs("Expected host/embedded probe bundle layout is missing; no request submitted.\n",stderr);return 2;}
        ProbeRequestDelegate *delegate=[ProbeRequestDelegate new];
        OSSystemExtensionRequest *request=activate?
            [OSSystemExtensionRequest activationRequestForExtension:ExtensionIdentifier queue:dispatch_get_main_queue()]:
            [OSSystemExtensionRequest deactivationRequestForExtension:ExtensionIdentifier queue:dispatch_get_main_queue()];
        request.delegate=delegate;
        [[OSSystemExtensionManager sharedManager] submitRequest:request];
        while(!delegate.finished)[[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.25]];
        return delegate.result;
    }
}
