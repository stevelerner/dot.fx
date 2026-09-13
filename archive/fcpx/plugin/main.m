/* main.m — FxPlug 4 out-of-process entry point (XPC service).
 *
 * The host launches this executable; FxPrincipal wires up the XPC
 * connection and instantiates SpacengravePlugIn on demand.
 *
 * NOTE: Keep this minimal. Do NOT swizzle ObjC methods here
 * (objc/runtime method_setImplementation) — that pattern is a classic
 * EDR/antivirus false-positive trigger and will get the bundle quarantined
 * before the PROXPC handshake can even start. Observe the handshake the
 * *supported* way: FxPrincipalDelegate (public API), used below.
 */

#import <Foundation/Foundation.h>

#import <FxPlug/FxPlugSDK.h>

/* Diagnostic delegate (public FxPrincipalDelegate API — NOT swizzling).
 * didEstablishConnectionWithHost: fires only after the PROXPC handshake
 * completes, so it tells us definitively whether FCP accepted the
 * connection and agreed on a protocol version. */
@interface DotFxPrincipalDelegate : NSObject <FxPrincipalDelegate>
@end

@implementation DotFxPrincipalDelegate
- (void)didEstablishConnectionWithHost:(NSString *)hostBundleIdentifier
                               version:(NSString *)hostVersionString
{
    NSLog(@"DOTFX-SE didEstablishConnectionWithHost: %@ version=%@",
          hostBundleIdentifier, hostVersionString);
}
@end

/* Keep the delegate alive for the life of the service. */
static __strong id g_dotfxPrincipalDelegate = nil;

int main(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;
    @autoreleasepool {
        g_dotfxPrincipalDelegate =
            [[DotFxPrincipalDelegate alloc] init];
        [FxPrincipal startServicePrincipalWithDelegate:g_dotfxPrincipalDelegate];
    }
    return 0;
}
