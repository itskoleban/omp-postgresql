// Test instrumentation around the actual tutorial; never install this gamemode.
// Connections and dialogs arrive through the ordinary player network protocol.
#include <open.mp>
#include <omp_postgresql>
#include <samp_bcrypt>
#define TUTORIAL_BCRYPT (1)

new bool:RetainWork;
new bool:DropProvider;
new ProviderCost = 12;
forward AccountInit();
forward AccountExit();
forward AccountConnection(connection, connection_state);
forward AccountConnect(playerid);
forward AccountDisconnect(playerid, reason);
forward AccountDialog(playerid, dialogid, response, listitem, inputtext[]);
forward OnAccountComplete(job, playerid, token);
forward OnAccountHashed(playerid, token);
forward OnAccountVerified(playerid, bool:success, token);
forward AuthDeadline(playerid, token);
forward bool:FixtureCancel(job);

#define OnGameModeInit AccountInit
#define OnGameModeExit AccountExit
#define OnPgConnection AccountConnection
#define OnPlayerConnect AccountConnect
#define OnPlayerDisconnect AccountDisconnect
#define OnDialogResponse AccountDialog
#define OnAccountComplete AccountComplete
#define OnAccountHashed AccountHashed
#define OnAccountVerified AccountVerified
#define AuthDeadline AccountDeadline
#define pg_cancel FixtureCancel
#define bcrypt_hash FixtureHash
#define bcrypt_verify FixtureVerify
#include "../examples/accounts.pwn"
#undef OnGameModeInit
#undef OnGameModeExit
#undef OnPgConnection
#undef OnPlayerConnect
#undef OnPlayerDisconnect
#undef OnDialogResponse
#undef OnAccountComplete
#undef OnAccountHashed
#undef OnAccountVerified
#undef AuthDeadline
#undef pg_cancel
#undef bcrypt_hash
#undef bcrypt_verify
#include "lane_diagnostics.inc"

stock Snapshot(const event[], playerid) {
	printf("QA event=%s p=%d connected=%d npc=%d token=%d state=%d job=%d account=%d score=%d ready=%d counter=%d",
		event, playerid, IsPlayerConnected(playerid), IsPlayerNPC(playerid), Token[playerid],
		AccountState[playerid], PendingJob[playerid], AccountID[playerid], GetPlayerScore(playerid), Ready, GetSVarInt(TOKEN_COUNTER));
}

public OnGameModeInit() {
	pg_log_config(PG_LOG_DEBUG);
	printf("QA event=init counter=%d", GetSVarInt(TOKEN_COUNTER));
	new result = AccountInit();
	StartLaneDiagnostics(Database, 1);
	return result;
}
public OnGameModeExit() {
	StopLaneDiagnostics();
	printf("QA event=exit");
	return AccountExit();
}
public OnPgConnection(connection, connection_state) {
	new result = AccountConnection(connection, connection_state);
	printf("QA event=database state=%d ready=%d", connection_state, Ready);
	return result;
}
public OnPlayerConnect(playerid) {
	new result = AccountConnect(playerid);
	new name[MAX_PLAYER_NAME+1]; GetPlayerName(playerid, name, sizeof name);
	printf("QA event=connect p=%d name=%s", playerid, name);
	Snapshot("connected", playerid);
	return result;
}
public OnPlayerDisconnect(playerid, reason) {
	Snapshot("disconnect_begin", playerid);
	new result = AccountDisconnect(playerid, reason);
	Snapshot("disconnect_end", playerid);
	return result;
}
public OnDialogResponse(playerid, dialogid, response, listitem, inputtext[]) {
	new result = AccountDialog(playerid, dialogid, response, listitem, inputtext);
	Snapshot("dialog", playerid);
	return result;
}
public OnAccountComplete(job, playerid, token) {
	new status = pg_job_status(job), sqlstate[6];
	if (pg_result_count(job)) pg_result_sqlstate(job, 0, sqlstate);
	printf("QA event=sql_begin p=%d callback_token=%d callback_job=%d status=%d sqlstate=%s", playerid, token, job, status, sqlstate);
	Snapshot("sql_owner", playerid);
	new result = AccountComplete(job, playerid, token);
	printf("QA event=sql_closed job=%d closed=%d", job, pg_job_connection(job) == 0);
	Snapshot("sql_end", playerid);
	return result;
}
public OnAccountHashed(playerid, token) {
	printf("QA event=hash_begin p=%d callback_token=%d", playerid, token);
	Snapshot("hash_owner", playerid);
	new result = AccountHashed(playerid, token);
	Snapshot("hash_end", playerid);
	return result;
}
public OnAccountVerified(playerid, bool:success, token) {
	printf("QA event=verify_begin p=%d callback_token=%d success=%d", playerid, token, success);
	Snapshot("verify_owner", playerid);
	new result = AccountVerified(playerid, success, token);
	Snapshot("verify_end", playerid);
	return result;
}
public AuthDeadline(playerid, token) {
	new result = AccountDeadline(playerid, token);
	printf("QA event=deadline p=%d callback_token=%d", playerid, token);
	Snapshot("deadline_end", playerid);
	return result;
}
forward IgnoredProvider(playerid, token);
public IgnoredProvider(playerid, token) {
	printf("QA event=provider_suppressed p=%d callback_token=%d", playerid, token);
	return 1;
}
stock FixtureHash(playerid, const callback[], const input[], cost, const args[], token) {
	#pragma unused cost
	if (DropProvider) return bcrypt_hash(playerid, "IgnoredProvider", input, ProviderCost, args, token);
	return bcrypt_hash(playerid, callback, input, ProviderCost, args, token);
}
stock FixtureVerify(playerid, const callback[], const input[], const hash[], const args[], token) {
	return bcrypt_verify(playerid, callback, input, hash, args, token);
}
stock bool:FixtureCancel(job) {
	// Explicit uncancellable-work variant. Ordinary cancellation is tested separately.
	printf("QA event=cancel job=%d forwarded=%d", job, !RetainWork);
	return RetainWork || pg_cancel(job);
}
public OnPlayerCommandText(playerid, cmdtext[]) {
	if (!strcmp(cmdtext, "/qa state")) Snapshot("snapshot", playerid);
	else if (!strcmp(cmdtext, "/qa retain")) RetainWork = true;
	else if (!strcmp(cmdtext, "/qa cancel")) RetainWork = false;
	else if (!strcmp(cmdtext, "/qa slow")) ProviderCost = 16;
	else if (!strcmp(cmdtext, "/qa gmxslow")) ProviderCost = 18;
	else if (!strcmp(cmdtext, "/qa normal")) ProviderCost = 12;
	else if (!strcmp(cmdtext, "/qa drop")) DropProvider = true;
	else if (!strcmp(cmdtext, "/qa deliver")) DropProvider = false;
	// Deliberately seed the real tutorial's counter in this disposable fixture only.
	else if (!strcmp(cmdtext, "/qa tokenlast")) SetSVarInt(TOKEN_COUNTER, 2147483646);
	else if (!strcmp(cmdtext, "/qa tokennegative")) SetSVarInt(TOKEN_COUNTER, -1);
	else if (!strcmp(cmdtext, "/qa lookup") && !PendingJob[playerid]) Lookup(playerid);
	else if (!strcmp(cmdtext, "/qa recover") && !Ready) {
		if (Database) pg_connection_close(Database);
		Database = pg_connect("game");
	} else if (!strcmp(cmdtext, "/qa npc")) {
		new npc = NPC_Create("QOfficialNPC");
		printf("QA event=npc_created p=%d connected=%d npc=%d", npc, IsPlayerConnected(npc), IsPlayerNPC(npc));
		NPC_Destroy(npc);
		printf("QA event=npc_destroyed p=%d connected=%d", npc, IsPlayerConnected(npc));
	}
	else if (!strcmp(cmdtext, "/qa pending")) {
		new job = pg_query(Database, "SELECT pg_sleep(5)", 0, 0, 0, true, "ShutdownComplete");
		printf("QA event=shutdown_pending job=%d", job);
	} else return 0;
	printf("QA event=control p=%d retain=%d cost=%d drop=%d", playerid, RetainWork, ProviderCost, DropProvider);
	return 1;
}
forward ShutdownComplete(job);
public ShutdownComplete(job) {
	printf("QA event=shutdown_callback job=%d", job);
	pg_job_close(job);
	return 1;
}
