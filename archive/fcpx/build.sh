#!/bin/sh
# build.sh — assemble Spacengrave.app (FxPlug 4 plugin, FCPX/Motion/Compressor).
#
# Layout (mirrors the Xcode FxPlug 4 template's build scripts):
#   dist/Spacengrave.app
#     Contents/MacOS/Spacengrave                          registration-launcher app
#     Contents/PlugIns/Spacengrave XPC Service.pluginkit  the actual plug-in
#       (template's XPC target uses WRAPPER_EXTENSION=pluginkit — the
#       .pluginkit extension is what LaunchServices/PlugInKit auto-discover;
#       a bare .xpc bundle fails lsregister with -10811 and never registers)
#       Contents/MacOS/Spacengrave XPC Service
#       Contents/Frameworks/{FxPlug,PluginManager}.framework
#
# `make` hangs on this volume (see HANDOFF.md) — this script drives cc
# directly. Re-run after any source change; it signs and re-registers.

set -eu

cd "$(dirname "$0")"
ROOT=$(cd ../.. && pwd)   # repo root (script lives in archive/fcpx/)

SDK_FW=/Library/Developer/SDKs/FxPlug.sdk/Library/Frameworks   # headers for compile
RUN_FW=/Library/Developer/Frameworks                           # dylibs for link+embed
LSREGISTER=/System/Library/Frameworks/CoreServices.framework/Versions/A/Frameworks/LaunchServices.framework/Versions/A/Support/lsregister

DIST=dist
APP=$DIST/Spacengrave.app
XPC="$APP/Contents/PlugIns/Spacengrave XPC Service.pluginkit"
EXE="$XPC/Contents/MacOS/Spacengrave XPC Service"

echo "== clean =="
rm -rf "$DIST"
mkdir -p "$APP/Contents/MacOS" \
         "$XPC/Contents/MacOS" \
         "$XPC/Contents/Frameworks"

echo "== compile plug-in (XPC service) =="
# FxPlug/PluginManager use @rpath install names: resolve to the embedded
# copies first, fall back to the system copies.
cc -ObjC -fobjc-arc -fobjc-exceptions -O2 -Wall -Wextra -Werror \
   -I "$ROOT/core" \
   -F "$SDK_FW" \
   -o "$EXE" \
   plugin/SpacengravePlugIn.m plugin/main.m "$ROOT/core/dot.c" \
   -F "$RUN_FW" \
   -rpath @loader_path/../Frameworks \
   -rpath /Library/Developer/Frameworks \
   -framework FxPlug -framework PluginManager \
   -framework Foundation -framework AppKit \
   -framework CoreVideo -framework IOSurface -framework Metal \
   -lm

echo "== compile wrapper app =="
cc -ObjC -fobjc-arc -O2 -Wall -Wextra -Werror \
   -o "$APP/Contents/MacOS/Spacengrave" \
   app/main.m \
   -framework Cocoa

echo "== install plists =="
cp plugin/Info.plist "$XPC/Contents/Info.plist"
cp app/Info.plist "$APP/Contents/Info.plist"

echo "== embed frameworks =="
cp -R "$RUN_FW/FxPlug.framework" "$XPC/Contents/Frameworks/"
cp -R "$RUN_FW/PluginManager.framework" "$XPC/Contents/Frameworks/"

echo "== code sign (real team identity, leaf-first order) =="
# amfid rejects ad-hoc bundles ("adhoc signed or signed by an unknown cert
# chain", -423) and PlugInKit cannot derive a valid designated requirement
# from an ad-hoc signature — so the PROXPC handshake FCP waits on never
# completes (no effect in the browser). A real, non-ad-hoc identity fixes both
# the embedded-framework library-validation rejections and the handshake.
#   - Pick a cert from `security find-identity -v -p codesigning`.
#   - Fallback: SIGN_IDENTITY=- to ad-hoc (local-testing only, currently
#     rejected by amfid — left in so the script still runs on a certless box).
# Sign WITH hardened runtime + secure timestamp: both known-working
# third-party plugins (GyroflowToolbox — MAS/Developer ID — and
# elliotttate/Spectra2 — Developer ID, build script uses
# `--timestamp --options runtime`) are runtime-signed; FCP's PROXPC
# trust layer is the suspected gate for a non-runtime bundle.
# (Apple's internal InternalFiltersXPC is flags=0x0, but it is Apple-signed
# and App Store-entitled — not a valid third-party comparison.)
# Embedded frameworks are signed with the SAME identity (same team), so
# library validation under hardened runtime should pass; if the service
# fails to load FxPlug.framework, add
# com.apple.security.cs.disable-library-validation to the entitlements
# (Spectra2 ships exactly that).
#
# DEFAULT IS THE CONTRAST LABS CERT. The slerner@bitdrift.io cert
# (657X8R55D7) is REVOKED — signing with it makes macOS flag the app as
# malware, fails launchd spawn (RBS 5 / POSIX 163) and quarantines the
# bundle. Verify a cert's status with: spctl -a -vvv -t exec <app>
#   → "CSSMERR_TP_CERT_REVOKED" means do NOT use it.
SIGN_IDENTITY="${SIGN_IDENTITY:-Apple Development: Steven Lerner (JKLLF8FB7M)}"
if [ "$SIGN_IDENTITY" = "-" ]; then
  RUNTIME_ARGS=""
else
  RUNTIME_ARGS="--timestamp --options runtime"
fi
codesign --force --sign "$SIGN_IDENTITY" $RUNTIME_ARGS \
    "$XPC/Contents/Frameworks/FxPlug.framework"
codesign --force --sign "$SIGN_IDENTITY" $RUNTIME_ARGS \
    "$XPC/Contents/Frameworks/PluginManager.framework"
codesign --force --sign "$SIGN_IDENTITY" $RUNTIME_ARGS \
    --entitlements app/entitlements.plist "$XPC"
codesign --force --sign "$SIGN_IDENTITY" $RUNTIME_ARGS \
    --entitlements app/entitlements.plist "$APP"

echo "== register =="
"$LSREGISTER" -f "$APP"
open "$APP"          # one launch = PlugInKit registration, then it exits
sleep 2

echo "== verify registration =="
pluginkit -m -p FxPlug 2>/dev/null | grep -i spacengrave \
    && echo "REGISTERED" \
    || { echo "NOT FOUND in pluginkit yet (FCPX/lsregister may need a host relaunch)"; exit 1; }

echo "== bundle =="
find "$DIST" -maxdepth 6 -not -path "*/.*" | sed "s|$DIST/||" | sort
