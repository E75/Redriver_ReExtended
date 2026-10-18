#include "driver2.h"
#include "d1chase.h"
#include "handling.h"
#include "cosmetic.h"
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

static D1CHASE_FRAME* sFrames = NULL;
static int sFrameCount = 0;
static int sFrame = 0;

int D1ChasePath_Load(int missionNumber)
{
	char filename[64];
	int head[3], variants, pick, i;
	int offset;

	free(sFrames);
	sFrames = NULL;
	sFrameCount = 0;
	sFrame = 0;

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

	if (sFrames == NULL)
		return 0;

	if (sFrame + 1 >= sFrameCount)
	{
		if (sFrame + 1 == sFrameCount)
		{
			printInfo("D1 chase route over after %d frames, civilian AI drives on\n", sFrame);
			sFrame++;
		}

		return 0;
	}

	cur = &sFrames[sFrame];
	next = &sFrames[sFrame + 1];

	// the physics moved the car one step from the last recorded frame; without a contact that lands on this frame, a building
	// pushes it back (the recorded route is only valid until the first contact), then the civilian AI drives on
	if (sFrame > 0)
	{
		int offX = (cp->st.n.fposition[0] >> 4) - cur->x;
		int offZ = (cp->st.n.fposition[2] >> 4) - cur->z;

		if (offX * offX + offZ * offZ > D1CHASE_CONTACT_DIST * D1CHASE_CONTACT_DIST)
		{
			printInfo("D1 chase route left after %d frames (off by %d %d), civilian AI drives on\n", sFrame, offX, offZ);
			sFrame = sFrameCount;
			return 0;
		}
	}

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
