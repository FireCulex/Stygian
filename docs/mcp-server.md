# Built-in MCP server (Linux port)

The native Linux game binary hosts a minimal
[MCP](https://modelcontextprotocol.io) server **inside the game process**.
An MCP client (an agent) can attach to a running game and read the console
log, query player state, and inspect what the renderer is drawing — without
any external helper process, log tailing, or screen scraping.

Linux-only. Windows/doomclassic are untouched.

## Enable / configure

| cvar | default | meaning |
|------|---------|---------|
| `mcp_enable` | `1` | start the server (checked every frame) |
| `mcp_port` | `8787` | TCP port, bound to `127.0.0.1` only |
| `mcp_maxLogLines` | `4096` | console lines kept in the ring buffer |

Examples:

```
+set mcp_enable 1 +set mcp_port 8787      # on the command line
mcp_enable 0                              # console: stop the server
mcp_enable 1                              # console: start it again
```

Changing `mcp_port` while the server is running restarts it on the new port.
The server starts on the first frame after the cvars are read.

## Endpoints

Served as Streamable-HTTP, one request per connection (`Connection: close`).

| method | path | response |
|--------|------|----------|
| `POST` | `/mcp` | JSON-RPC 2.0 (MCP): `initialize`, `ping`, `tools/list`, `tools/call` |
| `GET` | `/health` | `ok` — liveness probe |
| `GET` | `/` | JSON server info (name, version, tool list) |
| `GET` | `/mcp` | `405` — no SSE stream, use POST |

JSON-RPC notifications (requests without `id`) get `202 Accepted` and no body.
Unknown methods return `-32601`; unknown tools return `-32602`.

## Tools

### `console_log`

Tail of the engine console log. Lines come from the SDL log hook, i.e. from
`Sys_Printf` / `idCommonLocal::Printf` output. Boot output before the first
frame is not captured.

```json
{ "lines": 100 }
```

- `lines`: integer, default `100`, clamped to `1..2000`.

### `player_state`

Session state, map, and — when in game — player position, view angles, health.

```json
{
  "sessionState": "INGAME",
  "map": "game/mars_city2",
  "inGame": true,
  "position": { "x": -698.35, "y": -2338.76, "z": 290.0 },
  "viewAngles": { "pitch": -3.5, "yaw": 91.2, "roll": 0.0 },
  "health": 100
}
```

When not in game, `position`/`viewAngles`/`health` are `null` and
`inGame` is `false`. The player is only touched when the session state is
`INGAME`, so this is safe during menus and map loads.

### `render_stats`

What the renderer is doing right now.

```json
{ "entities": false }
```

- `entities`: boolean, default `false`. When `true`, also lists the model
  names of the view entities (capped at 64).

Result (abridged):

```json
{
  "fps": 61.4,
  "frame": 12345,
  "resolution": { "width": 1280, "height": 720 },
  "map": "game/mars_city2",
  "view": {
    "valid": true,
    "frame": 12345,
    "numDrawSurfs": 912,
    "numViewLights": 37,
    "numViewEntities": 214,
    "is2Dgui": false,
    "areaNum": 12,
    "materials": [ "textures/base_wall/...", "..." ]
  },
  "frontend": { "numViews": 1, "viewLights": 37, "visibleViewEntities": 214, "...": 0 },
  "backend": { "surfaces": 912, "drawElements": 1450, "totalMicroSec": 3100, "...": 0 },
  "timings": { "gameThreadTotalMs": 12, "rendererBackEndUs": 2900, "rendererGpuUs": 8000, "...": 0 }
}
```

The `view` block is a snapshot of the last **main** (non-subview) view,
taken in `R_AddDrawViewCmd` on the render thread; mirrors/cameras and
GUI views don't overwrite it with stale data. Material and entity name
lists are unique and capped at 64 entries.

## curl examples

```sh
curl -s http://127.0.0.1:8787/health

curl -s -X POST http://127.0.0.1:8787/mcp \
  -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":1,"method":"initialize",
       "params":{"protocolVersion":"2025-06-18","capabilities":{},
                 "clientInfo":{"name":"curl","version":"0"}}}'

curl -s -X POST http://127.0.0.1:8787/mcp \
  -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":2,"method":"tools/list"}'

curl -s -X POST http://127.0.0.1:8787/mcp \
  -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":3,"method":"tools/call",
       "params":{"name":"console_log","arguments":{"lines":50}}}'
```

## OpenCode client config

Add to `opencode.json`:

```json
{
  "mcp": {
    "doom3bfg": {
      "type": "remote",
      "url": "http://127.0.0.1:8787/mcp"
    }
  }
}
```

Then the agent can call `console_log`, `player_state`, and `render_stats`
while the game runs.

## Threading model

- The TCP/HTTP server runs on its own `std::thread` and never touches engine
  objects directly.
- `tools/call` requests are queued and executed on the **main thread** by
  `mcp::PumpFrame()`, called from `idCommonLocal::Frame()` right after
  `idLib::frameNumber++`. If the main thread doesn't service a call within
  5 s the client gets JSON-RPC error `-32603`.
- Console output is captured from any thread by hooking `SDL_Log`
  (the previous output function is chained, so normal logging is unchanged).
- The render view snapshot is built on the render thread in
  `R_AddDrawViewCmd` (`R_RenderView` / GUI views), storing only plain
  counters and copied strings — no engine pointers cross threads.
- `tr.pc` / `backEnd.pc` / timing counters read by `render_stats` are plain
  integer racy reads, which is fine for diagnostics.

## Limitations

- Single request per connection (no keep-alive, no SSE stream).
- No authentication — bound to `127.0.0.1` only.
- Console capture starts when the server starts (first frame); the boot
  sequence before that is not in the buffer.
- Material/entity name lists are capped at 64 unique entries each.
- If several GUI views are rendered in one frame, `view` reflects the last
  one added; 3D scene views take precedence in practice because the main
  scene view is added after the GUI overlays.

## Files

- `neo/sys/sdl/mcp_server.h` / `mcp_server.cpp` — server, tools, log hook.
- `neo/sys/sdl/mcp_json.h` — minimal header-only JSON parser/serializer.
- `neo/framework/common_frame.cpp` — `mcp::PumpFrame()` call.
- `neo/renderer/RenderSystem.cpp` — `mcp::SnapshotView()` call in
  `R_AddDrawViewCmd`.
- No CMake changes needed: `sys/sdl/*.cpp` is globbed into the target.
  **New source files require re-running CMake configure**, not just
  `cmake --build`.
