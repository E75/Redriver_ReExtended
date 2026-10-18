#include "driver2.h"
#include "d1chase.h"
#include "handling.h"
#include "cosmetic.h"
#include "pad.h"
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
#define D1CHASE_THRUST	4779			// thrust of the chased car while the throttle is down

static D1CHASE_FRAME* sFrames = NULL;
static int sFrameCount = 0;
static int sFrame = 0;			// route frames placed (no pad stream) or pad frames played (pad stream)
static int sRouteOver = 0;
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
			printInfo("D1 chase route left after %d frames (off by %d %d), %s drives on\n", sFrame, offX, offZ, sPads ? "the pad stream" : "civilian AI");
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

int D1ChasePath_ProcessPad(CAR_DATA* cp)
{
	const unsigned char* rec;
	u_int pad;

	if (sPads == NULL || !IsChaseCar(cp))
		return 0;

	// no input before the first placement, or the step of the spawn frame would move the car off frame 0
	if (!sPlaced)
		return 1;

	if (sFrame >= sPadCount)
	{
		cp->handbrake = 1;
		cp->wheel_angle = 0;
		return 1;
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
