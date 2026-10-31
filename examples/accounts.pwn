// Interactive account tutorial. Read docs/accounts.md and install its schema first.
// Authentication FAILS CLOSED unless TUTORIAL_BCRYPT is defined and samp_bcrypt is loaded.
#include <open.mp>
#include <omp_postgresql>
#if defined TUTORIAL_BCRYPT
	#include <samp_bcrypt>
#endif

#define LOGIN_DIALOG (700)
#define REGISTER_DIALOG (701)
#define HASH_SIZE (61)
#define TOKEN_COUNTER "pg_tutorial_auth_token"

enum { OFFLINE, LOOKUP, NEW_ACCOUNT, NEED_PASSWORD, HASHING, VERIFYING, REGISTERING, LOGGED_IN };
new Database;
new bool:Ready;
new Token[MAX_PLAYERS];
new AccountState[MAX_PLAYERS];
new PendingJob[MAX_PLAYERS];
new AccountID[MAX_PLAYERS];
new AccountScore[MAX_PLAYERS];
new AccountName[MAX_PLAYERS][MAX_PLAYER_NAME+1];
new PasswordHash[MAX_PLAYERS][HASH_SIZE];

forward OnAccountHashed(playerid, token);
forward OnAccountVerified(playerid, bool:success, token);
forward bool:Current(playerid, token, expected);
forward bool:HashPassword(playerid, token, const password[]);
forward bool:VerifyPassword(playerid, token, const password[], const hash[]);
forward AuthDeadline(playerid, token);
forward OnAccountComplete(job, playerid, token);

main() {}

public OnGameModeInit() {
	Database = pg_connect("game");
	if (!Database) LocalFailure("connect rejected");
	AddPlayerClass(0, 1958.3, 1343.1, 15.3, 0.0, WEAPON_FIST, 0, WEAPON_FIST, 0, WEAPON_FIST, 0);
	return 1;
}

public OnGameModeExit() {
	if (Database) pg_connection_close(Database);
	// Owner unload cancels/collects jobs; it cannot wait for final saves.
	return 1;
}

public OnPgConnection(connection, connection_state) {
	if (connection != Database) return 1;
	Ready = connection_state == PG_CONNECTED;
	// Reconnection restores transport, not player authentication or uncertain operations.
	for (new playerid = 0; playerid < MAX_PLAYERS; playerid++) {
		if (!IsPlayerConnected(playerid)) continue;
		if (Ready && AccountState[playerid] == OFFLINE && !PendingJob[playerid]) Lookup(playerid);
		else if (!Ready) {
			Token[playerid] = 0;
			AccountState[playerid] = OFFLINE;
			TogglePlayerControllable(playerid, false);
			SendClientMessage(playerid, -1, "Database unavailable. Log in again when it recovers.");
		}
	}
	return 1;
}

public OnPlayerConnect(playerid) {
	Token[playerid] = 0;
	AccountState[playerid] = OFFLINE;
	AccountID[playerid] = 0;
	AccountScore[playerid] = 0;
	PendingJob[playerid] = 0;
	PasswordHash[playerid][0] = EOS;
	GetPlayerName(playerid, AccountName[playerid], MAX_PLAYER_NAME+1);
	TogglePlayerControllable(playerid, false);
	if (Ready) Lookup(playerid);
	else SendClientMessage(playerid, -1, "Waiting for the database.");
	return 1;
}

public OnPlayerDisconnect(playerid, reason) {
	#pragma unused reason
	Token[playerid] = 0;
	AccountState[playerid] = OFFLINE;
	PasswordHash[playerid][0] = EOS;
	// A lookup can be cancelled. A registration write must still be reconciled by name.
	if (PendingJob[playerid]) pg_cancel(PendingJob[playerid]);
	PendingJob[playerid] = 0;
	return 1;
}

public OnPlayerRequestSpawn(playerid) {
	return Ready && AccountState[playerid] == LOGGED_IN;
}

public OnDialogResponse(playerid, dialogid, response, listitem, inputtext[]) {
	#pragma unused listitem
	if (dialogid != LOGIN_DIALOG && dialogid != REGISTER_DIALOG) return 0;
	if (!response) { Kick(playerid); return 1; }
	if (!Ready || PendingJob[playerid]) return 1;
	// Example policy: 12..64 bytes, below bcrypt's 72-byte input limit; never log input.
	if (strlen(inputtext) < 12 || strlen(inputtext) > 64) {
		SendClientMessage(playerid, -1, "Use 12 to 64 bytes. Reconnect to try again.");
		return 1;
	}
	if (dialogid == REGISTER_DIALOG && AccountState[playerid] == NEW_ACCOUNT) {
		AccountState[playerid] = HASHING;
		if (!HashPassword(playerid, Token[playerid], inputtext)) AuthUnavailable(playerid);
		else SetTimerEx("AuthDeadline", 30000, false, "ii", playerid, Token[playerid]);
	} else if (dialogid == LOGIN_DIALOG && AccountState[playerid] == NEED_PASSWORD) {
		AccountState[playerid] = VERIFYING;
		if (!VerifyPassword(playerid, Token[playerid], inputtext, PasswordHash[playerid])) AuthUnavailable(playerid);
		else SetTimerEx("AuthDeadline", 30000, false, "ii", playerid, Token[playerid]);
	}
	return 1;
}

stock Lookup(playerid) {
	// A server variable survives GMX: old provider callbacks cannot reuse a new token.
	new next = GetSVarInt(TOKEN_COUNTER);
	if (next < 0 || next == 2147483647 || !SetSVarInt(TOKEN_COUNTER, next + 1)) { Kick(playerid); return; }
	Token[playerid] = next + 1;
	new statement = pg_prepare(Database,
		"SELECT id,password_hash,score FROM tutorial_accounts WHERE username=$1", 1);
	if (!statement || !pg_bind_string(statement, 1, AccountName[playerid]) || !pg_add(statement)) {
		LocalFailure("lookup staging rejected");
		AuthUnavailable(playerid);
	} else {
		PendingJob[playerid] = pg_execute(statement, 0, 0, 0, true, "OnAccountComplete", "ii", playerid, Token[playerid]);
		if (!PendingJob[playerid]) {
			LocalFailure("lookup submission rejected");
			AuthUnavailable(playerid);
		}
		else AccountState[playerid] = LOOKUP;
	}
	if (statement) pg_statement_close(statement);
}

public OnAccountComplete(job, playerid, token) {
	if (playerid < 0 || playerid >= MAX_PLAYERS || !IsPlayerConnected(playerid) || PendingJob[playerid] != job) {
		// Still release a completed request after its player left or the slot was reused.
		pg_job_close(job);
		return 1;
	}
	PendingJob[playerid] = 0;
	if (Token[playerid] != token) {
		if (Ready && AccountState[playerid] == OFFLINE) Lookup(playerid);
		pg_job_close(job);
		return 1;
	}
	if (pg_job_status(job) != PG_CONFIRMED) {
		new sqlstate[6];
		if (pg_result_count(job) > 0) pg_result_sqlstate(job, 0, sqlstate);
		// 23505 is a registration race. UNCERTAIN needs a fresh lookup, never replay.
		if (!strcmp(sqlstate, "23505")) SendClientMessage(playerid, -1, "Account already registered. Reconnect to log in.");
		else SendClientMessage(playerid, -1, "Account request failed. Reconnect to reconcile its state.");
		AccountState[playerid] = OFFLINE;
	} else if (!Ready || AccountState[playerid] == OFFLINE) {
		// A connection transition invalidated this authentication attempt while it ran.
		if (Ready) Lookup(playerid);
	} else if (AccountState[playerid] == LOOKUP) {
		new rows = pg_result_rows(job);
		if (!rows) {
			AccountState[playerid] = NEW_ACCOUNT;
			ShowPlayerDialog(playerid, REGISTER_DIALOG, DIALOG_STYLE_PASSWORD, "Register", "Choose a password (12..64 bytes).", "Register", "Leave");
		} else if (rows != 1 || !pg_result_int(job, 0, 0, 0, AccountID[playerid]) ||
			!pg_result_string(job, 0, 0, 1, PasswordHash[playerid], HASH_SIZE) ||
			!pg_result_int(job, 0, 0, 2, AccountScore[playerid])) {
			AuthUnavailable(playerid);
		} else {
			AccountState[playerid] = NEED_PASSWORD;
			ShowPlayerDialog(playerid, LOGIN_DIALOG, DIALOG_STYLE_PASSWORD, "Login", "Enter your password.", "Login", "Leave");
		}
	} else if (AccountState[playerid] == REGISTERING) {
		if (pg_result_rows(job) == 1 && pg_result_int(job, 0, 0, 0, AccountID[playerid])) Authenticate(playerid);
		else AuthUnavailable(playerid);
	}
	pg_job_close(job);
	return 1;
}

public OnAccountHashed(playerid, token) {
	if (!Current(playerid, token, HASHING)) return 1;
	new hash[HASH_SIZE];
	#if defined TUTORIAL_BCRYPT
		bcrypt_get_hash(hash);
	#endif
	if (strlen(hash) != 60) { AuthUnavailable(playerid); return 1; }
	new statement = pg_prepare(Database,
		"INSERT INTO tutorial_accounts(username,password_hash) VALUES($1,$2) RETURNING id", 2);
	if (!statement || !pg_bind_string(statement, 1, AccountName[playerid]) ||
		!pg_bind_string(statement, 2, hash) || !pg_add(statement)) {
		LocalFailure("registration staging rejected");
		AuthUnavailable(playerid);
	}
	else {
		PendingJob[playerid] = pg_execute(statement, 0, 0, 0, true, "OnAccountComplete", "ii", playerid, token);
		if (!PendingJob[playerid]) {
			LocalFailure("registration submission rejected");
			AuthUnavailable(playerid);
		}
		else AccountState[playerid] = REGISTERING;
	}
	if (statement) pg_statement_close(statement);
	return 1;
}

public OnAccountVerified(playerid, bool:success, token) {
	if (!Current(playerid, token, VERIFYING)) return 1;
	if (success) Authenticate(playerid);
	else AuthUnavailable(playerid);
	return 1;
}

stock bool:Current(playerid, token, expected) {
	return playerid >= 0 && playerid < MAX_PLAYERS && IsPlayerConnected(playerid) &&
		Ready && token != 0 && Token[playerid] == token && AccountState[playerid] == expected;
}

stock Authenticate(playerid) {
	AccountState[playerid] = LOGGED_IN;
	PasswordHash[playerid][0] = EOS;
	SetPlayerScore(playerid, AccountScore[playerid]);
	TogglePlayerControllable(playerid, true);
	SendClientMessage(playerid, -1, "Account loaded. You can spawn.");
}

stock bool:HashPassword(playerid, token, const password[]) {
	#if defined TUTORIAL_BCRYPT
		return bcrypt_hash(playerid, "OnAccountHashed", password, 12, "i", token) != 0;
	#else
		#pragma unused playerid, token, password
		return false;
	#endif
}

stock bool:VerifyPassword(playerid, token, const password[], const hash[]) {
	#if defined TUTORIAL_BCRYPT
		return bcrypt_verify(playerid, "OnAccountVerified", password, hash, "i", token) != 0;
	#else
		#pragma unused playerid, token, password, hash
		return false;
	#endif
}

stock AuthUnavailable(playerid) {
	Token[playerid] = 0;
	AccountState[playerid] = OFFLINE;
	PasswordHash[playerid][0] = EOS;
	SendClientMessage(playerid, -1, "Authentication unavailable or rejected. Reconnect to try again.");
}

public AuthDeadline(playerid, token) {
	if (Current(playerid, token, HASHING) || Current(playerid, token, VERIFYING)) AuthUnavailable(playerid);
	return 1;
}

stock LocalFailure(const reason[]) {
	new detail[256];
	pg_last_error(detail);
	// Local diagnostics contain no passwords from this tutorial. Filter operator logs.
	printf("Account tutorial: %s (%s)", reason, detail);
}
