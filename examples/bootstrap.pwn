// Self-contained startup example: temporary fixtures belong to lane 0 only.
// Replace the fixture setup and trusted SELECTs with your application's schema.
#include <open.mp>
#include <omp_postgresql>
#pragma dynamic 16384

#define MAX_PROPERTIES (64)
#define MAX_WORLD_VEHICLES (64)
#define MAX_CLANS (64)
#define CLAN_NAME_SIZE (33)

enum {
	STEP_SETUP = 1,
	STEP_LOAD,
	STEP_VALIDATE
};

enum E_PROPERTY {
	PropertyID,
	Float:PropertyX,
	Float:PropertyY,
	Float:PropertyZ
};

enum E_VEHICLE {
	VehicleID,
	VehicleModel,
	Float:VehicleX,
	Float:VehicleY,
	Float:VehicleZ,
	Float:VehicleAngle
};

enum E_CLAN {
	ClanID,
	ClanName[CLAN_NAME_SIZE]
};

new Database;
new CurrentJob;
new bool:Started;
new bool:Ready;
new bool:Failed;
new PropertyCount;
new VehicleCount;
new ClanCount;
new Properties[MAX_PROPERTIES][E_PROPERTY];
new Vehicles[MAX_WORLD_VEHICLES][E_VEHICLE];
new Clans[MAX_CLANS][E_CLAN];
new PropertyPickups[MAX_PROPERTIES];
new SpawnedVehicles[MAX_WORLD_VEHICLES];

forward bool:ReadProperties(job);
forward bool:ReadVehicles(job);
forward bool:ReadClans(job);
forward bool:ActivateWorld();

main() {}

public OnGameModeInit() {
	for (new i = 0; i < MAX_PROPERTIES; i++) {
		PropertyPickups[i] = INVALID_PICKUP;
	}

	for (new i = 0; i < MAX_WORLD_VEHICLES; i++) {
		SpawnedVehicles[i] = INVALID_VEHICLE_ID;
	}

	AddPlayerClass(0, 1958.3783, 1343.1572, 15.3740, 269.1425,
		WEAPON_FIST, 0, WEAPON_FIST, 0, WEAPON_FIST, 0);
	Database = pg_connect("game", 1);

	if (!Database) {
		FailLocal("connection was rejected");
	}
	return 1;
}

public OnGameModeExit() {
	ClearWorld();
	if (CurrentJob) {
		pg_cancel(CurrentJob);
	}
	if (Database) {
		pg_connection_close(Database);
	}
	// Component owner-unload collection releases jobs still pending at shutdown.
	return 1;
}

public OnPlayerConnect(playerid) {
	if (!Ready) {
		TogglePlayerControllable(playerid, false);
		SendClientMessage(playerid, 0xD0D0D0FF, "The server is starting. Please wait before spawning.");
	}
	return 1;
}

public OnPlayerRequestSpawn(playerid) {
	if (!Ready) {
		SendClientMessage(playerid, 0xD0D0D0FF, "The world is not ready yet.");
		return 0;
	}
	return 1;
}

public OnPlayerSpawn(playerid) {
	TogglePlayerControllable(playerid, Ready);
	return 1;
}

public OnPgConnection(connection, connection_state) {
	if (connection != Database || Failed) {
		return 1;
	}
	if (connection_state == PG_CONNECTED && !Started) {
		Started = true;
		StartSetup();
	} else if (connection_state == PG_CONNECTION_FAILED ||
		(Started && connection_state == PG_RECONNECTING)) {
		// TEMP fixtures disappear after reconnect. Keep the game closed; do not
		// automatically repeat writes or activate a partly loaded world.
		Fail("startup connection was interrupted");
	}
	return 1;
}

public OnPgComplete(job, context) {
	if (job == CurrentJob) {
		CurrentJob = 0;
	}

	if (Failed) {
		pg_job_close(job);
		return 1;
	}

	if (pg_job_status(job) != PG_CONFIRMED) {
		printf("PG BOOTSTRAP ERROR: category=%d item=%d", pg_job_error(job), pg_job_error_index(job));
		Fail("an asynchronous startup job failed");
		pg_job_close(job);
		return 1;
	}

	if (context == STEP_SETUP) {
		if (pg_job_completed(job) != 6) {
			Fail("fixture setup was incomplete");
		} else {
			StartLoads();
		}
	} else if (context == STEP_LOAD) {
		if (pg_result_count(job) != 3 || !ReadProperties(job) || !ReadVehicles(job) || !ReadClans(job)) {
			Fail("startup data was invalid or exceeded the configured capacities");
		} else {
			CurrentJob = pg_query(Database,
				"SELECT (SELECT count(*)::integer FROM omp_bootstrap_properties), \
				(SELECT count(*)::integer FROM omp_bootstrap_vehicles), \
				(SELECT count(*)::integer FROM omp_bootstrap_clans)",
				0, 0, STEP_VALIDATE);
			if (!CurrentJob) {
				FailLocal("validation query was rejected");
			}
		}
	} else if (context == STEP_VALIDATE) {
		new properties, vehicles, clans;

		if (pg_result_rows(job) != 1 ||
			!pg_result_int(job, 0, 0, 0, properties) ||
			!pg_result_int(job, 0, 0, 1, vehicles) ||
			!pg_result_int(job, 0, 0, 2, clans) ||
			properties != PropertyCount || vehicles != VehicleCount || clans != ClanCount) {
			Fail("startup data was not loaded completely");
		} else if (ActivateWorld()) {
			Ready = true;
			for (new playerid = 0; playerid < MAX_PLAYERS; playerid++) {
				if (IsPlayerConnected(playerid)) {
					TogglePlayerControllable(playerid, true);
				}
			}
			printf("PG BOOTSTRAP PASS: properties=%d vehicles=%d clans=%d",
				PropertyCount, VehicleCount, ClanCount);
		} else {
			Fail("world resources could not be allocated");
		}
	} else {
		Fail("unexpected startup callback context");
	}

	pg_job_close(job);
	return 1;
}

stock bool:AppendSql(batch, const sql[]) {
	new statement = pg_prepare(Database, sql, 0, false);

	if (!statement) {
		FailLocal("fixture statement was rejected");
		return false;
	}

	new bool:accepted = pg_add(statement) && pg_batch_add(batch, statement);

	if (!accepted) {
		FailLocal("fixture batch item was rejected");
	}

	pg_statement_close(statement);
	return accepted;
}

stock StartSetup() {
	new batch = pg_batch_create(Database);

	if (!batch) {
		FailLocal("setup batch was rejected");
		return;
	}

	if (AppendSql(batch,
		"CREATE TEMP TABLE omp_bootstrap_properties(\
		id integer PRIMARY KEY, x real NOT NULL, y real NOT NULL, z real NOT NULL)") &&
		AppendSql(batch,
		"CREATE TEMP TABLE omp_bootstrap_vehicles(id integer PRIMARY KEY, model smallint NOT NULL, \
		x real NOT NULL, y real NOT NULL, z real NOT NULL, angle real NOT NULL)") &&
		AppendSql(batch,
		"CREATE TEMP TABLE omp_bootstrap_clans(id integer PRIMARY KEY, name text NOT NULL)") &&
		AppendSql(batch,
		"INSERT INTO omp_bootstrap_properties VALUES(1,1950,1340,15),(2,1960,1340,15)") &&
		AppendSql(batch,
		"INSERT INTO omp_bootstrap_vehicles VALUES(1,411,1965,1340,15,90)") &&
		AppendSql(batch,
		"INSERT INTO omp_bootstrap_clans VALUES(1,'Explorers'),(2,'Mechanics')")) {
		CurrentJob = pg_batch_execute(batch, 0, 0, STEP_SETUP);
		if (!CurrentJob) {
			FailLocal("setup execution was rejected");
		}
	}

	pg_batch_close(batch);
}

stock bool:AppendLoad(batch, const sql[], capacity) {
	new statement = pg_prepare(Database, sql, 1);

	if (!statement) {
		FailLocal("load statement was rejected");
		return false;
	}

	// Fetch one extra row so exceeding the fixed-size world arrays is detected.
	new bool:accepted = pg_bind_int(statement, 1, capacity + 1) &&
		pg_add(statement) && pg_batch_add(batch, statement);

	if (!accepted) {
		FailLocal("load batch item was rejected");
	}

	pg_statement_close(statement);
	return accepted;
}

stock StartLoads() {
	new batch = pg_batch_create(Database);

	if (!batch) {
		FailLocal("load batch was rejected");
		return;
	}

	if (AppendLoad(batch,
		"SELECT id,x,y,z FROM omp_bootstrap_properties ORDER BY id LIMIT $1", MAX_PROPERTIES) &&
		AppendLoad(batch,
		"SELECT id,model,x,y,z,angle FROM omp_bootstrap_vehicles ORDER BY id LIMIT $1",
		MAX_WORLD_VEHICLES) &&
		AppendLoad(batch,
		"SELECT id,name FROM omp_bootstrap_clans ORDER BY id LIMIT $1", MAX_CLANS)) {
		CurrentJob = pg_batch_execute(batch, 0, 0, STEP_LOAD);
		if (!CurrentJob) {
			FailLocal("load execution was rejected");
		}
	}

	pg_batch_close(batch);
}

stock bool:ReadProperties(job) {
	PropertyCount = pg_result_rows(job, 0);
	if (PropertyCount > MAX_PROPERTIES || pg_result_columns(job, 0) != 4) {
		return false;
	}

	new previous;

	for (new i = 0; i < PropertyCount; i++) {
		if (!pg_result_int(job, 0, i, 0, Properties[i][PropertyID]) ||
			!pg_result_float(job, 0, i, 1, Properties[i][PropertyX]) ||
			!pg_result_float(job, 0, i, 2, Properties[i][PropertyY]) ||
			!pg_result_float(job, 0, i, 3, Properties[i][PropertyZ]) ||
			Properties[i][PropertyID] <= previous) {
			return false;
		}

		previous = Properties[i][PropertyID];
	}
	return true;
}

stock bool:ReadVehicles(job) {
	VehicleCount = pg_result_rows(job, 1);
	if (VehicleCount > MAX_WORLD_VEHICLES || pg_result_columns(job, 1) != 6) {
		return false;
	}

	new previous;

	for (new i = 0; i < VehicleCount; i++) {
		if (!pg_result_int(job, 1, i, 0, Vehicles[i][VehicleID]) ||
			!pg_result_int(job, 1, i, 1, Vehicles[i][VehicleModel]) ||
			!pg_result_float(job, 1, i, 2, Vehicles[i][VehicleX]) ||
			!pg_result_float(job, 1, i, 3, Vehicles[i][VehicleY]) ||
			!pg_result_float(job, 1, i, 4, Vehicles[i][VehicleZ]) ||
			!pg_result_float(job, 1, i, 5, Vehicles[i][VehicleAngle]) ||
			Vehicles[i][VehicleID] <= previous || Vehicles[i][VehicleModel] < 400 ||
			Vehicles[i][VehicleModel] > 611) {
			return false;
		}

		previous = Vehicles[i][VehicleID];
	}
	return true;
}

stock bool:ReadClans(job) {
	ClanCount = pg_result_rows(job, 2);
	if (ClanCount > MAX_CLANS || pg_result_columns(job, 2) != 2) {
		return false;
	}

	new previous;

	for (new i = 0; i < ClanCount; i++) {
		if (!pg_result_int(job, 2, i, 0, Clans[i][ClanID]) ||
			!pg_result_string(job, 2, i, 1, Clans[i][ClanName], CLAN_NAME_SIZE) ||
			Clans[i][ClanID] <= previous || !strlen(Clans[i][ClanName])) {
			return false;
		}

		previous = Clans[i][ClanID];
	}
	return true;
}

stock bool:ActivateWorld() {
	for (new i = 0; i < PropertyCount; i++) {
		PropertyPickups[i] = CreatePickup(1273, 1, Properties[i][PropertyX],
			Properties[i][PropertyY], Properties[i][PropertyZ]);
		if (PropertyPickups[i] == INVALID_PICKUP) {
			return false;
		}
	}

	for (new i = 0; i < VehicleCount; i++) {
		SpawnedVehicles[i] = CreateVehicle(Vehicles[i][VehicleModel], Vehicles[i][VehicleX],
			Vehicles[i][VehicleY], Vehicles[i][VehicleZ], Vehicles[i][VehicleAngle], 1, 1, -1);
		if (SpawnedVehicles[i] == INVALID_VEHICLE_ID || !SpawnedVehicles[i]) {
			return false;
		}
	}
	return true;
}

stock ClearWorld() {
	Ready = false;

	for (new i = 0; i < MAX_PROPERTIES; i++) {
		if (PropertyPickups[i] != INVALID_PICKUP) {
			DestroyPickup(PropertyPickups[i]);
		}
		PropertyPickups[i] = INVALID_PICKUP;
	}

	for (new i = 0; i < MAX_WORLD_VEHICLES; i++) {
		if (SpawnedVehicles[i] != INVALID_VEHICLE_ID && SpawnedVehicles[i]) {
			DestroyVehicle(SpawnedVehicles[i]);
		}
		SpawnedVehicles[i] = INVALID_VEHICLE_ID;
	}

	PropertyCount = 0;
	VehicleCount = 0;
	ClanCount = 0;
}

stock Fail(const reason[]) {
	if (Failed) {
		return;
	}

	Failed = true;
	ClearWorld();
	for (new playerid = 0; playerid < MAX_PLAYERS; playerid++) {
		if (IsPlayerConnected(playerid)) {
			TogglePlayerControllable(playerid, false);
		}
	}

	printf("PG BOOTSTRAP FAILED: %s", reason);
	if (CurrentJob) {
		pg_cancel(CurrentJob);
	}
	if (Database) {
		pg_connection_close(Database);
	}
}

stock FailLocal(const reason[]) {
	new message[256];

	pg_last_error(message);
	printf("PG BOOTSTRAP LOCAL ERROR: %s", message);
	Fail(reason);
}
