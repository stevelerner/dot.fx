/* SpacengravePlugIn.h — FxPlug 4 filter exposing core/dot.c's
 * dot_spacengrave() to Final Cut Pro X / Motion / Compressor.
 * C99 core is the single source of truth; this is a thin ObjC shell. */

#import <Foundation/Foundation.h>
#import <FxPlug/FxPlugSDK.h>

@interface SpacengravePlugIn : NSObject <FxTileableEffect>
@property (assign) id<PROAPIAccessing> apiManager;
@end
