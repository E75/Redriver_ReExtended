#include "driver2.h"
#include "d1chase.h"
#include "handling.h"
#include "cosmetic.h"
#include "pad.h"
#include "civ_ai.h"
#include "system.h"

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
#define D1CHASE_GIVE_UP	900			// steps of recovery before the civilian AI gets the car wherever it is
#define D1CHASE_JAM_FRAMES	90			// steps of the civilian AI without progress before the car backs off
#define D1CHASE_THRUST	4779			// thrust of the chased car while the throttle is down

static D1CHASE_FRAME* sFrames = NULL;
static int sFrameCount = 0;
static int sFrame = 0;			// route frames placed (no pad stream) or pad frames played (pad stream)
static int sRouteOver = 0;
static int sStall = 0;			// steps since the pad driven car last made progress
static int sStallX = 0, sStallZ = 0;	// where it was then
static int sRecovering = 0;		// the pad stream let go of the car, it drives back to a lane by itself
static int sPadsDone = 0;		// back on the road: the civilian AI drives on
static EXTRA_CIV_DATA sLane;		// the lane it is heading for
static int sLaneX, sLaneZ;
static int sMonFrame = 0, sMonX = 0, sMonZ = 0;		// progress check of the civilian AI driven car
static int sRecFrame = 0, sRevLeft = 0, sCheckX = 0, sCheckZ = 0;
static int sPlaced = 0;			// the car stood on route frame 0 once (the spawn spot is only near it)

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

int D1ChasePath_Step(CAR_DATA* cp)
{
	const D1CHASE_FRAME* cur;
	const D1CHASE_FRAME* next;
	int dx, dz, dz4;
	int sn, cs;

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

	sPlaced = 1;

	if (sPads == NULL)
		sFrame++;

	dx = next->x - cur->x;
	dz = next->z - cur->z;

	cp->hd.direction = cur->yaw;

	// same pitch of the body as the car physics start with (handling.c TempBuildHandlingMatrix)
	dz4 = (car_cosmetics[cp->ap.model].wheelDisp[0].vz + car_cosmetics[cp->ap.model].wheelDisp[1].vz) / 5;
	sn = RSIN(cur->yaw / 2);
	cs = RCOS(cur->yaw / 2);

	cp->st.n.fposition[0] = cur->x << 4;
	cp->st.n.fposition[2] = cur->z << 4;
	cp->st.n.orientation[0] = FIXEDH(-cs * dz4);
	cp->st.n.orientation[1] = sn;
	cp->st.n.orientation[2] = FIXEDH(sn * dz4);
	cp->st.n.orientation[3] = cs;

	// a step moves the car by linearVelocity / 4096 (handling.c), so the next hand-over keeps the recorded speed
	cp->st.n.linearVelocity[0] = dx * 4096;
	cp->st.n.linearVelocity[2] = dz * 4096;
	cp->hd.wheel_speed = (int)sqrt((double)dx * dx + (double)dz * dz) * 4096;

	RebuildCarMatrix(&cp->st, cp);
	return 1;
}

static int IsChaseCar(CAR_DATA* cp)
{
	return cp->inform != NULL && (*cp->inform & TARGET_FLAG_CAR_D1_CHASE);
}

// the car stands somewhere off its way (a crash): reverse and drive at the nearest traffic lane, then the civilian AI takes over
static int Recover(CAR_DATA* cp)
{
	int px = cp->hd.where.t[0], pz = cp->hd.where.t[2];
	int dx, dz, diff, steer, pad;

	if (sRecFrame % 30 == 0)
		CivFindLaneNear(cp, &sLane, &sLaneX, &sLaneZ);

	dx = sLaneX - px;
	dz = sLaneZ - pz;

	if ((sRecFrame > 60 && sLaneX != 0 && dx * dx + dz * dz < D1CHASE_REJOIN_DIST * D1CHASE_REJOIN_DIST) || sRecFrame > D1CHASE_GIVE_UP)
	{
		sLane.thrustState = CIV_AI_THRUST_ACCELERATE;
		sLane.ctrlState = CIV_AI_CTRL_DRIVE;
		sLane.controlFlags = 0;

		printInfo("D1 chase car back at the road after %d steps\n", sRecFrame);
		InitCivState(cp, sLaneX != 0 ? &sLane : NULL);
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
			sRevLeft = 45;

		sCheckX = px;
		sCheckZ = pz;
	}

	sRecFrame++;

	diff = ((ratan2(dx, dz) - cp->hd.direction + 2048) & 4095) - 2048;
	steer = diff / 12;
	steer = steer > 127 ? 127 : (steer < -127 ? -127 : steer);

	if (sRevLeft > 0)
	{
		sRevLeft--;
		pad = CAR_PAD_BRAKE;
		steer = -steer;
	}
	else
		pad = CAR_PAD_ACCEL;

	ProcessCarPad(cp, pad, (char)steer, 1);
	return 1;
}

int D1ChasePath_ProcessPad(CAR_DATA* cp)
{
	const unsigned char* rec;
	u_int pad;

	if (sPads == NULL || !IsChaseCar(cp))
		return 0;

	// the civilian AI waits in a jam like any traffic; the chased car must keep going, so it backs off and tries again
	if (sPadsDone)
	{
		int mx = cp->hd.where.t[0] - sMonX, mz = cp->hd.where.t[2] - sMonZ;

		if (++sMonFrame < D1CHASE_JAM_FRAMES)
			return 0;

		sMonFrame = 0;
		sMonX = cp->hd.where.t[0];
		sMonZ = cp->hd.where.t[2];

		if (mx * mx + mz * mz > D1CHASE_STALL_DIST * D1CHASE_STALL_DIST)
			return 0;

		printInfo("D1 chase car stands in traffic, backing off\n");
		sPadsDone = 0;
		sRecovering = 1;
		sRecFrame = 0;
		sRevLeft = 45;
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

int D1ChaseCar_DamageCounts(CAR_DATA* cp, CAR_DATA* other)
{
	if (!IsChaseCar(cp))
		return 1;

	return other != NULL && other->controlType == CONTROL_TYPE_PLAYER;
}
