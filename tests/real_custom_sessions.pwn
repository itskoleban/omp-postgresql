// Isolated fixture: actual custom.pwn routing; only its RCON gate is bypassed.
#include <open.mp>
#include <omp-postgresql>
forward CustomInit();
forward CustomExit();
forward CustomConnection(connection, connection_state);
forward CustomConnect(playerid);
forward CustomDisconnect(playerid, reason);
forward CustomCommand(playerid, cmdtext[]);
forward OnEntityLoaded(job, playerid, session, request);
forward OnEntitySaved(job, playerid, session, request);
forward bool:FixtureAdmin(playerid);
forward bool:FixtureCancel(job);
#define OnGameModeInit CustomInit
#define OnGameModeExit CustomExit
#define OnPgConnection CustomConnection
#define OnPlayerConnect CustomConnect
#define OnPlayerDisconnect CustomDisconnect
#define OnPlayerCommandText CustomCommand
#define OnEntityLoaded CustomLoaded
#define OnEntitySaved CustomSaved
#define IsPlayerAdmin FixtureAdmin
#define pg_cancel FixtureCancel
#include "../examples/custom.pwn"
#undef OnGameModeInit
#undef OnGameModeExit
#undef OnPgConnection
#undef OnPlayerConnect
#undef OnPlayerDisconnect
#undef OnPlayerCommandText
#undef OnEntityLoaded
#undef OnEntitySaved
#undef IsPlayerAdmin
#undef pg_cancel
#include "lane_diagnostics.inc"

stock Snapshot(const event[], playerid) {
	new pending;
	for (new i = 0; i < REQUESTS; i++) if (Pending[playerid][i]) pending++;
	printf("QA event=%s p=%d connected=%d npc=%d token=%d account=%d score=%d ready=%d counter=%d pending=%d",
		event, playerid, IsPlayerConnected(playerid), IsPlayerNPC(playerid), Session[playerid],
		Account[playerid], GetPlayerScore(playerid), Ready, GetSVarInt(CUSTOM_TOKEN), pending);
}
public OnGameModeInit() {
	pg_log_config(PG_LOG_DEBUG);
	new result = CustomInit();
	StartLaneDiagnostics(Database, 2);
	return result;
}
public OnGameModeExit() { StopLaneDiagnostics(); return CustomExit(); }
public OnPgConnection(connection, connection_state) {
	new result = CustomConnection(connection, connection_state);
	printf("QA event=database state=%d ready=%d", connection_state, Ready);
	return result;
}
public OnPlayerConnect(playerid) {
	new result = CustomConnect(playerid), name[MAX_PLAYER_NAME+1];
	GetPlayerName(playerid, name, sizeof name);
	printf("QA event=connect p=%d name=%s", playerid, name);
	Snapshot("connected", playerid);
	return result;
}
public OnPlayerDisconnect(playerid, reason) {
	new result = CustomDisconnect(playerid, reason);
	Snapshot("disconnect_end", playerid);
	return result;
}
public OnPlayerCommandText(playerid, cmdtext[]) {
	if (!strcmp(cmdtext, "/qa state")) { Snapshot("snapshot", playerid); printf("QA event=control p=%d", playerid); return 1; }
	// Counter mutation is confined to this test gamemode, never the tutorial.
	if (!strcmp(cmdtext, "/qa tokenlast")) { SetSVarInt(CUSTOM_TOKEN, 2147483646); printf("QA event=control p=%d", playerid); return 1; }
	if (!strcmp(cmdtext, "/qa tokennegative")) { SetSVarInt(CUSTOM_TOKEN, -1); printf("QA event=control p=%d", playerid); return 1; }
	new result = CustomCommand(playerid, cmdtext);
	printf("QA event=submitted p=%d load=%d clan=%d save=%d property=%d", playerid,
		Pending[playerid][ACCOUNT_LOAD], Pending[playerid][CLAN_LOAD], Pending[playerid][ACCOUNT_SAVE], Pending[playerid][PROPERTY_SAVE]);
	return result;
}
stock bool:FixtureAdmin(playerid) { return IsPlayerConnected(playerid); }
stock bool:FixtureCancel(job) { printf("QA event=cancel job=%d forwarded=0", job); return true; }
public OnEntityLoaded(job, playerid, session, request) {
	printf("QA event=load_begin p=%d callback_token=%d callback_job=%d request=%d", playerid, session, job, request);
	Snapshot("load_owner", playerid);
	new result = CustomLoaded(job, playerid, session, request);
	printf("QA event=sql_closed job=%d closed=%d", job, pg_job_connection(job) == 0);
	Snapshot("load_end", playerid);
	return result;
}
public OnEntitySaved(job, playerid, session, request) {
	printf("QA event=save_begin p=%d callback_token=%d callback_job=%d request=%d", playerid, session, job, request);
	Snapshot("save_owner", playerid);
	new result = CustomSaved(job, playerid, session, request);
	printf("QA event=sql_closed job=%d closed=%d", job, pg_job_connection(job) == 0);
	Snapshot("save_end", playerid);
	return result;
}
