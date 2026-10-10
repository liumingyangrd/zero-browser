# Zero Browser v0.1.7 — image resource loading fixes

Release date: 2026-10-10

Zero Browser is a Windows browser with its own C++17 / Win32 rendering engine
and JavaScript interpreter.

## 修复

- 通过 HTML 解析器收集真实图片和 CSS 资源，避免将脚本字符串、HTML 注释中的
  `url()`、`<style>` 或 `<img>` 当作图片下载。
- 修复 `<style>` 扫描条件错误造成背景图全部漏抓的问题，同时保留 `style` 属性
  中的内联背景图、懒加载图片和 HTML 实体解码。
- 修复超过 20 张图片时多个线程同时写图片表的数据竞争：最多 16 个线程下载，
  等待下载结束后由导航线程统一解码、更新图片表。
- 重排前重新附加图片，确保行内布局持有已加载的位图。
- WinHTTP 连接超时调整为基础超时的 2 倍，接收超时为 3 倍。
- 修复媒体调试日志对非原子播放时长调用 `.load()` 的错误。

页面 HTML 仍先显示，图片和外链脚本随后统一应用。本版本没有逐批渐进更新图片。

## Fixes

- Collect image resources from parsed HTML and CSS, excluding fake tags and URLs
  inside scripts and HTML/CSS comments. Inline backgrounds and lazy image sources
  remain supported.
- Fix the style scanner that skipped every style block.
- Remove concurrent writes to the shared image map when loading more than 20 images.
  Downloads use up to 16 workers; decoding and map updates happen after they finish.
- Attach decoded images before rebuilding inline layout.
- Allow longer WinHTTP connection and receive timeouts, and correct media debug logging.

HTML appears before assets. Images and external scripts are applied together when
asset loading finishes; this release does not add incremental image batches.

## Validation

- Fresh static builds for Windows x86 and x64 with `-Wall -Wextra`.
- Image regression on both architectures: 36 unique images decoded, no requests for
  script/comment decoys, and decoded images present in the rendered screenshot.
  Run: `python tools/image_loading_regression.py build/zero-browser.exe`.
- JavaScript self-test: 139 assertions passed; robustness suite: 13/13 passed.
- Local page regression on both architectures: layout pages, image/background pages,
  DOM changes, timers, external scripts, and script error isolation.

## Downloads

- `zero-browser-v0.1.7-win32.exe`: Windows 32-bit, statically linked portable executable.
- `zero-browser-v0.1.7-win64.exe`: Windows 64-bit, statically linked portable executable.
- `SHA256SUMS.txt`: checksums for both executables.

MIT licensed. See [LICENSE](LICENSE).

## Checksums

- `zero-browser-v0.1.7-win32.exe` — 4023434 bytes; SHA256 `a5e678c39a31224639585761e1e1a96ad95464789f926bb6fa9c662dc225b39d`
- `zero-browser-v0.1.7-win64.exe` — 4813073 bytes; SHA256 `60388bfe0ca3195376f9a62424b33165ff3f7feb8bf59917ead721be3274d1f8`
