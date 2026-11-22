/*
===============================================================================

Built-in MCP (Model Context Protocol) server for the Linux port.

The game itself exposes a minimal Streamable-HTTP MCP endpoint on
127.0.0.1 (default port 8787, see cvar mcp_port). MCP clients (agents)
can then read the console log, query player state and inspect what is
being rendered while the game runs.

Threading model:
  - a TCP server thread accepts HTTP connections and speaks JSON-RPC;
  - tool calls that need game state are deferred to the main thread
    (PumpFrame, called from idCommonLocal::Frame) so engine objects are
    only ever touched by the thread that owns them;
  - console output is captured from any thread via LogLine (SDL log hook);
  - per-view render data is snapshotted by SnapshotView, called from
    R_AddDrawViewCmd on the render thread.

===============================================================================
*/
#pragma once

struct viewDef_t;

namespace mcp {

// Called once per frame from idCommonLocal::Frame() on the main thread.
// Starts/stops the server based on the mcp_enable cvar and services any
// queued MCP tool calls.
void PumpFrame();

// Append a line (or multi-line block) to the console log ring buffer.
// Safe to call from any thread.
void LogLine( const char *line );

// Snapshot the just-finished view (draw surfaces, lights, entities) for the
// render_stats tool. Called from R_AddDrawViewCmd on the render thread.
void SnapshotView( const viewDef_t *viewDef );

// Called from GL_BlockingSwapBuffers() right before the swap: if a
// screen_pixels probe is armed by the tool, glReadPixels(GL_BACK) the rect
// (the just-rendered frame, still valid pre-swap on llvmpipe/Xvfb) and store
// the stats. Main thread only.
void RenderThreadPixelProbe();

// Explicit control (also automatic via cvars inside PumpFrame).
bool Start( int port );
void Shutdown();
bool IsRunning();

} // namespace mcp
