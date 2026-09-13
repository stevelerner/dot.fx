/* main.m — Spacengrave wrapper application.
 *
 * FxPlug 4 plugins register with macOS (PlugInKit) when their wrapper
 * app runs once (Apple: "You only need to launch it once to be
 * registered"). This app does nothing but self-terminate, so it is a
 * fire-and-forget registration launcher: `open Spacengrave.app` or run
 * the executable directly. */

#import <Cocoa/Cocoa.h>

@interface SEAppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation SEAppDelegate
- (void)applicationDidFinishLaunching:(NSNotification *)notification
{
    (void)notification;
    [NSApp terminate:nil];
}
@end

int main(int argc, const char *argv[])
{
    (void)argc; (void)argv;
    @autoreleasepool {
        NSApplication *app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyAccessory];
        SEAppDelegate *delegate = [[SEAppDelegate alloc] init];
        [app setDelegate:delegate];
        [app run];
    }
    return 0;
}
