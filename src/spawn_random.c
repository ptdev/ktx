#include "g_local.h"

#include <math.h>
#include <string.h>

#define RANDOM_SURFACE_SPAWN_CLASSNAME "info_player_random_surface"
#define RANDOM_SURFACE_SPAWN_MAX_ANCHORS 256
#define RANDOM_SURFACE_SPAWN_TRACE_UP 192.0f
#define RANDOM_SURFACE_SPAWN_TRACE_DOWN 512.0f
#define RANDOM_SURFACE_SPAWN_MIN_FLOOR_NORMAL 0.70f
#define RANDOM_SURFACE_SPAWN_YAW_TRACE 256.0f
#define RANDOM_SURFACE_SPAWN_DEG2RAD (3.14159265358979323846f / 180.0f)

typedef enum
{
	rsaPrecomputed = 0,
	rsaOnDemand
} randomSpawnAlgorithm_t;

typedef enum
{
	rsamNone = 0,
	rsamDistance,
	rsamNearestSpawnType
} randomSpawnAreaMode_t;

typedef struct
{
	gedict_t *spot;
	float score;
} randomSpawnCandidate_t;

static int random_surface_spawn_count = 0;
static int random_surface_native_spawn_count = 0;
static gedict_t *random_surface_native_spawns[RANDOM_SURFACE_SPAWN_MAX_ANCHORS];
static vec3_t random_surface_last_origin[MAX_CLIENTS];
static qbool random_surface_last_origin_valid[MAX_CLIENTS];

static int RandomSpawnMinPoints(void);
static int RandomSpawnTargetScale(void);
static int RandomSpawnAttemptsPerPoint(void);
static float RandomSpawnMinDistance(void);
static int RandomSpawnAreaMode(void);
static int RandomSpawnSpreadLevel(void);
static int RandomSpawnPlayerSlot(gedict_t *player);
static void RandomSpawnResetHistory(void);
static void RandomSpawnRefreshNativeSpawnTypes(void);
static gedict_t* RandomSpawnNearestNativeSpawn(vec3_t origin);

static randomSpawnAlgorithm_t RandomSpawnAlgorithm(void)
{
	// Keep the public entry-points algorithm driven so we can add an on-demand
	// sampler behind the same call sites once that variant exists.
	return rsaPrecomputed;
}

static qbool RandomSpawnModeEnabled(void)
{
	if ((int)cvar("k_spw") != 5)
	{
		return false;
	}

	if (!deathmatch || k_bloodfest)
	{
		return false;
	}

	if (isFFA() || isCTF() || isRA() || isCA() || isRACE() || isHoonyModeAny())
	{
		return false;
	}

	return (isDuel() || isTeam());
}

static qbool RandomSpawnUsingPrecomputedPool(void)
{
	return (RandomSpawnModeEnabled()
			&& (RandomSpawnAlgorithm() == rsaPrecomputed)
			&& (random_surface_spawn_count >= RandomSpawnMinPoints()));
}

static int RandomSpawnMinPoints(void)
{
	return (int)bound(1, cvar("k_spw_rnd_min_points"), MAX_SPAWN_WEIGHTS);
}

static int RandomSpawnTargetScale(void)
{
	return (int)bound(1, cvar("k_spw_rnd_target_scale"), MAX_SPAWN_WEIGHTS);
}

static int RandomSpawnAttemptsPerPoint(void)
{
	return (int)bound(1, cvar("k_spw_rnd_attempts_per_point"), 1024);
}

static float RandomSpawnMinDistance(void)
{
	return bound(16, cvar("k_spw_rnd_min_distance"), 512);
}

static int RandomSpawnAreaMode(void)
{
	return (int)bound(0, cvar("k_spw_rnd_area_mode"), rsamNearestSpawnType);
}

static int RandomSpawnSpreadLevel(void)
{
	return (int)bound(0, cvar("k_spw_rnd_spread"), 3);
}

static float RandomSpawnSpreadBias(void)
{
	static const float spread_bias[] = { 0.0f, 3.0f, 9.0f, 27.0f };

	return spread_bias[RandomSpawnSpreadLevel()];
}

static int RandomSpawnPlayerSlot(gedict_t *player)
{
	int slot;

	if (!player || (player == world))
	{
		return -1;
	}

	slot = NUM_FOR_EDICT(player) - 1;

	return ((slot >= 0) && (slot < MAX_CLIENTS)) ? slot : -1;
}

static void RandomSpawnResetHistory(void)
{
	memset(random_surface_last_origin, 0, sizeof(random_surface_last_origin));
	memset(random_surface_last_origin_valid, 0, sizeof(random_surface_last_origin_valid));
	memset(random_surface_native_spawns, 0, sizeof(random_surface_native_spawns));
	random_surface_native_spawn_count = 0;
}

static void RandomSpawnClearPool(void)
{
	gedict_t *player;
	gedict_t *spawn_point;

	for (player = world; (player = nextent(player));)
	{
		if ((player->ct == ctPlayer)
				&& player->k_lastspawn
				&& (player->k_lastspawn != world)
				&& !strnull(player->k_lastspawn->classname)
				&& streq(player->k_lastspawn->classname, RANDOM_SURFACE_SPAWN_CLASSNAME))
		{
			player->k_lastspawn = world;
		}
	}

	for (spawn_point = world; (spawn_point = ez_find(spawn_point, RANDOM_SURFACE_SPAWN_CLASSNAME));)
	{
		ent_remove(spawn_point);
	}

	random_surface_spawn_count = 0;
}

static qbool RandomSpawnIsAnchor(gedict_t *ent)
{
	if (!ent || (ent == world) || strnull(ent->classname))
	{
		return false;
	}

	if (streq(ent->classname, RANDOM_SURFACE_SPAWN_CLASSNAME) || streq(ent->classname, "spawnpoint"))
	{
		return false;
	}

	if (streq(ent->classname, "info_player_deathmatch")
			|| streq(ent->classname, "info_player_team1")
			|| streq(ent->classname, "info_player_team2")
			|| streq(ent->classname, "info_teleport_destination"))
	{
		return true;
	}

	return (!strncmp(ent->classname, "item_", 5) || !strncmp(ent->classname, "weapon_", 7));
}

static void RandomSpawnRefreshNativeSpawnTypes(void)
{
	gedict_t *spawn_point;

	memset(random_surface_native_spawns, 0, sizeof(random_surface_native_spawns));
	random_surface_native_spawn_count = 0;

	for (spawn_point = world; (spawn_point = find(spawn_point, FOFCLSN, "info_player_deathmatch"));)
	{
		if (random_surface_native_spawn_count >= RANDOM_SURFACE_SPAWN_MAX_ANCHORS)
		{
			break;
		}

		random_surface_native_spawns[random_surface_native_spawn_count++] = spawn_point;
	}
}

static int RandomSpawnTargetCount(void)
{
	int forced_count = cvar("k_spw_rnd_target_count");
	int spawn_count = find_cnt(FOFCLSN, "info_player_deathmatch");
	int min_points = RandomSpawnMinPoints();

	if (forced_count > 0)
	{
		return (int)bound(min_points, forced_count, MAX_SPAWN_WEIGHTS);
	}

	if (!spawn_count)
	{
		spawn_count = find_cnt(FOFCLSN, "info_player_team1") + find_cnt(FOFCLSN, "info_player_team2");
	}

	if (!spawn_count)
	{
		return 0;
	}

	// Auto-scale off the authored spawn count so most maps get more variety
	// without forcing admins to tune every map by hand.
	return (int)bound(min_points, spawn_count * RandomSpawnTargetScale(), MAX_SPAWN_WEIGHTS);
}

static void RandomSpawnDirectionFromYaw(float yaw, vec3_t dir)
{
	float radians = yaw * RANDOM_SURFACE_SPAWN_DEG2RAD;

	dir[0] = cosf(radians);
	dir[1] = sinf(radians);
	dir[2] = 0;
}

static void RandomSpawnFloorHitToPlayerOrigin(vec3_t floor_hit, vec3_t origin)
{
	VectorCopy(floor_hit, origin);

	// Spawn entities store the player's origin, not the exact contact point on
	// the ground. Lift the sampled floor hit by the hull min so validation uses
	// the same coordinate space as normal Quake spawn points.
	origin[2] += -VEC_HULL_MIN[2] + 1;
}

static qbool RandomSpawnHasPlayableContents(vec3_t origin)
{
	int feet = trap_pointcontents(origin[0], origin[1], origin[2] + VEC_HULL_MIN[2] + 1);
	int torso = trap_pointcontents(origin[0], origin[1], origin[2]);
	int head = trap_pointcontents(origin[0], origin[1], origin[2] + VEC_HULL_MAX[2] - 1);

	return ((feet == CONTENT_EMPTY) && (torso == CONTENT_EMPTY) && (head == CONTENT_EMPTY));
}

static qbool RandomSpawnHasClearSightToAnchor(gedict_t *anchor, vec3_t origin)
{
	vec3_t spawn_eye;
	vec3_t anchor_eye;

	VectorCopy(origin, spawn_eye);
	spawn_eye[2] += 24;

	VectorCopy(anchor->s.v.origin, anchor_eye);
	anchor_eye[2] += 24;

	traceline(PASSVEC3(spawn_eye), PASSVEC3(anchor_eye), true, world);

	return (g_globalvars.trace_fraction == 1);
}

static qbool RandomSpawnIsDuplicate(vec3_t origin)
{
	gedict_t *spawn_point;
	vec3_t delta;

	for (spawn_point = world; (spawn_point = ez_find(spawn_point, RANDOM_SURFACE_SPAWN_CLASSNAME));)
	{
		VectorSubtract(spawn_point->s.v.origin, origin, delta);

		if (vlen(delta) < RandomSpawnMinDistance())
		{
			return true;
		}
	}

	return false;
}

static qbool RandomSpawnHullFits(vec3_t origin)
{
	gedict_t *hit;

	TraceCapsule(PASSVEC3(origin), PASSVEC3(origin), false, world,
					 PASSVEC3(VEC_HULL_MIN), PASSVEC3(VEC_HULL_MAX));

	hit = PROG_TO_EDICT(g_globalvars.trace_ent);

	if (g_globalvars.trace_allsolid || g_globalvars.trace_startsolid
			|| (g_globalvars.trace_fraction != 1))
	{
		return false;
	}

	if ((hit != world) && ((hit->s.v.solid == SOLID_BSP) || (hit->s.v.solid == SOLID_SLIDEBOX)))
	{
		return false;
	}

	return true;
}

static float RandomSpawnBestYaw(vec3_t origin)
{
	float best_fraction = -1;
	float best_yaw = g_random() * 360;
	float yaw;

	// Face the most open direction so a fresh spawn does not begin by staring
	// into a wall unless the surrounding geometry really leaves no better option.
	for (yaw = 0; yaw < 360; yaw += 45)
	{
		vec3_t start;
		vec3_t end;
		vec3_t dir;

		VectorCopy(origin, start);
		start[2] += 24;

		RandomSpawnDirectionFromYaw(yaw, dir);
		VectorMA(start, RANDOM_SURFACE_SPAWN_YAW_TRACE, dir, end);

		traceline(PASSVEC3(start), PASSVEC3(end), true, world);

		if (g_globalvars.trace_fraction > best_fraction)
		{
			best_fraction = g_globalvars.trace_fraction;
			best_yaw = yaw;
		}
	}

	return best_yaw;
}

static qbool RandomSpawnSampleFromAnchor(gedict_t *anchor, vec3_t origin, vec3_t angles)
{
	vec3_t direction;
	vec3_t probe_start;
	vec3_t probe_end;
	float yaw = g_random() * 360;
	float distance = dist_random(32, 320, 0.75f);

	// Sample around gameplay anchors instead of raw BSP bounds. That keeps the
	// first implementation on surfaces players already reach, without limiting
	// the pool to authored spawn entities.
	RandomSpawnDirectionFromYaw(yaw, direction);

	VectorMA(anchor->s.v.origin, distance, direction, probe_start);
	probe_start[2] = anchor->s.v.origin[2] + RANDOM_SURFACE_SPAWN_TRACE_UP;

	VectorCopy(probe_start, probe_end);
	probe_end[2] = anchor->s.v.origin[2] - RANDOM_SURFACE_SPAWN_TRACE_DOWN;

	traceline(PASSVEC3(probe_start), PASSVEC3(probe_end), true, world);

	if (g_globalvars.trace_allsolid || g_globalvars.trace_startsolid || (g_globalvars.trace_fraction == 1))
	{
		return false;
	}

	if (g_globalvars.trace_plane_normal[2] < RANDOM_SURFACE_SPAWN_MIN_FLOOR_NORMAL)
	{
		return false;
	}

	RandomSpawnFloorHitToPlayerOrigin(g_globalvars.trace_endpos, origin);

	if (!RandomSpawnHasPlayableContents(origin))
	{
		return false;
	}

	if (!RandomSpawnHullFits(origin))
	{
		return false;
	}

	if (!RandomSpawnHasClearSightToAnchor(anchor, origin))
	{
		return false;
	}

	if (RandomSpawnIsDuplicate(origin))
	{
		return false;
	}

	VectorSet(angles, 0, RandomSpawnBestYaw(origin), 0);

	return true;
}

static void RandomSpawnCreatePoint(vec3_t origin, vec3_t angles)
{
	gedict_t *spawn_point = spawn();

	spawn_point->classname = RANDOM_SURFACE_SPAWN_CLASSNAME;
	spawn_point->netname = "Random Surface Spawn";
	spawn_point->s.v.flags = 0;
	spawn_point->s.v.solid = SOLID_NOT;
	spawn_point->s.v.movetype = MOVETYPE_NONE;
	spawn_point->s.v.modelindex = 0;
	spawn_point->model = "";
	spawn_point->cnt = random_surface_spawn_count;

	VectorCopy(angles, spawn_point->s.v.angles);
	setorigin(spawn_point, PASSVEC3(origin));
	setsize(spawn_point, 0, 0, 0, 0, 0, 0);

	random_surface_spawn_count++;
}

static float RandomSpawnDistanceBetween(vec3_t left, vec3_t right)
{
	vec3_t delta;

	VectorSubtract(left, right, delta);

	return vlen(delta);
}

static gedict_t* RandomSpawnNearestNativeSpawn(vec3_t origin)
{
	gedict_t *nearest = NULL;
	float nearest_distance = 0;
	int i;

	for (i = 0; i < random_surface_native_spawn_count; i++)
	{
		float distance = RandomSpawnDistanceBetween(origin, random_surface_native_spawns[i]->s.v.origin);

		if (!nearest || (distance < nearest_distance))
		{
			nearest = random_surface_native_spawns[i];
			nearest_distance = distance;
		}
	}

	return nearest;
}

static float RandomSpawnCandidateScore(gedict_t *spot, randomSpawnAreaMode_t area_mode,
		vec3_t last_origin, gedict_t *last_spawn_type)
{
	if (area_mode == rsamDistance)
	{
		return RandomSpawnDistanceBetween(spot->s.v.origin, last_origin);
	}

	if (area_mode == rsamNearestSpawnType)
	{
		gedict_t *spot_spawn_type = RandomSpawnNearestNativeSpawn(spot->s.v.origin);

		if (last_spawn_type && spot_spawn_type)
		{
			return RandomSpawnDistanceBetween(spot_spawn_type->s.v.origin, last_spawn_type->s.v.origin);
		}
	}

	return RandomSpawnDistanceBetween(spot->s.v.origin, last_origin);
}

static void RandomSpawnRememberSelection(gedict_t *spot, int totalspots)
{
	int slot = RandomSpawnPlayerSlot(self);

	if (slot >= 0)
	{
		VectorCopy(spot->s.v.origin, random_surface_last_origin[slot]);
		random_surface_last_origin_valid[slot] = true;
	}

	if ((match_in_progress == 2) && (totalspots > 2))
	{
		self->k_lastspawn = spot;
	}
}

static gedict_t* RandomSpawnChooseWeightedCandidate(randomSpawnCandidate_t candidates[], int candidate_count)
{
	float max_score = 0;
	float bias = RandomSpawnSpreadBias();
	float weight_sum = 0;
	float roll;
	int i;

	for (i = 0; i < candidate_count; i++)
	{
		if (candidates[i].score > max_score)
		{
			max_score = candidates[i].score;
		}
	}

	if ((bias <= 0) || (max_score <= 0))
	{
		return candidates[i_rnd(0, candidate_count - 1)].spot;
	}

	for (i = 0; i < candidate_count; i++)
	{
		float normalized = candidates[i].score / max_score;

		// Keep the rule soft: every valid spot remains selectable, but higher
		// spread levels make far-away areas increasingly more likely.
		candidates[i].score = 1.0f + (normalized * normalized * bias);
		weight_sum += candidates[i].score;
	}

	roll = g_random() * weight_sum;

	for (i = 0; i < candidate_count; i++)
	{
		roll -= candidates[i].score;

		if (roll <= 0)
		{
			return candidates[i].spot;
		}
	}

	return candidates[candidate_count - 1].spot;
}

static qbool RandomSpawnSpotIsAvailable(gedict_t *spot)
{
	gedict_t *thing;
	int pcount = 0;

	// Mirror the normal deathmatch safety checks so random-surface mode only
	// changes how we pick between safe spots, not what counts as safe.
	for (thing = world; (thing = trap_findradius(thing, spot->s.v.origin, 84));)
	{
		if ((thing->ct != ctPlayer) || ISDEAD(thing) || (thing == self))
		{
			continue;
		}

		pcount++;
	}

	if (!k_yawnmode && cvar("k_spw") && (match_in_progress == 2) && (self->k_lastspawn == spot))
	{
		pcount++;
	}

	return !pcount;
}

static gedict_t* RandomSpawnSelectPrecomputedPoint(void)
{
	randomSpawnCandidate_t candidates[MAX_SPAWN_WEIGHTS];
	gedict_t *last_spawn_type = NULL;
	gedict_t *spot;
	randomSpawnAreaMode_t area_mode = RandomSpawnAreaMode();
	int candidate_count = 0;
	int i;
	int player_slot = RandomSpawnPlayerSlot(self);
	int totalspots = 0;

	spot = find(world, FOFCLSN, "testplayerstart");
	if (spot)
	{
		return spot;
	}

	for (spot = world; (spot = find(spot, FOFCLSN, RANDOM_SURFACE_SPAWN_CLASSNAME));)
	{
		totalspots++;

		if (!RandomSpawnSpotIsAvailable(spot))
		{
			continue;
		}

		if (candidate_count < MAX_SPAWN_WEIGHTS)
		{
			candidates[candidate_count].spot = spot;
			candidates[candidate_count].score = 0;
			candidate_count++;
		}
	}

	if (match_in_progress == 2)
	{
		self->k_1spawn = g_globalvars.time + 2.6;
	}

	if (!candidate_count)
	{
		spot = SelectSpawnPoint(RANDOM_SURFACE_SPAWN_CLASSNAME);

		if (spot && (spot != world) && !strnull(spot->classname)
				&& streq(spot->classname, RANDOM_SURFACE_SPAWN_CLASSNAME))
		{
			RandomSpawnRememberSelection(spot, totalspots);
		}

		return spot;
	}

	if ((player_slot < 0) || !random_surface_last_origin_valid[player_slot]
			|| !area_mode || !RandomSpawnSpreadLevel() || (candidate_count == 1))
	{
		spot = candidates[i_rnd(0, candidate_count - 1)].spot;
		RandomSpawnRememberSelection(spot, totalspots);
		return spot;
	}

	if (area_mode == rsamNearestSpawnType)
	{
		last_spawn_type = RandomSpawnNearestNativeSpawn(random_surface_last_origin[player_slot]);
	}

	for (i = 0; i < candidate_count; i++)
	{
		candidates[i].score = RandomSpawnCandidateScore(candidates[i].spot, area_mode,
				random_surface_last_origin[player_slot], last_spawn_type);
	}

	spot = RandomSpawnChooseWeightedCandidate(candidates, candidate_count);
	RandomSpawnRememberSelection(spot, totalspots);

	return spot;
}

static void RandomSpawnBuildPrecomputedPool(void)
{
	gedict_t *anchors[RANDOM_SURFACE_SPAWN_MAX_ANCHORS];
	gedict_t *ent;
	int anchor_count = 0;
	int target_count = RandomSpawnTargetCount();
	int attempt_limit;
	int min_points = RandomSpawnMinPoints();
	int attempt;

	if (!target_count)
	{
		return;
	}

	for (ent = world; (ent = nextent(ent));)
	{
		if (RandomSpawnIsAnchor(ent))
		{
			if (anchor_count < RANDOM_SURFACE_SPAWN_MAX_ANCHORS)
			{
				anchors[anchor_count++] = ent;
			}
		}
	}

	if (!anchor_count)
	{
		return;
	}

	attempt_limit = target_count * RandomSpawnAttemptsPerPoint();
	if (attempt_limit < 128)
	{
		attempt_limit = 128;
	}

	for (attempt = 0; (attempt < attempt_limit) && (random_surface_spawn_count < target_count); attempt++)
	{
		vec3_t origin;
		vec3_t angles;
		gedict_t *anchor = anchors[i_rnd(0, anchor_count - 1)];

		if (RandomSpawnSampleFromAnchor(anchor, origin, angles))
		{
			RandomSpawnCreatePoint(origin, angles);
		}
	}

	if (random_surface_spawn_count < min_points)
	{
		G_Printf("Random surface respawns disabled on %s: only %d/%d valid points generated\n",
				 mapname, random_surface_spawn_count, target_count);
		RandomSpawnClearPool();
		return;
	}

	G_Printf("Random surface respawns prepared on %s: %d/%d points (min distance %.0f)\n",
			 mapname, random_surface_spawn_count, target_count, RandomSpawnMinDistance());
}

void RandomSpawnReset(void)
{
	random_surface_spawn_count = 0;
	RandomSpawnResetHistory();
}

void RandomSpawnResetClient(gedict_t *player)
{
	int slot = RandomSpawnPlayerSlot(player);

	if (slot < 0)
	{
		return;
	}

	VectorSet(random_surface_last_origin[slot], 0, 0, 0);
	random_surface_last_origin_valid[slot] = false;
	player->k_lastspawn = world;
}

void RandomSpawnPrepareMatch(void)
{
	RandomSpawnClearPool();
	RandomSpawnResetHistory();

	if (!RandomSpawnModeEnabled())
	{
		return;
	}

	RandomSpawnRefreshNativeSpawnTypes();

	switch (RandomSpawnAlgorithm())
	{
		case rsaPrecomputed:
			RandomSpawnBuildPrecomputedPool();
			break;

		case rsaOnDemand:
		default:
			break;
	}
}

char* ActiveDeathmatchSpawnClassname(void)
{
	return (RandomSpawnUsingPrecomputedPool()
			? RANDOM_SURFACE_SPAWN_CLASSNAME
			: "info_player_deathmatch");
}

gedict_t* SelectDeathmatchSpawnPoint(void)
{
	if (!RandomSpawnModeEnabled())
	{
		return SelectSpawnPoint("info_player_deathmatch");
	}

	switch (RandomSpawnAlgorithm())
	{
		case rsaPrecomputed:
			if (RandomSpawnUsingPrecomputedPool())
			{
				return RandomSpawnSelectPrecomputedPoint();
			}

			return SelectSpawnPoint(ActiveDeathmatchSpawnClassname());

		case rsaOnDemand:
		default:
			// The future on-demand sampler will select directly here. Until then,
			// fall back to authored spawn entities instead of half-implemented logic.
			return SelectSpawnPoint("info_player_deathmatch");
	}
}