# Zero Browser

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
![Version](https://img.shields.io/badge/version-0.1.2-blue.svg)
![Platform](https://img.shields.io/badge/platform-Windows%20Win32-lightgrey.svg)
![Language](https://img.shields.io/badge/C%2B%2B-17-00599C.svg)

**A Windows browser with a rendering engine written from scratch (C++17 + Win32, no Chromium).**

No Chromium / Blink / Gecko / WebView / iframe, and no embedded browser component such as
Electron / CEF. HTML parsing, CSS selector matching and style computation, box-model layout,
text layout, page painting and the browser shell are **all implemented in this project**.
System components are used strictly as low-level pipes (transport, decode, pixels) — see
[System boundary](#system-boundary).

![Zero Browser home page](docs/screenshots/home.png)

---

## Feature tour

### Static layout, CSS and the browser shell

Multi-tab, address bar, back/forward/reload, wheel scrolling, custom scrollbar and status bar —
all self-drawn. CSS supports tag / class / id / descendant / `>` / comma groups / `*` /
**attribute selectors** / **structural pseudo-classes**; layout supports block flow, inline text
wrapping (including per-character wrapping for CJK), flex row and column, **CSS Grid**,
**`position: relative / absolute / fixed` with `z-index`**, table rows, lists and `<pre>`.

### Images and background images (WIC decodes bytes to pixels)

`<img>` and `background-image` support PNG / JPEG / GIF / BMP, `width`/`height` attributes and
CSS sizes, aspect-ratio preservation, `width:100%`, transparent-PNG compositing and an `alt`
placeholder box for broken images; `background-size` supports
`cover / contain / auto / stretch` and is clipped inside the box.

![Images and background images](docs/screenshots/images.png)

### Video playback (Media Foundation decode)

`<video>` / `<source>` with `autoplay` / `loop` / `muted` / `controls` and explicit sizes.
Click the picture to pause/resume, click the progress bar to seek, click the mute icon to toggle.
**When `autoplay` is absent or playback is paused, the first frame is decoded and used as a
poster frame**, so the element is never a plain black rectangle. The picture, progress bar, time
text and buttons are all drawn by this project.

![Video playback](docs/screenshots/video.png)

### CSS Grid

`grid-template-columns` accepts `px` / `%` / `fr` / `auto` / `repeat()` / `minmax()`,
with `grid-column: span N` (including the `1 / 3` form) and `gap`; items are auto-placed row by row.

![CSS Grid](docs/screenshots/grid.png)

### position and z-index

`relative` offsets from the in-flow position; `absolute` is taken out of flow; `fixed` is
positioned against the viewport and stays put while scrolling. Positioned siblings are painted in
stable `z-index` order, and hit testing shares exactly the same coordinate transform as painting.

![Fixed positioning](docs/screenshots/position.png)

> The screenshot above was taken with `--scroll 780` (scrolled down 780px): the page header has
> scrolled out of the viewport, while the fixed top navigation bar and the fixed box in the
> bottom-right corner stay in place — including the text inside them.

### Form controls

`input` (`text` / `password` / `submit` / `button` / `reset`), `button`, `select` and `textarea`
are painted with a control-like appearance, showing `value` / `placeholder` / `option` text, with
a focus highlight.

![Form controls](docs/screenshots/forms.png)

> Rendering only: typing into a field and submitting a form are not implemented yet
> (see [Not implemented yet](#not-implemented-yet)).

### Real-world pages

After reworking resource loading (parallel fetch + session reuse), CSS parsing (`@media` nested
blocks) and length parsing (`max-content` / `calc()` / `vh` …, which used to collapse to `0` and
made whole pages zero-width), the browser can open real websites and show their content:

| Page type | Result |
| --- | --- |
| A large video-sharing site | Top navigation, channel categories, and video card titles / uploader names / dates are all readable |
| A competitive-programming judge site | The "script sets a cookie then reloads" anti-bot challenge is detected and completed first, after which the banner, countdown and problem-list entries render |

> Both were fetched from the live network, not local mock-ups. The layout is still rougher than a
> mainstream browser, for the reasons listed under [Not implemented yet](#not-implemented-yet):
> there is no JavaScript engine, so anything a script renders (login, playback, lazy-loaded
> thumbnails) is unavailable.

---

## System boundary

This project treats system components purely as low-level pipes and makes no layout or painting
decisions in them:

| System component | How this project uses it | What it does **not** do |
| --- | --- | --- |
| WinHTTP | HTTP/HTTPS transport, redirects, gzip decompression | HTML, CSS, layout, painting |
| Media Foundation | **Decodes** video/audio into frames and PCM | Playback control, timeline, controls, presentation |
| WASAPI | PCM audio output | Decoding, mixing, playback logic |
| WIC | **Decodes bytes into BGRA pixels** for `<img>` / `background-image` | Sizing, scaling, cropping, layout, compositing position |
| GDI | Final pixel and text output, AlphaBlend compositing | Layout decisions, box model, image scaling strategy |

In one sentence: **the system turns bytes into pixels and sound; everything else is built here.**

---

## Build

Requires 32-bit MinGW g++ (developed with `D:\Dev-Cpp\MinGW32`, g++ 10.2).

```bat
cd zero-browser
build.bat
```

Output: `build\zero-browser.exe`

The script links **statically** (`-static -static-libgcc -static-libstdc++`), producing a single
dependency-free executable (~2.7 MB) that needs no extra runtime beyond system DLLs:

```
GDI32 / USER32 / KERNEL32 / msvcrt / ole32 / MSIMG32 / WINHTTP / MFPlat / MFReadWrite
```

Libraries (already in `build.bat`): `gdi32 winhttp mfplat mfreadwrite mfuuid ole32 uuid mmdevapi
strmiids ksuser avrt windowscodecs msimg32`.

> `src\media.cpp` and `src\audio_out.cpp` must be compiled **separately**: MinGW's `ksmedia.h`
> and MF's strmif headers both define `TIMECODE_SAMPLE` / `DDPIXELFORMAT`. See
> [Pitfalls](#pitfalls-read-before-changing-anything).

---

## Run

```bat
build\zero-browser.exe
build\zero-browser.exe http://info.cern.ch/hypertext/WWW/TheProject.html
build\zero-browser.exe https://example.com/
```

- Typing a bare host into the address bar prepends `https://`
- Supported schemes: `browser://home`, `browser://about`, `file:///...`, `data:text/html,...`,
  `http://`, `https://`
- Environment variable `ZB_PROXY=http://host:port` selects an HTTP proxy
- Environment variable `ZB_KEEP_CONSOLE=1` keeps the console window for debugging
- Compile with `-DZB_MEDIA_DEBUG` to enable the media module's stderr log

Local test pages:

```bat
python -m http.server 8765 --directory testpage
```

---

## Diagnostics and evidence

`--shot` is a windowless render diagnostic: it reuses the **real** `Render` / `OnLButtonDown`
code paths, draws the window into a memory DC and saves it as a BMP — so evidence can be
collected even without a desktop window (or where screenshots are restricted).

```bat
:: Screenshot a page and dump the layout tree (doc + screen coordinates, runs, player state)
build\zero-browser.exe --shot --url http://127.0.0.1:8765/video2.html ^
    --out build\shot.bmp --wait 3000 --size 1180x1240 --dump-boxes

:: Scroll to a position first, then screenshot (verifies position:fixed stays put)
build\zero-browser.exe --shot --url http://127.0.0.1:8765/position.html ^
    --out build\scrolled.bmp --scroll 780 --wait 1200 --size 1100x900

:: Deliver real clicks in order (pause the picture, then seek), capturing a second image
build\zero-browser.exe --shot --url http://127.0.0.1:8765/video2.html ^
    --out build\a.bmp --out2 build\b.bmp ^
    --click 512,358 --click 678,521 --wait 1200 --after 600

:: Images / background images / grid / forms
build\zero-browser.exe --shot --url http://127.0.0.1:8765/images.html   --out build\img.bmp --wait 1500 --size 1100x1900
build\zero-browser.exe --shot --url http://127.0.0.1:8765/grid.html     --out build\grid.bmp --wait 1200 --size 1100x1000 --dump-boxes
build\zero-browser.exe --shot --url http://127.0.0.1:8765/forms.html    --out build\forms.bmp --wait 1200 --size 900x760 --dump-boxes

:: Network diagnostics (the URL follows --net-test directly; the third argument is optional
:: and writes the body to a file)
build\zero-browser.exe --net-test http://example.com/
build\zero-browser.exe --net-test https://example.com/ build\page.html

:: Address-bar input regression (goes through the real OnChar / OnKey paths)
::   --set-address "\empty"  clears the address bar
build\zero-browser.exe --shot --url browser://home --out build\addr.bmp --wait 300 ^
    --size 900x600 --set-address "\empty" --focus-address --type "http://a.cn"
::   expected: [address] typed=11 bytes=11 caret=11 hex=68 74 74 70 ... and no crash
build\zero-browser.exe --shot --url browser://home --out build\addr2.bmp --wait 300 ^
    --size 900x600 --set-address "a b" --focus-address --backspace 1
::   expected: [address] backspace=1 bytes=2 caret=2
::   (multi-byte characters are removed as a single code point — see pitfall 22)

:: Cookie round-trip: start the local cookie test server first
python tools\cookie_server.py 8899
:: First request absorbs Set-Cookie, second sends it back to the echo endpoint
:: expected: echo_body=Cookie: auth=abc123; pref=dark  and jar_size=2
build\zero-browser.exe --cookie-test http://127.0.0.1:8899/set-auth http://127.0.0.1:8899/echo
```

Alpha in the BMP is forced to 255 so it can be converted to PNG directly for eyeballing.

Other probes:

```bat
:: Full engine chain: parse -> layout -> media -> paint, printing the playback timeline and the
:: number of non-black pixels in the video picture area
g++ -std=c++17 -O2 -Isrc tools\page_media_probe.cpp src\engine.cpp src\gdi.cpp ^
    src\html.cpp src\network.cpp src\media.cpp src\audio_out.cpp src\image.cpp ^
    -o build\page_media_probe.exe -lgdi32 -lwinhttp -lmfplat -lmfreadwrite ^
    -lmfuuid -lole32 -luuid -lmmdevapi -lstrmiids -lksuser -lavrt ^
    -lwindowscodecs -lmsimg32
build\page_media_probe.exe http://127.0.0.1:8765/video2.html --seconds 4

:: Generate a clip with obvious content (moving colour blocks + frame number + progress bar) so
:: that "black screen" is never mistaken for "playback failed"
g++ -std=c++17 -O2 tools\make_clip.cpp -o build\make_clip.exe ^
    -lmfplat -lmfreadwrite -lmfuuid -lole32 -luuid -lgdi32 -lstrmiids
build\make_clip.exe testpage\anim.wmv 90 640 360 15

:: Inspect the decoded frame buffer directly: size, byte count, alpha histogram, top/middle/bottom
:: row samples (separates "decode problem" from "buffer format problem" from "compositing problem",
:: see pitfall 18)
g++ -std=c++17 -O2 -Isrc tools\frame_probe.cpp src\media.cpp src\audio_out.cpp ^
    src\network.cpp -o build\frame_probe.exe -lwinhttp -lmfplat -lmfreadwrite ^
    -lmfuuid -lole32 -luuid -lmmdevapi -lstrmiids -lksuser -lavrt
build\frame_probe.exe testpage\anim.wmv 2500

:: Check cross-thread visibility of std::atomic<double> with this toolchain (see pitfalls)
g++ -std=c++17 -O2 tools\atomic_double_check.cpp -o build\atomic_check.exe
build\atomic_check.exe 2000

:: Screenshot structure comparison (mine vs another browser's PNG) and pixel statistics
python tools\pixelstat.py build\shot.bmp
python tools\compare_render.py build\mine.bmp other-browser.png --crop-top 78

:: Echo request headers, to verify User-Agent / Accept-Language / Referer
python tools\echo_headers.py 8901
```

### Measured results

- `http://example.com/` and `http://info.cern.ch/hypertext/WWW/TheProject.html` fetch, lay out,
  and follow a relative link when clicked
- Local pages (external `style.css`, HTML entities, CJK spacing, lists, tables, `pre`, flex cards)
  render completely
- Video: `autoplay` starts playback; without `autoplay` the first frame is shown as a poster;
  clicking pauses/resumes; clicking the progress bar seeks (0.00 → 4.80); the mute icon toggles;
  `loop` wraps around after 6s.
  **Pixel-level check**: 12208/12324 non-black pixels in the video picture area in the screenshot;
  `page_media_probe` reports 207107 non-black pixels in the picture area (excluding the control bar)
- Images: PNG / JPEG / GIF / transparent PNG decode and paint, inline images in a flex row align to
  the text baseline, `width:100%` keeps the aspect ratio, broken images show `alt`,
  `background-size: cover / contain` stays clipped inside the box
- Grid: `repeat(3, 1fr)` gives three equal columns; `180px 1fr 1fr` mixes a fixed and equal
  columns; `span 2` spans columns; `auto auto` works
- position: after scrolling 780px the fixed navigation bar and floating box stay at the viewport
  position, with their text following the box
- Forms: `input` / `select` / `textarea` / `button` render with correct sizes and values, and the
  `input[type="text"]` attribute selector matches only text fields (the submit button keeps its
  intrinsic width)
- Cookies: `--cookie-test` prints `echo_body=Cookie: auth=abc123; pref=dark` with `jar_size=2`
- Network: with `ZB_PROXY=http://127.0.0.1:7890` set, `http://example.com` is fetched successfully
- **Structure comparison against another browser** (same local page, same window width, comparing
  row/column content bands with `tools/compare_render.py`): the first content band aligns exactly
  (relative y 22–40 vs 22–40) and later bands differ by about 6px. Before the font-size fix each
  line was 10–12px taller (root cause: `font-size` was converted as points, see pitfall 23)
- Local regression (`--shot` measured `content_height`): images 1861 / grid 467 / position 1200 /
  forms 469 / flexblocks 233 / video2 920 / index 577

> If `https://` fails with `Win32 error 12185` (`ERROR_WINHTTP_CANNOT_CONNECT`), the environment is
> usually blocking TLS/CONNECT — it is not a bug in the browser code. On a normal Windows install
> WinHTTP completes TLS. `ZB_PROXY` can point at a local proxy to work around it.

---

## Implemented

**HTML**
- Tag/attribute parsing, comments, DOCTYPE, character entities (`&amp;` `&lt;` `&#x...`), void elements
- Raw-text handling for `<title>` / `<style>` / `<script>` / `<textarea>`; case-insensitive tag and
  attribute names

**Charset**
- Converts GBK / GB2312 / GB18030 / Big5 / Shift-JIS / EUC-KR / Latin-1 and others to UTF-8 based
  on `Content-Type: charset=` and `<meta charset>`

**CSS**
- Selectors: tag, `.class`, `#id`, descendant (space-separated and `>`, both currently treated as
  descendant), comma groups, `*`
- Attribute selectors: `[attr]`, `[attr=v]`, `[attr^=v]`, `[attr$=v]`, `[attr*=v]`, `[attr~=v]`,
  `[attr|=v]`
- Structural pseudo-classes: `:first-child` `:last-child` `:only-child`
  `:nth-child(an+b|odd|even)` `:nth-last-child` `:first-of-type` `:last-of-type` `:nth-of-type`
  `:root` `:empty`
- State pseudo-classes (`:hover` `:focus` `:active` …) never match, so their rules cannot apply
  permanently
- **CSS custom properties**: `--x: v` definitions and `var(--x[, fallback])` substitution
  (including nesting) — modern pages define colours and spacing with these
- **`@media` / `@supports` nested blocks** are parsed with the desktop-width branch taken (they no
  longer corrupt the whole stylesheet)
- Colours: `#rgb` / `#rrggbb` / `#rgba` / `#rrggbbaa`, `rgb()` / `rgba()`, `hsl()` / `hsla()` and
  named colours; values with alpha are composited through AlphaBlend
- `!important` is stripped from the value; declarations are split respecting parentheses and
  quotes (`url(data:…;base64,…)` no longer breaks a declaration)
- Properties: `display`, `width`/`height`/`max-width`, `margin`/`padding` (shorthand and per side),
  `border`/`border-radius`, `background`/`background-image`/`background-size`/
  **`background-repeat`/`background-position`**, `color`,
  `font-size`/`font-weight` (numeric)/`font-style`/**`font-family`**/`font` shorthand,
  `text-align`, **`line-height` (including `normal`, taken from real font metrics)**,
  `letter-spacing`, `text-transform`, `white-space`, `gap`,
  `flex-direction`/`justify-content`/`align-items`, `grid-template-columns`/`grid-column`,
  `position`/`left`/`top`/`right`/`bottom`/`z-index`, **`box-shadow`**, **`opacity`**,
  `box-sizing`, `overflow`
- Length units: `px` / `%` / `em` / **`rem`** / **`vh`** / **`vw`** / `pt` / `cm` / `mm` / `in` /
  `ch` and **`calc()`** (addition/subtraction plus multiplication/division by a number);
  keywords such as `max-content`/`fit-content` are treated as `auto`

**Layout**
- Block flow and inline text wrapping (per-character wrapping for CJK, word wrapping for Latin)
- **Inline content and block children are interleaved in document order** (inline runs form
  anonymous block boxes, matching browser behaviour)
- Flex row/column, `margin: auto` centring, table rows, list markers, whitespace preserved in `<pre>`
- CSS Grid: `px` / `%` / `fr` / `auto` / `repeat()` / `minmax()`, `grid-column: span N`, `gap`
- `position: relative` / `absolute` / `fixed` with `z-index` layering
- Inline images and form controls take part in line height and align to the text baseline

**Painting**
- Background colour, background image (clipped), borders, rounded corners, text, underline, link
  colour, scaled bitmaps (video frames), control appearance

**Network**
- HTTP/HTTPS (system TLS), automatic redirects, gzip/deflate, timeouts, error pages, `ZB_PROXY`
- Page HTML / external CSS / images / media requests automatically carry cookies and absorb `Set-Cookie`
- Cookie jar: host/path matching, `Secure` / `HttpOnly` / `Max-Age` / `Expires`, same-name overwrite

**Browser shell**
- Multi-tab, address bar, back/forward/reload/home, wheel scrolling, scrollbar, link clicks,
  relative URL resolution, asynchronous load state and a status bar

**Video**
- `<video src>` / `<source src>` with `autoplay` / `loop` / `muted` / `controls` / `width` / `height`
- Autoplay, click-to-pause/resume, click-the-progress-bar to seek, click-to-mute
- The first frame is decoded as a poster frame when paused or when `autoplay` is absent; a play
  badge is drawn in the centre while paused
- Picture, progress bar, time text and buttons are all drawn by this project; only decoding is
  delegated to Media Foundation

**Images**
- `<img>` and `background-image` decoded through WIC: PNG / JPEG / GIF / BMP
- `width`/`height` attributes, CSS sizes, aspect-ratio preservation, `width:100%`
- Transparent PNG compositing; broken images draw an `alt` placeholder box
- `background-size: contain / cover / auto / stretch`, clipped inside the box
- **Lazy-load fallback**: when `src` is a placeholder, `data-src` / `data-original` /
  `data-lazy-src` / `srcset` are tried in order (this is what makes thumbnails appear on sites
  that lazy-load them)
- Downscaling uses HALFTONE for smooth thumbnails; upscaling uses COLORONCOLOR to keep edges crisp

**Forms (rendering only)**
- `input` (`text`/`password`/`submit`/`button`/`reset`), `button`, `select`, `textarea`
- Shows `value` / `placeholder` / first `option` text, with focus highlight and intrinsic sizing

---

## Not implemented yet

- **JavaScript**: there is no JS engine, so `<script>` never runs. SPA pages only show the static
  HTML the server returned; login, playback, infinite scroll and lazy-loaded thumbnails are
  unavailable. The one exception is **anti-bot challenge pages**: the "script sets a cookie then
  reloads" pattern is recognised and emulated
- **Streaming**: HLS / DASH / m3u8 segment fetching is not implemented; only direct media files work
- **Form interaction**: controls render, but typing, focus traversal and form submission (`Enter`,
  `form` submission) are not implemented
- **CSS extras**: `float`, `position: sticky`, `transform`/`transition`/`animation`, `flex-wrap`
  (parsed but not wrapping yet), pseudo-elements `::before`/`::after`
- **Positioning precision**: `absolute` is currently resolved against the **parent's content box**
  rather than the nearest positioned ancestor
- **Image extras**: animated GIFs show only the first frame; `<canvas>`, `<svg>`, `srcset` and
  `object-fit` are not implemented
- **State and storage**: no localStorage, HTTP cache, download manager or bookmark persistence
- **Cookie UI**: a basic jar exists, but there is no UI to inspect or clear it, and no third-party
  cookie or SameSite policy handling

---

## Architecture

```
WinHTTP transport ──> html.cpp parser ──> DOM tree
                                            │
                                CollectStyleRules / DefaultRules
                                            │
                                     ComputeStyle (css.h)
                                            │
                         PopulateBoxes ──> box tree (layout.h)
                                            │
                  LayoutBox: block / inline / flex / grid / position
                                            │
                         PaintBox ──> GdiCanvas (32-bit DIB section)
                                            │
                                BitBlt to the window (self-drawn shell in app.cpp)
```

- Coordinate convention: `screen = viewport origin + document - scroll`; hit testing uses exactly
  the inverse, so painting and clicking share one transform
- `--shot` reuses these real paths and only swaps the window for a memory DC, so its output matches
  what the browser paints

---

## Directory layout

```
zero-browser/
  LICENSE                 MIT license
  build.bat               build script
  src/
    main.cpp              entry point; --shot / --net-test / --cookie-test diagnostics
    app.cpp / app.h       self-drawn shell: tabs, address bar, toolbar, scrolling, clicks, async load
    html.cpp / html.h     HTML tokenizer + tree builder + entity decoding + raw text elements
    css.h                 selector parsing/matching (attribute selectors, pseudo-classes), style and colour parsing
    engine.cpp / engine.h DOM -> box model -> block/inline/flex/grid/position -> paint -> hit testing
    layout.h              Box / TextRun / LinkArea
    gfx.h                 canvas abstraction
    gdi.cpp / gdi.h       self-managed canvas (32-bit DIB section), text and image output
    image.cpp / image.h   WIC decoding of image bytes to BGRA (with alpha premultiply)
    network.cpp / .h      WinHTTP transport: redirects, gzip, charset normalization, cookie jar, proxy
    media.cpp / .h        Media Foundation player (decode, timeline, seek)
    audio_out.cpp / .h    WASAPI audio output
  docs/screenshots/       README screenshots (PNG)
  testpage/               local test pages (images, grid, flex, position, forms, video, external CSS)
  testmedia/              test clips
  tools/                  probes and evidence tools (pixel/timeline/header helpers)
```

---

## Pitfalls (read before changing anything)

1. **The canvas bit depth must be explicit.** `GdiCanvas` used `CreateCompatibleBitmap(dc_, ...)`,
   whose depth follows the DC passed in. With a DC from `CreateCompatibleDC(nullptr)` (a 1x1
   monochrome bitmap selected by default) you get a **1bpp bitmap** and everything is dithered to
   black and white. It now always creates a 32-bit DIB section.
2. **The coordinate transform must be exactly invertible.** The convention is
   `screen = viewport origin + document - scroll`. The painter once used `doc - viewport + scroll`
   (missing the origin, inverted scroll) and ended up `2 * viewport.y` off from hit testing, which
   cropped the top of the page and made the video control bar and links unclickable. Always check
   `PaintBox` and `OnLButtonDown` together.
3. **`Length` defaults to `is_auto = true`, which is only right for width/height.** `margin` and
   `padding` must be explicitly zeroed (use `ZeroLength()`), otherwise every block's horizontal
   margins are treated as `auto` and all fixed-width elements get centred.
4. **Never use `std::atomic<double>` to pass playback position or seek requests.** With 32-bit
   MinGW at `-O2` cross-thread writes were observed to be invisible (seeking had no effect, the
   position stopped updating) while the same code was fine at `-O0`. The playback clock is now a
   plain `double` guarded by a mutex; boolean flags still use `std::atomic<bool>`.
5. **`media.cpp` and `audio_out.cpp` must be compiled separately.** MinGW's `ksmedia.h` and MF's
   strmif headers both define `TIMECODE_SAMPLE` / `DDPIXELFORMAT`. The WASAPI GUIDs are defined
   locally inside `audio_out.cpp` — do not delete them.
6. **`Page` has `unique_ptr` members and a declared destructor**, so the explicit `Page(Page&&)` /
   `operator=` must stay, otherwise `vector<TabState>` will not compile.
7. **`windows.h` must be included before `gfx.h`**, otherwise `DrawText` is affected by the
   `DrawTextA/W` macros and the compiler reports "marked override but does not override".
8. **Whitespace-only text nodes must not create empty lines.** Newlines/indentation between block
   elements in HTML are separate Text nodes; feeding them straight into `TokenizeText` treats
   `\n` as a hard break and emits a row of empty line boxes that push later blocks down (up to
   ~100px per nesting level). `TokenizeText` now only emits a hard break when the node already
   contained a real word, and whitespace-only nodes take no height. Use `--dump-boxes` to read
   live box coordinates — old coordinates become invalid after layout fixes.
9. **Indentation text inside block containers must be handled while collecting inline content.**
   For `.boxed>\n  <img>`, even without a hard break `pending_space` emits a `" "` run after the
   image piece. The rule is: **whitespace between inline elements collapses to one space,
   whitespace at block boundaries is dropped, and trailing whitespace is always dropped.**
10. **Background `cover` / `contain` must be clipped to the box.** `background-size: cover` scales
    the image larger than the box and centres it, so `oy` can be negative; without
    `canvas->Clip(box)` the image bleeds into neighbouring content. Clip before drawing and
    `ResetClip` afterwards.
11. **`WinHttpQueryHeaders` misses multiple `Set-Cookie` headers with this MinGW header.** MinGW's
    `winhttp.h` declares the last parameter as `LPDWORD` (the Windows SDK uses `DWORD`), and
    index-based enumeration returns only the first header. Instead, fetch the whole raw response
    header with `WINHTTP_QUERY_RAW_HEADERS_CRLF` and parse every `set-cookie:` line — only then are
    all cookies captured reliably.
12. **WIC's `InitializeFromMemory` takes `BYTE*`.** Passing `const uint8_t*` fails to compile;
    `const_cast<BYTE*>(data)` is required. WIC only reads those bytes, so the cast is safe.
13. **A block container must interleave inline content and block children in document order.**
    The early implementation laid out all inline content first and all block children afterwards,
    which in a `label`/`input` form pushed **every input to the top of the container** while the
    labels stayed below. Consecutive inline content now forms an anonymous block box that
    interleaves with block children in document order.
14. **Moving a box during positioning must move its inline runs too.** `ApplyPositioning` used to
    update `rect` / `content` only, leaving the text and images at their layout-time coordinates,
    so a `fixed` floating box appeared empty. The whole subtree is now shifted by the `content`
    origin delta (`ShiftBoxSubtree`).
15. **Trailing whitespace after a wide replaced element must be dropped.** Otherwise the space does
    not fit, wraps to a new line, and inherits the line height stretched by the image — adding a
    full image height (a 918×561 image added 561px). The rule lives in `LayoutInlineInto`:
    "a space that does not fit is skipped with `continue`".
16. **Attribute selectors must not be skipped.** Selector parsing used to skip `[...]`, so
    `input[type="text"] { width:320px }` behaved like `input { width:320px }` and stretched the
    submit button to 320px. `[attr]` and all comparison operators are now parsed, and state
    pseudo-classes (`:hover` / `:focus`) are made to never match.
17. **`grid-template-columns` separates tracks with spaces, not commas.** Only `repeat()` /
    `minmax()` arguments use commas; splitting on commas turns `120px 1fr` into a single track
    (one column, every item full width).
18. **Media Foundation's RGB32 is really BGRX, and the X byte is not guaranteed to be 255.**
    Measured: the whole frame had alpha 0. Because this engine composites with **premultiplied
    AlphaBlend**, alpha 0 makes the frame fully transparent — the symptom is a permanently black
    `<video>` while the player state, `has_frame` and frame size all look perfectly normal. The
    alpha byte is now forced to 255 when copying a frame. `tools/frame_probe.cpp` exists to expose
    exactly this (it prints the alpha histogram and top/middle/bottom row samples).
19. **Metrics such as "non-black pixels in the video area" must exclude the control bar.** The
    control bar alone contributes tens of thousands of non-black pixels, so a fully black picture
    still looks like "there is content". `page_media_probe` now only counts the picture area (box
    height minus the 34px control bar).
20. **When adding a `display:grid` branch, do not overwrite the `display:flex` branch.**
    `LayoutBox` dispatches by `display` to `LayoutFlexRow` / `LayoutColumn` / `LayoutGrid` /
    `LayoutBlockFlow`. Inserting the grid branch once replaced the whole flex branch, and every
    flex container (including the card row on the built-in home page) degraded to stacked blocks.
    `testpage/flexblocks.html` is the regression page that guards this.
21. **Never use PowerShell `Get-Content` / `Set-Content` to bulk-edit UTF-8 source files.**
    Windows PowerShell 5.1 decodes BOM-less files using the system ANSI code page: non-ASCII
    comments turn into mojibake, and worse, "the third byte of a character followed by ASCII
    (<0x40)" is consumed as an invalid double-byte pair — so newlines, `<`, `/` and `"` bytes
    vanish, comments swallow the next line of code, string literals lose their closing quote, and
    the compiler reports a pile of unrelated errors. Use an editor or a patch tool, or explicit
    `[System.IO.File]::ReadAllText` (UTF-8) plus `WriteAllBytes`. If it already happened, the
    damage is reversible: decode as the ANSI code page and write back as UTF-8 to remove the
    mojibake, then restore the few dropped bytes (they always look like "<first two bytes of a
    character> + `?`") from context.
22. **The real hazard in UTF-8 helpers is `size_t` underflow, not encoding.** The old code was:
    ```cpp
    if (i > s.size()) i = s.size();   // with an empty string, i is clamped to 0
    size_t j = i - 1;                 // 0 - 1 underflows to SIZE_MAX
    while (j > 0 && ((unsigned char)s[j] & 0xC0) == 0x80) j--;   // s[SIZE_MAX] read
    ```
    That helper runs on **every keystroke** (to snap the caret to a code-point boundary), so
    "typing into an empty address bar crashes" — it walked backwards from `data() - 1` until it hit
    unmapped memory. Fix: guard the empty string and `i == 0` first, and split boundary snapping
    into its own function (`Utf8SnapToBoundary`) instead of reusing `Utf8PrevIndex`. The same fix
    also removed an off-by-one where a character was inserted *before* the last character.
    Regression: `--set-address "\empty" --focus-address --type "http://a.cn"` must print
    `hex=68 74 74 70 ...` and not crash.
23. **CSS `font-size` is in pixels, not points.** `CreateFontW`'s height parameter is the character
    (em) height; the early code used `-MulDiv(font_size, dpi, 72)`, turning 16px into a 21px em at
    96 DPI, so **all text was about 30% larger than a mainstream browser** and line heights were
    inflated (visible as 10–12px extra per line in the structure comparison). The correct form is
    `-MulDiv(font_size, dpi, 96)`, i.e. `-font_size` at standard DPI.
24. **`MFCreateTempFile` returns `E_ACCESSDENIED` (0x80070005) in a restricted environment.**
    The symptom was network video failing to play with only the message "cannot create media cache
    file" — which is why error messages must carry the `HRESULT`. The fix is to fall back to
    writing a cache file with `CreateFileW` and opening it with `MFCreateFile`, deleting it in the
    player's `Close()` (measured: zero files left in the temp directory before and after).
    Note that MinGW's `mfplat` import library has **no** `MFCreateMFByteStreamOnStream` symbol, so
    the in-memory-stream approach fails to link (`undefined reference to ...@8`).

---

## License

This project is released under the **MIT** license — see [LICENSE](LICENSE).

- The project is at an early stage; the goal is to align layout behaviour with production browsers
  as closely as possible. Rendering differences are welcome as issues.
