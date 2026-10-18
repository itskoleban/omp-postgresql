// Interactive custom-callback tutorial. Install the schema in docs/callbacks.md first.
// Commands require an RCON administrator; integrate real application authorization separately.
#include <open.mp>
#include <omp-postgresql>

#define CUSTOM_TOKEN "pg_custom_session"
enum { ACCOUNT_LOAD, CLAN_LOAD, ACCOUNT_SAVE, PROPERTY_SAVE, REQUESTS };
new Database;
new bool:Ready;
new Session[MAX_PLAYERS];
new Pending[MAX_PLAYERS][REQUESTS];
new Account[MAX_PLAYERS];
forward OnEntityLoaded(job, playerid, session, request);
forward OnEntitySaved(job, playerid, session, request);

main() {}

public OnGameModeInit() {
	Database = pg_connect("game", 2);
	if (!Database) LocalError();
	return 1;
}

public OnGameModeExit() {
	if (Database) pg_connection_close(Database);
	return 1;
}

public OnPgConnection(connection, connection_state) {
	if (connection == Database) Ready = connection_state == PG_CONNECTED;
	return 1;
}

public OnPlayerConnect(playerid) {
	new token = GetSVarInt(CUSTOM_TOKEN);
	if (token < 0 || token == 2147483647 || !SetSVarInt(CUSTOM_TOKEN, token + 1)) { Kick(playerid); return 1; }
	Session[playerid] = token + 1;
	Account[playerid] = 0;
	for (new i = 0; i < REQUESTS; i++) Pending[playerid][i] = 0;
	return 1;
}

public OnPlayerDisconnect(playerid, reason) {
	#pragma unused reason
	Session[playerid] = 0;
	Account[playerid] = 0;
	for (new i = 0; i < REQUESTS; i++) {
		if (Pending[playerid][i]) pg_cancel(Pending[playerid][i]);
		Pending[playerid][i] = 0;
	}
	// Cancellation does not prove that a dispatched save was rolled back.
	return 1;
}

public OnPlayerCommandText(playerid, cmdtext[]) {
	if (strcmp(cmdtext, "/pgload") && strcmp(cmdtext, "/pgsave")) return 0;
	if (!Ready || !IsPlayerAdmin(playerid)) return 1;
	if (!strcmp(cmdtext, "/pgload")) {
		if (Pending[playerid][ACCOUNT_LOAD] || Pending[playerid][CLAN_LOAD] ||
		    Pending[playerid][ACCOUNT_SAVE] || Pending[playerid][PROPERTY_SAVE]) return 1;
		new name[MAX_PLAYER_NAME+1]; GetPlayerName(playerid, name, sizeof name);
		new stmt = pg_prepare(Database, "SELECT id,score FROM tutorial_custom_accounts WHERE username=$1", 1);
		if (stmt && pg_bind_string(stmt, 1, name) && pg_add(stmt)) Submit(playerid, stmt, ACCOUNT_LOAD);
		else LocalError();
		if (stmt) pg_statement_close(stmt);
		stmt = pg_prepare(Database, "SELECT name FROM tutorial_custom_clans WHERE id=$1", 1);
		if (stmt && pg_bind_int(stmt, 1, 1) && pg_add(stmt)) Submit(playerid, stmt, CLAN_LOAD);
		else LocalError();
		if (stmt) pg_statement_close(stmt);
	} else {
		if (!Account[playerid] || Pending[playerid][ACCOUNT_LOAD] ||
		    Pending[playerid][ACCOUNT_SAVE] || Pending[playerid][PROPERTY_SAVE]) return 1;
		new stmt = pg_prepare(Database, "UPDATE tutorial_custom_accounts SET score=$1 WHERE id=$2 RETURNING id", 2);
		if (stmt && pg_bind_int(stmt, 1, GetPlayerScore(playerid)) &&
		    pg_bind_int(stmt, 2, Account[playerid]) && pg_add(stmt)) Submit(playerid, stmt, ACCOUNT_SAVE);
		else LocalError();
		if (stmt) pg_statement_close(stmt);
		stmt = pg_prepare(Database, "UPDATE tutorial_custom_properties SET label=$1 WHERE id=$2 AND owner_id=$3 RETURNING id", 3);
		if (stmt && pg_bind_string(stmt, 1, "Saved property") && pg_bind_int(stmt, 2, 1) &&
		    pg_bind_int(stmt, 3, Account[playerid]) && pg_add(stmt)) Submit(playerid, stmt, PROPERTY_SAVE);
		else LocalError();
		if (stmt) pg_statement_close(stmt);
	}
	return 1;
}

stock Submit(playerid, statement, request) {
	new callback[32];
	format(callback, sizeof callback, "%s", request < ACCOUNT_SAVE ? "OnEntityLoaded" : "OnEntitySaved");
	Pending[playerid][request] = pg_execute(statement, request % 2, 0, 0, true,
		callback, "iii", playerid, Session[playerid], request);
	if (!Pending[playerid][request]) LocalError();
}

stock bool:Take(job, playerid, session, request) {
	if (playerid < 0 || playerid >= MAX_PLAYERS || request < 0 || request >= REQUESTS ||
	    !IsPlayerConnected(playerid) || !session || Session[playerid] != session ||
	    Pending[playerid][request] != job) return false;
	Pending[playerid][request] = 0;
	return true;
}

public OnEntityLoaded(job, playerid, session, request) {
	if (!Take(job, playerid, session, request)) { pg_job_close(job); return 1; }
	if (pg_job_status(job) != PG_CONFIRMED) SendClientMessage(playerid, -1, "Load failed; inspect the database before retrying.");
	else if (request == ACCOUNT_LOAD) {
		new id, score;
		Account[playerid] = 0;
		if (pg_result_rows(job) == 1 && pg_result_int(job, 0, 0, 0, id) && pg_result_int(job, 0, 0, 1, score)) {
			Account[playerid] = id; SetPlayerScore(playerid, score);
		} else SendClientMessage(playerid, -1, "Account absent or invalid; saves remain disabled.");
	} else {
		new name[65];
		if (pg_result_rows(job) == 1 && pg_result_string(job, 0, 0, 0, name)) SendClientMessage(playerid, -1, name);
		else SendClientMessage(playerid, -1, "Clan absent or invalid.");
	}
	if (!pg_job_close(job)) LocalError();
	return 1;
}

public OnEntitySaved(job, playerid, session, request) {
	if (!Take(job, playerid, session, request)) { pg_job_close(job); return 1; }
	if (pg_job_status(job) == PG_UNCERTAIN) SendClientMessage(playerid, -1, "Save outcome unknown. Reconcile before another save.");
	else if (pg_job_status(job) != PG_CONFIRMED || pg_result_rows(job) != 1) SendClientMessage(playerid, -1, "Save failed or matched no owned row.");
	else SendClientMessage(playerid, -1, "Save confirmed.");
	// Invalidate local write eligibility after an ambiguous/failed write. /pgload reconciles it.
	if (pg_job_status(job) != PG_CONFIRMED) Account[playerid] = 0;
	if (!pg_job_close(job)) LocalError();
	return 1;
}

stock LocalError() {
	new detail[256]; pg_last_error(detail);
	printf("PG CUSTOM local rejection: %s", detail);
}
