#include "driver2.h"
#include "denting.h"
#include "wheelforces.h"
#include "cars.h"
#include "main.h"
#include "gamesnd.h"
#include "debris.h"
#include "handling.h"
#include "cosmetic.h"
#include "dr2roads.h"
#include "map.h"
#include "mission.h"
#include "pad.h"
#include "players.h"
#include "sound.h"
#include "glaunch.h"
#include "system.h"
#include "cutscene.h"

#define GRAVITY_FORCE		(-7456)			// D1 has -10922

// A Driver 1 city is 3000 / 2048 (the D1 cell size against the D2 one) times bigger than a Driver 2 city and so is every force
// of the D1 EXE (gravity -10922, throttle 7200, slip clamp 18000, collision friction 1440 / 8640). The converted D1 levels use
// 2048 unit cells too, so the scale follows the city, not MAP_CELL_SIZE. Driver 2 cities and d1Handling=0 stay as they are.
#define D1_CELL_SIZE		3000
#define WORLD_FORCE(v)		((int)((long long)(v) * (D1HandlingActive() ? D1_CELL_SIZE : 2048) / 2048))

int gD1Handling = 1;	// config.ini [d1dlc] d1Handling: Driver 1 cities use the forces and limits of the D1 EXE (docs/dev-notes/d1-car-physics.md)

// The D2 code is the D1 code with every speed, length and force divided by 1.4648: in a D1 city WORLD_FORCE undoes that for
// the forces and the limits that compare speeds or depths take their D1 value.
int D1HandlingActive(void)
{
	return gD1Handling && IS_DRIVER1_CITY(GameLevel);
}

// D1 depth offset 20, D2 14 (20 / 1.4648): the wheels are drawn with the same offset, or they float above the road
int WheelRestCompression(void)
{
	return D1HandlingActive() ? 20 : 14;
}

struct CAR_LOCALS
{
	LONGVECTOR4 vel;
	LONGVECTOR4 avel;
	int extraangulardamping;
	int aggressive;
};

HANDLING_TYPE handlingType[7] =
{
	// frictionScaleRatio, aggressiveBraking, fourWheelDrive, autoBrakeOn
	{ 32, 1, 0, 1 },
	{ 29, 0, 0, 0 },
	{ 45, 1, 1, 0 },
	{ 55, 1, 1, 0 },
	{ 68, 1, 1, 0 },
	{ 32, 1, 0, 1 },
	{ 29, 0, 0, 0 }
};

// [D] [T]
void StepCars(void)
{
	CAR_DATA** ppCar;
	CAR_DATA** end;

	ppCar = active_car_list;
	end = active_car_list + num_active_cars;

	// step active cars
	while (ppCar < end)
	{
		StepOneCar(*ppCar);
		ControlCarRevs(*ppCar++);
	}
}

// [D] [T]
void GetFrictionScalesDriver1(CAR_DATA* cp, CAR_LOCALS* cl, int* frontFS, int* rearFS)
{
	int autoBrake;
	int q;
	int lockSpeed;
	_HANDLING_TYPE* hp;

	hp = &handlingType[cp->hndType];
	lockSpeed = D1HandlingActive() ? 61440 : 41943;

	if (cp->thrust < 0)
		*frontFS = 1453;
	else if (cp->thrust < 1)
		*frontFS = 937;
	else
		*frontFS = 820;

	autoBrake = cp->hd.autoBrake;

	if (cp->wheelspin == 0 && hp->autoBrakeOn != 0 && autoBrake > 0 && cp->hd.wheel_speed > 0)
	{
		q = autoBrake << 1;

		if (autoBrake > 13)
		{
			autoBrake = 13;
			q = 26;
		}

		*frontFS += (q + autoBrake) * 15;

		D_CHECK_ERROR(hp->autoBrakeOn == 2, "invalid autoBrakeOn");
	}

	if ((cp->thrust < 0 && cp->hd.wheel_speed > lockSpeed && cp->hndType == 0) ||
		(cp->controlType == CONTROL_TYPE_CIV_AI && cp->ai.c.thrustState == CIV_AI_THRUST_STOP && cp->ai.c.ctrlState != CIV_AI_CTRL_DIED_WITH_MUSIC))
	{
		cp->hd.wheel[3].locked = 1;
		cp->hd.wheel[2].locked = 1;
		cp->hd.wheel[1].locked = 1;
		cp->hd.wheel[0].locked = 1;
	}
	else
	{
		cp->hd.wheel[3].locked = 0;
		cp->hd.wheel[2].locked = 0;
		cp->hd.wheel[1].locked = 0;
		cp->hd.wheel[0].locked = 0;
	}

	if (cp->handbrake == 0)
	{
		if (cp->wheelspin != 0)
			*frontFS += 600;
	}
	else
	{
		if (cp->thrust > -1)
			cp->thrust = 0;

		if (cp->hd.wheel_speed < 1)
			*frontFS -= 375;
		else
			*frontFS += 656;

		cp->hd.wheel[1].locked = 1;
		cp->hd.wheel[3].locked = 1;
		cp->wheelspin = 0;
	}

	if (cp->hd.wheel_speed < 0 && cp->thrust > -1 && cp->handbrake == 0)
	{
		*frontFS -= 400;
	}

	*rearFS = 0x780 - *frontFS;

	if (cp->wheelspin != 0)
	{
		cp->thrust = FIXEDH(cp->ap.carCos->powerRatio * (D1HandlingActive() ? 4778 : 5000));	// D1: 7000 after WORLD_FORCE
	}

	if (cp->thrust < 0 && cp->hd.wheel_speed > lockSpeed && cl->aggressive != 0)
	{
		*frontFS = (*frontFS * 10) / 8;
		*rearFS = (*rearFS * 10) / 8;
	}
	else
	{
		if (cp->hd.wheel[0].onGrass == 0)
			*frontFS = (*frontFS * 36 - *frontFS) / 32;
		else
			*frontFS = (*frontFS * 40 - *frontFS) / 32;
	}

	*frontFS = (*frontFS * hp->frictionScaleRatio) / 32;
	*rearFS = (*rearFS * hp->frictionScaleRatio) / 32;

	if ((cp->hndType == 5) && (cp->ai.l.dstate == 5))
	{
		*frontFS = (*frontFS * 3) / 2;
		*rearFS = (*rearFS * 3) / 2;
	}

	int traction = cp->ap.carCos->traction;

	if (traction != 4096)
	{
		*frontFS = FIXEDH(*frontFS * traction);
		*rearFS = FIXEDH(*rearFS * traction);
	}
}

// [D] [T]
void ConvertTorqueToAngularAcceleration(CAR_DATA* cp, CAR_LOCALS* cl)
{
	int twistY, twistZ;
	int zd;
	int i;

	twistY = car_cosmetics[cp->ap.model].twistRateY;
	twistZ = car_cosmetics[cp->ap.model].twistRateZ;

	// the twist rates were set for the unscaled forces (D1 cars: D1 110 / 550 x 1.4648^2 for D2 sized levers and forces),
	// so with WORLD_FORCE scaling the forces the turn rate per force drops by the same factor and rotation stays as before
	if (D1HandlingActive())
	{
		twistY = twistY * 2048 / D1_CELL_SIZE;
		twistZ = twistZ * 2048 / D1_CELL_SIZE;
	}

	zd = (twistZ - twistY) * FIXEDH(cp->hd.where.m[0][2] * cp->hd.aacc[0] + cp->hd.where.m[1][2] * cp->hd.aacc[1] + cp->hd.where.m[2][2] * cp->hd.aacc[2]);

	for (i = 0; i < 3; i++)
	{
		cp->hd.aacc[i] = cp->hd.aacc[i] * twistY + FIXEDH(cp->hd.where.m[i][2] * zd - cl->avel[i] * 128);

		if (cl->extraangulardamping == 1)
			cp->hd.aacc[i] -= cl->avel[i] / 8;
	}
}


// [D] [T]
void AddWheelForcesDriver1(CAR_DATA* cp, CAR_LOCALS* cl)
{
	int oldCompression, newCompression;
	int dir;
	int forcefac;
	int angle;
	int lfx, lfz;
	int sidevel, slidevel;
	int susForce;
	int chan;
	WHEEL* wheel;
	int friction_coef;
	int oldSpeed, wheelspd;
	LONGVECTOR4 wheelPos, surfacePoint, surfaceNormal;
	VECTOR force;
	LONGVECTOR4 pointVel;
	int frontFS, rearFS;
	sdPlane* SurfacePtr;
	int i;
	int cdx, cdz;
	int sdx, sdz;
	CAR_COSMETICS* car_cos;
	int player_id;
	int oldCutRoughness;
	int thrust, slipMax;
	int d1, slideMax, susMax;

	// D1 limits in a D1 city: the grip curve takes the speed as it is (D2 scales it to D1 size with 3 / 2), slide clamp,
	// depth offset and travel of the wheels are the D1 ones (D2 has them divided by 1.4648)
	d1 = D1HandlingActive();
	slideMax = d1 ? 72000 : 50000;
	susMax = d1 ? 60 : 42;

	oldSpeed = d1 ? cp->hd.speed : cp->hd.speed * 3 >> 1;

	if (oldSpeed < 32)
		oldSpeed = oldSpeed * -72 + 3696;
	else
		oldSpeed = 1424 - oldSpeed;

	dir = cp->hd.direction;
	cdx = RSIN(dir);
	cdz = RCOS(dir);

	dir += cp->wheel_angle;
	sdx = RSIN(dir);
	sdz = RCOS(dir);

	player_id = GetPlayerId(cp);
	car_cos = &car_cosmetics[cp->ap.model];
	oldCutRoughness = gInGameCutsceneActive && gCurrentMissionNumber == 23 && gInGameCutsceneID == 0; // [A] hack moved here

	GetFrictionScalesDriver1(cp, cl, &frontFS, &rearFS);
	thrust = WORLD_FORCE(cp->thrust);
	slipMax = d1 ? 18000 : WORLD_FORCE(12500);
	cp->hd.front_vel = 0;
	cp->hd.rear_vel = 0;

	if (oldSpeed > 3300)
		oldSpeed = 3300;

	i = 3;
	wheel = cp->hd.wheel + 3;
	do {
		gte_ldv0(&car_cos->wheelDisp[i]);

		gte_rtv0tr();
		gte_stlvnl(wheelPos);

		FindSurfaceD2((VECTOR*)&wheelPos, (VECTOR*)&surfaceNormal, (VECTOR*)&surfacePoint, &SurfacePtr);

		if (SurfacePtr && SurfacePtr->surface == SURF_GRASS)
		{
			int roughness;
			roughness = RSIN((surfacePoint[0] + surfacePoint[2]) * 2) >> 8;

			surfacePoint[1] += oldCutRoughness ? (roughness >> 1) : (roughness / 3);

			forcefac = 2048;
		}
		else
		{
			forcefac = 4096;
		}

		friction_coef = (forcefac * (32400 - wetness) >> 15) + 500;

		if (SurfacePtr != NULL)
			wheel->onGrass = SurfacePtr->surface == SURF_GRASS;
		else
			wheel->onGrass = 0;

		if (SurfacePtr)
		{
			switch (SurfacePtr->surface)
			{
				case SURF_GRASS:
				case SURF_WATER:
				case SURF_DEEPWATER:
				case SURF_SAND:
					wheel->surface = 0x80;
					break;
				default:
					wheel->surface = 0;
			}

			// [A] indication of Event surface which means we can't add tyre tracks for that wheel
			if (SurfacePtr->surface - 16U < 16)
				wheel->surface |= 0x8;

			switch (SurfacePtr->surface)
			{
				case SURF_ALLEY:
					wheel->surface |= 0x2;
					break;
				case SURF_WATER:
				case SURF_DEEPWATER:
					wheel->surface |= 0x1;
					break;
				case SURF_SAND:
					wheel->surface |= 0x3;
					break;
			}
		}
		else
		{
			wheel->surface = 0;
		}

		oldCompression = wheel->susCompression;
		if (d1)
			newCompression = surfacePoint[1] - wheelPos[1] + WheelRestCompression();	// D1 takes the plain height difference, no slope factor
		else
			newCompression = FIXEDH((surfacePoint[1] - wheelPos[1]) * surfaceNormal[1]) + WheelRestCompression();

		if (newCompression < 0)
			newCompression = 0;

		if (newCompression > 800)
			newCompression = 12;

		// play wheel collision sound
		// and apply vibration to player controller
		if (cp->controlType == CONTROL_TYPE_PLAYER)
		{
			if (ABS(newCompression - oldCompression) > 12 && (i & 1U) != 0)
			{
				chan = GetFreeChannel(0);
				if(chan > -1)
				{
					if (NumPlayers > 1 && NoPlayerControl == 0)
						SetPlayerOwnsChannel(chan, player_id);

					Start3DSoundVolPitch(chan, SOUND_BANK_SFX, 1, cp->hd.where.t[0], cp->hd.where.t[1], cp->hd.where.t[2], -2500, 400);
					SetChannelPosition3(chan, (VECTOR*)cp->hd.where.t, NULL, -2500, 400, 0);
				}
			}

			if (newCompression >= 65)
				SetPadVibration(*cp->ai.padid, 1);
			else if (newCompression >= 35)
				SetPadVibration(*cp->ai.padid, 2);
			else if (newCompression > 25)
				SetPadVibration(*cp->ai.padid, 3);
		}

		if (newCompression > susMax)
			newCompression = susMax;

		if (newCompression == 0 && oldCompression == 0)
		{
			wheel->susCompression = 0;
		}
		else
		{
			wheelPos[2] = wheelPos[2] - cp->hd.where.t[2];
			wheelPos[1] = wheelPos[1] - cp->hd.where.t[1];
			wheelPos[0] = wheelPos[0] - cp->hd.where.t[0];

			force.vz = 0;
			force.vx = 0;

			pointVel[0] = FIXEDH(cl->avel[1] * wheelPos[2] - cl->avel[2] * wheelPos[1]) + cl->vel[0];
			pointVel[2] = FIXEDH(cl->avel[0] * wheelPos[1] - cl->avel[1] * wheelPos[0]) + cl->vel[2];

			// that's our spring
			susForce = newCompression * 230 - oldCompression * 100;

			if (!d1)
				susForce = WORLD_FORCE(susForce);	// the D1 depth is already in D1 units

			if (wheel->locked)
			{
				dir = ratan2(pointVel[0] >> 6, pointVel[2] >> 6);

				lfx = RSIN(dir);
				lfz = RCOS(dir);

				if (ABS(pointVel[0]) + ABS(pointVel[2]) < 8000)
				{
					surfaceNormal[0] = 0;
					surfaceNormal[1] = ONE;
					surfaceNormal[2] = 0;
				}
			}
			else
			{
				if (i & 1)
				{
					lfz = -cdx;
					lfx = cdz;
				}
				else
				{
					lfz = -sdx;
					lfx = sdz;
				}
			}

			slidevel = (pointVel[0] / 64) * (lfx / 64) + (pointVel[2] / 64) * (lfz / 64);
			wheelspd = ABS((oldSpeed / 64) * (slidevel / 64));

			if (slidevel > slideMax)
			{
				slidevel = slipMax;
			}
			else if (slidevel < -slideMax)
			{
				slidevel = -slipMax;
			}
			else
			{
				slidevel = FIXEDH(oldSpeed * slidevel);

				if (slidevel > slipMax)
					slidevel = slipMax;

				if (slidevel < -slipMax)
					slidevel = -slipMax;
			}
			
			if ((i & 1U) != 0)
			{
				// rear wheels
				if (wheel->locked == 0)
				{
					sidevel = FIXEDH(rearFS * slidevel);

					if (handlingType[cp->hndType].autoBrakeOn != 0 && 0 < sidevel * cp->wheel_angle)
						cp->hd.autoBrake = -1;

					force.vx = -lfz * thrust;
					force.vz = lfx * thrust;
				}
				else
				{
					sidevel = FixHalfRound(frontFS * slidevel, 14);
				}

				if (cp->hd.rear_vel < wheelspd)
					cp->hd.rear_vel = wheelspd;
			}
			else
			{
				// front wheels
				sidevel = frontFS * slidevel + 2048 >> 12;
				
				if (wheel->locked)
				{
					sidevel = (frontFS * slidevel + 2048 >> 13) + sidevel >> 1;
					
					forcefac = FixHalfRound(FIXEDH(-sidevel * lfx) * sdz - FIXEDH(-sidevel * lfz) * sdx, 11);
					
					force.vx = forcefac * sdz;
					force.vz = -forcefac * sdx;
				}
				else
				{
					if (cp->controlType == CONTROL_TYPE_PURSUER_AI)
					{
						force.vx = sdx * thrust;
						force.vz = sdz * thrust;
					}
				}

				if (cp->hd.front_vel < wheelspd)
					cp->hd.front_vel = wheelspd;

			}

			force.vx += (susForce * surfaceNormal[0] - sidevel * lfx) - cl->vel[0] * 12;
			force.vz += (susForce * surfaceNormal[2] - sidevel * lfz) - cl->vel[2] * 12;

			// apply speed reduction by water
			if ((wheel->surface & 7) == 1)
			{
				force.vx -= cl->vel[0] * 75;
				force.vz -= cl->vel[2] * 75;
			}

			angle = cp->hd.where.m[1][1];

			if (angle < 2048)
			{
				angle = 4096 - angle;

				if (angle <= 4096)
					angle = 4096 - FIXEDH(angle * angle);
				else
					angle = 0;

				friction_coef = FIXEDH(friction_coef * angle);
			}

			if (surfaceNormal[1] < 3276)
				friction_coef = friction_coef * surfaceNormal[1] * 5 >> 0xe;

			force.vy = FIXEDH(susForce * surfaceNormal[1] - cl->vel[1] * 12);
			force.vx = FIXEDH(force.vx) * friction_coef >> 0xc;
			force.vz = FIXEDH(force.vz) * friction_coef >> 0xc;

#ifndef PSX
			if (WheelIsLost(cp, i))
			{
				// [A] no wheel here: the corner drags on the ground, carries less and pulls the car to that side
				force.vy = force.vy * 55 / 100;
				force.vx = force.vx * 30 / 100 - FIXEDH(cl->vel[0] * 12);
				force.vz = force.vz * 30 / 100 - FIXEDH(cl->vel[2] * 12);
			}
#endif

			if (d1)
			{
				// D1 lowers the roll lever of every car: cops to 7 / 32 or 8 / 32, all others to 10 / 32
				if (cp->controlType == CONTROL_TYPE_PURSUER_AI)
					wheelPos[1] = (wheelPos[1] * (gCopDifficultyLevel == 2 ? 7 : 8)) / 32;
				else
					wheelPos[1] = (wheelPos[1] * 10) / 32;
			}
			else if (cp->controlType == CONTROL_TYPE_PURSUER_AI)
			{
				// pursuer cars have more stability
				if (gCopDifficultyLevel == 2)
					wheelPos[1] = (wheelPos[1] * 12) / 32;
				else
					wheelPos[1] = (wheelPos[1] * 19) / 32;
			}

			cp->hd.acc[0] += force.vx;
			cp->hd.acc[1] += force.vy;
			cp->hd.acc[2] += force.vz;
	
			cp->hd.aacc[0] += FIXEDH(wheelPos[1] * force.vz - wheelPos[2] * force.vy);
			cp->hd.aacc[1] += FIXEDH(wheelPos[2] * force.vx - wheelPos[0] * force.vz);
			cp->hd.aacc[2] += FIXEDH(wheelPos[0] * force.vy - wheelPos[1] * force.vx);

			wheel->susCompression = newCompression;
		}
		wheel--;
		i--;
	} while (i >= 0);

	if (cp->hd.wheel[1].susCompression == 0 && cp->hd.wheel[3].susCompression == 0)
	{
		if (cp->thrust >= 1)
			cp->hd.wheel_speed = 1703936 + 0x4000;
		else if (cp->thrust <= -1)
			cp->hd.wheel_speed = -1245184 + 0x4000;
		else
			cp->hd.wheel_speed = 0;
	}
	else
	{
		cp->hd.wheel_speed = cdz / 64 * (cl->vel[2] / 64) + cdx / 64 * (cl->vel[0] / 64);
	}
}

int gFlyingCar = 0;		// cheat, toggled with F8 or the debug menu
int gInfiniteMass = 0;	// cheat, toggled with F6 or the debug menu (used in handling.c)
int gD1ChaseInfiniteMass = 0;	// config.ini [d1dlc] chaseInfiniteMass: the chased car of a Driver 1 mission is never pushed off its path
int gSpeedBoost = 0;	// cheat (Fun Cheats menu): the horn pushes the player car forward
int gFlyAscend = 0;		// held keys (left shift / left ctrl)
int gFlyDescend = 0;
int gFlyTestUp = 0;		// -flytest: forced climb (-128..128) for test runs
int gFlyTestFwd = 0;	// -flyfwd: gas held down in test runs
int gFlyTestAlt = 0;	// -flyalt: climb to this height, then hover (test runs)

#define FLY_STICK_DEADZONE	24	// left stick Y (-128..127) below which the car hovers

#define FLY_THRUST		24000	// velocity per step while accelerating; drag of 1/64 gives a top speed of ~375 units/frame
#define FLY_LIFT		10000	// same for climbing and descending, drag of 1/16
#define FLY_TURN_DIV	14		// wheel angle (max ~500) to heading change per step
#define FLY_BANK		2		// wheel angle to roll quaternion (4096 = 180 degrees), ~30 degrees at full steering
#define FLY_PITCH		600		// pitch quaternion while climbing/descending, ~15 degrees

// arcade flight: no gravity, accelerate/brake along the nose, steering turns the heading and banks,
// the velocity follows the nose (strong sideways damping) so the car flies instead of sliding like a hovercraft
static void StepFlyingCar(CAR_DATA* cp)
{
	static int pitch = 0, roll = 0;
	int i;
	int pad;
	int forward[3];
	int vf, sn, cs;
	int side[3];
	int up;

	pad = cp->lastPad;

	if (gFlyTestFwd)
		pad |= CAR_PAD_ACCEL;

	// climb: keys (full), else the left stick (down climbs, up descends, proportional); gFlyTestUp overrides for test runs.
	// Steering is the stick X and the gas is a button, so the stick Y is free.
	up = gFlyAscend ? 128 : gFlyDescend ? -128 : 0;

	if (up == 0 && cp->controlType == CONTROL_TYPE_PLAYER && *cp->ai.padid >= 0 && *cp->ai.padid < 2)
	{
		PAD* stick = &Pads[*cp->ai.padid];

		if ((stick->type == 2 || stick->type == 4) && ABS(stick->mapanalog[3]) > FLY_STICK_DEADZONE)
			up = stick->mapanalog[3];	// like a flight stick: push forward (up) to dive, pull back to climb
	}

	if (gFlyTestUp)
		up = gFlyTestUp;

	if (gFlyTestAlt)
		up = (cp->hd.where.t[1] < gFlyTestAlt) ? 100 : 0;
	cp->hd.direction = (cp->hd.direction + cp->wheel_angle / FLY_TURN_DIV) & 0xfff;
	TempBuildHandlingMatrix(cp, 0);

	// bank into the turn, nose up when climbing: the base orientation is yaw * (pitch, 0, roll)
	pitch += (-FLY_PITCH * up / 128 - pitch) / 6;
	roll += (-cp->wheel_angle * FLY_BANK - roll) / 6;

	sn = RSIN(cp->hd.direction / 2);
	cs = RCOS(cp->hd.direction / 2);

	cp->st.n.orientation[0] += FIXEDH(cs * pitch) + FIXEDH(sn * roll);
	cp->st.n.orientation[2] += FIXEDH(cs * roll) - FIXEDH(sn * pitch);
	RebuildCarMatrix(&cp->st, cp);

	forward[0] = RSIN(cp->hd.direction);
	forward[1] = 0;
	forward[2] = RCOS(cp->hd.direction);

	// speed along the nose and the sideways rest
	vf = ((cp->st.n.linearVelocity[0] >> 4) * forward[0] + (cp->st.n.linearVelocity[2] >> 4) * forward[2]) >> 12;
	side[0] = cp->st.n.linearVelocity[0] - (FIXEDH(forward[0] * vf) << 4);
	side[1] = 0;
	side[2] = cp->st.n.linearVelocity[2] - (FIXEDH(forward[2] * vf) << 4);

	for (i = 0; i < 3; i++)
	{
		cp->st.n.angularVelocity[i] = 0;

		cp->hd.acc[i] = -(cp->st.n.linearVelocity[i] >> 6) - (side[i] >> 3);

		if (pad & CAR_PAD_ACCEL)
			cp->hd.acc[i] += FIXEDH(forward[i] * FLY_THRUST);
		else if (pad & CAR_PAD_BRAKE)
			cp->hd.acc[i] -= cp->st.n.linearVelocity[i] >> 4;
	}

	if (up)
		cp->hd.acc[1] += FLY_LIFT * up / 128;
	else
		cp->hd.acc[1] -= cp->st.n.linearVelocity[1] >> 4;	// hover
}

#define BOOST_THRUST	60000	// extra acceleration along the nose while the horn is held (the engine gives about 7200)

// Speed Boost cheat: holding the horn adds a strong push along the car's heading
static void StepSpeedBoost(CAR_DATA* cp)
{
	int p;

	for (p = 0; p < 2; p++)
	{
		if (player[p].playerType == PLAYER_TYPE_CAR && player[p].playerCarId >= 0 && &car_data[player[p].playerCarId] == cp && player[p].horn.on)
		{
			cp->hd.acc[0] += FIXEDH(RSIN(cp->hd.direction) * WORLD_FORCE(BOOST_THRUST));
			cp->hd.acc[2] += FIXEDH(RCOS(cp->hd.direction) * WORLD_FORCE(BOOST_THRUST));
			return;
		}
	}
}

// no tyre forces or sounds while flying; the wheels just follow the speed so the engine pitch matches
static void FlyingCarWheels(CAR_DATA* cp)
{
	int i;
	int dir;

	dir = cp->hd.direction;

	for (i = 0; i < 4; i++)
		cp->hd.wheel[i].susCompression = 1;

	cp->hd.front_vel = 0;
	cp->hd.rear_vel = 0;
	cp->wheelspin = 0;
	cp->hd.wheel_speed = RCOS(dir) / 64 * (cp->st.n.linearVelocity[2] / 64) + RSIN(dir) / 64 * (cp->st.n.linearVelocity[0] / 64);
}

// [D] [T]
void StepOneCar(CAR_DATA* cp)
{
	static int frictionLimit[6] = {
		1005 * ONE, 6030 * ONE,
		1005 * ONE, 5025 * ONE,
		1884 * ONE, 5025 * ONE
	};

	volatile int impulse;
	CAR_COSMETICS* car_cos;
	int friToUse;
	int lift;
	int flying;
	int a, b, speed;
	int count, i;
	CAR_LOCALS _cl;
	LONGVECTOR4 deepestNormal, deepestLever, deepestPoint;
	LONGVECTOR4 pointPos, surfacePoint, surfaceNormal;
	LONGVECTOR4 lever, reaction;
	VECTOR direction;
	sdPlane* SurfacePtr;

	if (cp->controlType == CONTROL_TYPE_NONE)
		return;

	SurfacePtr = NULL;
	_cl.aggressive = handlingType[cp->hndType].aggressiveBraking;
	_cl.extraangulardamping = 0;

	for (i = 0; i < 3; i++)
	{
		_cl.vel[i] = cp->st.n.linearVelocity[i];
		_cl.avel[i] = cp->st.n.angularVelocity[i];
		
		cp->st.n.fposition[i] = (cp->st.n.fposition[i] & 0xF) + cp->hd.where.t[i] * 16;
	}

	cp->hd.acc[0] = 0;
	cp->hd.acc[1] = WORLD_FORCE(GRAVITY_FORCE); // apply gravity
	cp->hd.acc[2] = 0;

	flying = gFlyingCar && cp->controlType == CONTROL_TYPE_PLAYER;

	if (flying)
		StepFlyingCar(cp);
	else if (gSpeedBoost && cp->controlType == CONTROL_TYPE_PLAYER)
		StepSpeedBoost(cp);

	// calculate car speed
	a = ABS(FIXEDH(_cl.vel[0]));
	b = ABS(FIXEDH(_cl.vel[2]));

	if (a < b)
		speed = b + a / 2;
	else
		speed = a + b / 2;

	cp->hd.speed = speed;

	car_cos = cp->ap.carCos;
	lift = 0;

	gte_SetRotMatrix(&cp->hd.where);
	gte_SetTransMatrix(&cp->hd.where);

	count = 12;

	if (cp->hd.where.m[1][1] > 2048)
	{
		if(cp->controlType == CONTROL_TYPE_CIV_AI)
			count = (cp->totalDamage != 0) * 4;
		else
			count = 4;
	}

	count--;

	// calculate lifting factor
	while (count >= 0)
	{
		gte_ldv0(&car_cos->cPoints[count]);

		gte_rtv0tr();

		gte_stlvnl(pointPos);

		lever[0] = pointPos[0] - cp->hd.where.t[0];
		lever[1] = pointPos[1] - cp->hd.where.t[1];
		lever[2] = pointPos[2] - cp->hd.where.t[2];

		FindSurfaceD2((VECTOR*)pointPos, (VECTOR*)surfaceNormal, (VECTOR*)&surfacePoint, &SurfacePtr);

		if (SurfacePtr && SurfacePtr->surface == SURF_GRASS)
		{
			surfacePoint[1] += (RSIN((surfacePoint[0] + surfacePoint[2]) * 2) >> 8) / 3;
		}

		if ((surfacePoint[1] - pointPos[1]) - 1U < 799)
		{
			int newLift;

			newLift = FIXEDH((surfacePoint[1] - pointPos[1]) * surfaceNormal[1]);

			if (lift < newLift)
			{
				friToUse = 0;

				deepestNormal[0] = surfaceNormal[0];
				deepestNormal[1] = surfaceNormal[1];
				deepestNormal[2] = surfaceNormal[2];

				deepestLever[0] = lever[0];
				deepestLever[1] = lever[1];
				deepestLever[2] = lever[2];

				deepestPoint[0] = surfacePoint[0];
				deepestPoint[1] = surfacePoint[1];
				deepestPoint[2] = surfacePoint[2];

				lift = newLift;

				if (count > 3)
					friToUse = 3;
			}
		}

		count--;
	}

	// do lifting
	if (lift != 0)
	{
		int strikeVel;
		int componant;

		int lever_dot_n;
		int twistY;
		int displacementsquared;

		lever[0] = FIXEDH(_cl.avel[1] * deepestLever[2] - _cl.avel[2] * deepestLever[1]) + _cl.vel[0];
		lever[1] = FIXEDH(_cl.avel[2] * deepestLever[0] - _cl.avel[0] * deepestLever[2]) + _cl.vel[1];
		lever[2] = FIXEDH(_cl.avel[0] * deepestLever[1] - _cl.avel[1] * deepestLever[0]) + _cl.vel[2];

		twistY = car_cos->twistRateY;

		lever_dot_n = FIXEDH(deepestLever[0] * deepestNormal[0] + deepestLever[1] * deepestNormal[1] + deepestLever[2] * deepestNormal[2]);
		displacementsquared = FIXEDH(((deepestLever[0] * deepestLever[0] + deepestLever[1] * deepestLever[1] + deepestLever[2] * deepestLever[2]) - lever_dot_n * lever_dot_n) * twistY) + 4096;

		strikeVel = (lever[0] >> 6) * (deepestNormal[0] >> 6) + (lever[1] >> 6) * (deepestNormal[1] >> 6) + (lever[2] >> 6) * (deepestNormal[2] >> 6);
		impulse = (strikeVel / displacementsquared) * -2048;

		// apply friction
		componant = 2;
		do {
			int loss;
			int limit;

			limit = WORLD_FORCE(frictionLimit[friToUse + 2 - componant]);
			loss = lever[componant] * 67;

			if (loss <= limit)
			{
				if (loss < -limit)
					limit = -limit;
				else
					limit = loss;
			}

			reaction[componant] = FIXEDH(impulse * deepestNormal[componant] - limit);
			componant--;
		} while (componant >= 0);

		if (impulse > 20000)
		{
			if (gNight == 1)
			{
				direction.vx = 0;
				direction.vy = 50;
				direction.vz = 0;

				Setup_Sparks((VECTOR*)&deepestPoint, &direction, 15, 1);
			}
			else
			{
				direction.vx = 0;
				direction.vy = 40;
				direction.vz = 0;

				Setup_Debris((VECTOR*)&deepestPoint, &direction, 10, 0);
			}

			if (SurfacePtr && (SurfacePtr->surface != SURF_DEEPWATER) && (SurfacePtr->surface != SURF_WATER))
			{
				CollisionSound(GetPlayerId(cp), cp, (impulse / 6 + (impulse >> 0x1f) >> 3) - (impulse >> 0x1f), 0);
			}
		}

		cp->hd.acc[0] += reaction[0];
		cp->hd.acc[1] += reaction[1];
		cp->hd.acc[2] += reaction[2];

		cp->hd.aacc[0] += FIXEDH(deepestLever[1] * reaction[2] - deepestLever[2] * reaction[1]);
		cp->hd.aacc[1] += FIXEDH(deepestLever[2] * reaction[0] - deepestLever[0] * reaction[2]);
		cp->hd.aacc[2] += FIXEDH(deepestLever[0] * reaction[1] - deepestLever[1] * reaction[0]);

		//if (lift != 0)
		{
			lever[0] = FIXEDH(lift * deepestNormal[0]);
			lever[1] = FIXEDH(lift * deepestNormal[1]);
			lever[2] = FIXEDH(lift * deepestNormal[2]);

			cp->hd.where.t[0] += lever[0];
			cp->hd.where.t[1] += lever[1];
			cp->hd.where.t[2] += lever[2];

			cp->st.n.fposition[0] += lever[0] * 16;
			cp->st.n.fposition[1] += lever[1] * 16;
			cp->st.n.fposition[2] += lever[2] * 16;

			gte_SetTransMatrix(&cp->hd.where);

			_cl.extraangulardamping = 1;

			if (lift > 120)
				cp->st.n.linearVelocity[1] = 0;
		}
	}

	if (flying)
		FlyingCarWheels(cp);
	else
		AddWheelForcesDriver1(cp, &_cl);

	ConvertTorqueToAngularAcceleration(cp, &_cl);

	if (flying)
	{
		cp->hd.aacc[0] = 0;
		cp->hd.aacc[1] = 0;
		cp->hd.aacc[2] = 0;
	}

	cp->hd.mayBeColliding = 0;
#ifndef PSX
	memset(cp->hd.mayBeCollidingWith, 0, sizeof(cp->hd.mayBeCollidingWith));
#endif
}