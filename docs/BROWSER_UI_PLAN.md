# Browser UI plan

## Goal

Build an interactive browser interface for the native Buddha++ renderer. Orbit
sampling and histogram updates stay in C++/Metal. The browser chooses render
settings, displays progressive results, and provides navigation and image
controls. Interactive images are bounded by the display size; large offline
renders remain a separate command-line workflow.

This is an architecture plan, not a change to the current renderer. The first
implementation can use the Metal sampler already present in `buddha-metal` for
overview renders. Zoomed renders need a sampler that contributes efficiently to
the selected window: use the planned GPU Metropolis sampler if it is ready, or
run Metropolis on the CPU. The UI protocol should support either renderer.

## User experience

1. Launch the local renderer app; it starts its web server and opens the browser
   page. During development, launch it from the terminal.
2. Choose the complex-plane center and scale, channel iteration ranges, and
   display controls. The image size follows the viewport.
3. Start a render and watch a progressive image with elapsed time and sampling
   statistics.
4. Drag to pan and scroll or pinch to zoom. A changed complex-plane window
   starts a new render; the previous image remains visible until the first new
   preview arrives. The UI uses the same 90-degree clockwise orientation as
   the TIFF output.
5. Adjust display controls without discarding the histogram.
6. Stop, resume, or export the current image and, where supported, save a
   checkpoint with its render settings.

The browser requests a physical pixel size based on its image area and device
pixel ratio. The native process enforces a configurable pixel cap. Resizing the
page should be debounced so a drag of the window does not start many renders.
The UI does not need tiles: each interactive render fits within its display
area. A full-size preview frame represents the entire image.

## Settings and their effects

| Setting | Effect |
| --- | --- |
| Viewport size, center, scale, sampler, channel iteration ranges | Start a new histogram. |
| Pan or zoom | Compute a new center and scale, then start a new histogram. The UI can transform the old frame while waiting. |
| Brightness, contrast, saturation | Recompute RGB from the current histogram. No orbit work. |
| Clarity and Texture | Apply to the display image after tone mapping. |

Changing histogram settings must be explicit in the protocol. A change to a
display control must never silently reset accumulated samples. The server returns
the effective settings, including any clamped image size, so the UI and render
state agree.

## Target architecture

```text
Browser page
  controls, navigation, progress, image canvas
       | commands and status: local HTTP, with optional push later
       | preview frames: raw RGBA responses, with optional push later
       v
Local C++ service (127.0.0.1)
  settings validation and render-session state
  existing C++/Metal orbit sampler -> atomic RGB histogram
  shared native tone and adjustment pipeline -> RGBA preview or TIFF export
  optional checkpoint and export writers
```

A web page cannot directly import the renderer's native `MTLBuffer`. The service
sends completed display frames across the local connection. The adjustments
remain native because the user wants the exported TIFF to match the adjusted
preview. A browser-only shader would require a second implementation of those
adjustments and a separate way to reproduce them in export.

The renderer should expose a reusable session API rather than making the UI
launch and kill `buddha-metal` for each pan or zoom. A session owns the
settings, histogram, sampler, progress counters, and output state. Commands
are `start`, `stop`, `setDisplay`, `save`, and `export`. Each new histogram gets
a monotonically increasing render ID; frames and status messages carry that ID
so the browser can discard delayed results from an older render.

Start with the current Metal sampler for overview renders. Select the sampler
for zoomed renders based on measured time to a useful preview. The GPU
Metropolis work may replace the CPU fallback when it is ready. The service
must reject a requested sampler it cannot run, and report which sampler is
active; it should not silently change the requested algorithm.

## Progressive frames

The current TIFF path scans the histogram to find a maximum for each channel,
then applies contrast and lightness while mapping counts to RGB. Extract that
mapping into a documented tone-mapping specification shared by export and
preview paths. Preserve the mirrored histogram behavior for windows centered
on the real axis. Add display controls only after a baseline
preview matches the existing TIFF output closely enough to compare by eye.

For Metal renders, capture a stable histogram snapshot after a completed batch.
The CPU can process that snapshot while the next batch runs, but it must not
read the live shared histogram while the GPU writes to it. The current CLI
queues two large batches ahead; an interactive
session needs a way to accept stop, navigation, and display commands between
batches. Measure the time from each command to its visible effect and tune
batch size and queue depth if commands wait too long. CPU renders need an
equivalent point at which the histogram can be read safely. Begin with at most
two preview frames per second, and send a new frame after a display-control
change. If preview work noticeably reduces sample throughput, lower its rate
or use a separate snapshot buffer. Never send a new frame when the previous
one is still queued for the client; keep the newest state instead.

The prototype serves raw RGBA frames over HTTP and draws them into a browser
canvas. At the two-million-pixel cap, one frame is at most 8 MB. Measure
conversion, transfer, and browser paint time before changing transport. If
polling or transfer becomes significant, consider pushed frames, compression,
or browser-side display filters. Keep exact image export separate from the
preview format.

## Navigation and coordinates

The server is authoritative for the image geometry. The UI sends center,
scale, width, and height in explicit units. The displayed preview has the same
90-degree clockwise orientation as the TIFF output. On pointer input, the
browser maps screen coordinates through that rotation and the current view to
complex-plane coordinates. Zooming around a pointer must preserve the complex
point under that pointer. The server echoes the resulting window, and the UI
uses it for subsequent gestures. Tests should cover even and odd heights,
mirrored and off-axis windows, and device pixel ratios other than one.

## Lifecycle and persistence

- Starting a new render cancels the previous sampler, waits for its GPU work to
  finish, and then allocates or clears the next histogram.
- Stopping leaves the histogram available for recoloring, export, and optional
  resumption with matching render settings.
- Saving a checkpoint records all settings that affect the histogram,
  including the sampler. Loading one must validate those settings.
- UI refresh or reconnect reports the active render ID and settings and
  receives a fresh preview. A browser disconnect does not discard the render.
- The local service binds to loopback. A remote-access mode, if wanted later,
  needs its own authentication and exposure design.

## Milestones

### 0. Smallest useful prototype

Build one macOS executable that serves a single page on loopback and owns one
Metal render. The page takes its output resolution from the image viewport;
resolution is not a form input. It has center-real, center-imaginary, and scale
inputs, channel ranges, Start and Stop buttons,
and one image. Load the committed exclusion map automatically. Cap the render at a small display-sized image (initially two
million output pixels). Use the current naive Metal sampler and test only
overview or shallow-zoom windows. The supplied 8192 × 8192, scale 2048 command
is an editable full-view preset: at a smaller display size, choose scale to
preserve its four-complex-unit square framing.

Launch the executable from the terminal for the prototype. It binds a local
port, reports the URL, and opens that URL in the default browser. Closing the
tab does not stop the process; exiting the process shuts down the server and
render. A later packaged macOS app can provide a normal app icon and own the
same launch flow.

Use ordinary HTTP for this slice: `POST /render` starts or replaces the render,
`POST /stop` stops it, `GET /status` reports its ID and counters, and
`GET /frame.rgba` returns the latest complete frame. The page polls status
and preview about once per second. A replacement render keeps the old image
visible until a frame with the new render ID arrives. One client is enough.

Run one GPU batch at a time and check for commands between batches. Choose a
batch size by measuring stop/restart latency. After a completed batch, read
the shared histogram and apply the current TIFF tone mapping on the CPU, then
send the raw RGBA frame to a browser canvas. This proves the
native-renderer-to-browser loop without first building a Metal tone-mapping
shader or a WebSocket protocol. The preview uses the same orientation as TIFF.

The prototype is successful when changing center or scale starts a new render,
the displayed image gains detail while it runs, Stop responds promptly, and
the browser never shows a late frame from the previous render. Measure time to
first frame and sample throughput with preview polling on and off.

### 1. Interactive controls and GPU preview

Replace the prototype's CPU preview conversion if measurements justify a
Metal reduction and tone-mapping pass. Add pointer navigation, resize
debouncing, validation feedback, and a reusable render-session boundary. Keep
the existing CLI unchanged.

The first local measurement with a valid exclusion map put preview conversion
at about 2.5% of elapsed rendering time, so the CPU conversion remains for now.
The browser maps image x to imaginary and image y to real, matching the TIFF
rotation. Dragging changes the center, while scrolling or pinching scales around
the pointer anchor. A resize keeps the shorter axis' complex span and starts a
new render after a debounce. The native `render_session` owns the Metal command
and frame capture operations.

### 2. Display controls and transport

Add brightness, contrast, saturation, clarity, and texture. Define
clarity as a local-contrast adjustment with a stated radius and strength, then
choose its implementation after comparing sample images. Verify that changing
these controls does not restart sampling. These settings are part of the image:
session TIFF export must apply them, using the same native pipeline as the
preview. Compare a stopped preview with the TIFF export at the same settings;
report any intentional 8-bit preview differences. Keep HTTP polling if it
meets latency goals; otherwise add a push transport.

The first implementation keeps display controls on the native preview path.
`POST /display` replaces only display settings, and a completed GPU batch (or a
stopped session) produces a new frame from the existing histogram. Contrast and
saturation are signed offsets around neutral zero. The editable defaults are
Brightness +25, Contrast +10, Saturation +50, Clarity +50, and Texture +50.
Brightness uses a midtone curve with fixed black and white endpoints, so it
does not multiply highlights into clipping. Clarity applies broad local
contrast mostly to midtones; Texture affects a smaller detail scale with a
two-pixel blur radius applied twice. Both use multiple small box-blur passes to
avoid the rectangular support of a single large box, and clarity suppresses
its effect near black and white. Neutral
zero settings leave the existing TIFF-style mapping untouched. These curves
approximate the behavior of photo editing controls; they are not exact
Capture One or Photoshop algorithms. Tone mapping and adjustments share one
float pipeline (`core/image_pipeline.h`): histogram counts become float RGB in
output orientation, the adjustments run on that image, and only the result is
quantized, to 8-bit RGBA for the browser or 16-bit RGB for TIFF. At neutral
settings both quantizers reproduce the headless TIFF samples exactly. The
existing headless TIFF path still streams rows from the histogram. Compare
both outputs from one histogram when session export arrives in milestone 4.

The primary button shows Start when idle or paused and Pause while sampling.
Pause waits for the current batch and keeps the histogram; Start then resumes
sampling from it. Stop ends the run, and the next Start creates a new histogram.
The status bar reports new-histogram preview and display-only recolor averages
separately, and formats sample totals with up to three significant digits.

The worker copies the histogram after a completed batch, dispatches the next
batch, and builds the preview from the copy while the GPU runs; it never reads
the histogram while a batch may write it. Paused and stopped sessions convert
directly, since the GPU is idle. Tone mapping walks the histogram in bands of
output rows, reads `pow` results from a table for counts below 65,536, and
runs the tone, adjustment and blur passes across CPU cores with libdispatch.

On an M5 Pro (5+10 CPU cores, 16-core GPU) at 1920 × 1040 with the default
display settings, the histogram copy takes about 0.3 ms of GPU idle time per
preview. A new-histogram preview takes about 6 ms off the GPU's critical path,
and a display-only recolor about 3.3 ms, reaching a published frame about
3.6 ms after the request. Sampling ran at 99.8% of GPU batch throughput with
previews once per second, up from about 96.5% with serial conversion. These
are native measurements, not browser paint times.

### 3. Zoom sampler and session recovery

Extract the render session from the HTTP file before adding a second sampler.
Give the worker a command mailbox and publish immutable status/frame snapshots,
so status and frame metadata stay paired. Add a sampler interface with a time
budget and an active sampler name, then integrate the CPU Metropolis fallback
for zoom unless GPU Metropolis is ready. Add reconnect and test start, pause,
resume, stop, restart, and stale-frame ordering without a browser. Measure
time to a useful preview at representative zoom levels.

### 4. Export and performance pass

Add checkpoint and image export from the current session. Measure render
throughput with the UI closed, connected but idle, and showing previews. Track
preview latency and browser memory at representative display sizes. Report
histogram-to-preview and display-only recolor timings separately. Build frames
only while a client requests them, and change frame encoding or preview
frequency in response to those measurements.

## Target architecture decisions

- Native process serves the page on loopback. Start with HTTP polling; consider
  WebSocket status and binary frames if measurement shows a need.
- Metal performs orbit sampling for overview renders and histogram-to-RGB
  conversion in the target design. The smallest prototype does tone mapping on
  the CPU. CPU Metropolis can render zoomed windows until GPU Metropolis is
  ready. The browser does not need WebGPU for the first version.
- One active interactive render, bounded by a server-side pixel limit.
- Full-frame previews, with no tile protocol.
- Display controls are separate from settings that reset the histogram.

Questions for iteration: the stop/restart latency target, the zoom level where
the current Metal sampler ceases to be useful, and the exact mapping and useful
ranges for the display controls.
