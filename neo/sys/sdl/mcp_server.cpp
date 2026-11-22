/*
===============================================================================

Built-in MCP (Model Context Protocol) server - Linux port.

Minimal Streamable-HTTP MCP endpoint served from inside the game process:

	POST /mcp      JSON-RPC 2.0: initialize, ping, tools/list, tools/call
	GET  /health   liveness probe ("ok")
	GET  /         server info

Tools: console_log, console_command, subtitle_log, player_state, render_stats, pointed_entity, screen_pixels.

Threading:
	- the TCP/HTTP server runs on its own std::thread and never touches
	  engine state directly;
	- tools/call requests are queued and executed on the main thread by
	  mcp::PumpFrame() (called from idCommonLocal::Frame);
	- console output is captured from any thread by hooking SDL_Log;
	- the last non-subview view is snapshotted on the render thread by
	  mcp::SnapshotView() (called from R_AddDrawViewCmd).

===============================================================================
*/

#include "../../idlib/precompiled.h"

#include "mcp_server.h"
#include "mcp_json.h"

#include "../../sys/sys_public.h"
#include "../../sys/sys_session.h"
#include "../../framework/Common.h"
#include "../../framework/Common_local.h"
#include "../../framework/CVarSystem.h"
#include "../../framework/CmdSystem.h"
#include "../../d3xp/Game_local.h"
#include "../../d3xp/Player.h"
#include "../../d3xp/Entity.h"
#include "../../d3xp/SubtitleManager.h"
#include "../../d3xp/physics/Physics.h"
#include "../../d3xp/physics/Clip.h"
#include "../../ui/UserInterface.h"
#include "../../renderer/tr_local.h"
#include "../../renderer/RenderWorld.h"

#include <SDL.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

using mcpjson::Value;

//=============================================================================
// cvars
//=============================================================================

static idCVar mcp_enable( "mcp_enable", "1", CVAR_BOOL,
	"enable the built-in MCP server on 127.0.0.1 (Linux port)" );
static idCVar mcp_port( "mcp_port", "8787", CVAR_INTEGER,
	"TCP port of the built-in MCP server" );
static idCVar mcp_maxLogLines( "mcp_maxLogLines", "4096", CVAR_INTEGER,
	"number of console log lines kept for the MCP console_log tool" );

namespace {

//=============================================================================
// console log ring buffer (written from any thread via the SDL log hook)
//=============================================================================

std::mutex					g_logMu;
std::deque<std::string>		g_logLines;
std::atomic<int>			g_logCap( 4096 );

const size_t MAX_MATERIALS	= 64;
const size_t MAX_ENTITIES	= 64;

void AddUnique( std::vector<std::string> & v, const char * name, size_t cap ) {
	if ( name == NULL || name[0] == '\0' ) {
		return;
	}
	for ( size_t i = 0; i < v.size(); i++ ) {
		if ( v[i] == name ) {
			return;
		}
	}
	if ( v.size() < cap ) {
		v.push_back( name );
	}
}

//=============================================================================
// last non-subview render view snapshot (written on the render thread)
//=============================================================================

struct ViewSnapshot {
	bool						valid = false;
	int							frame = 0;
	int							numDrawSurfs = 0;
	int							numViewLights = 0;
	int							numViewEntities = 0;
	bool						is2Dgui = false;
	int							areaNum = -1;
	std::vector<std::string>	materials;
	std::vector<std::string>	entityModels;
};

std::mutex		g_viewMu;
ViewSnapshot	g_view;

//=============================================================================
// pixel probe for the screen_pixels tool: request/result handshake.
// ToolScreenPixels runs on the main thread at the START of the frame
// (PumpFrame), but GL_BACK only reliably holds the finished frame right
// before the swap (on Xvfb/llvmpipe the post-swap back buffer reads black),
// so the tool arms a rect here and mcp::RenderThreadPixelProbe() — called
// from GL_BlockingSwapBuffers pre-swap — does the actual glReadPixels.
// Protocol: call once to arm, call again (next frame) to get the capture.
//=============================================================================

struct PixelProbeRequest {
	bool		pending = false;
	int			x = 0, y = 0, w = 0, h = 0;	// screen px, top-left origin
	int			reqFrame = 0;
};

struct PixelProbeResult {
	bool		valid = false;
	int			frame = 0;
	int			x = 0, y = 0, w = 0, h = 0;
	double		mean[4] = { 0, 0, 0, 0 };
	double		stddev[4] = { 0, 0, 0, 0 };
	uint32_t	checksum = 0;
	double		grid[4][4][3];					// 4x4 cells of mean RGB
	double		meanAbsDiffX = 0;				// mean |px[x+1]-px[x]| over RGB — high = static noise
	double		meanAbsDiffY = 0;				// mean |px[y+1]-px[y]| over RGB — high = static noise
	unsigned	glError = 0;
};

std::mutex			g_probeMu;
PixelProbeRequest	g_probeReq;
PixelProbeResult	g_probeRes;
int					g_probeResReturned = -1;	// frame already handed to a caller

//=============================================================================
// deferred tool-call queue: server thread -> main thread
//=============================================================================

struct PendingRequest {
	Value						id;
	std::string					method;
	Value						params;
	Value						result;
	int							errorCode = 0;
	std::string					errorMsg;
	std::mutex					mu;
	std::condition_variable		cv;
	bool						done = false;
};

std::mutex										g_qMu;
std::queue< std::shared_ptr<PendingRequest> >	g_queue;

//=============================================================================
// server state
//=============================================================================

std::atomic<bool>		g_running( false );
int						g_port = 0;
std::thread				g_thread;

SDL_LogOutputFunction	g_prevLogFn = NULL;
void *					g_prevLogData = NULL;
bool					g_logHooked = false;

float					g_fpsEma = 0.0f;

//=============================================================================
// helpers
//=============================================================================

const char * SessionStateName( int st ) {
	switch ( st ) {
		case idSession::PRESS_START:	return "PRESS_START";
		case idSession::IDLE:			return "IDLE";
		case idSession::SEARCHING:		return "SEARCHING";
		case idSession::CONNECTING:		return "CONNECTING";
		case idSession::PARTY_LOBBY:	return "PARTY_LOBBY";
		case idSession::GAME_LOBBY:		return "GAME_LOBBY";
		case idSession::LOADING:		return "LOADING";
		case idSession::INGAME:			return "INGAME";
		case idSession::BUSY:			return "BUSY";
		default:						return "UNKNOWN";
	}
}

Value MakeToolResult( const std::string & text, bool isError = false ) {
	Value result = Value::object();
	Value content = Value::array();
	Value item = Value::object();
	item.set( "type", Value::string( "text" ) );
	item.set( "text", Value::string( text ) );
	content.push( item );
	result.set( "content", content );
	result.set( "isError", Value::boolean( isError ) );
	return result;
}

//=============================================================================
// tool implementations (main thread only)
//=============================================================================

Value ToolConsoleLog( const Value & args ) {
	int lines = 100;
	const Value * lv = ( args.type == mcpjson::T_OBJ ) ? args.get( "lines" ) : NULL;
	if ( lv != NULL && lv->type == mcpjson::T_NUM ) {
		lines = (int) lv->num;
		if ( lines < 1 ) {
			lines = 1;
		}
		if ( lines > 2000 ) {
			lines = 2000;
		}
	}

	std::string out;
	{
		std::lock_guard<std::mutex> lk( g_logMu );
		size_t n = g_logLines.size();
		size_t start = ( n > (size_t) lines ) ? n - (size_t) lines : 0;
		for ( size_t i = start; i < n; i++ ) {
			out += g_logLines[i];
			out += '\n';
		}
	}
	if ( out.empty() ) {
		out = "(no console output captured yet)\n";
	}
	return MakeToolResult( out );
}

Value ToolPlayerState() {
	Value o = Value::object();

	bool inGame = false;
	const char * stateName = "UNKNOWN";
	if ( session != NULL ) {
		stateName = SessionStateName( (int) session->GetState() );
		inGame = ( session->GetState() == idSession::INGAME );
	}
	o.set( "sessionState", Value::string( stateName ) );
	o.set( "map", Value::string( gameLocal.GetMapName() ) );

	idPlayer * player = NULL;
	if ( inGame ) {
		player = gameLocal.GetLocalPlayer();
	}

	if ( player != NULL ) {
		const idVec3 & origin = player->GetPhysics()->GetOrigin();
		Value pos = Value::object();
		pos.set( "x", Value::number( origin.x ) );
		pos.set( "y", Value::number( origin.y ) );
		pos.set( "z", Value::number( origin.z ) );
		o.set( "position", pos );

		Value angles = Value::object();
		angles.set( "pitch", Value::number( player->viewAngles.pitch ) );
		angles.set( "yaw", Value::number( player->viewAngles.yaw ) );
		angles.set( "roll", Value::number( player->viewAngles.roll ) );
		o.set( "viewAngles", angles );
		o.set( "health", Value::number( player->health ) );
	} else {
		inGame = false;
		o.set( "position", Value::null() );
		o.set( "viewAngles", Value::null() );
		o.set( "health", Value::null() );
	}
	o.set( "inGame", Value::boolean( inGame ) );

	return MakeToolResult( o.serialize( true ) );
}

// Executes an arbitrary console command (e.g. "setpos x y z", "noclip 1",
// "map game/mars_city2") on the main thread through the normal command buffer.
Value ToolConsoleCommand( const Value & args ) {
	const Value * cv = ( args.type == mcpjson::T_OBJ ) ? args.get( "command" ) : NULL;
	if ( cv == NULL || cv->type != mcpjson::T_STR || cv->str.empty() ) {
		return MakeToolResult( "missing required string argument 'command'", true );
	}
	std::string cmd = cv->str;
	if ( cmd.size() > 2048 ) {
		cmd.resize( 2048 );
	}
	// append to the command buffer: executed by idCommonLocal::Frame's
	// ExecuteCommandBuffer(), i.e. at the same point console input runs
	cmdSystem->BufferCommandText( CMD_EXEC_APPEND, cmd.c_str() );
	return MakeToolResult( "queued: " + cmd );
}

// Returns the subtitle manager's ring of recent sound-start events as
// JSON: every sound that started (shader name, length, whether it had
// a subtitle entry and was displayed). Immune to console-log flooding.
Value ToolSubtitleLog( const Value & args ) {
	int lines = 100;
	bool all = false;
	if ( args.type == mcpjson::T_OBJ ) {
		const Value * lv = args.get( "lines" );
		if ( lv != NULL && lv->type == mcpjson::T_NUM ) {
			lines = (int) lv->num;
			if ( lines < 1 ) {
				lines = 1;
			}
			if ( lines > idSubtitleManager::kMaxEvents ) {
				lines = idSubtitleManager::kMaxEvents;
			}
		}
		const Value * av = args.get( "all" );
		if ( av != NULL && av->type == mcpjson::T_BOOL ) {
			all = av->b;
		}
	}

	const idList< idSubtitleEvent, TAG_SUBTITLE > & events = subtitleManager.GetEvents();
	int n = events.Num();

	Value arr = Value::array();
	int shown = 0;
	// walk from the newest backwards, collecting up to 'lines' events
	for ( int i = n - 1; i >= 0 && shown < lines; i-- ) {
		const idSubtitleEvent & ev = events[i];
		if ( !all && ev.speaker.IsEmpty() ) {
			continue;	// default: only sounds that have a subtitle entry
		}
		Value e = Value::object();
		e.set( "time", Value::number( (double) ev.time ) );
		e.set( "shader", Value::string( ev.shader.c_str() ) );
		e.set( "speaker", Value::string( ev.speaker.c_str() ) );
		e.set( "cues", Value::number( (double) ev.numCues ) );
		e.set( "lenMs", Value::number( (double) ev.lengthMs ) );
		e.set( "displayed", Value::boolean( ev.displayed ) );
		arr.push( e );
		shown++;
	}

	Value o = Value::object();
	o.set( "total", Value::number( (double) n ) );
	o.set( "events", arr );
	return MakeToolResult( o.serialize( true ) );
}

//=============================================================================
// pointed_entity tool: what the crosshair is on + what the frob highlight has
//=============================================================================

static void JsonSetVec3( Value & o, const char * key, const idVec3 & v ) {
	Value a = Value::array();
	a.push( Value::number( v.x ) );
	a.push( Value::number( v.y ) );
	a.push( Value::number( v.z ) );
	o.set( key, a );
}

static Value JsonEntityInfo( idEntity * ent ) {
	Value e = Value::object();
	if ( ent == NULL ) {
		return e;
	}
	e.set( "classname", Value::string( ent->spawnArgs.GetString( "classname", "" ) ) );
	e.set( "name", Value::string( ent->GetName() ) );
	e.set( "entityNumber", Value::number( (double) ent->entityNumber ) );
	idPhysics * phys = ent->GetPhysics();
	if ( phys != NULL ) {
		JsonSetVec3( e, "origin", phys->GetOrigin() );
	} else {
		idVec3 o;
		ent->spawnArgs.GetVector( "origin", "0 0 0", o );
		JsonSetVec3( e, "origin", o );
	}
	const renderEntity_t * re = ent->GetRenderEntity();
	if ( re != NULL ) {
		if ( re->hModel != NULL ) {
			e.set( "model", Value::string( re->hModel->Name() ) );
		}
		Value guis = Value::array();
		for ( int i = 0; i < 3; i++ ) {
			if ( re->gui[i] != NULL ) {
				guis.push( Value::string( re->gui[i]->Name() ) );
			}
		}
		e.set( "guis", guis );
	}
	e.set( "spawnModel", Value::string( ent->spawnArgs.GetString( "model", "" ) ) );
	e.set( "spawnGui", Value::string( ent->spawnArgs.GetString( "gui", "" ) ) );
	e.set( "spawnGui2", Value::string( ent->spawnArgs.GetString( "gui2", "" ) ) );
	e.set( "target", Value::string( ent->spawnArgs.GetString( "target", "" ) ) );
	e.set( "targetname", Value::string( ent->spawnArgs.GetString( "targetname", "" ) ) );
	Value args = Value::object();
	int n = ent->spawnArgs.GetNumKeyVals();
	for ( int i = 0; i < n && i < 128; i++ ) {
		const idKeyValue * kv = ent->spawnArgs.GetKeyVal( i );
		if ( kv != NULL ) {
			args.set( kv->GetKey().c_str(), Value::string( kv->GetValue().c_str() ) );
		}
	}
	e.set( "spawnArgs", args );
	return e;
}

Value ToolPointedEntity() {
	Value o = Value::object();
	bool inGame = ( session != NULL && session->GetState() == idSession::INGAME );
	o.set( "sessionState", Value::string( session != NULL ? SessionStateName( (int) session->GetState() ) : "UNKNOWN" ) );
	o.set( "inGame", Value::boolean( inGame ) );
	o.set( "map", Value::string( gameLocal.GetMapName() ) );

	idPlayer * player = inGame ? gameLocal.GetLocalPlayer() : NULL;
	if ( player == NULL ) {
		return MakeToolResult( o.serialize( true ) );
	}

	idVec3 start = player->GetEyePosition();
	idVec3 dir = player->viewAngles.ToForward();
	idVec3 end = start + dir * 4096.0f;
	JsonSetVec3( o, "eye", start );
	JsonSetVec3( o, "forward", dir );

	trace_t tr;
	gameLocal.clip.TracePoint( tr, start, end, MASK_SHOT_RENDERMODEL, player );
	o.set( "traceFraction", Value::number( tr.fraction ) );
	JsonSetVec3( o, "tracePoint", tr.c.point );
	o.set( "traceEntityNum", Value::number( (double) tr.c.entityNum ) );

	if ( tr.c.entityNum >= 0 && tr.c.entityNum < MAX_GENTITIES && tr.c.entityNum != ENTITYNUM_WORLD ) {
		idEntity * ent = gameLocal.entities[ tr.c.entityNum ];
		if ( ent != NULL ) {
			o.set( "traceEntity", JsonEntityInfo( ent ) );
			if ( gameRenderWorld != NULL && ent->GetRenderEntity() != NULL ) {
				guiPoint_t gp = gameRenderWorld->GuiTrace( ent->GetModelDefHandle(), start, end );
				if ( gp.x != -1.0f ) {
					Value g = Value::object();
					g.set( "u", Value::number( gp.x ) );
					g.set( "v", Value::number( gp.y ) );
					g.set( "guiId", Value::number( gp.guiId ) );
					o.set( "guiTrace", g );
				}
			}
		}
	} else if ( tr.c.entityNum == ENTITYNUM_WORLD ) {
		o.set( "traceEntityClass", Value::string( "worldspawn (brush geometry)" ) );
	}

	// the frob-highlighted entity (what the game focus logic currently has)
	if ( player->GetFocusGUIent() != NULL ) {
		Value f = JsonEntityInfo( player->GetFocusGUIent() );
		if ( player->GetFocusUI() != NULL ) {
			f.set( "focusGui", Value::string( player->GetFocusUI()->Name() ) );
		}
		o.set( "focusGUIent", f );
	}
	o.set( "focusCharacter", Value::boolean( player->HasFocusCharacter() ) );
	o.set( "focusVehicle", Value::boolean( player->HasFocusVehicle() ) );

	return MakeToolResult( o.serialize( true ) );
}

//=============================================================================
// screen_pixels tool: read back a rect of the last RENDERED frame so static
// noise vs. real video can be told apart numerically (stddev) without
// screenshots. Args: x, y, w, h in screen pixels, top-left origin.
//
// The readback cannot happen here (this tool runs at the start of the frame,
// and on Xvfb/llvmpipe the post-swap back buffer reads black), so this arms a
// probe rect; mcp::RenderThreadPixelProbe() — called from
// GL_BlockingSwapBuffers right before the swap — does glReadPixels(GL_BACK)
// while the frame is still valid. Protocol: call once to arm, call again next
// frame to get the captured stats (each call also re-arms for the next frame).
//=============================================================================

static void ComputeProbeStats( const uint8_t * buf, int w, int h, PixelProbeResult & r ) {
	double sum[4] = { 0, 0, 0, 0 };
	double sumsq[4] = { 0, 0, 0, 0 };
	const size_t n = (size_t) w * (size_t) h;
	uint32_t cksum = 2166136261u;
	double gridCnt[4][4];
	double diffX = 0, diffY = 0;
	size_t diffXN = 0, diffYN = 0;
	memset( r.grid, 0, sizeof( r.grid ) );
	memset( gridCnt, 0, sizeof( gridCnt ) );

	for ( size_t i = 0; i < n; i++ ) {
		const uint8_t * px = &buf[i * 4];
		for ( int c = 0; c < 4; c++ ) {
			sum[c] += px[c];
			sumsq[c] += (double) px[c] * px[c];
			cksum = ( cksum ^ px[c] ) * 16777619u;
		}
		int gx = (int) ( ( i % (size_t) w ) * 4 / (size_t) w );
		int gy = (int) ( ( i / (size_t) w ) * 4 / (size_t) h );
		for ( int c = 0; c < 3; c++ ) {
			r.grid[gy][gx][c] += px[c];
		}
		gridCnt[gy][gx] += 1.0;
		// neighbor differences (spatial structure: static noise is uncorrelated)
		if ( ( i % (size_t) w ) + 1 < (size_t) w ) {
			const uint8_t * nx = &buf[( i + 1 ) * 4];
			for ( int c = 0; c < 3; c++ ) {
				diffX += fabs( (double) nx[c] - px[c] );
			}
			diffXN++;
		}
		if ( i + (size_t) w < n ) {
			const uint8_t * ny = &buf[( i + (size_t) w ) * 4];
			for ( int c = 0; c < 3; c++ ) {
				diffY += fabs( (double) ny[c] - px[c] );
			}
			diffYN++;
		}
	}
	r.meanAbsDiffX = diffXN > 0 ? diffX / (double) diffXN : 0.0;
	r.meanAbsDiffY = diffYN > 0 ? diffY / (double) diffYN : 0.0;

	for ( int c = 0; c < 4; c++ ) {
		double m = sum[c] / (double) n;
		double var = sumsq[c] / (double) n - m * m;
		if ( var < 0 ) { var = 0; }
		r.mean[c] = m;
		r.stddev[c] = sqrt( var );
	}
	r.checksum = cksum;
	for ( int gy = 0; gy < 4; gy++ ) {
		for ( int gx = 0; gx < 4; gx++ ) {
			double cnt = gridCnt[gy][gx] > 0 ? gridCnt[gy][gx] : 1.0;
			for ( int c = 0; c < 3; c++ ) {
				r.grid[gy][gx][c] /= cnt;
			}
		}
	}
}

static Value ProbeResultToJson( const PixelProbeResult & r, int currentFrame ) {
	Value o = Value::object();
	o.set( "frame", Value::number( (double) currentFrame ) );
	o.set( "capturedFrame", Value::number( (double) r.frame ) );
	Value res = Value::object();
	res.set( "width", Value::number( glConfig.nativeScreenWidth ) );
	res.set( "height", Value::number( glConfig.nativeScreenHeight ) );
	o.set( "resolution", res );
	Value rect = Value::object();
	rect.set( "x", Value::number( r.x ) );
	rect.set( "y", Value::number( r.y ) );
	rect.set( "w", Value::number( r.w ) );
	rect.set( "h", Value::number( r.h ) );
	o.set( "rect", rect );
	if ( !r.valid ) {
		o.set( "ok", Value::boolean( false ) );
		char eb[64];
		snprintf( eb, sizeof( eb ), "glReadPixels error 0x%x", r.glError );
		o.set( "error", Value::string( eb ) );
		return MakeToolResult( o.serialize( true ) );
	}
	Value mean = Value::array();
	Value stddev = Value::array();
	for ( int c = 0; c < 4; c++ ) {
		mean.push( Value::number( r.mean[c] ) );
		stddev.push( Value::number( r.stddev[c] ) );
	}
	o.set( "meanRGBA", mean );
	o.set( "stddevRGBA", stddev );
	o.set( "checksum", Value::number( (double) r.checksum ) );
	o.set( "meanAbsDiffX", Value::number( r.meanAbsDiffX ) );
	o.set( "meanAbsDiffY", Value::number( r.meanAbsDiffY ) );
	Value g = Value::array();
	for ( int gy = 0; gy < 4; gy++ ) {
		Value row = Value::array();
		for ( int gx = 0; gx < 4; gx++ ) {
			Value cell = Value::array();
			for ( int c = 0; c < 3; c++ ) {
				cell.push( Value::number( r.grid[gy][gx][c] ) );
			}
			row.push( cell );
		}
		g.push( row );
	}
	o.set( "grid4x4meanRGB", g );
	o.set( "ok", Value::boolean( true ) );
	return MakeToolResult( o.serialize( true ) );
}

Value ToolScreenPixels( const Value & args ) {
	int x = 0, y = 0, w = 0, h = 0;
	if ( args.type == mcpjson::T_OBJ ) {
		const Value * v;
		if ( ( v = args.get( "x" ) ) != NULL && v->type == mcpjson::T_NUM ) { x = (int) v->num; }
		if ( ( v = args.get( "y" ) ) != NULL && v->type == mcpjson::T_NUM ) { y = (int) v->num; }
		if ( ( v = args.get( "w" ) ) != NULL && v->type == mcpjson::T_NUM ) { w = (int) v->num; }
		if ( ( v = args.get( "h" ) ) != NULL && v->type == mcpjson::T_NUM ) { h = (int) v->num; }
	}

	Value o = Value::object();
	o.set( "frame", Value::number( (double) idLib::frameNumber ) );
	Value res = Value::object();
	res.set( "width", Value::number( glConfig.nativeScreenWidth ) );
	res.set( "height", Value::number( glConfig.nativeScreenHeight ) );
	o.set( "resolution", res );

	const int W = glConfig.nativeScreenWidth;
	const int H = glConfig.nativeScreenHeight;
	if ( x < 0 ) { x = 0; }
	if ( y < 0 ) { y = 0; }
	if ( w > W - x ) { w = W - x; }
	if ( h > H - y ) { h = H - y; }

	if ( w <= 0 || h <= 0 || W <= 0 || H <= 0 ) {
		o.set( "ok", Value::boolean( false ) );
		o.set( "error", Value::string( "need integer args x,y,w,h (top-left origin, screen pixels) inside the window" ) );
		return MakeToolResult( o.serialize( true ) );
	}

	Value rect = Value::object();
	rect.set( "x", Value::number( x ) );
	rect.set( "y", Value::number( y ) );
	rect.set( "w", Value::number( w ) );
	rect.set( "h", Value::number( h ) );
	o.set( "rect", rect );

	// handshake: hand back the previous capture (if not yet returned), and
	// (re-)arm the probe so RenderThreadPixelProbe captures this frame's rect
	// at the swap
	std::lock_guard<std::mutex> lk( g_probeMu );
	g_probeReq.pending = true;
	g_probeReq.x = x; g_probeReq.y = y; g_probeReq.w = w; g_probeReq.h = h;
	g_probeReq.reqFrame = idLib::frameNumber;

	if ( g_probeRes.valid && g_probeRes.frame != g_probeResReturned ) {
		g_probeResReturned = g_probeRes.frame;
		return ProbeResultToJson( g_probeRes, idLib::frameNumber );
	}

	o.set( "ok", Value::boolean( false ) );
	o.set( "pending", Value::boolean( true ) );
	o.set( "note", Value::string( "probe armed; captured at the next swap — call screen_pixels again with the same rect to get the pixels" ) );
	return MakeToolResult( o.serialize( true ) );
}

Value ToolRenderStats( const Value & args ) {
	bool wantEntities = false;
	const Value * ev = ( args.type == mcpjson::T_OBJ ) ? args.get( "entities" ) : NULL;
	if ( ev != NULL && ev->type == mcpjson::T_BOOL ) {
		wantEntities = ev->b;
	}

	Value o = Value::object();
	o.set( "fps", Value::number( g_fpsEma ) );
	o.set( "frame", Value::number( (double) idLib::frameNumber ) );

	Value res = Value::object();
	res.set( "width", Value::number( glConfig.nativeScreenWidth ) );
	res.set( "height", Value::number( glConfig.nativeScreenHeight ) );
	o.set( "resolution", res );
	o.set( "map", Value::string( gameLocal.GetMapName() ) );

	ViewSnapshot snap;
	{
		std::lock_guard<std::mutex> lk( g_viewMu );
		snap = g_view;
	}

	Value view = Value::object();
	view.set( "valid", Value::boolean( snap.valid ) );
	view.set( "frame", Value::number( snap.frame ) );
	view.set( "numDrawSurfs", Value::number( snap.numDrawSurfs ) );
	view.set( "numViewLights", Value::number( snap.numViewLights ) );
	view.set( "numViewEntities", Value::number( snap.numViewEntities ) );
	view.set( "is2Dgui", Value::boolean( snap.is2Dgui ) );
	view.set( "areaNum", Value::number( snap.areaNum ) );
	Value mats = Value::array();
	for ( size_t i = 0; i < snap.materials.size(); i++ ) {
		mats.push( Value::string( snap.materials[i] ) );
	}
	view.set( "materials", mats );
	if ( wantEntities ) {
		Value ents = Value::array();
		for ( size_t i = 0; i < snap.entityModels.size(); i++ ) {
			ents.push( Value::string( snap.entityModels[i] ) );
		}
		view.set( "entityModels", ents );
	}
	o.set( "view", view );

	Value fe = Value::object();
	fe.set( "numViews", Value::number( tr.pc.c_numViews ) );
	fe.set( "viewLights", Value::number( tr.pc.c_viewLights ) );
	fe.set( "visibleViewEntities", Value::number( tr.pc.c_visibleViewEntities ) );
	fe.set( "shadowViewEntities", Value::number( tr.pc.c_shadowViewEntities ) );
	fe.set( "createInteractions", Value::number( tr.pc.c_createInteractions ) );
	o.set( "frontend", fe );

	Value be = Value::object();
	be.set( "surfaces", Value::number( backEnd.pc.c_surfaces ) );
	be.set( "shaders", Value::number( backEnd.pc.c_shaders ) );
	be.set( "drawElements", Value::number( backEnd.pc.c_drawElements ) );
	be.set( "drawIndexes", Value::number( backEnd.pc.c_drawIndexes ) );
	be.set( "shadowElements", Value::number( backEnd.pc.c_shadowElements ) );
	be.set( "shadowIndexes", Value::number( backEnd.pc.c_shadowIndexes ) );
	be.set( "totalMicroSec", Value::number( backEnd.pc.totalMicroSec ) );
	be.set( "shadowMicroSec", Value::number( backEnd.pc.shadowMicroSec ) );
	o.set( "backend", be );

	Value tim = Value::object();
	tim.set( "gameThreadTotalMs", Value::number( commonLocal.GetGameThreadTotalTime() ) );
	tim.set( "rendererBackEndUs", Value::number( commonLocal.GetRendererBackEndMicroseconds() ) );
	tim.set( "rendererGpuUs", Value::number( commonLocal.GetRendererGPUMicroseconds() ) );
	tim.set( "rendererIdleUs", Value::number( commonLocal.GetRendererIdleMicroseconds() ) );
	tim.set( "rendererShadowsUs", Value::number( commonLocal.GetRendererShadowsMicroseconds() ) );
	o.set( "timings", tim );

	return MakeToolResult( o.serialize( true ) );
}

void ExecuteTool( PendingRequest & req ) {
	std::string name;
	const Value * nameValue = ( req.params.type == mcpjson::T_OBJ ) ? req.params.get( "name" ) : NULL;
	if ( nameValue != NULL && nameValue->type == mcpjson::T_STR ) {
		name = nameValue->str;
	}

	Value args = Value::object();
	const Value * argsValue = ( req.params.type == mcpjson::T_OBJ ) ? req.params.get( "arguments" ) : NULL;
	if ( argsValue != NULL && argsValue->type == mcpjson::T_OBJ ) {
		args = *argsValue;
	}

	if ( name == "console_log" ) {
		req.result = ToolConsoleLog( args );
	} else if ( name == "console_command" ) {
		req.result = ToolConsoleCommand( args );
	} else if ( name == "subtitle_log" ) {
		req.result = ToolSubtitleLog( args );
	} else if ( name == "player_state" ) {
		req.result = ToolPlayerState();
	} else if ( name == "pointed_entity" ) {
		req.result = ToolPointedEntity();
	} else if ( name == "screen_pixels" ) {
		req.result = ToolScreenPixels( args );
	} else if ( name == "render_stats" ) {
		req.result = ToolRenderStats( args );
	} else {
		req.errorCode = -32602;
		req.errorMsg = "Unknown tool: " + name;
	}
}

//=============================================================================
// JSON-RPC 2.0 / MCP
//=============================================================================

Value ToolsList() {
	Value tools = Value::array();

	{
		Value t = Value::object();
		t.set( "name", Value::string( "console_log" ) );
		t.set( "description", Value::string(
			"Read the tail of the game console log. Lines are captured from engine Printf output "
			"(boot output before the server starts is not captured)." ) );
		Value schema = Value::object();
		schema.set( "type", Value::string( "object" ) );
		Value props = Value::object();
		Value lines = Value::object();
		lines.set( "type", Value::string( "integer" ) );
		lines.set( "description", Value::string( "number of trailing lines to return (default 100, max 2000)" ) );
		lines.set( "default", Value::number( 100 ) );
		lines.set( "maximum", Value::number( 2000 ) );
		props.set( "lines", lines );
		schema.set( "properties", props );
		t.set( "inputSchema", schema );
		tools.push( t );
	}

	{
		Value t = Value::object();
		t.set( "name", Value::string( "console_command" ) );
		t.set( "description", Value::string(
			"Execute an arbitrary console command on the main thread via the normal command buffer "
			"(e.g. \"setpos -640 -1848 300\", \"noclip 1\", \"set ui_subtitles 0\", \"map game/mars_city2\"). "
			"Execution happens at the next command-buffer point in the frame; observe effects via console_log." ) );
		Value schema = Value::object();
		schema.set( "type", Value::string( "object" ) );
		Value props = Value::object();
		Value command = Value::object();
		command.set( "type", Value::string( "string" ) );
		command.set( "description", Value::string( "console command line to execute (max 2048 chars)" ) );
		props.set( "command", command );
		schema.set( "properties", props );
		Value reqd = Value::array();
		reqd.push( Value::string( "command" ) );
		schema.set( "required", reqd );
		t.set( "inputSchema", schema );
		tools.push( t );
	}

	{
		Value t = Value::object();
		t.set( "name", Value::string( "subtitle_log" ) );
		t.set( "description", Value::string(
			"Recent sound-start events from the subtitle manager (shader name, length ms, subtitle "
			"speaker/cue count, whether a subtitle was displayed), newest last. This is the reliable way "
			"to see which dialogue actually fired; the console log is not used." ) );
		Value schema = Value::object();
		schema.set( "type", Value::string( "object" ) );
		Value props = Value::object();
		Value lines = Value::object();
		lines.set( "type", Value::string( "integer" ) );
		lines.set( "description", Value::string( "number of trailing events to return (default 100, max 512)" ) );
		lines.set( "default", Value::number( 100 ) );
		lines.set( "maximum", Value::number( 512 ) );
		props.set( "lines", lines );
		schema.set( "properties", props );
		t.set( "inputSchema", schema );
		tools.push( t );
	}

	{
		Value t = Value::object();
		t.set( "name", Value::string( "player_state" ) );
		t.set( "description", Value::string(
			"Current session state, map, and (when in game) player position, view angles and health." ) );
		Value schema = Value::object();
		schema.set( "type", Value::string( "object" ) );
		schema.set( "properties", Value::object() );
		t.set( "inputSchema", schema );
		tools.push( t );
	}

	{
		Value t = Value::object();
		t.set( "name", Value::string( "pointed_entity" ) );
		t.set( "description", Value::string(
			"What the player crosshair is aimed at and what the frob highlight currently has: a clip trace from "
			"the eye along viewAngles (fraction, hit point, entity number), full info on the hit entity "
			"(classname, name, origin, model, guis, target, all spawn args), a GuiTrace against its render model, "
			"and the current focusGUIent/focusUI (the highlighted interactive panel)." ) );
		Value schema = Value::object();
		schema.set( "type", Value::string( "object" ) );
		schema.set( "properties", Value::object() );
		t.set( "inputSchema", schema );
		tools.push( t );
	}

	{
		Value t = Value::object();
		t.set( "name", Value::string( "screen_pixels" ) );
		t.set( "description", Value::string(
			"Read back a rectangle of the last rendered frame (glReadPixels on GL_BACK, captured pre-swap by the "
			"renderer) and return per-channel mean/stddev, an FNV checksum and a 4x4 grid of mean RGB — enough to "
			"tell static noise (high stddev, checksum changes every frame) from a working videoMap screen without "
			"screenshots. x,y,w,h are screen pixels, top-left origin. Two-call protocol: the first call arms the "
			"probe, the second call (next frame) returns the captured stats and re-arms." ) );
		Value schema = Value::object();
		schema.set( "type", Value::string( "object" ) );
		Value props = Value::object();
		const char * names[4] = { "x", "y", "w", "h" };
		const char * descs[4] = { "left edge, px", "top edge, px", "width, px", "height, px" };
		for ( int i = 0; i < 4; i++ ) {
			Value p = Value::object();
			p.set( "type", Value::string( "integer" ) );
			p.set( "description", Value::string( descs[i] ) );
			props.set( names[i], p );
		}
		schema.set( "properties", props );
		Value reqd = Value::array();
		reqd.push( Value::string( "x" ) );
		reqd.push( Value::string( "y" ) );
		reqd.push( Value::string( "w" ) );
		reqd.push( Value::string( "h" ) );
		schema.set( "required", reqd );
		t.set( "inputSchema", schema );
		tools.push( t );
	}

	{
		Value t = Value::object();
		t.set( "name", Value::string( "render_stats" ) );
		t.set( "description", Value::string(
			"What the renderer is doing: fps, resolution, last view (draw surfaces, lights, entities, "
			"materials), front-end/back-end counters and frame timings." ) );
		Value schema = Value::object();
		schema.set( "type", Value::string( "object" ) );
		Value props = Value::object();
		Value entities = Value::object();
		entities.set( "type", Value::string( "boolean" ) );
		entities.set( "description", Value::string( "include the model names of view entities (default false)" ) );
		entities.set( "default", Value::boolean( false ) );
		props.set( "entities", entities );
		schema.set( "properties", props );
		t.set( "inputSchema", schema );
		tools.push( t );
	}

	Value result = Value::object();
	result.set( "tools", tools );
	return result;
}

Value MakeRpcResult( const Value & id, const Value & result ) {
	Value r = Value::object();
	r.set( "jsonrpc", Value::string( "2.0" ) );
	r.set( "id", id );
	r.set( "result", result );
	return r;
}

Value MakeRpcError( const Value & id, int code, const std::string & message ) {
	Value r = Value::object();
	r.set( "jsonrpc", Value::string( "2.0" ) );
	r.set( "id", id );
	Value e = Value::object();
	e.set( "code", Value::number( code ) );
	e.set( "message", Value::string( message ) );
	r.set( "error", e );
	return r;
}

// Returns the JSON response body; sets isNotification for requests without an id
// (the caller answers those with HTTP 202 and no body).
std::string HandleRpc( const std::string & body, bool & isNotification ) {
	isNotification = false;

	Value req;
	std::string parseError;
	if ( !mcpjson::parse( body, req, &parseError ) || req.type != mcpjson::T_OBJ ) {
		return MakeRpcError( Value::null(), -32700, "Parse error: " + parseError ).serialize();
	}

	const bool hasId = req.has( "id" );
	Value id = hasId ? *req.get( "id" ) : Value::null();

	std::string method;
	const Value * mv = req.get( "method" );
	if ( mv != NULL && mv->type == mcpjson::T_STR ) {
		method = mv->str;
	}

	Value params = Value::object();
	const Value * pv = req.get( "params" );
	if ( pv != NULL && pv->type == mcpjson::T_OBJ ) {
		params = *pv;
	}

	if ( !hasId ) {
		// notification: no response body
		isNotification = true;
		return "";
	}

	if ( method == "initialize" ) {
		std::string protocolVersion = "2025-06-18";
		const Value * cv = params.get( "protocolVersion" );
		if ( cv != NULL && cv->type == mcpjson::T_STR ) {
			const std::string & clientVersion = cv->str;
			if ( clientVersion == "2024-11-05" ||
				 clientVersion == "2025-03-26" ||
				 clientVersion == "2025-06-18" ) {
				protocolVersion = clientVersion;
			}
		}
		Value result = Value::object();
		result.set( "protocolVersion", Value::string( protocolVersion ) );
		Value caps = Value::object();
		Value toolsCap = Value::object();
		toolsCap.set( "listChanged", Value::boolean( false ) );
		caps.set( "tools", toolsCap );
		result.set( "capabilities", caps );
		Value serverInfo = Value::object();
		serverInfo.set( "name", Value::string( "doom3bfg" ) );
		serverInfo.set( "version", Value::string( "1.0.0" ) );
		result.set( "serverInfo", serverInfo );
		return MakeRpcResult( id, result ).serialize();
	}

	if ( method == "ping" ) {
		return MakeRpcResult( id, Value::object() ).serialize();
	}

	if ( method == "tools/list" ) {
		return MakeRpcResult( id, ToolsList() ).serialize();
	}

	if ( method == "tools/call" ) {
		std::shared_ptr<PendingRequest> pending( new PendingRequest() );
		pending->id = id;
		pending->method = method;
		pending->params = params;

		{
			std::lock_guard<std::mutex> lk( g_qMu );
			g_queue.push( pending );
		}

		std::unique_lock<std::mutex> lk( pending->mu );
		const bool completed = pending->cv.wait_for( lk, std::chrono::seconds( 5 ),
			[ &pending ]() { return pending->done; } );
		if ( !completed ) {
			return MakeRpcError( id, -32603,
				"Internal error: tool call timed out (main thread busy)" ).serialize();
		}
		if ( pending->errorCode != 0 ) {
			return MakeRpcError( id, pending->errorCode, pending->errorMsg ).serialize();
		}
		return MakeRpcResult( id, pending->result ).serialize();
	}

	return MakeRpcError( id, -32601, "Method not found: " + method ).serialize();
}

//=============================================================================
// HTTP
//=============================================================================

void SendResponse( int fd, int code, const char * status, const char * contentType, const std::string & body ) {
	char header[512];
	int n = snprintf( header, sizeof( header ),
		"HTTP/1.1 %d %s\r\n"
		"Content-Type: %s\r\n"
		"Content-Length: %d\r\n"
		"Connection: close\r\n"
		"\r\n",
		code, status, contentType, (int) body.size() );
	if ( n > 0 ) {
		send( fd, header, (size_t) n, MSG_NOSIGNAL );
	}
	size_t offset = 0;
	while ( offset < body.size() ) {
		ssize_t sent = send( fd, body.data() + offset, body.size() - offset, MSG_NOSIGNAL );
		if ( sent <= 0 ) {
			break;
		}
		offset += (size_t) sent;
	}
}

void HandleConnection( int fd ) {
	// read the request head
	std::string request;
	char buf[4096];
	size_t headerEnd = std::string::npos;
	for ( ;; ) {
		ssize_t n = recv( fd, buf, sizeof( buf ), 0 );
		if ( n <= 0 ) {
			return;
		}
		request.append( buf, (size_t) n );
		headerEnd = request.find( "\r\n\r\n" );
		if ( headerEnd != std::string::npos ) {
			break;
		}
		if ( request.size() > 65536 ) {
			return;
		}
	}

	// request line
	const std::string requestLine = request.substr( 0, request.find( "\r\n" ) );
	char httpMethod[16] = { 0 };
	char path[256] = { 0 };
	char httpVersion[16] = { 0 };
	if ( sscanf( requestLine.c_str(), "%15s %255s %15s", httpMethod, path, httpVersion ) != 3 ) {
		SendResponse( fd, 400, "Bad Request", "text/plain", "malformed request line\n" );
		return;
	}

	// content-length
	size_t contentLength = 0;
	{
		const std::string headers = request.substr( 0, headerEnd );
		for ( size_t i = 0; i + 15 <= headers.size(); i++ ) {
			if ( strncasecmp( headers.c_str() + i, "content-length:", 15 ) == 0 ) {
				contentLength = (size_t) atol( headers.c_str() + i + 15 );
				break;
			}
		}
	}
	if ( contentLength > 1024 * 1024 ) {
		SendResponse( fd, 413, "Payload Too Large", "text/plain", "request body too large\n" );
		return;
	}

	// body
	std::string body = request.substr( headerEnd + 4 );
	while ( body.size() < contentLength ) {
		ssize_t n = recv( fd, buf, sizeof( buf ), 0 );
		if ( n <= 0 ) {
			return;
		}
		body.append( buf, (size_t) n );
	}

	// strip query string
	std::string route = path;
	size_t queryPos = route.find( '?' );
	if ( queryPos != std::string::npos ) {
		route = route.substr( 0, queryPos );
	}

	if ( strcmp( httpMethod, "GET" ) == 0 && route == "/health" ) {
		SendResponse( fd, 200, "OK", "text/plain", "ok\n" );
		return;
	}

	if ( strcmp( httpMethod, "GET" ) == 0 && ( route == "/" || route == "/info" ) ) {
		Value info = Value::object();
		info.set( "name", Value::string( "doom3bfg-mcp" ) );
		info.set( "version", Value::string( "1.0.0" ) );
		info.set( "transport", Value::string( "streamable-http" ) );
		info.set( "endpoint", Value::string( "/mcp" ) );
		Value tools = Value::array();
		tools.push( Value::string( "console_log" ) );
		tools.push( Value::string( "console_command" ) );
		tools.push( Value::string( "subtitle_log" ) );
		tools.push( Value::string( "player_state" ) );
		tools.push( Value::string( "pointed_entity" ) );
		tools.push( Value::string( "screen_pixels" ) );
		tools.push( Value::string( "render_stats" ) );
		info.set( "tools", tools );
		SendResponse( fd, 200, "OK", "application/json", info.serialize( true ) );
		return;
	}

	if ( route == "/mcp" && strcmp( httpMethod, "POST" ) == 0 ) {
		bool isNotification = false;
		const std::string response = HandleRpc( body, isNotification );
		if ( isNotification ) {
			SendResponse( fd, 202, "Accepted", "application/json", "" );
		} else {
			SendResponse( fd, 200, "OK", "application/json", response );
		}
		return;
	}

	if ( route == "/mcp" && strcmp( httpMethod, "GET" ) == 0 ) {
		SendResponse( fd, 405, "Method Not Allowed", "text/plain",
			"use POST for JSON-RPC; GET /mcp (SSE stream) is not supported\n" );
		return;
	}

	SendResponse( fd, 404, "Not Found", "text/plain", "not found\n" );
}

void ServerThread( int port ) {
	int listenFd = socket( AF_INET, SOCK_STREAM, 0 );
	if ( listenFd < 0 ) {
		g_running = false;
		return;
	}

	int one = 1;
	setsockopt( listenFd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof( one ) );

	sockaddr_in addr;
	memset( &addr, 0, sizeof( addr ) );
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
	addr.sin_port = htons( (unsigned short) port );

	if ( bind( listenFd, (sockaddr *) &addr, sizeof( addr ) ) != 0 ||
		 listen( listenFd, 8 ) != 0 ) {
		SDL_Log( "MCP: failed to listen on 127.0.0.1:%d: %s", port, strerror( errno ) );
		close( listenFd );
		g_running = false;
		return;
	}

	fcntl( listenFd, F_SETFL, fcntl( listenFd, F_GETFL, 0 ) | O_NONBLOCK );

	SDL_Log( "MCP: listening on http://127.0.0.1:%d/mcp", port );

	while ( g_running ) {
		pollfd pfd;
		pfd.fd = listenFd;
		pfd.events = POLLIN;
		pfd.revents = 0;

		int ready = poll( &pfd, 1, 250 );
		if ( ready <= 0 ) {
			continue;
		}

		int clientFd = accept( listenFd, NULL, NULL );
		if ( clientFd < 0 ) {
			continue;
		}

		setsockopt( clientFd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof( one ) );
		timeval tv;
		tv.tv_sec = 10;
		tv.tv_usec = 0;
		setsockopt( clientFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof( tv ) );
		setsockopt( clientFd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof( tv ) );

		HandleConnection( clientFd );
		close( clientFd );
	}

	close( listenFd );
}

void McpLogOutput( void * userdata, int category, SDL_LogPriority priority, const char * message ) {
	if ( g_prevLogFn != NULL ) {
		g_prevLogFn( g_prevLogData, category, priority, message );
	}
	mcp::LogLine( message );
}

} // anonymous namespace

//=============================================================================
// public API
//=============================================================================

// Called from GL_BlockingSwapBuffers() right before GLimp_SwapBuffers().
void mcp::RenderThreadPixelProbe() {
	PixelProbeRequest req;
	{
		std::lock_guard<std::mutex> lk( g_probeMu );
		if ( !g_probeReq.pending ) {
			return;
		}
		req = g_probeReq;
		g_probeReq.pending = false;		// capture exactly one frame per arm
	}

	const int W = glConfig.nativeScreenWidth;
	const int H = glConfig.nativeScreenHeight;
	if ( W <= 0 || H <= 0 || req.w <= 0 || req.h <= 0 ) {
		return;
	}

	std::vector<uint8_t> buf( (size_t) req.w * (size_t) req.h * 4 );
	glReadBuffer( GL_BACK );	// the just-rendered frame, still valid pre-swap
	glReadPixels( req.x, H - ( req.y + req.h ), req.w, req.h, GL_RGBA, GL_UNSIGNED_BYTE, buf.data() );
	const GLenum glErr = glGetError();

	PixelProbeResult r;
	r.frame = idLib::frameNumber;
	r.x = req.x; r.y = req.y; r.w = req.w; r.h = req.h;
	r.glError = (unsigned) glErr;
	if ( glErr == GL_NO_ERROR ) {
		ComputeProbeStats( buf.data(), req.w, req.h, r );
		r.valid = true;
	}

	std::lock_guard<std::mutex> lk( g_probeMu );
	g_probeRes = r;
}

void mcp::LogLine( const char * text ) {
	if ( text == NULL || text[0] == '\0' ) {
		return;
	}

	int cap = g_logCap.load();
	if ( cap < 16 ) {
		cap = 16;
	}

	std::lock_guard<std::mutex> lk( g_logMu );

	// split on newlines; a call boundary also ends a line
	std::string segment;
	for ( const char * p = text; ; p++ ) {
		const char c = *p;
		if ( c == '\n' || c == '\0' ) {
			if ( !segment.empty() ) {
				g_logLines.push_back( segment );
				segment.clear();
			}
			if ( c == '\0' ) {
				break;
			}
		} else {
			segment += c;
		}
	}

	while ( (int) g_logLines.size() > cap ) {
		g_logLines.pop_front();
	}
}

void mcp::SnapshotView( const viewDef_t * viewDef ) {
	if ( viewDef == NULL ) {
		return;
	}

	ViewSnapshot snap;
	snap.frame = idLib::frameNumber;
	snap.numDrawSurfs = viewDef->numDrawSurfs;
	snap.is2Dgui = viewDef->is2Dgui;
	snap.areaNum = viewDef->areaNum;

	for ( int i = 0; i < viewDef->numDrawSurfs; i++ ) {
		const drawSurf_t * surf = viewDef->drawSurfs[i];
		if ( surf != NULL && surf->material != NULL ) {
			AddUnique( snap.materials, surf->material->GetName(), MAX_MATERIALS );
		}
	}

	for ( const viewLight_t * light = viewDef->viewLights; light != NULL; light = light->next ) {
		snap.numViewLights++;
	}

	for ( const viewEntity_t * entity = viewDef->viewEntitys; entity != NULL; entity = entity->next ) {
		snap.numViewEntities++;
		if ( entity->entityDef != NULL && entity->entityDef->parms.hModel != NULL ) {
			AddUnique( snap.entityModels, entity->entityDef->parms.hModel->Name(), MAX_ENTITIES );
		}
	}

	std::lock_guard<std::mutex> lk( g_viewMu );
	g_view = snap;
	g_view.valid = true;
}

bool mcp::Start( int port ) {
	if ( g_running.exchange( true ) ) {
		return true;	// already running
	}

	signal( SIGPIPE, SIG_IGN );

	if ( !g_logHooked ) {
		SDL_LogGetOutputFunction( &g_prevLogFn, &g_prevLogData );
		SDL_LogSetOutputFunction( McpLogOutput, NULL );
		g_logHooked = true;
	}

	g_port = port;
	g_thread = std::thread( ServerThread, port );
	return true;
}

void mcp::Shutdown() {
	g_running = false;

	// fail any queued requests so a blocked server thread can finish
	{
		std::lock_guard<std::mutex> lk( g_qMu );
		while ( !g_queue.empty() ) {
			std::shared_ptr<PendingRequest> pending = g_queue.front();
			g_queue.pop();
			{
				std::lock_guard<std::mutex> rl( pending->mu );
				pending->done = true;
				pending->errorCode = -32603;
				pending->errorMsg = "Internal error: MCP server shutting down";
			}
			pending->cv.notify_all();
		}
	}

	if ( g_thread.joinable() ) {
		g_thread.join();
	}

	if ( g_logHooked ) {
		SDL_LogSetOutputFunction( g_prevLogFn, g_prevLogData );
		g_logHooked = false;
	}

	g_port = 0;
}

bool mcp::IsRunning() {
	return g_running.load();
}

void mcp::PumpFrame() {
	// fps, smoothed from frame-to-frame deltas
	static int lastMs = 0;
	const int now = Sys_Milliseconds();
	if ( lastMs > 0 && now > lastMs ) {
		const float instant = 1000.0f / (float) ( now - lastMs );
		g_fpsEma = ( g_fpsEma <= 0.0f ) ? instant : g_fpsEma * 0.9f + instant * 0.1f;
	}
	lastMs = now;

	g_logCap = mcp_maxLogLines.GetInteger();

	// start / stop from cvars
	const bool want = mcp_enable.GetBool();
	int port = mcp_port.GetInteger();
	if ( port < 1 || port > 65535 ) {
		port = 8787;
	}
	if ( want && ( !g_running || port != g_port ) ) {
		if ( g_running ) {
			mcp::Shutdown();
		}
		mcp::Start( port );
	} else if ( !want && g_running ) {
		mcp::Shutdown();
	}

	// service deferred tool calls on the main thread
	std::queue< std::shared_ptr<PendingRequest> > local;
	{
		std::lock_guard<std::mutex> lk( g_qMu );
		local.swap( g_queue );
	}

	while ( !local.empty() ) {
		std::shared_ptr<PendingRequest> pending = local.front();
		local.pop();

		ExecuteTool( *pending );

		{
			std::lock_guard<std::mutex> lk( pending->mu );
			pending->done = true;
		}
		pending->cv.notify_all();
	}
}
