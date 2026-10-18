#include "driver2.h"
#include "cosmetic.h"
#include "system.h"
#include "mission.h"
#include "handling.h"
#include "cars.h"
#include "debris.h"
#include "pause.h"
#include "camera.h"
#include "director.h"
#include "main.h"
#include "allcars.h"
#include "draw.h"
#include "ASM/rndrasm.h"

char* CosmeticFiles[] = {
	"LEVELS\\CHICAGO.LCF",
	"LEVELS\\HAVANA.LCF",
	"LEVELS\\VEGAS.LCF",
	"LEVELS\\RIO.LCF",
};

CAR_COSMETICS car_cosmetics[MAX_CAR_RESIDENT_MODELS];

// [A] Driver 1 player cars: the wheels are drawn where the body's wheel arches are, the physics keeps the values of the borrowed
// Driver 2 car (cars.c DrawCarWheels reads these when the flag of the slot is set)
SVECTOR gD1WheelVisual[MAX_CAR_RESIDENT_MODELS][4];
int gD1WheelSizeVisual[MAX_CAR_RESIDENT_MODELS];
char gD1WheelVisualOn[MAX_CAR_RESIDENT_MODELS];

// [A] Driver 1 cities: the D2 cars (civilians 1..4, specials 8..12) of the borrowed cosmetics lump, the handling donors of the
// player cars (D1PickDonor)
#define D1_DONOR_MODELS		13
static CAR_COSMETICS sD1Donors[D1_DONOR_MODELS];
static char sD1DonorValid[D1_DONOR_MODELS];

#if ENABLE_GAME_FIXES
// [A] storage for spooled models
// remember: we already have more than 1k of free memory with optimizations
CAR_COSMETICS levelSpecCosmetics[MAX_CAR_RESIDENT_MODELS];
#endif

#if USE_PC_FILESYSTEM
extern int gContentOverride;

// [A] loads car cosmetics from file
void LoadCustomCarCosmetics(CAR_COSMETICS* dest, int modelNumber)
{
	char filename[64];

	sprintf(filename, "LEVELS\\%s\\CARMODEL_%d.COS", LevelNames[GameLevel], modelNumber);
	if (!FileExists(filename))
	{
		return;
	}

	LoadfileSeg(filename, (char*)dest, 0, sizeof(CAR_COSMETICS));
}
#endif

// [D] [T]
void ProcessCosmeticsLump(char *lump_ptr, int lump_size)
{
	int model;
	int i;
	int offset;

	memset(gD1WheelVisualOn, 0, sizeof(gD1WheelVisualOn));

	for (i = 0; i < MAX_CAR_RESIDENT_MODELS; i++)
	{
		model = residentCarModels[i];

		if (model == 13)
		{
			model = 10 - (residentCarModels[0] + residentCarModels[1] + residentCarModels[2]);

			if (model < 1) 
				model = 1;
			else if (model > 4) 
				model = 4;
		}

		if (model != -1) 
		{
			offset = *(int*)(lump_ptr + model * sizeof(int));
			car_cosmetics[i] = *(CAR_COSMETICS*)((u_char*)lump_ptr + offset);

#if USE_PC_FILESYSTEM
			if(gContentOverride)
				LoadCustomCarCosmetics(&car_cosmetics[i], model);
#endif
			FixCarCos(&car_cosmetics[i], model);
		}
	}

	memset(sD1DonorValid, 0, sizeof(sD1DonorValid));

	if (IS_DRIVER1_CITY(GameLevel))
	{
		for (model = 1; model < D1_DONOR_MODELS; model++)
		{
			if (model > 4 && model < FIRST_D1_PLAYER_CAR)
				continue;

			offset = *(int*)(lump_ptr + model * sizeof(int));
			sD1Donors[model] = *(CAR_COSMETICS*)((u_char*)lump_ptr + offset);
			FixCarCos(&sD1Donors[model], model);
			sD1DonorValid[model] = 1;
		}
	}

	// [A] cache all special vehicle cosmetics
#if ENABLE_GAME_FIXES
	for (i = 0; i < 5; i++)
	{
		model = 8 + i;

		offset = *(int*)(lump_ptr + model * sizeof(int));
		levelSpecCosmetics[i] = *(CAR_COSMETICS*)((u_char*)lump_ptr + offset);
	}
#endif
}

// [D] [T]
void LoadCosmetics(int level)
{
	LoadfileSeg(CosmeticFiles[level], (char*)_other_buffer, 0, 3120);
	ProcessCosmeticsLump((char*)_other_buffer, 0);
}

// [A] Driver 1 cities borrow the cosmetics of a Driver 2 city by model number, but Driver 1 cars are about 1.5 times as big
// (next to Tanner and the Driver 2 traffic they look huge): the models are shrunk to the length of the Driver 2 car.
// Driver 2 sets colBox to the half size of the model: take it from the shrunk Driver 1 model and scale every position by
// the same ratio (the camera distance, wheels, lights and collision points follow the car).
static void ShrinkCarModel(MODEL* model, int num, int den)
{
	int v;
	SVECTOR* verts;

	if (model == NULL)
		return;

	verts = GET_MODEL_DATA(SVECTOR, model, vertices);

	for (v = 0; v < model->num_vertices; v++)
	{
		verts[v].vx = verts[v].vx * num / den;
		verts[v].vy = verts[v].vy * num / den;
		verts[v].vz = verts[v].vz * num / den;
	}

	model->bounding_sphere = model->bounding_sphere * num / den;
}

static void ScaleCosmeticVector(SVECTOR* v, const SVECTOR* ratioTo, const SVECTOR* ratioFrom)
{
	v->vx = v->vx * ratioTo->vx / ratioFrom->vx;
	v->vy = v->vy * ratioTo->vy / ratioFrom->vy;
	v->vz = v->vz * ratioTo->vz / ratioFrom->vz;
}

// half length of a typical D2 car (colBox.vz) and of the D1 player cars (measured), the base of the player car scale
#define D2_CAR_HALF_LENGTH				375
#define D1_CAR_HALF_LENGTH				576
#define D1_WHEEL_PERCENT_DEFAULT		85	// drawn wheel radius in percent of the radius that fills the arch (a full arch looked oversized)
#define D1_PLAYER_CAR_LOWER_DEFAULT		14	// units the body hangs lower than "lower edge at the wheel centre" without a table entry
#define D1_WHEEL_TRACK_PERCENT			105	// drawn wheels stand 5% further out than the 85%-of-the-width rule
#define D1_BODY_RAISE_PERCENT			5	// the body of every player car sits this many percent of its own height higher

// test options -d1lower <units> / -d1wheel <percent>: override the table to look for the right values by rendering a few settings
int gD1CarLower = -9999;
int gD1WheelPercent = 0;

// Corrections per city and D1 car model found by eye from the side, rear, front and 3/4 view (reference: test_scripts\d1cars).
// 'vertices' = vertex count of the clean model (0 = any): other D1 data gets the derived values. lower (negative = body higher) and
// wheelPercent (0 = default) are for player cars; brake/head = lamp shift in full size units, dx outward, other side mirrored.
struct D1CarFix
{
	int city, model, vertices;
	int lower, wheelPercent;
	int brakeDx, brakeDy;
	int headDx, headDy;
	int doubleHeadLamps;
	int wheelShift;		// both axles this many percent of the half length towards the front (negative = back)
	int indBackDy, indFrontDy;	// indicator lamps: shift down in full size units (the borrowed position sits on the body)
	int tailPair;		// 1..3: two tail lamps per side, the brake lamp position is the middle of the pair (gap = 14 + 4 * n each side)
};

static const D1CarFix sD1CarFixes[] = {
	// traffic cars (lamps only)
	{ 4, 2, 0, 0, 0, 0, -45, -22, 0, 1 },		// Miami civilian 2
	{ 5, 1, 0, 0, 0, 0, -39, 0, 0, 0 },			// Frisco civilian 1
	{ 6, 1, 0, 0, 0, 82, -22, 0, 0, 0 },		// LA civilian 1
	{ 6, 2, 0, 0, 0, 0, -12, 0, 0, 0 },			// LA civilian 2
	{ 7, 1, 0, 0, 0, 51, -60, 0, 0, 0 },		// New York civilian 1
	// player cars (models 8 and up): lower = body height, lamp shifts (dx outward) measured from rear and front shots
	{ 4,  8, 126, 14, 0, 22, -24, 0, 12, 0, 0, 0, 0, 2 },		// Miami
	{ 4,  9, 121, 14, 0, 0, -87, 0, -68, 0 },
	{ 4, 10, 107, 14, 0, -56, -51, 65, -23, 0 },
	{ 4, 11, 126, 14, 0, -33, -82, -33, -51, 0, 0, 0, 0, 1 },
	{ 5,  8, 102, 2, 95, 28, 36, 55, -43, 0, 4 },		// Frisco (no arch found: bigger wheels for its 120 wide arches, body raised along)
	{ 5,  9, 89, 14, 0, 0, 90, 0, 12, 0 },
	{ 5, 10, 126, 14, 0, 0, -57, 0, 12, 0 },
	{ 5, 11, 111, 14, 0, 0, -75, 0, -56, 0 },
	{ 6,  8, 123, 14, 0, 75, 12, 0, 12, 0 },		// LA
	{ 6,  9, 126, 14, 0, 0, -75, 20, -21, 0 },
	{ 6, 10, 127, 14, 0, 0, -54, 0, -48, 0 },
	{ 6, 11, 111, 14, 0, 20, -10, 0, -25, 0, 0, 0, 0, 1 },
	{ 8,  1, 0, 14, 105, 0, -35, 0, -28, 2 },			// Newcastle civilian 1 (the player car there)
	{ 8,  2, 0, 14, 105, 0, -60, 0, -18, 2 },			// Newcastle civilian 2
	{ 9,  8, 0, 14, 0, 22, 62, 50, -31, 2, 0, 25, 55, 2 },			// Training
	{ 7,  8, 126, 14, 0, 45, 27, 63, -38, 0 },		// New York
	{ 7,  9, 89, 14, 0, 0, 89, 0, 12, 0 },
	{ 7, 10, 127, 14, 0, 0, -11, 0, 12, 0 },
	{ 7, 11, 113, 14, 0, 0, -82, 0, -33, 0 },
	{ 7, 12, 120, 14, 0, 44, -58, -45, -53, 1 },
};

int gD1CarFixCity = -1;	// Car Spawner: the city a Driver 1 car comes from, -1 = the loaded city

static const D1CarFix* FindD1CarFix(int model, int vertices)
{
	int j;

	for (j = 0; j < (int)(sizeof(sD1CarFixes) / sizeof(sD1CarFixes[0])); j++)
	{
		const D1CarFix& fix = sD1CarFixes[j];

		if (fix.city == (gD1CarFixCity >= 0 ? gD1CarFixCity : GameLevel) && fix.model == model && (fix.vertices == 0 || fix.vertices == vertices))
			return &fix;
	}

	return NULL;
}

// [A] Wheel centres of a Driver 1 car from its wheel arches: the sill (lowest line of the side) is interrupted by each arch,
// and the middle of that gap is the wheel centre. An arch is a gap of 9 to 26% of the car length whose middle lies at 35 to
// 75% of the half length (the door gap is longer, the bumper gaps shorter); the one nearest to 55% wins. Returns 0 when there
// is no arch in the front or in the rear half (some bodies have none), 'gap' is the mean arch width.
static int D1FindWheelArches(MODEL* model, int maxX, int maxY, int maxZ, int* frontZ, int* rearZ, int* gap)
{
	SVECTOR* verts = GET_MODEL_DATA(SVECTOR, model, vertices);
	int sill[64];
	int count = 0;
	int frontLen = 0, rearLen = 0;
	int frontBest = 0, rearBest = 0;
	int v, i, j;

	for (v = 0; v < model->num_vertices && count < 64; v++)
	{
		if (verts[v].vx * 100 >= maxX * 70 && verts[v].vy * 100 >= maxY * 80)
			sill[count++] = verts[v].vz;
	}

	for (i = 1; i < count; i++)
	{
		int z = sill[i];

		for (j = i - 1; j >= 0 && sill[j] > z; j--)
			sill[j + 1] = sill[j];

		sill[j + 1] = z;
	}

	for (i = 0; i + 1 < count; i++)
	{
		int len = sill[i + 1] - sill[i];
		int mid = (sill[i] + sill[i + 1]) / 2;
		int off = ABS(ABS(mid) - maxZ * 55 / 100);

		if (len * 100 < maxZ * 2 * 9 || len * 100 > maxZ * 2 * 26 || ABS(mid) * 100 < maxZ * 35 || ABS(mid) * 100 > maxZ * 75)
			continue;

		if (mid < 0 && (!frontLen || off < frontBest))
		{
			frontLen = len;
			frontBest = off;
			*frontZ = mid;
		}
		else if (mid >= 0 && (!rearLen || off < rearBest))
		{
			rearLen = len;
			rearBest = off;
			*rearZ = mid;
		}
	}

	*gap = (frontLen + rearLen) / 2;

	return frontLen && rearLen;
}

// The one place that sets how big a D1 car is: D1_CAR_SIZE_PERCENT of a D2 car, returned as factor num / den for the model.
// A player car keeps its own proportions (one factor from the typical D1 and D2 half length), a traffic car takes the length
// of the D2 car it borrows from.
static void D1CarScaleFactor(int model, const CAR_COSMETICS* borrowed, int rawMaxZ, int* num, int* den)
{
	if (model >= FIRST_D1_PLAYER_CAR)
	{
		*num = D2_CAR_HALF_LENGTH * D1_CAR_SIZE_PERCENT / 100;
		*den = D1_CAR_HALF_LENGTH;
	}
	else
	{
		*num = borrowed->colBox.vz * D1_CAR_SIZE_PERCENT / 100;
		*den = MAX(rawMaxZ, 1);
	}
}

// [A] Physics values of a player car: what the borrowed Driver 2 car gives when its body is scaled to the Driver 2 length. The
// handling (inertia from colBox, wheelbase, wheel size) stays that of the Driver 2 car whatever size the car is drawn at; only
// the collision points follow the drawn size. It must not depend on the wheel and body fitting of D1ScaleCarCosmetics, so
// those only change what is drawn.
static void D1PhysicsFromBorrowed(CAR_COSMETICS* out, const CAR_COSMETICS* borrowed, int rawMaxX, int rawMinY, int rawMaxY, int rawMaxZ)
{
	SVECTOR oldBox = borrowed->colBox;
	SVECTOR newBox;
	int num = borrowed->colBox.vz;
	int den = MAX(rawMaxZ, 1);
	int maxX = rawMaxX * num / den;
	int minY = rawMinY * num / den;
	int maxY = rawMaxY * num / den;
	int j;

	newBox.vx = MAX(maxX - 6, 1);
	newBox.vy = MAX((maxY - minY) / 2, 1);
	newBox.vz = MAX(rawMaxZ * num / den, 1);

	out->colBox = newBox;

	for (j = 0; j < 4; j++)
	{
		out->wheelDisp[j] = borrowed->wheelDisp[j];
		out->wheelDisp[j].vx = borrowed->wheelDisp[j].vx * newBox.vx * 98 / (oldBox.vx * 100);
		out->wheelDisp[j].vz = borrowed->wheelDisp[j].vz * newBox.vz / oldBox.vz;
	}

	for (j = 0; j < 12; j++)
	{
		out->cPoints[j] = borrowed->cPoints[j];
		ScaleCosmeticVector(&out->cPoints[j], &newBox, &oldBox);
		out->cPoints[j].vx = out->cPoints[j].vx * D1_CAR_SIZE_PERCENT / 100;
		out->cPoints[j].vz = out->cPoints[j].vz * D1_CAR_SIZE_PERCENT / 100;
	}

	out->wheelSize = borrowed->wheelSize * newBox.vz / oldBox.vz;
	out->cog = borrowed->cog;
	out->cog.vz = borrowed->cog.vz * newBox.vz / oldBox.vz;
	out->cog.vy = -(maxY + 5 + out->wheelSize / 4);
}

static void ShiftCarModel(MODEL* model, int dy, int dz)
{
	SVECTOR* verts;
	int v;

	if (model == NULL)
		return;

	verts = GET_MODEL_DATA(SVECTOR, model, vertices);

	for (v = 0; v < model->num_vertices; v++)
	{
		verts[v].vy += dy;
		verts[v].vz += dz;
	}
}

// The D2 car (civilian 1..4 or special 8..12 of the borrowed lump) whose size is closest to the D1 car, so a D1 player car
// handles like a D2 car of its type. Boxes of a car are 250..600 long; anything else is not a car.
static int D1PickDonor(int rawMaxZ, CAR_COSMETICS* out)
{
	int target = rawMaxZ * D2_CAR_HALF_LENGTH / D1_CAR_HALF_LENGTH;
	int best = -1, bestDiff = 0, m, diff;

	for (m = 1; m < D1_DONOR_MODELS; m++)
	{
		if (!sD1DonorValid[m] || sD1Donors[m].colBox.vz < 250 || sD1Donors[m].colBox.vz > 600 ||
			sD1Donors[m].colBox.vx <= 0 || sD1Donors[m].colBox.vy <= 0)
			continue;

		diff = ABS(sD1Donors[m].colBox.vz - target);

		if (best < 0 || diff < bestDiff)
		{
			best = m;
			bestDiff = diff;
		}
	}

	if (best < 0)
		return 0;

	printInfo("D1 handling donor: D2 model %d (length %d, wanted about %d) mass %d power %d traction %d twist %d %d %d sus %d\n", best, sD1Donors[best].colBox.vz, target,
		sD1Donors[best].mass, sD1Donors[best].powerRatio, sD1Donors[best].traction, sD1Donors[best].twistRateX, sD1Donors[best].twistRateY, sD1Donors[best].twistRateZ,
		sD1Donors[best].susCoeff);
	*out = sD1Donors[best];
	return 1;
}

static void D1ScaleCarCosmeticsSlot(int i)
{
	int j, v;

	{
		MODEL* model = gCarCleanModelPtr[i];
		CAR_COSMETICS* cos = &car_cosmetics[i];
		SVECTOR* verts;
		SVECTOR oldBox, newBox;
		int minY = 0, maxY = 0, maxX = 0, maxZ = 0;
		int scaledOldCogY, scaledOldCogZ;
		int shrinkNum, shrinkDen;
		int frontZ = 0, rearZ = 0, archesFound = 0, cogZ;
		int rawMaxX = 0, rawMinY = 0, rawMaxY = 0, rawMaxZ = 0;
		CAR_COSMETICS borrowed = *cos;
		const D1CarFix* fix;
		int lower, wheelPercent;

		if (model == NULL || model->num_vertices == 0 || cos->colBox.vx <= 0 || cos->colBox.vy <= 0 || cos->colBox.vz <= 0)
			return;

		verts = GET_MODEL_DATA(SVECTOR, model, vertices);

		fix = FindD1CarFix(residentCarModels[i], model->num_vertices);
		lower = (gD1CarLower != -9999) ? gD1CarLower : (fix ? fix->lower : D1_PLAYER_CAR_LOWER_DEFAULT);
		wheelPercent = (gD1WheelPercent != 0) ? gD1WheelPercent : ((fix && fix->wheelPercent) ? fix->wheelPercent : D1_WHEEL_PERCENT_DEFAULT);

		for (v = 0; v < model->num_vertices; v++)
		{
			maxZ = MAX(maxZ, ABS(verts[v].vz));
			rawMaxX = MAX(rawMaxX, ABS(verts[v].vx));
			rawMinY = MIN(rawMinY, verts[v].vy);
			rawMaxY = MAX(rawMaxY, verts[v].vy);
		}

		rawMaxZ = maxZ;

		D1CarScaleFactor(residentCarModels[i], cos, maxZ, &shrinkNum, &shrinkDen);

		ShrinkCarModel(gCarCleanModelPtr[i], shrinkNum, shrinkDen);
		ShrinkCarModel(gCarDamModelPtr[i], shrinkNum, shrinkDen);
		ShrinkCarModel(gCarLowModelPtr[i], shrinkNum, shrinkDen);
		maxZ = 0;

		for (v = 0; v < model->num_vertices; v++)
		{
			minY = MIN(minY, verts[v].vy);
			maxY = MAX(maxY, verts[v].vy);
			maxX = MAX(maxX, ABS(verts[v].vx));
			maxZ = MAX(maxZ, ABS(verts[v].vz));
		}

		// the player drives a civilian car in Newcastle (no player car textures there): it is treated like a player car
		const bool fitWheels = residentCarModels[i] >= 8 || (GameLevel == D1_NEWCASTLE_LEVEL && residentCarModels[i] >= 1 && residentCarModels[i] <= 2);

		// the table values were found at the old body height: raise the body by a share of its own height
		if (fitWheels)
			lower -= (maxY - minY) * D1_BODY_RAISE_PERCENT / 100;

		oldBox = cos->colBox;
		newBox.vx = MAX(maxX - 6, 1);
		newBox.vy = MAX((maxY - minY) / 2, 1);
		newBox.vz = MAX(maxZ, 1);

		scaledOldCogY = cos->cog.vy * newBox.vy / oldBox.vy;
		scaledOldCogZ = cos->cog.vz * newBox.vz / oldBox.vz;

		// the light, smoke and fire positions are the first nine vectors of the structure
		for (j = 0; j < 9; j++)
			ScaleCosmeticVector(&(&cos->headLight)[j], &newBox, &oldBox);

		// the wheels keep their height: it sets how far the car stands above the ground. The track is 2% smaller so
		// the tyres do not stick out of the body.
		for (j = 0; j < 4; j++)
		{
			cos->wheelDisp[j].vx = cos->wheelDisp[j].vx * newBox.vx * 98 / (oldBox.vx * 100);
			cos->wheelDisp[j].vz = cos->wheelDisp[j].vz * newBox.vz / oldBox.vz;
		}

		for (j = 0; j < 12; j++)
			ScaleCosmeticVector(&cos->cPoints[j], &newBox, &oldBox);

		cos->wheelSize = cos->wheelSize * newBox.vz / oldBox.vz;

		// the borrowed D2 wheels do not fit the D1 body: the wheels sit in the middle of the arches and fill them (wheelDisp z is
		// the negated model z, front first). A body without arches gets wheels at 58% of the half length.
		if (fitWheels)
		{
			int gap = 0;

			archesFound = D1FindWheelArches(model, maxX, maxY, maxZ, &frontZ, &rearZ, &gap);

			if (archesFound)
				cos->wheelSize = gap * 50 / 100 * wheelPercent / 100;
			else
			{
				frontZ = -maxZ * 58 / 100;
				rearZ = maxZ * 58 / 100;
				cos->wheelSize = maxZ * 14 / 100 * wheelPercent / 100;
			}

			if (fix)
			{
				frontZ -= maxZ * fix->wheelShift / 100;
				rearZ -= maxZ * fix->wheelShift / 100;
			}

			// the body is drawn shifted by the cog, the wheels are not: wheel position = model position + cog. The cog goes to
			// the middle of the wheelbase like FixCarCos does (the two wheelDisp z values add up to 14).
			cogZ = -(frontZ + rearZ + 14) / 2;
			cos->wheelDisp[0].vz = cos->wheelDisp[2].vz = -(frontZ + cogZ);
			cos->wheelDisp[1].vz = cos->wheelDisp[3].vz = -(rearZ + cogZ);

			// the outer face of the tyre (wheel mesh is 25 wide each side, drawn 17 off the point) sits at 85% of the body width, then D1_WHEEL_TRACK_PERCENT
			cos->wheelDisp[0].vx = cos->wheelDisp[1].vx = (maxX * 85 / 100 - 8) * D1_WHEEL_TRACK_PERCENT / 100;
			cos->wheelDisp[2].vx = cos->wheelDisp[3].vx = -cos->wheelDisp[0].vx;
		}
		cos->cog.vz = fitWheels ? cogZ : scaledOldCogZ;
		cos->colBox = newBox;
		// the body hangs on the wheels: its lower edge sits at the wheel centre
		cos->cog.vy = -(maxY + 5 + cos->wheelSize / 4);

		// the D1 arches are tall: the player cars sit lower on their wheels (the wheel top nearly touches the arch)
		if (fitWheels)
			cos->cog.vy += lower;

		// the light, smoke and fire positions are relative to the cog: keep them where they were on the model
		for (j = 0; j < 9; j++)
		{
			(&cos->headLight)[j].vy += scaledOldCogY - cos->cog.vy + (fitWheels ? lower : 0);	// the lamps hang on the lowered body
			(&cos->headLight)[j].vz += scaledOldCogZ - cos->cog.vz;
		}
		// the borrowed D2 lights sit on a different body: move them onto the lamps of the D1 model (checked by eye, sD1CarFixes)
		if (fix)
		{
			// the table holds shifts for the full size D1 model
			cos->brakeLight.vx += fix->brakeDx * shrinkNum / shrinkDen;
			cos->brakeLight.vy += fix->brakeDy * shrinkNum / shrinkDen;
			cos->headLight.vx += fix->headDx * shrinkNum / shrinkDen;
			cos->headLight.vy += fix->headDy * shrinkNum / shrinkDen;

			if (fix->doubleHeadLamps != 1)
				cos->extraInfo &= ~(0x8000 | 0xC00);	// the borrowed car has twin head lamps, the D1 bodies have one lamp per side unless the table says so
			else
				cos->extraInfo |= 0x8000 | 0xC00;	// both lamps of a pair light up, offset 22 each side of the pair centre
		}

		// the D1 cars have one tail lamp per side: the double flag of the D2 car would draw a second flare beside it
		cos->extraInfo &= ~0x4000;

		// ... except for the bodies with two round lamps per side: both light up
		if (fix)
		{
			cos->backInd.vy += fix->indBackDy * shrinkNum / shrinkDen;
			cos->frontInd.vy += fix->indFrontDy * shrinkNum / shrinkDen;
		}

		if (fix && fix->tailPair)
			cos->extraInfo = (cos->extraInfo & ~(0x300 | 0x1000)) | 0x4000 | (fix->tailPair << 8);

		// reverse lamps are the white lens next to the red tail lamp, towards the middle of the car
		cos->revLight = cos->brakeLight;
		cos->revLight.vx -= maxX * 14 / 100;

		if (fitWheels)
		{
			CAR_COSMETICS physics;
			SVECTOR shift;

			printInfo("D1 car %d (%d vertices) wheels: z %d / %d, radius %d (D2 car %d), x %d, arches %s (half length %d, width %d), lamp x %d / %d\n", residentCarModels[i], model->num_vertices, cos->wheelDisp[0].vz, cos->wheelDisp[1].vz,
				cos->wheelSize, borrowed.wheelSize, cos->wheelDisp[0].vx, archesFound ? "found" : "none", maxZ, maxX, cos->brakeLight.vx, cos->headLight.vx);

			// what is drawn: wheels at the arches (kept apart), the body and the lamps as computed above
			for (j = 0; j < 4; j++)
				gD1WheelVisual[i][j] = cos->wheelDisp[j];

			gD1WheelSizeVisual[i] = cos->wheelSize;
			gD1WheelVisualOn[i] = 1;

			// what drives: the borrowed car (handling, collision and camera as before). The body keeps its place on screen:
			// the models and lamps move by the difference of the two cogs.
			// handling of a D2 car of about the same size (the same model number says nothing: some do not even describe a car)
			if (D1PickDonor(rawMaxZ, &borrowed))
			{
				// Driver 1 has one set of values for all cars (no per car table in the EXE): neutral power, traction and mass
				cos->mass = D1_CAR_MASS;
				cos->powerRatio = D1_CAR_POWER_RATIO;
				cos->traction = D1_CAR_TRACTION;
				cos->twistRateX = D1_CAR_TWIST_Y;
				cos->twistRateY = D1_CAR_TWIST_Y;
				cos->twistRateZ = D1_CAR_TWIST_Z;
				cos->susCoeff = borrowed.susCoeff;
			}

			D1PhysicsFromBorrowed(&physics, &borrowed, rawMaxX, rawMinY, rawMaxY, rawMaxZ);

			shift.vx = cos->cog.vx - physics.cog.vx;
			shift.vy = cos->cog.vy - physics.cog.vy;
			shift.vz = cos->cog.vz - physics.cog.vz;

			for (j = 0; j < 9; j++)
			{
				(&cos->headLight)[j].vx += shift.vx;
				(&cos->headLight)[j].vy += shift.vy;
				(&cos->headLight)[j].vz += shift.vz;
			}

			ShiftCarModel(gCarCleanModelPtr[i], shift.vy, shift.vz);
			ShiftCarModel(gCarDamModelPtr[i], shift.vy, shift.vz);
			ShiftCarModel(gCarLowModelPtr[i], shift.vy, shift.vz);

			for (j = 0; j < 4; j++)
				cos->wheelDisp[j] = physics.wheelDisp[j];

			for (j = 0; j < 12; j++)
				cos->cPoints[j] = physics.cPoints[j];

			cos->wheelSize = physics.wheelSize;
			cos->colBox = physics.colBox;
			printInfo("D1 car %d physics: box %d %d %d (D2 car %d %d %d), drawn half length %d\n", residentCarModels[i], cos->colBox.vx, cos->colBox.vy, cos->colBox.vz,
				borrowed.colBox.vx, borrowed.colBox.vy, borrowed.colBox.vz, maxZ);
			cos->cog = physics.cog;
		}
	}
}

void D1ScaleCarCosmetics(void)
{
	int i;

	for (i = 0; i < MAX_CAR_RESIDENT_MODELS; i++)
		D1ScaleCarCosmeticsSlot(i);
}

// [A] Car Spawner: the special slot got another model (models.c LoadCarModelIntoSpecialSlot). Its cosmetics start from the
// borrowed ones again and are scaled like at the level load; the models are fresh copies, so they are not scaled twice.
int D1PrepareSpecialSlotCosmetics(int model)
{
	if (model < 1 || model >= D1_DONOR_MODELS || !sD1DonorValid[model])
		return 0;

	car_cosmetics[SPECIAL_CAR_SLOT] = sD1Donors[model];
	gD1WheelVisualOn[SPECIAL_CAR_SLOT] = 0;
	D1ScaleCarCosmeticsSlot(SPECIAL_CAR_SLOT);
	return 1;
}

#define REVERSELIGHT_SIZE		14
#define INDICATORLIGHT_SIZE		20
#define BRAKELIGHT_SIZE			17
#define HEADLIGHT_SIZE			18
#define BACKLIGHT_SIZE			17

// [D] [T]
void AddReverseLight(CAR_DATA *cp)
{
	CAR_COSMETICS *car_cos;
	SVECTOR v1;
	CVECTOR col;

	car_cos = cp->ap.carCos;

	col.r = 100;
	col.g = 90;
	col.b = 90;

	v1 = car_cos->revLight;

	v1.vx += car_cos->cog.vx;
	v1.vy += car_cos->cog.vy;
	v1.vz += car_cos->cog.vz;

	if (cp->ap.damage[4] < 500)
	{
		ShowCarlight(&v1, cp, &col, REVERSELIGHT_SIZE, REVERSELIGHT_SIZE * 3, &light_texture, 0);
	}

	if (cp->ap.damage[3] < 500) 
	{
		v1.vx = car_cos->cog.vx * 2 - v1.vx;
		ShowCarlight(&v1, cp, &col, REVERSELIGHT_SIZE, REVERSELIGHT_SIZE * 3, &light_texture, 0);
	}
}

// [D] [T]
void SetupSpecCosmetics(char *loadbuffer)
{
	int model;
	model = residentCarModels[SPECIAL_CAR_SLOT];

#if ENABLE_GAME_FIXES
	// [A] always use cached cosmetics
	car_cosmetics[SPECIAL_CAR_SLOT] = levelSpecCosmetics[model - 8];

	// [A] the cache holds the specials of this city, a car of another city brings its own wheels and lights
	if (gForeignSpool)
		car_cosmetics[SPECIAL_CAR_SLOT] = *(CAR_COSMETICS*)loadbuffer;
#else
	car_cosmetics[SPECIAL_CAR_SLOT] = *(CAR_COSMETICS*)loadbuffer;
#endif

#if USE_PC_FILESYSTEM
	if (gContentOverride)
		LoadCustomCarCosmetics(&car_cosmetics[SPECIAL_CAR_SLOT], model);
#endif

	// [A] don't forget
	FixCarCos(&car_cosmetics[SPECIAL_CAR_SLOT], model);
}

// [D] [T]
void AddIndicatorLight(CAR_DATA *cp, int Type)
{
	u_int brightness;
	char *life;
	CAR_COSMETICS *car_cos;
	char *life2;
	CVECTOR col;
	SVECTOR vfrnt;
	SVECTOR vback;

	//if (cp->controlType != CONTROL_TYPE_CIV_AI)		// [A] weird way to disable it here
	//	return;

	life = &cp->ap.life;
	life2 = &cp->ap.life2;

	if (cp->ap.life < 0)
		brightness = (255 - (u_int)cp->ap.life) * 2 & 255;
	else
		brightness = cp->ap.life << 1 & 255;

	col.r = brightness;
	col.g = 0;
	col.b = 0;

	if (pauseflag == 0)
		*life += 8;

	car_cos = cp->ap.carCos;

	vback = car_cos->backInd;

	vback.vx += car_cos->cog.vx;
	vback.vy += car_cos->cog.vy;
	vback.vz += car_cos->cog.vz;

	vfrnt = car_cos->frontInd;

	vfrnt.vx += car_cos->cog.vx;
	vfrnt.vy += car_cos->cog.vy;
	vfrnt.vz += car_cos->cog.vz;

	if (Type == 0 || Type == 2)
	{
		if (cp->ap.damage[4] < 500)
		{
			*life2 += brightness >> 3;
			ShowCarlight(&vback, cp, &col, INDICATORLIGHT_SIZE, INDICATORLIGHT_SIZE * 4, &light_texture, 0);
		}

		if (cp->ap.damage[0] < 500) 
		{
			ShowCarlight(&vfrnt, cp, &col, INDICATORLIGHT_SIZE, INDICATORLIGHT_SIZE * 4, &light_texture, 1);
		}
	}

	if (Type - 1U < 2)
	{
		if (cp->ap.damage[3] < 500) 
		{
			vback.vx = car_cos->cog.vx * 2 - vback.vx;
			*life2 += brightness >> 3;

			ShowCarlight(&vback, cp, &col, INDICATORLIGHT_SIZE, INDICATORLIGHT_SIZE * 4, &light_texture, 0);
		}

		if (cp->ap.damage[1] < 500) 
		{
			vfrnt.vx = car_cos->cog.vx * 2 - vfrnt.vx;
			ShowCarlight(&vfrnt, cp, &col, INDICATORLIGHT_SIZE, INDICATORLIGHT_SIZE * 4, &light_texture, 1);
		}
	}
}

// [D] [T]
void AddBrakeLight(CAR_DATA *cp)
{
	short damageFac;
	int damIndex;
	short cogOffset;
	CAR_COSMETICS *car_cos;
	int offset;
	char *life2;
	int loop;
	SVECTOR v1;
	SVECTOR v2;
	SVECTOR vec;
	CVECTOR col;
	short doubleFlag;
	short verticalFlag;

	car_cos = cp->ap.carCos;
	col.r = 200;
	col.g = 0;
	col.b = 0;

	life2 = &cp->ap.life2;

	if (!(car_cos->extraInfo & 8))
		return;

	vec = car_cos->brakeLight;

	vec.vx += car_cos->cog.vx;
	vec.vy += car_cos->cog.vy;
	vec.vz += car_cos->cog.vz;

	offset = ((car_cos->extraInfo & 0x300) >> 6) + 10;
	cogOffset = vec.vz + car_cos->cog.vz;

	loop = 0;

	doubleFlag = (car_cos->extraInfo & 0x4000) != 0;
	verticalFlag = (car_cos->extraInfo & 0x1000) != 0;

	do {
		damIndex = (4 - loop);

		if (doubleFlag)
		{
			if (verticalFlag)
			{
				v1 = vec;
				v2 = vec;

				damageFac = cp->ap.damage[damIndex] >> 6;

				v1.vz = cogOffset + damageFac;
				v2.vz = cogOffset + damageFac;

				v1.vx = vec.vx;
				v1.vy = offset + vec.vy;
				v2.vy = vec.vy - offset;

				if (cp->ap.damage[damIndex] < 500)
				{
					ShowCarlight(&v1, cp, &col, BRAKELIGHT_SIZE, BRAKELIGHT_SIZE * 4, &light_texture, 0);
					ShowCarlight(&v2, cp, &col, BRAKELIGHT_SIZE, BRAKELIGHT_SIZE * 4, &light_texture, 0);
					*life2 += 8;
				}
			}
			else
			{
				v1 = vec;
				v2 = vec;

				damageFac = cp->ap.damage[damIndex] >> 6;
				v1.vz = cogOffset + damageFac;
				v2.vz = cogOffset + damageFac;

				v1.vx = offset + vec.vx;
				v2.vx = vec.vx - offset;

				if (cp->ap.damage[damIndex] < 500)
				{
					ShowCarlight(&v1, cp, &col, BRAKELIGHT_SIZE, BRAKELIGHT_SIZE * 4, &light_texture, 0);
					ShowCarlight(&v2, cp, &col, BRAKELIGHT_SIZE, BRAKELIGHT_SIZE * 4, &light_texture, 0);
					*life2 += 8;
				}
			}
		}
		else
		{
			v1 = vec;

			if (cp->ap.damage[damIndex] < 500)
			{
				ShowCarlight(&v1, cp, &col, BRAKELIGHT_SIZE, BRAKELIGHT_SIZE * 4, &light_texture, 0);
				*life2 += 8;
			}
		}

		offset = -offset;
				
		vec.vx = car_cos->cog.vx * 2 - vec.vx;

		loop++;
	} while (loop < 2);
}

#ifdef DYNAMIC_LIGHTING
#define SIREN_LIGHT_RADIUS	5000	// lights cars ~2400 away and the road ~3300 (shader), halo ~600

// [A] the rotating lights light the air, cars, road and walls, drawn or not: one flashing point light per bar, the
// bars in turn
static void AddCopCarDlights(CAR_DATA* cp)
{
	CAR_COSMETICS* car_cos = cp->ap.carCos;
	SVECTOR lamp;
	SVECTOR siren = { 0, 0, 0, DLIGHT_SIREN };
	VECTOR pos;
	CVECTOR col;
	int side, sides, pulse;

	if (!gVolumeLights || !car_cos || FastForward != 0 || cp->hd.where.m[1][1] < 100)
		return;

	// as AddCopCarLight: one blue bar in Havana and Rio, two red bars in Chicago, red and blue in Las Vegas
	sides = (BASE_CITY == 1 || BASE_CITY == 3) ? 1 : 2;

	for (side = 0; side < sides; side++)
	{
		pulse = (ONE + rcos(CameraCnt * 256 + CAR_INDEX(cp) * 700 + side * 2048)) >> 1;

		if (BASE_CITY == 1 || BASE_CITY == 3 || BASE_CITY == 2 && side == 1)
		{
			col.r = 3 * pulse >> 12;
			col.g = 8 * pulse >> 12;
			col.b = 32 * pulse >> 12;
		}
		else
		{
			col.r = 32 * pulse >> 12;
			col.g = 3 * pulse >> 12;
			col.b = 2 * pulse >> 12;
		}

		lamp.vx = car_cos->cog.vx;

		if (sides == 2)
			lamp.vx += side ? -(car_cos->policeLight.vx + 24) : car_cos->policeLight.vx + 24;

		lamp.vy = car_cos->policeLight.vy + car_cos->cog.vy;
		lamp.vz = car_cos->policeLight.vz + car_cos->cog.vz;

		pos.vx = cp->hd.where.t[0] - camera_position.vx;
		pos.vy = -camera_position.vy - cp->hd.where.t[1];
		pos.vz = cp->hd.where.t[2] - camera_position.vz;

		InitFXPos(&pos, &lamp, cp);
		Apply_Inv_CameraMatrix(&pos);

		AddDlight(&pos, &col, SIREN_LIGHT_RADIUS, &siren);
	}
}
#endif

// [D] [T]
void AddCopCarLight(CAR_DATA *cp)
{
	static char xpos1[8] = {
		48, 32, 16,  0,
		0, 16, 32, 48
	};

	int light;
	int size;
	short sign;
	char count_speed;
	int num_lights;
	CAR_COSMETICS *car_cos;
	char *coplife;
	u_int pos;
	int side;
	SVECTOR v1;
	CVECTOR col;

	coplife = &cp->ap.coplife;

#ifdef DYNAMIC_LIGHTING
	AddCopCarDlights(cp);
#endif

	if (CameraCar == CAR_INDEX(cp) && cameraview == 2)
		return;

	if (FastForward != 0) 
		return;

	// don't display on flipped cars
	if (cp->hd.where.m[1][1] < 100)
		return;

	car_cos = cp->ap.carCos;

	// configure
	if (BASE_CITY == 1 || BASE_CITY == 3)
	{
		side = 1;
		num_lights = 2;
		pos = 3;
		count_speed = 48;
	}
	else if (BASE_CITY == 0)
	{
		side = 2;
		num_lights = 2;
		pos = 3;
		count_speed = 16;
	}
	else 
	{
		side = 2;
		num_lights = 3;
		count_speed = 16;
		pos = main_cop_light_pos + CAR_INDEX(cp) & 7;
	}

	sign = 1;

	do {
		side--;

		if (side == -1)
			return;

		light = num_lights;

		while (light-- >= 0) 
		{
			v1.vx = car_cos->cog.vx;

			if (BASE_CITY != 1 && BASE_CITY != 3) 
				v1.vx += (xpos1[pos] + car_cos->policeLight.vx) * sign;

			col.g = 90;
			v1.vy = car_cos->policeLight.vy + car_cos->cog.vy;

			if (gNight != 0)
				col.g = 50;

			if (pauseflag == 0 && (CameraCnt & 1U) != 0 && BASE_CITY == 2) 
				pos++;

			v1.vz = car_cos->policeLight.vz + car_cos->cog.vz;

			size = *coplife >> 1;

			// switch colours if needed
			if (BASE_CITY == 1 || BASE_CITY == 3 || BASE_CITY == 2 && side == 0)
			{
				col.b = 255;
				col.r = col.g;
			}
			else 
			{
				col.r = 255;
				col.b = col.g;
			}

			if (pauseflag == 0) 
				cp->ap.coplife += count_speed;

			ShowCarlight(&v1, cp, &col, size, size*3, &light_texture, 0xff);

			if (pauseflag == 0 && (CameraCnt & 1U) != 0 && BASE_CITY == 2) 
				pos++;

			pos = pos & 7;
		}

		sign = -sign;

	} while (true);
}

int gPlayerCarLights = 0;
int gcar_num = 0;

// [D] [T]
void AddNightLights(CAR_DATA *cp)
{
	short offset;
	int lightFlag;
	char lights;
	int damIndex;
	CAR_COSMETICS *car_cos;
	SVECTOR Position1;
	SVECTOR Position2;
	SVECTOR vec;
	CVECTOR col;
	CVECTOR col2;
	int lit;
	char *life2;
	short loop;
	short doubleFlag;
	short verticalFlag;
	short cogOffset;

	life2 = &cp->ap.life2;

	LeftLight = 0;
	RightLight = 0;

	car_cos = cp->ap.carCos;

	cogOffset = car_cos->cog.vx;
	gcar_num = CAR_INDEX(cp);

	vec.vx = car_cos->headLight.vx + car_cos->cog.vx;
	vec.vy = car_cos->headLight.vy + car_cos->cog.vy;
	vec.vz = car_cos->headLight.vz + car_cos->cog.vz;

	offset = ((car_cos->extraInfo & 0xC00) >> 8) + 10;
	doubleFlag = (car_cos->extraInfo & 0x8000);
	verticalFlag = (car_cos->extraInfo & 0x2000);

	// draw front lights
	lit = 0;
	loop = 0;
	do {
		lights = 0;
		lightFlag = 8 << (loop & 0x1f);

		col.r = 128;
		col.g = 124;
		col.b = 112;
		col2 = col;

		if (cp->ap.damage[loop] < 1000)
		{
			if (doubleFlag)
			{
				if (verticalFlag)
				{
					Position1.vy = offset + vec.vy;
					Position2.vy = vec.vy - offset;

					Position1.vx = vec.vx;
					Position2.vx = vec.vx;
				}
				else
				{
					Position1.vx = offset + vec.vx;
					Position2.vx = vec.vx - offset;

					Position1.vy = vec.vy;
					Position2.vy = vec.vy;
				}

				Position1.vz = vec.vz + (cp->ap.damage[loop] >> 6);
				Position2.vz = vec.vz + (cp->ap.damage[loop] >> 6);

				ShowCarlight(&Position1, cp, &col, HEADLIGHT_SIZE, HEADLIGHT_SIZE*4, &light_texture, lightFlag & 0xff | 1);

				lights = 1;
				lit++;

				if (cp->ap.damage[loop] < 500)
				{
					lights++;

					ShowCarlight(&Position2, cp, &col2, HEADLIGHT_SIZE, HEADLIGHT_SIZE * 4, &light_texture, 1);
					lit++;
				}
			}
			else
			{
				Position1.vx = vec.vx;
				Position1.vy = vec.vy;

				Position1.vz = vec.vz + (cp->ap.damage[loop] >> 6);
				Position2.vz = vec.vz + (cp->ap.damage[loop] >> 6);

				ShowCarlight(&Position1, cp, &col, HEADLIGHT_SIZE, HEADLIGHT_SIZE * 4, &light_texture, lightFlag & 0xff | 1);

				lights = 1;
				lit++;

				if (cp->ap.damage[loop] < 500)
					lights++;
			}
		}

		if (loop == 0)
			LeftLight = lights;
		else
			RightLight = lights;

		vec.vx = cogOffset * 2 - vec.vx;
		offset = -offset;

		loop++;
	} while (loop < 2);


	// check if back lights are supported
	// and draw
	if (car_cos->extraInfo & 8)
	{
		offset = ((car_cos->extraInfo & 0x300) >> 6) + 10;
		doubleFlag = car_cos->extraInfo & 0x4000;
		verticalFlag = car_cos->extraInfo & 0x1000;

		vec.vx = car_cos->brakeLight.vx + car_cos->cog.vx;
		vec.vy = car_cos->brakeLight.vy + car_cos->cog.vy;
		vec.vz = car_cos->brakeLight.vz + car_cos->cog.vz;

		loop = 0;

		do {
			lightFlag = 2 << (loop & 0x1f);
			damIndex = (4 - loop);

			col.r = 110;
			col.b = 0;
			col.g = 0;

			col2 = col;

			if (cp->ap.damage[damIndex] < 500)
			{
				if (doubleFlag)
				{
					// double flag
					if (verticalFlag)
					{
						Position1.vy = offset + vec.vy;
						Position2.vy = vec.vy - offset;

						Position1.vx = vec.vx;
						Position2.vx = vec.vx;
					}
					else
					{
						Position1.vx = offset + vec.vx;
						Position2.vx = vec.vx - offset;

						Position1.vy = vec.vy;
						Position2.vy = vec.vy;
					}

					Position2.vz = vec.vz - (cp->ap.damage[damIndex] >> 6);
					Position1.vz = vec.vz - (cp->ap.damage[damIndex] >> 6);

					ShowCarlight(&Position1, cp, &col, BACKLIGHT_SIZE, BACKLIGHT_SIZE * 3, &light_texture, lightFlag & 0xff);
					ShowCarlight(&Position2, cp, &col2, BACKLIGHT_SIZE, BACKLIGHT_SIZE * 3, &light_texture, 0);
				}
				else
				{
					Position1 = vec;

					ShowCarlight(&Position1, cp, &col, BACKLIGHT_SIZE, BACKLIGHT_SIZE * 3, &light_texture, lightFlag & 0xff);
				}

				*life2 += 16;
			}

			vec.vx = cogOffset * 2 - vec.vx;
			offset = -offset;

			loop++;
		} while (loop < 2);
	}

	// front lights pool
	if (lit)
	{
		col.r = 128;
		col.g = 120;
		col.b = 110;

		PlacePoolForCar(cp, &col, 1, 0);
	}

	// back lights pool
	col.r = *life2;
	if (col.r)
	{
		col.b = 0;
		col.g = 0;

		PlacePoolForCar(cp, &col, 0, 0);
		*life2 = 0;
	}
}

#ifdef DYNAMIC_LIGHTING
// [A] a car that is not drawn (beside or behind the camera, out of the visible cells) keeps its headlight and siren
// light, so the light of an overtaken car on the player's car fades with the distance instead of going out
void AddHiddenCarLights(CAR_DATA* cp)
{
	CAR_COSMETICS* car_cos = cp->ap.carCos;
	SVECTOR lamp;
	CVECTOR col = { 128, 124, 112 };
	int side;

	if (!car_cos)
		return;

	// headlight reach in the renderer is 7000
	if (ABS(cp->hd.where.t[0] - camera_position.vx) > 8000 || ABS(cp->hd.where.t[2] - camera_position.vz) > 8000)
		return;

	if (CarSirenOn(cp))
		AddCopCarDlights(cp);

	if (cp->controlType == CONTROL_TYPE_PLAYER || !CarShowsNightLights(cp))
		return;

	CarLightFadeFront = -1;		// ShowCarlight then adds only the light, no sprite

	for (side = 0; side < 2; side++)
	{
		if (cp->ap.damage[side] >= 1000)
			continue;

		lamp.vx = side ? car_cos->cog.vx - car_cos->headLight.vx : car_cos->headLight.vx + car_cos->cog.vx;
		lamp.vy = car_cos->headLight.vy + car_cos->cog.vy;
		lamp.vz = car_cos->headLight.vz + car_cos->cog.vz + (cp->ap.damage[side] >> 6);

		ShowCarlight(&lamp, cp, &col, HEADLIGHT_SIZE, HEADLIGHT_SIZE * 4, &light_texture, (8 << side) | 1);	// as AddNightLights
	}
}
#endif

// [D] [T]
void AddSmokingEngine(CAR_DATA *cp, int black_smoke, int WheelSpeed)
{
	CAR_COSMETICS *car_cos;
	VECTOR SmokePos;
	VECTOR Drift;
	SVECTOR svec;

	if((CameraCnt & 3U) == (CAR_INDEX(cp) & 3U) && pauseflag == 0)
	{
		car_cos = cp->ap.carCos;

		svec.vx = (rand() & 0x7f) - 0x40; //car_cos->smoke.vx + car_cos->cog.vx;
		svec.vy = car_cos->smoke.vy + car_cos->cog.vy;
		svec.vz = car_cos->smoke.vz + car_cos->cog.vz;

		SmokePos.vx = cp->hd.where.t[0];
		SmokePos.vy = -cp->hd.where.t[1];
		SmokePos.vz = cp->hd.where.t[2];
		
		if (black_smoke != 0)
			SmokePos.vy -= 50;
	
		gte_SetRotMatrix(cp->hd.drawCarMat.m);

		InitFXPos(&SmokePos, &svec, cp);
		GetSmokeDrift(&Drift);

		if (black_smoke)
			Setup_Smoke(&SmokePos, 100, 500, SMOKE_BLACK, WheelSpeed, &Drift, 0);
		else
			Setup_Smoke(&SmokePos, 100, 400, SMOKE_WHITE, WheelSpeed, &Drift, 0);
	}
}

// [A] custom function for bringing back exhaust
void AddExhaustSmoke(CAR_DATA *cp, int black_smoke, int WheelSpeed)
{
	CAR_COSMETICS *car_cos;
	VECTOR SmokePos;
	VECTOR Drift;
	SVECTOR svec;
	SVECTOR smokedir;

	D_CHECK_ERROR(cp < car_data, "Invalid car");

	if (cp->controlType == CONTROL_TYPE_CIV_AI && cp->ai.c.thrustState == CIV_AI_THRUST_STOP && (cp->ai.c.ctrlState == CIV_AI_CTRL_PARKED || cp->ai.c.ctrlState == CIV_AI_CTRL_EMPTY))
		return;

	if (WheelSpeed > 512 * 64)
		return;

	if ((CameraCnt & 3U) == (CAR_INDEX(cp) & 3U) && pauseflag == 0)
	{
		car_cos = cp->ap.carCos;

		svec.vx = car_cos->exhaust.vx + car_cos->cog.vx;
		svec.vy = car_cos->exhaust.vy + car_cos->cog.vy;
		svec.vz = car_cos->exhaust.vz + car_cos->cog.vz;

		SmokePos.vx = cp->hd.where.t[0];
		SmokePos.vy = -cp->hd.where.t[1];
		SmokePos.vz = cp->hd.where.t[2];

		gte_SetRotMatrix(cp->hd.drawCarMat.m);

		InitFXPos(&SmokePos, &svec, cp);
		GetSmokeDrift(&Drift);

		gte_ldv0(&svec);
		gte_rtir();
		gte_stsv(&smokedir);

		Drift.vx /= 2;
		Drift.vz /= 2;

		Drift.vx -= FIXEDH(smokedir.vx) / 2;
		Drift.vz -= FIXEDH(smokedir.vz) / 2;

		if (black_smoke)
			Setup_Smoke(&SmokePos, 10, 40, SMOKE_BLACK, WheelSpeed, &Drift, 1);
		else
			Setup_Smoke(&SmokePos, 10, 40, SMOKE_WHITE, WheelSpeed, &Drift, 1);
	}
}

// [D] [T]
void AddFlamingEngine(CAR_DATA *cp)
{
	CAR_COSMETICS *car_cos;
	VECTOR SmokePos;
	SVECTOR svec;
	VECTOR Drift;

	if ((CameraCnt & 1U) == (CAR_INDEX(cp) & 1U) && pauseflag == 0)
	{
		car_cos = cp->ap.carCos;

		svec.vx = (rand() & 0x3f); //car_cos->smoke.vx + car_cos->cog.vx;
		svec.vy = car_cos->fire.vy + car_cos->cog.vy;
		svec.vz = car_cos->fire.vz + car_cos->cog.vz + (rand() & 0x3f);

		SmokePos.vx = (cp->hd).where.t[0];

		SmokePos.vz = (cp->hd).where.t[2];
		SmokePos.vy = -(cp->hd).where.t[1];

		SetRotMatrix(&(cp->hd).drawCarMat);
		InitFXPos(&SmokePos, &svec, cp);
		Drift.vx = 0;
		Drift.vy = 0;
		Drift.vz = 0;

		Setup_Smoke(&SmokePos, 50, 100, SMOKE_FIRE, 0, &Drift, 0);
	}
}





