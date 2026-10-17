#include "driver2.h"
#include "objanim.h"
#include "map.h"
#include "models.h"
#include "texture.h"
#include "debris.h"
#include "civ_ai.h"
#include "main.h"
#include "mission.h"
#include "cars.h"
#include "players.h"
#include "cop_ai.h"
#include "felony.h"
#include "spool.h"
#include "system.h"
#include "pause.h"
#include "draw.h"
#include "camera.h"

// [A] Driver 1 light kinds (ANIMATED_OBJECT::kind)
enum
{
	AO_NONE,
	AO_LAMP,		// one glow at x, y, z
	AO_LAMP2,		// glows at x and at x2 (same y, z)
	AO_SPRITE,		// sprite lamp: glow at the top of the model (y is found at load)
	AO_SIGNAL,		// traffic light: red lens at x, y, z, amber and green follow every extra units lower
	AO_SIGNALH		// horizontal head: amber and green follow every extra units along x; x2 = red lens of a second head
};

struct ANIMATED_OBJECT
{
	int internal_id;
	int model_num;
	char* name;
	char LitPoly;
	char kind;
	short x, y, z, extra, x2;
};

#define D1_LIGHT_MAX_DIST	30000	// lamps and signals further away are not drawn

struct GARAGE_DOOR
{
	CELL_OBJECT* cop;
	VECTOR_NOPAD old_pos;
	VECTOR_NOPAD pos;
	short rotation;
	char yang;
};

struct CYCLE_OBJECT
{
	char* name;
	short vx, vy;
	short start1, stop1, speed1;
	short start2, stop2, speed2;
};

CYCLE_OBJECT Lev0[2] =
{
	{ "REDRVR", 0, 0, 6, 10, 2, 11, 15, 2 },
	{ "NAVPIR34", 0, 0, 0, 6, 0, 7, 13, 0 }
};

CYCLE_OBJECT Lev1[] = {
	{ "DOOR11", 0, 0, 1, 7, 2, 8, 14, 3 },
};

CYCLE_OBJECT Lev2[12] =
{
	{ "DTSYN01", 0, 0, 0, 3, 0, 4, 14, 0 },
	{ "DTSYN02", 0, 0, 0, 15, 0, -1, -1, 0 },
	{ "F-MTSYN2", 0, 0, 0, 3, 0, 10, 14, 0 },
	{ "DTSYN03", 0, 0, 0, 3, 0, -1, -1, 0 },
	{ "SYN-CAS1", 0, 0, 0, 3, 0, 4, 14, 0 },
	{ "SYNSLOT", 0, 0, 0, 3, 0, 4, 7, 0 },
	{ "ENT1B", 0, 0, 0, 15, 0, -1, -1, 0 },
	{ "FLAMINGO", 0, 0, 0, 3, 0, 9, 12, 0 },
	{ "CYCLE-01", 0, 0, 0, 5, 1, 6, 11, 1 },
	{ "CYCLE-02", 0, 0, 0, 3, 3, -1, -1, 0 },
	{ "CYCLE-03", 0, 0, 0, 6, 7, 7, 13, 7 },
	{ "CYCLE-04", 0, 0, 0, 6, 15, 7, 13, 15 }
};

CYCLE_OBJECT Lev3[] = {
	{ "FWING11", 0, 0, 1, 7, 2, 8, 14, 3 }
};


// the per-city tables below stay empty (zeroed) for the Driver 1 cities
CYCLE_OBJECT* Lev_CycleObjPtrs[NUM_CITIES] = {
	Lev0,
	Lev1,
	Lev2,
	Lev3
};

int Num_LevCycleObjs[NUM_CITIES] = { numberOf(Lev0), 0, numberOf(Lev2), 0 };

ANIMATED_OBJECT Lev0AnimObjects[9] =
{
	{ 0, 0, "TLIGHT01", 0 },
	{ 1, 0, "TLIGHT02", 0 },
	{ 2, 0, "SLIGHT01", 1 },
	{ 3, 0, "LOW_SLIGHT01", 1 },
	{ 4, 0, "DLIGHT01", 1 },
	{ 5, 0, "STRUT", 0 },
	{ 6, 0, "NPLIGHT", 0 },
	{ 7, 0, "TLIGHT01_LOW", 0 },
	{ 8, 0, "TLIGHT02_LOW", 0 }
};

ANIMATED_OBJECT Lev1AnimObjects[5] =
{
	{ 0, 0, "TLIGHT1", 0 },
	{ 1, 0, "TLIGHT2", 0 },
	{ 2, 0, "SLIGHT1", 1 },
	{ 3, 0, "SLIGHT2", 1 },
	{ 4, 0, "DLIGHT1", 1 }
};

ANIMATED_OBJECT Lev2AnimObjects[5] =
{
	{ 0, 0, "TLIGHT01", 0 },
	{ 1, 0, "TLIGHT02", 0 },
	{ 2, 0, "SLIGHT01", 1 },
	{ 3, 0, "DLIGHT01", 1 },
	{ 4, 0, "SLIGHT02", 1 }
};

ANIMATED_OBJECT Lev3AnimObjects[4] =
{
	{ 0, 0, "TLIGHT01", 0 },
	{ 1, 0, "TLIGHT02", 0 },
	{ 2, 0, "SLIGHT01", 0 },
	{ 3, 0, "MLIGHT01", 0 }
};

// [A] Driver 1 cities (internal_id = row). Head positions come from the D1 models (y negative = up); the model
// units are not scaled. A vertical signal head is a 142 x 346 box at the arm tip, its lenses are the thirds of its face (z -77)
ANIMATED_OBJECT Lev4AnimObjects[] =		// Miami
{
	{ 0, 0, "SLAMP2", 1, AO_LAMP, -540, -1491, 0 },
	{ 1, 0, "SLAMP3", 1, AO_LAMP2, -579, -1517, 1, 0, 564 },
	{ 2, 0, "SLAMP1", 1, AO_SPRITE, 0, 0, 0 },
	{ 3, 0, "TLIGHT01", 0, AO_SIGNALH, -1472, -1025, -100, 75, -393 },
	{ 4, 0, "TLIGHT02", 0, AO_SIGNALH, -1528, -1026, -70, 75, -448 },
	{ 5, 0, "TLIGHT03", 0, AO_SIGNALH, -1528, -1026, -70, 75, -448 },
	{ 6, 0, "TLIGHT04", 0, AO_SIGNALH, -1528, -1026, -70, 75, -448 },
	{ 7, 0, "TLIGHT05", 0, AO_SIGNALH, -1528, -1026, -70, 75, -448 },
	{ 8, 0, "TLIGHT06", 0, AO_SIGNALH, -1528, -1026, -70, 75, -448 },
	{ 9, 0, "TLIGHT07", 0, AO_SIGNALH, -1528, -1026, -70, 75, -448 },
	{ 10, 0, "TLIGHT08", 0, AO_SIGNALH, -1528, -1026, -70, 75, -448 },
	{ 11, 0, "TLIGHT09", 0, AO_SIGNALH, -1528, -1026, -70, 75, -448 },
	{ 12, 0, "TLIGHT10", 0, AO_SIGNALH, -1528, -1026, -70, 75, -448 },
	{ 13, 0, "TLIGHT11", 0, AO_SIGNALH, -1528, -1026, -70, 75, -448 },
	{ 14, 0, "TLIGHT12", 0, AO_SIGNALH, -1528, -1026, -70, 75, -448 },
	{ 15, 0, "TLIGHT13", 0, AO_SIGNALH, -1528, -1026, -70, 75, -448 },
	{ 16, 0, "TLIGHT14", 0, AO_SIGNALH, -1528, -1026, -70, 75, -448 },
	{ 17, 0, "TLIGHT15", 0, AO_SIGNALH, -1528, -1026, -70, 75, -448 },
	{ 18, 0, "TLIGHT16", 0, AO_SIGNALH, -1528, -1026, -70, 75, -448 },
	{ 19, 0, "TLIGHT17", 0, AO_SIGNALH, -1528, -1026, -70, 75, -448 },
	{ 20, 0, "TLIGHT18", 0, AO_SIGNALH, -1528, -1026, -70, 75, -448 }
};

ANIMATED_OBJECT Lev5AnimObjects[] =		// Frisco
{
	{ 0, 0, "SLIGHT00", 1, AO_LAMP, -712, -2005, -61 },
	{ 1, 0, "SLIGHT01", 1, AO_LAMP, -268, -1760, 0 },
	{ 2, 0, "SLIGHT02", 1, AO_SPRITE, 0, 0, 0 },
	{ 3, 0, "SLIGHT03", 1, AO_SPRITE, 0, 0, 0 },
	{ 4, 0, "TLIGHT00", 0, AO_SIGNAL, -1471, -1470, -77, 115 },
	{ 5, 0, "TLIGHT01", 0, AO_SIGNAL, -1466, -1479, -77, 117 }
};

ANIMATED_OBJECT Lev6AnimObjects[] =		// Los Angeles
{
	{ 0, 0, "SLIGHT00", 1, AO_LAMP, -811, -2330, 0 },
	{ 1, 0, "SLIGHT01", 1, AO_LAMP2, -1080, -2290, 0, 0, 1080 },
	{ 2, 0, "SLIGHT02", 1, AO_LAMP, 0, -3120, 0 },
	{ 3, 0, "TLIGHT00", 0, AO_SIGNAL, -1486, -1469, -77, 115 },
	{ 4, 0, "TLIGHT01", 0, AO_SIGNAL, -1446, -1469, -77, 115 },
	{ 5, 0, "TLIGHT02", 0, AO_SIGNAL, -1486, -1469, -77, 115 },
	{ 6, 0, "TLIGHT03", 0, AO_SIGNAL, -1486, -1469, -77, 115 },
	{ 7, 0, "TLIGHT04", 0, AO_SIGNAL, -1486, -1469, -77, 115 },
	{ 8, 0, "TLIGHT05", 0, AO_SIGNAL, -1486, -1469, -77, 115 },
	{ 9, 0, "TLIGHT06", 0, AO_SIGNAL, -1486, -1469, -77, 115 },
	{ 10, 0, "TLIGHT07", 0, AO_SIGNAL, -1486, -1469, -77, 115 },
	{ 11, 0, "TLIGHT08", 0, AO_SIGNAL, -1486, -1469, -77, 115 },
	{ 12, 0, "TLIGHT09", 0, AO_SIGNAL, -1486, -1469, -77, 115 },
	{ 13, 0, "TLIGHT10", 0, AO_SIGNAL, -1486, -1469, -77, 115 }
};

ANIMATED_OBJECT Lev7AnimObjects[] =		// New York
{
	{ 0, 0, "SLIGHT00", 1, AO_LAMP, -758, -2300, 0 },
	{ 1, 0, "SLIGHT01", 1, AO_LAMP2, -1450, -3868, 1, 0, 1450 },
	{ 2, 0, "SLIGHT02", 1, AO_SPRITE, 0, 0, 0 },
	{ 3, 0, "TLIGHT00", 0, AO_SIGNAL, -1716, -1469, -77, 115 },
	{ 4, 0, "TLIGHT01", 0, AO_SIGNAL, -1716, -1469, -77, 115 }
};

ANIMATED_OBJECT* Level_AnimatingObjectPtrs[NUM_CITIES] = {
	Lev0AnimObjects,
	Lev1AnimObjects,
	Lev2AnimObjects,
	Lev3AnimObjects,
	Lev4AnimObjects,
	Lev5AnimObjects,
	Lev6AnimObjects,
	Lev7AnimObjects
};

int Level_NumAnimatedObjects[NUM_CITIES] = {
	numberOf(Lev0AnimObjects),
	numberOf(Lev1AnimObjects),
	numberOf(Lev2AnimObjects),
	numberOf(Lev3AnimObjects),
	numberOf(Lev4AnimObjects),
	numberOf(Lev5AnimObjects),
	numberOf(Lev6AnimObjects),
	numberOf(Lev7AnimObjects)
};

// sound ids for SOUND_BANK_SFX
SMASHABLE_OBJECT smashable[] =
{
	{ 0, "", 1, 0, 800 },		// default
	{ 0, "CONE_TASTIC", 1, 0, 1400 },
	{ 0, "CONE1_TASTIC", 1, 0, 1400 },
	{ 0, "CONE2_TASTIC", 1, 0, 1400 },
	{ 0, "BOX_TASTIC", 0, -6000, 4096 },
	{ 0, "BOX2_TASTIC", 0, -6000, 4096 },
	{ 0, "BOX3_TASTIC", 0, -6000, 4096 },
	{ 0, "BIN_TASTIC", 3, -6500, 3200 },
	{ 0, "BARRIER_TASTIC", 3, -6500, 3200 },
	{ 0, "BARRIER1_TASTIC", 3, -6500, 3200 },
	{ 0, "BARREL_TASTIC", 2, -3300, 2400 },
	{ 0, "BARREL1_TASTIC", 2, -3300, 2400 },
	{ 0, "BARREL2_TASTIC", 2, -3300, 2400 },
	{ 0, "TABLE_TASTIC", 3, -6500, 3200 },
	{ 0, "BENCH_TASTIC", 3, -6500, 3200 },
	{ 0, "CHAIR_TASTIC", 3, -6500, 3200 },
	{ 0, "CHAIR1_TASTIC", 3, -6500, 3200 },
	{ 0, "CHAIR2_TASTIC", 3, -6500, 3200 },
	{ 0, "UMBRELLA_TASTIC", 3, -6500, 3200 },
	{ 0, "UMBRELLA1_TASTIC", 3, -6500, 3200 },
	{ 0, "PHONE_TASTIC", 2, -3300, 3600 },
	{ 0, "PHONE1_TASTIC", 2, -3300, 3600 },
	{ 0, "SIGN00_TASTIC", 3, -6500, 3200 },
	{ 0, "NEWS_TASTIC", 3, -6500, 3200 },
	{ 0, "PARKING_TASTIC", 2, -7500, 4096 },
	{ 0, "DRINKS_TASTIC", 2, -3300, 3600 },
	{ 0, "FENCE_TASTIC", 3, -6500, 3200 },
	{ 0, "FENCE00_TASTIC", 3, -6500, 3200 },
	{ 0, "FENCE01_TASTIC", 3, -6500, 3200 },
	{ 0, "FENCE02_TASTIC", 3, -6500, 3200 },
	{ 0, "FENCE03_TASTIC", 3, -6500, 3200 },
	{ 0, "FENCE04_TASTIC", 3, -6500, 3200 },
	{ 0, "CONE_TASTIC1", 1, 0, 800 },
	{ 0, "CONE_TASTIC2", 1, 0, 800 },
	{ 0, "CONE_TASTIC3", 1, 0, 800 },
	{ 0, "CONE_TASTIC4", 1, 0, 800 },
	{ 0, 0, 0, 0, 0 }
};

int num_anim_objects = 0;
int num_cycle_obj = 0;

TEXTURE_DETAILS cycle_tex[12];
DR_LOAD cyclecluts[12];

int cycle_phase = 0;
int cycle_timer = 0;

// [D] [T]
void InitCyclingPals(void)
{
	int i;

	// only enabled at Night
	if (gTimeOfDay != TIME_NIGHT)
	{
		num_cycle_obj = 0;
		return;
	}
	
	num_cycle_obj = Num_LevCycleObjs[GameLevel];
	CYCLE_OBJECT* cyc = Lev_CycleObjPtrs[GameLevel];

	for (i = 0; i < num_cycle_obj; i++)
	{
		GetTextureDetails(cyc->name, &cycle_tex[i]);
		cyc++;
	}

	cycle_phase = 0;
	cycle_timer = 0;
}

// [D] [T]
void ColourCycle(void)
{
	ushort temp;
	ushort* bufaddr;
	CYCLE_OBJECT* cyc;
	int i;
	RECT16 vram;

	if (!num_cycle_obj)
	{
		return;
	}

	if (LoadingArea != 0)
	{
		cycle_phase = 0;
		return;
	}

	if (FrameCnt & 1) // [A]
	{
		return;
	}

	vram.w = 16;
	vram.h = 1;
	cyc = Lev_CycleObjPtrs[GameLevel];
		
	for (i = 0; i < num_cycle_obj; i++, cyc++)
	{
		TEXTURE_DETAILS* cycTex = &cycle_tex[i];
		bufaddr = (u_short*)cyclecluts[i].p;

		if (tpageloaded[cycTex->texture_page] == 0)
		{
			continue;
		}

		if (cycle_phase == 0)
		{
			// initialize clut data from VRAM
			temp = texture_cluts[cycTex->texture_page][cycTex->texture_number];

			cyc->vx = vram.x = (temp & 63) << 4;
			cyc->vy = vram.y = (temp >> 6);

			StoreImage(&vram, (u_long*)bufaddr);
		}
		else
		{
			if ((cycle_timer & cyc->speed1) == 0)
			{
				if (cyc->start1 != -1)
				{
					temp = bufaddr[cyc->start1];
					memmove((u_char*)(bufaddr + cyc->start1), (u_char*)(bufaddr + cyc->start1 + 1), (cyc->stop1 - cyc->start1) * sizeof(ushort));

					bufaddr[cyc->stop1] = temp;
				}
			}

			if ((cycle_timer & cyc->speed2) == 0)
			{
				if (cyc->start2 != -1)
				{
					temp = bufaddr[cyc->start2];
					memmove((u_char*)(bufaddr + cyc->start2), (u_char*)(bufaddr + cyc->start2 + 1), (cyc->stop2 - cyc->start2) * sizeof(ushort));

					bufaddr[cyc->stop2] = temp;
				}
			}

			vram.x = cyc->vx;
			vram.y = cyc->vy;

			SetDrawLoad(&cyclecluts[i], &vram);

			addPrim(current->ot, &cyclecluts[i]);
		}
	}

	if (cycle_phase != 0)
	{
		cycle_timer++;
	}
	else
	{
		cycle_phase = 1;
	}
}


// [D] [T]
void FindSmashableObjects(void)
{
	SMASHABLE_OBJECT* sip;

	sip = smashable;
	while (sip->name != NULL)
	{
		sip->modelIdx = FindModelIdxWithName(sip->name);
		sip++;
	}
}

// [A] glow height of a sprite lamp: 20 units below the top vertex of the model (or of the model it is an instance of)
static short SpriteLampHeight(MODEL* model)
{
	int i, top = 0;
	SVECTOR* verts;

	if (model->instance_number != -1 && modelpointers[model->instance_number] != &dummyModel)
		model = modelpointers[model->instance_number];

	verts = GET_MODEL_DATA(SVECTOR, model, vertices);

	for (i = 0; i < model->num_vertices; i++)
	{
		if (verts[i].vy < top)
			top = verts[i].vy;
	}

	return top + 20;
}

// [D] [T]
void InitAnimatingObjects(void)
{
	ANIMATED_OBJECT* aop;

	int loop;
	MODEL* modelPtr;

	num_anim_objects = Level_NumAnimatedObjects[GameLevel];
	aop = Level_AnimatingObjectPtrs[GameLevel];

	for (loop = 0; loop < num_anim_objects; loop++)
	{
		// My way
		int model_idx = FindModelIdxWithName(aop->name);
		aop->model_num = model_idx;

		if (model_idx != -1 && modelpointers[model_idx] != &dummyModel)
		{
			modelPtr = modelpointers[model_idx];
			modelPtr->flags2 |= MODEL_FLAG_ANIMOBJ;

			if (aop->kind == AO_SPRITE)
				aop->y = SpriteLampHeight(modelPtr);

#ifdef DYNAMIC_LIGHTING
			if (gEnableDlights)
			{
				modelPtr->bounding_sphere <<= 3;
			}
#endif // DYNAMIC_LIGHTING

			if (aop->LitPoly)
				modelPtr->flags2 |= MODEL_FLAG_LAMP;

			// [A] store animated object number in normals pointer
			// after all it was always unused
			modelPtr->tri_verts = loop;

			if (modelPtr->instance_number != -1 &&
				modelpointers[modelPtr->instance_number] != &dummyModel)
			{
				modelPtr = modelpointers[modelPtr->instance_number];
				modelPtr->flags2 |= MODEL_FLAG_ANIMOBJ;

#ifdef DYNAMIC_LIGHTING
				if (gEnableDlights)
				{
					modelPtr->bounding_sphere <<= 3;
				}
#endif // DYNAMIC_LIGHTING

				if (aop->LitPoly)
					modelPtr->flags2 |= MODEL_FLAG_LAMP;

				// [A] store animated object number in normals pointer
				// after all it was always unused
				modelPtr->tri_verts = loop;
			}
		}

		aop++;
	}

	FindSmashableObjects();
}

// [D] [T]
void InitSpooledAnimObj(int model_number)
{
	int i;
	ANIMATED_OBJECT* aop;
	MODEL* modelPtr;

	if (model_number == -1)
		return;

	aop = Level_AnimatingObjectPtrs[GameLevel];

	// Reflections way
	for (i = 0; i < num_anim_objects; i++)
	{
		if (aop->model_num == model_number)
		{
			modelPtr = modelpointers[model_number];
			modelPtr->flags2 |= MODEL_FLAG_ANIMOBJ;

			if (aop->LitPoly)
				modelPtr->flags2 |= MODEL_FLAG_LAMP;

#ifdef DYNAMIC_LIGHTING
			if (gEnableDlights)
			{
				modelPtr->bounding_sphere <<= 3;
			}
#endif // DYNAMIC_LIGHTING

			// [A] store animated object number in normals pointer
			// after all it was always unused
			modelPtr->tri_verts = i;
			
			break;
		}
		aop++;
	}
}

GARAGE_DOOR CurrentGarage;

// [D] [T]
void int_garage_door(void)
{
	CurrentGarage.cop = NULL;
}

// [A] Driver 1 street lamp glow or traffic light lens, by the table row of the model
static void AnimateD1Light(CELL_OBJECT* cop, const ANIMATED_OBJECT* ao)
{
	int yang, phase;
	int maxDist = MIN(ViewDrawDistance(), D1_LIGHT_MAX_DIST);
	long long dx = cop->pos.vx - camera_position.vx;
	long long dz = cop->pos.vz - camera_position.vz;

	if (dx * dx + dz * dz > (long long)maxDist * maxDist)
		return;

	if (ao->kind == AO_SIGNAL || ao->kind == AO_SIGNALH)
	{
		yang = cop->yang * 64;
		phase = junctionLightsPhase[(yang + 128 >> 10) & 1];

		// 1 red, 2 amber, 3 green: the flag is 0x200, 0x400, 0x800
		if (phase >= 1 && phase <= 3)
		{
			int step = (phase - 1) * ao->extra;
			int dx = ao->kind == AO_SIGNALH ? step : 0;
			int dy = ao->kind == AO_SIGNALH ? 0 : step;

			AddTrafficLight(cop, ao->x + dx, ao->y + dy, ao->z, 0x100 << phase, yang);

			if (ao->x2)
				AddTrafficLight(cop, ao->x2 + dx, ao->y + dy, ao->z, 0x100 << phase, yang);
		}

		return;
	}

	if (gLightsOn == 0)
		return;

	AddLightEffect(cop, ao->x, ao->y, ao->z, 0, 3);

	if (ao->kind == AO_LAMP2)
		AddLightEffect(cop, ao->x2, ao->y, ao->z, 0, 3);
}

// [A] [T]
void DrawAllAnimatingObjects(CELL_OBJECT** objects, int num_animated)
{
	ANIMATED_OBJECT* aop;
	CELL_OBJECT* cop;
	MODEL* model;
	int i, j;
	int type;

	aop = Level_AnimatingObjectPtrs[GameLevel];

	for (i = 0; i < num_animated; i++)
	{
		cop = objects[i];
		type = cop->type;
		model = modelpointers[type];

#ifndef PSX
		// [A] lamp glows and traffic light lenses fade out towards the distance where they stop being drawn
		char* fadeStart = current->primptr;
		long long dx = cop->pos.vx - camera_position.vx;
		long long dz = cop->pos.vz - camera_position.vz;
		int lampDist = IS_DRIVER1_CITY(GameLevel) ? MIN(ViewDrawDistance(), D1_LIGHT_MAX_DIST) : LampDrawDistance();
#endif

		// [A] optimized
		if (IS_DRIVER1_CITY(GameLevel))
			AnimateD1Light(cop, &aop[model->tri_verts & 31]);
		else
			animate_object(cop, aop[model->tri_verts & 31].internal_id);

#ifndef PSX
		SetObjectFade(fadeStart, current->primptr, ObjectFade((int)sqrt((double)(dx * dx + dz * dz)), lampDist, -1));
#endif
	}
}


// [D] [T]
void animate_object(CELL_OBJECT* cop, int type)
{
	char phase;
	int yang;

	yang = cop->yang * 64;

	if (GameLevel == 0)
	{
		switch (type)
		{

			case 0:
			case 7:
				yang += 1024;
				phase = junctionLightsPhase[yang + 128 >> 10 & 1];

				if (phase == 1)
				{
					AddTrafficLight(cop, 0, -708, -45, 0x200, yang);
				}
				else if (phase == 2)
				{
					AddTrafficLight(cop, 0,  -644, -45, 0x400, yang);
				}
				else if (phase == 3)
				{
					AddTrafficLight(cop, 0, -580, -45, 0x800, yang);
				}
				break;
			case 1:
			case 8:
				phase = junctionLightsPhase[yang + 128 >> 10 & 1];

				if (phase == 1)
				{
					AddTrafficLight(cop, 406, -708, -46, 0x200, yang);
				}
				else if (phase == 2)
				{
					AddTrafficLight(cop, 406, -658, -46, 0x400, yang);
				}
				else if (phase == 3)
				{
					AddTrafficLight(cop, 406, -594, -46, 0x800, yang);
				}
				break;
			case 2:
			case 3:
				if (gLightsOn == 0)
					break;

				AddLightEffect(cop, 429, -1234, 0, 0, 3);
				break;
			case 4:
				if (gLightsOn == 0)
					break;

				AddLightEffect(cop, -432, -1241, 0, 0, 3);
				AddLightEffect(cop, 432, -1241, 0, 0, 3);
				break;
			case 5:
				if (gLightsOn == 0)
					break;

				AddLightEffect(cop, 0, -80, 0, 2, 2);
				break;
			case 6:
				if (gLightsOn == 0)
					break;

				AddLightEffect(cop, 234, -1146, 0, 0, 3);
				AddLightEffect(cop, -234, -1146, 0, 0, 3);
		}
	}
	else if (GameLevel == 1)
	{
		switch (type)
		{
			case 0:
				phase = junctionLightsPhase[yang + 128 >> 10 & 1];

				if (phase == 2)
				{
					AddTrafficLight(cop, -417, -800, -30, 0x400, yang);
				}
				else if (phase == 1)
				{
					AddTrafficLight(cop, -490, -800, -30, 0x200, yang);
				}
				else if (phase == 3)
				{
					AddTrafficLight(cop, -343, -800, -30, 0x800, yang);
				}

				break;
			case 2:
				if (gLightsOn == 0)
					break;

				AddSmallStreetLight(cop, 230, -1090, 0, 0);
				break;
			case 3:
				if (gLightsOn == 0)
					break;

				AddLightEffect(cop, -613, -2002, 0, 0, 3);
				break;
			case 4:
				if (gLightsOn == 0)
					break;

				AddLightEffect(cop, -592, -2000, 0, 0, 3);
				AddLightEffect(cop, 594, -2000, 0, 0, 3);
				break;
		}
	}
	else if (GameLevel == 2)
	{
		switch (type)
		{
			case 0:
				phase = junctionLightsPhase[yang + 128 >> 10 & 1];
			
				if (phase == 1)
				{
					AddTrafficLight(cop, -2135, -1112, -68, 0x200, yang);
					AddTrafficLight(cop, -1312, -1057, -68, 0x200, yang);
					AddTrafficLight(cop, -514, -1024, -68, 0x200, yang);
				}
				else if (phase == 2)
				{
					AddTrafficLight(cop, -2140, -1021, -65, 0x400, yang);
					AddTrafficLight(cop, -1310, -980, -65, 0x400, yang);
					AddTrafficLight(cop, -520, -939, -65, 0x400, yang);
				}
				else if (phase == 3)
				{
					AddTrafficLight(cop, -2138, -937, -62, 0x800, yang);
					AddTrafficLight(cop, -1309, -897, -62, 0x800, yang);
					AddTrafficLight(cop, -518, -851, -62, 0x800, yang);
				}
				break;
			case 1:
				phase = junctionLightsPhase[yang + 128 >> 10 & 1];

				if (phase == 1)
				{
					AddTrafficLight(cop, -4, -537, -41, 0x200, yang);
				}
				else if (phase == 2)
				{
					AddTrafficLight(cop, -4, -466, -41, 0x400, yang);
				}
				else if (phase == 3)
				{
					AddTrafficLight(cop, -4, -395, -41, 0x800, yang);
				}
				break;
			case 2:
				if (gLightsOn == 0)
					break;

				AddLightEffect(cop, -840, -1972, -61, 0, 3);
				break;
			case 3:
				if (gLightsOn == 0)
					break;

				AddLightEffect(cop, -865, -2249, 0, 0, 3);
				AddLightEffect(cop, 865, -2249, 0, 0, 3);
				break;
			case 4:
				if (gLightsOn == 0)
					break;

				// spooled?
				if (cop->pos.vx - 137190U < 50687 && cop->pos.vz > 713372 && cop->pos.vz < 719516)
				{
					AddSmallStreetLight(cop, -620, -101, 0, 0);
					break;
				}
			
				AddLightEffect(cop, -620, -1618, 0, 0, 3);
				break;
		}
	}
	else if (GameLevel == 3)
	{
		switch (type)
		{
			case 0:
				phase = junctionLightsPhase[yang + 128 >> 10 & 1];
			
				if (phase == 1)
				{
					AddTrafficLight(cop, -719, -906, -22, 0x200, yang);
				}
				else if (phase == 2)
				{
					AddTrafficLight(cop, -719, -837, -22, 0x400, yang);
				}
				else if (phase == 3)
				{
					AddTrafficLight(cop, -719, -762, -22, 0x800, yang);
				}
				break;
			case 1:
				phase = junctionLightsPhase[yang + 128 >> 10 & 1];

				if (phase == 1)
				{
					AddTrafficLight(cop, 0, -654, -21, 0x200, yang);
				}
				else if (phase == 2)
				{
					AddTrafficLight(cop, 0, -578, -21, 0x400, yang);
				}
				else if (phase == 3)
				{
					AddTrafficLight(cop, 0, -509, -21, 0x800, yang);
				}
				break;
			case 2:
				if (gLightsOn == 0)
					break;

				AddLightEffect(cop, -497, -1437, 0, 0, 3);

				break;
			case 3:
				if (gLightsOn == 0)
					break;

				AddLightEffect(cop, 0,  -2727, 0, 0, 3);
			
				break;
			case 4:
				if (gLightsOn == 0)
					break;

				AddSmallStreetLight(cop, 0, -1170, 0, 1);
				break;
		}
	}
}


// [D] [T]
void animate_garage_door(void)
{
	short* felony;
	int dz;
	int dx;

	if (CurrentGarage.cop == NULL)
		return;

	int playerCarId = MainPlayer.playerCarId;

	if (gCurrentMissionNumber != 53 && CopsCanSeePlayer)
	{
		felony = GetPlayerFelony(&MainPlayer);

		if (*felony > FELONY_PURSUIT_MIN_VALUE)
		{
			CurrentGarage.cop->pos = CurrentGarage.old_pos;
			CurrentGarage.cop = NULL;
			return;
		}
	}

	dx = ABS(CurrentGarage.old_pos.vx - car_data[playerCarId].hd.where.t[0]); // [A] bug fix
	dz = ABS(CurrentGarage.old_pos.vz - car_data[playerCarId].hd.where.t[2]);

	if (dx + dz > 6000 || gStopPadReads != 0)
	{
		if (CurrentGarage.pos.vy < CurrentGarage.old_pos.vy)
		{
			CurrentGarage.rotation -= 25;
			CurrentGarage.pos.vy += 12;
		}
	}
	else
	{
		if (dx + dz < 4000 && CurrentGarage.pos.vy > CurrentGarage.old_pos.vy - 400)
		{
			CurrentGarage.rotation += 25;
			CurrentGarage.pos.vy -= 12;
		}
	}
}





