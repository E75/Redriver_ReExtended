#include "driver2.h"
#include "camera.h"
#include "convert.h"
#include "event.h"
#include "mission.h"
#include "glaunch.h"
#include "cutscene.h"
#include "main.h"
#include "cars.h"
#include "pause.h"
#include "director.h"
#include "dr2roads.h"
#include "system.h"
#include "objcoll.h"
#include "pad.h"
#include "players.h"
#include "draw.h"
#include "cell.h"
#include "models.h"
#include "map.h"
#include "pedest.h"
#include "cell.h"

#ifndef PSX
#include "PsyX/PsyX_public.h"

static int camMouseOn = 0;

// the mouse is captured (relative mode) only while the orbit camera runs, so menus keep a normal cursor
static void CamMouseRead(int* dx, int* dy)
{
	if (!PsyX_GetMouseDelta(dx, dy))
	{
		PsyX_SetCursorRelative(1);
		*dx = 0;
		*dy = 0;
	}

	camMouseOn = 1;
}

static void CamMouseRelease(void)
{
	if (camMouseOn)
	{
		PsyX_SetCursorRelative(0);
		camMouseOn = 0;
	}
}
#else
#define CamMouseRead(dx, dy)
#define CamMouseRelease()
#endif

VECTOR gCameraOffset = { 0};
VECTOR camera_position = { 0, 380, 0, 0 };
SVECTOR camera_angle = { 0,0,0 };

MATRIX camera_matrix;

MATRIX face_camera_work =
{
	{ 
		{ 4096, 0, 0 }, 
		{ 0, 4096, 0 },
		{ 0, 0, 4096 } 
	},
	{ 0, 0, 0 } 
};

char old_camera_change = 0;
char camera_change = 0;

unsigned short paddCamera;
char cameraview = 0;
int CameraCnt = 0;

static LONGVECTOR4 basePos; // [A]
static int baseDir = 0;

char tracking_car = 0;

int gCameraAngle = 2048; // offset 0xAA104

int TargetCar = 0;
int CameraCar = 0;

// [A] custom function - recalculates base camera position (separated from InitCamera)
void CalcCameraBasePos(PLAYER* lp)
{
	CAR_COSMETICS* car_cos;
	EVENT* event;
	SVECTOR boxDisp = { 0 };

	if (lp->cameraCarId < 0)
	{
		if (lp->cameraCarId < -1)
		{
			event = events.track[-2 - lp->cameraCarId];

			basePos[0] = event->position.vx;
			basePos[1] = -event->position.vy;
			basePos[2] = event->position.vz;
			baseDir = event->rotation + 0x400U & 0xfff;

			EventCameraOffset(&boxDisp);	// [A]

			basePos[0] += boxDisp.vx;
			basePos[1] += boxDisp.vy - gCameraOffset.vy;
			basePos[2] += boxDisp.vz;
		}
		else
		{
			basePos[0] = lp->pos[0];
			basePos[1] = lp->pos[1];
			basePos[2] = lp->pos[2];
			baseDir = lp->dir;
		}
	}
	else
	{
		// [A]
		gte_SetRotMatrix(&car_data[lp->cameraCarId].hd.where);
		gte_SetTransMatrix(&car_data[lp->cameraCarId].hd.where);

		// [A]
		car_cos = car_data[lp->cameraCarId].ap.carCos;
		if (car_cos)
		{
			boxDisp.vx = -car_cos->cog.vx;
			boxDisp.vy = -car_cos->cog.vy;
			boxDisp.vz = -car_cos->cog.vz;
		}

		gte_ldv0(&boxDisp);

		gte_rtv0tr();
		gte_stlvnl(basePos);

		if(gInGameCutsceneActive == 0)
			basePos[1] -= gCameraOffset.vy;

		baseDir = car_data[lp->cameraCarId].hd.direction;
	}
}

// [D] [T]
void InitCamera(PLAYER *lp)
{
	if (events.cameraEvent == NULL) 
	{
		paddCamera = Pads[lp->padid].mapped;

		if (NoPlayerControl != 0 || gInGameCutsceneActive != 0)
		{
			lp->cameraView = cameraview;
		}

		CalcCameraBasePos(lp);

		if (lp->cameraView != 0)
			CamMouseRelease();

		if (lp->cameraView == 0)
		{
			if (FrameCnt == 0)
			{
				lp->cameraDist = 1000;

				gCameraAngle = 2048;
				gCameraMaxDistance = 0;
				
				gCameraOffset.vx = 0;
				gCameraOffset.vy = 0;
				gCameraOffset.vz = 0;
			}
			else
			{
				PlaceCameraFollowCar(lp);
			}
		}
		else if (lp->cameraView == 1)
		{
			if (NoPlayerControl == 0 && gInGameCutsceneActive == 0)
				tracking_car = 1;

			PlaceCameraAtLocation(lp, 1);
		}
		else if (lp->cameraView == 2)
		{
			PlaceCameraInCar(lp, 0);
		}
		else if (lp->cameraView == 5)
		{
			if (NoPlayerControl == 0)
			{
				if (gInGameCutsceneActive == 0)
					tracking_car = 1;

				TargetCar = 0;
			}

			PlaceCameraAtLocation(lp, 0);
		}

		// "Beat the Train" hack
		if (gCurrentMissionNumber == 22 && 
			lp->cameraPos.vx < 0 && 
			lp->cameraPos.vz < 793671 && lp->cameraPos.vz > 700000)
		{
			SetSpecialCamera(SPECIAL_CAMERA_SET, 0);
		}

		camera_position.vx = lp->cameraPos.vx;
		camera_position.vy = lp->cameraPos.vy;
		camera_position.vz = lp->cameraPos.vz;

		CameraCar = lp->cameraCarId;

		lp->snd_cam_ang = camera_angle.vy;
	}
	else 
	{
		BuildWorldMatrix();
	}

#ifndef PSX
	extern void BuildFreeCameraMatrix();
	BuildFreeCameraMatrix();
#endif
}

// [D] [T]
void ModifyCamera(void)
{
	// camera types which can be chosen during game
	static char inGameCamera[4] = {
		1,0,2,1
	};

	PLAYER *lp;
	char *pNextCameraView;
	int angle;
	int length;

	int i;

	if (NoPlayerControl || cameraview == 6 || events.cameraEvent != NULL) // || NumPlayers > 1)	// [A] allow switching cameras in multiplayer
		return;

	for(i = 0; i < NumPlayers; i++)
	{
		lp = &player[i];

		if (Pads[i].mapped & 0x100)
		{
			// [A] if only new button is pressed
			if (lp->viewChange != FrameCnt-1 && (Pads[i].mapnew & 0x100))
			{
				pNextCameraView = inGameCamera;

				while (*pNextCameraView++ != lp->cameraView)
				{
					if (pNextCameraView > inGameCamera + 2)
						break;
				}
			
				lp->cameraView = *pNextCameraView;
				lp->viewChange = FrameCnt;
			}

			if (lp->cameraView == 1)
			{
				if (lp->cameraCarId < 1)
					length = 360;
				else
					length = car_data[lp->cameraCarId].ap.carCos->colBox.vz;

				CalcCameraBasePos(lp);

				angle = baseDir + 2048;
				lp->cameraPos.vx = basePos[0] + FIXEDH(RSIN(angle) * (length - 60));
				lp->cameraPos.vz = basePos[2] + FIXEDH(RCOS(angle) * (length - 60));
			}
		}
	}
}

// [D] [T]
int CameraCollisionCheck(void)
{
	int sphere;
	PACKED_CELL_OBJECT *ppco;
	CELL_OBJECT tempCO;
	MODEL *model;
	MATRIX2* mat;
	int zd;
	int sizey;
	int xd;
	int *boxptr;
	int sizez;
	int sizex;
	COLLISION_PACKET *collide;
	int count;
	CELL_ITERATOR ci;
	int box_loop;
	int num_cb;
	int cellx;
	int cellz;
	int cellLevel;

	count = 0;
	sphere = scr_z * 3 - 382;

	cellLevel = events.camera ? events.draw : -1;

	do {
		cellx = (camera_position.vx + (count % 3) * MAP_REGION_SIZE*MAP_REGION_SIZE - MAP_REGION_SIZE*MAP_REGION_SIZE + units_across_halved) / MAP_CELL_SIZE;
		cellz = (camera_position.vz + (count / 3) * MAP_REGION_SIZE*MAP_REGION_SIZE - MAP_REGION_SIZE*MAP_REGION_SIZE + units_down_halved) / MAP_CELL_SIZE;

		if (gCameraDistance > 0)
		{
			for (ppco = GetFirstPackedCop(cellx, cellz, &ci, 0, cellLevel); ppco; ppco = GetNextPackedCop(&ci))
			{
				int type = (ppco->value >> 6) | ((ppco->pos.vy & 1) << 10);

				model = modelpointers[type];
				boxptr = GET_MODEL_DATA(int, model, collision_block);

				if (boxptr != NULL && (model->flags2 & MODEL_FLAG_SMASHABLE) == 0)
				{
					QuickUnpackCellObject(ppco, &ci.nearCell, &tempCO);

					xd = ABS(tempCO.pos.vx - camera_position.vx);
					zd = ABS(tempCO.pos.vz - camera_position.vz);

					num_cb = *boxptr;
					collide = (COLLISION_PACKET *)(boxptr + 1);
					if (xd * xd + zd * zd < model->bounding_sphere * model->bounding_sphere + sphere * sphere) 
					{
						box_loop = 0;

						while (box_loop < num_cb)
						{
							VECTOR cam_vec;
							VECTOR offset;

							cam_vec.vx = camera_position.vx - tempCO.pos.vx;
							cam_vec.vy = camera_position.vy - tempCO.pos.vy;
							cam_vec.vz = camera_position.vz - tempCO.pos.vz;

							mat = &matrixtable[tempCO.yang];

							offset.vx = FIXED(cam_vec.vx * mat->m[0][0] + cam_vec.vz * mat->m[2][0]) - collide->xpos;
							offset.vy = cam_vec.vy - collide->ypos;
							offset.vz = FIXED(cam_vec.vx * mat->m[0][2] + cam_vec.vz * mat->m[2][2]) - collide->zpos;

							if (collide->yang != 0)
							{
								mat = &matrixtable[collide->yang];
								offset.vx = offset.vx * mat->m[0][0] + offset.vz * mat->m[2][0];
								offset.vz = offset.vx * mat->m[0][2] + offset.vz * mat->m[2][2];
							}

							if (collide->xsize < 500 && collide->zsize < 500)
								break;

							sizex = (collide->xsize / 2) + sphere;
							sizey = (collide->ysize / 2) + sphere;
							sizez = (collide->zsize / 2) + sphere;

							if (-sizex < offset.vx && offset.vx < sizex &&
								-sizez < offset.vz && offset.vz < sizez &&
								-sizey < offset.vy && offset.vy < sizey)
							{
								return 1;
							}

							box_loop++;
							collide++;
						}
					}
				}
			}
		}
		count++;

		if (count > 8)
			break;

	} while (true);

	return 0;
}

// [D] [T]
void TurnHead(PLAYER *lp)
{
	LPPEDESTRIAN pPlayerPed;
	
	pPlayerPed = lp->pPed;

	// [A] handle REDRIVER2 dedicated look back button
	if ((paddCamera & CAMERA_PAD_LOOK_BACK) == CAMERA_PAD_LOOK_BACK || (paddCamera & CAMERA_PAD_LOOK_BACK_DED))
	{
		if (pPlayerPed != NULL)  // look back
			pPlayerPed->head_rot = 0;
	}
	else if (paddCamera & CAMERA_PAD_LOOK_LEFT)
	{
		if (pPlayerPed != NULL)
			pPlayerPed->head_rot = 512;

		if (lp->headTimer > 1)
			lp->headTarget = 0x4000000;
		else
			lp->headTimer++;
	}
	else if (paddCamera & CAMERA_PAD_LOOK_RIGHT)
	{
		if (pPlayerPed != NULL)
			pPlayerPed->head_rot = -512;

		if (lp->headTimer > 1)
			lp->headTarget = -0x4000000;
		else
			lp->headTimer++;
	}
	else
	{
		if (pPlayerPed != NULL)
			pPlayerPed->head_rot = 0;

		lp->headTimer = 0;
		lp->headTarget = 0;
	}

	lp->headPos += (lp->headTarget - lp->headPos) / 2;
}

// [A] right stick orbit around the car with a smooth return (plans/camera-orbit.md)
#define CAMORBIT_DEADZONE		28		// of 128
#define CAMORBIT_YAW_RATE		44		// units per tick at full deflection
#define CAMORBIT_PITCH_RATE		24		// units per tick at full deflection
#define CAMORBIT_PITCH_MIN		-120	// camera lower than the default: about -10 degrees
#define CAMORBIT_PITCH_MAX		380		// camera higher than the default: about 33 degrees
#define CAMORBIT_RETURN_RAMP	12		// ticks over which the return speed builds up
#define CAMORBIT_RETURN_TAU		12		// ticks, time constant of the return
#define CAMORBIT_SNAP			8		// units, snap to zero below this
#define CAMORBIT_LOOK_BACK		2048	// look back offset: half a turn
#define CAMORBIT_MOUSE_YAW		3		// units per mouse pixel at 100% (4096 = 360 degrees)
#define CAMORBIT_MOUSE_PITCH	2		// units per mouse pixel at 100%
#define CAMORBIT_MOUSE_MAX		600		// units per tick, so a hitch cannot spin the camera

int gCameraMouse = 1;					// config.ini cameraMouse: mouse look
int gCameraMouseSens = 100;				// config.ini cameraMouseSensitivity, percent
int gCameraOrbit = 1;				// Options - Gameplay - Secret, saved in the profile
int gCameraZoom = 0;				// Extended Settings: index of the follow camera distance 1.0 / 1.2 / 1.5 / 1.8 / 2.0, saved in the profile

#ifndef PSX
extern int gChopper;

// follow camera distance in percent; the chopper flies with at least 1.4 so it stays in view
static int CameraZoomPercent(PLAYER* lp)
{
	static const int percent[5] = { 100, 120, 150, 180, 200 };
	int zoom = CAMERA_DISTANCE_MENU ? percent[gCameraZoom] : 100;

	if (IS_DRIVER1_CITY(GameLevel))
		zoom = zoom * D1_CAMERA_ZOOM_PERCENT / 100;

	if (gChopper && lp == &player[0])
		zoom = MAX(zoom, 180);

	return zoom;
}
#endif
int gCameraReturnDelay = 1000;			// config.ini cameraReturnDelayMs: wait before the camera returns
int gCameraStickSens = 100;				// config.ini cameraStickSensitivity, percent
int gCameraInvertY = 0;					// config.ini cameraInvertY
int gCameraAutoCenter = 1;				// config.ini cameraAutoCenter: 0 keeps the offset until look back is pressed

static int camOrbitReturnW = 0;			// last return weight (0..16), for the test log
static int camOrbitActive = 0;			// the orbit camera handled this tick (else look back snaps like the original)

#if defined(DEBUG_OPTIONS) && !defined(PSX)
int gCamTest = 0;						// -camtest: scripted stick input, logs CAMTEST lines and exits
static int camTestTick = -1;
static int camTestRx, camTestRy, camTestBack;

// {ticks, stick x, stick y, look back held}
static const int camTestScript[][4] = {
	{ 45, 100, 0, 0 }, { 160, 0, 0, 0 },									// A: hold right, release, wait for the return
	{ 45, 100, 0, 0 }, { 20, 0, 0, 0 }, { 20, -100, 0, 0 }, { 160, 0, 0, 0 },	// B: new input aborts the return
	{ 60, 20, 0, 0 }, { 30, 0, 0, 0 },										// C: inside the deadzone, nothing may move
	{ 50, 0, -100, 0 }, { 160, 0, 0, 0 },									// D: stick up: camera lower, release
	{ 50, 0, 100, 0 }, { 160, 0, 0, 0 },									// E: stick down: camera higher, release
	{ 30, 0, 0, 1 }, { 60, 0, 0, 0 },										// F: look back eases to half a turn and back
	{ 20, 100, 0, 0 }, { 40, 0, 0, 1 }, { 160, 0, 0, 0 },					// G: look back while turned: yaw eases to 0
};

int gCamTestPad = 0;					// pad bits the on foot test presses (added to the real pad in ProcessTannerPad)

// on foot: {ticks, stick x, pad bits}: turn the camera, walk forward, right, back (all relative to the camera)
static const int camTestFootScript[][3] = {
	{ 60, 100, 0 }, { 150, 0, MPAD_D_UP }, { 150, 0, MPAD_D_RIGHT }, { 60, 0, 0 }, { 150, 0, MPAD_D_DOWN }, { 60, 0, 0 },
};

static int CamTestFootStep(void)
{
	int i, t;

	if (camTestTick < 0)
	{
		if (FrameCnt < 90)
			return 1;

		camTestTick = 0;
	}

	t = camTestTick++;

	for (i = 0; i < (int)(sizeof(camTestFootScript) / sizeof(camTestFootScript[0])); i++)
	{
		if (t < camTestFootScript[i][0])
		{
			camTestRx = camTestFootScript[i][1];
			gCamTestPad = camTestFootScript[i][2];
			return 1;
		}

		t -= camTestFootScript[i][0];
	}

	return 0;
}

// sets the scripted input of this tick, returns 0 when the script is over
static int CamTestStep(void)
{
	int i, t;

	if (camTestTick < 0)
	{
		if (FrameCnt < 90)
			return 1;

		camTestTick = 0;
	}

	t = camTestTick++;

	for (i = 0; i < (int)(sizeof(camTestScript) / sizeof(camTestScript[0])); i++)
	{
		if (t < camTestScript[i][0])
		{
			camTestRx = camTestScript[i][1];
			camTestRy = camTestScript[i][2];
			camTestBack = camTestScript[i][3];
			return 1;
		}

		t -= camTestScript[i][0];
	}

	return 0;
}
#endif

// one step of an exponential return with a soft start, w = 1..16
static int CamOrbitReturn(int v, int w)
{
	int d = v * w / (16 * CAMORBIT_RETURN_TAU);

	if (d == 0)
		d = (v > 0) ? 1 : -1;

	v -= d;

	return (ABS(v) <= CAMORBIT_SNAP) ? 0 : v;
}

// moves a third of the way to the target per tick, snaps when close
static int CamOrbitEase(int v, int target)
{
	v += (target - v) / 3;

	return (ABS(target - v) <= CAMORBIT_SNAP) ? target : v;
}

static void UpdateCameraOrbit(PLAYER* lp)
{
	int rx, ry, mag, f;
	int mx, my, dyaw, dpitch, useMouse;
	int moved, lookHeld, lookBack, enabled;

	rx = 0;
	ry = 0;
	mx = 0;
	my = 0;
	moved = 0;
	enabled = gCameraOrbit;

#if defined(DEBUG_OPTIONS) && !defined(PSX)
	if (gCamTest)
		enabled = 1;
#endif

	camOrbitActive = 0;

	// on foot (cameraCarId -1) the orbit works too; below -1 the camera sits on an event
	if (!enabled || NoPlayerControl || gInGameCutsceneActive || events.cameraEvent != NULL || lp->padid < 0 || lp->cameraCarId < -1)
	{
		lp->camYaw = 0;
		lp->camPitch = 0;
		lp->camLook = 0;
		lp->camIdle = 0;
		CamMouseRelease();
		return;
	}

	camOrbitActive = 1;

	useMouse = gCameraMouse && lp->padid == 0;

#if defined(DEBUG_OPTIONS) && !defined(PSX)
	// automated test runs must not grab the mouse of the person at the desk
	extern int gShotFrame;
	if (gCamTest || gShotFrame)
		useMouse = 0;
#endif

	if (useMouse)
		CamMouseRead(&mx, &my);
	else
		CamMouseRelease();

	lookBack = (paddCamera & CAMERA_PAD_LOOK_BACK) == CAMERA_PAD_LOOK_BACK || (paddCamera & CAMERA_PAD_LOOK_BACK_DED);
	lookHeld = (paddCamera & (CAMERA_PAD_LOOK_LEFT | CAMERA_PAD_LOOK_RIGHT | CAMERA_PAD_LOOK_BACK_DED)) != 0;

#if defined(DEBUG_OPTIONS) && !defined(PSX)
	if (gCamTest)
	{
		if (lp->cameraCarId < 0)
		{
			if (!CamTestFootStep())
				exit(0);	// graceful, so the log is flushed

			rx = camTestRx;
			ry = 0;
			lookBack = lookHeld = 0;
		}
		else
		{
			if (!CamTestStep())
				exit(0);

			rx = camTestRx;
			ry = camTestRy;
			lookBack = lookHeld = camTestBack;
		}
	}
	else
#endif
	if (Pads[lp->padid].type == 2 || Pads[lp->padid].type == 4)	// digital and analogue pads both report the sticks
	{
		rx = Pads[lp->padid].mapanalog[0];
		ry = Pads[lp->padid].mapanalog[1];
	}

	// radial deadzone, then scale to 0..128 and ease in
	mag = SquareRoot0(rx * rx + ry * ry);

	dyaw = 0;
	dpitch = 0;

	if (mag > CAMORBIT_DEADZONE && !lookBack)
	{
		f = (mag - CAMORBIT_DEADZONE) * 128 / (128 - CAMORBIT_DEADZONE);

		if (f > 128)
			f = 128;

		rx = rx * f / mag;
		ry = ry * f / mag;
		rx = rx * ABS(rx) / 128 * gCameraStickSens / 100;
		ry = ry * ABS(ry) / 128 * gCameraStickSens / 100;

		if (gCameraInvertY)
			ry = -ry;

		// stick up looks up, so the camera goes down
		dyaw = rx * CAMORBIT_YAW_RATE / 128;
		dpitch = ry * CAMORBIT_PITCH_RATE / 128;
		moved = 1;
	}

	if ((mx != 0 || my != 0) && !lookBack)
	{
		// mouse delta is a distance, not a speed: no ease-in curve
		mx = mx * CAMORBIT_MOUSE_YAW * gCameraMouseSens / 100;
		my = my * CAMORBIT_MOUSE_PITCH * gCameraMouseSens / 100;

		if (gCameraInvertY)
			my = -my;

		dyaw += MAX(-CAMORBIT_MOUSE_MAX, MIN(mx, CAMORBIT_MOUSE_MAX));
		dpitch += MAX(-CAMORBIT_MOUSE_MAX, MIN(my, CAMORBIT_MOUSE_MAX));
		moved = 1;
	}

	if (moved)
	{
		lp->camYaw += dyaw;
		lp->camYaw = ((lp->camYaw + 2048) & 4095) - 2048;
		lp->camPitch = MAX(CAMORBIT_PITCH_MIN, MIN(lp->camPitch + dpitch, CAMORBIT_PITCH_MAX));
	}

#if defined(DEBUG_OPTIONS) && !defined(PSX)
	{
		// test hooks: REDRIVER2_CAMPITCH / REDRIVER2_CAMYAW pin the orbit offsets (units, 4096 = 360 degrees)
		const char* envPitch = getenv("REDRIVER2_CAMPITCH");
		const char* envYaw = getenv("REDRIVER2_CAMYAW");

		if (envPitch)
		{
			lp->camPitch = atoi(envPitch);
			moved = 1;
		}

		if (envYaw)
			lp->camYaw = atoi(envYaw);

		if (getenv("REDRIVER2_CAMSWEEP"))
		{
			extern int gDrawStats[4];

			lp->camYaw = (FrameCnt * 8) % 4096 - 2048;
			printInfo("SWEEP yaw %d pitch %d tiles %d buildings %d cars %d" "\n", lp->camYaw, lp->camPitch, gDrawStats[0], gDrawStats[1], num_cars_drawn);
		}
	}
#endif

	if (moved || lookHeld)
		lp->camIdle = 0;
	else if (lp->camIdle < 255)
		lp->camIdle++;

	camOrbitReturnW = 0;

	if (lp->cameraCarId < 0)
	{
		// on foot the camera stays where it was put, like GTA: the player walks relative to it
	}
	else if (lookBack)
	{
		// looking back replaces the manual offset
		lp->camYaw = CamOrbitEase(lp->camYaw, 0);
		lp->camPitch = CamOrbitEase(lp->camPitch, 0);
	}
	else if (!moved && !lookHeld && gCameraAutoCenter && (lp->camYaw != 0 || lp->camPitch != 0))
	{
		// wait, then ease the offset back to zero: exponential decay with a soft start
		camOrbitReturnW = (lp->camIdle - gCameraReturnDelay * 30 / 1000) * 16 / CAMORBIT_RETURN_RAMP;

		camOrbitReturnW = MAX(0, MIN(camOrbitReturnW, 16));

		if (camOrbitReturnW > 0)
		{
			if (lp->camYaw != 0)
				lp->camYaw = CamOrbitReturn(lp->camYaw, camOrbitReturnW);

			if (lp->camPitch != 0)
				lp->camPitch = CamOrbitReturn(lp->camPitch, camOrbitReturnW);
		}
	}

	lp->camLook = CamOrbitEase(lp->camLook, lookBack ? CAMORBIT_LOOK_BACK : 0);
}


#if defined(DEBUG_OPTIONS) && !defined(PSX)
// Paused with the camera in a bad spot: writes CAMSTATE.txt (working directory) with the exact camera angles in units and degrees.
void DumpCameraState(void)
{
	extern int gDrawStats[4];
	extern int CellInLoadedArea(int cellx, int cellz);
	PLAYER* lp = &player[0];
	FILE* f = fopen("CAMSTATE.txt", "w");

	if (!f)
		return;

	fprintf(f, "frame %d city %d\n", FrameCnt, GameLevel);
	fprintf(f, "player pos %d %d %d\n", lp->pos[0], lp->pos[1], lp->pos[2]);
	fprintf(f, "camera pos %d %d %d (height above ground %d)\n", camera_position.vx, camera_position.vy, camera_position.vz, -camera_position.vy - MapHeight(&camera_position));
	fprintf(f, "camera angle units (4096 = 360): x %d y %d z %d\n", camera_angle.vx, camera_angle.vy, camera_angle.vz);
	fprintf(f, "camera angle degrees: x %d y %d z %d\n", camera_angle.vx * 360 / 4096, camera_angle.vy * 360 / 4096, camera_angle.vz * 360 / 4096);
	fprintf(f, "orbit: yaw %d (%d deg) pitch %d (%d deg) look %d idle %d dist %d view %d car %d foot %d\n", lp->camYaw, lp->camYaw * 360 / 4096, lp->camPitch, lp->camPitch * 360 / 4096,
		lp->camLook, lp->camIdle, lp->cameraDist, lp->cameraView, lp->cameraCarId, lp->camFoot);
	fprintf(f, "orbit on %d mouse %d  draw mode %d  tiles %d buildings %d\n", gCameraOrbit, gCameraMouse, gDrawDistanceMode, gDrawStats[0], gDrawStats[1]);
	fprintf(f, "cell %d %d  inarea %d\n", current_cell_x, current_cell_z, CellInLoadedArea(current_cell_x, current_cell_z));
	fclose(f);
}
#endif

short gCameraDistance = 1000;
short gCameraMaxDistance = 0;

#ifdef PSX
#define gCameraDefaultScrZ 256
#else
short gCameraDefaultScrZ = 256;
#endif


CAR_DATA *jcam = NULL;
int switch_detail_distance = 10000;

// [D] [T]
void PlaceCameraFollowCar(PLAYER *lp)
{
	CAR_COSMETICS *car_cos;
	int camAngle;
	int cdist;
	int sdist;
	int carheight;
	int camExpandSpeed;
	int angleDelta;
	int cammapht;
	int camPosVy;
	int maxCameraDist;

	maxCameraDist = 850;
	carheight = -220;
	camExpandSpeed = 10;
	
	if (lp->cameraCarId >= 0)
	{
		CAR_DATA* camCar;
		int carSpeed;
		camCar = &car_data[lp->cameraCarId];

		car_cos = camCar->ap.carCos;

		if(car_cos)
		{
			int addDist;

			carheight = car_cos->colBox.vy * -3 + 85;

			// [A] default just adds 248, but it's too close for big vehicles
			addDist = MAX(248, car_cos->colBox.vy * 3);

			if (gCameraMaxDistance == 0)
				maxCameraDist = car_cos->colBox.vz * 2 + car_cos->colBox.vy + addDist;
			else
				maxCameraDist = gCameraMaxDistance;

			carSpeed = FIXEDH(camCar->hd.wheel_speed);

			if (ABS(carSpeed) > 9 && (gCameraDistance + 30 <= maxCameraDist))
				camExpandSpeed = 20;
		}
	}

	if (pauseflag == 0)
	{
		TurnHead(lp);
		UpdateCameraOrbit(lp);
	}
	else
	{
		CamMouseRelease();
	}

#ifndef PSX
	maxCameraDist = maxCameraDist * CameraZoomPercent(lp) / 100;
#endif

	camera_angle.vx = 25;

	// on foot the orbit camera no longer trails Tanner's heading: it keeps its own world angle (camFootDir + camYaw)
	if (camOrbitActive && lp->cameraCarId == -1)
	{
		if (!lp->camFoot)
			lp->camFootDir = lp->cameraAngle;

		lp->camFoot = 1;
		lp->camHeading = (lp->camFootDir + lp->camYaw + 2048) & 0xfff;
		baseDir = lp->camFootDir - gCameraAngle;
	}
	else
	{
		lp->camFoot = 0;
	}

	camAngle = lp->cameraAngle - (lp->headPos >> 16) + lp->camYaw + lp->camLook;

	if (pauseflag == 0 || EditMode == 2)
	{
		// [A] handle REDRIVER2 dedicated look back button; the orbit camera eases there with camLook instead
		if (!camOrbitActive && ((paddCamera & CAMERA_PAD_LOOK_BACK) == CAMERA_PAD_LOOK_BACK || (paddCamera & CAMERA_PAD_LOOK_BACK_DED)))
		{
			camAngle = baseDir; // look back
		}
		else
		{
			angleDelta = DIFF_ANGLES(lp->cameraAngle, baseDir + gCameraAngle); // (((baseDir + gCameraAngle) - lp->cameraAngle) + 2048U & 0xfff) - 2048;

			lp->cameraAngle += (angleDelta >> 3) & 0xfff;
		}
	}

	// D1 cars are bigger and the original D1 camera sits a little higher (y is negative = up)
	if (IS_DRIVER1_CITY(GameLevel))
		carheight = carheight * D1_CAMERA_HEIGHT_PERCENT / 100;

	lp->cameraPos.vx = basePos[0] + FIXEDH(RSIN(camAngle) * lp->cameraDist);
	lp->cameraPos.vz = basePos[2] + FIXEDH(RCOS(camAngle) * lp->cameraDist);

	lp->cameraPos.vy = basePos[1];
	camPosVy = MapHeight(&lp->cameraPos);
	lp->cameraPos.vy = carheight - basePos[1];

	if (lp->camPitch != 0)
	{
		// the camera rides up (or down) on the same horizontal distance and keeps looking at the car
		lp->cameraPos.vy -= lp->cameraDist * RSIN(lp->camPitch) / RCOS(lp->camPitch);
		camera_angle.vx += lp->camPitch;
	}

	cammapht = (carheight - camPosVy) - 100 + gCameraOffset.vy;

	if (lp->cameraPos.vy > cammapht)
	{
		int height;
		height = MapHeight((VECTOR*)basePos);

		if (ABS(height - camPosVy) < 900)
		{
			camera_angle.vx = (lp->cameraPos.vy - cammapht >> 1) + 25;
			lp->cameraPos.vy = cammapht;
		}
	}

#if defined(DEBUG_OPTIONS) && !defined(PSX)
	extern int gCamDown;	// -camdown: aerial test shots look down at the ground
	if (gCamDown)
		camera_angle.vx = gCamDown;
#endif

	jcam = &car_data[CAMERA_COLLIDER_CARID];
	ClearMem((char *)jcam, sizeof(CAR_DATA));

	jcam->controlType = CONTROL_TYPE_CAMERACOLLIDER;

	gCameraDistance = maxCameraDist;

	if (lp->cameraCarId >= 0) 
		jcam->ap.carCos = car_data[lp->cameraCarId].ap.carCos;

	jcam->hd.direction = camAngle & 0xfff;
	
	sdist = maxCameraDist * RSIN(camAngle) + 2048;
	cdist = maxCameraDist * RCOS(camAngle) + 2048;

	jcam->hd.oBox.location.vx = jcam->hd.where.t[0] = basePos[0] + (sdist >> 13);
	jcam->hd.oBox.location.vy = jcam->hd.where.t[1] = -lp->cameraPos.vy;
	jcam->hd.oBox.location.vz = jcam->hd.where.t[2] = basePos[2] + (cdist >> 13);

	CheckScenaryCollisions(jcam);

	lp->cameraDist += camExpandSpeed;

	if (lp->cameraDist > gCameraDistance)
		lp->cameraDist = gCameraDistance;

	lp->cameraPos.vy = -jcam->hd.where.t[1];
	lp->cameraPos.vx = basePos[0] + FIXEDH(lp->cameraDist * RSIN(jcam->hd.direction));
	lp->cameraPos.vz = basePos[2] + FIXEDH(lp->cameraDist * RCOS(jcam->hd.direction));

	if (lp->cameraCarId < 0)
		camera_angle.vy = -(jcam->hd.direction + 2048);
	else
		camera_angle.vy = -ratan2(basePos[0] - lp->cameraPos.vx, basePos[2] - lp->cameraPos.vz) & 0xfff;
	
	camera_angle.vz = 0;

#if defined(DEBUG_OPTIONS) && !defined(PSX)
	if (gCamTest && camTestTick >= 0)
	{
		// lateral = camera offset along the car's right vector; < 0 means the camera is left of the car
		int lateral = ((lp->cameraPos.vx - basePos[0]) * RCOS(baseDir) - (lp->cameraPos.vz - basePos[2]) * RSIN(baseDir)) >> 12;

		// above = camera height over the ground
		int above = -lp->cameraPos.vy - MapHeight(&lp->cameraPos);

		printInfo("CAMTEST t=%d fc=%d yaw=%d idle=%d w=%d lateral=%d pitch=%d look=%d above=%d\n", camTestTick - 1, FrameCnt, lp->camYaw, lp->camIdle, camOrbitReturnW, lateral,
			lp->camPitch, lp->camLook, above);
	}
#endif

	SetGeomScreen(scr_z = gCameraDefaultScrZ);
	switch_detail_distance = 10000;

	BuildWorldMatrix();
}


// [D] [T]
int PointAtTarget(VECTOR *pPosition, VECTOR *pTarget, SVECTOR *pAngleVec)
{
	VECTOR delta;

	delta.vx = pTarget->vx - pPosition->vx;
	delta.vy = pTarget->vy - pPosition->vy;
	delta.vz = pTarget->vz - pPosition->vz;

	int d = SquareRoot0(delta.vx * delta.vx + delta.vz * delta.vz);

	pAngleVec->vx = 1024 - ratan2(d, delta.vy) & 0xfff;
	pAngleVec->vy = -ratan2(delta.vx, delta.vz) & 0xfff;
	pAngleVec->vz = 0;

	return d;
}

// [D] [T]
void PlaceCameraInCar(PLAYER *lp, int BumperCam)
{
	VECTOR viewer_position;
	int angle;
	CAR_DATA *cp;

	cp = NULL;

	if (lp->cameraCarId > -1)
		cp = &car_data[lp->cameraCarId];

	viewer_position.vx = 0;
	viewer_position.vy = 0;
	viewer_position.vz = 0;

	TurnHead(lp);

	// [A] handle REDRIVER2 dedicated look back button
	if ((paddCamera & CAMERA_PAD_LOOK_BACK) == CAMERA_PAD_LOOK_BACK || (paddCamera & CAMERA_PAD_LOOK_BACK_DED))
		camera_angle.vy = 2048 - baseDir & 4095;
	else
		camera_angle.vy = (lp->headPos >> 16) - baseDir & 4095;

	if (cp)
	{
		// build custom matrix using car draw matrix
		InvertMatrix(&cp->hd.drawCarMat, &inv_camera_matrix);

		viewer_position.vy = -40;

		if (!((paddCamera & CAMERA_PAD_LOOK_BACK) == CAMERA_PAD_LOOK_BACK || (paddCamera & CAMERA_PAD_LOOK_BACK_DED)))
		{
			viewer_position.vz -= cp->ap.carCos->colBox.vz - 250;

			// rotate vector on vehicle
			SetRotMatrix(&cp->hd.drawCarMat);
			_MatrixRotate(&viewer_position);

			// rotate head
			_RotMatrixY(&inv_camera_matrix, 2048 + (lp->headPos >> 16));
		}

		MulMatrix0(&aspect, &inv_camera_matrix, &inv_camera_matrix);
		InvertMatrix(&inv_camera_matrix, &camera_matrix);

		face_camera_work.m[0][0] = ONE;
		face_camera_work.m[0][2] = 0;
		face_camera_work.m[2][0] = 0;
		face_camera_work.m[2][2] = ONE;

		RotMatrixY(-(camera_angle.vy & 0xfff), &face_camera_work);
		MulMatrix0(&inv_camera_matrix, &face_camera_work, &face_camera);

		lp->cameraPos.vx = basePos[0] + viewer_position.vx;
		lp->cameraPos.vy = viewer_position.vy - basePos[1];
		lp->cameraPos.vz = basePos[2] + viewer_position.vz;
	}
	else
	{
		LPPEDESTRIAN pPlayerPed;
		pPlayerPed = lp->pPed;

		camera_angle.vx = -tannerLookAngle.vx;

		BuildWorldMatrix();

		if (pPlayerPed)
		{
			// apply pedestrian bobbing
			viewer_position.vy += pPlayerPed->head_pos - 25;
		}

		angle = -baseDir;

		lp->cameraPos.vx = basePos[0] + FIXEDH(RSIN(angle) * viewer_position.vz);
		lp->cameraPos.vy = viewer_position.vy - basePos[1];
		lp->cameraPos.vz = basePos[2] - FIXEDH(RCOS(angle) * viewer_position.vz);
	}

	SetGeomScreen(scr_z = gCameraDefaultScrZ);

	switch_detail_distance = 10000;
}

// [D] [T]
int OK_To_Zoom(void)
{
	int old_z;
	VECTOR temp;

	old_z = scr_z;

	temp.vx = MainPlayer.pos[0];
	temp.vy = -MainPlayer.pos[1];
	temp.vz = MainPlayer.pos[2];

	scr_z = (dist(&camera_position, &temp) >> 4) + 256;

	if (scr_z > 800)
		scr_z = 800;

	if (scr_z < gCameraDefaultScrZ)
		scr_z = gCameraDefaultScrZ;

	scr_z = old_z;
	return CameraCollisionCheck() == 0;
}

// [D] [T]
void SetBasePos(VECTOR *pVec)
{
	basePos[0] = pVec->vx;
	basePos[1] = pVec->vy;
	basePos[2] = pVec->vz;
}

// [D] [T]
void PlaceCameraAtLocation(PLAYER* lp, int zoom)
{
	int d;
	VECTOR temp;

	d = 0;

	if (tracking_car)
	{
		CalcCameraBasePos(lp);

		temp.vx = basePos[0];
		temp.vy = -basePos[1];
		temp.vz = basePos[2];

		d = PointAtTarget(&lp->cameraPos, &temp, &camera_angle);

		if (d > VIEW_DRAW_DISTANCE)
		{
			lp->cameraView = 0;
			return;
		}

		if (zoom == 0)
		{
			scr_z = gCameraDefaultScrZ;
		}
		else
		{
			scr_z = (d >> 4) + gCameraDefaultScrZ;

			if (scr_z > 800)
				scr_z = 800;
		}
	}

	if (scr_z < gCameraDefaultScrZ)
		scr_z = gCameraDefaultScrZ;

	SetGeomScreen(scr_z);
	switch_detail_distance = 10000 + (d >> 1);
	BuildWorldMatrix();
}