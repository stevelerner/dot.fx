/* SpacengravePlugIn.m — dot.spacengrave as an FxPlug 4 filter.
 *
 * CPU renderer (Phase 1): expand the BGRA tile to packed RGB888, run
 * dot_spacengrave() from core/dot.c verbatim (byte-identical to the
 * frei0r plugin by construction), compress back to BGRA. The core is
 * time-invariant and stateless, so no frame index, no per-frame state.
 *
 * Full-buffer contract (NeedsFullBuffer): the effect probes +/-pitch/2
 * px around each pixel and computes a whole-frame Otsu split, so it is
 * genuinely non-tileable; the host always sends the full frame.
 *
 * Threading: pluginState/render can be called concurrently — every
 * method uses only local buffers and a fresh NSData (per the API docs). */

#import "SpacengravePlugIn.h"

#import <CoreVideo/CoreVideo.h>

#include "dot.h"

/* FCPX parameter IDs — same order and names as the shipped frei0r
 * plugin (adapter slot RETROFX_EFFECT_DOT_SPACENGRAVE). */
enum {
    PID_SIZE = 1,
    PID_FILL,
    PID_HALFTONE,
    PID_LINE,
    PID_LEVEL,
    PID_GLOW,
    PID_COLOR,
    PID_DIM,
    PID_GATE
};
#define PID_COUNT 9

/* Render state: the 9 parameters as doubles, packed verbatim into the
 * pluginState NSData. */
typedef struct {
    double size, fill, halftone, line, level, glow, color, dim, gate;
} se_state_t;

static NSError *se_error(int code)
{
    return [NSError errorWithDomain:FxPlugErrorDomain
                               code:code
                           userInfo:@{NSLocalizedDescriptionKey:
                                          @"Spacengrave plug-in error"}];
}

@implementation SpacengravePlugIn

/* Diagnostic: plain logging in OUR OWN class (NOT objc swizzling — that
 * pattern trips EDRs). Tells us how far FCP gets in the PROXPC protocol:
 * class-load -> instantiate -> properties -> addParameters -> render. */
+ (void)initialize
{
    if (self == [SpacengravePlugIn class]) {
        NSLog(@"DOTFX-SE +initialize (class loaded)");
    }
}

- (nullable instancetype)initWithAPIManager:(id<PROAPIAccessing>)newApiManager
{
    NSLog(@"DOTFX-SE -initWithAPIManager: apiManager=%@", newApiManager);
    self = [super init];
    if (self != nil) {
        _apiManager = newApiManager;
    }
    return self;
}

/* NeedsFullBuffer: non-tileable by construction (see file header). */
- (BOOL)properties:(NSDictionary **)properties error:(NSError **)error
{
    NSLog(@"DOTFX-SE -properties:error:");
    (void)error;
    *properties = @{
        kFxPropertyKey_MayRemapTime : @NO,
        kFxPropertyKey_PixelTransformSupport : @(kFxPixelTransform_ScaleTranslate),
        kFxPropertyKey_VariesWhenParamsAreStatic : @NO,
        kFxPropertyKey_NeedsFullBuffer : @YES
    };
    return YES;
}

/* The 9 shipped parameters, flat (FCP's dynamic-parameter API is
 * restricted, so no show/hide-by-parameter). */
typedef struct {
    const char *name;
    UInt32 id;
    double def, pmin, pmax, delta;
} se_param_def_t;

static const se_param_def_t SE_PARAMS[PID_COUNT] = {
    { "size",     PID_SIZE,     5,   0, 512, 1 },
    { "fill",     PID_FILL,     45,  0, 100, 1 },
    { "halftone", PID_HALFTONE, 60,  0, 100, 1 },
    { "line",     PID_LINE,     75,  0, 100, 1 },
    { "level",    PID_LEVEL,    100, 0, 200, 1 },
    { "glow",     PID_GLOW,     65,  0, 100, 1 },
    { "color",    PID_COLOR,    360, 0, 360, 1 },
    { "dim",      PID_DIM,      100, 0, 100, 1 },
    { "gate",     PID_GATE,     30,  0, 100, 1 }
};

- (BOOL)addParametersWithError:(NSError **)error
{
    NSLog(@"DOTFX-SE -addParametersWithError:");
    id<FxParameterCreationAPI_v5> paramAPI =
        [_apiManager apiForProtocol:@protocol(FxParameterCreationAPI_v5)];
    if (paramAPI == nil) {
        if (error != NULL)
            *error = se_error(kFxError_APIUnavailable);
        return NO;
    }
    for (size_t i = 0; i < sizeof(SE_PARAMS) / sizeof(SE_PARAMS[0]); i++) {
        const se_param_def_t *d = &SE_PARAMS[i];
        if (![paramAPI addFloatSliderWithName:
                  [NSString stringWithUTF8String:d->name]
                                 parameterID:d->id
                                defaultValue:d->def
                                parameterMin:d->pmin
                                parameterMax:d->pmax
                                   sliderMin:d->pmin
                                   sliderMax:d->pmax
                                       delta:d->delta
                              parameterFlags:kFxParameterFlag_DEFAULT]) {
            if (error != NULL)
                *error = se_error(kFxError_InvalidParameter);
            return NO;
        }
    }
    return YES;
}

- (BOOL)pluginState:(NSData **)pluginState
             atTime:(CMTime)renderTime
            quality:(FxQuality)qualityLevel
              error:(NSError **)error
{
    (void)qualityLevel;
    id<FxParameterRetrievalAPI_v6> get =
        [_apiManager apiForProtocol:@protocol(FxParameterRetrievalAPI_v6)];
    if (get == nil) {
        if (error != NULL)
            *error = se_error(kFxError_APIUnavailable);
        return NO;
    }
    double v[PID_COUNT];
    for (int i = 0; i < PID_COUNT; i++) {
        if (![get getFloatValue:&v[i]
                  fromParameter:(UInt32)(i + 1)
                         atTime:renderTime]) {
            if (error != NULL)
                *error = se_error(kFxError_InvalidParameter);
            return NO;
        }
    }
    se_state_t s = { v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8] };
    *pluginState = [NSData dataWithBytes:&s length:sizeof(s)];
    return (*pluginState != nil);
}

/* Output bounds = input bounds: pure in-place pixel filter. */
- (BOOL)destinationImageRect:(FxRect *)destinationImageRect
                sourceImages:(NSArray<FxImageTile *> *)sourceImages
            destinationImage:(FxImageTile *)destinationImage
                 pluginState:(NSData *)pluginState
                      atTime:(CMTime)renderTime
                       error:(NSError **)outError
{
    NSLog(@"DOTFX-SE -destinationImageRect:... (render path reached)");
    (void)destinationImage; (void)pluginState; (void)renderTime;
    if (sourceImages.count < 1) {
        if (outError != NULL)
            *outError = se_error(kFxError_InvalidParameter);
        return NO;
    }
    *destinationImageRect = sourceImages[0].imagePixelBounds;
    return YES;
}

- (BOOL)sourceTileRect:(FxRect *)sourceTileRect
      sourceImageIndex:(NSUInteger)sourceImageIndex
          sourceImages:(NSArray<FxImageTile *> *)sourceImages
   destinationTileRect:(FxRect)destinationTileRect
      destinationImage:(FxImageTile *)destinationImage
           pluginState:(NSData *)pluginState
                atTime:(CMTime)renderTime
                 error:(NSError **)outError
{
    (void)sourceImageIndex; (void)sourceImages;
    (void)destinationImage; (void)pluginState; (void)renderTime;
    (void)outError;
    *sourceTileRect = destinationTileRect;
    return YES;
}

- (BOOL)renderDestinationImage:(FxImageTile *)destinationImage
                  sourceImages:(NSArray<FxImageTile *> *)sourceImages
                   pluginState:(NSData *)pluginState
                        atTime:(CMTime)renderTime
                         error:(NSError **)outError
{
    (void)renderTime;
    if ((pluginState == nil) ||
        (sourceImages.count < 1) ||
        (sourceImages[0].ioSurface == nil) ||
        (destinationImage.ioSurface == nil)) {
        if (outError != NULL)
            *outError = se_error(kFxError_InvalidParameter);
        return NO;
    }

    se_state_t s;
    [pluginState getBytes:&s length:sizeof(s)];

    IOSurfaceRef inSurf = (__bridge IOSurfaceRef)sourceImages[0].ioSurface;
    IOSurfaceRef outSurf = (__bridge IOSurfaceRef)destinationImage.ioSurface;

    if (IOSurfaceGetPixelFormat(inSurf) != (OSType)kCVPixelFormatType_32BGRA) {
        NSLog(@"Spacengrave: unsupported input pixel format 0x%08lX "
              "(BGRA8 is the supported format)",
              (unsigned long)IOSurfaceGetPixelFormat(inSurf));
        if (outError != NULL)
            *outError = se_error(kFxError_InvalidParameter);
        return NO;
    }

    FxRect tb = destinationImage.tilePixelBounds;
    int w = (int)(tb.right - tb.left);
    int h = (int)(tb.top - tb.bottom);
    if (w < 1 || h < 1) {
        if (outError != NULL)
            *outError = se_error(kFxError_InvalidParameter);
        return NO;
    }

    void *inBase = IOSurfaceGetBaseAddress(inSurf);
    void *outBase = IOSurfaceGetBaseAddress(outSurf);
    size_t inBPR = IOSurfaceGetBytesPerRow(inSurf);
    size_t outBPR = IOSurfaceGetBytesPerRow(outSurf);
    if ((inBase == NULL) || (outBase == NULL) ||
        (inBPR < (size_t)w * 4) || (outBPR < (size_t)w * 4) ||
        (IOSurfaceGetHeight(inSurf) < (unsigned long)h) ||
        (IOSurfaceGetWidth(inSurf) < (unsigned long)w)) {
        NSLog(@"Spacengrave: surface too small for tile %dx%d", w, h);
        if (outError != NULL)
            *outError = se_error(kFxError_InvalidParameter);
        return NO;
    }

    /* IOSurface rows are stored top-to-bottom in memory, so local tile
     * row r (visual top first) maps to surface row r regardless of the
     * rect origin convention. The core operates row-wise with row 0 =
     * visual top — the same orientation as the frei0r renders, so the
     * (x, y) dither and the y%pitch stroke phase come out identical. */
    size_t nb = (size_t)w * (size_t)h * 3;
    uint8_t *rgb = (uint8_t *)malloc(nb);
    if (rgb == NULL) {
        if (outError != NULL)
            *outError = se_error(kFxError_InvalidParameter);
        return NO;
    }

    for (int r = 0; r < h; r++) {
        const uint8_t *inrow =
            (const uint8_t *)inBase + (size_t)r * inBPR + (size_t)tb.left * 4;
        uint8_t *dst = rgb + (size_t)r * (size_t)w * 3;
        for (int x = 0; x < w; x++) {
            dst[x * 3 + 0] = inrow[x * 4 + 2]; /* R */
            dst[x * 3 + 1] = inrow[x * 4 + 1]; /* G */
            dst[x * 3 + 2] = inrow[x * 4 + 0]; /* B */
        }
    }

    dot_spacengrave_params_t p;
    p.size = (int)s.size;
    p.fill = (int)s.fill;
    p.halftone = (int)s.halftone;
    p.line = (int)s.line;
    p.level = (int)s.level;
    p.grain = (int)s.glow; /* adapter name mapping: glow -> grain */
    p.color = (int)s.color;
    p.dim = (int)s.dim;
    p.gate = (int)s.gate;

    dot_spacengrave(rgb, w, h, w * 3, &p, 0);

    for (int r = 0; r < h; r++) {
        uint8_t *ourow =
            (uint8_t *)outBase + (size_t)r * outBPR + (size_t)tb.left * 4;
        const uint8_t *src = rgb + (size_t)r * (size_t)w * 3;
        for (int x = 0; x < w; x++) {
            ourow[x * 4 + 0] = src[x * 3 + 2]; /* B */
            ourow[x * 4 + 1] = src[x * 3 + 1]; /* G */
            ourow[x * 4 + 2] = src[x * 3 + 0]; /* R (alpha untouched) */
        }
    }

    free(rgb);
    return YES;
}

@end
