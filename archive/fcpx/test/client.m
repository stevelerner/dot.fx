/* client.m — a "mini FCP": talks to the Spacengrave XPC service exactly the
 * way Final Cut Pro does.
 *
 *  1. Activates the mach service `com.dotfx.spacengrave.xpc`.
 *  2. Performs the PROXPCProtocol handshake (newConnectionForProcessID:...)
 *     that FCP performs on every FxPlug 4 plugin at startup.
 *  3. On the returned endpoint, asks for the registered plug-in groups and
 *     plug-ins (PROPlugInRegistering), i.e. the manifest FCP would display.
 *
 * Debug harness only — not part of the shipped plugin.
 * Build:
 *   cc -fobjc-arc -framework Foundation \
 *      -iframework /Library/Developer/SDKs/FxPlug.sdk/Library/Frameworks \
 *      archive/fcpx/test/client.m -o archive/fcpx/test/client
 */

#import <Foundation/Foundation.h>

#import <PluginManager/PROPlugProtocols.h>
#import <PluginManager/PROPlugInBundleRegistration.h>

int main(void)
{
    @autoreleasepool {
        NSXPCConnection *conn =
            [[NSXPCConnection alloc]
                initWithServiceName:@"com.dotfx.spacengrave.xpc"];
        conn.remoteObjectInterface =
            [NSXPCInterface interfaceWithProtocol:@protocol(PROXPCProtocol)];

        __block BOOL replied = NO;
        conn.invalidationHandler = ^{
            NSLog(@"CONNECTION INVALIDATED (service unavailable to this "
                  "client)");
            if (!replied) {
                exit(3);
            }
        };
        [conn resume];
        NSLog(@"resumed; service activates on first message...");

        id<PROXPCProtocol> principal =
            [conn remoteObjectProxyWithErrorHandler:^(NSError *e) {
                NSLog(@"PROXY ERROR: %@", e);
                if (!replied) {
                    exit(9);
                }
            }];

        [principal newConnectionForProcessID:getpid()
                              minimumVersion:1
                              maximumVersion:4294967295ULL
                            hostCapabilities:@{}
                                       reply:^(NSXPCListenerEndpoint *ep,
                                               NSUInteger v,
                                               NSError *e) {
            if (e != nil) {
                NSLog(@"HANDSHAKE ERROR: %@", e);
                replied = YES;
                exit(4);
            }
            NSLog(@"HANDSHAKE OK: xpcVersion=%lu endpoint=%@",
                  (unsigned long)v, ep);

            NSXPCConnection *c2 =
                [[NSXPCConnection alloc] initWithListenerEndpoint:ep];
            c2.remoteObjectInterface =
                [NSXPCInterface
                    interfaceWithProtocol:@protocol(PROPlugInRegistering)];
            [c2 resume];

            id<PROPlugInRegistering> reg =
                [c2 remoteObjectProxyWithErrorHandler:^(NSError *e) {
                    NSLog(@"C2 PROXY ERROR: %@", e);
                }];
            [NSThread sleepForTimeInterval:1.0];

            NSError *err = nil;
            @try {
                NSArray *groups = [reg registeredPlugInGroupsWithError:&err];
                NSLog(@"GROUPS (%lu): %@  err=%@",
                      (unsigned long)groups.count, groups, err);
            } @catch (NSException *ex) {
                NSLog(@"GROUPS EXCEPTION: %@", ex);
            }
            @try {
                NSArray *plugs = [reg registeredPlugInsWithError:&err];
                NSLog(@"PLUGS (%lu): %@  err=%@",
                      (unsigned long)plugs.count, plugs, err);
            } @catch (NSException *ex) {
                NSLog(@"PLUGS EXCEPTION: %@", ex);
            }

            replied = YES;
            [conn invalidate];
            exit(0);
        }];

        [NSThread sleepForTimeInterval:15.0];
        NSLog(@"TIMEOUT: no handshake reply from the service");
        return 6;
    }
}
