/* probe.m — in-process probe of the FxPlug principal (Apple's "embedded"
 * path, the same one Apple's InternalFilters use).
 *
 * Runs from inside a bundle whose Contents/Info.plist is a byte-for-byte
 * copy of the shipped plugin Info.plist, so [NSBundle mainBundle] is exactly
 * what the real XPC service sees. Prints:
 *   - whether the ProPlug keys are visible to the framework
 *   - what embeddedPrincipal responds to
 *   - the registered plug-in groups / plug-ins, if queryable
 *
 * Build (from the dot.fx repo root):
 *   cc -fobjc-arc probe.m -o archive/fcpx/test/Probe/Contents/MacOS/probe \
 *      -iframework /Library/Developer/SDKs/FxPlug.sdk/Library/Frameworks \
 *      -F/Library/Developer/Frameworks -framework FxPlug \
 *      -framework PluginManager -rpath /Library/Developer/Frameworks
 */

#import <Foundation/Foundation.h>
#import <FxPlug/FxPlugSDK.h>

static void try_query(id principal, const char *selName, const char *label)
{
    SEL sel = sel_registerName(selName);
    if (![principal respondsToSelector:sel]) {
        return;
    }
    @try {
        NSMethodSignature *sig = [principal methodSignatureForSelector:sel];
        NSInvocation *inv = [NSInvocation invocationWithMethodSignature:sig];
        [inv setTarget:principal];
        [inv setSelector:sel];
        NSError *err = nil;
        [inv setArgument:&err atIndex:2]; /* the NSError** argument */
        [inv invoke];
        id result = nil;
        [inv getReturnValue:&result];
        NSLog(@"%s (%lu entries): %@  err=%@", label,
              (unsigned long)[(NSArray *)result count], result, err);
    } @catch (NSException *ex) {
        NSLog(@"%s EXCEPTION: %@", label, ex);
    }
}

int main(void)
{
    @autoreleasepool {
        NSBundle *main = [NSBundle mainBundle];
        NSDictionary *info = [main infoDictionary];
        NSLog(@"mainBundle: %@", main.bundlePath);
        NSLog(@"ProPlugPlugInList present: %@",
              info[@"ProPlugPlugInList"] != nil ? @"YES" : @"NO");
        NSLog(@"ProPlugPlugInGroupList present: %@",
              info[@"ProPlugPlugInGroupList"] != nil ? @"YES" : @"NO");
        NSLog(@"ProPlugPlugInList raw: %@", info[@"ProPlugPlugInList"]);

        id principal = [FxPrincipal embeddedPrincipal];
        NSLog(@"embeddedPrincipal (no start) = %@", principal);

        if (principal == nil) {
            NSLog(@"trying startServicePrincipal first...");
            [FxPrincipal startServicePrincipal];
            principal = [FxPrincipal embeddedPrincipal];
            NSLog(@"embeddedPrincipal (after start) = %@", principal);
        }

        if (principal == nil) {
            NSLog(@"FAIL: embeddedPrincipal returned nil");
            return 1;
        }

        const char *candidates[] = {
            "registeredPlugInGroupsWithError:",
            "registeredPlugInsWithError:",
            "shouldLoadFirstInstanceOfPlugInWithError:",
            "sharedInstance",
            "principalAPI",
            "plugInWithUUID:",
            "registeredPlugInGroups",
            "registeredPlugIns",
            "availablePlugIns",
            "plugIns",
            "plugInGroups",
            "groups",
        };
        for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]);
             i++) {
            SEL s = sel_registerName(candidates[i]);
            NSLog(@"responds(%s) = %d", candidates[i],
                  (int)[principal respondsToSelector:s]);
        }

        try_query(principal, "registeredPlugInGroupsWithError:", "GROUPS");
        try_query(principal, "registeredPlugInsWithError:", "PLUGS");

        return 0;
    }
}
