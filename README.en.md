# Zero Browser

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
![Version](https://img.shields.io/badge/version-0.1.7-blue.svg)
![Platform](https://img.shields.io/badge/platform-Windows%20Win32-lightgrey.svg)
![Language](https://img.shields.io/badge/C%2B%2B-17-00599C.svg)

**[中文](README.md) | English**

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
all self-drawn. The address bar is a **self-implemented single-line edit box**: caret placement,
deletion by code point, the selection range (drag-select / `Shift`+arrows / `Ctrl+A` /
double-click select-all) and the clipboard (`Ctrl+C/X/V`, `Shift+Insert`, context menu) all live
inside the self-drawn shell.
CSS supports tag / class / id / descendant / `>` / comma groups / `*` /
**attribute selectors** / **structural pseudo-classes**; layout supports block flow, inline text
wrapping (including per-character wrapping for CJK), flex row and column (including the default
`align-items: stretch`), **CSS Grid**, **`float` / `clear` columns**,
**`position: relative / absolute / fixed` with `z-index`**, table rows, lists and `<pre>`.
Font sizes follow the CSS rules for `px` / `%` / `em` / `rem` (the `rem` base tracks `html`'s
`font-size`).

### Settings and interface language

The gear button in the toolbar opens `browser://settings` — that page is laid out by the
self-built renderer like any other page. The interface language switches between
**follow system / 中文 / English**; the choice is written to
`%APPDATA%\ZeroBrowser\settings.ini` and survives a restart. The status bar, the context menu,
the error pages, all five built-in pages and `navigator.language` follow it.

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

### JavaScript (self-built ES5-subset interpreter)

**No third-party engine (V8 / QuickJS / Duktape) is linked in** — the interpreter is implemented
here, split across these files:

| File | Responsibility |
| --- | --- |
| `src/js.cpp` | lexing and parsing, producing the syntax tree |
| `src/js_eval.cpp` | evaluator: statements/expressions, scopes, calls and the step budget |
| `src/js_builtins.cpp` | built-in method table (dispatched by receiver type) |
| `src/js_globals.cpp` | global objects such as `Math` / `JSON` / `Date` / `Error` / `console` |
| `src/js_dom.cpp` | DOM bindings: `document` / elements / `window` / `location` / events / timers |
| `src/js.h`, `src/js_internal.h` | public interface and interpreter-internal interface |

**Language features**: `var` / `let` / `const` (**no block scoping** — `let`/`const` behave like
`var`), function declarations / function expressions / arrow functions, closures, recursion,
variable and function hoisting, object and array literals, computed property names, template
strings (**no `${}` interpolation** — that reports a clear error), `for` / `for-in` / `for-of` /
`while` / `do-while`, `switch` / `case` / `default`, `try` / `catch` / `finally` / `throw`,
`break` / `continue`, `++` / `--` / compound assignment, the conditional operator,
`typeof` / `instanceof` / `in` / `delete` / `void`, bitwise operators, loose and strict equality.

**Built-in objects**:

| Object | Implemented |
| --- | --- |
| `Object` | `keys` / `values` / `entries` / `assign` / `create` / `defineProperty` / `freeze` |
| `Array` | `push` / `pop` / `shift` / `unshift` / `slice` / `splice` / `concat` / `join` / `indexOf` / `lastIndexOf` / `includes` / `forEach` / `map` / `filter` / `some` / `every` / `find` / `findIndex` / `reduce` / `sort` / `reverse` / `toString`, statics `isArray` / `from` / `of` |
| `String` | `charAt` / `charCodeAt` / `indexOf` / `lastIndexOf` / `includes` / `startsWith` / `endsWith` / `slice` / `substring` / `substr` / `toUpperCase` / `toLowerCase` / `trim` / `trimStart` / `trimEnd` / `split` / `replace` / `replaceAll` / `concat` / `repeat` / `padStart` / `padEnd` / `localeCompare`, static `fromCharCode` |
| `Number` | `toFixed` / `toString(radix)` / `isNaN` / `isFinite` |
| `Math` | `abs` / `floor` / `ceil` / `round` / `sqrt` / `pow` / `max` / `min` / `random` / `sin` / `cos` / `tan` / `log` / `exp` / `sign` / `trunc` plus `PI` / `E` / `LN2` |
| `JSON` | `parse` / `stringify` (**errors on circular references** instead of recursing forever) |
| `Date` | `now` / `parse` and `getTime` / `getFullYear` / `getMonth` / `getDate` / `getHours` / `getMinutes` / `getSeconds` / `getDay` / `toISOString` / `toString` |
| `Error` | `Error` / `TypeError` / `RangeError` / `ReferenceError` / `SyntaxError` |
| Global functions | `parseInt` / `parseFloat` / `isNaN` / `isFinite` / `String` / `Number` / `Boolean` / `Array` / `Object` / `Function` / `encodeURI(Component)` / `decodeURI(Component)` |
| Output | `console.log` / `info` / `warn` / `error` / `debug`, `alert` (**written to the script log**, no system dialog box) |

**DOM bindings**:

- `document`: `getElementById` / `getElementsByTagName` / `getElementsByClassName` /
  `querySelector` / `querySelectorAll` / `createElement` / `createTextNode` /
  `write` / `writeln` / `body` / `head` / `documentElement` / `title` / `readyState` /
  `URL` / `cookie` (**read-only**) / `addEventListener`
- Elements: `tagName` / `id` / `className` / `classList.add / remove / toggle / contains` /
  `style.xxx` and `style.cssText` / `style.setProperty` / `innerHTML` / `outerHTML` /
  `textContent` / `innerText` / `children` / `childNodes` / `parentNode` /
  `nextSibling` / `previousSibling` / `firstChild` / `lastChild` /
  `appendChild` / `insertBefore` / `removeChild` / `replaceChild` /
  `setAttribute` / `getAttribute` / `removeAttribute` / `hasAttribute` /
  `getBoundingClientRect` / `offsetWidth` / `offsetHeight` / `offsetTop` / `offsetLeft` /
  `value` / `checked` / `href` / `src` / `click` / `addEventListener` / `querySelector…`
- `window`: `document` / `location` / `innerWidth` / `innerHeight` / `scrollY` /
  `scrollTo` / `addEventListener` / `getComputedStyle` (returns `undefined`)
- `location`: `href` / `pathname` / `search` / `hash` / `host` / `hostname` / `protocol` /
  `origin` / `assign` / `replace` / `reload`

**Events**: both `addEventListener` and inline attributes (`onclick="..."`) are supported, and
events **bubble** to `document` and `window`; an event object exposes `type` / `target` /
`currentTarget` / `clientX` / `clientY` / `preventDefault()` / `stopPropagation()` /
`defaultPrevented`; a handler returning `false` is equivalent to `preventDefault`; the default
action of clicking an `<a href>` navigates (unless it is prevented).
The shell hands real clicks to the script: hit testing **maps the runs / boxes of the layout tree
back to DOM nodes** (the self-drawn `button` / `input` controls are runs, not Boxes — see pitfall
33).

**Timers**: `setTimeout` / `setInterval` / `clearTimeout` / `clearInterval` /
`requestAnimationFrame`, driven by the shell's **60ms heartbeat** (`WM_TIMER`) and **capped at
512**.

**Script execution**: inline `<script>` and external `<script src>` both run; **an external
script's body is fetched in parallel with images by the navigation thread and never blocks the
UI**; scripts run in document order, after the first layout completes, followed by `load` /
`DOMContentLoaded` dispatch.

**Safety and isolation** (the most important design decision in this layer):

- every script runs **on its own** — exceptions are only logged, **never rethrown**, and never
  interrupt rendering
- **infinite loops** are cut off by the step budget (**20 million steps per script**)
- **infinite recursion** is stopped by the call-depth guard (**200 frames** plus an 8MB thread
  stack)
- `null.x`, calling a non-function and syntax errors only affect that one script; the page keeps
  rendering
- `JSON.stringify` errors on circular references instead of recursing forever

> This layer only aims at the script coverage real sites need; it does not chase the full language
> specification. The gaps in the language, built-in objects, DOM and global objects are listed item
> by item under [Not implemented yet](#not-implemented-yet).

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
a focus highlight. `input[type="hidden"]` takes no layout space, as in a real browser, so the
CSRF hidden fields that every login page carries no longer push the page around.

![Form controls](docs/screenshots/forms.png)

**Interaction** (self-drawn, no system `EDIT` control):

- Clicking a control gives it the keyboard focus; page focus and the address bar are exclusive,
  and `Esc` drops the focus.
- Typing, `Backspace` / `Delete` and `←` / `→` / `Home` / `End` edit the text by **code point**.
- `Tab` / `Shift+Tab` walk the focusable controls in DOM order and wrap around.
- `Enter` (or a click on `input[type=submit]` / `<button>`) submits the owning `<form>`:
  `GET` builds a query string, `POST` sends a request body
  (`application/x-www-form-urlencoded`). Fields are encoded in **document order**, hidden
  fields are included, the activated submit button is appended last, and unchecked
  `checkbox` / `radio` controls are skipped. `<button type="button">` does not submit.

> `select` shows the current option only — there is **no drop-down list** yet — and a click
> inside a control still just puts the caret at the end of the text (use `Home` / `End` /
> the arrow keys to move it).

### Real-world pages

After reworking resource loading (parallel fetch + session reuse), CSS parsing (`@media` nested
blocks) and length parsing (`max-content` / `calc()` / `vh` …, which used to collapse to `0` and
made whole pages zero-width), plus **`float` columns** and **`rem`/`em` font sizes**, the browser
can open real websites:

| Page type | Result |
| --- | --- |
| A large video-sharing site | Top navigation, a four-column channel grid, and video titles / uploader names / dates are all readable |
| A competitive-programming judge site | The "script sets a cookie then reloads" anti-bot challenge is detected and completed first, after which the banner, countdown, problem-jump box and the two-column contest / announcement sections render |

> **That judge site ships two UIs, and it matters which one you see.** Its HTML contains both an
> empty `<div id="app"></div>` and a `<div id="app-old">` holding the old server-rendered markup
> (an Amaze UI grid). What a mainstream browser shows is the **new UI that Vue 3 renders into
> `#app`** — that needs a working 235 KB webpack bundle, which this project's self-built ES5 subset
> interpreter cannot run (no `Promise`, no modules, no regex literals). So this browser shows the
> **old `#app-old` markup**. That is a JavaScript capability boundary, not a layout bug.
>
> That old markup now lays out **correctly**: the framework's `.am-u-md-*` grid columns are laid
> out with `float: left`, and before `float` existed every column degenerated into a stacked block
> flow, squeezing the whole page into one narrow strip.

> Both were fetched from the live network, not local mock-ups. The layout is still rougher than a
> mainstream browser, for the reasons listed under [Not implemented yet](#not-implemented-yet):
> the script subset has limited coverage and there is no `fetch` / `XHR`, so interactions that lean
> on APIs or on the full language (login, playback, infinite scroll) are still unavailable.
>
> The two screenshots above are evidence from **0.1.3 and earlier** (before JS support existed).
> From 0.1.4 on, `<script>` really executes — see the
> [JavaScript section](#javascript-self-built-es5-subset-interpreter) above.

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

Requires a MinGW that ships `g++` (the development machine uses the one bundled with
Embarcadero Dev-Cpp at `C:\Program Files (x86)\Embarcadero\Dev-Cpp\TDM-GCC-64`, TDM-GCC 9.2).
`build.bat` prefers the `g++` on `PATH` and otherwise adds a few known install locations.

```bat
cd zero-browser
build.bat
```

One run produces **both architectures**:

```
build\zero-browser.exe       32-bit (x86, PE32)   ~3.8 MB
build\zero-browser-x64.exe   64-bit (x64, PE32+)  ~4.5 MB
```

The script links **statically** (`-static -static-libgcc -static-libstdc++`), producing a single
dependency-free executable (size varies with the toolchain) that needs no extra runtime beyond
system DLLs:

```
GDI32 / USER32 / KERNEL32 / msvcrt / ole32 / MSIMG32 / WINHTTP / MFPlat / MFReadWrite
```

Libraries (already in `build.bat`): `gdi32 winhttp mfplat mfuuid ole32 uuid strmiids ksuser
avrt windowscodecs msimg32`, plus an import library for `mfreadwrite`.

> Three toolchain differences worth knowing:
> - **The `mfreadwrite` import library is not always installed with MinGW.** This project uses
>   exactly one export from it, `MFCreateSourceReaderFromByteStream`, so `build.bat` probes with
>   `g++ --print-file-name=libmfreadwrite.a`; TDM-GCC's multilib ships the header but not the
>   `.a`, so it falls back to the same-named import library from the Windows SDK
>   (`Windows Kits\10\Lib\<version>\um\<arch>\`).
> - `-D_WIN32_WINNT=0x0601`: older MinGW headers default lower, and `GetTickCount64` is then
>   not declared at all.
> - Stale `zero-browser*.exe` processes are killed before linking; while one is alive the link
>   fails with `cannot open output file ...: Permission denied`.

> `src\media.cpp` and `src\audio_out.cpp` must be compiled **separately**: MinGW's `ksmedia.h`
> and MF's strmif headers both define `TIMECODE_SAMPLE` / `DDPIXELFORMAT`. See
> [Pitfalls I hit](#pitfalls-i-hit).

---

## Run

```bat
build\zero-browser.exe
build\zero-browser.exe http://info.cern.ch/hypertext/WWW/TheProject.html
build\zero-browser.exe https://example.com/
```

- Typing a bare host into the address bar prepends `https://`
- Supported schemes: `browser://home`, `browser://settings`, `browser://about`,
  `about:parser`, `about:css`, `file:///...`, `data:text/html,...`, `http://`, `https://`
- Environment variable `ZB_PROXY=http://host:port` selects an HTTP proxy
- Environment variable `ZB_KEEP_CONSOLE=1` keeps the console window for debugging
- Environment variable `ZB_SETTINGS=path` moves the settings file somewhere else (used by the
  automated tests so they never touch real user configuration)
- Compile with `-DZB_MEDIA_DEBUG` to enable the media module's stderr log

### Settings and interface language

The **gear** button in the toolbar (or typing `browser://settings`) opens the settings page.
Like every other built-in page, it is drawn by **this project's own rendering engine**:

- Interface language: **follow system / 中文 / English**, with the active one highlighted and
  marked "(current)"
- The footer shows the **language actually in effect** and the **path of the config file**, so
  there is no guessing where a setting went

Settings live in `%APPDATA%\ZeroBrowser\settings.ini` and survive a restart. The status bar, the
address-bar context menu, the tab fallback title, the error pages and all five built-in pages
(home / parser / CSS / about / settings) follow the language, as does `navigator.language`
(`zh-CN` or `en-US`).

> All built-in page text lives in the string table in `src/i18n.cpp`, and the pages themselves
> are templates with `{{key}}` placeholders — each page has one copy of its markup, so there is
> no "changed the Chinese, forgot the English". When `T(key)` finds no entry it returns **the key
> itself**, so a missed translation shows up as `settings.xxx` instead of silently rendering
> blank (blank is invisible to a screenshot regression).

> Switching the language repaints the current page only; other already-open **built-in** tabs
> need a refresh to follow (ordinary web pages are unaffected).

Local test pages:

```bat
python -m http.server 8765 --directory testpage
```

Form submission needs to show what the server actually received, so there is a separate probe
server (same `testpage/` static root, plus a `GET`/`POST /echo` endpoint):

```bat
python tools\form_server.py 8902
```

---

## If the app will not start

If Windows shows **"This app can't run on your PC"** after you double-click the exe, the message
comes from the Windows loader and is returned before any of the browser's code runs — it means
the executable could not be loaded, not that the browser crashed. Check, in order:

1. **Is the file complete?** In the folder that holds it, verify with PowerShell (size and SHA256
   are on the [Releases](https://github.com/liumingyangrd/zero-browser/releases) page):
   ```powershell
   $f = ".\zero-browser-v0.1.7-win32.exe"
   (Get-Item $f).Length                                          # expect 4023434
   (Get-FileHash $f -Algorithm SHA256).Hash
   [BitConverter]::ToString([IO.File]::ReadAllBytes($f)[0..1])   # expect 4D-5A ("MZ")
   ```
   A length of 0, a hash that does not match, or first bytes other than `4D-5A` all mean the
   download was truncated or replaced in transit (a proxy, antivirus, or an HTML block page saved
   as `.exe`) — download it again. This was hit for real once: the download was a **0-byte** file.
2. **Was the file blocked?** Right-click it → Properties → tick "Unblock" if present, and copy it
   out of a cloud-synced or network folder (OneDrive, a mapped drive, a zip preview) into a plain
   local folder such as `C:\zb\` before running it.
3. **Is the machine ARM-based?** The 32-bit package is `PE32 / i386` and the 64-bit package is
   `PE32+ / AMD64`; on Windows on ARM the 32-bit package needs the x86 emulation layer. If that is
   unavailable or disabled by policy, Windows
   reports exactly this message. Check Settings → System → About → System type.

## Diagnostics and evidence

`--shot` is a windowless render diagnostic: it reuses the **real** `Render` / `OnLButtonDown`
code paths, draws the window into a memory DC and saves it as a BMP — so evidence can be
collected even without a desktop window (or where screenshots are restricted).

```bat
:: Screenshot a page and dump the layout tree (doc + screen coordinates, runs, player state)
::   note: --dump-boxes prints the layout tree only after the --click events are delivered, so the
::   dump reflects the final post-click state; live box coordinates are those of the last output
build\zero-browser.exe --shot --url http://127.0.0.1:8765/video2.html ^
    --out build\shot.bmp --wait 3000 --size 1180x1240 --dump-boxes

:: Scroll to a position first, then screenshot (verifies position:fixed stays put)
build\zero-browser.exe --shot --url http://127.0.0.1:8765/position.html ^
    --out build\scrolled.bmp --scroll 780 --wait 1200 --size 1100x900

:: Deliver real clicks in order (pause the picture, then seek), capturing a second image
build\zero-browser.exe --shot --url http://127.0.0.1:8765/video2.html ^
    --out build\a.bmp --out2 build\b.bmp ^
    --click 512,330 --click 678,493 --wait 1200 --after 600

:: --click / --type-field TEXT / --press NAME are queued and executed in the order given on
:: the command line, so a multi-step flow such as "click a field, type, click the next one,
:: press Enter" is expressible. --press accepts tab / shift+tab / enter / escape / backspace /
:: delete / left / right / home / end. Note that --type targets the address bar only; use
:: --type-field to drive page controls.

:: Images / background images / grid / forms / float columns
build\zero-browser.exe --shot --url http://127.0.0.1:8765/images.html   --out build\img.bmp --wait 1500 --size 1100x1900
build\zero-browser.exe --shot --url http://127.0.0.1:8765/grid.html     --out build\grid.bmp --wait 1200 --size 1100x1000 --dump-boxes
build\zero-browser.exe --shot --url http://127.0.0.1:8765/forms.html    --out build\forms.bmp --wait 1200 --size 900x760 --dump-boxes
::   float column regression (expect content_height=619; without float every column stacks and
::   the number grows noticeably)
build\zero-browser.exe --shot --url http://127.0.0.1:8765/floats.html   --out build\floats.bmp --wait 1200 --size 1100x1200 --dump-boxes

:: Form interaction regression. Start the form probe server first:
python tools\form_server.py 8902
::   Typing into a control + Tab traversal (each [form] press should report, in turn,
::   input(text) -> input(password) -> textarea -> select -> button -> input(submit))
build\zero-browser.exe --shot --url http://127.0.0.1:8765/forms.html ^
    --out build\ft.bmp --wait 1500 --size 900x760 ^
    --click 311,240 --type-field "-OK" --press tab --press tab --press shift+tab
::   Enter submits the POST form (the echo page should show
::   method=POST and body=csrf=tok-42&user=zero-user-X&pass=secret123&remember=1)
build\zero-browser.exe --shot --url http://127.0.0.1:8902/formsubmit.html ^
    --out build\fp.bmp --wait 1500 --size 900x900 --dump-boxes ^
    --click 311,203 --type-field "-X" --press enter
::   Clicking the submit button (the body should end with &do=login -- a pressed button is
::   submitted only when it carries a name)
build\zero-browser.exe --shot --url http://127.0.0.1:8902/formsubmit.html ^
    --out build\fb.bmp --wait 1500 --size 900x900 --dump-boxes ^
    --click 311,203 --type-field "-X" --click 208,319
::   GET form (expect query=q=%E8%87%AA%E7%A0%94%E5%86%85%E6%A0%B8-Y&go=%E6%90%9C%E7%B4%A2)
build\zero-browser.exe --shot --url http://127.0.0.1:8902/formsubmit.html ^
    --out build\fg.bmp --wait 1500 --size 900x900 --dump-boxes ^
    --click 311,499 --type-field "-Y" --click 512,499
::   <button type="button"> must not submit (no [form] click-submit in the output)
build\zero-browser.exe --shot --url http://127.0.0.1:8902/formsubmit.html ^
    --out build\fc.bmp --wait 1500 --size 900x900 --dump-boxes --click 286,319

:: Network diagnostics (the URL follows --net-test directly; the third argument is optional
:: and writes the body to a file)
build\zero-browser.exe --net-test http://example.com/
build\zero-browser.exe --net-test https://example.com/ build\page.html

:: Settings and interface-language regression. --settings-file redirects the settings file so
:: the automated tests never touch the real %APPDATA%\ZeroBrowser\settings.ini.
::   current language and config path
build\zero-browser.exe --shot --settings-file build\t.ini --dump-settings
::   expect: [settings] lang=auto effective=zh file=build\t.ini  (Chinese system)
::   switch to English and persist
build\zero-browser.exe --shot --settings-file build\t.ini --set-lang en --save-settings --dump-settings
::   expect: [settings] save=1 … then [settings] lang=en effective=en english=1
::   a fresh process without --set-lang must read en back from the file
build\zero-browser.exe --shot --settings-file build\t.ini --dump-settings
::   expect: [settings] lang=en effective=en english=1
::   click a language option on the settings page: an ordinary link navigation plus the query
::   string being applied in StartNavigate
build\zero-browser.exe --shot --url browser://settings --out build\set.bmp --wait 1000 ^
    --size 1000x800 --set-lang zh --settings-file build\t.ini --click 360,432 --wait 1500 --dump-boxes
::   expect: [settings] lang=en saved=1, and the page text becomes Settings / Interface language /
::   English (current); the config file says lang=en
build\zero-browser.exe --shot --url browser://settings --out build\set2.bmp --wait 1000 ^
    --size 1000x800 --settings-file build\t.ini --click 360,376 --wait 1500 --dump-boxes
::   expect: clicking back to 中文 gives [settings] lang=zh saved=1
::   built-in pages in both languages (home / settings / about / parser / CSS)
build\zero-browser.exe --shot --url browser://home --out build\i18n.bmp --wait 900 ^
    --size 1000x800 --set-lang en --settings-file build\t.ini --dump-boxes
::   expect: a run reading "Self-built" (Chinese source splits into per-character runs).
::   Note that --dump-boxes splits runs per word / character, so do not match a whole sentence.

:: Address-bar input regression (goes through the real OnChar / OnKey paths)
::   --set-address "\empty"  clears the address bar
build\zero-browser.exe --shot --url browser://home --out build\addr.bmp --wait 300 ^
    --size 900x600 --set-address "\empty" --focus-address --type "http://a.cn"
::   expected: [address] typed=11 bytes=11 caret=11 hex=68 74 74 70 ... and no crash
build\zero-browser.exe --shot --url browser://home --out build\addr2.bmp --wait 300 ^
    --size 900x600 --set-address "a b" --focus-address --backspace 1
::   expected: [address] backspace=1 bytes=2 caret=2
::   (multi-byte characters are removed as a single code point — see pitfall 22)

:: Address-bar clipboard regression (--clipboard first writes the text into the system clipboard;
:: --paste/--copy/--cut go through the real Ctrl+V/C/X branches)
build\zero-browser.exe --shot --url browser://home --out build\clip.bmp --wait 300 ^
    --size 900x600 --set-address "\empty" --focus-address ^
    --clipboard "https://example.com/path" --paste 1
::   expected: clipboard-set bytes=24 ok=1 / paste=1 bytes=24 caret=24
build\zero-browser.exe --shot --url browser://home --out build\clip2.bmp --wait 300 ^
    --size 900x600 --set-address "old-text-here" --focus-address ^
    --clipboard "new.example.com" --select-all --paste 1
::   expected: select-all sel=[0,13) / paste=1 bytes=15 (pasting replaces the whole selection)
build\zero-browser.exe --shot --url browser://home --out build\clip3.bmp --wait 300 ^
    --size 900x600 --set-address "https://cut.example.com/" --focus-address --select-all --cut
::   expected: cut bytes=0 caret=0 readback='https://cut.example.com/'

:: Hotkey regression (a windowless session has no keyboard and GetKeyState is always 0, so
:: --hotkey feeds the modifier keys explicitly into the real OnKeyEx branch)
build\zero-browser.exe --shot --url http://127.0.0.1:8765/index.html --out build\hk.bmp ^
    --wait 1500 --size 900x600 --hotkey ctrl+l
::   expected: hotkey ctrl+l focused=1 sel=[0,32) (focus and select all)
build\zero-browser.exe --shot --url http://127.0.0.1:8765/index.html --out build\hk2.bmp ^
    --wait 1500 --size 900x600 --set-address "half-typed" --focus-address --hotkey f5
::   expected: hotkey f5 focused=1 bytes=10 pending=2
::         (F5 still reloads while focused, and does not discard the text being typed)

:: Cookie round-trip: start the local cookie test server first
python tools\cookie_server.py 8899
:: First request absorbs Set-Cookie, second sends it back to the echo endpoint
:: expected: echo_body=Cookie: auth=abc123; pref=dark  and jar_size=2
build\zero-browser.exe --cookie-test http://127.0.0.1:8899/set-auth http://127.0.0.1:8899/echo
```

Alpha in the BMP is forced to 255 so it can be converted to PNG directly for eyeballing.

Two things to note about the address-bar clipboard regression:

- **`--clipboard` writes the system clipboard** (deliberately, so that `--paste` reads real
  content).
- **Non-ASCII text cannot travel through command-line arguments**: `main(int, char**)` receives
  ANSI code-page bytes, so passing Chinese through is converted to GBK first. To verify
  multi-byte text, put the UTF-8 text on the clipboard with `Set-Clipboard` and then run only
  `--paste` / `--copy`:
  ```powershell
  Set-Clipboard -Value "零浏览器.cn/路径"      # 22 bytes in UTF-8
  build\zero-browser.exe --shot --url browser://home --out build\clip4.bmp ^
      --wait 300 --size 900x600 --set-address "\empty" --focus-address --paste 1 --copy
  :: expected: paste=1 bytes=22 caret=22, and (Get-Clipboard) then matches the original exactly
  ```

The selection highlight can be verified at pixel level: the highlight colour painted underneath
the text is `#bfdbfe`; count the pixels of that colour along the address-bar strip
(y 44–74) — thousands with a selection, 0 without one.

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

:: JS: inline and external scripts, DOM mutation, timers (works windowless too; timers are driven
:: by WM_TIMER)
build\zero-browser.exe --shot --url http://127.0.0.1:8765/js.html --out build\js.bmp ^
    --wait 1500 --size 1100x900 --dump-boxes
::   expected: [js] scripts=1 failed=0, plus script-generated content such as item-A/B/C, count=3
::   and interval-ticks=2
:: Clicking the button (the inline onclick and the addEventListener listener both fire)
build\zero-browser.exe --shot --url http://127.0.0.1:8765/js.html --out build\js2.bmp ^
    --wait 1200 --size 1100x900 --click 53,459 --dump-boxes
::   expected: clicks=1 and clicked-target=BUTTON
:: Error isolation: after a throw / an undefined function / an infinite loop, later scripts and the
:: page render must all continue
build\zero-browser.exe --shot --url http://127.0.0.1:8765/jserr.html --out build\jserr.bmp --wait 2500 --dump-boxes
::   expected: [js] scripts=4 failed=3, script 4 ok=1, normal content_height
:: Interpreter self-test (independent of the DOM)
g++ -std=c++17 -O2 -Isrc tools\js_probe.cpp src\js.cpp src\js_eval.cpp src\js_builtins.cpp src\js_globals.cpp ^
    -o build\js_probe.exe -Wl,--stack,8388608
build\js_probe.exe tools\js_selftest.js
build\js_probe.exe --robust

:: Screenshot structure comparison (mine vs another browser's PNG) and pixel statistics
python tools\pixelstat.py build\shot.bmp
python tools\compare_render.py build\mine.bmp other-browser.png --crop-top 78

:: Echo request headers, to verify User-Agent / Accept-Language / Referer
python tools\echo_headers.py 8901
```

> `--dump-boxes` now **prints the layout tree after the clicks** (it used to print first and click
> afterwards). The new order is what makes `--click` useful for verifying event handlers:
> `[js] scripts=…`, `内联脚本 N ok=…`, `listeners=` and `timers=` are all emitted after the
> dump, and the text visible on the page must already be the post-click content. To see only the
> layout as it was before the click, drop `--click`; after a layout fix in the same file, the old
> box coordinates become invalid.

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
- Form submission end to end (`tools/form_server.py` + `testpage/formsubmit.html`; `--shot
  --dump-boxes` reads the server's echo page): pressing Enter submits the `POST` form and yields
  `body=csrf=tok-42&user=zero-user-X&pass=secret123&remember=1` (**document order**, hidden field
  included, no un-pressed button), clicking the submit button makes it `...&do=login`, the `GET`
  form yields `query=q=%E8%87%AA%E7%A0%94%E5%86%85%E6%A0%B8-Y&go=%E6%90%9C%E7%B4%A2`, and
  `<button type="button">` does not submit; `Tab` walks
  `input(text) → input(password) → textarea → select → button → input(submit)`
- Settings and interface language: `--settings-file` + `--set-lang` + `--save-settings` +
  `--dump-settings` form a windowless regression. Measured round trip: `lang=auto` (in effect
  zh) → click the English option → `lang=en saved=1` with the config file written as `lang=en` →
  **a separate process without `--set-lang` reads en back** → click 中文 → `lang=zh saved=1`.
  All five built-in pages, the error page, the tab fallback title and the status bar have a
  Chinese and an English wording
- Tab switching no longer re-lays out: the `[perf]` log shows switching to a page with a
  document height of 5914 going from **1976 ms to no layout at all** (the result is reused when
  the viewport is unchanged, see pitfall 39)
- Cookies: `--cookie-test` prints `echo_body=Cookie: auth=abc123; pref=dark` with `jar_size=2`
- Network: with `ZB_PROXY=http://127.0.0.1:7890` set, `http://example.com` is fetched successfully
- **JavaScript** (measured windowless with `--shot`; both the script output and the layout come from
  the real code paths):
  - `testpage/js.html` (DOM mutation + `createElement`/`appendChild` + `forEach` + `JSON` +
    `setInterval` + an inline `onclick` + `addEventListener`): `[js] scripts=1 failed=0 listeners=1`,
    and everything the script generates renders normally — `item-A` / `item-B` / `item-C`,
    `count=3`, `joined=A|B|C`, `even-sum=6`, `interval-ticks=2`, with `content_height=466`
  - after clicking the button (`--click 53,459`): `clicks=1` (the inline `onclick`) and
    `clicked-target=BUTTON` (from `e.target.tagName` inside the `addEventListener`) **both appear**,
    so the inline attribute and the listener were both dispatched in that single click
  - `testpage/jserr.html` (a deliberate throw + an undefined function + an infinite loop + a later
    script): `[js] scripts=4 failed=3`, and the four scripts report
    `Error: 故意抛出的错误`, `Error: undefined 不是函数` and
    `Error: 脚本执行步数超限（疑似死循环），已中止` in that order, while **script 4 still reports
    `ok=1` and the page renders normally at `content_height=140`**
  - `testpage/jsext.html` + `jsext.js` (external): `外链脚本 1 ok=1`, and the page shows
    `外链脚本已执行`, `ext-sum=60` and `ext-flag=true`, with listeners=1
    (the page has 2 scripts in total — external + inline, both `ok=1`)
  - interpreter self-test: `tools/js_selftest.js` **passes all 139 assertions**
    (`SUMMARY passed=139 failed=0`); `build\js_probe.exe --robust` **passes all 13 robustness
    cases** (infinite loop, infinite recursion, syntax error, `null` property, calling a
    non-function, `catch` recovery, …)
  - adding JS did not disturb the page regressions: index 577 / images 1861 / grid 467 / forms 469 /
    flexblocks 233 / video2 864 / position 1200 / floats 619
- **Structure comparison against another browser** (same local page, same window width, comparing
  row/column content bands with `tools/compare_render.py`): the first content band aligns exactly
  (relative y 22–40 vs 22–40) and later bands differ by about 6px. Before the font-size fix each
  line was 10–12px taller (root cause: `font-size` was converted as points, see pitfall 23)
- Local regression (`--shot` measured `content_height`): images 1861 / grid 467 / position 1200 /
  forms 469 / flexblocks 233 / video2 864 / index 577 / floats 619

> If `https://` fails with `Win32 error 12185` (`ERROR_WINHTTP_CANNOT_CONNECT`), the environment is
> usually blocking TLS/CONNECT — it is not a bug in the browser code. On a normal Windows install
> WinHTTP completes TLS. `ZB_PROXY` can point at a local proxy to work around it.

---

## Implemented

**HTML**
- Tag/attribute parsing, comments, DOCTYPE, character entities (`&amp;` `&lt;` `&#x...`), void elements
- Raw-text handling for `<title>` / `<style>` / `<script>` / `<textarea>`; case-insensitive tag and
  attribute names
- `<!doctype …>` / `<!-- … -->` / `<![CDATA[…]]>` declaration regions are skipped as a whole
  (case-insensitively), and a UTF-8 BOM is stripped automatically

**JavaScript (self-built ES5-subset interpreter, no third-party engine)**
- Syntax: `var`/`let`/`const`, function declarations/expressions/arrow functions, closures,
  recursion, hoisting, object/array literals and computed property names, template strings
  (no `${}` interpolation), `for`/`for-in`/`for-of`/`while`/`do-while`, `switch`,
  `try`/`catch`/`finally`/`throw`, `break`/`continue`, `++`/`--`/compound assignment, the
  conditional operator, `typeof`/`instanceof`/`in`/`delete`/`void`, bitwise operators, loose and
  strict equality
- Built-ins: `Object` / `Array` / `String` / `Number` / `Math` / `JSON` / `Date` /
  the `Error` family / `parseInt` / `parseFloat` / `isNaN` / `isFinite` /
  `String` / `Number` / `Boolean` / `Array` / `Object` / `Function` /
  `encodeURI(Component)` / `decodeURI(Component)` / `console.*` / `alert` (writes to the script log)
- DOM: `document` queries and creation (`getElementById` / `querySelector(All)` / `createElement` …),
  element attributes and `classList` / `style` / `innerHTML` / `textContent` /
  `appendChild` / `insertBefore` / `removeChild` / `setAttribute` /
  `getBoundingClientRect` / `offset*`, `window` / `location`, events (including bubbling)
- Events: `addEventListener` and the inline `onclick="…"`, bubbling to `document` / `window`,
  returning `false` as an equivalent of `preventDefault`, the `<a href>` default action navigating
- Timers: `setTimeout` / `setInterval` / `clearTimeout` / `clearInterval` /
  `requestAnimationFrame`, driven by the shell's 60ms heartbeat (`WM_TIMER`), capped at 512
- Scripts: inline and external `<script src>` (an external body is fetched in parallel with images
  by the navigation thread and does not block the UI), executed in document order, after the first
  layout, followed by `DOMContentLoaded` / `load` dispatch
- Isolation: each script runs separately and exceptions are only logged, never rethrown; infinite
  loops are cut off by the 20-million-steps-per-script budget, infinite recursion by the 200-frame
  call-depth guard

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
- The flex default `align-items: stretch`: the built-in home page's four cards end up equal height
  with their links aligned (before the fix each card took its own content height)
- `float` columns: the judge site's Amaze UI grid (`.am-g` + `.am-u-md-8` / `.am-u-md-4`) sits side
  by side correctly; before the fix every column degenerated into a stacked block flow and the page
  squeezed into one narrow strip
- Font sizes: `1.4rem` / `.875em` / `80%` resolve by the CSS rules; before the fix the judge site's
  announcement column collapsed to 1–2 px and 613 character runs piled onto a single `y`
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
- **Address-bar editing**: click to place the caret, drag to select, `Shift`+`←/→/Home/End` to
  extend the selection, `Ctrl+A` select-all, `Ctrl+C/X/V` and `Shift+Insert` clipboard, a context
  menu (cut/copy/paste/select-all), and `Delete`/`Backspace` deletes the selection
- **Address-bar shortcuts**: `Ctrl+L` / `F6` focus and select all, `F5` / `Ctrl+R` reload
  (they work while the address bar is focused too, and keep the text being typed), `Esc`
  abandons the edit, `Enter` navigates

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

**Forms (rendering + interaction)**
- `input` (`text`/`password`/`submit`/`button`/`reset`/`checkbox`/`hidden`), `button`, `select`,
  `textarea`
- Shows `value` / `placeholder` / first `option` text, with focus highlight and intrinsic sizing
- `input[type="hidden"]` takes no layout space (CSRF hidden fields no longer skew the page)
- Click to focus, code-point text editing, `Tab`/`Shift+Tab` focus traversal
- `Enter`, or a click on the submit button, submits the `<form>`: `GET` query string / `POST`
  request body, fields encoded in document order, hidden fields included, the pressed submit
  button appended last

---

## Not implemented yet

- **Very few settings**: interface language is the only one. No home page setting, no proxy
  setting, no font/zoom setting, no "clear data" entry, and no search inside the settings page.
  The settings screen is just a built-in page, so adding one means extending both the string
  table and the page template
- **Only Chinese and English**: the `T()` table has exactly two columns (zh / en), so a third
  language would need a different table shape. There is no locale-aware date or number
  formatting either.
- **JavaScript language features**: no regular expressions (`/.../` literals are unsupported), no
  `Promise` / `async` / `await`, no generators, no `class` syntax, no destructuring or spread, no
  `${}` interpolation in template strings (it reports an error), the `Function` constructor does not
  compile code, and there is no block scoping (`let`/`const` behave like `var`).
  Strings use **UTF-8 byte semantics** (`length` and indices count bytes, not UTF-16), but every
  slicing operation **snaps to a code-point boundary**, so it never cuts a character in half.
  There is no GC: objects use reference counting, so a reference cycle is only reclaimed when the
  **whole page is destroyed**. There is no `fetch` / `XMLHttpRequest`, so a script cannot issue
  requests on its own
- **DOM subset**: only the commonly used subset exists — no `dataset` / `insertAdjacentHTML` /
  `cloneNode`, no event capture phase, and `addEventListener` supports neither `once` nor `capture`;
  `document.cookie` is **read-only**; `getComputedStyle` returns `undefined`; a `<style>` inserted
  through `innerHTML` **does not take effect**
- **The script fallbacks stay**: the static lazy-load fallback for thumbnails (`src` is a
  placeholder → fall back to `data-src` / `data-original` / `data-lazy-src` / `srcset`) and the
  **anti-bot challenge page** handling (the fixed "script sets a cookie then reloads" pattern is
  recognised and emulated, which is how that judge site can be opened at all) are both still
  there — having JS does not remove them
- **Streaming**: HLS / DASH / m3u8 segment fetching is not implemented; only direct media files work
- **Form interaction is still incomplete**: typing, focus traversal and `GET`/`POST` submission
  work, but `select` has **no drop-down list** (it only shows the current option); a click inside
  a control just puts the caret at the end of the text; `enctype="multipart/form-data"` (file
  upload), `formnovalidate`, constraint validation and `:invalid` are not supported; and clicking
  `input[type=checkbox]` does not toggle its checked state
- **CSS extras**: `position: sticky`, `transform`/`transition`/`animation`, `flex-wrap`
  (parsed but not wrapping yet), `vertical-align`, pseudo-elements `::before`/`::after`
- **`float` is a deliberately simplified subset**: floats sit side by side on a line and wrap when
  they no longer fit, and the container's height includes them (equivalent to the clearfix every
  grid framework ships). That is enough to drive grid frameworks, but ordinary block flow
  **avoids** floats instead of being allowed to overlap them as strict CSS permits, and inline
  text still does not wrap around a floated image. `clear` is parsed but has no separate effect
  (block flow always avoids floats).
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

JavaScript hangs off **both sides** of that chain: scripts run in document order **after the first
layout completes** and read/write the same DOM tree through `js_dom.cpp` (a mutation triggers a
re-layout and repaint), while events and timers return to the interpreter through the shell's 60ms
`WM_TIMER` heartbeat:

```
DOM tree <── js_dom.cpp (DOM/event/timer bindings) <── js_eval.cpp (evaluator)
                                                           ↑
                                                     js.cpp parsing
                                                           ↑
                                    js_builtins.cpp / js_globals.cpp (built-ins)
```

- Coordinate convention: `screen = viewport origin + document - scroll`; hit testing uses exactly
  the inverse, so painting and clicking share one transform
- Hit testing ends at a **DOM node**: the run / box found in the layout tree is mapped back to its
  node, and the event is then handed to the script
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
    i18n.cpp / i18n.h     interface language, string table and settings file I/O (settings.ini)
    network.cpp / .h      WinHTTP transport: redirects, gzip, charset normalization, cookie jar, proxy
    js.cpp                lexing and parsing for the self-built JS interpreter
    js_eval.cpp           evaluator: scopes, statements/expressions, calls and the step budget
    js_builtins.cpp       built-in method table (Object/Array/String/Number/Math/JSON/Date/…)
    js_globals.cpp        global objects such as Math/JSON/Date/Error/console
    js_dom.cpp / js_dom.h DOM bindings: document/elements/window/location/events/timers
    js.h / js_internal.h  public interpreter interface and the internal interface
    media.cpp / .h        Media Foundation player (decode, timeline, seek)
    audio_out.cpp / .h    WASAPI audio output
  docs/screenshots/       README screenshots (PNG)
  testpage/               local test pages (images, grid, flex, float columns, position, forms, form submit, video, external CSS, JS)
  testmedia/              test clips
  tools/                  probes and evidence tools (pixel/timeline/header helpers, form_server.py, js_probe.cpp, js_selftest.js)
```

---

## Pitfalls I hit

Every one of these is something I ran into myself while building this. I wrote them down so I do
not have to run into them twice. If you are about to touch rendering, layout, networking or the
JS interpreter, it is worth a skim first.

1. **The canvas bit depth has to be explicit.** I first built the canvas with
   `CreateCompatibleBitmap(dc_, ...)`, whose depth follows the DC you hand it. Mine came from
   `CreateCompatibleDC(nullptr)`, which has a 1×1 monochrome bitmap selected by default — so I got a
   **1bpp monochrome bitmap** and every colour page and video frame was dithered to black and white.
   I switched to a 32-bit DIB section everywhere.
2. **The coordinate systems have to be exact inverses.** My convention is
   "screen = viewport origin + document − scroll". The rendering side said
   `doc - viewport + scroll` instead: no viewport origin, and the scroll sign was flipped, so it
   differed from hit testing by `2 * viewport.y`. The top of the page was cut off and the video
   controls and links could not be clicked. My rule now: if you touch the coordinate system, check
   `PaintBox` and `OnLButtonDown` together.
3. **`Length` defaulting to `is_auto = true` only suits width/height.** `margin` and `padding` have
   to be zeroed explicitly (with `ZeroLength()`). I forgot, so every block's left and right margins
   were treated as `auto` and every fixed-width element was centred by mistake.
4. **`std::atomic<double>` did not carry the playback position on 32-bit MinGW with `-O2`.** I used
   it for the playback clock and seek requests and hit cross-thread writes that were simply not
   visible (dragging the progress bar did nothing, the position did not update after a seek), while
   the same `-O0` build was fine. The clock is now a plain `double` behind a mutex; boolean flags
   still use `std::atomic<bool>`.
5. **`media.cpp` and `audio_out.cpp` must be compiled separately.** MinGW's `ksmedia.h` and MF's
   strmif headers both define `TIMECODE_SAMPLE` / `DDPIXELFORMAT`. I keep a local copy of the WASAPI
   GUIDs inside `audio_out.cpp` — do not delete it.
6. **`Page` has a `unique_ptr` member and a declared destructor, so the move operations have to be
   written out.** Without `Page(Page&&)` / `operator=`, `vector<TabState>` does not compile.
7. **`windows.h` must come before `gfx.h`.** Otherwise `DrawText` is affected by the
   `DrawTextA/W` macros and you get "marked override but does not override".
8. **Whitespace-only text nodes must not produce blank lines.** The newlines and indentation
   between block elements in HTML are separate Text nodes, and I fed them straight into
   `TokenizeText`, where `\n` counts as a hard break — so a row of empty line boxes appeared and
   pushed every following block down (one level of indentation was worth nearly 100px). Now
   `TokenizeText` only emits a hard break when the node already contains a real word, and
   whitespace-only nodes take no height. Related: `--dump-boxes` reports live box coordinates, so
   old coordinates go stale the moment the layout changes.
9. **Those indentation text nodes also have to be handled when collecting inline content.** With
   something like `.boxed>\n  <img>`, even when `TokenizeText` emits no hard break,
   `pending_space` still produces a `" "` run after the image piece. The rule I settled on:
   **keep one space between inline elements, drop whitespace at block boundaries, and always drop
   trailing whitespace.**
10. **`background-size: cover` / `contain` has to be clipped to the box.** `cover` scales the image
    larger than the box and centres it, so `oy` can go negative; I called `DrawImage` directly and
    the oversized image spilled into neighbouring areas. Now the background is clipped with
    `canvas->Clip(box)` first and `ResetClip` afterwards.
11. **Enumerating multiple `Set-Cookie` headers with `WinHttpQueryHeaders` loses them under the
    local MinGW headers.** In MinGW's `winhttp.h` that function's last parameter is `LPDWORD`
    (the Windows SDK has `DWORD`), and polling by index only returned the first header. I now
    request `WINHTTP_QUERY_RAW_HEADERS_CRLF` once, take the whole header block and parse every
    `set-cookie:` line myself, which is the only way I got all the cookies reliably.
12. **WIC's `InitializeFromMemory` wants a `BYTE*`.** Passing a `const uint8_t*` does not compile;
    it needs `const_cast<BYTE*>(data)`. WIC only reads those bytes, so the cast is safe.
13. **A block container has to interleave inline content and block children in document order.** My
    early version laid out "all inline content first, then all block children", so in a form that
    alternates `label` and `input`, **every input piled up at the top of the container** with the
    labels left below. Now consecutive inline content forms one anonymous block box that interleaves
    with block children in document order.
14. **When positioning moves a box, the runs inside it have to move too.** `ApplyPositioning` only
    changed `rect` / `content` at first, so the text and images inside kept their layout-time
    absolute coordinates and the text of a `fixed` overlay stayed where it was — it looked like an
    empty box. Now the whole subtree is shifted by the `content` origin delta (`ShiftBoxSubtree`).
15. **Trailing spaces after a wide replaced element have to be dropped.** Otherwise a space that
    does not fit starts a new line and inherits the line height the image inflated, adding a full
    image height for nothing (561px after a 918×561 image). The "skip the space that does not fit"
    branch in `LayoutInlineInto` is that rule.
16. **Attribute selectors cannot be ignored.** My early selector parser skipped `[...]` entirely, so
    `input[type="text"] { width:320px }` was treated as `input { width:320px }` and stretched the
    submit button to 320 wide too. It now parses `[attr]` and every comparison operator, and
    state pseudo-classes (`:hover` / `:focus`) are treated as not matching.
17. **`grid-template-columns` separates tracks with spaces, not commas.** Only the arguments of
    `repeat()` / `minmax()` use commas. I split on commas once, so `120px 1fr` became a single
    track — which shows up as one column with every item stacked full width.
18. **Media Foundation's RGB32 is really BGRX, and the X byte is not guaranteed to be 255.** I
    measured an entire frame with alpha 0, and this engine composites with **premultiplied
    AlphaBlend** — alpha 0 means the whole frame is fully transparent, so `<video>` stayed pure
    black while the player state, `has_frame` and the frame size all looked "fine". Frames now get
    alpha forced to 255 on copy. `tools/frame_probe.cpp` is what I wrote to see through this at a
    glance (it prints the alpha histogram and top/middle/bottom row samples).
19. **A "non-black pixel count" for a video area has to exclude the control bar.** The control bar
    alone has tens of thousands of non-black pixels, so a fully black picture still reported "has
    content" and I nearly concluded playback was fine. `page_media_probe` now only counts the
    picture area (the video box height minus the 34px control bar).
20. **When adding a `display:grid` branch, do not knock out the `display:flex` one.** Dispatch on
    `display` goes to `LayoutFlexRow` / `LayoutColumn` / `LayoutGrid` / `LayoutBlockFlow`, all at
    the top of `LayoutBox`. I once replaced the whole flex branch while inserting grid, and every
    flex container (including the built-in home page's card row) degenerated into a stacked block
    flow. `testpage/flexblocks.html` is the regression page I keep for exactly this.
21. **Do not batch-edit UTF-8 sources with PowerShell's `Get-Content` / `Set-Content`.** I did this
    once and paid for it: Windows PowerShell 5.1 decodes BOM-less files as the system ANSI code page
    (GBK here), so Chinese comments turned to mojibake first — and worse, a "third byte of a Han
    character + following ASCII(<0x40)" pair is treated as an illegal GBK double byte and **swallowed
    whole**, so newlines and the bytes for `<`, `/` and `"` simply vanished. Comments swallowed the
    next line of code, string literals lost their quotes, and the compiler produced a pile of
    nonsense errors. Use an editor or a patch tool for bulk edits, or read and write explicitly with
    `[System.IO.File]::ReadAllText` (UTF-8) plus `WriteAllBytes`. If it already happened, do not
    panic: this corruption is reversible — decode the file as GBK and write it back as UTF-8 to
    remove the mojibake, then repair the few remaining dropped bytes (they look like "first two
    bytes of a Han character followed by `?`") from context.
22. **The easy trap in UTF-8 helpers is a `size_t` underflow, not the encoding itself.** This is
    what I wrote at first:
    ```cpp
    if (i > s.size()) i = s.size();   // clamp puts i at 0 for an empty string
    size_t j = i - 1;                 // 0 - 1 underflows to SIZE_MAX
    while (j > 0 && ((unsigned char)s[j] & 0xC0) == 0x80) j--;   // reads s[SIZE_MAX]
    ```
    That function runs on **every keystroke** (snapping the caret to a code point boundary), which
    is why "typing into an empty address bar crashed": it walked backwards from `data()-1` until it
    hit unmapped memory. The fix is to reject the empty string and `i == 0` up front, and to split
    "snap to a boundary" into its own function (`Utf8SnapToBoundary`) instead of reusing
    `Utf8PrevIndex` for it; that also fixed characters being inserted before the last one. The
    regression for it: `--set-address "\empty" --focus-address --type "http://a.cn"` must print
    `hex=68 74 74 70 ...` and must not crash.
23. **CSS `font-size` is in pixels, not points.** `CreateFontW`'s height parameter is a character
    height (em), and I wrote `-MulDiv(font_size, dpi, 72)` at first — at 96 DPI a 16px font became
    a 21px em, so **all text was 30% larger than Chromium** and line heights were uniformly too big
    (comparing against Edge, every line was 10–12px taller). The right form is
    `-MulDiv(font_size, dpi, 96)`, which is just `-font_size` at standard DPI.
24. **`MFCreateTempFile` returns `E_ACCESSDENIED` (0x80070005) in a restricted environment.** The
    symptom was network video failing to play with nothing but "could not create the media temp
    file" — which is why the error text now carries the `HRESULT`. The fix is to write the cache
    file myself with `CreateFileW` and open it with `MFCreateFile` when the first call fails, then
    delete it in the player's `Close()` (measured: zero files in the temp directory before and
    after a run). Also note that MinGW's `mfplat` import library does **not** export
    `MFCreateMFByteStreamOnStream` — my in-memory-stream attempt failed to link
    (`undefined reference to ...@8`).
25. **Focusing the address bar swallowed every global shortcut in `OnKey`.** I had
    `if (address_focused_) { ...edit keys...; return; }` at the very top, so as soon as the caret
    was in the address bar, `F5` / `Ctrl+R` / `Ctrl+L` all did nothing. The correct order is
    **global shortcuts first, edit branch second**. Same area: an `F5` reload must not wipe what the
    user is typing in the address bar, and since `NavigateTo` deliberately blurs and re-syncs the
    URL, the reload path has to save and restore the text around it.
26. **Clipboard text has to lose its line breaks before it goes into the address bar.** Copied text
    often carries a trailing `\r\n`, and pasting it verbatim into a URL makes `WinHttpOpen` receive
    an address with a newline in it (navigation fails, or the request line is corrupted). I now drop
    C0/C1 control characters and U+2028/2029 as well; the `CF_TEXT` fallback converts via `CP_ACP`,
    and its **buffer must be `n` `wchar_t`s** — allocating `n-1` while the API writes `n` (including
    the terminating `\0`) overflows by one `wchar_t`.
27. **Once `SetClipboardData` succeeds, the memory belongs to the system.** I called `GlobalFree`
    after a successful call, which is a double free; only the failure path frees it in this process.
    Also `CF_TEXT` is only a fallback — prefer `CF_UNICODETEXT`, or Chinese text takes a detour
    through the ANSI code page.
28. **A window class without `CS_DBLCLKS` never receives `WM_LBUTTONDBLCLK`.** Double-click
    select-all needs it. Likewise, my self-drawn edit box is not an `EDIT` control, so
    `WM_PASTE` / `WM_COPY` / `WM_CUT` have to be handled by hand — IMEs and accessibility tools send
    only those three messages.
29. **Command-line arguments are not UTF-8.** `main(int, char**)` receives ANSI code page bytes, so
    when I use `--clipboard "中文"` in a regression the argument is first converted to GBK and then
    interpreted as UTF-8 — mojibake. And in a `--shot` windowless session `GetKeyState` is always 0,
    so `--hotkey` / `--paste` have to take the modifier keys **as explicit arguments**; otherwise
    what is being tested is not the branch a real user triggers.
30. **If the scope chain is a value type, closures capture a snapshot.** My early implementation
    copied the scope environment by value, so `total` in
    `var total=0; arr.forEach(function(x){ total += x; })` stayed 0 forever, and assignments to an
    outer variable inside a `for` body did not count either. The fix is to hold the variable table
    in a `shared_ptr` and expose it as a reference member: copying an `Env` **hands out another
    handle to the same scope**, so every closure sees the bindings written into it.
31. **In an interpreter, "throwing an error also costs steps" can recurse you to death.** Once the
    step budget is exhausted, throwing an error object that has a `toString` makes error formatting
    go through `CallFunction` → `JsCall` → `BumpSteps`, which exceeds the budget again, which
    formats again — an infinite `CallFunction` → `ToPrimitive` → `ToString` → `CallFunction` chain
    that in my measurement blew the stack and killed the process. The fix: past the budget, throw
    **plain string values only**, and format error text with something that never calls back into JS
    (read the `name` / `message` properties directly, see `ErrorText`).
32. **I shipped a build that rendered `<!doctype html>` as body text.** The parser only recognised
    uppercase `<!DOCTYPE` while real pages almost always write it lowercase, so every page grew an
    extra line of text at the top. The fix: skip every `<!`-introduced declaration region (including
    `<!-- -->` and `<![CDATA[]]>`) wholesale and **case-insensitively**, and strip the **UTF-8 BOM**
    too (Notepad's "save as UTF-8" adds one by default, and without stripping it the `<!doctype`
    shows up as body text as well).
33. **A self-drawn control is a `TextRun`, not a `Box`, so walking only the box tree never hits a
    button.** I had `NodeAt` looking at boxes alone, so neither `--click` nor a real click ever
    reached a `<button>`. `NodeAt` now checks the runs inside a box as well and returns the run's DOM
    node as the more precise hit (which is why `TextRun` gained a `node` field).
34. **The clipboard can be held by another process.** When `OpenClipboard` fails, `GetLastError` is
    5 (access denied), and at that point even PowerShell's own `Set-Clipboard` fails. The right
    behaviour is to report **"the operation failed"** honestly — do not crash, and do not silently
    write bad data.
35. **`font-size` must not treat `1.4rem` as "1.4 truncated".** What I wrote was
    `(int)std::atof(value)`, so `1.4rem` → `1px`, `1.6rem` → `1px` and `2em` → `2px`: body text
    collapsed to one or two pixels and hundreds of lines piled onto a single `y` into a black smear.
    The grid framework one judge site uses contains `1.4rem` thirty times and its announcement column
    was flattened completely. `px` / `%` / `em` / `rem` have to be handled by the CSS rules, and the
    `rem` base has to actually follow `html`'s `font-size` (pages commonly write
    `html{font-size:62.5%}`). The same edit removed a GNU `?:` extension that MSVC would not compile.
36. **`float` is not an optional feature; it is the foundation of a lot of sites.** The Bootstrap-era
    and Amaze UI generation of grid frameworks lay out entirely with `float: left` plus percentage
    widths, and rely on a `:before`/`:after` clearfix so the container contains its floats. Without
    `float`, columns such as `.am-u-md-8` / `.am-u-md-4` all degenerate into a stacked block flow and
    the page squeezes into one narrow strip — it looks like "the layout collapsed" when exactly one
    property was missing. Likewise, **inline content after a float** has to step aside first, or it
    is painted straight on top of the float, two blocks of text on top of each other.
37. **The default of `align-items` is `stretch`, not "do nothing".** My flex row only handled
    `center` / `flex-end`, so the default fell through and every card took its own content height.
    The built-in home page's four cards and the links inside them ended up misaligned — the first
    thing anyone sees when they open the browser.
38. **A `textarea`'s initial value comes from its text child, and `input[type=hidden]` occupies no
    layout.** My `CollectInline` never set `widget_value` in its `textarea` branch, so
    `<textarea>text</textarea>` rendered as an empty box; hidden inputs were laid out like ordinary
    controls, so a single CSRF token pushed out 74×28 of blank space. Checkbox and radio controls are
    the same trap: they must not fall into the generic "white box + border + text" input branch, or
    `value="1"` gets painted inside the box as text.
39. **Switching tabs must not re-lay out the whole page — mine froze for two seconds.** The tab
    branch of `OnLButtonDown` called `RelayoutActive()` unconditionally, for nothing: the viewport had
    not changed and the page had not been touched. Measured on a page with a document height of 5914
    and roughly 10k runs: **1955–1976 ms per switch**, which is the freeze users report; clicking the
    already-active tab cost the same. The fix is for `TabState` to remember the viewport size the last
    layout was computed at (`layout_w`/`layout_h`) and for the new `EnsureLayout()` to re-lay out only
    when that size differs. **No extra dirty flag is needed**: every path that changes page content
    (navigation finishing, assets arriving, scripts mutating the DOM, form edits) already forces
    `RelayoutTab` afterwards, which refreshes the record. The same commit removed a second piece of
    duplicated work: `OnAssetsDone` also re-laid out unconditionally, so one navigation laid the same
    page out twice back to back; now it only does so when images were really attached or a script
    dirtied the DOM. Measuring this **cannot use `GetTickCount64`** (about 15.6 ms resolution) — it
    needs `QueryPerformanceCounter`. The log threshold is tunable through `ZB_PERF_MS` (default 4 ms;
    set it to 0 to see every layout).
40. **A block-level `<a>` was not clickable at all, and it took me a while to find.** `CollectLinks`
    only collected runs carrying a link flag, but a `display:block` `<a>` goes through
    `PopulateBoxes` and becomes a box whose text is an ordinary run in a child box that never got the
    flag — so the whole block simply did not exist as far as hit testing was concerned. Real sites
    write their nav items and list items this way, and so does my settings page (which is how it was
    noticed: clicking did nothing). The fix appends the block `<a>`'s own `rect` as a link area too,
    and appends it **after the child boxes**, so an inline link nested inside a block link still wins.
41. **Built-in pages must accept both the `about:` and the `browser://` spelling.** The home page's
    "parser details" link has `href="about:parser"`, but `BuiltinHtml` only recognised `parser` /
    `browser://parser`, so both links had been **silently falling back to the home page** — they were
    clickable, just going to the wrong place, which is harder to notice than a dead link. The
    `about:` prefix is now stripped before matching.
42. **Do not make a template placeholder's shape check too narrow.** The built-in pages use
    `{{key}}` placeholders, and to avoid mistaking CSS braces for placeholders I restricted keys to
    `a-z0-9.` — which made `{{about.barTitle}}` fail the check because of its uppercase `T`, and the
    token was **printed literally on the page**. Allowing uppercase and underscores is enough.

---

## License

This project is released under the **MIT** license — see [LICENSE](LICENSE).

- The project is at an early stage; the goal is to align layout behaviour with production browsers
  as closely as possible. Rendering differences are welcome as issues.
