#include "driver2.h"
#include "denting.h"
#include "cars.h"
#include "texture.h"
#include "overmap.h"
#include "draw.h"
#include "mission.h"
#include "system.h"
#include "debris.h"
#include "main.h"
#include "camera.h"
#include "handling.h"
#include "cosmetic.h"
#include "shadow.h"
#include "sky.h"
#include "daycycle.h"
#include "civ_ai.h"
#include "mc_snd.h"
#include "gamesnd.h"
#include "players.h"
#include "cutscene.h"
#include "convert.h"
#include "glaunch.h"
#include "ASM/rndrasm.h"

struct plotCarGlobals
{
	u_char* primptr;
	OTTYPE* ot;
	u_int intensity;
	u_short* pciv_clut;
	u_char* damageLevel;
};


#ifndef PSX
// [A] Driver 1 cities and the extended draw distance show the detailed car model twice as far
#define CAR_LOD_SWITCH_DISTANCE (switch_detail_distance * ((IS_DRIVER1_CITY(GameLevel) || gDrawDistanceMode) ? 2 : 1))
#else
#define CAR_LOD_SWITCH_DISTANCE 5500
#endif

MATRIX light_matrix =
{ 
	{ 
		{ 4096, 0, 0 }, 
		{ 0, 0, 0 }, 
		{ 0, 0, 0 }
	}, 
	{ 0, 0, 0 } 
};

MATRIX colour_matrix =
{ 
	{ 
		{ 4032, 0, 0 }, 
		{ 3936, 0, 0 }, 
		{ 3520, 0, 0 }
	}, 
	{ 0, 0, 0 } 
};

// PHYSICS
CAR_DATA car_data[MAX_CARS + 2];	// all cars + Tanner cbox + Camera cbox

HUBCAP gHubcap;

MODEL* gHubcapModelPtr;
MODEL* gCleanWheelModelPtr;
MODEL* gFastWheelModelPtr;
MODEL* gDamWheelModelPtr;

// active cars
CAR_DATA* active_car_list[MAX_CARS];

u_char lightsOnDelay[MAX_CARS];
short FrontWheelRotation[MAX_CARS]; // offset 0x0
short BackWheelRotation[MAX_CARS]; // offset 0x30

SVECTOR gTempCarVertDump[MAX_CARS][MAX_DENTING_VERTS];

DENTUVS *gTempCarUVPtr;
DENTUVS gTempHDCarUVDump[MAX_CARS][MAX_DENTING_UVS];
DENTUVS gTempLDCarUVDump[MAX_CARS][MAX_DENTING_LOD_UVS];

CAR_MODEL NewCarModel[MAX_CAR_RESIDENT_MODELS];
CAR_MODEL NewLowCarModel[MAX_CAR_RESIDENT_MODELS];

MODEL* gCarLowModelPtr[MAX_CAR_RESIDENT_MODELS];
MODEL* gCarDamModelPtr[MAX_CAR_RESIDENT_MODELS];
MODEL* gCarCleanModelPtr[MAX_CAR_RESIDENT_MODELS];

// pedestrian palette at 0 and next are cars
// model_id, texture_number, palette
u_short civ_clut[8][32][6];

#define MAX_CAR_POLYS	(200 * 2) * MAX_CAR_RESIDENT_MODELS

int whichCP = 0;
int baseSpecCP = 0;
CAR_POLY carPolyBuffer[MAX_CAR_POLYS + 1];

char LeftLight = 0;
char RightLight = 0;
char TransparentObject = 0;

// [D] [T]
void plotCarPolyB3(int numTris, CAR_POLY *src, SVECTOR *vlist, plotCarGlobals *pg)
{
	int Z;
	int indices;
	u_int FT3rgb;
	SVECTOR *v2;
	SVECTOR *v1;
	SVECTOR *v0;
	POLY_F3 *prim;
	OTTYPE *ot;

	prim = (POLY_F3 *)pg->primptr;
	FT3rgb = pg->intensity;
	ot = pg->ot;

	while (numTris > 0)
	{
		indices = src->vindices;
		v0 = vlist + (indices & 0xff);
		v1 = vlist + (indices >> 8 & 0xff);
		v2 = vlist + (indices >> 16 & 0xff);

		gte_ldv3(v0, v1, v2);

		gte_rtpt();

		gte_nclip();
		gte_stopz(&Z);
		gte_avsz3();

		if (Z > -1) 
		{
			*(u_int*)&prim->r0 = FT3rgb | 0x20000000;

			gte_stsxy3(&prim->x0, &prim->x1, &prim->x2);

			gte_stotz(&Z);

			setPolyF3(prim);
			addPrim(ot + (Z >> 1), prim);

			prim++;
		}

		numTris--;
		src++;
	}

	pg->primptr = (unsigned char*)prim;
}

// [D] [T]
void plotCarPolyFT3(int numTris, CAR_POLY *src, SVECTOR *vlist, plotCarGlobals *pg)
{
	int indices;
	int ofse;
	SVECTOR *v2;
	SVECTOR *v1;
	SVECTOR *v0;
	int Z;
	POLY_FT3 *prim;
	OTTYPE *ot;
	int FT3rgb;
	int reg;

	prim = (POLY_FT3 *)pg->primptr;
	FT3rgb = pg->intensity | 0x24000000;
	ot = pg->ot;

	gte_ldrgb(&FT3rgb);

	while (numTris > 0)
	{
		indices = src->vindices;
		v0 = vlist + (indices & 0xff);
		v1 = vlist + (indices >> 8 & 0xff);
		v2 = vlist + (indices >> 16 & 0xff);

		gte_ldv3(v0, v1, v2);

		gte_rtpt();

		gte_nclip();
		gte_stopz(&Z);
		gte_avsz3();

		if (Z > -1) 
		{
			ofse = pg->damageLevel[src->originalindex];
			*(u_int*)&prim->r0 = FT3rgb;
			*(u_int*)&prim->u0 = src->clut_uv0 + ofse;
			*(u_int*)&prim->u1 = src->tpage_uv1 + ofse;
			*(u_int*)&prim->u2 = src->uv3_uv2 + ofse;

			gte_stsxy3(&prim->x0, &prim->x1, &prim->x2);

			gte_stotz(&Z);

			setPolyFT3(prim);
			addPrim(ot + (Z >> 1), prim);

			prim++;
		}
		numTris--;
		src++;
	}

	pg->primptr = (unsigned char*)prim;
}

// [D] [T]
void plotCarPolyGT3(int numTris, CAR_POLY *src, SVECTOR *vlist, SVECTOR *nlist, plotCarGlobals *pg, int palette)
{
	int Z;
	int otz;	
	SVECTOR* v2;
	SVECTOR *v1;
	SVECTOR *v0;
	u_int indices;
	POLY_GT3 *prim;
	u_int r0,r1,r2;
	int ofse;

	prim = (POLY_GT3 *)pg->primptr;

	int GT3rgb = pg->intensity | 0x34000000;
	gte_ldrgb(&GT3rgb);

	while (numTris > 0)
	{
		indices = src->vindices;

		v0 = vlist + (indices & 0xff);
		v1 = vlist + (indices >> 8 & 0xff);
		v2 = vlist + (indices >> 16 & 0xff);

		gte_ldv3(v0, v1, v2);

		gte_rtpt();
		gte_nclip();

		gte_stopz(&Z);

		gte_avsz3();
		
		gte_stotz(&otz);

		if (Z > -1 && otz > 0)
		{
			indices = src->nindices;

			r0 = (u_int)(ushort)nlist[indices & 0xff].pad;
			r1 = (u_int)(ushort)nlist[indices >> 8 & 0xff].pad;
			r2 = (u_int)(ushort)nlist[indices >> 16 & 0xff].pad;

			*(u_int*)&prim->r0 = (r0 & 0xff) << 0x10 | r0;
			*(u_int*)&prim->r1 = (r1 & 0xff) << 0x10 | r1;
			*(u_int*)&prim->r2 = (r2 & 0xff) << 0x10 | r2;

			ofse = pg->damageLevel[src->originalindex];

			*(u_int*)&prim->u0 = pg->pciv_clut[(src->clut_uv0 >> 0x10) + palette] << 0x10 | (src->clut_uv0 & 0xffff) + ofse;
			*(u_int*)&prim->u1 = src->tpage_uv1 + ofse;
			*(u_int*)&prim->u2 = src->uv3_uv2 + ofse;

			gte_stsxy3(&prim->x0, &prim->x1, &prim->x2);

			setPolyGT3(prim);
			addPrim(pg->ot + (otz >> 1), prim);

			prim++;
		}

		src++;
		numTris--;
	}

	pg->primptr = (unsigned char*)prim;
}


#ifdef DYNAMIC_LIGHTING
void plotCarPolyGT3Lit(int numTris, CAR_POLY* src, SVECTOR* vlist, SVECTOR* nlist, plotCarGlobals* pg, int palette)
{
	int Z;
	int otz;
	SVECTOR* v2;
	SVECTOR* v1;
	SVECTOR* v0;
	u_int indices;
	POLY_GT3* prim;
	u_int r0, r1, r2;
	int ofse;

	prim = (POLY_GT3*)pg->primptr;

	int GT3rgb = pg->intensity | 0x34000000;
	gte_ldrgb(&GT3rgb);

	while (numTris > 0)
	{
		indices = src->vindices;

		v0 = vlist + (indices & 0xff);
		v1 = vlist + (indices >> 8 & 0xff);
		v2 = vlist + (indices >> 16 & 0xff);

		gte_ldv3(v0, v1, v2);

		gte_rtpt();
		gte_nclip();

		gte_stopz(&Z);

		gte_avsz3();

		gte_stotz(&otz);

		if (Z > -1 && otz > 0)
		{
			indices = src->nindices;

			r0 = (u_int)(ushort)nlist[indices & 0xff].pad;
			r1 = (u_int)(ushort)nlist[indices >> 8 & 0xff].pad;
			r2 = (u_int)(ushort)nlist[indices >> 16 & 0xff].pad;

			*(u_int*)&prim->r0 = (r0 & 0xff) << 0x10 | r0;
			*(u_int*)&prim->r1 = (r1 & 0xff) << 0x10 | r1;
			*(u_int*)&prim->r2 = (r2 & 0xff) << 0x10 | r2;

			ofse = pg->damageLevel[src->originalindex];

			*(u_int*)&prim->u0 = pg->pciv_clut[(src->clut_uv0 >> 0x10) + palette] << 0x10 | (src->clut_uv0 & 0xffff) + ofse;
			*(u_int*)&prim->u1 = src->tpage_uv1 + ofse;
			*(u_int*)&prim->u2 = src->uv3_uv2 + ofse;

			gte_stsxy3(&prim->x0, &prim->x1, &prim->x2);

			SVECTOR tmpPos;
			gte_ldv0(v0);
			gte_rtps();
			gte_stsv(&tmpPos);
			GetDLightLevel(&tmpPos, (u_int*)&prim->r0);

			gte_ldv0(v1);
			gte_rtps();
			gte_stsv(&tmpPos);
			GetDLightLevel(&tmpPos, (u_int*)&prim->r1);

			gte_ldv0(v2);
			gte_rtps();
			gte_stsv(&tmpPos);
			GetDLightLevel(&tmpPos, (u_int*)&prim->r2);

			setPolyGT3(prim);
			addPrim(pg->ot + (otz >> 1), prim);

			prim++;
		}

		src++;
		numTris--;
	}

	pg->primptr = (unsigned char*)prim;
}
#endif // DYNAMIC_LIGHTING


// [D] [T]
void plotCarPolyGT3nolight(int numTris, CAR_POLY *src, SVECTOR *vlist, plotCarGlobals *pg, int palette)
{
	int Z;
	int otz;
	SVECTOR* v2;
	SVECTOR* v1;
	SVECTOR* v0;
	u_int indices;
	POLY_FT3* prim;
	int ofse;

	prim = (POLY_FT3*)pg->primptr;

	int GT3rgb = pg->intensity | 0x34000000;
	gte_ldrgb(&GT3rgb);

	while (numTris > 0)
	{
		indices = src->vindices;

		v0 = vlist + (indices & 0xff);
		v1 = vlist + (indices >> 8 & 0xff);
		v2 = vlist + (indices >> 16 & 0xff);

		gte_ldv3(v0, v1, v2);

		gte_rtpt();
		gte_nclip();
		gte_stopz(&Z);

		gte_avsz3();
		gte_stotz(&otz);

		if (Z > -1 && otz > 0)
		{
			*(u_int*)&prim->r0 = GT3rgb;

			ofse = pg->damageLevel[src->originalindex];

			*(u_int*)&prim->u0 = pg->pciv_clut[(src->clut_uv0 >> 0x10) + palette] << 0x10 | (src->clut_uv0 & 0xffff) + ofse;
			*(u_int*)&prim->u1 = src->tpage_uv1 + ofse;
			*(u_int*)&prim->u2 = src->uv3_uv2 + ofse;

			gte_stsxy3(&prim->x0, &prim->x1, &prim->x2);

			setPolyFT3(prim);
			addPrim(pg->ot + (otz >> 1), prim);

			prim++;
		}

		src++;
		numTris--;
	}

	pg->primptr = (unsigned char*)prim;
}

#ifndef PSX
int gCarGloss = 1;	// config.ini [render] carGloss: the sun mirrors in the paint of cars

#define CAR_GLOSS_SPOT		120		// brightest sun spot added to the paint (of 255)
#define CAR_GLOSS_SKY		36		// brightest soft reflection of the sky
#define CAR_GLOSS_FADE_FROM	4000	// distance (view z) where the gloss starts to fade
#define CAR_GLOSS_FADE_TO	12000	// and where it is gone
#define CAR_GLOSS_LAMP_RADIUS	80	// model units around a lamp that get no gloss (the volumetric lights do the shining)
#define CAR_REFLECT_HORIZON	44		// brightest haze of the horizon mirrored in the paint (config.ini [render] carReflection)
#define CAR_REFLECT_OVERCAST	0.55f	// environment reflection that remains without a sun: wet paint under a grey sky
#define CAR_REFLECT_FOG		0.30f	// the same in fog and falling snow

int gCarReflection = 1;			// config.ini [render] carReflection: the sky, the horizon and a wet sheen mirror in the paint, also without sun

static int sGlossOn;				// the car being drawn gets gloss
static float sGlossGain;			// sun strength x distance fade
static float sEnvGain;				// environment reflection (sky, horizon) x distance fade: stays in rain and fog
static float sWet;					// 1 = wet paint (rain, wet road): the sky reflection is stronger
static float sCarM[3][3];			// model space to view space of the car being drawn (scaled to 1)
static float sSun[3], sEye[3], sHalf[3], sUp[3];	// sun, camera, half way vector and world up in view space
static float sPointNormal[256][3];	// point normals of the car in view space, pointing out of the car
static int sHasPointNormals;
static SVECTOR sLamps[12];			// lamp positions in model space (both sides)
static int sNumLamps;

static void GlossNormalize(float* v)
{
	float len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);

	if (len > 0.0001f)
	{
		v[0] /= len; v[1] /= len; v[2] /= len;
	}
}

static void GlossToView(float* out, float x, float y, float z)
{
	for (int i = 0; i < 3; i++)
		out[i] = sCarM[i][0] * x + sCarM[i][1] * y + sCarM[i][2] * z;
}

// Sets up everything the gloss of one car needs: the sun, the camera and the car in view space
static void SetupCarGloss(CAR_DATA* cp, MATRIX* carMat, VECTOR* pos)
{
	MODEL* model;
	CAR_COSMETICS* cc = cp->ap.carCos;
	const SVECTOR* sun = &sun_position[BASE_CITY];
	int sunY = sun->vy;
	float len;
	int i, j;

	sGlossOn = 0;

	if (!gCarGloss || pos->vz <= 0 || pos->vz >= CAR_GLOSS_FADE_TO || sun->vy >= 0)
		return;

	{
		float fade = 1.0f - SmoothStepF(CAR_GLOSS_FADE_FROM, CAR_GLOSS_FADE_TO, (float)pos->vz);
		float shine = SunShine();
		float overcast = (gWeather == WEATHER_RAIN || gWeather == WEATHER_WET) ? CAR_REFLECT_OVERCAST : (gFogTarget || gSnowTarget) ? CAR_REFLECT_FOG : 0.0f;

		sWet = (gWeather == WEATHER_RAIN || gWeather == WEATHER_WET) ? 1.0f : 0.0f;
		sGlossGain = shine * fade;
		float rest = overcast * SmoothStepF(-2.0f, 6.0f, DayCycleActive() ? gSunElevation : 10.0f);

		sEnvGain = gCarReflection ? (shine > rest ? shine : rest) * fade : sGlossGain;
	}

	if (sEnvGain < 0.02f)
		return;

	// the fixed dawn and dusk sun stands lower than its position says: the same rule as SunShadow, so the spot sits where the shadow points
	if (!DayCycleActive() && (gTimeOfDay == TIME_DAWN || gTimeOfDay == TIME_DUSK))
		sunY /= 2;

	for (i = 0; i < 3; i++)
	{
		sSun[i] = (inv_camera_matrix.m[i][0] * sun->vx + inv_camera_matrix.m[i][1] * sunY + inv_camera_matrix.m[i][2] * sun->vz) / 4096.0f;
		sUp[i] = -inv_camera_matrix.m[i][1] / 4096.0f;
		for (j = 0; j < 3; j++)
			sCarM[i][j] = carMat->m[i][j] / 4096.0f;
	}

	// towards the camera from the middle of the car
	sEye[0] = -(float)pos->vx; sEye[1] = -(float)pos->vy; sEye[2] = -(float)pos->vz;

	GlossNormalize(sSun);
	GlossNormalize(sEye);

	for (i = 0; i < 3; i++)
		sHalf[i] = sSun[i] + sEye[i];

	len = sqrtf(sHalf[0] * sHalf[0] + sHalf[1] * sHalf[1] + sHalf[2] * sHalf[2]);

	if (len < 0.01f)
		return;

	GlossNormalize(sHalf);

	// the model normals point into the car in this convention: minus gives the outward normal
	model = gCarCleanModelPtr[cp->ap.model];
	sHasPointNormals = model != NULL && model->num_point_normals != 0;

	if (sHasPointNormals)
	{
		SVECTOR* norms = GET_MODEL_DATA(SVECTOR, model, point_normals);
		int count = MIN(256, (model->num_point_normals / 3 + 1) * 3);

		for (i = 0; i < count; i++)
		{
			GlossToView(sPointNormal[i], -norms[i].vx / 4096.0f, -norms[i].vy / 4096.0f, -norms[i].vz / 4096.0f);
			GlossNormalize(sPointNormal[i]);
		}
	}

	// the lights are drawn by the volumetric lights: no gloss on their lenses
	const SVECTOR* lamps[] = { &cc->headLight, &cc->frontInd, &cc->backInd, &cc->brakeLight, &cc->revLight, &cc->policeLight };

	sNumLamps = 0;

	for (i = 0; i < 6; i++)
	{
		for (j = -1; j <= 1; j += 2)
		{
			sLamps[sNumLamps].vx = lamps[i]->vx * j + cc->cog.vx;
			sLamps[sNumLamps].vy = lamps[i]->vy + cc->cog.vy;
			sLamps[sNumLamps].vz = lamps[i]->vz + cc->cog.vz;
			sNumLamps++;
		}
	}

	sGlossOn = 1;
}

// Gloss (0..255) of a surface point with the outward normal n in view space: a tight mirror image of the sun plus a faint
// reflection of the sky on surfaces that mirror upwards, strongest at glancing angles like on glass
static int GlossAt(const float* n)
{
	float sunSide = n[0] * sSun[0] + n[1] * sSun[1] + n[2] * sSun[2];
	float d = n[0] * sHalf[0] + n[1] * sHalf[1] + n[2] * sHalf[2];
	float nv = n[0] * sEye[0] + n[1] * sEye[1] + n[2] * sEye[2];
	float g = 0.0f;

	if (sunSide > 0.0f && d > 0.0f && sGlossGain > 0.0f)
	{
		float d2 = d * d, d4 = d2 * d2, d16 = d4 * d4 * d4 * d4, d32 = d16 * d16;

		g += CAR_GLOSS_SPOT * d32 * d32 * sGlossGain;
	}

	if (nv > 0.0f)
	{
		float rx = 2.0f * nv * n[0] - sEye[0];
		float ry = 2.0f * nv * n[1] - sEye[1];
		float rz = 2.0f * nv * n[2] - sEye[2];
		float up = rx * sUp[0] + ry * sUp[1] + rz * sUp[2];

		if (up > 0.0f)
		{
			float edge = 1.0f - nv;

			g += CAR_GLOSS_SKY * (1.0f + sWet * gCarReflection) * up * (0.3f + 0.7f * edge * edge * edge) * sEnvGain;
		}

		// the horizon: panels that mirror sideways show its haze, glass-like at glancing angles
		if (gCarReflection)
		{
			float h = 1.0f - 2.5f * fabsf(up);
			float edge = 1.0f - nv;

			if (h > 0.0f)
				g += CAR_REFLECT_HORIZON * h * h * (0.25f + 0.75f * edge * edge) * sEnvGain;
		}
	}

	return MIN(255, (int)g);
}

static int GlossNearLamp(const SVECTOR* v)
{
	for (int i = 0; i < sNumLamps; i++)
	{
		int dx = v->vx - sLamps[i].vx, dy = v->vy - sLamps[i].vy, dz = v->vz - sLamps[i].vz;

		if (dx * dx + dy * dy + dz * dz < CAR_GLOSS_LAMP_RADIUS * CAR_GLOSS_LAMP_RADIUS)
			return 1;
	}

	return 0;
}

// Second pass over the body and the parts under it: an additive white layer with the gloss as vertex colour, built from the
// same (dented) vertices as the car itself. All polygons share one ordering slot just in front of the car, preceded by a
// primitive that switches the blend mode to additive.
static void plotCarGloss(CAR_MODEL* car, plotCarGlobals* pg)
{
	int Z, otz, slot = 0x7fffffff;
	POLY_G3* first = (POLY_G3*)pg->primptr;
	POLY_G3* prim = first;
	POLY_FT3* mode;
	SVECTOR* vlist = car->vlist;

	// per vertex normal = sum of the face normals around it (the winding is the same everywhere): a flat triangle gets
	// a smooth gloss across its corners instead of one value per facet, which showed as triangles on the paint
	static float vertexNormal[256][3];

	if (!sGlossOn)
		return;

	memset(vertexNormal, 0, sizeof(vertexNormal));

	for (int list = 0; list < 2; list++)
	{
		CAR_POLY* src = list ? car->pGT3 : car->pFT3;
		int numTris = list ? car->numGT3 : car->numFT3;

		for (; numTris > 0; numTris--, src++)
		{
			const int i0 = src->vindices & 0xff, i1 = src->vindices >> 8 & 0xff, i2 = src->vindices >> 16 & 0xff;
			const SVECTOR* a = vlist + i0, *b = vlist + i1, *c = vlist + i2;
			const float cx = (float)(b->vy - a->vy) * (c->vz - a->vz) - (float)(b->vz - a->vz) * (c->vy - a->vy);
			const float cy = (float)(b->vz - a->vz) * (c->vx - a->vx) - (float)(b->vx - a->vx) * (c->vz - a->vz);
			const float cz = (float)(b->vx - a->vx) * (c->vy - a->vy) - (float)(b->vy - a->vy) * (c->vx - a->vx);
			const int idx[3] = { i0, i1, i2 };

			for (int k = 0; k < 3; k++)
			{
				vertexNormal[idx[k]][0] += cx;
				vertexNormal[idx[k]][1] += cy;
				vertexNormal[idx[k]][2] += cz;
			}
		}
	}

	for (int list = 0; list < 2; list++)
	{
		CAR_POLY* src = list ? car->pGT3 : car->pFT3;
		int numTris = list ? car->numGT3 : car->numFT3;

		for (; numTris > 0; numTris--, src++)
		{
			u_int indices = src->vindices;
			SVECTOR* v0 = vlist + (indices & 0xff);
			SVECTOR* v1 = vlist + (indices >> 8 & 0xff);
			SVECTOR* v2 = vlist + (indices >> 16 & 0xff);
			float face[3], n[3];
			int g[3], k;

			gte_ldv3(v0, v1, v2);
			gte_rtpt();
			gte_nclip();
			gte_stopz(&Z);
			gte_avsz3();
			gte_stotz(&otz);

			if (Z < 0 || otz <= 0)
				continue;

			// face normal of the dented triangle, turned to the camera (a visible triangle faces out of the car)
			GlossToView(face, (v1->vy - v0->vy) * (v2->vz - v0->vz) - (v1->vz - v0->vz) * (v2->vy - v0->vy),
				(v1->vz - v0->vz) * (v2->vx - v0->vx) - (v1->vx - v0->vx) * (v2->vz - v0->vz),
				(v1->vx - v0->vx) * (v2->vy - v0->vy) - (v1->vy - v0->vy) * (v2->vx - v0->vx));
			GlossNormalize(face);

			const float outward = (face[0] * sEye[0] + face[1] * sEye[1] + face[2] * sEye[2] < 0.0f) ? -1.0f : 1.0f;

			face[0] *= outward; face[1] *= outward; face[2] *= outward;

			for (k = 0; k < 3; k++)
			{
				const SVECTOR* v = k == 0 ? v0 : k == 1 ? v1 : v2;

				if (list && sHasPointNormals)
				{
					// smooth point normal of the clean model, bent by the real facet so dents show
					const float* p = sPointNormal[(src->nindices >> (8 * k)) & 0xff];

					n[0] = p[0] + 0.5f * face[0]; n[1] = p[1] + 0.5f * face[1]; n[2] = p[2] + 0.5f * face[2];
				}
				else
				{
					const float* vn = vertexNormal[(indices >> (8 * k)) & 0xff];

					GlossToView(n, vn[0], vn[1], vn[2]);
					GlossNormalize(n);
					n[0] = n[0] * outward + 0.25f * face[0]; n[1] = n[1] * outward + 0.25f * face[1]; n[2] = n[2] * outward + 0.25f * face[2];
				}

				GlossNormalize(n);

				// no gloss on a lamp lens: fade it out per corner instead of dropping the whole triangle
				g[k] = GlossNearLamp(v) ? 0 : GlossAt(n);
			}

			if (g[0] + g[1] + g[2] < 12)
				continue;

			setPolyG3(prim);
			setSemiTrans(prim, 1);
			prim->r0 = prim->g0 = prim->b0 = g[0];
			prim->r1 = prim->g1 = prim->b1 = g[1];
			prim->r2 = prim->g2 = prim->b2 = g[2];
			gte_stsxy3(&prim->x0, &prim->x1, &prim->x2);

			slot = MIN(slot, otz >> 1);
			prim++;
		}
	}

	if (prim == first)
		return;

	// the OT draws the last added primitive of a slot first, so the blend mode primitive goes in last
	for (POLY_G3* p = first; p < prim; p++)
		addPrim(pg->ot + (slot - 1), p);

	mode = (POLY_FT3*)prim;
	setPolyFT3(mode);
	mode->x0 = mode->y0 = mode->x1 = mode->y1 = mode->x2 = mode->y2 = -1;
	mode->tpage = 0x20;
	addPrim(pg->ot + (slot - 1), mode);

	pg->primptr = (u_char*)(mode + 1);
}
#endif

MATRIX save_colour_matrix;
MATRIX save_light_matrix;

// [D] [T]
void setupLightingMatrices(void)
{
	gte_ReadColorMatrix(&save_colour_matrix);
	gte_ReadLightMatrix(&save_light_matrix);

	gte_SetColorMatrix(&colour_matrix);
	gte_SetLightMatrix(&light_matrix);

	if (gTimeOfDay == TIME_NIGHT)
	{
		gte_SetBackColor(48, 48, 48);
	}
	else
	{
		gte_SetBackColor(140, 140, 140);
	}
}

// [D] [T]
void restoreLightingMatrices(void)
{
	gte_SetColorMatrix(&save_colour_matrix);
	gte_SetLightMatrix(&save_light_matrix);
}

// [D] [T]
void ComputeCarLightingLevels(CAR_DATA* cp, char detail)
{
#ifdef PSX
	MATRIX& scratchPadMat = *(MATRIX*)((u_char*)getScratchAddr(0) + 0x100);
#else
	MATRIX scratchPadMat;
#endif

	int doLight;
	int orW, orY;
	MODEL* model;
	int num_norms, count;
	SVECTOR* ppads;
	SVECTOR* norms;
	SVECTOR colour, lightsourcevector;
	CVECTOR c0, c1, c2;
	u_int GT3rgb;

	if (gTimeOfDay == TIME_NIGHT)
	{
		lightsourcevector = night_vectors[BASE_CITY];
		colour = night_colours[BASE_CITY];
	}
	else
	{
		lightsourcevector = day_vectors[BASE_CITY];
		colour = day_colours[BASE_CITY];
	}

	InvertMatrix(&cp->hd.where, &scratchPadMat);
	SetRotMatrix(&scratchPadMat);

	gte_ldv0(&lightsourcevector);
	gte_rtv0();
	gte_stsv(light_matrix.m[0]);

	colour_matrix.m[0][0] = colour.vx;
	colour_matrix.m[1][0] = colour.vy;
	colour_matrix.m[2][0] = colour.vz;

	orY = ABS(cp->st.n.orientation[1] - cp->ap.qy);
	orW = ABS(cp->st.n.orientation[3] - cp->ap.qw);

	doLight = 0;

	if ((orY + orW > 200) || (cp->lowDetail != (detail | lightning)))
		doLight = 1;

	if ((M_BIT(gTimeOfDay) & (M_BIT(TIME_DAWN) | M_BIT(TIME_DUSK))) && (cp->id & 15) == (CameraCnt & 15))
		doLight = 1;

	if (doLight)
	{
		setupLightingMatrices();

		GT3rgb = combointensity & 0xffffffU | 0x34000000;
		gte_ldrgb(&GT3rgb);

		cp->ap.qy = cp->st.n.orientation[1];
		cp->ap.qw = cp->st.n.orientation[3];
		cp->lowDetail = detail | lightning;

		model = detail ? gCarCleanModelPtr[cp->ap.model] : gCarLowModelPtr[cp->ap.model];
		num_norms = model->num_point_normals / 3;
		norms = GET_MODEL_DATA(SVECTOR, model, point_normals);

		ppads = gTempCarVertDump[cp->id];
		count = num_norms;// +1;

		// [A] Driver 1 civilian cars carry no point normals: every vertex is shaded as if it pointed away from the car centre
		if (model->num_point_normals == 0)
		{
			SVECTOR* verts = GET_MODEL_DATA(SVECTOR, model, vertices);
			SVECTOR dir[3];
			int i, j;

			for (i = 0; i <= model->num_vertices; i += 3)
			{
				for (j = 0; j < 3; j++)
				{
					int x = verts[i + j].vx;
					int y = verts[i + j].vy + 40;
					int z = verts[i + j].vz;
					int len = SquareRoot0(x * x + y * y + z * z);

					if (len == 0)
						len = 1;

					dir[j].vx = x * 4096 / len;
					dir[j].vy = y * 4096 / len;
					dir[j].vz = z * 4096 / len;
				}

				gte_ldv3(&dir[0], &dir[1], &dir[2]);
				gte_ncct();
				gte_strgb3(&c0, &c1, &c2);

				ppads[i].pad = *(short*)&c0;
				ppads[i + 1].pad = *(short*)&c1;
				ppads[i + 2].pad = *(short*)&c2;
			}

			count = -1;
		}

		while (count >= 0)
		{
			gte_ldv3(&norms[0], &norms[1], &norms[2]);
			gte_ncct();
			gte_strgb3(&c0, &c1, &c2);

			ppads[0].pad = *(short*)&c0;
			ppads[1].pad = *(short*)&c1;
			ppads[2].pad = *(short*)&c2;

			count--;
			norms += 3;
			ppads += 3;
		}

		restoreLightingMatrices();
	}
}

// [D] [T]
void DrawWheelObject(MODEL* model, SVECTOR* verts, int transparent, int wheelnum)
{
	ushort clut;
	ushort tpage;
	int i;
	int Z;
	int otZ;
	int combo;
	POLY_FT4* poly;
	POLYFT4* src;
	u_int bright;

	src = GET_MODEL_DATA(POLYFT4, model, poly_block);
	poly = (POLY_FT4*)current->primptr;

	clut = texture_cluts[src->texture_set][src->texture_id];
	tpage = texture_pages[src->texture_set] | 0x20;

	if (gTimeOfDay > -1)
	{
		if (gTimeOfDay < TIME_NIGHT)
		{
			bright = combointensity & 0xffffffU | 0x2c000000;
		}
		else if (gTimeOfDay == TIME_NIGHT)
		{
			combo = (combointensity & 0xffU) / 3;
			combo = combo << 0x10 | combo << 8 | combo;

			bright = combo | 0x2c000000;
		}
	}

	i = model->num_polys;

	while (i-- != -1)
	{
		gte_ldv3(&verts[src->v0], &verts[src->v1], &verts[src->v2]);

		gte_rtpt();
		gte_nclip();

		gte_stopz(&Z);
		gte_stsxy0(&poly->x0);

		gte_ldv0(&verts[src->v3]);

		gte_rtps();
		gte_avsz4();

		gte_stotz(&otZ);

		if (otZ > 2)
		{
			setPolyFT4(poly);
			addPrim(current->ot + (otZ >> 1) + 5, poly);

			// [A] the wheel has no tyre, only a hubcap on both sides: the inner one (dimmed) showed a second hubcap through
			// the near wheel when a low camera looked under the car. Black, it reads as the tyre.
			if (i < 2 || Z < 0 || ((i ^ wheelnum >> 1) & 1) == 0)
				*(u_int*)&poly->r0 = 0x2c000000;
			else
				*(u_int*)&poly->r0 = bright;

			setSemiTrans(poly, transparent);

			gte_stsxy3(&poly->x1, &poly->x3, &poly->x2);

			*(u_short*)&poly->u0 = *(u_short*)&src->uv0;
			*(u_short*)&poly->u1 = *(u_short*)&src->uv1;
			*(u_short*)&poly->u2 = *(u_short*)&src->uv3;
			*(u_short*)&poly->u3 = *(u_short*)&src->uv2;
			poly->clut = clut;
			poly->tpage = tpage;

			poly++;
		}
		src++;
	}
	current->primptr = (char*)poly;
}

// [D] [T]
void DrawCarWheels(CAR_DATA *cp, MATRIX *RearMatrix, VECTOR *pos, int zclip)
{
	short wheelSize;
	int FW1z, FW2z;
	int BW1z, BW2z;
	int FrontWheelIncrement, BackWheelIncrement;
	int sizeScale;
	int wheelnum;
	SVECTOR* VertPtr;
	SVECTOR* wheelDisp;
	WHEEL* wheel;
	int car_id;
	MODEL* WheelModelBack;
	MODEL* WheelModelFront;
	MODEL* model;

#ifdef PSX
	MATRIX& FrontMatrix = *(MATRIX*)(u_char*)getScratchAddr(0);
	MATRIX& SteerMatrix = *(MATRIX*)((u_char*)getScratchAddr(0) + sizeof(MATRIX));
	VECTOR& WheelPos = *(VECTOR*)((u_char*)getScratchAddr(0) + sizeof(MATRIX) * 2);
	SVECTOR& sWheelPos = *(SVECTOR*)((u_char*)getScratchAddr(0) + sizeof(MATRIX) * 2 + sizeof(VECTOR));
	static_assert(sizeof(MATRIX) * 2 + sizeof(VECTOR) + sizeof(SVECTOR) * 25 < 1024 - sizeof(_pct), "Scratchpad overflow");
#else
	MATRIX FrontMatrix;
	MATRIX SteerMatrix;
	VECTOR WheelPos;
	SVECTOR sWheelPos;
#endif

	D_CHECK_ERROR(cp < car_data, "Invalid car");

	car_id = CAR_INDEX(cp);

	BackWheelIncrement = FrontWheelIncrement = cp->hd.wheel_speed >> 8;

	if (cp->wheelspin != 0)
		BackWheelIncrement = 700;

	if (cp->hd.wheel[0].locked != 0)
		FrontWheelIncrement = 0;

	if (cp->hd.wheel[3].locked != 0)
		BackWheelIncrement = 0;

	if (FrontWheelIncrement + 400U < 801)
		WheelModelFront = gCleanWheelModelPtr;
	else
		WheelModelFront = gFastWheelModelPtr;

	if (BackWheelIncrement + 400U < 801)
		WheelModelBack = gCleanWheelModelPtr;
	else
		WheelModelBack = gFastWheelModelPtr;

	wheelSize = gD1WheelVisualOn[cp->ap.model] ? gD1WheelSizeVisual[cp->ap.model] : car_cosmetics[cp->ap.model].wheelSize;

	sizeScale = (wheelSize * 14142) / 10000;

	// rotate wheel verts

	FW1z = FIXEDH(RSIN(FrontWheelRotation[car_id]) * sizeScale);
	FW2z = FIXEDH(RCOS(FrontWheelRotation[car_id]) * sizeScale);

	VertPtr = GET_MODEL_DATA(SVECTOR, WheelModelFront, vertices);

	VertPtr[8].vz = FW1z;
	VertPtr[15].vz = FW1z;
	VertPtr[11].vy = FW1z;
	VertPtr[12].vy = FW1z;

	VertPtr[9].vy = -FW1z;
	VertPtr[14].vy = -FW1z;
	VertPtr[10].vz = -FW1z;
	VertPtr[13].vz = -FW1z;

	VertPtr[8].vy = FW2z;
	VertPtr[15].vy = FW2z;
	VertPtr[9].vz = FW2z;
	VertPtr[14].vz = FW2z;

	VertPtr[10].vy = -FW2z;
	VertPtr[13].vy = -FW2z;
	VertPtr[11].vz = -FW2z;
	VertPtr[12].vz = -FW2z;

	VertPtr[23].vz = 0;
	VertPtr[22].vz = 0;
	VertPtr[21].vy = 0;
	VertPtr[20].vy = 0;
	VertPtr[19].vy = 0;
	VertPtr[18].vy = 0;
	VertPtr[17].vz = 0;
	VertPtr[16].vz = 0;

	VertPtr[23].vy = wheelSize;
	VertPtr[22].vy = wheelSize;
	VertPtr[21].vz = wheelSize;
	VertPtr[20].vz = wheelSize;

	VertPtr[19].vz = -wheelSize;
	VertPtr[18].vz = -wheelSize;
	VertPtr[17].vy = -wheelSize;
	VertPtr[16].vy = -wheelSize;

	BW1z = FIXEDH(RSIN(BackWheelRotation[car_id]) * sizeScale);
	BW2z = FIXEDH(RCOS(BackWheelRotation[car_id]) * sizeScale);

	VertPtr = GET_MODEL_DATA(SVECTOR, WheelModelBack, vertices);
	
	VertPtr[8].vz = BW1z;
	VertPtr[15].vz = BW1z;
	VertPtr[11].vy = BW1z;
	VertPtr[12].vy = BW1z;

	VertPtr[9].vy = -BW1z;
	VertPtr[14].vy = -BW1z;
	VertPtr[10].vz = -BW1z;
	VertPtr[13].vz = -BW1z;

	VertPtr[8].vy = BW2z;
	VertPtr[15].vy = BW2z;
	VertPtr[9].vz = BW2z;
	VertPtr[14].vz = BW2z;

	VertPtr[10].vy = -BW2z;
	VertPtr[13].vy = -BW2z;
	VertPtr[11].vz = -BW2z;
	VertPtr[12].vz = -BW2z;

	VertPtr[23].vz = 0;
	VertPtr[22].vz = 0;
	VertPtr[21].vy = 0;
	VertPtr[20].vy = 0;
	VertPtr[19].vy = 0;
	VertPtr[18].vy = 0;
	VertPtr[17].vz = 0;
	VertPtr[16].vz = 0;

	VertPtr[23].vy = wheelSize;
	VertPtr[22].vy = wheelSize;
	VertPtr[21].vz = wheelSize;
	VertPtr[20].vz = wheelSize;

	VertPtr[19].vz = -wheelSize;
	VertPtr[18].vz = -wheelSize;
	VertPtr[17].vy = -wheelSize;
	VertPtr[16].vy = -wheelSize;

	SteerMatrix.m[0][0] = RCOS(cp->wheel_angle);
	SteerMatrix.m[0][2] = RSIN(cp->wheel_angle);
	SteerMatrix.m[1][1] = ONE;
	SteerMatrix.m[2][1] = 0;
	SteerMatrix.m[1][2] = 0;
	SteerMatrix.m[1][0] = 0;
	SteerMatrix.m[0][1] = 0;
	SteerMatrix.m[2][0] = -SteerMatrix.m[0][2];
	SteerMatrix.m[2][2] = SteerMatrix.m[0][0];

	MulMatrix0(RearMatrix, &SteerMatrix, &FrontMatrix);

	wheelDisp = gD1WheelVisualOn[cp->ap.model] ? gD1WheelVisual[cp->ap.model] : car_cosmetics[cp->ap.model].wheelDisp;
	wheel = cp->hd.wheel;
	wheelnum = 0;

	do {
		if ((wheelnum & 1) != 0)
			model = WheelModelBack;
		else
			model = WheelModelFront;

		VertPtr = GET_MODEL_DATA(SVECTOR, model, vertices);

#ifndef PSX
		// test switch: every wheel without its hubcap, to look at the damaged wheel model
		static int forceDamaged = -1;
		if (forceDamaged < 0)
			forceDamaged = getenv("REDRIVER2_DAMWHEEL") != NULL;
#else
		const int forceDamaged = 0;
#endif

		if (forceDamaged || (cp->ap.flags & (1 << wheelnum))) // [A] used appearance flags to store hubcap presence
		{
			model = gDamWheelModelPtr;
		}

		if ((wheelnum & 2) == 0) 
			sWheelPos.vx = 17 - wheelDisp->vx;
		else
			sWheelPos.vx = -17 - wheelDisp->vx;

		sWheelPos.vz = -wheelDisp->vz;
		sWheelPos.vy = (-wheelSize - wheelDisp->vy) - wheel->susCompression + 14;

		gte_SetRotMatrix(RearMatrix);
		gte_ldv0(&sWheelPos);

		gte_rtv0();

		gte_stlvl(&WheelPos);

		WheelPos.vx += pos->vx;
		WheelPos.vy += pos->vy;
		WheelPos.vz += pos->vz;

		gte_SetTransVector(&WheelPos);

		if ((wheelnum & 1) == 0) 
		{
			gte_SetRotMatrix(&FrontMatrix);
		}

#ifndef PSX
		if (!WheelIsLost(cp, wheelnum))	// [A] a torn off wheel is gone
#endif
			DrawWheelObject(model, VertPtr, TransparentObject, wheelnum);

		wheelDisp++;
		wheel++;
		wheelnum++;
	} while (wheelnum < 4);
}

// [A] lamps of a car: the time of day and the weather (gLightsOn), the player's own switch wins for the player's car
int CarLightsOn(CAR_DATA* cp)
{
#ifndef PSX
	extern int gPlayerLights;

	if (gPlayerLights && cp->controlType == CONTROL_TYPE_PLAYER)
		return gPlayerLights == 1;
#endif

	return gLightsOn && !lightsOnDelay[cp->id];
}

// [A] lamps burn, and the car is not a parked civ car, nor a silenced one; a car waiting at a junction keeps
// its lamps on (it switched them off, so their light on the road and on the cars around went out at once)
int CarShowsNightLights(CAR_DATA* cp)
{
	if (!CarLightsOn(cp))
		return 0;

	if (cp->controlType == CONTROL_TYPE_CIV_AI)
		return cp->ai.c.thrustState != CIV_AI_THRUST_STOP || (cp->ai.c.ctrlState != CIV_AI_CTRL_PARKED && cp->ai.c.ctrlState != CIV_AI_CTRL_EMPTY);

	return SilenceThisCar(cp->id) == 0;
}

// [A] the rotating lights of a cop car (also roadblocks, cutscene cars with a siren and the Vegas ambulance) burn
int CarSirenOn(CAR_DATA* cp)
{
	if (!CarHasSiren(cp->ap.model) || cp->ai.p.dying >= 75)
		return 0;

	return IS_ROADBLOCK_CAR(cp) || cp->controlType == CONTROL_TYPE_PURSUER_AI ||
		gInGameCutsceneActive && cp->controlType == CONTROL_TYPE_CUTSCENE && force_siren[CAR_INDEX(cp)] != 0 ||
		gCurrentMissionNumber == 26 && cp->controlType == CONTROL_TYPE_CUTSCENE && cp->ap.model == SPECIAL_CAR_SLOT;
}

// [D] [T]
void PlayerCarFX(CAR_DATA *cp)
{
	int WheelSpeed;
	WheelSpeed = cp->hd.wheel_speed;

#ifndef PSX
	// test switch: hazard lights on the player car, to check the indicator lamp positions of a model
	static int hazard = -1;
	if (hazard < 0)
		hazard = getenv("REDRIVER2_HAZARD") != NULL;

	if (hazard)
		AddIndicatorLight(cp, 2);
#endif

	if (WheelSpeed + 199U < 0x4b0c7)
	{
		if (cp->wheel_angle < -200)
			AddIndicatorLight(cp, 0);
		else if (cp->wheel_angle > 200)
			AddIndicatorLight(cp, 1);
	}

	if( WheelSpeed < 0 && cp->thrust > 0 ||
		WheelSpeed > 0 && cp->thrust < 0)
	{
		AddBrakeLight(cp);
	}

#if defined(DEBUG_OPTIONS) && !defined(PSX)
	extern int gForceLightsOn;

	// -lightson test option: show the reverse lamps too
	if (gForceLightsOn)
		AddReverseLight(cp);
#endif

	if (WheelSpeed < 0 && cp->thrust < 0)
		AddReverseLight(cp);
}

// [D] [T]
void plotNewCarModel(CAR_MODEL* car, int palette)
{
#ifdef PSX
	plotCarGlobals& _pg = *(plotCarGlobals*)((u_char*)getScratchAddr(0) + 1024 - sizeof(plotCarGlobals) - sizeof(_pct));
#else
	plotCarGlobals _pg;
#endif

	u_int lightlevel;
	u_int underIntensity;
	SVECTOR v = { 0, -4096, 0 };

	lightlevel = combointensity | 0x3000000;

	if (car == NULL)
		return;

	if (gTimeOfDay > -1)
	{
		if (gTimeOfDay < TIME_NIGHT)
			lightlevel = combointensity | 0x30000000;
		else if (gTimeOfDay == TIME_NIGHT)
			lightlevel = 0x302a2a2a;
	}

	setupLightingMatrices();

	gte_ldv0(&v);
	gte_ldrgb(&lightlevel);

	gte_nccs();

	_pg.primptr = (u_char*)current->primptr;
	_pg.intensity = 0;
	_pg.pciv_clut = (u_short*)&civ_clut[1];
	_pg.damageLevel = (u_char*)gTempCarUVPtr;

	_pg.ot = (OTTYPE*)(current->ot + 28);

	gte_strgb(&underIntensity);

	// draw wheel arcs
	plotCarPolyB3(car->numB3, car->pB3, car->vlist, &_pg);
	_pg.intensity = underIntensity & 0xffffff;

	// draw car bottom
	_pg.ot = (OTTYPE*)(current->ot + 16);
	plotCarPolyFT3(car->numFT3, car->pFT3, car->vlist, &_pg);

	// draw car body
	_pg.ot = (OTTYPE*)(current->ot + 4);
	if (gTimeOfDay == TIME_NIGHT)
	{
		_pg.intensity = (combointensity & 0xfcfcf0U) >> 2;
#ifdef DYNAMIC_LIGHTING
		(gEnableDlights ? plotCarPolyGT3Lit : plotCarPolyGT3)(car->numGT3, car->pGT3, car->vlist, car->nlist, &_pg, palette);
#else
		plotCarPolyGT3nolight(car->numGT3, car->pGT3, car->vlist, &_pg, palette);
#endif // DYNAMIC_LIGHTING
	}
	else
	{
		_pg.intensity = combointensity & 0xffffff;
#ifdef DYNAMIC_LIGHTING
		(gEnableDlights ? plotCarPolyGT3Lit : plotCarPolyGT3)(car->numGT3, car->pGT3, car->vlist, car->nlist, &_pg, palette);
#else
		plotCarPolyGT3(car->numGT3, car->pGT3, car->vlist, car->nlist, &_pg, palette);
#endif

#ifndef PSX
		plotCarGloss(car, &_pg);
#endif
	}

	current->primptr = (char*)_pg.primptr;

	restoreLightingMatrices();
}

// [A]
void startBuildNewCars(int isSpecial)
{
	if(isSpecial)
		baseSpecCP = whichCP;
	else
		whichCP = 0;
}


#if USE_PC_FILESYSTEM
static char* CarModelTypeNames[] = {
	"CLEAN",
	"DAMAGED",
	"LOW",
};

// [A] loads car model from file
char* LoadCarModelFromFile(char* dest, int modelNumber, int type)
{
	char* mem;
	char filename[64];

	sprintf(filename, "LEVELS\\%s\\CARMODEL_%d_%s.MDL", LevelNames[GameLevel], modelNumber, CarModelTypeNames[type - 1]);
	if (FileExists(filename))
	{
		mem = (char*)(dest ? dest : (_other_buffer + modelNumber * 0x10000 + (type - 1) * 0x4000));

		// get model from file
		Loadfile(filename, mem);
		return mem;
	}

	return NULL;
}
#endif

// [D] [T]
MODEL* GetCarModel(char* src, char** dest, int KeepNormals)
{
	int size;
	MODEL* model;
	char* mem;

	model = (MODEL*)*dest;

	if (KeepNormals == 0)
		size = ((MODEL*)src)->normals;
	else
		size = ((MODEL*)src)->poly_block;

	// if loaded externally don't copy from source lump
	memcpy((u_char*)*dest, (u_char*)src, size);

	if (KeepNormals == 0)
		size = model->normals;
	else
		size = model->poly_block;

	//*dest += size + 2;
	*dest = (char*)((u_intptr)model + size + 3 & ~3);

#if MODEL_RELOCATE_POINTERS
	model->vertices += (int)model;
	model->normals += (int)model;
	model->poly_block = (int)src + model->poly_block;

	if (KeepNormals == 0)
		model->point_normals = 0;
	else
		model->point_normals += (int)model;
#else
	if (KeepNormals == 0)
		model->point_normals = 0;

	model->instance_number = -1;
#endif
	return model;
}

// [D] [T] [A]
void buildNewCarFromModel(int index, int detail, char* polySrc, MODEL* model)
{
	int newNumPolys;
	int i, pass;
	ushort clut;
	u_char ptype, carid;
	u_char *polyList;
	CAR_POLY* cp;
	CAR_MODEL* car;
	MODEL* polySrcModel;

	polySrcModel = (MODEL*)polySrc;
	car = detail ? &NewCarModel[index] : &NewLowCarModel[index];

	if (polySrcModel == NULL || (polySrcModel->shape_flags & 0xfffffff) > 0x800000)
	{
		car->numGT3 = 0;
		car->numFT3 = 0;
		car->numB3 = 0;
		return;
	}

	model->instance_number = -1;
	polySrcModel->instance_number = -1;

	car->vlist = GET_MODEL_DATA(SVECTOR, model, vertices);
	car->nlist = GET_MODEL_DATA(SVECTOR, model, point_normals);
;
	for (pass = 0; pass < 3; pass++)
	{
		polyList = GET_RELOC_MODEL_DATA(u_char, polySrcModel, poly_block);

		if (pass == 1)
			car->pFT3 = carPolyBuffer + whichCP;
		else if (pass == 0)
			car->pGT3 = carPolyBuffer + whichCP;
		else if (pass == 2)
			car->pB3 = carPolyBuffer + whichCP;

		newNumPolys = whichCP;

		// [A] CAR_SNOW_TPAGE: only a light dusting of weather snow on the up facing body panels (the full layer turned a car white)
		for (i = 0; newNumPolys < MAX_CAR_POLYS && i < model->num_polys; i++)
		{
			ptype = *polyList;

			cp = carPolyBuffer + newNumPolys;

			switch (ptype & 0x1f) 
			{
				case 0:
				case 18:
					if (pass == 2)	// F3
					{
						cp->vindices = M_INT_4R(polyList[1], polyList[2], polyList[3], 0);
						cp->originalindex = i;

						newNumPolys++;
					}
					break;
				case 1:
				case 19:
					if (pass == 2)	// F4
					{
						cp->vindices = M_INT_4R(polyList[4], polyList[5], polyList[6], 0); 
						cp->originalindex = i;

						cp++;

						cp->vindices = M_INT_4R(polyList[4], polyList[6], polyList[7], 0);
						cp->originalindex = i;

						newNumPolys += 2;
					}
					break;
				case 20:
					if (pass == 1)	// FT3
					{
						POLYFT3* pft3 = (POLYFT3*)polyList;
									
						cp->vindices = M_INT_4R(pft3->v0, pft3->v1, pft3->v2, 0);
						cp->clut_uv0 = M_INT_2(texture_cluts[pft3->texture_set][pft3->texture_id], *(ushort*)&pft3->uv0);
						cp->tpage_uv1 = M_INT_2(CAR_SNOW_TPAGE(texture_pages[pft3->texture_set]), *(ushort*)&pft3->uv1);
						cp->uv3_uv2 = *(ushort*)&pft3->uv2;
						cp->originalindex = i;

						newNumPolys++;
					}
					break;
				case 21:
					if (pass == 1)	// FT4
					{
						POLYFT4* pft4 = (POLYFT4*)polyList;

						cp->vindices = M_INT_4R(pft4->v0, pft4->v1, pft4->v2, 0);
						cp->clut_uv0 = M_INT_2(texture_cluts[pft4->texture_set][pft4->texture_id], *(ushort *)&pft4->uv0);
						cp->tpage_uv1 = M_INT_2(CAR_SNOW_TPAGE(texture_pages[pft4->texture_set]), *(ushort*)&pft4->uv1);
						cp->uv3_uv2 = *(ushort*)&pft4->uv2;
						cp->originalindex = i;

						cp++;
						
						cp->vindices = M_INT_4R(pft4->v0, pft4->v2, pft4->v3, 0);
						cp->clut_uv0 = M_INT_2(texture_cluts[polyList[1]][polyList[2]], *(ushort*)&pft4->uv0);
						cp->tpage_uv1 = M_INT_2(CAR_SNOW_TPAGE(texture_pages[polyList[1]]), *(ushort*)&pft4->uv2);
						cp->uv3_uv2 = *(ushort*)&pft4->uv3;
						cp->originalindex = i;

						newNumPolys += 2;
					}
					break;
				case 22:
					if (pass == 0) // GT3
					{
						POLYGT3* pgt3 = (POLYGT3*)polyList;
							
						carid = GetCarPalIndex(pgt3->texture_set);
						clut = (carid - 1) * 6 * 32 + pgt3->texture_id * 6;

						civ_clut[carid][pgt3->texture_id][0] = texture_cluts[pgt3->texture_set][pgt3->texture_id];
						
						cp->vindices = M_INT_4R(pgt3->v0, pgt3->v1, pgt3->v2, 0);
						cp->nindices = M_INT_4R(pgt3->n0, pgt3->n1, pgt3->n2, 0);
						cp->clut_uv0 = M_INT_2(clut, *(ushort*)&pgt3->uv0);
						cp->tpage_uv1 = M_INT_2(CAR_SNOW_TPAGE(texture_pages[pgt3->texture_set]), *(ushort *)&pgt3->uv1);
						cp->uv3_uv2 = *(ushort *)&pgt3->uv2;
						cp->originalindex = i;

						newNumPolys++;
					}
					break;
				case 23:
					if (pass == 0)  // GT4
					{
						POLYGT4* pgt4 = (POLYGT4*)polyList;

						carid = GetCarPalIndex(pgt4->texture_set);
						clut = (carid - 1) * 6 * 32 + pgt4->texture_id * 6;

						civ_clut[carid][pgt4->texture_id][0] = texture_cluts[pgt4->texture_set][pgt4->texture_id];

						cp->vindices = M_INT_4R(pgt4->v0, pgt4->v1, pgt4->v2, 0);
						cp->nindices = M_INT_4R(pgt4->n0, pgt4->n1, pgt4->n2, 0);
						cp->clut_uv0 = M_INT_2(clut, *(ushort*)&pgt4->uv0);
						cp->tpage_uv1 = M_INT_2(CAR_SNOW_TPAGE(texture_pages[pgt4->texture_set]), *(ushort*)&pgt4->uv1);
						cp->uv3_uv2 = *(ushort*)&pgt4->uv2;
						cp->originalindex = i;

						cp++;

						cp->vindices = M_INT_4R(pgt4->v0, pgt4->v2, pgt4->v3, 0);
						cp->nindices = M_INT_4R(pgt4->n0, pgt4->n2, pgt4->n3, 0);
						cp->clut_uv0 = M_INT_2(clut, *(ushort*)&pgt4->uv0);
						cp->tpage_uv1 = M_INT_2(CAR_SNOW_TPAGE(texture_pages[pgt4->texture_set]), *(ushort *)&pgt4->uv2);
						cp->uv3_uv2 = *(ushort *)&pgt4->uv3;
						cp->originalindex = i;

						newNumPolys += 2;
					}
			}

			polyList += PolySizes[ptype & 0x1f];
		}

		if (pass == 1) 
			car->numFT3 = newNumPolys - whichCP;
		else if (pass == 0)
			car->numGT3 = newNumPolys - whichCP;
		else if (pass == 2) 
			car->numB3 = newNumPolys - whichCP;

		whichCP = newNumPolys;
	}
}

// [D] [T]
void MangleWheelModels(void)
{
	UV_INFO tmpUV2;
	u_char tmpUV;
	u_int v0, v1, v2;
	POLYFT4*src;
	MODEL *m;
	int i, j;

	for (i = 0; i < 3; i++)
	{
		if (i == 1)
			m = gFastWheelModelPtr;
		else if (i == 2)
			m = gDamWheelModelPtr;
		else
			m = gCleanWheelModelPtr;

		// do some fuckery swaps
		src = GET_MODEL_DATA(POLYFT4, m, poly_block);
		
		v0 = *(u_int *)&src[2].v0;
		v1 = *(u_int *)&src[2].uv0;
		v2 = *(u_int *)&src[2].uv2;
		*(u_int *)src = *(u_int *)(src + 2);
		*(u_int *)&src->v0 = v0;
		*(u_int *)&src->uv0 = v1;
		*(u_int *)&src->uv2 = v2;
		src->color = src[2].color;

		v0 = *(u_int *)&src[3].v0;
		v1 = *(u_int *)&src[3].uv0;
		v2 = *(u_int *)&src[3].uv2;
		*(u_int *)(src + 1) = *(u_int *)(src + 3);
		*(u_int *)&src[1].v0 = v0;
		*(u_int *)&src[1].uv0 = v1;
		*(u_int *)&src[1].uv2 = v2;
		src[1].color = src[3].color;

		v0 = *(u_int *)&src[4].v0;
		v1 = *(u_int *)&src[4].uv0;
		v2 = *(u_int *)&src[4].uv2;
		*(u_int *)(src + 2) = *(u_int *)(src + 4);
		*(u_int *)&src[2].v0 = v0;
		*(u_int *)&src[2].uv0 = v1;
		*(u_int *)&src[2].uv2 = v2;
		src[2].color = src[4].color;

		v0 = *(u_int *)&src[6].v0;
		v1 = *(u_int *)&src[6].uv0;
		v2 = *(u_int *)&src[6].uv2;
		*(u_int *)(src + 3) = *(u_int *)(src + 6);
		*(u_int *)&src[3].v0 = v0;
		*(u_int *)&src[3].uv0 = v1;
		*(u_int *)&src[3].uv2 = v2;
		src[3].color = src[6].color;

		src[2].v2 = 23;
		src[2].v3 = 22;
		src[3].v0 = 21;
		src[3].v1 = 20;
		src[3].v2 = 19;
		src[3].v3 = 18;
		src[2].v1 = 17;
		src[2].v0 = 16;

		tmpUV = (src->uv0.u + src->uv2.u) / 2;
		src[3].uv3.u = tmpUV;
		src[3].uv2.u = tmpUV;
		src[3].uv1.u = tmpUV;
		src[3].uv0.u = tmpUV;
		src[2].uv3.u = tmpUV;
		src[2].uv2.u = tmpUV;
		src[2].uv1.u = tmpUV;
		src[2].uv0.u = tmpUV;

		tmpUV = (src->uv0.v + src->uv2.v) / 2;
		src[3].uv3.v = tmpUV;
		src[3].uv2.v = tmpUV;
		src[3].uv1.v = tmpUV;
		src[3].uv0.v = tmpUV;
		src[2].uv3.v = tmpUV;
		src[2].uv2.v = tmpUV;
		src[2].uv1.v = tmpUV;
		src[2].uv0.v = tmpUV;

		m->num_polys = 4;

		for (j = 0; j < 2; j++)
		{
			tmpUV2 = src->uv0;
			
			src->uv0 = src->uv1;
			src->uv1 = src->uv2;
			src->uv2 = src->uv3;
			src->uv3 = tmpUV2;

			src++;
		}

	}

	// HACK: Show clean model only in Rio.
	//if (GameLevel == 3) 
	//	gFastWheelModelPtr = gCleanWheelModelPtr;
}

// [A] Driver 1 cities: carTpages[0] is the page with the pedestrian textures (their palettes share civ_clut[0]), 1..5 are
// the pages of the cop and civilian car models. A page missing here would share civ_clut[0] with the pedestrians.
void D1SetupCarTpages(char* lump_ptr)
{
	char* pages = carTpages[GameLevel];
	int* offsets = (int*)(lump_ptr + 4);
	char* models = lump_ptr + 4 + 160;
	int tpageNum, texNum;
	int count, model, poly, i;

	memset(civ_clut, 0, sizeof(civ_clut));
	memset(pages, -1, 8);
	memset(specTpages[GameLevel], -1, 12);

	count = 1;
	if (GetTEXINFName("JEANS1", &tpageNum, &texNum))
		pages[0] = tpageNum;

	for (model = 0; model <= 12; model++)
	{
		MODEL* mdl;
		u_char* polys;
		char* spec = model >= 8 ? &specTpages[GameLevel][(model - 8) * 2] : NULL;
		int specCount = 0;

		// 5..7 do not exist: the player cars start at 8
		if ((model > 4 && model < 8) || offsets[model * 3] == -1)
			continue;

		mdl = (MODEL*)(models + offsets[model * 3]);
		polys = GET_RELOC_MODEL_DATA(u_char, mdl, poly_block);

		for (poly = 0; poly < mdl->num_polys; poly++)
		{
			int type = polys[0] & 0x1f;

			if (type >= 20 && type <= 23 && polys[1] != 0xFF)
			{
				if (model >= 8)
				{
					// the two own pages of a player car (the ones in the level's special list), in order of first use
					if (specCount < 2 && IsSpecialPage(polys[1]) && (specCount == 0 || spec[0] != polys[1]))
						spec[specCount++] = polys[1];
				}
				else
				{
					for (i = 0; i < count && pages[i] != polys[1]; i++);

					if (i == count && count < 6)
						pages[count++] = polys[1];
				}
			}

			polys += PolySizes[type];
		}

		if (model >= 8)
			printInfo("D1 player car %d: pages %d, %d\n", model, spec[0], spec[1]);
	}
}

// [D] [T]
void ProcessPalletLump(char *lump_ptr, int lump_size)
{
	ushort clutValue;
	int *buffPtr;
	int texnum;
	int palette;
	u_short *clutTablePtr;
	u_short clutTable[320];
	int tpageindex;
	int total_cluts;
	int clut_number;

	total_cluts = *(int*)lump_ptr;
	
	if (total_cluts == 0)
		return;

	buffPtr = (int*)(lump_ptr + 4);
	clutTablePtr = (u_short*)clutTable;

	while (*buffPtr != -1)
	{
		palette = buffPtr[0];
		texnum = buffPtr[1];
		tpageindex = buffPtr[2];
		clut_number = buffPtr[3];
		buffPtr += 4;

		if (clut_number == -1)
		{
			// store clut
			LoadImage(&clutpos, (u_long*)buffPtr);
			buffPtr += 8;

			clutValue = GetClut(clutpos.x, clutpos.y);
			IncrementClutNum(&clutpos);

			*clutTablePtr++ = clutValue;			
		}
		else
		{
			// use stored clut
			clutValue = clutTable[clut_number];
		}

		civ_clut[GetCarPalIndex(tpageindex)][texnum][palette + 1] = clutValue;
	}
}

// [D] [T]
void DrawCarObject(CAR_MODEL* car, MATRIX* matrix, VECTOR* pos, int palette, CAR_DATA* cp, int detail)
{
	VECTOR modelLocation;
	SVECTOR cog;

	cog = cp->ap.carCos->cog;

	// [A] mini cars correct position
	if (ActiveCheats.cheat13 != 0)
	{
		cog.vx <<= 1;
		cog.vy <<= 1;
		cog.vz <<= 1;
	}

	gte_SetRotMatrix(matrix);
	gte_SetTransVector(pos);

	gte_ldv0(&cog);

	gte_rtv0tr();

	gte_stlvnl(&modelLocation);

	gte_SetTransVector(&modelLocation);

	plotNewCarModel(car, palette);
}

// [D] [T] [A]
void DrawCar(CAR_DATA* cp, int view)
{
	int yVal;
	int maxDamage;
	int WheelSpeed;
	int oboxLenSq;
	CAR_MODEL* CarModelPtr;
	int model;
	CVECTOR col;
	SVECTOR d;
	VECTOR pos, dist;
	VECTOR corners[4];
	MATRIX workmatrix;

	D_CHECK_ERROR(cp < car_data, "Invalid car");

	model = cp->ap.model;

	// draw car lights in for InCar camera
	if (player[view].cameraView == 2 && cp->id == player[view].cameraCarId)
	{
		if (cp->ap.damage[0] < 500)
			LeftLight = 2;
		else if (cp->ap.damage[0] < 1000)
			LeftLight = 1;
		else
			LeftLight = 0;

		if (cp->ap.damage[1] < 500)
			RightLight = 2;
		else if (cp->ap.damage[1] < 1000)
			RightLight = 1;
		else
			RightLight = 0;

		if (!CarLightsOn(cp))
			return;

		col.r = 128;
		col.g = 120;
		col.b = 110;

		PlacePoolForCar(cp, &col, 1, 1);
		return;
	}

	pos.vx = cp->hd.where.t[0];
	pos.vz = cp->hd.where.t[2];
	pos.vy = -cp->hd.where.t[1];

	SetFrustrumMatrix();

	if (FrustrumCheck(&pos, 800) == -1)
		return;

	// corners for frustrum checking of big cars
	corners[0].vx = corners[1].vx = pos.vx + cp->hd.oBox.radii[0].vx;
	corners[0].vx += cp->hd.oBox.radii[2].vx;
	corners[1].vx -= cp->hd.oBox.radii[2].vx;

	corners[2].vx = corners[3].vx = pos.vx - cp->hd.oBox.radii[0].vx;
	corners[2].vx += cp->hd.oBox.radii[2].vx;
	corners[3].vx -= cp->hd.oBox.radii[2].vx;

	corners[0].vz = corners[1].vz = pos.vz + cp->hd.oBox.radii[0].vz;
	corners[0].vz += cp->hd.oBox.radii[2].vz;
	corners[1].vz -= cp->hd.oBox.radii[2].vz;

	corners[2].vz = corners[3].vz = pos.vz - cp->hd.oBox.radii[0].vz;
	corners[2].vz += cp->hd.oBox.radii[2].vz;
	corners[3].vz -= cp->hd.oBox.radii[2].vz;

	corners[3].vy = pos.vy;
	corners[2].vy = pos.vy;
	corners[1].vy = pos.vy;
	corners[0].vy = pos.vy;

	if (FrustrumCheck(corners, 0) == -1 &&
		FrustrumCheck(corners + 1, 0) == -1 &&
		FrustrumCheck(corners + 2, 0) == -1 &&
		FrustrumCheck(corners + 3, 0) == -1)
	{
		return;
	}

	d.vx = cp->hd.oBox.location.vx - camera_position.vx;
	d.vy = -camera_position.vy - cp->hd.oBox.location.vy;
	d.vz = cp->hd.oBox.location.vz - camera_position.vz;

	dist.vx = d.vx * cp->hd.oBox.radii[0].vx + d.vy * cp->hd.oBox.radii[0].vy + d.vz * cp->hd.oBox.radii[0].vz;

	if (dist.vx < 0)
		dist.vx = -dist.vx;

	dist.vy = d.vx * cp->hd.oBox.radii[1].vx + d.vy * cp->hd.oBox.radii[1].vy + d.vz * cp->hd.oBox.radii[1].vz;
	if (dist.vy < 0)
		dist.vy = -dist.vy;

	dist.vz = d.vx * cp->hd.oBox.radii[2].vx + d.vy * cp->hd.oBox.radii[2].vy + d.vz * cp->hd.oBox.radii[2].vz;
	if (dist.vz < 0)
		dist.vz = -dist.vz;

	oboxLenSq = cp->hd.oBox.length[0];

	if (dist.vx < oboxLenSq * oboxLenSq)
	{
		oboxLenSq = cp->hd.oBox.length[1];

		if (dist.vy < oboxLenSq * oboxLenSq)
		{
			oboxLenSq = cp->hd.oBox.length[2];

			if (dist.vz < oboxLenSq * oboxLenSq)
				return;
		}
	}

	pos.vx -= camera_position.vx;
	pos.vy -= camera_position.vy;
	pos.vz -= camera_position.vz;

	Apply_Inv_CameraMatrix(&pos);

	num_cars_drawn++;

#if defined(DEBUG_OPTIONS) && !defined(PSX)
	extern int gCivSpawnFrame[MAX_CARS], gCarPopIns;

	// pos is in view space here: a car behind the camera passes the frustum check but is not seen
	if (cp->controlType == CONTROL_TYPE_CIV_AI && FrameCnt > 100 && FrameCnt - gCivSpawnFrame[cp->id] < 3 && pos.vz > 0)
	{
		gCarPopIns++;
		printInfo("popin: car %d at distance %d, frame %d\n", cp->id, SquareRoot0((camera_position.vx - cp->hd.where.t[0]) / 16 * ((camera_position.vx - cp->hd.where.t[0]) / 16) + (camera_position.vz - cp->hd.where.t[2]) / 16 * ((camera_position.vz - cp->hd.where.t[2]) / 16)) * 16, FrameCnt);
	}
#endif

	MulMatrix0(&inv_camera_matrix, &cp->hd.drawCarMat, &workmatrix);

	// [A] there was mini cars cheat
	// we need full blown mini cars with physics support
	if (ActiveCheats.cheat13 != 0)
	{
		int i;
		for (i = 0; i < 3; i++)
		{
			workmatrix.m[i][0] >>= 1;
			workmatrix.m[i][1] >>= 1;
			workmatrix.m[i][2] >>= 1;
		}
	}

	// to check if car is flipped
	yVal = cp->hd.where.m[1][1];

	// LOD switching
	if (pos.vz <= CAR_LOD_SWITCH_DISTANCE && gForceLowDetailCars == 0 || cp->controlType == CONTROL_TYPE_PLAYER)
	{
		int doSmoke = 0;

		WheelSpeed = cp->hd.speed * 8192;
		maxDamage = MaxPlayerDamage[0];

		if (cp->controlType == CONTROL_TYPE_PLAYER)
		{
			if(*cp->ai.padid >= 0 && *cp->ai.padid < 2)
				maxDamage = MaxPlayerDamage[*cp->ai.padid];
		}

		if (cp->totalDamage >= maxDamage)
		{
			if (WheelSpeed + 59999U < 119999)
				AddFlamingEngine(cp);

			doSmoke = 2;
		}
		else
		{
			if (cp->ap.damage[0] > 2000 || cp->ap.damage[1] > 2000)
			{
				if (cp->ap.damage[0] > 3000 || cp->ap.damage[1] > 3000)
					doSmoke = 2;
				else
					doSmoke = 1;
			}
		}

#ifdef PSX
		if (doSmoke && WheelSpeed + 399999U < 1199999)
#else
		if (doSmoke)	// [A] the smoke stays at speed too (thinner, see Setup_Smoke)
#endif
			AddSmokingEngine(cp, doSmoke - 1, WheelSpeed);

#if ENABLE_GAME_ENCHANCEMENTS
		AddExhaustSmoke(cp, doSmoke > 1, WheelSpeed);
#endif

		SetShadowPoints(cp, corners);
		PlaceShadowForCar(corners, 4, 10, yVal < 0 ? 0 : 2, SunShadow(cp, corners));

		ComputeCarLightingLevels(cp, 1);

		gTempCarUVPtr = gTempHDCarUVDump[cp->id];
		CarModelPtr = &NewCarModel[model];
		CarModelPtr->vlist = gTempCarVertDump[cp->id];
		CarModelPtr->nlist = gTempCarVertDump[cp->id];

		FindCarLightFade(&workmatrix);

#if defined(DEBUG_OPTIONS) && !defined(PSX)
		char* primBefore = (char*)current->primptr;
#endif

#ifndef PSX
		SetupCarGloss(cp, &workmatrix, &pos);
#endif

		DrawCarObject(CarModelPtr, &workmatrix, &pos, cp->ap.palette, cp, 1);

#ifndef PSX
		sGlossOn = 0;
#endif

#if defined(DEBUG_OPTIONS) && !defined(PSX)
		if (current->primptr == primBefore)
			printInfo("ghost car %d model %d type %d (shadow without car), frame %d\n", cp->id, model, cp->controlType, FrameCnt);
#endif

		if (ActiveCheats.cheat13 != 0)
		{
			MulMatrix0(&inv_camera_matrix, &cp->hd.drawCarMat, &workmatrix);
		}

		DrawCarWheels(cp, &workmatrix, &pos, view);
	}
	else
	{
		CarModelPtr = &NewLowCarModel[model];
		CarModelPtr->nlist = gTempCarVertDump[cp->id];

		gTempCarUVPtr = gTempLDCarUVDump[cp->id];

		if (pos.vz < 8000)
		{
			SetShadowPoints(cp, corners);
			PlaceShadowForCar(corners, 0, 0, yVal < 0 ? 0 : 2, SunShadow(cp, corners));
		}

		ComputeCarLightingLevels(cp, 0);

		FindCarLightFade(&workmatrix);

		DrawCarObject(CarModelPtr, &workmatrix, &pos, cp->ap.palette, cp, 0);
	}

	TransparentObject = 0;

#ifndef PSX
	gCarLampsStart = current->primptr;
#endif

	if (cp->controlType == CONTROL_TYPE_PLAYER)
		PlayerCarFX(cp);
	else if (cp->controlType == CONTROL_TYPE_CIV_AI)
		CivCarFX(cp);

	if (CarShowsNightLights(cp))
		AddNightLights(cp);

	if (CarSirenOn(cp))
		AddCopCarLight(cp);
}

#ifndef PSX
char* gCarLampsStart;	// DrawCar: the primitives from here on are the lamps of the car (they fade apart from its body)

// [A] a car beyond the draw distance shows only its lamps at night, like far traffic does; they fade in before its body
void DrawCarLamps(CAR_DATA* cp)
{
	MATRIX workmatrix;
	VECTOR pos;

	if (!cp->ap.carCos || !CarShowsNightLights(cp))
		return;

	pos.vx = cp->hd.where.t[0];
	pos.vy = -cp->hd.where.t[1];
	pos.vz = cp->hd.where.t[2];

	SetFrustrumMatrix();

	if (FrustrumCheck(&pos, 800) == -1)
		return;

	MulMatrix0(&inv_camera_matrix, &cp->hd.drawCarMat, &workmatrix);
	FindCarLightFade(&workmatrix);

	AddNightLights(cp);
}
#endif

// [D] [T]
char GetCarPalIndex(int tpage)
{
	int i;

	for (i = 0; i < 8; i++)
	{
		if (tpage == carTpages[GameLevel][i])
			return i;
	}

	return 0;
}