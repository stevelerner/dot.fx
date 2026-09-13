/* fxclient_probe.m — local PROXPC handshake probe (FCP stand-in).
 *
 * Connects to the service's XPC listener by service name (exactly how
 * FCP's PlugInKit client connects: NSXPCConnection with service name
 * com.dotfx.spacengrave.xpc) and calls the PROXPCProtocol entry point
 * newConnectionForProcessID:… that the host is supposed to trigger.
 *
 * Outcomes:
 *   REPLY endpoint=…  → framework is fine; problem is host-side.
 *   REPLY error=…     → framework answered with a diagnosable error.
 *   TIMEOUT           → reproduced the FCP silent-cancel locally.
 *
 * Build: cc -fobjc-arc -Wall -fmodules fxclient_probe.m -o fxclient_probe -framework Foundation
 * Run service first:  "<pluginkit>/Contents/MacOS/Spacengrave XPC Service" &
 */

#import <Foundation/Foundation.h>
#import <XPC/XPC.h>

@protocol FxProbeProtocol <NSObject>
@optional
- (void)newConnectionForProcessID:(pid_t)processID
                   minimumVersion:(NSUInteger)minimumVersion
                   maximumVersion:(NSUInteger)maximumVersion
                 hostCapabilities:(NSDictionary *)hostCapabilities
                            reply:(void (^)(NSXPCListenerEndpoint *endpoint,
                                           NSUInteger xpcVersion,
                                           NSError *error))reply;
- (id)servicePrincipal;
@end

int main(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;
    @autoreleasepool {
        NSString *name =
            @"com.dotfx.spacengrave.xpc";

        NSXPCConnection *conn =
            [NSXPCConnection connectionWithServiceName:name];
        NSXPCInterface *iface = [[NSXPCInterface alloc] init];
        [iface setProtocol:@protocol(FxProbeProtocol)];
        conn.remoteObjectInterface = iface;
        conn.interruptionHandler = ^{
            NSLog(@"PROBE: connection interrupted");
        };
        conn.invalidationHandler = ^{
            NSLog(@"PROBE: connection invalidated");
        };
        [conn resume];
        NSLog(@"PROBE: connection resumed (name=%@)", name);

        __block int fired = 0;
        dispatch_semaphore_t sem = dispatch_semaphore_create(0);
        id proxy = conn.remoteObjectProxy;

        [proxy newConnectionForProcessID:getpid()
                           minimumVersion:1
                           maximumVersion:2
                         hostCapabilities:@{@"probe": @"dotfx"}
                                    reply:^(NSXPCListenerEndpoint *endpoint,
                                           NSUInteger xpcVersion,
                                           NSError *error) {
            fired = 1;
            NSLog(@"PROBE: REPLY endpoint=%@ version=%lu error=%@",
                  endpoint, (unsigned long)xpcVersion, error);
            dispatch_semaphore_signal(sem);
        }];

        long wait =
            dispatch_semaphore_wait(
                sem,
                dispatch_time(DISPATCH_TIME_NOW, 8 * NSEC_PER_SEC));
        if (wait != 0 && !fired) {
            NSLog(@"PROBE: TIMEOUT — no reply from service (reproduces FCP silent-cancel)");
            return 3;
        }
        return 0;
    }
}
