/*
===============================================================================

Linux stub implementations for leaderboard functions (multiplayer - no-op)

===============================================================================
*/

#include "../../idlib/precompiled.h"
#include "../../sys/sys_stats.h"

const leaderboardDefinition_t * Sys_FindLeaderboardDef( int index ) {
	return NULL;
}

leaderboardDefinition_t * Sys_CreateLeaderboardDef( int index, int maxEntries, const columnDef_t * columns, rankOrder_t rankOrder, bool isLocal, bool isGlobal ) {
	return NULL;
}

void Sys_DestroyLeaderboardDefs() {
}

bool LeaderboardLocal_Init() {
	return true;
}

void LeaderboardLocal_Shutdown() {
}

bool LeaderboardLocal_Upload( lobbyUserID_t userID, int leaderboardIndex, column_t * stats ) {
	return true;
}