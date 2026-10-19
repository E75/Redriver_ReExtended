#include "driver2.h"
#include "d1chase.h"
#include "handling.h"
#include "cosmetic.h"
#include "pad.h"
#include "civ_ai.h"
#include "objcoll.h"
#include "cars.h"
#include "system.h"
#include "dr2roads.h"
#include "overlay.h"
#include "main.h"

#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int gD1ChasePaths = 1;

#define D1CHASE_MAGIC	0x50433144		// "D1CP"
#define D1CHASE_VERSION	1
#define D1CHASE_CONTACT_DIST	40		// units a step may differ from the recorded frame before it counts as a contact

typedef struct
{
	int x, z;
	short y, yaw;
} D1CHASE_FRAME;

#define D1CHASE_PADS_MAGIC	0x44503144		// "D1PD"
#define D1CHASE_PADS_VERSION	1
#define D1CHASE_STALL_DIST	200			// units a crashed car has to cover within D1CHASE_STALL_FRAMES, or it counts as stuck (wheels may spin)
#define D1CHASE_STALL_FRAMES	60
#define D1CHASE_REJOIN_DIST	700			// units from the lane at which the civilian AI takes the car again
#define D1CHASE_GIVE_UP	450			// steps of recovery before the civilian AI gets the car wherever it is
#define D1CHASE_JAM_FRAMES	30			// steps of the civilian AI without progress before the car backs off: a chased car never waits long
#define D1CHASE_SENSE_GAP	150			// the eight body sensors sit this far outside the car's box
#define D1CHASE_SLIDE_STEER	300			// wheel angle (civilian AI steers up to 512) from which a turn at speed is a sharp bend
#define D1CHASE_SLIDE_SPEED	60			// speed window of a handbrake turn: faster than this spins the car round
#define D1CHASE_SLIDE_TOP	170
#define D1CHASE_OFFROAD_STEPS	15			// steps off the road surface (pavement, grass, trees) before the car drives back to its lane
#define D1CHASE_REVERSE_MAX	90			// steps a recovery backs up at most; it stops as soon as the nose points at the road
#define D1CHASE_MERGE_AHEAD	1200		// the recovery aims this far along the lane past the nearest spot, so the car merges in the lane's direction
#define D1CHASE_MERGE_ANGLE	768			// heading difference to the lane under which the civilian AI takes the car again
#define D1CHASE_MERGE_STEPS	45			// steps after the merge at town speed before the chase speed comes back
#define D1CHASE_LANE_LOOK	15			// steps between two looks for the nearest lane during a recovery
#define D1CHASE_KNOCK_EVERY	450			// test hook REDRIVER2_CHASEKNOCK: steps between two knocks off the road
#define D1CHASE_KNOCK_OFF	400			// how far past the road edge a knock puts the car
#define D1CHASE_BACK_STEPS	15			// test hook: steps on the road with the civilian AI driving that count as back
#define D1CHASE_SLIDE_STEPS	8			// steps of the handbrake pull that swings the tail round
#define D1CHASE_THRUST	4779
#define D1CHASE_MIN_HITS	6			// rams of the player it takes at least to fill the damage bar of the chased car
#define D1CHASE_HIT_GAP	30			// steps without contact that make the next contact a new ram
#define D1CHASE_FEEL_BASE	500			// feelers: how far ahead the car looks at a standstill
#define D1CHASE_FEEL_PER_SPEED	6			// and per unit of speed more
#define D1CHASE_FEEL_CELL	110			// radius of the free-space check of a feeler (buildings, lamp posts, trees)
#define D1CHASE_FEEL_CAR	330			// a car this close to a feeler blocks it
#define D1CHASE_STEER	300				// wheel angle of an evasive turn
#define D1CHASE_BRAKE	2000			// thrust of the chased car while the throttle is down

static D1CHASE_FRAME* sFrames = NULL;
static int sFrameCount = 0;
static int sFrame = 0;			// route frames placed (no pad stream) or pad frames played (pad stream)
static int sRouteOver = 0;
static int sStall = 0;			// steps since the pad driven car last made progress
static int sStallX = 0, sStallZ = 0;	// where it was then
static int sRecovering = 0;		// the pad stream let go of the car, it drives back to a lane by itself
static int sPadsDone = 0;		// back on the road: the civilian AI drives on
static EXTRA_CIV_DATA sLane;		// the lane it is heading for
static int sLaneX, sLaneZ, sLaneDir, sLaneFound;	// where the recovery heads (sLaneFound: sLane is a real lane to hand over to)
static int sRoadX, sRoadZ, sRoadDir, sRoadKnown;	// last spot and heading while the civilian AI drove it on the road
static int sMonFrame = 0, sMonX = 0, sMonZ = 0;		// progress check of the civilian AI driven car
static int sRecFrame = 0, sRevLeft = 0, sCheckX = 0, sCheckZ = 0;
static int sOffRoad = 0;			// steps the civilian AI driven car has been off the road surface
static int sHitFrame = -1000, sHitSum = 0;	// last contact with the player's car, damage of the current ram so far
static int sKnockSteps = -1, sKnockOnRoad = 0;	// test hook: steps since the last knock (-1 = none pending), steps back on the road since
static int sPlaced = 0;			// the car stood on route frame 0 once (the spawn spot is only near it)
static int sWrecked = 0;		// damage bar full: the car stops for good, no more recovery
static int sSharp = 0, sSlideLeft = 0;		// handbrake turns: in a sharp bend now, steps of the pull left
static int sMergeLeft = 0;		// steps the car still drives at town speed after merging back into its lane

static unsigned char* sPads = NULL;		// per frame: uint16 pad, int8 steer
static int sPadCount = 0;

// pad stream of the same variant; optional, without it the route is followed by position only
static void LoadPads(int missionNumber, int variants, int pick)
{
	char filename[64];
	int head[3], offset, i;

	sprintf(filename, "..\\DRIVER1\\CONVERTED\\M%d.PADS", missionNumber);
	if (!FileExists(filename))
		return;

	LoadfileSeg(filename, (char*)head, 0, sizeof(head));
	if (head[0] != D1CHASE_PADS_MAGIC || head[1] != D1CHASE_PADS_VERSION || head[2] != variants)
		return;

	offset = sizeof(head);

	for (i = 0; i <= pick; i++)
	{
		int var[6];		// frames, start x y z, yaw, car model

		LoadfileSeg(filename, (char*)var, offset, sizeof(var));
		offset += sizeof(var);

		if (var[0] < 1 || var[0] > 100000)
			return;

		if (i == pick)
		{
			sPads = (unsigned char*)malloc(var[0] * 3);
			if (sPads == NULL)
				return;

			LoadfileSeg(filename, (char*)sPads, offset, var[0] * 3);
			sPadCount = var[0];
			printInfo("D1 chase pads: %d frames\n", sPadCount);
		}

		offset += var[0] * 3;
	}
}

int D1ChasePath_Load(int missionNumber)
{
	char filename[64];
	int head[3], variants, pick, i;
	int offset;

	free(sFrames);
	free(sPads);
	sFrames = NULL;
	sPads = NULL;
	sFrameCount = 0;
	sPadCount = 0;
	sFrame = 0;
	sRouteOver = 0;
	sPlaced = 0;
	sStall = 0;
	sPadsDone = 0;
	sRecovering = 0;
	sRecFrame = 0;
	sRevLeft = 0;
	sMonFrame = 0;
	sWrecked = 0;
	sSharp = 0;
	sSlideLeft = 0;
	sMergeLeft = 0;
	sOffRoad = 0;
	sRoadKnown = 0;
	sHitFrame = -1000;
	sHitSum = 0;
	sKnockSteps = -1;
	sKnockOnRoad = 0;

	if (!gD1ChasePaths)
		return 0;

	sprintf(filename, "..\\DRIVER1\\CONVERTED\\M%d.CHASE", missionNumber);
	if (!FileExists(filename))
		return 0;

	LoadfileSeg(filename, (char*)head, 0, sizeof(head));
	if (head[0] != D1CHASE_MAGIC || head[1] != D1CHASE_VERSION || head[2] < 1 || head[2] > 64)
		return 0;

	variants = head[2];
	pick = (int)(SDL_GetTicks() % variants);		// the original picks a variant at random (rand() is not seeded here)
	offset = sizeof(head);

	for (i = 0; i <= pick; i++)
	{
		int count;

		LoadfileSeg(filename, (char*)&count, offset, sizeof(count));
		offset += sizeof(count);

		if (count < 2 || count > 100000)
			return 0;

		if (i == pick)
		{
			sFrames = (D1CHASE_FRAME*)malloc(count * sizeof(D1CHASE_FRAME));
			if (sFrames == NULL)
				return 0;

			LoadfileSeg(filename, (char*)sFrames, offset, count * sizeof(D1CHASE_FRAME));
			sFrameCount = count;
			printInfo("D1 chase route: mission %d variant %d of %d, %d frames\n", missionNumber, pick, variants, count);
		}

		offset += count * sizeof(D1CHASE_FRAME);
	}

	LoadPads(missionNumber, variants, pick);
	return 1;
}

void D1ChasePath_Start(int* x, int* z, int* direction)
{
	*x = sFrames[0].x;
	*z = sFrames[0].z;
	*direction = sFrames[0].yaw;
	sFrame = 0;
}

// puts the car at x, z facing yaw, with the body pitch the car physics start with (handling.c TempBuildHandlingMatrix)
static void PlaceCar(CAR_DATA* cp, int x, int z, int yaw)
{
	int dz4 = (car_cosmetics[cp->ap.model].wheelDisp[0].vz + car_cosmetics[cp->ap.model].wheelDisp[1].vz) / 5;
	int sn = RSIN(yaw / 2), cs = RCOS(yaw / 2);

	cp->hd.direction = yaw;
	cp->st.n.fposition[0] = x << 4;
	cp->st.n.fposition[2] = z << 4;
	cp->st.n.orientation[0] = FIXEDH(-cs * dz4);
	cp->st.n.orientation[1] = sn;
	cp->st.n.orientation[2] = FIXEDH(sn * dz4);
	cp->st.n.orientation[3] = cs;
}

int D1ChasePath_Step(CAR_DATA* cp)
{
	const D1CHASE_FRAME* cur;
	const D1CHASE_FRAME* next;
	int dx, dz;

	if (sFrames == NULL || sRouteOver)
		return 0;

	if (sFrame + 1 >= sFrameCount)
	{
		printInfo("D1 chase route over after %d frames, %s drives on\n", sFrame, sPads ? "the pad stream" : "civilian AI");
		sRouteOver = 1;
		return 0;
	}

	cur = &sFrames[sFrame];
	next = &sFrames[sFrame + 1];

	// the physics moved the car one step from the last recorded frame; without a contact that lands on this frame, a building
	// pushes it back (the recorded route is only valid until the first contact), then the pads (or civilian AI) drive on
	if (sFrame > 0)
	{
		int offX = (cp->st.n.fposition[0] >> 4) - cur->x;
		int offZ = (cp->st.n.fposition[2] >> 4) - cur->z;

		if (offX * offX + offZ * offZ > D1CHASE_CONTACT_DIST * D1CHASE_CONTACT_DIST)
		{
			printInfo("D1 chase route left after %d frames (off by %d %d, at %d %d), %s drives on\n", sFrame, offX, offZ, cp->st.n.fposition[0] >> 4, cp->st.n.fposition[2] >> 4, sPads ? "the pad stream" : "civilian AI");
			sRouteOver = 1;
			return 0;
		}
	}

	// the recorded pose lies inside a building of this world (the route comes from another run): never snap the car into it
	if (sFrame > 0)
	{
		VECTOR pose;

		pose.vx = cur->x;
		pose.vy = cp->hd.where.t[1];
		pose.vz = cur->z;

		if (QuickBuildingCollisionCheck(&pose, cur->yaw, cp->ap.carCos->colBox.vz, cp->ap.carCos->colBox.vx, 0x14))
		{
			printInfo("D1 chase route left after %d frames (building at %d %d), %s drives on\n", sFrame, cur->x, cur->z, sPads ? "the pad stream" : "civilian AI");
			sRouteOver = 1;
			return 0;
		}
	}

	sPlaced = 1;

	if (sPads == NULL)
		sFrame++;

	dx = next->x - cur->x;
	dz = next->z - cur->z;

	PlaceCar(cp, cur->x, cur->z, cur->yaw);

	// a step moves the car by linearVelocity / 4096 (handling.c), so the next hand-over keeps the recorded speed
	cp->st.n.linearVelocity[0] = dx * 4096;
	cp->st.n.linearVelocity[2] = dz * 4096;
	cp->hd.wheel_speed = (int)sqrt((double)dx * dx + (double)dz * dz) * 4096;

	RebuildCarMatrix(&cp->st, cp);
	return 1;
}

// a road or junction surface lies at x, z (not the pavement, grass or a building plot)
static int OnRoad(int x, int y, int z)
{
	VECTOR pos;
	int surf;

	pos.vx = x;
	pos.vy = y;
	pos.vz = z;
	surf = GetSurfaceIndex(&pos);

	return IS_STRAIGHT_SURFACE(surf) || IS_CURVED_SURFACE(surf) || IS_JUNCTION_SURFACE(surf);
}

static int CarOnRoad(CAR_DATA* cp)
{
	return OnRoad(cp->hd.where.t[0], cp->hd.where.t[1], cp->hd.where.t[2]);
}

static int IsChaseCar(CAR_DATA* cp)
{
	return cp->inform != NULL && (*cp->inform & TARGET_FLAG_CAR_D1_CHASE);
}

// a feeler point is blocked by a building or object (grass, pavement and concrete are fine) or by another car
static int FeelerBlocked(const CAR_DATA* cp, int x, int z)
{
	VECTOR pos;
	int i;

	pos.vx = x;
	pos.vy = cp->hd.where.t[1];
	pos.vz = z;

	if (!CellAtPositionEmpty(&pos, D1CHASE_FEEL_CELL))
		return 1;

	for (i = 0; i < MAX_CARS; i++)
	{
		const CAR_DATA* o = &car_data[i];
		int dx = o->hd.where.t[0] - x, dz = o->hd.where.t[2] - z;

		if (o != cp && o->controlType != CONTROL_TYPE_NONE && dx * dx + dz * dz < D1CHASE_FEEL_CAR * D1CHASE_FEEL_CAR)
			return 1;
	}

	return 0;
}

// feelers ahead of the car (middle near and far, left and right far): 0 = the way is free, else 1 = turn away from the
// obstacle with *steer (wheel angle, + is right), *brake = too fast for what lies ahead
static int Feel(const CAR_DATA* cp, int speed, int* steer, int* brake)
{
	int reach = D1CHASE_FEEL_BASE + speed * D1CHASE_FEEL_PER_SPEED;
	int tx = cp->hd.where.t[0], tz = cp->hd.where.t[2];
	int fx = cp->hd.where.m[0][2], fz = cp->hd.where.m[2][2];		// forward and right axis, 4096 = 1
	int rx = cp->hd.where.m[0][0], rz = cp->hd.where.m[2][0];
	int side = reach / 3;
	int nearBlocked = FeelerBlocked(cp, tx + FIXEDH(fx * (reach / 2)), tz + FIXEDH(fz * (reach / 2)));
	int farBlocked = FeelerBlocked(cp, tx + FIXEDH(fx * reach), tz + FIXEDH(fz * reach));
	int left, right;

	*brake = 0;

	if (!nearBlocked && !farBlocked)
		return 0;

	left = FeelerBlocked(cp, tx + FIXEDH(fx * reach - rx * side), tz + FIXEDH(fz * reach - rz * side));
	right = FeelerBlocked(cp, tx + FIXEDH(fx * reach + rx * side), tz + FIXEDH(fz * reach + rz * side));

	if (left && right)
	{
		*steer = 0;
		*brake = 1;
	}
	else if (left)
		*steer = D1CHASE_STEER;
	else if (right)
		*steer = -D1CHASE_STEER;
	else
		*steer = cp->wheel_angle >= 0 ? D1CHASE_STEER : -D1CHASE_STEER;		// both sides free: keep the way the lane steering turns

	// brake in time: a close obstacle at any speed, a far one when fast
	if (nearBlocked && speed > 40)
		*brake = 1;
	else if (farBlocked && speed > WorldSpeedLimit(1))
		*brake = 1;

	return 1;
}

enum { SENSE_FL = 1, SENSE_F = 2, SENSE_FR = 4, SENSE_L = 8, SENSE_R = 16, SENSE_RL = 32, SENSE_B = 64, SENSE_RR = 128 };

// eight sensors round the body (corners, front, back, both sides): a set bit = a building, object or car is there
static int SenseRing(const CAR_DATA* cp)
{
	static const signed char ofs[8][2] = { { -1, 1 }, { 0, 1 }, { 1, 1 }, { -1, 0 }, { 1, 0 }, { -1, -1 }, { 0, -1 }, { 1, -1 } };	// side, along
	int w = cp->ap.carCos->colBox.vx + D1CHASE_SENSE_GAP, l = cp->ap.carCos->colBox.vz + D1CHASE_SENSE_GAP;
	int i, ring = 0;

	for (i = 0; i < 8; i++)
	{
		int s = ofs[i][0] * w, a = ofs[i][1] * l;
		int x = cp->hd.where.t[0] + FIXEDH(cp->hd.where.m[0][2] * a + cp->hd.where.m[0][0] * s);
		int z = cp->hd.where.t[2] + FIXEDH(cp->hd.where.m[2][2] * a + cp->hd.where.m[2][0] * s);

		if (FeelerBlocked(cp, x, z))
			ring |= 1 << i;
	}

	return ring;
}

// the way ahead is shut: the front sensor, or both front corners
static int FrontShut(int ring)
{
	return (ring & SENSE_F) || (ring & (SENSE_FL | SENSE_FR)) == (SENSE_FL | SENSE_FR);
}

// the civilian AI follows the lane and brakes for cars; this adds what it cannot see: it turns away from buildings and
// objects and slips round cars, over grass and pavement, instead of driving into them at full speed
void D1ChaseCar_Avoid(CAR_DATA* cp)
{
	int steer, brake, speed;

	if (!IsChaseCar(cp) || cp->controlType != CONTROL_TYPE_CIV_AI)
		return;

	speed = FIXEDH(cp->hd.wheel_speed);

	// a sharp bend at speed: a short handbrake pull swings the tail round
	if (sSlideLeft > 0)
		cp->handbrake = --sSlideLeft > 0;
	else if (ABS(cp->wheel_angle) > D1CHASE_SLIDE_STEER && speed > D1CHASE_SLIDE_SPEED && speed < D1CHASE_SLIDE_TOP)
	{
		if (!sSharp)
		{
			sSlideLeft = D1CHASE_SLIDE_STEPS;
			cp->handbrake = 1;
		}

		sSharp = 1;
	}
	else
		sSharp = 0;

	// just merged back into the lane: settle at town speed first
	if (sMergeLeft > 0)
	{
		sMergeLeft--;

		if (speed > WorldSpeedLimit(0) && cp->thrust > 0)
			cp->thrust = 0;
	}

	if (speed < 5)
		return;

	if (!Feel(cp, speed, &steer, &brake))
		return;

	if (steer != 0)
		cp->wheel_angle = steer;

	if (brake)
		cp->thrust = -D1CHASE_BRAKE;
}

// the car stands somewhere off its way (a crash): reverse and drive at the nearest traffic lane, then the civilian AI takes over
static int Recover(CAR_DATA* cp)
{
	int px = cp->hd.where.t[0], pz = cp->hd.where.t[2];
	int dx, dz, diff, steer, pad, ring;

	// a stopped civilian AI car has its wheels locked (wheelforces.c): free them while the pads drive it
	cp->ai.c.thrustState = CIV_AI_THRUST_ACCELERATE;

	// the ring search can miss the road (car facing away): then head for the last spot it drove on the road
	if (sRecFrame % D1CHASE_LANE_LOOK == 0 || sLaneX == 0)
	{
		long long laneDist = 0, roadDist = 0;

		sLaneFound = CivFindLaneNear(cp, cp->hd.direction, &sLane, &sLaneX, &sLaneZ, &sLaneDir);

		if (sLaneFound)
			laneDist = (long long)(sLaneX - px) * (sLaneX - px) + (long long)(sLaneZ - pz) * (sLaneZ - pz);

		if (sRoadKnown)
			roadDist = (long long)(sRoadX - px) * (sRoadX - px) + (long long)(sRoadZ - pz) * (sRoadZ - pz);

		if (sRoadKnown && (!sLaneFound || laneDist > roadDist + 1000000LL))
		{
			sLaneFound = 0;
			sLaneX = sRoadX;
			sLaneZ = sRoadZ;
			sLaneDir = sRoadDir;
		}
		else if (!sLaneFound)
			sLaneX = 0;
	}

	dx = sLaneX - px;
	dz = sLaneZ - pz;

	// on the road, at the lane and facing its way: the civilian AI takes over (across or the wrong way it would swing into the buildings)
	if ((sRecFrame > 10 && sRevLeft == 0 && sLaneFound && CarOnRoad(cp) && dx * dx + dz * dz < D1CHASE_REJOIN_DIST * D1CHASE_REJOIN_DIST &&
		ABS(DIFF_ANGLES(cp->hd.direction, sLaneDir)) < D1CHASE_MERGE_ANGLE && !FrontShut(SenseRing(cp))) || sRecFrame > D1CHASE_GIVE_UP)
	{
		sLane.thrustState = CIV_AI_THRUST_ACCELERATE;
		sLane.ctrlState = CIV_AI_CTRL_DRIVE;
		sLane.controlFlags = 0;

		printInfo("D1 chase car back at the road after %d steps\n", sRecFrame);
		InitCivState(cp, sLaneFound ? &sLane : NULL);
		sMergeLeft = D1CHASE_MERGE_STEPS;
		sPadsDone = 1;
		sMonFrame = 0;
		sMonX = px;
		sMonZ = pz;
		return 0;
	}

	// blocked (wall, car): back off for a moment, then try again
	if (sRecFrame % 45 == 0)
	{
		int mx = px - sCheckX, mz = pz - sCheckZ;

		if (sRecFrame > 0 && mx * mx + mz * mz < D1CHASE_STALL_DIST * D1CHASE_STALL_DIST / 4)
			sRevLeft = (FrontShut(SenseRing(cp)) || (sRecFrame / 45) % 2 == 0) ? D1CHASE_REVERSE_MAX : 0;	// no gain forward either: alternate

		sCheckX = px;
		sCheckZ = pz;
	}

	sRecFrame++;

	// aim past the nearest lane spot along the lane, so the car merges in instead of crossing it; no lane known: straight on
	if (sLaneX != 0)
		diff = ((ratan2(dx + FIXEDH(RSIN(sLaneDir) * D1CHASE_MERGE_AHEAD), dz + FIXEDH(RCOS(sLaneDir) * D1CHASE_MERGE_AHEAD)) - cp->hd.direction + 2048) & 4095) - 2048;
	else
		diff = 0;
	steer = diff / 12;
	steer = steer > 127 ? 127 : (steer < -127 ? -127 : steer);

	ring = SenseRing(cp);

	if (sRevLeft > 0)
	{
		sRevLeft--;
		pad = CAR_PAD_BRAKE;

		// backing up on full opposite lock swings the nose towards the road; the rear moves to the wheel side, so not into a blocked corner
		steer = ABS(diff) < 256 ? 0 : (diff >= 0 ? -127 : 127);

		if ((steer < 0 && (ring & SENSE_RL)) || (steer > 0 && (ring & SENSE_RR)))
			steer = 0;

		// done once the nose points at the road with room ahead, or the back is shut
		if ((ABS(diff) < 512 && D1CHASE_REVERSE_MAX - sRevLeft > 20 && !FrontShut(ring)) || (ring & SENSE_B))
			sRevLeft = 0;
	}
	else
	{
		int avoid, brake;
		int speed = FIXEDH(cp->hd.wheel_speed);
		int cap = ABS(diff) > 1024 ? 25 : (ABS(diff) > 512 ? 45 : 1000);	// line up with the road first, then speed up

		pad = speed <= cap ? CAR_PAD_ACCEL : (speed > cap + 15 ? CAR_PAD_BRAKE : 0);

		if (Feel(cp, speed, &avoid, &brake))
		{
			// go round on the side of the road when that front corner is free, else the other way; no full throttle into trees
			int toRoad = diff >= 0 ? 1 : -1;
			int roadSideFree = !(ring & (toRoad > 0 ? SENSE_FR : SENSE_FL));

			steer = (roadSideFree ? toRoad : -toRoad) * 127;

			if (brake && speed > 40)
				pad = 0;
		}
		else if ((ring & SENSE_FL) && !(ring & SENSE_FR))
			steer = 127;
		else if ((ring & SENSE_FR) && !(ring & SENSE_FL))
			steer = -127;
	}

	ProcessCarPad(cp, pad, (char)steer, 1);
	return 1;
}

// test hook REDRIVER2_CHASEKNOCK: puts the car off the road facing away, to measure the way back (log line chaseknock)
static void TestKnock(CAR_DATA* cp)
{
	static int enabled = -1, counter = 0, knocks = 0;
	int i, side;

	if (enabled < 0)
		enabled = getenv("REDRIVER2_CHASEKNOCK") != NULL;

	// back = the civilian AI drives it on the road for half a second
	if (sKnockSteps >= 0)
	{
		sKnockSteps++;
		sKnockOnRoad = (CarOnRoad(cp) && sPadsDone) ? sKnockOnRoad + 1 : 0;

		if (sKnockOnRoad >= D1CHASE_BACK_STEPS)
		{
			printInfo("chaseknock: back on the road after %d steps\n", sKnockSteps - D1CHASE_BACK_STEPS);
			sKnockSteps = -1;
		}
	}

	// due: the next step the civilian AI drives it on the road
	if (!enabled || ++counter < D1CHASE_KNOCK_EVERY || sRecovering || sKnockSteps >= 0 || !CarOnRoad(cp))
		return;

	side = (knocks & 1) ? 1 : -1;

	// sideways past the road edge, on this side or else the other, not inside a building
	for (i = 0; i < 2; i++)
	{
		int dirSide = i == 0 ? side : -side;
		int s, x, z, yaw;
		VECTOR spot;

		for (s = 300; s < 4000; s += 100)
		{
			if (!OnRoad(cp->hd.where.t[0] + FIXEDH(cp->hd.where.m[0][0] * s * dirSide), cp->hd.where.t[1], cp->hd.where.t[2] + FIXEDH(cp->hd.where.m[2][0] * s * dirSide)))
				break;
		}

		if (s >= 4000)
			continue;

		s = (s + D1CHASE_KNOCK_OFF) * dirSide;
		x = cp->hd.where.t[0] + FIXEDH(cp->hd.where.m[0][0] * s);
		z = cp->hd.where.t[2] + FIXEDH(cp->hd.where.m[2][0] * s);
		yaw = (cp->hd.direction + (s > 0 ? 1024 : -1024)) & 4095;

		spot.vx = x;
		spot.vy = cp->hd.where.t[1];
		spot.vz = z;

		if (QuickBuildingCollisionCheck(&spot, yaw, cp->ap.carCos->colBox.vz, cp->ap.carCos->colBox.vx, 0x14))
			continue;

		PlaceCar(cp, x, z, yaw);
		cp->st.n.linearVelocity[0] = cp->st.n.linearVelocity[1] = cp->st.n.linearVelocity[2] = 0;
		cp->st.n.angularVelocity[0] = cp->st.n.angularVelocity[1] = cp->st.n.angularVelocity[2] = 0;
		cp->hd.wheel_speed = 0;
		RebuildCarMatrix(&cp->st, cp);

		knocks++;
		counter = 0;
		sKnockSteps = 0;
		sKnockOnRoad = 0;
		printInfo("chaseknock: %d at %d %d, %d to the side\n", knocks, x, z, s);
		return;
	}
}

int D1ChasePath_ProcessPad(CAR_DATA* cp)
{
	const unsigned char* rec;
	u_int pad;

	if (!IsChaseCar(cp) || sWrecked)
		return 0;

	TestKnock(cp);

	// without a pad stream the civilian AI drives from the start, watched like after the stream
	if (sPads == NULL && !sRecovering && !sPadsDone)
	{
		sPadsDone = 1;
		sMonFrame = 0;
		sMonX = cp->hd.where.t[0];
		sMonZ = cp->hd.where.t[2];
	}

	// the civilian AI waits in a jam like any traffic; the chased car must keep going, so it backs off and tries again
	if (sPadsDone)
	{
		int mx = cp->hd.where.t[0] - sMonX, mz = cp->hd.where.t[2] - sMonZ;
		int gaveUp = cp->ai.c.thrustState == CIV_AI_THRUST_STOP && ABS(FIXEDH(cp->hd.wheel_speed)) < 3;	// the civilian AI stopped for good (lost its road): no need to wait

		// off the road surface for half a second (pushed onto the pavement or grass, among trees): no need to wait either
		sOffRoad = CarOnRoad(cp) ? 0 : sOffRoad + 1;

		if (sOffRoad == 0)
		{
			sRoadX = cp->hd.where.t[0];
			sRoadZ = cp->hd.where.t[2];
			sRoadDir = cp->hd.direction;
			sRoadKnown = 1;
		}

		if (sOffRoad > D1CHASE_OFFROAD_STEPS)
			gaveUp = 1;

		if (++sMonFrame < D1CHASE_JAM_FRAMES && !gaveUp)
			return 0;

		sMonFrame = 0;
		sMonX = cp->hd.where.t[0];
		sMonZ = cp->hd.where.t[2];

		// per axis: a squared distance from a far start overflows
		if (!gaveUp && (ABS(mx) > D1CHASE_STALL_DIST || ABS(mz) > D1CHASE_STALL_DIST))
			return 0;

		sPadsDone = 0;
		sRecovering = 1;
		sRecFrame = 0;
		sRevLeft = FrontShut(SenseRing(cp)) ? D1CHASE_REVERSE_MAX : 0;		// only back up when the way ahead is shut
		sLaneX = 0;
		printInfo("D1 chase car %s, %s\n", sOffRoad > D1CHASE_OFFROAD_STEPS ? "off the road" : "stuck", sRevLeft ? "backing off" : "driving on");
		sOffRoad = 0;
	}

	if (sRecovering)
		return Recover(cp);

	// no input before the first placement, or the step of the spawn frame would move the car off frame 0
	if (!sPlaced)
		return 1;

	// after a crash the open loop pads cannot get the car going again: when it stands, or the stream is over, the civilian AI takes it
	if (sRouteOver)
	{
		int offX = (cp->st.n.fposition[0] >> 4) - sStallX;
		int offZ = (cp->st.n.fposition[2] >> 4) - sStallZ;

		if (sStall == 0 || offX * offX + offZ * offZ > D1CHASE_STALL_DIST * D1CHASE_STALL_DIST)
		{
			sStall = 1;
			sStallX = cp->st.n.fposition[0] >> 4;
			sStallZ = cp->st.n.fposition[2] >> 4;
		}
		else
			sStall++;
	}

	if (sFrame >= sPadCount || sStall > D1CHASE_STALL_FRAMES)
	{
		printInfo("D1 chase pads over after %d frames, driving back to the road\n", sFrame);
		sRecovering = 1;
		return Recover(cp);
	}

	rec = &sPads[sFrame++ * 3];
	pad = rec[0] | (rec[1] << 8);

	// bit 0 marks a frame with analogue steering (the stick value may be 0)
	ProcessCarPad(cp, pad & ~1u, (char)rec[2], pad & 1);

	if ((pad & CAR_PAD_ACCEL) && !(pad & CAR_PAD_BRAKE) && cp->thrust > 1)
		cp->thrust = D1CHASE_THRUST;

	return 1;
}

void D1ChaseCar_Wrecked(void)
{
	sWrecked = 1;
}

void D1ChaseCar_LimitHit(CAR_DATA* cp, int before)
{
	int cap, gain;

	if (!IsChaseCar(cp) || cp->totalDamage <= before)
		return;

	if (FrameCnt - sHitFrame > D1CHASE_HIT_GAP)
		sHitSum = 0;

	sHitFrame = FrameCnt;
	cap = MAX(DamageBar.max / D1CHASE_MIN_HITS, 1);
	gain = MIN(cp->totalDamage - before, MAX(cap - sHitSum, 0));
	sHitSum += gain;
	cp->totalDamage = before + gain;
}

int D1ChaseCar_DamageCounts(CAR_DATA* cp, CAR_DATA* other)
{
	if (!IsChaseCar(cp))
		return 1;

	return other != NULL && other->controlType == CONTROL_TYPE_PLAYER;
}
