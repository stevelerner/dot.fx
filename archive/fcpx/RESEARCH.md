# FCP Registration Research — Q&A Guide

**Purpose.** `dot.spacengrave` is a Final Cut Pro X effect shipped as an **FxPlug‑4 out‑of‑process (XPC service)** plug‑in. It builds, signs, registers in PlugInKit, and FCP **launches** the service — but "Spacengrave" never appears in the Effects browser, and the one hook we installed on `FxPrincipal.newConnectionForProcessID:...` never fires.

This doc is a **research brief for an AI agent** (or a human). Work through the questions in order, fill in each **Findings** block with concise, *sourced* answers (cite URLs / file paths), then land a **Decision**. Prioritize Q2 → Q3 → Q5 → Q6 first — they determine whether the XPC approach is even the right target before we spend more time on the handshake.

---

## Context (what we know — verified locally)

- **What we built.** C99 core `core/dot.c` (`dot_spacengrave()`) wrapped in a thin ObjC XPC‑service shell under `archive/fcpx/plugin/`. `sh archive/fcpx/build.sh` assembles `archive/fcpx/dist/Spacengrave.app` with `Contents/XPCServices/Spacengrave.xpc` (the FxPlug‑4 service) + embedded `FxPlug.framework`/`PluginManager.framework`, ad‑hoc signed, registered with `pluginkit -a`.
- **Structure matches Apple's public template.** `archive/fcpx/plugin/Info.plist` matches the Xcode `FxPlug 4.xctemplate` service plist key‑for‑key (except the version key, see Q5).
- **Symptom.** FCP launches the service ("plugin loaded and ready for host", RunningBoard identity `xpcservice<com.dotfx.spacengrave.xpc([app<FinalCut...>])>`), but never enumerates the plug‑in and never shows it.
- **Hook result.** `FxPrincipal.newConnectionForProcessID:minimumVersion:maximumVersion:hostCapabilities:reply:` was swizzled and logged as installed, but **FCP never called it.**
- **`pluginkit -m -p FxPlug` (state column):**
  - Ours: `F  com.dotfx.spacengrave.xpc(2.0)`
  - Apple's (works): `H. com.apple.InternalFiltersXPC(2.0)`
  - Third‑party extensions: **blank** leading column; Apple share extensions: `+`.
- **Apple's InternalFiltersXPC `PlugInKit` dict** (Apple‑only keys we cannot use): `FxPlugInternal: true`, `Dedicated: true`, `EmbeddedCode: "Filters.bundle"`, `EmbeddedProtocol: "NSObject"`. Uses attribute key `version` (not `com.apple.version`). PrincipalClass `FxPrincipal`, Protocol `PROXPCProtocol`, Subsystem `NSViewService_PKSubsystem`.
- **FCP internals.** FCP's main binary links **no** FxPlug and has no FxPlug strings. FCP.app bundles its own `FxPlug.framework`, `PluginManager.framework`, `ProAppsFxSupport.framework`, and `FxPlugProvider.fxp` (also present inside `Flexo.framework`). `FxPlugProvider.fxp` **does** link FxPlug/PluginManager.
- **Stable plugin UUIDs — do not change:** group `422118F1-B043-4F5A-BF94-6971B499DC1E`, plugin `0E571DD2-8C31-400F-81DA-3EF91F0D4D68`.
- **Our service Info.plist keys** (`archive/fcpx/plugin/Info.plist`): `PlugInKit.{Attributes.{com.apple.protocol=FxPlug, com.apple.version=2.0}, PrincipalClass=FxPrincipal, Protocol=PROXPCProtocol, Subsystems=[NSViewService_PKSubsystem]}`, `ProPlugDynamicRegistration=false`, one group + one plug‑in (`className=SpacengravePlugIn`, `protocolNames=[FxFilter]`), `XPCService.{ServiceType=Application, JoinExistingSession=true, RunLoopType=_NSApplicationMain}`.

**The blocker, one line:** FCP launches our XPC service but never enumerates the plug‑in — we don't know which method FCP calls, or whether a **third‑party** FxPlug‑4/XPC plug‑in is even a supported path.

---

## Questions

### Q1. What do the `pluginkit -m` leading state characters mean?
- **Known:** Ours=`F`, Apple's FxPlug=`H.`, third‑party extensions=blank, Apple share ext=`+`. The `pluginkit` man page only documents `+ - ! = ?` as *user‑election* states — `F` and `H` are not in that list.
- **Find:** The exact meaning of each state character, especially `F` vs `H`. Is `F` a "failed validation / not eligible / rejected" state? Is `H` "host‑bundled / healthy / hosted"?
- **Why it matters:** If `F` = failed/not‑eligible, FCP ignores us at discovery time and **no fix on the service side will help** — the problem is registration/eligibility, not the XPC handshake.
- **Sources to check:** `pluginkit` open‑source (Apple opensources), `/System/Library/PrivateFrameworks/PlugInKit.framework`, `man pluginkit`.
- **Findings:** `pluginkit(8)` documents only the optional user-election
  prefixes `+ - ! = ?`; it does **not** define `F` or `H`. Crucially, its
  `-a` documentation says that it explicitly adds plug-ins “even if they are
  not normally eligible for automatic discovery.” Our `archive/fcpx/build.sh` does
  exactly that (`pluginkit -a "$XPC"`), so `F` is consistent with a
  force-added/internal registry state, not evidence of failed FxPlug
  validation. `H.` likewise has no public meaning. Treat neither as a host
  eligibility verdict. Source: local `man pluginkit`; [build.sh](build.sh).
  The useful test is discovery of the wrapper-installed XPC without `-a`, not
  an undocumented display prefix.

### Q2. Is third‑party FxPlug‑4 (out‑of‑process XPC) a supported path for FCP effects?
- **Known:** Apple's working FxPlug uses Apple‑only keys (`FxPlugInternal`, `Dedicated`, `EmbeddedCode`, `EmbeddedProtocol`). A public Xcode "FxPlug 4" template exists and our structure matches it.
- **Find:** Apple's **documented** way to ship a *third‑party* FCP effect. Is the XPC (FxPlug‑4) model third‑party‑supported, or Apple‑internal? Do real third‑party FCP effects actually use the classic **in‑process FxPlug‑3 `.pluginkit` bundle** instead?
- **Why it matters:** If XPC is Apple‑internal, our whole approach is the wrong target and we should **pivot to an in‑process bundle** (see Q7).
- **Sources to check:** Apple "Professional Video Applications" docs, FxPlug SDK headers, any third‑party FCP effect documentation.
- **Findings:** **Yes — this is Apple’s documented third-party architecture.**
  Apple says every FxPlug plug-in is contained in an application bundle and
  uses an XPC service to communicate with Final Cut Pro and Motion; FxPlug 4
  plug-ins are fully out-of-process. Its documentation explicitly calls out
  third-party plug-ins, and the installed public FxPlug 4 Xcode template has
  the same two-target wrapper-app + XPC-service layout as ours.
  [Building manually](https://developer.apple.com/documentation/professional-video-applications/building-an-fxplug-plug-in-manually),
  [FxPlug overview](https://developer.apple.com/documentation/professional-video-applications/fxplug),
  [installed template Info.plist](/Library/Developer/Xcode/Templates/FxPlug/FxPlug%204.xctemplate/Plugin/Info.plist).
  The older FxPlug 3 fallback is not an in-process replacement: Apple says it
  was an app wrapper containing an XPC that contained an in-process plug-in,
  and current FCP/Motion require migration to FxPlug 4.
  [Migration guide](https://developer.apple.com/documentation/professional-video-applications/migrating-fxplug-3-plug-ins-to-fxplug-4).

### Q3. What is FCP's `FxPlugProvider.fxp` / `Flexo.framework` / `ProAppsFxSupport.framework`, and which one discovers third‑party FxPlug?
- **Known:** FCP bundles all three; `FxPlugProvider.fxp` links FxPlug/PluginManager and is the likely plugin discovery/load component.
- **Find:** Which component is responsible for discovering/loading third‑party FxPlug plug‑ins, and what registration shape it expects — an **in‑process bundle** or an **XPC service**? What does it query PlugInKit for (protocol, attributes, subsystem)?
- **Why it matters:** This is almost certainly the real enumeration path FCP uses and the one we must satisfy.
- **Sources to check:** `strings`/`otool` on `FxPlugProvider.fxp`, `ProAppsFxSupport.framework`, `Flexo.framework` (targeted, not a whole‑app sweep).
- **Findings:** Publicly, the discovering host is Final Cut Pro via
  PlugInKit: after host launch, PlugInKit reports plug-ins matching that host,
  then the host instantiates the selected plug-in’s XPC.
  [Apple: out-of-process registration](https://developer.apple.com/documentation/professional-video-applications/using-out-of-process-fxplug-plug-ins).
  Locally, `FxPlugProvider.fxp` is the FCP effect-provider component: it links
  FxPlug and PluginManager and contains `registerProPlugIns`,
  `registerEffectClass:forEffectID:withProperties:`, and the `FxFilter` /
  `FxGenerator` markers. See [its Info.plist](/Applications/Final%20Cut%20Pro.app/Contents/Frameworks/FxPlugProvider.fxp/Contents/Info.plist)
  and binary at `/Applications/Final Cut Pro.app/Contents/Frameworks/FxPlugProvider.fxp/Contents/MacOS/FxPlugProvider`.
  That supports (but cannot publicly prove beyond Apple’s ABI) that it is the
  FCP-side adapter over the documented PlugInKit path. The public required
  shape is **the XPC service**, identified by `Protocol=PROXPCProtocol`,
  `PrincipalClass=FxPrincipal`, and `Attributes.com.apple.protocol=FxPlug` —
  not an in-process third-party bundle.

### Q4. What does FCP call on the principal to enumerate, and with what version/capabilities?
- **Known:** Hooked `newConnectionForProcessID:...` (PROXPCProtocol) **never fired.** Other candidates: `createNewConnectionForProcessID:...` (FxXPCProtocol), `viewServiceListenerEndPoint:`, `dynamicRegistrationEndpointForProcessID:version:reply:`.
- **Find:** The exact entry point FCP uses to enumerate a plug‑in's groups/plug‑ins, the version range it sends, and the host capabilities dict.
- **Why it matters:** Distinguishes "wrong entry method" vs "version mismatch" vs "no connection at all." (This is the instrumentation path — only pursue if Q2/Q3 confirm XPC is the right target.)
- **Findings:** The public contract deliberately hides this handshake behind
  `FxPrincipal`; a plug-in must call `[FxPrincipal startServicePrincipal]`.
  The installed SDK declares both host-only entry points:
  `createNewConnectionForProcessID:minimumVersion:maximumVersion:hostCapabilities:reply:` (`FxXPCProtocol`) and
  `newConnectionForProcessID:minimumVersion:maximumVersion:hostCapabilities:reply:` (`PROXPCProtocol`), plus the ViewBridge and dynamic-registration endpoints.
  `FxPrincipal` implements `PROXPCProtocol`; third-party code must not invoke
  or implement these methods. The SDK exposes no documented values for the
  version range or capability dictionary. Sources:
  [PROPlugProtocols.h](/Library/Developer/SDKs/FxPlug.sdk/Library/Frameworks/PluginManager.framework/Versions/B/Headers/PROPlugProtocols.h),
  [FxPrincipalAPI.h](/Library/Developer/SDKs/FxPlug.sdk/Library/Frameworks/FxPlug.framework/Headers/FxPrincipalAPI.h),
  [Apple setup instructions](https://developer.apple.com/documentation/professional-video-applications/building-an-fxplug-plug-in-manually).
  Therefore a swizzle on only `newConnection…` is not a valid registration
  test and broader hooks are diagnostic-only, not the next product fix.

### Q5. Beyond Apple‑only keys, which third‑party‑compatible keys/attributes are we missing?
- **Known:** Ours uses `com.apple.version`; Apple uses `version`. Apple also has `FxPlugInternal/Dedicated/EmbeddedCode/EmbeddedProtocol` (unavailable to us).
- **Find:** The exact `PlugInKit` dict a valid **third‑party** FxPlug‑4 plug‑in needs. Does the `version` vs `com.apple.version` key difference matter? Any required `Attributes`, `Subsystems`, `Protocol`, or `Dedicated` values we lack? Is a `ProPlugProtocolList` (present in Apple's) required?
- **Why it matters:** A missing or mis‑keyed attribute is a plausible direct cause of the `F` state in Q1.
- **Findings:** No required third-party key is missing. Apple’s complete
  public prescription is exactly: `PlugInKit.PrincipalClass=FxPrincipal`,
  `Protocol=PROXPCProtocol`, `Attributes.com.apple.protocol=FxPlug`, and
  `Attributes.com.apple.version=<version>`; static `ProPlugPlugInGroupList` /
  `ProPlugPlugInList` with an `FxFilter` or `FxGenerator` protocol name; and
  the three XPC-service additions. Our plist contains all of these and matches
  the installed FxPlug 4 template key-for-key.
  [Apple plist guide](https://developer.apple.com/documentation/professional-video-applications/editing-property-lists-for-fxplug-plug-ins),
  [template plist](/Library/Developer/Xcode/Templates/FxPlug/FxPlug%204.xctemplate/Plugin/Info.plist),
  [our plist](plugin/Info.plist).
  `com.apple.version`, not `version`, is the documented public key; do not
  copy Apple’s internal `version` attribute. `Dedicated`, `EmbeddedCode`, and
  `EmbeddedProtocol` belong to FCP’s internal legacy-compatible service and
  must not be added. `ProPlugProtocolList` is not required for static FxPlug 4
  registration (the current public template omits it); it is a legacy API
  declaration used by FCP’s internal bundle.

### Q6. Is there any known‑working third‑party FxPlug‑4 (XPC) FCP effect in the wild?
- **Find:** Real examples of third‑party FCP effects shipping the **out‑of‑process XPC** model (vs in‑process). Vendor names, how they install, any public technical write‑ups.
- **Why it matters:** Direct evidence for/against Q2. If every third‑party FCP effect is in‑process, that's strong signal to pivot.
- **Findings:** Yes. Gyroflow Toolbox is a real third-party FxPlug 4 FCP
  effect and is open source. Spectra is another open-source third-party FxPlug
  4 FCP plug-in; it runs out-of-process via XPC with Metal. FCP Cafe links
  both as real-world examples and identifies several other FxPlug vendors.
  [Gyroflow FCP documentation](https://docs.gyroflow.xyz/app/video-editor-plugins/final-cut-pro-x),
  [FCP Cafe FxPlug guide](https://fcp.cafe/developers/fxplug/),
  [Gyroflow source](https://github.com/gyroflow/gyroflow).
  This directly rules out “XPC is Apple-internal” as an explanation.

### Q7. Fallback: what would shipping Spacengrave as an in‑process FxPlug‑3 bundle require?
- **Find:** The structure/registration an in‑process FCP `.pluginkit` bundle needs (Info.plist keys, principal class, load model), and whether our C99 core + ObjC shell can be reused in that form vs the current XPC shell. Effort estimate to pivot.
- **Why it matters:** The concrete pivot option if Q2/Q3 show XPC is Apple‑internal.
- **Findings:** Not a viable fallback. FxPlug 3 is a legacy three-layer design
  (wrapper app → XPC → in-process `.fxplug`); FxPlug 4 removes the innermost
  bundle and places the plug-in class directly in the XPC. Apple directs
  existing FxPlug 3 developers to migrate to FxPlug 4 because current FCP and
  Motion require it. Our C99 core would be reusable, but a pivot would add a
  legacy bridge and make the result less compatible — not solve discovery.
  [Apple migration guide](https://developer.apple.com/documentation/professional-video-applications/migrating-fxplug-3-plug-ins-to-fxplug-4).

---

## Decision
_(Filled 2026-08-31.)_
- [x] **(a)** Fix registration/attributes to satisfy FCP's real discovery path (stays XPC).
- [ ] **(b)** Keep the XPC target but debug the handshake with broader `FxPrincipal` hooks (Q4).
- [ ] **(c)** Pivot to an in-process FxPlug‑3 bundle (Q7).

**Recommendation:** Keep FxPlug 4/XPC. The plist uses the public key set and
the architecture is correct; do **not** add Apple-private keys or rebuild as
FxPlug 3. The remaining evidence points to registration provenance: the
current build forcibly adds an XPC located under `archive/fcpx/dist/` with
`pluginkit -a`, exactly the command Apple says can register an otherwise
ineligible plug-in. Next test: place the signed wrapper app in a normal
application location, register/launch the wrapper normally (without `-a`),
relaunch FCP, then inspect the real PlugInKit record. Only if that succeeds
but the service still gets no `FxPrincipal` connection should Q4 instrumentation
be expanded.

---

## Research constraints / environment notes
- **Web research:** use the `fetch` tool; append `.md` to Apple documentation URLs to get plain text (e.g. `developer.apple.com/documentation/professional-video-applications/...md`).
- **Local inspection:** `otool -L`, `strings`, `plutil -p`, `nm -gU` on *specific* FCP.app paths are fine and fast. **Avoid** broad `find … | otool/strings` sweeps over the entire FCP.app — they are slow and noisy.
- **`make` hangs on this external volume** — never use it; drive `cc`/`sh` directly (see `archive/fcpx/build.sh`).
- **`edit_file` corrupts `README.md` / `HANDOFF.md`** — for those two files only, use Python `str.replace` (assert count==1) or `sed`. This `RESEARCH.md` is safe to edit with `edit_file`.
- **`timeout` does not exist on macOS** — use `sleep`+`kill` or a Python `subprocess` timeout.
- **`read_file` cannot read paths outside the project** — use the terminal (`cat`/`ls`/`plutil`) for system paths (`/Library/Developer`, `/Applications/Final Cut Pro.app`, `/System/Library/PrivateFrameworks`).
