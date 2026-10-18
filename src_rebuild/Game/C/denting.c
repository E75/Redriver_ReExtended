#include "driver2.h"
#include "cosmetic.h"
#include "denting.h"
#include "system.h"
#include "mission.h"
#include "cars.h"
#include "convert.h"
#include "pause.h"
#include "camera.h"
#include "draw.h"
#include "dr2roads.h"
#include "debris.h"
#include "players.h"
#include "main.h"

#if USE_PC_FILESYSTEM
extern int gContentOverride;

// [A] loads car model from file
char* LoadCustomCarDentingFromFile(char* dest, int modelNumber)
{
	char* mem;
	char filename[64];

	sprintf(filename, "LEVELS\\%s\\CARMODEL_%d.DEN", LevelNames[GameLevel], modelNumber);
	if (FileExists(filename))
	{
		mem = dest ? dest : ((char*)_other_buffer + modelNumber * 4096);

		// get model from file
		Loadfile(filename, mem);
		return mem;
	}

	return NULL;
}
#endif

char* DentingFiles[] =
{
	"LEVELS\\CHICAGO.DEN",
	"LEVELS\\HAVANA.DEN",
	"LEVELS\\VEGAS.DEN",
	"LEVELS\\RIO.DEN",
};

// DO NOT CHANGE THIS!
#define NUM_DAMAGE_ZONES			6
#define MAX_FILE_DAMAGE_ZONE_VERTS	50
#define MAX_FILE_DAMAGE_ZONE_POLYS	70
#define MAX_FILE_DAMAGE_LEVELS		256

u_char gCarDamageZoneVerts[MAX_CAR_RESIDENT_MODELS][NUM_DAMAGE_ZONES][MAX_DAMAGE_ZONE_VERTS];
u_char gHDCarDamageZonePolys[MAX_CAR_RESIDENT_MODELS][NUM_DAMAGE_ZONES][MAX_DAMAGE_ZONE_POLYS];
u_char gHDCarDamageLevels[MAX_CAR_RESIDENT_MODELS][MAX_DAMAGE_LEVELS];	// the damage level (texture) count for polygons

// [D] [T]
void InitialiseDenting(void)
{
	LoadDenting(BASE_CITY);
	InitHubcap();
}

#ifdef PSX
#define DENT_OFFSET(cp, vi, axis)	0
#define DENT_ZONE_BLEND		1 / 2
#else
// [A] impact dents: a hit pushes the vertices round the hit point inwards, on top of the zone damage (DentCar adds
// the offsets). Hit points are in model space (x right, y down, z along the nose), every car has its own offsets.
static SVECTOR gDentOffset[MAX_CARS][MAX_DENTING_VERTS];
static unsigned char gDentDirty[MAX_CARS];
static short gWheelStress[MAX_CARS][4];		// hits taken near each wheel; a wheel that took enough comes off
static unsigned char gWheelLost[MAX_CARS];		// bit per wheel (physics order: 0 and 2 front, 1 and 3 rear) that was torn off

int gImpactDents = 2;							// option Impact Dents (Extended Settings): 0 Off, 1 Sensitive, 2 Less Sensitive (default); dents and lost wheels

#define DENT_WHEEL_DAMAGE	99		// percent of the damage bar from which a hard hit at a wheel tears it off
#define DENT_WHEEL_MIN_HIT	300		// damage value of that hit
#define DENT_WHEEL_STRESS	3000	// hits taken near one wheel (sum of the damage values) after which it hangs loose and comes off
#define DENT_WHEEL_STRESS_DAMAGE	40	// percent of the damage bar the car needs for that
#define DENT_WHEEL_REACH	210		// how close (units, Driver 2 sized car) the hit must be to the wheel

int WheelIsLost(CAR_DATA* cp, int wheel)
{
	return gWheelLost[cp->id] >> wheel & 1;
}

// a hard hit at a wheel of a car with more than 80 percent damage tears that wheel off: it is not drawn, carries less and drags
static void MaybeLoseWheel(CAR_DATA* cp, int hx, int hz, int value, int scale)
{
	CAR_COSMETICS* cos = cp->ap.carCos;
	const SVECTOR* wheelDisp = gD1WheelVisualOn[cp->ap.model] ? gD1WheelVisual[cp->ap.model] : cos->wheelDisp;	// Driver 1 cars: where the wheels are drawn
	int i;
	long long reach2 = (long long)(DENT_WHEEL_REACH * scale >> 12) * (DENT_WHEEL_REACH * scale >> 12);

	int nearlyWrecked = cp->totalDamage * 100 >= MaxPlayerDamage[0] * DENT_WHEEL_DAMAGE && value >= DENT_WHEEL_MIN_HIT && (Random2(0) & 3) != 0;
	int damaged = cp->totalDamage * 100 >= MaxPlayerDamage[0] * DENT_WHEEL_STRESS_DAMAGE;

	// every wheel within reach of the hit goes (a corner hit one wheel, a hit in the middle of the front or rear both)
	for (i = 0; i < 4; i++)
	{
		// wheel position in model space (DrawCarWheels: x = -wheelDisp.vx, z = -wheelDisp.vz)
		long long dx = hx + wheelDisp[i].vx, dz = hz + wheelDisp[i].vz, d2 = dx * dx + dz * dz;

		if (!WheelIsLost(cp, i) && d2 < reach2)
			gWheelStress[cp->id][i] = MIN(gWheelStress[cp->id][i] + value, 30000);

		if (!WheelIsLost(cp, i) && d2 < reach2 && (nearlyWrecked || (damaged && gWheelStress[cp->id][i] >= DENT_WHEEL_STRESS)))
		{
			gWheelLost[cp->id] |= 1 << i;
			gDentDirty[cp->id] = 1;
		}
	}
}

#define DENT_OFFSET(cp, vi, axis)	(gDentOffset[(cp)->id][vi].axis)

#define DENT_MIN_VALUE		250		// Less Sensitive: a hit below this damage value leaves no dent
#define DENT_DEPTH_DIV_SENSITIVE	16	// Sensitive: every hit dents, (damage value) / this
#define DENT_DEPTH_DIV		24		// (damage value - minimum) / this = depth of a dent for a Driver 2 sized car
#define DENT_MAX_DEPTH		85
#define DENT_RADIUS_MIN		90
#define DENT_MAX_TOTAL		110		// no vertex moves farther than this in all dents together
#define DENT_FRONT_STRETCH	1.6f	// front and rear dents reach this much farther along the car
#define DENT_SIDE_STRETCH	2.0f	// side dents reach from wheel to wheel
#define DENT_CABIN_PERCENT	35		// percent of a dent that reaches the cabin (roof, windows, pillars): the driver's space stays whole
#define DENT_SIDE_BEND		0.5f	// a side hit swings the front and rear end this much of the dent depth towards the hit side
#define DENT_ROOF_SAG		30		// percent of the dent depth the roof beside a side hit sinks
#define DENT_ZONE_BLEND		3 / 10	// the damaged model is blended in at this part of the original strength: the dents carry the shape
#define DENT_CRINKLE		15		// percent: the dent depth varies a bit from vertex to vertex

void ImpactDent(CAR_DATA* cp, int hx, int hy, int hz, int value)
{
	MODEL* model = gCarCleanModelPtr[cp->ap.model];
	CAR_COSMETICS* cos = cp->ap.carCos;
	SVECTOR* clean;
	SVECTOR* off = gDentOffset[cp->id];
	int scale, depth, radius, maxTotal;
	int dirX, dirY, dirZ, len, vi;
	float zStretch, xStretch;
	int frontRear, roofLine = -32767, beltLine, minY = 32767, maxY = -32767;

	if (!gImpactDents || model == NULL || value <= 0 || model->num_vertices >= MAX_DENTING_VERTS)
		return;

	// god mode: the player's car stays whole
	if (gInvincibleCar && cp->controlType == CONTROL_TYPE_PLAYER)
		return;

	// sizes are for a Driver 2 car (half length 375), longer cars dent over a larger area
	scale = cos->colBox.vz * 4096 / 375;
	depth = MIN(MAX(value - (gImpactDents == 1 ? 0 : DENT_MIN_VALUE), 0) / (gImpactDents == 1 ? DENT_DEPTH_DIV_SENSITIVE : DENT_DEPTH_DIV), DENT_MAX_DEPTH) * scale >> 12;
	radius = (DENT_RADIUS_MIN + value / 16) * scale >> 12;
	maxTotal = DENT_MAX_TOTAL * scale >> 12;

	// inwards: from the hit to the middle of the car, mostly horizontal
	dirX = cos->cog.vx - hx;
	dirY = (cos->cog.vy - hy) / 3;
	dirZ = cos->cog.vz - hz;
	len = SquareRoot0(dirX * dirX + dirY * dirY + dirZ * dirZ);

	if (len < 1 || depth < 1)
		return;

	dirX = dirX * 4096 / len;
	dirY = dirY * 4096 / len;
	dirZ = dirZ * 4096 / len;

	clean = GET_MODEL_DATA(SVECTOR, model, vertices);

	// a hit on the front or the rear crumples back to the wheel arch and stays on its side of the car; a hit on the side
	// pushes the doors straight in from wheel to wheel and presses the roof beside it down
	for (vi = 0; vi < model->num_vertices; vi++)
	{
		minY = MIN(minY, clean[vi].vy);
		maxY = MAX(maxY, clean[vi].vy);
	}

	beltLine = minY + (maxY - minY) * 55 / 100;		// y is down: above this line lies the cabin (roof, windows, pillars)

	frontRear = ABS(hz - cos->cog.vz) > cos->colBox.vz * 6 / 10;

	if (frontRear)
	{
		zStretch = DENT_FRONT_STRETCH;
		xStretch = 0.75f;
	}
	else
	{
		zStretch = DENT_SIDE_STRETCH;
		xStretch = 1.0f;
		dirX = dirX < 0 ? -4096 : 4096;
		dirY = 0;
		dirZ = 0;
		depth = depth * 14 / 10;

		roofLine = minY + (maxY - minY) * 35 / 100;		// y is down: the upper third of the body
	}

	for (vi = 0; vi < model->num_vertices; vi++)
	{
		int px = clean[vi].vx + off[vi].vx;
		int py = clean[vi].vy + off[vi].vy;
		int pz = clean[vi].vz + off[vi].vz;
		float nx = (px - hx) / (radius * xStretch), ny = (py - hy) / (radius * 1.2f), nz = (pz - hz) / (radius * zStretch);
		float n2 = nx * nx + ny * ny + nz * nz;
		int t, w, middle, push, ox, oy, oz, sag = 0;

		if (!frontRear)
		{
			// banana shape: the far ends of the car lean towards the side of the hit, the middle gives in
			float ez = (ABS(pz - cos->cog.vz) - cos->colBox.vz * 0.4f) / (cos->colBox.vz * 0.6f);

			if (ez > 0.0f)
			{
				ez = MIN(ez, 1.0f);
				off[vi].vx -= (int)(depth * DENT_SIDE_BEND * ez * ez) * (dirX < 0 ? -1 : 1);
				off[vi].vx = MAX(-maxTotal, MIN(maxTotal, off[vi].vx));
			}
		}

		if (py < roofLine)
		{
			float rx = (px - hx) / (radius * 1.5f), rz = (pz - hz) / (radius * zStretch * 1.2f);
			float r2 = rx * rx + rz * rz;

			if (r2 < 1.0f)
			{
				t = (int)((1.0f - sqrtf(r2)) * 4096);
				sag = depth * DENT_ROOF_SAG / 100 * (t * t >> 12) >> 12;
				sag = sag * DENT_CABIN_PERCENT / 100;
			}
		}

		if (n2 >= 1.0f && sag == 0)
			continue;

		if (n2 >= 1.0f)
		{
			// only the roof moves
			off[vi].vy += sag;
			continue;
		}

		t = (int)((1.0f - sqrtf(n2)) * 4096);
		w = t * t >> 12;

		// the middle of the car (cabin) resists: no dent when the vertex lies close to the centre line of the body
		middle = SquareRoot0((px - cos->cog.vx) * (px - cos->cog.vx) + (pz - cos->cog.vz) * (pz - cos->cog.vz));
		if (frontRear)
			w = w * MIN(4096, middle * 4096 / (cos->colBox.vz / 2 + 1)) >> 12;

		if (py < beltLine && ABS(pz - cos->cog.vz) < cos->colBox.vz * 6 / 10)
			w = w * DENT_CABIN_PERCENT / 100;

		w = w * (4096 + RSIN(vi * 977 + hx * 3 + hz * 7) * DENT_CRINKLE / 100) >> 12;
		push = depth * w >> 12;

		ox = off[vi].vx + (dirX * push >> 12);
		oy = off[vi].vy + (dirY * push >> 12) + sag;
		oz = off[vi].vz + (dirZ * push >> 12);

		len = SquareRoot0(ox * ox + oy * oy + oz * oz);

		if (len > maxTotal)
		{
			ox = ox * maxTotal / len;
			oy = oy * maxTotal / len;
			oz = oz * maxTotal / len;
		}

		off[vi].vx = ox;
		off[vi].vy = oy;
		off[vi].vz = oz;
	}

	gDentDirty[cp->id] = 1;
	cp->ap.needsDenting = 1;

	// wheels only come off in Take a Ride, a mission keeps the car drivable
	if (GameType == GAME_TAKEADRIVE)
		MaybeLoseWheel(cp, hx, hz, value, scale);
}

// a car that leaves (pinged out) or is created loses its dents, so the next car in the slot is whole
void ResetImpactDents(int carId)
{
	if (gDentDirty[carId])
	{
		memset(gDentOffset[carId], 0, sizeof(gDentOffset[carId]));
		gWheelLost[carId] = 0;
		memset(gWheelStress[carId], 0, sizeof(gWheelStress[carId]));
		gDentDirty[carId] = 0;
	}
}
#endif

// [D] [T]
void DentCar(CAR_DATA *cp)
{
	int Zone;
	int Damage;
	int MaxDamage;
	DENTUVS *dentptr;
	SVECTOR *CleanVertPtr;
	int VertNo;
	unsigned char *DamPtr;
	SVECTOR *DamVertPtr;
	MODEL *pCleanModel;
	int model;
	int Poly;

	short tempDamage[256];
	memset((u_char*)tempDamage, 0, sizeof(tempDamage));

	MaxDamage = 0;
	model = cp->ap.model;
	cp->lowDetail = -1;
	cp->ap.qy = 0;
	cp->ap.qw = 0;

	pCleanModel = gCarCleanModelPtr[model];

	// collect vertices from zones
	if (pCleanModel != NULL) 
	{
		for (VertNo = 0; VertNo < pCleanModel->num_vertices; VertNo++)
			tempDamage[VertNo] = 0;

		for (Zone = 0; Zone < NUM_DAMAGE_ZONES; Zone++)
		{
			Damage = cp->ap.damage[Zone];

			if (Damage > MaxDamage)
				MaxDamage = Damage;

			DamPtr = gCarDamageZoneVerts[cp->ap.model][Zone];

			for (VertNo = 0; VertNo < MAX_DAMAGE_ZONE_VERTS && *DamPtr != 0xFF; VertNo++, DamPtr++)
			{
				if (tempDamage[*DamPtr] == 0)
					tempDamage[*DamPtr] += Damage;
				else 
					tempDamage[*DamPtr] += Damage / 2;
			}
		} 
	}

	// update vertices positon
	if (gCarCleanModelPtr[model] != NULL && gCarDamModelPtr[model] != NULL) 
	{
		DamVertPtr = GET_MODEL_DATA(SVECTOR, gCarDamModelPtr[model], vertices);
		CleanVertPtr = GET_MODEL_DATA(SVECTOR, gCarCleanModelPtr[model], vertices);

		for (VertNo = 0; VertNo < pCleanModel->num_vertices; VertNo++, DamVertPtr++, CleanVertPtr++)
		{
			gTempCarVertDump[cp->id][VertNo].vx = CleanVertPtr->vx + FIXEDH((DamVertPtr->vx - CleanVertPtr->vx) * tempDamage[VertNo] * DENT_ZONE_BLEND) + DENT_OFFSET(cp, VertNo, vx);
			gTempCarVertDump[cp->id][VertNo].vy = CleanVertPtr->vy + FIXEDH((DamVertPtr->vy - CleanVertPtr->vy) * tempDamage[VertNo] * DENT_ZONE_BLEND) + DENT_OFFSET(cp, VertNo, vy);
			gTempCarVertDump[cp->id][VertNo].vz = CleanVertPtr->vz + FIXEDH((DamVertPtr->vz - CleanVertPtr->vz) * tempDamage[VertNo] * DENT_ZONE_BLEND) + DENT_OFFSET(cp, VertNo, vz);
		}
	}

	// update polygon UVs
	if (pCleanModel != NULL) 
	{
		dentptr = gTempHDCarUVDump[cp->id];

		// reset UV coordinates
		
		for (Poly = 0; Poly < pCleanModel->num_polys; Poly++)
		{
			dentptr->u3 = 0;
			dentptr++;
		}

		for(Zone = 0; Zone < NUM_DAMAGE_ZONES; Zone++)
		{
			Damage = cp->ap.damage[Zone];

			for (Poly = 0; Poly < MAX_DAMAGE_ZONE_POLYS; Poly++)
			{
				if (gHDCarDamageZonePolys[cp->ap.model][Zone][Poly] == 0xFF)
					break;

				dentptr = gTempHDCarUVDump[cp->id] + gHDCarDamageZonePolys[cp->ap.model][Zone][Poly];

				// add a damage level
				dentptr->u3 += (Damage >> 0xA);

				// clamp level
				if (dentptr->u3 > 2)
					dentptr->u3 = 2;
			}
		}

		DamPtr = gHDCarDamageLevels[model];
		dentptr = gTempHDCarUVDump[cp->id];

		for (Poly = 0; Poly < pCleanModel->num_polys; Poly++, DamPtr++, dentptr++)
		{
			// calculate the UV offset with strange XORs
			if(dentptr->u3 > 0)
				dentptr->u3 = (*DamPtr ^ 1 ^ (*DamPtr ^ 1 | dentptr->u3)) * 64;
		}
	}
}

// [D] [T]
void CreateDentableCar(CAR_DATA *cp)
{
	MODEL *srcModel;
	SVECTOR *dst;
	int count;
	SVECTOR *src;
	int vcount;
	int model;

	model = cp->ap.model;

	srcModel = gCarCleanModelPtr[model];
	if (srcModel != NULL)
	{
		src = GET_MODEL_DATA(SVECTOR, srcModel, vertices);
		dst = gTempCarVertDump[cp->id];

		vcount = srcModel->num_vertices;

		while (vcount-- != -1) 
			*dst++ = *src++;

		for (count = 0; count < srcModel->num_polys; count++)
		{
			gTempHDCarUVDump[cp->id][count].u3 = 0;
		}
	}
	else
	{
		printError("gCarCleanModelPtr is NULL in CreateDentableCar\n");
	}

	srcModel = gCarLowModelPtr[model];
	if (srcModel != NULL)
	{
		for (count = 0; count < srcModel->num_polys; count++)
		{
			gTempLDCarUVDump[cp->id][count].u3 = 0;
		}
	}
	else
	{
		printError("gCarLowModelPtr is NULL in CreateDentableCar\n");
	}

	if (gDontResetCarDamage == 0) 
	{
#ifndef PSX
		ResetImpactDents(cp->id);
#endif
		for (count = 0; count < NUM_DAMAGE_ZONES; count++)
		{
			cp->ap.damage[count] = 0;
		}

		cp->totalDamage = 0;
	}
}

extern HUBCAP gHubcap;
int gHubcapTime = 0;

// [D] [T]
void InitHubcap(void)
{
	gHubcapTime = Random2(1) & 0x7ff;
}


// [D] [T]
void LoseHubcap(int car, int Hubcap, int Velocity)
{
	SVECTOR InitialLocalAngle = { 0, 0, 10 };
	CAR_DATA* cp;

	SVECTOR* wheelDisp;
	
	cp = &car_data[car];

	// check speed and if hubcap lost
	if (cp->hd.wheel_speed < 0 || (cp->ap.flags & 1 << Hubcap))
		return;

	wheelDisp = &cp->ap.carCos->wheelDisp[Hubcap];

	cp->ap.flags |= (1 << Hubcap);		// [A] cars now hold hubcaps

	gHubcap.Position.vx = wheelDisp->vx;
	gHubcap.Position.vy = wheelDisp->vy;
	gHubcap.Position.vz = wheelDisp->vz;

	SetRotMatrix(&cp->hd.where);
	_MatrixRotate(&gHubcap.Position);

	gHubcap.Position.vx += cp->hd.where.t[0];
	gHubcap.Position.vy -= cp->hd.where.t[1];
	gHubcap.Position.vz += cp->hd.where.t[2];

	gHubcap.Orientation = cp->hd.where;
	gHubcap.Rotation = 0;
	
	if (Hubcap > 1)
	{
		gHubcap.Orientation.m[0][0] = -gHubcap.Orientation.m[0][0];
		gHubcap.Orientation.m[1][0] = -gHubcap.Orientation.m[1][0];

		gHubcap.Orientation.m[2][0] = -gHubcap.Orientation.m[2][0];
		gHubcap.Orientation.m[0][2] = -gHubcap.Orientation.m[0][2];

		gHubcap.Orientation.m[1][2] = -gHubcap.Orientation.m[1][2];
		gHubcap.Orientation.m[2][2] = -gHubcap.Orientation.m[2][2];
	}

	gHubcap.Duration = 100;
	
	gHubcap.Direction.vx = FIXEDH(FIXEDH(cp->st.n.angularVelocity[1]) * wheelDisp->vz) + FIXEDH(cp->st.n.linearVelocity[0]);
	gHubcap.Direction.vy = FIXEDH(cp->st.n.linearVelocity[1]);
	gHubcap.Direction.vz = FIXEDH(-FIXEDH(cp->st.n.angularVelocity[1]) * wheelDisp->vx) + FIXEDH(cp->st.n.linearVelocity[2]);
}


// [A]
void HandlePlayerHubcaps(int playerId)
{
	int VelocityMagnitude;
	CAR_DATA* cp;
	int playerCarId;

	playerCarId = player[playerId].playerCarId;

	if (playerCarId < 0)
		return;

	cp = &car_data[playerCarId];

	if (gHubcap.Duration < 1 && gHubcapTime == 0)
	{
		VelocityMagnitude = cp->st.n.angularVelocity[0] + cp->st.n.angularVelocity[1] + cp->st.n.angularVelocity[2];

		if (VelocityMagnitude < -1000000)
		{
			LoseHubcap(playerCarId, Random2(2) & 1, VelocityMagnitude);
			VelocityMagnitude = 3;
		}
		else if (VelocityMagnitude > 1000001)
		{
			LoseHubcap(playerCarId, Random2(4) & 1 | 2, VelocityMagnitude);
			VelocityMagnitude = 5;
		}
		else
			return;

		gHubcapTime = Random2(VelocityMagnitude) & 0x417;
	}
}

// [D] [T]
void MoveHubcap()
{
	int CurrentMapHeight;
	int savecombo;
	int cmb;
	VECTOR ShadowPos;
	VECTOR Position;
	MATRIX Orientation;
	CVECTOR col = {72,72,72};

	InitMatrix(Orientation);

	if (pauseflag == 0)
	{
		if(gHubcapTime > 0)
			gHubcapTime--;
	}

	// draw current hubcap
	if (gHubcap.Duration > 0) 
	{
		if (pauseflag == 0 && CurrentPlayerView == 0)
		{
			gHubcap.Duration--;

			// move hubcap
			gHubcap.Position.vx += gHubcap.Direction.vx;
			gHubcap.Position.vy += gHubcap.Direction.vy;
			gHubcap.Position.vz += gHubcap.Direction.vz;

			gHubcap.Rotation -= 220;

			_RotMatrixX(&Orientation, gHubcap.Rotation);

			gHubcap.Direction.vy += 5;

			CurrentMapHeight = MapHeight(&gHubcap.Position);

			if (gHubcap.Position.vy >= -40 - CurrentMapHeight)
			{
				gHubcap.Direction.vy = -(gHubcap.Direction.vy / 2);
				gHubcap.Position.vy = -40 - CurrentMapHeight;
			}
		}

		MulMatrix0(&gHubcap.Orientation, &Orientation, &Orientation);

		ShadowPos.vx = gHubcap.Position.vx - camera_position.vx;
		ShadowPos.vy = -MapHeight(&gHubcap.Position);
		ShadowPos.vz = gHubcap.Position.vz - camera_position.vz;

		Position.vx = ShadowPos.vx;
		Position.vy = gHubcap.Position.vy - camera_position.vy;
		Position.vz = ShadowPos.vz;

		SetRotMatrix(&inv_camera_matrix);

		_MatrixRotate(&Position);
		savecombo = combointensity;

		if (gTimeOfDay == TIME_NIGHT)
		{
			cmb = (combointensity & 255) / 3;
			combointensity = cmb << 0x10 | cmb << 8 | cmb;
		}

		RenderModel(gHubcapModelPtr, &Orientation, &Position, 0, 0, 0, 0);
		ShadowPos.vy -= camera_position.vy;

		combointensity = savecombo;

		RoundShadow(&ShadowPos, &col, 65);
	}
}

// [D] [T]
void ProcessDentLump(char *lump_ptr, int lump_size)
{
	int model;
	int i;
	int offset;
	u_char* mem;

	for (i = 0; i < MAX_CAR_RESIDENT_MODELS; i++)
	{
		model = residentCarModels[i];

		if (model == 13)
		{
			model = 10 - (residentCarModels[0] + residentCarModels[1] + residentCarModels[2]);

			if (model < 1) 
				model = 1;
			else if(model > 4)
				model = 4;
		}

		if (model != -1) 
		{
			offset = *(int *)(lump_ptr + model * 4);
			mem = (u_char*)lump_ptr;
#if USE_PC_FILESYSTEM
			if (gContentOverride)
			{
				char* newDenting;
				newDenting = LoadCustomCarDentingFromFile(NULL, model);
				if (newDenting)
				{
					mem = (u_char*)newDenting;
					offset = 0;
				}
			}
#endif

			memcpy((u_char*)gCarDamageZoneVerts[i], mem + offset, NUM_DAMAGE_ZONES * MAX_FILE_DAMAGE_ZONE_VERTS);
			offset += NUM_DAMAGE_ZONES * MAX_FILE_DAMAGE_ZONE_VERTS;
			
			memcpy((u_char*)gHDCarDamageZonePolys[i], mem + offset, NUM_DAMAGE_ZONES * MAX_FILE_DAMAGE_ZONE_POLYS);
			offset += NUM_DAMAGE_ZONES * MAX_FILE_DAMAGE_ZONE_POLYS;
			
			memcpy((u_char*)gHDCarDamageLevels[i], mem + offset, MAX_FILE_DAMAGE_LEVELS);
		}
	}
}


// [D] [T]
void SetupSpecDenting(char *loadbuffer)
{
	int offset;

#if USE_PC_FILESYSTEM
	if (gContentOverride)
	{
		char* newDenting;
		int model;
		model = residentCarModels[SPECIAL_CAR_SLOT];

		newDenting = LoadCustomCarDentingFromFile(NULL, model);
		if (newDenting)
			loadbuffer = newDenting;
	}
	
#endif

	// [A] this is better
	memcpy((u_char*)gCarDamageZoneVerts[SPECIAL_CAR_SLOT], (u_char*)loadbuffer, NUM_DAMAGE_ZONES * MAX_FILE_DAMAGE_ZONE_VERTS);
	offset = NUM_DAMAGE_ZONES * MAX_FILE_DAMAGE_ZONE_VERTS;

	memcpy((u_char*)gHDCarDamageZonePolys[SPECIAL_CAR_SLOT], (u_char*)loadbuffer + offset, NUM_DAMAGE_ZONES * MAX_FILE_DAMAGE_ZONE_POLYS);
	offset += NUM_DAMAGE_ZONES * MAX_FILE_DAMAGE_ZONE_POLYS;

	memcpy((u_char*)gHDCarDamageLevels[SPECIAL_CAR_SLOT], (u_char*)loadbuffer + offset, MAX_FILE_DAMAGE_LEVELS);
}

// [A] Driver 1 cars have no damage zone file: sort the vertices of the clean model into the six Driver 2 zones by position
// (0 front +x, 1 front -x, 2 side -x, 3 rear -x, 4 rear +x, 5 side +x; the front is -z, measured in Chicago). The damaged model gives the dent.
void D1BuildDamageZones(int slot)
{
	MODEL* clean = gCarCleanModelPtr[slot];
	MODEL* dam = gCarDamModelPtr[slot];
	SVECTOR* v;
	int zone, k, n, minZ = 32767, maxZ = -32767, third;
	int score[NUM_DAMAGE_ZONES][MAX_DAMAGE_ZONE_VERTS];
	int count[NUM_DAMAGE_ZONES];

	memset(gCarDamageZoneVerts[slot], 0xFF, sizeof(gCarDamageZoneVerts[slot]));
	memset(gHDCarDamageZonePolys[slot], 0xFF, sizeof(gHDCarDamageZonePolys[slot]));
	memset(gHDCarDamageLevels[slot], 0, sizeof(gHDCarDamageLevels[slot]));

	if (clean == NULL || dam == NULL || clean->num_vertices != dam->num_vertices || clean->num_vertices > 255)
		return;

	v = GET_MODEL_DATA(SVECTOR, clean, vertices);

	for (n = 0; n < clean->num_vertices; n++)
	{
		minZ = MIN(minZ, v[n].vz);
		maxZ = MAX(maxZ, v[n].vz);
	}

	third = (maxZ - minZ) * 38 / 100;

	// per zone the vertices farthest from the middle of the car stay, when more than the file format holds belong to it
	for (zone = 0; zone < NUM_DAMAGE_ZONES; zone++)
		for (k = 0; k < MAX_DAMAGE_ZONE_VERTS; k++)
			score[zone][k] = -1;

	for (n = 0; n < clean->num_vertices; n++)
	{
		int pos = v[n].vx >= 0;
		int s;

		if (v[n].vz < minZ + third)
		{
			zone = pos ? 0 : 1;
			s = -v[n].vz;
		}
		else if (v[n].vz > maxZ - third)
		{
			zone = pos ? 4 : 3;
			s = v[n].vz;
		}
		else
		{
			zone = pos ? 5 : 2;
			s = ABS(v[n].vx);
		}

		// insert into the list of this zone, sorted by distance, shortest dropped when it is full
		for (k = 0; k < MAX_DAMAGE_ZONE_VERTS; k++)
		{
			if (gCarDamageZoneVerts[slot][zone][k] == 0xFF || s > score[zone][k])
			{
				int m;

				for (m = MAX_DAMAGE_ZONE_VERTS - 1; m > k; m--)
				{
					gCarDamageZoneVerts[slot][zone][m] = gCarDamageZoneVerts[slot][zone][m - 1];
					score[zone][m] = score[zone][m - 1];
				}

				gCarDamageZoneVerts[slot][zone][k] = n;
				score[zone][k] = s;
				break;
			}
		}
	}

	for (zone = 0; zone < NUM_DAMAGE_ZONES; zone++)
		for (k = 0, count[zone] = 0; k < MAX_DAMAGE_ZONE_VERTS && gCarDamageZoneVerts[slot][zone][k] != 0xFF; k++)
			count[zone]++;

	printInfo("D1 damage zones slot %d: %d vertices in zones %d %d %d %d %d %d\n", slot, clean->num_vertices,
		count[0], count[1], count[2], count[3], count[4], count[5]);
}

// [D] [T]
void LoadDenting(int level)
{
	LoadfileSeg(DentingFiles[level], (char*)_other_buffer, 0, 12727);
	ProcessDentLump((char*)_other_buffer, 0);

	// [A] the damage zones of the borrowed Driver 2 city list Driver 2 vertices and polygons: Driver 1 cars do not dent
	if (IS_DRIVER1_CITY(GameLevel))
	{
		memset(gCarDamageZoneVerts, 0xFF, sizeof(gCarDamageZoneVerts));
		memset(gHDCarDamageZonePolys, 0xFF, sizeof(gHDCarDamageZonePolys));
		memset(gHDCarDamageLevels, 0, sizeof(gHDCarDamageLevels));

		for (int slot = 0; slot < MAX_CAR_RESIDENT_MODELS; slot++)
			D1BuildDamageZones(slot);
	}
}



